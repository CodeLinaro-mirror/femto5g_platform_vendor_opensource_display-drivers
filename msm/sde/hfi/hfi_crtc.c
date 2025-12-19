// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/types.h>

#include "hfi_crtc.h"
#include "hfi_plane.h"
#include "hfi_kms.h"
#include "hfi_props.h"
#include "hfi_adapter.h"
#include "hfi_defs_display_color.h"
#include "hfi_color_proc.h"

#define HFI_CRTC_ID(c) ((c)->sde_base->base.base.id)

#define HFI_DEBUG_CRTC(c, fmt, ...) SDE_DEBUG("crtc%d " fmt,\
		(c) ? HFI_CRTC_ID(c) : -1, ##__VA_ARGS__)

#define HFI_ERROR_CRTC(c, fmt, ...) SDE_ERROR("crtc%d " fmt,\
		(c) ? HFI_CRTC_ID(c) : -1, ##__VA_ARGS__)

#define to_hfi_crtc(x) x->hfi_crtc

#define HFI_CRTC_MAX_PROPS 128
#define HFI_CRTC_BASE_PROP_MAX_SIZE 1024

/*
 * This macro ensures that operations on the dirty flags are protected
 * by the property_lock mutex to prevent race conditions.
 */
#define CRTC_DIRTY_OP_LOCK(crtc, op_func, bit, dirty) \
	do { \
		mutex_lock(&(crtc)->property_info.property_lock); \
		op_func((bit), (dirty)); \
		mutex_unlock(&(crtc)->property_info.property_lock); \
	} while (0)

/*
 * struct base_prop_lookup: tuple of drm property ID to HFI property ID
 */
struct base_prop_lookup {
	u32 drm_prop;
	u32 hfi_prop;
};

struct base_prop_lookup hfi_crtc_base_props_map[] = {
	{CRTC_PROP_DRAM_IB, HFI_PROPERTY_DISPLAY_DRAM_IB},
	{CRTC_PROP_DRAM_AB, HFI_PROPERTY_DISPLAY_DRAM_AB},
	{CRTC_PROP_LLCC_IB, HFI_PROPERTY_DISPLAY_LLCC_IB},
	{CRTC_PROP_LLCC_AB, HFI_PROPERTY_DISPLAY_LLCC_AB},
	{CRTC_PROP_CORE_IB, HFI_PROPERTY_DISPLAY_CORE_IB},
	{CRTC_PROP_CORE_AB, HFI_PROPERTY_DISPLAY_CORE_AB},
	{CRTC_PROP_CORE_CLK, HFI_PROPERTY_DISPLAY_CORE_CLK},
	{CRTC_PROP_DIM_LAYER_V1, HFI_PROPERTY_DISPLAY_DIM_LAYER},
};

struct kv_prop_lookup {
	u32 drm_prop;
	u32 hfi_prop;
	void (*add_hfi_prop)(u32 hfi_prop, struct sde_crtc *crtc,
			struct sde_crtc_state *old_state, struct hfi_cmdbuf_t *cmd_buf);
};

struct kv_prop_lookup hfi_crtc_custom_kv_props_map[] = {};

static struct hfi_kms *sde_crtc_get_kms(struct sde_crtc *crtc)
{
	struct msm_drm_private *priv;

	if (crtc && crtc->base.dev && crtc->base.dev->dev_private) {
		priv = crtc->base.dev->dev_private;
		return ((priv && priv->kms) ?
				to_hfi_kms(to_sde_kms(priv->kms)) : NULL);
	}

	return NULL;
}

#if IS_ENABLED(CONFIG_DRM_SDE_LSR)
static bool _hfi_crtc_is_prop_excluded_for_repro(u32 drm_prop, enum wb_opmode opmode,
	struct sde_mdss_cfg *catalog)
{
	int i;

	if (opmode != WB_CSC && opmode != WB_REPRO)
		return false;

	if (!catalog || !catalog->repro_excluded_props ||
			!catalog->repro_excluded_props[SDE_OBJ_CRTC] ||
			!catalog->repro_excluded_props_count[SDE_OBJ_CRTC] ||
			(catalog->repro_excluded_props_count[SDE_OBJ_CRTC]
				> HFI_CRTC_MAX_PROPS))
		return false;

	for (i = 0; i < catalog->repro_excluded_props_count[SDE_OBJ_CRTC]; i++) {
		if (catalog->repro_excluded_props[SDE_OBJ_CRTC][i] == drm_prop)
			return true;
	}
	return false;
}
#else
static bool _hfi_crtc_is_prop_excluded_for_repro(u32 drm_prop, enum wb_opmode opmode,
	struct sde_mdss_cfg *catalog)
{
	return false;
}
#endif

int _hfi_crtc_add_base_prop_helper(u32 hfi_prop, struct sde_crtc *crtc,
		struct sde_crtc_state *cstate,
		struct hfi_util_u32_prop_helper *prop_collector, u32 drm_prop)
{
	u64 prop_val;
	struct hfi_prop_u64 prop_u64;
	struct hfi_crtc *crtc_hfi;
	enum wb_opmode opmode;
	struct hfi_kms *hfi_kms;
	struct sde_mdss_cfg *sde_cfg;
	struct hfi_display_dim_layer *dim_layers;
	int i;

	if (!crtc || !cstate)
		return -EINVAL;

	crtc_hfi = to_hfi_crtc(crtc);
	hfi_kms = sde_crtc_get_kms(crtc);

	if (!hfi_kms || !hfi_kms->base)
		return -EINVAL;

	sde_cfg = hfi_kms->base->catalog;
	opmode = sde_crtc_check_for_lsr_opmode(&crtc->base, &cstate->base);
	if (_hfi_crtc_is_prop_excluded_for_repro(drm_prop, opmode, sde_cfg)) {
		HFI_DEBUG_CRTC(crtc_hfi, "Unsupported property for Repro drm_prop:%x\n", drm_prop);
		return 0;
	}

