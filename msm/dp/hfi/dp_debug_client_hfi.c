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
#include "hfi_defs_device.h"
#include "sde_connector.h"
#include "hfi_adapter.h"
#include "dp_mgr.h"
#include "dp_mgr_hfi.h"
#include "hfi_defs_display.h"
#include "dp_altmode.h"

#if IS_ENABLED(CONFIG_DEBUG_FS)

/* Response handling for HFI commands */
struct dp_hfi_response_data {
	u32 state;
	u32 bw_code;
	u8 dpcd_data[256];
	u32 dpcd_size;
	struct hfi_dp_crc_info crc_data;
	u32 misr_values[8];
	bool connected;
	bool response_received;
	enum {
		HFI_RESPONSE_NONE,
		HFI_RESPONSE_BW_CODE,
		HFI_RESPONSE_DPCD,
		HFI_RESPONSE_CRC,
		HFI_RESPONSE_MISR
	} response_type;
	struct completion response_complete;
	struct mutex response_lock;
};

struct dp_debug_client_hfi_priv {
	struct device *dev;
	struct hfi_client_t *hfi_client;
	struct dp_hfi_response_data response_data;
	struct hfi_prop_listener hfi_cb_obj;  /* HFI callback listener object */
	u32 hpd_pin_config;              /* Cached HPD pin config */
	u32 hpd_orientation;             /* Cached HPD orientation */
	struct hfi_shared_addr_map *dpcd_addr_map;
	struct hfi_shared_addr_map *edid_addr_map[MAX_DP_MST_STREAMS];
	struct hfi_shared_addr_map *info_addr_map;
	struct drm_connector *mst_conn;
};

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

	rc = scnprintf(buf + len, max_size, "eotf = %d\n", c_conn->hdr_eotf);
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

	rc = scnprintf(buf + len, max_size, "hdr_supported = %d\n", hdr->hdr_supported);
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
		struct sde_connector_dyn_hdr_metadata *dhdr = &c_state->dyn_hdr_meta;

		for (i = 0; i < dhdr->dynamic_hdr_payload_size; i += rowsize) {
			rc = scnprintf(buf + len, max_size, "DHDR: ");
			if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
				goto error;

			rem = dhdr->dynamic_hdr_payload_size - i;
			rc = hex_dump_to_buffer(&dhdr->dynamic_hdr_payload[i],
				min(rowsize, rem), rowsize, 1, buf + len, max_size, false);
			if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
				goto error;

			rc = scnprintf(buf + len, max_size, "\n");
			if (dp_debug_client_hfi_check_buffer_overflow(rc, &max_size, &len))
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

/**
 * dp_debug_hfi_hex_to_bytes - decode a hex-encoded ASCII string into bytes
 * @hex:   input buffer of hex digit pairs (2 chars per byte)
 * @out:   output byte buffer (must hold at least @count bytes)
 * @count: number of bytes to decode
 *
 * Returns 0 on success, -EINVAL on any malformed hex pair.
 */
static int dp_debug_hfi_hex_to_bytes(const u8 *hex, u8 *out, size_t count)
{
	size_t i;
	char t[3];
	int d;

	for (i = 0; i < count; i++) {
		memcpy(t, hex + i * 2, 2);
		t[2] = '\0';
		if (kstrtoint(t, 16, &d)) {
			DP_ERR("kstrtoint error\n");
			return -EINVAL;
		}
		out[i] = d;
	}
	return 0;
}

/*
 * Helper: return the HFI display obj_id of the base (SST) DP connector.
 * Used as the default obj_id for commands that are not MST-port-specific.
 */
static u32 dp_debug_hfi_get_base_obj_id(struct dp_debug_client_hfi_priv *priv)
{
	struct dp_drv *dp_drv = dp_debug_hfi_get_dp_drv(priv);

	if (dp_drv && dp_drv->client && dp_drv->client->base_connector)
		return sde_conn_get_display_obj_id(dp_drv->client->base_connector);

	DP_WARN("Base connector not available, using fallback obj_id=2\n");
	return 2;
}

/* Helper function to send HFI command for simulation mode - doesn't need dp_client */
static int dp_debug_hfi_send_cmd(struct dp_debug_client_hfi_priv *priv,
		struct hfi_client_t *hfi_client, u32 hfi_cmd,
		u32 hfi_payload_type, void *payload, u32 payload_size, u32 flags, u32 obj_id)
{
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	enum hfi_cmdbuf_type cmd_buf_type = HFI_CMDBUF_TYPE_GET_DEBUG_DATA;
	int rc = 0;

	if (!priv || !hfi_client) {
		DP_ERR("Invalid priv or hfi_client\n");
		return -EINVAL;
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
		hfi_adapter_release_cmd_buf(hfi_client, cmd_buf);
		return rc;
	}

	DP_DEBUG("Sending HFI command 0x%x to DCP (obj_id=%u, payload_size=%u)\n",
		hfi_cmd, obj_id, payload_size);

	rc = hfi_adapter_set_cmd_buf_blocking(hfi_client, cmd_buf);
	if (rc)
		DP_ERR("Failed to send hfi_cmd 0x%x, rc=%d\n", hfi_cmd, rc);
	else
		DP_DEBUG("Successfully sent HFI command 0x%x to DCP\n", hfi_cmd);

	return rc;
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
	u32 *payload_ptr = payload;
	u32 num_misr;

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
		if (payload && payload_size == sizeof(struct hfi_dp_crc_info)) {
			memcpy(&priv->response_data.crc_data, payload,
			       sizeof(priv->response_data.crc_data));
			priv->response_data.response_type = HFI_RESPONSE_CRC;
			DP_DEBUG("Received CRC response\n");
		} else {
			DP_WARN("Invalid CRC response payload\n");
			memset(&priv->response_data.crc_data, 0,
			       sizeof(priv->response_data.crc_data));
		}
		break;

	case HFI_COMMAND_DEBUG_MISR_READ: {
		if (payload && payload_size >= 2 * sizeof(u32)) {
			num_misr = min_t(u32, payload_ptr[1],
					ARRAY_SIZE(priv->response_data.misr_values));
			if (num_misr * sizeof(u32) > payload_size - 2 * sizeof(u32)) {
				DP_ERR("Expected misr payload size %zu but got payload size %d\n",
						(num_misr + 2) * sizeof(u32), payload_size);
				break;
			}
			memcpy(priv->response_data.misr_values, &payload_ptr[2],
			       num_misr * sizeof(u32));
			priv->response_data.response_type = HFI_RESPONSE_MISR;
			DP_DEBUG("Received MISR response: module=%u num=%u\n",
					payload_ptr[0], num_misr);
		} else {
			DP_WARN("Invalid MISR response payload\n");
			memset(priv->response_data.misr_values, 0,
					sizeof(priv->response_data.misr_values));
		}
		break;
	}

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

/* Send an HFI get-property command and block until the response arrives */
static int dp_debug_hfi_send_cmd_with_response(struct dp_debug_client_hfi_priv *priv,
		struct hfi_client_t *hfi_client, u32 hfi_cmd,
		u32 hfi_payload_type, void *payload, u32 payload_size, u32 flags,
		int response_type, unsigned long timeout_ms)
{
	struct hfi_cmdbuf_t *cmd_buf;
	enum hfi_cmdbuf_type cmd_buf_type = HFI_CMDBUF_TYPE_GET_DEBUG_DATA;
	int rc;
	/* unsigned long timeout_jiffies = msecs_to_jiffies(timeout_ms); */
	u32 obj_id, packet_id = 0;

	if (!priv || !hfi_client)
		return -EINVAL;

	obj_id = dp_debug_hfi_get_base_obj_id(priv);

	/* Get command buffer */
	cmd_buf = hfi_adapter_get_cmd_buf(hfi_client, obj_id, cmd_buf_type);
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
			hfi_payload_type, payload, payload_size, &priv->hfi_cb_obj, flags,
			true, &packet_id);
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

static int _alloc_addr_map(struct hfi_client_t *hfi_client, struct hfi_shared_addr_map **buf,
		size_t size)
{
	if (*buf && size <= (*buf)->size) {
		memset((*buf)->local_addr, 0, size);
		return 0;
	}

	/* buffer exists but requested size is larger, so free first */
	if (*buf)
		dp_mgr_hfi_deinit_shared_addr(hfi_client, *buf);

	/* allocate */
	*buf = dp_mgr_hfi_init_shared_addr(hfi_client, size);
	if (!*buf) {
		DP_ERR("Failed to allocate shared buffer of size %zu\n", size);
		return -ENOMEM;
	}

	return 0;
}

/* Helper function to get dp_mgr_hfi_priv from debug client */
static struct dp_mgr_hfi_priv *_get_mgr_hfi(struct dp_debug_client_hfi_priv *priv)
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

/*
 * _get_connector - look up a statically-created HFI connector
 * by DRM object ID.
 *
 * In HFI mode connectors are created once at boot and never removed, so we
 * can simply walk hfi_priv->hfi[i]->connector without taking a reference.
 *
 * Returns the matching drm_connector, or NULL if not found.
 */
static struct drm_connector *_get_connector(struct dp_mgr_hfi_priv *mgr_priv,
		u32 con_id)
{
	int i;

	if (!mgr_priv)
		return NULL;

	for (i = 0; i < mgr_priv->max_streams; i++) {
		if (mgr_priv->hfi[i] && mgr_priv->hfi[i]->connector &&
				mgr_priv->hfi[i]->connector->base.id == con_id)
			return mgr_priv->hfi[i]->connector;
	}

	return NULL;
}

/* -------------------------------------------------------------------------
 * Read operations
 * -------------------------------------------------------------------------
 */

/* Read DPCD registers from DCP via a shared memory buffer */
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
	rc = _alloc_addr_map(hfi_client, &priv->dpcd_addr_map, SZ_1K);
	if (rc)
		return rc;

	dpcd_addr_map = priv->dpcd_addr_map;
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
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE,
			dp_debug_hfi_get_base_obj_id(priv));
	if (rc) {
		DP_ERR("Failed to send READ_DPCD command to DCP, rc=%d\n", rc);
		return rc;
	}

	DP_INFO("READ_DPCD command sent, DCP will populate shared buffer\n");

	/* Read the data that DCP populated in the shared buffer */
	dpcd_data = (u8 *)dpcd_addr_map->local_addr;
	actual_size = min_t(u32, size, dpcd_addr_map->size);
	if (actual_size > 0)
		memcpy(dpcd, dpcd_data, actual_size);

	DP_DEBUG("Read %u bytes of DPCD data from offset 0x%x\n", actual_size, offset);
	return actual_size;
}

