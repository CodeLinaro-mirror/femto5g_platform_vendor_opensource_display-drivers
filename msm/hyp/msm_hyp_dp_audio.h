/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "virtio/virtio_kms.h"

struct msm_hyp_dp_audio {
	/**
	 * on()
	 *
	 * Notifies user mode clients that DP is powered on, and that audio
	 * playback can start on the external display.
	 *
	 * @dp_audio: an instance of struct msm_hyp_dp_audio.
	 *
	 * Returns the error code in case of failure, 0 in success case.
	 */
	int (*on)(struct msm_hyp_dp_audio *dp_audio);

	/**
	 * off()
	 *
	 * Notifies user mode clients that DP is shutting down, and audio
	 * playback should be stopped on the external display.
	 *
	 * @dp_audio: an instance of struct msm_hyp_dp_audio.
	 * @skip_wait: flag to skip any waits
	 *
	 * Returns the error code in case of failure, 0 in success case.
	 */
	int (*off)(struct msm_hyp_dp_audio *dp_audio, bool skip_wait);
};

struct msm_hyp_dp_audio *msm_hyp_dp_audio_get(struct platform_device *pdev,
			struct virtio_kms_output *panel);

void msm_hyp_dp_audio_put(struct msm_hyp_dp_audio *dp_audio);
