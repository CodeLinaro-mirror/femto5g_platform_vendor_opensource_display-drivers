// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

//#define SUPPORT_PARA_VIRTUALIZE_CMD
#define SUPPORT_LEGACY_CMD

struct plane_config {
	u32 plane_id;
	u32 sspp_id;
	u32 rect_mask;
	u32 plane_type;
	u32 max_width;
	u32 max_height;
	u32 num_formats;
	u32 formats[VIRTIO_GPU_MAX_PIXEL_FORMATS];
	u32 max_scale;
	u32 min_scale;
	u32 pair_plane_id;
	u32 zorder;
	u32 global_alpha;
	u32 blend_mode;
	struct virtio_gpu_rect src_rect;
	struct virtio_gpu_rect dst_rect;
	u32 color_space;
	u32 colorimetry;
	u32 color_range;
	u32 hue;
	u32 saturation;
	u32 contrast;
	u32 brightness;
};

struct scanout_config {
	u32 scanout_id;
	char *hw_assign_str;

	u32 type;
	u32 connection_status;
	u32 width_mm;
	u32 height_mm;
	int width, height, refresh;
	bool enabled;
	u32 flags;
	u32 power_mode;
	u32 mode_index;
	u32 rotation;
	struct virtio_gpu_rect dest_rect;

	int num_planes;
	struct plane_config *planes[];
};

struct device_config {
	u32 num_vriq;
	u32 virq_shmem[2];
	u32 qseed_type;
	u32 max_mdp_clk;
	u32 has_src_split;
	u32 device_version;

	int num_scanout;
	struct scanout_config *scanouts[];
};

#define STR(x) #x