/* Read frame CRC values from DCP (source and sink R/G/B components) */
static int dp_debug_client_hfi_read_crc(struct dp_debug_client *client,
		char *buf, u32 size)
{
	struct dp_debug_client_hfi_priv *priv;
	struct hfi_client_t *hfi_client;
	struct dp_drv *dp_drv;
	struct misr_setup_data misr_setup;
	struct misr_read_data misr_read;
	u32 ctrl_misr[8];
	u32 phy_misr[8];
	u32 len = 0;
	u16 src_crc[3] = {0};
	u16 sink_crc[3] = {0};
	bool skip_misr = false;
	int rc, i;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;
	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client)
		return -ENODEV;

	memset(ctrl_misr, 0, sizeof(ctrl_misr));
	memset(phy_misr, 0, sizeof(phy_misr));

	dp_drv = dp_debug_hfi_get_dp_drv(priv);
	if (!dp_drv || !dp_drv->client || !dp_drv->client->base_connector) {
		skip_misr = true;
		goto misr_done;
	}

	misr_setup.display_id  = sde_conn_get_display_obj_id(dp_drv->client->base_connector);
	misr_setup.enable      = 1;
	misr_setup.frame_count = 1;

	misr_read.display_id  = misr_setup.display_id;

	/* Setup MISR */
	misr_setup.module_type = HFI_DEBUG_MISR_DP_PHY;
	dp_debug_hfi_send_cmd(priv, hfi_client,
			HFI_COMMAND_DEBUG_MISR_SETUP,
			HFI_PAYLOAD_TYPE_U32_ARRAY,
			&misr_setup, sizeof(misr_setup),
			HFI_HOST_FLAGS_RESPONSE_REQUIRED |
			HFI_HOST_FLAGS_NON_DISCARDABLE,
			dp_debug_hfi_get_base_obj_id(priv));

	misr_setup.module_type = HFI_DEBUG_MISR_DP_CTRL;
	dp_debug_hfi_send_cmd(priv, hfi_client,
			HFI_COMMAND_DEBUG_MISR_SETUP,
			HFI_PAYLOAD_TYPE_U32_ARRAY,
			&misr_setup, sizeof(misr_setup),
			HFI_HOST_FLAGS_RESPONSE_REQUIRED |
			HFI_HOST_FLAGS_NON_DISCARDABLE,
			dp_debug_hfi_get_base_obj_id(priv));

