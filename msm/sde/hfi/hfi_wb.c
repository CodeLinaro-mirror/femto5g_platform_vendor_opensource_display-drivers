// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hfi_connector.h"
#include "hfi_kms.h"
#include "hfi_crtc.h"
#include "hfi_props.h"
#include "sde_wb.h"
#include "sde_encoder_phys.h"
#include "hfi_plane.h"
#include "hfi_catalog.h"
#include "hfi_defs_display.h"
#include "hfi_wb.h"
#include "hfi_defs_lsr.h"

#define to_sde_encoder_phys_wb(x) \
	container_of(x, struct sde_encoder_phys_wb, base)

/**
 * _hfi_wb_get_out_resolution - get output resolution considering DNSC
 * @cstate: connector state
 * @out_width: pointer to populate output width
 * @out_height: pointer to populate output height
 * @dnsc_blur_res: pointer to the output struct to populate the src/dst
 */
static void _hfi_wb_get_out_resolution(struct sde_connector_state *cstate,
		u32 *out_width, u32 *out_height, struct sde_io_res *dnsc_blur_res)
{
	const struct drm_display_mode *mode = cstate->msm_mode.base;
	enum sde_wb_rot_type rotation_type;

	if (!cstate || !dnsc_blur_res || !out_width || !out_height)
		return;

	sde_connector_get_dnsc_blur_io_res(&cstate->base, dnsc_blur_res);
	rotation_type = sde_connector_get_property(&cstate->base, CONNECTOR_PROP_WB_ROT_TYPE);

	if (dnsc_blur_res->enabled) {
		*out_width = dnsc_blur_res->dst_w;
		*out_height = dnsc_blur_res->dst_h;
	} else {
		*out_width = mode->hdisplay;
		*out_height = mode->vdisplay;
	}

	if (rotation_type != WB_ROT_NONE)
		swap(*out_width, *out_height);
}

static int _hfi_wb_add_roi_prop(struct sde_wb_device *wb_dev,
		struct sde_connector_state *cstate,
		struct hfi_util_u32_prop_helper *prop_collector)
{
	u32 prop_id;
	const struct drm_display_mode *mode = cstate->msm_mode.base;
	struct hfi_display_roi src_roi, dst_roi;
	struct sde_rect roi;
	struct sde_io_res dnsc_blur_res = {0, };
	u32 out_width = 0, out_height = 0;
	int ret = 0;

	/* Get output resolution considering DNSC */
	_hfi_wb_get_out_resolution(cstate, &out_width, &out_height, &dnsc_blur_res);

	if (dnsc_blur_res.enabled) {
		/* For DNSC enabled case, source ROI is DNSC source dimensions */
		HFI_POPULATE_RECT(&src_roi, 0, 0, dnsc_blur_res.src_w,
				dnsc_blur_res.src_h, false);
	} else {
		/* For non-DNSC case, source ROI is mode dimensions */
		HFI_POPULATE_RECT(&src_roi, 0, 0, mode->hdisplay, mode->vdisplay, false);
	}

	sde_wb_get_output_roi(wb_dev, &roi);

	/* If ROI is not set, use output resolution */
	if (!roi.w || !roi.h) {
		roi.x = 0;
		roi.y = 0;
		roi.w = out_width;
		roi.h = out_height;
	}

	HFI_POPULATE_RECT(&dst_roi, roi.x, roi.y, roi.w, roi.h, false);

	prop_id = HFI_PROPERTY_OUTPUT_LAYER_SRC_ROI;
	u32 src_roi_payload[1 + (sizeof(struct hfi_display_roi) / sizeof(u32))];