#if 0
struct plane_config plane1 = {
	.plane_id = 1,
	.sspp_id = (SOURCE_PIPE_TYPE_DMA << SOURCE_PIPE_TYPE_SHIFT) + 0,	// DMA0
	.rect_mask = 0x3,
	.plane_type = 1,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 8,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct plane_config plane2 = {
	.plane_id = 2,
	.sspp_id = (SOURCE_PIPE_TYPE_VIDEO << SOURCE_PIPE_TYPE_SHIFT) + 0,	// VIG0
	.rect_mask = 0x3,
	.plane_type = 1,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 12,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2,
		VIRTIO_GPU_FORMAT_NV12,
		VIRTIO_GPU_FORMAT_UYVY,
		VIRTIO_GPU_FORMAT_NV12_QC_TP10,
		VIRTIO_GPU_FORMAT_P010 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct plane_config plane3 = {
	.plane_id = 3,
	.sspp_id = (SOURCE_PIPE_TYPE_DMA << SOURCE_PIPE_TYPE_SHIFT) + 1,	// DMA1
	.rect_mask = 0x3,
	.plane_type = 1,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 8,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct plane_config plane4 = {
	.plane_id = 4,
	.sspp_id = (SOURCE_PIPE_TYPE_DMA << SOURCE_PIPE_TYPE_SHIFT) + 2,	// DMA2
	.rect_mask = 0x3,
	.plane_type = 1,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 8,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct plane_config plane5 = {
	.plane_id = 5,
	.sspp_id = (SOURCE_PIPE_TYPE_VIDEO << SOURCE_PIPE_TYPE_SHIFT) + 1,	// VIG1
	.rect_mask = 0x3,
	.plane_type = 1,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 12,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2,
		VIRTIO_GPU_FORMAT_NV12,
		VIRTIO_GPU_FORMAT_UYVY,
		VIRTIO_GPU_FORMAT_NV12_QC_TP10,
		VIRTIO_GPU_FORMAT_P010 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct plane_config plane6 = {
	.plane_id = 6,
	.sspp_id = (SOURCE_PIPE_TYPE_VIDEO << SOURCE_PIPE_TYPE_SHIFT) + 2,	// VIG2
	.rect_mask = 0x3,
	.plane_type = 1,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 12,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2,
		VIRTIO_GPU_FORMAT_NV12,
		VIRTIO_GPU_FORMAT_UYVY,
		VIRTIO_GPU_FORMAT_NV12_QC_TP10,
		VIRTIO_GPU_FORMAT_P010 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct scanout_config scanout1 = {
	.scanout_id = 0,
	.hw_assign_str = STR(dpu_id=1;\
		ctl_id=1;\
		ctl_owner=true;\
		vq_id=1;\
		lm_mask=1;\
		lm_owner=true;\
		lm_stage_start=0;\
		lm_stages=4;\
		dspp_mask=1;\
		dspp_owner=true;\
		dsc_mask=1;\
		dsc_owner=true;\
		pingpong_mask=1;\
		pingpong_owner=true;\
		intf_mask=0x10;\
		intf_owner=true;
		topology=singlepipe),
	.type = VIRTIO_PORT_TYPE_DP,
	.connection_status = 1,
	.width_mm = 100,
	.height_mm = 50,
	.width = 1920,
	.height = 1080,
	.refresh = 60,
	.enabled = true,
	.flags = 0,
	.power_mode = 0,
	.mode_index = 0,
	.rotation = 0,
	.num_planes = 2,
	.planes = { &plane1, &plane2 },
};

struct scanout_config scanout2 = {
	.scanout_id = 1,
	.hw_assign_str =STR(dpu_id=1;\
		ctl_id=2;\
		ctl_owner=false;\
		vq_id=2;\
		lm_mask=0xc;\
		lm_owner=false;\
		lm_stage_start=4;\
		lm_stages=4;\
		dspp_mask=0xc;\
		dspp_owner=false;\
		dsc_mask=0xc;\
		dsc_owner=false;\
		merge3d_mask=1;\
		merge3d_owner=false;\
		pingpong_mask=0xc;\
		pingpong_owner=false;\
		intf_mask=0x20;\
		intf_owner=false;
		topology=dualpipe),
	.type = VIRTIO_PORT_TYPE_DP,
	.connection_status = 1,
	.width_mm = 200,
	.height_mm = 100,
	.width = 3840,
	.height = 2160,
	.refresh = 60,
	.enabled = true,
	.flags = 0,
	.power_mode = 0,
	.mode_index = 0,
	.rotation = 0,
	.num_planes = 4,
	.planes = { &plane3, &plane4, &plane5, &plane6 },
};


struct device_config test_config = {
	.qseed_type = 1,
	.max_mdp_clk = 0,
	.has_src_split = 0,
	.device_version = 1,
	.num_scanout = 2,
	.scanouts = { &scanout1, &scanout2 },
	.num_vriq = 2,
	.virq_shmem = { 0, 0 },
};

#elif 0

struct plane_config plane1 = {
	.plane_id = 1,
	.sspp_id = (SOURCE_PIPE_TYPE_DMA << SOURCE_PIPE_TYPE_SHIFT) + 3,	// DMA3
	.rect_mask = 0x3,
	.plane_type = SOURCE_PIPE_TYPE_DMA,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 8,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct plane_config plane2 = {
	.plane_id = 2,
	.sspp_id = (SOURCE_PIPE_TYPE_DMA << SOURCE_PIPE_TYPE_SHIFT) + 4,	// DMA4
	.rect_mask = 0x3,
	.plane_type = SOURCE_PIPE_TYPE_DMA,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 8,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct plane_config plane3 = {
	.plane_id = 3,
	.sspp_id = (SOURCE_PIPE_TYPE_VIDEO << SOURCE_PIPE_TYPE_SHIFT) + 1,	// VIG1
	.rect_mask = 0x3,
	.plane_type = SOURCE_PIPE_TYPE_VIDEO,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 12,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2,
		VIRTIO_GPU_FORMAT_NV12,
		VIRTIO_GPU_FORMAT_UYVY,
		VIRTIO_GPU_FORMAT_NV12_QC_TP10,
		VIRTIO_GPU_FORMAT_P010 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct plane_config plane4 = {
	.plane_id = 4,
	.sspp_id = (SOURCE_PIPE_TYPE_VIDEO << SOURCE_PIPE_TYPE_SHIFT) + 2,	// VIG2
	.rect_mask = 0x3,
	.plane_type = SOURCE_PIPE_TYPE_VIDEO,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 12,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2,
		VIRTIO_GPU_FORMAT_NV12,
		VIRTIO_GPU_FORMAT_UYVY,
		VIRTIO_GPU_FORMAT_NV12_QC_TP10,
		VIRTIO_GPU_FORMAT_P010 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct scanout_config scanout1 = {
	.scanout_id = 0,
	.hw_assign_str = STR(dpu_id=0;\
		ctl_id=4;\
		ctl_owner=true;\
		vq_id=2;\
		lm_mask=2;\
		lm_owner=true;\
		lm_stage_start=4;\
		lm_stages=4;\
		dspp_mask=2;\
		dspp_owner=true;\
		dsc_mask=2;\
		dsc_owner=true;\
		pingpong_mask=2;\
		pingpong_owner=true;\
		intf_mask=0x10;\
		intf_owner=true;
		topology=singlepipe),
	.type = VIRTIO_PORT_TYPE_DP,
	.connection_status = 1,
	.width_mm = 100,
	.height_mm = 50,
	.width = 1280,
	.height = 720,
	.refresh = 60,
	.enabled = true,
	.flags = 0,
	.power_mode = 0,
	.mode_index = 0,
	.rotation = 0,
	.num_planes = 4,
	.planes = { &plane1, &plane2, &plane3, &plane4 },
};


struct plane_config plane5 = {
	.plane_id = 5,
	.sspp_id = (SOURCE_PIPE_TYPE_DMA << SOURCE_PIPE_TYPE_SHIFT) + 3,	// DMA3
	.rect_mask = 0x3,
	.plane_type = SOURCE_PIPE_TYPE_DMA,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 8,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct plane_config plane6 = {
	.plane_id = 6,
	.sspp_id = (SOURCE_PIPE_TYPE_DMA << SOURCE_PIPE_TYPE_SHIFT) + 4,	// DMA44
	.rect_mask = 0x3,
	.plane_type = SOURCE_PIPE_TYPE_DMA,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 8,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct plane_config plane7 = {
	.plane_id = 7,
	.sspp_id = (SOURCE_PIPE_TYPE_VIDEO << SOURCE_PIPE_TYPE_SHIFT) + 1,	// VIG1
	.rect_mask = 0x3,
	.plane_type = SOURCE_PIPE_TYPE_VIDEO,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 12,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2,
		VIRTIO_GPU_FORMAT_NV12,
		VIRTIO_GPU_FORMAT_UYVY,
		VIRTIO_GPU_FORMAT_NV12_QC_TP10,
		VIRTIO_GPU_FORMAT_P010 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct plane_config plane8 = {
	.plane_id = 8,
	.sspp_id = (SOURCE_PIPE_TYPE_VIDEO << SOURCE_PIPE_TYPE_SHIFT) + 2,	// VIG2
	.rect_mask = 0x3,
	.plane_type = SOURCE_PIPE_TYPE_VIDEO,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 12,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2,
		VIRTIO_GPU_FORMAT_NV12,
		VIRTIO_GPU_FORMAT_UYVY,
		VIRTIO_GPU_FORMAT_NV12_QC_TP10,
		VIRTIO_GPU_FORMAT_P010 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};


struct scanout_config scanout2 = {
	.scanout_id = 1,
	.hw_assign_str =STR(dpu_id=1;\
		ctl_id=0;\
		ctl_owner=false;\
		vq_id=2;\
		lm_mask=1;\
		lm_owner=false;\
		lm_stage_start=4;\
		lm_stages=4;\
		dspp_mask=1;\
		dspp_owner=false;\
		dsc_mask=1;\
		dsc_owner=false;\
		pingpong_mask=1;\
		pingpong_owner=false;\
		intf_mask=0x1;\
		intf_owner=false;
		topology=singlepipe),
	.type = VIRTIO_PORT_TYPE_DP,
	.connection_status = 1,
	.width_mm = 200,
	.height_mm = 100,
	.width = 1920,
	.height = 1080,
	.refresh = 60,
	.enabled = true,
	.flags = 0,
	.power_mode = 0,
	.mode_index = 0,
	.rotation = 0,
	.num_planes = 4,
	.planes = { &plane5, &plane6, &plane7, &plane8 },
};


struct device_config test_config = {
	.qseed_type = 1,
	.max_mdp_clk = 0,
	.has_src_split = 0,
	.device_version = 1,
	.num_scanout = 2,
	.scanouts = { &scanout1, &scanout2 },
	.num_vriq = 2,
	.virq_shmem = { 0, 0 },
};

#else

struct plane_config plane1 = {
	.plane_id = 1,
	.sspp_id = (SOURCE_PIPE_TYPE_DMA << SOURCE_PIPE_TYPE_SHIFT) + 4,	// DMA4
	.rect_mask = 0x3,
	.plane_type = SOURCE_PIPE_TYPE_DMA,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 8,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct plane_config plane2 = {
	.plane_id = 2,
	.sspp_id = (SOURCE_PIPE_TYPE_VIDEO << SOURCE_PIPE_TYPE_SHIFT) + 2,	// VIG2
	.rect_mask = 0x3,
	.plane_type = SOURCE_PIPE_TYPE_DMA,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 12,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2,
		VIRTIO_GPU_FORMAT_NV12,
		VIRTIO_GPU_FORMAT_UYVY,
		VIRTIO_GPU_FORMAT_NV12_QC_TP10,
		VIRTIO_GPU_FORMAT_P010 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct scanout_config scanout1 = {
	.scanout_id = 0,
	.hw_assign_str = STR(dpu_id=0;\
		ctl_id=0;\
		ctl_owner=false;\
		vq_id=4;\
		lm_mask=1;\
		lm_owner=false;\
		lm_stage_start=4;\
		lm_stages=4;\
		dspp_mask=1;\
		dspp_owner=false;\
		dsc_mask=1;\
		dsc_owner=false;\
		pingpong_mask=1;\
		pingpong_owner=false;\
		intf_mask=0x1;\
		intf_owner=false;
		topology=singlepipe),
	.type = VIRTIO_PORT_TYPE_DP,
	.connection_status = 1,
	.width_mm = 200,
	.height_mm = 100,
	.width = 1920,
	.height = 1080,
	.refresh = 60,
	.enabled = true,
	.flags = 0,
	.power_mode = 0,
	.mode_index = 0,
	.rotation = 0,
	.num_planes = 2,
	.planes = { &plane1, &plane2 },
};


struct plane_config plane3 = {
	.plane_id = 3,
	.sspp_id = (SOURCE_PIPE_TYPE_DMA << SOURCE_PIPE_TYPE_SHIFT) + 5,	// DMA5
	.rect_mask = 0x3,
	.plane_type = SOURCE_PIPE_TYPE_DMA,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 8,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct plane_config plane4 = {
	.plane_id = 4,
	.sspp_id = (SOURCE_PIPE_TYPE_VIDEO << SOURCE_PIPE_TYPE_SHIFT) + 3,	// VIG3
	.rect_mask = 0x3,
	.plane_type = SOURCE_PIPE_TYPE_VIDEO,
	.max_width = 5120,
	.max_height = 2160,
	.num_formats = 12,
	.formats = {
		VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM,
		VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM,
		VIRTIO_GPU_FORMAT_R8G8B8,
		VIRTIO_GPU_FORMAT_B8G8R8,
		VIRTIO_GPU_FORMAT_R5G6B5,
		VIRTIO_GPU_FORMAT_B5G6R5,
		VIRTIO_GPU_FORMAT_R10G10B10A2,
		VIRTIO_GPU_FORMAT_B10G10R10A2,
		VIRTIO_GPU_FORMAT_NV12,
		VIRTIO_GPU_FORMAT_UYVY,
		VIRTIO_GPU_FORMAT_NV12_QC_TP10,
		VIRTIO_GPU_FORMAT_P010 },
	.max_scale = 16,
	.min_scale = 4,
	.pair_plane_id = 0,
	.zorder = 0,
	.global_alpha = 0xFF,
	.blend_mode = 0,
	.src_rect = {0, 0, 0, 0},
	.dst_rect = {0, 0, 0, 0},
	.color_space = 0,
	.colorimetry = 0,
	.color_range = 0,
	.hue = 0,
	.saturation = 0,
	.contrast = 0,
	.brightness = 0,
};

struct scanout_config scanout2 = {
	.scanout_id = 1,
	.hw_assign_str =STR(dpu_id=0;\
		ctl_id=4;\
		ctl_owner=true;\
		vq_id=5;\
		lm_mask=2;\
		lm_owner=true;\
		lm_stage_start=4;\
		lm_stages=4;\
		dspp_mask=2;\
		dspp_owner=true;\
		dsc_mask=2;\
		dsc_owner=true;\
		pingpong_mask=2;\
		pingpong_owner=true;\
		intf_mask=0x10;\
		intf_owner=false;
		topology=singlepipe),
	.type = VIRTIO_PORT_TYPE_DP,
	.connection_status = 1,
	.width_mm = 200,
	.height_mm = 100,
	.width = 1920,
	.height = 1080,
	.refresh = 60,
	.enabled = true,
	.flags = 0,
	.power_mode = 0,
	.mode_index = 0,
	.rotation = 0,
	.num_planes = 2,
	.planes = { &plane3, &plane4 },
};


struct device_config test_config = {
	.qseed_type = 1,
	.max_mdp_clk = 0,
	.has_src_split = 0,
	.device_version = 1,
	.num_scanout = 2,
	.scanouts = { &scanout1, &scanout2 },
	.num_vriq = 2,
	.virq_shmem = { 0, 0 },
};

#endif

#ifdef SUPPORT_LEGACY_CMD
static int virtio_test_get_capset_info(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_get_capset_info *cmd = req;
	struct virtio_gpu_resp_capset_info *rsp = resp;

	memcpy(&rsp->hdr, &cmd->hdr, sizeof(cmd->hdr));
	rsp->capset_id = 0;
	rsp->capset_max_version = 0;
	rsp->capset_max_size = 0;
	return 0;
}

static int virtio_test_get_capset(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_get_capset *cmd = req;
	struct virtio_gpu_resp_capset *rsp = resp;

	memcpy(&rsp->hdr, &cmd->hdr, sizeof(cmd->hdr));
	memset(rsp->capset_data, 0, 0);
	return 0;
}

static int virtio_test_get_edid(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_cmd_get_edid *cmd = req;
	struct virtio_gpu_resp_edid *rsp = resp;

	memcpy(&rsp->hdr, &cmd->hdr, sizeof(cmd->hdr));
	rsp->size = 0;
	memset(rsp->edid, 0, sizeof(rsp->edid));
	return 0;
}

static int virtio_test_get_display_info(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_ctrl_hdr *cmd = req;
	struct virtio_gpu_resp_display_info *rsp = resp;
	int i;
	struct scanout_config *scanout;

	memcpy(&rsp->hdr, cmd, sizeof(*cmd));
	memset(rsp->pmodes, 0, sizeof(rsp->pmodes));
	for (i = 0; i < test_config.num_scanout; i++) {
		scanout = test_config.scanouts[i];
		rsp->pmodes[i].r.x = 0;
		rsp->pmodes[i].r.y = 0;
		rsp->pmodes[i].r.width = scanout->width;
		rsp->pmodes[i].r.height = scanout->height;
		rsp->pmodes[i].enabled = scanout->enabled;
		rsp->pmodes[i].flags = scanout->flags;
	}
	return 0;
}

static int virtio_test_get_display_info_ext(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_get_display_info_ext *cmd = req;
	struct virtio_gpu_resp_display_info_ext *rsp = resp;
	int i;
	struct scanout_config *scanout;

	for (i = 0; i < test_config.num_scanout; i++) {
		scanout = test_config.scanouts[i];
		if (scanout->scanout_id == cmd->scanout_id) {
			memcpy(&rsp->hdr, &cmd->hdr, sizeof(cmd->hdr));
			memset(&rsp->pmodes, 0, sizeof(rsp->pmodes));
			rsp->pmodes[0].r.x = 0;
			rsp->pmodes[0].r.y = 0;
			rsp->pmodes[0].r.width = scanout->width;
			rsp->pmodes[0].r.height = scanout->height;
			rsp->pmodes[0].refresh = scanout->refresh;
			rsp->pmodes[0].enabled = scanout->enabled;
			rsp->pmodes[0].flags = scanout->flags;
			return 0;
		}
	}
	return -EINVAL;
}

static int virtio_test_get_scanout_attrib(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_get_scanout_attributes *cmd = req;
	struct virtio_gpu_resp_scanout_atttributes *rsp = resp;
	int i;
	struct scanout_config *scanout;

	for (i = 0; i < test_config.num_scanout; i++) {
		scanout = test_config.scanouts[i];
		if (scanout->scanout_id == cmd->scanout_id) {
			memcpy(&rsp->hdr, &cmd->hdr, sizeof(cmd->hdr));
			rsp->scanout_id = scanout->scanout_id;
			rsp->type = scanout->type;
			rsp->connection_status = scanout->connection_status;
			rsp->width_mm = scanout->width_mm;
			rsp->height_mm = scanout->height_mm;
			rsp->panel_orientation = 0;
			return 0;
		}
	}
	return -EINVAL;
}

static int virtio_test_set_scanout_attrib(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_set_scanout_properties *cmd = req;
	struct virtio_gpu_resp_scanout_properties *rsp = resp;
	int i;
	struct scanout_config *scanout;

	for (i = 0; i < test_config.num_scanout; i++) {
		scanout = test_config.scanouts[i];
		if (scanout->scanout_id == cmd->scanout_id) {
			memcpy(&rsp->hdr, &cmd->hdr, sizeof(cmd->hdr));
			rsp->scanout_id = scanout->scanout_id;
			rsp->error_code = 0;
			scanout->power_mode = cmd->power_mode;
			scanout->mode_index = cmd->mode_index;
			scanout->rotation = cmd->rotation;
			scanout->dest_rect = cmd->r;
			return 0;
		}
	}
	return -EINVAL;

	memcpy(&rsp->hdr, &cmd->hdr, sizeof(cmd->hdr));
	return 0;
}

static int virtio_test_get_plane_caps(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_get_planes_caps *cmd = req;
	struct virtio_gpu_resp_planes_caps *rsp = resp;
	int i, j;
	struct scanout_config *scanout;
	struct plane_config *plane;

	for (i = 0; i < test_config.num_scanout; i++) {
		scanout = test_config.scanouts[i];
		if (scanout->scanout_id == cmd->scanout_id) {
			for (j = 0; j < scanout->num_planes; j++) {
				plane = scanout->planes[j];
				if (plane->plane_id == cmd->plane_id){
					rsp->caps.scanout_id = scanout->scanout_id;
					rsp->caps.plane_id = plane->plane_id;
					rsp->caps.plane_type = plane->plane_type;
					rsp->caps.max_width = plane->max_width;
					rsp->caps.max_height = plane->max_height;
					rsp->caps.max_scale = plane->max_scale;
					rsp->caps.num_formats = plane->num_formats;
					memcpy(rsp->caps.formats, plane->formats, sizeof(u32) * plane->num_formats);
					rsp->caps.min_scale = plane->min_scale;
					rsp->caps.pair_plane_id = plane->pair_plane_id;
					return 0;
				}
			}
		}
	}
	return -EINVAL;
}

static int virtio_test_get_plane_properties(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_get_plane_properties *cmd = req;
	struct virtio_gpu_resp_get_plane_properties *rsp = resp;
	int i, j;
	struct scanout_config *scanout;
	struct plane_config *plane;

	for (i = 0; i < test_config.num_scanout; i++) {
		scanout = test_config.scanouts[i];
		if (scanout->scanout_id == cmd->scanout_id) {
			for (j = 0; j < scanout->num_planes; j++) {
				plane = scanout->planes[j];
				if (plane->plane_id == cmd->plane_id){
					rsp->scanout_id = scanout->scanout_id;
					rsp->plane_id = plane->plane_id;
					rsp->zorder = plane->zorder;
					return 0;
				}
			}
		}
	}
	return -EINVAL;
}

static int virtio_test_event_control(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_event_control *cmd = req;
	struct virtio_gpu_ctrl_hdr *rsp = resp;

	memcpy(rsp, cmd, sizeof(*cmd));
	return 0;
}

static int virtio_test_wait_events(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_ctrl_hdr *cmd = req;
	struct virtio_gpu_resp_event *rsp = resp;

	memcpy(&rsp->hdr, cmd, sizeof(*cmd));
	msleep(10 * 1000);
	return -ETIMEDOUT;
}

static int virtio_test_get_device_info(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_ctrl_hdr *cmd = req;
	struct virtio_gpu_resp_device_info *rsp = resp;

	memcpy(&rsp->hdr, cmd, sizeof(*cmd));
	rsp->device_info.qseed_type = 3;
	rsp->device_info.max_mdp_clk = 650;
	rsp->device_info.has_src_split = false;
	rsp->device_info.device_version = 0x1201;
	return 0;
}

static int virtio_test_get_scanout_planes(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_resp_scanout_planes *cmd = req;
	struct virtio_gpu_resp_scanout_planes *rsp = resp;
	int i, j;
	struct scanout_config *scanout;

	rsp->scanout_id = cmd->scanout_id;
	for (i = 0; i < test_config.num_scanout; i++) {
		scanout = test_config.scanouts[i];
		if (scanout->scanout_id == cmd->scanout_id) {
			rsp->num_planes = scanout->num_planes;
			for (j = 0; j < scanout->num_planes; j++)
				rsp->plane_ids[j] = scanout->planes[j]->plane_id;
			return 0;
		}
	}
	return -EINVAL;
}
#endif

static int virtio_test_get_device_hw_attrib(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_get_device_hw_attributes *cmd = req;
	struct virtio_gpu_resp_device_hw_attributes *rsp = resp;
	int i;

	memcpy(&rsp->hdr, &cmd->hdr, sizeof(cmd->hdr));
	rsp->num_vriq = test_config.num_vriq;
	for (i = 0; i < rsp->num_vriq; i++)
		rsp->virq_shmem[i] = test_config.virq_shmem[i];
	return 0;
}

static int virtio_test_get_scanout_hw_attrib(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_get_scanout_hw_attributes *cmd = req;
	struct virtio_gpu_resp_scanout_hw_attributes *rsp = resp;
	int i;
	struct scanout_config *scanout;

	rsp->scanout_id = cmd->scanout_id;
	for (i = 0; i < test_config.num_scanout; i++) {
		scanout = test_config.scanouts[i];
		if (scanout->scanout_id == cmd->scanout_id) {
			rsp->scanout_id = scanout->scanout_id;
			strlcpy(rsp->blob, scanout->hw_assign_str, strlen(scanout->hw_assign_str));
			rsp->size = strlen(rsp->blob);
			return 0;
		}
	}
	return -EINVAL;
}

static int virtio_test_get_plane_hw_attrib(void *req, uint32_t req_size,
		void *resp, uint32_t resp_size)
{
	struct virtio_gpu_get_plane_hw_attributes *cmd = req;
	struct virtio_gpu_resp_plane_hw_attributes *rsp = resp;
	int i, j;
	struct scanout_config *scanout;
	struct plane_config *plane;

	rsp->scanout_id = cmd->scanout_id;
	for (i = 0; i < test_config.num_scanout; i++) {
		scanout = test_config.scanouts[i];
		if (scanout->scanout_id == cmd->scanout_id) {
			for (j = 0; j < scanout->num_planes; j++) {
				plane = scanout->planes[j];
				if (plane->plane_id == cmd->plane_id){
					rsp->scanout_id = scanout->scanout_id;
					rsp->plane_id = plane->plane_id;
					rsp->sspp_id = plane->sspp_id;
					rsp->rect_mask = plane->rect_mask;
					return 0;
				}
			}
		}
	}
	return -EINVAL;
}

static int virtio_hab_send_and_recv(uint32_t hab_socket,
		struct channel_map *phab_channel,
		void *req,
		uint32_t req_size,
		void *resp,
		uint32_t resp_size,
		bool lock_flag)
{
	struct virtio_gpu_ctrl_hdr *cmd_hdr = req;

	switch (cmd_hdr->type) {
#if !defined(SUPPORT_LEGACY_CMD)
	case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
	case VIRTIO_GPU_CMD_GET_CAPSET_INFO:
	case VIRTIO_GPU_CMD_GET_CAPSET:
	case VIRTIO_GPU_CMD_GET_EDID:
	case VIRTIO_GPU_CMD_GET_DISPLAY_INFO_EXT:
	case VIRTIO_GPU_CMD_GET_SCANOUT_ATTRIBUTES:
	case VIRTIO_GPU_CMD_SET_SCANOUT_PROPERTIES:
	case VIRTIO_GPU_CMD_GET_PLANES_CAPS:
	case VIRTIO_GPU_CMD_GET_PLANE_PROPERTIES:
	case VIRTIO_GPU_CMD_EVENT_CONTROL:
	case VIRTIO_GPU_CMD_WAIT_EVENTS:
	case VIRTIO_GPU_CMD_GET_DEVICE_INFO:
#ifdef SUPPORT_PARA_VIRTUALIZE_CMD
	case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D:
	case VIRTIO_GPU_CMD_RESOURCE_UNREF:
	case VIRTIO_GPU_CMD_SET_SCANOUT:
	case VIRTIO_GPU_CMD_RESOURCE_FLUSH:
	case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D:
	case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING:
	case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING:
	case VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID:
	case VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB:
	case VIRTIO_GPU_CMD_SET_SCANOUT_BLOB:
	case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING_EXT:
	case VIRTIO_GPU_CMD_GET_SCANOUT_PLANES:
	case VIRTIO_GPU_CMD_PLANE_CREATE:
	case VIRTIO_GPU_CMD_PLANE_DESTROY:
	case VIRTIO_GPU_CMD_SET_PLANE:
	case VIRTIO_GPU_CMD_SET_PLANE_PROPERTIES:
	case VIRTIO_GPU_CMD_SET_RESOURCE_INFO:
	case VIRTIO_GPU_CMD_WAIT_FOR_VSYNC:
	case VIRTIO_GPU_CMD_SET_PLANE_HDR:
	case VIRTIO_GPU_CMD_SET_PIC_ADJUST:
	case VIRTIO_GPU_CMD_PLANE_FLUSH:
	case VIRTIO_GPU_CMD_SCANOUT_FLUSH:
	case VIRTIO_GPU_CMD_FULL_FLUSH:
#endif
		return virtio_hab_send_and_recv_ext(hab_socket, phab_channel,
				req, req_size, resp, resp_size, lock_flag);
		break;
#else
	case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
		return virtio_test_get_display_info(req, req_size, resp, resp_size);
		break;

	case VIRTIO_GPU_CMD_GET_CAPSET_INFO:
		return virtio_test_get_capset_info(req, req_size, resp, resp_size);
		break;

	case VIRTIO_GPU_CMD_GET_CAPSET:
		return virtio_test_get_capset(req, req_size, resp, resp_size);
		break;

	case VIRTIO_GPU_CMD_GET_EDID:
		return virtio_test_get_edid(req, req_size, resp, resp_size);
		break;

	case VIRTIO_GPU_CMD_GET_DISPLAY_INFO_EXT:
		return virtio_test_get_display_info_ext(req, req_size, resp, resp_size);
		break;

	case VIRTIO_GPU_CMD_GET_SCANOUT_ATTRIBUTES:
		return virtio_test_get_scanout_attrib(req, req_size, resp, resp_size);
		break;

	case VIRTIO_GPU_CMD_SET_SCANOUT_PROPERTIES:
		return virtio_test_set_scanout_attrib(req, req_size, resp, resp_size);
		break;

	case VIRTIO_GPU_CMD_GET_PLANES_CAPS:
		return virtio_test_get_plane_caps(req, req_size, resp, resp_size);
		break;

	case VIRTIO_GPU_CMD_GET_PLANE_PROPERTIES:
		return virtio_test_get_plane_properties(req, req_size, resp, resp_size);
		break;

	case VIRTIO_GPU_CMD_EVENT_CONTROL:
		return virtio_test_event_control(req, req_size, resp, resp_size);
		break;

	case VIRTIO_GPU_CMD_WAIT_EVENTS:
		return virtio_test_wait_events(req, req_size, resp, resp_size);
		break;

	case VIRTIO_GPU_CMD_GET_DEVICE_INFO:
		return virtio_test_get_device_info(req, req_size, resp, resp_size);
		break;

	case VIRTIO_GPU_CMD_GET_SCANOUT_PLANES:
		return virtio_test_get_scanout_planes(req, req_size, resp, resp_size);
		break;

#ifdef SUPPORT_PARA_VIRTUALIZE_CMD
	case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D:
	case VIRTIO_GPU_CMD_RESOURCE_UNREF:
	case VIRTIO_GPU_CMD_SET_SCANOUT:
	case VIRTIO_GPU_CMD_RESOURCE_FLUSH:
	case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D:
	case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING:
	case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING:
	case VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID:
	case VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB:
	case VIRTIO_GPU_CMD_SET_SCANOUT_BLOB:
	case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING_EXT:
	case VIRTIO_GPU_CMD_PLANE_CREATE:
	case VIRTIO_GPU_CMD_PLANE_DESTROY:
	case VIRTIO_GPU_CMD_SET_PLANE:
	case VIRTIO_GPU_CMD_SET_PLANE_PROPERTIES:
	case VIRTIO_GPU_CMD_SET_RESOURCE_INFO:
	case VIRTIO_GPU_CMD_WAIT_FOR_VSYNC:
	case VIRTIO_GPU_CMD_SET_PLANE_HDR:
	case VIRTIO_GPU_CMD_SET_PIC_ADJUST:
	case VIRTIO_GPU_CMD_PLANE_FLUSH:
	case VIRTIO_GPU_CMD_SCANOUT_FLUSH:
	case VIRTIO_GPU_CMD_FULL_FLUSH:
		return 0;
		break;
#endif
#endif

	case VIRTIO_GPU_CMD_GET_DEVICE_HW_ATTRIBUTES:
		return virtio_test_get_device_hw_attrib(req, req_size, resp, resp_size);
		break;

	case VIRTIO_GPU_CMD_GET_SCANOUT_HW_ATTRIBUTES:
		return virtio_test_get_scanout_hw_attrib(req, req_size, resp, resp_size);
		break;

	case VIRTIO_GPU_CMD_GET_PLANE_HW_ATTRIBUTES:
		return virtio_test_get_plane_hw_attrib(req, req_size, resp, resp_size);
		break;

	default:
		return virtio_hab_send_and_recv_ext(hab_socket, phab_channel,
				req, req_size, resp, resp_size, lock_flag);
		break;
	}
}


int virtio_hab_send_and_recv_timeout(uint32_t hab_socket,
		struct mutex *hab_lock,
		void *req,
		uint32_t req_size,
		void *resp,
		uint32_t resp_size)
{
#if !defined(SUPPORT_LEGACY_CMD)
	return virtio_hab_send_and_recv_timeout_ext(hab_socket, hab_lock,
			req, req_size, resp, resp_size);
#else
	struct channel_map hab_channel = { 0 };
	return virtio_hab_send_and_recv(hab_socket, &hab_channel,
			req, req_size, resp, resp_size, false);
#endif
}
