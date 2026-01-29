// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/component.h>
#include <linux/clk.h>

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
#include "sde_edid_parser.h"
#include "hfi_commands_device.h"
#include "dp_aux_switch.h"
#include "hfi_defs_display.h"
#include "dp_panel_tu.h"

#define DRM_DP_IPC_NUM_PAGES 10
#define HPD_STRING_SIZE	    30
#define MAX_MODES           32

struct dpcd_info {
	u32 lane_count;
	u32 link_rate_khz;
	u32 pclk_factor;
	bool fec_en;
};

struct dp_mgr_hfi_priv {
	char *name;
	struct platform_device *pdev;
	struct dp_client client;
	struct msm_drm_private *priv;
	struct dp_hfi *hfi;
	struct dp_intf_info intf_info;
	struct dp_hpd *hpd;
	struct dp_hpd_cb hpd_cb;
	struct dpcd_info dpcd;

	struct hfi_shared_addr_map *edid_addr_map;
	struct hfi_shared_addr_map *modes_addr_map;

	struct dp_aux_switch *aux_switch;
	struct dp_debug_client *debug;
	struct dp_display_mode default_mode;

	struct sde_edid_ctrl *edid_ctrl;
	struct hfi_display_mode_info mode_list[MAX_MODES];
	u32 mode_count;
	u32 link_rate;
	u32 lane_count;

	struct device *pd_dp_phy_gdsc;
	struct clk *usb3_tcsr_clk;
	struct clk *usb3_pipe_clk;

