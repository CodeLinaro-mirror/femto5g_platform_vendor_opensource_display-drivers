/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_VTG_H
#define WM_VTG_H

#include "wm_display.h"

struct wm_vtg {
	int (*pre_enable)(struct wm_vtg *vtg);
	int (*enable)(struct wm_vtg *vtg);
	int (*disable)(struct wm_vtg *vtg);
	int (*post_disable)(struct wm_vtg *vtg);
	int (*irq_handler)(struct wm_vtg *vtg, int irq);
};

struct wm_vtg *wm_vtg_init(struct wm_display_info *display_info);

void wm_vtg_deinit(struct wm_vtg *vtg);

#endif