	src_roi_payload[0] = to_sde_connector(wb_dev->connector)->conn_id;
	memcpy(&src_roi_payload[1], &src_roi, sizeof(struct hfi_display_roi));
	hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id,
		HFI_VAL_U32_ARRAY, src_roi_payload, sizeof(src_roi_payload));

	prop_id = HFI_PROPERTY_OUTPUT_LAYER_DST_ROI;
	u32 dst_roi_payload[1 + (sizeof(struct hfi_display_roi) / sizeof(u32))];

	dst_roi_payload[0] = to_sde_connector(wb_dev->connector)->conn_id;
	memcpy(&dst_roi_payload[1], &dst_roi, sizeof(struct hfi_display_roi));
	hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id,
		HFI_VAL_U32_ARRAY, dst_roi_payload, sizeof(dst_roi_payload));

	return ret;
}

static int _hfi_wb_add_dnsc_prop(struct sde_wb_device *wb_dev,
		struct sde_connector_state *cstate,
		struct hfi_util_u32_prop_helper *prop_collector)
{
	u32 prop_id;
	int ret = 0;
	int i, j;
	struct sde_drm_dnsc_blur_cfg *src_cfg;
	struct hfi_dnsc_blur_cfg hfi_cfg;
	struct sde_connector *sde_conn;
	struct hfi_connector *hfi_conn;
	struct hfi_shared_addr_map *hfi_buff_map = NULL;
	u32 payload_size;
	struct hfi_buff prop_hfi_buff;
	u64 fw_buff_addr;
	u32 dnsc_payload[1 + (sizeof(struct hfi_buff) / sizeof(u32))];

	if (!cstate->dnsc_blur_count)
		return ret;

	sde_conn = to_sde_connector(wb_dev->connector);
	hfi_conn = sde_conn->hfi_conn;

	if (!hfi_conn) {
		SDE_ERROR("hfi_conn is NULL\n");
		return -EINVAL;
	}

	hfi_buff_map = &hfi_conn->hfi_buff_base_props[HFI_BUFF_CONN_DNSC];
	payload_size = sizeof(struct hfi_dnsc_blur_cfg);

	if (!hfi_buff_map || !hfi_buff_map->remote_addr ||
		!hfi_buff_map->local_addr) {
		SDE_ERROR("Invalid hfi_buff_map: %pK, remote_addr %lu, local_addr %pK\n",
			hfi_buff_map ? hfi_buff_map : NULL, (hfi_buff_map->remote_addr ?
			hfi_buff_map->remote_addr : 0), (hfi_buff_map->local_addr ?
			hfi_buff_map->local_addr : NULL));
		return -EINVAL;
	}

	for (i = 0; i < cstate->dnsc_blur_count; i++) {
		src_cfg = &cstate->dnsc_blur_cfg[i];
		memset(&hfi_cfg, 0, sizeof(struct hfi_dnsc_blur_cfg));

		/* Copy basic configuration fields */
		hfi_cfg.flags = (u32)src_cfg->flags;
		hfi_cfg.src_width = src_cfg->src_width;
		hfi_cfg.src_height = src_cfg->src_height;
		hfi_cfg.dst_width = src_cfg->dst_width;
		hfi_cfg.dst_height = src_cfg->dst_height;

		/* Copy filter flags */
		hfi_cfg.flags_h = src_cfg->flags_h;
		hfi_cfg.flags_v = src_cfg->flags_v;

		/* Copy phase and scaling parameters */
		hfi_cfg.phase_init_h = src_cfg->phase_init_h;
		hfi_cfg.phase_step_h = src_cfg->phase_step_h;
		hfi_cfg.phase_init_v = src_cfg->phase_init_v;
		hfi_cfg.phase_step_v = src_cfg->phase_step_v;

		/* Copy normalization and ratio parameters */
		hfi_cfg.norm_h = src_cfg->norm_h;
		hfi_cfg.ratio_h = src_cfg->ratio_h;
		hfi_cfg.norm_v = src_cfg->norm_v;
		hfi_cfg.ratio_v = src_cfg->ratio_v;

		/* Copy coefficient arrays */
		for (j = 0; j < HFI_DNSC_BLUR_COEF_NUM; j++) {
			hfi_cfg.coef_hori[j] = src_cfg->coef_hori[j];
			hfi_cfg.coef_vert[j] = src_cfg->coef_vert[j];
		}

		hfi_cfg.dither_cfg.flags = (u32)src_cfg->dither_flags;
		hfi_cfg.dither_cfg.feature_flags = (u32)(src_cfg->dither_flags >> 32);
		hfi_cfg.dither_cfg.temporal_en = src_cfg->temporal_en;
		hfi_cfg.dither_cfg.c0_bitdepth = src_cfg->c0_bitdepth;
		hfi_cfg.dither_cfg.c1_bitdepth = src_cfg->c1_bitdepth;
		hfi_cfg.dither_cfg.c2_bitdepth = src_cfg->c2_bitdepth;
		hfi_cfg.dither_cfg.c3_bitdepth = src_cfg->c3_bitdepth;

		/* Copy dither matrix */
		for (j = 0; j < HFI_DNSC_DITHER_MATRIX_SZ; j++)
			hfi_cfg.dither_cfg.matrix[j] = src_cfg->dither_matrix[j];

		/* Copy data to shared buffer using local address */
		if (i * payload_size + payload_size > hfi_buff_map->size) {
			SDE_ERROR("Not enough memory, remaining size %u, payload_size %u\n",
				hfi_buff_map->size - (i * payload_size), payload_size);
			return -EINVAL;
		}
		memcpy(hfi_buff_map->local_addr + (i * payload_size), &hfi_cfg, payload_size);
	}