	bool connected;
	bool configured;
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

/**
 * _hfi_process_edid - Process raw EDID data from DCP and send to usermode
 * @hfi_priv: HFI private data structure
 * @raw_edid_addr: Pointer to raw EDID buffer from DCP
 * @buffer_size: Size of the EDID buffer
 *
 * This function parses the raw EDID buffer received from DCP via HFI,
 * validates it, and updates the connector to expose it to userspace.
 *
 * Return: 0 on success, negative error code on failure
 */
static int _hfi_process_edid(struct dp_mgr_hfi_priv *hfi_priv, u32 buffer_size)
{
	struct sde_edid_ctrl *edid_ctrl;
	struct drm_connector *connector;
	struct edid *edid_src;
	struct edid *edid_copy;
	u32 edid_size;
	int rc = 0;
	void *raw_edid_addr;

	if (!hfi_priv)
		return -EINVAL;

	raw_edid_addr = hfi_priv->edid_addr_map->local_addr;

	if (!raw_edid_addr || buffer_size < 128) {
		DP_ERR("Invalid parameters, edid buff size: %d\n", buffer_size);
		return -EINVAL;
	}

	connector = hfi_priv->client.base_connector;
	if (!connector) {
		DP_ERR("Invalid connector\n");
		return -EINVAL;
	}

	edid_ctrl = hfi_priv->edid_ctrl;
	if (!edid_ctrl) {
		DP_ERR("EDID control structure not initialized\n");
		return -EINVAL;
	}

	/* Cast raw buffer to struct edid to read header */
	edid_src = (struct edid *)raw_edid_addr;

	/* Calculate actual EDID size based on extension blocks */
	edid_size = (1 + edid_src->extensions) * EDID_LENGTH;

	if (edid_size > buffer_size) {
		DP_WARN("EDID size %d exceeds buffer size %d, truncating\n",
			edid_size, buffer_size);
		edid_size = buffer_size;
	}

	/* Basic EDID validation before copying */
	if (!drm_edid_is_valid(edid_src)) {
		DP_ERR("Invalid EDID received from DCP\n");
		return -EINVAL;
	}

	/* Allocate memory and copy EDID from DCP shared buffer */
	edid_copy = kmalloc(edid_size, GFP_KERNEL);
	if (!edid_copy) {
		DP_ERR("Failed to allocate memory for EDID copy\n");
		return -ENOMEM;
	}

	memcpy(edid_copy, raw_edid_addr, edid_size);

	/* Free any previous EDID data */
	if (edid_ctrl->edid)
		sde_free_edid((void **)&edid_ctrl);

	/* Assign copied EDID to the control structure */
	edid_ctrl->edid = edid_copy;

	/* Parse EDID for audio, vendor info, speaker allocation, etc. */
	sde_parse_edid(edid_ctrl);

	DP_INFO("Successfully processed EDID from DCP\n");

	return rc;
}

static bool _hfi_notify_hpd_user(struct dp_mgr_hfi_priv *hfi_priv, bool connection)
{
	struct drm_device *dev = NULL;
	struct drm_connector *connector;
	char name[HPD_STRING_SIZE], status[HPD_STRING_SIZE],
		bpp[HPD_STRING_SIZE], pattern[HPD_STRING_SIZE];
	char *envp[6];
	char *event_string = "HOTPLUG=1";
	struct dp_client *client;
	int rc = 0;

	connector = hfi_priv->client.base_connector;
	client = &hfi_priv->client;

	if (!connector) {
		DP_ERR("connector not set\n");
		return false;
	}

	client->is_sst_connected = connection;
	connector->status = connection ? connector_status_connected :
		connector_status_disconnected;

	dev = connector->dev;

	snprintf(name, HPD_STRING_SIZE, "name=%s", connector->name);
	snprintf(status, HPD_STRING_SIZE, "status=%s",
		drm_get_connector_status_name(connector->status));

	/* TODO Update this */
	snprintf(bpp, HPD_STRING_SIZE, "bpp=%d", 0);
	snprintf(pattern, HPD_STRING_SIZE, "pattern=%d", 0);

	DP_ERR("[%s]:[%s] [%s] [%s]\n", name, status, bpp, pattern);
	envp[0] = name;
	envp[1] = status;
	envp[2] = bpp;
	envp[3] = pattern;
	envp[4] = event_string;
	envp[5] = NULL;

	rc = kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
	DP_ERR("uevent %s: %d\n", rc ? "failure" : "success", rc);

	return true;
}

static int _hfi_parse_supported_modes(struct dp_mgr_hfi_priv *hfi_priv)
{
	u32 *modes_payload;
	u32 mode_count;
	struct hfi_display_mode_info *modes_array;
	int i;

	if (!hfi_priv) {
		DP_ERR("Invalid hfi_priv\n");
		return -EINVAL;
	}

	modes_payload = (u32 *)hfi_priv->modes_addr_map->local_addr;

	if (!modes_payload) {
		DP_ERR("Invalid modes payload\n");
		return -EINVAL;
	}

	/* HFI_PROPERTY_SUPPORTED_MODES format:
	 * payload[0]: count
	 * payload[1..n]: array of struct hfi_display_mode_info[count]
	 */
	mode_count = modes_payload[0];
	DP_DEBUG("mode count: %d\n", mode_count);

	if (mode_count > MAX_MODES) {
		DP_WARN("Mode count %u exceeds MAX_MODES %d, truncating\n",
			mode_count, MAX_MODES);
		mode_count = MAX_MODES;
	}

	if (mode_count == 0) {
		DP_WARN("No modes received from DCP\n");
		hfi_priv->mode_count = 0;
		return 0;
	}

	/* Point to the start of the mode array (after count field) */
	modes_array = (struct hfi_display_mode_info *)&modes_payload[1];

	/* Copy modes to hfi_priv */
	for (i = 0; i < mode_count; i++) {
		memcpy(&hfi_priv->mode_list[i], &modes_array[i],
				sizeof(struct hfi_display_mode_info));
	}

	hfi_priv->mode_count = mode_count;

	DP_DEBUG("Parsed %u display modes from DCP\n", mode_count);
	for (i = 0; i < mode_count; i++) {
		DP_DEBUG("Mode[%d]: %ux%u@%uHz\n", i,
			 hfi_priv->mode_list[i].h_active,
			 hfi_priv->mode_list[i].v_active,
			 hfi_priv->mode_list[i].refresh_rate);
	}

	return 0;
}

static int _register_hpd_events(struct dp_mgr_hfi_priv *hfi_priv, bool enable)
{
	int rc = 0;
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;
	struct hfi_client_t *hfi_client;
	u32 hfi_cmd = (enable ? HFI_COMMAND_DISPLAY_EVENT_REGISTER :
			HFI_COMMAND_DISPLAY_EVENT_DEREGISTER);
	u32 hfi_event;

	/* Get HFI client from the client structure */
	if (!hfi_priv->client.base_connector) {
		DP_ERR("Invalid base connector\n");
		return -EINVAL;
	}

	sde_kms = sde_connector_get_kms(hfi_priv->client.base_connector);
	if (!sde_kms) {
		DP_ERR("Failed to get SDE KMS\n");
		return -EINVAL;
	}

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms) {
		DP_ERR("Failed to get HFI KMS\n");
		return -EINVAL;
	}

	hfi_client = &hfi_kms->hfi_client;

