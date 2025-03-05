/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _HFI_KMS_H_
#define _HFI_KMS_H_

#include "sde_kms.h"
#include "hfi_adapter.h"
#include "hfi_msm_drv.h"
#include "linux/completion.h"

/**
 * struct hfi_kms - virtualized hfi kms structure
 * @base: Base sde kms structure
 * @hfi_client: hfi client structure
 * @hfi_adapter: hfi adapter structure
 * @device_init_listener: HFI listener object for catalog parsing
 * @resource_vote_listener: HFI listener object for resource vote
 * @cat_init_done: atomic variable tracking catalog parse status
 */
struct hfi_kms {
	struct sde_kms base;
	struct hfi_client_t hfi_client;
	struct hfi_adapter_t *hfi_adapter;
	struct hfi_prop_listener device_init_listener;
	struct hfi_prop_listener resource_vote_listener;
	atomic_t cat_init_done;
};

/**
 * struct kms_hfi_cb - hfi extension of hfi callback structure
 * @client: hfi client structure
 * @cmd_buf: pointer to hfi_adapter command buffer
 * @cb_work: hfi callback work structure
 */
struct kms_hfi_cb {
	struct hfi_client_t *client;
	struct hfi_cmdbuf_t *cmd_buf;
	struct kthread_work cb_work;
};

#if IS_ENABLED(CONFIG_MDSS_HFI)
/**
 * hfi_kms_init - initialize virtual hfi kms object
 * @dev:        Pointer to drm device structure
 * Returns:     Pointer to newly created sde kms
 */
struct sde_kms *hfi_kms_init(struct drm_device *dev);
#else
struct sde_kms *hfi_kms_init(struct drm_device *dev);
#endif // IS_ENABLED(CONFIG_MDSS_HFI)

/**
 * hfi_kms_resource_vote_hfi_prop_handler - listener function for resource voting
 * @UNIQUE_DISP_OR_OBJ_ID: Unique ID for display or object
 * @CMD_ID: HFI Command ID for which callback received
 * @payload: Pointer to the payload data
 * @size: Size of the payload
 * @resource_vote_listener: Pointer to the resource vote listener structure
 * Returns: This function does not return a value.
 */
void hfi_kms_resource_vote_hfi_prop_handler(u32 UNIQUE_DISP_OR_OBJ_ID, u32 CMD_ID, void *payload,
		u32 size, struct hfi_prop_listener *resource_vote_listener);

/**
 * to_hfi_kms - convert sde_kms pointer to hfi kms pointer
 * @X: Pointer to sde_kms structure
 * Returns: Pointer to hfi_kms structure
 */
#define to_hfi_kms(x) container_of(x, struct hfi_kms, base)

/**
 * hfi_kms_get_cmd_buf - retrieve a command buffer for a specific display and cmd_type
 * @hfi_kms: Pointer to hfi_kms structure
 * @display_id: ID of display for which buffer is requested
 * @cmd_type: type of command buffer needed
 * Returns: pointer to the command buffer structure on success,
 *          or NULL on failure
 */
struct hfi_cmdbuf_t *hfi_kms_get_cmd_buf(struct hfi_kms *hfi_kms,
		u16 display_id, u32 cmd_type);
#endif // _HFI_KMS_H_
