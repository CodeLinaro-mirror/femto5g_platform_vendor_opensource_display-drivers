/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_VTG_H
#define WM_VTG_H

#include "wm_display.h"

struct wm_vtg {
};

struct wm_vtg *wm_vtg_init(struct wm_display_info *display_info);

void wm_vtg_deinit(struct wm_vtg *vtg);

#endif
