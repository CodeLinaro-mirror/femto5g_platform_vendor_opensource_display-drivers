// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/component.h>
#include <linux/of_irq.h>
#include <linux/delay.h>
#include <linux/usb/phy.h>
#include <linux/jiffies.h>
#include <linux/pm_qos.h>
#include <linux/ipc_logging.h>

#include "sde_connector.h"

#include "msm_drv.h"
#include "dp_hpd.h"
#include "dp_parser.h"
#include "dp_power.h"
#include "dp_catalog.h"
#include "dp_aux.h"
#include "dp_link.h"
#include "dp_panel.h"
#include "dp_ctrl.h"
#include "dp_audio.h"
#include "dp_client.h"
#include "sde_hdcp.h"
#include "dp_debug.h"
#include "dp_pll.h"
#include "sde_dbg.h"

#define DRM_DP_IPC_NUM_PAGES 10
#define DP_MST_DEBUG(fmt, ...) DP_DEBUG(fmt, ##__VA_ARGS__)

#define dp_mgr_state_show(x) { \
	DP_ERR("%s: state (0x%x): %s\n", x, mgr->state, \
		dp_mgr_state_name(mgr->state)); \
	SDE_EVT32_EXTERNAL(mgr->state); }

#define dp_mgr_state_warn(x) { \
	DP_WARN("%s: state (0x%x): %s\n", x, mgr->state, \
		dp_mgr_state_name(mgr->state)); \
	SDE_EVT32_EXTERNAL(mgr->state); }

#define dp_mgr_state_log(x) { \
	DP_DEBUG("%s: state (0x%x): %s\n", x, mgr->state, \
		dp_mgr_state_name(mgr->state)); \
	SDE_EVT32_EXTERNAL(mgr->state); }

#define dp_mgr_state_is(x) (mgr->state & (x))
#define dp_mgr_state_add(x) { \
	(mgr->state |= (x)); \
	dp_mgr_state_log("add "#x); }
#define dp_mgr_state_remove(x) { \
	(mgr->state &= ~(x)); \
	dp_mgr_state_log("remove "#x); }

#define MAX_TMDS_CLOCK_HDMI_1_4 340000

enum dp_mgr_states {
	DP_STATE_DISCONNECTED           = 0,
	DP_STATE_CONFIGURED             = BIT(0),
	DP_STATE_INITIALIZED            = BIT(1),
	DP_STATE_READY                  = BIT(2),
	DP_STATE_CONNECTED              = BIT(3),
	DP_STATE_CONNECT_NOTIFIED       = BIT(4),
	DP_STATE_DISCONNECT_NOTIFIED    = BIT(5),
	DP_STATE_ENABLED                = BIT(6),
	DP_STATE_SUSPENDED              = BIT(7),
	DP_STATE_ABORTED                = BIT(8),
	DP_STATE_HDCP_ABORTED           = BIT(9),
	DP_STATE_SRC_PWRDN              = BIT(10),
	DP_STATE_TUI_ACTIVE             = BIT(11),
};

struct dp_mgr_type_info {
	int display_type;
};

static char *dp_mgr_state_name(enum dp_mgr_states state)
{
	static char buf[SZ_1K];
	u32 len = 0;

	memset(buf, 0, SZ_1K);

	if (state & DP_STATE_CONFIGURED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"CONFIGURED");

	if (state & DP_STATE_INITIALIZED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"INITIALIZED");

	if (state & DP_STATE_READY)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"READY");

	if (state & DP_STATE_CONNECTED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"CONNECTED");

	if (state & DP_STATE_CONNECT_NOTIFIED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"CONNECT_NOTIFIED");

	if (state & DP_STATE_DISCONNECT_NOTIFIED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"DISCONNECT_NOTIFIED");

	if (state & DP_STATE_ENABLED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"ENABLED");

	if (state & DP_STATE_SUSPENDED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"SUSPENDED");

	if (state & DP_STATE_ABORTED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"ABORTED");

	if (state & DP_STATE_HDCP_ABORTED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"HDCP_ABORTED");

	if (state & DP_STATE_SRC_PWRDN)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"SRC_PWRDN");

	if (state & DP_STATE_TUI_ACTIVE)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"TUI_ACTIVE");

	if (!strlen(buf))
		return "DISCONNECTED";

	return buf;
}

#define HPD_STRING_SIZE 30

struct dp_hdcp_dev {
	void *fd;
	struct sde_hdcp_ops *ops;
	enum sde_hdcp_version ver;
};

struct dp_hdcp {
	void *data;
	struct sde_hdcp_ops *ops;

	u32 source_cap;

	struct dp_hdcp_dev dev[HDCP_VERSION_MAX];
};

struct dp_mst {
	bool mst_active;

	bool drm_registered;
	struct dp_mst_drm_cbs cbs;
};

struct dp_mgr_priv {
	char *name;
	int irq;

	struct list_head panel_list_head;

	enum dp_mgr_states state;
	enum dp_aux_switch_type switch_type;

	struct platform_device *pdev;
	struct device_node *aux_switch_node;
	bool aux_switch_ready;
	struct dp_aux_bridge *aux_bridge;
	struct dentry *root;
	struct completion notification_comp;
	struct completion attention_comp;

	struct dp_hpd     *hpd;
	struct dp_parser  *parser;
	struct dp_power   *power;
	struct dp_catalog *catalog;
	struct dp_aux     *aux;
	struct dp_link    *link;
	struct dp_panel   *panel;
	struct dp_ctrl    *ctrl;
	struct dp_debug   *debug;
	struct dp_pll     *pll;

	struct dp_panel *active_panels[DP_STREAM_MAX];
	struct dp_hdcp hdcp;

	struct dp_hpd_cb hpd_cb;
	struct dp_display_mode mode;
	struct dp_client client;
	struct msm_drm_private *priv;

	struct workqueue_struct *wq;
	struct delayed_work hdcp_cb_work;
	struct work_struct connect_work;
	struct work_struct attention_work;
	struct work_struct disconnect_work;
	struct mutex session_lock;
	struct mutex accounting_lock;
	bool hdcp_delayed_off;
	bool no_aux_switch;

	u32 active_stream_cnt;
	struct dp_mst mst;

	u32 tot_dsc_blks_in_use;
	u32 tot_lm_blks_in_use;

	bool process_hpd_connect;
	struct dev_pm_qos_request pm_qos_req[NR_CPUS];
	bool pm_qos_requested;

	struct notifier_block usb_nb;

	struct dp_intf_info intf_info;
};

static const struct dp_mgr_type_info dp_info = {
	.display_type = DRM_MODE_CONNECTOR_DisplayPort,
};

static const struct dp_mgr_type_info edp_info = {
	.display_type = DRM_MODE_CONNECTOR_eDP,
};

static const struct of_device_id dp_dt_match[] = {
	{ .compatible = "qcom,dp-display",
	  .data = &dp_info,},
	{ .compatible = "qcom,edp-display",
	  .data = &edp_info,},
	{}
};

static inline bool dp_mgr_is_hdcp_enabled(struct dp_mgr_priv *mgr)
{
	return mgr->link->hdcp_status.hdcp_version && mgr->hdcp.ops;
}

static irqreturn_t dp_mgr_irq(int irq, void *dev_id)
{
	struct dp_mgr_priv *mgr = dev_id;

	if (!mgr) {
		DP_ERR("invalid data\n");
		return IRQ_NONE;
	}

	/* DP HPD isr */
	if (mgr->hpd->type ==  DP_HPD_LPHW)
		mgr->hpd->isr(mgr->hpd);

	/* DP controller isr */
	mgr->ctrl->isr(mgr->ctrl);

	/* DP aux isr */
	mgr->aux->isr(mgr->aux);

	/* HDCP isr */
	if (dp_mgr_is_hdcp_enabled(mgr) && mgr->hdcp.ops->isr) {
		if (mgr->hdcp.ops->isr(mgr->hdcp.data))
			DP_ERR("dp_hdcp_isr failed\n");
	}

	return IRQ_HANDLED;
}
static bool dp_mgr_is_ds_bridge(struct dp_panel *panel)
{
	return (panel->dpcd[DP_DOWNSTREAMPORT_PRESENT] &
		DP_DWN_STRM_PORT_PRESENT);
}

static bool dp_mgr_is_sink_count_zero(struct dp_mgr_priv *mgr)
{
	return dp_mgr_is_ds_bridge(mgr->panel) &&
		(mgr->link->sink_count.count == 0);
}

static bool dp_mgr_is_ready(struct dp_mgr_priv *mgr)
{
	return mgr->hpd->hpd_high && dp_mgr_state_is(DP_STATE_CONNECTED) &&
		!dp_mgr_is_sink_count_zero(mgr) &&
		mgr->hpd->alt_mode_cfg_done;
}

static void dp_audio_enable(struct dp_mgr_priv *mgr, bool enable)
{
	struct dp_panel *panel;
	int idx;

	for (idx = DP_STREAM_0; idx < DP_STREAM_MAX; idx++) {
		if (!mgr->active_panels[idx])
			continue;
		panel = mgr->active_panels[idx];

		if (panel->audio_supported) {
			if (enable) {
				panel->audio->bw_code =
					mgr->link->link_params.bw_code;
				panel->audio->lane_count =
					mgr->link->link_params.lane_count;
				panel->audio->on(panel->audio);
			} else {
				panel->audio->off(panel->audio, false);
			}
		}
	}
}

static void dp_mgr_qos_request(struct dp_mgr_priv *mgr, bool add_vote)
{
	struct device *cpu_dev;
	int cpu = 0;
	struct cpumask *cpu_mask;
	u32 latency = mgr->parser->qos_cpu_latency;
	unsigned long mask = mgr->parser->qos_cpu_mask;

	if (!mgr->parser->qos_cpu_mask || (mgr->pm_qos_requested == add_vote))
		return;

	cpu_mask = to_cpumask(&mask);
	for_each_cpu(cpu, cpu_mask) {
		cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev) {
			SDE_DEBUG("%s: failed to get cpu%d device\n", __func__, cpu);
			continue;
		}

		if (add_vote)
			dev_pm_qos_add_request(cpu_dev, &mgr->pm_qos_req[cpu],
				DEV_PM_QOS_RESUME_LATENCY, latency);
		else
			dev_pm_qos_remove_request(&mgr->pm_qos_req[cpu]);
	}

	SDE_EVT32_EXTERNAL(add_vote, mask, latency);
	mgr->pm_qos_requested = add_vote;
}

static void dp_mgr_update_hdcp_status(struct dp_mgr_priv *mgr,
					bool reset)
{
	if (reset) {
		mgr->link->hdcp_status.hdcp_state = HDCP_STATE_INACTIVE;
		mgr->link->hdcp_status.hdcp_version = HDCP_VERSION_NONE;
	}

	memset(mgr->debug->hdcp_status, 0, sizeof(mgr->debug->hdcp_status));

	snprintf(mgr->debug->hdcp_status, sizeof(mgr->debug->hdcp_status),
		"%s: %s\ncaps: %d\n",
		sde_hdcp_version(mgr->link->hdcp_status.hdcp_version),
		sde_hdcp_state_name(mgr->link->hdcp_status.hdcp_state),
		mgr->hdcp.source_cap);
}

static void dp_mgr_update_hdcp_info(struct dp_mgr_priv *mgr)
{
	void *fd = NULL;
	struct dp_hdcp_dev *dev = NULL;
	struct sde_hdcp_ops *ops = NULL;
	int i = HDCP_VERSION_2P2;

	dp_mgr_update_hdcp_status(mgr, true);

	mgr->hdcp.data = NULL;
	mgr->hdcp.ops = NULL;

	if (mgr->debug->hdcp_disabled || mgr->debug->sim_mode)
		return;

	while (i) {
		dev = &mgr->hdcp.dev[i];
		ops = dev->ops;
		fd = dev->fd;

		i >>= 1;

		if (!(mgr->hdcp.source_cap & dev->ver))
			continue;

		if (ops->sink_support(fd)) {
			mgr->hdcp.data = fd;
			mgr->hdcp.ops = ops;
			mgr->link->hdcp_status.hdcp_version = dev->ver;
			break;
		}
	}

	DP_DEBUG("HDCP version supported: %s\n",
		sde_hdcp_version(mgr->link->hdcp_status.hdcp_version));
}

static void dp_mgr_check_source_hdcp_caps(struct dp_mgr_priv *mgr)
{
	int i;
	struct dp_hdcp_dev *hdcp_dev = mgr->hdcp.dev;

	if (mgr->debug->hdcp_disabled) {
		DP_DEBUG("hdcp disabled\n");
		return;
	}

	for (i = 0; i < HDCP_VERSION_MAX; i++) {
		struct dp_hdcp_dev *dev = &hdcp_dev[i];
		struct sde_hdcp_ops *ops = dev->ops;
		void *fd = dev->fd;

		if (!fd || !ops)
			continue;

		if (ops->set_mode && ops->set_mode(fd, mgr->mst.mst_active,
				mgr->intf_info.cell_idx))
			continue;

		if (!(mgr->hdcp.source_cap & dev->ver) &&
				ops->feature_supported &&
				ops->feature_supported(fd))
			mgr->hdcp.source_cap |= dev->ver;
	}

	dp_mgr_update_hdcp_status(mgr, false);
}

static void dp_mgr_hdcp_register_streams(struct dp_mgr_priv *mgr)
{
	int rc;
	size_t i;
	struct sde_hdcp_ops *ops = mgr->hdcp.ops;
	void *data = mgr->hdcp.data;

	if (dp_mgr_is_ready(mgr) && mgr->mst.mst_active && ops &&
			ops->register_streams){
		struct stream_info streams[DP_STREAM_MAX];
		int index = 0;

		DP_DEBUG("Registering all active panel streams with HDCP\n");
		for (i = DP_STREAM_0; i < DP_STREAM_MAX; i++) {
			if (!mgr->active_panels[i])
				continue;
			streams[index].stream_id = i;
			streams[index].virtual_channel =
				mgr->active_panels[i]->vcpi;
			index++;
		}

		if (index > 0) {
			rc = ops->register_streams(data, index, streams);
			if (rc)
				DP_ERR("failed to register streams. rc = %d\n",
					rc);
		}
	}
}

static void dp_mgr_hdcp_deregister_stream(struct dp_mgr_priv *mgr,
		enum dp_stream_id stream_id)
{
	if (mgr->hdcp.ops->deregister_streams && mgr->active_panels[stream_id]) {
		struct stream_info stream = {stream_id,
				mgr->active_panels[stream_id]->vcpi};

		DP_DEBUG("Deregistering stream within HDCP library\n");
		mgr->hdcp.ops->deregister_streams(mgr->hdcp.data, 1, &stream);
	}
}

static void dp_mgr_hdcp_process_delayed_off(struct dp_mgr_priv *mgr)
{
	if (mgr->hdcp_delayed_off) {
		if (mgr->hdcp.ops && mgr->hdcp.ops->off)
			mgr->hdcp.ops->off(mgr->hdcp.data);
		dp_mgr_update_hdcp_status(mgr, true);
		mgr->hdcp_delayed_off = false;
	}
}

static int dp_mgr_hdcp_process_sink_sync(struct dp_mgr_priv *mgr)
{
	u8 sink_status = 0;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY);

	if (mgr->debug->hdcp_wait_sink_sync) {
		drm_dp_dpcd_readb(mgr->aux->drm_aux, DP_SINK_STATUS,
				&sink_status);
		sink_status &= (DP_RECEIVE_PORT_0_STATUS |
				DP_RECEIVE_PORT_1_STATUS);
		if (sink_status < 1) {
			DP_DEBUG("Sink not synchronized. Queuing again then exiting\n");
			queue_delayed_work(mgr->wq, &mgr->hdcp_cb_work, HZ);
			return -EAGAIN;
		}
		/*
		 * Some sinks need more time to stabilize after synchronization
		 * and before it can handle an HDCP authentication request.
		 * Adding the delay for better interoperability.
		 */
		msleep(6000);
	}
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT);

	return 0;
}

static int dp_mgr_hdcp_start(struct dp_mgr_priv *mgr)
{
	if (mgr->link->hdcp_status.hdcp_state != HDCP_STATE_INACTIVE)
		return -EINVAL;

	dp_mgr_check_source_hdcp_caps(mgr);
	dp_mgr_update_hdcp_info(mgr);

	if (dp_mgr_is_hdcp_enabled(mgr)) {
		if (mgr->hdcp.ops && mgr->hdcp.ops->on &&
				mgr->hdcp.ops->on(mgr->hdcp.data)) {
			dp_mgr_update_hdcp_status(mgr, true);
			return 0;
		}
	} else {
		dp_mgr_update_hdcp_status(mgr, true);
		return 0;
	}

	return -EINVAL;
}

static void dp_mgr_hdcp_print_auth_state(struct dp_mgr_priv *mgr)
{
	u32 hdcp_auth_state;
	int rc;

	rc = mgr->catalog->ctrl.read_hdcp_status(&mgr->catalog->ctrl);
	if (rc >= 0) {
		hdcp_auth_state = (rc >> 20) & 0x3;
		DP_DEBUG("hdcp auth state %d\n", hdcp_auth_state);
	}
}

