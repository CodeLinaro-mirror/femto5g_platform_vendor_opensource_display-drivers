/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_DEFS_DISPLAY_COLOR_H__
#define __H_HFI_DEFS_DISPLAY_COLOR_H__

#include "hfi_defs_common.h"

/**
 * @brief Enable the feature.
 */
#define HFI_DISPLAY_COLOR_FLAGS_CMN_FEATURE_ENABLE         (1 << 0)

/**
 * @brief Broadcast the feature to allocated HW blocks.
 */
#define HFI_DISPLAY_COLOR_FLAGS_CMN_FEATURE_BROADCAST      (1 << 1)

/**
 * @brief If HFI_DISPLAY_FLAGS_COLOR_FEATURE_BROADCAST is not set,
 * then the feature is applied to the HW blocks specified by the HW block index flags.
 *
 * @details
 * - HFI_DISPLAY_COLOR_HW_BLK_INDEX_0_FLAG: HW block index 0
 * - HFI_DISPLAY_COLOR_HW_BLK_INDEX_1_FLAG: HW block index 1
 * - HFI_DISPLAY_COLOR_HW_BLK_INDEX_2_FLAG: HW block index 2
 * - HFI_DISPLAY_COLOR_HW_BLK_INDEX_3_FLAG: HW block index 3
 */
#define HFI_DISPLAY_COLOR_FLAGS_CMN_HW_BLK_INDEX_0_FLAG    (1 << 2)
#define HFI_DISPLAY_COLOR_FLAGS_CMN_HW_BLK_INDEX_1_FLAG    (1 << 3)
#define HFI_DISPLAY_COLOR_FLAGS_CMN_HW_BLK_INDEX_2_FLAG    (1 << 4)
#define HFI_DISPLAY_COLOR_FLAGS_CMN_HW_BLK_INDEX_3_FLAG    (1 << 5)

#define HFI_DITHER_MATRIX_SZ                16
#define HFI_DITHER_ENABLE                   (1 << 0)
#define HFI_DITHER_LUMA_MODE                (1 << 1)
#define HFI_DITHER_OFFSET_ENABLE            (1 << 2)

#define HFI_DITHER_MATRIX_SZ_EXTENDED       256
#define HFI_DITHER_MATRIX_SELECT_4_4        0
#define HFI_DITHER_MATRIX_SELECT_6_6        1
#define HFI_DITHER_MATRIX_SELECT_8_8        2
#define HFI_DITHER_MATRIX_SELECT_16_16      3

/**
 * struct hfi_display_dither - dither feature structure
 * @flags: flags for the feature customization, values can be:
			-HFI_DITHER_LUMA_MODE: Enable LUMA dither mode
			-HFI_DITHER_OFFSET_ENABLE: Enable DC offset
 * @temporal_en: temporal dither enable
 * @c0_bitdepth: c0 component bit depth
 * @c1_bitdepth: c1 component bit depth
 * @c2_bitdepth: c2 component bit depth
 * @c3_bitdepth: c2 component bit depth
 * @dither_matrix_select: represents dither matrix size
			-DITHER_MATRIX_SELECT_4_4: new dither matrix of size 4x4
			-DITHER_MATRIX_SELECT_6_6: new dither matrix of size 6x6
			-DITHER_MATRIX_SELECT_8_8: new dither matrix of size 8x8
			-DITHER_MATRIX_SELECT_16_16: new dither matrix of size 16x16
 * @matrix: dither strength matrix
 */
struct hfi_display_dither {
	u32 flags;
	u32 temporal_en;
	u32 c0_bitdepth;
	u32 c1_bitdepth;
	u32 c2_bitdepth;
	u32 c3_bitdepth;
	u32 dither_matrix_select;
	u32 matrix[HFI_DITHER_MATRIX_SZ_EXTENDED];
};

#endif // __H_HFI_DEFS_DISPLAY_COLOR_H__
