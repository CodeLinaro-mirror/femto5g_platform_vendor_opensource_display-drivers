// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <linux/kthread.h>
#include <linux/msm_hdcp.h>
#include "sde_hdcp_2x.h"
#include <linux/kfifo.h>
#include "hdmi_util.h"
#include "hdmi_regs.h"
#include "hdmi_debug.h"
/*
 * Defined addresses and offsets of standard HDCP 2.2 sink registers
 * for DDC, as defined in HDCP 2.2 spec section 2.14 table 2.7
 */
#define HDCP_SINK_DDC_SLAVE_ADDR 0x74            /* Sink DDC slave address */
#define HDCP_SINK_DDC_HDCP2_VERSION 0x50         /* Does sink support HDCP2.2 */
#define HDCP_SINK_DDC_HDCP2_WRITE_MESSAGE 0x60   /* HDCP Tx writes here */
#define HDCP_SINK_DDC_HDCP2_RXSTATUS 0x70        /* RxStatus, 2 bytes */
#define HDCP_SINK_DDC_HDCP2_READ_MESSAGE 0x80    /* HDCP Tx reads here */

#define HDCP2P2_DEFAULT_TIMEOUT 500

/*
 * HDCP 2.2 encryption requires the data encryption block that is present in
 * HDMI controller version 4.0.0 and above
 */
#define MIN_HDMI_TX_MAJOR_VERSION 4

#define hdmi_read(x, off)  \
	readl_relaxed((x)->io_data->io.base + (off))

#define hdmi_write(x, off, value) \
	writel_relaxed(value, (x)->io_data->io.base + (off))

enum hdmi_hdcp2p2_sink_status {
	SINK_DISCONNECTED,
	SINK_CONNECTED
};

struct hdmi_hdcp2p2_ctrl {
	DECLARE_KFIFO(cmd_q, enum hdcp_transport_wakeup_cmd, 8);
	wait_queue_head_t wait_q;
	atomic_t auth_state;
	atomic_t abort;
	enum hdmi_hdcp2p2_sink_status sink_status; /* Is sink connected */
	struct sde_hdcp_init_data init_data;
	struct mutex mutex; /* mutex to protect access to hdcp2 */
	struct mutex msg_lock; /* mutex to protect access to msg buffer */
	struct sde_hdcp_ops *ops;
	void *lib_ctx; /* Handle to HDCP 2.2 Trustzone library */
	struct sde_hdcp_2x_ops *lib; /* Ops for driver to call into TZ */

	struct task_struct *thread;
	struct hdcp2_buffer response;
	struct hdcp2_buffer request;
	uint32_t total_message_length;
	uint32_t transaction_delay;
	uint32_t transaction_timeout;
	uint32_t timeout;
	uint32_t timeout_left;
	struct sde_hdcp_2x_msg_part msg_part[HDCP_MAX_MESSAGE_PARTS];
	u8 sink_rx_status;
	u8 rx_status;
	char abort_mask;
	struct work_struct link_work;
	bool polling;
};


int hdmi_ddc_hdcp2p2_isr(void *input)
{
	struct hdmi_hdcp2p2_ctrl *hdcp2 = input;
	struct hdmi_ddc *ddc = hdcp2->init_data.ddc;
	struct hdmi_tx_hdcp2p2_ddc_data *data;
	u32 intr0, intr2, intr5;
	u32 msg_size;
	int rc = 0;

	if (!ddc) {
		pr_err("invalid input\n");
		return -EINVAL;
	}

	data = &ddc->ddc_ctrl.hdcp2p2_ddc_data;

	intr0 = hdmi_read(ddc, HDMI_DDC_INT_CTRL0);
	intr2 = hdmi_read(ddc, HDMI_HDCP_INT_CTRL2);
	intr5 = hdmi_read(ddc, HDMI_DDC_INT_CTRL5);

	HDMI_DEBUG("intr0: 0x%x, intr2: 0x%x, intr5: 0x%x\n",
			 intr0, intr2, intr5);

	/* check if encryption is enabled */
	if (intr2 & BIT(0)) {
		/*
		 * ack encryption ready interrupt.
		 * disable encryption ready interrupt.
		 * enable encryption not ready interrupt.
		 */
		intr2 &= ~BIT(2);
		intr2 |= BIT(1) | BIT(6);

		HDMI_INFO("HDCP 2.2 Encryption enabled\n");
		data->encryption_ready = true;
	}

	/* check if encryption is disabled */
	if (intr2 & BIT(4)) {
		/*
		 * ack encryption not ready interrupt.
		 * disable encryption not ready interrupt.
		 * enable encryption ready interrupt.
		 */
		intr2  &= ~BIT(6);
		intr2  |= BIT(5) | BIT(2);

		HDMI_INFO("HDCP 2.2 Encryption disabled\n");
		data->encryption_ready = false;
	}

	hdmi_write(ddc, HDMI_HDCP_INT_CTRL2, intr2);

	/* get the message size bits 29:20 */
	msg_size = (intr0 & (0x3FF << 20)) >> 20;

	if (msg_size) {
		/* ack and disable message size interrupt */
		intr0 |= BIT(30);
		intr0 &= ~BIT(31);

		data->message_size = msg_size;
	}

	/* check and disable ready interrupt */
	if (intr0 & BIT(16)) {
	/* ack ready/not ready interrupt */
		intr0 |= BIT(17);
		intr0 &= ~BIT(18);
		HDMI_DEBUG("got ready interrupt\n");
		data->ready = true;
	}

	/* check for reauth req interrupt */
	if (intr0 & BIT(12)) {
		/* ack and disable reauth req interrupt */
		intr0 |= BIT(13);
		intr0 &= ~BIT(14);
		HDMI_DEBUG("got reauth interrupt\n");
		data->reauth_req = true;
	}

	/* check for ddc fail interrupt */
	if (intr0 & BIT(8)) {
		/* ack ddc fail interrupt */
		intr0 |= BIT(9);
		HDMI_ERR("got ddc fail interrupt\n");
		data->ddc_max_retries_fail = true;
	}

	/* check for ddc done interrupt */
	if (intr0 & BIT(4)) {
		/* ack ddc done interrupt */
		intr0 |= BIT(5);
		HDMI_DEBUG("got ddc done interrupt\n");
		data->ddc_done = true;
	}

	/* check for ddc read req interrupt */
	if (intr0 & BIT(0)) {
		/* ack read req interrupt */
		intr0 |= BIT(1);

		data->ddc_read_req = true;
	}

	hdmi_write(ddc, HDMI_DDC_INT_CTRL0, intr0);

	if (intr5 & BIT(0)) {
		HDMI_ERR("RXSTATUS_DDC_REQ_TIMEOUT\n");

		/* ack and disable timeout interrupt */
		intr5 |= BIT(1);
		intr5 &= ~BIT(2);

		data->ddc_timeout = true;
	}
	hdmi_write(ddc, HDMI_DDC_INT_CTRL5, intr5);

	if (data->message_size || data->ready || data->reauth_req) {
		if (data->wait) {
			complete(&ddc->ddc_ctrl.rx_status_done);
		} else if (data->link_cb && data->link_data) {
			data->link_cb(data->link_data);
		} else {
			HDMI_ERR("new msg/reauth not handled\n");
			rc = -EINVAL;
		}
	}

	hdmi_hdcp2p2_ddc_clear_status(ddc);

	return rc;
}


