/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_DEFS_DISPLAY_COLOR_H__
#define __H_HFI_DEFS_DISPLAY_COLOR_H__

#include "hfi_defs_common.h"

typedef signed int s32;

/**
 * @brief Enable the feature.
 */
#define HFI_DISPLAY_COLOR_FLAGS_FEATURE_ENABLE         (1 << 0)

/**
 * @brief Broadcast the feature to allocated HW blocks.
 */
#define HFI_DISPLAY_COLOR_FLAGS_FEATURE_BROADCAST      (1 << 1)

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
#define HFI_DISPLAY_COLOR_HW_BLK_INDEX_0_FLAG    (1 << 2)
#define HFI_DISPLAY_COLOR_HW_BLK_INDEX_1_FLAG    (1 << 3)
#define HFI_DISPLAY_COLOR_HW_BLK_INDEX_2_FLAG    (1 << 4)
#define HFI_DISPLAY_COLOR_HW_BLK_INDEX_3_FLAG    (1 << 5)

#define HFI_DITHER_MATRIX_SZ                16
#define HFI_DITHER_LUMA_MODE                (1 << 0)
#define HFI_DITHER_OFFSET_ENABLE            (1 << 1)

#define HFI_DITHER_MATRIX_SZ_EXTENDED       256
#define HFI_DITHER_MATRIX_SELECT_4_4        0
#define HFI_DITHER_MATRIX_SELECT_6_6        1
#define HFI_DITHER_MATRIX_SELECT_8_8        2
#define HFI_DITHER_MATRIX_SELECT_16_16      3

/**
 * struct hfi_display_dither - dither feature structure for SPR and PPB
 * @flags: flags representing enable and broadcast setting
 * @feature_flags: flags for the feature customization, values can be:
			-HFI_DITHER_LUMA_MODE: Enable LUMA dither mode
			-HFI_DITHER_OFFSET_ENABLE: Enable DC offset
 * @temporal_en: temporal dither enable
 * @c0_bitdepth: c0 component bit depth
 * @c1_bitdepth: c1 component bit depth
 * @c2_bitdepth: c2 component bit depth
 * @c3_bitdepth: c3 component bit depth
 * @dither_matrix_select: represents dither matrix size
			-DITHER_MATRIX_SELECT_4_4: new dither matrix of size 4x4
			-DITHER_MATRIX_SELECT_6_6: new dither matrix of size 6x6
			-DITHER_MATRIX_SELECT_8_8: new dither matrix of size 8x8
			-DITHER_MATRIX_SELECT_16_16: new dither matrix of size 16x16
 * @matrix: dither strength matrix
 */
struct hfi_display_dither {
	u32 flags;
	u32 feature_flags;
	u32 temporal_en;
	u32 c0_bitdepth;
	u32 c1_bitdepth;
	u32 c2_bitdepth;
	u32 c3_bitdepth;
	u32 dither_matrix_select;
	s32 matrix[HFI_DITHER_MATRIX_SZ_EXTENDED];
};

/**
 * struct hfi_display_pa_dither - dspp PA dither feature structure
 * @flags: for customizing operations
 * @strength: dither strength
 * @offset_en: offset enable bit
 * @matrix: dither data matrix
 */
struct hfi_display_pa_dither {
	u32 flags;
	u32 strength;
	u32 offset_en;
	u32 matrix[HFI_DITHER_MATRIX_SZ];
};

#define HFI_QRTC_LUT_SIZE 81        /* Each LUT is 9x9 */
#define HFI_QRTC_NUM_LUTS 3         /* Number of LPF/HPF tables */

/* Subsample Mode Definitions */
#define HFI_QRTC_SUBSAMPLE_1x1 0x0  /* Horizontal: 1, Vertical: 1 */
#define HFI_QRTC_SUBSAMPLE_2x1 0x1  /* Horizontal: 2, Vertical: 1 */
#define HFI_QRTC_SUBSAMPLE_2x2 0x2  /* Horizontal: 2, Vertical: 2 */
#define HFI_QRTC_SUBSAMPLE_3x3 0x3  /* Horizontal: 3, Vertical: 3 */

