// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <media/cec-notifier.h>
#include <drm/drm_connector.h>
#include "hdmi_cec.h"
#include "hdmi_debug.h"
#include "hdmi_regs.h"

#define hdmi_read(x, off) \
	readl_relaxed((x)->hdmi_cec.io_data->io.base + (off))

#define hdmi_write(x, off, value) \
	writel_relaxed(value, (x)->hdmi_cec.io_data->io.base + (off))


struct hdmi_cec_private {
	struct hdmi_cec hdmi_cec;
	struct cec_adapter *adap;
	struct platform_device *pdev;
	struct cec_notifier *notifier;
	struct workqueue_struct *wq;
	struct work_struct cec_rx_work;
	struct work_struct cec_tx_work;
	struct work_struct cec_tx_error_work;
	u8 cec_log_addr;
	u32 cec_reg_status;
};

static int hdmi_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	u32 hdmi_hw_version, reg_val;
	struct hdmi_cec_private *cec;

	if (!adap) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	cec = cec_get_drvdata(adap);
	if (!cec) {
		HDMI_ERR("failed to get cec private data\n");
		return -EINVAL;
	}

	HDMI_DEBUG("adap enable %d\n", enable);

	if (enable) {
		pm_runtime_get_sync(&cec->pdev->dev);

		/* 19.2Mhz * 0.00005 us = 950 = 0x3B6 */
		hdmi_write(cec, HDMI_CEC_REFTIMER, (0x3B6 & 0xFFF) | BIT(16));

		hdmi_hw_version = hdmi_read(cec, HDMI_VERSION);
		if (hdmi_hw_version >= CEC_SUPPORTED_HW_VERSION) {
			hdmi_write(cec, HDMI_CEC_RD_RANGE, 0x30AB9888);
			hdmi_write(cec, HDMI_CEC_WR_RANGE, 0x888AA888);

			hdmi_write(cec, HDMI_CEC_RD_START_RANGE, 0x88888888);
			hdmi_write(cec, HDMI_CEC_RD_TOTAL_RANGE, 0x99);
			hdmi_write(cec, HDMI_CEC_COMPL_CTL, 0xF);
			hdmi_write(cec, HDMI_CEC_WR_CHECK_CONFIG, 0x4);
		} else {
			HDMI_ERR("CEC version %d is not supported.\n",
				hdmi_hw_version);
			pm_runtime_put_sync(&cec->pdev->dev);
			return -EPERM;
		}

		hdmi_write(cec, HDMI_CEC_RD_FILTER, BIT(0) | (0x7FF << 4));
		hdmi_write(cec, HDMI_CEC_TIME, BIT(0) | ((7 * 0x30) << 7));

		/* Enable CEC interrupts */
		hdmi_write(cec, HDMI_CEC_INT, CEC_INTR_MASK);

		/* Enable Engine */
		hdmi_write(cec, HDMI_CEC_CTRL, BIT(0));
	} else {
		/* Disable Engine */
		hdmi_write(cec, HDMI_CEC_CTRL, 0);

		/* Disable CEC interrupts */
		reg_val = hdmi_read(cec, HDMI_CEC_INT);
		hdmi_write(cec, HDMI_CEC_INT, reg_val & ~CEC_INTR_MASK);

		pm_runtime_put_sync(&cec->pdev->dev);
	}

	return 0;
}

static int hdmi_cec_adap_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	struct hdmi_cec_private *cec;

	if (!adap) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	cec = cec_get_drvdata(adap);
	if (!cec) {
		HDMI_ERR("failed to get cec private data\n");
		return -EINVAL;
	}

	cec->cec_log_addr = logical_addr;
	if (logical_addr != CEC_LOG_ADDR_INVALID) {
		HDMI_DEBUG("set log addr %d\n", logical_addr);
		hdmi_write(cec, HDMI_CEC_ADDR, logical_addr & 0xF);
	}

	return 0;
}

static int hdmi_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				  u32 signal_free_time, struct cec_msg *msg)
{

	struct hdmi_cec_private *cec;
	u32 frame_type;
	u8 retransmits;
	int i;

	if (!adap) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	cec = cec_get_drvdata(adap);
	if (!cec) {
		HDMI_ERR("failed to get cec private data\n");
		return -EINVAL;
	}

	HDMI_DEBUG("transmit msg [%d]->[%d]: len = %d, attempts=%d, signal_free_time=%d\n",
		cec_msg_initiator(msg), cec_msg_destination(msg), msg->len,
		attempts, signal_free_time);

	/* toggle cec in order to flush out bad hw state, if any */
	hdmi_write(cec, HDMI_CEC_CTRL, 0);
	hdmi_write(cec, HDMI_CEC_CTRL, BIT(0));

	retransmits = attempts ? (attempts - 1) : 0;

	hdmi_write(cec, HDMI_CEC_RETRANSMIT, (retransmits << 4) | BIT(0));

	frame_type = cec_msg_is_broadcast(msg) ? BIT(0) : 0;

	for (i = 0; i < msg->len; i++)
		hdmi_write(cec, HDMI_CEC_WR_DATA,
			(msg->msg[i] << 8) | frame_type);

	/* check line status */
	if (hdmi_read(cec, HDMI_CEC_STATUS) & BIT(0)) {
		HDMI_ERR("CEC line is busy\n");
		return -EBUSY;
	}

	/* start transmission */
	hdmi_write(cec, HDMI_CEC_CTRL, BIT(0) | BIT(1) |
		((msg->len & 0x1F) << 4) | BIT(9));

	return 0;
}

