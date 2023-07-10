/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_HDMI_H
#define WM_HDMI_H

#include "wm_display.h"

#define WM_HDMI_REG_MASK	0xff
/* HDMI_TX Register Offsets */
#define WM_HDMI_IH_PHY_STAT0	0x104
#define WM_HDMI_IH_I2CM_STAT0	0x105
#define WM_HDMI_IH_MUTE_PHY_STAT0	0x184
#define WM_HDMI_IH_MUTE_I2CM_STAT0	0x185

#define WM_HDMI_PHY_CONF0 	0x3000
#define WM_HDMI_PHY_STAT0 	0x3004
#define WM_HDMI_PHY_MASK0 	0x3006
#define WM_HDMI_PHY_POL0 	0x3007

#define WM_HDMI_I2CM_SLAVE 	0x7e00
#define WM_HDMI_I2CM_ADDRESS 	0x7e01
#define WM_HDMI_I2CM_DATAI 	0x7e03
#define WM_HDMI_I2CM_OPERATION 	0x7e04
#define WM_HDMI_I2CM_SEGADDR 	0x7e08
#define WM_HDMI_I2CM_SEGPTR 	0x7e0a
#define WM_HDMI_I2CM_READ_BUFFx	0x7e20

enum hdmi_tx_output_format
{
	HDMI_OUTPUT_FORMAT_RGB,
	HDMI_OUTPUT_FORMAT_YCBCR444,
	HDMI_OUTPUT_FORMAT_YCBCR422,
	HDMI_OUTPUT_FORMAT_YCBCR420,
	HDMI_OUTPUT_FORMAT_INVALID
};

enum wm_hdmi_tx_state
{
	HDMI_CONNECTED = BIT(0),
	HDMI_DISCONNECTED = BIT(7),
};

struct wm_hdmi_intrs {
	u8 edid_itr;
	u8 phy_itr;
};

struct edid_ctrl {
	u8 retry;
	u32 count, total_ext, curr_ext;
};

struct wm_hdmi_tx {
	u8 state;
	struct wm_display *display;
	struct wm_dt_props *dt_props;
	struct device *dev;

	/* Sink details */
	u8 *edid;
	bool hpd_status;
	struct wm_hdmi_intrs intr;
	struct edid_ctrl edid_ctrl;

	// struct wm_hdmi_tx_info hdmi_info;
	enum hdmi_tx_output_format output_format;

	struct mutex lock;

	struct workqueue_struct *workq;
	struct work_struct hpd_work;
	struct work_struct edid_work;
	struct work_struct phy_work;

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
