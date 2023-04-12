/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_HDMI_H
#define WM_HDMI_H

#include "wm_display.h"

struct wm_hdmi_tx {
	int (*configure_video_mode)(struct wm_hdmi_tx *hdmi_tx, struct drm_display_mode *mode);
	int (*configure_info_frames)(struct wm_hdmi_tx *hdmi_tx);
};

struct wm_hdmi_tx *wm_hdmi_tx_init(struct wm_display_info *display_info);

void wm_hdmi_tx_deinit(struct wm_hdmi_tx *hdmi_tx);

#endif
