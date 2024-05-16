/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_DEBUG_H
#define WM_DEBUG_H

#include "wm_display.h"

struct wm_debug {
};

struct wm_debug *wm_debug_init(struct wm_display_info *display_info);

void wm_debug_deinit(struct wm_debug *debug);

#endif
