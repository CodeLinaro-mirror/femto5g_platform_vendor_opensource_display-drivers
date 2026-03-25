// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/io.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/iopoll.h>
#include <linux/version.h>
#include <linux/msm_hdcp.h>
#include <drm/display/drm_dp_helper.h>
#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
#include <linux/hdcp_qseecom.h>
#endif
#include "sde_hdcp.h"
#include "hdcp/msm_hdmi_hdcp_mgr.h"
#include "dp_hdcp.h"
#include "dp_debug.h"
#include "sde_hdcp_2x.h"

/**
 * struct dp_hdcp1x_ctx - DP HDCP 1.x context
 * @init_data: HDCP initialization data from sde_hdcp
 * @hdcp1_handle: TrustZone HDCP handle from hdcp1_init()
 * @dcp_state: Current HDCP state as known by DCP
 * @tz_state: Current HDCP state as known by TrustZone
 * @topology: Topology information for TrustZone
 */
struct dp_hdcp1x_ctx {
	struct sde_hdcp_init_data init_data;
	void *hdcp1_handle;
	enum sde_hdcp_state dcp_state;
	enum sde_hdcp_state tz_state;
#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	struct hdcp1_topology topology;
#endif
};

/**
 * struct dp_hdcp2x_ctx - DP HDCP 2.x context
 * @init_data: HDCP initialization data from sde_hdcp
 * @hdcp2_handle: TrustZone HDCP 2.x handle from hdcp2_init()
 * @state: Current HDCP 2.x state
 * @app_data: HDCP 2.x application data for TrustZone communication
 * @response_buf: Buffer for response messages from TrustZone
 * @buf_len: Length of response buffer
 *
 * Note: request buffer is managed internally by hdcp2_handle and accessed
 * via app_data.request.data pointer which is synchronized by hdcp2_update_app_data()
 */
struct dp_hdcp2x_ctx {
	struct sde_hdcp_init_data init_data;
	void *hdcp2_handle;
	enum sde_hdcp_state state;
#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	struct hdcp2_app_data app_data;
#endif
	uint8_t *response_buf;
	uint32_t buf_len;
};

/**
 * dp_hdcp1x_start() - Start HDCP authentication and get AKSV from TrustZone
 * @input: HDCP context handle
 * @aksv_msb: Pointer to store upper 8 bits of AKSV
 * @aksv_lsb: Pointer to store lower 32 bits of AKSV
 *
 * Called when DCP sends HDCP1X_START event. Gets AKSV from TrustZone
 * to be sent back to DCP for authentication.
 *
 * Return: 0 on success, negative error code on failure
 */
int dp_hdcp1x_start(void *input, u32 *aksv_msb, u32 *aksv_lsb)
{
	struct dp_hdcp1x_ctx *ctx = input;
	int rc;

	if (!ctx || !aksv_msb || !aksv_lsb) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	DP_DEBUG("Starting HDCP authentication\n");

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	/* Get AKSV from TrustZone */
	rc = hdcp1_start(ctx->hdcp1_handle, aksv_msb, aksv_lsb);
	if (rc) {
		DP_ERR("hdcp1_start failed: %d\n", rc);
		ctx->tz_state = HDCP_STATE_AUTH_FAIL;
		return rc;
	}
#endif

	/* Update states */
	ctx->dcp_state = HDCP_STATE_AUTHENTICATING;
	ctx->tz_state = HDCP_STATE_AUTHENTICATING;

	DP_DEBUG("HDCP started successfully, AKSV: msb=0x%x, lsb=0x%x\n",
		 *aksv_msb, *aksv_lsb);

	return 0;
}

/**
 * dp_hdcp1x_set_enc() - Control HDCP encryption
 * @input: HDCP context handle
 * @enable: true to enable encryption, false to disable
 *
 * Called when DCP sends HDCP1X_ENC event. Notifies TrustZone
 * to control encryption state.
 */
void dp_hdcp1x_set_enc(void *input, bool enable)
{
	struct dp_hdcp1x_ctx *ctx = input;
	int rc;

	if (!ctx) {
		DP_ERR("invalid input\n");
		return;
	}

	DP_DEBUG("Setting HDCP encryption: %s\n", enable ? "enabled" : "disabled");

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	rc = hdcp1_set_enc(ctx->hdcp1_handle, enable);
	if (rc)
		DP_ERR("hdcp1_set_enc failed: %d\n", rc);
	else
		DP_DEBUG("HDCP encryption %s\n", enable ? "enabled" : "disabled");
#endif
}

