/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DP_HFI_AUDIO_H_
#define _DP_HFI_AUDIO_H_

#include <linux/types.h>
#include <linux/version.h>
#include <linux/platform_device.h>

#if (KERNEL_VERSION(6, 3, 0) <= LINUX_VERSION_CODE)
#include <msm_ext_display.h>
#else
#include <linux/soc/qcom/msm_ext_display.h>
#endif

#include "dp_client.h"
#include "dp_mgr_hfi.h"

/**
 * struct dp_audio
 * @lane_count: number of lanes configured in current session
 * @bw_code: link rate's bandwidth code for current session
 * @tui_active: set to true if TUI is active in the system
 */
struct dp_audio {
	u32 lane_count;
	u32 bw_code;
	bool tui_active;

	/**
	 * on()
	 *
	 * Notifies user mode clients that DP is powered on, and that audio
	 * playback can start on the external display.
	 *
	 * @dp_audio: an instance of struct dp_audio.
	 *
	 * Returns the error code in case of failure, 0 in success case.
	 */
	int (*on)(struct dp_audio *dp_audio);

	/**
	 * off()
	 *
	 * Notifies user mode clients that DP is shutting down, and audio
	 * playback should be stopped on the external display.
	 *
	 * @dp_audio: an instance of struct dp_audio.
	 * @skip_wait: flag to skip any waits
	 *
	 * Returns the error code in case of failure, 0 in success case.
	 */
	int (*off)(struct dp_audio *dp_audio, bool skip_wait);

	/**
	 * get_data()
	 *
	 * Gets the dp_audio instance from platform device.
	 *
	 * @pdev: platform device instance.
	 *
	 * Returns the dp_audio instance.
	 */
	struct dp_audio *(*get_data)(struct platform_device *pdev);
};

/**
 * dp_hfi_audio_get() - Initialize HFI audio subsystem
 * @pdev: Platform device
 * @client: DP client handle
 *
 * Returns: Pointer to dp_audio structure on success, ERR_PTR on failure
 */
struct dp_audio *dp_hfi_audio_get(struct platform_device *pdev,
				   struct dp_client *client);

/**
 * dp_hfi_audio_put() - Deinitialize HFI audio subsystem
 * @dp_audio: Pointer to dp_audio structure
 */
void dp_hfi_audio_put(struct dp_audio *dp_audio);

#endif /* _DP_HFI_AUDIO_H_ */