static inline int hdmi_hdcp2p2_valid_handle(struct hdmi_hdcp2p2_ctrl *hdcp2)
{
	if (!hdcp2) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	if (!hdcp2->lib_ctx) {
		HDMI_ERR("HDCP library needs to be acquired\n");
		return -EINVAL;
	}

	if (!hdcp2->lib) {
		HDMI_ERR("invalid lib ops data\n");
		return -EINVAL;
	}
	return 0;
}

static inline bool hdmi_hdcp2p2_is_valid_state(struct hdmi_hdcp2p2_ctrl *hdcp2)
{
	enum hdcp_transport_wakeup_cmd cmd;

	if (kfifo_peek(&hdcp2->cmd_q, &cmd) &&
			cmd == HDCP_TRANSPORT_CMD_AUTHENTICATE)
		return true;

	if (atomic_read(&hdcp2->auth_state) != HDCP_STATE_INACTIVE)
		return true;

	return false;
}

static int hdmi_hdcp2p2_copy_buf(struct hdmi_hdcp2p2_ctrl *hdcp2,
	struct hdcp_transport_wakeup_data *data)
{
	int i = 0;
	uint32_t num_messages = 0;

	if (!data || !data->message_data)
		return 0;

	mutex_lock(&hdcp2->msg_lock);

	num_messages = data->message_data->num_messages;
	hdcp2->total_message_length = 0; /* Total length of all messages */

	for (i = 0; i < num_messages; i++)
		hdcp2->total_message_length +=
			data->message_data->messages[i].length;

	memcpy(hdcp2->msg_part, data->message_data->messages,
		sizeof(data->message_data->messages));

	hdcp2->rx_status = data->message_data->rx_status;
	hdcp2->abort_mask = data->abort_mask;

	if (!hdcp2->total_message_length) {
		mutex_unlock(&hdcp2->msg_lock);
		return 0;
	}

	hdcp2->response.data = data->buf;
	hdcp2->response.length = hdcp2->total_message_length;
	hdcp2->response.length += 1;
	hdcp2->request.data = data->buf;
	hdcp2->request.length = hdcp2->total_message_length;

	hdcp2->transaction_delay = data->transaction_delay;
	hdcp2->transaction_timeout = data->transaction_timeout;

	mutex_unlock(&hdcp2->msg_lock);

	return 0;
}

static void hdmi_hdcp2p2_send_auth_status(struct hdmi_hdcp2p2_ctrl *hdcp2)
{
	hdcp2->init_data.notify_status(hdcp2->init_data.cb_data,
		atomic_read(&hdcp2->auth_state));
}