	/* Send hfi_buff with remote address to firmware */
	fw_buff_addr = (u64)hfi_buff_map->remote_addr;
	prop_hfi_buff.addr_l = (fw_buff_addr & 0xFFFFFFFF);
	prop_hfi_buff.addr_h = (fw_buff_addr >> 32);
	prop_hfi_buff.size = (payload_size / sizeof(u32)) * cstate->dnsc_blur_count;
	prop_hfi_buff.version = 0;
	prop_hfi_buff.flags = 0;

	/* Create payload with wb_id followed by hfi_buff structure */
	dnsc_payload[0] = sde_conn->conn_id;
	memcpy(&dnsc_payload[1], &prop_hfi_buff, sizeof(struct hfi_buff));

	prop_id = HFI_PROPERTY_OUTPUT_LAYER_DNSC_BLUR_CFG;
	ret = hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id,
		HFI_VAL_U32_ARRAY, dnsc_payload, sizeof(dnsc_payload));
	if (ret)
		SDE_ERROR("Failed to add hfi prop %d ret %d\n", prop_id, ret);

	return ret;
}

static int _hfi_wb_add_drm_props(struct sde_wb_device *wb_dev,
		struct hfi_connector *hfi_conn,
		struct sde_connector_state *cstate,
		struct hfi_util_u32_prop_helper *prop_collector,
		u32 disp_id)
{
	u32 prop_id;
	int width, height;
	int ret = 0;
	u32 hfi_format;
	u32 wb_rotate_type;
	u32 rotation_flags, connector_cache_state_prop = 0, rot_payload[2];
	struct drm_framebuffer *fb;
	struct sde_hw_wb_cfg wb_cfg = {0,};
	struct sde_sc_cfg *sc_cfg = NULL;
	struct sde_encoder_virt *sde_enc;
	struct sde_encoder_phys_wb *wb_enc;
	struct sde_hw_wb *hw_wb;
	struct sde_format_extended fmt = {0,};
	struct sde_connector *sde_conn = to_sde_connector(wb_dev->connector);
	/* This should be the index which this WB device received as part of init caps */
	u32 wb_id = sde_conn->conn_id;
	u32 format_payload[2], width_payload[2], height_payload[2];
	u32 addr_payload[1 + SDE_MAX_PLANES], stride_payload[1 + SDE_MAX_PLANES];
	u32 tap_point, tap_payload[2], cache_attr_payload[3], llcc_scid_payload[2];
	u32 sec_policy, sec_payload[2];

	sde_enc = to_sde_encoder_virt(wb_dev->encoder);

	if (!sde_enc) {
		SDE_ERROR("failed to get sde encoder\n");
		return -EINVAL;
	}

	wb_enc = to_sde_encoder_phys_wb(sde_enc->phys_encs[0]);
	if (!wb_enc) {
		SDE_ERROR("failed to get wb encoder\n");
		return -EINVAL;
	}

	hw_wb = wb_enc->hw_wb;

	connector_cache_state_prop = sde_connector_get_property(&cstate->base,
		CONNECTOR_PROP_CACHE_STATE);
	if (connector_cache_state_prop && hw_wb && hw_wb->catalog)
		sc_cfg = &hw_wb->catalog->sc_cfg[SDE_SYS_CACHE_DISP];

	prop_id = HFI_PROPERTY_DISPLAY_ATTACH_OUTPUT_LAYER;
	hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id,
		HFI_VAL_U32, &wb_id, sizeof(u32));

	fb = sde_wb_get_output_fb(wb_dev);
	if (!fb) {
		SDE_ERROR("failed to get output buffer\n");
		return -EINVAL;
	}

	fmt.fourcc_format = fb->format->format;
	fmt.modifier = fb->modifier;

	hfi_format = hfi_catalog_get_hfi_format(&fmt);
	prop_id = HFI_PROPERTY_OUTPUT_LAYER_DST_FORMAT;
	format_payload[0] = wb_id;
	format_payload[1] = hfi_format;
	hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id,
		HFI_VAL_U32_ARRAY, format_payload, sizeof(format_payload));

	/*
	 * Use framebuffer dimensions for SRC_IMG_SIZE properties
	 * This matches the HWIO path behavior in sde_encoder_phys_wb_setup_fb()
	 * where wb_cfg->dest.width/height are set to fb->width/height
	 */
	width = fb->width;
	height = fb->height;

	prop_id = HFI_PROPERTY_OUTPUT_LAYER_SRC_IMG_SIZE_W;
	width_payload[0] = wb_id;
	width_payload[1] = width;
	hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id,
		HFI_VAL_U32_ARRAY, width_payload, sizeof(width_payload));

	prop_id = HFI_PROPERTY_OUTPUT_LAYER_SRC_IMG_SIZE_H;
	height_payload[0] = wb_id;
	height_payload[1] = height;
	hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id,
		HFI_VAL_U32_ARRAY, height_payload, sizeof(height_payload));

	ret = sde_wb_get_scan_out_info(wb_dev, cstate, fb, &wb_cfg);
	if (ret) {
		SDE_ERROR("failed to get scan out info\n");
		return ret;
	}

	prop_id = HFI_PROPERTY_OUTPUT_LAYER_DST_ADDR;
	addr_payload[0] = wb_id;
	memcpy(&addr_payload[1], &wb_cfg.dest.plane_addr[0], sizeof(u32) * SDE_MAX_PLANES);
	hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id,
		HFI_VAL_U32_ARRAY, addr_payload, sizeof(addr_payload));

	prop_id = HFI_PROPERTY_OUTPUT_LAYER_STRIDE;
	stride_payload[0] = wb_id;
	memcpy(&stride_payload[1], &wb_cfg.dest.plane_pitch[0], sizeof(u32) * SDE_MAX_PLANES);
	hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id,
		HFI_VAL_U32_ARRAY, stride_payload, sizeof(stride_payload));

	prop_id = HFI_PROPERTY_OUTPUT_LAYER_CACHE_ATTR;
	cache_attr_payload[0] = wb_id;
	if (connector_cache_state_prop && sc_cfg)
		cache_attr_payload[1] = HFI_CACHE_STATE_WRITE;
	else
		cache_attr_payload[1] = HFI_CACHE_STATE_DISABLE;
	cache_attr_payload[2] = HFI_CACHE_OP_TYPE_NONE;
	hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id, HFI_VAL_U32_ARRAY,
		cache_attr_payload, sizeof(cache_attr_payload));

	if (cache_attr_payload[1] != HFI_CACHE_STATE_DISABLE) {
		prop_id = HFI_PROPERTY_OUTPUT_LAYER_LLCC_SCID;
		if (!sc_cfg) {
			SDE_ERROR("Invalid sc_cfg\n");
			return -EINVAL;
		}
		llcc_scid_payload[0] = wb_id;
		llcc_scid_payload[1] = sc_cfg->llcc_scid;
		hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id, HFI_VAL_U32_ARRAY,
			llcc_scid_payload, sizeof(llcc_scid_payload));
	}

	if (connector_cache_state_prop)
		sde_crtc_set_cwb_idle(sde_conn->base.state->crtc);

	tap_point = HFI_TAP_POINT_NONE;

	/* Add ROI properties with DNSC consideration */
	_hfi_wb_add_roi_prop(wb_dev, cstate, hfi_conn->base_props);

	wb_rotate_type = sde_connector_get_property(&cstate->base, CONNECTOR_PROP_WB_ROT_TYPE);

	/* Only set rotation property if WB rotation is enabled */
	if (wb_rotate_type != WB_ROT_NONE) {
		rotation_flags = HFI_DISPLAY_ROTATION_90;
		prop_id = HFI_PROPERTY_OUTPUT_LAYER_ROTATION;
		rot_payload[0] = wb_id;
		rot_payload[1] = rotation_flags;
		hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id,
			HFI_VAL_U32_ARRAY, rot_payload, sizeof(rot_payload));
	}

	if (cstate->capture_mode == CAPTURE_MIXER_OUT)
		tap_point = HFI_TAP_POINT_LM;
	else if (cstate->capture_mode == CAPTURE_DSPP_OUT)
		tap_point = HFI_TAP_POINT_DSPP;
	else if (cstate->capture_mode == CAPTURE_DEMURA_OUT)
		tap_point = HFI_TAP_POINT_DEMURA;

	if (tap_point != HFI_TAP_POINT_NONE) {
		prop_id = HFI_PROPERTY_OUTPUT_LAYER_CWB_TAP_POINT;
		tap_payload[0] = wb_id;
		tap_payload[1] = tap_point;
		hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id,
			HFI_VAL_U32_ARRAY, tap_payload, sizeof(tap_payload));
	}

	prop_id = HFI_PROPERTY_OUTPUT_LAYER_CWB_TAP_POINT;
	hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
		wb_id, HFI_VAL_U32, &tap_point, sizeof(u32));

	_hfi_wb_add_dnsc_prop(wb_dev, cstate, hfi_conn->base_props);

	prop_id = HFI_PROPERTY_OUTPUT_LAYER_SECURITY_POLICY;
	sec_policy = sde_connector_get_property(&cstate->base,
		CONNECTOR_PROP_FB_TRANSLATION_MODE);

	switch (sec_policy) {
	case SDE_DRM_FB_SEC:
		sec_policy = (u32)HFI_LAYER_SECURITY_POLICY_SECURE;
		break;
	default:
		sec_policy = (u32)HFI_LAYER_SECURITY_POLICY_NON_SECURE;
	}
	sec_payload[0] = wb_id;
	sec_payload[1] = sec_policy;
	hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id,
		HFI_VAL_U32_ARRAY, sec_payload, sizeof(sec_payload));

	SDE_DEBUG("Done adding hfi props for wb\n");

	return ret;
}

