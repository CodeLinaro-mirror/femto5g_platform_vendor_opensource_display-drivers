// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt) "[drm:%s:%d] " fmt, __func__, __LINE__

#include "hfi_color_proc.h"
#include "hfi_defs_layer_color.h"
#include "hfi_defs_display_color.h"
#include "hfi_properties_display.h"
#include "sde_kms.h"
#include "sde_hw_dspp.h"
#include "hfi_kms.h"

static struct hfi_display_pa_dither hfi_pa_dither_cached[DPU_MAX][DSPP_MAX] = {};
static u32 hfi_demura_backlight_cached[DPU_MAX][DSPP_MAX] = {};
static struct hfi_display_ltm_init_param hfi_ltm_init_cached[DPU_MAX][DSPP_MAX] = {};
static struct hfi_display_ltm_cfg_param hfi_ltm_roi_cached[DPU_MAX][DSPP_MAX] = {};
static u32 hfi_ltm_hist_ctrl_cached[DPU_MAX][DSPP_MAX] = {};
static u32 hfi_ltm_thresh_cached[DPU_MAX][DSPP_MAX] = {};
static struct hfi_display_rgb_hist_ctrl hfi_rgb_hist_ctrl_cached[DPU_MAX][DSPP_MAX] = {};
static u32 hfi_pa_hist_ctrl_cached[DPU_MAX][DSPP_MAX] = {};

#define U32_MASK_LOW 0xFFFFFFFFU
#define U32_SHIFT_BITS 32
#define HFI_PA_HIST_BUFFER_SIZE 2048

void hfi_sspp_setup_csc(struct sde_hw_pipe *ctx, struct sde_csc_cfg *data, enum msm_disp_op disp_op)
{
	struct hfi_csc hfi_cfg;
	int ret = 0;
	u32 prop_id = HFI_PROPERTY_LAYER_COLOR_CSC;

	if (!ctx || !data) {
		SDE_ERROR("invalid parameter ctx: %pK data: %pK\n", ctx, data);
		return;
	}

	prop_id = HFI_PACK_VERSION(1, 0, prop_id);
	hfi_cfg.flags = HFI_COLOR_LAYER_FEATURE_ENABLE_FLAG;
	for (int i = 0; i < HFI_CSC_MATRIX_COEFF_SIZE; i++)
		hfi_cfg.ctm_coeff[i] = data->csc_mv[i];

	for (int i = 0; i < HFI_CSC_BIAS_SIZE; i++) {
		hfi_cfg.pre_bias[i] = data->csc_pre_bv[i];
		hfi_cfg.post_bias[i] = data->csc_post_bv[i];
	}

	for (int i = 0; i < HFI_CSC_CLAMP_SIZE; i++) {
		hfi_cfg.pre_clamp[i] = data->csc_pre_lv[i];
		hfi_cfg.post_clamp[i] = data->csc_post_lv[i];
	}

	ret = hfi_util_u32_prop_helper_add_prop_by_obj(ctx->prop_helper,
		prop_id, ctx->obj_id, HFI_VAL_U32_ARRAY, &hfi_cfg,
		sizeof(struct hfi_csc));

	if (ret)
		SDE_ERROR("failed to add HFI prop: %d ret: %d\n", prop_id, ret);

}

void hfi_setup_ucsc_igcv1(struct sde_hw_pipe *ctx,
		enum sde_sspp_multirect_index index, void *data)
{
	struct sde_hw_cp_cfg *hw_cfg = data;
	u32 hfi_cfg = UCSC_IGC_MODE_DISABLE;
	int *ucsc_igc;
	int ret = 0;

	if (!ctx || !data || index == SDE_SSPP_RECT_MAX) {
		SDE_ERROR("invalid parameter ctx: %pK data: %pK index: %d\n",
				ctx, data, index);
		return;
	}

	hw_cfg->prop_id = HFI_PACK_VERSION(1, 1, hw_cfg->prop_id);
	ucsc_igc = (int *)(hw_cfg->payload);
	if (!ucsc_igc || (hw_cfg->len != sizeof(int))) {
		SDE_ERROR("invalid payload pipe: %d index: %d payload: %pK len: %d\n",
				ctx->idx, index, ucsc_igc, hw_cfg->len);
		return;
	}

	switch (*ucsc_igc) {
	case UCSC_IGC_MODE_SRGB:
		hfi_cfg = HFI_COLOR_LAYER_IGC_SRGB;
		break;
	case UCSC_IGC_MODE_REC709:
		hfi_cfg = HFI_COLOR_LAYER_IGC_709;
		break;
	case UCSC_IGC_MODE_GAMMA2_2:
		hfi_cfg = HFI_COLOR_LAYER_IGC_2_2;
		break;
	case UCSC_IGC_MODE_HLG:
		hfi_cfg = HFI_COLOR_LAYER_IGC_HLG;
		break;
	case UCSC_IGC_MODE_PQ:
		hfi_cfg = HFI_COLOR_LAYER_IGC_PQ;
		break;
	case UCSC_IGC_MODE_DISABLE:
		hfi_cfg = HFI_COLOR_LAYER_IGC_NONE;
		break;
	default:
		SDE_ERROR("Invalid UCSC IGC mode: %d\n", *ucsc_igc);
		return;
	}

	ret = hfi_util_u32_prop_helper_add_prop_by_obj(hw_cfg->prop_helper, hw_cfg->prop_id,
			hw_cfg->obj_id, HFI_VAL_U32, &hfi_cfg, sizeof(u32));
	if (ret)
		SDE_ERROR("failed to add HFI prop: %d ret: %d\n", hw_cfg->prop_id, ret);
}

void hfi_setup_ucsc_gcv1(struct sde_hw_pipe *ctx,
		enum sde_sspp_multirect_index index, void *data)
{
	struct sde_hw_cp_cfg *hw_cfg = data;
	u32 hfi_cfg = UCSC_GC_MODE_DISABLE;
	int *ucsc_gc;
	int ret = 0;

	if (!ctx || !data || index == SDE_SSPP_RECT_MAX) {
		SDE_ERROR("invalid parameter ctx: %pK data: %pK index: %d\n",
			ctx, data, index);
		return;
	}

	hw_cfg->prop_id = HFI_PACK_VERSION(1, 1, hw_cfg->prop_id);
	ucsc_gc = (int *)(hw_cfg->payload);
	if (!ucsc_gc || (hw_cfg->len != sizeof(int))) {
		SDE_ERROR("invalid payload pipe: %d index: %d payload: %pK len: %d\n",
			ctx->idx, index, ucsc_gc, hw_cfg->len);
		return;
	}

	switch (*ucsc_gc) {
	case UCSC_GC_MODE_SRGB:
		hfi_cfg = HFI_COLOR_LAYER_GC_SRGB;
		break;
	case UCSC_GC_MODE_PQ:
		hfi_cfg = HFI_COLOR_LAYER_GC_PQ;
		break;
	case UCSC_GC_MODE_GAMMA2_2:
		hfi_cfg = HFI_COLOR_LAYER_GC_2_2;
		break;
	case UCSC_GC_MODE_HLG:
		hfi_cfg = HFI_COLOR_LAYER_GC_HLG;
		break;
	case UCSC_GC_MODE_DISABLE:
		hfi_cfg = HFI_COLOR_LAYER_GC_NONE;
		break;
	default:
		SDE_ERROR("Invalid UCSC GC mode: %d\n", *ucsc_gc);
		return;
	}

	ret = hfi_util_u32_prop_helper_add_prop_by_obj(hw_cfg->prop_helper, hw_cfg->prop_id,
			hw_cfg->obj_id, HFI_VAL_U32, &hfi_cfg, sizeof(u32));
	if (ret)
		SDE_ERROR("failed to add HFI prop: %d ret: %d\n", hw_cfg->prop_id, ret);
}

void hfi_setup_ucsc_cscv1(struct sde_hw_pipe *ctx,
		enum sde_sspp_multirect_index index, void *data)
{
	struct sde_hw_cp_cfg *hw_cfg = data;
	struct hfi_ucsc_csc hfi_cfg;
	drm_msm_ucsc_csc *ucsc_csc;
	int ret = 0;