static int hdmi_hdcp2p2_wakeup(struct hdcp_transport_wakeup_data *data)
{
	struct hdmi_hdcp2p2_ctrl *hdcp2;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY);
	if (!data) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdcp2 = data->context;
	if (!hdcp2) {
		HDMI_ERR("invalid hdcp2\n");
		return -EINVAL;
	}

	if (data->transaction_timeout)
		hdcp2->timeout = data->transaction_timeout * 2;
	else
		hdcp2->timeout = HDCP2P2_DEFAULT_TIMEOUT;

	if (hdmi_hdcp2p2_copy_buf(hdcp2, data))
		goto exit;

	hdcp2->polling = false;
	switch (data->cmd) {
	case HDCP_TRANSPORT_CMD_STATUS_SUCCESS:
		atomic_set(&hdcp2->auth_state, HDCP_STATE_AUTHENTICATED);
		kfifo_put(&hdcp2->cmd_q, data->cmd);
		wake_up(&hdcp2->wait_q);
		break;
	case HDCP_TRANSPORT_CMD_STATUS_FAILED:
		atomic_set(&hdcp2->auth_state, HDCP_STATE_AUTH_FAIL);
		kfifo_put(&hdcp2->cmd_q, data->cmd);
		kthread_park(hdcp2->thread);
		break;
	default:
		kfifo_put(&hdcp2->cmd_q, data->cmd);
		wake_up(&hdcp2->wait_q);
		break;
	}

exit:
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, data->cmd);
	return 0;
}

static inline void hdmi_hdcp2p2_wakeup_lib(struct hdmi_hdcp2p2_ctrl *hdcp2,
	struct sde_hdcp_2x_wakeup_data *data)
{
	int rc = 0;

	if (hdcp2 && hdcp2->lib && hdcp2->lib->wakeup &&
		data && (data->cmd != HDCP_2X_CMD_INVALID)) {
		rc = hdcp2->lib->wakeup(data);
		if (rc)
			HDMI_ERR("error sending %s to lib\n",
				sde_hdcp_2x_cmd_to_str(data->cmd));
	}
}

static void hdmi_hdcp2p2_reset(struct hdmi_hdcp2p2_ctrl *hdcp2)
{
	if (!hdcp2) {
		HDMI_ERR("invalid input\n");
		return;
	}

	hdcp2->sink_status = SINK_DISCONNECTED;
	atomic_set(&hdcp2->auth_state, HDCP_STATE_INACTIVE);
	hdmi_hdcp2p2_send_auth_status(hdcp2);
}

static void hdmi_hdcp2p2_auth_failed(struct hdmi_hdcp2p2_ctrl *hdcp2)
{
	if (!hdcp2) {
		HDMI_ERR("invalid input\n");
		return;
	}

	atomic_set(&hdcp2->auth_state, HDCP_STATE_AUTH_FAIL);

	hdmi_hdcp2p2_ddc_disable(hdcp2->init_data.ddc);

	/* notify hdmi tx about HDCP failure */
	hdcp2->init_data.notify_status(hdcp2->init_data.cb_data,
		HDCP_STATE_AUTH_FAIL);
}

static int hdmi_hdcp2p2_ddc_rd_message(struct hdmi_hdcp2p2_ctrl *hdcp2,
	u8 *buf, int size, u32 timeout)
{
	struct hdmi_tx_ddc_data *ddc_data;
	struct hdmi_tx_ddc_ctrl *ddc_ctrl;

	int rc;

	if (!hdcp2) {
		HDMI_ERR("invalid hdcp2\n");
		return -EINVAL;
	}

	ddc_ctrl = &((struct hdmi_ddc *)hdcp2->init_data.ddc)->ddc_ctrl;
	ddc_data = &ddc_ctrl->ddc_data;

	if (!ddc_data) {
		HDMI_ERR("invalid ddc data\n");
		return -EINVAL;
	}

	if (atomic_read(&hdcp2->auth_state) == HDCP_STATE_INACTIVE) {
		HDMI_ERR("hdcp is off\n");
		return -EINVAL;
	}

	memset(ddc_data, 0, sizeof(*ddc_data));
	ddc_data->dev_addr = HDCP_SINK_DDC_SLAVE_ADDR;
	ddc_data->offset = HDCP_SINK_DDC_HDCP2_READ_MESSAGE;
	ddc_data->data_buf = buf;
	ddc_data->data_len = size;
	ddc_data->request_len = size;
	ddc_data->retry = 0;
	ddc_data->hard_timeout = timeout;
	ddc_data->what = "HDCP2ReadMessage";

	rc = hdmi_ddc_read_helper(hdcp2->init_data.ddc);
	if (rc)
		HDMI_ERR("Cannot read HDCP message register\n");

	hdcp2->timeout_left = ddc_data->timeout_left;

	return rc;
}

static int hdmi_hdcp2p2_ddc_wt_message(struct hdmi_hdcp2p2_ctrl *hdcp2,
	u8 *buf, size_t size, uint timeout)
{
	struct hdmi_tx_ddc_data *ddc_data;
	struct hdmi_tx_ddc_ctrl *ddc_ctrl;

	int rc;

	if (!hdcp2) {
		HDMI_ERR("invalid hdcp2\n");
		return -EINVAL;
	}

	ddc_ctrl = &((struct hdmi_ddc *)hdcp2->init_data.ddc)->ddc_ctrl;
	ddc_data = &ddc_ctrl->ddc_data;

	if (!ddc_data) {
		HDMI_ERR("invalid ddc data\n");
		return -EINVAL;
	}

	memset(ddc_data, 0, sizeof(*ddc_data));
	ddc_data->dev_addr = HDCP_SINK_DDC_SLAVE_ADDR;
	ddc_data->offset = HDCP_SINK_DDC_HDCP2_WRITE_MESSAGE;
	ddc_data->data_buf = buf;
	ddc_data->data_len = size;
	ddc_data->hard_timeout = hdcp2->timeout;
	ddc_data->what = "HDCP2WriteMessage";

	rc = hdmi_ddc_write_helper((void *)hdcp2->init_data.ddc);
	if (rc)
		HDMI_ERR("Cannot write HDCP message register\n");

	hdcp2->timeout_left = ddc_data->timeout_left;

	return rc;
}

