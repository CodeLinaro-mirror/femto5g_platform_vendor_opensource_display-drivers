// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>

#include "msm_drv.h"
#include "msm_mmu.h"
#include "hfi_msm_drv.h"
#include "hfi_connector.h"
#include "dp_drm.h"
#include "dp_mgr_hfi.h"
#include "dp_drv.h"
#include "dp_hfi.h"
#include "hfi_adapter.h"
#include "hfi_props.h"
#include "hfi_kms.h"
#include "hfi_commands_device.h"
#include "dp_debug.h"
#include "sde_hdcp.h"

#define NUM_VSWING_VAL          16
#define NUM_BW_CODE             4
#define DP_LINK_RATE_HBR	10
#define DP_AUX_CFG_BASE_OFFSET	0x20

static int _dp_hfi_process_ssr_start(struct hfi_client_t *hfi_client)
{
	struct dp_hfi *hfi;
	int rc = 0;

	if (!hfi_client) {
		DP_ERR("invalid client\n");
		return -EINVAL;
	}

	hfi = hfi_client->priv;
	if (!hfi) {
		DP_ERR("invalid display hfi handle\n");
		return -EINVAL;
	}

	if (!hfi->shared_addr_map)
		DP_DEBUG("shared addr map is null\n");
	else if (hfi->shared_addr_map->remote_addr ||
			hfi->shared_addr_map->local_addr)
		hfi_adapter_buffer_dealloc(hfi_client, hfi->shared_addr_map);

	rc = hfi_adapter_release_all_cmd_bufs(hfi_client);
	if (rc) {
		DP_ERR("failed to release command buffers, rc: %d\n", rc);
		return rc;
	}

	return rc;
}

static int _dp_hfi_process_ssr_end(struct hfi_client_t *hfi_client)
{
	struct dp_hfi *hfi;
	int rc = 0;

	if (!hfi_client) {
		DP_ERR("invalid client\n");
		return -EINVAL;
	}

	hfi = hfi_client->priv;
	if (!hfi) {
		DP_ERR("invalid display hfi handle\n");
		return -EINVAL;
	}

	return rc;
}

static int dp_hfi_process_event(struct hfi_client_t *hfi_client, enum hfi_adapter_event_type event,
			bool blocking)
{
	if (!hfi_client) {
		DP_ERR("invalid client\n");
		return -EINVAL;
	}

	switch (event) {
	case HFI_ADAPTER_EVENT_SSR_START:
		return _dp_hfi_process_ssr_start(hfi_client);
	case HFI_ADAPTER_EVENT_SSR_END:
		return _dp_hfi_process_ssr_end(hfi_client);
	default:
		DP_ERR("%s: invalid event type: %d\n", __func__, event);
		return -EINVAL;
	}

	return 0;
}

static int dp_hfi_process_cmd_buf(struct hfi_client_t *hfi_client, struct hfi_cmdbuf_t *cmd_buf)
{
	int rc = 0;

	if (!hfi_client || !cmd_buf) {
		DP_ERR("Invalid client or buffer\n");
		return -EINVAL;
	}

	rc = hfi_adapter_unpack_cmd_buf(hfi_client, cmd_buf);
	if (rc) {
		DP_ERR("[WARNING] Error in response packet or unpacking buffer\n");
		return rc;
	}

	rc = hfi_adapter_release_cmd_buf(hfi_client, cmd_buf);
	if (rc)
		DP_ERR("[WARNING] Failed to release command buffer\n");

	return rc;
}

static void dp_hfi_prop_handler(u32 hfi_uid, u32 prop, void *payload, u32 size,
			  struct hfi_prop_listener *listener)
{
	struct dp_hfi *hfi;
	u32 dp_display_obj_id;

	if (!listener) {
		DP_ERR("invalid listener\n");
		return;
	}

	hfi = container_of(listener, struct dp_hfi, hfi_cb_obj);

	if (!hfi) {
		DP_ERR("invalid hfi\n");
		return;
	}

	dp_display_obj_id = sde_conn_get_display_obj_id(hfi->connector);
	if (dp_display_obj_id != hfi_uid) {
		DP_ERR("Component and HFI ID mismatch (%d != %d)\n",
				dp_display_obj_id, hfi_uid);
		return;
	}

