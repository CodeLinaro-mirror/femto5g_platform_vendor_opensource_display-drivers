/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_HDCP_H
#define WM_HDCP_H

#include "wm_display.h"

struct wm_hdcp {
	int (*download_firmware)(struct wm_hdcp *hdcp);
	int (*configure)(struct wm_hdcp *hdcp);
	int (*irq_handler)(struct wm_hdcp *hdcp, int irq);
};

struct wm_hdcp *wm_hdcp_init(struct wm_display_info *display_info);

void wm_hdcp_deinit(struct wm_hdcp *hdcp);

#endif
