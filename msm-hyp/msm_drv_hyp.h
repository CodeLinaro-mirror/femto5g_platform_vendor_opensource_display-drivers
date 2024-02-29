/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2017-2018, 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MSM_DRV_HYP_H__
#define __MSM_DRV_HYP_H__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/atomic.h>
#include <linux/kthread.h>
#include <linux/component.h>
#include <linux/backlight.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_connector.h>
#include <drm/drm_client.h>
#include <drm/msm_drm.h>
#include <drm/sde_drm.h>
#include <drm/msm_drm_pp.h>
#include "msm_hyp_fence.h"
#include <drm/drm_framebuffer.h>
#include <drm/drm_blend.h>
#include "msm_drv.h"

#define DRM_DRI_NAME_SIZE 32

enum msm_hyp_panel_rotation {
	PANEL_ROTATE_NONE = 0,
	PANEL_ROTATE_90,
	PANEL_ROTATE_180,
	PANEL_ROTATE_HV_FLIP = PANEL_ROTATE_180,
	PANEL_ROTATE_270,
	PANEL_ROTATE_H_FLIP,
	PANEL_ROTATE_V_FLIP
};

struct msm_hyp_connector_info {
	int connector_type;
	enum msm_hyp_panel_rotation panel_orientation;
	const struct drm_bridge_funcs *bridge_funcs;
	const struct drm_connector_helper_funcs *connector_funcs;
	uint32_t possible_crtcs;
	struct drm_display_info display_info;
	const char *display_type;
	const char *extra_caps;
};

struct msm_hyp_plane_info {
	enum drm_plane_type plane_type;
	const struct drm_plane_helper_funcs *plane_funcs;
	uint32_t possible_crtcs;
	uint32_t format_count;
	uint32_t *format_types;
	uint32_t maxdwnscale;
	uint32_t maxupscale;
	uint32_t maxhdeciexp;
	uint32_t maxvdeciexp;
	uint32_t max_width;
	uint64_t max_bandwidth;
	bool support_scale;
	bool support_csc;
	bool support_multirect;
	bool support_rotation;
	bool vig_pipe;
	int master_plane_index;
	const char *extra_caps;
};

struct msm_hyp_crtc_info {
	const struct drm_crtc_helper_funcs *crtc_funcs;
	uint32_t primary_plane_index;
	uint32_t max_blendstages;
	uint64_t max_mdp_clk;
	uint64_t max_bandwidth_low;
	uint64_t max_bandwidth_high;
	const char *qseed_type;
	const char *smart_dma_rev;
	bool has_src_split;
	bool has_hdr;
	const char *extra_caps;
};

struct msm_hyp_framebuffer_info {
	void (*destroy)(struct drm_framebuffer *framebuffer);
};

struct msm_hyp_connector {
	struct drm_connector base;
	struct drm_encoder encoder;
	struct drm_bridge bridge;
	struct msm_hyp_fence_context *retire_fence;
	struct backlight_device *bl_device;
	struct msm_hyp_connector_info *info;
	struct drm_property_blob *blob_caps;
	struct drm_property_blob *blob_mode_info;
	struct drm_property_blob *blob_edid;
};

struct msm_hyp_connector_state {
	struct drm_connector_state base;
	uint64_t __user *retire_fence_ptr;
};

struct msm_hyp_plane {
	struct drm_plane base;
	struct drm_plane *primary_plane;
	struct msm_hyp_plane_info *info;
	struct drm_property_blob *blob_caps;
};

enum {
	MSM_HYP_PLANE_DIRTY_NONE 			= 0,
	MSM_HYP_PLANE_DIRTY_ZPOS 			= 1 << 0,
	MSM_HYP_PLANE_DIRTY_BLENDOP 		= 1 << 1,
	MSM_HYP_PLANE_DIRTY_ALPHA 			= 1 << 2,
	MSM_HYP_PLANE_DIRTY_MULTIRECT 		= 1 << 3,
	MSM_HYP_PLANE_DIRTY_CSC 			= 1 << 4,
	MSM_HYP_PLANE_DIRTY_SCALER 			= 1 << 5,
	MSM_HYP_PLANE_DIRTY_DMA_CSC 		= 1 << 6,
	MSM_HYP_PLANE_DIRTY_DMA_IGC 		= 1 << 7,
	MSM_HYP_PLANE_DIRTY_DMA_GC 			= 1 << 8,
	MSM_HYP_PLANE_DIRTY_VIG_IGC 		= 1 << 9,
	MSM_HYP_PLANE_DIRTY_GAMUT 			= 1 << 10,
	MSM_HYP_PLANE_DIRTY_INVERSE_PMA 	= 1 << 11,
	MSM_HYP_PLANE_DIRTY_INPUT_FENCE 	= 1 << 12,
};