static int _hfi_wb_set_props_base(struct sde_wb_device *wb_dev, u32 disp_id,
		struct sde_connector_state *cstate, struct hfi_cmdbuf_t *cmd_buf)
{
	int ret = 0;
	int flags = 0;
	struct sde_connector *sde_conn;
	struct hfi_connector *hfi_conn;
	enum wb_opmode opmode = WB_DPU;

	if (!wb_dev || !wb_dev->connector || !cmd_buf) {
		SDE_ERROR("invalid wb device\n");
		return -EINVAL;
	}

	sde_conn = to_sde_connector(wb_dev->connector);
	hfi_conn = sde_conn->hfi_conn;

	if (!hfi_conn) {
		SDE_ERROR("failed to get hfi connector\n");
		return -EINVAL;
	}

	mutex_lock(&hfi_conn->hfi_lock);
	hfi_util_u32_prop_helper_reset(hfi_conn->base_props);

	if (wb_dev->wb_cfg)
		opmode = wb_dev->wb_cfg->opmode;

	if (opmode == WB_DPU) {
		ret = _hfi_wb_add_drm_props(wb_dev, hfi_conn,
				cstate, hfi_conn->base_props, disp_id);
		if (ret) {
			SDE_ERROR("failed to add drm-prop HFI prop\n");
			goto end;
		}
	} else if (opmode == WB_CSC || opmode == WB_REPRO) {
		ret = hfi_wb_lsr_add_props(wb_dev, hfi_conn, cstate, disp_id, cmd_buf);
		if (ret) {
			SDE_ERROR("Failed to add LSR HFI WB prop\n");
			goto end;
		}
	}

