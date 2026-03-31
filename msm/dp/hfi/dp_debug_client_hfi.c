// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#if (KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE)
#include <drm/display/drm_dp_mst_helper.h>
#else
#include <drm/drm_dp_mst_helper.h>
#endif
#include <drm/drm_probe_helper.h>

#include "msm_drv.h"
#include "sde_kms.h"
#include "hfi_kms.h"
#include "dp_drv.h"
#include "dp_client.h"
#include "dp_debug.h"
#include "dp_debug_client.h"
#include "dp_debug_client_hfi.h"
#include "hfi_commands_device.h"
#include "hfi_commands_debug.h"
#include "sde_connector.h"
#include "hfi_adapter.h"
#include "dp_mgr.h"
#include "dp_mgr_hfi.h"
#include "hfi_defs_display.h"

#if IS_ENABLED(CONFIG_DEBUG_FS)

/* Helper function to check buffer overflow */
static int dp_debug_client_hfi_check_buffer_overflow(int rc, int *max_size, int *len)
{
	if (rc < 0)
		return rc;

	if (rc >= *max_size) {
		DP_ERR("buffer overflow\n");
		return -EOVERFLOW;
	}

	*len += rc;
	*max_size -= rc;

	return 0;
}

/* Helper function to print HDR parameters to buffer */
static int dp_debug_client_hfi_print_hdr_params_to_buf(struct drm_connector *connector,
		char *buf, u32 size)
{
	int rc;
	u32 i, len = 0, max_size = size;
	struct sde_connector *c_conn;
	struct sde_connector_state *c_state;
	struct drm_msm_ext_hdr_metadata *hdr;

	c_conn = to_sde_connector(connector);
	c_state = to_sde_connector_state(connector->state);

	hdr = &c_state->hdr_meta;

	rc = scnprintf(buf + len, max_size,
		"============SINK HDR PARAMETERS===========\n");
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "eotf = %d\n",
		c_conn->hdr_eotf);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "type_one = %d\n",
		c_conn->hdr_metadata_type_one);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "hdr_plus_app_ver = %d\n",
			c_conn->hdr_plus_app_ver);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "max_luminance = %d\n",
		c_conn->hdr_max_luminance);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "avg_luminance = %d\n",
		c_conn->hdr_avg_luminance);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "min_luminance = %d\n",
		c_conn->hdr_min_luminance);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size,
		"============VIDEO HDR PARAMETERS===========\n");
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "hdr_state = %d\n", hdr->hdr_state);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "hdr_supported = %d\n",
			hdr->hdr_supported);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "eotf = %d\n", hdr->eotf);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "white_point_x = %d\n",
		hdr->white_point_x);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "white_point_y = %d\n",
		hdr->white_point_y);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "max_luminance = %d\n",
		hdr->max_luminance);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "min_luminance = %d\n",
		hdr->min_luminance);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "max_content_light_level = %d\n",
		hdr->max_content_light_level);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "min_content_light_level = %d\n",
		hdr->max_average_light_level);
	if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	for (i = 0; i < HDR_PRIMARIES_COUNT; i++) {
		rc = scnprintf(buf + len, max_size, "primaries_x[%d] = %d\n",
			i, hdr->display_primaries_x[i]);
		if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
			goto error;

		rc = scnprintf(buf + len, max_size, "primaries_y[%d] = %d\n",
			i, hdr->display_primaries_y[i]);
		if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
			goto error;
	}

	if (hdr->hdr_plus_payload && hdr->hdr_plus_payload_size) {
		u32 rowsize = 16, rem;
		struct sde_connector_dyn_hdr_metadata *dhdr =
				&c_state->dyn_hdr_meta;

		for (i = 0; i < dhdr->dynamic_hdr_payload_size; i += rowsize) {
			rc = scnprintf(buf + len, max_size, "DHDR: ");
			if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size,
					&len))
				goto error;

			rem = dhdr->dynamic_hdr_payload_size - i;
			rc = hex_dump_to_buffer(&dhdr->dynamic_hdr_payload[i],
				min(rowsize, rem), rowsize, 1, buf + len,
				max_size, false);
			if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size,
					&len))
				goto error;

			rc = scnprintf(buf + len, max_size, "\n");
			if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size,
					&len))
				goto error;
		}
	}

	return len;
error:
	return -EOVERFLOW;
}

/* Helper functions to get dp drv context */
static struct dp_drv *dp_debug_hfi_get_dp_drv(struct dp_debug_client_hfi_priv *priv)
{
	struct platform_device *pdev;

	if (!priv || !priv->dev)
		return NULL;

	pdev = to_platform_device(priv->dev);
	return platform_get_drvdata(pdev);
}

/* Helper function to send HFI command for simulation mode - doesn't need dp_client */
static int dp_debug_hfi_send_cmd(struct dp_debug_client_hfi_priv *priv,
		struct hfi_client_t *hfi_client, u32 hfi_cmd,
		u32 hfi_payload_type, void *payload, u32 payload_size, u32 flags)
{
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	enum hfi_cmdbuf_type cmd_buf_type = HFI_CMDBUF_TYPE_GET_DEBUG_DATA;
	int rc = 0;
	u32 obj_id;
	struct platform_device *pdev;
	struct dp_drv *dp_drv;
	struct drm_connector *connector = NULL;
	const u32 fallback_obj_id = 2; /* Fallback obj_id if connector not available */

	if (!priv || !hfi_client) {
		DP_ERR("Invalid priv or hfi_client\n");
		return -EINVAL;
	}

	/* Get obj_id from connector using sde_conn_get_display_obj_id() */
	if (priv->dev) {
		pdev = to_platform_device(priv->dev);
		dp_drv = platform_get_drvdata(pdev);
		if (dp_drv && dp_drv->client && dp_drv->client->base_connector) {
			connector = dp_drv->client->base_connector;
			obj_id = sde_conn_get_display_obj_id(connector);
		} else {
			obj_id = fallback_obj_id;
			DP_WARN("Connector not available, using fallback obj_id=%d\n",
					fallback_obj_id);
		}
	} else {
		obj_id = fallback_obj_id;
		DP_WARN("Device not available, using fallback obj_id=%d\n", fallback_obj_id);
	}

	cmd_buf = hfi_adapter_get_cmd_buf(hfi_client, obj_id, cmd_buf_type);
	if (!cmd_buf) {
		DP_ERR("Could not get cmd_buf for hfi_cmd 0x%x\n", hfi_cmd);
		return -ENODEV;
	}

	rc = hfi_adapter_add_set_property(hfi_client, cmd_buf, hfi_cmd,
			obj_id, hfi_payload_type, payload, payload_size, flags);
	if (rc) {
		DP_ERR("Could not set property for hfi_cmd 0x%x, rc=%d\n", hfi_cmd, rc);
		return rc;
	}

	DP_DEBUG("Sending HFI command 0x%x to DCP (obj_id=%u, payload_size=%u)\n",
		hfi_cmd, obj_id, payload_size);

	rc = hfi_adapter_set_cmd_buf_blocking(hfi_client, cmd_buf);
	if (rc) {
		DP_ERR("Failed to send hfi_cmd 0x%x, rc=%d\n", hfi_cmd, rc);
		return rc;
	}

	DP_DEBUG("Successfully sent HFI command 0x%x to DCP\n", hfi_cmd);
	return 0;
}