	if (!ctx || !data || index == SDE_SSPP_RECT_MAX) {
		SDE_ERROR("invalid parameter ctx: %pK data: %pK index: %d\n",
			ctx, data, index);
		return;
	}

	hw_cfg->prop_id = HFI_PACK_VERSION(1, 1, hw_cfg->prop_id);
	ucsc_csc = (drm_msm_ucsc_csc *)(hw_cfg->payload);
	if (!ucsc_csc) {
		hfi_cfg.flags = 0;
		goto send_payload;
	}

	if (hw_cfg->len != sizeof(drm_msm_ucsc_csc)) {
		SDE_ERROR("invalid payload length pipe: %d index: %d len: %d expected len: %lu\n",
			ctx->idx, index, hw_cfg->len, sizeof(drm_msm_ucsc_csc));
		return;
	}

	hfi_cfg.flags = HFI_COLOR_LAYER_FEATURE_ENABLE_FLAG;
	for (int i = 0; i < HFI_UCSC_CSC_MATRIX_COEFF_SIZE; i++)
		hfi_cfg.ctm_coeff[i] = ucsc_csc->cfg_param_0[i];

	for (int i = 0; i < HFI_UCSC_CSC_PRE_CLAMP_SIZE; i++)
		hfi_cfg.pre_clamp[i] = ucsc_csc->cfg_param_1[i];

	for (int i = 0; i < HFI_UCSC_CSC_POST_CLAMP_SIZE; i++)
		hfi_cfg.post_clamp[i] = ucsc_csc->cfg_param_1[HFI_UCSC_CSC_PRE_CLAMP_SIZE + i];

send_payload:
	ret = hfi_util_u32_prop_helper_add_prop_by_obj(hw_cfg->prop_helper, hw_cfg->prop_id,
			hw_cfg->obj_id, HFI_VAL_U32_ARRAY, &hfi_cfg, sizeof(struct hfi_ucsc_csc));
	if (ret)
		SDE_ERROR("failed to add HFI prop: %d ret: %d\n", hw_cfg->prop_id, ret);
}

void hfi_setup_ucsc_unmultv1(struct sde_hw_pipe *ctx,
		enum sde_sspp_multirect_index index, void *data)
{
	u32 hfi_cfg = 0;
	struct sde_hw_cp_cfg *hw_cfg = data;
	bool *ucsc_unmult;
	int ret = 0;

	if (!ctx || !data || index == SDE_SSPP_RECT_MAX) {
		SDE_ERROR("invalid parameter ctx: %pK data: %pK index: %d\n",
			ctx, data, index);
		return;
	} else if (!hw_cfg->payload || hw_cfg->len != sizeof(bool)) {
		SDE_ERROR("invalid payload pipe: %d index: %d payload: %pK len: %d\n",
			ctx->idx, index, hw_cfg->payload, hw_cfg->len);
		return;
	}

	hw_cfg->prop_id = HFI_PACK_VERSION(1, 1, hw_cfg->prop_id);
	ucsc_unmult = (bool *)(hw_cfg->payload);
	if (ucsc_unmult && *ucsc_unmult)
		hfi_cfg = HFI_COLOR_LAYER_FEATURE_ENABLE_FLAG;

	ret = hfi_util_u32_prop_helper_add_prop_by_obj(hw_cfg->prop_helper, hw_cfg->prop_id,
			hw_cfg->obj_id, HFI_VAL_U32, &hfi_cfg, sizeof(u32));
	if (ret)
		SDE_ERROR("failed to add HFI prop: %d ret: %d\n", hw_cfg->prop_id, ret);
}

void hfi_setup_ucsc_alpha_ditherv1(struct sde_hw_pipe *ctx,
		enum sde_sspp_multirect_index index, void *data)
{
	u32 hfi_cfg = 0;
	struct sde_hw_cp_cfg *hw_cfg = data;
	bool *ucsc_alpha_dither;
	int ret = 0;

	if (!ctx || !data || index == SDE_SSPP_RECT_MAX) {
		SDE_ERROR("invalid parameter ctx: %pK data: %pK index: %d\n",
			ctx, data, index);
		return;
	} else if (!hw_cfg->payload || hw_cfg->len != sizeof(bool)) {
		SDE_ERROR("invalid payload pipe: %d index: %d payload: %pK len: %d\n",
			ctx->idx, index, hw_cfg->payload, hw_cfg->len);
		return;
	}

	hw_cfg->prop_id = HFI_PACK_VERSION(1, 0, hw_cfg->prop_id);
	ucsc_alpha_dither  = (bool *)(hw_cfg->payload);
	if (ucsc_alpha_dither && *ucsc_alpha_dither)
		hfi_cfg = HFI_COLOR_LAYER_FEATURE_ENABLE_FLAG;

	ret = hfi_util_u32_prop_helper_add_prop_by_obj(hw_cfg->prop_helper, hw_cfg->prop_id,
			hw_cfg->obj_id, HFI_VAL_U32, &hfi_cfg, sizeof(u32));
	if (ret)
		SDE_ERROR("failed to add HFI prop: %d ret: %d\n", hw_cfg->prop_id, ret);
}

/* Helper function to send hfi_buff packet for AHB non-broadcast use-case */
static int hfi_buff_send_payload(void *cfg, void *hfi_cfg, u32 prop_id,
	u32 major_ver, u32 minor_ver)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct hfi_shared_addr_map *hfi_buff_map = NULL;
	u32 ret = 0, indx, payload_size;
	struct hfi_buff prop_hfi_buff;
	u64 fw_buff_addr;

	hfi_buff_map = hw_cfg->hfi_buff_map;
	hfi_cfg = (struct hfi_display_dither *)hfi_cfg;
	payload_size = sizeof(struct hfi_display_dither);

	if (!hfi_buff_map || !hfi_buff_map->remote_addr ||
		!hfi_buff_map->local_addr) {
		SDE_ERROR("Invalid inputs: hfi_buff_map %pK, remote_addr %lu, local_addr %pK\n",
			hfi_buff_map, (hfi_buff_map ? hfi_buff_map->remote_addr : 0),
			(hfi_buff_map ? hfi_buff_map->local_addr : NULL));
		return -EINVAL;
	}

	if (hw_cfg->dspp_idx < hw_cfg->dspp_start_idx) {
		SDE_ERROR("Invalid dspp_idx %d or dspp_start_idx %d\n", hw_cfg->dspp_idx,
				hw_cfg->dspp_start_idx);
		return -EINVAL;
	}

	indx = (hw_cfg->dspp_idx - hw_cfg->dspp_start_idx) * payload_size;
	if (indx + payload_size > hfi_buff_map->size) {
		SDE_ERROR("Not enough memory left, remaining size %u, payload_size %u\n",
			hfi_buff_map->size - indx, payload_size);
		return -EINVAL;
	}
	memcpy(hfi_buff_map->local_addr + indx, hfi_cfg, payload_size);

	/* non-broadcast and it is the last dspp idx - send the packet */
	if (hw_cfg->dspp_idx == (hw_cfg->dspp_start_idx + hw_cfg->num_of_mixers - 1)) {
		// populate hfi_buff to send over hfi packet.
		fw_buff_addr = (u64) hfi_buff_map->remote_addr;
		prop_hfi_buff.addr_l = (fw_buff_addr & 0xFFFFFFFF);
		prop_hfi_buff.addr_h = (fw_buff_addr >> 32);
		prop_hfi_buff.size = (payload_size / sizeof(u32)) * hw_cfg->num_of_mixers;

		ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper,
				HFI_PACK_VERSION(major_ver, minor_ver, prop_id),
				HFI_VAL_U32_ARRAY, &prop_hfi_buff,
				sizeof(struct hfi_buff));
		if (ret)
			SDE_ERROR("Failed to add hfi prop %d ret %d\n", prop_id, ret);
		else
			SDE_DEBUG("non-broadcast feature: submitted to prop_helper\n");
	}

	return ret;
}