struct msm_hyp_plane_state {
	struct drm_plane_state base;
	struct dma_fence *input_fence;
	uint32_t zpos;
	uint32_t blend_op;
	uint32_t alpha;
	uint32_t fb_mode;
	uint32_t multirect_mode;
	struct sde_drm_csc_v1 csc;
	struct sde_drm_scaler_v2 scaler;
	bool dma_csc_en;
	struct sde_drm_csc_v1 dma_csc;
	bool dma_igc_en;
	struct drm_msm_igc_lut dma_igc;
	bool dma_gc_en;
	struct drm_msm_pgc_lut dma_gc;
	bool vig_igc_en;
	struct drm_msm_pgc_lut vig_igc;
	bool gamut_en;
	struct drm_msm_3d_gamut gamut;
	uint32_t dirty_flags;
};

struct msm_hyp_crtc {
	struct drm_crtc base;
	struct msm_hyp_fence_context *output_fence;
	struct msm_hyp_crtc_info *info;
	struct task_struct *thread;
	struct kthread_worker worker;
	struct completion commit_done;
	struct drm_property_blob *blob_caps;
	struct drm_property_blob *blob_cp_hsic;
};

#define PA_HSIC_HUE_ENABLE (1 << 0)
#define PA_HSIC_SAT_ENABLE (1 << 1)
#define PA_HSIC_VAL_ENABLE (1 << 2)
#define PA_HSIC_CONT_ENABLE (1 << 3)
/**
 * struct msm_hyp_pa_hsic - pa hsic feature structure
 * @flags: flags for the feature customization, values can be:
 *         - PA_HSIC_HUE_ENABLE: Enable hue adjustment
 *         - PA_HSIC_SAT_ENABLE: Enable saturation adjustment
 *         - PA_HSIC_VAL_ENABLE: Enable value adjustment
 *         - PA_HSIC_CONT_ENABLE: Enable contrast adjustment
 *
 * @hue: hue setting
 * @saturation: saturation setting
 * @value: value setting
 * @contrast: contrast setting
 */
struct msm_hyp_pa_hsic {
	__u64 flags;
	__u32 hue;
	__u32 saturation;
	__u32 value;
	__u32 contrast;
};

struct msm_hyp_cp_hsic {
	uint64_t prop_value;
	struct msm_hyp_pa_hsic pa_hsic;
};

struct msm_hyp_crtc_state {
	struct drm_crtc_state base;
	uint32_t input_fence_timeout;
	uint32_t output_fence_offset;
	uint64_t __user *output_fence_ptr;
	struct msm_hyp_cp_hsic cp_hsic;
};

struct msm_hyp_framebuffer {
	struct drm_framebuffer base;
#if IS_ENABLED(CONFIG_DRM_MSM_HYP_VIRTIO)
	struct drm_gem_object *bo;
#endif
	struct msm_hyp_framebuffer_info *info;
};

struct msm_hyp_mode_info {
	uint32_t num_lm;
	uint32_t num_enc;
	uint32_t num_intf;
};

#define to_msm_hyp_connector(x)\
		container_of((x), struct msm_hyp_connector, base)

#define to_msm_hyp_connector_state(x)\
		container_of((x), struct msm_hyp_connector_state, base)

#define to_msm_hyp_plane(x)\
		container_of((x), struct msm_hyp_plane, base)

#define to_msm_hyp_plane_state(x)\
		container_of((x), struct msm_hyp_plane_state, base)

#define to_msm_hyp_crtc(x)\
		container_of((x), struct msm_hyp_crtc, base)

#define to_msm_hyp_crtc_state(x)\
		container_of((x), struct msm_hyp_crtc_state, base)

#define to_msm_hyp_fb(x)\
		container_of((x), struct msm_hyp_framebuffer, base)

struct msm_hyp_kms;