/* Get existing HFI client and only create new if doesn't exist */
static int dp_debug_hfi_create_client(struct dp_debug_client_hfi_priv *priv)
{
	struct platform_device *pdev;
	struct dp_drv *dp_drv;
	struct hfi_client_t *existing_hfi_client;
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;

	if (!priv) {
		DP_ERR("Invalid priv pointer\n");
		return -EINVAL;
	}

	if (priv->hfi_client) {
		DP_DEBUG("HFI client already exists\n");
		return 0;
	}

	if (!priv->dev) {
		DP_ERR("No device available for HFI client creation\n");
		return -ENODEV;
	}

	/* Get HFI client directly from the device */
	pdev = to_platform_device(priv->dev);
	dp_drv = platform_get_drvdata(pdev);

	DP_DEBUG("Getting HFI client from device: dev=%p, pdev=%p, dp_drv=%p\n",
		priv->dev, pdev, dp_drv);

	if (!dp_drv) {
		DP_ERR("No dp_drv found in platform device data\n");
		return -ENODEV;
	}

	if (!dp_drv->client) {
		DP_ERR("No dp_client found in dp_drv\n");
		return -ENODEV;
	}

	/* Get the existing HFI client from HFI KMS */
	if (!dp_drv->client->base_connector) {
		DP_ERR("No base connector found in dp_client\n");
		return -ENODEV;
	}

	sde_kms = sde_connector_get_kms(dp_drv->client->base_connector);

	if (!sde_kms) {
		DP_ERR("Failed to get SDE KMS\n");
		return -ENODEV;
	}

	hfi_kms = to_hfi_kms(sde_kms);

	if (!hfi_kms) {
		DP_ERR("Failed to get HFI KMS\n");
		return -ENODEV;
	}

	existing_hfi_client = &hfi_kms->hfi_client;
	if (!existing_hfi_client) {
		DP_ERR("No HFI client found in HFI KMS\n");
		return -ENODEV;
	}

	DP_INFO("Found existing HFI client from device: %p\n", existing_hfi_client);

	/* Use the existing HFI client instead of creating our own */
	priv->hfi_client = existing_hfi_client;

	DP_INFO("Successfully using existing HFI client: %p\n", priv->hfi_client);
	return 0;
}

/* Helper function to get or create HFI client for simulation mode
 * This creates our own dedicated HFI client for simulation, avoiding dependency on device/connector
 */
static struct hfi_client_t *dp_debug_hfi_get_client(struct dp_debug_client_hfi_priv *priv)
{
	struct platform_device *pdev;
	struct dp_drv *dp_drv;

	if (!priv || !priv->dev) {
		DP_ERR("Invalid state\n");
		return NULL;
	}

	if (priv->hfi_client) {
		return priv->hfi_client;
	}

	pdev = to_platform_device(priv->dev);
	dp_drv = platform_get_drvdata(pdev);
	if (!dp_drv || !dp_drv->client) {
		DP_DEBUG("DP driver not properly initialized yet\n");
		return NULL;
	}

	/* Check if HFI system is ready */
	if (!dp_drv->client->base_connector) {
		DP_DEBUG("Base connector not available - HFI system not ready\n");
		return NULL;
	}

	if (dp_debug_hfi_create_client(priv) != 0) {
		DP_DEBUG("Failed to create HFI client - HFI system may not be ready\n");
		return NULL;
	}

	return priv->hfi_client;
}


/* Destroy our HFI client */
static void dp_debug_hfi_destroy_client(struct dp_debug_client_hfi_priv *priv)
{
	if (!priv || !priv->hfi_client)
		return;

	DP_DEBUG("Releasing reference to existing HFI client: %p\n", priv->hfi_client);

	/* We're using the existing HFI client from dp_client, so we don't free it.
	 * Just clear our reference to it.
	 */
	priv->hfi_client = NULL;
}


/* HFI response handler for DP simulation read commands */
static void dp_debug_hfi_response_handler(u32 obj_id, u32 cmd_id, void *payload,
		u32 payload_size, struct hfi_prop_listener *listener)
{
	struct dp_debug_client_hfi_priv *priv;

	if (!listener) {
		DP_ERR("Invalid listener\n");
		return;
	}

	/* Use container_of to get back to the parent structure, just like dp_hfi.c does */
	priv = container_of(listener, struct dp_debug_client_hfi_priv, hfi_cb_obj);

	/* Add safety checks to prevent crashes from corrupted memory */
	if (!priv) {
		DP_ERR("Invalid priv pointer from container_of\n");
		return;
	}

	/* Validate the priv structure by checking a known field */
	if (!priv->dev) {
		DP_ERR("Priv structure appears corrupted (dev is NULL)\n");
		return;
	}

	DP_DEBUG("Received HFI response: obj_id=0x%x, cmd_id=0x%x, payload_size=%u, priv=%p\n",
		obj_id, cmd_id, payload_size, priv);

	mutex_lock(&priv->response_data.response_lock);

	switch (cmd_id) {
	case HFI_COMMAND_DEBUG_DP_READ_BW_CODE:
		if (payload && payload_size >= sizeof(u32)) {
			priv->response_data.bw_code = *(u32 *)payload;
			priv->response_data.response_type = HFI_RESPONSE_BW_CODE;
			DP_DEBUG("Received BW_CODE response: %u\n", priv->response_data.bw_code);
		} else {
			DP_WARN("Invalid BW_CODE response payload\n");
			priv->response_data.bw_code = 0;
		}
		break;

	case HFI_COMMAND_DEBUG_DP_READ_DPCD:
		if (payload && payload_size > 0) {
			priv->response_data.dpcd_size = min_t(u32, payload_size,
				      sizeof(priv->response_data.dpcd_data));
			memcpy(priv->response_data.dpcd_data, payload,
			       priv->response_data.dpcd_size);
			priv->response_data.response_type = HFI_RESPONSE_DPCD;
			DP_DEBUG("Received DPCD response: size=%u\n",
					priv->response_data.dpcd_size);
		} else {
			DP_WARN("Invalid DPCD response payload\n");
			priv->response_data.dpcd_size = 0;
		}
		break;

	case HFI_COMMAND_DEBUG_DP_READ_CRC:
		if (payload && payload_size >= sizeof(u32) * 6) {
			memcpy(priv->response_data.crc_data, payload,
			       sizeof(priv->response_data.crc_data));
			priv->response_data.response_type = HFI_RESPONSE_CRC;
			DP_DEBUG("Received CRC response\n");
		} else {
			DP_WARN("Invalid CRC response payload\n");
			memset(priv->response_data.crc_data, 0,
			       sizeof(priv->response_data.crc_data));
		}
		break;

	/* Empty response handling for all other HFI commands */
	case HFI_COMMAND_DEBUG_DP_SET_EDID:
	case HFI_COMMAND_DEBUG_DP_SET_DPCD:
	case HFI_COMMAND_DEBUG_DP_SET_BW_CODE:
	case HFI_COMMAND_DEBUG_DP_SET_TPG:
	case HFI_COMMAND_DEBUG_DP_HDCP_CONTROL:
	case HFI_COMMAND_DEBUG_DP_SIMULATION_CONTROL:
	case HFI_COMMAND_DEBUG_DP_SET_ATTENTION:
		/* Empty response handling - just acknowledge receipt */
		DP_DEBUG("Received nop response for command: 0x%x\n", cmd_id);
		priv->response_data.response_type = HFI_RESPONSE_NONE;
		break;

	default:
		DP_WARN("Unknown response command: 0x%x\n", cmd_id);
		priv->response_data.response_type = HFI_RESPONSE_NONE;
		break;
	}

	priv->response_data.response_received = true;
	complete(&priv->response_data.response_complete);
	mutex_unlock(&priv->response_data.response_lock);
}

/* Send HFI command and wait for response */
static int dp_debug_hfi_send_cmd_with_response(struct dp_debug_client_hfi_priv *priv,
		struct hfi_client_t *hfi_client, u32 hfi_cmd,
		u32 hfi_payload_type, void *payload, u32 payload_size, u32 flags,
		int response_type, unsigned long timeout_ms)
{
	struct hfi_cmdbuf_t *cmd_buf;
	int rc;
	/* unsigned long timeout_jiffies = msecs_to_jiffies(timeout_ms); */
	u32 obj_id;
	struct platform_device *pdev;
	struct dp_drv *dp_drv;
	struct drm_connector *connector = NULL;

	if (!priv || !hfi_client)
		return -EINVAL;

	/* Get obj_id from connector */
	if (priv->dev) {
		pdev = to_platform_device(priv->dev);
		dp_drv = platform_get_drvdata(pdev);
		if (dp_drv && dp_drv->client && dp_drv->client->base_connector) {
			connector = dp_drv->client->base_connector;
			obj_id = sde_conn_get_display_obj_id(connector);
		} else {
			obj_id = 2; /* Fallback */
		}
	} else {
		obj_id = 2; /* Fallback */
	}

	/* Get command buffer */
	cmd_buf = hfi_adapter_get_cmd_buf(hfi_client, obj_id,
			HFI_CMDBUF_TYPE_DISPLAY_INFO_BLOCKING);
	if (!cmd_buf)
		return -ENOMEM;

	/* Prepare response handling - callback handler already set during initialization */
	mutex_lock(&priv->response_data.response_lock);
	priv->response_data.response_type = response_type;
	priv->response_data.response_received = false;
	reinit_completion(&priv->response_data.response_complete);
	mutex_unlock(&priv->response_data.response_lock);

	/* Add get property command with listener for response */
	rc = hfi_adapter_add_get_property(hfi_client, cmd_buf, hfi_cmd, obj_id,
			hfi_payload_type, payload, payload_size, &priv->hfi_cb_obj, flags);
	if (rc) {
		hfi_adapter_release_cmd_buf(hfi_client, cmd_buf);
		return rc;
	}

	/* Send the command buffer */
	rc = hfi_adapter_set_cmd_buf_blocking(hfi_client, cmd_buf);
	if (rc)
		return rc;

	mutex_lock(&priv->response_data.response_lock);
	if (!priv->response_data.response_received) {
		mutex_unlock(&priv->response_data.response_lock);
		return -EIO;
	}
	mutex_unlock(&priv->response_data.response_lock);

	return 0;
}

