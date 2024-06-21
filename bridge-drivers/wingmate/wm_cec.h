/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_CEC_H
#define WM_CEC_H

#include "wm_display.h"

struct wm_cec {
};

struct wm_cec *wm_cec_init(struct wm_display_info *display_info);

void wm_cec_deinit(struct wm_cec *cec);

#endif