static void dp_mgr_hdcp_process_state(struct dp_mgr_priv *mgr)
{
	struct dp_link_hdcp_status *status;
	struct sde_hdcp_ops *ops;
	void *data;
	int rc = 0;

	status = &mgr->link->hdcp_status;

	ops = mgr->hdcp.ops;
	data = mgr->hdcp.data;

	if (status->hdcp_state != HDCP_STATE_AUTHENTICATED &&
		mgr->debug->force_encryption && ops && ops->force_encryption)
		ops->force_encryption(data, mgr->debug->force_encryption);

	if (status->hdcp_state == HDCP_STATE_AUTHENTICATED)
		dp_mgr_qos_request(mgr, false);
	else
		dp_mgr_qos_request(mgr, true);

	switch (status->hdcp_state) {
	case HDCP_STATE_INACTIVE:
		dp_mgr_hdcp_register_streams(mgr);
		if (mgr->hdcp.ops && mgr->hdcp.ops->authenticate)
			rc = mgr->hdcp.ops->authenticate(data);
		if (!rc)
			status->hdcp_state = HDCP_STATE_AUTHENTICATING;
		break;
	case HDCP_STATE_AUTH_FAIL:
		if (dp_mgr_is_ready(mgr) &&
		    dp_mgr_state_is(DP_STATE_ENABLED)) {
			if (ops && ops->on && ops->on(data)) {
				dp_mgr_update_hdcp_status(mgr, true);
				return;
			}
			dp_mgr_hdcp_register_streams(mgr);
			if (ops && ops->reauthenticate) {
				rc = ops->reauthenticate(data);
				if (rc)
					DP_ERR("failed rc=%d\n", rc);
			}
			status->hdcp_state = HDCP_STATE_AUTHENTICATING;
		} else {
			DP_DEBUG("not reauthenticating, cable disconnected\n");
		}
		break;
	default:
		dp_mgr_hdcp_register_streams(mgr);
		break;
	}
}

static void dp_mgr_abort_hdcp(struct dp_mgr_priv *mgr,
		bool abort)
{
	u8 i = HDCP_VERSION_2P2;
	struct dp_hdcp_dev *dev = NULL;

	while (i) {
		dev = &mgr->hdcp.dev[i];
		i >>= 1;
		if (!(mgr->hdcp.source_cap & dev->ver))
			continue;

		dev->ops->abort(dev->fd, abort);
	}
}

static void dp_mgr_hdcp_cb_work(struct work_struct *work)
{
	struct dp_mgr_priv *mgr;
	struct delayed_work *dw = to_delayed_work(work);
	struct dp_link_hdcp_status *status;
	int rc = 0;

	mgr = container_of(dw, struct dp_mgr_priv, hdcp_cb_work);

	if (!dp_mgr_state_is(DP_STATE_ENABLED | DP_STATE_CONNECTED) ||
	     dp_mgr_state_is(DP_STATE_ABORTED | DP_STATE_HDCP_ABORTED))
		return;

	if (dp_mgr_state_is(DP_STATE_SUSPENDED)) {
		DP_DEBUG("System suspending. Delay HDCP operations\n");
		queue_delayed_work(mgr->wq, &mgr->hdcp_cb_work, HZ);
		return;
	}

	dp_mgr_hdcp_process_delayed_off(mgr);

	rc = dp_mgr_hdcp_process_sink_sync(mgr);
	if (rc)
		return;

	rc = dp_mgr_hdcp_start(mgr);
	if (!rc)
		return;

	dp_mgr_hdcp_print_auth_state(mgr);

	status = &mgr->link->hdcp_status;
	DP_DEBUG("%s: %s\n", sde_hdcp_version(status->hdcp_version),
		sde_hdcp_state_name(status->hdcp_state));

	dp_mgr_update_hdcp_status(mgr, false);

	dp_mgr_hdcp_process_state(mgr);
}

static void dp_mgr_notify_hdcp_status_cb(void *ptr,
		enum sde_hdcp_state state)
{
	struct dp_mgr_priv *mgr = ptr;

	if (!mgr) {
		DP_ERR("invalid input\n");
		return;
	}

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY,
					mgr->link->hdcp_status.hdcp_state);

	mgr->link->hdcp_status.hdcp_state = state;

	queue_delayed_work(mgr->wq, &mgr->hdcp_cb_work, HZ/4);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT,
					mgr->link->hdcp_status.hdcp_state);
}

static void dp_mgr_deinitialize_hdcp(struct dp_mgr_priv *mgr)
{
	if (!mgr) {
		DP_ERR("invalid input\n");
		return;
	}

	sde_hdcp_1x_deinit(mgr->hdcp.dev[HDCP_VERSION_1X].fd);
	sde_dp_hdcp2p2_deinit(mgr->hdcp.dev[HDCP_VERSION_2P2].fd);
}

static int dp_mgr_initialize_hdcp(struct dp_mgr_priv *mgr)
{
	struct sde_hdcp_init_data hdcp_init_data;
	struct dp_parser *parser;
	void *fd;
	int rc = 0;

	if (!mgr) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	parser = mgr->parser;

	hdcp_init_data.client_id     = HDCP_CLIENT_DP;
	hdcp_init_data.drm_aux       = mgr->aux->drm_aux;
	hdcp_init_data.cb_data       = (void *)mgr;
	hdcp_init_data.workq         = mgr->wq;
	hdcp_init_data.sec_access    = true;
	hdcp_init_data.notify_status = dp_mgr_notify_hdcp_status_cb;
	hdcp_init_data.dp_ahb        = &parser->get_io(parser, "dp_ahb")->io;
	hdcp_init_data.dp_aux        = &parser->get_io(parser, "dp_aux")->io;
	hdcp_init_data.dp_link       = &parser->get_io(parser, "dp_link")->io;
	hdcp_init_data.dp_p0         = &parser->get_io(parser, "dp_p0")->io;
	hdcp_init_data.hdcp_io       = &parser->get_io(parser,
						"hdcp_physical")->io;
	hdcp_init_data.revision      = &mgr->panel->link_info.revision;
	hdcp_init_data.msm_hdcp_dev  = mgr->parser->msm_hdcp_dev;

	fd = sde_hdcp_1x_init(&hdcp_init_data);
	if (IS_ERR_OR_NULL(fd)) {
		DP_DEBUG("Error initializing HDCP 1.x\n");
		return -EINVAL;
	}

	mgr->hdcp.dev[HDCP_VERSION_1X].fd = fd;
	mgr->hdcp.dev[HDCP_VERSION_1X].ops = sde_hdcp_1x_get(fd);
	mgr->hdcp.dev[HDCP_VERSION_1X].ver = HDCP_VERSION_1X;
	DP_INFO("HDCP 1.3 initialized\n");

	fd = sde_dp_hdcp2p2_init(&hdcp_init_data);
	if (IS_ERR_OR_NULL(fd)) {
		DP_DEBUG("Error initializing HDCP 2.x\n");
		rc = -EINVAL;
		goto error;
	}

	mgr->hdcp.dev[HDCP_VERSION_2P2].fd = fd;
	mgr->hdcp.dev[HDCP_VERSION_2P2].ops = sde_dp_hdcp2p2_get(fd);
	mgr->hdcp.dev[HDCP_VERSION_2P2].ver = HDCP_VERSION_2P2;
	DP_INFO("HDCP 2.2 initialized\n");

	return 0;
error:
	sde_hdcp_1x_deinit(mgr->hdcp.dev[HDCP_VERSION_1X].fd);

	return rc;
}

static void dp_mgr_pause_audio(struct dp_mgr_priv *mgr, bool pause)
{
	struct dp_panel *panel;
	int idx;

	for (idx = DP_STREAM_0; idx < DP_STREAM_MAX; idx++) {
		if (!mgr->active_panels[idx])
			continue;
		panel = mgr->active_panels[idx];

		if (panel->audio_supported)
			panel->audio->tui_active = pause;
	}
}

static int dp_mgr_pre_hw_release(void *data)
{
	struct dp_mgr_priv *mgr;
	struct dp_client *client = data;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY);

	if (!client)
		return -EINVAL;

	mgr = container_of(client, struct dp_mgr_priv, client);

	mutex_lock(&mgr->session_lock);

	dp_mgr_state_add(DP_STATE_TUI_ACTIVE);
	cancel_work_sync(&mgr->connect_work);
	cancel_work_sync(&mgr->attention_work);
	cancel_work_sync(&mgr->disconnect_work);
	flush_workqueue(mgr->wq);

	dp_mgr_pause_audio(mgr, true);
	disable_irq(mgr->irq);

	mutex_unlock(&mgr->session_lock);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT);
	return 0;
}

static int dp_mgr_post_hw_acquire(void *data)
{
	struct dp_mgr_priv *mgr;
	struct dp_client *client = data;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY);

	if (!client)
		return -EINVAL;

	mgr = container_of(client, struct dp_mgr_priv, client);

	mutex_lock(&mgr->session_lock);

	dp_mgr_state_remove(DP_STATE_TUI_ACTIVE);
	dp_mgr_pause_audio(mgr, false);
	enable_irq(mgr->irq);

	mutex_unlock(&mgr->session_lock);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT);
	return 0;
}

static int dp_mgr_get_cell_info(struct dp_mgr_priv *mgr)
{
	struct device_node *of_node = mgr->pdev->dev.of_node;
	int i, rc;

	of_property_read_u32(of_node, "cell-index", &mgr->intf_info.cell_idx);

	if (of_property_read_bool(of_node, "qcom,mst-enable"))
		mgr->intf_info.stream_cnt = DP_STREAM_MAX;

	of_property_read_u32_index(of_node,
			"qcom,intf-index", 0, &mgr->intf_info.intf_idx[0]);

	for (i = 1; i < mgr->intf_info.stream_cnt; i++) {
		rc = of_property_read_u32_index(of_node,
				"qcom,intf-index", i, &mgr->intf_info.intf_idx[i]);
		if (rc)
			mgr->intf_info.intf_idx[i] = mgr->intf_info.intf_idx[0] + i;
	}

	of_property_read_u32(of_node, "qcom,phy-index", &mgr->intf_info.phy_idx);

	return 0;
}

static int dp_mgr_bind(struct device *dev, struct device *master,
		struct dp_client *client)
{
	int rc = 0;
	struct dp_mgr_priv *mgr;
	struct drm_device *drm;
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_vm_ops vm_event_ops = {
		.vm_pre_hw_release = dp_mgr_pre_hw_release,
		.vm_post_hw_acquire = dp_mgr_post_hw_acquire,
	};

	if (!dev || !pdev || !master) {
		DP_ERR("invalid param(s), dev %pK, pdev %pK, master %pK\n",
				dev, pdev, master);
		rc = -EINVAL;
		goto end;
	}

	drm = dev_get_drvdata(master);
	mgr = container_of(client, struct dp_mgr_priv, client);
	if (!drm || !mgr) {
		DP_ERR("invalid param(s), drm %pK, mgr %pK\n",
				drm, mgr);
		rc = -EINVAL;
		goto end;
	}

	mgr->client.drm_dev = drm;
	mgr->priv = drm->dev_private;
	msm_register_vm_event(master, dev, &vm_event_ops,
			(void *)&mgr->client);
end:
	return rc;
}

static void dp_mgr_unbind(struct device *dev, struct device *master,
		struct dp_client *client)
{
	struct dp_mgr_priv *mgr;
	struct platform_device *pdev = to_platform_device(dev);

	if (!dev || !pdev) {
		DP_ERR("invalid param(s)\n");
		return;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	if (!mgr) {
		DP_ERR("Invalid params\n");
		return;
	}

	if (mgr->power)
		(void)mgr->power->power_client_deinit(mgr->power);

	if (mgr->aux)
		(void)mgr->aux->drm_aux_deregister(mgr->aux);

	dp_mgr_deinitialize_hdcp(mgr);
}

static bool dp_mgr_send_hpd_event(struct dp_mgr_priv *mgr)
{
	struct drm_device *dev = NULL;
	struct drm_connector *connector;
	char name[HPD_STRING_SIZE], status[HPD_STRING_SIZE],
		bpp[HPD_STRING_SIZE], pattern[HPD_STRING_SIZE];
	char *envp[6];
	char *event_string = "HOTPLUG=1";
	struct dp_client *display;
	int rc = 0;

	connector = mgr->client.base_connector;
	display = &mgr->client;

	if (!connector) {
		DP_ERR("connector not set\n");
		return false;
	}

	connector->status = display->is_sst_connected ? connector_status_connected :
			connector_status_disconnected;

	dev = connector->dev;

	if (mgr->debug->skip_uevent) {
		DP_INFO("skipping uevent\n");
		return false;
	}

	snprintf(name, HPD_STRING_SIZE, "name=%s", connector->name);
	snprintf(status, HPD_STRING_SIZE, "status=%s",
		drm_get_connector_status_name(connector->status));
	snprintf(bpp, HPD_STRING_SIZE, "bpp=%d",
		dp_link_bit_depth_to_bpp(
		mgr->link->test_video.test_bit_depth));
	snprintf(pattern, HPD_STRING_SIZE, "pattern=%d",
		mgr->link->test_video.test_video_pattern);

	DP_INFO("[%s]:[%s] [%s] [%s]\n", name, status, bpp, pattern);
	envp[0] = name;
	envp[1] = status;
	envp[2] = bpp;
	envp[3] = pattern;
	envp[4] = event_string;
	envp[5] = NULL;

	rc = kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
	DP_INFO("uevent %s: %d\n", rc ? "failure" : "success", rc);

	return true;
}

static int dp_mgr_send_hpd_notification(struct dp_mgr_priv *mgr, bool skip_wait)
{
	int ret = 0;
	bool hpd = !!dp_mgr_state_is(DP_STATE_CONNECTED);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state, hpd);

	/*
	 * Send the notification only if there is any change. This check is
	 * necessary since it is possible that the connect_work may or may not
	 * skip sending the notification in order to respond to a pending
	 * attention message. Attention work thread will always attempt to
	 * send the notification after successfully handling the attention
	 * message. This check here will avoid any unintended duplicate
	 * notifications.
	 */
	if (dp_mgr_state_is(DP_STATE_CONNECT_NOTIFIED) && hpd) {
		DP_DEBUG("connection notified already, skip notification\n");
		goto skip_wait;
	} else if (dp_mgr_state_is(DP_STATE_DISCONNECT_NOTIFIED) && !hpd) {
		DP_DEBUG("disonnect notified already, skip notification\n");
		goto skip_wait;
	}

	mgr->aux->state |= DP_STATE_NOTIFICATION_SENT;

	reinit_completion(&mgr->notification_comp);

	if (!mgr->mst.mst_active) {
		mgr->client.is_sst_connected = hpd;

		if (!dp_mgr_send_hpd_event(mgr))
			goto skip_wait;
	} else {
		mgr->client.is_sst_connected = false;

		if (!mgr->mst.cbs.hpd)
			goto skip_wait;

		mgr->mst.cbs.hpd(&mgr->client, hpd);
	}

	if (hpd) {
		dp_mgr_state_add(DP_STATE_CONNECT_NOTIFIED);
		dp_mgr_state_remove(DP_STATE_DISCONNECT_NOTIFIED);
	} else {
		dp_mgr_state_add(DP_STATE_DISCONNECT_NOTIFIED);
		dp_mgr_state_remove(DP_STATE_CONNECT_NOTIFIED);
	}

	/*
	 * Skip the wait if TUI is active considering that the user mode will
	 * not act on the notification until after the TUI session is over.
	 */
	if (dp_mgr_state_is(DP_STATE_TUI_ACTIVE)) {
		dp_mgr_state_log("[TUI is active, skipping wait]");
		goto skip_wait;
	}

	if (skip_wait || (hpd && mgr->mst.mst_active))
		goto skip_wait;

	if (!mgr->mst.mst_active &&
			(!!dp_mgr_state_is(DP_STATE_ENABLED) == hpd))
		goto skip_wait;

	// wait 4 seconds
	if (wait_for_completion_timeout(&mgr->notification_comp, HZ * 4))
		goto skip_wait;

	//resend notification
	if (mgr->mst.mst_active)
		mgr->mst.cbs.hpd(&mgr->client, hpd);
	else
		dp_mgr_send_hpd_event(mgr);

	// wait another 2 seconds
	if (!wait_for_completion_timeout(&mgr->notification_comp, HZ * 2)) {
		DP_WARN("%s timeout\n", hpd ? "connect" : "disconnect");
		ret = -EINVAL;
	}

skip_wait:
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state, hpd, ret);
	return ret;
}

static void dp_mgr_update_mst_state(struct dp_mgr_priv *mgr,
					bool state)
{
	mgr->mst.mst_active = state;
	mgr->panel->mst_state = state;
}

static void dp_mgr_mst_init(struct dp_mgr_priv *mgr)
{
	bool is_mst_receiver;
	const unsigned long clear_mstm_ctrl_timeout_us = 100000;
	u8 old_mstm_ctrl;
	int ret;

	if (!mgr->parser->has_mst || !mgr->mst.drm_registered) {
		DP_MST_DEBUG("mst not enabled. has_mst:%d, registered:%d\n",
				mgr->parser->has_mst, mgr->mst.drm_registered);
		return;
	}

	is_mst_receiver = mgr->panel->read_mst_cap(mgr->panel);

	if (!is_mst_receiver) {
		DP_MST_DEBUG("sink doesn't support mst\n");
		return;
	}

	/* clear sink mst state */
	drm_dp_dpcd_readb(mgr->aux->drm_aux, DP_MSTM_CTRL, &old_mstm_ctrl);
	drm_dp_dpcd_writeb(mgr->aux->drm_aux, DP_MSTM_CTRL, 0);

	/* add extra delay if MST state is not cleared */
	if (old_mstm_ctrl) {
		DP_MST_DEBUG("MSTM_CTRL is not cleared, wait %luus\n",
				clear_mstm_ctrl_timeout_us);
		usleep_range(clear_mstm_ctrl_timeout_us,
			clear_mstm_ctrl_timeout_us + 1000);
	}

	ret = drm_dp_dpcd_writeb(mgr->aux->drm_aux, DP_MSTM_CTRL,
				DP_MST_EN | DP_UP_REQ_EN | DP_UPSTREAM_IS_SRC);
	if (ret < 0) {
		DP_ERR("sink mst enablement failed\n");
		return;
	}

	dp_mgr_update_mst_state(mgr, true);
}