misr_done:
	/* Send HFI command and wait for response from DCP */
	rc = dp_debug_hfi_send_cmd_with_response(priv, hfi_client,
			HFI_COMMAND_DEBUG_DP_READ_CRC,
			HFI_PAYLOAD_TYPE_NONE, NULL, 0,
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE,
			HFI_RESPONSE_CRC, 1000);
	if (rc) {
		DP_ERR("Failed to get CRC response from DCP, rc=%d\n", rc);
		len += scnprintf(buf + len, size - len, "FRAME_CRC:\nSource vs Sink\n");
		len += scnprintf(buf + len, size - len, "CRC_R: 0000 0000\n");
		len += scnprintf(buf + len, size - len, "CRC_G: 0000 0000\n");
		len += scnprintf(buf + len, size - len, "CRC_B: 0000 0000\n");
		return len;
	}

	/* Get CRC response data */
	mutex_lock(&priv->response_data.response_lock);
	src_crc[0] = priv->response_data.crc_data.src_crc[0];
	src_crc[1] = priv->response_data.crc_data.src_crc[1];
	src_crc[2] = priv->response_data.crc_data.src_crc[2];
	sink_crc[0] = priv->response_data.crc_data.sink_crc[0];
	sink_crc[1] = priv->response_data.crc_data.sink_crc[1];
	sink_crc[2] = priv->response_data.crc_data.sink_crc[2];
	mutex_unlock(&priv->response_data.response_lock);

	len += scnprintf(buf + len, size - len, "FRAME_CRC:\nSource vs Sink\n");
	len += scnprintf(buf + len, size - len, "CRC_R: %04X %04X\n", src_crc[0], sink_crc[0]);
	len += scnprintf(buf + len, size - len, "CRC_G: %04X %04X\n", src_crc[1], sink_crc[1]);
	len += scnprintf(buf + len, size - len, "CRC_B: %04X %04X\n", src_crc[2], sink_crc[2]);

	if (!skip_misr) {
		/* Read MISR */
		misr_read.module_type = HFI_DEBUG_MISR_DP_PHY;
		rc = dp_debug_hfi_send_cmd_with_response(priv, hfi_client,
				HFI_COMMAND_DEBUG_MISR_READ,
				HFI_PAYLOAD_TYPE_U32_ARRAY,
				&misr_read, sizeof(misr_read),
				HFI_HOST_FLAGS_RESPONSE_REQUIRED |
				HFI_HOST_FLAGS_NON_DISCARDABLE,
				HFI_RESPONSE_MISR, 1000);
		if (!rc) {
			mutex_lock(&priv->response_data.response_lock);
			memcpy(phy_misr, priv->response_data.misr_values, sizeof(phy_misr));
			mutex_unlock(&priv->response_data.response_lock);
		}

		misr_read.module_type = HFI_DEBUG_MISR_DP_CTRL;
		rc = dp_debug_hfi_send_cmd_with_response(priv, hfi_client,
				HFI_COMMAND_DEBUG_MISR_READ,
				HFI_PAYLOAD_TYPE_U32_ARRAY,
				&misr_read, sizeof(misr_read),
				HFI_HOST_FLAGS_RESPONSE_REQUIRED |
				HFI_HOST_FLAGS_NON_DISCARDABLE,
				HFI_RESPONSE_MISR, 1000);
		if (!rc) {
			mutex_lock(&priv->response_data.response_lock);
			memcpy(ctrl_misr, priv->response_data.misr_values, sizeof(ctrl_misr));
			mutex_unlock(&priv->response_data.response_lock);
		}
	}

	len += scnprintf(buf + len, size - len, "\nMISR40:\nCTLR vs PHY\n");
	for (i = 0; i < 4; i++) {
		len += scnprintf(buf + len, size - len,
				"Lane%d %08X%08X %08X%08X\n", i,
				ctrl_misr[2 * i], ctrl_misr[(2 * i) + 1],
				phy_misr[2 * i],  phy_misr[(2 * i) + 1]);
	}

	return len;
}

/* Report current hotplug/connection state (1 = connected, 0 = disconnected) */
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

/* Read DP link info (state, rate, lanes, resolution, bpp, etc.) from DCP */
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
	rc = _alloc_addr_map(hfi_client, &priv->info_addr_map, SZ_1K);
	if (rc)
		return rc;

	info_addr_map = priv->info_addr_map;
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
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE,
			dp_debug_hfi_get_base_obj_id(priv));
	if (rc) {
		DP_ERR("Failed to send READ_INFO command to DCP, rc=%d\n", rc);
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

	DP_DEBUG("Returning DP info from DCP: state=0x%x, %ux%u@%uHz, %u lanes, rate=%u\n",
		state, h_active, v_active, refresh_rate, lane_count, link_rate);
	return len;
}

/* Read maximum bandwidth code from DCP */
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

static int dp_debug_client_hfi_read_max_lclk_khz(struct dp_debug_client *client,
		char *buf, u32 size)
{
	struct dp_debug_client_hfi_priv *priv;
	struct hfi_client_t *hfi_client;
	int rc;
	u32 bw_code = 0;
	u32 max_lclk_khz;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;
	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client) {
		/* Fallback: return 0 if HFI client not available */
		return scnprintf(buf, size, "max_lclk_khz = 0\n");
	}

	/* Reuse the same HFI read command as read_bw_code */
	rc = dp_debug_hfi_send_cmd_with_response(priv, hfi_client,
			HFI_COMMAND_DEBUG_DP_READ_BW_CODE,
			HFI_PAYLOAD_TYPE_NONE, NULL, 0,
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE,
			HFI_RESPONSE_BW_CODE, 1000);
	if (rc) {
		DP_ERR("Failed to get BW_CODE response from DCP, rc=%d\n", rc);
		return scnprintf(buf, size, "max_lclk_khz = 0\n");
	}

	mutex_lock(&priv->response_data.response_lock);
	bw_code = priv->response_data.bw_code;
	mutex_unlock(&priv->response_data.response_lock);

	/* Convert bw_code back to lclk_khz: each unit is 270000 KHz */
	max_lclk_khz = bw_code * 270000;

	DP_DEBUG("bw_code=%u -> max_lclk_khz=%u\n", bw_code, max_lclk_khz);

	return scnprintf(buf, size, "max_lclk_khz = %u\n", max_lclk_khz);
}

/* Return the current test pattern generator (TPG) pattern index */
static int dp_debug_client_hfi_read_tpg(struct dp_debug_client *client,
		char *buf, u32 size)
{
	if (!client || !buf)
		return -EINVAL;

	return scnprintf(buf, size, "%d\n", client->tpg_pattern);
}

/* Stub: register dump is not implemented for HFI mode */
static int dp_debug_client_hfi_read_dump(struct dp_debug_client *client,
		char *buf, u32 size, const char *reg_name)
{
	if (!client || !buf || !reg_name)
		return -EINVAL;

	return 0;
}

/* Report MST mode (enabled/disabled) and current MST state from dp_mgr_hfi_priv */
static int dp_debug_client_hfi_read_mst_mode(struct dp_debug_client *client,
		char *buf, u32 size)
{
	struct dp_debug_client_hfi_priv *priv;
	struct dp_mgr_hfi_priv *mgr_priv;
	u32 mst_mode = 0;
	u32 mst_state = 0;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;
	mgr_priv = _get_mgr_hfi(priv);

	if (!mgr_priv) {
		DP_ERR("Could not access dp_mgr_hfi_priv\n");
		goto exit;
	}

	mst_mode = mgr_priv->mst_en;
	mst_state = mgr_priv->mst_st;

exit:
	return scnprintf(buf, size, "mst_mode = %u, mst_state = %u\n", mst_mode, mst_state);
}

