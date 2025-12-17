/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _MSM_LSR_SYNX_H_
#define _MSM_LSR_SYNX_H_

#include <linux/types.h>
#include <synx_api.h>

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
#endif
