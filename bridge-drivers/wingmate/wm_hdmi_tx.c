// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/of_irq.h>
#include <drm/drm_edid.h>
#include <drm/drm_file.h>

#include "wm_debug.h"
#include "wm_hdmi_tx.h"

#define spi_read8(reg) \
	hdmi->display->read_register(hdmi->display, (reg))

#define spi_write8(reg, data) \
	hdmi->display->update_register_bits(hdmi->display, (reg), \
			WM_HDMI_REG_MASK, (data))

enum wm_hdmi_phy_conf {
	PDZ = BIT(7),
	ENTMDS = BIT(6),
	SVSRET = BIT(5),
	PDDQ = BIT(4),
	TXPWRON = BIT(3),
	ENHPDRXSENSE = BIT(2),
	SELDATAENPOL = BIT(1),
	SELDIPIF = BIT(0),
};

enum wm_hdmi_phy {
	TXPHYLOCK = BIT(0),
	HPD = BIT(1),
	RXSENSE_0 = BIT(4),
	RXSENSE_1 = BIT(5),
	RXSENSE_2 = BIT(6),
	RXSENSE_3 = BIT(7),
};

enum wm_hdmi_ih_mute {
	IH_HPD = BIT(0),
	IH_TXPHYLOCK = BIT(1),
	IH_RXSENSE_0 = BIT(2),
	IH_RXSENSE_1 = BIT(3),
	IH_RXSENSE_2 = BIT(4),
	IH_RXSENSE_3 = BIT(5),
};

static void _wm_hdmi_tx_conf_phy(struct wm_hdmi_tx *hdmi, u8 bits)
{
	u8 data;

	data = spi_read8(WM_HDMI_PHY_CONF0);
	data |= bits;
	spi_write8(WM_HDMI_PHY_CONF0, data);
}

static void _wm_hdmi_tx_config_edid_param(struct wm_hdmi_tx *hdmi,
				u8 addr, u8 segptr, bool single_read)
{
	/* Optional Configuration,
	 * E-DDC bus clear.
	 */
	spi_write8(WM_HDMI_I2CM_OPERATION, BIT(5));

	/* Mandatory Register Configurations. */
	spi_write8(WM_HDMI_I2CM_SLAVE, 0xa0);
	spi_write8(WM_HDMI_I2CM_ADDRESS, addr);

	spi_write8(WM_HDMI_I2CM_SEGPTR, 0x00 + (segptr/2));

	if (segptr % 2)
		spi_write8(WM_HDMI_I2CM_SEGADDR, 0x80);
	else
		spi_write8(WM_HDMI_I2CM_SEGADDR, 0x00);

	if (single_read)
		spi_write8(WM_HDMI_I2CM_OPERATION, 0x02);
	else
		spi_write8(WM_HDMI_I2CM_OPERATION, 0x08);

}

static void _wm_hdmi_tx_enable_edid_irq(struct wm_hdmi_tx *hdmi, bool en)
{
	u8 val;

	val = spi_read8(WM_HDMI_IH_MUTE_I2CM_STAT0);
	if (en)
		/* mute DDC Read interrupt */
		spi_write8(WM_HDMI_IH_MUTE_I2CM_STAT0, val | 0x3);
	else
		/* Unmute DDC Read interrupt. */
		spi_write8(WM_HDMI_IH_MUTE_I2CM_STAT0, val & 0x4);
}

static inline void _wm_hdmi_tx_hpd_status(struct wm_hdmi_tx *hdmi, bool *hpd)
{
	u8 mask, data = 0;

	/* HPD and RX_SENSE_* status. */
	mask = 0xf2;

	data = hdmi->intr.phy_itr;

	if (!((data & mask) ^ mask))
		*hpd = true;
}

static inline void _wm_hdmi_tx_edid_i2c_intr(struct wm_hdmi_tx *hdmi, u8 *stat)
{
	/* read EDID interrupt status. */
	*stat = spi_read8(WM_HDMI_IH_I2CM_STAT0);
	/* clear E-EDID interrupt. */
	spi_write8(WM_HDMI_IH_I2CM_STAT0, *stat);
}

static void _wm_hdmi_tx_mute_phy(struct wm_hdmi_tx *hdmi, bool mute, u8 bits)
{
	u8 data = 0;

	data = spi_read8(WM_HDMI_IH_MUTE_PHY_STAT0);

	if (mute)
		data |= bits;
	else
		data &= ~bits;

	spi_write8(WM_HDMI_IH_MUTE_PHY_STAT0, data);
}