/* Read operations */
static int dp_debug_client_hfi_read_dpcd(struct dp_debug_client *client,
		u8 *dpcd, u32 size, u32 offset)
{
	struct dp_debug_client_hfi_priv *priv;
	struct hfi_client_t *hfi_client;
	struct hfi_shared_addr_map *dpcd_addr_map = NULL;
	struct hfi_dp_dpcd_request dpcd_request;
	int rc;
	u32 actual_size = 0;
	u8 *dpcd_data;

	if (!client || !dpcd)
		return -EINVAL;

	priv = client->priv;
	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client) {
		DP_ERR("HFI client not available\n");
		return -ENODEV;
	}

	/* Allocate shared buffer for DCP to populate with DPCD data */
	dpcd_addr_map = dp_mgr_hfi_init_shared_addr(hfi_client, SZ_1K);
	if (!dpcd_addr_map) {
		DP_ERR("Failed to allocate shared buffer for READ_DPCD\n");
		return -ENOMEM;
	}

	DP_INFO("Allocated DPCD buffer: local=%p, remote=0x%llx, size=%u\n",
		dpcd_addr_map->local_addr, (u64) dpcd_addr_map->remote_addr,
		dpcd_addr_map->size);

	/* Prepare command payload with buffer address for DCP to write to */
	dpcd_request.buffer.addr_l = HFI_VAL_L32(dpcd_addr_map->remote_addr);
	dpcd_request.buffer.addr_h = HFI_VAL_H32(dpcd_addr_map->remote_addr);
	dpcd_request.buffer.size = dpcd_addr_map->size;
	dpcd_request.dpcd_offset = offset;
	dpcd_request.bytes = size;

	/* Send HFI command with shared buffer address */
	rc = dp_debug_hfi_send_cmd(priv, hfi_client,
			HFI_COMMAND_DEBUG_DP_READ_DPCD,
			HFI_PAYLOAD_TYPE_U32_ARRAY, &dpcd_request, sizeof(dpcd_request),
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc) {
		DP_ERR("Failed to send READ_DPCD command to DCP, rc=%d\n", rc);
		dp_mgr_init_deinit_shared_addr(hfi_client, dpcd_addr_map);
		return rc;
	}

	DP_INFO("READ_DPCD command sent, DCP will populate shared buffer\n");

	/* Read the data that DCP populated in the shared buffer */
	dpcd_data = (u8 *)dpcd_addr_map->local_addr;
	actual_size = min_t(u32, size, dpcd_addr_map->size);
	if (actual_size > 0)
		memcpy(dpcd, dpcd_data, actual_size);

	/* Free the shared buffer */
	dp_mgr_init_deinit_shared_addr(hfi_client, dpcd_addr_map);

	DP_DEBUG("Read %u bytes of DPCD data from offset 0x%x\n", actual_size, offset);
	return actual_size;
}

static int dp_debug_client_hfi_read_crc(struct dp_debug_client *client,
		char *buf, u32 size)
{
	struct dp_debug_client_hfi_priv *priv;
	struct hfi_client_t *hfi_client;
	int rc;
	u32 len = 0;
	u16 src_crc[3] = {0};
	u16 sink_crc[3] = {0};

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;
	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client)
		return -ENODEV;

	/* Send HFI command and wait for response from DCP */
	rc = dp_debug_hfi_send_cmd_with_response(priv, hfi_client,
			HFI_COMMAND_DEBUG_DP_READ_CRC,
			HFI_PAYLOAD_TYPE_NONE, NULL, 0,
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE,
			HFI_RESPONSE_CRC, 1000); /* 1 second timeout */
	if (rc) {
		DP_ERR("Failed to get CRC response from DCP, rc=%d\n", rc);
		/* Return empty CRC data on error */
		len += scnprintf(buf + len, size - len, "FRAME_CRC:\nSource vs Sink\n");
		len += scnprintf(buf + len, size - len, "CRC_R: 0000 0000\n");
		len += scnprintf(buf + len, size - len, "CRC_G: 0000 0000\n");
		len += scnprintf(buf + len, size - len, "CRC_B: 0000 0000\n");
		return len;
	}

	/* Get the response data */
	mutex_lock(&priv->response_data.response_lock);
	/* CRC data format: [src_R, src_G, src_B, sink_R, sink_G, sink_B] */
	src_crc[0] = (u16) priv->response_data.crc_data[0];  /* Source R */
	src_crc[1] = (u16) priv->response_data.crc_data[1];  /* Source G */
	src_crc[2] = (u16) priv->response_data.crc_data[2];  /* Source B */
	sink_crc[0] = (u16) priv->response_data.crc_data[3]; /* Sink R */
	sink_crc[1] = (u16) priv->response_data.crc_data[4]; /* Sink G */
	sink_crc[2] = (u16) priv->response_data.crc_data[5]; /* Sink B */
	mutex_unlock(&priv->response_data.response_lock);

	/* Format response like legacy implementation */
	len += scnprintf(buf + len, size - len, "FRAME_CRC:\nSource vs Sink\n");
	len += scnprintf(buf + len, size - len, "CRC_R: %04X %04X\n", src_crc[0], sink_crc[0]);
	len += scnprintf(buf + len, size - len, "CRC_G: %04X %04X\n", src_crc[1], sink_crc[1]);
	len += scnprintf(buf + len, size - len, "CRC_B: %04X %04X\n", src_crc[2], sink_crc[2]);

	/* Note: MISR40 data would require additional HFI command implementation */
	len += scnprintf(buf + len, size - len, "\nMISR40:\nCTLR vs PHY\n");
	len += scnprintf(buf + len, size - len, "Lane0 00000000 00000000\n");
	len += scnprintf(buf + len, size - len, "Lane1 00000000 00000000\n");
	len += scnprintf(buf + len, size - len, "Lane2 00000000 00000000\n");
	len += scnprintf(buf + len, size - len, "Lane3 00000000 00000000\n");

	return len;
}

static int dp_debug_client_hfi_read_connected(struct dp_debug_client *client,
		char *buf, u32 size)
{
	int connected;

	if (!client || !buf)
		return -EINVAL;

	/* In simulation mode, connection status follows hotplug state */
	connected = client->hotplug ? 1 : 0;

	return scnprintf(buf, size, "%d\n", connected);
}

