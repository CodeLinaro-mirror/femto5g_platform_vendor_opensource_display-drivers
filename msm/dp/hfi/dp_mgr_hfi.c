// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/component.h>

#include "msm_drv.h"
#include "hfi_msm_drv.h"
#include "msm_gem.h"
#include "msm_mmu.h"
#include "sde_kms.h"
#include "hfi_kms.h"
#include "dp_mgr_hfi.h"
#include "dp_drv.h"
#include "dp_client.h"
#include "dp_hfi.h"
#include "dp_debug.h"
#include "sde_connector.h"
#include "sde_dbg.h"

struct dp_mgr_hfi_priv {
	char *name;
	struct platform_device *pdev;
	struct dp_client client;
	struct msm_drm_private *priv;
	struct dp_hfi *hfi;
	struct dp_intf_info intf_info;
};

static int dp_mgr_hfi_set_mode(struct dp_client *client, int panel_id, struct dp_display_mode *mode)
{
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;
	struct hfi_client_t *hfi_client;
	struct hfi_display_mode_info *hfi_mode_info;
	struct dp_mgr_hfi_priv *hfi_priv;
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_SET_MODE;
	int rc = 0;

	if (!client || !mode) {
		DP_ERR("Invalid params\n");
		return -EINVAL;
	}

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);

	DP_DEBUG("HFI set mode for panel_id: %d\n", panel_id);

	sde_kms = sde_connector_get_kms(client->base_connector);
	if (!sde_kms)
		return -EINVAL;

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms)
		return -EINVAL;

	hfi_client = &hfi_kms->hfi_client;

	hfi_mode_info = kvzalloc(sizeof(struct hfi_display_mode_info), GFP_KERNEL);
	if (!hfi_mode_info)
		return -ENOMEM;

	hfi_mode_info->size =			sizeof(struct hfi_display_mode_info);
	hfi_mode_info->h_active =		mode->timing.h_active;
	hfi_mode_info->h_back_porch =		mode->timing.h_back_porch;
	hfi_mode_info->h_sync_width =		mode->timing.h_sync_width;
	hfi_mode_info->h_front_porch =		mode->timing.h_front_porch;
	hfi_mode_info->h_skew =			mode->timing.h_skew;
	hfi_mode_info->h_sync_polarity =	mode->timing.h_active_low ? 0 : 1;
	hfi_mode_info->v_active =		mode->timing.v_active;
	hfi_mode_info->v_back_porch =		mode->timing.v_back_porch;
	hfi_mode_info->v_sync_width =		mode->timing.v_sync_width;
	hfi_mode_info->v_front_porch =		mode->timing.v_front_porch;
	hfi_mode_info->v_sync_polarity =	mode->timing.v_active_low ? 0 : 1;
	hfi_mode_info->refresh_rate =		mode->timing.refresh_rate;
	hfi_mode_info->flags_lo =		0; /* DP specific flags if needed */

	rc = dp_hfi_send_cmd_buf(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
			HFI_PAYLOAD_TYPE_U32_ARRAY, hfi_mode_info, hfi_mode_info->size,
			(HFI_HOST_FLAGS_NON_DISCARDABLE));
	if (rc)
		DP_ERR("Could not send HFI_COMMAND_DISPLAY_SET_MODE, rc=%d\n", rc);
	kfree(hfi_mode_info);

	return rc;
}

static enum drm_mode_status dp_mgr_hfi_validate_mode(struct dp_client *client, int panel_id,
		struct drm_display_mode *mode, const struct msm_resource_caps_info *avail_res)
{
	if (!client || !mode) {
		DP_ERR("Invalid params\n");
		return MODE_ERROR;
	}

	DP_DEBUG("HFI validate mode for panel_id: %d\n", panel_id);
	/* For now, accept all modes - can add validation logic later */
	/* avail_res parameter can be used for resource validation if needed */
	return MODE_OK;
}

static int dp_mgr_hfi_get_modes(struct dp_client *client, int panel_id,
		struct dp_display_mode *dp_mode)
{
	if (!client) {
		DP_ERR("Invalid params\n");
		return 0;
	}

	DP_DEBUG("HFI get modes for panel_id: %d\n", panel_id);
	/* Return 0 modes for now - can add mode detection logic later */
	/* dp_mode parameter can be used to return mode information if needed */
	return 0;
}

