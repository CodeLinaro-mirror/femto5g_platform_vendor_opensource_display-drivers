/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_DISPLAY_H
#define WM_DISPLAY_H

#include <linux/of_platform.h>
#include <drm/drm_bridge.h>

struct wm_dt_props {
	struct device_node *ext_disp_np;
	unsigned int audio_supported;
	unsigned int cec_supported;
};

struct wm_display {
	void (*set_mode)(struct wm_display *display, struct drm_display_mode *mode);
	void (*pre_enable)(struct wm_display *display);
	void (*enable)(struct wm_display *display);
	void (*disable)(struct wm_display *display);
	void (*post_disable)(struct wm_display *display);
};

struct wm_display_info {
	struct wm_display *display;
	struct device *dev;
	struct wm_dt_props *dt_props;
	struct wmmgr_client_context *wmmgr_ctxt;
};

#endif
