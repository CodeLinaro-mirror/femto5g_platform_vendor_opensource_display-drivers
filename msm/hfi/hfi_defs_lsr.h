/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_DEFS_LSR_H__
#define __H_HFI_DEFS_LSR_H__

#include <hfi_defs_common.h>

#define HFI_LSR_VIEW_ID_BIT_POS             0
#define HFI_LSR_VIEW_ID_NO_OF_BITS          1

#define HFI_LSR_COLOR_FIELD_BIT_POS        1
#define HFI_LSR_COLOR_FIELD_NO_OF_BITS     3

#define HFI_LSR_BUFFER_INDEX_BIT_POS        4
#define HFI_LSR_BUFFER_INDEX_NO_OF_BITS     2

/**
 * enum hfi_display_lsr_wb_panel_type - Panel types supported for LSR writeback.
 * @HFI_DISPLAY_LSR_WB_PANEL_TYPE_RGB : RGB panel type used for standard color rendering.
 * @HFI_DISPLAY_LSR_WB_PANEL_TYPE_FSD : Field Sequential Display panel type used for
 * time-multiplexed color rendering.
 */
enum hfi_display_lsr_wb_panel_type {
	HFI_DISPLAY_LSR_WB_PANEL_TYPE_RGB = 0,
	HFI_DISPLAY_LSR_WB_PANEL_TYPE_FSD = 1
};

/**
 * enum hfi_lsr_layer_type - App local/remote layer type hint.
 * @HFI_LSR_LAYER_LOCAL  : Local layer.
 * @HFI_LSR_LAYER_REMOTE : Remote layer.
 */
enum hfi_lsr_layer_type {
	HFI_LSR_LAYER_LOCAL = 0,
	HFI_LSR_LAYER_REMOTE
};

/**
 * enum hfi_lsr_layer_lock_type - App layer lock type
 * @HFI_LSR_LAYER_LOCK_WORLD_LOCK  : World locked layer.
 * @HFI_LSR_LAYER_LOCK_HEAD_LOCK   : Head locked layer.
 * @HFI_LSR_LAYER_LOCK_SPHERE_LOCK : Sphere locked layer.
 */
enum hfi_lsr_layer_lock_type {
	HFI_LSR_LAYER_LOCK_WORLD_LOCK = 0,
	HFI_LSR_LAYER_LOCK_HEAD_LOCK,
	HFI_LSR_LAYER_LOCK_SPHERE_LOCK
};

/**
 * enum hfi_layer_gamma_type - Layer Gamma type
 * @HFI_LAYER_GAMMA_NONE     : None
 * @HFI_LAYER_GAMMA_1_0      : 1.0
 * @HFI_LAYER_GAMMA_2_2      : 2.2
 * @HFI_LAYER_GAMMA_2_6      : 2.6
 * @HFI_LAYER_GAMMA_REC_601  : Rec. 601
 * @HFI_LAYER_GAMMA_REC_709  : Rec. 709
 * @HFI_LAYER_GAMMA_REC_SRGB : SRGB
 */
enum hfi_layer_gamma_type {
	HFI_LAYER_GAMMA_NONE = 0,
	HFI_LAYER_GAMMA_1_0,
	HFI_LAYER_GAMMA_2_2,
	HFI_LAYER_GAMMA_2_6,
	HFI_LAYER_GAMMA_REC_601,
	HFI_LAYER_GAMMA_REC_709,
	HFI_LAYER_GAMMA_REC_SRGB
};

/**
 * enum hfi_lsr_view_id - Enumerates the possible view IDs for HFI LSR.
 * @HFI_LSR_VIEW_ID_LEFT	: Left view
 * @HFI_LSR_VIEW_ID_RIGHT	: Right view
 */
enum hfi_lsr_view_id {
	HFI_LSR_VIEW_ID_LEFT = 0,
	HFI_LSR_VIEW_ID_RIGHT
};

/**
 * @enum hfi_lsr_color_fields - Enumerates the possible color fields for HFI LSR.
 * @HFI_LSR_COLOR_FIELD_RGB	: RGB color field.
 * @HFI_LSR_COLOR_FIELD_R	: Red color field.
 * @HFI_LSR_COLOR_FIELD_G	: Green color field.
 * @HFI_LSR_COLOR_FIELD_B	: Blue color field.
 * @HFI_LSR_COLOR_FIELD_ALPHA : Alpha color field.
 */
enum hfi_lsr_color_fields {
	HFI_LSR_COLOR_FIELD_RGB,
	HFI_LSR_COLOR_FIELD_R,
	HFI_LSR_COLOR_FIELD_G,
	HFI_LSR_COLOR_FIELD_B,
	HFI_LSR_COLOR_FIELD_ALPHA,
	HFI_LSR_COLOR_FIELD_MAX = 8
};