static void _wm_hdmi_tx_mask_phy(struct wm_hdmi_tx *hdmi, bool mask, u8 bits)
{
	u8 data = 0;

	data = spi_read8(WM_HDMI_PHY_MASK0);

	if (mask)
		data |= bits;
	else
		data &= ~bits;

	spi_write8(WM_HDMI_PHY_MASK0, data);
}

static void _wm_hdmi_tx_toggle_pol(struct wm_hdmi_tx *hdmi, bool pol, u8 bits)
{
	u8 data = 0;

	data = spi_read8(WM_HDMI_PHY_POL0);

	if (pol)
		data &= ~bits;
	else
		data |= bits;

	spi_write8(WM_HDMI_PHY_POL0, data);
}

static inline void _wm_hdmi_tx_config_hpd(struct wm_hdmi_tx *hdmi)
{
	_wm_hdmi_tx_mask_phy(hdmi, false, HPD);

	_wm_hdmi_tx_toggle_pol(hdmi, false, HPD);

	_wm_hdmi_tx_mute_phy(hdmi, false, IH_HPD);

	_wm_hdmi_tx_conf_phy(hdmi, ENHPDRXSENSE);

	WM_INFO("[OK]");
}

static void _wm_hdmi_tx_edid_fetch_bytes(struct wm_hdmi_tx *hdmi, u8 *edid,
		u8 addr, bool single_read)
{
	int i;

	if (!single_read) {
		for (i = 0; i < 8; i++)
			edid[addr + i] = spi_read8(WM_HDMI_I2CM_READ_BUFFx + i);
	} else
		edid[addr] = spi_read8(WM_HDMI_I2CM_DATAI);
}

static void _wm_hdmi_tx_send_hpd_event(struct wm_hdmi_tx *hdmi, bool state)
{
	char name[32], status[32];
	char *envp[5];
	char *event_string = "HOTPLUG=1";
	struct drm_device *dev = NULL;
	enum drm_connector_status c_status;

	c_status = state ? connector_status_connected :
		connector_status_disconnected;

	dev = hdmi->display->connector->dev;

	scnprintf(name, 32, "name=%s", hdmi->display->connector->name);
	scnprintf(status, 32, "status=%s",
		drm_get_connector_status_name(c_status));

	WM_DEBUG("[%s]:[%s]", name, status);

	envp[0] = name;
	envp[1] = status;
	envp[2] = event_string;
	envp[3] = NULL;
	envp[4] = NULL;
	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);

	WM_DEBUG("notify hpd %s UEvent", state ? "connect" : "disconnect");
}

static inline void _wm_hdmi_tx_reset_edid(struct wm_hdmi_tx *hdmi)
{
	memset(&hdmi->edid_ctrl, 0, sizeof(hdmi->edid_ctrl));
}

static int _wm_hdmi_tx_process_hpd_low(struct wm_hdmi_tx *hdmi)
{
	/* Clear and reset all the memory/values set during
	 * HPD HIGH. This will prevent conflict at the time of
	 * next HPD HIGH event.
	 */

	if (hdmi->edid) {
		kfree(hdmi->edid);
		hdmi->edid = NULL;
		_wm_hdmi_tx_reset_edid(hdmi);
	}

	_wm_hdmi_tx_send_hpd_event(hdmi, false);

	/* Disable HDMI Tx functionalities. */
	return 0;
}

static inline void _wm_hdmi_tx_process_hpd_high(struct wm_hdmi_tx *hdmi)
{

	_wm_hdmi_tx_send_hpd_event(hdmi, true);
	/* Parse extracted E-EDID data to extract discrete information,
	 * such as Audio, Video, HDR, etc.
	 */
}

static int _wm_hdmi_tx_edid_buff_alloc(struct wm_hdmi_tx *hdmi)
{
	u8 *new;
	int num_ext = 0;

	if (hdmi->edid)
		num_ext = hdmi->edid[0x7e];

	new = krealloc(hdmi->edid,
			(num_ext + 1) * EDID_LENGTH,
			GFP_KERNEL);

	if (new)
		hdmi->edid = new;

	return num_ext;
}

