/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_VTG_H
#define WM_VTG_H

#include "wm_display.h"
#include <linux/printk.h>
#include <drm/drm_print.h>

/*registers*/
#define VTG_ENABLE				0x0
#define VTG_VERT_ERROR				0x04
#define VTG_HORIZ_ERROR				0x08
#define VTG_BUFFER_LEVEL_LOW			0x10
#define VTG_BUFFER_LEVEL_HIGH			0x14
#define VTG_BUFFER_INTERRUPT_MASK		0x1C

#define VTG_FRAME_INTERVAL      		0x20
#define VTG_FRAME_DELTA				0x24
#define VTG_FRAME_DELTA_BOUND			0x28
#define VTG_FRAME_DELTA_LP2			0x2C
#define VTG_FRAME_DELTA_LP2_BOUND		0x30
#define VTG_FRAME_DELTA_LP4			0x34
#define VTG_FRAME_DELTA_LP4_BOUND		0x38
#define VTG_FRAME_DELTA_LP8			0x3C
#define VTG_FRAME_DELTA_LP8_BOUND		0x40
#define VTG_FRAME_POSTPONE			0x44
#define VTG_FRAME_LATENCY			0x48
#define VTG_FRAME_LATENCY_BOUND			0x4C
#define VTG_VFP_LINES				0x50
#define VTG_VSA_LINES				0x54
#define VTG_VBP_LINES				0x58
#define VTG_VACT_LINES				0x5C
#define VTG_HFP_CYCLES				0x60
#define VTG_HSA_CYCLES				0x64
#define VTG_HBP_CYCLES				0x68
#define VTG_HACT_CYCLES				0x6C
#define VTG_TOTAL_PIXELS			0x70

#define VTG_HW_FEEDBACK                         0x74
#define VTG_HW_K_PERIOD				0x78
#define VTG_HW_K_LATENCY			0x7C

#define VTG_VID_PLL_ENABLE			0x80
#define VTG_VID_PLL_STAT			0x84
#define VTG_VID_PLL_REFDIV			0x88
#define VTG_VID_PLL_FBDIV			0x8C
#define VTG_VID_PLL_FRAC			0x90
#define VTG_VID_PLL_POSTDIV			0x94

#define WM_DEBUG(fmt, ...)      DRM_DEV_DEBUG(NULL, "[wm-debug]: "fmt, \
					##__VA_ARGS__)
#define WM_INFO(fmt, ...)	DRM_DEV_INFO(NULL, "[wm-info]: "fmt, \
					##__VA_ARGS__)
#define WM_ERR(fmt, ...)	DRM_DEV_ERROR(NULL, "[wm-error]: " fmt, \
					##__VA_ARGS__)

struct wm_vtg {
	int (*pre_enable)(struct wm_vtg *vtg);
	int (*enable)(struct wm_vtg *vtg);
	int (*disable)(struct wm_vtg *vtg);
	int (*post_disable)(struct wm_vtg *vtg);
	int (*irq_handler)(struct wm_vtg *vtg, int irq);
	void (*deinit) (struct wm_vtg *vtg);
	bool hw_enable;
	struct wm_display *display;
	struct workqueue_struct *workq;
	struct work_struct buffer_work;
	struct work_struct err_work;
};

struct wm_vtg *wm_vtg_init(struct wm_display_info *display_info);

#endif
