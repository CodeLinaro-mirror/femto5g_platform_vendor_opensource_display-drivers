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
 * enum hfi_dump_evt_request - control values written to the
 *   "hfi_debug_ondemand_dump" debugfs node
 * @DUMP_REQUEST: trigger a new on-demand dump (write 0x1)
 * @DUMP_RESET:   acknowledge dump completion and reset state (write 0x2)
 */
enum hfi_dump_evt_request {
	DUMP_REQUEST = 0x1,
	DUMP_RESET   = 0x2,
};

/**
 * enum hfi_dump_state - state machine for on-demand HFI debug dump
 * @READY_TO_DUMP:   idle; a new dump request may be accepted
 * @DUMP_TRIGGERED:  user wrote 0x1; waiting for FW command to be sent
 * @DUMP_ON_GOING:   HFI command dispatched; waiting for FW response
 * @DUMP_READY:      FW responded and dump is complete; waiting for user
 *                   to acknowledge by writing 0x2
 */
enum hfi_dump_state {
	READY_TO_DUMP  = 0,
	DUMP_TRIGGERED = 1,
	DUMP_ON_GOING  = 2,
	DUMP_READY     = 3,
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
 * @ps_listener_packet_id: cache PANIC_SUBSCRIBE listener packet_id to remove during enc disable
 * @panic_events_state: maintains state of panic events registration
 * @dbg_dump_listener: listener for debug dump
 * @dump_state: current state of the on-demand debug dump state machine (atomic)
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
	u32 ps_listener_packet_id[2];
	bool panic_events_state;
	struct hfi_prop_listener dbg_dump_listener;
	atomic_t dump_state;
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
 * hfi_enc_debugfs_init - register HFI encoder debugfs nodes
 * @enc:        Pointer to virtual sde encoder structure
 * @Returns:    0 on success, or error code on failure
 */
int hfi_enc_debugfs_init(struct sde_encoder_virt *enc);

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

static inline int hfi_enc_debugfs_init(struct sde_encoder_virt *enc)
{
	return 0;
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