static bool hdmi_hdcp2p2_feature_supported(void *input)
{
	int rc;
	struct hdmi_hdcp2p2_ctrl *hdcp2 = input;
	struct sde_hdcp_2x_ops *lib = NULL;
	bool supported = false;

	rc = hdmi_hdcp2p2_valid_handle(hdcp2);
	if (rc)
		return supported;

	lib = hdcp2->lib;
	if (lib->feature_supported)
		supported = lib->feature_supported(
			hdcp2->lib_ctx);

	return supported;
}

static void hdmi_hdcp2p2_link_work(struct work_struct *work)
{
	int rc = 0;
	struct hdmi_hdcp2p2_ctrl *hdcp2 = container_of(work,
				struct hdmi_hdcp2p2_ctrl, link_work);
	struct sde_hdcp_2x_wakeup_data cdata = {HDCP_2X_CMD_INVALID};
	struct hdmi_tx_hdcp2p2_ddc_data *ddc_data;
	struct hdmi_tx_ddc_ctrl *ddc_ctrl;
	u8 *buf;

	if (!hdcp2) {
		HDMI_ERR("invalid input\n");
		return;
	}

	buf = hdcp2->request.data;
	cdata.context = hdcp2->lib_ctx;

	ddc_ctrl = &((struct hdmi_ddc *)hdcp2->init_data.ddc)->ddc_ctrl;
	if (!ddc_ctrl) {
		rc = -EINVAL;
		cdata.cmd = HDCP_2X_CMD_STOP;
		goto exit;
	}

	ddc_data = &ddc_ctrl->hdcp2p2_ddc_data;

	if (ddc_data->reauth_req) {
		HDMI_DEBUG("reauth triggered by sink\n");

		ddc_data->reauth_req = false;
		rc = -ENOLINK;
		cdata.cmd = HDCP_2X_CMD_STOP;
		goto exit;
	}

	if (ddc_data->ready && ddc_data->message_size) {
		HDMI_DEBUG("topology changed. rxstatus msg size %d\n",
			ddc_data->message_size);

		ddc_data->ready  = false;

		memset(buf, 0, hdcp2->request.length);
		rc = hdmi_hdcp2p2_ddc_rd_message(hdcp2, buf,
			ddc_data->message_size, HDCP2P2_DEFAULT_TIMEOUT);
		if (rc) {
			cdata.cmd = HDCP_2X_CMD_STOP;
			HDMI_ERR("error reading message %d\n", rc);
		} else {
			cdata.cmd = HDCP_2X_CMD_MSG_RECV_SUCCESS;
			cdata.total_message_length = ddc_data->message_size;
		}

		ddc_data->message_size = 0;
	}
exit:
	hdmi_hdcp2p2_wakeup_lib(hdcp2, &cdata);

	if (rc) {
		hdmi_hdcp2p2_auth_failed(hdcp2);
		return;
	}
}

static void hdmi_hdcp2p2_link_cb(void *data)
{
	struct hdmi_hdcp2p2_ctrl *hdcp2 = data;

	if (!hdcp2) {
		HDMI_DEBUG("invalid input\n");
		return;
	}

	if (atomic_read(&hdcp2->auth_state) != HDCP_STATE_INACTIVE)
		queue_work(hdcp2->init_data.workq, &hdcp2->link_work);

}

static int hdmi_hdcp2p2_register(void *input, bool mst_enabled)
{
	int rc;
	struct hdmi_hdcp2p2_ctrl *hdcp2 = input;
	struct sde_hdcp_2x_wakeup_data cdata = {HDCP_2X_CMD_ENABLE};

	rc = hdmi_hdcp2p2_valid_handle(hdcp2);
	if (rc)
		return rc;

	cdata.device_type = HDCP_TXMTR_HDMI;
	cdata.context = hdcp2->lib_ctx;
	rc = hdcp2->lib->wakeup(&cdata);

	return rc;
}

static int hdmi_hdcp2p2_on(void *input)
{
	int rc = 0;
	struct hdmi_hdcp2p2_ctrl *hdcp2 = input;
	struct sde_hdcp_2x_wakeup_data cdata = {HDCP_2X_CMD_INVALID};

	rc = hdmi_hdcp2p2_valid_handle(hdcp2);
	if (rc)
		return rc;

	cdata.cmd = HDCP_2X_CMD_START;
	cdata.context = hdcp2->lib_ctx;
	rc = hdcp2->lib->wakeup(&cdata);
	if (rc)
		HDMI_ERR("Unable to start the HDCP 2.2 library (%d)\n", rc);

	return rc;
}

static void hdmi_hdcp2p2_off(void *input)
{
	int rc;
	struct hdmi_hdcp2p2_ctrl *hdcp2 = (struct hdmi_hdcp2p2_ctrl *)input;
	struct sde_hdcp_2x_wakeup_data cdata = {HDCP_2X_CMD_DISABLE};

	rc = hdmi_hdcp2p2_valid_handle(hdcp2);
	if (rc)
		return;

	hdmi_hdcp2p2_reset(hdcp2);

	kthread_park(hdcp2->thread);

	cdata.context = hdcp2->lib_ctx;
	hdcp2->lib->wakeup(&cdata);
}

