// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>
#include <linux/version.h>

#include "msm_drv.h"
#include "msm_kms.h"
#include "sde_connector.h"
#include "dp_drm.h"
#include "dp_mst_drm.h"
#include "dp_debug_client.h"
#include "dp_client.h"

#define DP_MST_DEBUG(fmt, ...) DP_DEBUG(fmt, ##__VA_ARGS__)

#define to_dp_bridge(x)     container_of((x), struct dp_bridge, base)

void convert_to_drm_mode(const struct dp_display_mode *dp_mode,
				struct drm_display_mode *drm_mode)
{
	u32 flags = 0;

	memset(drm_mode, 0, sizeof(*drm_mode));

	drm_mode->hdisplay = dp_mode->timing.h_active;
	drm_mode->hsync_start = drm_mode->hdisplay +
				dp_mode->timing.h_front_porch;
	drm_mode->hsync_end = drm_mode->hsync_start +
			      dp_mode->timing.h_sync_width;
	drm_mode->htotal = drm_mode->hsync_end + dp_mode->timing.h_back_porch;
	drm_mode->hskew = dp_mode->timing.h_skew;

	drm_mode->vdisplay = dp_mode->timing.v_active;
	drm_mode->vsync_start = drm_mode->vdisplay +
				dp_mode->timing.v_front_porch;
	drm_mode->vsync_end = drm_mode->vsync_start +
			      dp_mode->timing.v_sync_width;
	drm_mode->vtotal = drm_mode->vsync_end + dp_mode->timing.v_back_porch;

	drm_mode->clock = dp_mode->timing.pixel_clk_khz;

	if (dp_mode->timing.h_active_low)
		flags |= DRM_MODE_FLAG_NHSYNC;
	else
		flags |= DRM_MODE_FLAG_PHSYNC;

	if (dp_mode->timing.v_active_low)
		flags |= DRM_MODE_FLAG_NVSYNC;
	else
		flags |= DRM_MODE_FLAG_PVSYNC;

	drm_mode->flags = flags;

	drm_mode->type = 0x48;
	drm_mode_set_name(drm_mode);
}

#if (KERNEL_VERSION(6, 16, 0) > LINUX_VERSION_CODE)
static int dp_bridge_attach(struct drm_bridge *dp_bridge,
				enum drm_bridge_attach_flags flags)
#else
static int dp_bridge_attach(struct drm_bridge *dp_bridge,
		struct drm_encoder *encoder, enum drm_bridge_attach_flags flags)
#endif
{
	struct dp_bridge *bridge = to_dp_bridge(dp_bridge);

	if (!dp_bridge) {
		DP_ERR("Invalid params\n");
		return -EINVAL;
	}

	DP_DEBUG("[%d] attached\n", bridge->id);

	return 0;
}

static void dp_bridge_pre_enable(struct drm_bridge *drm_bridge)
{
	int rc = 0;
	struct dp_bridge *bridge;
	struct dp_drv *drv;
	struct dp_client_drm_ops *ops;

	if (!drm_bridge) {
		DP_ERR("Invalid params\n");
		return;
	}

	bridge = to_dp_bridge(drm_bridge);
	drv = bridge->drv;

	if (!bridge->connector) {
		DP_ERR("Invalid connector\n");
		return;
	}

	if (!drv || !drv->client) {
		DP_ERR("no dp client found\n");
		return;
	}

	ops = &drv->client->drm_ops;

	DP_DEBUG("SET MODE:: x: %d, y: %d, fps: %d\n",
		bridge->dp_mode.timing.h_active,
		bridge->dp_mode.timing.v_active,
		bridge->dp_mode.timing.refresh_rate);

	/* By this point mode should have been validated through mode_fixup */
	rc = ops->set_mode(drv->client, bridge->panel_id, &bridge->dp_mode);
	if (rc) {
		DP_ERR("[%d] failed to perform a mode set, rc=%d\n",
		       bridge->id, rc);
		return;
	}

	rc = ops->prepare(drv->client, bridge->panel_id);
	if (rc) {
		DP_ERR("[%d] DP display prepare failed, rc=%d\n",
		       bridge->id, rc);
		return;
	}

	/* for SST force stream id, start slot and total slots to 0 */
	ops->set_stream_info(drv->client, bridge->panel_id, 0, 0, 0, 0, 0);

	rc = ops->enable(drv->client, bridge->panel_id);
	if (rc)
		DP_ERR("[%d] DP display enable failed, rc=%d\n",
		       bridge->id, rc);
}