	hfi_event = HFI_EVENT_HPD_STATUS;
	rc = dp_hfi_start_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
		HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32), HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc) {
		DP_ERR("Could not register for updates from DCP, rc=%d\n", rc);
		goto end;
	}

	hfi_event = HFI_EVENT_DISPLAY_EDID_INFO;
	rc = dp_hfi_end_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
		HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32), HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc) {
		DP_ERR("Could not register for updates from DCP, rc=%d\n", rc);
		goto end;
	}

end:
	return rc;
}

static int _send_plug(struct dp_mgr_hfi_priv *hfi_priv,
		struct hfi_device_hotplug_config *config)
{
	struct hfi_buff edid_buf = {0};
	struct hfi_buff modes_buf = {0};
	struct hfi_device_hotplug_info payload = {0};
	struct hfi_client_t *hfi_client;
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

static int _send_unplug(struct dp_mgr_hfi_priv *hfi_priv, struct hfi_device_hotplug_config *config)
{
	struct hfi_device_hotplug_info payload = {0};
	struct hfi_client_t *hfi_client;
	u32 hfi_cmd = HFI_COMMAND_DEVICE_HOT_PLUG_DETECT;
	int rc = 0;

	hfi_client = dp_mgr_hfi_get_hfi_client(hfi_priv);
	if (!hfi_client)
		return -EINVAL;

	payload.config = *config;

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

static int _hfi_send_hot_plug(struct dp_mgr_hfi_priv *hfi_priv,
		struct hfi_device_hotplug_config *config)
{
	DP_INFO("****** HOTPLUG ******* = %d\n", hfi_priv->connected);

	if (hfi_priv->connected)
		return _send_plug(hfi_priv, config);
	else
		return _send_unplug(hfi_priv, config);
}

static int _hfi_power_init(struct dp_mgr_hfi_priv *hfi_priv)
{
	int rc = 0;
	struct device *dev = &hfi_priv->pdev->dev;

	DP_DEBUG("dp_mgr_hfi: power_init: pm_domain: %p\n", dev->pm_domain);
	if (!dev->pm_domain)
		return rc;

	pm_runtime_enable(dev);
	hfi_priv->pd_dp_phy_gdsc = dev;

	DP_DEBUG("usb/dp phy power enable\n");
	rc = pm_runtime_get_sync(hfi_priv->pd_dp_phy_gdsc);
	if (rc < 0)
		DP_ERR("Fail to enable pd_dp_phy_gdsc regulator ret = %d\n", rc);

	return rc;
}

static int _hfi_power_deinit(struct dp_mgr_hfi_priv *hfi_priv)
{
	int rc = 0;
	struct device *dev = hfi_priv->pd_dp_phy_gdsc;

	if (!dev)
		return 0;

	DP_DEBUG("dp_mgr_hfi: power_deinit: pm_domain: %p\n", dev->pm_domain);

	if (!dev->pm_domain)
		return rc;

	DP_DEBUG("usb/dp phy power disable\n");
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return rc;
}

int dp_mgr_hfi_clk_init(struct dp_mgr_hfi_priv *hfi_priv)
{
	int num_clk = 0;
	int rc = 0;
	struct device *dev = &hfi_priv->pdev->dev;

	num_clk = of_property_count_strings(dev->of_node, "clock-names");
	if (num_clk <= 0) {
		DP_ERR("no clocks are defined\n");
		rc = -EINVAL;
		goto exit;
	}

	hfi_priv->usb3_tcsr_clk = clk_get(dev, "usb3_tcsr_clk");
	if (IS_ERR(hfi_priv->usb3_tcsr_clk)) {
		DP_ERR("Unable to get usb3_tcsr_clk: %ld\n", PTR_ERR(hfi_priv->usb3_tcsr_clk));
		rc = PTR_ERR(hfi_priv->usb3_tcsr_clk);
		hfi_priv->usb3_tcsr_clk = NULL;
		goto exit;
	}

	hfi_priv->usb3_pipe_clk = clk_get(dev, "usb3_pipe_clk");
	if (IS_ERR(hfi_priv->usb3_pipe_clk)) {
		DP_ERR("Unable to get usb3_pipe_clk: %ld\n", PTR_ERR(hfi_priv->usb3_pipe_clk));
		rc = PTR_ERR(hfi_priv->usb3_pipe_clk);
		hfi_priv->usb3_pipe_clk = NULL;
		goto exit;
	}
exit:
	return rc;
}

void dp_mgr_hfi_clk_deinit(struct dp_mgr_hfi_priv *hfi_priv)
{
	if (hfi_priv->usb3_tcsr_clk) {
		clk_put(hfi_priv->usb3_tcsr_clk);
		hfi_priv->usb3_tcsr_clk = NULL;
	}

	if (hfi_priv->usb3_pipe_clk) {
		clk_put(hfi_priv->usb3_pipe_clk);
		hfi_priv->usb3_pipe_clk = NULL;
	}
}

int dp_mgr_hfi_clk_enable(struct dp_mgr_hfi_priv *hfi_priv, bool enable)
{
	int rc = 0;

	if (!hfi_priv || !hfi_priv->usb3_tcsr_clk || !hfi_priv->usb3_pipe_clk) {
		DP_ERR("Required clocks not found\n");
		return -EINVAL;
	}

	if (enable) {
		rc = clk_prepare_enable(hfi_priv->usb3_tcsr_clk);
		if (rc) {
			DEV_ERR("Failed to enable usb3_tcsr_clk\n");
			return rc;
		}

		rc = clk_prepare_enable(hfi_priv->usb3_pipe_clk);
		if (rc) {
			DEV_ERR("Failed to enable usb3_pipe_clk\n");
			return rc;
		}
	} else {
		clk_disable_unprepare(hfi_priv->usb3_pipe_clk);
		clk_disable_unprepare(hfi_priv->usb3_tcsr_clk);
	}

	return 0;
}

static void _hfi_update_config(struct dp_mgr_hfi_priv *hfi_priv,
		struct hfi_device_hotplug_config *config)
{
	if (!hfi_priv || !hfi_priv->hpd || !config)
		return;

	config->orientation = hfi_priv->hpd->orientation;
	config->port_index = hfi_priv->hpd->port_id;
	config->pin_config = hfi_priv->hpd->pin_config;
	config->hpd_state = hfi_priv->hpd->hpd_high;
	config->hpd_irq = hfi_priv->hpd->hpd_irq;
	config->port_index = hfi_priv->hpd->port_id;
	config->pin_config = hfi_priv->hpd->pin_config;

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
	int rc = 0;

	if (!hfi_priv) {
		DP_ERR("Invalid hfi_priv data\n");
		return -EINVAL;
	}

	hfi_client = dp_mgr_hfi_get_hfi_client(hfi_priv);
	if (!hfi_client) {
		rc = -EINVAL;
		goto end;
	}

	if (hfi_priv->configured)
		goto end;

	_hfi_update_config(hfi_priv, &config);

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

	if (hfi_priv->aux_switch) {
		rc = hfi_priv->aux_switch->init(hfi_priv->aux_switch);
		if (rc) {
			rc = -EINVAL;
			goto end;
		}

		rc = hfi_priv->aux_switch->configure(hfi_priv->aux_switch, true,
				config.orientation);
		if (rc) {
			rc = -EINVAL;
			goto end;
		}
	}

	rc = _hfi_power_init(hfi_priv);
	if (rc) {
		DP_ERR("failed to init dp phy power\n");
		goto end;
	}

	rc = dp_mgr_hfi_clk_enable(hfi_priv, true);
	if (rc) {
		DP_ERR("failed to enable core clocks\n");
		goto end;
	}

	rc = _register_hpd_events(hfi_priv, true);
	if (rc) {
		DP_ERR("failed to register hpd events\n");
		goto end;
	}

	hfi_priv->configured = true;
	DP_INFO("configured\n");

	if (config.hpd_state) {
		hfi_priv->connected = true;
		rc = _hfi_send_hot_plug(hfi_priv, &config);
		DP_INFO("connected\n");
	}

end:
	return rc;
}

static int dp_mgr_hfi_hpd_disconnect_cb(void *data)
{
	struct dp_mgr_hfi_priv *hfi_priv = data;
	struct hfi_device_hotplug_config config = {0};
	struct hfi_client_t *hfi_client;
	int rc = 0;

	if (!hfi_priv) {
		DP_ERR("Invalid hfi_priv data\n");
		return -EINVAL;
	}

	hfi_client = dp_mgr_hfi_get_hfi_client(hfi_priv);
	if (!hfi_client) {
		rc = -EINVAL;
		goto end;
	}

	_hfi_update_config(hfi_priv, &config);

	if (hfi_priv->aux_switch) {
		rc = hfi_priv->aux_switch->configure(hfi_priv->aux_switch,
				false, ORIENTATION_NONE);
		if (rc)
			goto end;
	}

	hfi_priv->connected = false;
	_hfi_send_hot_plug(hfi_priv, &config);
	DP_INFO("disconnected\n");

end:
	return rc;
}

static int dp_mgr_hfi_hpd_cleanup(struct dp_mgr_hfi_priv *hfi_priv)
{
	struct hfi_client_t *hfi_client;
	int rc = 0;

	if (!hfi_priv) {
		DP_ERR("Invalid hfi_priv data\n");
		return -EINVAL;
	}

	hfi_client = dp_mgr_hfi_get_hfi_client(hfi_priv);
	if (!hfi_client) {
		rc = -EINVAL;
		goto end;
	}

	dp_mgr_init_deinit_shared_addr(hfi_client, hfi_priv->edid_addr_map);
	dp_mgr_init_deinit_shared_addr(hfi_client, hfi_priv->modes_addr_map);

	_register_hpd_events(hfi_priv, false);

	dp_mgr_hfi_clk_enable(hfi_priv, false);

	_hfi_power_deinit(hfi_priv);

	DP_INFO("cleanup\n");
	hfi_priv->configured = false;
end:
	return rc;
}

static int dp_mgr_hfi_hpd_attention_cb(void *data)
{
	struct dp_mgr_hfi_priv *hfi_priv = data;
	struct hfi_device_hotplug_config config;
	int rc = 0;
	bool hpd_state;
	bool hpd_irq;

	if (!hfi_priv) {
		DP_ERR("Invalid hfi_priv data\n");
		return -EINVAL;
	}

	hpd_state = hfi_priv->hpd->hpd_high;
	hpd_irq = hfi_priv->hpd->hpd_irq;

	DP_DEBUG("hpd status from %d to %d irq %d\n", hfi_priv->connected, hpd_state, hpd_irq);

	/* check if there was any change in state */
	if ((hpd_state == hfi_priv->connected) && !hpd_irq)
		return 0;

	if (hpd_state && !hfi_priv->configured)
		dp_mgr_hfi_hpd_configure_cb(data);

	_hfi_update_config(hfi_priv, &config);

	hfi_priv->connected = hpd_state;
	rc = _hfi_send_hot_plug(hfi_priv, &config);

	return rc;
}

static void dp_mgr_hfi_calc_tu_parameters(struct dp_mgr_hfi_priv *hfi_priv,
		struct dp_panel_info *pinfo, struct dp_vc_tu_mapping_table *tu_table)
{
	struct dp_tu_calc_input in;
	struct dp_tu_calc_output out;

	in.lclk = hfi_priv->link_rate / 1000;
	in.nlanes = hfi_priv->lane_count;
	in.pclk_khz = pinfo->pixel_clk_khz;
	in.hactive = pinfo->h_active;
	in.hporch = pinfo->h_back_porch + pinfo->h_front_porch +
				pinfo->h_sync_width;

	in.bpp = 24 ; // #TODO# add support for other bpps. (pinfo->bpp)
	in.pixel_enc = 444;
	in.dsc_en = pinfo->comp_info.enabled;
	in.async_en = 0;
	in.fec_en = hfi_priv->dpcd.fec_en;
	in.num_of_dsc_slices = pinfo->comp_info.dsc_info.slice_per_pkt;
	in.ppc_div_factor = 2; /*hfi_priv->dpcd.pclk_factor;*/

	if (pinfo->comp_info.enabled) {
		in.compress_ratio = mult_frac(100, pinfo->comp_info.src_bpp,
				pinfo->comp_info.tgt_bpp);
		in.comp_bpp = pinfo->comp_info.tgt_bpp;
	} else {
		in.compress_ratio = 100;
		in.comp_bpp = in.bpp;
	}

	DP_INFO("tu in1: %x %x %x %x %x %x %x %x\n", (u32)in.lclk, (u32)in.pclk_khz,
			(u32)in.hactive, (u32)in.hporch, in.nlanes, in.bpp, in.pixel_enc,
			(u32)in.comp_bpp);

	DP_INFO("tu in2: %x %x %x %x %x %x\n", in.dsc_en, in.async_en, in.fec_en,
			(u32)in.compress_ratio, in.num_of_dsc_slices,
			in.ppc_div_factor);

	dp_tu_calculate(&in, &out);

	/* Log output results */
	DP_INFO("tu out: %x %x %x %x %x %x %x\n", out.valid_boundary_link, out.delay_start_link,
			out.boundary_moderation_en, out.valid_lower_boundary_link,
			out.upper_boundary_count, out.lower_boundary_count,
			out.tu_size_minus1);

	tu_table->valid_boundary_link       = out.valid_boundary_link;
	tu_table->delay_start_link          = out.delay_start_link;
	tu_table->boundary_moderation_en    = out.boundary_moderation_en;
	tu_table->valid_lower_boundary_link = out.valid_lower_boundary_link;
	tu_table->upper_boundary_count      = out.upper_boundary_count;
	tu_table->lower_boundary_count      = out.lower_boundary_count;
	tu_table->tu_size_minus1            = out.tu_size_minus1;
}

static int dp_mgr_hfi_send_transfer_unit(struct dp_mgr_hfi_priv *hfi_priv,
	struct hfi_client_t *hfi_client, struct dp_display_mode *mode)
{
	struct dp_vc_tu_mapping_table tu_calc_table;
	struct hfi_display_dp_tu tu;
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_SET_DP_TU;

	if (!hfi_priv || !hfi_client || !mode) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	dp_mgr_hfi_calc_tu_parameters(hfi_priv, &mode->timing, &tu_calc_table);

	tu.dp_tu = tu_calc_table.tu_size_minus1;
	tu.valid_boundary = tu_calc_table.valid_boundary_link;
	tu.valid_boundary |= (tu_calc_table.delay_start_link << 16);

	tu.valid_boundary2 = (tu_calc_table.valid_lower_boundary_link << 1);
	tu.valid_boundary2 |= (tu_calc_table.upper_boundary_count << 16);
	tu.valid_boundary2 |= (tu_calc_table.lower_boundary_count << 20);

	if (tu_calc_table.boundary_moderation_en)
		tu.valid_boundary2 |= BIT(0);

	DP_DEBUG("dp_tu=0x%x, valid_boundary=0x%x, valid_boundary2=0x%x\n",
			tu.dp_tu, tu.valid_boundary, tu.valid_boundary2);

	return dp_hfi_start_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
		HFI_PAYLOAD_TYPE_U32_ARRAY, &tu, sizeof(struct hfi_display_dp_tu),
		HFI_HOST_FLAGS_NON_DISCARDABLE);
}

static int dp_mgr_hfi_set_mode(struct dp_client *client, int panel_id, struct dp_display_mode *mode)
{
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;
	struct hfi_client_t *hfi_client;
	struct hfi_display_mode_info hfi_mode_info;
	struct dp_mgr_hfi_priv *hfi_priv;
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_SET_MODE;
	int rc = 0;

	if (!client || !mode) {
		DP_ERR("Invalid params\n");
		return -EINVAL;
	}

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);

