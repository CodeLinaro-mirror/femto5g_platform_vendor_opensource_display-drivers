/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_DRM_H
#define WM_DRM_H

#include "wm_display.h"

struct wm_drm {
};

struct wm_drm *wm_drm_init(struct wm_display_info *display_info);

void wm_drm_deinit(struct wm_drm *drm);

#endif