static int dp_mgr_hfi_request_irq(struct dp_client *client)
{
	if (!client) {
		DP_ERR("Invalid params\n");
		return -EINVAL;
	}

	DP_DEBUG("HFI request IRQ\n");
	/* No IRQ handling needed for HFI implementation */
	return 0;
}

static void dp_mgr_hfi_post_open(struct dp_client *client)
{
	if (!client) {
		DP_ERR("Invalid params\n");
		return;
	}

	DP_DEBUG("HFI post open\n");
}

static int dp_mgr_hfi_post_init(struct dp_client *client)
{
	struct dp_mgr_hfi_priv *hfi_priv;
	int rc = 0;

	if (!client) {
		DP_ERR("Invalid params\n");
		return -EINVAL;
	}

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);

	/* Call dp_hfi_setup to initialize HFI interface */
	hfi_priv->hfi = dp_hfi_setup(client);
	if (IS_ERR(hfi_priv->hfi)) {
		rc = PTR_ERR(hfi_priv->hfi);
		DP_ERR("dp_hfi_setup failed: %d\n", rc);
		hfi_priv->hfi = NULL;
		goto end;
	}
end:
	return rc;
}

static int dp_mgr_hfi_config_hdr(struct dp_client *client, int panel_id,
		struct drm_msm_ext_hdr_metadata *hdr_meta, bool dhdr_update)
{
	if (!client) {
		DP_ERR("Invalid params\n");
		return -EINVAL;
	}

	DP_DEBUG("HFI config HDR for panel_id: %d, dhdr_update: %d\n", panel_id, dhdr_update);
	/* HDR configuration can be added later if needed */
	return 0;
}

static struct dp_display_mode *dp_mgr_hfi_get_display_mode(struct dp_client *client, int panel_id)
{
	if (!client) {
		DP_ERR("Invalid params\n");
		return NULL;
	}

	DP_DEBUG("HFI get display mode for panel_id: %d\n", panel_id);
	/* Return current mode - can be implemented later */
	return NULL;
}

int dp_mgr_hfi_prepare(struct dp_client *client, int panel_id)
{
	int rc = 0;

	if (!client) {
		DP_ERR("Invalid params\n");
		goto end;
	}

	DP_DEBUG("HFI prepare for panel_id: %d\n", panel_id);

	/* Mode setting will be handled by the set_mode callback when needed */
	/* For now, just return success as preparation is complete */

end:
	DP_DEBUG("%s: DP core power prepare\n", __func__);
	return rc;
}

int dp_mgr_hfi_enable(struct dp_client *client, int panel_id)
{
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;
	struct hfi_client_t *hfi_client;
	struct dp_mgr_hfi_priv *hfi_priv;
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_ENABLE;
	int rc = 0;

	if (!client) {
		DP_ERR("Invalid params\n");
		return -EINVAL;
	}

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);

	DP_DEBUG("HFI enable for panel_id: %d\n", panel_id);

	sde_kms = sde_connector_get_kms(client->base_connector);
	if (!sde_kms)
		return -EINVAL;

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms)
		return -EINVAL;

	hfi_client = &hfi_kms->hfi_client;

	rc = dp_hfi_send_cmd_buf(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
			HFI_PAYLOAD_TYPE_NONE, NULL, 0,
			(HFI_HOST_FLAGS_NON_DISCARDABLE));
	if (rc)
		DP_ERR("Could not send HFI_COMMAND_DISPLAY_ENABLE, rc=%d\n", rc);

	return rc;
}

int dp_mgr_hfi_post_enable(struct dp_client *client, int panel_id)
{
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;
	struct hfi_client_t *hfi_client;
	struct dp_mgr_hfi_priv *hfi_priv;
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_POST_ENABLE;
	int rc = 0;

	if (!client) {
		DP_ERR("Invalid params\n");
		return -EINVAL;
	}

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);

	DP_DEBUG("HFI post enable for panel_id: %d\n", panel_id);

	sde_kms = sde_connector_get_kms(client->base_connector);
	if (!sde_kms)
		return -EINVAL;

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms)
		return -EINVAL;

	hfi_client = &hfi_kms->hfi_client;

	rc = dp_hfi_send_cmd_buf(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
			HFI_PAYLOAD_TYPE_NONE, NULL, 0,
			(HFI_HOST_FLAGS_NON_DISCARDABLE));
	if (rc)
		DP_ERR("Could not send HFI_COMMAND_DISPLAY_POST_ENABLE, rc=%d\n", rc);

	return rc;
}

