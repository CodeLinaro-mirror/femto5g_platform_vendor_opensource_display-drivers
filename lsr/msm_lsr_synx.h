/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _MSM_LSR_SYNX_H_
#define _MSM_LSR_SYNX_H_

#include <linux/types.h>
#if IS_ENABLED(CONFIG_QTI_HW_FENCE)
#include <synx_api.h>
#endif
#include "hfi_defs_lsr.h"

#define LSR_REUSABLE_FENCE_MAX HFI_LSR_REUSABLE_FENCE_MAX

#ifdef CONFIG_LSR_SERAPH
#define LSR_SYNX_ENABLED 1
#else
#define LSR_SYNX_ENABLED 0
#endif

struct lsr_device;

struct msm_lsr_synx_ops {
	int (*lsr_sess_init_synx)(struct lsr_device *dev);
	int (*lsr_sess_deinit_synx)(struct lsr_device *dev);
};

void lsr_synx_ftbl_init(struct lsr_device *dev);

#if LSR_SYNX_ENABLED
int lsr_sde_fence_create_and_import(struct sde_fence_context *ctx, uint64_t *val,
		uint32_t offset, void *waiting_client_hw_fence_handle);
int lsr_create_reusable_hsynx(u32 *h_synx_arr);
#else
static inline int lsr_sde_fence_create_and_import(struct sde_fence_context *ctx, uint64_t *val,
		uint32_t offset, void *waiting_client_hw_fence_handle)
{
	return -EINVAL;
}
static inline int lsr_create_reusable_hsynx(u32 *h_synx_arr)
{
	return -EINVAL;
}
#endif

#endif