static void dp_mgr_set_mst_mgr_state(struct dp_mgr_priv *mgr,
					bool state)
{
	if (!mgr->mst.mst_active)
		return;

	if (mgr->mst.cbs.set_mgr_state)
		mgr->mst.cbs.set_mgr_state(&mgr->client, state);

	DP_MST_DEBUG("mst_mgr_state: %d\n", state);
}

static int dp_mgr_host_init(struct dp_mgr_priv *mgr)
{
	bool flip = false;
	bool reset;
	int rc = 0;

	if (dp_mgr_state_is(DP_STATE_INITIALIZED)) {
		dp_mgr_state_log("[already initialized]");
		return rc;
	}

	if (mgr->hpd->orientation == ORIENTATION_CC2)
		flip = true;

	reset = mgr->debug->sim_mode || mgr->client.is_cont_splash_enabled ?
			false : !mgr->hpd->multi_func;

	if (!mgr->client.is_cont_splash_enabled) {
		rc = mgr->power->init(mgr->power, flip);
		if (rc) {
			DP_WARN("Power init failed.\n");
			SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_CASE1, mgr->state);
			return rc;
		}
	}

	mgr->hpd->host_init(mgr->hpd, &mgr->catalog->hpd);
	rc = mgr->ctrl->init(mgr->ctrl, flip, reset);
	if (rc) {
		DP_WARN("Ctrl init Failed.\n");
		SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_CASE2, mgr->state);
		goto error_ctrl;
	}

	enable_irq(mgr->irq);
	dp_mgr_abort_hdcp(mgr, false);

	dp_mgr_state_add(DP_STATE_INITIALIZED);

	/* log this as it results from user action of cable connection */
	DP_INFO("[OK]\n");
	return rc;

error_ctrl:
	mgr->hpd->host_deinit(mgr->hpd, &mgr->catalog->hpd);
	mgr->power->deinit(mgr->power);
	return rc;
}

static int dp_mgr_panel_ready(struct dp_mgr_priv *mgr)
{
	int rc = 0;

	if ((mgr->client.is_edp)  && (!mgr->client.ext_hpd_en)) {
		rc = mgr->power->edp_panel_set_gpio(mgr->power, DP_GPIO_EDP_VCC_EN, true);
		if (rc) {
			DP_ERR("Cannot turn edp panel power on");
			return rc;
		}

		if (!(mgr->catalog->hpd.wait_for_edp_panel_ready(&mgr->catalog->hpd))) {
			DP_ERR("EDP PANEL is not ready yet, powering off panel\n");
			rc = mgr->power->edp_panel_set_gpio(mgr->power, DP_GPIO_EDP_VCC_EN, false);
			if (rc) {
				DP_ERR("Cannot turn edp panel power off");
				return rc;
			}
			return -ETIMEDOUT;
		}
	}
	mgr->panel->init(mgr->panel, mgr->client.is_cont_splash_enabled);

	return 0;
}

static int dp_mgr_host_ready(struct dp_mgr_priv *mgr)
{
	int rc = 0;

	if (!dp_mgr_state_is(DP_STATE_INITIALIZED)) {
		rc = dp_mgr_host_init(mgr);
		if (rc) {
			dp_mgr_state_show("[not initialized]");
			return rc;
		}
	}

	if (dp_mgr_state_is(DP_STATE_READY)) {
		dp_mgr_state_log("[already ready]");
		return rc;
	}

	/*
	 * Reset the aborted state for AUX and CTRL modules. This will
	 * allow these modules to execute normally in response to the
	 * cable connection event.
	 *
	 * One corner case still exists. While the execution flow ensures
	 * that cable disconnection flushes all pending work items on the DP
	 * workqueue, and waits for the user module to clean up the DP
	 * connection session, it is possible that the system delays can
	 * lead to timeouts in the connect path. As a result, the actual
	 * connection callback from user modules can come in late and can
	 * race against a subsequent connection event here which would have
	 * reset the aborted flags. There is no clear solution for this since
	 * the connect/disconnect notifications do not currently have any
	 * sessions IDs.
	 */
	mgr->aux->abort(mgr->aux, false);
	mgr->ctrl->abort(mgr->ctrl, false);

	mgr->aux->init(mgr->aux, mgr->parser->aux_cfg, mgr->client.is_cont_splash_enabled);
	rc = dp_mgr_panel_ready(mgr);

	dp_mgr_state_add(DP_STATE_READY);
	/* log this as it results from user action of cable connection */
	DP_INFO("[OK]\n");
	return rc;
}

static void dp_mgr_host_unready(struct dp_mgr_priv *mgr)
{
	if (!dp_mgr_state_is(DP_STATE_INITIALIZED)) {
		dp_mgr_state_warn("[not initialized]");
		return;
	}

	if (!dp_mgr_state_is(DP_STATE_READY)) {
		dp_mgr_state_show("[not ready]");
		return;
	}

	dp_mgr_state_remove(DP_STATE_READY);
	mgr->aux->deinit(mgr->aux);
	/* log this as it results from user action of cable disconnection */
	DP_INFO("[OK]\n");
}

static void dp_mgr_host_deinit(struct dp_mgr_priv *mgr)
{
	if (mgr->active_stream_cnt) {
		SDE_EVT32_EXTERNAL(mgr->state, mgr->active_stream_cnt);
		DP_DEBUG("active stream present\n");
		return;
	}

	if (!dp_mgr_state_is(DP_STATE_INITIALIZED)) {
		dp_mgr_state_show("[not initialized]");
		return;
	}

	if (dp_mgr_state_is(DP_STATE_READY)) {
		DP_DEBUG("mgr deinit before unready\n");
		dp_mgr_host_unready(mgr);
	}

	dp_mgr_abort_hdcp(mgr, true);
	mgr->ctrl->deinit(mgr->ctrl);
	mgr->hpd->host_deinit(mgr->hpd, &mgr->catalog->hpd);
	mgr->power->deinit(mgr->power);
	disable_irq(mgr->irq);
	mgr->aux->state = 0;

	dp_mgr_state_remove(DP_STATE_INITIALIZED);

	/* log this as it results from user action of cable dis-connection */
	DP_INFO("[OK]\n");
}

static bool dp_mgr_hpd_irq_pending(struct dp_mgr_priv *mgr)
{

	unsigned long wait_timeout_ms = 0;
	unsigned long t_out = 0;
	unsigned long wait_time = 0;

	do {
		/*
		 * If an IRQ HPD is pending, then do not send a connect notification.
		 * Once this work returns, the IRQ HPD would be processed and any
		 * required actions (such as link maintenance) would be done which
		 * will subsequently send the HPD notification. To keep things simple,
		 * do this only for SST use-cases. MST use cases require additional
		 * care in order to handle the side-band communications as well.
		 *
		 * One of the main motivations for this is DP LL 1.4 CTS use case
		 * where it is possible that we could get a test request right after
		 * a connection, and the strict timing requriements of the test can
		 * only be met if we do not wait for the e2e connection to be set up.
		 */
		if (!mgr->mst.mst_active && (work_busy(&mgr->attention_work) ==
				WORK_BUSY_PENDING)) {
			SDE_EVT32_EXTERNAL(mgr->state, 99, jiffies_to_msecs(t_out));
			DP_DEBUG("Attention pending, skip HPD notification\n");
			return true;
		}

		/*
		 * If no IRQ HPD, delay the HPD connect notification for
		 * MAX_CONNECT_NOTIFICATION_DELAY_MS to see if sink generates any IRQ HPDs
		 * after the HPD high. Wait for
		 * MAX_CONNECT_NOTIFICATION_DELAY_MS to make sure any IRQ HPD from test
		 * requests aren't missed.
		 */
		reinit_completion(&mgr->attention_comp);
		wait_timeout_ms = min_t(unsigned long, mgr->debug->connect_notification_delay_ms,
				(unsigned long) MAX_CONNECT_NOTIFICATION_DELAY_MS - wait_time);
		t_out = wait_for_completion_timeout(&mgr->attention_comp,
				msecs_to_jiffies(wait_timeout_ms));
		wait_time += (t_out == 0) ?  wait_timeout_ms : t_out;

	} while ((wait_timeout_ms < wait_time) && (wait_time < MAX_CONNECT_NOTIFICATION_DELAY_MS));

	DP_DEBUG("wait_timeout=%lu ms, time_waited=%lu ms\n", wait_timeout_ms, wait_time);

	return false;

}

static int dp_mgr_process_hpd_high(struct dp_mgr_priv *mgr)
{
	int rc = -EINVAL;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);
	mutex_lock(&mgr->session_lock);

	if (dp_mgr_state_is(DP_STATE_CONNECTED)) {
		DP_DEBUG("mgr already connected, skipping hpd high\n");
		mutex_unlock(&mgr->session_lock);
		return -EISCONN;
	}

	dp_mgr_state_add(DP_STATE_CONNECTED);

	mgr->client.max_pclk_khz = min(mgr->parser->max_pclk_khz,
					mgr->debug->max_pclk_khz);

	if (!mgr->debug->sim_mode && !mgr->no_aux_switch && !mgr->parser->gpio_aux_switch
			&& mgr->aux_switch_node && mgr->aux->switch_configure) {
		rc = mgr->aux->switch_configure(mgr->aux, true, mgr->hpd->orientation);
		if (rc)
			goto err_state;
	}

	/*
	 * If mgr video session is not restored from a previous session teardown
	 * by userspace, ensure the host_init is executed, in such a scenario,
	 * so that all the required DP resources are enabled.
	 *
	 * Below is one of the sequences of events which describe the above
	 * scenario:
	 *  a. Source initiated power down resulting in host_deinit.
	 *  b. Sink issues hpd low attention without physical cable disconnect.
	 *  c. Source initiated power up sequence returns early because hpd is
	 *     not high.
	 *  d. Sink issues a hpd high attention event.
	 */
	if (dp_mgr_state_is(DP_STATE_SRC_PWRDN) &&
			dp_mgr_state_is(DP_STATE_CONFIGURED)) {
		rc = dp_mgr_host_init(mgr);
		if (rc) {
			DP_WARN("Host init Failed");
			if (!dp_mgr_state_is(DP_STATE_SUSPENDED)) {
				/*
				 * If not suspended no point of going forward if
				 * resource is not enabled.
				 */
				dp_mgr_state_remove(DP_STATE_CONNECTED);
			}
			goto err_unlock;
		}

		/*
		 * If device is suspended and host_init fails, there is
		 * one more chance for host init to happen in prepare which
		 * is why DP_STATE_SRC_PWRDN is removed only at success.
		 */
		dp_mgr_state_remove(DP_STATE_SRC_PWRDN);
	}

	rc = dp_mgr_host_ready(mgr);
	if (rc) {
		dp_mgr_state_show("[ready failed]");
		goto err_state;
	}

	if (!mgr->client.is_cont_splash_enabled)
		mgr->link->psm_config(mgr->link, &mgr->panel->link_info, false);

	mgr->debug->psm_enabled = false;

	if (!mgr->client.base_connector)
		goto err_unready;

	rc = mgr->panel->read_sink_caps(mgr->panel,
			mgr->client.base_connector, mgr->hpd->multi_func);
	/*
	 * ETIMEDOUT --> cable may have been removed
	 * ENOTCONN --> no downstream device connected
	 */
	if (rc == -ETIMEDOUT || rc == -ENOTCONN)
		goto err_unready;

	/*
	 * In the PHY layer of a DP connection (cable or/and the sink), often
	 * "Link Training(LT) tunable PHY repeaters (LTTPR)" are employed. These LTTPRs can operate
	 * in 2 modes: Transparent & Non-Transparent. Even though the DP 1.4spec suggests
	 * transparent mode as default for LTTPRs, it is observed that some cables with LTTPRs
	 * (e.g. apple cable) misbehave if the operating mode isn't set explicitly. Hence set the
	 * transparent mode if at least 1 LTTPR is present in the path.
	 */
	if (drm_dp_lttpr_count(mgr->panel->lttpr_common_caps))
		mgr->panel->set_lttpr_mode(mgr->panel, true);

	mgr->link->process_request(mgr->link);
	mgr->panel->handle_sink_request(mgr->panel);

	dp_mgr_mst_init(mgr);

	rc = mgr->ctrl->on(mgr->ctrl, mgr->mst.mst_active, mgr->panel->fec_en,
		mgr->panel->dsc_en, false, mgr->client.is_cont_splash_enabled);
	if (rc)
		goto err_mst;

	mgr->process_hpd_connect = false;

	dp_mgr_set_mst_mgr_state(mgr, true);

	mutex_unlock(&mgr->session_lock);

	if (dp_mgr_hpd_irq_pending(mgr))
		goto end;

	if (!rc && !dp_mgr_state_is(DP_STATE_ABORTED))
		dp_mgr_send_hpd_notification(mgr, false);

	goto end;

err_mst:
	dp_mgr_update_mst_state(mgr, false);
err_unready:
	dp_mgr_host_unready(mgr);
err_state:
	dp_mgr_state_remove(DP_STATE_CONNECTED);
err_unlock:
	mutex_unlock(&mgr->session_lock);
end:
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state, rc);
	return rc;
}

static void dp_mgr_process_mst_hpd_low(struct dp_mgr_priv *mgr, bool skip_wait)
{
	int rc = 0;

	if (mgr->mst.mst_active) {
		DP_MST_DEBUG("mst_hpd_low work\n");

		/*
		 * HPD unplug callflow:
		 * 1. send hpd unplug on base connector so usermode can disable
		 * all external displays.
		 * 2. unset mst state in the topology mgr so the branch device
		 *  can be cleaned up.
		 */

		if ((dp_mgr_state_is(DP_STATE_CONNECT_NOTIFIED) ||
				dp_mgr_state_is(DP_STATE_ENABLED)))
			rc = dp_mgr_send_hpd_notification(mgr, skip_wait);

		dp_mgr_set_mst_mgr_state(mgr, false);
		dp_mgr_update_mst_state(mgr, false);
	}

	DP_MST_DEBUG("mst_hpd_low. mst_active:%d\n", mgr->mst.mst_active);
}

static int dp_mgr_process_hpd_low(struct dp_mgr_priv *mgr, bool skip_wait)
{
	int rc = 0;

	dp_mgr_state_remove(DP_STATE_CONNECTED);
	mgr->process_hpd_connect = false;
	dp_audio_enable(mgr, false);

	if (mgr->mst.mst_active) {
		dp_mgr_process_mst_hpd_low(mgr, skip_wait);
	} else {
		if ((dp_mgr_state_is(DP_STATE_CONNECT_NOTIFIED) ||
				dp_mgr_state_is(DP_STATE_ENABLED)))
			rc = dp_mgr_send_hpd_notification(mgr, skip_wait);
	}

	mutex_lock(&mgr->session_lock);
	if (!mgr->active_stream_cnt)
		mgr->ctrl->off(mgr->ctrl);
	mutex_unlock(&mgr->session_lock);

	mgr->panel->video_test = false;

	return rc;
}

static int dp_mgr_aux_switch_callback(struct notifier_block *self,
		unsigned long event, void *data)
{
	return 0;
}

static int dp_mgr_init_aux_switch(struct dp_mgr_priv *mgr)
{
	int rc = 0;
	struct notifier_block nb;
	const u32 max_retries = 50;
	u32 retry;

	if (mgr->aux_switch_ready)
		return rc;

	if (!mgr->aux->switch_register_notifier)
		return rc;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY);

	nb.notifier_call = dp_mgr_aux_switch_callback;
	nb.priority = 0;

	/*
	 * Iteratively wait for reg notifier which confirms that fsa driver is probed.
	 * Bootup DP with cable connected usecase can hit this scenario.
	 */
	for (retry = 0; retry < max_retries; retry++) {
		rc = mgr->aux->switch_register_notifier(&nb, mgr->aux_switch_node);
		if (rc == 0) {
			DP_DEBUG("registered notifier successfully\n");
			mgr->aux_switch_ready = true;
			break;
		}

		DP_DEBUG("failed to register notifier retry=%d rc=%d\n", retry, rc);
		msleep(100);
	}

	if (retry == max_retries) {
		DP_WARN("Failed to register fsa notifier\n");
		mgr->aux_switch_ready = false;
		return rc;
	}

	if (mgr->aux->switch_unregister_notifier)
		mgr->aux->switch_unregister_notifier(&nb, mgr->aux_switch_node);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, rc);
	return rc;
}