	sde_kms = sde_connector_get_kms(client->base_connector);
	if (!sde_kms)
		return -EINVAL;

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms)
		return -EINVAL;

	hfi_client = &hfi_kms->hfi_client;

	rc = dp_mgr_hfi_send_transfer_unit(hfi_priv, hfi_client, mode);
	if (rc)
		DP_WARN("couldn't send TU data\n");

	hfi_mode_info.size            = sizeof(struct hfi_display_mode_info);
	hfi_mode_info.h_active        =	mode->timing.h_active;
	hfi_mode_info.h_back_porch    =	mode->timing.h_back_porch;
	hfi_mode_info.h_sync_width    =	mode->timing.h_sync_width;
	hfi_mode_info.h_front_porch   =	mode->timing.h_front_porch;
	hfi_mode_info.h_skew          =	mode->timing.h_skew;
	hfi_mode_info.h_sync_polarity =	mode->timing.h_active_low ? 0 : 1;
	hfi_mode_info.v_active        =	mode->timing.v_active;
	hfi_mode_info.v_back_porch    =	mode->timing.v_back_porch;
	hfi_mode_info.v_sync_width    =	mode->timing.v_sync_width;
	hfi_mode_info.v_front_porch   =	mode->timing.v_front_porch;
	hfi_mode_info.v_sync_polarity =	mode->timing.v_active_low ? 0 : 1;
	hfi_mode_info.refresh_rate    =	mode->timing.refresh_rate;

	rc = dp_hfi_append_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
			HFI_PAYLOAD_TYPE_U32_ARRAY, &hfi_mode_info, hfi_mode_info.size,
			HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc)
		DP_ERR("Could not send HFI_COMMAND_DISPLAY_SET_MODE, rc=%d\n", rc);

	return rc;
}