void hfi_setup_dspp_pa_dither_v1_7(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct drm_msm_pa_dither *dither;
	u32 i, ret = 0;
	u32 payload_size = sizeof(struct hfi_display_pa_dither);
	u32 prop_id = HFI_PACK_VERSION(1, 7, HFI_PROPERTY_DISPLAY_COLOR_PA_DITHER);
	struct hfi_display_pa_dither *hfi_cfg = NULL;

	if (!hw_cfg || (hw_cfg->len != sizeof(struct drm_msm_pa_dither) &&
			hw_cfg->payload)) {
		SDE_ERROR("hw %pK payload %pK size %d expected sz %zd\n",
			hw_cfg, ((hw_cfg) ? hw_cfg->payload : NULL),
			((hw_cfg) ? hw_cfg->len : 0),
			sizeof(struct drm_msm_pa_dither));
		return;
	}

	if (ctx->dpu_idx < DPU_0 || ctx->dpu_idx >= DPU_MAX) {
		SDE_ERROR("Invalid dpu idx: %d\n", ctx->dpu_idx);
		return;
	}

	hfi_cfg = &hfi_pa_dither_cached[ctx->dpu_idx][hw_cfg->dspp_idx];

	if (!hw_cfg->payload) {
		/* Turn off feature */
		hfi_cfg->flags = 0;
	} else {
		/* Turn on feature */
		hfi_cfg->flags = HFI_DISPLAY_COLOR_FLAGS_FEATURE_ENABLE;
		dither = hw_cfg->payload;
		for (i = 0; i < DITHER_MATRIX_SZ; i++)
			hfi_cfg->matrix[i] = dither->matrix[i];
		hfi_cfg->strength = dither->strength;
		hfi_cfg->offset_en = dither->offset_en;
	}

	if (hw_cfg->dspp_idx == (hw_cfg->dspp_start_idx + hw_cfg->num_of_mixers - 1)) {
		/* non-broadcast and it is the last dspp idx - send the payload */
		ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper,
				prop_id, HFI_VAL_U32_ARRAY,
				&hfi_pa_dither_cached[ctx->dpu_idx][hw_cfg->dspp_start_idx],
				payload_size * hw_cfg->num_of_mixers);
		if (ret)
			SDE_ERROR("Failed to add hfi prop for PA dither %d ret %d\n",
				prop_id, ret);
		else
			SDE_DEBUG("non-broadcast feature: submitted to prop_helper\n");
		/* reset the cached struct for current dpu_idx after submitting to FW */
		memset(&hfi_pa_dither_cached[ctx->dpu_idx], 0,
			   sizeof(hfi_pa_dither_cached[ctx->dpu_idx]));
	}
}

void hfi_setup_dspp_spr_dither_v2(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct drm_msm_dither *dither;
	u32 i, ret = 0, row, col;
	struct hfi_display_dither hfi_cfg = {};

	if (!hw_cfg || (hw_cfg->len != sizeof(struct drm_msm_dither) &&
			hw_cfg->payload)) {
		DRM_ERROR("hw %pK payload %pK size %d expected sz %zd\n",
			hw_cfg, ((hw_cfg) ? hw_cfg->payload : NULL),
			((hw_cfg) ? hw_cfg->len : 0),
			sizeof(struct drm_msm_dither));
		return;
	}

	if (!hw_cfg->payload) {
		/* Turn off feature */
		DRM_DEBUG_DRIVER("Disable DSPP SPR dither feature\n");
		hfi_cfg.flags = 0;
	} else {
		/* Turn on feature */
		DRM_DEBUG_DRIVER("Enable DSPP SPR dither feature\n");
		hfi_cfg.flags = HFI_DISPLAY_COLOR_FLAGS_FEATURE_ENABLE;

		dither = hw_cfg->payload;
		hfi_cfg.feature_flags = dither->flags;
		hfi_cfg.temporal_en = dither->temporal_en;
		hfi_cfg.c0_bitdepth = dither->c0_bitdepth;
		hfi_cfg.c1_bitdepth = dither->c1_bitdepth;
		hfi_cfg.c2_bitdepth = dither->c2_bitdepth;
		hfi_cfg.c3_bitdepth = dither->c3_bitdepth;
		hfi_cfg.dither_matrix_select =
			dither->dither_matrix_select ? (dither->dither_matrix_select - 1) : 0;

		if (dither->dither_matrix_select == DITHER_MATRIX_SELECT_NONE) {
			/* Legacy Matrix */
			for (i = 0; i < HFI_DITHER_MATRIX_SZ; i++) {
				row = i / 4;
				col = i % 4;
				hfi_cfg.matrix[row * 16 + col] = dither->matrix[i];
			}
		} else {
			memcpy(hfi_cfg.matrix, dither->dither_matrix_extended,
				HFI_DITHER_MATRIX_SZ_EXTENDED * sizeof(s32));
		}
	}

	ret = hfi_buff_send_payload(cfg, &hfi_cfg,
			HFI_PROPERTY_DISPLAY_COLOR_SPR_DITHER, 2, 0);
	if (ret)
		SDE_ERROR("Failed to send hfi_buff from SPR dither ret: %d\n", ret);
}

void hfi_setup_demura_backlight_cfg_v4(struct sde_hw_dspp *ctx, struct sde_hw_cp_cfg *hw_cfg)
{
	u32 backlight = 0;
	u32 ret = 0;
	u32 prop_id = HFI_PACK_VERSION(4, 0, HFI_PROPERTY_DISPLAY_COLOR_DEMURA_BACKLIGHT);

	if (!ctx || !hw_cfg || (hw_cfg->len != sizeof(u64) && hw_cfg->payload)) {
		DRM_ERROR("ctx %pK hw_cfg %pK payload %pK size %d expected sz %zd\n",
			ctx, hw_cfg, ((hw_cfg) ? hw_cfg->payload : NULL),
			((hw_cfg) ? hw_cfg->len : 0), sizeof(u64));
		return;
	}

	if (ctx->dpu_idx < DPU_0 || ctx->dpu_idx >= DPU_MAX) {
		SDE_ERROR("Invalid dpu idx: %d\n", ctx->dpu_idx);
		return;
	}

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable demura backlight feature\n");
		hfi_demura_backlight_cached[ctx->dpu_idx][hw_cfg->dspp_idx] = 0;
	} else {
		backlight = (*((u64 *)(hw_cfg->payload)) & REG_MASK(32));
		hfi_demura_backlight_cached[ctx->dpu_idx][hw_cfg->dspp_idx] = backlight;
	}

	if (hw_cfg->dspp_idx == (hw_cfg->dspp_start_idx + hw_cfg->num_of_mixers - 1)) {
		ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper,
				prop_id, HFI_VAL_U32_ARRAY,
				&hfi_demura_backlight_cached[ctx->dpu_idx][hw_cfg->dspp_start_idx],
				sizeof(u32) * hw_cfg->num_of_mixers);
		if (ret)
			SDE_ERROR("Failed to add hfi prop for demura backlight %d ret %d\n",
				prop_id, ret);
		else
			SDE_DEBUG("non-broadcast feature %d: submitted to prop_helper\n",
				HFI_PROPERTY_DISPLAY_COLOR_DEMURA_BACKLIGHT);
	}
}