	switch (prop) {
	case HFI_COMMAND_DISPLAY_AUDIO_CONFIG:
		DP_DEBUG("Audio config command acknowledged\n");
		break;

	case HFI_COMMAND_DISPLAY_AUDIO_CONTROL:
		DP_DEBUG("Audio control command acknowledged\n");
		break;

	case HFI_COMMAND_DISPLAY_MODE_VALIDATE:
		if (payload)
			hfi->mode_valid = true;
		break;
	default:
		hfi->handle_event(hfi, prop, payload, size);
		break;
	}
}

static int dp_hfi_setup_client(struct dp_hfi *hfi,	struct hfi_adapter_t *hfi_host)
{
	int rc = 0;

	if (!hfi_host || !hfi) {
		DP_ERR("invalid data\n");
		return -EINVAL;
	}

	/* Initialize hfi structure */
	hfi->tx_cmd_buf_dva = 0;
	hfi->tx_cmd_buf_fill_level = 0;
	hfi->hfi_adapter = hfi_host;

	hfi->hfi_client = kmalloc(sizeof(struct hfi_client_t), GFP_KERNEL);
	if (!hfi->hfi_client)
		return -ENOMEM;

	hfi->hfi_client->process_cmd_buf = dp_hfi_process_cmd_buf;
	hfi->hfi_client->process_event = dp_hfi_process_event;
	hfi->hfi_cb_obj.hfi_prop_handler = dp_hfi_prop_handler;
	hfi->hfi_client->priv = hfi;

	rc = hfi_adapter_client_register(hfi_host, hfi->hfi_client);
	if (rc) {
		DP_ERR("unable to register hfi client\n");
		kfree(hfi->hfi_client);
		return -ENODEV;
	}

	return 0;
}

static int _pack_cmd(struct dp_hfi *hfi, struct hfi_client_t *hfi_client,
		struct hfi_cmdbuf_t *cmd_buf, u32 hfi_cmd, u32 obj_id,
		u32 hfi_payload_type, void *payload, u32 payload_size, u32 flags)
{
	int rc = 0;
	bool remove_on_cb = false;
	u32 packet_id = 0;

	switch (hfi_cmd) {
	case HFI_COMMAND_DISPLAY_MODE_VALIDATE:
		if (payload && hfi)
			hfi->mode_valid = true;
		flags = HFI_HOST_FLAGS_NONE;
		break;
	case HFI_COMMAND_DISPLAY_SET_MODE:
	case HFI_COMMAND_DISPLAY_ENABLE:
	case HFI_COMMAND_DISPLAY_DISABLE:
	case HFI_COMMAND_DISPLAY_EVENT_REGISTER:
	case HFI_COMMAND_DISPLAY_EVENT_DEREGISTER:
		flags |= HFI_HOST_FLAGS_RESPONSE_REQUIRED;
		break;
	default:
		break;
	}
	remove_on_cb = ((hfi_cmd != HFI_COMMAND_DISPLAY_EVENT_REGISTER)
			&& (hfi_cmd != HFI_COMMAND_DISPLAY_EVENT_DEREGISTER));
	DP_INFO("hfi_cmd=0x%x, obj_id=0x%x, flags=0x%x\n", hfi_cmd, obj_id, flags);

	if (flags & HFI_HOST_FLAGS_RESPONSE_REQUIRED) {
		rc = hfi_adapter_add_get_property(hfi_client, cmd_buf, hfi_cmd, obj_id,
			hfi_payload_type, payload, payload_size, &hfi->hfi_cb_obj, flags,
			remove_on_cb, &packet_id);
		if (rc)
			DP_ERR("could not set property for hfi_cmd 0x%x\n", hfi_cmd);
	} else {
		rc = hfi_adapter_add_set_property(hfi_client, cmd_buf, hfi_cmd,
			obj_id, hfi_payload_type, payload, payload_size, flags);
		if (rc)
			DP_ERR("could not set property for hfi_cmd 0x%x\n", hfi_cmd);
	}

	return rc;
}

