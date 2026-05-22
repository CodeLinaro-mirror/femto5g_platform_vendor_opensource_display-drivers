/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef __VIRTIO_KMS_H__
#define __VIRTIO_KMS_H__
#include <linux/virtio_gpu.h>
#include <msm_drv_hyp.h>
#include "virtio_ext.h"
#define PANEL_NAME_LEN 13
#define VIRTIO_MAX_CLIENTS	10
#define MARKER_BUFF_LENGTH 256
#define NO_SPIN_LOCK_CHANNEL 0x00
#define SPIN_LOCK_CHANNEL 0x01
#define VIRTIO_CSC_LUT_ENTRIES                                  3
#define VIRTIO_GAMMA_LUT_ENTRIES                                256
#define VIRTIO_WGM_MAX_COLOR_COMPONENTS                         3
#define VIRTIO_WGM_MAX_COMPONENT_TABLES_FINE_COARSE             4
#define VIRTIO_WGM_TABLE_0_ENTRIES                              1229
#define VIRTIO_WGM_TABLE_1_ENTRIES                              1228
#define VIRTIO_WGM_TABLE_2_ENTRIES                              1228
#define VIRTIO_WGM_TABLE_3_ENTRIES                              1228
#define VIRTIO_WGM_MAX_NON_UNIFORM_MAP_TABLE_ENTRIES_A          16
#define VIRTIO_NUM_COLOR_CONFIG_BUFFERS                         2
/*
 * Color support related definations and variables
 */
#define MAX_PIPELINES_GVM 16

#define to_virtio_kms(x)\
		container_of((x), struct virtio_kms, base)


enum virtio_channel_ids {
	CHANNEL_CMD,
	CHANNEL_EVENTS,
	MAX_CHANNELS
};

enum virtio_hpd_connection_status {
	VIRTIO_HPD_DISCONNECT = 1,
	VIRTIO_HPD_CONNECT
};

struct scanout_attrib {
	uint32_t type;
	uint32_t connection_status;
	uint32_t width_mm;
	uint32_t height_mm;
	uint32_t panel_orientation;
};

struct virtio_plane_caps {
	uint32_t plane_id;
	uint32_t plane_type;
	uint32_t max_width;
	uint32_t max_height;
	uint32_t num_formats;
	uint32_t formats[VIRTIO_GPU_MAX_PIXEL_FORMATS];
	uint32_t max_scale;
	uint32_t min_scale;
	uint32_t zorder;
	uint32_t pair_plane_id;
	int32_t  master_plane_id;
	uint32_t support_rotation;
};

struct virtio_display_modes {
	struct virtio_gpu_rect r;
	uint32_t refresh;
	uint32_t flags;
};

struct virtio_kms_output {
	int index;
	struct virtio_display_modes info[VIRTIO_GPU_MAX_MODES]; //modes
	uint32_t num_modes;
	struct scanout_attrib attr;
	bool enabled;
	uint32_t type;
	struct edid *edid;
	uint32_t plane_cnt;
	struct virtio_plane_caps plane_caps[VIRTIO_GPU_MAX_PLANES];
	struct drm_crtc *crtc;
	bool vblank_enabled;
};

struct channel_map {
	int32_t hab_socket[MAX_CHANNELS];
	spinlock_t hyp_chl_spin_lock;
	struct mutex hyp_chl_lock[MAX_CHANNELS];
};

struct device_info_type {
	uint32_t qseed_type;
	uint32_t max_mdp_clk;
	uint32_t has_src_split;
	uint32_t device_version;
};
struct virtio_kms {
	struct msm_hyp_kms base;
	struct channel_map channel[VIRTIO_MAX_CLIENTS];
	uint32_t mmid_cmd;
	uint32_t mmid_buffer;
	uint32_t mmid_event;
	bool stop;
	struct drm_device *dev;
	uint32_t client_id;
	struct virtio_device *vdev;
	wait_queue_head_t resp_wq;
	uint32_t max_sdma_width;

	/* current display info */
	spinlock_t display_info_lock;
	bool display_info_pending;

	uint32_t num_capsets;
	struct virtio_gpu_drv_capset *capsets;
	uint32_t num_scanouts;
	struct virtio_kms_output outputs[VIRTIO_GPU_MAX_SCANOUTS];
	bool has_edid;
	struct device_info_type device_info;
};

