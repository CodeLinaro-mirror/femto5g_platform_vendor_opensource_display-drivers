/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _HFI_COLOR_PROC_H_
#define _HFI_COLOR_PROC_H_

#include "sde_hw_sspp.h"
#include "sde_crtc.h"

#if IS_ENABLED(CONFIG_MDSS_HFI)

/**
 * hfi_sspp_setup_csc - setup color space conversion in HFI path
 * @ctx: Pointer to pipe context
 * @data: Pointer to config structure
 * @disp_op: Display operation mode (HWIO, HFI)
 */
void hfi_sspp_setup_csc(struct sde_hw_pipe *ctx, struct sde_csc_cfg *data,
	enum msm_disp_op disp_op);

/**
 * hfi_setup_ucsc_igcv1 - set UCSC IGC cp block in HFI path
 * @ctx: Pointer to pipe object
 * @index: Pipe rectangle to operate on
 * @mode: Pointer to sde_hw_cp_cfg object containing IGC mode data
 */
void hfi_setup_ucsc_igcv1(struct sde_hw_pipe *ctx,
		enum sde_sspp_multirect_index index, void *data);

/**
 * hfi_setup_ucsc_gcv1 - api to set UCSC GC cp block in HFI path
 * @ctx: pointer to pipe object
 * @index: pipe rectangle to operate on
 * @data: pointer to sde_hw_cp_cfg object containing gc mode data
 */
void hfi_setup_ucsc_gcv1(struct sde_hw_pipe *ctx,
		enum sde_sspp_multirect_index index, void *data);

/**
 * hfi_setup_ucsc_cscv1 - api to set UCSC CSC cp block in HFI path
 * @ctx: pointer to pipe object
 * @index: pipe rectangle to operate on
 * @data: pointer to sde_hw_cp_cfg object containing drm_msm_ucsc_csc data
 */
void hfi_setup_ucsc_cscv1(struct sde_hw_pipe *ctx,
		enum sde_sspp_multirect_index index, void *data);

/**
 * hfi_setup_ucsc_unmultv1 - api to set UCSC UNMULT cp block in HFI path
 * @ctx: pointer to pipe object
 * @index: pipe rectangle to operate on
 * @data: pointer to sde_hw_cp_cfg object containing bool data
 */
void hfi_setup_ucsc_unmultv1(struct sde_hw_pipe *ctx,
		enum sde_sspp_multirect_index index, void *data);

/**
 * hfi_setup_ucsc_alpha_ditherv1 - set UCSC ALPHA DITHER cp block in HFI path
 * @ctx: Pointer to pipe object
 * @index: Pipe rectangle to operate on
 * @data: Pointer to sde_hw_cp_cfg object containing bool data
 */
void hfi_setup_ucsc_alpha_ditherv1(struct sde_hw_pipe *ctx,
		enum sde_sspp_multirect_index index, void *data);

/**
 * hfi_setup_dspp_pa_dither_v1_7 - setup DSPP dither feature in HFI path
 * @ctx: Pointer to DSPP context
 * @cfg: Pointer to dither data
 */
void hfi_setup_dspp_pa_dither_v1_7(struct sde_hw_dspp *ctx, void *cfg);

/**
 * hfi_setup_dspp_spr_dither_v2 - setup DSPP SPR dither feature in HFI path
 * @ctx: Pointer to DSPP context
 * @cfg: Pointer to dither data
 */
void hfi_setup_dspp_spr_dither_v2(struct sde_hw_dspp *ctx, void *cfg);

/**
 * hfi_setup_demura_backlight_cfg_v4 - setup demura backlight in HFI path
 * @ctx: Pointer to DSPP context
 * @hw_cfg: pointer to sde_hw_cp_cfg containing u32 backlight data
 */
void hfi_setup_demura_backlight_cfg_v4(struct sde_hw_dspp *ctx, struct sde_hw_cp_cfg *hw_cfg);


/**
 * hfi_setup_ltm_initv1_4 - setup LTM init feature in HFI path
 * @ctx: Pointer to DSPP context
 * @cfg: Pointer to sde_hw_cp_cfg object containing ltm init data
 */
