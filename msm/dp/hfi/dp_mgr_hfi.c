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
#include "dp_debug_client_hfi.h"
#include "dp_hfi_audio.h"
#include "dp_hdcp.h"
#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
#include "linux/msm_hdcp.h"
#endif

#define DRM_DP_IPC_NUM_PAGES 10
#define HPD_STRING_SIZE	    30
#define MAX_MODES           32

/* HDCP 2.x message IDs */
#define SKE_SEND_EKS            11
#define REP_STREAM_READY        17
#define SKE_SEND_TYPE_ID        18

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

static void dp_mgr_hfi_update_hdcp_info(struct dp_mgr_hfi_priv *mgr, bool reset)
{
	if (!mgr || !mgr->debug) {
		DP_ERR("Invalid mgr or debug structure\n");
		return;
	}

	if (reset) {
		mgr->hdcp_info.hdcp_state = HDCP_STATE_INACTIVE;
		mgr->hdcp_info.hdcp_version = HDCP_VERSION_NONE;
	}

	memset(mgr->debug->hdcp_status, 0, sizeof(mgr->debug->hdcp_status));

	snprintf(mgr->debug->hdcp_status, sizeof(mgr->debug->hdcp_status),
		"%s: %s\ncaps: %d\n",
		sde_hdcp_version(mgr->hdcp_info.hdcp_version),
		sde_hdcp_state_name(mgr->hdcp_info.hdcp_state),
		mgr->hdcp_info.source_cap);
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

	if (!hfi_priv->edid_addr_map) {
		DP_ERR("EDID buffer not available, cannot process EDID\n");
		return -EINVAL;
	}

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

	DP_INFO("[%s]:[%s] [%s] [%s]\n", name, status, bpp, pattern);
	envp[0] = name;
	envp[1] = status;
	envp[2] = bpp;
	envp[3] = pattern;
	envp[4] = event_string;
	envp[5] = NULL;

	rc = kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
	if  (rc)
		DP_ERR("uevent failed, rc=%d\n", rc);
	else
		DP_DEBUG("uevent successful!\n");

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

	if (!hfi_priv->modes_addr_map) {
		DP_ERR("Modes buffer not available, cannot parse modes\n");
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
		DP_ERR("Could not register for HPD status from DCP, rc=%d\n", rc);
		goto end;
	}

	hfi_event = HFI_EVENT_DISPLAY_EDID_INFO;
	rc = dp_hfi_append_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
		HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32), HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc) {
		DP_ERR("Could not register for EDID info from DCP, rc=%d\n", rc);
		goto end;
	}

	hfi_event = HFI_EVENT_HDCP_FEATURE_SUPPORTED;
	rc = dp_hfi_end_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
		HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32), HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc)
		DP_ERR("Could not register HDCP feature supported event, rc=%d\n", rc);

end:
	return rc;
}

static int _register_hdcp_events(struct dp_mgr_hfi_priv *hfi_priv,
				  bool enable,
				  u32 hdcp_version)
{
	struct hfi_client_t *hfi_client;
	u32 hfi_cmd = (enable ? HFI_COMMAND_DISPLAY_EVENT_REGISTER :
		       HFI_COMMAND_DISPLAY_EVENT_DEREGISTER);
	u32 hfi_event;
	int rc = 0;

	hfi_client = dp_mgr_hfi_get_hfi_client(hfi_priv);
	if (!hfi_client)
		return -EINVAL;
	if (hdcp_version == HDCP_VERSION_2P2) {
		hfi_event = HFI_EVENT_HDCP2X_START;
		if (enable)
			rc = dp_hfi_start_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd,
						"DisplayPort",
						HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32),
						HFI_HOST_FLAGS_NON_DISCARDABLE);
		else
			rc = dp_hfi_append_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd,
						"DisplayPort",
						HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32),
						HFI_HOST_FLAGS_NON_DISCARDABLE);
		if (rc) {
			DP_ERR("Could not register HDCP2x start, rc=%d\n", rc);
			return rc;
		}

		hfi_event = HFI_EVENT_HDCP2X_PROCESS_MSG;
		rc = dp_hfi_append_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
					HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32),
					HFI_HOST_FLAGS_NON_DISCARDABLE);
		if (rc) {
			DP_ERR("Could not register HDCP2x process msg, rc=%d\n", rc);
			return rc;
		}

		hfi_event = HFI_EVENT_HDCP2X_TIMEOUT;
		if (enable)
			rc = dp_hfi_append_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd,
					"DisplayPort",
					HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32),
					HFI_HOST_FLAGS_NON_DISCARDABLE);
		else
			rc = dp_hfi_end_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
					HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32),
					HFI_HOST_FLAGS_NON_DISCARDABLE);


		if (rc) {
			DP_ERR("Could not register HDCP2x timeout, rc=%d\n", rc);
			return rc;
		}
	} else if (hdcp_version == HDCP_VERSION_1X) {
		hfi_event = HFI_EVENT_HDCP1X_START;
		if (enable)
			rc = dp_hfi_start_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd,
						"DisplayPort",
						HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32),
						HFI_HOST_FLAGS_NON_DISCARDABLE);
		else
			rc = dp_hfi_append_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd,
						"DisplayPort",
						HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32),
						HFI_HOST_FLAGS_NON_DISCARDABLE);
		if (rc) {
			DP_ERR("Could not register HDCP1x start, rc=%d\n", rc);
			return rc;
		}

		hfi_event = HFI_EVENT_HDCP1X_STOP;
		rc = dp_hfi_append_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
					HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32),
					HFI_HOST_FLAGS_NON_DISCARDABLE);
		if (rc) {
			DP_ERR("Could not register HDCP1x stop, rc=%d\n", rc);
			return rc;
		}

		hfi_event = HFI_EVENT_HDCP1X_ENC;
		rc = dp_hfi_append_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
					HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32),
					HFI_HOST_FLAGS_NON_DISCARDABLE);
		if (rc) {
			DP_ERR("Could not register HDCP1x ENC, rc=%d\n", rc);
			return rc;
		}

		hfi_event = HFI_EVENT_HDCP1X_TOPOLOGY_UPDATE;
		if (enable)
			rc = dp_hfi_append_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd,
					"DisplayPort",
					HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32),
					HFI_HOST_FLAGS_NON_DISCARDABLE);
		else
			rc = dp_hfi_end_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
					HFI_PAYLOAD_TYPE_U32, &hfi_event, sizeof(u32),
					HFI_HOST_FLAGS_NON_DISCARDABLE);
		if (rc) {
			DP_ERR("Could not register HDCP1x topology, rc=%d\n", rc);
			return rc;
		}
	}

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

