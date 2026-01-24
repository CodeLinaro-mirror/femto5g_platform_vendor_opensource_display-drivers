/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _HFI_ENCODER_H_
#define _HFI_ENCODER_H_

#include "hfi_kms.h"
#include "sde_encoder.h"
#include "hfi_adapter.h"
#include "hfi_utils.h"

#define to_hfi_encoder(x) x->hfi_encoder

struct hw_event_state {
	u32 state;
	u32 pending;
};

/**
 * struct hfi_encoder - hfi implementation extension of sde_encoder object
 * @sde_base: Pointer to sde encoder base structure
 * @event_cbs: event ops for sde encoder
 * @vblank_ts: actual timestamp of last reported vblank event
 * @hfi_vsync_cnt: Atomic counter for tracking vsync events
 * @hfi_commit_cnt: Atomic counter for tracking frame commits to FW
 * @hfi_frame_done_cnt: Atomic counter for tracking which enc is
 *				done with frame processing
 * @hfi_frame_done_seqno: Atomic counter for number of frames processed with events handling
 * @hw_events_state: maintains state of HW events
 * @pending_kickoff_wq: Wait queue for blocking until kickoff completes
 * @hfi_cb_obj: hfi listener call back object
 * @misr_read_listener: hfi listener call back object for MISR
 */
struct hfi_encoder {
	struct sde_encoder_virt *sde_base;
	struct sde_encoder_event_ops event_cbs;
	ktime_t vblank_ts;
	atomic_t hfi_vsync_cnt;
	atomic_t hfi_commit_cnt;
	atomic_t hfi_frame_done_cnt;
	atomic_t hfi_frame_done_seqno;

	struct hw_event_state hw_events_state[MSM_ENC_EVENT_MAX];
	wait_queue_head_t pending_kickoff_wq;

	struct hfi_prop_listener hfi_cb_obj;
	struct hfi_prop_listener misr_read_listener;
};

#if IS_ENABLED(CONFIG_MDSS_HFI)
/**
 * hfi_encoder_init - initialize virtual hfi encoder object
 * @dev:        Pointer to drm device structure
 * @sde_enc:    Pointer to virtual sde encoder structure
 * @Returns:    0 on success, or error code on failure
 */
int hfi_encoder_init(struct drm_device *dev, struct sde_encoder_virt *sde_enc);

/**
 * hfi_set_power_vote - set power vote for HFI HW fence resources
 * @enable:     true to enable, false to disable
 * @Returns:    0 on success, error code on failure
 */
int hfi_set_power_vote(bool enable);
#else
int hfi_encoder_init(struct drm_device *dev, struct sde_encoder_virt *sde_enc)
{
	return -HFI_ERROR;
}

static inline int hfi_set_power_vote(bool enable)
{
	return 0;
}
#endif // IS_ENABLED(CONFIG_MDSS_HFI)

#if IS_ENABLED(CONFIG_QTI_HFI_CORE) && IS_ENABLED(CONFIG_QTI_HW_FENCE)
/**
 * sde_encoder_check_hfi_hw_fence_support - check if HFI HW fence is supported
 * @enc:        Pointer to virtual sde encoder structure
 * @Returns:    true if supported, false otherwise
 */
bool sde_encoder_check_hfi_hw_fence_support(struct sde_encoder_virt *enc);
#else
static inline bool sde_encoder_check_hfi_hw_fence_support(struct sde_encoder_virt *enc)
{
	return false;
}
#endif

#endif  // _HFI_ENCODER_H_