static int hdmi_hdcp2p2_authenticate(void *input)
{
	int rc;
	struct hdmi_hdcp2p2_ctrl *hdcp2 = input;
	struct hdcp_transport_wakeup_data cdata = {
					HDCP_TRANSPORT_CMD_AUTHENTICATE};
	rc = hdmi_hdcp2p2_valid_handle(hdcp2);
	if (rc)
		return rc;

	hdcp2->sink_status = SINK_CONNECTED;
	atomic_set(&hdcp2->auth_state, HDCP_STATE_AUTHENTICATING);

	if (kthread_should_park())
		kthread_park(hdcp2->thread);
	kfifo_reset(&hdcp2->cmd_q);
	kthread_unpark(hdcp2->thread);

	_sde_hdmi_scrambler_ddc_disable(hdcp2->init_data.ddc);
	hdmi_hdcp2p2_ddc_disable(hdcp2->init_data.ddc);

	cdata.context = input;
	hdmi_hdcp2p2_wakeup(&cdata);

	return rc;
}

static int hdmi_hdcp2p2_reauthenticate(void *input)
{
	struct hdmi_hdcp2p2_ctrl *hdcp2 = (struct hdmi_hdcp2p2_ctrl *)input;

	if (!hdcp2) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi_hdcp2p2_reset((struct hdmi_hdcp2p2_ctrl *)input);

	return  hdmi_hdcp2p2_authenticate(input);
}

static void hdmi_hdcp2p2_min_level_change(void *client_ctx,
		u8 min_enc_level)
{
	struct hdmi_hdcp2p2_ctrl *hdcp2 =
					(struct hdmi_hdcp2p2_ctrl *)client_ctx;
	struct sde_hdcp_2x_wakeup_data cdata = {
		HDCP_2X_CMD_MIN_ENC_LEVEL};

	if (!hdcp2) {
		HDMI_ERR("invalid input\n");
		return;
	}

	cdata.context = hdcp2->lib_ctx;
	cdata.min_enc_level = min_enc_level;
	hdmi_hdcp2p2_wakeup_lib(hdcp2, &cdata);
}

static void hdmi_hdcp2p2_force_encryption(void *data, bool enable)
{
	int rc;
	struct hdmi_hdcp2p2_ctrl *hdcp2 = data;
	struct sde_hdcp_2x_ops *lib = NULL;

	rc = hdmi_hdcp2p2_valid_handle(hdcp2);
	if (rc)
		return;

	lib = hdcp2->lib;
	if (lib->force_encryption)
		lib->force_encryption(hdcp2->lib_ctx, enable);
}

static void hdmi_hdcp2p2_send_msg(struct hdmi_hdcp2p2_ctrl *hdcp2)
{
	int rc = 0;
	struct sde_hdcp_2x_wakeup_data cdata = {HDCP_2X_CMD_INVALID};

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY);
	if (!hdcp2) {
		HDMI_ERR("invalid input\n");
		rc = -EINVAL;
		goto exit;
	}

	cdata.context = hdcp2->lib_ctx;

	if (atomic_read(&hdcp2->auth_state) == HDCP_STATE_INACTIVE) {
		HDMI_ERR("hdcp is off\n");
		goto exit;
	}

	mutex_lock(&hdcp2->msg_lock);

	rc = hdmi_hdcp2p2_ddc_wt_message(hdcp2, hdcp2->response.data,
			hdcp2->response.length, hdcp2->transaction_delay);
	if (rc) {
		HDMI_ERR("Error sending msg to sink %d\n", rc);
		mutex_unlock(&hdcp2->msg_lock);
		goto exit;
	}

	cdata.cmd = HDCP_2X_CMD_MSG_SEND_SUCCESS;
	cdata.timeout = hdcp2->transaction_delay;
	mutex_unlock(&hdcp2->msg_lock);

exit:
	if (rc == -ETIMEDOUT)
		cdata.cmd = HDCP_2X_CMD_MSG_SEND_TIMEOUT;
	else if (rc)
		cdata.cmd = HDCP_2X_CMD_MSG_SEND_FAILED;

	hdmi_hdcp2p2_wakeup_lib(hdcp2, &cdata);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, cdata.cmd);
}