struct virtio_mem_info {
	void *buffer;
	uint32_t size;
	uint64_t shmem_id;
};

struct virtio_framebuffer_priv {
	struct msm_hyp_framebuffer_info base;
	struct virtio_kms *kms;
	uint32_t format;
	uint32_t hw_res_handle;
	struct virtio_mem_info mem;
	bool created;
	bool secure;
	bool compressed;
};

struct virtio_connector_info_priv {
	struct msm_hyp_connector_info base;
	struct virtio_kms *kms;
	struct drm_crtc *crtc;
	int connector_status;
	uint32_t scanout;
	uint32_t mode_count;
	struct drm_display_mode *modes;
	char panel_name[PANEL_NAME_LEN];
	struct virtio_gpu_rect mode_rect;
	uint32_t mode_index;
};

struct virtio_crtc_info_priv {
	struct msm_hyp_crtc_info base;
	struct virtio_kms *kms;
	bool vblank_enable;
	int scanout;
	struct msm_hyp_prop_blob_info extra_info;
};

struct virtio_plane_info_priv {
	struct msm_hyp_plane_info base;
	struct virtio_kms *kms;
	uint32_t  plane_type;
	uint32_t plane_id;
	uint32_t scanout;
	bool committed;
};

struct buffer_info {
	bool valid;
	bool in_use;
	int  export_id;
	dma_addr_t *dmabuf_handle;
	int32_t *va;
};

struct color_buffer {
	bool     valid;
	uint32_t plane_id;
	int      curr_buff_in_use;
	struct virtio_kms  *kms;
	struct buffer_info buffer_info[2];
};

/*
 * VIRTIO_PipelineDMAConfigBuffType defines DMA pipe LUT config
 * this is buffer struture which is passed as input to WFD layer by GVM or wfd_client_gen
 * This will come as pmem handle to openwfd server in case of QNX
 */
struct VIRTIO_PipelineDMAConfigBuffType {
	uint32_t    bIGCEnabled;
	uint32_t    bCSCEnabled;
	uint32_t    bGCEnabled;
	uint32_t    uCscMatrix[VIRTIO_CSC_LUT_ENTRIES][VIRTIO_CSC_LUT_ENTRIES];
	uint16_t    uGCLut[VIRTIO_GAMMA_LUT_ENTRIES];
	uint16_t    uIGCLut[VIRTIO_GAMMA_LUT_ENTRIES];
} __packed;

/*
 * VIRTIO_Color_VIGConfigType defines VIG pipe LUT (17*17*17) config
 * this is buffer struture which is passed as input to WFD layer by GVM or wfd_client_gen
 * This will come as pmem handle to openwfd server in case of QNX
 */
struct VIRTIO_PipelineVIGConfigBuffType {
	uint32_t bGamutEn;
	uint32_t bGamutMapEn;
	uint16_t uGammutTable0Entries[VIRTIO_WGM_MAX_COLOR_COMPONENTS]
		[VIRTIO_WGM_TABLE_0_ENTRIES];
	uint16_t uGammutTable1Entries[VIRTIO_WGM_MAX_COLOR_COMPONENTS]
		[VIRTIO_WGM_TABLE_1_ENTRIES];
	uint16_t uGammutTable2Entries[VIRTIO_WGM_MAX_COLOR_COMPONENTS]
		[VIRTIO_WGM_TABLE_2_ENTRIES];
	uint16_t uGammutTable3Entries[VIRTIO_WGM_MAX_COLOR_COMPONENTS]
		[VIRTIO_WGM_TABLE_3_ENTRIES];
	uint32_t uNonUniformMapTableEntries[VIRTIO_WGM_MAX_COLOR_COMPONENTS]
		[VIRTIO_WGM_MAX_NON_UNIFORM_MAP_TABLE_ENTRIES_A];
} __packed;

/*
 * VIRTIO_PipelineColorConfigBuffType defines essential parameters for
 * getting the LUT tables for the DMA or VIG pipes
 */
union VIRTIO_PipelineColorConfigBuffType {
	struct VIRTIO_PipelineDMAConfigBuffType   sDMAConfig;
	struct VIRTIO_PipelineVIGConfigBuffType   sVigConfigType;
};

void  virtio_kms_event_handler(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t num_event,
		uint32_t event_type);

#endif //_VIRTIO_KMS_H__