/* DMA Selection Definitions */
#define HFI_QRTC_DMA_1 0x1
#define HFI_QRTC_DMA_3 0x3

/* Rectangle Selection Definitions */
#define HFI_QRTC_RECT0 0x0  /* Select Rectangle 0 */
#define HFI_QRTC_RECT1 0x1  /* Select Rectangle 1 */
#define HFI_WRITE_BACK_0 0x0
#define HFI_WRITE_BACK_1 0x1
#define HFI_WRITE_BACK_2 0x2
#define HFI_CWB_BLOCK_0 0x0
#define HFI_CWB_BLOCK_1 0x1
#define HFI_CWB_BLOCK_2 0x2
#define HFI_CWB_BLOCK_3 0x3
#define HFI_CWB_BLOCK_4 0x4
#define HFI_CWB_BLOCK_5 0x5
#define HFI_CWB_BLOCK_6 0x6
#define HFI_CWB_BLOCK_7 0x7

/*!
 * @struct hfi_qrtc_config
 * @brief QRTC configuration structure.
 *
 * @var flags
 *   Configuration flags.
 * @var lut_dma_flags
 *   Flags for LUT dma packet needed for HPF/LPF tables. Flag macros are
 *   defined in hfi_defs_common.h including: enable, broadcast, and block select flags.
 * @var iova_l
 *   Lower 32 bits of IOVA address.
 * @var iova_h
 *   Upper 32 bits of IOVA address.
 * @var len
 *   Length of the data.
 * @var coring_en
 *   Enable/disable global coring (0 = disable, 1 = enable).
 *   Global coring parameters in U10 format (0-1023).
 * @var coring_pos
 *   Positive coring value.
 * @var coring_neg
 *   Negative coring value.
 * @var lpf_en
 *   Enable/disable LPF (0 = disable, 1 = enable).
 * @var subsample_mode
 *   Subsampling mode (use HFI_QRTC_SUBSAMPLE_* macros).
 * @var dma_sel
 *   DMA selection (use HFI_QRTC_DMA_* macros).
 * @var rect_sel
 *   Rectangle selection (use HFI_QRTC_RECT0 or HFI_QRTC_RECT1).
 * @var wb_sel
 *   Write back selection.
 * @var cwb_sel
 *   CWB selection.
 * @var cwb_tap_point
 *   CWB tap point selection.
 * @var cwb_iova_l
 *   CWB buffer capture information - lower IOVA.
 * @var cwb_iova_h
 *   CWB buffer capture information - upper IOVA.
 * @var cwb_len
 *   CWB buffer length.
 * @var cwb_height
 *   CWB buffer height.
 * @var cwb_width
 *   CWB buffer width.
 * @var format
 *   Buffer format.
 */
struct hfi_qrtc_config {
	u32 flags;
	u32 lut_dma_flags;
	u32 iova_l;
	u32 iova_h;
	u32 len;
	u32 coring_en;
	u32 coring_pos;
	u32 coring_neg;
	u32 lpf_en;
	u32 subsample_mode;
	u32 dma_sel;
	u32 rect_sel;
	u32 wb_sel;
	u32 cwb_sel;
	u32 cwb_tap_point;
	u32 cwb_iova_l;
	u32 cwb_iova_h;
	u32 cwb_len;
	u32 cwb_height;
	u32 cwb_width;
	u32 format;
};

/**
 * @brief LTM stats data sizes
 *
 * @details
 * - HFI_LTM_DATA_SIZE_0: LTM data size for stats 0
 * - HFI_LTM_DATA_SIZE_1: LTM data size for stats 1
 * - HFI_LTM_DATA_SIZE_2: LTM data size for stats 2
 * - HFI_LTM_BLOCK_MAX: Maximum number of LTM blocks
 */
#define HFI_LTM_DATA_SIZE_0        32
#define HFI_LTM_DATA_SIZE_1        128
#define HFI_LTM_DATA_SIZE_2        256
#define HFI_LTM_BLOCK_MAX          4