static int dp_debug_client_hfi_read_info(struct dp_debug_client *client,
		char *buf, u32 size)
{
	struct dp_debug_client_hfi_priv *priv;
	struct hfi_client_t *hfi_client;
	struct hfi_shared_addr_map *info_addr_map = NULL;
	int rc;
	int len = 0;
	u32 *info_data;
	struct {
		u64 buffer_addr;
		u32 buffer_size;
	} cmd_payload;
	u32 status, state, link_rate, lane_count,
		h_active, v_active, refresh_rate, pixel_clk_khz,
		bpp, test_req, bw_code, v_level, p_level;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;
	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client) {
		DP_ERR("HFI client not available\n");
		return -ENODEV;
	}

	/* Allocate shared buffer for DCP to populate with info data */
	info_addr_map = dp_mgr_hfi_init_shared_addr(hfi_client, SZ_1K);
	if (!info_addr_map) {
		DP_ERR("Failed to allocate shared buffer for READ_INFO\n");
		return -ENOMEM;
	}

	DP_INFO("Allocated info buffer: local=%p, remote=0x%llx, size=%u\n",
		info_addr_map->local_addr, (u64) info_addr_map->remote_addr,
		info_addr_map->size);

	/* Prepare command payload with buffer address for DCP to write to */
	cmd_payload.buffer_addr = info_addr_map->remote_addr;
	cmd_payload.buffer_size = info_addr_map->size;

	/* Send HFI command with shared buffer address */
	rc = dp_debug_hfi_send_cmd(priv, hfi_client,
			HFI_COMMAND_DEBUG_DP_READ_INFO,
			HFI_PAYLOAD_TYPE_U32_ARRAY, &cmd_payload, sizeof(cmd_payload),
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc) {
		DP_ERR("Failed to send READ_INFO command to DCP, rc=%d\n", rc);
		dp_mgr_init_deinit_shared_addr(hfi_client, info_addr_map);
		return rc;
	}

	DP_INFO("READ_INFO command sent, DCP will populate shared buffer\n");

	/* Read the data that DCP populated in the shared buffer */
	info_data = (u32 *)info_addr_map->local_addr;

	/* Parse INFO response according to hfi_dp_sim_read_info_response structure */
	/* Field order matches DCP's hfi_dp_sim_read_info_response structure */
	status = info_data[0];                    /* status */
	state = info_data[1];                     /* state */
	link_rate = info_data[2];                 /* link_rate */
	lane_count = info_data[3];                /* lane_count */
	h_active = info_data[4];                  /* h_active */
	v_active = info_data[5];                  /* v_active */
	refresh_rate = info_data[6];              /* refresh_rate */
	pixel_clk_khz = info_data[7];             /* pixel_clk_khz */
	bpp = info_data[8];                       /* bpp */
	test_req = info_data[9];                  /* test_req */
	bw_code = info_data[10];                  /* bw_code */
	v_level = info_data[11];                  /* v_level */
	p_level = info_data[12];                  /* p_level */

	DP_INFO("LINK: status=%u, state=0x%x, link_rate=%u, lanes=%u, res=%ux%u@%uHz, bpp=%u\n",
		status, state, link_rate, lane_count, h_active, v_active, refresh_rate, bpp);

	/* Format response in the expected format for the Python script */
	len = scnprintf(buf, size, "\tstate=0x%x\n", state);

	/* Add display information from DCP response */
	if (len < size - 50)
		len += scnprintf(buf + len, size - len, "\tlink_rate=%u\n", link_rate);
	if (len < size - 50)
		len += scnprintf(buf + len, size - len, "\tnum_lanes=%u\n", lane_count);
	if (len < size - 50)
		len += scnprintf(buf + len, size - len, "\tresolution=%ux%u@%uHz\n",
			h_active, v_active, refresh_rate);
	if (len < size - 50)
		len += scnprintf(buf + len, size - len, "\tpclock=%uKHz\n", pixel_clk_khz);
	if (len < size - 50)
		len += scnprintf(buf + len, size - len, "\tbpp=%u\n", bpp);
	if (len < size - 50)
		len += scnprintf(buf + len, size - len, "\ttest_req=%u\n", test_req);
	if (len < size - 50)
		len += scnprintf(buf + len, size - len, "\tlane_count=%u\n", lane_count);
	if (len < size - 50)
		len += scnprintf(buf + len, size - len, "\tbw_code=%u\n", bw_code);
	if (len < size - 50)
		len += scnprintf(buf + len, size - len, "\tv_level=%u\n", v_level);
	if (len < size - 50)
		len += scnprintf(buf + len, size - len, "\tp_level=%u\n", p_level);

	/* Free the shared buffer */
	dp_mgr_init_deinit_shared_addr(hfi_client, info_addr_map);

	DP_DEBUG("Returning DP info from DCP: state=0x%x, %ux%u@%uHz, %u lanes, rate=%u\n",
		state, h_active, v_active, refresh_rate, lane_count, link_rate);
	return len;
}

static int dp_debug_client_hfi_read_bw_code(struct dp_debug_client *client,
		char *buf, u32 size)
{
	struct dp_debug_client_hfi_priv *priv;
	struct hfi_client_t *hfi_client;
	int rc;
	u32 bw_code = 0;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;
	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client) {
		/* Fallback: return default value if HFI client not available */
		return scnprintf(buf, size, "max_bw_code = 0\n");
	}

	/* Send HFI command and wait for response from DCP */
	rc = dp_debug_hfi_send_cmd_with_response(priv, hfi_client,
			HFI_COMMAND_DEBUG_DP_READ_BW_CODE,
			HFI_PAYLOAD_TYPE_NONE, NULL, 0,
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE,
			HFI_RESPONSE_BW_CODE, 1000); /* 1 second timeout */
	if (rc) {
		DP_ERR("Failed to get BW_CODE response from DCP, rc=%d\n", rc);
		/* Return default value on error */
		return scnprintf(buf, size, "max_bw_code = 0\n");
	}

	/* Get the response data */
	mutex_lock(&priv->response_data.response_lock);
	bw_code = priv->response_data.bw_code;
	mutex_unlock(&priv->response_data.response_lock);

	return scnprintf(buf, size, "max_bw_code = %u\n", bw_code);
}

static int dp_debug_client_hfi_read_tpg(struct dp_debug_client *client,
		char *buf, u32 size)
{
	if (!client || !buf)
		return -EINVAL;

	return scnprintf(buf, size, "%d\n", client->tpg_pattern);
}

static int dp_debug_client_hfi_read_dump(struct dp_debug_client *client,
		char *buf, u32 size, const char *reg_name)
{
	if (!client || !buf || !reg_name)
		return -EINVAL;

	return 0;
}

static int dp_debug_client_hfi_read_mst_mode(struct dp_debug_client *client,
		char *buf, u32 size)
{
	return scnprintf(buf, size, "mst_mode = %u, mst_state = %u\n", 0, 0);
}

static int dp_debug_client_hfi_read_max_pclk_khz(struct dp_debug_client *client,
		char *buf, u32 size)
{
	if (!client || !buf)
		return -EINVAL;

	return scnprintf(buf, size, "max_pclk_khz = %d, org: %d\n",
			client->max_pclk_khz, client->max_pclk_khz);
}

static int dp_debug_client_hfi_read_hdcp(struct dp_debug_client *client,
		char *buf, u32 size)
{
	u32 len = 0;

	if (!client || !buf)
		return -EINVAL;

	len = sizeof(client->hdcp_status);
	len = min_t(u32, size, len);

	memcpy(buf, client->hdcp_status, len);

	return len;
}

static int dp_debug_client_hfi_read_hdr(struct dp_debug_client *client,
		char *buf, u32 size, int panel_id)
{
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector = NULL;
	struct dp_debug_client_hfi_priv *priv;
	struct dp_drv *dp_drv;
	bool in_list = false;
	int len = 0;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;

	/* Get connector from device */
	dp_drv = dp_debug_hfi_get_dp_drv(priv);
	if (!dp_drv || !dp_drv->client || !dp_drv->client->base_connector)
		return -ENODEV;

	/* For panel_id == 0, use the base connector */
	if (panel_id == 0) {
		connector = dp_drv->client->base_connector;
	} else {
		/* For MST panels, find the connector by mst_con_id */
		drm_connector_list_iter_begin(dp_drv->client->base_connector->dev, &conn_iter);
		drm_for_each_connector_iter(connector, &conn_iter) {
			if (connector->base.id == client->mst_con_id) {
				in_list = true;
				break;
			}
		}
		drm_connector_list_iter_end(&conn_iter);

		if (!in_list) {
			DP_ERR("connector %u not in mst list\n", client->mst_con_id);
			return -EINVAL;
		}
	}

	if (!connector) {
		DP_ERR("connector is NULL\n");
		return -EINVAL;
	}

	len = dp_debug_client_hfi_print_hdr_params_to_buf(connector, buf, size);
	if (len == -EOVERFLOW) {
		DP_ERR("HDR buffer overflow\n");
		return len;
	}

	return len;
}

static int dp_debug_client_hfi_read_edid_modes(struct dp_debug_client *client,
		char *buf, u32 size)
{
	struct dp_debug_client_hfi_priv *priv;
	struct dp_drv *dp_drv;
	struct drm_connector *connector;
	struct drm_display_mode *mode;
	u32 len = 0, ret = 0, max_size = size;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;

	/* Get dp_drv to access base connector (equivalent to legacy client->connector) */
	dp_drv = dp_debug_hfi_get_dp_drv(priv);
	if (!dp_drv || !dp_drv->client || !dp_drv->client->base_connector)
		return -ENODEV;

	connector = dp_drv->client->base_connector;

	if (!connector) {
		DP_ERR("connector is NULL\n");
		return -EINVAL;
	}

	mutex_lock(&connector->dev->mode_config.mutex);
	list_for_each_entry(mode, &connector->modes, head) {
		ret = scnprintf(buf + len, max_size,
			"%s %d %d %d %d %d 0x%x\n",
			mode->name,
			drm_mode_vrefresh(mode),
			mode->picture_aspect_ratio,
			mode->htotal,
			mode->vtotal,
			mode->clock,
			mode->flags);
		if (dp_debug_client_hfi_check_buffer_overflow(ret, &max_size, &len))
			break;
	}
	mutex_unlock(&connector->dev->mode_config.mutex);

	return len;
}

