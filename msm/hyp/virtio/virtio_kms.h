/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef __VIRTIO_KMS_H__
#define __VIRTIO_KMS_H__
#include <linux/virtio_gpu.h>
#include <msm_drv_hyp.h>
#include "virtio_ext.h"
#include "sde_rm.h"
#include "sde_edid_parser.h"

#define PANEL_NAME_LEN 13
#define VIRTIO_MAX_CLIENTS 10
#define MARKER_BUFF_LENGTH 256
#define NO_SPIN_LOCK_CHANNEL 0x00
#define SPIN_LOCK_CHANNEL 0x01
#define MAX_PORT_NAME_LENGTH (PANEL_NAME_LEN - 1)

#define to_virtio_kms(x)\
		container_of((x), struct virtio_kms, base)

enum virtio_channel_ids {
	CHANNEL_CMD,
	CHANNEL_EVENTS,
	MAX_CHANNELS
};

enum hab_dbl_handle_t {
	HAB_DBL_HANDLE_NONE,
	HAB_DBL_HANDLE_1,
	HAB_DBL_HANDLE_2,
	HAB_DBL_HANDLE_MAX
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
	/* HDR */
	uint32_t panel_colorspace;
	uint32_t hdr_max_luminance;
	uint32_t hdr_avg_luminance;
	uint32_t hdr_min_luminance;
	uint32_t avr_supported;
	uint32_t avr_min_fps;
	uint32_t avr_step;
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
	uint32_t sspp_id;
	uint32_t rect_mask;
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
	struct display_hw_assigment hw_assign;
	bool enabled;
	uint32_t type;
	struct edid *edid;
	struct sde_edid_ctrl *edid_ctrl;
	struct mutex edid_lock;
	uint32_t plane_cnt;
	struct virtio_plane_caps plane_caps[VIRTIO_GPU_MAX_PLANES];
	struct drm_crtc *crtc;
	bool vblank_enabled;
	bool hpd_enabled;
	bool rc_enabled;
	struct completion commit_done;
	uint32_t offset_x;
	uint32_t offset_y;
	char port_name[MAX_PORT_NAME_LENGTH];
};

struct channel_map {
	int32_t hab_socket[MAX_CHANNELS];
	spinlock_t hyp_chl_spin_lock;
	struct mutex hyp_chl_lock[MAX_CHANNELS];
};

struct virq_info_t {
	enum hab_dbl_handle_t hab_dbl_handle;
	struct virtio_kms *kms;
	uint32_t dpu_id;
};

struct device_info_type {
	uint32_t qseed_type;
	uint32_t max_mdp_clk;
	uint32_t has_src_split;
	uint32_t device_version;
	uint32_t num_virq;
	uint64_t virq_shmem[VIRTIO_GPU_MAX_VIRQ];
};

struct virtio_kms {
	struct msm_hyp_kms base;
	struct channel_map channel[VIRTIO_MAX_CLIENTS];
	struct virq_info_t *virq_info[VIRTIO_GPU_MAX_VIRQ];
	uint32_t client_hab_id;
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
	char panel_name[MAX_PORT_NAME_LENGTH];
	struct virtio_gpu_rect mode_rect;
	uint32_t mode_index;
	struct msm_freq_step_list freq_step_list;
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

void  virtio_kms_event_handler(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t num_event,
		uint32_t event_type);

#endif //_VIRTIO_KMS_H__