void hfi_setup_ltm_initv1_4(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct drm_msm_ltm_init_param *ltm_init = NULL;
	int ret = 0;
	struct hfi_display_ltm_init_param *hfi_cfg;
	u32 payload_size = sizeof(struct hfi_display_ltm_init_param);

	if (!hw_cfg || (hw_cfg->len != sizeof(struct drm_msm_ltm_init_param) &&
			hw_cfg->payload)) {
		SDE_ERROR("invalid params hw_cfg %pK payload %pK size %d expected sz %zd\n",
			hw_cfg, ((hw_cfg) ? hw_cfg->payload : NULL),
			((hw_cfg) ? hw_cfg->len : 0),
			sizeof(struct drm_msm_ltm_init_param));
		return;
	}

	if (ctx->dpu_idx < DPU_0 || ctx->dpu_idx >= DPU_MAX) {
		SDE_ERROR("Invalid dpu idx: %d\n", ctx->dpu_idx);
		return;
	}

	hfi_cfg = &hfi_ltm_init_cached[ctx->dpu_idx][hw_cfg->dspp_idx];
	hw_cfg->prop_id = HFI_PACK_VERSION(1, 4, hw_cfg->prop_id);

	if (!hw_cfg->payload) {
		/* Turn off feature */
		SDE_DEBUG("Disable LTM feature\n");
		hfi_cfg->flags = 0;
	} else {
		/* Turn on feature */
		SDE_DEBUG("Enable LTM feature\n");
		hfi_cfg->flags = HFI_DISPLAY_COLOR_FLAGS_FEATURE_ENABLE;
		ltm_init = hw_cfg->payload;
		hfi_cfg->init_param_01 = ltm_init->init_param_01;
		hfi_cfg->init_param_02 = ltm_init->init_param_02;
		hfi_cfg->init_param_03 = ltm_init->init_param_03;
		hfi_cfg->init_param_04 = ltm_init->init_param_04;
	}

	if (hw_cfg->dspp_idx == (hw_cfg->dspp_start_idx + hw_cfg->num_of_mixers - 1)) {
		/* non-broadcast and it is the last dspp idx - send the payload */
		ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper,
				hw_cfg->prop_id, HFI_VAL_U32_ARRAY,
				&hfi_ltm_init_cached[ctx->dpu_idx][hw_cfg->dspp_start_idx],
				payload_size * hw_cfg->num_of_mixers);
		if (ret)
			SDE_ERROR("Failed to add hfi prop for LTM INIT %d ret %d\n",
				hw_cfg->prop_id, ret);
		else
			SDE_DEBUG("non-broadcast feature %d: submitted to prop_helper\n",
				hw_cfg->prop_id);
		/* reset the cached struct for current dpu_idx after submitting to FW */
		memset(&hfi_ltm_init_cached[ctx->dpu_idx], 0,
			   sizeof(hfi_ltm_init_cached[ctx->dpu_idx]));
	}
}

void hfi_setup_ltm_roiv1_3(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct drm_msm_ltm_cfg_param *ltm_cfg = NULL;
	int ret = 0;
	struct hfi_display_ltm_cfg_param *hfi_cfg;
	u32 payload_size = sizeof(struct hfi_display_ltm_cfg_param);

	if (!hw_cfg || (hw_cfg->len != sizeof(struct drm_msm_ltm_cfg_param) &&
			hw_cfg->payload)) {
		SDE_ERROR("invalid params hw_cfg %pK payload %pK size %d expected sz %zd\n",
			hw_cfg, ((hw_cfg) ? hw_cfg->payload : NULL),
			((hw_cfg) ? hw_cfg->len : 0),
			sizeof(struct drm_msm_ltm_cfg_param));
		return;
	}

	if (ctx->dpu_idx < DPU_0 || ctx->dpu_idx >= DPU_MAX) {
		SDE_ERROR("Invalid dpu idx: %d\n", ctx->dpu_idx);
		return;
	}

	hfi_cfg = &hfi_ltm_roi_cached[ctx->dpu_idx][hw_cfg->dspp_idx];
	hw_cfg->prop_id = HFI_PACK_VERSION(1, 3, HFI_PROPERTY_DISPLAY_COLOR_LTM_CFG);
	if (!hw_cfg->payload) {
		/* Turn off feature */
		hfi_cfg->flags = 0;
		SDE_DEBUG("Disable LTM ROI feature\n");
	} else {
		/* Turn on feature */
		SDE_DEBUG("Enable LTM ROI feature\n");
		hfi_cfg->flags = HFI_DISPLAY_COLOR_FLAGS_FEATURE_ENABLE;
		ltm_cfg = hw_cfg->payload;
		hfi_cfg->cfg_param_01 = ltm_cfg->cfg_param_01;
		hfi_cfg->cfg_param_02 = ltm_cfg->cfg_param_02;
		hfi_cfg->cfg_param_03 = ltm_cfg->cfg_param_03;
		hfi_cfg->cfg_param_04 = ltm_cfg->cfg_param_04;
		hfi_cfg->cfg_param_05 = ltm_cfg->cfg_param_05;
		hfi_cfg->cfg_param_06 = ltm_cfg->cfg_param_06;
	}

	if (hw_cfg->dspp_idx == (hw_cfg->dspp_start_idx + hw_cfg->num_of_mixers - 1)) {
		/* non-broadcast and it is the last dspp idx - send the payload */
		ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper,
				hw_cfg->prop_id, HFI_VAL_U32_ARRAY,
				&hfi_ltm_roi_cached[ctx->dpu_idx][hw_cfg->dspp_start_idx],
				payload_size * hw_cfg->num_of_mixers);
		if (ret)
			SDE_ERROR("Failed to add hfi prop for LTM ROI %d ret %d\n",
				hw_cfg->prop_id, ret);
		else
			SDE_DEBUG("non-broadcast feature %d: submitted to prop_helper\n",
				hw_cfg->prop_id);

		/* reset the cached struct for current dpu_idx after submitting to FW */
		memset(&hfi_ltm_roi_cached[ctx->dpu_idx], 0,
			   sizeof(hfi_ltm_roi_cached[ctx->dpu_idx]));
	}
}

void hfi_setup_dspp_ltm_hist_ctrlv1_2(struct sde_hw_dspp *ctx, void *cfg,
				    bool enable, u64 addr)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	int ret = 0;
	u32 *hfi_cfg;
	u32 payload_size = sizeof(u32);

	if (!hw_cfg || !ctx) {
		DRM_ERROR("invalid params hw_cfg %pK\n", hw_cfg);
		return;
	}

	if (ctx->dpu_idx < DPU_0 || ctx->dpu_idx >= DPU_MAX) {
		SDE_ERROR("Invalid dpu idx: %d\n", ctx->dpu_idx);
		return;
	}


	hfi_cfg = &hfi_ltm_hist_ctrl_cached[ctx->dpu_idx][hw_cfg->dspp_idx];
	hw_cfg->prop_id = HFI_PACK_VERSION(1, 2, hw_cfg->prop_id);

	*hfi_cfg = enable;
	if (hw_cfg->dspp_idx == (hw_cfg->dspp_start_idx + hw_cfg->num_of_mixers - 1)) {
		/* non-broadcast and it is the last dspp idx - send the payload */
		ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper,
				hw_cfg->prop_id, HFI_VAL_U32_ARRAY,
				&hfi_ltm_hist_ctrl_cached[ctx->dpu_idx][hw_cfg->dspp_start_idx],
				payload_size * hw_cfg->num_of_mixers);
		if (ret)
			SDE_ERROR("Failed to add hfi prop for LTM HIST CTRL %d ret %d\n",
				hw_cfg->prop_id, ret);
		else
			SDE_DEBUG("non-broadcast feature %d: submitted to prop_helper\n",
				hw_cfg->prop_id);

		/* reset the cached struct for current dpu_idx after submitting to FW */
		memset(&hfi_ltm_hist_ctrl_cached[ctx->dpu_idx], 0,
			   sizeof(hfi_ltm_hist_ctrl_cached[ctx->dpu_idx]));
	}
}

void hfi_setup_dspp_ltm_threshv1(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	int ret = 0;
	u32 *hfi_cfg;
	u32 payload_size = sizeof(u32);

	if (!hw_cfg) {
		SDE_ERROR("invalid params hw_cfg %pK\n", hw_cfg);
		return;
	}

	if (ctx->dpu_idx < DPU_0 || ctx->dpu_idx >= DPU_MAX) {
		SDE_ERROR("Invalid dpu idx: %d\n", ctx->dpu_idx);
		return;
	}

	hfi_cfg = &hfi_ltm_thresh_cached[ctx->dpu_idx][hw_cfg->dspp_idx];
	hw_cfg->prop_id = HFI_PACK_VERSION(1, 0, hw_cfg->prop_id);
	if (!hw_cfg->payload) {
		SDE_DEBUG("Disable LTM noise thresh feature\n");
		*hfi_cfg = 0;
	} else {
		SDE_DEBUG("Enable LTM noise thresh feature\n");
		*hfi_cfg = *((u64 *)hw_cfg->payload);
	}

	if (hw_cfg->dspp_idx == (hw_cfg->dspp_start_idx + hw_cfg->num_of_mixers - 1)) {
		/* non-broadcast and it is the last dspp idx - send the payload */
		ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper,
				hw_cfg->prop_id, HFI_VAL_U32_ARRAY,
				&hfi_ltm_thresh_cached[ctx->dpu_idx][hw_cfg->dspp_start_idx],
				payload_size * hw_cfg->num_of_mixers);
		if (ret)
			SDE_ERROR("Failed to add hfi prop for LTM NOISE THRESH %d ret %d\n",
				hw_cfg->prop_id, ret);
		else
			SDE_DEBUG("non-broadcast feature %d: submitted to prop_helper\n",
				hw_cfg->prop_id);

		/* reset the cached struct for current dpu_idx after submitting to FW */
		memset(&hfi_ltm_thresh_cached[ctx->dpu_idx], 0,
			   sizeof(hfi_ltm_thresh_cached[ctx->dpu_idx]));
	}

}

