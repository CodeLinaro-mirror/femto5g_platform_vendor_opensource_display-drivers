/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef __VIRTIO_EXT_H__
#define __VIRTIO_EXT_H__

#include <linux/virtio_gpu.h>

#pragma pack(push, 1)
#define VIRTIO_GPU_MAX_MODIFIERS 4
#define VIRTIO_GPU_MAX_EVENTS 4
#define VIRTIO_GPU_MAX_PIXEL_FORMATS 32
#define VIRTIO_GPU_MAX_PLANES 16
#define VIRTIO_GPU_MAX_MODES 8
#define VIRTIO_GPU_MAX_VIRQ 2
#define VIRTIO_GPU_MAX_SCANOUT_HW_BLOB_SIZE 512

#define VIRTIO_GPU_PORT_DSI 1
#define VIRTIO_GPU_PORT_DP 2
#define VIRTIO_GPU_PORT_VIRTUAL 3

enum virtio_gpu_ctrl_type_ext {
	VIRTIO_GPU_CMD_GET_DISPLAY_INFO_EXT = 0x904,
	VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING_EXT,
	VIRTIO_GPU_CMD_GET_SCANOUT_ATTRIBUTES,
	VIRTIO_GPU_CMD_SET_SCANOUT_PROPERTIES,
	VIRTIO_GPU_CMD_GET_SCANOUT_PLANES,
	VIRTIO_GPU_CMD_PLANE_CREATE,
	VIRTIO_GPU_CMD_GET_PLANES_CAPS,
	VIRTIO_GPU_CMD_GET_PLANE_PROPERTIES,
	VIRTIO_GPU_CMD_PLANE_DESTROY,
	VIRTIO_GPU_CMD_SET_PLANE,
	VIRTIO_GPU_CMD_SET_PLANE_PROPERTIES,
	VIRTIO_GPU_CMD_SET_RESOURCE_INFO,
	VIRTIO_GPU_CMD_WAIT_FOR_VSYNC,
	VIRTIO_GPU_CMD_SET_PLANE_HDR,
	VIRTIO_GPU_CMD_SET_PIC_ADJUST,
	VIRTIO_GPU_CMD_PLANE_FLUSH,
	VIRTIO_GPU_CMD_SCANOUT_FLUSH,
	VIRTIO_GPU_CMD_FULL_FLUSH,
	VIRTIO_GPU_CMD_EVENT_CONTROL,
	VIRTIO_GPU_CMD_WAIT_EVENTS,
	VIRTIO_GPU_CMD_GET_DEVICE_INFO,
	VIRTIO_GPU_CMD_GET_DEVICE_HW_ATTRIBUTES,
	VIRTIO_GPU_CMD_GET_SCANOUT_HW_ATTRIBUTES,
	VIRTIO_GPU_CMD_GET_PLANE_HW_ATTRIBUTES,
	VIRTIO_GPU_CMD_ENABLE_VIRQ,
	VIRTIO_GPU_CMD_DISABLE_VIRQ,
	VIRTIO_GPU_CMD_SET_POWER,

	VIRTIO_GPU_RESP_EXTENTION_START = 0x1300,
	VIRTIO_GPU_RESP_ERR_UNSUPPORTED_COMMAND,
	VIRTIO_GPU_RESP_ERR_BACKING_SWAP_NOT_SUPPORTED,
	VIRTIO_GPU_RESP_ERR_BACKING_IN_USE,
	VIRTIO_GPU_RESP_OK_DISPLAY_INFO_EXT,
	VIRTIO_GPU_RESP_OK_SCANOUT_ATTRIBUTES,
	VIRTIO_GPU_RESP_OK_SET_SCANOUT_PROPERTIES,
	VIRTIO_GPU_RESP_OK_GET_SCANOUT_PLANES,
	VIRTIO_GPU_RESP_OK_GET_PLANES_CAPS,
	VIRTIO_GPU_RESP_OK_PLANE_CREATE,
	VIRTIO_GPU_RESP_OK_PLANE_DESTROY,
	VIRTIO_GPU_RESP_OK_GET_PLANE_PROPERTIES,
	VIRTIO_GPU_RESP_OK_SET_PLANE_PROPERTIES,
	VIRTIO_GPU_RESP_OK_SET_PLANE,
	VIRTIO_GPU_RESP_OK_SCANOUT_FLUSH,
	VIRTIO_GPU_RESP_OK_PLANE_FLUSH,
	VIRTIO_GPU_RESP_OK_FULL_FLUSH,
	VIRTIO_GPU_RESP_OK_WAIT_FOR_EVENTS,
	VIRTIO_GPU_RESP_OK_SET_PIC_ADJUST,
	VIRTIO_GPU_RESP_OK_DEVICE_INFO,
	VIRTIO_GPU_RESP_OK_DEVICE_HW_ATTRIBUTES,
	VIRTIO_GPU_RESP_OK_SCANOUT_HW_ATTRIBUTES,
	VIRTIO_GPU_RESP_OK_PLANE_HW_ATTRIBUTES,
	VIRTIO_GPU_RESP_OK_ENABLE_VIRQ,
	VIRTIO_GPU_RESP_OK_DISABLE_VIRQ,
	VIRTIO_GPU_RESP_OK_SET_POWER,
	VIRTIO_GPU_RESP_EXTENTION_END
};

