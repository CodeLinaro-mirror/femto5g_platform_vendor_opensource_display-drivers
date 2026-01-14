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
#include "dp_hpd.h"
#include "sde_connector.h"
#include "sde_dbg.h"
#include "hfi_commands_device.h"

struct dp_mgr_hfi_priv {
	char *name;
	struct platform_device *pdev;
	struct dp_client client;
	struct msm_drm_private *priv;
	struct dp_hfi *hfi;
	struct dp_intf_info intf_info;
	struct dp_hpd *hpd;
	struct dp_hpd_cb hpd_cb;

	struct hfi_shared_addr_map *edid_addr_map;
	struct hfi_shared_addr_map *modes_addr_map;
};

struct hfi_shared_addr_map *dp_mgr_hfi_init_shared_addr(struct hfi_client_t *ctx, u32 size)
{
	struct hfi_shared_addr_map *map = kvzalloc(sizeof(struct hfi_shared_addr_map), GFP_KERNEL);
	int rc;

	if (!map)
		return NULL;

	map->size = size;

	rc = hfi_adapter_buffer_alloc(ctx, map);
	if (rc) {
		kfree(map);
		return NULL;
	}

	return map;
}

void dp_mgr_init_deinit_shared_addr(struct hfi_client_t *ctx, struct hfi_shared_addr_map *map)
{
	if (!ctx || !map)
		return;

	hfi_adapter_buffer_dealloc(ctx, map);
	kfree(map);
}

void dp_mgr_hfi_init_hfi_buff(struct hfi_buff *buff, struct hfi_shared_addr_map *map)
{
	u64 remote_addr;

	if (!buff || !map)
		return;

	remote_addr = (u64)map->remote_addr;

	buff->addr_l = HFI_VAL_L32(remote_addr);
	buff->addr_h = HFI_VAL_H32(remote_addr);
	buff->size = map->size;
}

static struct hfi_client_t *dp_mgr_hfi_get_hfi_client(struct dp_mgr_hfi_priv *hfi_priv)
{
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;

	/* Get HFI client from the client structure */
	if (!hfi_priv->client.base_connector) {
		DP_ERR("Invalid base connector\n");
		goto err;
	}

	sde_kms = sde_connector_get_kms(hfi_priv->client.base_connector);
	if (!sde_kms) {
		DP_ERR("Failed to get SDE KMS\n");
		goto err;
	}

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms) {
		DP_ERR("Failed to get HFI KMS\n");
		goto err;
	}

	return &hfi_kms->hfi_client;
err:
	return NULL;
}

static int dp_mgr_hfi_send_hot_plug(struct dp_mgr_hfi_priv *hfi_priv,
		struct hfi_device_hotplug_config *config)
{
	struct hfi_client_t *hfi_client;
	struct hfi_buff edid_buf = {0};
	struct hfi_buff modes_buf = {0};
	struct hfi_device_hotplug_info payload = {0};
	u32 hfi_cmd = HFI_COMMAND_DEVICE_HOT_PLUG_DETECT;
	int rc = 0;

	hfi_client = dp_mgr_hfi_get_hfi_client(hfi_priv);
	if (!hfi_client)
		return -EINVAL;

	dp_mgr_hfi_init_hfi_buff(&edid_buf, hfi_priv->edid_addr_map);
	dp_mgr_hfi_init_hfi_buff(&modes_buf, hfi_priv->modes_addr_map);

	payload.config = *config;
	payload.edid_buf = edid_buf;
	payload.modes_buf = modes_buf;