void hfi_cp_crtc_set_ltm_buffer(struct sde_crtc *sde_crtc, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct drm_msm_ltm_buffers_ctrl *buf_cfg = NULL;
	struct sg_table *sgt = NULL;
	unsigned long *mapped_iova = NULL;
	u32 i = 0, num = 0;
	int ret = 0;
	u32 payload = 0;
	u32 clear_buffs_prop_id = HFI_PROPERTY_DISPLAY_COLOR_LTM_CLEAR_BUFS;

	if (!sde_crtc || !cfg) {
		SDE_ERROR("invalid parameters sde_crtc %pK cfg %pK\n", sde_crtc,
				cfg);
		return;
	}

	if (sde_crtc->do_clear_buf) {
		clear_buffs_prop_id = HFI_PACK_VERSION(1, 0,
			HFI_PROPERTY_DISPLAY_COLOR_LTM_CLEAR_BUFS);
		ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper, clear_buffs_prop_id,
				HFI_VAL_U32_ARRAY, &payload, sizeof(u32));
		sde_crtc->do_clear_buf = false;
	}

	buf_cfg = hw_cfg->payload;
	num = buf_cfg->num_of_buffers;
	if (num == 0 || num > LTM_BUFFER_SIZE) {
		SDE_ERROR("invalid buffer size %d\n", num);
		return;
	}

	if (sde_crtc->ltm_buffer_cnt) {
		SDE_DEBUG("%d ltm_buffers already allocated\n",
			sde_crtc->ltm_buffer_cnt);
		return;
	}

	for (i = 0; i < num; i++) {
		ret = map_single_ltm_buffer(sde_crtc, i, buf_cfg->fds[i]);
		if (ret)
			goto exit;

		sgt = msm_gem_get_sgt(sde_crtc->ltm_buffers[i]->gem);
		sde_crtc->ltm_buffers[i]->addr_map.size = sde_crtc->ltm_buffers[i]->gem->size;
		ret = hfi_adapter_map_sg_table(sde_crtc->hfi_client, sgt,
			&sde_crtc->ltm_buffers[i]->addr_map);
		if (ret)
			goto exit;

		mapped_iova = _hfi_cp_crtc_get_mapped_iova(&(sde_crtc->ltm_buffers[i]->addr_map));
		if (!mapped_iova) {
			SDE_ERROR("failed to get mapped iova for buffer %d\n", i);
			goto exit;
		}
		sde_crtc->ltm_buffers[i]->dcp_iova = *mapped_iova;

		if (!(sde_crtc->ltm_buffers[i]->dcp_iova)) {
			SDE_ERROR("invalid dcp_iova for buffer %d\n", i);
			goto exit;
		}
	}

	/* Add buffers to ltm_buf_free list */
	for (i = 0; i < num; i++) {
		ret = hfi_cp_crtc_queue_ltm_buffer(sde_crtc->ltm_buffers[i], cfg);
		if (ret) {
			SDE_ERROR("Failed to queue ltm buffer %d\n", buf_cfg->fds[i]);
			goto exit;
		}
	}

	sde_crtc->ltm_buffer_cnt = num;

	return;
exit:
	sde_crtc_cp_unmap_ltm_buffers(sde_crtc, i);
}

int hfi_cp_crtc_queue_ltm_buffer(struct sde_ltm_buffer *ltm_buff, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct hfi_display_ltm_buffer hfi_cfg;
	int ret = 0;

	if (!ltm_buff) {
		SDE_ERROR("invalid parameters ltm_buff %pK\n", ltm_buff);
		return -EINVAL;
	}

	hw_cfg->prop_id = HFI_PACK_VERSION(1, 0, HFI_PROPERTY_DISPLAY_COLOR_LTM_QUEUE_BUF);
	hfi_cfg.flags = 0;
	hfi_cfg.dcp_addr_l = (ltm_buff->dcp_iova & ~((u32)0));
	hfi_cfg.dcp_addr_h = ((ltm_buff->dcp_iova >> 32) & ~((u32)0));
	hfi_cfg.dpu_iova_l = (ltm_buff->iova & ~((u32)0));
	hfi_cfg.dpu_iova_h = ((ltm_buff->iova >> 32) & ~((u32)0));
	hfi_cfg.size = sizeof(struct hfi_display_ltm_buffer);

	ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper, hw_cfg->prop_id,
			HFI_VAL_U32_ARRAY, &hfi_cfg, sizeof(struct hfi_display_ltm_buffer));

	if (ret) {
		SDE_ERROR("Failed to send queue ltm buffer prop ret: %d\n", ret);
		return ret;
	}

	return 0;
}

void hfi_cp_crtc_free_ltm_buffer(struct sde_crtc *sde_crtc, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	int ret = 0;
	u32 buffer_count = 0;
	u32 hfi_cfg = 1;
	u32 prop_id = HFI_PACK_VERSION(1, 0, HFI_PROPERTY_DISPLAY_COLOR_LTM_CLEAR_BUFS);

	if (!sde_crtc || !cfg) {
		DRM_ERROR("invalid parameters sde_crtc %pK, cfg %pK\n", sde_crtc, cfg);
		return;
	}

	if (sde_crtc->ltm_hist_en) {
		DRM_ERROR("cannot free LTM buffers when hist is enabled\n");
		return;
	}

	if (!sde_crtc->ltm_buffer_cnt) {
		/* ltm_buffers are already freed */
		return;
	}

	buffer_count = sde_crtc->ltm_buffer_cnt;
	sde_crtc->ltm_buffer_cnt = 0;
	sde_crtc_cp_unmap_ltm_buffers(sde_crtc, buffer_count);

	ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper, prop_id,
			HFI_VAL_U32, &hfi_cfg, sizeof(u32));

	if (ret) {
		SDE_ERROR("Failed to send free ltm buffer prop ret: %d\n", ret);
		return;
	}
}

void hfi_cp_crtc_reset_color_props(struct hfi_util_u32_prop_helper *color_props)
{
	if (!color_props) {
		SDE_ERROR("Invalid color_props is null\n");
		return;
	}
	hfi_util_u32_prop_helper_reset(color_props);
}

int hfi_cp_crtc_get_color_props_count(struct hfi_util_u32_prop_helper *color_props)
{
	if (!color_props) {
		SDE_ERROR("Invalid color_props is null\n");
		return 0;
	}
	return hfi_util_u32_prop_helper_prop_count(color_props);
}

void hfi_cp_crtc_unmap_sg_table(struct hfi_shared_addr_map *addr_map, struct hfi_client_t *client)
{
	unsigned long *mapped_iova = NULL;

	if (!addr_map) {
		SDE_ERROR("Invalid parameters addr_map %pK\n", addr_map);
		return;
	}

	mapped_iova = _hfi_cp_crtc_get_mapped_iova(addr_map);
	if (mapped_iova && *mapped_iova) {
		hfi_adapter_unmap_sg_table(client, *mapped_iova,
			addr_map->aligned_size);
		*mapped_iova = 0;
	}
}

void hfi_cp_crtc_set_rgb_hist_buffers(struct sde_crtc *sde_crtc,
		struct sde_hw_dspp *ctx, void *data)
{
	int ret = 0;
	struct sde_hw_cp_cfg *hw_cfg = data;
	struct drm_msm_rgb_hist_buffers_ctrl *input_cfg;
	struct hfi_display_rgb_hist_buffer hfi_cfg[RGB_HISTOGRAM_BUFFER_SIZE];
	struct sde_rgb_hist_buffer *hist_buf = NULL;
	struct hfi_display_rgb_hist_buffer *hfi_buf = NULL;
	struct sg_table *sgt = NULL;
	unsigned long *mapped_iova = NULL;
	u32 clear_buffs_prop_id = HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_CLEAR_BUFFERS;
	u32 payload = 0;