/* Report the maximum pixel clock in kHz */
static int dp_debug_client_hfi_read_max_pclk_khz(struct dp_debug_client *client,
		char *buf, u32 size)
{
	if (!client || !buf)
		return -EINVAL;

	return scnprintf(buf, size, "max_pclk_khz = %d, org: %d\n",
			client->max_pclk_khz, client->max_pclk_khz);
}

/* Return the HDCP status string cached in the client */
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

/* Read HDR metadata for the given panel (0=SST, >0=MST connector by mst_con_id) */
static int dp_debug_client_hfi_read_hdr(struct dp_debug_client *client,
		char *buf, u32 size, int panel_id)
{
	struct drm_connector *connector = NULL;
	struct dp_debug_client_hfi_priv *priv;
	struct dp_drv *dp_drv;
	int len = 0;
	struct dp_mgr_hfi_priv *mgr_priv;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;
	mgr_priv = _get_mgr_hfi(priv);

	dp_drv = dp_debug_hfi_get_dp_drv(priv);
	if (!dp_drv || !dp_drv->client || !dp_drv->client->base_connector)
		return -ENODEV;

	/* panel_id == 0: SST base connector; > 0: MST connector identified by mst_con_id */
	if (panel_id == 0)
		connector = dp_drv->client->base_connector;
	else
		connector = priv->mst_conn;

	if (!connector) {
		DP_ERR("connector is NULL\n");
		return -EINVAL;
	}

	len = dp_debug_client_hfi_print_hdr_params_to_buf(connector, buf, size);
	if (len == -EOVERFLOW)
		DP_ERR("HDR buffer overflow\n");

	return len;
}

/* List display modes available on the base (SST) connector */
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
		ret = scnprintf(buf + len, max_size, "%s %d %d %d %d %d 0x%x\n",
			mode->name, drm_mode_vrefresh(mode), mode->picture_aspect_ratio,
			mode->htotal, mode->vtotal, mode->clock, mode->flags);
		if (dp_debug_client_hfi_check_buffer_overflow(ret, &max_size, &len))
			break;
	}
	mutex_unlock(&connector->dev->mode_config.mutex);

	return len;
}

/* List display modes for MST connectors. */
static int dp_debug_client_hfi_read_edid_modes_mst(struct dp_debug_client *client,
		char *buf, u32 size)
{
	struct dp_debug_client_hfi_priv *priv;
	struct dp_mgr_hfi_priv *mgr_priv;
	struct drm_connector *connector = NULL;
	struct drm_display_mode *mode;
	u32 len = 0, ret = 0, max_size = size;
	int i;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;

	mgr_priv = _get_mgr_hfi(priv);
	if (!mgr_priv)
		return -ENODEV;

	if (client->mst_con_id) {
		/* Specific connector requested via mst_con_id */
		connector = priv->mst_conn;
		if (!connector) {
			DP_ERR("MST connector %u not found for read_edid_modes_mst\n",
				client->mst_con_id);
			return 0;
		}

		mutex_lock(&connector->dev->mode_config.mutex);
		list_for_each_entry(mode, &connector->modes, head) {
			ret = scnprintf(buf + len, max_size, "%s %d %d %d %d %d 0x%x\n",
				mode->name, drm_mode_vrefresh(mode), mode->picture_aspect_ratio,
				mode->htotal, mode->vtotal, mode->clock, mode->flags);
			if (dp_debug_client_hfi_check_buffer_overflow(ret, &max_size, &len))
				break;
		}
		mutex_unlock(&connector->dev->mode_config.mutex);
	} else {
		/*
		 * mst_con_id not set: enumerate all MST stream connectors
		 * directly from hfi_priv->hfi[i]->connector (statically created
		 * in HFI mode, no reference counting needed).
		 */
		for (i = 0; i < mgr_priv->max_streams; i++) {
			if (!mgr_priv->hfi[i] || !mgr_priv->hfi[i]->connector)
				continue;

			connector = mgr_priv->hfi[i]->connector;

			ret = scnprintf(buf + len, max_size, "connector_id=%u:\n",
					connector->base.id);
			if (dp_debug_client_hfi_check_buffer_overflow(ret, &max_size, &len))
				break;

			mutex_lock(&connector->dev->mode_config.mutex);
			list_for_each_entry(mode, &connector->modes, head) {
				ret = scnprintf(buf + len, max_size, "%s %d %d %d %d %d 0x%x\n",
					mode->name, drm_mode_vrefresh(mode),
					mode->picture_aspect_ratio, mode->htotal, mode->vtotal,
					mode->clock, mode->flags);
				if (dp_debug_client_hfi_check_buffer_overflow(ret, &max_size, &len))
					break;
			}
			mutex_unlock(&connector->dev->mode_config.mutex);

			if (max_size <= 0)
				break;
		}
	}

	return len;
}

/* List all MST connectors belonging to this DP instance. */
static int dp_debug_client_hfi_read_mst_conn_info(struct dp_debug_client *client,
		char *buf, u32 size)
{
	struct dp_debug_client_hfi_priv *priv;
	struct dp_mgr_hfi_priv *mgr_priv;
	struct dp_drv *dp_drv;
	struct drm_connector *connector;
	u32 len = 0, ret = 0, max_size = size;
	int i;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;

	mgr_priv = _get_mgr_hfi(priv);
	if (!mgr_priv)
		return -ENODEV;

	dp_drv = dp_debug_hfi_get_dp_drv(priv);
	if (!dp_drv || !dp_drv->client || !dp_drv->client->base_connector)
		return -ENODEV;

	for (i = 0; i < mgr_priv->max_streams; i++) {
		connector = dp_drv->client->connectors[i];
		if (!connector)
			continue;
		ret = scnprintf(buf + len, max_size,
				"conn name:%s, conn id:%d state:%d\n",
				connector->name, connector->base.id,
				connector->status);
		if (dp_debug_client_hfi_check_buffer_overflow(ret, &max_size, &len))
			break;
	}

	return len;
}

/* -------------------------------------------------------------------------
 * Write operations
 * -------------------------------------------------------------------------
 */