static enum drm_mode_status dp_mgr_hfi_validate_mode(struct dp_client *client, int panel_id,
		struct drm_display_mode *mode, const struct msm_resource_caps_info *avail_res)
{
	struct dp_mgr_hfi_priv *hfi_priv;
	int i;
	u32 drm_refresh_rate;

	if (!client || !mode) {
		DP_ERR("Invalid params\n");
		return MODE_ERROR;
	}

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);

	if (hfi_priv->mode_count == 0) {
		DP_WARN("No modes available from DCP for validation\n");
		return MODE_ERROR;
	}

	drm_refresh_rate = drm_mode_vrefresh(mode);

	for (i = 0; i < hfi_priv->mode_count; i++) {
		struct hfi_display_mode_info *hfi_mode = &hfi_priv->mode_list[i];

		/* Compare resolution and refresh rate */
		if ((hfi_mode->h_active == mode->hdisplay) &&
		    (hfi_mode->v_active == mode->vdisplay) &&
		    (hfi_mode->refresh_rate == drm_refresh_rate)) {
			DP_DEBUG("Mode validated: matches HFI mode[%d]\n", i);
			return MODE_OK;
		}
	}

	return MODE_ERROR;
}

static int dp_mgr_hfi_get_modes(struct dp_client *client, int panel_id,
		struct dp_display_mode *dp_mode)
{
	int rc = 0;
	struct dp_mgr_hfi_priv *hfi_priv;