	if (!sde_crtc || !data) {
		SDE_ERROR("Invalid sde_crtc: %pK data: %pK\n", sde_crtc, data);
		return;
	}

	if (ctx->dpu_idx < DPU_0 || ctx->dpu_idx >= DPU_MAX) {
		SDE_ERROR("Invalid dpu idx: %d\n", ctx->dpu_idx);
		return;
	}

	if (sde_crtc->do_clear_rgb_hist_buf) {
		clear_buffs_prop_id = HFI_PACK_VERSION(2, 0,
			HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_CLEAR_BUFFERS);
		ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper, clear_buffs_prop_id,
				HFI_VAL_U32_ARRAY, &payload, sizeof(u32));
		sde_crtc->do_clear_rgb_hist_buf = false;
	}

	hw_cfg->prop_id = HFI_PACK_VERSION(
		2, 0, HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_QUEUE_BUFFER);

	if (!hw_cfg->payload) {
		SDE_DEBUG("Disable case\n");
		for (int i = 0; i < RGB_HISTOGRAM_BUFFER_SIZE; i++)
			hfi_cfg[i].flags = 0;
	} else {
		if (hw_cfg->len != sizeof(struct drm_msm_rgb_hist_buffers_ctrl)) {
			SDE_ERROR("Invalid len: %d\n", hw_cfg->len);
			return;
		}

		input_cfg = hw_cfg->payload;
		for (int i = 0; i < RGB_HISTOGRAM_BUFFER_SIZE; i++) {
			sde_crtc->rgb_hist_buffers[i] =
				kzalloc(sizeof(struct sde_rgb_hist_buffer), GFP_KERNEL);
			if (!sde_crtc->rgb_hist_buffers[i]) {
				SDE_ERROR("Failed to kzalloc hist_buf[%d]\n", i);
				goto exit;
			}
			hist_buf = sde_crtc->rgb_hist_buffers[i];
			hfi_buf = &hfi_cfg[i];

			for (int j = 0; j < RGB_COMPONENT_SIZE; j++) {
				// Map buffer to kva, dup_iova and dcp_iova
				ret = map_single_rgb_hist_buffer(sde_crtc, i, j,
					input_cfg->fds[i][j]);
				if (ret) {
					SDE_ERROR("Failed to map buffer, fd %d, ret %d\n",
						input_cfg->fds[i][j], ret);
					goto exit;
				}
				sgt = msm_gem_get_sgt(hist_buf->gem[j]);
				hist_buf->addr_map[j].size = hist_buf->gem[j]->size;
				ret = hfi_adapter_map_sg_table(sde_crtc->hfi_client, sgt,
						&hist_buf->addr_map[j]);
				if (ret) {
					SDE_ERROR("Failed to map buffer to dcp_iova ret %d\n", ret);
					goto exit;
				}

				mapped_iova = NULL;
				mapped_iova = _hfi_cp_crtc_get_mapped_iova(
						&(hist_buf->addr_map[j]));
				if (!mapped_iova) {
					SDE_ERROR("failed to get mapped iova, i %d j %d\n", i, j);
					goto exit;
				}
				hist_buf->dcp_iova[j] = *mapped_iova;

				// Add to hfi struct
				hfi_buf->dpu_iova_lo[j] =
					(u32)(hist_buf->dpu_iova[j] & U32_MASK_LOW);
				hfi_buf->dpu_iova_hi[j] =
					(u32)(hist_buf->dpu_iova[j] >> U32_SHIFT_BITS);
				hfi_buf->dcp_addr_lo[j] =
					(u32)(hist_buf->dcp_iova[j] & U32_MASK_LOW);
				hfi_buf->dcp_addr_hi[j] =
					(u32)(hist_buf->dcp_iova[j] >> U32_SHIFT_BITS);
			}
			hfi_buf->flags = HFI_DISPLAY_COLOR_FLAGS_FEATURE_ENABLE;
			hfi_buf->size = sizeof(struct hfi_display_rgb_hist_buffer);
		}
	}

	for (int i = 0; i < RGB_HISTOGRAM_BUFFER_SIZE; i++) {
		ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper,
				hw_cfg->prop_id, HFI_VAL_U32_ARRAY, &hfi_cfg[i],
				sizeof(struct hfi_display_rgb_hist_buffer));
		if (ret)
			SDE_ERROR("failed to add HFI prop: %d ret: %d\n", hw_cfg->prop_id, ret);
	}
	return;

exit:
	sde_crtc_cp_unmap_rgb_hist_buffers(sde_crtc);
}

static int hfi_cp_crtc_validate_rgb_hist_params(struct drm_msm_rgb_hist_ctrl *hist_ctrl,
		u32 panel_width, u32 panel_height)
{

	if (!hist_ctrl) {
		SDE_ERROR("invalid parameter hist_ctrl: %pK\n", hist_ctrl);
		return -EINVAL;
	}

	// Check tap_point
	if (hist_ctrl->tap_point != RGB_HIST_TAP_POINT_AFTER_DSPP &&
		hist_ctrl->tap_point != RGB_HIST_TAP_POINT_BEFORE_DSPP) {
		SDE_ERROR("invalid tap point: %d\n", hist_ctrl->tap_point);
		return -EINVAL;
	}

	// Check colorspace_mode
	if (hist_ctrl->colorspace_mode != RGB_HIST_COLORMODE_Y &&
		hist_ctrl->colorspace_mode != RGB_HIST_COLORMODE_V &&
		hist_ctrl->colorspace_mode != RGB_HIST_COLORMODE_RGB) {
		SDE_ERROR("invalid colorspace mode: %d\n", hist_ctrl->colorspace_mode);
		return -EINVAL;
	}

	// Check roi
	if (hist_ctrl->flags & RGB_HIST_ROI_ENABLE) {
		if (hist_ctrl->roi_mode != ROI_MODE_WITHIN &&
			hist_ctrl->roi_mode != ROI_MODE_OUTSIDE) {
			SDE_ERROR("invalid roi mode: %d\n", hist_ctrl->roi_mode);
			return -EINVAL;
		}

		if (hist_ctrl->roi_x + hist_ctrl->roi_width > panel_width) {
			SDE_ERROR("invalid roi x input = [%u,%u], panel_width = %u\n",
				hist_ctrl->roi_x, hist_ctrl->roi_width, panel_width);
			return -EINVAL;
		}

		if (hist_ctrl->roi_y + hist_ctrl->roi_height > panel_height) {
			SDE_ERROR("invalid roi y input = [%u,%u], panel_height = %u\n",
				hist_ctrl->roi_y, hist_ctrl->roi_height, panel_height);
			return -EINVAL;
		}
	}

	return 0;
}

void hfi_cp_crtc_queue_rgb_hist_buffer(struct sde_crtc *sde_crtc,
		struct sde_hw_dspp *ctx, void *data)
{
	int ret = 0;
	struct sde_hw_cp_cfg *hw_cfg = data;
	struct drm_msm_rgb_hist_buffer *input_cfg;
	struct hfi_display_rgb_hist_buffer hfi_buf;
	struct sde_rgb_hist_buffer *hist_buf = NULL;
	bool found = false;
	int i = 0;

	if (!sde_crtc || !data) {
		SDE_ERROR("invalid sde_crtc: %pK data: %pK\n", sde_crtc, data);
		return;
	}

	if (ctx->dpu_idx < DPU_0 || ctx->dpu_idx >= DPU_MAX) {
		SDE_ERROR("Invalid dpu idx: %d\n", ctx->dpu_idx);
		return;
	}

	if (!hw_cfg->payload ||
		hw_cfg->len != sizeof(struct drm_msm_rgb_hist_buffer)) {
		SDE_ERROR("invalid payload: %pK len: %d\n", hw_cfg->payload, hw_cfg->len);
		return;
	}

