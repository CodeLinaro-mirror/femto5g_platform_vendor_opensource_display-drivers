/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_HDMI_H
#define WM_HDMI_H

#include "wm_display.h"

struct wm_hdmi_tx {
	int (*pre_enable)(struct wm_hdmi_tx *hdmi_tx);
	int (*enable)(struct wm_hdmi_tx *hdmi_tx);
	int (*disable)(struct wm_hdmi_tx *hdmi_tx);
	int (*post_disable)(struct wm_hdmi_tx *hdmi_tx);
	int (*get_modes)(struct wm_hdmi_tx *hdmi_tx,
			struct drm_connector *connector);
	enum drm_mode_status (*mode_valid)(struct wm_hdmi_tx *hdmi_tx,
			const struct drm_display_mode *drm_mode);
	int (*irq_handler)(struct wm_hdmi_tx *hdmi_tx, int irq);
};

struct wm_hdmi_tx *wm_hdmi_tx_init(struct wm_display_info *display_info);

void wm_hdmi_tx_deinit(struct wm_hdmi_tx *hdmi_tx);

#endif