	switch (hfi_prop) {
	case HFI_PROPERTY_DISPLAY_DRAM_IB:
	case HFI_PROPERTY_DISPLAY_DRAM_AB:
	case HFI_PROPERTY_DISPLAY_LLCC_IB:
	case HFI_PROPERTY_DISPLAY_LLCC_AB:
	case HFI_PROPERTY_DISPLAY_CORE_IB:
	case HFI_PROPERTY_DISPLAY_CORE_AB:
	case HFI_PROPERTY_DISPLAY_CORE_CLK:
		prop_val = sde_crtc_get_property(cstate, drm_prop);

		prop_u64.val_lo = HFI_VAL_L32(prop_val);
		prop_u64.val_hi = HFI_VAL_H32(prop_val);
		hfi_util_u32_prop_helper_add_prop(prop_collector, hfi_prop,
				HFI_VAL_U32_ARRAY, &prop_u64,
				sizeof(struct hfi_prop_u64));
		break;
	case HFI_PROPERTY_DISPLAY_DIM_LAYER:
		if (!test_bit(SDE_CRTC_DIRTY_DIM_LAYERS, cstate->dirty))
			break;

		dim_layers = kzalloc(sizeof(struct hfi_display_dim_layer) * cstate->num_dim_layers,
				GFP_KERNEL);
		if (!dim_layers) {
			SDE_ERROR("failed to allocate memory for hfi dim layers\n");
			return -ENOMEM;
		}

		for (i = 0; i < cstate->num_dim_layers; i++) {
			struct hfi_display_dim_layer *dst = &dim_layers[i];

			dst->flags = cstate->dim_layer[i].flags;
			dst->stage = cstate->dim_layer[i].stage;

			dst->color_fill.color_0 = cstate->dim_layer[i].color_fill.color_0;
			dst->color_fill.color_1 = cstate->dim_layer[i].color_fill.color_1;
			dst->color_fill.color_2 = cstate->dim_layer[i].color_fill.color_2;
			dst->color_fill.color_3 = cstate->dim_layer[i].color_fill.color_3;

			dst->rect.x_pos = (u32)cstate->dim_layer[i].rect.x;
			dst->rect.y_pos = (u32)cstate->dim_layer[i].rect.y;
			dst->rect.width = (u32)cstate->dim_layer[i].rect.w;
			dst->rect.height = (u32)cstate->dim_layer[i].rect.h;
		}

		hfi_util_u32_prop_helper_add_prop(prop_collector, hfi_prop, HFI_VAL_U32_ARRAY,
			dim_layers, sizeof(struct hfi_display_dim_layer) * cstate->num_dim_layers);

		CRTC_DIRTY_OP_LOCK(crtc, clear_bit, SDE_CRTC_DIRTY_DIM_LAYERS, cstate->dirty);
		kfree(dim_layers);
		break;
	default:
		HFI_ERROR_CRTC(crtc_hfi, "unsupported HFI property\n");
		return -EINVAL;
	}

	HFI_DEBUG_CRTC(crtc_hfi, "done adding HFI prop:0x%x\n", hfi_prop);

	return 0;
}

int hfi_set_hw_fence_prop(struct sde_fence_context *ctx,
			enum hfi_fence_type hfi_fence_type,
			struct hfi_util_u32_prop_helper *prop_collector,
			u32 disp_id, u32 hfi_prop_id)
{
	u64 hwfence_index = 0;
	struct hfi_hw_fence fence_prop;
	int ret = 0;

	if (!ctx) {
		SDE_ERROR("Invalid fence ctx\n");
		return -EINVAL;
	}

	hwfence_index = sde_fence_get_hwfence_index(ctx);

	if (!hwfence_index) {
		SDE_DEBUG("no valid hwfence index for commit_cnt:%u, skipping set prop\n",
			ctx->commit_count);
		return 0;
	}

	fence_prop.h_synx = hwfence_index;
	fence_prop.flags = hfi_fence_type;

	ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
		hfi_prop_id, HFI_VAL_U32_ARRAY, &fence_prop,
		sizeof(struct hfi_hw_fence));
	if (ret) {
		SDE_ERROR("hfi set fence prop failed %d\n", ret);
		return ret;
	}

	SDE_EVT32(disp_id, hfi_prop_id, fence_prop.h_synx, fence_prop.flags);
	SDE_DEBUG("disp_id = %d, prop = 0x%x, h_synx = 0x%x, flags = 0x%x\n", disp_id,
		hfi_prop_id, fence_prop.h_synx, fence_prop.flags);
	return ret;
}


static int _hfi_crtc_set_props_base(struct sde_crtc *crtc, u32 disp_id,
		struct sde_crtc_state *cstate, struct hfi_cmdbuf_t *cmd_buf)
{
	u32 drm_prop, hfi_prop;
	int i, ret = 0;
	struct hfi_crtc *crtc_hfi;
	uint32_t detach_plane_mask;
	struct drm_plane *plane;

	if (!crtc) {
		SDE_ERROR("invalid crtc\n");
		return -EINVAL;
	}

	crtc_hfi = to_hfi_crtc(crtc);
	if (!crtc_hfi->base_props)
		return -EINVAL;

	mutex_lock(&crtc_hfi->hfi_lock);
	hfi_util_u32_prop_helper_reset(crtc_hfi->base_props);

	/* append msm properties */
	for (i = 0; i < ARRAY_SIZE(hfi_crtc_base_props_map); i++) {
		drm_prop = hfi_crtc_base_props_map[i].drm_prop;
		hfi_prop = hfi_crtc_base_props_map[i].hfi_prop;
		_hfi_crtc_add_base_prop_helper(hfi_prop, crtc, cstate,
			crtc_hfi->base_props, drm_prop);
	}

	/*
	 * LSR opmode has custom logic to send the output fence to DCP FW as hfi input fence
	 * property for scan_complete event.
	 * Outside of this use case, trigger the output fence as hfi output fence property
	 * for scan_start.
	 */
	if (sde_crtc_check_for_lsr_opmode(&crtc->base, crtc->base.state) == WB_CSC) {
		ret = hfi_set_hw_fence_prop(crtc->output_fence, HFI_FENCE_SCAN_COMPLETE,
				crtc_hfi->base_props, disp_id, HFI_PROPERTY_DISPLAY_INPUT_FENCE);
		if (ret)
			HFI_ERROR_CRTC(crtc_hfi, "failed to set input hw-fence prop ret:%d\n", ret);
	} else if (test_bit(HW_FENCE_OUT_FENCES_ENABLE, crtc->hwfence_features_mask)) {
		ret = hfi_set_hw_fence_prop(crtc->output_fence, HFI_FENCE_SCAN_START,
			crtc_hfi->base_props, disp_id, HFI_PROPERTY_DISPLAY_OUTPUT_FENCE);
		if (ret)
			HFI_ERROR_CRTC(crtc_hfi, "failed to set output hw-fence prop ret:%d\n",
				ret);
	}

	if (!hfi_util_u32_prop_helper_prop_count(crtc_hfi->base_props))
		goto end;

