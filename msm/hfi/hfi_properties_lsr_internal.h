/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_PROPERTIES_LSR_INTERNAL_H__
#define __H_HFI_PROPERTIES_LSR_INTERNAL_H__

#include "hfi_defs_display.h"
#include "hfi_defs_lsr.h"

#define LSR_HFI_NUM_COLOR_FIELDS  (4)
#define LSR_HFI_NUM_VIEWS (2)

enum hfi_lsr_wb_functional_mode {
	HFI_LSR_WB_RENDER_MODE = 0,
	HFI_LSR_WB_REPROJECTION_MODE,
	HFI_LSR_WB_FUNCTIONAL_MODE_MAX,
};

enum lsr_reproj_display_config_ext_key {
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_NONE = 0,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_FUNCTIONAL_MODE - This key denotes the functional
	 * mode of the LSR WB Reprojection display. Host is expected to set this key, count and
	 * values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_FUNCTIONAL_MODE
	 *     (count) payload [2]     : 1
	 *     (values) payload [3-..] : enum hfi_lsr_wb_functional_mode
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_FUNCTIONAL_MODE,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID - This key denotes the sparse grid
	 * of the LSR WB Reprojection display. Host is expected to set this key, count
	 * and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID
	 *     (count) payload [2]     : sizeof(struct hfi_buff) / sizeof(u32)
	 *     (values) payload [3-..] : struct hfi_buff
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_RADIAL_DISTORTION_GRID - This key denotes the radial
	 * distortion grid of the LSR WB Reprojection display. Host is expected to set this key,
	 * count and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_RADIAL_DISTORTION_GRID
	 *     (count) payload [2]     : sizeof(struct hfi_buff) / sizeof(u32)
	 *     (values) payload [3-..] : struct hfi_buff
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_RADIAL_DISTORTION_GRID,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_OPTICAL_AXIS_OFFSET - This key denotes the optical
	 * axis offset of the LSR WB Reprojection display. Host is expected to set this key, count
	 * and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_OPTICAL_AXIS_OFFSET
	 *     (count) payload [2]     : sizeof(struct hfi_lsr_wb_point) *
	 *						LSR_HFI_NUM_VIEWS /sizeof(u32)
	 *     (values) payload [3-..] : struct hfi_lsr_wb_point array[LSR_HFI_NUM_VIEWS]
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_OPTICAL_AXIS_OFFSET,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_DISPLAY_GAMMA - This key denotes the gamma
	 * of the LSR WB Reprojection display. Host is expected to set this key, count
	 * and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_DISPLAY_GAMMA
	 *     (count) payload [2]     : sizeof(struct hfi_buff) / sizeof(u32)
	 *     (values) payload [3-..] : struct hfi_buff
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_DISPLAY_GAMMA,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_PANEL_TYPE - This key denotes the panel type
	 * of the LSR WB Reprojection display. Host is expected to set this key, count
	 * and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_PANEL_TYPE
	 *     (count) payload [2]     : 1
	 *     (values) payload [3-..] : enum hfi_display_lsr_wb_panel_type
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_PANEL_TYPE,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID_WIDTH - This key denotes the sparse grid
	 * width used in LSR WB Reprojection display. Host is expected to set this key, count and
	 * values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID_WIDTH
	 *     (count) payload [2]     : 1
	 *     (values) payload [3-..] : uint32_t
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID_WIDTH,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID_HEIGHT - This key denotes the sparse grid
	 * height used in LSR WB Reprojection display. Host is expected to set this key, count and
	 * values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID_HEIGHT
	 *     (count) payload [2]     : 1
	 *     (values) payload [3-..] : uint32_t
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID_HEIGHT,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_R_MAX - This key denotes the maximum radius (Rmax)
	 * used in LSR WB Reprojection display. Host is expected to set this key, count and
	 * values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_R_MAX
	 *     (count) payload [2]     : 1
	 *     (values) payload [3-..] : r32
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_R_MAX,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_DISTORT_RESOLUTION - This key denotes the distortion
	 * resolution used in LSR WB Reprojection display. Host is expected to set this key, count
	 * and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_DISTORT_RESOLUTION
	 *     (count) payload [2]     : 1
	 *     (values) payload [3-..] : uint32_t
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_DISTORT_RESOLUTION,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TO_LRGB - This key denotes the transformation to LRGB
	 * in LSR WB Reprojection display. Host is expected to set this key, count and
	 * values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TO_LRGB
	 *     (count) payload [2]     : LSR_HFI_NUM_VIEWS
	 *     (values) payload [3-..] : r32_array[LSR_HFI_NUM_VIEWS]
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TO_LRGB,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_ERROR_TO_L - This key denotes the error-to-L
	 * transformation in LSR WB Reprojection display. Host is expected to set this key, count
	 * and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_ERROR_TO_L
	 *     (count) payload [2]     : 1
	 *     (values) payload [3-..] : uint32_t
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_ERROR_TO_L,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_IMAGE_WIDTH - This key denotes the DISP image width
	 * used in LSR WB Reprojection display. Host is expected to set this key,
	 * count and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_IMAGE_WIDTH
	 *     (count) payload [2]     : 1
	 *     (values) payload [3-..] : uint32_t
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_IMAGE_WIDTH,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_IMAGE_HEIGHT - This key denotes the DISP image height
	 * used in LSR WB Reprojection display. Host is expected to set this key,
	 * count and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_IMAGE_HEIGHT
	 *     (count) payload [2]     : 1
	 *     (values) payload [3-..] : uint32_t
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_IMAGE_HEIGHT,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TILE_WIDTH - This key denotes the tile width
	 * used in LSR WB Reprojection display. Host is expected to set this key, count
	 * and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TILE_WIDTH
	 *     (count) payload [2]     : 1
	 *     (values) payload [3-..] : uint32_t
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TILE_WIDTH,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TILE_HEIGHT - This key denotes the tile height
	 * used in LSR WB Reprojection display. Host is expected to set this key, count
	 * and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TILE_HEIGHT
	 *     (count) payload [2]     : 1
	 *     (values) payload [3-..] : uint32_t
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TILE_HEIGHT,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_MIN_BBOX_WIDTH - This key denotes the minimum
	 * bounding box width used in LSR WB Reprojection display. Host is expected to set this
	 * key, count and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property
	 * payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_MIN_BBOX_WIDTH
	 *     (count) payload [2]     : 1
	 *     (values) payload [3-..] : uint32_t
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_MIN_BBOX_WIDTH,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_MIN_BBOX_HEIGHT - This key denotes the minimum
	 * bounding box height used in LSR WB Reprojection display. Host is expected to set this
	 * key, count and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property
	 * payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_MIN_BBOX_HEIGHT
	 *     (count) payload [2]     : 1
	 *     (values) payload [3-..] : uint32_t
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_MIN_BBOX_HEIGHT,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_GCX_SESSION_CONFIG - This key denotes the session
	 * configuration of the LSR WB Reprojection display. Host is expected to set this key,
	 * count and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_GCX_SESSION_CONFIG
	 *     (count) payload [2]     : sizeof(struct hfi_buff) / sizeof(u32)
	 *     (values) payload [3-..] : struct hfi_buff
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_GCX_SESSION_CONFIG,

