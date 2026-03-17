/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 *
 */

#ifndef _SHD_DRM_H_
#define _SHD_DRM_H_
#include <linux/types.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include "msm_drv.h"
#include "dsi_display.h"
#include "sde_hw_catalog.h"

struct shd_mode_info {
	int x_offset;
	int y_offset;
	int width;
	int height;
};

struct shd_stage_range {
	u32 start;
	u32 size;
};

struct shd_roi_bypass_range {
	u32 roi_mask;
	struct sde_rect roi_info[ROI_MISR_MAX_ROIS_PER_MISR];
};

struct shd_display_base {
	struct drm_display_mode mode;
	struct drm_display_info info;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct list_head head;
	struct list_head disp_list;
	struct device_node *of_node;
	struct sde_connector_ops ops;

	int intf_idx;
	int hw_dev_id;
	bool mst_port;
	bool dynamic_mode;
	bool fill_ops;
};

struct shd_display {
	struct dsi_display *dsi_base;
	struct drm_device *drm_dev;
	const char *name;
	const char *display_type;
	struct drm_display_info info;

	struct shd_display_base *base;
	struct drm_bridge *bridge;

	struct device_node *base_of;
	struct sde_rect src;
	struct sde_rect roi;
	struct shd_stage_range stage_range;
	uint32_t misr_roi_mask;
	struct sde_rect misr_range[ROI_MISR_MAX_ROIS_PER_CRTC];
	bool full_screen;

	bool dspp_enabled;
	bool has_bypass_property;
	struct shd_roi_bypass_range bypass_range[MAX_MIXERS_PER_CRTC];

	struct platform_device *pdev;
	struct list_head head;
	struct notifier_block notifier;
	struct drm_crtc *crtc;
};

void *sde_encoder_phys_shd_init(enum sde_intf_type type, u32 controller_id,
				void *phys_init_params);

void sde_shd_hw_flush(struct sde_hw_ctl *ctl_ctx,
		struct sde_hw_mixer *lm_ctx[MAX_MIXERS_PER_CRTC], int lm_num,
		struct sde_hw_dspp *dspp_ctx[MAX_MIXERS_PER_CRTC], int dspp_num,
		struct sde_hw_roi_misr *misr_ctx[MAX_MIXERS_PER_CRTC], int misr_num);

/* helper for seamless plane handoff */

u32 shd_get_shared_crtc_mask(struct drm_crtc *crtc);
void shd_skip_shared_plane_update(struct drm_plane *plane, struct drm_crtc *crtc);
int shd_crtc_get_max_blendstages(struct drm_crtc *crtc, u32 *max_blendstages);
void sde_connector_get_avail_res_info_shd(struct drm_connector *conn,
					  struct msm_resource_caps_info *avail_res);
#endif /* _SHD_DRM_H_ */
