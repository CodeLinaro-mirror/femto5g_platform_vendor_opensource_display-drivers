// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/slab.h>
#include <linux/version.h>
#if (KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE)
#include <drm/display/drm_dp_helper.h>
#else
#include <drm/drm_dp_helper.h>
#endif

#include "dp_aux_hfi.h"
#include "dp_mgr_hfi.h"          /* dp_mgr_hfi_init_shared_addr / deinit */
#include "dp_hfi.h"              /* dp_hfi_send_cmd_buf */
#include "hfi_commands_display.h" /* HFI_COMMAND_DISPLAY_AUX_READ */
#include "hfi_defs_debug.h"      /* hfi_dp_aux_request, hfi_dp_aux_data */
#include "hfi_kms.h"
#include "hfi_utils.h"           /* HFI_VAL_L32 / HFI_VAL_H32 */
#include "hfi_props.h"           /* HFI_PAYLOAD_TYPE_U32_ARRAY */
#include "dp_debug.h"

/* Size of the shared-memory window DCP writes DPCD data into */
#define DP_AUX_HFI_DPCD_BUF_SIZE    SZ_1K

#define DP_AUX_OPERATION_MASK       0x1
#define DP_AUX_MODE_MASK            0x8

/**
 * struct dp_aux_hfi_private - HFI AUX channel private state
 *
 * Self-contained module; no dependency on the HWIO dp_aux / dp_catalog_aux.
 *
 * @drm_aux:       embedded drm_dp_aux registered with the DRM core.
 *                 container_of(drm_aux, ..., drm_aux) recovers priv from
 *                 the transfer callback.
 * @dev:           device pointer (used for drm_dp_aux.dev)
 * @hfi_client:    HFI client used to send commands to DCP
 * @dpcd_addr_map: shared-memory buffer DCP writes DPCD data into
 * @registered:    true once drm_dp_aux_register() has succeeded
 */
struct dp_aux_hfi_private {
	struct drm_dp_aux           drm_aux;   /* MUST be first */
	struct device              *dev;
	struct hfi_client_t        *hfi_client;
	struct hfi_shared_addr_map *dpcd_addr_map;
	bool                        registered;
};

/**
 * _dp_aux_hfi_get_mgr_priv() - Helper: walk dev -> platform_device -> dp_drv -> dp_mgr_hfi_priv.
 * Same pattern as _get_mgr_hfi() in dp_debug_client_hfi.c.
 *
 * @priv: HFI AUX private state
 * @return Pointer to dp_mgr_hfi_priv on success, or NULL on failure.
 */
static struct dp_mgr_hfi_priv *_dp_aux_hfi_get_mgr_priv(
		struct dp_aux_hfi_private *priv)
{
	struct platform_device *pdev;
	struct dp_drv *dp_drv;

	if (!priv || !priv->dev)
		return NULL;

	pdev = to_platform_device(priv->dev);
	dp_drv = platform_get_drvdata(pdev);

	if (!dp_drv || !dp_drv->client)
		return NULL;

	/* dp_drv->client is embedded inside dp_mgr_hfi_priv */
	return container_of(dp_drv->client, struct dp_mgr_hfi_priv, client);
}

/**
 * _dp_aux_hfi_write() - Build the HFI write payload from @msg, sends it to DCP blocking.
 *
 * @msg: DP AUX message to be written
 * @flags: AUX transfer flags derived from msg->request (mode in first byte)
 * @hfi_client: HFI client for communication
 * @hfi: DP HFI instance
 * @return msg->size on success, or a negative errno.
 */
static ssize_t _dp_aux_hfi_write(struct drm_dp_aux_msg *msg, u32 flags,
		struct hfi_client_t *hfi_client, struct dp_hfi *hfi)
{
	struct hfi_dp_aux_data aux_write = {0};
	int rc;

	if (msg->size > sizeof(aux_write.data)) {
		DP_ERR("HFI AUX: write size %zu exceeds max %zu\n",
				msg->size, sizeof(aux_write.data));
		return -EINVAL;
	}

	aux_write.flags = flags;
	aux_write.offset = msg->address;
	aux_write.size = msg->size;
	memcpy(aux_write.data, msg->buffer, msg->size);

	rc = dp_hfi_send_cmd_buf(hfi, hfi_client, HFI_COMMAND_DISPLAY_AUX_WRITE,
			"DisplayPort", HFI_PAYLOAD_TYPE_U32_ARRAY, &aux_write,
			sizeof(aux_write.flags) + sizeof(aux_write.offset) +
			sizeof(aux_write.size) + aux_write.size,
			HFI_HOST_FLAGS_RESPONSE_REQUIRED | HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc) {
		DP_ERR("Failed to send hfi_cmd 0x%x, rc=%d\n",
				HFI_COMMAND_DISPLAY_AUX_WRITE, rc);
		return rc;
	}

