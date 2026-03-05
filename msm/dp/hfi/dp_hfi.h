/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DP_HFI_HEADER_H_
#define _DP_HFI_HEADER_H_

#include <linux/types.h>
#include <linux/platform_device.h>

#include "msm_drv.h"
#include "dp_drv.h"
#include "dp_client.h"
#include "dp_mgr_hfi.h"
#include "hfi_utils.h"
#include "hfi_defs_panel.h"

/**
 * struct dp_hfi - dp display hfi structure
 * @hfi_adapter:          Pointer to hfi adapter structure
 * @hfi_client:           Pointer to hfi client structure
 * @kv_props:             Pointer to hfi util kv helper structure
 * @hfi_cb_obj:           callback object for hfi responses
 * @connector:            Pointer to drm connector
 * @batch_cmd_buf         Pointer to cmd buf used in batch mode
 * @cmd_buf_worker:       kthread worker
 * @shared_addr_map:      Pointer to hold dcp shared buffer map addr
 * @mode_valid:           Indicate whether mode is valid
 * @tx_cmd_buf_dva:       DCP virtual address of the DCS cmd tx buffer
 * @tx_cmd_buf_fill_level:Tracks fill level of the DCS cmd tx buffer
 * @tx_cmd_buf_map:       Address map of DCS command payload HFI buffer
 * @cb_data:              callback data for display_update callback function
 * @handle_event:         callback function for HFI events for DP
 */
struct dp_hfi {
	struct hfi_adapter_t *hfi_adapter;
	struct hfi_client_t *hfi_client;
	struct hfi_util_kv_helper *kv_props;
	struct hfi_prop_listener hfi_cb_obj;
	struct drm_connector *connector;
	struct hfi_cmdbuf_t *batch_cmd_buf;

	struct kthread_worker cmd_buf_worker;
	struct hfi_shared_addr_map *shared_addr_map;

	bool mode_valid;
	unsigned long tx_cmd_buf_dva;
	u32 tx_cmd_buf_fill_level;
	struct hfi_shared_addr_map tx_cmd_buf_map;

	void *cb_data;
	void (*handle_event)(void *cb_data, u32 event, void *payload, u32 size);
};

/**
 * dp_hfi_send_cmd_buf() - dp wrapper for sending hfi cmd buffer
 * @client: handle to dp client structure
 * @hfi_client: handle to hfi client
 * @hfi_cmd: hfi command
 * @display_type: display type string
 * @hfi_payload_type: hfi payload type
 * @payload: handle to payload
 * @payload_size: payload size
 * @flags: flags
 *
 * Return: error code.
 */
int dp_hfi_send_cmd_buf(struct dp_hfi *hfi,
					struct hfi_client_t *hfi_client, u32 hfi_cmd,
					const char *display_type, u32 hfi_payload_type,
					void *payload, u32 payload_size, u32 flags);

/**
 * dp_hfi_start_batch_cmd() - first cmd of a batch cmd sequence
 * @hfi: handle to dp hfi structure
 * @hfi_client: handle to hfi client
 *
 * Return: error code.
 */
int dp_hfi_start_batch_cmd(struct dp_hfi *hfi, struct hfi_client_t *hfi_client);

/**
 * dp_hfi_append_batch_cmd() - middle cmds of a batch cmd sequence
 * @hfi: handle to dp hfi structure
 * @hfi_client: handle to hfi client
 * @hfi_cmd: hfi command
 * @display_type: display type string
 * @hfi_payload_type: hfi payload type
 * @payload: handle to payload
 * @payload_size: payload size
 * @flags: flags
 *
 * Return: error code.
 */
int dp_hfi_append_batch_cmd(struct dp_hfi *hfi,
				struct hfi_client_t *hfi_client, u32 hfi_cmd,
				const char *display_type, u32 hfi_payload_type,
				void *payload, u32 payload_size, u32 flags);

/**
 * dp_hfi_end_batch_cmd() - last cmd of a batch cmd sequence. This will trigger the HFI.
 * @hfi: handle to dp hfi structure
 * @hfi_client: handle to hfi client
 * @hfi_cmd: hfi command
 * @display_type: display type string
 * @hfi_payload_type: hfi payload type
 * @payload: handle to payload
 * @payload_size: payload size
 * @flags: flags
 *
 * Return: error code.
 */
int dp_hfi_end_batch_cmd(struct dp_hfi *hfi,
				struct hfi_client_t *hfi_client, u32 hfi_cmd,
				const char *display_type, u32 hfi_payload_type,
				void *payload, u32 payload_size, u32 flags);

/**
 * dp_hfi_send_batch_cmd() - Trigger the HFI for the current batch of commands
 * @hfi: handle to dp hfi structure
 * @hfi_client: handle to hfi client
 * @blocking: blocking hfi flag
 *
 * Return: error code.
 */
int dp_hfi_send_batch_cmd(struct dp_hfi *hfi, struct hfi_client_t *hfi_client, bool blocking);

/**
 * dp_hfi_setup() - setup dp hfi interface
 * @client: handle to dp client structure
 * @cb_data: callback data
 *
 * Return: pointer to dp_mgr_hfi structure on success, ERR_PTR on failure.
 */
struct dp_hfi *dp_hfi_setup(struct dp_client *client, void *cb_data);

#endif /* _DP_HFI_HEADER_H_ */