/**
 * dp_hdcp1x_topology_update() - Update HDCP repeater topology
 * @input: HDCP context handle
 * @depth: Repeater cascade depth
 * @device_count: Number of downstream devices
 * @max_devices_exceeded: Max device count exceeded flag
 * @max_cascade_exceeded: Max cascade depth exceeded flag
 *
 * Called when DCP sends HDCP1X_TOPOLOGY_UPDATE event. Notifies TrustZone
 * with topology information for repeater authentication.
 */
void dp_hdcp1x_topology_update(void *input, u32 depth, u32 device_count,
			     u32 max_devices_exceeded, u32 max_cascade_exceeded)
{
	struct dp_hdcp1x_ctx *ctx = input;
	int rc;

	if (!ctx) {
		DP_ERR("invalid input\n");
		return;
	}

	DP_DEBUG("Updating HDCP topology: depth=%u, devices=%u, max_dev=%u, max_cascade=%u\n",
		 depth, device_count, max_devices_exceeded, max_cascade_exceeded);

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	/* Update topology structure */
	ctx->topology.depth = depth;
	ctx->topology.device_count = device_count;
	ctx->topology.max_devices_exceeded = max_devices_exceeded;
	ctx->topology.max_cascade_exceeded = max_cascade_exceeded;
	ctx->topology.hdcp2LegacyDeviceDownstream = 0;
	ctx->topology.hdcp1DeviceDownstream = 0;

	/* Notify TrustZone with topology information */
	rc = hdcp1_ops_notify(ctx->hdcp1_handle, &ctx->topology, true);
	if (rc)
		DP_ERR("hdcp1_ops_notify failed: %d\n", rc);
	else
		DP_DEBUG("HDCP topology updated\n");
#endif
}

/**
 * dp_hdcp1x_stop() - Stop HDCP authentication
 * @input: HDCP context handle
 *
 * Called when DCP sends HDCP1X_STOP event. Notifies TrustZone
 * to stop HDCP authentication and disable encryption.
 */
void dp_hdcp1x_stop(void *input)
{
	struct dp_hdcp1x_ctx *ctx = input;

	if (!ctx) {
		DP_ERR("invalid input\n");
		return;
	}

	DP_DEBUG("Stopping HDCP authentication\n");

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	/* Notify TrustZone that authentication stopped */
	hdcp1_ops_notify(ctx->hdcp1_handle, &ctx->topology, false);

	/* Notify TrustZone */
	hdcp1_stop(ctx->hdcp1_handle);
#endif

	/* Update states */
	ctx->dcp_state = HDCP_STATE_INACTIVE;
	ctx->tz_state = HDCP_STATE_INACTIVE;

	DP_DEBUG("HDCP stopped\n");
}

/**
 * dp_hdcp1x_feature_supported() - Check if HDCP is supported
 * @input: HDCP context handle
 *
 * Return: true if HDCP 1.x is supported, false otherwise
 */
bool dp_hdcp1x_feature_supported(void *input)
{
	struct dp_hdcp1x_ctx *ctx = input;
	bool supported = false;

	if (!ctx) {
		DP_ERR("invalid input\n");
		return false;
	}

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	supported = hdcp1_feature_supported(ctx->hdcp1_handle);
#endif

	return supported;
}

/**
 * dp_hdcp1x_init() - Initialize DP HDCP TrustZone bridge
 * @init_data: HDCP initialization data from sde_hdcp
 *
 * Creates HDCP context and initializes TrustZone interface.
 * This should be called during display post_enable.
 *
 * Return: Opaque handle to DP HDCP context on success, NULL on failure
 */