static int dp_debug_client_hfi_write_edid(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	struct dp_debug_client_hfi_priv *priv;
	struct dp_mgr_hfi_priv *mgr_priv;
	struct hfi_client_t *hfi_client;
	struct hfi_shared_addr_map *edid_addr_map = NULL;
	struct hfi_buff edid_request;
	u8 *edid;
	size_t size, edid_buf_index = 0;
	u32 display_obj_id = 2;
	bool is_mst_mode = false;
	int rc = count;
	int i;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;

	mgr_priv = _get_mgr_hfi(priv);
	if (mgr_priv) {
		is_mst_mode = mgr_priv->client.is_mst_supported;
		DP_DEBUG("MST mode: %d, mst_edid_idx: %d, max_streams: %d\n", is_mst_mode,
				client->mst_edid_idx, mgr_priv->max_streams);
	}

	if (client->mst_edid_idx >= mgr_priv->max_streams) {
		DP_ERR("mst edid idx %d out of bounds for %d max streams\n", client->mst_edid_idx,
				mgr_priv->max_streams);
		rc = -EINVAL;
		goto bail;
	}

	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client) {
		rc = -ENODEV;
		goto bail;
	}

	for (i = 0; i < mgr_priv->max_streams; i++) {
		rc = _alloc_addr_map(hfi_client, &priv->edid_addr_map[i], SZ_1K);
		if (rc) {
			DP_ERR("failed to alloc edid_addr_map for stream %d\n", i);
			goto bail;
		}
	}

	edid_addr_map = priv->edid_addr_map[client->mst_edid_idx];

	size = min_t(size_t, count, SZ_1K);
	edid_buf_index = size / 2;  /* char_to_nib = 2 */
	edid = (u8 *)edid_addr_map->local_addr;

	rc = dp_debug_hfi_hex_to_bytes((u8 *)buf, edid, edid_buf_index);
	if (rc)
		goto bail;

	/*
	 * Determine display obj_id from the connector stored in hfi[stream_id].
	 * SST: hfi[0]->connector
	 * MST: hfi[mst_edid_idx]->connector
	 * The obj_id is set both in the HFI command header (via dp_debug_hfi_send_cmd)
	 * and in the payload display_id field so DCP can route the EDID correctly.
	 */
	if (mgr_priv) {
		struct drm_connector *edid_connector = NULL;
		u32 stream_id = is_mst_mode ? client->mst_edid_idx : 0;

		if (stream_id < DP_STREAMS_MAX && mgr_priv->hfi[stream_id])
			edid_connector = mgr_priv->hfi[stream_id]->connector;

		if (edid_connector) {
			display_obj_id = sde_conn_get_display_obj_id(edid_connector);
			DP_INFO("Writing EDID for %s (stream=%u, display_obj_id=%u, size=%zu)\n",
				is_mst_mode ? "MST" : "SST",
				stream_id, display_obj_id, edid_buf_index);
		} else {
			DP_WARN("No connector for stream %u, using default obj_id=%u\n",
				stream_id, display_obj_id);
		}
	}

	edid_request.size = edid_buf_index;
	edid_request.addr_l = HFI_VAL_L32(edid_addr_map->remote_addr);
	edid_request.addr_h = HFI_VAL_H32(edid_addr_map->remote_addr);

	rc = dp_debug_hfi_send_cmd(priv, hfi_client,
			HFI_COMMAND_DEBUG_DP_SET_EDID,
			HFI_PAYLOAD_TYPE_U32_ARRAY, &edid_request, sizeof(edid_request),
			HFI_HOST_FLAGS_RESPONSE_REQUIRED | HFI_HOST_FLAGS_NON_DISCARDABLE,
			display_obj_id);
	if (rc) {
		DP_ERR("Failed to send SET_EDID (display_obj_id=%u), rc=%d\n",
			display_obj_id, rc);
		goto bail;
	}
	rc = count;

bail:
	return rc;
}

/* Write DPCD register(s) to DCP; input is hex-encoded offset + data */
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
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE,
			dp_debug_hfi_get_base_obj_id(priv));
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
	mgr_priv = _get_mgr_hfi(priv);
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
				mgr_priv->hpd->pin_config = client->force_multi_func ?
						DPAM_HPD_F : DPAM_HPD_E;
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
			DP_ERR("HPD disconnect cb failed with rc=%d\n", rc);
		} else if (!wait_for_completion_timeout(&mgr_priv->hpd_comp, HZ)) {
			DP_ERR("wait for hpd disconnect processing timeout\n");
			rc = -ETIMEDOUT;
		}

		DP_INFO("Soft HPD Unplug completed with rc:%d\n", rc);
	}

	return rc ? rc : count;
}

/* Set a display mode override for SST; clears override if input is invalid */
static int dp_debug_client_hfi_write_edid_modes(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	struct dp_debug_client_hfi_priv *priv;
	struct dp_mgr_hfi_priv *mgr_priv;
	int hdisplay = 0, vdisplay = 0, vrefresh = 0, aspect_ratio = 0;
	struct dp_hfi *hfi = NULL;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;

	/* Get dp_mgr_hfi_priv */
	mgr_priv = _get_mgr_hfi(priv);
	if (!mgr_priv) {
		DP_ERR("Could not access dp_mgr_hfi_priv\n");
		return -ENODEV;
	}

	hfi = mgr_priv->hfi[DP_STREAM_0];

	/* Parse input */
	if (sscanf(buf, "%d %d %d %d", &hdisplay, &vdisplay, &vrefresh, &aspect_ratio) != 4)
		goto clear;

	if (!hdisplay || !vdisplay || !vrefresh)
		goto clear;

	/* Set mode override */
	hfi->mode_ovr.enabled = true;
	hfi->mode_ovr.h_active = hdisplay;
	hfi->mode_ovr.v_active = vdisplay;
	hfi->mode_ovr.refresh_rate = vrefresh;
	hfi->mode_ovr.aspect_ratio = aspect_ratio;

	DP_DEBUG("Set mode override: %dx%d@%dHz, aspect=%d\n", hdisplay, vdisplay, vrefresh,
			aspect_ratio);
	return count;

clear:
	DP_DEBUG("clearing debug modes\n");
	memset(&hfi->mode_ovr, 0, sizeof(hfi->mode_ovr));

	return count;
}