static void dp_bridge_enable(struct drm_bridge *drm_bridge)
{
	int rc = 0;
	struct dp_bridge *bridge;
	struct dp_drv *drv;
	struct dp_client_drm_ops *ops;

	if (!drm_bridge) {
		DP_ERR("Invalid params\n");
		return;
	}

	bridge = to_dp_bridge(drm_bridge);
	if (!bridge->connector) {
		DP_ERR("Invalid connector\n");
		return;
	}

	drv = bridge->drv;

	if (!drv || !drv->client) {
		DP_ERR("no dp client found\n");
		return;
	}

	ops = &drv->client->drm_ops;

	rc = ops->post_enable(drv->client, bridge->panel_id);
	if (rc)
		DP_ERR("[%d] DP display post enable failed, rc=%d\n",
		       bridge->id, rc);
}

static void dp_bridge_disable(struct drm_bridge *drm_bridge)
{
	int rc = 0;
	struct dp_bridge *bridge;
	struct dp_drv *drv;
	struct dp_client_drm_ops *ops;

	if (!drm_bridge) {
		DP_ERR("Invalid params\n");
		return;
	}

	bridge = to_dp_bridge(drm_bridge);
	if (!bridge->connector) {
		DP_ERR("Invalid connector\n");
		return;
	}

	drv = bridge->drv;
	if (!drv || !drv->client) {
		DP_ERR("no dp client found\n");
		return;
	}

	ops = &drv->client->drm_ops;

	sde_connector_helper_bridge_disable(bridge->connector);

	rc = ops->pre_disable(drv->client, bridge->panel_id);
	if (rc) {
		DP_ERR("[%d] DP display pre disable failed, rc=%d\n",
		       bridge->id, rc);
	}
}

static void dp_bridge_post_disable(struct drm_bridge *drm_bridge)
{
	int rc = 0;
	struct dp_bridge *bridge;
	struct dp_drv *drv;
	struct dp_client_drm_ops *ops;

	if (!drm_bridge) {
		DP_ERR("Invalid params\n");
		return;
	}

	bridge = to_dp_bridge(drm_bridge);
	if (!bridge->connector) {
		DP_ERR("Invalid connector\n");
		return;
	}

	drv = bridge->drv;
	if (!drv || !drv->client) {
		DP_ERR("no dp client found\n");
		return;
	}

	ops = &drv->client->drm_ops;

	rc = ops->disable(drv->client, bridge->panel_id);
	if (rc) {
		DP_ERR("[%d] DP display disable failed, rc=%d\n",
		       bridge->id, rc);
		return;
	}

	rc = ops->unprepare(drv->client, bridge->panel_id);
	if (rc) {
		DP_ERR("[%d] DP display unprepare failed, rc=%d\n",
		       bridge->id, rc);
		return;
	}
}

static void dp_bridge_mode_set(struct drm_bridge *drm_bridge,
				const struct drm_display_mode *mode,
				const struct drm_display_mode *adjusted_mode)
{
	struct dp_bridge *bridge;
	struct dp_drv *drv;
	struct dp_client_drm_ops *ops;

	if (!drm_bridge || !mode || !adjusted_mode) {
		DP_ERR("Invalid params\n");
		return;
	}

	bridge = to_dp_bridge(drm_bridge);
	if (!bridge->connector) {
		DP_ERR("Invalid connector\n");
		return;
	}

	drv = bridge->drv;
	if (!drv || !drv->client) {
		DP_ERR("no dp client found\n");
		return;
	}

	ops = &drv->client->drm_ops;
	ops->convert_to_dp_mode(drv->client, bridge->panel_id,
		adjusted_mode, &bridge->dp_mode);

	ops->clear_reservation(drv->client, bridge->panel_id);
}

