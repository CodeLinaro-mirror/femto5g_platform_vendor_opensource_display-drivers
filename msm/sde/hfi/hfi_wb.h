/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __HFI_WB_H__
#define __HFI_WB_H__

#include "hfi_utils.h"
#include "sde_wb.h"

/* Downscale Blur (DNSC Blur) - number of gaussian coefficient LUTs */
#define HFI_DNSC_BLUR_COEF_NUM              64

/* Downscale Blur flags */
#define HFI_DNSC_BLUR_EN                    (1 << 0)
#define HFI_DNSC_BLUR_RND_8B_EN             (1 << 1)
#define HFI_DNSC_BLUR_DITHER_EN             (1 << 2)

/* Downscale Blur horizontal/vertical filter flags */
#define HFI_DNSC_BLUR_GAUS_FILTER           (1 << 0)
#define HFI_DNSC_BLUR_PCMN_FILTER           (1 << 1)

/* Downscale Blur dither matrix size (4x4 matrix = 16 elements) */
#define HFI_DNSC_DITHER_MATRIX_SZ           16

#define HFI_WB_BASE_PROP_MAX_SIZE 1024

/*!
 * struct hfi_display_dnsc_dither - dither feature structure for downscaler blur (DNSC Blur)
 * @brief DNSC Blur dither config.
 * @var flags          flags representing enable and broadcast setting
 * @var feature_flags  flags for the feature customization
 * @var temporal_en    temporal dither enable
 * @var c0_bitdepth    c0 component bit depth
 * @var c1_bitdepth    c1 component bit depth
 * @var c2_bitdepth    c2 component bit depth
 * @var c3_bitdepth    c3 component bit depth
 * @var matrix         dither strength matrix
 */
struct hfi_display_dnsc_dither {
	u32 flags;
	u32 feature_flags;
	u32 temporal_en;
	u32 c0_bitdepth;
	u32 c1_bitdepth;
	u32 c2_bitdepth;
	u32 c3_bitdepth;
	u32 matrix[HFI_DNSC_DITHER_MATRIX_SZ];
};

/*!
 * struct hfi_dnsc_blur_cfg - hfi downscale blur config.
 * @brief Downscale blur config. Passed through hfi_buff mechanism.
 * @var flags         Configuration flags for the downscaler blur.
 * @var src_width     Source image width in pixels.
 * @var src_height    Source image height in pixels.
 * @var dst_width     Destination image width in pixels.
 * @var dst_height    Destination image height in pixels.
 * @var flags_h       Horizontal filter flags (Gaussian/PCMN).
 * @var flags_v       Vertical filter flags (Gaussian/PCMN).
 * @var phase_init_h  Initial horizontal phase value.
 * @var phase_step_h  Horizontal phase step value.
 * @var phase_init_v  Initial vertical phase value.
 * @var phase_step_v  Vertical phase step value.
 * @var norm_h        Horizontal normalization factor.
 * @var ratio_h       Horizontal scaling ratio.
 * @var norm_v        Vertical normalization factor.
 * @var ratio_v       Vertical scaling ratio.
 * @var coef_hori     Horizontal blur coefficients array.
 * @var coef_vert     Vertical blur coefficients array.
 * @var dither_cfg    Dither configuration settings.
 */
struct hfi_dnsc_blur_cfg {
	u32 flags;
	u32 src_width;
	u32 src_height;
	u32 dst_width;
	u32 dst_height;
	u32 flags_h;
	u32 flags_v;
	u32 phase_init_h;
	u32 phase_step_h;
	u32 phase_init_v;
	u32 phase_step_v;
	u32 norm_h;
	u32 ratio_h;
	u32 norm_v;
	u32 ratio_v;
	u32 coef_hori[HFI_DNSC_BLUR_COEF_NUM];
	u32 coef_vert[HFI_DNSC_BLUR_COEF_NUM];
	struct hfi_display_dnsc_dither dither_cfg;
};

struct hfi_wb_device {
	struct sde_wb_device sde_wb_base;

	struct mutex hfi_lock;
	struct hfi_util_u32_prop_helper *base_props;
};

#define to_hfi_wb(x) container_of(x, struct hfi_wb_device, sde_wb_base)

int hfi_wb_display_prepare_commit(struct sde_wb_device *wb_dev,
		struct sde_connector_state *cstate);

#endif //  __HFI_WB_H__