struct msm_hyp_kms_funcs {
	int (*get_connector_infos)(struct msm_hyp_kms *kms,
			struct msm_hyp_connector_info **connector_infos,
			int *connector_num);
	int (*get_plane_infos)(struct msm_hyp_kms *kms,
			struct msm_hyp_plane_info **plane_infos,
			int *plane_num);
	int (*get_crtc_infos)(struct msm_hyp_kms *kms,
			struct msm_hyp_crtc_info **crtc_infos,
			int *crtc_num);
	int (*get_mode_info)(struct msm_hyp_kms *kms,
			const struct drm_display_mode *mode,
			struct msm_hyp_mode_info *modeinfo);
	int (*get_framebuffer_info)(struct msm_hyp_kms *kms,
			struct drm_framebuffer *fb,
			struct msm_hyp_framebuffer_info **fb_info);

	void (*prepare_commit)(struct msm_hyp_kms *kms,
			struct drm_atomic_state *old_state);
	void (*commit)(struct msm_hyp_kms *kms,
			struct drm_atomic_state *old_state);
	void (*complete_commit)(struct msm_hyp_kms *kms,
			struct drm_atomic_state *old_state);

	void (*enable_vblank)(struct msm_hyp_kms *kms,
			struct drm_crtc *crtc);
	void (*disable_vblank)(struct msm_hyp_kms *kms,
			struct drm_crtc *crtc);
	void (*free_connector_port_modes)(
			struct msm_hyp_connector *c_conn);
	void (*register_event)(struct msm_hyp_kms *kms);

};

struct msm_hyp_kms {
	const struct msm_hyp_kms_funcs *funcs;
};

struct msm_hyp_drm_private {
	struct drm_device *dev;
	struct msm_hyp_kms *kms;
	struct drm_driver driver;

	struct drm_property *prop_retire_fence;
	struct drm_property *prop_topology_name;
	struct drm_property *prop_connector_caps;
	struct drm_property *prop_mode_info;
	struct drm_property *prop_zpos;
	struct drm_property *prop_alpha;
	struct drm_property *prop_input_fence;
	struct drm_property *prop_blend_op;
	struct drm_property *prop_src_config;
	struct drm_property *prop_color_fill;
	struct drm_property *prop_scaler;
	struct drm_property *prop_csc;
	struct drm_property *prop_plane_caps;
	struct drm_property *prop_master_plane_id;
	struct drm_property *prop_multirect_mode;
	struct drm_property *prop_fb_translation_mode;
	struct drm_property *prop_input_fence_timeout;
	struct drm_property *prop_output_fence;
	struct drm_property *prop_output_fence_offset;
	struct drm_property *prop_crtc_caps;
	struct drm_property *prop_dma_igc;
	struct drm_property *prop_dma_gc;
	struct drm_property *prop_dma_csc;
	struct drm_property *prop_vig_igc;
	struct drm_property *prop_vig_gamut;
	struct drm_property *prop_inverse_pma;
	struct drm_property *prop_crtc_cp_hsic;

	uint32_t pending_crtcs;
	wait_queue_head_t pending_crtcs_event;

	struct drm_atomic_state *suspend_state;
	struct drm_client_dev client;

	struct blocking_notifier_head component_notifier_list;

	char dev_name_from_dt[DRM_DRI_NAME_SIZE];
	struct kthread_work commit_thread_priority_work;
};

void msm_hyp_set_kms(struct drm_device *dev, struct msm_hyp_kms *kms);
void msm_hyp_crtc_commit_done(struct drm_crtc *crtc);
void msm_hyp_crtc_vblank_done(struct drm_crtc *crtc);
void msm_hyp_send_hpd_event(struct drm_device *dev, struct drm_connector *connector);

#if IS_ENABLED(CONFIG_DRM_MSM_HYP_WFD)
void __init wfd_kms_register(void);
void __exit wfd_kms_unregister(void);
#else
static inline void __init wfd_kms_register(void)
{
}
static inline void __exit wfd_kms_unregister(void)
{
}
#endif /* CONFIG_DRM_MSM_HYP_WFD */

#if IS_ENABLED(CONFIG_DRM_MSM_HYP_VIRTIO)
void __init virtio_kms_register(void);
void __exit virtio_kms_unregister(void);
#else
static inline void __init virtio_kms_register(void)
{
}
static inline void __exit virtio_kms_unregister(void)
{
}
#endif /* CONFIG_DRM_MSM_HYP_VIRTIO */

#endif /* __MSM_DRV_HYP_H__ */
