/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_PLL_H
#define WM_PLL_H

#include "wm_display.h"

struct wm_pll {
	int (*configure_pixel_pll)(struct wm_pll *pll);
	int (*configure_audio_pll)(struct wm_pll *pll);
};

struct wm_pll *wm_pll_init(struct wm_display_info *display_info);

void wm_pll_deinit(struct wm_pll *pll);

#endif