static int dp_debug_client_hfi_read_edid_modes_mst(struct dp_debug_client *client,
		char *buf, u32 size)
{
	struct dp_debug_client_hfi_priv *priv;
	struct dp_drv *dp_drv;
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector = NULL;
	struct drm_display_mode *mode;
	u32 len = 0, ret = 0, max_size = size;
	bool found = false;
	struct platform_device *pdev;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;

	if (!priv->dev)
		return -ENODEV;

	/* Get base dp_drv / base connector (equivalent to legacy client->connector) */
	pdev = to_platform_device(priv->dev);
	dp_drv = platform_get_drvdata(pdev);
	if (!dp_drv || !dp_drv->client || !dp_drv->client->base_connector)
		return -ENODEV;

	/* Find MST connector by client->mst_con_id */
	drm_connector_list_iter_begin(dp_drv->client->base_connector->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->base.id == client->mst_con_id) {
			found = true;
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	if (!found || !connector) {
		DP_ERR("MST connector %u not found for read_edid_modes_mst\n", client->mst_con_id);
		return -EINVAL;
	}

	mutex_lock(&connector->dev->mode_config.mutex);
	list_for_each_entry(mode, &connector->modes, head) {
		ret = scnprintf(buf + len, max_size,
			"%s %d %d %d %d %d 0x%x\n",
			mode->name,
			drm_mode_vrefresh(mode),
			mode->picture_aspect_ratio,
			mode->htotal,
			mode->vtotal,
			mode->clock,
			mode->flags);
		if (dp_debug_client_hfi_check_buffer_overflow(ret, &max_size, &len))
			break;
	}
	mutex_unlock(&connector->dev->mode_config.mutex);

	return len;
}

static int dp_debug_client_hfi_read_mst_conn_info(struct dp_debug_client *client,
		char *buf, u32 size)
{
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;
	struct sde_connector *sde_conn;
	struct dp_drv *drv;
	struct dp_debug_client_hfi_priv *priv;
	struct platform_device *pdev;
	struct dp_drv *dp_drv;
	u32 len = 0, ret = 0, max_size = size;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;

	if (!priv->dev)
		return -ENODEV;

	pdev = to_platform_device(priv->dev);
	dp_drv = platform_get_drvdata(pdev);
	if (!dp_drv || !dp_drv->client || !dp_drv->client->base_connector)
		return -ENODEV;

	drm_connector_list_iter_begin(dp_drv->client->base_connector->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		sde_conn = to_sde_connector(connector);
		drv = sde_conn->display;
		if (!sde_conn->mst_port ||
				drv->client->base_connector != dp_drv->client->base_connector)
			continue;
		ret = scnprintf(buf + len, max_size,
				"conn name:%s, conn id:%d state:%d\n",
				connector->name, connector->base.id,
				connector->status);
		if (dp_debug_client_hfi_check_buffer_overflow(ret, &max_size, &len))
			break;
	}
	drm_connector_list_iter_end(&conn_iter);

	return len;
}

/* Write operations */
static int dp_debug_client_hfi_write_edid(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	struct dp_debug_client_hfi_priv *priv;
	u8 *input_buf = NULL, *buf_t = NULL, *edid = NULL;
	const int char_to_nib = 2;
	size_t edid_size = 0;
	size_t size = 0, edid_buf_index = 0;
	int rc = count;
	u32 hfi_cmd;
	struct hfi_client_t *hfi_client;
	struct hfi_shared_addr_map *edid_addr_map = NULL;
	struct hfi_buff edid_request;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;

	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client) {
		rc = -ENODEV;
		goto bail;
	}

	/* Allocate shared buffer for DCP to populate with DPCD data */
	edid_addr_map = dp_mgr_hfi_init_shared_addr(hfi_client, SZ_1K);
	if (!edid_addr_map) {
		DP_ERR("Failed to allocate shared buffer for READ_DPCD\n");
		return -ENOMEM;
	}

	DP_INFO("Allocated EDID buffer: local=%pK, remote=0x%llx, size=%u\n",
		edid_addr_map->local_addr, (u64) edid_addr_map->remote_addr,
		edid_addr_map->size);

	size = min_t(size_t, count, SZ_1K);

	memcpy(edid_addr_map->local_addr, buf, size);

	edid_size = size / char_to_nib;
	buf_t = (u8 *) buf;
	size = edid_size;

	edid = (u8 *) edid_addr_map->local_addr;

	while (size--) {
		char t[3];
		int d;

		memcpy(t, buf_t, char_to_nib);
		t[char_to_nib] = '\0';

		if (kstrtoint(t, 16, &d)) {
			DP_ERR("kstrtoint error\n");
			rc = -EINVAL;
			goto bail;
		}

		edid[edid_buf_index++] = d;
		buf_t += char_to_nib;
	}

	/* Choose command based on MST mode */
	if (client->mst_edid_idx > 0) {
		/* MST mode: Use MST_WRITE_PORT_EDID command */
		rc = 0;
	} else {
		/* SST mode: Use regular WRITE_EDID command */
		hfi_cmd = HFI_COMMAND_DEBUG_DP_SET_EDID;

		DP_INFO("Writing EDID for SST mode (size=%zu)\n", edid_buf_index);
		edid_request.size = edid_size;
		edid_request.addr_l = HFI_VAL_L32(edid_addr_map->remote_addr);
		edid_request.addr_h = HFI_VAL_H32(edid_addr_map->remote_addr);

		rc = dp_debug_hfi_send_cmd(priv, hfi_client, hfi_cmd,
				HFI_PAYLOAD_TYPE_U32_ARRAY, &edid_request,
				sizeof(edid_request),
				HFI_HOST_FLAGS_RESPONSE_REQUIRED | HFI_HOST_FLAGS_NON_DISCARDABLE);
		if (rc) {
			DP_ERR("Failed to send HFI_COMMAND_DEBUG_DP_SET_EDID, rc=%d\n", rc);
			goto bail;
		}
		rc = count;
	}

bail:
	kfree(input_buf);
	kfree(edid);
	return rc;
}

static int dp_debug_client_hfi_write_dpcd(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	struct dp_debug_client_hfi_priv *priv;
	u8 *input_buf = NULL, *buf_t = NULL, *dpcd = NULL;
	const int char_to_nib = 2;
	size_t dpcd_size = 0;
	size_t size = 0, dpcd_buf_index = 0;
	char offset_ch[5];
	u32 offset, data_len;
	int rc = 0;
	struct hfi_client_t *hfi_client;
	struct {
		u32 offset;
		u32 size;
		u8 data[256];
	} cmd_data;

	if (!client || !buf || count < 4)
		return -EINVAL;

	priv = client->priv;

	size = min_t(size_t, count, SZ_2K);

	if (size < 4)
		return -EINVAL;

	input_buf = kzalloc(size, GFP_KERNEL);
	if (!input_buf)
		return -ENOMEM;

	memcpy(input_buf, buf, size);

	memcpy(offset_ch, input_buf, 4);
	offset_ch[4] = '\0';

	if (kstrtoint(offset_ch, 16, &offset)) {
		DP_ERR("offset kstrtoint error\n");
		rc = -EINVAL;
		goto bail;
	}

	size -= 4;
	if (size < char_to_nib) {
		rc = -EINVAL;
		goto bail;
	}

	dpcd_size = size / char_to_nib;
	data_len = dpcd_size;
	buf_t = input_buf + 4;

	dpcd = kzalloc(dpcd_size, GFP_KERNEL);
	if (!dpcd) {
		rc = -ENOMEM;
		goto bail;
	}

	while (dpcd_size--) {
		char t[3];
		int d;

		memcpy(t, buf_t, char_to_nib);
		t[char_to_nib] = '\0';

		if (kstrtoint(t, 16, &d)) {
			DP_ERR("kstrtoint error\n");
			rc = -EINVAL;
			goto bail;
		}

		dpcd[dpcd_buf_index++] = d;
		buf_t += char_to_nib;
	}

	/* Prepare command data */
	memset(&cmd_data, 0, sizeof(cmd_data));
	cmd_data.offset = offset;
	cmd_data.size = dpcd_buf_index;
	memcpy(cmd_data.data, dpcd, min_t(size_t, dpcd_buf_index, sizeof(cmd_data.data)));

	/* Send immediately - no batching */
	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client) {
		rc = -ENODEV;
		goto bail;
	}

	rc = dp_debug_hfi_send_cmd(priv, hfi_client,
			HFI_COMMAND_DEBUG_DP_SET_DPCD,
			HFI_PAYLOAD_TYPE_U32_ARRAY, &cmd_data,
			sizeof(cmd_data.offset) + sizeof(cmd_data.size) + cmd_data.size,
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc) {
		DP_ERR("Failed to send HFI_COMMAND_DEBUG_DP_SET_DPCD, rc=%d\n", rc);
		goto bail;
	}
	rc = count;

