/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DRV_H_
#define _DP_DRV_H_

#include <linux/types.h>
#include <linux/platform_device.h>
#include <drm/drm_device.h>

#include "dp_client.h"

/**
 * struct dp_drv_pair - Structure to track master and slave eDP displays
 * @m_pll_display: Pointer to the master eDP display (eDP1)
 * @s_pll_display: Pointer to the slave eDP display (eDP0)
 *
 * This structure is used to synchronize operations between primary and
 * secondary eDP displays, particularly for shared PLL operations.
 */
struct dp_drv_pair {
	struct dp_drv *m_pll_display; /* eDP1 */
	struct dp_drv *s_pll_display;  /* eDP0 */
};

extern struct dp_drv_pair g_edp_pair;

struct dp_drv {
	struct platform_device *pdev;
	struct drm_device *drm_dev;
	struct drm_connector *base_connector;
	struct dp_client *client;
};

#if IS_ENABLED(CONFIG_DRM_MSM_DP)
int dp_drv_get_num_of_displays(struct drm_device *dev);
int dp_drv_get_displays(struct drm_device *dev, void **displays, int count);
int dp_drv_get_num_of_streams(struct drm_device *dev);
struct dp_intf_info *dp_drv_get_info(void *dp_drv);
int edp_drv_get_num_of_displays(struct drm_device *dev);
void *get_ipc_log_context(void);
#else
static inline int dp_drv_get_num_of_displays(struct drm_device *dev)
{
	return 0;
}
static inline int dp_drv_get_displays(struct drm_device *dev, void **displays, int count)
{
	return 0;
}
static inline int dp_drv_get_num_of_streams(struct drm_device *dev)
{
	return 0;
}
static inline struct dp_intf_info *dp_drv_get_info(void *dp_drv)
{
	return NULL;
}
static inline int dp_connector_update_pps(struct drm_connector *connector,
		char *pps_cmd, void *display)
{
	return 0;
}
static inline int edp_drv_get_num_of_displays(struct drm_device *dev)
{
	return 0;
}

#endif /* CONFIG_DRM_MSM_DP */
#endif /* _DP_DRV_H_ */