	msg->reply = DP_AUX_NATIVE_REPLY_ACK;
	DP_INFO("HFI AUX: wrote %zu bytes to DPCD offset 0x%x\n", msg->size, msg->address);

	return msg->size;
}

/**
 * _dp_aux_hfi_read() - Build the HFI read request, sends it to DCP blocking.
 *
 * @priv: HFI AUX private state
 * @msg: DP AUX message to be read
 * @flags: AUX transfer flags derived from msg->request (mode in first byte)
 * @hfi_client: HFI client for communication
 * @hfi: DP HFI instance
 * @return msg->size on success, or a negative errno.
 */
static ssize_t _dp_aux_hfi_read(struct dp_aux_hfi_private *priv, struct drm_dp_aux_msg *msg,
		u32 flags, struct hfi_client_t *hfi_client, struct dp_hfi *hfi)
{
	struct hfi_dp_aux_request aux_req = {0};
	int rc;

	/* Lazily allocate the shared DPCD read buffer */
	if (!priv->dpcd_addr_map) {
		priv->dpcd_addr_map = dp_mgr_hfi_init_shared_addr(hfi_client,
				DP_AUX_HFI_DPCD_BUF_SIZE);
		if (!priv->dpcd_addr_map) {
			DP_ERR("HFI AUX: failed to allocate DPCD shared buffer\n");
			return -ENOMEM;
		}
	}

	aux_req.flags = flags;
	aux_req.buffer.addr_l = HFI_VAL_L32(priv->dpcd_addr_map->remote_addr);
	aux_req.buffer.addr_h = HFI_VAL_H32(priv->dpcd_addr_map->remote_addr);
	aux_req.buffer.size = priv->dpcd_addr_map->size;
	aux_req.offset = msg->address;
	aux_req.bytes = msg->size;

	rc = dp_hfi_send_cmd_buf(hfi, hfi_client, HFI_COMMAND_DISPLAY_AUX_READ,
			"DisplayPort", HFI_PAYLOAD_TYPE_U32_ARRAY, &aux_req, sizeof(aux_req),
			HFI_HOST_FLAGS_RESPONSE_REQUIRED | HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (rc) {
		DP_ERR("Failed to send hfi_cmd 0x%x, rc=%d\n", HFI_COMMAND_DISPLAY_AUX_READ, rc);
		return rc;
	}

	memcpy(msg->buffer, priv->dpcd_addr_map->local_addr,
			min_t(u32, msg->size, priv->dpcd_addr_map->size));
	msg->reply = DP_AUX_NATIVE_REPLY_ACK;

	DP_INFO("HFI AUX: read %zu bytes from DPCD offset 0x%x\n", msg->size, msg->address);

	return msg->size;
}

/**
 * dp_aux_hfi_transfer() - drm_dp_aux transfer callback for HFI-based AUX channel.
 *
 * Same signature as the HWIO dp_aux_transfer, Decodes msg->request to determine the
 * AUX transfer mode and direction using two independent bit fields defined in drm_dp.h:
 *
 *  Bit 3 (DP_AUX_MODE_MASK = 0x8) - AUX transfer mode:
 *    Set to 1     -> Native AUX (direct DPCD access): DP_AUX_NATIVE
 *    Cleared to 0 -> I2C-over-AUX (EDID access):      DP_AUX_I2C
 *
 *  Bit 0 (DP_AUX_OPERATION_MASK = 0x1) - Transaction direction:
 *    Set to 1     -> Read  (DP_AUX_NATIVE_READ = 0x9,  DP_AUX_I2C_READ  = 0x1)
 *    Cleared to 0 -> Write (DP_AUX_NATIVE_WRITE = 0x8, DP_AUX_I2C_WRITE = 0x0)
 *
 * The mode is decoded by masking bit 3 with DP_AUX_MODE_MASK and mapped to
 * bit 0 of hfi_dp_aux_(request|data)->flags.
 *
 * The direction is decoded by masking bit 0 with DP_AUX_OPERATION_MASK in the switch
 * statement, routing to _dp_aux_hfi_write() or _dp_aux_hfi_read() for both
 * native and I2C-over-AUX transfers.
 */
static ssize_t dp_aux_hfi_transfer(struct drm_dp_aux *drm_aux, struct drm_dp_aux_msg *msg)
{
	struct dp_aux_hfi_private *priv =
		container_of(drm_aux, struct dp_aux_hfi_private, drm_aux);
	struct dp_mgr_hfi_priv *mgr_priv;
	struct hfi_client_t *hfi_client;
	struct dp_hfi *hfi;
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;
	u32 mode, flags = 0, ret = 0;

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, msg->request, msg->address, msg->size);

	if (!msg->buffer || !msg->size)
		return msg->size;

	DP_DEBUG("aux transfer: request=0x%x offset=%d size=%zu\n",
		msg->request, msg->address, msg->size);

	/* Refuse to talk to DCP if DP is not plugged in */
	mgr_priv = _dp_aux_hfi_get_mgr_priv(priv);
	if (!mgr_priv || !mgr_priv->connected) {
		DP_DEBUG("HFI AUX: DP not connected, skipping DPCD access\n");
		return -ENODEV;
	}

	sde_kms = sde_connector_get_kms(mgr_priv->client.connectors[0]);
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
	hfi = mgr_priv->hfi[0];

	mode = msg->request & DP_AUX_MODE_MASK;

	switch (msg->request & DP_AUX_OPERATION_MASK) {
	case DP_AUX_I2C_WRITE:
		flags |= mode ? 0 /* Native transaction*/ : HFI_DP_AUX_FLAG_I2C;
		ret = _dp_aux_hfi_write(msg, flags, hfi_client, hfi);
		break;
	case DP_AUX_I2C_READ:
		flags |= mode ? 0 /* Native transaction*/ : HFI_DP_AUX_FLAG_I2C;
		ret = _dp_aux_hfi_read(priv, msg, flags, hfi_client, hfi);
		break;
	default:
		DP_DEBUG("HFI AUX: non-native request 0x%x - not supported\n", msg->request);
		ret = -ENODEV;
		break;
	}

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, msg->request, msg->address, msg->size, ret);
	return ret;
}