static int dp_mgr_usbpd_configure_cb(void *data)
{
	int rc = 0;
	struct dp_mgr_priv *mgr;

	if (!data) {
		DP_ERR("invalid dev\n");
		return -EINVAL;
	}

	mgr = data;

	/*
	 * When mgr is connected during boot, there is a chance that
	 * configure_cb is called before drm probe is finished and
	 * cause host_init failure. Here we poll the value of
	 * poll_enabled and wait until drm driver is ready.
	 */
	if (!mgr->client.drm_dev->mode_config.poll_enabled) {
		const int poll_timeout = 10000;
		int i;

		for (i = 0; !mgr->client.drm_dev->mode_config.poll_enabled &&
				i < poll_timeout; i++)
			usleep_range(1000, 1100);

		if (i == poll_timeout) {
			DP_ERR("driver is not loaded\n");
			return -ENODEV;
		}
	}

	if (!mgr->debug->sim_mode && !mgr->no_aux_switch
	    && !mgr->parser->gpio_aux_switch && mgr->aux_switch_node &&
			mgr->aux->switch_configure) {
		rc = dp_mgr_init_aux_switch(mgr);
		if (rc)
			return rc;

		rc = mgr->aux->switch_configure(mgr->aux, true, mgr->hpd->orientation);
		if (rc)
			return rc;
	}

	mutex_lock(&mgr->session_lock);

	if (dp_mgr_state_is(DP_STATE_TUI_ACTIVE)) {
		dp_mgr_state_log("[TUI is active]");
		mutex_unlock(&mgr->session_lock);
		return 0;
	}

	dp_mgr_state_remove(DP_STATE_ABORTED);
	dp_mgr_state_add(DP_STATE_CONFIGURED);

	rc = dp_mgr_host_init(mgr);
	if (rc) {
		DP_ERR("Host init Failed");
		mutex_unlock(&mgr->session_lock);
		return rc;
	}

	/* check for hpd high */
	if (mgr->hpd->hpd_high)
		queue_work(mgr->wq, &mgr->connect_work);
	else
		mgr->process_hpd_connect = true;
	mutex_unlock(&mgr->session_lock);

	return 0;
}

static void dp_mgr_clear_reservation(struct dp_client *client, int panel_id)
{
	struct dp_mgr_priv *mgr;
	struct dp_panel *panel;

	if (!client) {
		DP_ERR("invalid params\n");
		return;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid panel info\n");
		return;
	}

	mutex_lock(&mgr->accounting_lock);

	mgr->tot_lm_blks_in_use -= panel->max_lm;
	panel->max_lm = 0;

	mutex_unlock(&mgr->accounting_lock);
}

static void dp_mgr_clear_dsc_resources(struct dp_mgr_priv *mgr,
		struct dp_panel *panel)
{
	mgr->tot_dsc_blks_in_use -= panel->dsc_blks_in_use;
	panel->dsc_blks_in_use = 0;
}

static int dp_mgr_get_mst_pbn_div(struct dp_client *client)
{
	struct dp_mgr_priv *mgr;
	u32 link_rate, lane_count;

	if (!client) {
		DP_ERR("invalid params\n");
		return 0;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	link_rate = drm_dp_bw_code_to_link_rate(mgr->link->link_params.bw_code);
	lane_count = mgr->link->link_params.lane_count;

	return link_rate * lane_count / 54000;
}

static int dp_mgr_stream_pre_disable(struct dp_mgr_priv *mgr,
			struct dp_panel *panel)
{
	if (!mgr->active_stream_cnt) {
		DP_WARN("streams already disabled cnt=%d\n",
				mgr->active_stream_cnt);
		return 0;
	}

	mgr->ctrl->stream_pre_off(mgr->ctrl, panel);

	return 0;
}

static void dp_mgr_stream_disable(struct dp_mgr_priv *mgr,
			struct dp_panel *panel)
{
	if (!mgr->active_stream_cnt) {
		DP_WARN("streams already disabled cnt=%d\n",
				mgr->active_stream_cnt);
		return;
	}

	if (panel->stream_id == DP_STREAM_MAX ||
			!mgr->active_panels[panel->stream_id]) {
		DP_ERR("panel is already disabled\n");
		return;
	}

	dp_mgr_clear_dsc_resources(mgr, panel);

	DP_DEBUG("stream_id=%d, active_stream_cnt=%d, tot_dsc_blks_in_use=%d\n",
			panel->stream_id, mgr->active_stream_cnt,
			mgr->tot_dsc_blks_in_use);

	mgr->ctrl->stream_off(mgr->ctrl, panel);
	mgr->active_panels[panel->stream_id] = NULL;
	mgr->active_stream_cnt--;
}

static void dp_mgr_clean(struct dp_mgr_priv *mgr, bool skip_wait)
{
	int idx;
	struct dp_panel *panel;
	struct dp_link_hdcp_status *status = &mgr->link->hdcp_status;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);

	if (dp_mgr_state_is(DP_STATE_TUI_ACTIVE)) {
		DP_WARN("TUI is active\n");
		return;
	}

	if (dp_mgr_is_hdcp_enabled(mgr) &&
			status->hdcp_state != HDCP_STATE_INACTIVE) {
		cancel_delayed_work_sync(&mgr->hdcp_cb_work);
		if (mgr->hdcp.ops->off)
			mgr->hdcp.ops->off(mgr->hdcp.data);

		dp_mgr_update_hdcp_status(mgr, true);
	}

	for (idx = DP_STREAM_0; idx < DP_STREAM_MAX; idx++) {
		if (!mgr->active_panels[idx])
			continue;

		panel = mgr->active_panels[idx];
		if (panel->audio_supported)
			panel->audio->off(panel->audio, skip_wait);

		if (!skip_wait)
			dp_mgr_stream_pre_disable(mgr, panel);
		dp_mgr_stream_disable(mgr, panel);
		dp_mgr_clear_reservation(&mgr->client, panel->id);
		panel->deinit(panel, 0);
	}

	dp_mgr_state_remove(DP_STATE_ENABLED | DP_STATE_CONNECTED);

	mgr->ctrl->off(mgr->ctrl);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state);
}

static int dp_mgr_handle_disconnect(struct dp_mgr_priv *mgr, bool skip_wait)
{
	int rc;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);
	rc = dp_mgr_process_hpd_low(mgr, skip_wait);
	if (rc) {
		/* cancel any pending request */
		mgr->ctrl->abort(mgr->ctrl, true);
		mgr->aux->abort(mgr->aux, true);
	}

	mutex_lock(&mgr->session_lock);
	if (dp_mgr_state_is(DP_STATE_ENABLED))
		dp_mgr_clean(mgr, skip_wait);

	dp_mgr_host_unready(mgr);

	mgr->tot_lm_blks_in_use = 0;

	mutex_unlock(&mgr->session_lock);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state);
	return rc;
}

static void dp_mgr_disconnect_sync(struct dp_mgr_priv *mgr)
{
	int disconnect_delay_ms;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);
	/* cancel any pending request */
	dp_mgr_state_add(DP_STATE_ABORTED);

	mgr->ctrl->abort(mgr->ctrl, true);
	mgr->aux->abort(mgr->aux, true);

	/* wait for idle state */
	cancel_work_sync(&mgr->connect_work);
	cancel_work_sync(&mgr->attention_work);
	cancel_work_sync(&mgr->disconnect_work);
	flush_workqueue(mgr->wq);

	/*
	 * Delay the teardown of the mainlink for better interop experience.
	 * It is possible that certain sinks can issue an HPD high immediately
	 * following an HPD low as soon as they detect the mainlink being
	 * turned off. This can sometimes result in the HPD low pulse getting
	 * lost with certain cable. This issue is commonly seen when running
	 * DP LL CTS test 4.2.1.3.
	 */
	disconnect_delay_ms = min_t(u32, mgr->debug->disconnect_delay_ms,
			(u32) MAX_DISCONNECT_DELAY_MS);
	DP_DEBUG("disconnect delay = %d ms\n", disconnect_delay_ms);
	msleep(disconnect_delay_ms);

	dp_mgr_handle_disconnect(mgr, false);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state,
		disconnect_delay_ms);
}

static int dp_mgr_usbpd_disconnect_cb(void *data)
{
	int rc = 0;
	struct dp_mgr_priv *mgr;

	if (!data) {
		DP_ERR("invalid data\n");
		rc = -EINVAL;
		goto end;
	}

	mgr = data;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state,
			mgr->debug->psm_enabled);

	/* skip if a disconnect is already in progress */
	if (dp_mgr_state_is(DP_STATE_ABORTED) &&
	    dp_mgr_state_is(DP_STATE_READY)) {
		DP_DEBUG("disconnect already in progress\n");
		SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_CASE1, mgr->state);
		return 0;
	}

	if (mgr->debug->psm_enabled && dp_mgr_state_is(DP_STATE_READY))
		mgr->link->psm_config(mgr->link, &mgr->panel->link_info, true);

	mgr->ctrl->abort(mgr->ctrl, true);
	mgr->aux->abort(mgr->aux, true);

	if (!mgr->debug->sim_mode && !mgr->no_aux_switch
	    && !mgr->parser->gpio_aux_switch && mgr->aux->switch_configure)
		mgr->aux->switch_configure(mgr->aux, false, ORIENTATION_NONE);

	dp_mgr_disconnect_sync(mgr);

	mutex_lock(&mgr->session_lock);
	dp_mgr_host_deinit(mgr);
	dp_mgr_state_remove(DP_STATE_CONFIGURED);
	mutex_unlock(&mgr->session_lock);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state);
end:
	return rc;
}

static int dp_mgr_stream_enable(struct dp_mgr_priv *mgr,
			struct dp_panel *panel)
{
	int rc = 0;

	if (!mgr->client.is_cont_splash_enabled)
		rc = mgr->ctrl->stream_on(mgr->ctrl, panel);

	if (mgr->debug->tpg_pattern)
		panel->tpg_config(panel, mgr->debug->tpg_pattern);

	if (!rc) {
		mgr->active_panels[panel->stream_id] = panel;
		mgr->active_stream_cnt++;
	}


	DP_DEBUG("mgr active_stream_cnt:%d, tot_dsc_blks_in_use=%d\n",
			mgr->active_stream_cnt, mgr->tot_dsc_blks_in_use);

	return rc;
}

static void dp_mgr_mst_attention(struct dp_mgr_priv *mgr)
{
	if (mgr->mst.mst_active && mgr->mst.cbs.hpd_irq)
		mgr->mst.cbs.hpd_irq(&mgr->client);

	DP_MST_DEBUG("mst_attention_work. mst_active:%d\n", mgr->mst.mst_active);
}

static void dp_mgr_attention_work(struct work_struct *work)
{
	struct dp_mgr_priv *mgr = container_of(work,
			struct dp_mgr_priv, attention_work);
	int rc = 0;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);
	mutex_lock(&mgr->session_lock);
	SDE_EVT32_EXTERNAL(mgr->state);

	if (dp_mgr_state_is(DP_STATE_ABORTED)) {
		DP_INFO("Hpd off, not handling any attention\n");
		mutex_unlock(&mgr->session_lock);
		goto exit;
	}

	if (!dp_mgr_state_is(DP_STATE_READY)) {
		mutex_unlock(&mgr->session_lock);
		goto mst_attention;
	}

	if (mgr->link->process_request(mgr->link)) {
		mutex_unlock(&mgr->session_lock);
		goto cp_irq;
	}

	mutex_unlock(&mgr->session_lock);
	SDE_EVT32_EXTERNAL(mgr->state, mgr->link->sink_request);

	if (mgr->link->sink_request & DS_PORT_STATUS_CHANGED) {
		SDE_EVT32_EXTERNAL(mgr->state, DS_PORT_STATUS_CHANGED);
		if (!mgr->mst.mst_active) {
			if (dp_mgr_is_sink_count_zero(mgr)) {
				dp_mgr_handle_disconnect(mgr, false);
			} else {
				/*
				 * connect work should take care of sending
				 * the HPD notification.
				 */
				queue_work(mgr->wq, &mgr->connect_work);
			}
		}

		goto mst_attention;
	}

	if (mgr->link->sink_request & DP_TEST_LINK_VIDEO_PATTERN) {
		SDE_EVT32_EXTERNAL(mgr->state, DP_TEST_LINK_VIDEO_PATTERN);
		dp_mgr_handle_disconnect(mgr, false);

		mgr->panel->video_test = true;
		/*
		 * connect work should take care of sending
		 * the HPD notification.
		 */
		queue_work(mgr->wq, &mgr->connect_work);

		goto mst_attention;
	}

	if (mgr->link->sink_request & (DP_TEST_LINK_PHY_TEST_PATTERN |
		DP_TEST_LINK_TRAINING | DP_LINK_STATUS_UPDATED)) {

		mutex_lock(&mgr->session_lock);
		dp_audio_enable(mgr, false);

		if (mgr->link->sink_request & DP_TEST_LINK_PHY_TEST_PATTERN) {
			SDE_EVT32_EXTERNAL(mgr->state,
					DP_TEST_LINK_PHY_TEST_PATTERN);
			mgr->ctrl->process_phy_test_request(mgr->ctrl);
		}

		if (mgr->link->sink_request & DP_TEST_LINK_TRAINING) {
			SDE_EVT32_EXTERNAL(mgr->state, DP_TEST_LINK_TRAINING);
			mgr->link->send_test_response(mgr->link);
			rc = mgr->ctrl->link_maintenance(mgr->ctrl);
		}

		if (mgr->link->sink_request & DP_LINK_STATUS_UPDATED) {
			SDE_EVT32_EXTERNAL(mgr->state, DP_LINK_STATUS_UPDATED);
			rc = mgr->ctrl->link_maintenance(mgr->ctrl);
		}

		if (!rc)
			dp_audio_enable(mgr, true);

		mutex_unlock(&mgr->session_lock);
		if (rc)
			goto exit;

		if (mgr->link->sink_request & (DP_TEST_LINK_PHY_TEST_PATTERN |
			DP_TEST_LINK_TRAINING))
			goto mst_attention;
	}

cp_irq:
	if (dp_mgr_is_hdcp_enabled(mgr) && mgr->hdcp.ops->cp_irq)
		mgr->hdcp.ops->cp_irq(mgr->hdcp.data);

	if (!mgr->mst.mst_active) {
		/*
		 * It is possible that the connect_work skipped sending
		 * the HPD notification if the attention message was
		 * already pending. Send the notification here to
		 * account for that. It is possible that the test sequence
		 * can trigger an unplug after DP_LINK_STATUS_UPDATED, before
		 * starting the next test case. Make sure to check the HPD status.
		 */
		if (!dp_mgr_state_is(DP_STATE_ABORTED))
			dp_mgr_send_hpd_notification(mgr, false);
	}

mst_attention:
	dp_mgr_mst_attention(mgr);
exit:
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state);
}

static int dp_mgr_usbpd_attention_cb(void *data)
{
	struct dp_mgr_priv *mgr;

	if (!data) {
		DP_ERR("invalid dev\n");
		return -EINVAL;
	}

	mgr = data;

	DP_DEBUG("hpd_irq:%d, hpd_high:%d, power_on:%d, is_connected:%d\n",
			mgr->hpd->hpd_irq, mgr->hpd->hpd_high,
			!!dp_mgr_state_is(DP_STATE_ENABLED),
			!!dp_mgr_state_is(DP_STATE_CONNECTED));
	SDE_EVT32_EXTERNAL(mgr->state, mgr->hpd->hpd_irq, mgr->hpd->hpd_high,
			!!dp_mgr_state_is(DP_STATE_ENABLED),
			!!dp_mgr_state_is(DP_STATE_CONNECTED));

	if (!mgr->hpd->hpd_high) {
		dp_mgr_disconnect_sync(mgr);
		return 0;
	}

	/*
	 * Ignore all the attention messages except HPD LOW when TUI is
	 * active, so user mode can be notified of the disconnect event. This
	 * allows user mode to tear down the control path after the TUI
	 * session is over. Ideally this should never happen, but on the off
	 * chance that there is a race condition in which there is a IRQ HPD
	 * during tear down of DP at TUI start then this check might help avoid
	 * a potential issue accessing registers in attention processing.
	 */
	if (dp_mgr_state_is(DP_STATE_TUI_ACTIVE)) {
		DP_WARN("TUI is active\n");
		return 0;
	}

	if (mgr->hpd->hpd_irq && dp_mgr_state_is(DP_STATE_READY)) {
		queue_work(mgr->wq, &mgr->attention_work);
		complete_all(&mgr->attention_comp);
	} else if (mgr->process_hpd_connect ||
			 !dp_mgr_state_is(DP_STATE_CONNECTED)) {
		dp_mgr_state_remove(DP_STATE_ABORTED);
		queue_work(mgr->wq, &mgr->connect_work);
	} else {
		DP_DEBUG("ignored\n");
	}

	return 0;
}

static void dp_mgr_connect_work(struct work_struct *work)
{
	int rc = 0;
	struct dp_mgr_priv *mgr = container_of(work,
			struct dp_mgr_priv, connect_work);

	if (dp_mgr_state_is(DP_STATE_TUI_ACTIVE)) {
		dp_mgr_state_log("[TUI is active]");
		return;
	}

	if (dp_mgr_state_is(DP_STATE_ABORTED)) {
		DP_WARN("HPD off requested\n");
		return;
	}

	if (!mgr->hpd->hpd_high) {
		DP_WARN("Sink disconnected\n");
		return;
	}

	rc = dp_mgr_process_hpd_high(mgr);

	if (!rc && mgr->panel->video_test)
		mgr->link->send_test_response(mgr->link);
}