bail:
	kfree(input_buf);
	kfree(dpcd);
	return rc;
}

/* Helper function to get dp_mgr_hfi_priv from debug client */
static struct dp_mgr_hfi_priv *dp_debug_hfi_get_mgr_priv(struct dp_debug_client_hfi_priv *priv)
{
	struct platform_device *pdev;
	struct dp_drv *dp_drv;

	if (!priv || !priv->dev)
		return NULL;

	pdev = to_platform_device(priv->dev);
	dp_drv = platform_get_drvdata(pdev);

	if (!dp_drv || !dp_drv->client)
		return NULL;

	/* dp_drv->client is actually dp_mgr_hfi_priv.client, so we can get the container */
	return container_of(dp_drv->client, struct dp_mgr_hfi_priv, client);
}

static int dp_debug_client_hfi_write_hpd(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	struct dp_debug_client_hfi_priv *priv;
	struct dp_mgr_hfi_priv *mgr_priv;
	int hpd = 0;
	int rc = 0;

	if (!client || !buf)
		return -EINVAL;

	if (kstrtoint(buf, 10, &hpd) != 0)
		return -EINVAL;

	priv = client->priv;

	client->hotplug = !!(hpd & BIT(0));
	client->psm_enabled = !!(hpd & BIT(1));

	DP_INFO("%s\n", client->hotplug ? "[CONNECT]" : "[DISCONNECT]");

	/* Get dp_mgr_hfi_priv to call the HPD configure callback */
	mgr_priv = dp_debug_hfi_get_mgr_priv(priv);
	if (!mgr_priv) {
		DP_ERR("Could not access dp_mgr_hfi_priv\n");
		return -ENODEV;
	}

	DP_INFO("Successfully accessed dp_mgr_hfi_priv: %p\n", mgr_priv);

	/* Call dp_mgr_hfi_hpd_configure_cb() instead of sending HFI command directly */
	if (client->hotplug) {
		DP_INFO("Soft HPD Plug Start\n");
		mgr_priv->soft_unplug = false;

		/* Update HPD structure with simulation values */
		if (mgr_priv->hpd) {
			/*
			 * For real monitor pin config comes from altmode driver which was cached
			 * on unlug
			 */
			if (client->sim_enable) {
				mgr_priv->hpd->pin_config = 5;
				if (mgr_priv->hpd->orientation == ORIENTATION_NONE)
					mgr_priv->hpd->orientation = ORIENTATION_CC1;
			} else {
				/* restore cached values */
				mgr_priv->hpd->pin_config = priv->hpd_pin_config;
				mgr_priv->hpd->orientation = priv->hpd_orientation;
			}

			mgr_priv->hpd->hpd_high = true;
			mgr_priv->hpd->hpd_irq = false;
		}

		/*
		 * Call the HPD configure callback which will handle buffer allocation and HFI
		 * command
		 */
		rc = dp_mgr_hfi_hpd_configure_cb(mgr_priv);
		DP_INFO("Soft HPD Plug completed with rc:%d\n", rc);
	} else {
		DP_INFO("Soft HPD Unplug Start\n");
		mgr_priv->soft_unplug = true;

		/* Update HPD structure */
		if (mgr_priv->hpd) {
			/* cache hpd values */
			priv->hpd_pin_config = mgr_priv->hpd->pin_config;
			priv->hpd_orientation = mgr_priv->hpd->orientation;

			mgr_priv->hpd->hpd_irq = false;
			mgr_priv->hpd->hpd_high = false;
			mgr_priv->hpd->pin_config = 0;
			mgr_priv->hpd->orientation = ORIENTATION_NONE;
		}

		reinit_completion(&mgr_priv->hpd_comp);

		/*
		 * Call the HPD disconnect callback and wait till all pending work is completed
		 * before returning.
		 */
		rc = dp_mgr_hfi_hpd_disconnect_cb(mgr_priv);
		if (rc) {
			DP_ERR("disonnect cb failed with rc=%d\n", rc);
		} else if (!wait_for_completion_timeout(&mgr_priv->hpd_comp, HZ)) {
			DP_ERR("wait for hpd disconnect processing timeout\n");
			rc = -ETIMEDOUT;
		}

		DP_INFO("Soft HPD Unplug completed with rc:%d\n", rc);
	}

	return rc ? rc : count;
}

static int dp_debug_client_hfi_write_edid_modes(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	struct dp_debug_client_hfi_priv *priv;
	struct dp_mgr_hfi_priv *mgr_priv;
	int hdisplay = 0, vdisplay = 0, vrefresh = 0, aspect_ratio = 0;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;

	/* Parse input */
	if (sscanf(buf, "%d %d %d %d", &hdisplay, &vdisplay, &vrefresh, &aspect_ratio) != 4)
		goto clear;

	if (!hdisplay || !vdisplay || !vrefresh)
		goto clear;

	/* Get dp_mgr_hfi_priv */
	mgr_priv = dp_debug_hfi_get_mgr_priv(priv);
	if (!mgr_priv) {
		DP_ERR("Could not access dp_mgr_hfi_priv\n");
		return -ENODEV;
	}

	/* Set mode override */
	mgr_priv->mode_ovr.enabled = true;
	mgr_priv->mode_ovr.h_active = hdisplay;
	mgr_priv->mode_ovr.v_active = vdisplay;
	mgr_priv->mode_ovr.refresh_rate = vrefresh;
	mgr_priv->mode_ovr.aspect_ratio = aspect_ratio;

	DP_DEBUG("Set mode override: %dx%d@%dHz, aspect=%d\n",
		hdisplay, vdisplay, vrefresh, aspect_ratio);
	return count;

clear:
	DP_DEBUG("clearing debug modes\n");

	mgr_priv = dp_debug_hfi_get_mgr_priv(priv);
	if (mgr_priv)
		memset(&mgr_priv->mode_ovr, 0, sizeof(mgr_priv->mode_ovr));

	return count;
}