	if (!client) {
		DP_ERR("Invalid params\n");
		return 0;
	}

	DP_DEBUG("HFI get modes for panel_id: %d\n", panel_id);

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);

	rc = _sde_edid_update_modes(client->base_connector, hfi_priv->edid_ctrl);

	if (dp_mode->timing.pixel_clk_khz)
		hfi_priv->client.max_pclk_khz = dp_mode->timing.pixel_clk_khz;

	return rc;
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

static void dp_mgr_hfi_handle_dp_info(struct dp_mgr_hfi_priv *hfi_priv, void *payload, u32 size)
{
	struct hfi_display_event_edid_info *info = payload;
	struct hfi_buff *edid_buf = &info->edid_buf;

	if (hfi_priv->connected) {
		print_hex_dump(KERN_INFO, "EDID(Little Endian): ",
			DUMP_PREFIX_NONE, 16, 4, hfi_priv->edid_addr_map->local_addr,
			edid_buf->size, false);

		_hfi_process_edid(hfi_priv, edid_buf->size);
		_hfi_parse_supported_modes(hfi_priv);
		hfi_priv->link_rate = info->link_rate;
		hfi_priv->lane_count = info->lane_count;
	}

	hfi_priv->link_rate = info->link_rate;
	hfi_priv->lane_count = info->lane_count;

	_hfi_notify_hpd_user(hfi_priv, hfi_priv->connected);
}