	/* Send HFI_COMMAND_DEVICE_HOT_PLUG_DETECT command with config as payload */
	rc = dp_hfi_send_cmd_buf(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
			HFI_PAYLOAD_TYPE_U32_ARRAY, (void *)&payload, sizeof(payload),
			(HFI_HOST_FLAGS_NON_DISCARDABLE));
	if (rc) {
		DP_ERR("Could not send HFI_COMMAND_DEVICE_HOT_PLUG_DETECT, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static void dp_mgr_hfi_update_config(struct dp_mgr_hfi_priv *hfi_priv,
		struct hfi_device_hotplug_config *config)
{
	if (!hfi_priv || !hfi_priv->hpd || !config)
		return;

	config->orientation = hfi_priv->hpd->orientation;
	config->port_index = hfi_priv->hpd->port_id;
	config->pin_config = hfi_priv->hpd->pin_config;
	config->hpd_state = hfi_priv->hpd->hpd_high;
	config->hpd_irq = hfi_priv->hpd->hpd_irq;

	DP_DEBUG("orientation=%u, port=%u, pin=%u, hpd=%u, irq=%u\n",
		config->orientation, config->port_index, config->pin_config,
		config->hpd_state, config->hpd_irq);
}

/* HPD callback functions */
static int dp_mgr_hfi_hpd_configure_cb(void *data)
{
	struct dp_mgr_hfi_priv *hfi_priv = data;
	struct hfi_client_t *hfi_client;
	struct hfi_device_hotplug_config config = {0};

	if (!hfi_priv) {
		DP_ERR("Invalid hfi_priv data\n");
		return -EINVAL;
	}

	hfi_client = dp_mgr_hfi_get_hfi_client(hfi_priv);
	if (!hfi_client)
		return -EINVAL;

	dp_mgr_hfi_update_config(hfi_priv, &config);

	hfi_priv->edid_addr_map = dp_mgr_hfi_init_shared_addr(hfi_client, SZ_4K);
	if (!hfi_priv->edid_addr_map) {
		DP_ERR("failed to allocate remote address for edid\n");
		return -ENOMEM;
	}

	hfi_priv->modes_addr_map = dp_mgr_hfi_init_shared_addr(hfi_client, SZ_4K);
	if (!hfi_priv->modes_addr_map) {
		DP_ERR("failed to allocate remote address for modes\n");
		dp_mgr_init_deinit_shared_addr(hfi_client, hfi_priv->edid_addr_map);
		return -ENOMEM;
	}

	return dp_mgr_hfi_send_hot_plug(hfi_priv, &config);
}

static int dp_mgr_hfi_hpd_disconnect_cb(void *data)
{
	struct dp_mgr_hfi_priv *hfi_priv = data;
	struct hfi_device_hotplug_config config = {0};
	struct hfi_client_t *hfi_client;

	if (!hfi_priv) {
		DP_ERR("Invalid hfi_priv data\n");
		return -EINVAL;
	}

	hfi_client = dp_mgr_hfi_get_hfi_client(hfi_priv);
	if (!hfi_client)
		return -EINVAL;

	dp_mgr_hfi_update_config(hfi_priv, &config);

	dp_mgr_hfi_send_hot_plug(hfi_priv, &config);

	/* Clean up shared address maps with null checks */
	if (hfi_priv->edid_addr_map) {
		dp_mgr_init_deinit_shared_addr(hfi_client, hfi_priv->edid_addr_map);
		hfi_priv->edid_addr_map = NULL;
	}
	if (hfi_priv->modes_addr_map) {
		dp_mgr_init_deinit_shared_addr(hfi_client, hfi_priv->modes_addr_map);
		hfi_priv->modes_addr_map = NULL;
	}

	return 0;
}

static int dp_mgr_hfi_hpd_attention_cb(void *data)
{
	struct dp_mgr_hfi_priv *hfi_priv = data;
	struct hfi_device_hotplug_config config;

	if (!hfi_priv) {
		DP_ERR("Invalid hfi_priv data\n");
		return -EINVAL;
	}

	dp_mgr_hfi_update_config(hfi_priv, &config);

	return dp_mgr_hfi_send_hot_plug(hfi_priv, &config);
}

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

	/* Initialize HPD callback structure */
	hfi_priv->hpd_cb.data = hfi_priv;
	hfi_priv->hpd_cb.configure = dp_mgr_hfi_hpd_configure_cb;
	hfi_priv->hpd_cb.disconnect = dp_mgr_hfi_hpd_disconnect_cb;
	hfi_priv->hpd_cb.attention = dp_mgr_hfi_hpd_attention_cb;

	/* Call dp_hpd_get with NULL for parser, catalog and aux_bridge, but pass callback */
	hfi_priv->hpd = dp_hpd_get(&hfi_priv->pdev->dev,
		NULL, NULL, NULL, &hfi_priv->hpd_cb);
	if (IS_ERR(hfi_priv->hpd)) {
		rc = PTR_ERR(hfi_priv->hpd);
		DP_ERR("dp_hpd_get failed: %d\n", rc);
		hfi_priv->hpd = NULL;
		/* Clean up HFI resources on error */
		if (hfi_priv->hfi && hfi_priv->hfi->hfi_client) {
			hfi_adapter_deinit(hfi_priv->hfi->hfi_client);
			kfree(hfi_priv->hfi->hfi_client);
		}
		if (hfi_priv->hfi) {
			kfree(hfi_priv->hfi);
			hfi_priv->hfi = NULL;
		}
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
	struct hfi_client_t *hfi_client;

	if (!dev || !pdev) {
		DP_ERR("invalid param(s)\n");
		return;
	}

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);
	if (!hfi_priv) {
		DP_ERR("Invalid params\n");
		return;
	}

	/* Clean up any remaining shared address maps */
	hfi_client = dp_mgr_hfi_get_hfi_client(hfi_priv);
	if (hfi_client) {
		if (hfi_priv->edid_addr_map) {
			dp_mgr_init_deinit_shared_addr(hfi_client, hfi_priv->edid_addr_map);
			hfi_priv->edid_addr_map = NULL;
		}
		if (hfi_priv->modes_addr_map) {
			dp_mgr_init_deinit_shared_addr(hfi_client, hfi_priv->modes_addr_map);
			hfi_priv->modes_addr_map = NULL;
		}
	}

	/* Clean up HPD and HFI resources */
	if (hfi_priv->hpd) {
		dp_hpd_put(hfi_priv->hpd);
		hfi_priv->hpd = NULL;
	}

	if (hfi_priv->hfi) {
		if (hfi_priv->hfi->hfi_client) {
			hfi_adapter_deinit(hfi_priv->hfi->hfi_client);
			kfree(hfi_priv->hfi->hfi_client);
		}
		kfree(hfi_priv->hfi);
		hfi_priv->hfi = NULL;
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
