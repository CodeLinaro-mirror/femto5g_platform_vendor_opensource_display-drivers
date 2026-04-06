/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _HFI_CRTC_H_
#define _HFI_CRTC_H_
#include <linux/types.h>

#include "sde_crtc.h"
#include "hfi_utils.h"
#include "hfi_adapter.h"
#include "hfi_defs_display.h"

enum hfi_crtc_event {
	HFI_CRTC_EVENT_LTM,
	HFI_CRTC_EVENT_RGB_HIST,
	HFI_CRTC_EVENT_PA_HIST,
	HFI_CRTC_EVENT_SPR_OPR,
	HFI_CRTC_EVENT_AIQE_COPR,
	HFI_CRTC_EVENT_MAX,
};

struct crtc_hw_event_state {
	u32 state;
	u32 pending;
};

/**
 * struct hfi_crtc - virtualized hfi CRTC data structure
 * @sde_base: Pointer to base sde crtc structure
 * @hfi_lock: Mutex to protect hfi specific data
 * @base_props: prop helper object for intermediate property collection
 * @color_props: color prop helper object for intermediate property collection
 * @kv_props: kv pair helper object for intermediate property collection
 * @misr_read_listener: hfi listener for MISR
 * @hfi_buff_map_dither: hfi_buff map object for SPR dither
 * @prev_plane_mask: tracks the previous plane mask
 * @pending_enc_mask: encoder_mask that has pending commit on the drm_crtc
 * @hw_events_state: tracks the state of the hw events
 * @hfi_cb_obj: hfi callback object for crtc events from hfi
 */
struct hfi_crtc {
	struct sde_crtc *sde_base;
	struct mutex hfi_lock;
	struct hfi_util_u32_prop_helper *base_props;
	struct hfi_util_u32_prop_helper *color_props;
	struct hfi_util_kv_helper *kv_props;
	struct hfi_prop_listener misr_read_listener;
	struct hfi_shared_addr_map hfi_buff_map_dither;
	uint32_t prev_plane_mask;
	u32 pending_enc_mask;
	struct crtc_hw_event_state hw_events_state[HFI_CRTC_EVENT_MAX];
	struct hfi_prop_listener hfi_cb_obj;
};

/**
 * struct hfi_crtc_state - hfi container for atomic crtc state
 * @sde_base: Pointer to base sde crtc state structure
 */
struct hfi_crtc_state {
	struct sde_crtc_state *sde_base;
};

/*
 * struct hfi_hw_fence - hfi hw fence prop
 * @flags    :  flags for input/output fence
 * @h_synx    : fence handle 32 bit id
 */
struct hfi_hw_fence {
	u32 flags;
	u32 h_synx;
};

/**
 * hfi_crtc_set_pending_enc_mask - helper to set/reset the enc mask for caching
 * @sde_crtc: Pointer to sde crtc struct
 * @enc_mask: input encoder mask to cache
 */
void hfi_crtc_set_pending_enc_mask(struct sde_crtc *sde_crtc, u32 enc_mask);

#if IS_ENABLED(CONFIG_MDSS_HFI)
/**
 * hfi_crtc_set_pending_enc_mask - helper to set/reset the enc mask for caching
 * @sde_crtc: Pointer to sde crtc struct
 * @enc_mask: input encoder mask to cache
 */
void hfi_crtc_set_pending_enc_mask(struct sde_crtc *sde_crtc, u32 enc_mask);

/**
 * hfi_crtc_init - create a new hfi crtc object
 * @sde_crtc: Pointer to sde crtc struct
 * @Returns: 0 on success, or error code on failure
 */
int hfi_crtc_init(struct sde_crtc *sde_crtc);
/**
 * hfi_crtc_get_display_id - Retrieve the display ID for a given CRTC
 * @crtc: Pointer to the DRM CRTC structure
 * @crtc_state: Pointer to the DRM CRTC state structure
 * Return: display ID
 */
u32 hfi_crtc_get_display_id(struct drm_crtc *crtc, struct drm_crtc_state *crtc_state);

/**
 * hfi_crtc_add_set_property - Add display properties to HFI command buffer
 * @crtc: Pointer to the DRM CRTC structure
 * @cmd_buf: Pointer to the HFI command buffer structure
 * @color_props: Pointer to the helper structure containing color properties
 * Return: 0 on success, or a negative error code on failure
 */
int hfi_crtc_add_set_property(struct drm_crtc *crtc, struct hfi_cmdbuf_t *cmd_buf,
		struct hfi_util_u32_prop_helper *color_props);

/**
 * hfi_crtc_get_cmd_buf - Get the HFI command buffer for a given CRTC
 * @crtc: Pointer to the DRM CRTC structure
 * Return: Pointer to the HFI command buffer structure, or NULL on failure
 */
struct hfi_cmdbuf_t *hfi_crtc_get_cmd_buf(struct drm_crtc *crtc);

/**
 * hfi_crtc_set_input_wait_hw_fence - Send input fence property to firmware
 * @crtc: Pointer to sde crtc struct
 * @synx_handle: 32-bit synx handle for the input fence
 * @prop: hfi property of input fence
 * @Returns: 0 on success, or error code on failure
 */
int hfi_crtc_set_input_wait_hw_fence(struct sde_crtc *crtc, u32 synx_handle, u32 prop);

/*
 * hfi_set_hw_fence_prop - Add fence property to the property collector
 * @ctx: Pointer to the sde fence context
 * @hfi_fence_type: Type of fence
 * @prop_collector: Pointer to the property collector
 * @disp_id: Display ID
 * @hfi_prop_id: HFI property ID
 * Return: 0 on success, or error code on failure
 */
int hfi_set_hw_fence_prop(struct sde_fence_context *ctx, enum hfi_fence_type hfi_fence_type,
		struct hfi_util_u32_prop_helper *prop_collector, u32 disp_id, u32 hfi_prop_id);
#else
static inline int hfi_crtc_init(struct sde_crtc *sde_crtc)
{
	return -HFI_ERROR;
}

static inline u32 hfi_crtc_get_display_id(struct drm_crtc *crtc, struct drm_crtc_state *crtc_state)
{
	return U32_MAX;
}

static inline void hfi_crtc_set_pending_enc_mask(struct sde_crtc *sde_crtc, u32 enc_mask)
{
}

static inline int hfi_crtc_add_set_property(struct drm_crtc *crtc, struct hfi_cmdbuf_t *cmd_buf,
		struct hfi_util_u32_prop_helper *color_props)
{
	return 0;
}

static inline struct hfi_cmdbuf_t *hfi_crtc_get_cmd_buf(struct drm_crtc *crtc)
{
	return NULL;
}

static inline int hfi_crtc_set_input_wait_hw_fence(struct sde_crtc *crtc, u32 synx_handle, u32 prop)
{
	return 0;
}

#endif // IS_ENABLED(CONFIG_MDSS_HFI)

#endif  // _HFI_CRTC_H_