static int _wm_hdmi_tx_read_edid(struct wm_hdmi_tx *hdmi, int *done)
{
	u8 stat, *edid;
	u8 *retry = NULL;
	int ret = 0;
	u32 *t_ext = NULL, *c_ext = NULL, *count = NULL;

	t_ext = &hdmi->edid_ctrl.total_ext;
	c_ext = &hdmi->edid_ctrl.curr_ext;
	count = &hdmi->edid_ctrl.count;
	retry = &hdmi->edid_ctrl.retry;

	edid = hdmi->edid;
	stat = hdmi->intr.edid_itr;

	/* Check E-DDC interrupt status.
	 * 1. if success, fetch the next bytes,
	 * 2. otherwise, reduce the count and retry the same
	 *    fetch.
	 */
	if (stat & BIT(1)) {
		_wm_hdmi_tx_edid_fetch_bytes(hdmi, edid, *count,
				hdmi->dt_props->edid_single_read);
		*retry = 5;

		if (hdmi->dt_props->edid_single_read)
			(*count)++;
		else
			*count += 8;
	} else {
		if (*retry > 0) {
			(*retry)--;
			WM_DEBUG("EDID read attempt %d failed", 5 - *retry);
		} else {
			WM_ERR("EDID read limit exceeded");
			ret = -ETIMEDOUT;
			goto error;
		}
	}

	/* Check if Base E-EDID Block fetch is complete,
	 * if complete, parse the number of extension blocks
	 * in this E-EDID.
	 *
	 * This condition will only be true on successful fetch
	 * of initial 128 bytes of data.
	 */
	if (*count == EDID_LENGTH) {
		if (drm_edid_block_valid(edid, *c_ext, false, NULL))
			*t_ext = _wm_hdmi_tx_edid_buff_alloc(hdmi);
		else {
			WM_ERR("invalid EDID");
			ret = -EINVAL;
			goto error;
		}
	}

	if (!(*count % EDID_LENGTH) && *count) {
		if (drm_edid_block_valid(edid, *c_ext, false, NULL))
			(*c_ext)++;
		else {
			WM_ERR("invalid EDID Extension");
			ret = -EINVAL;
			goto error;
		}
	}

	if (*c_ext == (*t_ext + 1)) {
		*done = true;
		WM_DEBUG("EDID read complete");
		print_hex_dump_debug("[HDMI EDID]: ", DUMP_PREFIX_NONE,
				16, 16, edid, *count, false);

		return 0;
	}

	_wm_hdmi_tx_config_edid_param(hdmi, *count % EDID_LENGTH, *c_ext,
				hdmi->dt_props->edid_single_read);

	return 0;

error:
	kfree(edid);
	hdmi->edid = NULL;
	WM_ERR("Terminating EDID fetch");

	return ret;
}

static void wm_hdmi_tx_read_sink_edid(struct work_struct *work)
{
	struct wm_hdmi_tx *hdmi;
	int ret = 0, complete = 0;

	hdmi = container_of(work, struct wm_hdmi_tx, edid_work);
	if (!hdmi) {
		WM_ERR("invalid input");
		return;
	}

	mutex_lock(&hdmi->lock);
	if((ret = _wm_hdmi_tx_read_edid(hdmi, &complete))) {
		/* Todo: add further conditions
		 * in case of EDID read failure.
		 */
		WM_ERR("EDID Read failed: %d", ret);
	}

	if (complete) {
		_wm_hdmi_tx_enable_edid_irq(hdmi, true);
		_wm_hdmi_tx_process_hpd_high(hdmi);
	}
	mutex_unlock(&hdmi->lock);
}

static int _wm_hdmi_tx_init_read_sink_edid(struct wm_hdmi_tx *hdmi)
{
	int blk = 0;

	if (!hdmi->edid) {
		goto reserve;
	} else {
		/* failsafe to parse fresh E-EDID on each HPD High. */
		kfree(hdmi->edid);
		hdmi->edid = NULL;
	}

reserve:
	blk = _wm_hdmi_tx_edid_buff_alloc(hdmi);
	if (!hdmi->edid) {
		WM_ERR("EDID Memory alloc failed");
		return -ENOMEM;
	}
	memset(hdmi->edid, 0, (blk + 1) * EDID_LENGTH * sizeof(*hdmi->edid));

	/* Enable interrupt mask and
	 * clear any previous edid read recordings.
	 */
	_wm_hdmi_tx_enable_edid_irq(hdmi, false);
	_wm_hdmi_tx_reset_edid(hdmi);

	/* Initiate fresh E-EDID parsing, therefore,
	 * address and segptr shall be 0.
	 */
	_wm_hdmi_tx_config_edid_param(hdmi, 0, 0,
			hdmi->dt_props->edid_single_read);

	return 0;
}

/* Reads HPD interrupt register to identify HPD state
 * and take respective action for high and low.
 */