	/*
	 * Once all the key value pairs of properties are collected invoke adapter api
	 * to add all these property array as a single HFI Packet
	 */
	ret = hfi_adapter_add_set_property(cmd_buf->ctx,
			cmd_buf,
			HFI_COMMAND_DISPLAY_SET_PROPERTY,
			disp_id,
			HFI_PAYLOAD_TYPE_U32_ARRAY,
			hfi_util_u32_prop_helper_get_payload_addr(crtc_hfi->base_props),
			hfi_util_u32_prop_helper_get_size(crtc_hfi->base_props),
			HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (ret) {
		HFI_ERROR_CRTC(crtc_hfi, "failed to send HFI commands\n");
		goto end;
	}

	detach_plane_mask = crtc_hfi->prev_plane_mask & ~(cstate->base.plane_mask);
	drm_for_each_plane_mask(plane, crtc->base.dev, detach_plane_mask)
		hfi_plane_disable(cmd_buf, disp_id, to_sde_plane(plane), false);

	crtc_hfi->prev_plane_mask = cstate->base.plane_mask;

	HFI_DEBUG_CRTC(crtc_hfi, "done adding all base props\n");
end:
	mutex_unlock(&crtc_hfi->hfi_lock);

	return ret;
}

static void _hfi_crtc_setup_sys_cache(struct sde_crtc_state *cstate, struct sde_crtc *sde_crtc)
{
	struct drm_crtc *crtc;
	struct drm_device *dev;
	struct drm_encoder *encoder;
	bool is_vid = false;

	if (!cstate || !sde_crtc)
		return;

	crtc = &sde_crtc->base;

	if (!crtc)
		return;

	dev = crtc->dev;
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->crtc != crtc)
			continue;

		if (sde_encoder_get_intf_mode(encoder) == INTF_MODE_VIDEO)
			is_vid = true;
	}

	if (!is_vid)
		return;

	/* Check if previous commit requested stale frame handling */
	if (sde_crtc->llcc_stale_frame_trigger) {
		sde_core_perf_llcc_stale_frame(crtc, SDE_SYS_CACHE_DISP);
		sde_crtc->llcc_stale_frame_trigger = false;
	}

	if (sde_crtc_get_property(cstate, CRTC_PROP_CACHE_STATE) || sde_crtc->cwb_idle) {
		sde_crtc->new_perf.llcc_active[SDE_SYS_CACHE_DISP] = true;
		sde_crtc->cwb_idle = false;
	}
	else
		sde_crtc->new_perf.llcc_active[SDE_SYS_CACHE_DISP] = false;

	if (sde_crtc->new_perf.llcc_active[SDE_SYS_CACHE_DISP])
		sde_crtc->llcc_stale_frame_trigger = true;

	sde_core_perf_crtc_update_llcc(crtc);
}

int hfi_crtc_populate_custom_kv_setter_props(struct sde_crtc *crtc, u32 disp_id,
		struct sde_crtc_state *cstate, struct hfi_cmdbuf_t *cmd_buf)
{
	int i, ret = 0;
	struct kv_prop_lookup *setter;
	struct hfi_crtc *crtc_hfi;
	u32 kv_count;

	if (!crtc) {
		SDE_ERROR("invalid crtc\n");
		return -EINVAL;
	}

	crtc_hfi = to_hfi_crtc(crtc);

	mutex_lock(&crtc_hfi->hfi_lock);
	hfi_util_kv_helper_reset(crtc_hfi->kv_props);

	for (i = 0; i < ARRAY_SIZE(hfi_crtc_custom_kv_props_map); i++) {
		setter = &hfi_crtc_custom_kv_props_map[i];
		if (!sde_crtc_property_is_dirty(cstate, setter->drm_prop))
			continue;

		if (setter->add_hfi_prop)
			setter->add_hfi_prop(setter->hfi_prop, crtc, cstate, cmd_buf);
	}

	_hfi_crtc_setup_sys_cache(cstate, crtc);

	kv_count = hfi_util_kv_helper_get_count(crtc_hfi->kv_props);
	if (!kv_count)
		goto end;

	ret = hfi_adapter_add_prop_array(cmd_buf->ctx,
			cmd_buf,
			HFI_COMMAND_DISPLAY_SET_PROPERTY,
			disp_id,
			HFI_PAYLOAD_TYPE_U32_ARRAY,
			hfi_util_kv_helper_get_payload_addr(crtc_hfi->kv_props),
			kv_count * sizeof(struct hfi_kv_pairs),
			kv_count);
	if (ret) {
		HFI_ERROR_CRTC(crtc_hfi, "failed to send HFI commands\n");
		goto end;
	}
end:
	mutex_unlock(&crtc_hfi->hfi_lock);
	HFI_DEBUG_CRTC(crtc_hfi, "kv-props count%d\n", kv_count);

	return ret;
}

int _hfi_crtc_populate_props(struct hfi_cmdbuf_t *cmd_buf, u32 disp_id,
		struct sde_crtc *crtc, struct sde_crtc_state *old_state)
{
	int ret = 0;
	struct hfi_crtc *crtc_hfi;

	if (!crtc) {
		SDE_ERROR("invalid crtc\n");
		return -EINVAL;
	}

	crtc_hfi = to_hfi_crtc(crtc);

	ret = _hfi_crtc_set_props_base(crtc, disp_id, old_state, cmd_buf);
	if (ret) {
		HFI_ERROR_CRTC(crtc_hfi, "failed to send hfis\n");
		return ret;
	}

	ret = hfi_crtc_populate_custom_kv_setter_props(crtc, disp_id, old_state, cmd_buf);
	if (ret) {
		HFI_ERROR_CRTC(crtc_hfi, "failed to add kv props packet\n");
		return ret;
	}

	return ret;
}

void _hfi_crtc_disable(struct hfi_cmdbuf_t *cmd_buf, u32 disp_id, struct sde_crtc *crtc,
		struct sde_crtc_state *cstate)
{
	int ret;
	struct hfi_crtc *crtc_hfi;

	if (!crtc || !cstate) {
		SDE_ERROR("invalid crtc args\n");
		return;
	}

	crtc_hfi = to_hfi_crtc(crtc);

	mutex_lock(&crtc_hfi->hfi_lock);

	hfi_util_u32_prop_helper_reset(crtc_hfi->base_props);