enum virtio_gpu_formats_ext {
	VIRTIO_GPU_FORMAT_BYTE = VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM + 1,
	VIRTIO_GPU_FORMAT_B4G4R4A4,
	VIRTIO_GPU_FORMAT_B4G4R4X4,
	VIRTIO_GPU_FORMAT_B5G5R5A1,
	VIRTIO_GPU_FORMAT_B5G5R5X1,
	VIRTIO_GPU_FORMAT_B10G10R10A2,
	VIRTIO_GPU_FORMAT_B10G10R10X2,
	VIRTIO_GPU_FORMAT_R10G10B10X2,
	VIRTIO_GPU_FORMAT_R10G10B10A2,
	VIRTIO_GPU_FORMAT_B5G6R5,
	VIRTIO_GPU_FORMAT_R5G6B5,
	VIRTIO_GPU_FORMAT_B8G8R8,
	VIRTIO_GPU_FORMAT_R8G8B8,
	VIRTIO_GPU_FORMAT_YVU410,
	VIRTIO_GPU_FORMAT_YUV420,
	VIRTIO_GPU_FORMAT_NV12,
	VIRTIO_GPU_FORMAT_P010,
	VIRTIO_GPU_FORMAT_NV12_QC_TP10,
	VIRTIO_GPU_FORMAT_YVU420,
	VIRTIO_GPU_FORMAT_UYVY,
	VIRTIO_GPU_FORMAT_YVYU,
	VIRTIO_GPU_FORMAT_YUYV,
	VIRTIO_GPU_FORMAT_VYUY,
	VIRTIO_GPU_FORMAT_AYUV
};

enum display_event_types {
	VIRTIO_VSYNC,
	VIRTIO_COMMIT_COMPLETE,
	VIRTIO_HPD,
	VIRTIO_DISPLAY_RESET,
	VIRTIO_VQ_RESET,
	VIRTIO_MAX_EVENT
};

enum display_reset_status {
	VIRTIO_DISPLAY_RESET_IDLE,
	VIRTIO_DISPLAY_RESET_BEGIN,
	VIRTIO_DISPLAY_RESET_END,
	VIRTIO_DISPLAY_RESET_STATUS_MAX
};

enum lutdma_vq_reset_status {
	VIRTIO_VQ_RESET_IDLE,
	VIRTIO_VQ_RESET_BEGIN,
	VIRTIO_VQ_RESET_END,
	VIRTIO_VQ_RESET_STATUS_MAX,
};

enum display_port_type {
	VIRTIO_PORT_TYPE_INTERNAL,
	VIRTIO_PORT_TYPE_HDMI,
	VIRTIO_PORT_TYPE_DSI,
	VIRTIO_PORT_TYPE_DP,
	VIRTIO_PORT_TYPE_MAX
};

enum
{
	VIRTIO_SCANOUT_POWER_MODE_OFF           = 0x7680,
	VIRTIO_SCANOUT_POWER_MODE_SUSPEND       = 0x7681,
	VIRTIO_SCANOUT_POWER_MODE_LIMITED_USE   = 0x7682,
	VIRTIO_SCANOUT_POWER_MODE_ON            = 0x7683,
	VIRTIO_SCANOUT_POWER_MODE_PRE_DISABLE   = 0x7684,
	VIRTIO_SCANOUT_POWER_MODE_PRE_ENABLE    = 0x7685,
	VIRTIO_SCANOUT_POWER_MODE_FORCE_32BIT   = 0x7FFFFFFF
};

