/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_AUDIO_H
#define WM_AUDIO_H

#include <linux/soc/qcom/msm_ext_display.h>
#include "wm_display.h"

struct wm_audio {
	int (*enable)(struct wm_audio *audio);
	int (*disable)(struct wm_audio *audio);
	int (*irq_handler)(struct wm_audio *audio, int irq);
	struct device *disp_dev;
	struct msm_ext_disp_init_data ext_audio_data;
	struct platform_device *audio_pdev;
	struct platform_device *ext_pdev;
	struct clk *clk;
	uint32_t base;
	uint32_t channels;
	uint32_t rec_chnls;
	uint32_t sample_rate;
	uint32_t bit_width;
	uint32_t insert_pcuv;
	u16 data_format;
};

struct wm_audio *wm_audio_init(struct wm_display_info *display_info);

void wm_audio_deinit(struct wm_audio *audio);

#endif
