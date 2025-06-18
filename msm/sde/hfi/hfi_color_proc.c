// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hfi_color_proc.h"
#include "hfi_defs_layer_color.h"
#include "hfi_properties_display.h"
#include "sde_kms.h"

void hfi_sspp_setup_csc(struct sde_hw_pipe *ctx, struct sde_csc_cfg *data)
{
	struct hfi_csc hfi_cfg;
	int ret = 0;
	u32 prop_id = HFI_PROPERTY_LAYER_COLOR_CSC;

	if (!ctx || !data) {
		SDE_ERROR("invalid parameter ctx: %pK data: %pK\n", ctx, data);
		return;
	}

	prop_id = HFI_PACK_VERSION(1, 0, prop_id);
	hfi_cfg.flags = HFI_BUFF_FEATURE_ENABLE;
	for (int  i = 0; i < HFI_CSC_MATRIX_COEFF_SIZE; i++)
		hfi_cfg.ctm_coeff[i] = data->csc_mv[i];

	for (int  i = 0; i < HFI_CSC_BIAS_SIZE; i++) {
		hfi_cfg.pre_bias[i] = data->csc_pre_bv[i];
		hfi_cfg.post_bias[i] = data->csc_post_bv[i];
	}

	for (int  i = 0; i < HFI_CSC_CLAMP_SIZE; i++) {
		hfi_cfg.pre_clamp[i] = data->csc_pre_lv[i];
		hfi_cfg.post_bias[i] = data->csc_post_lv[i];
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

	hfi_cfg.flags = HFI_BUFF_FEATURE_ENABLE;
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
		hfi_cfg = HFI_BUFF_FEATURE_ENABLE;

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
		hfi_cfg = HFI_BUFF_FEATURE_ENABLE;

	ret = hfi_util_u32_prop_helper_add_prop_by_obj(hw_cfg->prop_helper, hw_cfg->prop_id,
			hw_cfg->obj_id, HFI_VAL_U32, &hfi_cfg, sizeof(u32));
	if (ret)
		SDE_ERROR("failed to add HFI prop: %d ret: %d\n", hw_cfg->prop_id, ret);
}