enum source_pipe_id_type {
	SOURCE_PIPE_TYPE_NONE 		= 0,
	SOURCE_PIPE_TYPE_VIDEO,
	SOURCE_PIPE_TYPE_DMA,
	SOURCE_PIPE_TYPE_GRAPHICS,
	SOURCE_PIPE_TYPE_CURSOR,
	SOURCE_PIPE_TYPE_MAX,
	SOURCE_PIPE_TYPE_MASK = 0xFFFF0000,
	SOURCE_PIPE_TYPE_SHIFT = 16,

	SOURCE_PIPE_INDEX_MASK = 0x0000FFFF,
	SOURCE_PIPE_INDEX_SHIFT = 0,
};

enum {
	VIRTIO_DEVICE_POWER_OFF		= 0,
	VIRTIO_DEVICE_POWER_ON,
	VIRTIO_DEVICE_POWER_MAX,
};

struct virtio_gpu_get_display_info_ext {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 padding;
};

struct virtio_gpu_resp_display_info_ext {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	struct virtio_gpu_display_ext {
		struct virtio_gpu_rect r;
		__le32 refresh;
		__le32 enabled;
		__le32 flags;
	} pmodes[VIRTIO_GPU_MAX_MODES];
	__le32 padding;
};

struct virtio_gpu_resp_device_info {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_device_info {
		__le32 qseed_type;
		__le32 max_mdp_clk;
		__le32 has_src_split;
		__le32 device_version;
	} device_info;
	__le32 padding;
};

struct virtio_gpu_get_scanout_attributes {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 padding;
};

struct virtio_gpu_resp_scanout_atttributes {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 type;
	__le32 connection_status;
	__le32 width_mm;
	__le32 height_mm;
	__le32 panel_orientation;
	__le32 panel_colorspace;
	__le32 hdr_max_luminance;
	__le32 hdr_avg_luminance;
	__le32 hdr_min_luminance;
	__le32 avr_supported;
	__le32 avr_min_fps;
	__le32 avr_step;
	__le32 padding;
};

struct virtio_gpu_get_scanout_planes {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 padding;
};

struct virtio_gpu_resp_scanout_planes {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 num_planes;
	__le32 plane_ids[VIRTIO_GPU_MAX_PLANES];
	__le32 padding;
};

struct virtio_gpu_get_planes_caps {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
	__le32 padding;
};

struct virtio_gpu_resp_planes_caps {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_plane_caps {
		__le32 scanout_id;
		__le32 plane_id;
		__le32 plane_type;
		__le32 max_width;
		__le32 max_height;
		__le32 num_formats;
		__le32 formats[VIRTIO_GPU_MAX_PIXEL_FORMATS];
		__le32 max_scale;
		__le32 min_scale;
		__le32 pair_plane_id;
		__le32 support_rotation;
	}caps;
	__le32 padding;
};

struct virtio_gpu_set_scanout_properties {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 power_mode;
	__le32 mode_index;
	__le32 rotation;
	struct virtio_gpu_rect r; //dest_rect
	__le32 color_space;
	__le32 avr_trigger;
	__le32 padding;
};

struct virtio_gpu_set_scanout_properties_dup {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 power_mode;
	__le32 mode_index;
	__le32 rotation;
	struct virtio_gpu_rect r; //dest_rect
	__le32 padding;
};

struct virtio_gpu_resp_scanout_properties {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 error_code;
	__le32 padding;
};

struct virtio_gpu_set_plane {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect r;
	__le32 scanout_id;
	__le32 plane_id;
	__le32 resource_id;
	__le32 padding;
};

struct virtio_gpu_resp_set_plane {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
	__le32 error_code;
	__le32 padding;
};

struct virtio_gpu_create_plane {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
	__le32 padding;
};

struct virtio_gpu_resp_plane_create {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
	__le32 error_code;
	__le32 padding;
};

struct virtio_gpu_plane_destroy {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
	__le32 padding;
};

struct virtio_gpu_resp_plane_destroy {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
	__le32 error_code;
	__le32 padding;
};

enum plane_property_mask {
	Z_ORDER = (1<<0),
	GLOBAL_ALPHA = (1<<1),
	BLEND_MODE = (1<<2),
	SRC_RECT = (1<<3),
	DST_RECT = (1<<4),
	COLOR_SPACE = (1<<5),
	COLORIMETRY = (1<<6),
	COLOR_RANGE = (1<<7),
	HUE = (1<<8),
	SATURATION = (1<<9),
	CONTRAST = (1<<10),
	BRIGHTNESS = (1<<11),
	ROTATION = (1<<12),
};