static bool dp_bridge_mode_fixup(struct drm_bridge *drm_bridge,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	bool ret = true;
	struct dp_display_mode dp_mode;
	struct dp_bridge *bridge;
	struct dp_drv *drv;
	struct dp_client_drm_ops *ops;

	if (!drm_bridge || !mode || !adjusted_mode) {
		DP_ERR("Invalid params\n");
		ret = false;
		goto end;
	}

	bridge = to_dp_bridge(drm_bridge);
	if (!bridge->connector) {
		DP_ERR("Invalid connector\n");
		ret = false;
		goto end;
	}

	drv = bridge->drv;
	if (!drv || !drv->client) {
		DP_ERR("no dp client found\n");
		ret = false;
		goto end;
	}

	ops = &drv->client->drm_ops;

	ops->convert_to_dp_mode(drv->client, bridge->panel_id, mode, &dp_mode);
	ops->clear_reservation(drv->client, bridge->panel_id);
	convert_to_drm_mode(&dp_mode, adjusted_mode);
end:
	return ret;
}

static const struct drm_bridge_funcs dp_bridge_ops = {
	.attach       = dp_bridge_attach,
	.mode_fixup   = dp_bridge_mode_fixup,
	.pre_enable   = dp_bridge_pre_enable,
	.enable       = dp_bridge_enable,
	.disable      = dp_bridge_disable,
	.post_disable = dp_bridge_post_disable,
	.mode_set     = dp_bridge_mode_set,
};

int dp_connector_add_custom_mode(struct drm_connector *conn, struct dp_display_mode *dp_mode)
{
	struct drm_display_mode *m, drm_mode;

	memset(&drm_mode, 0x0, sizeof(drm_mode));
	convert_to_drm_mode(dp_mode, &drm_mode);
	m = drm_mode_duplicate(conn->dev, &drm_mode);
	if (!m) {
		DP_ERR("failed to add mode %ux%u\n", drm_mode.hdisplay, drm_mode.vdisplay);
		return 0;
	}
	m->width_mm = conn->display_info.width_mm;
	m->height_mm = conn->display_info.height_mm;
	drm_mode_probed_add(conn, m);

	return 1;
}

void init_failsafe_mode(struct dp_display_mode *dp_mode)
{
	static const struct dp_panel_info fail_safe = {
		.h_active = 640,
		.v_active = 480,
		.h_back_porch = 48,
		.h_front_porch = 16,
		.h_sync_width = 96,
		.h_active_low = 1,
		.v_back_porch = 33,
		.v_front_porch = 10,
		.v_sync_width = 2,
		.v_active_low = 1,
		.h_skew = 0,
		.refresh_rate = 60,
		.pixel_clk_khz = 25175,
		.bpp = 24,
		.widebus_en = true,
	};

	memcpy(&dp_mode->timing, &fail_safe, sizeof(fail_safe));
}

int dp_connector_config_hdr(struct drm_connector *connector, void *display,
	struct sde_connector_state *c_state)
{
	struct dp_drv *drv = display;
	struct sde_connector *sde_conn;
	struct dp_client_drm_ops *ops;

	if (!display || !c_state || !connector) {
		DP_ERR("invalid params\n");
		return -EINVAL;
	}

	if (!drv->client) {
		DP_ERR("no dp client found\n");
		return -EINVAL;
	}

	ops = &drv->client->drm_ops;

	sde_conn = to_sde_connector(connector);

	return ops->config_hdr(drv->client, sde_conn->panel_id, &c_state->hdr_meta,
			c_state->dyn_hdr_meta.dynamic_hdr_update);
}

int dp_connector_set_colorspace(struct drm_connector *connector,
	void *display)
{
	struct dp_drv *drv = display;
	struct sde_connector *sde_conn;
	struct dp_client_drm_ops *ops;

	if (!drv || !connector)
		return -EINVAL;

	sde_conn = to_sde_connector(connector);

	ops = &drv->client->drm_ops;
	return ops->set_colorspace(drv->client,
		sde_conn->panel_id, connector->state->colorspace);
}

