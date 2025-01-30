/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _HFI_ENCODER_H_
#define _HFI_ENCODER_H_

#include "hfi_kms.h"
#include "sde_encoder.h"
#include "hfi_adapter.h"
#include "hfi_utils.h"

#define to_hfi_encoder(x) container_of(x, struct hfi_encoder, sde_base)

struct hw_event_state {
	u32 state;
	u32 pending;
};

/**
 * struct hfi_encoder - hfi implementation extension of sde_encoder object
 * @sde_base: sde encoder base structure
 * @event_cbs: event ops for sde encoder
 * @hfi_cb_obj: hfi listener call back object
 */
struct hfi_encoder {
	struct sde_encoder_virt sde_base;
	struct sde_encoder_event_ops event_cbs;
	struct hw_event_state hw_events_state[MSM_ENC_EVENT_MAX];
	wait_queue_head_t pending_kickoff_wq;

	struct hfi_prop_listener hfi_cb_obj;
	struct hfi_prop_listener misr_read_listener;
};

#if IS_ENABLED(CONFIG_MDSS_HFI)
/**
 * hfi_encoder_init - initialize virtual hfi encoder object
 * @dev:        Pointer to drm device structure
 * @info:  Pointer to display information structure
 * @ops:	Pointer to hfi encoder event ops
 * Returns:     Pointer to newly created drm encoder
 */
struct sde_encoder_virt *hfi_encoder_init(struct drm_device *dev, struct msm_display_info *info,
		struct sde_encoder_event_ops *ops);
#else
struct sde_encoder_virt *hfi_encoder_init(struct drm_device *dev, struct msm_display_info *info,
		struct sde_encoder_event_ops *ops)
{
	return NULL;
}
#endif // IS_ENABLED(CONFIG_MDSS_HFI)

#endif  // _HFI_ENCODER_H_