	if (opmode == WB_CSC) {
		ret = hfi_set_hw_fence_prop(sde_conn->retire_fence, HFI_FENCE_SCAN_START,
				hfi_conn->base_props, disp_id, HFI_PROPERTY_DISPLAY_OUTPUT_FENCE);
		if (ret)
			SDE_ERROR("Failed to set hw-fence prop for retire fence ret:%d\n", ret);
	}

	if (!hfi_util_u32_prop_helper_prop_count(hfi_conn->base_props))
		goto end;

	ret = hfi_adapter_add_set_property(cmd_buf->ctx,
		cmd_buf,
		HFI_COMMAND_DISPLAY_SET_PROPERTY,
		disp_id,
		HFI_PAYLOAD_TYPE_U32_ARRAY,
		hfi_util_u32_prop_helper_get_payload_addr(hfi_conn->base_props),
		hfi_util_u32_prop_helper_get_size(hfi_conn->base_props),
		flags);
	if (ret) {
		SDE_ERROR("failed to send HFI commands\n");
		goto end;
	}

end:
	mutex_unlock(&hfi_conn->hfi_lock);

	return ret;
}

static int _hfi_wb_populate_props(struct hfi_cmdbuf_t *cmd_buf, u32 disp_id,
		struct sde_wb_device *wb_dev, struct sde_connector_state *cstate)
{
	return _hfi_wb_set_props_base(wb_dev, disp_id, cstate, cmd_buf);
}

static int hfi_wb_add_hfi_cmds(struct hfi_cmdbuf_t *cmd_buf, u32 disp_id,
		struct sde_wb_device *wb_dev, struct sde_connector_state *cstate)
{
	return _hfi_wb_populate_props(cmd_buf, disp_id, wb_dev, cstate);
}

int hfi_wb_display_prepare_commit(struct sde_wb_device *wb_dev,
		struct sde_connector_state *cstate)
{
	int ret = 0;
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	struct drm_connector *drm_conn;
	u32 disp_id;

	if (!wb_dev) {
		SDE_ERROR("invalid connector\n");
		return -EINVAL;
	}

	drm_conn = wb_dev->connector;
	disp_id = sde_conn_get_display_obj_id(drm_conn);

	cmd_buf = hfi_connector_get_cmd_buf(drm_conn, HFI_CMDBUF_TYPE_ATOMIC_COMMIT);
	if (!cmd_buf) {
		SDE_ERROR("failed to get cmd-buf in commit path\n");
		return -EINVAL;
	}

	ret = hfi_wb_add_hfi_cmds(cmd_buf, disp_id, wb_dev, cstate);
	if (ret) {
		SDE_ERROR("failed to add hfi cmds\n");
		return ret;
	}

	return ret;
}