int dp_connector_post_init(struct drm_connector *connector, void *display)
{
	int rc;
	struct dp_drv *drv = display;
	struct sde_connector *sde_conn;
	struct dp_client_drm_ops *ops;
	struct msm_drm_private *priv;

	priv = connector->dev->dev_private;

	if (!drv || !connector || !drv->client || !drv->client->bridge) {
		DP_ERR("Invalid data\n");
		return -EINVAL;
	}

	drv->client->base_connector = connector;
	drv->client->bridge->connector = connector;

	ops = &drv->client->drm_ops;
	rc = ops->post_init(drv->client);
	if (rc)
		goto end;

	sde_conn = to_sde_connector(connector);
	drv->client->bridge->panel_id = sde_conn->panel_id;

	if (IS_DISP_OP_HWIO(priv->disp_op))
		rc = dp_mst_init(drv);

	if (drv->client->dsc_cont_pps)
		sde_conn->ops.update_pps = NULL;

end:
	return rc;
}

int dp_connector_ctl_init(void *display, void *hfi_priv)
{
	int rc = 0;
	struct dp_drv *drv = display;
	struct dp_client_drm_ops *ops;

	if (!drv) {
		DP_ERR("Invalid data\n");
		return -EINVAL;
	}

	ops = &drv->client->drm_ops;
	if (ops->ctl_init) {
		rc = ops->ctl_init(drv->client);
		if (rc)
			goto end;
	}
end:
	return rc;
}

int dp_connector_get_mode_info(struct drm_connector *connector,
		const struct drm_display_mode *drm_mode,
		struct msm_sub_mode *sub_mode,
		struct msm_mode_info *mode_info,
		void *display, const struct msm_resource_caps_info *avail_res)
{
	const u32 single_intf = 1;
	const u32 no_enc = 0;
	struct msm_display_topology *topology;
	struct sde_connector *sde_conn;
	struct dp_display_mode dp_mode;
	struct dp_drv *drv = display;
	struct msm_drm_private *priv;
	struct msm_resource_caps_info avail_dp_res;
	struct dp_client_drm_ops *ops;
	struct dp_display_mode *mode;
	int rc = 0;

	if (!drm_mode || !mode_info || !avail_res ||
			!avail_res->max_mixer_width || !connector || !display ||
			!connector->dev || !connector->dev->dev_private) {
		DP_ERR("invalid params\n");
		return -EINVAL;
	}

	memset(mode_info, 0, sizeof(*mode_info));
	ops = &drv->client->drm_ops;

	sde_conn = to_sde_connector(connector);
	mode = ops->get_display_mode(drv->client, sde_conn->panel_id);
	if (!mode) {
		DP_ERR("invalid panel\n");
		return -EINVAL;
	}

	priv = connector->dev->dev_private;

	topology = &mode_info->topology;

	rc = ops->get_available_dp_resources(drv->client, avail_res,
			&avail_dp_res);
	if (rc) {
		DP_ERR("error getting max dp resources. rc:%d\n", rc);
		return rc;
	}

	rc = msm_get_mixer_count(priv, drm_mode, &avail_dp_res,
			&topology->num_lm);
	if (rc) {
		DP_ERR("error getting mixer count. rc:%d\n", rc);
		return rc;
	}
	/* reset dp connector lm_mask for every connection event and
	 * this will get re-populated in resource manager based on
	 * resolution and topology of dp display.
	 */
	sde_conn->lm_mask = 0;

	topology->num_enc = no_enc;
	topology->num_intf = single_intf;

	mode_info->frame_rate = drm_mode_vrefresh(drm_mode);
	mode_info->vtotal = drm_mode->vtotal;

	mode_info->wide_bus_en = mode->widebus_en;
	mode_info->pclk_factor = mode->pclk_factor;

	drv->client->drm_ops.convert_to_dp_mode(drv->client, sde_conn->panel_id,
		drm_mode, &dp_mode);

	if (dp_mode.timing.comp_info.enabled) {
		memcpy(&mode_info->comp_info,
			&dp_mode.timing.comp_info,
			sizeof(mode_info->comp_info));

		topology->num_enc = topology->num_lm;
		topology->comp_type = mode_info->comp_info.comp_type;
	}

	return 0;
}

int dp_connector_get_info(struct drm_connector *connector,
		struct msm_display_info *info, void *data)
{
	struct dp_drv *drv = data;
	const char *display_type = NULL;
	u32 conn_disp_type = SDE_CONNECTOR_PRIMARY;
	struct dp_client_drm_ops *ops;

	if (!info || !drv || !drv->drm_dev) {
		DP_ERR("invalid params\n");
		return -EINVAL;
	}

	info->intf_type = DRM_MODE_CONNECTOR_DisplayPort;