	input_cfg = (struct drm_msm_rgb_hist_buffer *)(hw_cfg->payload);
	for (i = 0; i < RGB_HISTOGRAM_BUFFER_SIZE; i++) {
		hist_buf = sde_crtc->rgb_hist_buffers[i];
		if (!hist_buf)
			continue;

		found = true;
		for (int j = 0; j < RGB_COMPONENT_SIZE; j++) {
			if (hist_buf->drm_fb_id[j] != input_cfg->fd[j]) {
				found = false;
				break;
			}
		}

		if (found)
			break;
	}

	if (!found) {
		SDE_ERROR("hist buffer not found\n");
		return;
	}

	hw_cfg->prop_id = HFI_PACK_VERSION(
		2, 0, HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_QUEUE_BUFFER);

	hfi_buf.flags = HFI_DISPLAY_COLOR_FLAGS_FEATURE_ENABLE;
	hfi_buf.size = sizeof(struct hfi_display_rgb_hist_buffer);

	for (int j = 0; j < RGB_COMPONENT_SIZE; j++) {
		hfi_buf.dpu_iova_lo[j] = (u32)(hist_buf->dpu_iova[j] & U32_MASK_LOW);
		hfi_buf.dpu_iova_hi[j] = (u32)(hist_buf->dpu_iova[j] >> U32_SHIFT_BITS);
		hfi_buf.dcp_addr_lo[j] = (u32)(hist_buf->dcp_iova[j] & U32_MASK_LOW);
		hfi_buf.dcp_addr_hi[j] = (u32)(hist_buf->dcp_iova[j] >> U32_SHIFT_BITS);
	}

	ret = hfi_util_u32_prop_helper_add_prop(
		hw_cfg->prop_helper, hw_cfg->prop_id, HFI_VAL_U32_ARRAY, &hfi_buf,
		sizeof(struct hfi_display_rgb_hist_buffer));
	if (ret)
		SDE_ERROR("failed to add HFI prop: %d ret: %d\n", hw_cfg->prop_id, ret);
}

int hfi_setup_dspp_rgb_hist_ctrlv2(struct sde_hw_dspp *ctx, void *data)
{
	struct sde_hw_cp_cfg *hw_cfg = data;
	struct drm_msm_rgb_hist_ctrl *hist_ctrl;
	struct hfi_display_rgb_hist_ctrl *hfi_cfg;
	int ret = 0;

	// Check input
	if (!ctx || !hw_cfg) {
		SDE_ERROR("invalid parameter ctx: %pK hw_cfg: %pK\n", ctx, hw_cfg);
		return -EINVAL;
	}

	if (ctx->dpu_idx < DPU_0 || ctx->dpu_idx >= DPU_MAX) {
		SDE_ERROR("Invalid dpu idx: %d\n", ctx->dpu_idx);
		return -EINVAL;
	}

	hw_cfg->prop_id = HFI_PACK_VERSION(2, 0, HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_CTRL);

	hfi_cfg = &hfi_rgb_hist_ctrl_cached[ctx->dpu_idx][hw_cfg->dspp_idx];
	if (!hw_cfg->payload) {
		SDE_DEBUG("Disable RGB hist feature\n");
		hfi_cfg->flags = 0;
	} else {
		SDE_DEBUG("Enable RGB hist feature\n");
		hist_ctrl = (struct drm_msm_rgb_hist_ctrl *)(hw_cfg->payload);
		if (hw_cfg->len != sizeof(struct drm_msm_rgb_hist_ctrl)) {
			SDE_ERROR("invalid rgb hist ctrl len %u exp: %lu\n",
				hw_cfg->len, sizeof(struct drm_msm_rgb_hist_ctrl));
			return -EINVAL;
		}

		// Validate input patams
		ret = hfi_cp_crtc_validate_rgb_hist_params(hist_ctrl,
			hw_cfg->panel_width, hw_cfg->panel_height);
		if (ret) {
			SDE_ERROR("Invalid rgb hist params, ret: %d\n", ret);
			return ret;
		}

		// Config data
		hfi_cfg->flags = HFI_DISPLAY_COLOR_FLAGS_FEATURE_ENABLE;
		hfi_cfg->tap_point = hist_ctrl->tap_point;
		hfi_cfg->colorspace_mode = hist_ctrl->colorspace_mode;
		hfi_cfg->roi_mode = 0;
		hfi_cfg->roi_x = 0;
		hfi_cfg->roi_y = 0;
		hfi_cfg->roi_width = 0;
		hfi_cfg->roi_height = 0;

		// Check ROI
		if (hist_ctrl->flags & RGB_HIST_ROI_ENABLE) {
			hfi_cfg->flags |= HFI_RGB_HIST_ROI_ENABLE;
			hfi_cfg->roi_mode = hist_ctrl->roi_mode;
			hfi_cfg->roi_x = hist_ctrl->roi_x;
			hfi_cfg->roi_y = hist_ctrl->roi_y;
			hfi_cfg->roi_width = hist_ctrl->roi_width;
			hfi_cfg->roi_height = hist_ctrl->roi_height;
		}
	}

	if (hw_cfg->dspp_idx == (hw_cfg->dspp_start_idx + hw_cfg->num_of_mixers - 1)) {
		ret = hfi_util_u32_prop_helper_add_prop(
			hw_cfg->prop_helper, hw_cfg->prop_id, HFI_VAL_U32_ARRAY, hfi_cfg,
			sizeof(struct hfi_display_rgb_hist_ctrl));
		if (ret)
			SDE_ERROR("failed to add HFI prop: %d ret: %d\n", hw_cfg->prop_id, ret);
	}
	return ret;
}

void hfi_cp_crtc_free_rgb_hist_buffers(struct sde_crtc *sde_crtc, void *cfg)
{
	u32 hfi_cfg = 0;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	int ret = 0;

	if (!sde_crtc || !cfg) {
		DRM_ERROR("invalid parameters sde_crtc %pK, cfg %pK\n", sde_crtc, cfg);
		return;
	}

	if (sde_crtc->rgb_hist_en) {
		DRM_ERROR("cannot free buffers when rgb hist is enabled\n");
		return;
	}

	hw_cfg->prop_id = HFI_PACK_VERSION(2, 0, HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_CLEAR_BUFFERS);

	// unmap buffers
	sde_crtc_cp_unmap_rgb_hist_buffers(sde_crtc);

	// Add prop to hfi
	ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper, hw_cfg->prop_id,
		HFI_VAL_U32, &hfi_cfg, sizeof(u32));
	if (ret) {
		SDE_ERROR("Failed to send free rgb hist buffer prop ret: %d\n", ret);
		return;
	}
}

void hfi_setup_mdnie_art_v1(struct sde_hw_dspp *ctx, void *cfg, void *aiqe_top)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct drm_msm_mdnie_art *art_payload = NULL;
	u32 prop_id, ret;
	u32 art_value = 0;

	if (!ctx || !cfg || (hw_cfg->len != sizeof(struct drm_msm_mdnie_art) && hw_cfg->payload)) {
		DRM_ERROR("ctx %pK hw_cfg %pK payload %pK size %d expected sz %zd\n",
			ctx, hw_cfg, ((hw_cfg) ? hw_cfg->payload : NULL),
			((hw_cfg) ? hw_cfg->len : 0), sizeof(struct drm_msm_mdnie_art));
		return;
	}

	if (ctx->dpu_idx < DPU_0 || ctx->dpu_idx >= DPU_MAX) {
		SDE_ERROR("Invalid dpu idx: %d\n", ctx->dpu_idx);
		return;
	}

	art_payload = (struct drm_msm_mdnie_art *)(hw_cfg->payload);

	prop_id = HFI_PACK_VERSION(2, 0, hw_cfg->prop_id);
	if (art_payload) {
		art_value =  art_payload->param;
	} else {
		DRM_DEBUG_DRIVER("disable art feature\n");
		art_value =  0;
	}

	ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper,
			prop_id, HFI_VAL_U32,
			&art_value,
			sizeof(u32));
	if (ret)
		SDE_ERROR("Failed to add hfi prop for mdnie art %d ret %d\n",
			prop_id, ret);
	else
		SDE_DEBUG("feature %d: submitted to prop_helper\n",
			HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE_ART);
}