struct virtio_gpu_set_plane_properties {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
	__le64 mask;
	__le32 z_order;
	__le32 global_alpha;
	__le32 blend_mode;
	struct virtio_gpu_rect src_rect;
	struct virtio_gpu_rect dst_rect;
	__le32 color_space;
	__le32 colorimetry;
	__le32 color_range;
	__le32 hue;
	__le32 saturation;
	__le32 contrast;
	__le32 brightness;
	__le32 rotation;
	__le32 padding;
};

struct virtio_gpu_resp_plane_properties {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
	__le32 error_code;
	__le32 padding;
};

struct virtio_gpu_get_plane_properties {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
	__le32 padding;
};

struct virtio_gpu_resp_get_plane_properties {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
	__le32 zorder;
	__le32 padding;
};

struct virtio_gpu_set_plane_hdr {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
//	u8 hdr_metadata[…];
	__le32 padding;
};

struct virtio_gpu_plane_flush {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
	__le32 async_mode;
	__le32 padding;
};

struct virtio_gpu_resp_plane_flush {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
	__le32 error_code;
	__le32 padding;
};

struct virtio_gpu_scanout_flush {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 async_mode;
	__le32 padding;
};

struct virtio_gpu_resp_scanout_flush {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 error_code;
	__le32 padding;
};

struct virtio_gpu_full_flush {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 async_mode;
	__le32 padding;
};

struct virtio_gpu_resp_full_flush {
	struct virtio_gpu_ctrl_hdr hdr;
	 __le32 error_code;
	 __le32 padding;
};

struct virtio_gpu_event_control {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 event_type;
	__le32 enable;
	__le32 padding;
};

struct virtio_gpu_wait_events {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 max_num_events;
	__le32 padding;
};

struct virtio_gpu_resp_event {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_event_count_one {
		__le32 enabled;
		__le32 vsync_count;
		__le32 commit_count;
		__le32 hpd_count;
		/* temporarily removed for backward compatibility */
		/*
		__le32 display_reset_status;
		__le32 lutdma_vq_reset_status;
		*/
	}scanout[VIRTIO_GPU_MAX_SCANOUTS];
	__le32 padding;
};

struct virtio_gpu_resource_attach_backing_ext {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 resource_id;
	__le64 shmem_id;
	__le32 size;
	__le32 padding;
};

struct virtio_gpu_set_scanout_pic_adjust {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 hue;
	__le32 saturation;
	__le32 contrast;
	__le32 brightness;
	__le32 padding;
};

struct virtio_gpu_resp_scanout_pic_adjust {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 error_code;
	__le32 padding;
};

enum resource_modifier_mask {
	SECURE_SOURCE = (1<<0),
	COMPRESSED_SOURCE = (1<<1),
};

struct virtio_gpu_set_resource_info {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 resource_id;
	__le32 modifiers;
	__le32 strides[4];
	__le32 offsets[4];
	__le32 ext_format;
	__le32 padding;
};

struct virtio_gpu_get_device_hw_attributes {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 device_id;
	__le32 padding;
};

struct virtio_gpu_resp_device_hw_attributes {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 device_id;
	__le32 num_vriq;
	__le64 virq_shmem[VIRTIO_GPU_MAX_VIRQ];
	__le32 padding;
};

struct virtio_gpu_get_scanout_hw_attributes {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 padding;
};

struct virtio_gpu_resp_scanout_hw_attributes {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 size;
	__u8 blob[VIRTIO_GPU_MAX_SCANOUT_HW_BLOB_SIZE];	// variable length
};

struct virtio_gpu_get_plane_hw_attributes {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
	__le32 padding;
};

struct virtio_gpu_resp_plane_hw_attributes {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout_id;
	__le32 plane_id;
	__le32 sspp_id;
	__le32 rect_mask;
	__le32 padding;
};

struct virtio_gpu_enable_virq {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 device_id;
	__le32 shmem_id;
	__le32 shmem_size;
	__le32 padding;
};

struct virtio_gpu_resp_enable_virq {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 device_id;
	__le32 shmem_id;
	__le32 error_code;
	__le32 padding;
};

struct virtio_gpu_disable_virq {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 device_id;
	__le32 shmem_id;
	__le32 padding;
};

struct virtio_gpu_resp_disable_virq {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 device_id;
	__le32 shmem_id;
	__le32 error_code;
	__le32 padding;
};

struct virtio_gpu_set_power {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 device_id;
	__le32 power_level;
	__le32 padding;
};

struct virtio_gpu_resp_set_power {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 device_id;
	__le32 power_level;
	__le32 error_code;
	__le32 padding;
};

#pragma pack(pop)
#endif //__VIRTIO_EXT_H__