	ops = &drv->client->drm_ops;
	ops->get_display_type(drv->client, &display_type);
	if (display_type) {
		if (!strcmp(display_type, "primary")) {
			if (drv->client->ctl_op_sync) {
				info->ctl_op_sync = true;
				info->is_master = true;
			}
			conn_disp_type = SDE_CONNECTOR_PRIMARY;
		} else if (!strcmp(display_type, "secondary")) {
			if (drv->client->ctl_op_sync) {
				info->ctl_op_sync = true;
				info->is_master = false;
			}
			conn_disp_type = SDE_CONNECTOR_SECONDARY;
		}
	}

	info->num_of_h_tiles = 1;
	info->h_tile_instance[0] = 0;
	info->is_connected = drv->client->is_sst_connected;
	info->curr_panel_mode = MSM_DISPLAY_VIDEO_MODE;
	info->capabilities = MSM_DISPLAY_CAP_VID_MODE | MSM_DISPLAY_CAP_EDID;

	if (drv && drv->client->is_edp) {
		info->intf_type = DRM_MODE_CONNECTOR_eDP;
		info->display_type = conn_disp_type;
		if (drv->client->ext_hpd_en)
			info->capabilities |= MSM_DISPLAY_CAP_HOT_PLUG;
		else
			info->is_connected = true;
	} else {
		info->capabilities |= MSM_DISPLAY_CAP_HOT_PLUG;
	}

	return 0;
}

enum drm_connector_status dp_connector_detect(struct drm_connector *conn,
		bool force,
		void *display)
{
	enum drm_connector_status status = connector_status_unknown;
	struct msm_display_info info;
	struct dp_drv *drv;
	int rc;

	if (!conn || !display)
		return status;

	drv = display;
	/* get display dp_info */
	memset(&info, 0x0, sizeof(info));
	rc = dp_connector_get_info(conn, &info, display);
	if (rc) {
		DP_ERR("failed to get display info, rc=%d\n", rc);
		return connector_status_disconnected;
	}

	if (info.capabilities & MSM_DISPLAY_CAP_HOT_PLUG &&
			!drv->client->is_cont_splash_enabled) {
		status = (info.is_connected ? connector_status_connected :
					      connector_status_disconnected);
	} else {
		status = connector_status_connected;

		rc = drv->client->drm_ops.edp_detect(drv->client);
		if (rc) {
			DP_ERR("error in turning on panel power sequence rc:%d\n", rc);
			return connector_status_unknown;
		}
	}
	conn->display_info.width_mm = info.width_mm;
	conn->display_info.height_mm = info.height_mm;

	return status;
}

void dp_connector_post_open(struct drm_connector *connector, void *display)
{
	struct dp_drv *drv;
	struct dp_client_drm_ops *ops;

	if (!display) {
		DP_ERR("invalid input\n");
		return;
	}

	drv = display;
	if (!drv || !drv->client) {
		DP_ERR("no dp client found\n");
		return;
	}

	ops = &drv->client->drm_ops;

	if (ops->post_open)
		ops->post_open(drv->client);
}

int dp_connector_atomic_check(struct drm_connector *connector,
	void *display,
	struct drm_atomic_state *a_state)
{
	struct sde_connector *sde_conn;
	struct drm_connector_state *old_state;
	struct drm_connector_state *c_state;

	if (!connector || !display || !a_state)
		return -EINVAL;

	c_state = drm_atomic_get_new_connector_state(a_state, connector);
	old_state =
		drm_atomic_get_old_connector_state(a_state, connector);

	if (!old_state || !c_state)
		return -EINVAL;

	sde_conn = to_sde_connector(connector);

	/*
	 * Marking the colorspace has been changed
	 * the flag shall be checked in the pre_kickoff
	 * to configure the new colorspace in HW
	 */
	if (c_state->colorspace != old_state->colorspace) {
		DP_DEBUG("colorspace has been updated\n");
		sde_conn->colorspace_updated = true;
	}

	return 0;
}