	ret = hfi_adapter_add_set_property(cmd_buf->ctx,
			cmd_buf,
			HFI_COMMAND_DISPLAY_SET_PROPERTY,
			disp_id,
			HFI_PAYLOAD_TYPE_U32_ARRAY,
			hfi_util_u32_prop_helper_get_payload_addr(crtc_hfi->base_props),
			hfi_util_u32_prop_helper_get_size(crtc_hfi->base_props),
			HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (ret) {
		HFI_ERROR_CRTC(crtc_hfi, "failed to send HFI commands\n");
		goto end;
	}

end:
	mutex_unlock(&crtc_hfi->hfi_lock);
}

void hfi_crtc_set_pending_enc_mask(struct sde_crtc *sde_crtc, u32 enc_mask)
{
	struct hfi_crtc *hfi_crtc;

	if (sde_crtc) {
		hfi_crtc = to_hfi_crtc(sde_crtc);
		hfi_crtc->pending_enc_mask = enc_mask;
	}
}

u32 hfi_crtc_get_display_id(struct drm_crtc *crtc, struct drm_crtc_state *crtc_state)
{
	u32 disp_id = U32_MAX;
	struct drm_connector *conn;
	struct drm_encoder *enc;
	struct drm_encoder *main_enc = NULL;
	struct drm_connector_list_iter iter;
	struct sde_crtc *sde_crtc;
	struct hfi_crtc *hfi_crtc;
	u32 enc_mask;

	if (!crtc || !crtc_state)
		return U32_MAX;

	sde_crtc =  to_sde_crtc(crtc);
	hfi_crtc = to_hfi_crtc(sde_crtc);

	enc_mask = crtc_state->encoder_mask ?
		crtc_state->encoder_mask : sde_crtc->cached_encoder_mask;
	enc_mask = enc_mask ? enc_mask : hfi_crtc->pending_enc_mask;
	drm_for_each_encoder_mask(enc, crtc->dev, enc_mask) {
		if (sde_encoder_in_clone_mode(enc))
			continue;

		main_enc = enc;
	}

	if (main_enc) {
		drm_connector_list_iter_begin(crtc->dev, &iter);
		drm_for_each_connector_iter(conn, &iter)
			if (sde_connector_get_encoder(conn) == main_enc)
				disp_id = sde_conn_get_display_obj_id(conn);
		drm_connector_list_iter_end(&iter);
	}

	return disp_id;
}

void hfi_crtc_destroy(struct sde_crtc *crtc)
{
	int ret = 0;
	struct hfi_crtc *crtc_hfi;
	struct hfi_kms *hfi_kms;

	if (!crtc) {
		SDE_ERROR("invalid crtc\n");
		return;
	}

	crtc_hfi = to_hfi_crtc(crtc);

	hfi_kms = sde_crtc_get_kms(crtc);
	if (!hfi_kms)
		return;

	ret = hfi_adapter_buffer_dealloc(&hfi_kms->hfi_client, &crtc_hfi->hfi_buff_map_dither);
	if (ret)
		SDE_ERROR("failed to deallocated hfi shared memory for dither\n");

	ret = hfi_cp_crtc_dealloc_pa_hist_buffers(crtc);
	if (ret)
		SDE_ERROR("failed to dealloc pa hist buffers: %d\n", ret);

	kfree(crtc_hfi->base_props);
	kfree(crtc_hfi->color_props);
	kfree(crtc_hfi->kv_props);

	mutex_destroy(&crtc_hfi->hfi_lock);

	kfree(crtc_hfi);
}

static int hfi_crtc_add_hfi_cmds(struct hfi_cmdbuf_t *cmd_buf, u32 disp_id,
		struct sde_crtc *crtc, struct sde_crtc_state *cstate)
{
	int ret = 0;

	if (!crtc || !cmd_buf) {
		SDE_ERROR("invalid args\n");
		return -EINVAL;
	}

	_hfi_crtc_populate_props(cmd_buf, disp_id, crtc, cstate);

