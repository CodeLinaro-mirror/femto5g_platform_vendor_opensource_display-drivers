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

int dp_hfi_process_event(struct hfi_client_t *hfi_client, enum hfi_adapter_event_type event,
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

int dp_hfi_process_cmd_buf(struct hfi_client_t *hfi_client, struct hfi_cmdbuf_t *cmd_buf)
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

void dp_hfi_prop_handler(u32 hfi_uid, u32 prop, void *payload, u32 size,
			  struct hfi_prop_listener *listener)
{
	struct dp_hfi *hfi;
	u32 dp_display_obj_id;

	if (!listener) {
		DP_ERR("invalid listener\n");
		return;
	}

	hfi = container_of(listener, struct dp_hfi, hfi_cb_obj);

	dp_display_obj_id = sde_conn_get_display_obj_id(hfi->connector);
	if (dp_display_obj_id != hfi_uid) {
		DP_ERR("Component and HFI ID mismatch (%d != %d)\n",
				dp_display_obj_id, hfi_uid);
		return;
	}

	switch (prop) {
	case HFI_COMMAND_DISPLAY_MODE_VALIDATE:
		if (payload && hfi)
			hfi->mode_valid = true;
		break;
	default:
		hfi->handle_event(hfi->cb_data, prop, payload, size);
		break;
	}
}

int dp_hfi_setup_client(struct dp_hfi *hfi,	struct hfi_adapter_t *hfi_host)
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

	switch (hfi_cmd) {
	case HFI_COMMAND_DISPLAY_MODE_VALIDATE:
		if (payload && hfi)
			hfi->mode_valid = true;
		flags = HFI_HOST_FLAGS_NONE;
		break;
	case HFI_COMMAND_DISPLAY_SET_MODE:
	case HFI_COMMAND_DISPLAY_ENABLE:
	case HFI_COMMAND_DISPLAY_POST_ENABLE:
	case HFI_COMMAND_DISPLAY_DISABLE:
	case HFI_COMMAND_DISPLAY_POST_DISABLE:
	case HFI_COMMAND_DISPLAY_EVENT_REGISTER:
	case HFI_COMMAND_DISPLAY_EVENT_DEREGISTER:
		flags |= HFI_HOST_FLAGS_RESPONSE_REQUIRED;
		break;
	default:
		break;
	}

	if (flags & HFI_HOST_FLAGS_RESPONSE_REQUIRED) {
		rc = hfi_adapter_add_get_property(hfi_client, cmd_buf, hfi_cmd, obj_id,
			hfi_payload_type, payload, payload_size, &hfi->hfi_cb_obj, flags);
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
	u32 obj_id;

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
	case HFI_COMMAND_DISPLAY_POST_ENABLE:
	case HFI_COMMAND_DISPLAY_DISABLE:
	case HFI_COMMAND_DISPLAY_POST_DISABLE:
	case HFI_COMMAND_DISPLAY_EVENT_REGISTER:
		flags |= HFI_HOST_FLAGS_RESPONSE_REQUIRED;
		break;
	default:
		break;
	}

	if (flags & HFI_HOST_FLAGS_RESPONSE_REQUIRED)
		cmd_buf_type = HFI_CMDBUF_TYPE_DISPLAY_INFO_BLOCKING;

	cmd_buf = hfi_adapter_get_cmd_buf(hfi_client, obj_id, cmd_buf_type);
	if (!cmd_buf) {
		DP_ERR("could not get cmd_buf for hfi_cmd 0x%x\n", hfi_cmd);
		return -ENODEV;
	}

	if (flags & HFI_HOST_FLAGS_RESPONSE_REQUIRED) {
		rc = hfi_adapter_add_get_property(hfi_client, cmd_buf, hfi_cmd, obj_id,
			hfi_payload_type, payload, payload_size, &hfi->hfi_cb_obj, flags);
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

int dp_hfi_start_batch_cmd(struct dp_hfi *hfi,
				struct hfi_client_t *hfi_client, u32 hfi_cmd,
				const char *display_type, u32 hfi_payload_type,
				void *payload, u32 payload_size, u32 flags)
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
	flags |= HFI_HOST_FLAGS_RESPONSE_REQUIRED;
	cmd_buf = hfi_adapter_get_cmd_buf(hfi_client, obj_id, cmd_buf_type);
	if (!cmd_buf) {
		DP_ERR("could not get cmd_buf for hfi_cmd 0x%x\n", hfi_cmd);
		return -ENODEV;
	}
	hfi->batch_cmd_buf = cmd_buf;

	return _pack_cmd(hfi, hfi_client, hfi->batch_cmd_buf, hfi_cmd, obj_id, hfi_payload_type,
			payload, payload_size, flags);

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

/**
 * dp_hfi_setup() - setup dp hfi interface
 * @client: handle to dp client structure
 *
 * Return: pointer to dp_mgr_hfi structure on success, ERR_PTR on failure.
 */
struct dp_hfi *dp_hfi_setup(struct dp_client *client, void *cb_data)
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

	hfi->connector = client->base_connector;
	hfi->cb_data = cb_data;

	/* Call dp_hfi_setup_client to setup DP-specific HFI and get hfi */
	rc = dp_hfi_setup_client(hfi, hfi_host);
	if (rc) {
		DP_ERR("dp_hfi_setup_client failed: %ld\n", PTR_ERR(hfi));
		vfree(hfi);
		return ERR_PTR(rc);
	}

	return hfi;
}