int dp_mgr_hfi_pre_disable(struct dp_client *client, int panel_id)
{
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;
	struct hfi_client_t *hfi_client;
	struct dp_mgr_hfi_priv *hfi_priv;
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_DISABLE;
	int rc = 0;

	if (!client) {
		DP_ERR("Invalid params\n");
		return -EINVAL;
	}

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);

	DP_DEBUG("HFI pre disable for panel_id: %d\n", panel_id);

	sde_kms = sde_connector_get_kms(client->base_connector);
	if (!sde_kms)
		return -EINVAL;

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms)
		return -EINVAL;

	hfi_client = &hfi_kms->hfi_client;

	rc = dp_hfi_send_cmd_buf(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
			HFI_PAYLOAD_TYPE_NONE, NULL, 0,
			(HFI_HOST_FLAGS_NON_DISCARDABLE));
	if (rc)
		DP_ERR("Could not send HFI_COMMAND_DISPLAY_DISABLE, rc=%d\n", rc);

	return rc;
}

int dp_mgr_hfi_disable(struct dp_client *client, int panel_id)
{
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;
	struct hfi_client_t *hfi_client;
	struct dp_mgr_hfi_priv *hfi_priv;
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_POST_DISABLE;
	int rc = 0;

	if (!client) {
		DP_ERR("Invalid params\n");
		return -EINVAL;
	}

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);

	DP_DEBUG("HFI disable for panel_id: %d\n", panel_id);

	sde_kms = sde_connector_get_kms(client->base_connector);
	if (!sde_kms)
		return -EINVAL;

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms)
		return -EINVAL;

	hfi_client = &hfi_kms->hfi_client;

	rc = dp_hfi_send_cmd_buf(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
			HFI_PAYLOAD_TYPE_NONE, NULL, 0,
			(HFI_HOST_FLAGS_NON_DISCARDABLE));
	if (rc)
		DP_ERR("Could not send HFI_COMMAND_DISPLAY_POST_DISABLE, rc=%d\n", rc);

	return rc;
}

int dp_mgr_hfi_unprepare(struct dp_client *client, int panel_id)
{
	int rc = 0;

	if (!client) {
		DP_ERR("Invalid params\n");
		goto end;
	}

	DP_DEBUG("HFI unprepare for panel_id: %d\n", panel_id);

end:
	DP_DEBUG("%s: DP core power unprepare\n", __func__);
	return rc;
}

static int dp_mgr_hfi_bind(struct device *dev, struct device *master,
		struct dp_client *client)
{
	int rc = 0;
	struct dp_mgr_hfi_priv *hfi_priv;
	struct drm_device *drm;
	struct platform_device *pdev = to_platform_device(dev);

	if (!dev || !pdev || !master) {
		DP_ERR("invalid param(s), dev %pK, pdev %pK, master %pK\n",
				dev, pdev, master);
		rc = -EINVAL;
		goto end;
	}

	drm = dev_get_drvdata(master);
	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);
	if (!drm || !hfi_priv) {
		DP_ERR("invalid param(s), drm %pK, hfi_priv %pK\n",
				drm, hfi_priv);
		rc = -EINVAL;
		goto end;
	}

	hfi_priv->client.drm_dev = drm;
	hfi_priv->priv = drm->dev_private;
end:
	return rc;
}

static void dp_mgr_hfi_unbind(struct device *dev, struct device *master,
		struct dp_client *client)
{
	struct dp_mgr_hfi_priv *hfi_priv;
	struct platform_device *pdev = to_platform_device(dev);

	if (!dev || !pdev) {
		DP_ERR("invalid param(s)\n");
		return;
	}

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);
	if (!hfi_priv) {
		DP_ERR("Invalid params\n");
		return;
	}
}

static struct dp_intf_info *dp_mgr_hfi_get_info(struct dp_client *client)
{
	struct dp_mgr_hfi_priv *mgr;

	if (!client) {
		DP_DEBUG("mgr display not initialized\n");
		return NULL;
	}

	mgr = container_of(client, struct dp_mgr_hfi_priv, client);

	return &mgr->intf_info;
}

static int dp_mgr_hfi_pm_prepare(struct dp_client *client)
{
	return 0;
}

static void dp_mgr_hfi_pm_complete(struct dp_client *client)
{
}