/**
 * --------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------
 */

struct dp_aux_hfi *dp_aux_hfi_get(struct device *dev,
		struct hfi_client_t *hfi_client, void *ipc_log_context)
{
	struct dp_aux_hfi_private *priv;

	if (!dev || !hfi_client) {
		DP_ERR("HFI AUX: invalid arguments\n");
		return ERR_PTR(-EINVAL);
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->dev = dev;
	priv->hfi_client = hfi_client;

	priv->drm_aux.transfer = dp_aux_hfi_transfer;

	DP_INFO("HFI AUX: channel created\n");
	return (struct dp_aux_hfi *)priv;
}

void dp_aux_hfi_put(struct dp_aux_hfi *aux_hfi)
{
	struct dp_aux_hfi_private *priv = (struct dp_aux_hfi_private *)aux_hfi;

	if (!priv)
		return;

	if (priv->registered) {
		drm_dp_aux_unregister(&priv->drm_aux);
		priv->registered = false;
	}

	if (priv->dpcd_addr_map)
		dp_mgr_hfi_deinit_shared_addr(priv->hfi_client, priv->dpcd_addr_map);

	kfree(priv);
}

int dp_aux_hfi_register(struct dp_aux_hfi *aux_hfi, struct drm_device *drm_dev)
{
	struct dp_aux_hfi_private *priv = (struct dp_aux_hfi_private *)aux_hfi;
	int ret;

	if (!priv)
		return -EINVAL;

	priv->drm_aux.name = "sde_dp_aux";
	priv->drm_aux.dev  = priv->dev;
#if (KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE)
	priv->drm_aux.drm_dev = drm_dev;
#endif

	ret = drm_dp_aux_register(&priv->drm_aux);
	if (ret) {
		DP_ERR("HFI AUX: drm_dp_aux_register failed: %d\n", ret);
		return ret;
	}

	priv->registered = true;
	DP_DEBUG("HFI AUX: drm_dp_aux registered\n");
	return 0;
}

void dp_aux_hfi_deregister(struct dp_aux_hfi *aux_hfi)
{
	struct dp_aux_hfi_private *priv = (struct dp_aux_hfi_private *)aux_hfi;

	if (!priv || !priv->registered)
		return;

	drm_dp_aux_unregister(&priv->drm_aux);
	priv->registered = false;
	DP_DEBUG("HFI AUX: drm_dp_aux deregistered\n");
}

struct drm_dp_aux *dp_aux_hfi_get_drm_aux(struct dp_aux_hfi *aux_hfi)
{
	struct dp_aux_hfi_private *priv = (struct dp_aux_hfi_private *)aux_hfi;

	if (!priv)
		return NULL;

	return &priv->drm_aux;
}

int dp_aux_hfi_process_response(struct dp_aux_hfi *aux_hfi,
		void *payload, u32 payload_size)
{
	/*
	 * Stub – called by dp_mgr_hfi when HFI_COMMAND_DP_AUX_READ_RESPONSE
	 * arrives.  Will be used once the async AUX path is wired up.
	 */
	return 0;
}