	/*
	 * LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_GCX_SESSION_CONFIG_DATA - This key denotes the
	 * session configuration data of the LSR WB Reprojection display. Host is expected to set
	 * this key, count and values in the HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT
	 * property payload.
	 *
	 *     (key) payload [1]       : LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_GCX_SESSION_CONFIG_DATA
	 *     (count) payload [2]     : sizeof(struct hfi_buff) / sizeof(u32)
	 *     (values) payload [3-..] : struct hfi_buff
	 */
	LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_GCX_SESSION_CONFIG_DATA
};

enum lsr_x_buffer_key {
	LSR_X_BUFFER_KEY_NONE = 0,

	/*
	 * LSR_X_BUFFER_KEY_GAIN - This key denotes the gain buffer handle of the LSR WB
	 *                         Reprojection layer. Host is expected to set this key, count
	 *                         and values in the HFI_PROPERTY_LAYER_LSR_X_BUFFER
	 *                         property payload.
	 *
	 *     (key) payload [2]       : LSR_X_BUFFER_KEY_GAIN
	 *     (count) payload [3]     : number of handles [LSR_HFI_NUM_COLOR_FIELDS]
	 *     (values) payload [4-..] : struct hfi_lsr_wb_x_buff
	 */
	LSR_X_BUFFER_KEY_GAIN,

	/*
	 * LSR_X_BUFFER_KEY_MV_GRID - This key denotes the mvgrid buffer handle of the LSR WB
	 *                         Reprojection layer. Host is expected to set this key, count
	 *                         and values in the HFI_PROPERTY_LAYER_LSR_X_BUFFER
	 *                         property payload.
	 *
	 *     (key) payload [2]       : LSR_X_BUFFER_KEY_MV_GRID
	 *     (count) payload [3]     : number of handles [LSR_HFI_NUM_COLOR_FIELDS]
	 *     (values) payload [4-..] : struct hfi_lsr_wb_x_buff
	 */
	LSR_X_BUFFER_KEY_MV_GRID
};

struct hfi_lsr_wb_point {
	r32 x;
	r32 y;
};

#endif /* __H_HFI_PROPERTIES_LSR_INTERNAL_H__ */