/* Set per-connector display mode overrides for MST topology */
static int dp_debug_client_hfi_write_edid_modes_mst(struct dp_debug_client *client,
		const char *buf)
{
	struct dp_debug_client_hfi_priv *priv;
	struct dp_drv *dp_drv;
	struct drm_connector *connector;
	struct sde_connector *sde_conn;
	int con_id = 0, offset = 0, debug_en = 0;
	int hdisplay, vdisplay, vrefresh, aspect_ratio;
	struct dp_hfi *hfi;
	struct dp_mgr_hfi_priv *mgr_priv;
	u32 stream_id;

	if (!client || !buf)
		return -EINVAL;

	priv = client->priv;

	dp_drv = dp_debug_hfi_get_dp_drv(priv);
	if (!dp_drv || !dp_drv->client || !dp_drv->client->base_connector)
		return -ENODEV;

	mgr_priv = _get_mgr_hfi(priv);
	if (!mgr_priv) {
		DP_ERR("Could not access dp_mgr_hfi_priv for MST override\n");
		return -ENODEV;
	}

	hfi = mgr_priv->hfi[DP_STREAM_0];

	while (sscanf(buf, "%d %d %d %d %d %d%n",
		      &debug_en, &con_id,
		      &hdisplay, &vdisplay, &vrefresh, &aspect_ratio,
		      &offset) == 6) {
		DP_DEBUG("MST EDID modes: debug_en=%d, con_id=%d, %dx%d@%dHz, aspect=%d\n",
			 debug_en, con_id, hdisplay, vdisplay, vrefresh, aspect_ratio);

		connector = _get_connector(mgr_priv, con_id);
		if (!connector) {
			DP_ERR("invalid connector id %d\n", con_id);
			buf += offset;
			continue;
		}

		/*
		 * In HFI mode panel_id == stream_id (set in dp_connector_post_init).
		 * Use it to address the correct per-stream hfi->mode_ovr, which is
		 * what dp_mgr_hfi_validate_mode() and dp_mgr_hfi_get_modes() read.
		 */
		sde_conn = to_sde_connector(connector);
		stream_id = sde_conn->panel_id;

		if (stream_id >= mgr_priv->max_streams || !hfi) {
			DP_ERR("Invalid stream_id %u for con_id=%d\n", stream_id, con_id);
			buf += offset;
			continue;
		}

		if (!debug_en || !hdisplay || !vdisplay || !vrefresh) {
			DP_DEBUG("clearing MST override (con_id=%d, stream_id=%u)\n",
				con_id, stream_id);
			memset(&hfi->mode_ovr, 0,
				sizeof(hfi->mode_ovr));
		} else {
			hfi->mode_ovr.enabled = true;
			hfi->mode_ovr.h_active = hdisplay;
			hfi->mode_ovr.v_active = vdisplay;
			hfi->mode_ovr.refresh_rate = vrefresh;
			hfi->mode_ovr.aspect_ratio = aspect_ratio;

			DP_DEBUG("MST override: %dx%d@%dHz aspect=%d (con_id=%d stream_id=%u)\n",
				 hdisplay, vdisplay, vrefresh, aspect_ratio, con_id, stream_id);
		}

		buf += offset;
	}

	return 0;
}

/* Configure MST sideband mode and stream count; sends HFI_COMMAND_DEBUG_DP_MST_CONFIG */
static int dp_debug_client_hfi_write_mst_sideband_mode(struct dp_debug_client *client,
		int mst_sideband_mode, u32 mst_port_cnt)
{
	struct dp_debug_client_hfi_priv *priv;
	struct dp_mgr_hfi_priv *mgr_priv;
	struct hfi_client_t *hfi_client;
	struct {
		u32 mst_enable;
		u32 num_streams;
	} mst_config;
	int rc;

	if (!client)
		return -EINVAL;

	priv = client->priv;

	DP_DEBUG("MST sideband mode: %d, port count: %u\n", mst_sideband_mode, mst_port_cnt);

	/* Reset MST EDID index */
	client->mst_edid_idx = 0;

	/* Synchronize kernel "mst_mode" reporting with DPSIM semantics: */
	mgr_priv = _get_mgr_hfi(priv);
	if (mgr_priv) {
		mgr_priv->client.is_mst_supported = !mst_sideband_mode;
		DP_INFO("HFI DPSIM: is_mst_supported=%d (mst_sideband_mode=%d)\n",
			mgr_priv->client.is_mst_supported, mst_sideband_mode);
	}

	/* Get HFI client */
	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client) {
		DP_ERR("HFI client not available for MST sideband mode\n");
		return -ENODEV;
	}

	/* Send MST configuration command to DCP with port count
	 * DCP will handle writing the DPCD DP_MSTM_CAP register (0x021) internally
	 */
	mst_config.mst_enable = !mst_sideband_mode;
	mst_config.num_streams = mst_port_cnt;

	rc = dp_debug_hfi_send_cmd(priv, hfi_client,
			HFI_COMMAND_DEBUG_DP_MST_CONFIG,
			HFI_PAYLOAD_TYPE_U32_ARRAY, &mst_config, sizeof(mst_config),
			HFI_HOST_FLAGS_RESPONSE_REQUIRED | HFI_HOST_FLAGS_NON_DISCARDABLE,
			dp_debug_hfi_get_base_obj_id(priv));
	if (rc) {
		DP_ERR("Failed to send HFI_COMMAND_DEBUG_DP_MST_CONFIG, rc=%d\n", rc);
		return rc;
	}

	if (mgr_priv) {
		mgr_priv->intf_info.stream_cnt = mst_port_cnt;
		DP_INFO("Set stream_cnt=%u for HPD event registration\n", mst_port_cnt);
	}

	DP_INFO("Sent MST config to DCP: mst_enable=%u, num_streams=%u (DCP will update DPCD)\n",
			mst_config.mst_enable, mst_config.num_streams);

	return 0;
}

/*
 * Set the active MST connector ID used by read_hdr / read_edid_modes_mst,
 * and also used to simulate a plug/unplug event on that connector.
 *
 * In HFI mode we validate that the requested connector is one of the streams in
 * client->connectors[0..mgr_priv->max_streams-1], store the ID, and — when a
 * definite status is requested — update hfi[stream_id]->connected so that
 * dp_mgr_hfi_hpd_detect() returns the new state, then fire a hotplug event
 * so that userspace re-queries the connector.
 *
 * dp_mgr_hfi_hpd_detect() returns hfi[stream_id]->connected, so updating
 * that field is the only way to make per-stream plug/unplug visible to the
 * DRM core without going through a full DCP firmware HPD cycle.
 */