static int dp_debug_client_hfi_write_edid_modes_mst(struct dp_debug_client *client,
		const char *buf)
{
	struct dp_debug_client_hfi_priv *priv;
	struct platform_device *pdev;
	struct dp_drv *dp_drv;
	struct drm_connector *connector;
	int con_id = 0, offset = 0, debug_en = 0;
	int hdisplay, vdisplay, vrefresh, aspect_ratio;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;

	if (!priv->dev)
		return -ENODEV;

	pdev = to_platform_device(priv->dev);
	dp_drv = platform_get_drvdata(pdev);
	if (!dp_drv || !dp_drv->client || !dp_drv->client->base_connector)
		return -ENODEV;

	while (sscanf(buf, "%d %d %d %d %d %d%n",
		      &debug_en, &con_id,
		      &hdisplay, &vdisplay, &vrefresh, &aspect_ratio,
		      &offset) == 6) {
		struct dp_mgr_hfi_priv *mgr_priv;

		DP_DEBUG("MST EDID modes: debug_en=%d, con_id=%d, %dx%d@%dHz, aspect=%d\n",
			 debug_en, con_id, hdisplay, vdisplay, vrefresh, aspect_ratio);

		connector = drm_connector_lookup(dp_drv->client->base_connector->dev,
				NULL, con_id);
		if (!connector) {
			DP_ERR("invalid connector id %d\n", con_id);
			buf += offset;
			continue;
		}

		/* For now we use the same global override (mgr_priv->mode_ovr)
		 * for both SST and MST. If needed, this can be extended to be
		 * per-MST-connector in dp_mgr_hfi_priv.
		 */
		mgr_priv = dp_debug_hfi_get_mgr_priv(priv);
		if (!mgr_priv) {
			DP_ERR("Could not access dp_mgr_hfi_priv for MST override\n");
			drm_connector_put(connector);
			return -ENODEV;
		}

		if (!debug_en || !hdisplay || !vdisplay || !vrefresh) {
			DP_DEBUG("clearing MST override (con_id=%d)\n", con_id);
			memset(&mgr_priv->mode_ovr, 0, sizeof(mgr_priv->mode_ovr));
		} else {
			mgr_priv->mode_ovr.enabled = true;
			mgr_priv->mode_ovr.h_active = hdisplay;
			mgr_priv->mode_ovr.v_active = vdisplay;
			mgr_priv->mode_ovr.refresh_rate = vrefresh;
			mgr_priv->mode_ovr.aspect_ratio = aspect_ratio;

			DP_DEBUG("Set MST override: %dx%d@%dHz, aspect=%d (con_id=%d)\n",
				 hdisplay, vdisplay, vrefresh, aspect_ratio, con_id);
		}

		drm_connector_put(connector);
		buf += offset;
	}

	return 0;
}

static int dp_debug_client_hfi_write_mst_con_id(struct dp_debug_client *client,
		int con_id, int status)
{
	return 0;
}

static int dp_debug_client_hfi_write_mst_con_add(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	return 0;
}

static int dp_debug_client_hfi_write_mst_con_remove(struct dp_debug_client *client,
		int con_id)
{
	return 0;
}

static int dp_debug_client_hfi_write_bw_code(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	struct dp_debug_client_hfi_priv *priv;
	u32 max_bw_code = 0;
	int rc;
	struct hfi_client_t *hfi_client;

	if (!client || !buf)
		return -EINVAL;

	if (kstrtoint(buf, 10, &max_bw_code) != 0)
		return -EINVAL;

	priv = client->priv;

	/* In HFI mode, DCP firmware handles bandwidth validation */
	DP_DEBUG("Setting bw_code: %d (validation handled by DCP)\n", max_bw_code);

	/* Send immediately - no batching */
	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client)
		return -ENODEV;

	rc = dp_debug_hfi_send_cmd(priv, hfi_client,
			HFI_COMMAND_DEBUG_DP_SET_BW_CODE,
			HFI_PAYLOAD_TYPE_U32_ARRAY, &max_bw_code, sizeof(max_bw_code),
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc) {
		DP_ERR("Failed to send HFI_COMMAND_DEBUG_DP_SET_BW_CODE, rc=%d\n", rc);
		return rc;
	}

	return count;
}

static int dp_debug_client_hfi_write_mst_mode(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	return count;
}

static int dp_debug_client_hfi_write_max_pclk_khz(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	u32 max_pclk = 0;

	if (!client || !buf)
		return -EINVAL;

	if (kstrtoint(buf, 10, &max_pclk) != 0)
		return -EINVAL;

	DP_DEBUG("max_pclk_khz: %d (managed by DCP)\n", max_pclk);

	return count;
}

static int dp_debug_client_hfi_write_mst_sideband_mode(struct dp_debug_client *client,
		int mst_sideband_mode, u32 mst_port_cnt)
{
	return 0;
}

static int dp_debug_client_hfi_write_tpg(struct dp_debug_client *client, u32 tpg_pattern)
{
	struct dp_debug_client_hfi_priv *priv;
	struct hfi_client_t *hfi_client;
	int rc;

	if (!client)
		return -EINVAL;

	priv = client->priv;
	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client)
		return -ENODEV;

	DP_DEBUG("tpg_pattern: %d\n", tpg_pattern);

	rc = dp_debug_hfi_send_cmd(priv, hfi_client,
			HFI_COMMAND_DEBUG_DP_SET_TPG,
			HFI_PAYLOAD_TYPE_U32_ARRAY, &tpg_pattern, sizeof(tpg_pattern),
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc)
		DP_ERR("Failed to send HFI_COMMAND_DEBUG_DP_SET_TPG, rc=%d\n", rc);

	client->tpg_pattern = tpg_pattern;

	return rc;
}

static int dp_debug_client_hfi_write_exe_mode(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	return count;
}

static int dp_debug_client_hfi_write_hdcp(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	struct dp_debug_client_hfi_priv *priv;
	struct hfi_client_t *hfi_client;
	int hdcp = 0;
	int rc;

	if (!client || !buf)
		return -EINVAL;

	if (kstrtoint(buf, 10, &hdcp) != 0)
		return -EINVAL;

	priv = client->priv;
	client->hdcp_disabled = !hdcp;

	/* Send immediately - no batching */
	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client)
		return -ENODEV;

	rc = dp_debug_hfi_send_cmd(priv, hfi_client,
			HFI_COMMAND_DEBUG_DP_HDCP_CONTROL,
			HFI_PAYLOAD_TYPE_U32_ARRAY, &hdcp, sizeof(hdcp),
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc) {
		DP_ERR("Failed to send HFI_COMMAND_DEBUG_DP_HDCP_CONTROL, rc=%d\n", rc);
		return rc;
	}

	return count;
}

static int dp_debug_client_hfi_write_sim(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	int sim;

	if (!client || !buf)
		return -EINVAL;

	if (kstrtoint(buf, 10, &sim) != 0)
		return -EINVAL;

	if (client->write_sim_mode)
		client->write_sim_mode(client, sim);

	return count;
}

static int dp_debug_client_hfi_write_attention(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	int vdo = 0;

	if (!client || !buf)
		return -EINVAL;

	if (kstrtoint(buf, 10, &vdo) != 0)
		return -EINVAL;

	DP_DEBUG("Attention simulation: vdo=%d\n", vdo);

	/* Call simulate_attention through client like legacy implementation */
	if (client->simulate_attention)
		return client->simulate_attention(client, vdo);

	return count;
}

static int dp_debug_client_hfi_write_dump(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	if (!client || !buf)
		return -EINVAL;

	if (!strcmp(buf, "qfprom_physical"))
		return -EINVAL;

	return count;
}

static int dp_debug_client_hfi_write_mmrm_clk_cb(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	struct dp_debug_client_hfi_priv *priv;
	struct platform_device *pdev;
	struct dp_drv *dp_drv;
	int cb_type = 0;
	struct dss_clk_mmrm_cb mmrm_cb_data;
	struct mmrm_client_notifier_data notifier_data;

	if (!client || !buf)
		return -ENODEV;

	if (kstrtoint(buf, 10, &cb_type) != 0)
		return -EINVAL;

	if (cb_type != MMRM_CLIENT_RESOURCE_VALUE_CHANGE) {
		DP_ERR("Invalid MMRM callback type: %d\n", cb_type);
		return -EINVAL;
	}

	DP_DEBUG("MMRM clock callback: type=%d\n", cb_type);

	priv = client->priv;

	/* Get the dp_drv instance from the platform device */
	if (!priv->dev)
		return -ENODEV;

	pdev = to_platform_device(priv->dev);
	dp_drv = platform_get_drvdata(pdev);

	if (!dp_drv) {
		DP_ERR("dp_drv is NULL\n");
		return -ENODEV;
	}

	/* Prepare MMRM notification data */
	notifier_data.cb_type = MMRM_CLIENT_RESOURCE_VALUE_CHANGE;
	mmrm_cb_data.phandle = (void *)dp_drv;
	notifier_data.pvt_data = (void *)&mmrm_cb_data;

	/* Call the MMRM callback function */
	dp_mgr_mmrm_callback(&notifier_data);

	return count;
}

