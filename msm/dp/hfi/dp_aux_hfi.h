/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DP_AUX_HFI_H_
#define _DP_AUX_HFI_H_

#include <linux/types.h>
#include "hfi_adapter.h"

#if (KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE)
#include <drm/display/drm_dp_helper.h>
#else
#include <drm/drm_dp_helper.h>
#endif

/**
 * struct dp_aux_hfi - HFI-backed AUX channel (HFI equivalent of dp_aux_private)
 *
 * Owns an embedded drm_dp_aux whose transfer function routes every native
 * DPCD read to DCP via HFI_COMMAND_DEBUG_DP_READ_DPCD.  No dependency on
 * the HWIO dp_aux / dp_catalog_aux infrastructure.
 */
struct dp_aux_hfi;

/**
 * dp_aux_hfi_get() - allocate and initialise an HFI AUX channel
 * @dev:             device pointer (used for drm_dp_aux.dev and allocations)
 * @hfi_client:      HFI client used to send commands to DCP
 * @ipc_log_context: optional IPC log context (may be NULL)
 *
 * Return: pointer to dp_aux_hfi on success, ERR_PTR on failure.
 */
struct dp_aux_hfi *dp_aux_hfi_get(struct device *dev,
		struct hfi_client_t *hfi_client, void *ipc_log_context);

/**
 * dp_aux_hfi_put() - free a dp_aux_hfi created by dp_aux_hfi_get()
 * @aux_hfi: handle returned by dp_aux_hfi_get()
 */
void dp_aux_hfi_put(struct dp_aux_hfi *aux_hfi);

/**
 * dp_aux_hfi_register() - register the embedded drm_dp_aux with the DRM core
 * @aux_hfi:  handle returned by dp_aux_hfi_get()
 * @drm_dev:  DRM device
 *
 * Equivalent of dp_aux_register() in the HWIO path.
 *
 * Return: 0 on success, negative error code on failure.
 */
int dp_aux_hfi_register(struct dp_aux_hfi *aux_hfi, struct drm_device *drm_dev);

/**
 * dp_aux_hfi_deregister() - deregister the embedded drm_dp_aux
 * @aux_hfi: handle returned by dp_aux_hfi_get()
 */
void dp_aux_hfi_deregister(struct dp_aux_hfi *aux_hfi);

/**
 * dp_aux_hfi_get_drm_aux() - return the embedded drm_dp_aux
 * @aux_hfi: handle returned by dp_aux_hfi_get()
 *
 * The returned pointer is valid for the lifetime of @aux_hfi.  Pass it
 * wherever a drm_dp_aux * is needed (e.g. drm_dp_dpcd_read()).
 */
struct drm_dp_aux *dp_aux_hfi_get_drm_aux(struct dp_aux_hfi *aux_hfi);

/**
 * dp_aux_hfi_process_response() - deliver an unsolicited AUX response from DCP
 * @aux_hfi:      handle returned by dp_aux_hfi_get()
 * @payload:      response payload from DCP
 * @payload_size: size of @payload in bytes
 *
 * Called by dp_mgr_hfi when HFI_COMMAND_DP_AUX_READ_RESPONSE arrives.
 * Stub for now; will be used once the async AUX path is wired up.
 *
 * Return: 0 on success, negative error code on failure.
 */
int dp_aux_hfi_process_response(struct dp_aux_hfi *aux_hfi,
		void *payload, u32 payload_size);

#endif /* _DP_AUX_HFI_H_ */