static void dp_mgr_disconnect_work(struct work_struct *work)
{
	struct dp_mgr_priv *mgr = container_of(work,
			struct dp_mgr_priv, disconnect_work);

	/*
	 * In DP simulation mode, DP link clock's parent is driven
	 * by usb pll clock, in case usb is disconnected during
	 * DP simulation. Accessing HW registers driven by DP link clock
	 * during this would trigger an exception. Hence, put xo clock as
	 * DP link clock's parent to keep the registers driven by
	 * link clock still be accessible.
	 */
	if (mgr->debug->sim_mode && dp_mgr_state_is(DP_STATE_ABORTED))
		mgr->power->park_clocks(mgr->power);

	dp_mgr_handle_disconnect(mgr, false);

	if (mgr->debug->sim_mode && dp_mgr_state_is(DP_STATE_ABORTED))
		dp_mgr_host_deinit(mgr);

	mgr->debug->abort(mgr->debug);
}

static int dp_mgr_usb_notifier(struct notifier_block *nb,
	unsigned long action, void *data)
{
	struct dp_mgr_priv *mgr = container_of(nb,
			struct dp_mgr_priv, usb_nb);

	SDE_EVT32_EXTERNAL(mgr->state, mgr->debug->sim_mode, action);
	if (!action && mgr->debug->sim_mode) {
		DP_WARN("usb disconnected during simulation\n");
		dp_mgr_state_add(DP_STATE_ABORTED);
		mgr->ctrl->abort(mgr->ctrl, true);
		mgr->aux->abort(mgr->aux, true);

		queue_work(mgr->wq, &mgr->disconnect_work);
	}

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state, NOTIFY_DONE);
	return NOTIFY_DONE;
}

static void dp_mgr_register_usb_notifier(struct dp_mgr_priv *mgr)
{
	int rc = 0;
	const char *phandle = "usb-phy";
	struct usb_phy *usbphy;

	usbphy = devm_usb_get_phy_by_phandle(&mgr->pdev->dev, phandle, 0);
	if (IS_ERR_OR_NULL(usbphy)) {
		DP_DEBUG("unable to get usbphy\n");
		return;
	}

	mgr->usb_nb.notifier_call = dp_mgr_usb_notifier;
	mgr->usb_nb.priority = 2;
	rc = usb_register_notifier(usbphy, &mgr->usb_nb);
	if (rc)
		DP_DEBUG("failed to register for usb event: %d\n", rc);
}

int dp_mgr_mmrm_callback(struct mmrm_client_notifier_data *notifier_data)
{
	struct dss_clk_mmrm_cb *mmrm_cb_data = (struct dss_clk_mmrm_cb *)notifier_data->pvt_data;
	struct dp_client *client = (struct dp_client *)mmrm_cb_data->phandle;
	struct dp_mgr_priv *mgr =
		container_of(client, struct dp_mgr_priv, client);
	int ret = 0;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state, notifier_data->cb_type);
	if (notifier_data->cb_type == MMRM_CLIENT_RESOURCE_VALUE_CHANGE
				&& dp_mgr_state_is(DP_STATE_ENABLED)
				&& !dp_mgr_state_is(DP_STATE_ABORTED)) {
		ret = dp_mgr_handle_disconnect(mgr, false);
		if (ret)
			DP_ERR("mmrm callback error reducing clk, ret:%d\n", ret);
	}

	DP_DEBUG("mmrm callback handled, state: 0x%x rc:%d\n", mgr->state, ret);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state, notifier_data->cb_type);
	return ret;
}

static void dp_mgr_deinit_sub_modules(struct dp_mgr_priv *mgr)
{
	dp_debug_put(mgr->debug);
	dp_hpd_put(mgr->hpd);

	if (mgr->panel) {
		dp_audio_put(mgr->panel->audio);

		if (!list_empty(&mgr->panel_list_head))
			list_del(&mgr->panel->list_node);
	}

	dp_ctrl_put(mgr->ctrl);
	dp_panel_put(mgr->panel);
	dp_link_put(mgr->link);
	dp_power_put(mgr->power);
	dp_pll_put(mgr->pll);
	dp_aux_put(mgr->aux);
	dp_catalog_put(mgr->catalog);
	dp_parser_put(mgr->parser);
	mutex_destroy(&mgr->session_lock);
}

static int dp_mgr_init_sub_modules(struct dp_mgr_priv *mgr)
{
	int rc = 0;
	u32 dp_core_revision = 0;
	bool hdcp_disabled;
	const char *phandle = "qcom,dp-aux-switch";
	struct device *dev = &mgr->pdev->dev;
	struct dp_hpd_cb *cb = &mgr->hpd_cb;
	struct dp_ctrl_in ctrl_in = {
		.dev = dev,
	};
	struct dp_panel_in panel_in = {
		.dev = dev,
	};
	struct dp_debug_in debug_in = {
		.dev = dev,
	};
	struct dp_pll_in pll_in = {
		.pdev = mgr->pdev,
	};

	mutex_init(&mgr->session_lock);
	mutex_init(&mgr->accounting_lock);

	mgr->parser = dp_parser_get(mgr->pdev);
	if (IS_ERR(mgr->parser)) {
		rc = PTR_ERR(mgr->parser);
		DP_ERR("failed to initialize parser, rc = %d\n", rc);
		mgr->parser = NULL;
		goto error;
	}

	mgr->parser->is_edp = mgr->client.is_edp;

	rc = mgr->parser->parse(mgr->parser);
	if (rc) {
		DP_ERR("device tree parsing failed\n");
		goto error_catalog;
	}

	mgr->client.is_mst_supported = mgr->parser->has_mst;
	mgr->client.dsc_cont_pps = mgr->parser->dsc_continuous_pps;

	mgr->client.no_backlight_support = mgr->parser->no_backlight_support;
	mgr->client.ext_hpd_en = mgr->parser->ext_hpd_en;

	mgr->catalog = dp_catalog_get(dev, mgr->parser);
	if (IS_ERR(mgr->catalog)) {
		rc = PTR_ERR(mgr->catalog);
		DP_ERR("failed to initialize catalog, rc = %d\n", rc);
		mgr->catalog = NULL;
		goto error_catalog;
	}

	mgr->catalog->hpd.set_edp_mode(&mgr->catalog->hpd, mgr->client.is_edp);
	dp_core_revision = dp_catalog_get_dp_core_version(mgr->catalog);

	mgr->aux_switch_node = of_parse_phandle(mgr->pdev->dev.of_node, phandle, 0);
	if (!mgr->aux_switch_node) {
		DP_DEBUG("cannot parse %s handle\n", phandle);
		mgr->no_aux_switch = true;
	} else {
		if (!strcmp(mgr->aux_switch_node->name, "fsa4480"))
			mgr->switch_type = DP_AUX_SWITCH_FSA4480;
		else if (!strcmp(mgr->aux_switch_node->name, "wcd939x_i2c"))
			mgr->switch_type = DP_AUX_SWITCH_WCD939x;
		else
			mgr->switch_type = DP_AUX_SWITCH_BYPASS;

		DP_ERR("aux_switch_name :%s\n", mgr->aux_switch_node->name);
	}

	mgr->aux = dp_aux_get(dev, &mgr->catalog->aux, mgr->parser,
			mgr->aux_switch_node, mgr->aux_bridge, mgr->client.dp_aux_ipc_log,
			mgr->switch_type);
	if (IS_ERR(mgr->aux)) {
		rc = PTR_ERR(mgr->aux);
		DP_ERR("failed to initialize aux, rc = %d\n", rc);
		mgr->aux = NULL;
		goto error_aux;
	}

	rc = mgr->aux->drm_aux_register(mgr->aux, mgr->client.drm_dev);
	if (rc) {
		DP_ERR("DRM DP AUX register failed\n");
		goto error_pll;
	}

	pll_in.aux = mgr->aux;
	pll_in.parser = mgr->parser;
	pll_in.dp_core_revision = dp_core_revision;

	mgr->pll = dp_pll_get(&pll_in);
	if (IS_ERR(mgr->pll)) {
		rc = PTR_ERR(mgr->pll);
		DP_ERR("failed to initialize pll, rc = %d\n", rc);
		mgr->pll = NULL;
		goto error_pll;
	}

	mgr->power = dp_power_get(mgr->parser, mgr->pll);
	if (IS_ERR(mgr->power)) {
		rc = PTR_ERR(mgr->power);
		DP_ERR("failed to initialize power, rc = %d\n", rc);
		mgr->power = NULL;
		goto error_power;
	}

	rc = mgr->power->power_client_init(mgr->power, &mgr->priv->phandle,
		mgr->client.drm_dev);
	if (rc) {
		DP_ERR("Power client create failed\n");
		goto error_link;
	}

	rc = mgr->power->power_mmrm_init(mgr->power, &mgr->priv->phandle,
		(void *)&mgr->client, dp_mgr_mmrm_callback);
	if (rc) {
		DP_ERR("failed to initialize mmrm, rc = %d\n", rc);
		goto error_link;
	}

	mgr->link = dp_link_get(dev, mgr->aux, dp_core_revision);
	if (IS_ERR(mgr->link)) {
		rc = PTR_ERR(mgr->link);
		DP_ERR("failed to initialize link, rc = %d\n", rc);
		mgr->link = NULL;
		goto error_link;
	}

	panel_in.aux = mgr->aux;
	panel_in.catalog = &mgr->catalog->panel;
	panel_in.link = mgr->link;
	panel_in.connector = mgr->client.base_connector;
	panel_in.base_panel = NULL;
	panel_in.parser = mgr->parser;

	mgr->panel = dp_panel_get(&panel_in);
	if (IS_ERR(mgr->panel)) {
		rc = PTR_ERR(mgr->panel);
		DP_ERR("failed to initialize panel, rc = %d\n", rc);
		mgr->panel = NULL;
		goto error_panel;
	}

	mgr->panel->id = 0;
	list_add_tail(&mgr->panel->list_node, &mgr->panel_list_head);

	ctrl_in.link = mgr->link;
	ctrl_in.panel = mgr->panel;
	ctrl_in.aux = mgr->aux;
	ctrl_in.power = mgr->power;
	ctrl_in.catalog = &mgr->catalog->ctrl;
	ctrl_in.parser = mgr->parser;
	ctrl_in.pll = mgr->pll;

	mgr->ctrl = dp_ctrl_get(&ctrl_in);
	if (IS_ERR(mgr->ctrl)) {
		rc = PTR_ERR(mgr->ctrl);
		DP_ERR("failed to initialize ctrl, rc = %d\n", rc);
		mgr->ctrl = NULL;
		goto error_ctrl;
	}

	mgr->panel->audio = dp_audio_get(mgr->pdev, mgr->panel,
						&mgr->catalog->audio);
	if (IS_ERR(mgr->panel->audio)) {
		rc = PTR_ERR(mgr->panel->audio);
		DP_ERR("failed to initialize audio, rc = %d\n", rc);
		mgr->panel->audio = NULL;
		goto error_audio;
	}

	memset(&mgr->mst, 0, sizeof(mgr->mst));
	mgr->active_stream_cnt = 0;

	cb->data = mgr;
	cb->configure  = dp_mgr_usbpd_configure_cb;
	cb->disconnect = dp_mgr_usbpd_disconnect_cb;
	cb->attention  = dp_mgr_usbpd_attention_cb;

	mgr->hpd = dp_hpd_get(dev, mgr->parser, &mgr->catalog->hpd,
			mgr->aux_bridge, cb);
	if (IS_ERR(mgr->hpd)) {
		rc = PTR_ERR(mgr->hpd);
		DP_ERR("failed to initialize hpd, rc = %d\n", rc);
		mgr->hpd = NULL;
		goto error_hpd;
	}

	hdcp_disabled = !!dp_mgr_initialize_hdcp(mgr);

	debug_in.panel = mgr->panel;
	debug_in.hpd = mgr->hpd;
	debug_in.link = mgr->link;
	debug_in.aux = mgr->aux;
	debug_in.connector = &mgr->client.base_connector;
	debug_in.catalog = mgr->catalog;
	debug_in.parser = mgr->parser;
	debug_in.ctrl = mgr->ctrl;
	debug_in.pll = mgr->pll;
	debug_in.client = &mgr->client;

	mgr->debug = dp_debug_get(&debug_in);
	if (IS_ERR(mgr->debug)) {
		rc = PTR_ERR(mgr->debug);
		DP_ERR("failed to initialize debug, rc = %d\n", rc);
		mgr->debug = NULL;
		goto error_debug;
	}

	mgr->tot_dsc_blks_in_use = 0;
	mgr->tot_lm_blks_in_use = 0;

	mgr->debug->hdcp_disabled = hdcp_disabled;
	dp_mgr_update_hdcp_status(mgr, true);

	dp_mgr_register_usb_notifier(mgr);

	if (mgr->hpd->register_hpd) {
		rc = mgr->hpd->register_hpd(mgr->hpd);
		if (rc) {
			DP_ERR("failed register hpd\n");
			goto error_hpd_reg;
		}
	}

	return rc;
error_hpd_reg:
	dp_debug_put(mgr->debug);
error_debug:
	dp_hpd_put(mgr->hpd);
error_hpd:
	dp_audio_put(mgr->panel->audio);
error_audio:
	dp_ctrl_put(mgr->ctrl);
error_ctrl:
	dp_panel_put(mgr->panel);
error_panel:
	dp_link_put(mgr->link);
error_link:
	dp_power_put(mgr->power);
error_power:
	dp_pll_put(mgr->pll);
error_pll:
	dp_aux_put(mgr->aux);
error_aux:
	dp_catalog_put(mgr->catalog);
error_catalog:
	dp_parser_put(mgr->parser);
error:
	mutex_destroy(&mgr->session_lock);
	return rc;
}

int dp_mgr_cont_splash_config(struct dp_client *client)
{
	int rc = 0;
	struct dp_mgr_priv *mgr;

	if (!client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);

	rc = mgr->power->init(mgr->power, false);
	if (rc) {
		DP_WARN("Power init failed.\n");
		SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_CASE1, mgr->state);
		return rc;
	}

	rc = mgr->power->clk_enable(mgr->power, DP_LINK_PM, true, true);
	if (rc) {
		DP_ERR("Unable to start link clocks\n");
		goto end;
	}

	rc = mgr->power->clk_enable(mgr->power, DP_STREAM0_PM, true, true);
	if (rc) {
		DP_ERR("Unable to start stream clocks\n");
		goto end;
	}

	client->is_cont_splash_enabled = true;

	DP_DEBUG("done\n");
	return rc;
end:
	mgr->power->deinit(mgr->power);
	return rc;
}

int dp_mgr_cont_splash_res_disable(struct dp_client *client)
{
	/* Operations to be performed in splash disabled case */
	return 0;
}

int dp_mgr_splash_res_cleanup(struct dp_client *client)
{
	client->is_cont_splash_enabled = false;

	DP_DEBUG("done\n");

	return 0;
}

static int dp_mgr_post_init(struct dp_client *client)
{
	int rc = 0;
	struct dp_mgr_priv *mgr;

	if (!client) {
		DP_ERR("invalid input\n");
		rc = -EINVAL;
		goto end;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	rc = dp_mgr_init_sub_modules(mgr);
	if (rc)
		goto end;

	client->drm_ops.post_init = NULL;
end:
	DP_DEBUG("%s\n", rc ? "failed" : "success");
	return rc;
}

struct dp_panel *dp_mgr_get_panel(struct dp_client *client, int panel_id)
{
	struct dp_panel *node;
	struct dp_mgr_priv *mgr;

	if (!client) {
		DP_ERR("invalid input\n");
		return NULL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	node = mgr->panel;
	if (!node) {
		DP_ERR("no panel found\n");
		return NULL;
	}

	list_for_each_entry(node, &mgr->panel_list_head, list_node) {
		if (node->id == panel_id)
			return node;
	}

	return NULL;
}

static int dp_mgr_set_mode(struct dp_client *client, int panel_id,
		struct dp_display_mode *mode)
{
	const u32 num_components = 3, default_bpp = 24;
	struct dp_mgr_priv *mgr;
	struct dp_panel *panel;
	bool dsc_en = (mode->capabilities & DP_PANEL_CAPS_DSC) ? true : false;

	if (!client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state,
			mode->timing.h_active, mode->timing.v_active,
			mode->timing.refresh_rate);

	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid connector input\n");
		return -EINVAL;
	}

	mutex_lock(&mgr->session_lock);
	mode->timing.bpp =
		panel->connector->display_info.bpc * num_components;
	if (!mode->timing.bpp)
		mode->timing.bpp = default_bpp;

	mode->timing.bpp = mgr->panel->get_mode_bpp(mgr->panel,
			mode->timing.bpp, mode->timing.pixel_clk_khz, dsc_en);

	if (mgr->mst.mst_active)
		mgr->mst.cbs.set_mst_mode_params(&mgr->client, mode);

	panel->pinfo = mode->timing;
	mutex_unlock(&mgr->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state);

	return 0;
}

static int dp_mgr_prepare(struct dp_client *client, int panel_id)
{
	struct dp_mgr_priv *mgr;
	struct dp_panel *panel;
	int rc = 0;
	bool shallow_mode = true;

	if (!client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);

	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid connector input\n");
		return -EINVAL;
	}

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);
	mutex_lock(&mgr->session_lock);

	/*
	 * If DP video session is restored by the userspace after display
	 * disconnect notification from dongle i.e. typeC cable connected to
	 * source but disconnected at the display side, the DP controller is
	 * not restored to the desired configured state. So, ensure host_init
	 * is executed in such a scenario so that all the DP controller
	 * resources are enabled for the next connection event.
	 */
	if (dp_mgr_state_is(DP_STATE_SRC_PWRDN) &&
			dp_mgr_state_is(DP_STATE_CONFIGURED)) {
		rc = dp_mgr_host_init(mgr);
		if (rc) {
			/*
			 * Skip all the events that are similar to abort case, just that
			 * the stream clks should be enabled so that no commit failure can
			 * be seen.
			 */
			DP_ERR("Host init failed.\n");
			goto end;
		}

		/*
		 * Remove DP_STATE_SRC_PWRDN flag on successful host_init to
		 * prevent cases such as below.
		 * 1. MST stream 1 failed to do host init then stream 2 can retry again.
		 * 2. Resume path fails, now sink sends hpd_high=0 and hpd_high=1.
		 */
		dp_mgr_state_remove(DP_STATE_SRC_PWRDN);
	}

	/*
	 * If the physical connection to the sink is already lost by the time
	 * we try to set up the connection, we can just skip all the steps
	 * here safely.
	 */
	if (dp_mgr_state_is(DP_STATE_ABORTED)) {
		dp_mgr_state_log("[aborted]");
		goto end;
	}

	/*
	 * If DP_STATE_ENABLED, there is nothing left to do.
	 * This would happen during MST flow. So, log this.
	 */
	if (dp_mgr_state_is(DP_STATE_ENABLED)) {
		dp_mgr_state_warn("[already enabled]");
		goto end;
	}

	if (!dp_mgr_is_ready(mgr)) {
		dp_mgr_state_show("[not ready]");
		goto end;
	}

	/* For supporting DP_PANEL_SRC_INITIATED_POWER_DOWN case */
	rc = dp_mgr_host_ready(mgr);
	if (rc) {
		dp_mgr_state_show("[ready failed]");
		goto end;
	}

	if (mgr->debug->psm_enabled) {
		mgr->link->psm_config(mgr->link, &mgr->panel->link_info, false);
		mgr->debug->psm_enabled = false;
	}

	/*
	 * Execute the mgr controller power on in shallow mode here.
	 * In normal cases, controller should have been powered on
	 * by now. In some cases like suspend/resume or framework
	 * reboot, we end up here without a powered on controller.
	 * Cable may have been removed in suspended state. In that
	 * case, link training is bound to fail on system resume.
	 * So, we execute in shallow mode here to do only minimal
	 * and required things.
	 * Only in case of edp , we will do complete link training
	 * and hence we set the shallow_mode to false here.
	 */

	if (client->is_edp)
		shallow_mode = false;

	rc = mgr->ctrl->on(mgr->ctrl, mgr->mst.mst_active, panel->fec_en,
			panel->dsc_en, shallow_mode,
			mgr->client.is_cont_splash_enabled);
	if (rc)
		goto end;