static void wm_hdmi_tx_detect_hpd(struct work_struct *work)
{
	struct wm_hdmi_tx *hdmi;
	bool hpd = false;
	int ret = 0;

	hdmi = container_of(work, struct wm_hdmi_tx, hpd_work);
	if (!hdmi) {
		WM_ERR("invalid input");
		return;
	}

	/* On successful reception of HPD Interrupt,
	 * further execution should be followed in the below order
	 * 1. Sense HPD State based on set polarity.
	 * 2. Reverse current HPD Detection Polarity
	 * 3. Clear the interrupt
	 * 4. Process HPD Event
	 * 	a. Read E-EDID in case of HPD HIGH
	 * 	   i. Send UEvent on successful E-EDID read.
	 * 	b. Clean HDMI Source Configurations
	 * 4. Notify other sub-modules about the HPD (Sink's) status.
	 */

	mutex_lock(&hdmi->lock);

	/* HPD status. */
	_wm_hdmi_tx_hpd_status(hdmi, &hpd);

	/* Reverse HPD detection polarity. */
	_wm_hdmi_tx_toggle_pol(hdmi, hpd, HPD);

	/* Process HPD Event. */
	if (hpd)
		ret = _wm_hdmi_tx_init_read_sink_edid(hdmi);
	else
		ret = _wm_hdmi_tx_process_hpd_low(hdmi);

	if (ret) {
		WM_ERR("hpd %s process fail: %d", hpd ? "high" : "low",	ret);
		goto fail;
	}

	hdmi->display->hpd_status = hpd;

	mutex_unlock(&hdmi->lock);
	return;
fail:
	/* Current HPD processing failed,
	 * revert HPD detection polarity
	 * to previous state.
	 *
	 * This will permit processing of further similar
	 * HPD Event which got failed previously.
	 */
	_wm_hdmi_tx_toggle_pol(hdmi, !hpd, HPD);

	mutex_unlock(&hdmi->lock);
	return;
}

static int wm_hdmi_tx_irq_handler(struct wm_hdmi_tx *hdmi, int irq)
{
	if (!hdmi) {
		WM_ERR("invalid input");
		return IRQ_NONE;
	}
	/* TODO: add irq handling. */
	return IRQ_HANDLED;
}

static int _wm_hdmi_tx_create_workqueue(struct wm_hdmi_tx *hdmi)
{
	hdmi->workq = create_singlethread_workqueue("wm_hdmi_workqueue");
	if (IS_ERR_OR_NULL(hdmi->workq)) {
		WM_ERR("error creating workqueue");
		return -EPERM;
	}

	INIT_WORK(&hdmi->hpd_work, wm_hdmi_tx_detect_hpd);
//	INIT_WORK(&hdmi->phy_work, wm_hdmi_tx_phy_config);
	INIT_WORK(&hdmi->edid_work, wm_hdmi_tx_read_sink_edid);
	return 0;
}

struct wm_hdmi_tx *wm_hdmi_tx_init(struct wm_display_info *info)
{
	struct wm_hdmi_tx *hdmi_tx;
	int ret = 0;

	if (!info || !info->dev || !info->dt_props || !info->display) {
		WM_ERR("invalid arguments");
		ret = -EINVAL;
		goto error;
	}
	hdmi_tx = devm_kzalloc(info->dev, sizeof(*hdmi_tx), GFP_KERNEL);
	if (!hdmi_tx) {
		ret = -ENOMEM;
		goto error;
	}

	hdmi_tx->display = info->display;
	hdmi_tx->dt_props = info->dt_props;
	hdmi_tx->dev = info->dev;

	/* assign function pointers. */
	hdmi_tx->pre_enable = NULL;
	hdmi_tx->enable = NULL;
	hdmi_tx->disable = NULL;
	hdmi_tx->post_disable = NULL;
	hdmi_tx->get_modes = NULL;
	hdmi_tx->mode_valid = NULL;
	hdmi_tx->irq_handler = wm_hdmi_tx_irq_handler;

	mutex_init(&hdmi_tx->lock);

	/* initialize workqueues. */
	ret = _wm_hdmi_tx_create_workqueue(hdmi_tx);
	if (ret)
		goto fail;

	_wm_hdmi_tx_config_hpd(hdmi_tx);

	return hdmi_tx;
fail:
	mutex_destroy(&hdmi_tx->lock);
error:
	WM_ERR("HDMI TX initialization failed with error: %d", ret);
	return ERR_PTR(ret);
}

void wm_hdmi_tx_deinit(struct wm_hdmi_tx *hdmi_tx)
{
	if (!hdmi_tx) {
		WM_DEBUG("hdmi allocation failed - deinit");
		return;
	}

	if (hdmi_tx->workq) {
		cancel_work_sync(&hdmi_tx->hpd_work);
		cancel_work_sync(&hdmi_tx->edid_work);
		destroy_workqueue(hdmi_tx->workq);
	}

	mutex_destroy(&hdmi_tx->lock);


	WM_DEBUG("hdmi deinit");
}
