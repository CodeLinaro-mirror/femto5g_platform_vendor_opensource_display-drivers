/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_ALTMODE_H_
#define _DP_ALTMODE_H_

#include <linux/types.h>
#include "dp_hpd.h"

enum dp_altmode_pin_assignment {
	DPAM_HPD_OUT,
	DPAM_HPD_A,
	DPAM_HPD_B,
	DPAM_HPD_C,
	DPAM_HPD_D,
	DPAM_HPD_E,
	DPAM_HPD_F,
};

struct device;

struct dp_altmode {
	struct dp_hpd base;
};

struct dp_hpd *dp_altmode_get(struct device *dev, struct dp_hpd_cb *cb);

void dp_altmode_put(struct dp_hpd *pd);
#endif /* _DP_ALTMODE_H_ */