static void dp_mgr_hfi_handle_hpd_status(struct dp_mgr_hfi_priv *hfi_priv, void *payload, u32 size)
{
	struct hfi_display_hpd_status *hpd_status = (struct hfi_display_hpd_status *) payload;

	switch (hpd_status->dp_evt) {
	case HFI_DP_EVENT_HPD_UNPLUGGED:
		_hfi_notify_hpd_user(hfi_priv, false);
		break;
	default:
		break;
	}
}

static void dp_mgr_hfi_handle_event(void *cb_data, u32 event, void *payload, u32 size)
{
	struct dp_mgr_hfi_priv *hfi_priv = cb_data;

	DP_INFO("hfi response: %x\n", event);
	switch (event) {
	case HFI_COMMAND_DISPLAY_EVENT_HPD_STATUS:
		dp_mgr_hfi_handle_hpd_status(hfi_priv, payload, size);
		break;
	case HFI_COMMAND_DISPLAY_EVENT_EDID_INFO:
		dp_mgr_hfi_handle_dp_info(hfi_priv, payload, size);
		break;
	default:
		break;
	}
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
	hfi_priv->hfi = dp_hfi_setup(client, hfi_priv);
	if (IS_ERR(hfi_priv->hfi)) {
		rc = PTR_ERR(hfi_priv->hfi);
		DP_ERR("dp_hfi_setup failed: %d\n", rc);
		hfi_priv->hfi = NULL;
		goto end;
	}

	hfi_priv->hfi->handle_event = dp_mgr_hfi_handle_event;

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
		goto end;
	}

	/* Initialize EDID control structure */
	hfi_priv->edid_ctrl = sde_edid_init();
	if (!hfi_priv->edid_ctrl) {
		DP_ERR("Failed to initialize edid_ctrl\n");
		rc = -ENOMEM;
		goto end;
	}

	hfi_priv->aux_switch = dp_aux_switch_get(&hfi_priv->pdev->dev);
	if (IS_ERR(hfi_priv->aux_switch)) {
		rc = PTR_ERR(hfi_priv->aux_switch);
		DP_ERR("failed to initialize aux, rc = %d\n", rc);
		hfi_priv->aux_switch = NULL;
		goto clear_hpd;
	}

	rc = dp_mgr_hfi_clk_init(hfi_priv);
	if (rc) {
		DP_ERR("failed to init clocks\n");
		goto clear_aux_switch;
	}

	return 0;