void hfi_setup_ltm_initv1_4(struct sde_hw_dspp *ctx, void *cfg);

/**
 * hfi_setup_ltm_roiv1_3 - setup LTM ROI feature in HFI path
 * @ctx: Pointer to DSPP context
 * @cfg: Pointer to sde_hw_cp_cfg object containing ltm roi data
 */
void hfi_setup_ltm_roiv1_3(struct sde_hw_dspp *ctx, void *cfg);

/**
 * hfi_setup_dspp_ltm_hist_ctrlv1_2 - setup LTM hist ctrl programming in HFI path
 * @ctx: Pointer to DSPP context
 * @cfg: Pointer to sde_hw_cp_cfg object containing ltm hist ctrl data
 * @enable: Enable/disable LTM hist ctrl
 * @addr: aligned iova address
 */
void hfi_setup_dspp_ltm_hist_ctrlv1_2(struct sde_hw_dspp *ctx, void *cfg,
				bool enable, u64 addr);

/**
 * hfi_setup_dspp_ltm_threshv1 - setup LTM threshold programming in HFI path
 * @ctx: Pointer to DSPP context
 * @cfg: Pointer to sde_hw_cp_cfg object containing ltm threshold data
 */
void hfi_setup_dspp_ltm_threshv1(struct sde_hw_dspp *ctx, void *cfg);

/**
 * hfi_cp_crtc_set_ltm_buffer - setup LTM buffer programming in HFI path
 * @sde_crtc: Pointer to sde_crtc context
 * @cfg: Pointer to hw config structure
 */
void hfi_cp_crtc_set_ltm_buffer(struct sde_crtc *sde_crtc, void *cfg);

/**
 * hfi_cp_crtc_queue_ltm_buffer - send LTM buffer to FW in HFI path
 * @ltm_buff: Pointer to sde ltm buffer
 * @cfg: Pointer to hw config structure
 *
 * Return: 0 on success, error code otherwise
 */
int hfi_cp_crtc_queue_ltm_buffer(struct sde_ltm_buffer *ltm_buff, void *cfg);

/**
 * hfi_cp_crtc_free_ltm_buffer - free LTM buffer in HFI path
 * @sde_crtc: Pointer to sde_crtc context
 * @cfg: Pointer to hw config structure
 */
void hfi_cp_crtc_free_ltm_buffer(struct sde_crtc *sde_crtc, void *cfg);

/**
 * hfi_cp_crtc_reset_color_props - reset color props
 * @color_props: Pointer to hfi color prop helper
 */
void hfi_cp_crtc_reset_color_props(struct hfi_util_u32_prop_helper *color_props);

/**
 * hfi_cp_crtc_get_color_props_count - get color props count
 * @color_props: Pointer to hfi color prop helper
 */
int hfi_cp_crtc_get_color_props_count(struct hfi_util_u32_prop_helper *color_props);

/**
 * hfi_cp_crtc_unmap_sg_table - unmap sg_table
 * @addr_map: Pointer to hfi shared address map
 * @client: Pointer to hfi client
 */
void hfi_cp_crtc_unmap_sg_table(struct hfi_shared_addr_map *addr_map, struct hfi_client_t *client);

/**
 * hfi_cp_crtc_set_rgb_hist_buffers - Set RGB histogram buffers
 * @sde_crtc: Pointer to sde_crtc context
 * @ctx: Pointer to DSPP context
 * @data: Pointer to RGB histogram buffer configuration
 */
void hfi_cp_crtc_set_rgb_hist_buffers(struct sde_crtc *sde_crtc,
		struct sde_hw_dspp *ctx, void *data);

/**
 * hfi_cp_crtc_queue_rgb_hist_buffer - Queue RGB histogram buffer
 * @sde_crtc: Pointer to sde_crtc context
 * @ctx: Pointer to DSPP context
 * @data: Pointer to RGB histogram buffer fd
 */