/**
 * @enum hfi_lsr_buffer_index - Enumerates the possible buffer indices for HFI LSR.
 * @HFI_LSR_BUFFER_INDEX_0	: Buffer index 0.
 * @HFI_LSR_BUFFER_INDEX_1	: Buffer index 1.
 */
enum hfi_lsr_buffer_index {
	HFI_LSR_BUFFER_INDEX_0,
	HFI_LSR_BUFFER_INDEX_1,
	HFI_LSR_BUFFER_INDEX_MAX = 4
};

/*
 * @enum hfi_lsr_reusable_fence_type - Enumerates the possible reusable fence type for HFI LSR
 * @HFI_LSR_REUSABLE_FENCE_GCX_OUT_BUFFERS : Fence for GCX output buffers.
 * @HFI_LSR_REUSABLE_FENCE_INVALIDATE_GFX_IN_BUFFERS : Fence to invalidate GFX input buffers.
 */
enum hfi_lsr_reusable_fence_type {
	HFI_LSR_REUSABLE_FENCE_GCX_OUT_BUFFERS,
	HFI_LSR_REUSABLE_FENCE_INVALIDATE_GFX_IN_BUFFERS,
};

/**
 * struct hfi_lsr_position - Represents a position in 3D space.
 * @x	:  x position in space.
 * @y	:  y position in space.
 * @z	:  z position in space.
 */
struct hfi_lsr_position {
	r32 x;
	r32 y;
	r32 z;
};

/**
 *  struct hfi_lsr_orientation - Represents an orientation in 3D space.
 * @x :  x orientation in space.
 * @y :  y orientation in space.
 * @z :  z orientation in space.
 * @w :  w orientation in space.
 */
struct hfi_lsr_orientation {
	r32 x;
	r32 y;
	r32 z;
	r32 w;
};

/**
 * struct hfi_lsr_render_pose - Represents a render pose with position and orientation.
 * @position	: Position in 3D space.
 * @orientation	: Orientation in 3D space.
 */
struct hfi_lsr_render_pose {
	struct hfi_lsr_position position;
	struct hfi_lsr_orientation orientation;
};

/**
 * struct hfi_lsr_reprojection_pose - Represents raster scan correction
 * In case of FSD display, start, mid and end poses will be repurposed as R, G and B poses.
 * @startPose : Start pose or Red pose
 * @midPose	  : Mid pose or Green pose.
 * @endPose	  : End pose or Blue pose.
 */
struct hfi_lsr_reprojection_pose {
	struct hfi_lsr_render_pose startPose;
	struct hfi_lsr_render_pose midPose;
	struct hfi_lsr_render_pose endPose;
};

/**
 * struct hfi_lsr_render_frustum - Render Frustum angles in radians.
 * @angle_left  : Angle of the left side of the field of view in radians.
 * @angle_right : Angle of the right side of the field of view in radians.
 * @angle_up    : Angle of the top part of the field of view in radians.
 * @angle_down  : Angle of the bottom part of the field of view in radians.
 */
struct hfi_lsr_render_frustum {
	r32 angle_left;
	r32 angle_right;
	r32 angle_up;
	r32 angle_down;
};

/**
 * struct hfi_lsr_plane_equation - Layer's 3D plane equation of the form ax + by + cz + d = 0.
 * @a :  Constant a of the general form of the equation of a plane.
 * @b : Constant b of the general form of the equation of a plane.
 * @c : Constant c of the general form of the equation of a plane.
 * @d : Constant d of the general form of the equation of a plane.
 */
struct hfi_lsr_plane_equation {
	r32 a;
	r32 b;
	r32 c;
	r32 d;
};

/*
 * struct hfi_lsr_reproj_matrix - LSR Reprojection Matrix.
 * @reproj_matrix	: 4*4 matrix used for reprojection.
 */
struct hfi_lsr_reproj_matrix {
	r32 reproj_matrix[4][4];
};

/*
 * struct hfi_lsr_wb_x_buff - Extended writeback buffer information for LSR.
 * @var width	: Width of the buffer in pixels.
 * @var height	: Height of the buffer in pixels.
 * @var stride	: Number of bytes per row of the buffer in pixels.
 * @var buffer	: HFI buffer structure See @struct hfi_buff for details.
 */
struct hfi_lsr_wb_x_buff {
	u32 width;
	u32 height;
	u32 stride;
	struct hfi_buff buffer;
};

#endif /* __H_HFI_DEFS_LSR_H__ */
