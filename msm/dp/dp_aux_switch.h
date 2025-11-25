/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_AUX_SWITCH_H_
#define _DP_AUX_SWITCH_H_

#include <linux/device.h>
#include <linux/of.h>

struct dp_aux_switch {
	int (*init)(struct dp_aux_switch *aux_switch);
	int (*configure)(struct dp_aux_switch *aux_switch,
		bool enable, int orientation);
};

/* dp_aux_switch.c functions */
struct dp_aux_switch *dp_aux_switch_get(struct device *dev);
void dp_aux_switch_put(struct dp_aux_switch *aux_switch);

#endif /*_DP_AUX_SWITCH_H_*/