int hfi_cp_crtc_alloc_pa_hist_buffers(struct sde_crtc *sde_crtc)
{
	u32 i = 0;
	int ret = 0;
	struct hfi_kms *hfi_kms = NULL;
	struct sde_pa_hist_buffer *pa_hist_buff = NULL;
	struct msm_drm_private *priv = NULL;

	if (!sde_crtc) {
		SDE_ERROR("invalid parameters sde_crtc %pK\n", sde_crtc);
		return -EINVAL;
	}

	if (sde_crtc->base.dev && sde_crtc->base.dev->dev_private) {
		priv = sde_crtc->base.dev->dev_private;
		hfi_kms = ((priv && priv->kms) ? to_hfi_kms(to_sde_kms(priv->kms)) : NULL);
	}

	if (!hfi_kms) {
		SDE_ERROR("%s: failed to get hfi kms\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < PA_HIST_BUFFER_NUM; i++) {
		pa_hist_buff = &sde_crtc->pa_hist_buffers[i];

		/* Set the required size */
		pa_hist_buff->buffer.size = HFI_PA_HIST_BUFFER_SIZE;

		/* Allocate shared memory via HFI adapter */
		ret = hfi_adapter_buffer_alloc(&hfi_kms->hfi_client, &pa_hist_buff->buffer);
		if (ret) {
			SDE_ERROR("hfi_adapter_buffer_alloc failed for PA hist buffer %d\n", i);
			goto cleanup;
		}

		pa_hist_buff->is_available = true;
	}
	return ret;

cleanup:
	/* deallocate previous allocated buffers */
	for (i = 0; i < PA_HIST_BUFFER_NUM; i++) {
		/* free buffer if buffer is available */
		if (sde_crtc->pa_hist_buffers[i].is_available) {
			ret = hfi_adapter_buffer_dealloc(&hfi_kms->hfi_client,
						&sde_crtc->pa_hist_buffers[i].buffer);
			sde_crtc->pa_hist_buffers[i].is_available = false;
		}
	}

	return -ENOMEM;
}

int hfi_cp_crtc_dealloc_pa_hist_buffers(struct sde_crtc *sde_crtc)
{
	u32 i = 0;
	int ret = 0;

	if (!sde_crtc) {
		SDE_ERROR("invalid parameters sde_crtc %pK\n", sde_crtc);
		return -EINVAL;
	}

	for (i = 0; i < PA_HIST_BUFFER_NUM; i++) {
		/* deallocate buffer */
		if (sde_crtc->pa_hist_buffers[i].is_available) {
			ret = hfi_adapter_buffer_dealloc(sde_crtc->hfi_client,
						&sde_crtc->pa_hist_buffers[i].buffer);
			if (ret) {
				SDE_ERROR("Failed to dealloc pa hist buffer %d\n", i);
				return ret;
			}
		}
		sde_crtc->pa_hist_buffers[i].is_available = false;
	}

	return ret;
}

int hfi_cp_crtc_queue_pa_hist_buffer(struct sde_crtc *sde_crtc, struct sde_hw_dspp *ctx,
				void *data)
{
	struct sde_hw_cp_cfg *hw_cfg = (struct sde_hw_cp_cfg *)data;
	struct sde_hw_mixer *hw_lm;
	struct hfi_display_pa_hist_buffer hfi_buf;
	int ret = 0;
	int i;

	if (!sde_crtc || !ctx || !hw_cfg) {
		SDE_ERROR("invalid parameters sde_crtc %pK ctx %pK hw_cfg %pK\n", sde_crtc,
			ctx, hw_cfg);
		return -EINVAL;
	}

	if (ctx->dpu_idx < DPU_0 || ctx->dpu_idx >= DPU_MAX) {
		SDE_ERROR("Invalid dpu idx: %d\n", ctx->dpu_idx);
		return -EINVAL;
	}

	hw_lm = hw_cfg->mixer_info;
	if (hw_lm->cfg.right_mixer)
		return 0;

	for (i = 0; i < PA_HIST_BUFFER_NUM; i++) {
		struct sde_pa_hist_buffer *pa_hist_buff = &sde_crtc->pa_hist_buffers[i];
		/* if buffer is available, send to FW */
		if (!pa_hist_buff->is_available)
			continue;

		hw_cfg->prop_id = HFI_PACK_VERSION(1, 7,
				HFI_PROPERTY_DISPLAY_COLOR_PA_HIST_QUEUE_BUFFER);
		hfi_buf.flags = HFI_DISPLAY_COLOR_FLAGS_FEATURE_ENABLE;
		hfi_buf.dcp_addr_lo = (u32)(pa_hist_buff->buffer.remote_addr & U32_MASK_LOW);
		hfi_buf.dcp_addr_hi = (u32)(pa_hist_buff->buffer.remote_addr >>
						U32_SHIFT_BITS);
		hfi_buf.size = sizeof(struct hfi_display_pa_hist_buffer);

		ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper,
						hw_cfg->prop_id, HFI_VAL_U32_ARRAY, &hfi_buf,
						sizeof(struct hfi_display_pa_hist_buffer));

		if (ret) {
			SDE_ERROR("Failed to send queue pa hist buffer prop ret: %d\n", ret);
			return ret;
		}
		pa_hist_buff->is_available = false;
	}

	return ret;
}

void hfi_cp_crtc_clear_pa_hist_buffers(struct sde_crtc *sde_crtc, void *data)
{
	struct sde_hw_cp_cfg *hw_cfg = data;
	u32 i = 0;
	u32 hfi_cfg = 0;
	int ret = 0;

	if (!sde_crtc || !hw_cfg) {
		SDE_ERROR("invalid parameters sde_crtc %pK, data %pK\n", sde_crtc, data);
		return;
	}

	hw_cfg->prop_id = HFI_PACK_VERSION(1, 7,
					HFI_PROPERTY_DISPLAY_COLOR_PA_HIST_CLEAR_BUFFERS);

	/* Add prop to hfi */
	ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper, hw_cfg->prop_id,
			HFI_VAL_U32, &hfi_cfg, sizeof(u32));

	if (ret) {
		SDE_ERROR("Failed to send free ltm buffer prop ret: %d\n", ret);
		return;
	}
	/* mark all the buffers available */
	for (i = 0; i < PA_HIST_BUFFER_NUM; i++)
		sde_crtc->pa_hist_buffers[i].is_available = true;
}

void hfi_setup_dspp_hist_v1_7(struct sde_hw_dspp *ctx, void *data, bool enable)
{
	struct sde_hw_cp_cfg *hw_cfg = (struct sde_hw_cp_cfg *) data;
	int ret = 0;
	u32 *hfi_cfg;
	u32 payload_size = sizeof(u32);

	if (!ctx || !hw_cfg) {
		SDE_ERROR("invalid parameters ctx %pK, hw_cfg %pK\n",
				ctx, hw_cfg);
		return;
	}

	if (ctx->dpu_idx < DPU_0 || ctx->dpu_idx >= DPU_MAX) {
		SDE_ERROR("Invalid dpu idx: %d\n", ctx->dpu_idx);
		return;
	}

	hfi_cfg = &hfi_pa_hist_ctrl_cached[ctx->dpu_idx][hw_cfg->dspp_idx];
	hw_cfg->prop_id = HFI_PACK_VERSION(1, 7, hw_cfg->prop_id);
	*hfi_cfg = enable;

	if (hw_cfg->dspp_idx == (hw_cfg->dspp_start_idx + hw_cfg->num_of_mixers - 1)) {
		ret = hfi_util_u32_prop_helper_add_prop(hw_cfg->prop_helper,
				hw_cfg->prop_id, HFI_VAL_U32,
				&hfi_pa_hist_ctrl_cached[ctx->dpu_idx][hw_cfg->dspp_start_idx],
				payload_size * hw_cfg->num_of_mixers);

		if (ret) {
			SDE_ERROR("Failed to add hfi prop for PA hist ctrl %d ret %d\n",
				hw_cfg->prop_id, ret);
			return;
		}

		/* reset the cached struct for current dpu_idx after submitting to FW */
		memset(&hfi_pa_hist_ctrl_cached[ctx->dpu_idx], 0,
			   sizeof(hfi_pa_hist_ctrl_cached[ctx->dpu_idx]));
	}
}