static void dp_mgr_hfi_convert_to_dp_mode(struct dp_client *client,
		int panel_id,
		const struct drm_display_mode *drm_mode,
		struct dp_display_mode *dp_mode)
{
}

static int dp_mgr_hfi_set_stream_info(struct dp_client *client,
			int panel_id, u32 strm_id, u32 start_slot,
			u32 num_slots, u32 pbn, int vcpi)
{
	return 0;
}

static int dp_mgr_hfi_update_pps(struct dp_client *client,
		struct drm_connector *connector, char *pps_cmd)
{
	return 0;
}

static int dp_mgr_hfi_setup_colospace(struct dp_client *client,
		int panel_id, u32 colorspace)
{
	return 0;
}

static int dp_mgr_hfi_get_available_dp_resources(struct dp_client *client,
		const struct msm_resource_caps_info *avail_res,
		struct msm_resource_caps_info *max_dp_avail_res)
{
	return 0;
}

static void dp_mgr_hfi_clear_reservation(struct dp_client *client, int panel_id)
{
}

static int dp_mgr_hfi_get_display_type(struct dp_client *client,
		const char **display_type)
{
	return 0;
}

static int dp_mgr_hfi_edp_detect(struct dp_client *client)
{
	return 0;
}

struct dp_client *dp_mgr_hfi_init(struct platform_device *pdev)
{
	int rc = 0;
	struct dp_mgr_hfi_priv *hfi_priv;
	struct dp_client *client;
	struct dp_client_drm_ops *drm_ops;

	if (!pdev || !pdev->dev.of_node) {
		DP_ERR("pdev not found\n");
		rc = -ENODEV;
		goto bail;
	}

	hfi_priv = devm_kzalloc(&pdev->dev, sizeof(*hfi_priv), GFP_KERNEL);
	if (!hfi_priv) {
		rc = -ENOMEM;
		goto bail;
	}

	hfi_priv->intf_info.stream_cnt = 2;

	hfi_priv->pdev = pdev;

	client = &hfi_priv->client;
	drm_ops = &client->drm_ops;

	/* Setup HFI-specific DRM operations */
	drm_ops->enable        = dp_mgr_hfi_enable;
	drm_ops->post_enable   = dp_mgr_hfi_post_enable;
	drm_ops->pre_disable   = dp_mgr_hfi_pre_disable;
	drm_ops->disable       = dp_mgr_hfi_disable;
	drm_ops->prepare       = dp_mgr_hfi_prepare;
	drm_ops->unprepare     = dp_mgr_hfi_unprepare;

	/* Link remaining function pointers to HFI implementations */
	drm_ops->set_mode      = dp_mgr_hfi_set_mode;
	drm_ops->validate_mode = dp_mgr_hfi_validate_mode;
	drm_ops->get_modes     = dp_mgr_hfi_get_modes;
	drm_ops->request_irq   = dp_mgr_hfi_request_irq;
	drm_ops->post_open     = dp_mgr_hfi_post_open;
	drm_ops->post_init     = dp_mgr_hfi_post_init;
	drm_ops->config_hdr    = dp_mgr_hfi_config_hdr;
	drm_ops->get_display_mode = dp_mgr_hfi_get_display_mode;
	drm_ops->set_stream_info = dp_mgr_hfi_set_stream_info;
	drm_ops->update_pps = dp_mgr_hfi_update_pps;
	drm_ops->convert_to_dp_mode = dp_mgr_hfi_convert_to_dp_mode;
	drm_ops->set_colorspace = dp_mgr_hfi_setup_colospace;
	drm_ops->get_available_dp_resources = dp_mgr_hfi_get_available_dp_resources;
	drm_ops->clear_reservation = dp_mgr_hfi_clear_reservation;
	drm_ops->get_display_type = dp_mgr_hfi_get_display_type;
	drm_ops->edp_detect = dp_mgr_hfi_edp_detect;

	client->bind = dp_mgr_hfi_bind;
	client->unbind = dp_mgr_hfi_unbind;
	client->get_intf_info = dp_mgr_hfi_get_info;
	client->pm_prepare = dp_mgr_hfi_pm_prepare;
	client->pm_complete = dp_mgr_hfi_pm_complete;

	DP_INFO("DP HFI display initialized successfully\n");
	return client;
bail:
	return ERR_PTR(rc);
}