/**
 * @brief LTM stats status_flag bitmask used in hfi_display_ltm_stats_data
 *
 * @details
 * - HFI_LTM_STATS_SAT: LTM stats saturation
 * - HFI_LTM_STATS_MERGE_SAT: LTM stats merge saturation
 */
#define HFI_LTM_STATS_SAT          (1 << 1)
#define HFI_LTM_STATS_MERGE_SAT    (1 << 2)

#define HFI_LTM_HIST_DONE                   (1 << 0)
#define HFI_LTM_WB_PB                       (1 << 1)
#define HFI_LTM_HIST_OFF                    (1 << 2)

/**
 * struct hfi_display_ltm_init_param - LTM init params structure
 * @flags: for customizing operations
 * @init_param_01: init param 1
 * @init_param_02: init param 2
 * @init_param_03: init param 3
 * @init_param_04: init param 4
 */
struct hfi_display_ltm_init_param {
	u32 flags;
	u32 init_param_01;
	u32 init_param_02;
	u32 init_param_03;
	u32 init_param_04;
};

/**
 * struct hfi_display_ltm_cfg_param - LTM cfg params structure
 * @flags: for customizing operations
 * @cfg_param_01: cfg param 1
 * @cfg_param_02: cfg param 2
 * @cfg_param_03: cfg param 3
 * @cfg_param_04: cfg param 4
 * @cfg_param_05: cfg param 5
 * @cfg_param_06: cfg param 6
 */
struct hfi_display_ltm_cfg_param {
	u32 flags;
	u32 cfg_param_01;
	u32 cfg_param_02;
	u32 cfg_param_03;
	u32 cfg_param_04;
	u32 cfg_param_05;
	u32 cfg_param_06;
};

/**
 * struct hfi_display_ltm_buffer - LTM buffer structure.
 * This struct is maintained in firmware and will be passed between HLOS driver and DCP firmware
 * for sharing LTM stats data.
 * @flags: for customizing operations
 * @dpu_iova: dpu virtual address of the buffer
 * @dcp_addr: dcp address of the buffer
 * @size: size of hfi_display_ltm_stats_data
 */
struct hfi_display_ltm_buffer {
	u32 flags;
	u32 dpu_iova_l;
	u32 dpu_iova_h;
	u32 dcp_addr_l;
	u32 dcp_addr_h;
	u32 size;
};

/**
 * struct hfi_display_ltm_stats_data - LTM stats data structure.
 */
struct hfi_display_ltm_stats_data {
	u32 stats_01[HFI_LTM_DATA_SIZE_0][HFI_LTM_DATA_SIZE_1];
	u32 stats_02[HFI_LTM_DATA_SIZE_2];
	u32 stats_03[HFI_LTM_DATA_SIZE_0];
	u32 stats_04[HFI_LTM_DATA_SIZE_0];
	u32 stats_05[HFI_LTM_DATA_SIZE_0];
	u32 status_flag;
	u32 display_h;
	u32 display_v;
	u32 init_h[HFI_LTM_BLOCK_MAX];
	u32 init_v;
	u32 inc_h;
	u32 inc_v;
	u32 portrait_en;
	u32 merge_en;
	u32 cfg_param_01;
	u32 cfg_param_02;
	u32 cfg_param_03;
	u32 cfg_param_04;
	u32 feature_flag;
	u32 checksum;
};

/*!
 * @struct hfi_display_ltm_event_resp
 * @brief LTM event struct. This structure will be used to send the DCP address of stats buffer
 * which must match the DCP address of one of the LTM buffers in circulation.
 *
 * @event_type: type of LTM event i.e. HIST_DONE, WB_PB, HIST_OFF
 * @dcp_addr_h: higher value of 64bit dcp address of LTM stats buffer
 * @dcp_addr_l: lower value of 64bit dcp address of LTM stats buffer
 */
struct hfi_display_ltm_event_resp {
	u32 event_type;
	u32 dcp_addr_h;
	u32 dcp_addr_l;
};

#endif // __H_HFI_DEFS_DISPLAY_COLOR_H__