int dp_hfi_send_cmd_buf(struct dp_hfi *hfi,
	struct hfi_client_t *hfi_client, u32 hfi_cmd,
	const char *display_type, u32 hfi_payload_type,
	void *payload, u32 payload_size, u32 flags)
{
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	struct drm_connector *drm_conn;
	enum hfi_cmdbuf_type cmd_buf_type = HFI_CMDBUF_TYPE_DISPLAY_INFO_NO_BLOCK;
	int rc = 0;
	u32 obj_id, packet_id = 0;
	bool remove_on_cb = false;

	if (!hfi || !hfi_client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	drm_conn = hfi->connector;
	obj_id = sde_conn_get_display_obj_id(drm_conn);

	switch (hfi_cmd) {
	case HFI_COMMAND_DEVICE_HOT_PLUG_DETECT:
		cmd_buf_type = HFI_CMDBUF_TYPE_DEVICE_INFO;
		obj_id = 0; // this is sent to device 0
		break;
	case HFI_COMMAND_DISPLAY_MODE_VALIDATE:
		if (payload && hfi)
			hfi->mode_valid = true;
		flags = HFI_HOST_FLAGS_NONE;
		break;
	case HFI_COMMAND_DISPLAY_SET_MODE:
	case HFI_COMMAND_DISPLAY_ENABLE:
	case HFI_COMMAND_DISPLAY_DISABLE:
	case HFI_COMMAND_DISPLAY_EVENT_REGISTER:
		flags |= HFI_HOST_FLAGS_RESPONSE_REQUIRED;
		break;
	default:
		break;
	}

	if (flags & HFI_HOST_FLAGS_RESPONSE_REQUIRED)
		cmd_buf_type = HFI_CMDBUF_TYPE_DISPLAY_INFO_BLOCKING;

	DP_INFO("hfi_cmd=0x%x, obj_id=0x%x, cmd_buf_type=%d, flags=0x%x\n",
		hfi_cmd, obj_id, cmd_buf_type, flags);

	cmd_buf = hfi_adapter_get_cmd_buf(hfi_client, obj_id, cmd_buf_type);
	if (!cmd_buf) {
		DP_ERR("could not get cmd_buf for hfi_cmd 0x%x\n", hfi_cmd);
		return -ENODEV;
	}

	if (flags & HFI_HOST_FLAGS_RESPONSE_REQUIRED) {
		remove_on_cb = ((hfi_cmd != HFI_COMMAND_DISPLAY_EVENT_REGISTER)
				&& (hfi_cmd != HFI_COMMAND_DISPLAY_EVENT_DEREGISTER));
		rc = hfi_adapter_add_get_property(hfi_client, cmd_buf, hfi_cmd, obj_id,
			hfi_payload_type, payload, payload_size, &hfi->hfi_cb_obj, flags,
			remove_on_cb, &packet_id);
		if (rc)
			DP_ERR("could not set property for hfi_cmd 0x%x\n", hfi_cmd);

		SDE_EVT32(obj_id, hfi_cmd, SDE_EVTLOG_FUNC_CASE1);
		rc = hfi_adapter_set_cmd_buf_blocking(hfi_client, cmd_buf);
		SDE_EVT32(obj_id, hfi_cmd, rc, SDE_EVTLOG_FUNC_CASE2);
	} else {
		rc = hfi_adapter_add_set_property(hfi_client, cmd_buf, hfi_cmd,
			obj_id, hfi_payload_type, payload, payload_size, flags);
		if (rc)
			DP_ERR("could not set property for hfi_cmd 0x%x\n", hfi_cmd);

		rc = hfi_adapter_set_cmd_buf(hfi_client, cmd_buf);
		SDE_EVT32(obj_id, hfi_cmd, rc, SDE_EVTLOG_FUNC_CASE3);
	}

	if (rc) {
		DP_ERR("failed to send hfi_cmd 0x%x\n", hfi_cmd);
		return rc;
	}

	return rc;
}

int dp_hfi_start_batch_cmd(struct dp_hfi *hfi, struct hfi_client_t *hfi_client)
{
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	struct drm_connector *drm_conn;
	enum hfi_cmdbuf_type cmd_buf_type = HFI_CMDBUF_TYPE_DISPLAY_INFO_BLOCKING;
	u32 obj_id;

	if (!hfi || !hfi_client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	drm_conn = hfi->connector;
	obj_id = sde_conn_get_display_obj_id(drm_conn);
	cmd_buf = hfi_adapter_get_cmd_buf(hfi_client, obj_id, cmd_buf_type);
	if (!cmd_buf) {
		DP_ERR("could not get cmd_buf\n");
		return -ENODEV;
	}
	hfi->batch_cmd_buf = cmd_buf;

	return 0;
}

int dp_hfi_append_batch_cmd(struct dp_hfi *hfi,
				struct hfi_client_t *hfi_client, u32 hfi_cmd,
				const char *display_type, u32 hfi_payload_type,
				void *payload, u32 payload_size, u32 flags)
{
	struct drm_connector *drm_conn;
	u32 obj_id;

	if (!hfi || !hfi_client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	drm_conn = hfi->connector;
	obj_id = sde_conn_get_display_obj_id(drm_conn);

	return _pack_cmd(hfi, hfi_client, hfi->batch_cmd_buf, hfi_cmd, obj_id, hfi_payload_type,
			payload, payload_size, flags);
}

int dp_hfi_end_batch_cmd(struct dp_hfi *hfi,
				struct hfi_client_t *hfi_client, u32 hfi_cmd,
				const char *display_type, u32 hfi_payload_type,
				void *payload, u32 payload_size, u32 flags)
{
	struct drm_connector *drm_conn;
	int rc = 0;
	u32 obj_id;

	if (!hfi || !hfi_client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	drm_conn = hfi->connector;
	obj_id = sde_conn_get_display_obj_id(drm_conn);
	flags |= HFI_HOST_FLAGS_RESPONSE_REQUIRED;

	rc = _pack_cmd(hfi, hfi_client, hfi->batch_cmd_buf, hfi_cmd, obj_id, hfi_payload_type,
			payload, payload_size, flags);

	SDE_EVT32(obj_id, hfi_cmd, SDE_EVTLOG_FUNC_CASE1);
	rc = hfi_adapter_set_cmd_buf_blocking(hfi_client, hfi->batch_cmd_buf);
	SDE_EVT32(obj_id, hfi_cmd, rc, SDE_EVTLOG_FUNC_CASE2);

	return rc;
}

int dp_hfi_send_batch_cmd(struct dp_hfi *hfi, struct hfi_client_t *hfi_client, bool blocking)
{
	struct drm_connector *drm_conn;
	int rc = 0;
	u32 obj_id;

	if (!hfi || !hfi_client) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	drm_conn = hfi->connector;
	obj_id = sde_conn_get_display_obj_id(drm_conn);

	SDE_EVT32(obj_id, SDE_EVTLOG_FUNC_CASE1);
	if (blocking)
		rc = hfi_adapter_set_cmd_buf_blocking(hfi_client, hfi->batch_cmd_buf);
	else
		rc = hfi_adapter_set_cmd_buf(hfi_client, hfi->batch_cmd_buf);
	SDE_EVT32(obj_id, rc, SDE_EVTLOG_FUNC_CASE2);

	return rc;
}

/*
 * _append_aux_cfg - Pack AUX config KV pairs into kv_props.
 *
 * aux_cfg_payload must remain valid until hfi_adapter_add_prop_array().
 */
static int _append_aux_cfg(struct dp_hfi *hfi, u32 *aux_cfg_payload)
{
	struct dp_mgr_hfi_priv *hfi_priv = (struct dp_mgr_hfi_priv *)hfi->priv;
	struct dp_parser *parsed;
	struct dp_aux_cfg *aux_cfg;
	struct hfi_util_kv_helper *kv_props = hfi->kv_props;
	u32 aux_offset, kv_size = 0;
	int i;

	if (!hfi_priv->parser)
		return kv_size;

	parsed = hfi_priv->parser;
	aux_cfg = parsed->aux_cfg;

	if (!kv_props)
		return kv_size;

	aux_cfg_payload[0] = PHY_AUX_CFG_MAX;
	for (i = 0; i < PHY_AUX_CFG_MAX; i++) {
		aux_offset = (aux_cfg[i].offset - DP_AUX_CFG_BASE_OFFSET) / 4;
		aux_cfg_payload[i + 1] = ((u8)aux_offset << 16) | (u8)aux_cfg[i].lut[0];
	}

	if (hfi_priv->aux_params_valid && aux_cfg_payload) {
		hfi_util_kv_helper_add(kv_props,
				HFI_PACKKEY(HFI_PROPERTY_PANEL_DP_AUX_CFG, 0,
				(((PHY_AUX_CFG_MAX + 1) * sizeof(aux_cfg_payload[0]))
				/ sizeof(u32))), aux_cfg_payload);
		kv_size += PHY_AUX_CFG_MAX * sizeof(u32);
	}

	return kv_size;
}

/*
 * _append_lt_params - Pack vswing/preemph KV pairs into kv_props.
 *
 * lt_params_payload must remain valid until hfi_adapter_add_prop_array().
 */
static int _append_lt_params(struct dp_hfi *hfi, u32 *payload)
{
	struct dp_mgr_hfi_priv *hfi_priv = (struct dp_mgr_hfi_priv *)hfi->priv;
	struct dp_parser *parsed;
	struct hfi_util_kv_helper *kv_props = hfi->kv_props;
	u32 bw_code[NUM_BW_CODE] = {6, 10, 20, 30};
	u32 (*lt_params_payload)[2 + NUM_VSWING_VAL] = (u32 (*)[2 + NUM_VSWING_VAL])payload;
	u32 kv_size = 0, lt_params_cols;
	int i, j;
	u8 *vswing_lut, *pre_emp_lut;

	if (!hfi_priv->parser)
		return kv_size;

	parsed = hfi_priv->parser;

	if (!kv_props)
		return kv_size;

	if (parsed->valid_lt_params && payload) {
		lt_params_cols = 2 + NUM_VSWING_VAL;
		for (i = 0; i < NUM_BW_CODE; i++) {
			if (bw_code[i] <= DP_LINK_RATE_HBR) {
				vswing_lut = parsed->swing_hbr_rbr;
				pre_emp_lut = parsed->pre_emp_hbr_rbr;
			} else {
				vswing_lut = parsed->swing_hbr2_3;
				pre_emp_lut = parsed->pre_emp_hbr2_3;
			}

			lt_params_payload[i][0] = bw_code[i];
			lt_params_payload[i][1] = NUM_VSWING_VAL;

			for (j = 0; j < NUM_VSWING_VAL; j++)
				lt_params_payload[i][j + 2] =
					(vswing_lut[j] << 16) | pre_emp_lut[j];

			hfi_util_kv_helper_add(kv_props,
					HFI_PACKKEY(HFI_PROPERTY_PANEL_DP_VOLTAGESWING_PREEMPHASIS,
					0, lt_params_cols), lt_params_payload[i]);
			kv_size += lt_params_cols * sizeof(u32);
		}
	}

	return kv_size;
}

/*
 * _append_max_link_params - Pack max link rate and lane count KV pairs.
 *
 * Uses &parsed->max_lclk_khz / max_lane_count as value pointers so the stored
 * kv_pair stays valid until hfi_adapter_add_prop_array() is called.
 */
static int _append_max_link_params(struct dp_hfi *hfi)
{
	struct dp_mgr_hfi_priv *hfi_priv = (struct dp_mgr_hfi_priv *)hfi->priv;
	struct dp_parser *parsed;
	struct hfi_util_kv_helper *kv_props = hfi->kv_props;
	u32 kv_size = 0;

	if (!hfi_priv->parser)
		return kv_size;

	parsed = hfi_priv->parser;

	if (!kv_props)
		return kv_size;

	if (parsed->max_lclk_khz) {
		hfi_util_kv_helper_add(kv_props,
				HFI_PACKKEY(HFI_PROPERTY_PANEL_MAX_LINK_RATE, 0,
				sizeof(parsed->max_lclk_khz) / sizeof(u32)),
				&parsed->max_lclk_khz);
		kv_size += sizeof(parsed->max_lclk_khz);
	}

	if (parsed->max_lane_count) {
		hfi_util_kv_helper_add(kv_props,
				HFI_PACKKEY(HFI_PROPERTY_PANEL_MAX_LANE_COUNT, 0,
				sizeof(parsed->max_lane_count) / sizeof(u32)),
				&parsed->max_lane_count);
		kv_size += sizeof(parsed->max_lane_count);
	}

	return kv_size;
}

void dp_hfi_panel_init(struct dp_hfi *hfi)
{
	struct hfi_client_t *hfi_client = hfi->hfi_client;
	struct dp_mgr_hfi_priv *hfi_priv = (struct dp_mgr_hfi_priv *)hfi->priv;
	struct hfi_util_kv_helper *kv_props;
	struct hfi_cmdbuf_t *buffer;
	u32 obj_id;
	int rc = 0;

	/* Declared here so they remain valid when hfi_adapter_add_prop_array()
	 * uses the kv_pairs packed by _append_aux_cfg() and _append_lt_params().
	 */
	u32 aux_cfg_payload[PHY_AUX_CFG_MAX + 1];
	u32 lt_params_payload[NUM_BW_CODE * (2 + NUM_VSWING_VAL)];
	u32 kv_count, payload_size, kv_size = 0;

	if (!hfi_client) {
		DP_ERR("Failed to get HFI client for dp panel init\n");
		return;
	}

	if (!hfi_priv || !hfi_priv->parser) {
		DP_ERR("No parser available for dp panel init\n");
		return;
	}

	obj_id = sde_conn_get_display_obj_id(hfi->connector);
	buffer = hfi_adapter_get_cmd_buf(hfi_client, obj_id, HFI_CMDBUF_TYPE_DISPLAY_INFO_BLOCKING);
	if (!buffer) {
		DP_ERR("Failed to get cmd buffer for dp panel init\n");
		return;
	}

	/* aux_cfg(1) + lt_params(NUM_BW_CODE) + max_link_rate(1) + max_lane_count(1) */
	kv_props = hfi_util_kv_helper_alloc(1 + NUM_BW_CODE + 2);
	if (!kv_props) {
		DP_ERR("Failed to allocate kv_props for dp panel init\n");
		hfi_adapter_release_cmd_buf(hfi_client, buffer);
		return;
	}

	hfi->kv_props = kv_props;
	hfi_util_kv_helper_reset(kv_props);

	kv_size += _append_aux_cfg(hfi, aux_cfg_payload);
	kv_size += _append_lt_params(hfi, lt_params_payload);
	kv_size += _append_max_link_params(hfi);

	if (!kv_size) {
		DP_DEBUG("No dp panel init caps to send\n");
		kfree(kv_props);
		hfi_adapter_release_cmd_buf(hfi_client, buffer);
		return;
	}

	kv_count = hfi_util_kv_helper_get_count(kv_props);
	payload_size = (kv_count * sizeof(u32)) + kv_size;
	rc = hfi_adapter_add_prop_array(buffer->ctx, buffer,
			HFI_COMMAND_PANEL_INIT_GENERIC_CAPS, obj_id, HFI_PAYLOAD_TYPE_U32_ARRAY,
			hfi_util_kv_helper_get_payload_addr(kv_props), kv_count, payload_size);
	if (rc) {
		DP_ERR("Failed to append HFI_COMMAND_PANEL_INIT_GENERIC_CAPS, rc=%d\n", rc);
		kfree(kv_props);
		hfi_adapter_release_cmd_buf(hfi_client, buffer);
		return;
	}

	rc = hfi_adapter_set_cmd_buf(hfi_client, buffer);
	if (rc) {
		DP_ERR("Failed to send HFI_COMMAND_PANEL_INIT_GENERIC_CAPS, rc=%d\n", rc);
		hfi_adapter_release_cmd_buf(hfi_client, buffer);
	}

	kfree(kv_props);
}

/**
 * dp_hfi_setup() - setup dp hfi interface
 * @client: handle to dp client structure
 *
 * Return: pointer to dp_mgr_hfi structure on success, ERR_PTR on failure.
 */
struct dp_hfi *dp_hfi_setup(struct dp_client *client, void *hfi_priv)
{
	struct msm_drm_private *priv;
	struct drm_device *dev;
	struct hfi_adapter_t *hfi_host;
	struct dp_hfi *hfi;
	int rc = 0;

	if (!client) {
		DP_ERR("invalid client\n");
		return ERR_PTR(-EINVAL);
	}

	/* Get the drm device from the client */
	dev = client->drm_dev;
	if (!dev || !dev->dev_private) {
		DP_ERR("invalid drm device or private data\n");
		return ERR_PTR(-EINVAL);
	}

	priv = dev->dev_private;

	/* Get HFI host adapter */
	if (!priv->hfi_priv) {
		DP_ERR("HFI private not available\n");
		return ERR_PTR(-EINVAL);
	}

	hfi_host = ((struct msm_drm_hfi_private *)priv->hfi_priv)->hfi_adapter;
	if (!hfi_host) {
		DP_ERR("HFI host adapter not available\n");
		return ERR_PTR(-EINVAL);
	}

	hfi = vzalloc(sizeof(struct dp_hfi));
	if (!hfi) {
		DP_ERR("failed to allocate memory for hfi\n");
		return ERR_PTR(-ENOMEM);
	}

	hfi->priv = hfi_priv;

	/* Call dp_hfi_setup_client to setup DP-specific HFI and get hfi */
	rc = dp_hfi_setup_client(hfi, hfi_host);
	if (rc) {
		DP_ERR("dp_hfi_setup_client failed: %ld\n", PTR_ERR(hfi));
		vfree(hfi);
		return ERR_PTR(rc);
	}

	hfi->hdcp_info.hdcp_state = HDCP_STATE_INACTIVE;
	hfi->hdcp_info.hdcp_version = HDCP_VERSION_NONE;
	hfi->hdcp_info.source_cap = 0;

	/* Initialize min_enc_level to default (standard content) */
	hfi->min_enc_level = 0;

	return hfi;
}
