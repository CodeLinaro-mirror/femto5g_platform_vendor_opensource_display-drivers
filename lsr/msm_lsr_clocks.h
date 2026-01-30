/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */


#ifndef _MSM_LSR_CLOCKS_H_
#define _MSM_LSR_CLOCKS_H_
#include "lsr_core.h"
#include "msm_lsr_debug.h"

int msm_lsr_update_power(struct msm_lsr_core *core);

int msm_lsr_set_clocks(struct msm_lsr_core *core);
int msm_lsr_set_clocks_impl(struct lsr_device *device, u32 freq);
int msm_lsr_scale_clocks(struct lsr_device *device);
int msm_lsr_prepare_enable_clk(struct lsr_device *device,
		const char *name);
int msm_lsr_disable_unprepare_clk(struct lsr_device *device,
		const char *name);
int msm_lsr_init_clocks(struct lsr_device *device);
void msm_lsr_deinit_clocks(struct lsr_device *device);
int msm_lsr_set_bw(struct msm_lsr_core *core, struct bus_info *bus, unsigned long bw,
		unsigned long peak_bw);
int lsr_set_bw(struct bus_info *bus, unsigned long bw, unsigned long peak_bw);
#endif
