/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DP_MST_DRM_H_
#define _DP_MST_DRM_H_

#include <linux/types.h>
#include <drm/drm_crtc.h>
#include <drm/drm_bridge.h>

#include "dp_display.h"

#if IS_ENABLED(CONFIG_DRM_MSM_DP_MST)

/**
 * dp_mst_drm_bridge_init - initialize mst bridge
 * @display: Pointer to private display structure
 * @encoder: Pointer to encoder for mst bridge mapping
 */
int dp_mst_drm_bridge_init(void *display,
	struct drm_encoder *encoder);

/**
 * dp_mst_drm_bridge_deinit - de-initialize mst bridges
 * @display: Pointer to private display structure
 */
void dp_mst_drm_bridge_deinit(void *display);

/**
 * dp_mst_init - initialize mst objects for the given display
 * @display: Pointer to private display structure
 */
int dp_mst_init(struct dp_display *dp_display);

/**
 * dp_mst_deinit - de-initialize mst objects for the given display
 * @display: Pointer to private display structure
 */
void dp_mst_deinit(struct dp_display *dp_display);

/**
 * dp_mst_clear_edid_cache - clear mst edid cache for the given display
 * @display: Pointer to private display structure
 */
void dp_mst_clear_edid_cache(void *dp_display);

/**
 * dp_mst_connector_mode_valid - callback to determine if specified mode is valid
 * @connector: Pointer to drm connector structure
 * @mode: Pointer to drm mode structure
 * @display: Pointer to private display handle
 * @avail_res: Pointer with curr available resources
 * Returns: Validity status for specified mode
 */
enum drm_mode_status dp_mst_connector_mode_valid(
                struct drm_connector *connector,
                struct drm_display_mode *mode,
                void *display, const struct msm_resource_caps_info *avail_res);

/**
 * dp_mst_connector_get_mode_info - retrieve information of the mode selected
 * @connector: Pointer to drm connector structure
 * @drm_mode: Display mode set for the display
 * @mode_info: Out parameter. Information of the mode
 * @sub_mode: Additional mode info to drm display mode
 * @display: Pointer to private display structure
 * @avail_res: Pointer with curr available resources
 * Returns: zero on success
 */

int dp_mst_connector_get_mode_info(struct drm_connector *connector,
                const struct drm_display_mode *drm_mode,
                struct msm_sub_mode *sub_mode,
                struct msm_mode_info *mode_info,
                void *display,
		const struct msm_resource_caps_info *avail_res);
#else

static inline int dp_mst_drm_bridge_init(void *display,
	struct drm_encoder *encoder)
{
	return 0;
}

static inline void dp_mst_drm_bridge_deinit(void *display)
{
}

static inline int dp_mst_init(struct dp_display *dp_display)
{
	return 0;
}

static inline int dp_mst_deinit(struct dp_display *dp_display)
{
	return 0;
}

static inline void dp_mst_clear_edid_cache(void *display)
{
}
#endif /* CONFIG_DRM_MSM_DP_MST */

#endif /* _DP_MST_DRM_H_ */