void hfi_cp_crtc_queue_rgb_hist_buffer(struct sde_crtc *sde_crtc,
		struct sde_hw_dspp *ctx, void *data);

/**
 * hfi_setup_dspp_rgb_hist_ctrlv2 - Setup DSPP RGB histogram control
 * @hw_dspp: Pointer to DSPP hardware context
 * @data: Pointer to histogram control configuration
 */
int hfi_setup_dspp_rgb_hist_ctrlv2(struct sde_hw_dspp *hw_dspp, void *data);

/**
 * hfi_cp_crtc_free_rgb_hist_buffers - free RGB Hist buffers in HFI path
 * @sde_crtc: Pointer to sde_crtc context
 * @cfg: Pointer to hw config structure
 */
void hfi_cp_crtc_free_rgb_hist_buffers(struct sde_crtc *sde_crtc, void *cfg);

/**
 * hfi_setup_mdnie_art_v1 - api to setup mdnie art programming
 * @ctx: pointer to dspp object.
 * @cfg: Pointer to sde_hw_cp_cfg.
 * @aiqe_top: Pointer to aiqe top level structure
 */
void hfi_setup_mdnie_art_v1(struct sde_hw_dspp *ctx, void *cfg, void *aiqe_top);

/**
 * hfi_cp_crtc_alloc_pa_hist_buffers - allocate PA histogram buffers
 * @sde_crtc: Pointer to sde_crtc context
 *
 * Return: 0 on success, error code otherwise
 */
int hfi_cp_crtc_alloc_pa_hist_buffers(struct sde_crtc *sde_crtc);

/**
 * hfi_cp_crtc_dealloc_pa_hist_buffers - deallocate PA histogram buffers
 * @sde_crtc: Pointer to sde_crtc context
 *
 * Return: 0 on success, error code otherwise
 */
int hfi_cp_crtc_dealloc_pa_hist_buffers(struct sde_crtc *sde_crtc);

/**
 * hfi_cp_crtc_queue_pa_hist_buffer - Queue PA histogram buffer
 * @sde_crtc: Pointer to sde_crtc context
 * @ctx: Pointer to DSPP context
 * @data: Pointer to PA histogram buffer fd
 */
int hfi_cp_crtc_queue_pa_hist_buffer(struct sde_crtc *sde_crtc,
		struct sde_hw_dspp *ctx, void *data);

/**
 * hfi_cp_crtc_clear_pa_hist_buffers - free PA Hist buffers in HFI path
 * @sde_crtc: Pointer to sde_crtc context
 * @data: Pointer to hw config structure
 */
void hfi_cp_crtc_clear_pa_hist_buffers(struct sde_crtc *sde_crtc, void *data);

/**
 * hfi_setup_dspp_hist_v1_7 - Setup DSPP PA histogram control
 * @hw_dspp: Pointer to DSPP hardware context
 * @data: Pointer to histogram control configuration
 * @enable: Enable/disable PA hist ctrl
 */
void hfi_setup_dspp_hist_v1_7(struct sde_hw_dspp *hw_dspp, void *data, bool enable);

#else

void hfi_sspp_setup_csc(struct sde_hw_pipe *ctx, struct sde_csc_cfg *data,
	enum msm_disp_op disp_op)
{
}

void hfi_setup_ucsc_igcv1(struct sde_hw_pipe *ctx,
	enum sde_sspp_multirect_index index, void *data)
{
}

void hfi_setup_ucsc_gcv1(struct sde_hw_pipe *ctx,
	enum sde_sspp_multirect_index index, void *data)
{
}

void hfi_setup_ucsc_cscv1(struct sde_hw_pipe *ctx,
	enum sde_sspp_multirect_index index, void *data)
{
}

void hfi_setup_ucsc_unmultv1(struct sde_hw_pipe *ctx,
	enum sde_sspp_multirect_index index, void *data)
{
}

void hfi_setup_ucsc_alpha_ditherv1(struct sde_hw_pipe *ctx,
	enum sde_sspp_multirect_index index, void *data)
{
}

