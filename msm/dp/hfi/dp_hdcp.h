/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DP_HDCP_H_
#define _DP_HDCP_H_

#include <linux/device.h>

#include "sde_hdcp.h"

/**
 * dp_hdcp_get_msm_hdcp_dev() - get msm-hdcp device from device tree
 *
 * Return: pointer to msm-hdcp device on success, NULL otherwise
 */
struct device *dp_hdcp_get_msm_hdcp_dev(void);

/**
 * dp_hdcp1x_init() - Initialize DP HDCP TrustZone bridge
 * @init_data: HDCP initialization data from sde_hdcp
 *
 * Creates HDCP context and initializes TrustZone interface.
 * This should be called during display post_enable.
 *
 * Return: Opaque handle to DP HDCP context on success, NULL on failure
 */
void *dp_hdcp1x_init(struct sde_hdcp_init_data *init_data);

/**
 * dp_hdcp1x_deinit() - Deinitialize DP HDCP TrustZone bridge
 * @input: DP HDCP context handle
 *
 * Cleans up HDCP context and deinitializes TrustZone interface.
 * This should be called during display disable.
 */
void dp_hdcp1x_deinit(void *input);

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
int dp_hdcp1x_start(void *input, u32 *aksv_msb, u32 *aksv_lsb);

/**
 * dp_hdcp1x_set_enc() - Control HDCP encryption
 * @input: HDCP context handle
 * @enable: true to enable encryption, false to disable
 *
 * Called when DCP sends HDCP1X_ENC event. Notifies TrustZone
 * to control encryption state.
 */
void dp_hdcp1x_set_enc(void *input, bool enable);

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
	u32 max_devices_exceeded, u32 max_cascade_exceeded);

/**
 * dp_hdcp1x_stop() - Stop HDCP authentication
 * @input: HDCP context handle
 *
 * Called when DCP sends HDCP1X_STOP event. Notifies TrustZone
 * to stop HDCP authentication and disable encryption.
 */
void dp_hdcp1x_stop(void *input);

/**
 * dp_hdcp1x_feature_supported() - Check if HDCP is supported
 * @input: HDCP context handle
 *
 * Return: true if HDCP 1.x is supported, false otherwise
 */
bool dp_hdcp1x_feature_supported(void *input);

/* HDCP 2.x functions */

/**
 * dp_hdcp2x_init() - Initialize DP HDCP 2.x TrustZone bridge
 * @init_data: HDCP initialization data from sde_hdcp
 *
 * Creates HDCP 2.x context and initializes TrustZone interface.
 * This should be called during display post_enable.
 *
 * Return: Opaque handle to DP HDCP 2.x context on success, NULL on failure
 */
void *dp_hdcp2x_init(void);

/**
 * dp_hdcp2x_deinit() - Deinitialize DP HDCP 2.x TrustZone bridge
 * @input: DP HDCP 2.x context handle
 *
 * Cleans up HDCP 2.x context and deinitializes TrustZone interface.
 * This should be called during display disable.
 */
void dp_hdcp2x_deinit(void *input);

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
int dp_hdcp2x_start(void *input, uint8_t **ake_init, uint32_t *ake_init_len);

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
	uint32_t *resp_len);

/**
 * dp_hdcp2x_feature_supported() - Check if HDCP 2.x is supported
 * @input: HDCP 2.x context handle
 *
 * Return: true if HDCP 2.x is supported, false otherwise
 */
bool dp_hdcp2x_feature_supported(void *input);

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
	uint32_t *resp_len);

/**
 * dp_hdcp2x_stop() - Stop HDCP 2.x authentication
 * @input: HDCP 2.x context handle
 *
 * Called when authentication should be stopped. Notifies TrustZone
 * to stop HDCP 2.x and disable encryption.
 */
void dp_hdcp2x_stop(void *input);

/**
 * dp_hdcp2x_enable_encryption() - Enable HDCP 2.x encryption
 * @input: HDCP 2.x context handle
 *
 * Calls TrustZone to enable HDCP 2.x encryption after successful authentication.
 *
 * Return: 0 on success, negative error code on failure
 */
int dp_hdcp2x_enable_encryption(void *input);

#endif /* _DP_HDCP_H_ */