int dp_connector_get_modes(struct drm_connector *connector,
		void *display, const struct msm_resource_caps_info *avail_res)
{
	int rc = 0;
	struct dp_drv *drv;
	struct dp_display_mode *dp_mode = NULL;
	struct sde_connector *sde_conn;
	struct dp_client_drm_ops *ops;

	if (!connector || !display)
		return 0;

	sde_conn = to_sde_connector(connector);
	drv = display;

	if (!drv || !drv->client) {
		DP_ERR("no dp client found\n");
		return -EINVAL;
	}

	ops = &drv->client->drm_ops;

	dp_mode = kzalloc(sizeof(*dp_mode),  GFP_KERNEL);
	if (!dp_mode)
		return 0;

	/* pluggable case assumes EDID is read when HPD */
	if (drv->client->is_sst_connected) {
		/*
		 * 1. for test request, rc = 1, and dp_mode will have test mode populated
		 * 2. During normal operation, dp_mode will be untouched
		 *    a. if mode query succeeds rc >= 0, valid modes will be added to connector
		 *    b. if edid read failed, then connector mode list will be empty and rc <= 0
		 */
		rc = ops->get_modes(drv->client, sde_conn->panel_id, dp_mode);
		if (!rc) {
			DP_WARN("failed to get DP sink modes, adding failsafe");
			init_failsafe_mode(dp_mode);
		}
		if (dp_mode->timing.pixel_clk_khz) /* valid DP mode */
			rc = dp_connector_add_custom_mode(connector, dp_mode);
	} else {
		DP_ERR("No sink connected\n");
	}
	kfree(dp_mode);

	return rc;
}

int dp_connector_set_info_blob(struct drm_connector *connector,
		void *info, void *display, struct msm_mode_info *mode_info)
{
	struct dp_drv *drv = display;
	const char *display_type = NULL;
	struct dp_client_drm_ops *ops;

	ops = &drv->client->drm_ops;
	ops->get_display_type(drv->client, &display_type);
	sde_kms_info_add_keystr(info, "display type", display_type);

	if ((drv->client->is_edp) && (drv->client->ext_hpd_en))
		sde_kms_info_add_keystr(info, "ext bridge hpd support", "true");

	if (drv->client->ctl_op_sync) {
		sde_kms_info_add_keystr(info, "has_disp_in_other_core", "true");
		sde_kms_info_add_keystr(info, "dpu_ctl_op_sync", "true");
	}

	return 0;
}

int dp_drm_bridge_init(void *data, struct drm_encoder *encoder,
	u32 max_mixer_count, u32 max_dsc_count)
{
	int rc = 0;
	struct dp_bridge *bridge;
	struct drm_device *dev;
	struct dp_drv *drv = data;
	struct msm_drm_private *priv = NULL;

	dev = drv->drm_dev;
	priv = dev->dev_private;
#if (KERNEL_VERSION(6, 16, 0) > LINUX_VERSION_CODE)
	bridge = kzalloc(sizeof(*bridge), GFP_KERNEL);
	if (!bridge) {
		rc = -ENOMEM;
		goto error;
	}
	bridge->base.funcs = &dp_bridge_ops;
	bridge->base.encoder = encoder;
#else
	bridge = __devm_drm_bridge_alloc(dev->dev,
				sizeof(*bridge),
				offsetof(struct dp_bridge, base),
				&dp_bridge_ops);
	if (IS_ERR(bridge)) {
		rc = PTR_ERR(bridge);
		DP_ERR("failed to alloc bridge, rc=%d\n", rc);
		goto error;
	}
#endif
	bridge->drv = drv;

	rc = drm_bridge_attach(encoder, &bridge->base, NULL, 0);
	if (rc) {
		DP_ERR("failed to attach bridge, rc=%d\n", rc);
		goto error_free_bridge;
	}

	rc = drv->client->drm_ops.request_irq(drv->client);
	if (rc) {
		DP_ERR("request_irq failed, rc=%d\n", rc);
		goto error;
	}

	priv->bridges[priv->num_bridges++] = &bridge->base;
	drv->client->bridge = bridge;
	drv->client->max_mixer_count = max_mixer_count;
	drv->client->max_dsc_count = max_dsc_count;

	return 0;

error_free_bridge:
#if (KERNEL_VERSION(6, 16, 0) > LINUX_VERSION_CODE)
	kfree(bridge);
#endif
error:
	return rc;
}

void dp_drm_bridge_deinit(void *data)
{
#if (KERNEL_VERSION(6, 16, 0) > LINUX_VERSION_CODE)
	struct dp_drv *drv = data;
	struct dp_bridge *bridge = drv->client->bridge;

	kfree(bridge);
#endif
}