static void hdmi_cec_handle_rx_done(struct work_struct *work)
{
	struct hdmi_cec_private *cec =
		container_of(work, struct hdmi_cec_private, cec_rx_work);
	struct cec_msg msg = {};
	u32 data;
	int i;

	HDMI_DEBUG("rx done\n");

	data = hdmi_read(cec, HDMI_CEC_RD_DATA);
	msg.len = (data & 0x1F00) >> 8;
	if (msg.len < 1 || msg.len > CEC_MAX_MSG_SIZE) {
		HDMI_ERR("invalid message size %d\n", msg.len);
		return;
	}

	msg.msg[0] = data & 0xFF;

	for (i = 1; i < msg.len; i++)
		msg.msg[i] = hdmi_read(cec, HDMI_CEC_RD_DATA) & 0xFF;

	cec_received_msg(cec->adap, &msg);
}

static void hdmi_cec_handle_tx_done(struct work_struct *work)
{
	struct hdmi_cec_private *cec =
		container_of(work, struct hdmi_cec_private, cec_tx_work);

	HDMI_DEBUG("tx done\n");
	cec_transmit_done(cec->adap, CEC_TX_STATUS_OK, 0, 0, 0, 0);
}

static void hdmi_cec_handle_tx_error(struct work_struct *work)
{
	struct hdmi_cec_private *cec =
		container_of(work, struct hdmi_cec_private, cec_tx_error_work);
	u32 cec_status;

	cec_status = READ_ONCE(cec->cec_reg_status);

	HDMI_DEBUG("tx error status %x\n", cec_status);

	/*
	 * Note: The below handling for cec_status has been altered.
	 * For cec_status value 0x30 it was CEC_TX_STATUS_ARB_LOST and
	 * for cec_status value 0x10 it was CEC_TX_STATUS_NACK.
	 */
	if ((cec_status & 0xF0) == 0x30)
		cec_transmit_done(cec->adap,
			CEC_TX_STATUS_NACK, 0, 1, 0, 0);
	else if ((cec_status & 0xF0) == 0x10)
		cec_transmit_done(cec->adap,
			CEC_TX_STATUS_ARB_LOST, 1, 0, 0, 0);
	else
		cec_transmit_done(cec->adap,
			CEC_TX_STATUS_ERROR | CEC_TX_STATUS_MAX_RETRIES,
			0, 0, 0, 1);
}

static irqreturn_t hdmi_cec_irq_handler(void *hdmi_cec)
{
	struct hdmi_cec_private *cec = NULL;
	u32 data;
	u32 cec_status;
	u8 irq_status = 0;

	if (!hdmi_cec) {
		HDMI_ERR("invalid input\n");
		return IRQ_NONE;
	}

	cec = container_of(hdmi_cec,
			struct hdmi_cec_private, hdmi_cec);

	data = hdmi_read(cec, HDMI_CEC_INT);
	cec_status = hdmi_read(cec, HDMI_CEC_STATUS);

	WRITE_ONCE(cec->cec_reg_status, cec_status);

	hdmi_write(cec, HDMI_CEC_INT, data);

	HDMI_DEBUG("irq handler: %x | cec_status: %x\n", data, cec->cec_reg_status);

	if ((data & BIT(2)) && (data & BIT(3))) {
		irq_status |= CEC_IRQ_FRAME_ERROR;
		queue_work(cec->wq, &cec->cec_tx_error_work);
	} else if ((data & BIT(0)) && (data & BIT(1))) {
		irq_status |= CEC_IRQ_FRAME_WR_DONE;
		queue_work(cec->wq, &cec->cec_tx_work);
	}

	if ((data & BIT(6)) && (data & BIT(7))) {
		irq_status |= CEC_IRQ_FRAME_RD_DONE;
		queue_work(cec->wq, &cec->cec_rx_work);
	}

	return irq_status ? IRQ_HANDLED : IRQ_NONE;
}

static const struct cec_adap_ops hdmi_cec_adap_ops = {
	.adap_enable = hdmi_cec_adap_enable,
	.adap_log_addr = hdmi_cec_adap_log_addr,
	.adap_transmit = hdmi_cec_adap_transmit,
};

struct hdmi_cec *hdmi_cec_get(struct platform_device *pdev,
			struct hdmi_parser *parser)
{
	struct hdmi_cec_private *cec;
	struct device *dev = &pdev->dev;
	int ret;