void *dp_hdcp1x_init(struct sde_hdcp_init_data *init_data)
{
	struct dp_hdcp1x_ctx *ctx;

	if (!init_data) {
		DP_ERR("invalid input\n");
		return NULL;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	/* Store initialization data */
	ctx->init_data = *init_data;

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	/* Initialize TrustZone HDCP handle */
	ctx->hdcp1_handle = hdcp1_init();
	if (!ctx->hdcp1_handle) {
		DP_ERR("hdcp1_init failed\n");
		kfree(ctx);
		return NULL;
	}
#endif

	/* Initialize states */
	ctx->dcp_state = HDCP_STATE_INACTIVE;
	ctx->tz_state = HDCP_STATE_INACTIVE;

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	/* Initialize topology structure */
	memset(&ctx->topology, 0, sizeof(ctx->topology));
#endif

	DP_DEBUG("DP HDCP initialized successfully\n");

	return ctx;
}

/**
 * dp_hdcp1x_deinit() - Deinitialize DP HDCP TrustZone bridge
 * @input: DP HDCP context handle
 *
 * Cleans up HDCP context and deinitializes TrustZone interface.
 * This should be called during display disable.
 */
void dp_hdcp1x_deinit(void *input)
{
	struct dp_hdcp1x_ctx *ctx = input;

	if (!ctx) {
		DP_ERR("invalid input\n");
		return;
	}

	/* Ensure HDCP is stopped before cleanup */
	if (ctx->tz_state != HDCP_STATE_INACTIVE) {
		DP_DEBUG("Stopping HDCP before deinit\n");
		dp_hdcp1x_stop(input);
	}

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	/* Deinitialize TrustZone handle */
	if (ctx->hdcp1_handle) {
		hdcp1_deinit(ctx->hdcp1_handle);
		ctx->hdcp1_handle = NULL;
	}
#endif

	kfree(ctx);
	DP_DEBUG("DP HDCP deinitialized\n");
}

/**
 * dp_hdcp2x_init() - Initialize DP HDCP 2.x TrustZone bridge
 * @init_data: HDCP initialization data from sde_hdcp
 *
 * Creates HDCP 2.x context and initializes TrustZone interface.
 * This should be called during display post_enable.
 *
 * Return: Opaque handle to DP HDCP 2.x context on success, NULL on failure
 */
void *dp_hdcp2x_init(void)
{
	struct dp_hdcp2x_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	/* Allocate response buffer (4KB should be sufficient for all HDCP 2.x messages) */
	ctx->buf_len = SZ_4K;
	ctx->response_buf = kzalloc(ctx->buf_len, GFP_KERNEL);
	if (!ctx->response_buf) {
		DP_ERR("Failed to allocate response buffer\n");
		kfree(ctx);
		return NULL;
	}

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	/* Initialize TrustZone HDCP 2.x handle */
	ctx->hdcp2_handle = hdcp2_init(HDCP_TXMTR_DP);
	if (!ctx->hdcp2_handle) {
		DP_ERR("hdcp2_init failed\n");
		kfree(ctx->response_buf);
		kfree(ctx);
		return NULL;
	}

	/* Initialize app_data structure */
	ctx->app_data.timeout = 0;
	ctx->app_data.repeater_flag = false;
	ctx->app_data.request.length = 0;

	ctx->app_data.response.data = ctx->response_buf;
	ctx->app_data.response.length = 0;
#endif

	/* Initialize state */
	ctx->state = HDCP_STATE_INACTIVE;

	DP_DEBUG("DP HDCP 2.x initialized successfully\n");

	return ctx;
}

/**
 * dp_hdcp2x_deinit() - Deinitialize DP HDCP 2.x TrustZone bridge
 * @input: DP HDCP 2.x context handle
 *
 * Cleans up HDCP 2.x context and deinitializes TrustZone interface.
 * This should be called during display disable.
 */
void dp_hdcp2x_deinit(void *input)
{
	struct dp_hdcp2x_ctx *ctx = input;

	if (!ctx) {
		DP_ERR("invalid input\n");
		return;
	}

	/* Ensure HDCP is stopped before cleanup */
	if (ctx->state != HDCP_STATE_INACTIVE) {
		DP_DEBUG("Stopping HDCP 2.x before deinit\n");
		dp_hdcp2x_stop(input);
	}

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	/* Deinitialize TrustZone handle */
	if (ctx->hdcp2_handle) {
		hdcp2_deinit(ctx->hdcp2_handle);
		ctx->hdcp2_handle = NULL;
	}
#endif

	/* Free response buffer */
	kfree(ctx->response_buf);

	kfree(ctx);
	DP_DEBUG("DP HDCP 2.x deinitialized\n");
}

/**
 * dp_hdcp2x_start() - Start HDCP 2.x authentication and get AKE_INIT
 * @input: HDCP 2.x context handle
 * @ake_init: Pointer to store AKE_INIT message buffer
 * @ake_init_len: Pointer to store AKE_INIT message length
 *
 * Called when DCP sends HDCP2X_START event. Starts HDCP 2.x in TrustZone
 * and retrieves the AKE_INIT message to be sent to the sink.
 *
 * Return: 0 on success, negative error code on failure
 */
int dp_hdcp2x_start(void *input, uint8_t **ake_init, uint32_t *ake_init_len)
{
	struct dp_hdcp2x_ctx *ctx = input;
	int rc;

	if (!ctx || !ake_init || !ake_init_len) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	DP_DEBUG("Starting HDCP 2.x authentication\n");

	ctx->state = HDCP_STATE_AUTHENTICATING;

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	/* Reset app_data for start command */
	ctx->app_data.timeout = 0;
	ctx->app_data.repeater_flag = false;
	ctx->app_data.request.length = 0;
	ctx->app_data.response.length = 0;

	/* Call TZ to start HDCP 2.x */
	rc = hdcp2_app_comm(ctx->hdcp2_handle, HDCP2_CMD_START, &ctx->app_data);
	if (rc) {
		DP_ERR("HDCP2_CMD_START failed: %d\n", rc);
		ctx->state = HDCP_STATE_AUTH_FAIL;
		return rc;
	}

	/* Get AKE_INIT message from TrustZone */
	rc = hdcp2_app_comm(ctx->hdcp2_handle, HDCP2_CMD_START_AUTH, &ctx->app_data);
	if (rc) {
		DP_ERR("HDCP2_CMD_START_AUTH failed: %d\n", rc);
		ctx->state = HDCP_STATE_AUTH_FAIL;
		return rc;
	}

	*ake_init = ctx->app_data.response.data;
	*ake_init_len = ctx->app_data.response.length;
#else
	DP_ERR("HDCP QSEECOM not enabled\n");
	ctx->state = HDCP_STATE_AUTH_FAIL;
	return -ENODEV;
#endif

	DP_DEBUG("AKE_INIT generated, length=%u\n", *ake_init_len);

	return 0;
}

/**
 * dp_hdcp2x_enable_encryption() - Enable HDCP 2.x encryption
 * @input: HDCP 2.x context handle
 *
 * Calls TrustZone to enable HDCP 2.x encryption after successful authentication.
 * This function is called by dp_mgr_hfi after SKE_SEND_TYPE_ID (non-repeater)
 * or REP_STREAM_READY (repeater).
 *
 * Return: 0 on success, negative error code on failure
 */
int dp_hdcp2x_enable_encryption(void *input)
{
	struct dp_hdcp2x_ctx *ctx = input;
	int rc;

	if (!ctx) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	DP_DEBUG("Enabling HDCP 2.x encryption\n");

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	rc = hdcp2_app_comm(ctx->hdcp2_handle, HDCP2_CMD_EN_ENCRYPTION, &ctx->app_data);
	if (!rc) {
		ctx->state = HDCP_STATE_AUTHENTICATED;
		DP_DEBUG("HDCP 2.x authentication completed with encryption enabled\n");
	} else {
		DP_ERR("Failed to enable encryption: %d\n", rc);
		ctx->state = HDCP_STATE_AUTH_FAIL;
	}
#else
	DP_ERR("HDCP QSEECOM not enabled\n");
	rc = -ENODEV;
	ctx->state = HDCP_STATE_AUTH_FAIL;
#endif

	return rc;
}

/**
 * dp_hdcp2x_process_msg() - Process HDCP 2.x message from sink
 * @input: HDCP 2.x context handle
 * @req_buf: Request message buffer from sink
 * @req_len: Length of request message
 * @resp_buf: Pointer to store response message buffer
 * @resp_len: Pointer to store response message length
 *
 * Called when DCP sends HDCP2X_PROCESS_MSG event. Forwards the message
 * from the sink to TrustZone for processing and returns the response
 * message to be sent back to the sink.
 *
 * Return: 0 on success, negative error code on failure
 */
int dp_hdcp2x_process_msg(void *input, uint8_t *req_buf, uint32_t req_len, uint8_t **resp_buf,
	uint32_t *resp_len)
{
	struct dp_hdcp2x_ctx *ctx = input;
	int rc;

	if (!ctx || !req_buf || !resp_buf || !resp_len) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	DP_DEBUG("Processing HDCP 2.x message, req_len=%u\n", req_len);

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	if (req_len > ctx->buf_len) {
		DP_ERR("Request too large: %u > %u\n", req_len, ctx->buf_len);
		ctx->state = HDCP_STATE_AUTH_FAIL;
		return -EINVAL;
	}

	if (!ctx->app_data.request.data) {
		DP_ERR("app_data.request.data is NULL\n");
		ctx->state = HDCP_STATE_AUTH_FAIL;
		return -EINVAL;
	}

	memcpy(ctx->app_data.request.data, req_buf, req_len);
	ctx->app_data.request.length = req_len;
	ctx->app_data.response.length = 0;

	rc = hdcp2_app_comm(ctx->hdcp2_handle, HDCP2_CMD_PROCESS_MSG, &ctx->app_data);
	if (rc) {
		DP_ERR("HDCP2_CMD_PROCESS_MSG failed: %d\n", rc);
		ctx->state = HDCP_STATE_AUTH_FAIL;
		return rc;
	}

	*resp_buf = ctx->app_data.response.data;
	*resp_len = ctx->app_data.response.length;
#else
	DP_ERR("HDCP QSEECOM not enabled\n");
	ctx->state = HDCP_STATE_AUTH_FAIL;
	return -ENODEV;
#endif

	DP_DEBUG("TZ response: length=%u, repeater_flag=%u\n",
		 *resp_len, ctx->app_data.repeater_flag);

	return 0;
}

/**
 * dp_hdcp2x_feature_supported() - Check if HDCP 2.x is supported
 * @input: HDCP 2.x context handle
 *
 * Return: true if HDCP 2.x is supported, false otherwise
 */
bool dp_hdcp2x_feature_supported(void *input)
{
	struct dp_hdcp2x_ctx *ctx = input;
	bool supported = false;

	if (!ctx) {
		DP_ERR("invalid input\n");
		return false;
	}

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	supported = hdcp2_feature_supported(ctx->hdcp2_handle);
#endif

	DP_DEBUG("HDCP 2.x feature supported: %s\n", supported ? "yes" : "no");

	return supported;
}

/**
 * dp_hdcp2x_timeout() - Handle HDCP 2.x timeout
 * @input: HDCP 2.x context handle
 * @req_buf: Request message buffer (should be empty for timeout)
 * @req_len: Length of request message (should be 0 for timeout)
 * @resp_buf: Pointer to store response message buffer
 * @resp_len: Pointer to store response message length
 *
 * Called when DCP sends HDCP2X_TIMEOUT event. Notifies TrustZone
 * which decides whether to retry (LC_INIT) or give up (NULL).
 *
 * Return: 0 on success, negative error code on failure
 */
int dp_hdcp2x_timeout(void *input, uint8_t *req_buf, uint32_t req_len, uint8_t **resp_buf,
	uint32_t *resp_len)
{
	struct dp_hdcp2x_ctx *ctx = input;
	int rc;

	if (!ctx || !resp_buf || !resp_len) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	DP_DEBUG("Handling HDCP 2.x timeout\n");

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	/* Request should be empty for timeout */
	if (req_len != 0)
		DP_WARN("Unexpected request length for timeout: %u\n", req_len);

	/* Reset lengths */
	ctx->app_data.request.length = 0;
	ctx->app_data.response.length = 0;

	/* Call TZ with TIMEOUT command - TZ decides retry or give up */
	rc = hdcp2_app_comm(ctx->hdcp2_handle, HDCP2_CMD_TIMEOUT, &ctx->app_data);
	if (rc) {
		DP_ERR("HDCP2_CMD_TIMEOUT failed: %d\n", rc);
		return rc;
	}

	*resp_buf = ctx->app_data.response.data;
	*resp_len = ctx->app_data.response.length;
#else
	DP_ERR("HDCP QSEECOM not enabled\n");
	return -ENODEV;
#endif

	DP_DEBUG("Timeout handled, response length=%u\n", *resp_len);

	return 0;
}

/**
 * dp_hdcp2x_stop() - Stop HDCP 2.x authentication
 * @input: HDCP 2.x context handle
 *
 * Called when authentication should be stopped. Notifies TrustZone
 * to stop HDCP 2.x and disable encryption.
 */
void dp_hdcp2x_stop(void *input)
{
	struct dp_hdcp2x_ctx *ctx = input;

	if (!ctx) {
		DP_ERR("invalid input\n");
		return;
	}

	DP_DEBUG("Stopping HDCP 2.x\n");

#if IS_ENABLED(CONFIG_HDCP_QSEECOM)
	/* Notify TrustZone to stop */
	if (ctx->hdcp2_handle) {
		ctx->app_data.request.length = 0;
		ctx->app_data.response.length = 0;
		hdcp2_app_comm(ctx->hdcp2_handle, HDCP2_CMD_STOP, &ctx->app_data);
	}
#endif

	/* Update state */
	ctx->state = HDCP_STATE_INACTIVE;

	DP_DEBUG("HDCP 2.x stopped\n");
}