int dp_mgr_hfi_send_audio_config(struct dp_client *client, struct hfi_audio_config *audio_config)
{
	struct hfi_client_t *hfi_client;
	struct dp_mgr_hfi_priv *hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_AUDIO_CONFIG;
	int rc = 0;

	hfi_client = dp_mgr_hfi_get_hfi_client(hfi_priv);
	if (!hfi_client)
		return -EINVAL;

	/* Send HFI_COMMAND_DEVICE_HOT_PLUG_DETECT command with config as payload */
	rc = dp_hfi_send_cmd_buf(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
			HFI_PAYLOAD_TYPE_U32_ARRAY, (void *)audio_config,
			sizeof(struct hfi_audio_config), (HFI_HOST_FLAGS_NON_DISCARDABLE));

	if (rc) {
		DP_ERR("Could not send HFI_COMMAND_DISPLAY_AUDIO_CONFIG, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int dp_mgr_hfi_send_audio_control(struct dp_client *client, u32 enable)
{
	struct hfi_client_t *hfi_client;
	struct dp_mgr_hfi_priv *hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_AUDIO_CONTROL;
	int rc = 0;

	hfi_client = dp_mgr_hfi_get_hfi_client(hfi_priv);
	if (!hfi_client)
		return -EINVAL;

	/* Send HFI_COMMAND_DISPLAY_AUDIO_CONTROL command with config as payload */
	rc = dp_hfi_send_cmd_buf(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
			HFI_PAYLOAD_TYPE_U32_ARRAY, &enable, sizeof(enable),
			(HFI_HOST_FLAGS_NON_DISCARDABLE));
	if (rc) {
		DP_ERR("Could not send HFI_COMMAND_DISPLAY_AUDIO_CONTROL, rc=%d\n", rc);
		return rc;
	}

	return 0;
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

static u32 _remap_orientation(u32 orientation)
{
	switch (orientation) {
	case ORIENTATION_CC1:
		return 0;
	case ORIENTATION_CC2:
		return 1;
	default:
		break;
	}

	return 0;
}

static void _hfi_update_config(struct dp_mgr_hfi_priv *hfi_priv,
		struct hfi_device_hotplug_config *config)
{
	if (!hfi_priv || !hfi_priv->hpd || !config)
		return;
	/*
	 * Handling of orientation - DP altmode driver receives the orientation as a 0-based value
	 * from PD driver but then it converts it to 1-based value on the first notification. The
	 * usb switch configuration, which is done on the first notification, uses the 1-based
	 * value. But, we send HFI, only when HPD HIGH is set, which could be either on the first
	 * or second. So to keep it consistent, we will remap the hpd orientation here. So the
	 * orientation in the hfi_device_hotplug_config is always 1-based.
	 */

	config->orientation = _remap_orientation(hfi_priv->hpd->orientation);
	config->port_index = hfi_priv->hpd->port_id;
	config->pin_config = hfi_priv->hpd->pin_config;
	config->hpd_state = hfi_priv->hpd->hpd_high;
	config->hpd_irq = hfi_priv->hpd->hpd_irq;
	config->port_index = hfi_priv->hpd->port_id;
	config->pin_config = hfi_priv->hpd->pin_config;

	DP_INFO("rsdbg: orientation=%u, port=%u, pin=%u, hpd=%u, irq=%u\n",
		config->orientation, config->port_index, config->pin_config,
		config->hpd_state, config->hpd_irq);
}

static int _aux_switch_enable(struct dp_mgr_hfi_priv *hfi_priv, bool enable)
{
	int rc;
	u32 orientation = enable ? hfi_priv->hpd->orientation : ORIENTATION_NONE;

	if (!hfi_priv->aux_switch)
		return 0;

	if (enable) {
		rc = hfi_priv->aux_switch->init(hfi_priv->aux_switch);
		if (rc)
			return rc;
	}

	DP_DEBUG("aux switch %sable with orientation:%d\n", (enable ? "en":"dis"),
			hfi_priv->hpd->orientation);
	return hfi_priv->aux_switch->configure(hfi_priv->aux_switch, enable, orientation);
}

/* HPD callback functions */
int dp_mgr_hfi_hpd_configure_cb(void *data)
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

	_aux_switch_enable(hfi_priv, true);

	if (hfi_priv->configured)
		goto end;

	/* Allocate buffers only if they don't already exist */
	if (!hfi_priv->edid_addr_map) {
		hfi_priv->edid_addr_map = dp_mgr_hfi_init_shared_addr(hfi_client, SZ_4K);
		if (!hfi_priv->edid_addr_map) {
			DP_ERR("failed to allocate remote address for edid\n");
			return -ENOMEM;
		}
		DP_DEBUG("Allocated new EDID buffer\n");
	} else {
		DP_DEBUG("Reusing existing EDID buffer\n");
	}

	if (!hfi_priv->modes_addr_map) {
		hfi_priv->modes_addr_map = dp_mgr_hfi_init_shared_addr(hfi_client, SZ_4K);
		if (!hfi_priv->modes_addr_map) {
			DP_ERR("failed to allocate remote address for modes\n");
			/* Only free EDID buffer if we just allocated it */
			if (hfi_priv->edid_addr_map) {
				dp_mgr_init_deinit_shared_addr(hfi_client, hfi_priv->edid_addr_map);
				hfi_priv->edid_addr_map = NULL;
			}
			return -ENOMEM;
		}
		DP_DEBUG("Allocated new modes buffer\n");
	} else {
		DP_DEBUG("Reusing existing modes buffer\n");
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

end:
	if (hfi_priv->hpd->hpd_high && !hfi_priv->connected) {
		hfi_priv->connected = true;

		_hfi_update_config(hfi_priv, &config);
		rc = _hfi_send_hot_plug(hfi_priv, &config);
		DP_INFO("connected\n");
	}

	return rc;
}

int dp_mgr_hfi_hpd_disconnect_cb(void *data)
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

	hfi_priv->connected = false;

	_aux_switch_enable(hfi_priv, false);

	if (hfi_priv->audio)
		hfi_priv->audio->off(hfi_priv->audio, false);

	_hfi_update_config(hfi_priv, &config);
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

	_register_hpd_events(hfi_priv, false);

	/* Free shared buffers */
	if (hfi_priv->edid_addr_map) {
		dp_mgr_init_deinit_shared_addr(hfi_client, hfi_priv->edid_addr_map);
		hfi_priv->edid_addr_map = NULL;
	}
	if (hfi_priv->modes_addr_map) {
		dp_mgr_init_deinit_shared_addr(hfi_client, hfi_priv->modes_addr_map);
		hfi_priv->modes_addr_map = NULL;
	}

	dp_mgr_hfi_clk_enable(hfi_priv, false);

	_hfi_power_deinit(hfi_priv);

	DP_INFO("cleanup\n");
	hfi_priv->configured = false;
	complete_all(&hfi_priv->hpd_comp);
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

	/* Ignore attention calls during soft replug */
	if (hfi_priv->soft_unplug)
		return 0;

	hpd_state = hfi_priv->hpd->hpd_high;
	hpd_irq = hfi_priv->hpd->hpd_irq;

	DP_DEBUG("hpd status from %d to %d irq %d\n", hfi_priv->connected, hpd_state, hpd_irq);

	/* check if there was any change in state */
	if ((hpd_state == hfi_priv->connected) && !hpd_irq)
		return 0;

	if (hpd_state && !hfi_priv->configured) {
		rc = dp_mgr_hfi_hpd_configure_cb(data);
		if (rc)
			return rc;
	} else if (!hfi_priv->connected && hpd_state) {
		_aux_switch_enable(hfi_priv, true);
	}

	_hfi_update_config(hfi_priv, &config);
	hfi_priv->connected = hpd_state;
	rc = _hfi_send_hot_plug(hfi_priv, &config);

	return rc;
}

static void dp_mgr_hfi_min_level_change(void *client_ctx, u8 min_enc_level)
{
	struct dp_mgr_hfi_priv *hfi_priv = client_ctx;

	if (!hfi_priv) {
		DP_ERR("invalid input\n");
		return;
	}

	DP_DEBUG("min_enc_level changed from %u to %u\n",
		 hfi_priv->min_enc_level, min_enc_level);

	hfi_priv->min_enc_level = min_enc_level;
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

	in.bpp = pinfo->bpp;
	in.pixel_enc = 444;
	in.dsc_en = pinfo->comp_info.enabled;
	in.async_en = 0;
	in.fec_en = hfi_priv->fec_en;
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

int dp_mgr_hfi_set_mode(struct dp_client *client, int panel_id, struct dp_display_mode *mode)
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
	if (rc) {
		DP_ERR("Could not send HFI_COMMAND_DISPLAY_SET_MODE, rc=%d\n", rc);
	} else {
		DP_DEBUG("Successfully set mode %ux%u@%uHz for panel_id=%d\n",
				mode->timing.h_active, mode->timing.v_active,
				mode->timing.refresh_rate, panel_id);
	}

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
	drm_refresh_rate = drm_mode_vrefresh(mode);

	/*
	 * If override is enabled, only accept the override mode and reject
	 * all others, even if they exist in the EDID/DCP mode list. This
	 * ensures that any cached or previously selected mode (like 720x480)
	 * is rejected while override is active.
	 */
	if (hfi_priv->mode_ovr.enabled) {
		bool aspect_match = true;

		/* If override aspect is non-zero, enforce aspect match.
		 * If override aspect is 0, treat it as "don't care".
		 */
		if (hfi_priv->mode_ovr.aspect_ratio != 0 &&
		    mode->picture_aspect_ratio != hfi_priv->mode_ovr.aspect_ratio)
			aspect_match = false;

		if (mode->hdisplay != hfi_priv->mode_ovr.h_active ||
				mode->vdisplay != hfi_priv->mode_ovr.v_active ||
				drm_refresh_rate != hfi_priv->mode_ovr.refresh_rate ||
				!aspect_match) {
			DP_DEBUG("invalid ovr %dx%d@%d %d vs %dx%d@%d %d\n",
				 mode->hdisplay, mode->vdisplay, drm_refresh_rate,
				 mode->picture_aspect_ratio, hfi_priv->mode_ovr.h_active,
				 hfi_priv->mode_ovr.v_active, hfi_priv->mode_ovr.refresh_rate,
				 hfi_priv->mode_ovr.aspect_ratio);
			return MODE_BAD;
		}

		mode->type |= DRM_MODE_TYPE_PREFERRED;
		DP_DEBUG("Mode override: %dx%d@%d (aspect=%d, mode_aspect=%d)\n",
			 mode->hdisplay, mode->vdisplay, drm_refresh_rate,
			 hfi_priv->mode_ovr.aspect_ratio,
			 mode->picture_aspect_ratio);
		return MODE_OK;
	}

	if (hfi_priv->mode_count == 0) {
		DP_WARN("No modes available from DCP for validation\n");
		return MODE_ERROR;
	}

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
	struct drm_connector *connector;
	struct drm_display_mode *mode, *tmp;
	struct drm_display_mode *override_mode = NULL;
	u32 ovr_h, ovr_v, ovr_fps;

	if (!client) {
		DP_ERR("Invalid params\n");
		return 0;
	}

	DP_DEBUG("HFI get modes for panel_id: %d\n", panel_id);

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);
	connector = client->base_connector;

	/* Now populate fresh modes from EDID */
	rc = _sde_edid_update_modes(connector, hfi_priv->edid_ctrl);

	if (dp_mode->timing.pixel_clk_khz)
		hfi_priv->client.max_pclk_khz = dp_mode->timing.pixel_clk_khz;

	/* If no override is enabled, just return the EDID modes */
	if (!hfi_priv->mode_ovr.enabled) {
		DP_DEBUG("HFI get_modes: override disabled, returning rc=%d\n", rc);
		return rc;
	}

	/*
	 * Override is enabled: keep only the EDID mode that matches
	 * the override timing. The override mode must be one of the
	 * EDID modes, so we do not synthesize any timing here.
	 */
	ovr_h   = hfi_priv->mode_ovr.h_active;
	ovr_v   = hfi_priv->mode_ovr.v_active;
	ovr_fps = hfi_priv->mode_ovr.refresh_rate;

	DP_DEBUG("HFI get_modes: override requested %ux%u@%uHz\n", ovr_h, ovr_v, ovr_fps);

	/* Find the matching mode in the connector's mode list */
	list_for_each_entry(mode, &connector->modes, head) {
		u32 mode_fps = drm_mode_vrefresh(mode);

		if ((mode->hdisplay == ovr_h) && (mode->vdisplay == ovr_v) &&
				(mode_fps == ovr_fps)) {
			override_mode = mode;
			break;
		}
	}

	if (!override_mode) {
		DP_ERR("HFI get_modes: override %ux%u@%uHz not found. disabling override. rc=%d\n",
			ovr_h, ovr_v, ovr_fps, rc);
		return rc;
	}

	/*
	 * Remove all other modes from the list, leaving only the override
	 * mode exposed to user space. This mirrors the requirement that
	 * when override is set, only that mode should be reported.
	 */
	list_for_each_entry_safe(mode, tmp, &connector->modes, head) {
		if (mode != override_mode) {
			list_del(&mode->head);
			drm_mode_destroy(connector->dev, mode);
		}
	}

	DP_DEBUG("HFI get_modes: override enabled, exposing only %ux%u@%uHz, returning 1\n",
		override_mode->hdisplay, override_mode->vdisplay, ovr_fps);

	/* We now have exactly one mode; return 1 to indicate this */
	return 1;
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
	int i;

	DP_INFO("EDID Info received: size=%u, link_rate=%u, lane_count=%u, bpp=%u\n",
			edid_buf->size, info->link_rate,
			info->lane_count, info->bits_per_pixel);
	/* Connection status is not in the payload - set connected=1 based on receiving EDID info */
	if (edid_buf->size > 0) {
		hfi_priv->connected = true;
	} else {
		hfi_priv->connected = false;
		DP_INFO("Setting connected=0 due to empty EDID buffer\n");
		goto end;
	}

	hfi_priv->link_rate = info->link_rate;
	hfi_priv->lane_count = info->lane_count;
	hfi_priv->tgt_bpp = info->bits_per_pixel;
	hfi_priv->fec_en = info->fec_enabled;

	print_hex_dump(KERN_INFO, "EDID(Little Endian): ",
		DUMP_PREFIX_NONE, 16, 4, hfi_priv->edid_addr_map->local_addr,
		edid_buf->size, false);

	if (_hfi_process_edid(hfi_priv, edid_buf->size)) {
		DP_ERR("Failed to process EDID, skipping modes parsing\n");
		goto end;
	}
	_hfi_parse_supported_modes(hfi_priv);
	/* Print the list of modes received from DCP */
	if (!hfi_priv->mode_count) {
		DP_ERR("No modes received from DCP\n");
		goto end;
	}
	DP_INFO("Received %u modes from DCP:\n",
			hfi_priv->mode_count);
	for (i = 0; i < hfi_priv->mode_count; i++) {
		struct hfi_display_mode_info *mode = &hfi_priv->mode_list[i];

		DP_INFO("Mode[%d]: %ux%u@%uHz hb:(%u %u %u) vb:(%u %u %u)\n",
				i, mode->h_active, mode->v_active, mode->refresh_rate,
				mode->h_front_porch, mode->h_sync_width, mode->h_back_porch,
				mode->v_front_porch, mode->v_sync_width, mode->v_back_porch);
	}
end:
	_hfi_notify_hpd_user(hfi_priv, hfi_priv->connected);
	if (hfi_priv->audio) {
		int ret = hfi_priv->audio->on(hfi_priv->audio);
		(void)ret;
	}
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

static void dp_mgr_hfi_handle_hdcp1x_start(struct dp_mgr_hfi_priv *hfi_priv,
					     void *payload, u32 size)
{
	u32 response[2];  // Array to hold aksv_lsb and aksv_msb
	u32 aksv_msb, aksv_lsb;
	struct hfi_client_t *hfi_client;
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_HDCP1X_AKSV;
	int rc;

	DP_DEBUG("Received HDCP1X_START request from DCP\n");

	/* Clean up any previous feature supported requests/contexts if needed */
	/* This ensures we start fresh for each authentication attempt */

	/* Call dp_hdcp to get AKSV from TrustZone */
	if (!hfi_priv->hdcp1x_ctx) {
		DP_ERR("HDCP context not initialized\n");
		return;
	}

	DP_DEBUG("HDCP1x is supported");

	rc = dp_hdcp1x_start(hfi_priv->hdcp1x_ctx, &aksv_msb, &aksv_lsb);
	if (rc) {
		DP_ERR("Failed to get AKSV from TZ, rc=%d\n", rc);

		hfi_priv->hdcp_info.hdcp_state = HDCP_STATE_AUTH_FAIL;
		dp_mgr_hfi_update_hdcp_info(hfi_priv, false);
		return;
	}

	/* Update status on success */
	hfi_priv->hdcp_info.hdcp_version = HDCP_VERSION_1X;
	hfi_priv->hdcp_info.hdcp_state = HDCP_STATE_AUTHENTICATING;
	dp_mgr_hfi_update_hdcp_info(hfi_priv, false);

	/* Get HFI client */
	hfi_client = dp_mgr_hfi_get_hfi_client(hfi_priv);
	if (!hfi_client) {
		DP_ERR("Failed to get HFI client\n");
		return;
	}

	/* Populate response payload with AKSV for DCP */
	response[0] = aksv_lsb;
	response[1] = aksv_msb;

	/* Send HFI_COMMAND_DISPLAY_HDCP1X_AKSV command with AKSV values */
	rc = dp_hfi_send_cmd_buf(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
			HFI_PAYLOAD_TYPE_U32_ARRAY, response, sizeof(response),
			(HFI_HOST_FLAGS_RESPONSE_REQUIRED | HFI_HOST_FLAGS_NON_DISCARDABLE));
	if (rc) {
		DP_ERR("Failed to send HDCP1X_AKSV command, rc=%d\n", rc);
		return;
	}

	DP_DEBUG("Successfully sent AKSV to DCP: msb=0x%x, lsb=0x%x\n", aksv_msb, aksv_lsb);
}


static void dp_mgr_hfi_handle_hdcp1x_stop(struct dp_mgr_hfi_priv *hfi_priv,
					    void *payload, u32 size)
{
	DP_DEBUG("Received HDCP1X_STOP request from DCP\n");

	/* Call dp_hdcp to notify TrustZone */
	if (!hfi_priv->hdcp1x_ctx) {
		DP_ERR("HDCP context not initialized\n");
		return;
	}

	dp_hdcp1x_stop(hfi_priv->hdcp1x_ctx);

	dp_mgr_hfi_update_hdcp_info(hfi_priv, true);

	DP_DEBUG("HDCP stopped\n");
}

static void dp_mgr_hfi_handle_hdcp1x_enc(struct dp_mgr_hfi_priv *hfi_priv,
					  void *payload, u32 size)
{
	u32 *data = (u32 *)payload;
	bool enable;

	if (size < sizeof(u32)) {
		DP_ERR("Invalid payload size: %u\n", size);
		return;
	}

	enable = (data[0] != 0);

	DP_DEBUG("Received HDCP1X_ENC request from DCP: enable=%d\n", enable);

	/* Call dp_hdcp to notify TrustZone */
	if (!hfi_priv->hdcp1x_ctx) {
		DP_ERR("HDCP context not initialized\n");
		return;
	}

	dp_hdcp1x_set_enc(hfi_priv->hdcp1x_ctx, enable);

	if (enable) {
		hfi_priv->hdcp_info.hdcp_state = HDCP_STATE_AUTHENTICATED;
		dp_mgr_hfi_update_hdcp_info(hfi_priv, false);
	}

	DP_DEBUG("HDCP encryption %s\n", enable ? "enabled" : "disabled");
}

static void dp_mgr_hfi_handle_hdcp1x_topology(struct dp_mgr_hfi_priv *hfi_priv,
						void *payload, u32 size)
{
	u32 *data = (u32 *)payload;
	u32 depth, device_count, max_devices_exceeded, max_cascade_exceeded;

	if (size < 4 * sizeof(u32)) {
		DP_ERR("Invalid payload size: %u\n", size);
		return;
	}

	depth = data[0];
	device_count = data[1];
	max_devices_exceeded = data[2];
	max_cascade_exceeded = data[3];

	DP_DEBUG("topology update: depth=%u, devices=%u, max_dev=%u, max_cascade=%u\n",
		 depth, device_count, max_devices_exceeded, max_cascade_exceeded);

	/* Call dp_hdcp to notify TrustZone */
	if (!hfi_priv->hdcp1x_ctx) {
		DP_ERR("HDCP context not initialized\n");
		return;
	}

	dp_hdcp1x_topology_update(hfi_priv->hdcp1x_ctx, depth, device_count,
				max_devices_exceeded, max_cascade_exceeded);

	DP_DEBUG("HDCP topology updated\n");
}

static int dp_mgr_hfi_send_type_id_to_sink(struct dp_mgr_hfi_priv *priv,
					    uint8_t stream_type)
{
	struct hfi_hdcp2_message response = {0};
	struct hfi_client_t *hfi_client;
	uint8_t type_id_msg[2];
	int rc;

	hfi_client = dp_mgr_hfi_get_hfi_client(priv);
	if (!hfi_client)
		return -EINVAL;

	DP_DEBUG("Sending TYPE_ID to sink, stream_type=%u\n", stream_type);

	/* Construct TYPE_ID message */
	type_id_msg[0] = SKE_SEND_TYPE_ID;
	type_id_msg[1] = stream_type;

	/* Copy to shared response buffer */
	memcpy(priv->hdcp2x_resp_map->local_addr, type_id_msg, 2);

	/* Prepare HFI response */
	dp_mgr_hfi_init_hfi_buff(&response.request, priv->hdcp2x_req_map);
	response.request.size = 0;

	dp_mgr_hfi_init_hfi_buff(&response.response, priv->hdcp2x_resp_map);
	response.response.size = 2;

	response.timeout_ms = 100;
	response.repeater_flag = 0;

	/* Send TYPE_ID to sink via HFI */
	rc = dp_hfi_send_cmd_buf(priv->hfi, hfi_client,
				 HFI_COMMAND_DISPLAY_HDCP2X_RESPONSE,
				 "DisplayPort", HFI_PAYLOAD_TYPE_U32_ARRAY,
				 &response, sizeof(response),
				 HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc)
		DP_ERR("Failed to send TYPE_ID to sink: %d\n", rc);
	else
		DP_DEBUG("TYPE_ID sent to sink successfully\n");

	return rc;
}

static void dp_mgr_hfi_handle_hdcp2x_start(struct dp_mgr_hfi_priv *priv, void *payload, u32 size)
{
	struct hfi_hdcp2_message response = {0};
	struct hfi_client_t *hfi_client;
	uint8_t *ake_init;
	uint32_t ake_init_len;
	int rc;

	DP_DEBUG("HDCP2X_START event received from DCP\n");

	/* Clean up any previous feature supported requests/contexts if needed */
	/* This ensures we start fresh for each authentication attempt */

	if (!priv || !priv->hdcp2x_ctx) {
		DP_ERR("Invalid HDCP 2.x context\n");
		return;
	}

	/* Check if shared buffers are allocated */
	if (!priv->hdcp2x_req_map || !priv->hdcp2x_resp_map) {
		DP_ERR("HDCP 2.x shared buffers not allocated\n");
		return;
	}

	/* Call dp_hdcp to start and get AKE_INIT */
	rc = dp_hdcp2x_start(priv->hdcp2x_ctx, &ake_init, &ake_init_len);
	if (rc) {
		DP_ERR("dp_hdcp2x_start failed: %d\n", rc);
		return;
	}

	priv->hdcp_info.hdcp_version = HDCP_VERSION_2P2;
	priv->hdcp_info.hdcp_state = HDCP_STATE_AUTHENTICATING;
	dp_mgr_hfi_update_hdcp_info(priv, false);

	DP_DEBUG("HDCP2X_START: ake_init=%p, ake_init_len=%u\n", ake_init, ake_init_len);

	/* Copy AKE_INIT to shared response buffer */
	if (ake_init_len > priv->hdcp2x_resp_map->size) {
		DP_ERR("AKE_INIT too large: %u > %u\n",
		       ake_init_len, priv->hdcp2x_resp_map->size);
		return;
	}

	memcpy(priv->hdcp2x_resp_map->local_addr, ake_init, ake_init_len);

	/* Prepare HFI response */
	hfi_client = dp_mgr_hfi_get_hfi_client(priv);
	if (!hfi_client)
		return;

	/* Initialize request buffer (empty for START event) */
	dp_mgr_hfi_init_hfi_buff(&response.request, priv->hdcp2x_req_map);

	/* Initialize response buffer with AKE_INIT */
	dp_mgr_hfi_init_hfi_buff(&response.response, priv->hdcp2x_resp_map);
	response.response.size = ake_init_len;

	/* Set timeout and repeater flag */
	response.timeout_ms = 0;
	response.repeater_flag = 0;

	/* Send HFI_COMMAND_DISPLAY_HDCP2X_RESPONSE back to DCP */
	rc = dp_hfi_send_cmd_buf(priv->hfi, hfi_client,
				 HFI_COMMAND_DISPLAY_HDCP2X_RESPONSE,
				 "DisplayPort", HFI_PAYLOAD_TYPE_U32_ARRAY,
				 &response, sizeof(response),
				 HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc)
		DP_ERR("Failed to send HDCP2X_RESPONSE: %d\n", rc);
	else
		DP_DEBUG("AKE_INIT sent to DCP via HDCP2X_RESPONSE, length=%u\n", ake_init_len);
}

static void dp_mgr_hfi_handle_hdcp2x_process_msg(struct dp_mgr_hfi_priv *priv,
						 void *payload, u32 size)
{
	struct hfi_hdcp2_message *hfi_data = payload;
	struct hfi_hdcp2_message response = {0};
	struct hfi_client_t *hfi_client;
	uint8_t *req_buf;
	uint8_t *resp_buf;
	uint32_t resp_len;
	uint8_t msg_id = 0;
	bool is_repeater;
	uint8_t stream_type;
	int rc;

	DP_DEBUG("HDCP2X_PROCESS_MSG event received from DCP\n");

	if (!priv || !priv->hdcp2x_ctx || !hfi_data) {
		DP_ERR("Invalid parameters\n");
		return;
	}

	/* Check if shared buffers are allocated */
	if (!priv->hdcp2x_req_map || !priv->hdcp2x_resp_map) {
		DP_ERR("HDCP 2.x shared buffers not allocated\n");
		return;
	}

	/* Get request buffer from shared memory */
	req_buf = priv->hdcp2x_req_map->local_addr;
	if (!req_buf || hfi_data->request.size > priv->hdcp2x_req_map->size) {
		DP_ERR("Invalid request buffer\n");
		return;
	}

	/* Validate request size is reasonable */
	if (hfi_data->request.size > 0 && hfi_data->request.size < sizeof(uint8_t)) {
		DP_ERR("Invalid request size: %u\n", hfi_data->request.size);
		return;
	}

	is_repeater = hfi_data->repeater_flag;

	DP_DEBUG("Processing message: request_length=%u, repeater=%d\n",
		 hfi_data->request.size, is_repeater);

	/* Process message through dp_hdcp (TZ) */
	rc = dp_hdcp2x_process_msg(priv->hdcp2x_ctx,
				   req_buf, hfi_data->request.size,
				   &resp_buf, &resp_len);
	if (rc) {
		DP_ERR("dp_hdcp2x_process_msg failed: %d\n", rc);
		return;
	}

	/* Copy response to shared buffer (if any) */
	if (resp_len > 0) {
		if (resp_len > priv->hdcp2x_resp_map->size) {
			DP_ERR("Response too large: %u > %u\n",
			       resp_len, priv->hdcp2x_resp_map->size);
			return;
		}
		memcpy(priv->hdcp2x_resp_map->local_addr, resp_buf, resp_len);

		msg_id = resp_buf[0];
		DP_DEBUG("Response message ID: 0x%02x\n", msg_id);
	}

	/* Prepare HFI response */
	hfi_client = dp_mgr_hfi_get_hfi_client(priv);
	if (!hfi_client)
		return;

	/* Initialize request buffer (empty for response) */
	dp_mgr_hfi_init_hfi_buff(&response.request, priv->hdcp2x_req_map);
	response.request.size = 0;

	/* Initialize response buffer with TZ response */
	dp_mgr_hfi_init_hfi_buff(&response.response, priv->hdcp2x_resp_map);
	response.response.size = resp_len;

	/* Copy timeout and repeater flag from incoming message */
	response.timeout_ms = hfi_data->timeout_ms;
	response.repeater_flag = hfi_data->repeater_flag;

	/* Send HFI_COMMAND_DISPLAY_HDCP2X_RESPONSE back to DCP */
	rc = dp_hfi_send_cmd_buf(priv->hfi, hfi_client,
				 HFI_COMMAND_DISPLAY_HDCP2X_RESPONSE,
				 "DisplayPort", HFI_PAYLOAD_TYPE_U32_ARRAY,
				 &response, sizeof(response),
				 HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc)
		DP_ERR("Failed to send HDCP2X_RESPONSE: %d\n", rc);
	else
		DP_DEBUG("Response sent to DCP, length=%u\n", resp_len);

	if (msg_id == SKE_SEND_EKS && !is_repeater) {
		switch (priv->min_enc_level) {
		case 0:
		case 1:
			stream_type = 0;  /* Type 0: Standard content */
			break;
		case 2:
			stream_type = 1;  /* Type 1: Premium content (4K HDR, etc.) */
			break;
		default:
			stream_type = 0;
			break;
		}

		DP_DEBUG("Using stream_type=%u (min_enc_level=%u)\n",
			 stream_type, priv->min_enc_level);

		/* Step 1: Send SKE_SEND_TYPE_ID directly to sink via HFI */
		rc = dp_mgr_hfi_send_type_id_to_sink(priv, stream_type);
		if (rc) {
			DP_ERR("Failed to send TYPE_ID to sink: %d\n", rc);
			priv->hdcp_info.hdcp_state = HDCP_STATE_AUTH_FAIL;
			dp_mgr_hfi_update_hdcp_info(priv, false);
			return;
		}

		/* Step 2: Enable encryption in TZ */
		rc = dp_hdcp2x_enable_encryption(priv->hdcp2x_ctx);
		if (rc) {
			DP_ERR("Failed to enable encryption: %d\n", rc);
			priv->hdcp_info.hdcp_state = HDCP_STATE_AUTH_FAIL;
			dp_mgr_hfi_update_hdcp_info(priv, false);
			return;
		}

		/* Step 3: Update status */
		priv->hdcp_info.hdcp_state = HDCP_STATE_AUTHENTICATED;
		dp_mgr_hfi_update_hdcp_info(priv, false);

		DP_DEBUG("HDCP 2.x authentication completed (non-repeater)\n");
	} else if (msg_id == REP_STREAM_READY && is_repeater) {
		rc = dp_hdcp2x_enable_encryption(priv->hdcp2x_ctx);
		if (rc) {
			DP_ERR("Failed to enable encryption: %d\n", rc);
			priv->hdcp_info.hdcp_state = HDCP_STATE_AUTH_FAIL;
		} else {
			priv->hdcp_info.hdcp_state = HDCP_STATE_AUTHENTICATED;
		}
		dp_mgr_hfi_update_hdcp_info(priv, false);

		DP_DEBUG("HDCP 2.x authentication completed (repeater)\n");
	}
}

static void dp_mgr_hfi_handle_hdcp2x_timeout(struct dp_mgr_hfi_priv *priv,
					      void *payload, u32 size)
{
	struct hfi_hdcp2_message response = {0};
	struct hfi_client_t *hfi_client;
	uint8_t *req_buf;
	uint8_t *resp_buf;
	uint32_t resp_len;
	int rc;

	DP_DEBUG("HDCP2X_TIMEOUT event received from DCP\n");

	if (!priv || !priv->hdcp2x_ctx) {
		DP_ERR("Invalid HDCP 2.x context\n");
		return;
	}

	/* Payload should be empty for timeout event */
	if (payload || size != 0) {
		DP_ERR("Unexpected payload for timeout event\n");
		return;
	}

	/* Check if shared buffers are allocated */
	if (!priv->hdcp2x_req_map || !priv->hdcp2x_resp_map) {
		DP_ERR("HDCP 2.x shared buffers not allocated\n");
		return;
	}

	/* Get buffer pointers from shared memory */
	req_buf = priv->hdcp2x_req_map->local_addr;

	/* Call TZ with TIMEOUT - pass buffer pointers so TZ can populate response */
	rc = dp_hdcp2x_timeout(priv->hdcp2x_ctx,
			       req_buf, 0,  /* request is empty (length=0) but pass pointer */
			       &resp_buf, &resp_len);
	if (rc) {
		DP_ERR("dp_hdcp2x_timeout failed: %d\n", rc);

		priv->hdcp_info.hdcp_state = HDCP_STATE_AUTH_FAIL;
		dp_mgr_hfi_update_hdcp_info(priv, false);
		return;
	}

	/* Copy response to shared buffer if TZ returned something */
	if (resp_len > 0) {
		if (resp_len > priv->hdcp2x_resp_map->size) {
			DP_ERR("Response too large: %u > %u\n",
			       resp_len, priv->hdcp2x_resp_map->size);
			return;
		}
		memcpy(priv->hdcp2x_resp_map->local_addr, resp_buf, resp_len);
	}

	/* Get HFI client */
	hfi_client = dp_mgr_hfi_get_hfi_client(priv);
	if (!hfi_client)
		return;

	/* Prepare HFI response with whatever TZ put in app_data */
	dp_mgr_hfi_init_hfi_buff(&response.request, priv->hdcp2x_req_map);
	response.request.size = 0;

	dp_mgr_hfi_init_hfi_buff(&response.response, priv->hdcp2x_resp_map);
	response.response.size = resp_len;

	response.timeout_ms = 0;
	response.repeater_flag = 0;

	/* Send response back to DCP */
	rc = dp_hfi_send_cmd_buf(priv->hfi, hfi_client,
				 HFI_COMMAND_DISPLAY_HDCP2X_RESPONSE,
				 "DisplayPort", HFI_PAYLOAD_TYPE_U32_ARRAY,
				 &response, sizeof(response),
				 HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc)
		DP_ERR("Failed to send timeout response: %d\n", rc);
	else
		DP_DEBUG("Timeout response sent to DCP, length=%u\n", resp_len);
}

static void dp_mgr_hfi_handle_hdcp_feature_supported(struct dp_mgr_hfi_priv *hfi_priv,
						     void *payload, u32 size)
{
	struct hfi_client_t *hfi_client;
	u32 response[2] = {0, 0}; // [hdcp1x_supported, hdcp2x_supported]
	bool hdcp1x_tz_support = false;
	bool hdcp2x_tz_support = false;
	int rc;
	u32 *hdcp_support = (u32 *)payload;
	u32 hfi_event;

	/* Get HFI client first as we'll need it for buffer allocation */
	hfi_client = dp_mgr_hfi_get_hfi_client(hfi_priv);
	if (!hfi_client) {
		DP_ERR("Failed to get HFI client\n");
		return;
	}

	hfi_priv->hdcp_info.source_cap = 0;
	if (hdcp_support[0])
		hfi_priv->hdcp_info.source_cap |= HDCP_VERSION_1X;
	if (hdcp_support[1])
		hfi_priv->hdcp_info.source_cap |= HDCP_VERSION_2P2;

	/* Initialize HDCP contexts if not already done */
	if (!hfi_priv->hdcp1x_ctx && hdcp_support[0]) {
		struct sde_hdcp_init_data init_data = {
			.msm_hdcp_dev = hfi_priv->msm_hdcp_dev,
			.client_id = HDCP_CLIENT_DP,
		};

		hfi_priv->hdcp1x_ctx = dp_hdcp1x_init(&init_data);
		if (!hfi_priv->hdcp1x_ctx) {
			DP_WARN("HDCP init failed, continuing without HDCP\n");
			hfi_priv->hdcp_info.source_cap &= ~HDCP_VERSION_1X;
		} else {
			DP_INFO("HDCP initialized successfully\n");
		}
	}
	if (!hfi_priv->hdcp2x_ctx && hdcp_support[1]) {
		hfi_priv->hdcp2x_ctx = dp_hdcp2x_init();
		if (!hfi_priv->hdcp2x_ctx) {
			DP_WARN("HDCP 2.x init failed, continuing without HDCP 2.x\n");
			hfi_priv->hdcp_info.source_cap &= ~HDCP_VERSION_2P2;
		} else {
			DP_INFO("HDCP 2.x initialized successfully\n");

			/* Allocate shared memory buffers for HDCP 2.x message exchange */
			hfi_priv->hdcp2x_req_map = dp_mgr_hfi_init_shared_addr(hfi_client, SZ_4K);
			if (!hfi_priv->hdcp2x_req_map) {
				DP_ERR("Failed to allocate HDCP 2.x request buffer\n");
				dp_hdcp2x_deinit(hfi_priv->hdcp2x_ctx);
				hfi_priv->hdcp2x_ctx = NULL;
				hfi_priv->hdcp_info.source_cap &= ~HDCP_VERSION_2P2;
			} else {
				hfi_priv->hdcp2x_resp_map = dp_mgr_hfi_init_shared_addr(hfi_client,
					SZ_4K);
				if (!hfi_priv->hdcp2x_resp_map) {
					DP_ERR("Failed to allocate HDCP 2.x response buffer\n");
					dp_mgr_init_deinit_shared_addr(hfi_client,
						hfi_priv->hdcp2x_req_map);
					hfi_priv->hdcp2x_req_map = NULL;
					dp_hdcp2x_deinit(hfi_priv->hdcp2x_ctx);
					hfi_priv->hdcp2x_ctx = NULL;
					hfi_priv->hdcp_info.source_cap &= ~HDCP_VERSION_2P2;
				} else {
					DP_DEBUG("HDCP 2.x shared buffers allocated\n");
				}
			}
		}
	}

	/* Check TrustZone support for both versions */
	if (hfi_priv->hdcp1x_ctx)
		hdcp1x_tz_support = dp_hdcp1x_feature_supported(hfi_priv->hdcp1x_ctx);
	if (hfi_priv->hdcp2x_ctx)
		hdcp2x_tz_support = dp_hdcp2x_feature_supported(hfi_priv->hdcp2x_ctx);

	response[0] = hdcp1x_tz_support ? 1 : 0;
	response[1] = hdcp2x_tz_support ? 1 : 0;

	/* Set version based on priority: prefer HDCP 2.2 if both supported */
	if (hdcp2x_tz_support)
		hfi_priv->hdcp_info.hdcp_version = HDCP_VERSION_2P2;
	else if (hdcp1x_tz_support)
		hfi_priv->hdcp_info.hdcp_version = HDCP_VERSION_1X;
	else {
		hfi_priv->hdcp_info.hdcp_version = HDCP_VERSION_NONE;
		hfi_priv->hdcp_info.hdcp_state = HDCP_STATE_INACTIVE;
	}

	/* Update debug buffer */
	dp_mgr_hfi_update_hdcp_info(hfi_priv, false);


	if (hfi_priv->hdcp_info.hdcp_version != HDCP_VERSION_NONE) {
		rc = _register_hdcp_events(hfi_priv, true, hfi_priv->hdcp_info.hdcp_version);
		if (rc) {
			DP_ERR("Failed to register HDCP events, rc=%d\n", rc);
			return;
		}
	} else {
		hfi_event = HFI_EVENT_HDCP_FEATURE_SUPPORTED;
		rc = dp_hfi_start_batch_cmd(hfi_priv->hfi, hfi_client,
						HFI_COMMAND_DISPLAY_EVENT_DEREGISTER,
						"DisplayPort", HFI_PAYLOAD_TYPE_U32,
						&hfi_event, sizeof(u32),
						HFI_HOST_FLAGS_NON_DISCARDABLE);
		if (rc)
			DP_ERR("Failed to deregister HDCP feature supported event, rc=%d\n", rc);
	}

	rc = dp_hfi_end_batch_cmd(hfi_priv->hfi, hfi_client,
					     HFI_COMMAND_DISPLAY_HDCP_FEATURE_SUPPORTED,
					     "DisplayPort", HFI_PAYLOAD_TYPE_U32_ARRAY,
					     response, sizeof(response),
					     HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc) {
		DP_ERR("Failed to start batch for feature supported response\n");
		return;
	}

	DP_DEBUG("HDCP feature supported response sent: 1x=%d, 2x=%d\n",
		 response[0], response[1]);
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
	case HFI_COMMAND_DISPLAY_EVENT_HDCP1X_START:
		dp_mgr_hfi_handle_hdcp1x_start(hfi_priv, payload, size);
		break;
	case HFI_COMMAND_DISPLAY_EVENT_HDCP1X_STOP:
		dp_mgr_hfi_handle_hdcp1x_stop(hfi_priv, payload, size);
		break;
	case HFI_COMMAND_DISPLAY_EVENT_HDCP1X_ENC:
		dp_mgr_hfi_handle_hdcp1x_enc(hfi_priv, payload, size);
		break;
	case HFI_COMMAND_DISPLAY_EVENT_HDCP1X_TOPOLOGY_UPDATE:
		dp_mgr_hfi_handle_hdcp1x_topology(hfi_priv, payload, size);
		break;
	case HFI_COMMAND_DISPLAY_EVENT_HDCP2X_START:
		dp_mgr_hfi_handle_hdcp2x_start(hfi_priv, payload, size);
		break;
	case HFI_COMMAND_DISPLAY_EVENT_HDCP2X_PROCESS_MSG:
		dp_mgr_hfi_handle_hdcp2x_process_msg(hfi_priv, payload, size);
		break;
	case HFI_COMMAND_DISPLAY_EVENT_HDCP2X_TIMEOUT:
		dp_mgr_hfi_handle_hdcp2x_timeout(hfi_priv, payload, size);
		break;
	case HFI_COMMAND_DISPLAY_EVENT_HDCP_FEATURE_SUPPORTED:
		dp_mgr_hfi_handle_hdcp_feature_supported(hfi_priv, payload, size);
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

	/* Initialize HFI audio subsystem */
	hfi_priv->audio = dp_hfi_audio_get(hfi_priv->pdev, client);
	if (IS_ERR(hfi_priv->audio)) {
		rc = PTR_ERR(hfi_priv->audio);
		DP_ERR("dp_hfi_audio_get failed: %d\n", rc);
		hfi_priv->audio = NULL;
	} else {
		DP_DEBUG("HFI audio initialized successfully\n");
	}

	/* Register min_enc_level callback */
	if (IS_ENABLED(CONFIG_HDCP_QSEECOM) && hfi_priv->msm_hdcp_dev) {
		msm_hdcp_register_cb(hfi_priv->msm_hdcp_dev, hfi_priv,
				    dp_mgr_hfi_min_level_change);
		DP_DEBUG("Registered HDCP min_enc_level callback\n");
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
	struct dp_mgr_hfi_priv *hfi_priv;
	struct sde_kms *sde_kms;
	struct sde_connector *sde_conn;
	struct hfi_kms *hfi_kms;
	struct hfi_client_t *hfi_client;
	struct hfi_display_hdr_cfg hdr_cfg;
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_CONFIG_HDR;
	int rc = 0;

	if (!client) {
		DP_ERR("Invalid params\n");
		return -EINVAL;
	}

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);

	/* Get HFI client */
	sde_kms = sde_connector_get_kms(client->base_connector);
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

	sde_conn = to_sde_connector(client->base_connector);
	if (!sde_conn) {
		DP_ERR("Failed to get SDE connector\n");
		return -EINVAL;
	}

	/* Check if display is enabled */
	if (!hfi_priv->connected) {
		DP_DEBUG("Display not connected, skipping HDR config\n");
		return 0;
	}

	/* Populate HDR configuration payload */
	memset(&hdr_cfg, 0, sizeof(hdr_cfg));

	if (hdr_meta) {
		hdr_cfg.hdr_meta.hdr_state = hdr_meta->hdr_state;
		hdr_cfg.hdr_meta.eotf = hdr_meta->eotf;
		hdr_cfg.hdr_meta.hdr_supported = hdr_meta->hdr_supported;
		memcpy(hdr_cfg.hdr_meta.display_primaries_x, hdr_meta->display_primaries_x,
			sizeof(hdr_cfg.hdr_meta.display_primaries_x));
		memcpy(hdr_cfg.hdr_meta.display_primaries_y, hdr_meta->display_primaries_y,
			sizeof(hdr_cfg.hdr_meta.display_primaries_y));
		hdr_cfg.hdr_meta.white_point_x = hdr_meta->white_point_x;
		hdr_cfg.hdr_meta.white_point_y = hdr_meta->white_point_y;
		hdr_cfg.hdr_meta.max_luminance = hdr_meta->max_luminance;
		hdr_cfg.hdr_meta.min_luminance = hdr_meta->min_luminance;
		hdr_cfg.hdr_meta.max_content_light_level = hdr_meta->max_content_light_level;
		hdr_cfg.hdr_meta.max_average_light_level = hdr_meta->max_average_light_level;

		DP_DEBUG("HDR config: state=%u, eotf=%u, supported=%u\n",
			hdr_cfg.hdr_meta.hdr_state, hdr_cfg.hdr_meta.eotf,
			hdr_cfg.hdr_meta.hdr_supported);

		/* Copy dynamic HDR (HDR10+) payload if dhdr_update is true */
		if (dhdr_update && client->base_connector) {
			struct sde_connector_state *c_state = to_sde_connector_state(
				client->base_connector->state);

			if (c_state && c_state->dyn_hdr_meta.dynamic_hdr_payload_size > 0) {
				u32 payload_size = min_t(u32,
					c_state->dyn_hdr_meta.dynamic_hdr_payload_size,
					HFI_DHDR_PAYLOAD_MAX_SIZE);

				hdr_cfg.dynamic_hdr_payload_size = payload_size;
				memcpy(hdr_cfg.dynamic_hdr_payload,
				       c_state->dyn_hdr_meta.dynamic_hdr_payload,
				       payload_size);

				DP_DEBUG("Copied %u bytes of dynamic HDR metadata to HFI payload\n",
					payload_size);
			} else {
				DP_DEBUG("No dynamic HDR payload available in connector state\n");
			}
		}
	} else {
		DP_ERR("Not sending CONFIG_HDR command, null hdr static metadata\n");
		rc = -EINVAL;
		goto out;
	}

	/* Send HFI command */
	rc = dp_hfi_send_cmd_buf(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
			HFI_PAYLOAD_TYPE_U32_ARRAY, &hdr_cfg, sizeof(hdr_cfg),
			HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc) {
		DP_ERR("Failed to send CONFIG_HDR command, rc=%d\n", rc);
	} else {
		DP_DEBUG("Successfully sent HDR config for panel_id=%d, dhdr_update=%d\n",
			panel_id, dhdr_update);
	}

out:
	return rc;
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

	DP_DEBUG("Initiating DISPLAY ENABLE HFI command to DCP, panel_id=%d\n",
			panel_id);

	sde_kms = sde_connector_get_kms(client->base_connector);
	if (!sde_kms)
		return -EINVAL;

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms)
		return -EINVAL;

	hfi_client = &hfi_kms->hfi_client;

	rc = dp_hfi_end_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
				  HFI_PAYLOAD_TYPE_NONE, NULL, 0,
				  HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc) {
		DP_ERR("failed to send enable, rc=%d\n", rc);
		goto error;
	} else {
		DP_DEBUG("enable successful for panel_id=%d\n", panel_id);
	}

error:
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

	sde_kms = sde_connector_get_kms(client->base_connector);
	if (!sde_kms)
		return -EINVAL;

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms)
		return -EINVAL;

	hfi_client = &hfi_kms->hfi_client;

	/* turn off audio if still enabled */
	if (hfi_priv->audio)
		hfi_priv->audio->off(hfi_priv->audio, false);

	DP_DEBUG("Sending DISPLAY_DISABLE command to DCP, panel_id=%d\n", panel_id);

	rc = dp_hfi_send_cmd_buf(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
			HFI_PAYLOAD_TYPE_NONE, NULL, 0,
			(HFI_HOST_FLAGS_NON_DISCARDABLE));
	if (rc) {
		DP_ERR("failed to send disable, rc=%d\n", rc);
	} else {
		DP_DEBUG("disable successful for panel_id=%d\n",
				panel_id);
	}

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

	sde_kms = sde_connector_get_kms(client->base_connector);
	if (!sde_kms)
		return -EINVAL;

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms)
		return -EINVAL;

	hfi_client = &hfi_kms->hfi_client;

	/* Deinitialize HDCP */
	if (hfi_priv->hdcp1x_ctx) {
		dp_hdcp1x_deinit(hfi_priv->hdcp1x_ctx);
		hfi_priv->hdcp1x_ctx = NULL;
		DP_DEBUG("HDCP deinitialized\n");
	}

	/* Deinitialize HDCP 2.x and free shared buffers */
	if (hfi_priv->hdcp2x_ctx) {
		dp_hdcp2x_deinit(hfi_priv->hdcp2x_ctx);
		hfi_priv->hdcp2x_ctx = NULL;
		DP_DEBUG("HDCP 2.x deinitialized\n");
	}

	/* Free HDCP 2.x shared buffers */
	if (hfi_priv->hdcp2x_req_map) {
		dp_mgr_init_deinit_shared_addr(hfi_client, hfi_priv->hdcp2x_req_map);
		hfi_priv->hdcp2x_req_map = NULL;
		DP_DEBUG("HDCP 2.x request buffer freed\n");
	}
	if (hfi_priv->hdcp2x_resp_map) {
		dp_mgr_init_deinit_shared_addr(hfi_client, hfi_priv->hdcp2x_resp_map);
		hfi_priv->hdcp2x_resp_map = NULL;
		DP_DEBUG("HDCP 2.x response buffer freed\n");
	}

	DP_DEBUG("Sending DISPLAY_POST_DISABLE command to DCP, panel_id=%d\n",
			panel_id);

	if (hfi_priv->hdcp_info.hdcp_version != HDCP_VERSION_NONE) {
		rc = dp_hfi_start_batch_cmd(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
				HFI_PAYLOAD_TYPE_NONE, NULL, 0,
				(HFI_HOST_FLAGS_NON_DISCARDABLE));
		if (rc) {
			DP_ERR("failed to send post disable, rc=%d, panel_id=%d\n",
					rc, panel_id);
		} else {
			DP_DEBUG("post disable successful for panel_id=%d\n",
					panel_id);
		}

		rc = _register_hdcp_events(hfi_priv, false, hfi_priv->hdcp_info.hdcp_version);
		if (rc)
			DP_ERR("Failed to deregister HDCP events, rc=%d\n", rc);
	} else {
		rc = dp_hfi_send_cmd_buf(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
				HFI_PAYLOAD_TYPE_NONE, NULL, 0,
				(HFI_HOST_FLAGS_NON_DISCARDABLE));
		if (rc) {
			DP_ERR("failed to send post disable, rc=%d, panel_id=%d\n",
					rc, panel_id);
		} else {
			DP_DEBUG("post disable successful for panel_id=%d\n",
					panel_id);
		}
	}

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
	struct dp_mgr_hfi_priv *hfi_priv;

	if (!client || !dp_mode || !drm_mode)
		return;

	hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);
	if (!hfi_priv) {
		DP_ERR("invalid param(s), hfi_priv %pK\n", hfi_priv);
		return;
	}

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

	dp_mode->timing.bpp = hfi_priv->tgt_bpp;

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
	struct hfi_client_t *hfi_client;
	struct dp_mgr_hfi_priv *hfi_priv = container_of(client, struct dp_mgr_hfi_priv, client);
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_SET_COLORSPACE;
	int rc = 0;

	hfi_client = dp_mgr_hfi_get_hfi_client(hfi_priv);
	if (!hfi_client)
		return -EINVAL;

	rc = dp_hfi_send_cmd_buf(hfi_priv->hfi, hfi_client, hfi_cmd, "DisplayPort",
			HFI_PAYLOAD_TYPE_U32_ARRAY, (void *)&colorspace,
			sizeof(colorspace), (HFI_HOST_FLAGS_NON_DISCARDABLE));

	if (rc)
		DP_ERR("Could not send HFI_COMMAND_DISPLAY_SET_COLORSPACE, rc=%d\n", rc);

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

struct dp_client *dp_mgr_hfi_init(struct platform_device *pdev, struct dp_debug_client *debug)
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
	hfi_priv->msm_hdcp_dev = dp_hdcp_get_msm_hdcp_dev();

	client = &hfi_priv->client;
	drm_ops = &client->drm_ops;

	hfi_priv->debug = debug;

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

	init_completion(&hfi_priv->hpd_comp);
	hfi_priv->hdcp_info.hdcp_state = HDCP_STATE_INACTIVE;
	hfi_priv->hdcp_info.hdcp_version = HDCP_VERSION_NONE;
	hfi_priv->hdcp_info.source_cap = 0;

	/* Initialize min_enc_level to default (standard content) */
	hfi_priv->min_enc_level = 0;

	DP_INFO("DP HFI display initialized successfully\n");
	return client;
bail:
	return ERR_PTR(rc);
}