static int dp_debug_client_hfi_write_mst_con_id(struct dp_debug_client *client,
		int con_id, int status)
{
	struct dp_debug_client_hfi_priv *priv;
	struct dp_mgr_hfi_priv *mgr_priv;
	struct drm_connector *connector;
	struct sde_connector *sde_conn;
	struct dp_hfi *hfi;
	u32 stream_id;
	int rc = 0;

	if (!client)
		return -EINVAL;

	if (!con_id) {
		DP_DEBUG("clearing mst_con_id\n");
		client->mst_con_id = 0;
		return 0;
	}

	priv = client->priv;
	mgr_priv = _get_mgr_hfi(priv);
	if (!mgr_priv)
		return -ENODEV;

	/* Look up the connector directly from HFI stream connectors */
	connector = _get_connector(mgr_priv, con_id);
	if (!connector) {
		DP_ERR("invalid connector id %u\n", con_id);
		return -EINVAL;
	}

	/*
	 * Store the DRM connector and ID for operations such as read_hdr,
	 * read_edid_modes_mst, etc., can be applied on MST displays.
	 * Note: mst_con_id is intentionally NOT used to derive the HFI
	 * display obj_id in write_edid — mst_edid_idx is used there instead.
	 */
	client->mst_con_id = con_id;
	priv->mst_conn = connector;

	if (status == connector_status_unknown) {
		DP_DEBUG("mst_con_id set to %d (status query only)\n", con_id);
		return 0;
	}

	if (status == connector_status_connected)
		DP_INFO("plug mst connector %d\n", con_id);
	else if (status == connector_status_disconnected)
		DP_INFO("unplug mst connector %d\n", con_id);

	/*
	 * In HFI mode panel_id == stream_id (set in dp_connector_post_init),
	 * so sde_conn->panel_id gives us the correct hfi[] index.
	 */
	sde_conn = to_sde_connector(connector);
	stream_id = sde_conn->panel_id;
	hfi = mgr_priv->hfi[stream_id];

	/*
	 * If connecting and the HFI infrastructure was fully torn down
	 * (configured=false, which happens when active_streams reaches 0
	 * during dp_mgr_hfi_unprepare/hpd_cleanup after the last stream
	 * disconnects), we must re-initialize via the HPD configure callback
	 * before the DRM core tries to enable the stream.
	 *
	 * DCP responds asynchronously with an EDID info event, which
	 * dp_mgr_hfi_handle_dp_info() handles by setting hfi->connected=true
	 * and firing a uevent to notify the DRM core.  We therefore return
	 * here without setting hfi->connected or firing drm_kms_helper_hotplug_event
	 * ourselves — DCP drives the rest of the connect sequence.
	 */
	if (status == connector_status_connected && mgr_priv->active_streams < 2)
		dp_debug_client_hfi_write_mst_sideband_mode(client, 0,
				mgr_priv->active_streams + 1);
	else if (status == connector_status_disconnected && mgr_priv->active_streams > 0)
		dp_debug_client_hfi_write_mst_sideband_mode(client, 0,
				mgr_priv->active_streams - 1);
	if (status == connector_status_connected && mgr_priv && !mgr_priv->configured) {
		DP_INFO("HFI not configured (teardown), re-init via HPD configure for stream %u\n",
				stream_id);
		mgr_priv->soft_unplug = false;
		/*
		 * hfi_priv->connected is still true from the original HPD connect —
		 * dp_mgr_hfi_hpd_cleanup() resets configured but NOT connected.
		 * dp_mgr_hfi_hpd_configure_cb() only sends the plug when
		 * hpd_high && !connected, so we must clear connected here.
		 */
		mgr_priv->connected = false;
		if (mgr_priv->hpd) {
			/*
			 * Restore simulation pin/orientation values so DCP
			 * accepts the plug (same logic as write_hpd connect).
			 */
			if (client->sim_enable)
				mgr_priv->hpd->pin_config = 5;
			if (mgr_priv->hpd->orientation == ORIENTATION_NONE)
				mgr_priv->hpd->orientation = ORIENTATION_CC1;
			mgr_priv->hpd->hpd_high = true;
			mgr_priv->hpd->hpd_irq = false;
		}
		rc = dp_mgr_hfi_hpd_configure_cb(mgr_priv);
		if (rc) {
			DP_ERR("HPD configure failed for stream %u reconnect, rc=%d\n",
					stream_id, rc);
		}
		return rc;
	}

	/*
	 * When disconnecting a physical display, set soft_unplug before updating
	 * the connected flag so that any attention callbacks that fire during
	 * or after the DRM disable sequence are suppressed. The physical DP link
	 * remains active. When reconnecting, clear soft_unplug first so that the
	 * attention callback and EDID info handler are re-enabled before the
	 * hotplug event is fired.
	 */
	if (status == connector_status_disconnected) {
		/*
		 * Send an HPD IRQ to DCP before marking the stream as
		 * soft-unplugged.  The attention callback is gated on
		 * soft_unplug==false, so the IRQ must be dispatched first.
		 * This notifies DCP of the MST topology change (monitor
		 * removed from port) while the physical DP link stays up.
		 */
		if (mgr_priv->hpd) {
			if (mgr_priv->active_streams > 1 && mgr_priv->hpd_cb.attention) {
				mgr_priv->hpd->hpd_high = true;
				mgr_priv->hpd->hpd_irq = true;
				DP_INFO("Sending HPD IRQ for MST unplug (con_id=%d)\n", con_id);
				mgr_priv->hpd_cb.attention(mgr_priv);
				mgr_priv->hpd->hpd_irq = false;
			} else  if (mgr_priv->hpd_cb.disconnect) {
				mgr_priv->hpd->hpd_high = false;
				mgr_priv->hpd->hpd_irq = false;
				mgr_priv->hpd_cb.disconnect(mgr_priv);
			}
		}
		mgr_priv->soft_unplug = true;
		DP_DEBUG("Set soft_unplug=true for con_id=%d\n", con_id);
	} else if (status == connector_status_connected) {
		mgr_priv->soft_unplug = false;
		DP_DEBUG("Cleared soft_unplug for con_id=%d\n", con_id);
	}

	/*
	 * HFI is configured (other streams still active, or disconnecting):
	 * update the per-stream connected flag and fire a hotplug event so
	 * the DRM core re-queries the connector via dp_mgr_hfi_hpd_detect().
	 */
	if (mgr_priv && stream_id < mgr_priv->max_streams && hfi) {
		hfi->connected = (status == connector_status_connected);
		DP_DEBUG("Set hfi[%u]->connected = %d for con_id=%d\n", stream_id, hfi->connected,
				con_id);
	} else {
		DP_WARN("Could not update hfi[%u]->connected (mgr_priv=%p)\n", stream_id, mgr_priv);
	}

	/*
	 * Fire a hotplug event so userspace re-queries the connector.
	 * hpd_detect() will now return the updated hfi->connected state.
	 */
	drm_kms_helper_hotplug_event(connector->dev);

	return 0;
}

/* Send a bandwidth code override to DCP */
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
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE,
			dp_debug_hfi_get_base_obj_id(priv));
	if (rc) {
		DP_ERR("Failed to send HFI_COMMAND_DEBUG_DP_SET_BW_CODE, rc=%d\n", rc);
		return rc;
	}

	return count;
}

/* Enable or disable MST mode in dp_mgr_hfi_priv parser config */
static int dp_debug_client_hfi_write_mst_mode(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	struct dp_debug_client_hfi_priv *priv;
	struct dp_mgr_hfi_priv *mgr_priv;
	u32 mst_mode = 0;

	if (!client || !buf)
		return -EINVAL;

	if (kstrtoint(buf, 10, &mst_mode) != 0)
		return -EINVAL;

	priv = client->priv;

	/* Get dp_mgr_hfi_priv to configure MST mode */
	mgr_priv = _get_mgr_hfi(priv);
	if (!mgr_priv) {
		DP_ERR("Could not access dp_mgr_hfi_priv for MST mode configuration\n");
		return -ENODEV;
	}

	dp_mgr_hfi_set_mst_mode(mgr_priv, mst_mode ? true : false);
	DP_DEBUG("mst_enable: %d\n", mst_mode);

	return count;
}

/* Stub: max pixel clock is managed by DCP firmware in HFI mode */
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