enum drm_mode_status dp_connector_mode_valid(struct drm_connector *connector,
		struct drm_display_mode *mode, void *display,
		const struct msm_resource_caps_info *avail_res)
{
	int rc = 0, vrefresh;
	struct dp_drv *drv;
	struct sde_connector *sde_conn;
	struct msm_resource_caps_info avail_dp_res;
	struct dp_client_drm_ops *ops;
	struct dp_display_mode *dp_mode;

	if (!mode || !display || !connector) {
		DP_ERR("invalid params\n");
		return MODE_ERROR;
	}

	sde_conn = to_sde_connector(connector);

	drv = display;
	ops = &drv->client->drm_ops;
	dp_mode = ops->get_display_mode(drv->client, sde_conn->panel_id);
	if (!dp_mode) {
		DP_ERR("invalid panel\n");
		return MODE_ERROR;
	}

	vrefresh = drm_mode_vrefresh(mode);

	rc = drv->client->drm_ops.get_available_dp_resources(drv->client, avail_res,
			&avail_dp_res);
	if (rc) {
		DP_ERR("error getting max dp resources. rc:%d\n", rc);
		return MODE_ERROR;
	}

	/* As per spec, failsafe mode should always be present */
	if ((mode->hdisplay == 640) && (mode->vdisplay == 480) && (mode->clock == 25175))
		goto validate_mode;

	if (dp_mode->mode_override &&
			(dp_mode->override_timing.h_active != mode->hdisplay ||
			 dp_mode->override_timing.v_active != mode->vdisplay ||
			 dp_mode->override_timing.refresh_rate !=  vrefresh ||
			 dp_mode->override_timing.aspect_ratio != mode->picture_aspect_ratio))
		return MODE_BAD;
	else if (dp_mode->mode_override)
		mode->type |= DRM_MODE_TYPE_PREFERRED;

validate_mode:
	return drv->client->drm_ops.validate_mode(drv->client, sde_conn->panel_id,
			mode, &avail_dp_res);
}

int dp_connector_update_pps(struct drm_connector *connector,
		char *pps_cmd, void *display)
{
	struct dp_drv *drv;

	if (!display || !connector) {
		DP_ERR("invalid params\n");
		return -EINVAL;
	}

	drv = display;
	return drv->client->drm_ops.update_pps(drv->client, connector, pps_cmd);
}

int dp_connector_install_properties(void *display, struct drm_connector *conn)
{
	struct dp_drv *drv = display;
	struct drm_connector *base_conn;
	int rc;

	if (!display || !conn) {
		DP_ERR("invalid params\n");
		return -EINVAL;
	}

	base_conn = drv->client->base_connector;

	/*
	 * Create the property on the base connector during probe time and then
	 * attach the same property onto new connector objects created for MST
	 */
	if (!base_conn->colorspace_property) {
		/* This is the base connector. create the drm property */
#if (KERNEL_VERSION(6, 3, 0) <= LINUX_VERSION_CODE)
		rc = drm_mode_create_dp_colorspace_property(base_conn, 0);
#else
		rc = drm_mode_create_dp_colorspace_property(base_conn);
#endif
		if (rc)
			return rc;
	} else {
		conn->colorspace_property = base_conn->colorspace_property;
	}

	drm_object_attach_property(&conn->base, conn->colorspace_property, 0);

	return 0;
}

int dp_connector_cont_splash_config(void *display)
{
	struct dp_drv *drv;
	int rc = 0;

	if (!display) {
		DP_ERR("invalid params\n");
		return -EINVAL;
	}

	drv = display;
	if (drv->client && drv->client->drm_ops.cont_splash_config)
		rc = drv->client->drm_ops.cont_splash_config(drv->client);

	return rc;
}

int dp_connector_cont_splash_res_disable(void *display)
{
	struct dp_drv *drv;
	int rc = 0;

	if (!display) {
		DP_ERR("invalid params\n");
		return -EINVAL;
	}

	drv = display;
	if (drv->client && drv->client->drm_ops.cont_splash_disable)
		rc = drv->client->drm_ops.cont_splash_disable(drv->client);

	return rc;
}