static int hdmi_hdcp2p2_get_msg_from_sink(struct hdmi_hdcp2p2_ctrl *hdcp2)
{
	int timeout_hsync = 0, rc = 0;
	u8 *buf;
	struct hdmi_tx_hdcp2p2_ddc_data *ddc_data;
	struct hdmi_tx_ddc_ctrl *ddc_ctrl;
	struct sde_hdcp_2x_wakeup_data cdata = { HDCP_2X_CMD_INVALID };

	if (!hdcp2) {
		HDMI_ERR("invalid input\n");
		rc = -EINVAL;
		goto exit;
	}

	buf = hdcp2->request.data;
	cdata.context = hdcp2->lib_ctx;

	ddc_ctrl = &((struct hdmi_ddc *)hdcp2->init_data.ddc)->ddc_ctrl;
	if (!ddc_ctrl) {
		HDMI_ERR("invalid ddc hdcp2\n");
		rc = -EINVAL;
		goto exit;
	}

	ddc_data = &ddc_ctrl->hdcp2p2_ddc_data;
	memset(ddc_data, 0, sizeof(*ddc_data));
	timeout_hsync = hdmi_get_timeout_in_hysnc(
	(void *)hdcp2->init_data.ddc, hdcp2->timeout);

	if (timeout_hsync <= 0) {
		HDMI_ERR("err in timeout hsync calc\n");
		timeout_hsync = HDMI_DEFAULT_TIMEOUT_HSYNC;
	}

	HDMI_DEBUG("timeout for rxstatus %dms, %d hsync\n",
	hdcp2->timeout, timeout_hsync);

	ddc_data->intr_mask = RXSTATUS_MESSAGE_SIZE | RXSTATUS_REAUTH_REQ;
	ddc_data->timeout_ms = hdcp2->timeout;
	ddc_data->timeout_hsync = timeout_hsync;
	ddc_data->periodic_timer_hsync = timeout_hsync / 20;
	ddc_data->read_method = HDCP2P2_RXSTATUS_HW_DDC_SW_TRIGGER;
	ddc_data->wait = true;

	rc = hdmi_hdcp2p2_read_rxstatus(hdcp2->init_data.ddc);
	if (rc) {
		HDMI_ERR("error reading rxstatus %d\n", rc);
		goto exit;
	}

	if (ddc_data->reauth_req) {
		ddc_data->reauth_req = false;

		HDMI_DEBUG("reauth triggered by sink\n");
		rc = -EINVAL;
		goto exit;
	}

	hdcp2->timeout_left = ddc_data->timeout_left;

	HDMI_DEBUG("timeout left after rxstatus %dms, msg size %d\n",
	hdcp2->timeout_left, ddc_data->message_size);

	if (!ddc_data->message_size) {
		HDMI_ERR("recvd invalid message size\n");
		rc = -EINVAL;
		goto exit;
	}

	rc = hdmi_hdcp2p2_ddc_rd_message(hdcp2, buf,
		ddc_data->message_size, hdcp2->timeout_left);
	if (rc) {
		HDMI_ERR("error reading message %d\n", rc);
		goto exit;
	}

	cdata.cmd = HDCP_2X_CMD_MSG_RECV_SUCCESS;
	cdata.total_message_length = ddc_data->message_size;
	cdata.timeout = hdcp2->timeout_left;
exit:
	if (rc == -ETIMEDOUT)
		cdata.cmd = HDCP_2X_CMD_MSG_RECV_TIMEOUT;
	else if (rc)
		cdata.cmd = HDCP_2X_CMD_MSG_RECV_FAILED;

	hdmi_hdcp2p2_wakeup_lib(hdcp2, &cdata);
	return rc;
}

static void hdmi_hdcp2p2_recv_msg(struct hdmi_hdcp2p2_ctrl *hdcp2)
{
	struct sde_hdcp_2x_wakeup_data cdata = { HDCP_2X_CMD_INVALID };

	cdata.context = hdcp2->lib_ctx;

	if (atomic_read(&hdcp2->auth_state) == HDCP_STATE_INACTIVE) {
		HDMI_ERR("hdcp is off\n");
		return;
	}

	if (hdcp2->transaction_delay)
		msleep(hdcp2->transaction_delay);

	hdmi_hdcp2p2_get_msg_from_sink(hdcp2);
}

static void hdmi_hdcp2p2_link_check(struct hdmi_hdcp2p2_ctrl *hdcp2)
{
	int rc = 0;
	struct hdmi_tx_ddc_ctrl *ddc_ctrl;
	struct hdmi_tx_hdcp2p2_ddc_data *ddc_data;
	int timeout_hsync = 0;

	if (!hdcp2) {
		HDMI_ERR("invalid input\n");
		return;
	}

	ddc_ctrl = &((struct hdmi_ddc *)hdcp2->init_data.ddc)->ddc_ctrl;

	if (!ddc_ctrl)
		return;

	hdmi_ddc_config(hdcp2->init_data.ddc);

	ddc_data = &ddc_ctrl->hdcp2p2_ddc_data;

	memset(ddc_data, 0, sizeof(*ddc_data));
	timeout_hsync = hdmi_get_timeout_in_hysnc(
					hdcp2->init_data.ddc,
					jiffies_to_msecs(HZ / 2));

	if (timeout_hsync <= 0) {
		HDMI_ERR("err in timeout hsync calc\n");
		timeout_hsync = HDMI_DEFAULT_TIMEOUT_HSYNC;
	}
	HDMI_DEBUG("timeout for rxstatus %d hsyncs\n", timeout_hsync);

	ddc_data->intr_mask = RXSTATUS_READY | RXSTATUS_MESSAGE_SIZE |
		RXSTATUS_REAUTH_REQ;
	ddc_data->timeout_hsync = timeout_hsync;
	ddc_data->periodic_timer_hsync = timeout_hsync;
	ddc_data->read_method = HDCP2P2_RXSTATUS_HW_DDC_SW_TRIGGER;
	ddc_data->link_cb = hdmi_hdcp2p2_link_cb;
	ddc_data->link_data = hdcp2;

	rc = hdmi_hdcp2p2_read_rxstatus((void *)hdcp2->init_data.ddc);

}