/* Set the test pattern generator (TPG) pattern on DCP */
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
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE,
			dp_debug_hfi_get_base_obj_id(priv));
	if (rc)
		DP_ERR("Failed to send HFI_COMMAND_DEBUG_DP_SET_TPG, rc=%d\n", rc);

	client->tpg_pattern = tpg_pattern;

	return rc;
}

/* Stub: execution mode selection is not applicable in HFI mode */
static int dp_debug_client_hfi_write_exe_mode(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	return count;
}

/* Enable or disable HDCP on DCP via HFI_COMMAND_DEBUG_DP_HDCP_CONTROL */
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
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE,
			dp_debug_hfi_get_base_obj_id(priv));
	if (rc) {
		DP_ERR("Failed to send HFI_COMMAND_DEBUG_DP_HDCP_CONTROL, rc=%d\n", rc);
		return rc;
	}

	return count;
}

/* Toggle simulation mode on/off by delegating to write_sim_mode */
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

/* Trigger a simulated attention event with the given VDO value */
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

/* Stub: register dump trigger (rejects qfprom_physical, accepts others) */
static int dp_debug_client_hfi_write_dump(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	if (!client || !buf)
		return -EINVAL;

	if (!strcmp(buf, "qfprom_physical"))
		return -EINVAL;

	return count;
}

/* Enable/disable DP simulation mode; sends HFI_COMMAND_DEBUG_DP_SIMULATION_CONTROL */
static int dp_debug_client_hfi_write_sim_mode(struct dp_debug_client *client, bool sim)
{
	struct dp_debug_client_hfi_priv *priv;
	struct hfi_client_t *hfi_client;
	u32 sim_enable = sim ? 1 : 0;
	int rc;
	struct dp_hfi *hfi;
	struct dp_mgr_hfi_priv *mgr_priv;

	if (!client)
		return -EINVAL;

	priv = client->priv;
	client->sim_enable = sim;

	DP_INFO("Simulation mode %s\n", sim ? "[ON]" : "[OFF]");

	/* Get HFI client */
	hfi_client = dp_debug_hfi_get_client(priv);
	if (!hfi_client)
		return -ENODEV;

	/* Get dp_mgr_hfi_priv */
	mgr_priv = _get_mgr_hfi(priv);
	if (!mgr_priv) {
		DP_ERR("Could not access dp_mgr_hfi_priv\n");
		return -ENODEV;
	}

	hfi = mgr_priv->hfi[DP_STREAM_0];
	/* clear mode override */
	hfi->mode_ovr.enabled = false;

	/* Cleanup state when disabling simulation */
	if (!sim) {
		if (client->hotplug) {
			DP_WARN("sim mode off before hotplug disconnect\n");
			client->hotplug = false;
		}
		client->mst_edid_idx = 0;
	}

	rc = dp_debug_hfi_send_cmd(priv, hfi_client,
			HFI_COMMAND_DEBUG_DP_SIMULATION_CONTROL,
			HFI_PAYLOAD_TYPE_U32_ARRAY, &sim_enable, sizeof(sim_enable),
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE,
			dp_debug_hfi_get_base_obj_id(priv));
	if (rc)
		DP_ERR("Failed to send HFI_COMMAND_DEBUG_DP_SIMULATION_CONTROL, rc=%d\n", rc);

	return rc;
}

/* Send a simulated attention event to DCP via HFI_COMMAND_DEBUG_DP_SET_ATTENTION */
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
			HFI_HOST_FLAGS_RESPONSE_REQUIRED|HFI_HOST_FLAGS_NON_DISCARDABLE,
			dp_debug_hfi_get_base_obj_id(priv));
	if (rc)
		DP_ERR("Failed to send HFI_COMMAND_DEBUG_DP_SET_ATTENTION, rc=%d\n", rc);

	return rc;
}

/* Abort simulation: force hotplug low and disable sim mode */
static void dp_debug_client_hfi_abort(struct dp_debug_client *client)
{
	if (!client)
		return;

	client->hotplug = false;
	if (client->write_sim_mode)
		client->write_sim_mode(client, false);
}

/*
 * dp_debug_client_hfi_get - Initialise the HFI debug client.
 *
 * Allocates priv state, wires up all function pointers, and defers HFI
 * client creation until the first debugfs operation.
 */
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
	client->read_max_lclk_khz = dp_debug_client_hfi_read_max_lclk_khz;
	client->read_tpg = dp_debug_client_hfi_read_tpg;
	client->read_dump = dp_debug_client_hfi_read_dump;
	client->read_max_pclk_khz = dp_debug_client_hfi_read_max_pclk_khz;
	client->read_hdr = dp_debug_client_hfi_read_hdr;
	client->read_edid_modes = dp_debug_client_hfi_read_edid_modes;
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
	client->write_dump = dp_debug_client_hfi_write_dump;

	/* MST Functions */
	client->read_mst_mode = dp_debug_client_hfi_read_mst_mode;
	client->read_edid_modes_mst = dp_debug_client_hfi_read_edid_modes_mst;
	client->read_mst_conn_info = dp_debug_client_hfi_read_mst_conn_info;
	client->write_mst_mode = dp_debug_client_hfi_write_mst_mode;
	client->write_edid_modes_mst = dp_debug_client_hfi_write_edid_modes_mst;
	client->write_mst_con_id = dp_debug_client_hfi_write_mst_con_id;
	client->write_mst_con_add = NULL;
	client->write_mst_con_remove = NULL;
	client->write_mst_sideband_mode = dp_debug_client_hfi_write_mst_sideband_mode;

	/* Simulation Functions */
	client->write_sim = dp_debug_client_hfi_write_sim;
	client->write_attention = dp_debug_client_hfi_write_attention;
	client->simulate_attention = dp_debug_client_hfi_simulate_attention;

	client->write_mmrm_clk_cb = NULL;
	/* Stub - not applicable for HFI */

	client->abort = dp_debug_client_hfi_abort;

	client->priv = priv;

	DP_INFO("DP HFI debug client initialized successfully\n");
	return 0;
}

/*
 * dp_debug_client_hfi_put - Tear down the HFI debug client.
 *
 * Unregisters all HFI listeners, releases the borrowed HFI client reference,
 * and frees the priv structure.
 */
void dp_debug_client_hfi_put(struct dp_debug_client *client)
{
	struct dp_debug_client_hfi_priv *priv;
	struct hfi_client_t *hfi_client;
	struct listener_list *listener_entry, *tmp;
	int stream_index;

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

		if (priv->dpcd_addr_map)
			dp_mgr_hfi_deinit_shared_addr(priv->hfi_client, priv->dpcd_addr_map);

		for (stream_index = 0; stream_index < MAX_DP_MST_STREAMS; stream_index++)
			if (priv->edid_addr_map[stream_index])
				dp_mgr_hfi_deinit_shared_addr(priv->hfi_client,
						priv->edid_addr_map[stream_index]);

		if (priv->info_addr_map)
			dp_mgr_hfi_deinit_shared_addr(priv->hfi_client, priv->info_addr_map);
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