clear_aux_switch:
	if (hfi_priv->aux_switch) {
		dp_aux_switch_put(hfi_priv->aux_switch);
		hfi_priv->aux_switch = NULL;
	}

clear_hpd:
	if (hfi_priv->hpd) {
		dp_hpd_put(hfi_priv->hpd);
		hfi_priv->hpd = NULL;
	}
end:
	return rc;
}

static int dp_mgr_hfi_ctl_init(struct dp_client *client)
{
	return 0;
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
	struct dp_mgr_hfi_priv *hfi_priv;

	if (!client) {
		DP_ERR("Invalid params\n");
		return NULL;
	}

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);

	return &hfi_priv->default_mode;
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

	sde_kms = sde_connector_get_kms(client->base_connector);
	if (!sde_kms)
		return -EINVAL;

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms)
		return -EINVAL;

	hfi_client = &hfi_kms->hfi_client;

	rc = dp_hfi_end_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
			HFI_PAYLOAD_TYPE_NONE, NULL, 0, HFI_HOST_FLAGS_NON_DISCARDABLE);
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
	struct dp_mgr_hfi_priv *hfi_priv;

	if (!client) {
		DP_ERR("Invalid params\n");
		goto end;
	}

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);
	if (!hfi_priv) {
		DP_ERR("invalid param(s), hfi_priv %pK\n", hfi_priv);
		rc = -EINVAL;
		goto end;
	}

	dp_mgr_hfi_hpd_cleanup(hfi_priv);

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

	/* Cleanup EDID control structure */
	if (hfi_priv->edid_ctrl) {
		sde_edid_deinit((void **)&hfi_priv->edid_ctrl);
		hfi_priv->edid_ctrl = NULL;
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
	const u32 num_components = 3, default_bpp = 24;

	if (!client || !dp_mode || !drm_mode)
		return;

	dp_mode->timing.h_active = drm_mode->hdisplay;
	dp_mode->timing.h_back_porch = drm_mode->htotal - drm_mode->hsync_end;
	dp_mode->timing.h_sync_width = drm_mode->htotal -
			(drm_mode->hsync_start + dp_mode->timing.h_back_porch);
	dp_mode->timing.h_front_porch = drm_mode->hsync_start - drm_mode->hdisplay;
	dp_mode->timing.h_skew = drm_mode->hskew;

	dp_mode->timing.v_active = drm_mode->vdisplay;
	dp_mode->timing.v_back_porch = drm_mode->vtotal - drm_mode->vsync_end;
	dp_mode->timing.v_sync_width = drm_mode->vtotal -
		(drm_mode->vsync_start + dp_mode->timing.v_back_porch);

	dp_mode->timing.v_front_porch = drm_mode->vsync_start - drm_mode->vdisplay;

	dp_mode->timing.refresh_rate = drm_mode_vrefresh(drm_mode);
	dp_mode->timing.pixel_clk_khz = drm_mode->clock;

	dp_mode->timing.v_active_low = !!(drm_mode->flags & DRM_MODE_FLAG_NVSYNC);

	dp_mode->timing.h_active_low = !!(drm_mode->flags & DRM_MODE_FLAG_NHSYNC);

	dp_mode->timing.bpp = client->base_connector->display_info.bpc * num_components;
	if (!dp_mode->timing.bpp)
		dp_mode->timing.bpp = default_bpp;

	/* As YUV was not supported now, so set the default format to RGB */
	dp_mode->output_format = DP_OUTPUT_FORMAT_RGB;
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
	if (!client || !avail_res || !max_dp_avail_res) {
		DP_ERR("invalid arguments\n");
		return -EINVAL;
	}

	memcpy(max_dp_avail_res, avail_res, sizeof(struct msm_resource_caps_info));

	max_dp_avail_res->num_lm = min(avail_res->num_lm, client->max_mixer_count);
	max_dp_avail_res->num_dsc = min(avail_res->num_dsc, client->max_dsc_count);

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

	client->dp_ipc_log = ipc_log_context_create(DRM_DP_IPC_NUM_PAGES, "drm_dp", 0);
	if (!client->dp_ipc_log)
		DP_WARN("Error in creating ipc_log_context for drm_dp\n");

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
	drm_ops->ctl_init      = dp_mgr_hfi_ctl_init;
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