static int dp_debug_client_hfi_write_sim_mode(struct dp_debug_client *client, bool sim)
{
	struct dp_debug_client_hfi_priv *priv;
	struct hfi_client_t *hfi_client;
	u32 sim_enable = sim ? 1 : 0;
	int rc;

	if (!client)
		return -EINVAL;

	priv = client->priv;
	client->sim_enable = sim;

	DP_INFO("Simulation mode %s\n", sim ? "[ON]" : "[OFF]");

	/* Get HFI client */
	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client)
		return -ENODEV;

	if (sim) {
		/* Send SIM_ENABLE command */
		rc = dp_debug_hfi_send_cmd(priv, hfi_client,
				HFI_COMMAND_DEBUG_DP_SIMULATION_CONTROL,
				HFI_PAYLOAD_TYPE_U32_ARRAY, &sim_enable, sizeof(sim_enable),
				HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE);
		if (rc) {
			DP_ERR("Failed to send HFI_COMMAND_DEBUG_DP_SIMULATION_CONTROL, rc=%d\n",
					rc);
			return rc;
		}
	} else {
		/* Disable simulation: perform cleanup like legacy implementation */

		/* Disconnect hotplug if connected */
		if (client->hotplug) {
			DP_WARN("sim mode off before hotplug disconnect\n");
			client->hotplug = false;
		}

		/* Reset MST EDID index like legacy */
		client->mst_edid_idx = 0;

		rc = dp_debug_hfi_send_cmd(priv, hfi_client,
				HFI_COMMAND_DEBUG_DP_SIMULATION_CONTROL,
				HFI_PAYLOAD_TYPE_U32_ARRAY, &sim_enable, sizeof(sim_enable),
				HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE);
		if (rc)
			DP_ERR("Failed to send HFI_COMMAND_DEBUG_DP_SIMULATION_CONTROL, rc=%d\n",
					rc);
	}

	return rc;
}

static int dp_debug_client_hfi_simulate_attention(struct dp_debug_client *client, int vdo)
{
	struct dp_debug_client_hfi_priv *priv;
	struct hfi_client_t *hfi_client;
	int rc;

	if (!client)
		return -EINVAL;

	priv = client->priv;
	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client)
		return -ENODEV;

	rc = dp_debug_hfi_send_cmd(priv, hfi_client,
			HFI_COMMAND_DEBUG_DP_SET_ATTENTION,
			HFI_PAYLOAD_TYPE_U32_ARRAY, &vdo, sizeof(vdo),
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc)
		DP_ERR("Failed to send HFI_COMMAND_DEBUG_DP_SET_ATTENTION, rc=%d\n", rc);

	return rc;
}

static void dp_debug_client_hfi_abort(struct dp_debug_client *client)
{
	if (!client)
		return;

	client->hotplug = false;
	if (client->write_sim_mode)
		client->write_sim_mode(client, false);
}

int dp_debug_client_hfi_get(struct dp_debug_client *client)
{
	struct dp_debug_client_hfi_priv *priv;

	if (!client)
		return -EINVAL;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Store the device from the client for later use */
	priv->dev = client->dev;

	/* Initialize response handling */
	memset(&priv->response_data, 0, sizeof(priv->response_data));
	mutex_init(&priv->response_data.response_lock);
	init_completion(&priv->response_data.response_complete);
	priv->response_data.connected = false;
	priv->response_data.response_received = false;

	/* Initialize HFI callback object - embedded directly like dp_hfi does */
	priv->hfi_cb_obj.hfi_prop_handler = dp_debug_hfi_response_handler;

	/*
	 * Don't create HFI client during initialization - it will be created lazily when first
	 * needed. This avoids timing issues where dp_client might not be available yet during
	 * early boot.
	 */
	priv->hfi_client = NULL;
	DP_DEBUG("HFI client will be created lazily when first needed\n");

	/* Register all function pointers */
	/* Read operations */
	client->read_dpcd = dp_debug_client_hfi_read_dpcd;
	client->read_crc = dp_debug_client_hfi_read_crc;
	client->read_connected = dp_debug_client_hfi_read_connected;
	client->read_info = dp_debug_client_hfi_read_info;
	client->read_bw_code = dp_debug_client_hfi_read_bw_code;
	client->read_tpg = dp_debug_client_hfi_read_tpg;
	client->read_dump = dp_debug_client_hfi_read_dump;
	client->read_max_pclk_khz = dp_debug_client_hfi_read_max_pclk_khz;
	client->read_hdr = dp_debug_client_hfi_read_hdr;
	/* NEW: HDR support */
	client->read_edid_modes = dp_debug_client_hfi_read_edid_modes;
	/* NEW: Reads from connector */
	client->read_hdcp = dp_debug_client_hfi_read_hdcp;

	/* Write operations - send HFI commands to DCP */
	client->write_sim_mode = dp_debug_client_hfi_write_sim_mode;
	client->write_edid = dp_debug_client_hfi_write_edid;
	client->write_dpcd = dp_debug_client_hfi_write_dpcd;
	client->write_bw_code = dp_debug_client_hfi_write_bw_code;
	client->write_hdcp = dp_debug_client_hfi_write_hdcp;
	client->write_hpd = dp_debug_client_hfi_write_hpd;
	client->write_edid_modes = dp_debug_client_hfi_write_edid_modes;
	client->write_max_pclk_khz = dp_debug_client_hfi_write_max_pclk_khz;
	client->write_tpg = dp_debug_client_hfi_write_tpg;
	client->write_exe_mode = dp_debug_client_hfi_write_exe_mode;
	/* Stub - not applicable for HFI */
	client->write_dump = dp_debug_client_hfi_write_dump;
	/* Stub - not applicable for HFI */

	/* MST Functions */
	client->read_mst_mode = dp_debug_client_hfi_read_mst_mode;
	client->read_edid_modes_mst = dp_debug_client_hfi_read_edid_modes_mst;
	/* NEW: MST EDID modes */
	client->read_mst_conn_info = dp_debug_client_hfi_read_mst_conn_info;
	/* NEW: MST connector info */
	client->write_mst_mode = dp_debug_client_hfi_write_mst_mode;
	/* Stub - not used */
	client->write_edid_modes_mst = dp_debug_client_hfi_write_edid_modes_mst;
	/* NEW: MST mode override */
	client->write_mst_con_id = dp_debug_client_hfi_write_mst_con_id;
	/* NEW: MST connector ID */
	client->write_mst_con_add = dp_debug_client_hfi_write_mst_con_add;
	/* NEW: MST connector add */
	client->write_mst_con_remove = dp_debug_client_hfi_write_mst_con_remove;
	/* NEW: MST connector remove */
	client->write_mst_sideband_mode = dp_debug_client_hfi_write_mst_sideband_mode;

	/* Simulation Functions */
	client->write_sim = dp_debug_client_hfi_write_sim;
	client->write_attention = dp_debug_client_hfi_write_attention;
	client->simulate_attention = dp_debug_client_hfi_simulate_attention;

	client->write_mmrm_clk_cb = dp_debug_client_hfi_write_mmrm_clk_cb;
	/* Stub - not applicable for HFI */

	client->abort = dp_debug_client_hfi_abort;

	client->priv = priv;

	DP_INFO("DP HFI debug client initialized successfully\n");
	return 0;
}

void dp_debug_client_hfi_put(struct dp_debug_client *client)
{
	struct dp_debug_client_hfi_priv *priv;
	struct hfi_client_t *hfi_client;
	struct listener_list *listener_entry, *tmp;

	if (!client)
		return;

	priv = client->priv;

	/* CRITICAL: Remove all registered listeners before freeing priv structure
	 * to prevent use-after-free when responses arrive after cleanup
	 */
	hfi_client = priv->hfi_client;
	if (hfi_client) {
		DP_INFO("Unregistering all listeners for HFI client %p before cleanup\n",
				hfi_client);

		mutex_lock(&hfi_client->listener_lock);
		list_for_each_entry_safe(listener_entry, tmp,
				&hfi_client->packet_listeners.list_ptr, list_ptr) {
			/* Check if this listener belongs to our priv structure */
			if (listener_entry->listener_obj == &priv->hfi_cb_obj) {
				DP_INFO("Removing listener for packet_id=%u\n",
						listener_entry->packet_id);
				list_del(&listener_entry->list_ptr);
				kfree(listener_entry);
			}
		}
		mutex_unlock(&hfi_client->listener_lock);
	}

	/* Destroy HFI client reference */
	dp_debug_hfi_destroy_client(priv);

	/* Clean up response handling */
	mutex_destroy(&priv->response_data.response_lock);

	DP_INFO("Freeing dp_debug_client_hfi_priv structure. \n");
	kfree(priv);
}

#else /* CONFIG_DEBUG_FS */

int dp_debug_client_hfi_get(struct dp_debug_client *client)
{
	return -ENODEV;
}

void dp_debug_client_hfi_put(struct dp_debug_client *client)
{
}

#endif /* CONFIG_DEBUG_FS */
