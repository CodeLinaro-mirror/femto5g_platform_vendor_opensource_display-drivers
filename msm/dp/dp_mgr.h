/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DISPLAY_H_
#define _DP_DISPLAY_H_

#include <linux/list.h>
#include <drm/sde_drm.h>

#include "dp_debug.h"
#include "dp_client.h"

#define MAX_DP_ACTIVE_DISPLAY 8

struct dp_client *dp_mgr_init(struct platform_device *pdev,
	struct dp_debug_client *debug);
int dp_mgr_deinit(struct platform_device *pdev);
struct dp_panel *dp_mgr_get_panel(struct dp_client *client, int panel_id);
int dp_mgr_mmrm_callback(struct mmrm_client_notifier_data *notifier_data);

#endif /* _DP_DISPLAY_H_ */