end:
	mutex_unlock(&mgr->session_lock);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state, rc);
	return rc;
}

static int dp_mgr_set_stream_info(struct dp_client *client,
			int panel_id, u32 strm_id, u32 start_slot,
			u32 num_slots, u32 pbn, int vcpi)
{
	int rc = 0;
	struct dp_panel *panel;
	struct dp_mgr_priv *mgr;
	const int max_slots = 64;

	if (!client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	if (strm_id >= DP_STREAM_MAX) {
		DP_ERR("invalid stream id:%d\n", strm_id);
		return -EINVAL;
	}

	if (start_slot + num_slots > max_slots) {
		DP_ERR("invalid channel info received. start:%d, slots:%d\n",
				start_slot, num_slots);
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state, strm_id,
			start_slot, num_slots);

	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid connector input\n");
		return -EINVAL;
	}

	mutex_lock(&mgr->session_lock);

	mgr->ctrl->set_mst_channel_info(mgr->ctrl, strm_id,
			start_slot, num_slots);

	panel->set_stream_info(panel, strm_id, start_slot,
			num_slots, pbn, vcpi);

	mutex_unlock(&mgr->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state, rc);

	return rc;
}

static struct dp_display_mode *dp_mgr_get_mode(struct dp_client *client,
		int panel_id)
{
	struct dp_mgr_priv *mgr;
	struct dp_panel *panel = NULL;

	if (!client) {
		DP_ERR("invalid input\n");
		return ERR_PTR(-EINVAL);
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid connector input\n");
		return ERR_PTR(-EINVAL);
	}

	return panel->get_mode(mgr->panel);
}

static int dp_mgr_enable(struct dp_client *client, int panel_id)
{
	int rc = 0;
	struct dp_mgr_priv *mgr;
	struct dp_panel *panel = NULL;

	if (!client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid connector input\n");
		return -EINVAL;
	}

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);
	mutex_lock(&mgr->session_lock);

	/*
	 * If DP_STATE_READY is not set, we should not do any HW
	 * programming.
	 */
	if (!dp_mgr_state_is(DP_STATE_READY)) {
		dp_mgr_state_show("[host not ready]");
		goto end;
	}

	/*
	 * It is possible that by the time we get call back to establish
	 * the DP pipeline e2e, the physical DP connection to the sink is
	 * already lost. In such cases, the DP_STATE_ABORTED would be set.
	 * However, it is necessary to NOT abort the display setup here so as
	 * to ensure that the rest of the system is in a stable state prior to
	 * handling the disconnect notification.
	 */
	if (dp_mgr_state_is(DP_STATE_ABORTED))
		dp_mgr_state_log("[aborted, but continue on]");

	rc = dp_mgr_stream_enable(mgr, panel);
	if (rc)
		goto end;

	if (!mgr->client.is_cont_splash_enabled) {
		/*edp backlight enable and edp pwm enable*/
		if ((client->is_edp) && (!client->no_backlight_support)) {
			rc = mgr->power->edp_panel_set_gpio(mgr->power,
				DP_GPIO_EDP_BACKLIGHT_PWR, true);
			if (rc) {
				DP_ERR("Cannot turn edp backlight power on");
				goto end;
			}

			usleep_range(99000, 100000);

			rc = mgr->power->edp_panel_set_gpio(mgr->power,
				DP_GPIO_EDP_PWM, true);
			if (rc) {
				DP_ERR("Cannot turn edp PWM on ");
				goto end;
			}
		}
	}

	dp_mgr_state_add(DP_STATE_ENABLED);
end:
	mutex_unlock(&mgr->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state, rc);
	return rc;
}

static void dp_mgr_stream_post_enable(struct dp_mgr_priv *mgr,
			struct dp_panel *panel)
{
	panel->spd_config(panel);
	panel->setup_hdr(panel, NULL, false, 0, true);
}

static int dp_mgr_post_enable(struct dp_client *client, int panel_id)
{
	struct dp_mgr_priv *mgr;
	struct dp_panel *panel;
	int rc = 0;

	if (!client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid connector input\n");
		return -EINVAL;
	}

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);
	mutex_lock(&mgr->session_lock);

	/*
	 * If DP_STATE_READY is not set, we should not do any HW
	 * programming.
	 */
	if (!dp_mgr_state_is(DP_STATE_ENABLED)) {
		dp_mgr_state_show("[not enabled]");
		goto end;
	}

	/*
	 * If the physical connection to the sink is already lost by the time
	 * we try to set up the connection, we can just skip all the steps
	 * here safely.
	 */
	if (dp_mgr_state_is(DP_STATE_ABORTED)) {
		dp_mgr_state_log("[aborted]");
		goto end;
	}

	if (!dp_mgr_is_ready(mgr) || !dp_mgr_state_is(DP_STATE_READY)) {
		dp_mgr_state_show("[not ready]");
		goto end;
	}

	if (!mgr->client.is_cont_splash_enabled) {
		dp_mgr_stream_post_enable(mgr, panel);

		if ((client->is_edp) && (!client->no_backlight_support)) {
			rc = mgr->power->edp_panel_set_gpio(mgr->power,
				DP_GPIO_EDP_BACKLIGHT_EN, true);
			if (rc)
				DP_ERR("Cannot turn edp backlight power on");
		}
	}

	cancel_delayed_work_sync(&mgr->hdcp_cb_work);
	queue_delayed_work(mgr->wq, &mgr->hdcp_cb_work, HZ);

	if (panel->audio_supported) {
		panel->audio->bw_code = mgr->link->link_params.bw_code;
		panel->audio->lane_count = mgr->link->link_params.lane_count;
		panel->audio->on(panel->audio);
	}

	mgr->aux->state &= ~DP_STATE_CTRL_POWERED_OFF;
	mgr->aux->state |= DP_STATE_CTRL_POWERED_ON;

	dp_mgr_splash_res_cleanup(&mgr->client);

	complete_all(&mgr->notification_comp);
	DP_DEBUG("display post enable complete. state: 0x%x\n", mgr->state);
end:
	mutex_unlock(&mgr->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state);
	return 0;
}

static void dp_mgr_clear_colorspaces(struct dp_client *client)
{
	struct drm_connector *connector;
	struct sde_connector *sde_conn;

	connector = client->base_connector;
	sde_conn = to_sde_connector(connector);
	sde_conn->color_enc_fmt = 0;
}

static int dp_mgr_pre_disable(struct dp_client *client, int panel_id)
{
	struct dp_mgr_priv *mgr;
	struct dp_panel *panel;
	struct dp_link_hdcp_status *status;
	int rc = 0;
	size_t i;

	if (!client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid connector input\n");
		return -EINVAL;
	}

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);
	mutex_lock(&mgr->session_lock);

	status = &mgr->link->hdcp_status;

	if (!dp_mgr_state_is(DP_STATE_ENABLED)) {
		dp_mgr_state_show("[not enabled]");
		goto end;
	}

	if ((client->is_edp) && (!client->no_backlight_support)) {
		rc = mgr->power->edp_panel_set_gpio(mgr->power, DP_GPIO_EDP_BACKLIGHT_EN, false);
		if (rc) {
			DP_ERR("Cannot turn edp backlight power off");
			goto end;
		}
	}

	dp_mgr_state_add(DP_STATE_HDCP_ABORTED);
	cancel_delayed_work_sync(&mgr->hdcp_cb_work);
	if (dp_mgr_is_hdcp_enabled(mgr) &&
			status->hdcp_state != HDCP_STATE_INACTIVE) {
		bool off = true;

		if (dp_mgr_state_is(DP_STATE_SUSPENDED)) {
			DP_DEBUG("Can't perform HDCP cleanup while suspended. Defer\n");
			mgr->hdcp_delayed_off = true;
			goto clean;
		}

		flush_delayed_work(&mgr->hdcp_cb_work);
		if (mgr->mst.mst_active) {
			dp_mgr_hdcp_deregister_stream(mgr,
				panel->stream_id);
			for (i = DP_STREAM_0; i < DP_STREAM_MAX; i++) {
				if (i != panel->stream_id &&
						mgr->active_panels[i]) {
					DP_DEBUG("Streams are still active. Skip disabling HDCP\n");
					off = false;
				}
			}
		}

		if (off) {
			if (mgr->hdcp.ops->off)
				mgr->hdcp.ops->off(mgr->hdcp.data);
			dp_mgr_update_hdcp_status(mgr, true);
		}
	}

	dp_mgr_clear_colorspaces(client);

clean:
	if (panel->audio_supported)
		panel->audio->off(panel->audio, false);

	rc = dp_mgr_stream_pre_disable(mgr, panel);

end:
	mutex_unlock(&mgr->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state);
	return 0;
}

static int dp_mgr_disable(struct dp_client *client, int panel_id)
{
	int i, rc = 0;
	struct dp_mgr_priv *mgr = NULL;
	struct dp_panel *panel = NULL;
	struct dp_link_hdcp_status *status;

	if (!client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid connector input\n");
		return -EINVAL;
	}

	status = &mgr->link->hdcp_status;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);
	mutex_lock(&mgr->session_lock);

	if (!dp_mgr_state_is(DP_STATE_ENABLED)) {
		dp_mgr_state_show("[not enabled]");
		goto end;
	}

	if (!dp_mgr_state_is(DP_STATE_READY)) {
		dp_mgr_state_show("[not ready]");
		goto end;
	}

	if ((client->is_edp) && (!client->no_backlight_support)) {
		rc = mgr->power->edp_panel_set_gpio(mgr->power, DP_GPIO_EDP_BACKLIGHT_PWR, false);
		if (rc)
			DP_ERR("Cannot turn edp backlight power off\n");

		rc = mgr->power->edp_panel_set_gpio(mgr->power, DP_GPIO_EDP_PWM, false);
		if (rc)
			DP_ERR("Cannot turn edp PWM off\n");
	}

	dp_mgr_stream_disable(mgr, panel);

	dp_mgr_state_remove(DP_STATE_HDCP_ABORTED);
	for (i = DP_STREAM_0; i < DP_STREAM_MAX; i++) {
		if (mgr->active_panels[i]) {
			if (status->hdcp_state != HDCP_STATE_AUTHENTICATED)
				queue_delayed_work(mgr->wq, &mgr->hdcp_cb_work,
						HZ/4);
			break;
		}
	}
end:
	mutex_unlock(&mgr->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state);
	return 0;
}

static int dp_request_irq(struct dp_client *client)
{
	int rc = 0;
	struct dp_mgr_priv *mgr;

	if (!client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);

	mgr->irq = irq_of_parse_and_map(mgr->pdev->dev.of_node, 0);
	if (mgr->irq < 0) {
		rc = mgr->irq;
		DP_ERR("failed to get irq: %d\n", rc);
		return rc;
	}

	rc = devm_request_irq(&mgr->pdev->dev, mgr->irq, dp_mgr_irq,
		IRQF_TRIGGER_HIGH, "dp_mgr_isr", mgr);
	if (rc < 0) {
		DP_ERR("failed to request IRQ%u: %d\n",
				mgr->irq, rc);
		return rc;
	}
	disable_irq(mgr->irq);

	return 0;
}

static int dp_mgr_unprepare(struct dp_client *client, int panel_id)
{
	struct dp_mgr_priv *mgr;
	struct dp_panel *panel = NULL;
	u32 flags = 0;
	int rc = 0;

	if (!client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid connector input\n");
		return -EINVAL;
	}

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);
	mutex_lock(&mgr->session_lock);

	/*
	 * Check if the power off sequence was triggered
	 * by a source initialated action like framework
	 * reboot or suspend-resume but not from normal
	 * hot plug. If connector is in MST mode, skip
	 * powering down host as aux needs to be kept
	 * alive to handle hot-plug sideband message.
	 */
	if (dp_mgr_is_ready(mgr) &&
		(dp_mgr_state_is(DP_STATE_SUSPENDED) ||
		!mgr->mst.mst_active))
		flags |= DP_PANEL_SRC_INITIATED_POWER_DOWN;

	if (mgr->active_stream_cnt)
		goto end;

	if (flags & DP_PANEL_SRC_INITIATED_POWER_DOWN) {
		mgr->link->psm_config(mgr->link, &mgr->panel->link_info, true);
		mgr->debug->psm_enabled = true;

		mgr->ctrl->off(mgr->ctrl);
		dp_mgr_host_unready(mgr);
		dp_mgr_host_deinit(mgr);
		dp_mgr_state_add(DP_STATE_SRC_PWRDN);
	}

	if ((client->is_edp)  && (!client->ext_hpd_en)) {
		rc = mgr->power->edp_panel_set_gpio(mgr->power, DP_GPIO_EDP_VCC_EN, false);
		if (rc)
			DP_ERR("Cannot turn edp panel power off\n");
	}
	dp_mgr_state_remove(DP_STATE_ENABLED);

	mgr->aux->state &= ~DP_STATE_CTRL_POWERED_ON;
	mgr->aux->state |= DP_STATE_CTRL_POWERED_OFF;

	complete_all(&mgr->notification_comp);

	/* log this as it results from user action of cable dis-connection */
	DP_INFO("[OK]\n");
end:
	mutex_lock(&mgr->accounting_lock);
	mgr->tot_lm_blks_in_use -= panel->max_lm;
	panel->max_lm = 0;
	mutex_unlock(&mgr->accounting_lock);
	panel->deinit(panel, flags);
	mutex_unlock(&mgr->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state);

	return 0;
}

static int dp_mgr_validate_link_clock(struct dp_mgr_priv *mgr,
		struct drm_display_mode *mode, struct dp_display_mode dp_mode)
{
	u32 mode_rate_khz = 0, supported_rate_khz = 0, mode_bpp = 0, lane_count = 0;
	u32 mode_bpc = 0, tmds_clock = 0;
	bool dsc_en;
	int rate = 0;
	struct msm_compression_info *c_info = &dp_mode.timing.comp_info;

	dsc_en = c_info->enabled;

	if (dsc_en) {
		mode_bpp = DSC_BPP(c_info->dsc_info.config);
		mode_bpc = c_info->dsc_info.config.bits_per_component;
	} else {
		mode_bpp = dp_mode.timing.bpp;
		mode_bpc = mode_bpp / 3;
	}

	if (mgr->client.is_edp) {
		rate = mgr->panel->link_info.rate;
		lane_count = mgr->panel->link_info.num_lanes;
	} else {
		rate = drm_dp_bw_code_to_link_rate(mgr->link->link_params.bw_code);
		lane_count =  mgr->link->link_params.lane_count;
	}

	mode_rate_khz = mode->clock * mode_bpp;
	tmds_clock = mode->clock * mode_bpc / 8;

	/*
	 * For a HBR 2 dongle, limit TMDS clock to ensure a max resolution
	 * of 4k@30fps for each MST port
	 */
	if (mgr->mst.mst_active && rate <= 540000 && tmds_clock > MAX_TMDS_CLOCK_HDMI_1_4) {
		DP_DEBUG("Limit mode clock: %d kHz\n", mode->clock);
		return -EPERM;
	}

	supported_rate_khz = lane_count * rate * 8;