static void hdmi_hdcp2p2_start_auth(struct hdmi_hdcp2p2_ctrl *hdcp2)
{
	struct sde_hdcp_2x_wakeup_data cdata = {HDCP_2X_CMD_START_AUTH};

	cdata.context = hdcp2->lib_ctx;

	if (atomic_read(&hdcp2->auth_state) == HDCP_STATE_AUTHENTICATING)
		hdmi_hdcp2p2_wakeup_lib(hdcp2, &cdata);
}

static int hdmi_hdcp2p2_read_version(struct hdmi_hdcp2p2_ctrl *hdcp2,
		u8 *hdcp2version)
{
	struct hdmi_tx_ddc_data *ddc_data;
	struct hdmi_tx_ddc_ctrl *ddc_ctrl;
	int rc;

	if (!hdcp2) {
		HDMI_ERR("invalid hdcp2\n");
		return -EINVAL;
	}

	ddc_ctrl = &((struct hdmi_ddc *)hdcp2->init_data.ddc)->ddc_ctrl;
	ddc_data = &ddc_ctrl->ddc_data;

	if (!ddc_data) {
		HDMI_ERR("invalid ddc data\n");
		return -EINVAL;
	}
	memset(ddc_data, 0, sizeof(*ddc_data));
	ddc_data->dev_addr = HDCP_SINK_DDC_SLAVE_ADDR;
	ddc_data->offset = HDCP_SINK_DDC_HDCP2_VERSION;
	ddc_data->data_buf = hdcp2version;
	ddc_data->data_len = 1;
	ddc_data->request_len = 1;
	ddc_data->retry = 1;
	ddc_data->what = "HDCP2Version";

	rc = hdmi_ddc_read_helper((void *)hdcp2->init_data.ddc);
	if (rc) {
		HDMI_ERR("Cannot read HDCP2Version register\n");
		return rc;
	}

	HDMI_DEBUG("Read HDCP2Version as %u\n", *hdcp2version);
	return rc;
}

static bool hdmi_hdcp2p2_supported(void *input)
{
	u8 hdcp2version = 0;
	int rc = hdmi_hdcp2p2_read_version(
			(struct hdmi_hdcp2p2_ctrl *)input, &hdcp2version);

	if (rc)
		goto error;

	if (hdcp2version & BIT(2)) {
		HDMI_DEBUG("Sink is HDCP 2.2 capable\n");
		return true;
	}

error:
	HDMI_DEBUG("Sink is not HDCP 2.2 capable\n");
	return false;
}

static int hdmi_hdcp2p2_change_streams(struct hdmi_hdcp2p2_ctrl *hdcp2,
		struct sde_hdcp_2x_wakeup_data *cdata)
{
	if (!hdcp2 || cdata->num_streams == 0 || !cdata->streams) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	if (!hdcp2->lib_ctx) {
		HDMI_ERR("HDCP library needs to be acquired\n");
		return -EINVAL;
	}

	if (!hdcp2->lib) {
		HDMI_ERR("invalid lib ops data\n");
		return -EINVAL;
	}

	cdata->context = hdcp2->lib_ctx;
	return hdcp2->lib->wakeup(cdata);
}


static int hdmi_hdcp2p2_register_streams(void *input, u8 num_streams,
			struct stream_info *streams)
{
	struct hdmi_hdcp2p2_ctrl *hdcp2 = input;
	struct sde_hdcp_2x_wakeup_data cdata = {HDCP_2X_CMD_OPEN_STREAMS};

	cdata.streams = streams;
	cdata.num_streams = num_streams;
	return hdmi_hdcp2p2_change_streams(hdcp2, &cdata);
}

static int hdmi_hdcp2p2_deregister_streams(void *input, u8 num_streams,
			struct stream_info *streams)
{
	struct hdmi_hdcp2p2_ctrl *hdcp2 = input;
	struct sde_hdcp_2x_wakeup_data cdata = {HDCP_2X_CMD_CLOSE_STREAMS};

	cdata.streams = streams;
	cdata.num_streams = num_streams;
	return hdmi_hdcp2p2_change_streams(hdcp2, &cdata);
}

void hdmi_hdcp2p2_deinit(void *input)
{
	struct hdmi_hdcp2p2_ctrl *hdcp2 = (struct hdmi_hdcp2p2_ctrl *)input;
	struct sde_hdcp_2x_wakeup_data cdata = {HDCP_2X_CMD_INVALID};

	if (!hdcp2) {
		HDMI_ERR("invalid input\n");
		return;
	}

	if (atomic_read(&hdcp2->auth_state) != HDCP_STATE_AUTH_FAIL) {
		cdata.cmd = HDCP_2X_CMD_STOP;
		cdata.context = hdcp2->lib_ctx;
		hdmi_hdcp2p2_wakeup_lib(hdcp2, &cdata);
	}

	sde_hdcp_2x_deregister(hdcp2->lib_ctx);

	kthread_stop(hdcp2->thread);

	mutex_destroy(&hdcp2->mutex);
	mutex_destroy(&hdcp2->msg_lock);
	kfree(hdcp2);
}