void hfi_setup_dspp_pa_dither_v1_7(struct sde_hw_dspp *ctx, void *cfg)
{
}

void hfi_setup_dspp_spr_dither_v2(struct sde_hw_dspp *ctx, void *cfg)
{
}

void hfi_setup_demura_backlight_cfg_v4(struct sde_hw_dspp *ctx, struct sde_hw_cp_cfg *hw_cfg)
{
}

void hfi_setup_ltm_initv1_4(struct sde_hw_dspp *ctx, void *cfg)
{
}

void hfi_setup_ltm_roiv1_3(struct sde_hw_dspp *ctx, void *cfg)
{
}

void hfi_setup_dspp_ltm_hist_ctrlv1_2(struct sde_hw_dspp *ctx, void *cfg,
	bool enable, u64 addr)
{
}

void hfi_setup_dspp_ltm_threshv1(struct sde_hw_dspp *ctx, void *cfg)
{
}

void hfi_cp_crtc_set_ltm_buffer(struct sde_crtc *sde_crtc, void *cfg)
{
}

int hfi_cp_crtc_queue_ltm_buffer(struct sde_ltm_buffer *ltm_buff, void *cfg)
{
	return 0;
}

void hfi_cp_crtc_free_ltm_buffer(struct sde_crtc *sde_crtc, void *cfg)
{
}

void hfi_cp_crtc_reset_color_props(struct hfi_util_u32_prop_helper *color_props)
{
}

int hfi_cp_crtc_get_color_props_count(struct hfi_util_u32_prop_helper *color_props)
{
	return 0;
}

void hfi_cp_crtc_unmap_sg_table(struct hfi_shared_addr_map *addr_map, struct hfi_client_t *client)
{
}

void hfi_cp_crtc_set_rgb_hist_buffers(struct sde_crtc *sde_crtc,
		struct sde_hw_dspp *ctx, void *data)
{
}

void hfi_cp_crtc_queue_rgb_hist_buffer(struct sde_crtc *sde_crtc,
	struct sde_hw_dspp *ctx, void *data)
{
}

int hfi_setup_dspp_rgb_hist_ctrlv2(struct sde_hw_dspp *hw_dspp, void *data)
{
	return 0;
}

void hfi_cp_crtc_free_rgb_hist_buffers(struct sde_crtc *sde_crtc, void *cfg)
{
}

void hfi_setup_mdnie_art_v1(struct sde_hw_dspp *ctx, void *cfg, void *aiqe_top)
{
}

int hfi_cp_crtc_alloc_pa_hist_buffers(struct sde_crtc *sde_crtc)
{
	return 0;
}

int hfi_cp_crtc_dealloc_pa_hist_buffers(struct sde_crtc *sde_crtc)
{
	return 0;
}

int hfi_cp_crtc_queue_pa_hist_buffer(struct sde_crtc *sde_crtc,
		struct sde_hw_dspp *ctx, void *data)
{
	return 0;
}

void hfi_cp_crtc_clear_pa_hist_buffers(struct sde_crtc *sde_crtc, void *data)
{
}

int hfi_setup_dspp_hist_v1_7(struct sde_hw_dspp *hw_dspp, void *data, bool enable)
{
	return 0;
}


#endif // CONFIG_MDSS_HFI

#if IS_ENABLED(CONFIG_QTI_HFI_CORE)

/**
 * _hfi_cp_crtc_get_mapped_iova - Retrieve the mapped IOVA address from HFI shared address map
 * @addr_map: Pointer to the HFI shared address map structure
 *
 * Returns a pointer to the mapped IOVA field if HFI core is enabled
 */
static inline unsigned long *_hfi_cp_crtc_get_mapped_iova(struct hfi_shared_addr_map *addr_map)
{
	return &addr_map->alloc_info.mapped_iova;
}

#else

static inline unsigned long *_hfi_cp_crtc_get_mapped_iova(struct hfi_shared_addr_map *addr_map)
{
	return NULL;
}
#endif // CONFIG_QTI_HFI_CORE

#endif /* _HFI_COLOR_PROC_H_ */