	if (mode_rate_khz > supported_rate_khz) {
		DP_DEBUG("mode_rate: %d kHz, supported_rate: %d kHz\n",
				mode_rate_khz, supported_rate_khz);
		return -EPERM;
	}

	return 0;
}

static int dp_mgr_validate_pixel_clock(struct dp_display_mode dp_mode,
		u32 max_pclk_khz, u32 pclk_factor)
{
	u32 pclk_khz = dp_mode.timing.pixel_clk_khz;

	pclk_khz = pclk_khz / pclk_factor;
	if (pclk_khz > max_pclk_khz) {
		DP_DEBUG("clk: %d kHz, max: %d kHz\n", pclk_khz, max_pclk_khz);
		return -EPERM;
	}

	return 0;
}

static int dp_mgr_validate_topology(struct dp_mgr_priv *mgr,
		struct dp_panel *panel, struct drm_display_mode *mode,
		struct dp_display_mode *dp_mode,
		const struct msm_resource_caps_info *avail_res)
{
	int rc;
	struct msm_drm_private *priv = mgr->priv;
	const u32 dual = 2, quad = 4;
	u32 num_lm = 0, num_dsc = 0, num_3dmux = 0;
	bool dsc_capable = dp_mode->capabilities & DP_PANEL_CAPS_DSC;
	u32 fps = dp_mode->timing.refresh_rate;
	int avail_lm = 0;

	mutex_lock(&mgr->accounting_lock);

	rc = msm_get_mixer_count(priv, mode, avail_res, &num_lm);
	if (rc) {
		DP_ERR("error getting mixer count. rc:%d\n", rc);
		goto end;
	}

	/* Merge using DSC, if enabled */
	if (panel->dsc_en && dsc_capable) {
		rc = msm_get_dsc_count(priv, mode->hdisplay, &num_dsc);
		if (rc) {
			DP_ERR("error getting dsc count. rc:%d\n", rc);
			goto end;
		}

		num_dsc = max(num_lm, num_dsc);
		if ((num_dsc > avail_res->num_lm) ||  (num_dsc > avail_res->num_dsc)) {
			DP_DEBUG("mode %sx%d: not enough resources for dsc %d dsc_a:%d lm_a:%d\n",
					mode->name, fps, num_dsc, avail_res->num_dsc,
					avail_res->num_lm);
			/* Clear DSC caps and retry */
			dp_mode->capabilities &= ~DP_PANEL_CAPS_DSC;
			rc = -EAGAIN;
			goto end;
		} else {
			/* Only DSCMERGE is supported on DP */
			num_lm = num_dsc;
		}
	}

	if (!num_dsc && (num_lm == 2) && avail_res->num_3dmux)
		num_3dmux = 1;

	avail_lm = avail_res->num_lm + avail_res->num_lm_in_use - mgr->tot_lm_blks_in_use
			+ panel->max_lm;

	if (num_lm > avail_lm) {
		DP_DEBUG("mode %sx%d is invalid, not enough lm req:%d avail:%d\n",
				mode->name, fps, num_lm, avail_lm);
		rc = -EPERM;
		goto end;
	} else if (!num_dsc && (num_lm == dual && !num_3dmux)) {
		DP_DEBUG("mode %sx%d is invalid, not enough 3dmux %d %d\n",
				mode->name, fps, num_3dmux, avail_res->num_3dmux);
		rc = -EPERM;
		goto end;
	} else if (num_lm == quad && num_dsc != quad)  {
		DP_DEBUG("mode %sx%d is invalid, unsupported DP topology lm:%d dsc:%d\n",
				mode->name, fps, num_lm, num_dsc);
		rc = -EPERM;
		goto end;
	}

	DP_DEBUG_V("mode %sx%d is valid, supported DP topology lm:%d dsc:%d 3dmux:%d\n",
				mode->name, fps, num_lm, num_dsc, num_3dmux);

	dp_mode->lm_count = num_lm;
	rc = 0;

end:
	mutex_unlock(&mgr->accounting_lock);
	return rc;
}

static enum drm_mode_status dp_mgr_validate_mode(
		struct dp_client *client,
		int panel_id, struct drm_display_mode *mode,
		const struct msm_resource_caps_info *avail_res)
{
	struct dp_mgr_priv *mgr;
	struct dp_panel *panel;
	struct dp_debug *debug;
	enum drm_mode_status mode_status = MODE_BAD;
	struct dp_display_mode dp_mode;
	int rc = 0;

	if (!client || !mode ||
			!avail_res || !avail_res->max_mixer_width) {
		DP_ERR("invalid params\n");
		return mode_status;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid connector input\n");
		return mode_status;
	}

	mutex_lock(&mgr->session_lock);

	debug = mgr->debug;
	if (!debug)
		goto end;

	client->drm_ops.convert_to_dp_mode(client, panel_id, mode, &dp_mode);

	/* As per spec, 640x480 mode should always be present as fail-safe */
	if ((dp_mode.timing.h_active == 640) && (dp_mode.timing.v_active == 480) &&
			(dp_mode.timing.pixel_clk_khz == 25175)) {
		goto skip_validation;
	}

	rc = dp_mgr_validate_topology(mgr, panel, mode, &dp_mode, avail_res);
	if (rc == -EAGAIN) {
		panel->convert_to_dp_mode(panel, mode, &dp_mode);
		rc = dp_mgr_validate_topology(mgr, panel, mode, &dp_mode, avail_res);
	}

	if (rc)
		goto end;

	rc = dp_mgr_validate_link_clock(mgr, mode, dp_mode);
	if (rc)
		goto end;

	rc = dp_mgr_validate_pixel_clock(dp_mode, client->max_pclk_khz,
			panel->pclk_factor);
	if (rc)
		goto end;

skip_validation:
	mode_status = MODE_OK;

	if (!avail_res->num_lm_in_use) {
		mutex_lock(&mgr->accounting_lock);
		mgr->tot_lm_blks_in_use -= panel->max_lm;
		panel->max_lm = max(panel->max_lm, dp_mode.lm_count);
		mgr->tot_lm_blks_in_use += panel->max_lm;
		mutex_unlock(&mgr->accounting_lock);
	}

end:
	mutex_unlock(&mgr->session_lock);

	DP_DEBUG_V("[%s clk:%d] mode is %s\n", mode->name, mode->clock,
			(mode_status == MODE_OK) ? "valid" : "invalid");

	return mode_status;
}

static int dp_mgr_get_available_dp_resources(struct dp_client *client,
		const struct msm_resource_caps_info *avail_res,
		struct msm_resource_caps_info *max_dp_avail_res)
{
	if (!client || !avail_res || !max_dp_avail_res) {
		DP_ERR("invalid arguments\n");
		return -EINVAL;
	}

	memcpy(max_dp_avail_res, avail_res,
			sizeof(struct msm_resource_caps_info));

	max_dp_avail_res->num_lm = min(avail_res->num_lm,
			client->max_mixer_count);
	max_dp_avail_res->num_dsc = min(avail_res->num_dsc,
			client->max_dsc_count);

	DP_DEBUG_V("max_lm:%d, avail_lm:%d, dp_avail_lm:%d\n",
			client->max_mixer_count, avail_res->num_lm,
			max_dp_avail_res->num_lm);

	DP_DEBUG_V("max_dsc:%d, avail_dsc:%d, dp_avail_dsc:%d\n",
			client->max_dsc_count, avail_res->num_dsc,
			max_dp_avail_res->num_dsc);

	return 0;
}

static int dp_mgr_get_modes(struct dp_client *client, int panel_id,
	struct dp_display_mode *dp_mode)
{
	struct dp_panel *panel;
	struct dp_mgr_priv *mgr;
	int ret = 0;

	if (!client || !dp_mode) {
		DP_ERR("invalid params\n");
		return 0;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);

	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid connector input\n");
		return -EINVAL;
	}

	ret = panel->get_modes(panel, panel->connector, dp_mode);

	if (dp_mode->timing.pixel_clk_khz)
		mgr->client.max_pclk_khz = dp_mode->timing.pixel_clk_khz;

	return ret;
}

static void dp_mgr_convert_to_dp_mode(struct dp_client *client,
		int panel_id,
		const struct drm_display_mode *drm_mode,
		struct dp_display_mode *dp_mode)
{
	int rc;
	struct dp_mgr_priv *mgr;
	struct dp_panel *panel;
	u32 free_dsc_blks = 0, required_dsc_blks = 0, curr_dsc = 0, new_dsc = 0;

	if (!client || !drm_mode || !dp_mode) {
		DP_ERR("invalid input\n");
		return;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid connector input\n");
		return;
	}

	memset(dp_mode, 0, sizeof(*dp_mode));

	if (panel->dsc_en) {
		free_dsc_blks = client->max_dsc_count -
				mgr->tot_dsc_blks_in_use +
				panel->dsc_blks_in_use;
		DP_DEBUG_V("Before: in_use:%d, max:%d, free:%d\n",
				mgr->tot_dsc_blks_in_use,
				client->max_dsc_count, free_dsc_blks);

		rc = msm_get_dsc_count(mgr->priv, drm_mode->hdisplay,
				&required_dsc_blks);
		if (rc) {
			DP_ERR("error getting dsc count. rc:%d\n", rc);
			return;
		}

		curr_dsc = panel->dsc_blks_in_use;
		mgr->tot_dsc_blks_in_use -= panel->dsc_blks_in_use;
		panel->dsc_blks_in_use = 0;

		if (free_dsc_blks >= required_dsc_blks) {
			dp_mode->capabilities |= DP_PANEL_CAPS_DSC;
			new_dsc = max(curr_dsc, required_dsc_blks);
			panel->dsc_blks_in_use = new_dsc;
			mgr->tot_dsc_blks_in_use += new_dsc;
		}

		DP_DEBUG_V("After: in_use:%d, max:%d, free:%d, req:%d, caps:0x%x\n",
				mgr->tot_dsc_blks_in_use,
				client->max_dsc_count,
				free_dsc_blks, required_dsc_blks,
				dp_mode->capabilities);
	}

	rc = panel->convert_to_dp_mode(panel, drm_mode, dp_mode);
	if (rc == -EAGAIN)
		panel->convert_to_dp_mode(panel, drm_mode, dp_mode);
}

static int dp_mgr_config_hdr(struct dp_client *client, int panel_id,
			struct drm_msm_ext_hdr_metadata *hdr, bool dhdr_update)
{
	struct dp_panel *panel;
	struct sde_connector *sde_conn;
	struct dp_mgr_priv *mgr;
	u64 core_clk_rate;
	bool flush_hdr;

	if (!client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid connector input\n");
		return -EINVAL;
	}

	sde_conn = to_sde_connector(panel->connector);

	if (sde_cesta_is_enabled(DPUID(client->drm_dev)))
		core_clk_rate = sde_cesta_get_core_clk_rate(DPUID(client->drm_dev));
	else
		core_clk_rate = mgr->power->clk_get_rate(mgr->power, "core_clk");
	if (!core_clk_rate) {
		DP_ERR("invalid rate for core_clk\n");
		return -EINVAL;
	}

	if (!dp_mgr_state_is(DP_STATE_ENABLED)) {
		dp_mgr_state_show("[not enabled]");
		return 0;
	}

	/*
	 * In rare cases where HDR metadata is updated independently
	 * flush the HDR metadata immediately instead of relying on
	 * the colorspace
	 */
	flush_hdr = !sde_conn->colorspace_updated;

	if (flush_hdr)
		DP_DEBUG("flushing the HDR metadata\n");
	else
		DP_DEBUG("piggy-backing with colorspace\n");

	return panel->setup_hdr(panel, hdr, dhdr_update,
		core_clk_rate, flush_hdr);
}

static int dp_mgr_setup_colospace(struct dp_client *client,
		int panel_id, u32 colorspace)
{
	struct dp_panel *panel;
	struct dp_mgr_priv *mgr;

	if (!client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	panel = dp_mgr_get_panel(client, panel_id);
	if (!panel || !panel->connector) {
		DP_ERR("invalid connector input\n");
		return -EINVAL;
	}

	if (!dp_mgr_state_is(DP_STATE_ENABLED)) {
		dp_mgr_state_show("[not enabled]");
		return 0;
	}

	return panel->set_colorspace(panel, colorspace);
}

static int dp_mgr_create_workqueue(struct dp_mgr_priv *mgr)
{
	mgr->wq = create_singlethread_workqueue("drm_dp");
	if (IS_ERR_OR_NULL(mgr->wq)) {
		DP_ERR("Error creating wq\n");
		return -EPERM;
	}

	INIT_DELAYED_WORK(&mgr->hdcp_cb_work, dp_mgr_hdcp_cb_work);
	INIT_WORK(&mgr->connect_work, dp_mgr_connect_work);
	INIT_WORK(&mgr->attention_work, dp_mgr_attention_work);
	INIT_WORK(&mgr->disconnect_work, dp_mgr_disconnect_work);

	return 0;
}

static int dp_mgr_bridge_internal_hpd(void *dev, bool hpd, bool hpd_irq)
{
	struct dp_mgr_priv *mgr = dev;
	struct drm_device *drm_dev = mgr->client.drm_dev;

	if (!drm_dev || !drm_dev->mode_config.poll_enabled)
		return -EBUSY;

	if (hpd_irq)
		dp_mgr_mst_attention(mgr);
	else
		mgr->hpd->simulate_connect(mgr->hpd, hpd);

	return 0;
}

static int dp_mgr_init_aux_bridge(struct dp_mgr_priv *mgr)
{
	int rc = 0;
	const char *phandle = "qcom,dp-aux-bridge";
	struct device_node *bridge_node;

	if (!mgr->pdev->dev.of_node) {
		DP_ERR("cannot find dev.of_node\n");
		rc = -ENODEV;
		goto end;
	}

	bridge_node = of_parse_phandle(mgr->pdev->dev.of_node,
			phandle, 0);
	if (!bridge_node)
		goto end;

	mgr->aux_bridge = of_dp_aux_find_bridge(bridge_node);
	if (!mgr->aux_bridge) {
		DP_ERR("failed to find mgr aux bridge\n");
		rc = -EPROBE_DEFER;
		goto end;
	}

	if (mgr->aux_bridge->register_hpd &&
			!(mgr->aux_bridge->flag & DP_AUX_BRIDGE_HPD))
		mgr->aux_bridge->register_hpd(mgr->aux_bridge,
				dp_mgr_bridge_internal_hpd, mgr);

end:
	return rc;
}

static int dp_mgr_mst_install(struct dp_client *client,
			struct dp_mst_drm_install_info *mst_install_info)
{
	struct dp_mgr_priv *mgr;

	if (!client || !mst_install_info) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);

	if (!mst_install_info->cbs->hpd || !mst_install_info->cbs->hpd_irq) {
		DP_ERR("invalid mst cbs\n");
		return -EINVAL;
	}

	client->dp_mst_prv_info = mst_install_info->dp_mst_prv_info;

	if (!mgr->parser->has_mst) {
		DP_DEBUG("mst not enabled\n");
		return -EPERM;
	}

	memcpy(&mgr->mst.cbs, mst_install_info->cbs, sizeof(mgr->mst.cbs));
	mgr->mst.drm_registered = true;

	DP_MST_DEBUG("mgr mst drm installed\n");
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state);

	return 0;
}

static int dp_mgr_mst_uninstall(struct dp_client *client)
{
	struct dp_mgr_priv *mgr;

	if (!client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);

	if (!mgr->mst.drm_registered) {
		DP_DEBUG("drm mst not registered\n");
		return -EPERM;
	}

	mgr = container_of(client, struct dp_mgr_priv,
				client);
	memset(&mgr->mst.cbs, 0, sizeof(mgr->mst.cbs));
	mgr->mst.drm_registered = false;

	DP_MST_DEBUG("mgr mst drm uninstalled\n");
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state);

	return 0;
}

static int dp_mgr_mst_connector_install(struct dp_client *client,
		struct drm_connector *connector)
{
	int rc = 0;
	struct dp_panel_in panel_in;
	struct dp_panel *panel;
	struct dp_panel *node = NULL;
	struct dp_mgr_priv *mgr;
	struct sde_connector *sde_conn;
	int last_panel_id = 0;

	if (!client || !connector) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);
	mutex_lock(&mgr->session_lock);

	if (!mgr->mst.drm_registered) {
		DP_DEBUG("drm mst not registered\n");
		rc = -EPERM;
		goto end;
	}

	panel_in.dev = &mgr->pdev->dev;
	panel_in.aux = mgr->aux;
	panel_in.catalog = &mgr->catalog->panel;
	panel_in.link = mgr->link;
	panel_in.connector = connector;
	panel_in.base_panel = mgr->panel;
	panel_in.parser = mgr->parser;

	panel = dp_panel_get(&panel_in);
	if (IS_ERR(panel)) {
		rc = PTR_ERR(panel);
		DP_ERR("failed to initialize panel, rc = %d\n", rc);
		goto end;
	}

	/* add the newly created panel to the end of the panel list */
	list_for_each_entry(node, &mgr->panel_list_head, list_node) {
		last_panel_id = node->id;
	}

	sde_conn = to_sde_connector(connector);
	sde_conn->panel_id = panel->id = last_panel_id + 1;
	list_add_tail(&panel->list_node, &mgr->panel_list_head);

	panel->audio = dp_audio_get(mgr->pdev, panel, &mgr->catalog->audio);
	if (IS_ERR(panel->audio)) {
		rc = PTR_ERR(panel->audio);
		DP_ERR("[mst] failed to initialize audio, rc = %d\n", rc);
		panel->audio = NULL;
		goto end;
	}

	DP_MST_DEBUG("mgr mst connector installed. conn:%d\n",
			connector->base.id);

