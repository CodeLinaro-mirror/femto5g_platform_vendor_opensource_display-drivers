/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_DSI_H
#define WM_DSI_H

#include "wm_display.h"

struct wm_dsi_rx {
	int (*pre_enable)(struct wm_dsi_rx *dsi_rx);
	int (*enable)(struct wm_dsi_rx *dsi_rx);
	int (*disable)(struct wm_dsi_rx *dsi_rx);
	int (*post_disable)(struct wm_dsi_rx *dsi_rx);
	int (*irq_handler)(struct wm_dsi_rx *dsi_rx, int irq);
};

struct wm_dsi_rx *wm_dsi_rx_init(struct wm_display_info *display_info);

void wm_dsi_rx_deinit(struct wm_dsi_rx *dsi_rx);

#endif