static int hdmi_hdcp2p2_main(void *data)
{
	struct hdmi_hdcp2p2_ctrl *hdcp2 = data;
	enum hdcp_transport_wakeup_cmd cmd;

	while (1) {
		wait_event_idle(hdcp2->wait_q,
			!kfifo_is_empty(&hdcp2->cmd_q) ||
			kthread_should_stop() ||
			kthread_should_park());

		if (kthread_should_stop())
			break;

		if (kfifo_is_empty(&hdcp2->cmd_q) && kthread_should_park()) {
			kthread_parkme();
			continue;
		}

		if (!kfifo_get(&hdcp2->cmd_q, &cmd))
			continue;

		switch (cmd) {
		case HDCP_TRANSPORT_CMD_SEND_MESSAGE:
			hdmi_hdcp2p2_send_msg(hdcp2);
			break;
		case HDCP_TRANSPORT_CMD_RECV_MESSAGE:
			hdmi_hdcp2p2_recv_msg(hdcp2);
			break;
		case HDCP_TRANSPORT_CMD_STATUS_SUCCESS:
			hdmi_hdcp2p2_send_auth_status(hdcp2);
			break;
		case HDCP_TRANSPORT_CMD_STATUS_FAILED:
			hdmi_hdcp2p2_send_auth_status(hdcp2);
			break;
		case HDCP_TRANSPORT_CMD_LINK_POLL:
			hdmi_hdcp2p2_link_check(hdcp2);
			break;
		case HDCP_TRANSPORT_CMD_AUTHENTICATE:
			hdmi_hdcp2p2_start_auth(hdcp2);
			break;
		default:
			break;
		}
	}

	return 0;
}

static void hdmi_hdcp2p2_abort(void *input, bool abort)
{
	struct hdmi_hdcp2p2_ctrl *hdcp2 = input;

	if (!hdcp2) {
		HDMI_ERR("invalid input\n");
		return;
	}

	atomic_set(&hdcp2->abort, abort);
}

void *hdmi_hdcp2p2_init(struct sde_hdcp_init_data *init_data)
{
	int rc;
	struct hdmi_hdcp2p2_ctrl *hdcp2;
	static struct sde_hdcp_ops ops = {
		.reauthenticate = hdmi_hdcp2p2_reauthenticate,
		.isr =  hdmi_ddc_hdcp2p2_isr,
		.authenticate = hdmi_hdcp2p2_authenticate,
		.feature_supported = hdmi_hdcp2p2_feature_supported,
		.force_encryption = hdmi_hdcp2p2_force_encryption,
		.sink_support = hdmi_hdcp2p2_supported,
		.set_mode = hdmi_hdcp2p2_register,
		.on = hdmi_hdcp2p2_on,
		.off = hdmi_hdcp2p2_off,
		.abort = hdmi_hdcp2p2_abort,
		.register_streams = hdmi_hdcp2p2_register_streams,
		.deregister_streams = hdmi_hdcp2p2_deregister_streams,
	};

	static struct hdcp_transport_ops client_ops = {
		.wakeup = hdmi_hdcp2p2_wakeup,
	};

	static struct sde_hdcp_2x_ops hdcp2x_ops;
	struct sde_hdcp_2x_register_data register_data = {0};

	if (!init_data || !init_data->cb_data ||
			!init_data->notify_status) {
		HDMI_ERR("invalid input\n");
		return ERR_PTR(-EINVAL);
	}

	hdcp2 = kzalloc(sizeof(*hdcp2), GFP_KERNEL);
	if (!hdcp2)
		return ERR_PTR(-ENOMEM);

	hdcp2->init_data = *init_data;
	hdcp2->lib = &hdcp2x_ops;
	hdcp2->response.data = NULL;
	hdcp2->request.data = NULL;

	hdcp2->sink_status = SINK_DISCONNECTED;

	INIT_KFIFO(hdcp2->cmd_q);

	init_waitqueue_head(&hdcp2->wait_q);
	atomic_set(&hdcp2->auth_state, HDCP_STATE_INACTIVE);

	hdcp2->ops = &ops;
	mutex_init(&hdcp2->mutex);
	mutex_init(&hdcp2->msg_lock);

	register_data.hdcp_data = &hdcp2->lib_ctx;
	register_data.client_ops = &client_ops;
	register_data.ops = &hdcp2x_ops;
	register_data.client_data = hdcp2;

	rc = sde_hdcp_2x_register(&register_data);
	if (rc) {
		HDMI_ERR("Unable to register with HDCP 2.2 library\n");
		goto error;
	}

	if (IS_ENABLED(CONFIG_HDCP_QSEECOM))
		msm_hdcp_register_cb(init_data->msm_hdcp_dev, hdcp2,
				hdmi_hdcp2p2_min_level_change);

	hdcp2->thread = kthread_run(hdmi_hdcp2p2_main, hdcp2, "hdmi_hdcp2p2");

	if (IS_ERR(hdcp2->thread)) {
		HDMI_ERR("unable to start DP hdcp2p2 thread\n");
		rc = PTR_ERR(hdcp2->thread);
		hdcp2->thread = NULL;
		goto error;
	}
	INIT_WORK(&hdcp2->link_work, hdmi_hdcp2p2_link_work);

	return hdcp2;
error:
	kfree(hdcp2);
	return ERR_PTR(rc);
}

struct sde_hdcp_ops *hdmi_hdcp2p2_get(void *input)
{
	return ((struct hdmi_hdcp2p2_ctrl *)input)->ops;
}