end:
	mutex_unlock(&mgr->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state, rc);

	return rc;
}

static int dp_mgr_mst_connector_uninstall(struct dp_client *client,
			struct drm_connector *connector)
{
	int rc = 0;
	struct sde_connector *sde_conn;
	struct dp_panel *panel;
	struct dp_mgr_priv *mgr;
	struct dp_audio *audio = NULL;

	if (!client || !connector) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, mgr->state);
	mutex_lock(&mgr->session_lock);

	if (!mgr->mst.drm_registered) {
		DP_DEBUG("drm mst not registered\n");
		mutex_unlock(&mgr->session_lock);
		return -EPERM;
	}

	sde_conn = to_sde_connector(connector);
	panel = dp_mgr_get_panel(client, sde_conn->panel_id);
	if (!panel) {
		DP_ERR("invalid panel for connector:%d\n", connector->base.id);
		mutex_unlock(&mgr->session_lock);
		return -EINVAL;
	}

	/* Make a copy of audio structure to call into dp_audio_put later */
	audio = panel->audio;

	if (!list_empty(&mgr->panel_list_head))
		list_del(&panel->list_node);

	dp_panel_put(panel);

	DP_MST_DEBUG("mgr mst connector uninstalled. conn:%d\n",
			connector->base.id);

	mutex_unlock(&mgr->session_lock);

	dp_audio_put(audio);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state);

	return rc;
}

static int dp_mgr_mst_connector_update_edid(struct dp_client *client,
			struct drm_connector *connector,
			struct edid *edid)
{
	int rc = 0;
	struct sde_connector *sde_conn;
	struct dp_panel *panel;
	struct dp_mgr_priv *mgr;

	if (!client || !connector || !edid) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);

	if (!mgr->mst.drm_registered) {
		DP_DEBUG("drm mst not registered\n");
		return -EPERM;
	}

	sde_conn = to_sde_connector(connector);
	panel = dp_mgr_get_panel(client, sde_conn->panel_id);
	if (!panel) {
		DP_ERR("invalid panel for connector:%d\n", connector->base.id);
		return -EINVAL;
	}

	rc = panel->update_edid(panel, edid);

	DP_MST_DEBUG("mgr mst connector:%d edid updated. mode_cnt:%d\n",
			connector->base.id, rc);

	return rc;
}

static int dp_mgr_update_pps(struct dp_client *client,
		struct drm_connector *connector, char *pps_cmd)
{
	struct sde_connector *sde_conn;
	struct dp_panel *panel;
	struct dp_mgr_priv *mgr;

	mgr = container_of(client, struct dp_mgr_priv, client);

	sde_conn = to_sde_connector(connector);
	panel = dp_mgr_get_panel(client, sde_conn->panel_id);
	if (!panel) {
		DP_ERR("invalid panel for connector:%d\n", connector->base.id);
		return -EINVAL;
	}

	if (!dp_mgr_state_is(DP_STATE_ENABLED)) {
		dp_mgr_state_show("[not enabled]");
		return 0;
	}

	panel->update_pps(panel, pps_cmd);
	return 0;
}

static int dp_mgr_mst_connector_update_link_info(
			struct dp_client *client,
			struct drm_connector *connector)
{
	int rc = 0;
	struct sde_connector *sde_conn;
	struct dp_panel *panel;
	struct dp_mgr_priv *mgr;

	if (!client || !connector) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);

	if (!mgr->mst.drm_registered) {
		DP_DEBUG("drm mst not registered\n");
		return -EPERM;
	}

	sde_conn = to_sde_connector(connector);
	panel = dp_mgr_get_panel(client, sde_conn->panel_id);
	if (!panel) {
		DP_ERR("invalid panel for connector:%d\n", connector->base.id);
		return -EINVAL;
	}

	memcpy(panel->dpcd, mgr->panel->dpcd,
			DP_RECEIVER_CAP_SIZE + 1);
	memcpy(panel->dsc_dpcd, mgr->panel->dsc_dpcd,
			DP_RECEIVER_DSC_CAP_SIZE + 1);
	memcpy(&panel->link_info, &mgr->panel->link_info,
			sizeof(panel->link_info));

	DP_MST_DEBUG("mgr mst connector:%d link info updated\n",
		connector->base.id);

	return rc;
}

static int dp_mgr_mst_get_fixed_topology_port(
			struct dp_client *client,
			u32 strm_id, u32 *port_num)
{
	struct dp_mgr_priv *mgr;
	u32 port;

	if (!client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	if (strm_id >= DP_STREAM_MAX) {
		DP_ERR("invalid stream id:%d\n", strm_id);
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);

	port = mgr->parser->mst_fixed_port[strm_id];

	if (!port || port > 255)
		return -ENOENT;

	if (port_num)
		*port_num = port;

	return 0;
}

static int dp_mgr_get_mst_caps(struct dp_client *client,
			struct dp_mst_caps *mst_caps)
{
	int rc = 0;
	struct dp_mgr_priv *mgr;

	if (!client || !mst_caps) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);

	mst_caps->has_mst = mgr->parser->has_mst;
	mst_caps->max_streams_supported = (mst_caps->has_mst) ? 2 : 0;
	mst_caps->max_dpcd_transaction_bytes = (mst_caps->has_mst) ? 16 : 0;
	mst_caps->drm_aux = mgr->aux->drm_aux;

	return rc;
}

static void dp_mgr_wakeup_phy_layer(struct dp_client *client,
		bool wakeup)
{
	struct dp_mgr_priv *mgr;
	struct dp_hpd *hpd;

	if (!client) {
		DP_ERR("invalid input\n");
		return;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);
	if (!mgr->mst.drm_registered) {
		DP_DEBUG("drm mst not registered\n");
		return;
	}

	hpd = mgr->hpd;
	if (hpd && hpd->wakeup_phy)
		hpd->wakeup_phy(hpd, wakeup);
}

static int dp_mgr_get_display_type(struct dp_client *client,
		const char **display_type)
{
	struct dp_mgr_priv *mgr;
	struct device_node *of_node;

	if (!client || !display_type) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);

	if (mgr->parser)
		*display_type = mgr->parser->display_type;
	else {
		of_node = mgr->pdev->dev.of_node;
		*display_type = of_get_property(of_node, "qcom,display-type",
					NULL);
	}
	return 0;
}

static int dp_mgr_mst_get_fixed_topology_display_type(
		struct dp_client *client, u32 strm_id,
		const char **display_type)
{
	struct dp_mgr_priv *mgr;

	if (!client || !display_type) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	if (strm_id >= DP_STREAM_MAX) {
		DP_ERR("invalid stream id:%d\n", strm_id);
		return -EINVAL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);

	*display_type = mgr->parser->mst_fixed_display_type[strm_id];

	return 0;
}

static int dp_mgr_edp_detect(struct dp_client *client)
{
	struct device *dev;
	struct dp_mgr_priv *mgr;
	int rc = 0;

	mgr = container_of(client, struct dp_mgr_priv, client);
	dev = &mgr->pdev->dev;

	if (mgr->client.is_sst_connected)
		return rc;

	mutex_lock(&mgr->session_lock);

	rc = dp_mgr_host_init(mgr);
	if (rc) {
		DP_ERR("Host init Failed");
		goto end;
	}

	mgr->client.is_sst_connected = true;
	mgr->hpd->hpd_high = true;
	mgr->hpd->alt_mode_cfg_done = true;

	mgr->client.max_pclk_khz = min(mgr->parser->max_pclk_khz,
					mgr->debug->max_pclk_khz);

	rc = dp_mgr_host_ready(mgr);
	if (rc) {
		dp_mgr_state_show("[ready failed]");
		dp_mgr_host_deinit(mgr);
		goto end;
	}

	if (!mgr->client.is_cont_splash_enabled)
		mgr->link->psm_config(mgr->link, &mgr->panel->link_info, false);

	mgr->debug->psm_enabled = false;

	rc = mgr->panel->read_sink_caps(mgr->panel,
			mgr->client.base_connector, mgr->hpd->multi_func);

	if (rc == -ETIMEDOUT || rc == -ENOTCONN)
		goto end;

	dp_mgr_state_remove(DP_STATE_ABORTED);
	dp_mgr_state_add(DP_STATE_CONFIGURED);
	dp_mgr_state_add(DP_STATE_CONNECTED);

	mgr->link->process_request(mgr->link);
	mgr->panel->handle_sink_request(mgr->panel);

	dp_mgr_state_add(DP_STATE_CONNECT_NOTIFIED);
	dp_mgr_state_remove(DP_STATE_DISCONNECT_NOTIFIED);

end:
	mutex_unlock(&mgr->session_lock);
	return rc;
}

struct dp_intf_info *dp_mgr_get_info(struct dp_client *client)
{
	struct dp_mgr_priv *mgr;

	if (!client) {
		DP_DEBUG("mgr display not initialized\n");
		return NULL;
	}

	mgr = container_of(client, struct dp_mgr_priv, client);

	return &mgr->intf_info;
}

static void dp_mgr_set_mst_state(struct dp_client *client,
		enum dp_drv_state mst_state)
{
	struct dp_mgr_priv *mgr;

	mgr = container_of(client, struct dp_mgr_priv, client);
	SDE_EVT32_EXTERNAL(mst_state, mgr->mst.mst_active);

	if (mgr->mst.mst_active && mgr->mst.cbs.set_drv_state)
		mgr->mst.cbs.set_drv_state(client, mst_state);
}

int dp_mgr_pm_prepare(struct dp_client *client)
{
	struct dp_mgr_priv *mgr;

	if (!client)
		return -EINVAL;

	mgr = container_of(client, struct dp_mgr_priv, client);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY);
	mutex_lock(&mgr->session_lock);
	dp_mgr_set_mst_state(&mgr->client, PM_SUSPEND);

	/*
	 * There are a few instances where the DP is hotplugged when the device
	 * is in PM suspend state. After hotplug, it is observed the device
	 * enters and exits the PM suspend multiple times while aux transactions
	 * are taking place. This may sometimes cause an unclocked register
	 * access error. So, abort aux transactions when such a situation
	 * arises i.e. when DP is connected but display not enabled yet.
	 */
	if (dp_mgr_state_is(DP_STATE_CONNECTED) &&
			!dp_mgr_state_is(DP_STATE_ENABLED)) {
		mgr->aux->abort(mgr->aux, true);
		mgr->ctrl->abort(mgr->ctrl, true);
	}

	dp_mgr_state_add(DP_STATE_SUSPENDED);
	mutex_unlock(&mgr->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state);

	return 0;
}

void dp_mgr_pm_complete(struct dp_client *client)
{
	struct dp_mgr_priv *mgr;

	if (!client)
		return;

	mgr = container_of(client, struct dp_mgr_priv, client);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY);
	mutex_lock(&mgr->session_lock);
	dp_mgr_set_mst_state(&mgr->client, PM_DEFAULT);

	/*
	 * There are multiple PM suspend entry and exits observed before
	 * the connect uevent is issued to userspace. The aux transactions are
	 * aborted during PM suspend entry in dp_pm_prepare to prevent unclocked
	 * register access. On PM suspend exit, there will be no host_init call
	 * to reset the abort flags for ctrl and aux incase DP is connected
	 * but display not enabled. So, resetting abort flags for aux and ctrl.
	 */
	if (dp_mgr_state_is(DP_STATE_CONNECTED) &&
			!dp_mgr_state_is(DP_STATE_ENABLED)) {
		mgr->aux->abort(mgr->aux, false);
		mgr->ctrl->abort(mgr->ctrl, false);
	}

	dp_mgr_state_remove(DP_STATE_SUSPENDED);
	mutex_unlock(&mgr->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, mgr->state);
}

void dp_mgr_post_open(struct dp_client *client)
{
}

struct dp_client *dp_mgr_init(struct platform_device *pdev)
{
	int rc = 0;
	struct dp_mgr_priv *mgr;
	struct dp_client *client;
	struct dp_client_drm_ops *drm_ops;
	struct dp_client_mst_ops *mst_ops;

	if (!pdev || !pdev->dev.of_node) {
		DP_ERR("pdev not found\n");
		rc = -ENODEV;
		goto bail;
	}

	mgr = devm_kzalloc(&pdev->dev, sizeof(*mgr), GFP_KERNEL);
	if (!mgr) {
		rc = -ENOMEM;
		goto bail;
	}

	mgr->pdev = pdev;

	init_completion(&mgr->notification_comp);
	init_completion(&mgr->attention_comp);

	INIT_LIST_HEAD(&mgr->panel_list_head);

	memset(&mgr->mst, 0, sizeof(mgr->mst));

	rc = dp_mgr_get_cell_info(mgr);
	if (rc)
		goto bail;

	rc = dp_mgr_init_aux_bridge(mgr);
	if (rc)
		goto bail;

	rc = dp_mgr_create_workqueue(mgr);
	if (rc) {
		DP_ERR("Failed to create workqueue\n");
		goto bail;
	}

	client = &mgr->client;
	drm_ops = &client->drm_ops;
	mst_ops = &client->mst_ops;

	client->dp_ipc_log = ipc_log_context_create(DRM_DP_IPC_NUM_PAGES, "drm_dp", 0);
	if (!client->dp_ipc_log)
		DP_WARN("Error in creating ipc_log_context for drm_dp\n");

	client->dp_aux_ipc_log = ipc_log_context_create(DRM_DP_IPC_NUM_PAGES,
		"drm_dp_aux", 0);

	if (!client->dp_aux_ipc_log)
		DP_WARN("Error in creating ipc_log_context for drm_dp_aux\n");

	// DRM OPS
	drm_ops->enable        = dp_mgr_enable;
	drm_ops->post_enable   = dp_mgr_post_enable;
	drm_ops->pre_disable   = dp_mgr_pre_disable;
	drm_ops->disable       = dp_mgr_disable;
	drm_ops->set_mode      = dp_mgr_set_mode;
	drm_ops->validate_mode = dp_mgr_validate_mode;
	drm_ops->get_modes     = dp_mgr_get_modes;
	drm_ops->prepare       = dp_mgr_prepare;
	drm_ops->unprepare     = dp_mgr_unprepare;
	drm_ops->request_irq   = dp_request_irq;
	drm_ops->post_open     = dp_mgr_post_open;
	drm_ops->post_init     = dp_mgr_post_init;
	drm_ops->config_hdr    = dp_mgr_config_hdr;
	drm_ops->get_display_mode = dp_mgr_get_mode;
	drm_ops->set_stream_info = dp_mgr_set_stream_info;
	drm_ops->update_pps = dp_mgr_update_pps;
	drm_ops->convert_to_dp_mode = dp_mgr_convert_to_dp_mode;
	drm_ops->set_colorspace = dp_mgr_setup_colospace;
	drm_ops->get_available_dp_resources =
					dp_mgr_get_available_dp_resources;
	drm_ops->clear_reservation = dp_mgr_clear_reservation;
	drm_ops->get_display_type = dp_mgr_get_display_type;
	drm_ops->edp_detect = dp_mgr_edp_detect;
	drm_ops->cont_splash_config = dp_mgr_cont_splash_config;
	drm_ops->cont_splash_disable = dp_mgr_cont_splash_res_disable;

	// MST OPS
	mst_ops->mst_install   = dp_mgr_mst_install;
	mst_ops->mst_uninstall = dp_mgr_mst_uninstall;
	mst_ops->mst_connector_install = dp_mgr_mst_connector_install;
	mst_ops->mst_connector_uninstall = dp_mgr_mst_connector_uninstall;
	mst_ops->mst_connector_update_edid = dp_mgr_mst_connector_update_edid;
	mst_ops->mst_connector_update_link_info =
				dp_mgr_mst_connector_update_link_info;
	mst_ops->get_mst_caps = dp_mgr_get_mst_caps;
	mst_ops->mst_get_fixed_topology_port =
					dp_mgr_mst_get_fixed_topology_port;
	mst_ops->wakeup_phy_layer = dp_mgr_wakeup_phy_layer;
	mst_ops->get_mst_pbn_div = dp_mgr_get_mst_pbn_div;
	mst_ops->mst_get_fixed_topology_display_type =
				dp_mgr_mst_get_fixed_topology_display_type;

	// BASE OPS
	client->bind = dp_mgr_bind;
	client->unbind = dp_mgr_unbind;
	client->get_intf_info = dp_mgr_get_info;
	client->pm_prepare = dp_mgr_pm_prepare;
	client->pm_complete = dp_mgr_pm_complete;

	client->is_edp = (dp_info.display_type == DRM_MODE_CONNECTOR_eDP)
						? true : false;

	return client;
bail:
	return ERR_PTR(rc);
}

int dp_mgr_deinit(struct platform_device *pdev)
{
	int rc = 0;
	struct dp_mgr_priv *mgr;

	if (!pdev) {
		rc = -EINVAL;
		goto end;
	}

	mgr = platform_get_drvdata(pdev);

	dp_mgr_deinit_sub_modules(mgr);

	if (mgr->wq)
		destroy_workqueue(mgr->wq);

	platform_set_drvdata(pdev, NULL);

	if (mgr->client.dp_ipc_log) {
		ipc_log_context_destroy(mgr->client.dp_ipc_log);
		mgr->client.dp_ipc_log = NULL;
	}

	if (mgr->client.dp_aux_ipc_log) {
		ipc_log_context_destroy(mgr->client.dp_aux_ipc_log);
		mgr->client.dp_aux_ipc_log = NULL;
	}

end:
	return rc;
}