	if (!pdev || !parser) {
		HDMI_ERR("invalid inputs\n");
		return ERR_PTR(-EINVAL);
	}

	cec = kzalloc(sizeof(*cec), GFP_KERNEL);
	if (!cec) {
		HDMI_ERR("Insufficient memory\n");
		return ERR_PTR(-ENOMEM);
	}

	cec->pdev = pdev;

	cec->hdmi_cec.io_data = parser->get_io(parser, "hdmi_ctrl");
	if (!cec->hdmi_cec.io_data) {
		HDMI_ERR("failed to get io data\n");
		kfree(cec);
		return ERR_PTR(-ENODEV);
	}

	cec->hdmi_cec.irq = of_irq_get(dev->of_node, 0);
	if (cec->hdmi_cec.irq < 0) {
		HDMI_ERR("failed to get irq\n");
		ret = cec->hdmi_cec.irq;
		kfree(cec);
		return ERR_PTR(ret);
	}

	cec->adap = cec_allocate_adapter(&hdmi_cec_adap_ops, cec,
			CEC_NAME,
			CEC_CAP_LOG_ADDRS | CEC_CAP_PASSTHROUGH |
			CEC_CAP_TRANSMIT, 1);
	ret = PTR_ERR_OR_ZERO(cec->adap);
	if (ret) {
		kfree(cec);
		return ERR_PTR(ret);
	}

	ret = cec_register_adapter(cec->adap, &cec->pdev->dev);
	if (ret)
		goto err_del_adap;

	pm_runtime_enable(dev);
	cec->notifier = cec_notifier_cec_adap_register(&cec->pdev->dev, NULL, cec->adap);
	if (!cec->notifier) {
		HDMI_ERR("failed to get cec notifier\n");
		pm_runtime_disable(dev);
		ret = -ENOMEM;
		goto err_unreg_adap;
	}

	HDMI_DEBUG("Probe done\n");

	cec->wq = alloc_ordered_workqueue("hdmi_cec_workqueue", 0);
	if (!cec->wq) {
		HDMI_ERR("failed to create workqueue\n");
		pm_runtime_disable(dev);
		ret = -ENOMEM;
		goto err_notifier;
	}

	INIT_WORK(&cec->cec_rx_work, hdmi_cec_handle_rx_done);
	INIT_WORK(&cec->cec_tx_work, hdmi_cec_handle_tx_done);
	INIT_WORK(&cec->cec_tx_error_work, hdmi_cec_handle_tx_error);

	cec->hdmi_cec.isr = hdmi_cec_irq_handler;

	return &cec->hdmi_cec;

err_notifier:
	cec_notifier_cec_adap_unregister(cec->notifier, cec->adap);
	cec_unregister_adapter(cec->adap);
	kfree(cec);
	return ERR_PTR(ret);
err_unreg_adap:
	cec_unregister_adapter(cec->adap);
	kfree(cec);
	return ERR_PTR(ret);
err_del_adap:
	cec_delete_adapter(cec->adap);
	kfree(cec);
	return ERR_PTR(ret);
}

void hdmi_cec_put(struct hdmi_cec *hdmi_cec)
{
	struct hdmi_cec_private *cec =  NULL;

	if (!hdmi_cec)
		return;

	cec = container_of(hdmi_cec, struct hdmi_cec_private, hdmi_cec);

	if (cec->wq) {
		cancel_work_sync(&cec->cec_rx_work);
		cancel_work_sync(&cec->cec_tx_work);
		cancel_work_sync(&cec->cec_tx_error_work);
		destroy_workqueue(cec->wq);
	}

	cec_notifier_cec_adap_unregister(cec->notifier, cec->adap);
	pm_runtime_disable(&cec->pdev->dev);
	cec_unregister_adapter(cec->adap);
	platform_set_drvdata(cec->pdev, NULL);
	kfree(cec);

	HDMI_DEBUG("De-init done\n");
}

int hdmi_cec_set_phys_addr_from_edid(struct hdmi_cec *cec, const struct edid *edid)
{
	struct hdmi_cec_private *priv =  NULL;

	if (!cec || !edid)
		return -EINVAL;

	priv = container_of(cec, struct hdmi_cec_private, hdmi_cec);

	if (!priv || !priv->notifier)
		return -ENODEV;

	cec_notifier_set_phys_addr_from_edid(priv->notifier, edid);
	return 0;
}

int hdmi_cec_invalidate_phys_addr(struct hdmi_cec *cec)
{
	struct hdmi_cec_private *priv = NULL;

	if (!cec)
		return -EINVAL;

	priv = container_of(cec, struct hdmi_cec_private, hdmi_cec);

	if (!priv->notifier)
		return -ENODEV;

	cec_notifier_set_phys_addr(priv->notifier, CEC_PHYS_ADDR_INVALID);
	return 0;
}