	return ret;
}

int hfi_crtc_atomic_check(struct sde_crtc *crtc, struct sde_crtc_state *state)
{
	int ret = 0;
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	struct hfi_crtc *crtc_hfi = NULL;
	struct hfi_kms *hfi_kms;
	struct drm_crtc_state *crtc_state;
	u32 disp_id;

	if (!crtc) {
		SDE_ERROR("invalid args\n");
		return -EINVAL;
	}

	crtc_hfi = to_hfi_crtc(crtc);
	crtc_state = drm_atomic_get_new_crtc_state(state->base.state, &crtc->base);

	hfi_kms = sde_crtc_get_kms(crtc);
	if (!hfi_kms)
		return -EINVAL;

	disp_id = hfi_crtc_get_display_id(&crtc->base, crtc_state);
	if (disp_id == U32_MAX) {
		SDE_ERROR("invalid display id\n");
		return -EINVAL;
	}

	cmd_buf = hfi_kms_get_cmd_buf(hfi_kms, disp_id, HFI_CMDBUF_TYPE_ATOMIC_CHECK);
	if (!cmd_buf) {
		SDE_ERROR("failed to get cmd_buf for crtc:%d disp_id:%d\n",
				DRMID(&crtc->base), disp_id);
		return -EINVAL;
	}

	ret = hfi_crtc_add_hfi_cmds(cmd_buf, disp_id, crtc, state);
	if (ret) {
		SDE_ERROR("failed to add hfi cmds\n");
		return ret;
	}

	return ret;
}

int hfi_crtc_atomic_begin(struct sde_crtc *sde_crtc, struct sde_crtc_state *cstate)
{
	int ret = 0;
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	struct hfi_crtc *crtc_hfi = NULL;
	struct hfi_kms *hfi_kms;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	u32 disp_id;

	if (!sde_crtc) {
		SDE_ERROR("invalid args\n");
		return -EINVAL;
	}

	crtc_hfi = to_hfi_crtc(sde_crtc);
	crtc_state = &cstate->base;
	crtc = &sde_crtc->base;

	hfi_kms = sde_crtc_get_kms(sde_crtc);
	if (!hfi_kms)
		return -EINVAL;

	disp_id = hfi_crtc_get_display_id(crtc, crtc->state);
	if (disp_id == U32_MAX) {
		SDE_ERROR("invalid display id\n");
		return -EINVAL;
	}

	cmd_buf = hfi_kms_get_cmd_buf(hfi_kms, disp_id, HFI_CMDBUF_TYPE_ATOMIC_COMMIT);
	if (!cmd_buf) {
		SDE_ERROR("failed to get cmd_buf for crtc:%d disp_id:%d\n",
				DRMID(&sde_crtc->base), disp_id);
		return -EINVAL;
	}

	ret = hfi_crtc_add_hfi_cmds(cmd_buf, disp_id, sde_crtc, cstate);
	if (ret) {
		SDE_ERROR("failed to add hfi cmds\n");
		return ret;
	}

	return ret;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
int hfi_crtc_debugfs_misr_setup(struct sde_crtc *sde_crtc)
{
	int rc = 0;
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	struct hfi_kms *hfi_kms;
	struct drm_crtc *crtc;
	struct misr_setup_data misr_data;
	u32 disp_id;

	if (!sde_crtc) {
		SDE_ERROR("invalid args\n");
		return -EINVAL;
	}

	hfi_kms = sde_crtc_get_kms(sde_crtc);
	if (!hfi_kms)
		return -EINVAL;

	crtc = &sde_crtc->base;

	disp_id = hfi_crtc_get_display_id(crtc, crtc->state);
	if (disp_id == U32_MAX) {
		SDE_ERROR("invalid display id\n");
		return -EINVAL;
	}

	cmd_buf = hfi_adapter_get_cmd_buf(&hfi_kms->hfi_client,
			MSM_DRV_HFI_ID, HFI_CMDBUF_TYPE_GET_DEBUG_DATA);
	if (!cmd_buf) {
		SDE_ERROR("Failed to get valid command buffer\n");
		return -EINVAL;
	}

	misr_data.display_id = disp_id;
	misr_data.enable = sde_crtc->misr_enable_debugfs;
	misr_data.frame_count = sde_crtc->misr_frame_count;
	misr_data.module_type = HFI_DEBUG_MISR_MIXER;

	rc = hfi_adapter_add_set_property(&hfi_kms->hfi_client, cmd_buf,
			HFI_COMMAND_DEBUG_MISR_SETUP, disp_id, HFI_PAYLOAD_TYPE_U32_ARRAY,
			&misr_data, sizeof(misr_data), HFI_HOST_FLAGS_NONE);
	if (rc) {
		SDE_ERROR("Failed to add property\n");
		return rc;
	}

	SDE_DEBUG("misr_setup: sending cmd buf\n");
	rc = hfi_adapter_set_cmd_buf(&hfi_kms->hfi_client, cmd_buf);
	SDE_EVT32(crtc->base.id, disp_id, HFI_COMMAND_DEBUG_MISR_SETUP, rc, SDE_EVTLOG_FUNC_CASE1);
	if (rc) {
		SDE_ERROR("Failed to send misr_setup command\n");
		return rc;
	}

	return rc;
}

void hfi_crtc_misr_read_hfi_prop_handler(u32 obj_uid, u32 CMD_ID, void *payload, u32 size,
			struct hfi_prop_listener *hfi_listener)
{
	struct hfi_crtc *hfi_crtc;
	struct misr_read_data_ret *misr_data;
	struct sde_misr_values *misr_read_values;
	u32 max_count = 0;
	u32 module_type = 0;
	u32 *misr_values;

	if (!hfi_listener)
		return;

	hfi_crtc = container_of(hfi_listener, struct hfi_crtc, misr_read_listener);

	/* Parse MISR values*/
	if (!payload || !size) {
		SDE_ERROR("Invalid payload received from FW\n");
		return;
	}

	SDE_DEBUG("About to read MISR values from %s\n", __func__);

	misr_read_values = &hfi_crtc->sde_base->misr_vals;
	misr_data = (struct misr_read_data_ret *)payload;

	max_count = misr_data->num_misr;
	module_type = misr_data->module_type;
	SDE_DEBUG("crtc_id:%d, Module_type:%d, Max_count:%d\n",
			HFI_CRTC_ID(hfi_crtc), module_type, max_count);
	misr_values = (u32 *)(payload + sizeof(u32) * 2);

	memset(&misr_read_values->misr_values, 0, sizeof(u32) * MAX_MISR_MODULES);

	for (int i = 0; i < max_count; i++)
		misr_read_values->misr_values[i] = misr_values[i];

	misr_read_values->count = max_count;
}

int hfi_crtc_debugfs_misr_read(struct sde_crtc *sde_crtc)
{
	int rc = 0;
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	struct hfi_kms *hfi_kms;
	struct drm_crtc *crtc;
	struct hfi_crtc *hfi_crtc;
	struct misr_read_data misr_read;
	u32 disp_id;

	hfi_kms = sde_crtc_get_kms(sde_crtc);
	if (!hfi_kms)
		return -EINVAL;

	crtc = &sde_crtc->base;

	hfi_crtc = to_hfi_crtc(sde_crtc);
	if (!hfi_crtc)
		return -EINVAL;

	disp_id = hfi_crtc_get_display_id(crtc, crtc->state);
	if (disp_id == U32_MAX) {
		SDE_ERROR("invalid display id\n");
		return -EINVAL;
	}

	cmd_buf = hfi_adapter_get_cmd_buf(&hfi_kms->hfi_client,
			MSM_DRV_HFI_ID, HFI_CMDBUF_TYPE_GET_DEBUG_DATA);
	if (!cmd_buf) {
		SDE_ERROR("Failed to get valid command buffer\n");
		return -EINVAL;
	}

	misr_read.display_id = disp_id;
	misr_read.module_type = HFI_DEBUG_MISR_MIXER;

	/* Listener init */
	hfi_crtc->misr_read_listener.hfi_prop_handler = &hfi_crtc_misr_read_hfi_prop_handler;

	rc = hfi_adapter_add_get_property(&hfi_kms->hfi_client, cmd_buf,
			HFI_COMMAND_DEBUG_MISR_READ, disp_id,
			HFI_PAYLOAD_TYPE_U32_ARRAY, &misr_read, sizeof(misr_read),
			&hfi_crtc->misr_read_listener, (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
			HFI_HOST_FLAGS_NON_DISCARDABLE));
	if (rc)
		SDE_ERROR("Failed to add MISR read command!\n");

	SDE_EVT32(crtc->base.id, disp_id, HFI_COMMAND_DEBUG_MISR_READ, SDE_EVTLOG_FUNC_CASE1);
	rc = hfi_adapter_set_cmd_buf_blocking(&hfi_kms->hfi_client, cmd_buf);
	SDE_EVT32(crtc->base.id, disp_id, HFI_COMMAND_DEBUG_MISR_READ, rc, SDE_EVTLOG_FUNC_CASE2);

	return rc;

}
#else
int hfi_crtc_debugfs_misr_setup(struct sde_crtc *sde_crtc)
{
	return 0;
}

int hfi_crtc_debugfs_misr_read(struct sde_crtc *sde_crtc)
{
	return 0;
}
#endif /* CONFIG_DEBUG_FS */

static void hfi_crtc_prop_handler(u32 obj_id, u32 cmd_id,
		void *payload, u32 size, struct hfi_prop_listener *listener)
{
	struct hfi_crtc *hfi_crtc = container_of(listener,
			struct hfi_crtc, hfi_cb_obj);
	struct sde_crtc *sde_crtc = NULL;
	struct hfi_display_ltm_event_resp *event_payload = NULL;

	if (!hfi_crtc) {
		SDE_ERROR("hfi_crtc is NULL\n");
		return;
	}

	sde_crtc = hfi_crtc->sde_base;
	if (!sde_crtc) {
		SDE_ERROR("sde_crtc is NULL\n");
		return;
	}

	switch (cmd_id) {
	case HFI_COMMAND_DISPLAY_EVENT_LTM:
		event_payload = (struct hfi_display_ltm_event_resp *)payload;
		if (event_payload->event_type == HFI_LTM_HIST_DONE)
			sde_crtc->crtc_event_cb(sde_crtc, DRM_EVENT_LTM_HIST, event_payload);
		else if (event_payload->event_type == HFI_LTM_WB_PB)
			sde_crtc->crtc_event_cb(sde_crtc, DRM_EVENT_LTM_WB_PB, event_payload);
		else if (event_payload->event_type == HFI_LTM_HIST_OFF)
			sde_crtc->crtc_event_cb(sde_crtc, DRM_EVENT_LTM_OFF, event_payload);
		else
			SDE_ERROR("unknown LTM event type %d\n", event_payload->event_type);
		break;
	case HFI_COMMAND_DISPLAY_EVENT_RGB_HIST: {
		struct hfi_display_rgb_hist_event_resp *event_payload;

		event_payload = payload;
		if (size != sizeof(struct hfi_display_rgb_hist_event_resp)) {
			SDE_ERROR("Invalid size for rgb hist event, size %d\n", size);
			return;
		}

		if (event_payload->event_type == HFI_RGB_HIST_DONE)
			sde_crtc->crtc_event_cb(sde_crtc, DRM_EVENT_RGB_HIST, event_payload);
		else if (event_payload->event_type == HFI_RGB_HIST_WB_ERR)
			sde_crtc->crtc_event_cb(sde_crtc, DRM_EVENT_RGB_HIST_WB_ERR, event_payload);
		else if (event_payload->event_type == HFI_RGB_HIST_OFF)
			sde_crtc->crtc_event_cb(sde_crtc, DRM_EVENT_RGB_HIST_OFF, event_payload);
		else
			SDE_ERROR("Invalid RGB Hist event type %d\n", event_payload->event_type);
		break;
	}
	case HFI_COMMAND_DISPLAY_EVENT_PA_HIST:
	{
		struct hfi_display_pa_hist_event_resp *event_payload;

		event_payload = (struct hfi_display_pa_hist_event_resp *)payload;
		if (size != sizeof(struct hfi_display_pa_hist_event_resp)) {
			SDE_ERROR("Invalid size for pa hist event, size %d\n", size);
			return;
		}
		if (event_payload)
			sde_crtc->crtc_event_cb(sde_crtc, DRM_EVENT_HISTOGRAM, event_payload);
		else
			SDE_ERROR("Invalid PA Hist event payload\n");
		break;
	}
	default:
		SDE_ERROR("invalid hfi command 0x%x\n", cmd_id);
	}
}

static int _hfi_crtc_hw_event_set_buff(struct sde_crtc *crtc, u32 payload,
		bool enable, bool defer_to_commit)
{
	struct hfi_crtc *hfi_crtc = to_hfi_crtc(crtc);
	struct hfi_kms *hfi_kms = sde_crtc_get_kms(crtc);
	struct hfi_cmdbuf_t *cmd_buf;
	u32 cmd, display_id = 0;
	int ret = 0;

	if (!hfi_crtc || !hfi_kms) {
		SDE_ERROR("invalid hfi_crtc:%pK or hfi_kms:%pK\n", hfi_crtc, hfi_kms);
		return -EINVAL;
	}

	display_id = hfi_crtc_get_display_id(&crtc->base, crtc->base.state);
	if (defer_to_commit) {
		cmd_buf = hfi_kms_get_cmd_buf(hfi_kms, display_id, HFI_CMDBUF_TYPE_ATOMIC_COMMIT);
	} else {
		cmd_buf = hfi_adapter_get_cmd_buf(&hfi_kms->hfi_client,
				display_id, HFI_CMDBUF_TYPE_DISPLAY_INFO_BLOCKING);
	}

	if (!cmd_buf) {
		SDE_ERROR("crtc:%d failed to get cmd buf in events enable:%d for display:%d\n",
				DRMID(&crtc->base), enable, display_id);
		return -EINVAL;
	}

	cmd = enable ? HFI_COMMAND_DISPLAY_EVENT_REGISTER : HFI_COMMAND_DISPLAY_EVENT_DEREGISTER;
	ret = hfi_adapter_add_get_property(cmd_buf->ctx, cmd_buf, cmd,
			display_id, HFI_PAYLOAD_TYPE_U32,
			&payload, sizeof(payload), &hfi_crtc->hfi_cb_obj,
			HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (ret) {
		SDE_ERROR("failed to update event: 0x%x\n", payload);
		return ret;
	}

	SDE_DEBUG("sending event:%d enable:%d for display:%d\n", payload, enable, display_id);
	if (!defer_to_commit) {
		ret = hfi_adapter_set_cmd_buf(cmd_buf->ctx, cmd_buf);
		SDE_EVT32(DRMID(&crtc->base), display_id, cmd, ret, SDE_EVTLOG_FUNC_CASE1);
		if (ret) {
			SDE_ERROR("failed to send event register command\n");
			return ret;
		}
	}

	return 0;
}

static int hfi_crtc_enable_hw_event(struct sde_crtc *crtc, u32 event, bool enable)
{
	int ret = 0;
	struct hfi_crtc *hfi_crtc = NULL;

	if (!crtc || event < HFI_EVENT_VSYNC) {
		SDE_ERROR("invalid crtc:%pK or event id: %d\n", crtc, event);
		return -EINVAL;
	}

	hfi_crtc = to_hfi_crtc(crtc);
	if (!hfi_crtc) {
		SDE_ERROR("invalid hfi_crtc:%p\n", hfi_crtc);
		return -EINVAL;
	}

	switch (event) {
	case HFI_EVENT_LTM:
		ret = _hfi_crtc_hw_event_set_buff(crtc, event, enable, false);
		if (ret) {
			SDE_ERROR("event registration failed: event %d, enable %d\n",
				event, enable);
			return ret;
		}

		hfi_crtc->hw_events_state[HFI_CRTC_EVENT_LTM].state = enable;
		hfi_crtc->hw_events_state[HFI_CRTC_EVENT_LTM].pending = false;
		break;
	case HFI_EVENT_RGB_HIST:
		ret = _hfi_crtc_hw_event_set_buff(crtc, event, enable, false);
		if (ret) {
			SDE_ERROR("event registration failed: event %d, enable %d\n",
				event, enable);
			return ret;
		}

		hfi_crtc->hw_events_state[HFI_CRTC_EVENT_RGB_HIST].state = enable;
		hfi_crtc->hw_events_state[HFI_CRTC_EVENT_RGB_HIST].pending = false;
		break;
	case HFI_EVENT_PA_HIST:
		ret = _hfi_crtc_hw_event_set_buff(crtc, event, enable, false);
		if (ret) {
			SDE_ERROR("event registration failed: event %d, enable %d\n",
				event, enable);
			return ret;
		}

		hfi_crtc->hw_events_state[HFI_CRTC_EVENT_PA_HIST].state = enable;
		hfi_crtc->hw_events_state[HFI_CRTC_EVENT_PA_HIST].pending = false;
		break;
	default:
		break;
	}

	return ret;
}

int hfi_crtc_set_idle_pc_timer(struct sde_crtc *sde_crtc, u32 val)
{
	struct hfi_cmdbuf_t *cmd_buf;
	struct hfi_kms *hfi_kms;
	struct drm_crtc *crtc;
	enum hfi_display_idle_timer_control payload;
	u32 disp_id;
	int rc = 0;

	if (!sde_crtc)
		return -EINVAL;

	hfi_kms = sde_crtc_get_kms(sde_crtc);
	if (!hfi_kms)
		return -EINVAL;

	crtc = &sde_crtc->base;
	if (!crtc->state)
		return -EINVAL;

	disp_id = hfi_crtc_get_display_id(crtc, crtc->state);
	if (disp_id == U32_MAX) {
		SDE_ERROR("invalid display id\n");
		return -EINVAL;
	}

	//Validate that value provided is in the range of enum.
	if (val > HFI_UNBLOCK_TIMER)
		return -EINVAL;

	payload = val;

	cmd_buf = hfi_adapter_get_cmd_buf(&hfi_kms->hfi_client,
			disp_id, HFI_CMDBUF_TYPE_DISPLAY_INFO_BLOCKING);
	if (!cmd_buf) {
		SDE_ERROR("Failed to get valid command buffer\n");
		return -EINVAL;
	}

	rc = hfi_adapter_add_set_property(&hfi_kms->hfi_client, cmd_buf,
		HFI_COMMAND_DISPLAY_IDLE_TIMER_CONTROL, disp_id, HFI_PAYLOAD_TYPE_U32,
		&payload, sizeof(payload), HFI_HOST_FLAGS_RESPONSE_REQUIRED);
	if (rc) {
		SDE_ERROR("Failed to add property rc:%d\n", rc);
		hfi_adapter_release_cmd_buf(&hfi_kms->hfi_client, cmd_buf);
		return rc;
	}

	rc = hfi_adapter_set_cmd_buf_blocking(&hfi_kms->hfi_client, cmd_buf);
	if (rc)
		SDE_ERROR("Failed to send idle pc timer control rc:%d\n", rc);

	return rc;
}

int _sde_crtc_hal_funcs_install(struct sde_crtc *crtc)
{
	if (!crtc) {
		SDE_ERROR("invalid args\n");
		return -EINVAL;
	}

	crtc->hal_ops.destroy[MSM_DISP_OP_HFI] = hfi_crtc_destroy;
	crtc->hal_ops.atomic_begin[MSM_DISP_OP_HFI] = hfi_crtc_atomic_begin;
	crtc->hal_ops.debugfs_misr_setup[MSM_DISP_OP_HFI] = hfi_crtc_debugfs_misr_setup;
	crtc->hal_ops.debugfs_misr_read[MSM_DISP_OP_HFI] = hfi_crtc_debugfs_misr_read;
	crtc->hal_ops.enable_hw_event[MSM_DISP_OP_HFI] = hfi_crtc_enable_hw_event;
	crtc->hal_ops.set_idle_pc_timer[MSM_DISP_OP_HFI] = hfi_crtc_set_idle_pc_timer;

	return 0;
}

static int _hfi_cp_crtc_allocate_dither(struct sde_crtc *crtc, struct hfi_crtc *hfi_crtc)
{
	int ret = 0;
	struct hfi_kms *hfi_kms;

	hfi_kms = sde_crtc_get_kms(crtc);
	if (!hfi_kms) {
		SDE_ERROR("%s: failed to get hfi kms\n", __func__);
		return -EINVAL;
	}

	hfi_crtc->hfi_buff_map_dither.size =
		sizeof(struct hfi_display_dither) * (DSPP_MAX - DSPP_0);
	ret = hfi_adapter_buffer_alloc(&hfi_kms->hfi_client, &hfi_crtc->hfi_buff_map_dither);
	if (ret) {
		hfi_crtc->hfi_buff_map_dither.size = 0;
		SDE_DEBUG("failed to allocate shared memory for SPR Dither, ret: %d\n", ret);
		return ret;
	}

	return ret;
}

int hfi_crtc_init(struct sde_crtc *sde_crtc)
{
	struct hfi_crtc *crtc = NULL;
	int ret;

	if (!sde_crtc)
		return -EINVAL;

	crtc = kvzalloc(sizeof(*crtc), GFP_KERNEL);
	if (!crtc) {
		SDE_ERROR("failed to allocate memory for hfi crtc\n");
		return -ENOMEM;
	}

	_sde_crtc_hal_funcs_install(sde_crtc);

	mutex_init(&crtc->hfi_lock);

	/* alloc util storage handlers for hfi props */
	crtc->kv_props = hfi_util_kv_helper_alloc(HFI_CRTC_MAX_PROPS);
	if (IS_ERR(crtc->kv_props)) {
		SDE_ERROR("failed to allocate memory for kv prop collector\n");
		ret = -ENOMEM;
		goto free_crtc;
	}

	crtc->base_props = hfi_util_u32_prop_helper_alloc(HFI_CRTC_BASE_PROP_MAX_SIZE);
	if (IS_ERR(crtc->base_props)) {
		SDE_ERROR("failed to allocate memory for base prop collector\n");
		ret = -ENOMEM;
		goto free_kv;
	}

	crtc->color_props = hfi_util_u32_prop_helper_alloc(HFI_CRTC_BASE_PROP_MAX_SIZE);
	if (IS_ERR(crtc->color_props)) {
		SDE_ERROR("failed to allocate memory for color prop collector\n");
		ret = -ENOMEM;
		goto free_kv;
	}

	ret = _hfi_cp_crtc_allocate_dither(sde_crtc, crtc);
	if (ret)
		SDE_DEBUG("failed to allocated shared memory for dither payloads ret: %d\n", ret);

	ret = hfi_cp_crtc_alloc_pa_hist_buffers(sde_crtc);
	if (ret)
		SDE_ERROR("failed to allocate pa hist buffers: %d\n", ret);

	crtc->hfi_cb_obj.hfi_prop_handler = hfi_crtc_prop_handler;
	crtc->sde_base = sde_crtc;
	sde_crtc->hfi_crtc = crtc;
	return 0;

free_kv:
	kfree(crtc->base_props);
	kfree(crtc->kv_props);
free_crtc:
	mutex_destroy(&crtc->hfi_lock);
	kfree(crtc);

	return -ENOMEM;
}

struct hfi_cmdbuf_t *hfi_crtc_get_cmd_buf(struct drm_crtc *crtc)
{
	u32 disp_id = 0;
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	struct hfi_kms *hfi_kms = NULL;
	struct msm_drm_private *priv = NULL;

	if (crtc && crtc->dev && crtc->dev->dev_private) {
		priv = crtc->dev->dev_private;
		hfi_kms = ((priv && priv->kms) ?
				to_hfi_kms(to_sde_kms(priv->kms)) : NULL);
	}
	if (!hfi_kms) {
		SDE_ERROR("invalid hfi_kms\n");
		return NULL;
	}

	disp_id = hfi_crtc_get_display_id(crtc, crtc->state);
	if (disp_id == U32_MAX) {
		SDE_ERROR("invalid display id\n");
		return NULL;
	}

	cmd_buf = hfi_kms_get_cmd_buf(hfi_kms, disp_id, HFI_CMDBUF_TYPE_ATOMIC_COMMIT);
	return cmd_buf;
}

int hfi_crtc_add_set_property(struct drm_crtc *crtc, struct hfi_cmdbuf_t *cmd_buf,
		struct hfi_util_u32_prop_helper *color_props)
{
	u32 disp_id = 0, ret = 0;

	if (!crtc || !cmd_buf || !color_props) {
		SDE_ERROR("invalid input: crtc=%p, cmd_buf=%p, color_props=%p\n",
			crtc, cmd_buf, color_props);
		return -EINVAL;
	}

	disp_id = hfi_crtc_get_display_id(crtc, crtc->state);
	if (disp_id == U32_MAX) {
		SDE_ERROR("invalid display id\n");
		return -EINVAL;
	}

	/*
	 * Once all the color processing properties are collected, invoke
	 * adapter api to add all these properties as a single HFI Packet
	 */
	ret = hfi_adapter_add_set_property(cmd_buf->ctx,
		cmd_buf,
		HFI_COMMAND_DISPLAY_SET_PROPERTY,
		disp_id,
		HFI_PAYLOAD_TYPE_U32_ARRAY,
		hfi_util_u32_prop_helper_get_payload_addr(color_props),
		hfi_util_u32_prop_helper_get_size(color_props),
		HFI_HOST_FLAGS_NONE);

	if (ret)
		SDE_ERROR("failed to set HFI prop\n");
	return ret;
}

int hfi_crtc_set_input_wait_hw_fence(struct sde_crtc *crtc, u32 h_synx, u32 prop)
{
	struct hfi_kms *hfi_kms;
	struct hfi_cmdbuf_t *cmd_buf;
	struct hfi_crtc *crtc_hfi;
	u32 disp_id;
	struct hfi_hw_fence fence_prop;
	int ret = 0;

	if (!crtc) {
		SDE_ERROR("invalid crtc\n");
		return -EINVAL;
	}

	if (!h_synx) {
		SDE_DEBUG("h_synx is 0, skipping fence send\n");
		return 0;
	}

	crtc_hfi = to_hfi_crtc(crtc);
	if (!crtc_hfi) {
		SDE_ERROR("hfi_crtc is null\n");
		return -EINVAL;
	}

	hfi_kms = sde_crtc_get_kms(crtc);
	if (!hfi_kms) {
		SDE_ERROR("failed to get hfi kms\n");
		return -EINVAL;
	}

	disp_id = hfi_crtc_get_display_id(&crtc->base, crtc->base.state);
	if (disp_id == U32_MAX) {
		SDE_ERROR("invalid display id\n");
		return -EINVAL;
	}

	cmd_buf = hfi_kms_get_cmd_buf(hfi_kms, disp_id, HFI_CMDBUF_TYPE_ATOMIC_COMMIT);
	if (!cmd_buf) {
		SDE_ERROR("failed to get cmd_buf for crtc:%d disp_id:%d\n",
				DRMID(&crtc->base), disp_id);
		return -EINVAL;
	}

	/* Format the fence property according to HFI specification */
	fence_prop.h_synx = h_synx;
	fence_prop.flags = HFI_FENCE_SCAN_START;

	if (!crtc_hfi->base_props)
		return -EINVAL;

	mutex_lock(&crtc_hfi->hfi_lock);
	hfi_util_u32_prop_helper_reset(crtc_hfi->base_props);

	ret = hfi_util_u32_prop_helper_add_prop(crtc_hfi->base_props,
			prop, HFI_VAL_U32_ARRAY, &fence_prop, sizeof(struct hfi_hw_fence));

	if (!hfi_util_u32_prop_helper_prop_count(crtc_hfi->base_props))
		goto end;

	/* Send the property to firmware */
	ret = hfi_adapter_add_set_property(cmd_buf->ctx,
			cmd_buf,
			HFI_COMMAND_DISPLAY_SET_PROPERTY,
			disp_id,
			HFI_PAYLOAD_TYPE_U32_ARRAY,
			hfi_util_u32_prop_helper_get_payload_addr(crtc_hfi->base_props),
			hfi_util_u32_prop_helper_get_size(crtc_hfi->base_props),
			HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (ret) {
		HFI_ERROR_CRTC(crtc_hfi, "failed to send fence property: %d\n", ret);
		goto end;
	}

	HFI_DEBUG_CRTC(crtc_hfi, "payload: disp_id = %d, prop = 0x%x, h_synx = 0x%x, flags = 0x%x",
			disp_id, prop, fence_prop.h_synx, fence_prop.flags);

end:
	mutex_unlock(&crtc_hfi->hfi_lock);
	return ret;
}
