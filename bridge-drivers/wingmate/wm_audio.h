/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_AUDIO_H
#define WM_AUDIO_H

#include "wm_display.h"

struct wm_audio {
	int (*enable)(struct wm_audio *audio);
	int (*disable)(struct wm_audio *audio);
	int (*irq_handler)(struct wm_audio *audio, int irq);
};

struct wm_audio *wm_audio_init(struct wm_display_info *display_info);

void wm_audio_deinit(struct wm_audio *audio);

#endif
