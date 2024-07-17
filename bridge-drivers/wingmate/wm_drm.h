/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_DRM_H
#define WM_DRM_H

#include "wm_display.h"

struct wm_drm {
	struct wm_display *display;
	struct device *dev;
	struct wm_dt_props *dt_props;

	struct drm_bridge bridge;
	struct drm_connector connector;
	struct mipi_dsi_device *dsi;
};

struct wm_drm *wm_drm_init(struct wm_display_info *display_info);

void wm_drm_deinit(struct wm_drm *drm);

#endif
