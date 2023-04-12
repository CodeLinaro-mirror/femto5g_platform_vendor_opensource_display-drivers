/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_DSI_H
#define WM_DSI_H

#include "wm_display.h"

struct wm_dsi_rx {
	int (*configure_mipi_rx)(struct wm_dsi_rx *dsi_rx);
	int (*configure_video_path)(struct wm_dsi_rx *dsi_rx);
};

struct wm_dsi_rx *wm_dsi_rx_init(struct wm_display_info *display_info);

void wm_dsi_rx_deinit(struct wm_dsi_rx *dsi_rx);

#endif
