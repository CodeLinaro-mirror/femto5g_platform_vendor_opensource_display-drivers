// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hfi_kms.h"
#include "hfi_wb.h"
#include "hfi_defs_lsr.h"
#include "hfi_connector.h"
#include "hfi_kms.h"
#include "hfi_encoder.h"
#include "hfi_commands_debug.h"
#include "hfi_defs_debug.h"
#include "hfi_msm_drv.h"

#define BLOB_PROPERTY_HEADER_SIZE 2
#define MATRICES_PER_VIEW 2

static u64 lsr_fw_debug_val;

int hfi_lsr_fw_debug_set(u64 val, struct drm_device *dev)
{
	struct hfi_cmdbuf_t *cmd_buf;
	struct hfi_debug_log_level_info payload;
	int ret = 0;
	struct hfi_client_t *lsr_hfi_client;
	struct msm_drm_private *priv;
	struct msm_kms *kms;
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;

	if (!dev) {
		SDE_ERROR("Invalid drm device");
		return -EINVAL;
	}

	lsr_fw_debug_val = val;
	priv = dev->dev_private;
	if (!priv) {
		SDE_ERROR("Invalid msm_drm priv");
		return -EINVAL;
	}

	kms = priv->kms;
	if (!kms) {
		SDE_ERROR("Invalid msm_kms");
		return -EINVAL;
	}

	sde_kms = to_sde_kms(kms);
	hfi_kms = to_hfi_kms(sde_kms);
	lsr_hfi_client = &hfi_kms->hfi_client;
	if (!lsr_hfi_client) {
		SDE_ERROR("Invalid HFI client\n");
		return -EINVAL;
	}

	cmd_buf = hfi_adapter_get_cmd_buf(lsr_hfi_client,
		MSM_DRV_HFI_ID, HFI_CMDBUF_TYPE_GET_DEBUG_DATA);
	if (!cmd_buf) {
		SDE_ERROR("failed to get hfi command buffer\n");
		return -EINVAL;
	}

	payload.feature = HFI_DEBUG_FEATURE_LSR;
	payload.level_bitmask = val;

	ret = hfi_adapter_add_set_property(lsr_hfi_client,
			cmd_buf,
			HFI_COMMAND_DEBUG_SET_LOG_LEVEL,
			MSM_DRV_HFI_ID,
			HFI_PAYLOAD_TYPE_U32_ARRAY,
			&payload,
			sizeof(payload),
			HFI_HOST_FLAGS_RESPONSE_REQUIRED);

	if (ret) {
		SDE_ERROR("failed to add debug command\n");
		return ret;
	}

	ret = hfi_adapter_set_cmd_buf(lsr_hfi_client, cmd_buf);
	if (ret)
		SDE_ERROR("failed to send debug command\n");

	SDE_DEBUG("LSR FW debug level is set to 0x%llx\n", val);
	SDE_EVT32(val);
	return ret;
}

int hfi_lsr_fw_debug_get(u64 *val)
{
	*val = lsr_fw_debug_val;
	SDE_DEBUG("LSR FW debug level : %llu\n", *val);
	return 0;
}

struct base_prop_lookup {
	u32 drm_prop;
	u32 hfi_prop;
};

struct base_prop_lookup hfi_wb_repro_lsr_custom_props_map[] = {
	{ CONNECTOR_PROP_REPROJ_FUNCTIONAL_MODE,
			LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_FUNCTIONAL_MODE },
	{ CONNECTOR_PROP_REPROJ_DISTORT_RESOLUTION,
			LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_DISTORT_RESOLUTION },
	{ CONNECTOR_PROP_REPROJ_GRID_WIDTH, LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID_WIDTH },
	{ CONNECTOR_PROP_REPROJ_GRID_HEIGHT, LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID_HEIGHT },
	{ CONNECTOR_PROP_LSR_WB_REPROJ_POSE_FB, HFI_PROPERTY_DISPLAY_HRP_HFI_CONFIG },
	{ CONNECTOR_PROP_REPROJ_R_MAX, LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_R_MAX },
	{ CONNECTOR_PROP_REPROJ_TO_LRGB_LEFT, LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TO_LRGB },
	{ CONNECTOR_PROP_REPROJ_ERROR_TO_L, LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_ERROR_TO_L },
	{ CONNECTOR_PROP_REPROJ_DISP_IM_W, LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_IMAGE_WIDTH },
	{ CONNECTOR_PROP_REPROJ_TILE_W, LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TILE_WIDTH },
	{ CONNECTOR_PROP_REPROJ_MIN_BBOX_W, LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_MIN_BBOX_WIDTH },
	{ CONNECTOR_PROP_REPROJ_DISP_IM_H, LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_IMAGE_HEIGHT },
	{ CONNECTOR_PROP_REPROJ_TILE_H, LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TILE_HEIGHT },
	{ CONNECTOR_PROP_REPROJ_MIN_BBOX_H, LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_MIN_BBOX_HEIGHT },
	{ CONNECTOR_PROP_LSR_WB_REPROJ_SYNC_TO, HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_SYNC_TO },
	{ CONNECTOR_PROP_LSR_WB_REPROJ_CONFIG_MATRIX,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_MATRIX },
	{CONNECTOR_PROP_REPROJ_OPTICAL_AXIS_OFFSET,
			LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_OPTICAL_AXIS_OFFSET },
};

struct base_prop_lookup hfi_wb_repro_lsr_blob_props_map[] = {
	{ CONNECTOR_PROP_REPROJ_SPARSE_GRID, LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID },
	{ CONNECTOR_PROP_REPROJ_RADIAL_DISTORTION_GRID,
				LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_RADIAL_DISTORTION_GRID },
	{ CONNECTOR_PROP_REPROJ_DISPLAY_GAMMA, LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_DISPLAY_GAMMA },
	{ CONNECTOR_PROP_REPROJ_GCX_SESSION_CONFIG,
				LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_GCX_SESSION_CONFIG },
	{ CONNECTOR_PROP_REPROJ_GCX_SESSION_CONFIG_DATA,
				LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_GCX_SESSION_CONFIG_DATA },
};

static int _hfi_wb_lsr_add_fb_id_list_prop(struct sde_wb_device *wb_dev,
	struct sde_connector_state *cstate,
	struct hfi_util_u32_prop_helper *prop_collector,
	u32 disp_id)
{
	struct sde_view_descriptor *view_desc = NULL;
	struct sde_view_descriptor *back_view_desc = NULL;
	struct hfi_wb_out_buff *out_buffers;
	void *payload;
	int ret = 0, i;
	bool is_back_view_en = false;
	u32 prop_id, num_fbs = 0;

	if (!wb_dev || !cstate || !prop_collector) {
		SDE_ERROR("Invalid params\n");
		return -EINVAL;
	}

	view_desc = cstate->view_descriptor;
	back_view_desc = cstate->back_view_descriptor;

	for (i = 0; i < MAX_VIEWS; i++) {
		if (view_desc[i].num_fbs > 0 && view_desc[i].num_fbs <= MAX_BUFFERS_PER_VIEW)
			num_fbs += view_desc[i].num_fbs;
		if (back_view_desc[i].num_fbs > 0 &&
				back_view_desc[i].num_fbs <= MAX_BUFFERS_PER_VIEW) {
			num_fbs += back_view_desc[i].num_fbs;
			is_back_view_en = true;
		}
	}
	out_buffers = kcalloc(num_fbs, sizeof(struct hfi_wb_out_buff), GFP_KERNEL);
	if (!out_buffers) {
		SDE_ERROR("failed to allocate memory for out_buffers\n");
		return -ENOMEM;
	}

	sde_wb_lsr_get_fb_id_list(wb_dev, out_buffers, view_desc,
			back_view_desc, is_back_view_en);

	payload = kmalloc(sizeof(u32) + num_fbs * sizeof(struct hfi_wb_out_buff), GFP_KERNEL);
	if (!payload) {
		SDE_ERROR("failed to allocate memory for payload\n");
		kfree(out_buffers);
		return -ENOMEM;
	}
	*(u32 *)payload = num_fbs;
	memcpy((char *)payload + sizeof(u32), out_buffers,
			num_fbs * sizeof(struct hfi_wb_out_buff));

	prop_id = HFI_PROPERTY_DISPLAY_LSR_WB_OUT_BUFFERS;

	ret = hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id,
		HFI_VAL_U32_ARRAY, payload, sizeof(u32) + num_fbs * sizeof(struct hfi_wb_out_buff));

	kfree(out_buffers);
	kfree(payload);
	return ret;
}

int _hfi_wb_lsr_out_buffer_prop_helper(struct sde_wb_device *wb_dev,
	struct sde_connector_state *cstate,
	struct hfi_util_u32_prop_helper *prop_collector,
	u32 disp_id)
{
	int ret = 0;

	if (!wb_dev || !cstate || !prop_collector)
		return -EINVAL;

	ret = _hfi_wb_lsr_add_fb_id_list_prop(wb_dev, cstate, prop_collector, disp_id);

	return ret;
}

static int _hfi_wb_lsr_repro_set_hrp(struct sde_connector_state *cstate,
	struct hfi_util_u32_prop_helper *prop_collector)
{
	struct hfi_buff *pose_buff = NULL;
	u32 payload_size = sizeof(struct hfi_buff);
	u32 ret = 0;

	if (cstate->reproj_pose_size == 0)
		return 0;

	pose_buff = kzalloc(payload_size, GFP_KERNEL);
	if (!pose_buff)
		return -ENOMEM;

	pose_buff->addr_l = cstate->reproj_pose_iova;
	pose_buff->size = cstate->reproj_pose_size;
	ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
		HFI_PROPERTY_DISPLAY_HRP_HFI_CONFIG, HFI_VAL_U32_ARRAY, pose_buff, payload_size);

	if (!ret)
		SDE_DEBUG("HRP buffer set with iova = 0x%x\n", pose_buff->addr_l);
	else
		SDE_ERROR("Failed to set HRP HFI config property");

	kfree(pose_buff);
	return ret;
}

int _hfi_wb_lsr_repro_custom_prop_helper(u32 hfi_prop, struct sde_wb_device *wb_dev,
	struct sde_connector_state *cstate,
	struct hfi_util_u32_prop_helper *prop_collector,
	u32 disp_id)
{
	struct sde_drm_reproj_matrix_list *reproj_matrix_list;
	struct sde_drm_lsr_point *optical_axis_offset;
	enum sde_drm_wb_functional_mode func_mode;
	u32 payload_size = 0, payload_lrgb[4], payload[3], drm_conn_id;
	int ret = 0, i, val = 0, view_index;

	if (!wb_dev || !cstate || !prop_collector)
		return -EINVAL;

	reproj_matrix_list = &cstate->repro_conn_cfg.reproj_matrix_list;
	optical_axis_offset = &cstate->optical_axis_offset;

	payload[0] = hfi_prop;
	payload[1] = 1;
	switch (hfi_prop) {
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_FUNCTIONAL_MODE:
		func_mode = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_REPROJ_FUNCTIONAL_MODE);
		payload[2] = func_mode;
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
				HFI_VAL_U32_ARRAY, payload, sizeof(payload));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_DISTORT_RESOLUTION:
		payload[2] = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_REPROJ_DISTORT_RESOLUTION);
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
				HFI_VAL_U32_ARRAY, payload, sizeof(payload));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID_WIDTH:
		payload[2] = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_REPROJ_GRID_WIDTH);
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
				HFI_VAL_U32_ARRAY, payload, sizeof(payload));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID_HEIGHT:
		payload[2] = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_REPROJ_GRID_HEIGHT);
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
				HFI_VAL_U32_ARRAY, payload, sizeof(payload));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_R_MAX:
		payload[2] = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_REPROJ_R_MAX);
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
				HFI_VAL_U32_ARRAY, payload, sizeof(payload));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TO_LRGB:
		payload_lrgb[0] = hfi_prop;
		payload_lrgb[1] = 2;
		payload_lrgb[2] = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_REPROJ_TO_LRGB_LEFT);
		payload_lrgb[3] = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_REPROJ_TO_LRGB_RIGHT);
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
			HFI_VAL_U32_ARRAY, payload_lrgb, sizeof(payload_lrgb));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_ERROR_TO_L:
		payload[2] = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_REPROJ_ERROR_TO_L);
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
			HFI_VAL_U32_ARRAY, payload, sizeof(payload));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_IMAGE_WIDTH:
		payload[2] = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_REPROJ_DISP_IM_W);
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
			HFI_VAL_U32_ARRAY, payload, sizeof(payload));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TILE_WIDTH:
		payload[2] = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_REPROJ_TILE_W);
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
			HFI_VAL_U32_ARRAY, payload, sizeof(payload));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_MIN_BBOX_WIDTH:
		payload[2] = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_REPROJ_MIN_BBOX_W);
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
			HFI_VAL_U32_ARRAY, payload, sizeof(payload));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_IMAGE_HEIGHT:
		payload[2] = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_REPROJ_DISP_IM_H);
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
			HFI_VAL_U32_ARRAY, payload, sizeof(payload));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_TILE_HEIGHT:
		payload[2] = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_REPROJ_TILE_H);
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
			HFI_VAL_U32_ARRAY, payload, sizeof(payload));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_MIN_BBOX_HEIGHT:
		payload[2] = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_REPROJ_MIN_BBOX_H);
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
			HFI_VAL_U32_ARRAY, payload, sizeof(payload));
		break;
	case HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_SYNC_TO:
		drm_conn_id = sde_connector_get_property(&cstate->base,
					CONNECTOR_PROP_LSR_WB_REPROJ_SYNC_TO);
		struct drm_connector_list_iter conn_iter;
		struct drm_connector *drm_conn;

		drm_connector_list_iter_begin(cstate->base.connector->dev, &conn_iter);
		drm_for_each_connector_iter(drm_conn, &conn_iter) {
			if (drm_conn->base.id == drm_conn_id) {
				val = sde_conn_get_display_obj_id(drm_conn);
				SDE_DEBUG("Reprojection is synced to display_id:%d\n", val);
				break;
			}
		}
		drm_connector_list_iter_end(&conn_iter);
		SDE_EVT32(val);
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			hfi_prop, HFI_VAL_U32, &val, sizeof(u32));
		break;
	case HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_MATRIX:
		if (!reproj_matrix_list) {
			SDE_ERROR("Invalid reproj_matrix_list\n");
			return -EINVAL;
		}

		for (view_index = 0; view_index < MAX_VIEWS; view_index++)	{
			payload_size = sizeof(u32) +
					MATRICES_PER_VIEW * sizeof(struct hfi_lsr_reproj_matrix);
			void *matrix_payload = kzalloc(payload_size, GFP_KERNEL);

			if (!matrix_payload) {
				SDE_ERROR("failed to allocate memory for matrix_payload\n");
				return -ENOMEM;
			}
			u32 *view_index_ptr = (u32 *) matrix_payload;
			*view_index_ptr = view_index;
			u32 *matrix_base = (u32 *)((char *)matrix_payload + sizeof(u32));
			bool is_inverse = false;

			for (i = 0; i < (MAX_VIEWS * MATRICES_PER_VIEW); i++) {
				if ((reproj_matrix_list->matrix_list[i].view_index != view_index) ||
					(reproj_matrix_list->matrix_list[i].is_inverse !=
						is_inverse)) {
					continue;
				}
				memcpy(matrix_base,
					&reproj_matrix_list->matrix_list[i].reproj_matrix,
					sizeof(struct hfi_lsr_reproj_matrix));
			}

			matrix_base = (u32 *)((char *)matrix_payload + sizeof(u32) +
					sizeof(struct hfi_lsr_reproj_matrix));
			is_inverse = true;
			for (i = 0; i < (MAX_VIEWS * MATRICES_PER_VIEW); i++) {
				if ((reproj_matrix_list->matrix_list[i].view_index != view_index) ||
					(reproj_matrix_list->matrix_list[i].is_inverse !=
						is_inverse)) {
					continue;
				}
				memcpy(matrix_base,
					&reproj_matrix_list->matrix_list[i].reproj_matrix,
					sizeof(struct hfi_lsr_reproj_matrix));
				break;
			}
			ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
				hfi_prop, HFI_VAL_U32_ARRAY, matrix_payload, payload_size);

			kfree(matrix_payload);
		}
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_OPTICAL_AXIS_OFFSET:
			if (!optical_axis_offset) {
				SDE_ERROR("Invalid optical_axis_offset\n");
				return -EINVAL;
			}

			payload_size = (MAX_VIEWS * sizeof(u32)) + sizeof(struct sde_drm_lsr_point);
			void *optical_axis_payload = kzalloc(payload_size, GFP_KERNEL);

			if (!optical_axis_payload) {
				SDE_ERROR("failed to allocate memory for optical axis payload\n");
				return -ENOMEM;
			}
			u32 *oa_base = (u32 *) optical_axis_payload;
			*oa_base = hfi_prop;
			oa_base = (u32 *)((char *)optical_axis_payload + sizeof(u32));
			*oa_base = sizeof(struct sde_drm_lsr_point)/sizeof(u32);
			oa_base = (u32 *)((char *)optical_axis_payload + (MAX_VIEWS * sizeof(u32)));
			memcpy(oa_base, optical_axis_offset, sizeof(struct sde_drm_lsr_point));
			ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
				HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
				HFI_VAL_U32_ARRAY, optical_axis_payload, payload_size);

			kfree(optical_axis_payload);
		break;
	case HFI_PROPERTY_DISPLAY_HRP_HFI_CONFIG:
			ret = _hfi_wb_lsr_repro_set_hrp(cstate, prop_collector);
		break;
	default:
		SDE_ERROR("Failed to send HFI commands\n");
		return -EINVAL;
	}

	if (ret)
		SDE_ERROR("Failed adding HFI prop:0x%x\n", hfi_prop);

	SDE_DEBUG("Done adding HFI prop:0x%x\n", hfi_prop);

	return ret;
}

int _hfi_wb_lsr_repro_blob_prop_helper(u32 hfi_prop, struct sde_wb_device *wb_dev,
	struct sde_connector_state *cstate,
	struct hfi_util_u32_prop_helper *prop_collector,
	u32 disp_id)
{
	struct sde_drm_opaque_config *opq_cfg = NULL;
	int ret = 0;
	u32 size;
	u32 *payload = NULL;

	if (!wb_dev || !cstate || !prop_collector)
		return -EINVAL;

	size = BLOB_PROPERTY_HEADER_SIZE + (sizeof(struct hfi_buff) / sizeof(u32));
	payload = kcalloc(size, sizeof(u32), GFP_KERNEL);

	if (!payload) {
		SDE_ERROR("failed to allocate memory for payload\n");
		return -ENOMEM;
	}

	payload[0] = hfi_prop;
	payload[1] = sizeof(struct hfi_buff)/sizeof(u32);
	struct hfi_buff *buff = (struct hfi_buff *)&payload[2];

	switch (hfi_prop) {
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_SPARSE_GRID:
		opq_cfg = &cstate->reproj_sparse_grid;
		buff->size = opq_cfg->usr_cfg.size;
		buff->addr_l = opq_cfg->remote_iova;
		if (!buff->addr_l) {
			SDE_ERROR("Invalid buffer address for property:%x\n", hfi_prop);
			kfree(payload);
			return -EINVAL;
		}
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
			HFI_VAL_U32_ARRAY, payload, size * sizeof(u32));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_RADIAL_DISTORTION_GRID:
		opq_cfg = &cstate->reproj_radial_dis_grid;
		buff->size = opq_cfg->usr_cfg.size;
		buff->addr_l = opq_cfg->remote_iova;
		if (!buff->addr_l) {
			SDE_ERROR("Invalid buffer address for property:%x\n", hfi_prop);
			kfree(payload);
			return -EINVAL;
		}
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
			HFI_VAL_U32_ARRAY, payload, size * sizeof(u32));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_DISPLAY_GAMMA:
		opq_cfg = &cstate->reproj_display_gamma;
		buff->size = opq_cfg->usr_cfg.size;
		buff->addr_l = opq_cfg->remote_iova;
		if (!buff->addr_l) {
			SDE_ERROR("Invalid buffer address for property:%x\n", hfi_prop);
			kfree(payload);
			return -EINVAL;
		}
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
			HFI_VAL_U32_ARRAY, payload, size * sizeof(u32));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_GCX_SESSION_CONFIG:
		opq_cfg = &cstate->reproj_gcx_session_config;
		buff->size = opq_cfg->usr_cfg.size;
		buff->addr_l = opq_cfg->remote_iova;
		if (!buff->addr_l) {
			SDE_ERROR("Invalid buffer address for property:%x\n", hfi_prop);
			kfree(payload);
			return -EINVAL;
		}
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
			HFI_VAL_U32_ARRAY, payload, size * sizeof(u32));
		break;
	case LSR_REPROJ_DISPLAY_CONFIG_EXT_KEY_GCX_SESSION_CONFIG_DATA:
		opq_cfg = &cstate->reproj_gcx_session_config_data;
		buff->size = opq_cfg->usr_cfg.size;
		buff->addr_l = opq_cfg->remote_iova;
		if (!buff->addr_l) {
			SDE_ERROR("Invalid buffer address for property:%x\n", hfi_prop);
			kfree(payload);
			return -EINVAL;
		}
		ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT,
			HFI_VAL_U32_ARRAY, payload, size * sizeof(u32));
		break;
	default:
		SDE_ERROR("Failed to send HFI commands\n");
		return -EINVAL;
	}

	if (ret)
		SDE_ERROR("Failed adding HFI prop:0x%x\n", hfi_prop);

	SDE_DEBUG("Done adding HFI prop:0x%x\n", hfi_prop);
	kfree(payload);

	return 0;
}

int hfi_wb_lsr_add_props(struct sde_wb_device *wb_dev, struct hfi_connector *hfi_conn,
		struct sde_connector_state *cstate,
		u32 disp_id, struct hfi_cmdbuf_t *cmd_buf)
{
	u32 drm_prop, hfi_prop;
	int i, ret = 0;
	int flags = 0;

	if (!wb_dev || !hfi_conn || !cstate || !cmd_buf) {
		SDE_ERROR("Invalid params\n");
		return -EINVAL;
	}

	hfi_util_u32_prop_helper_reset(hfi_conn->lsr_props);
	hfi_util_u32_prop_helper_reset(hfi_conn->lsr_blob_props);
	hfi_util_u32_prop_helper_reset(hfi_conn->lsr_out_buffer_props);

	ret = _hfi_wb_lsr_out_buffer_prop_helper(wb_dev, cstate,
			hfi_conn->lsr_out_buffer_props, disp_id);
	if (ret) {
		SDE_ERROR("Failed to set HFI out buffers :%d\n", ret);
		goto end;
	}

	if (wb_dev->wb_cfg->opmode == WB_REPRO) {
		for (i = 0; i < ARRAY_SIZE(hfi_wb_repro_lsr_custom_props_map); i++) {
			drm_prop = hfi_wb_repro_lsr_custom_props_map[i].drm_prop;
			hfi_prop = hfi_wb_repro_lsr_custom_props_map[i].hfi_prop;
			if (!cstate->gcx_session_dirty)
				continue;
			ret = _hfi_wb_lsr_repro_custom_prop_helper(hfi_prop, wb_dev, cstate,
				 hfi_conn->lsr_props, disp_id);
			if (ret) {
				SDE_ERROR("Failed to set HFI REPRO custom properties :%d\n", ret);
				goto end;
			}
		}

		for (i = 0; i < ARRAY_SIZE(hfi_wb_repro_lsr_blob_props_map); i++) {
			drm_prop = hfi_wb_repro_lsr_blob_props_map[i].drm_prop;
			hfi_prop = hfi_wb_repro_lsr_blob_props_map[i].hfi_prop;
			if (!cstate->gcx_session_dirty)
				continue;
			ret = _hfi_wb_lsr_repro_blob_prop_helper(hfi_prop, wb_dev, cstate,
				 hfi_conn->lsr_blob_props, disp_id);
			if (ret) {
				SDE_ERROR("Failed to set HFI REPRO blob properties :%d\n", ret);
				goto end;
			}
		}
	}

	if (!hfi_util_u32_prop_helper_prop_count(hfi_conn->lsr_out_buffer_props))
		goto lsr_props;

	ret = hfi_adapter_add_set_property(cmd_buf->ctx,
		cmd_buf,
		HFI_COMMAND_DISPLAY_SET_PROPERTY,
		disp_id,
		HFI_PAYLOAD_TYPE_U32_ARRAY,
		hfi_util_u32_prop_helper_get_payload_addr(hfi_conn->lsr_out_buffer_props),
		hfi_util_u32_prop_helper_get_size(hfi_conn->lsr_out_buffer_props),
		flags);

	if (ret) {
		SDE_ERROR("failed to send HFI commands\n");
		goto end;
	}

lsr_props:
	if (!hfi_util_u32_prop_helper_prop_count(hfi_conn->lsr_props))
		goto lsr_blob_props;

	ret = hfi_adapter_add_set_property(cmd_buf->ctx,
		cmd_buf,
		HFI_COMMAND_DISPLAY_SET_PROPERTY,
		disp_id,
		HFI_PAYLOAD_TYPE_U32_ARRAY,
		hfi_util_u32_prop_helper_get_payload_addr(hfi_conn->lsr_props),
		hfi_util_u32_prop_helper_get_size(hfi_conn->lsr_props),
		flags);

	if (ret) {
		SDE_ERROR("failed to send HFI commands\n");
		goto end;
	}

lsr_blob_props:
	if (!hfi_util_u32_prop_helper_prop_count(hfi_conn->lsr_blob_props))
		goto end;

	ret = hfi_adapter_add_set_property(cmd_buf->ctx,
		cmd_buf,
		HFI_COMMAND_DISPLAY_SET_PROPERTY,
		disp_id,
		HFI_PAYLOAD_TYPE_U32_ARRAY,
		hfi_util_u32_prop_helper_get_payload_addr(hfi_conn->lsr_blob_props),
		hfi_util_u32_prop_helper_get_size(hfi_conn->lsr_blob_props),
		flags);

	if (ret) {
		SDE_ERROR("failed to send HFI commands\n");
		goto end;
	}

end:
	return ret;
}

int hfi_wb_lsr_prop_helper_alloc(struct hfi_connector *hfi_conn)
{
	int rc = 0;

	if (!hfi_conn) {
		SDE_ERROR("Invalid params\n");
		return -EINVAL;
	}

	hfi_conn->lsr_props =
			hfi_util_u32_prop_helper_alloc(HFI_CONNECTOR_BASE_PROP_MAX_SIZE);
	if (IS_ERR(hfi_conn->lsr_props)) {
		SDE_ERROR("failed to allocate memory for lsr prop collection\n");
		goto free_lsr;
	}

	hfi_conn->lsr_blob_props =
			hfi_util_u32_prop_helper_alloc(HFI_CONNECTOR_BASE_PROP_MAX_SIZE);
	if (IS_ERR(hfi_conn->lsr_blob_props)) {
		SDE_ERROR("failed to allocate memory for lsr blob prop collection\n");
		goto free_lsr_blob;
	}

	hfi_conn->lsr_out_buffer_props =
			hfi_util_u32_prop_helper_alloc(HFI_CONNECTOR_BASE_PROP_MAX_SIZE);
	if (IS_ERR(hfi_conn->lsr_out_buffer_props)) {
		SDE_ERROR("failed to allocate memory for lsr out buffer prop collection\n");
		goto free_lsr_out_buffer;
	}

	return rc;

free_lsr_out_buffer:
	kfree(hfi_conn->lsr_out_buffer_props);
free_lsr_blob:
	kfree(hfi_conn->lsr_blob_props);
free_lsr:
	kfree(hfi_conn->lsr_props);

	return -ENOMEM;
}

int hfi_wb_add_lsr_init_props(struct sde_wb_device *wb_dev, struct drm_connector *drm_conn,
			struct hfi_util_u32_prop_helper *prop_collector)
{
	int ret = 0;
	struct sde_connector *sde_conn;
	struct sde_reproj *reproj_conn = NULL;
	struct hfi_buff lsr_hfi_config, arp_buf;
	struct hfi_buff scratch_mem[2];

	sde_conn = to_sde_connector(drm_conn);
	if (sde_conn)
		reproj_conn = sde_conn->reproj_conn;

	if (!reproj_conn) {
		SDE_ERROR("Invalid reprojection connector");
		return -EINVAL;
	}

	lsr_hfi_config.addr_l = reproj_conn->queue_table_dcp_addr;
	lsr_hfi_config.size = reproj_conn->queue_table_size;
	arp_buf.addr_l = reproj_conn->arp_buf_lsr_addr;
	arp_buf.size = reproj_conn->arp_buf_size;

	if (reproj_conn->type == WB_CSC) {
		scratch_mem[0].addr_l = reproj_conn->csc_scratch_dcp_addr;
		scratch_mem[0].size = reproj_conn->csc_scratch_size;
		scratch_mem[1].addr_l = reproj_conn->csc_scratch_lsr_addr;
		scratch_mem[1].size = reproj_conn->csc_scratch_size;
	} else if (reproj_conn->type == WB_REPRO) {
		scratch_mem[0].addr_l = reproj_conn->gcx_scratch_dcp_addr;
		scratch_mem[0].size = reproj_conn->gcx_scratch_size;
		scratch_mem[1].addr_l = reproj_conn->gcx_scratch_lsr_addr;
		scratch_mem[1].size = reproj_conn->gcx_scratch_size;
	}

	ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
		HFI_PROPERTY_DISPLAY_LSR_WB_HFI_CONFIG, HFI_VAL_U32_ARRAY,
		&lsr_hfi_config, sizeof(struct hfi_buff));
	if (ret)
		goto end;

	ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_SET_SCRATCH_MEM, HFI_VAL_U32_ARRAY,
			&scratch_mem, sizeof(struct hfi_buff)*2);
	if (ret)
		goto end;

	ret = hfi_util_u32_prop_helper_add_prop(prop_collector,
			HFI_PROPERTY_DISPLAY_LSR_WB_CVP_BUFF,
			HFI_VAL_U32_ARRAY, &arp_buf, sizeof(struct hfi_buff));
	if (ret)
		goto end;

	return ret;
end:
	SDE_ERROR("Failed setting enable lsr properties on HFI with ret = %d", ret);
	return ret;
}

#if IS_ENABLED(CONFIG_QTI_HW_FENCE)
static int _hfi_wb_setup_reusable_fence(struct drm_connector *drm_conn,
		struct sde_reproj *reproj_conn, u32 *new_h_synx,
		struct hfi_util_u32_prop_helper *base_props)
{
	struct synx_import_params import_params = {0};
	struct synx_session *session = NULL;
	u32 lsr_h_synx, ret = 0;
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;
	struct hfi_hwfence_data *hfi_hw_fence_data;
	int reusable_fence_count = 0;
	u32 *payload;
	u32 size = 0;

	sde_kms = sde_connector_get_kms(drm_conn);
	hfi_kms = sde_kms ? sde_kms->hfi_kms : NULL;
	hfi_hw_fence_data = hfi_kms ? hfi_kms->hfi_hw_fence_data : NULL;

	if (!hfi_hw_fence_data || !hfi_hw_fence_data->hw_fence_handle) {
		SDE_ERROR("invalid sde_kms:%pK hfi_kms:%pK hwfence_data:%pK hw_fence_handle:%pK\n",
			sde_kms, hfi_kms, hfi_hw_fence_data,
			hfi_hw_fence_data ? hfi_hw_fence_data->hw_fence_handle : NULL);
		return -EINVAL;
	}

	/* Setup reusable fence only once per device bootup */
	if (!reproj_conn->reusable_fence_cnt) {
		session = hfi_hw_fence_data->hw_fence_handle;
		lsr_h_synx = reproj_conn->lsr_reusable_hsynx;
		SDE_DEBUG("lsr reusable h_synx: 0x%x\n", lsr_h_synx);

		/* Setup individual params for reusable fence */
		import_params.indv.new_h_synx = new_h_synx;
		import_params.indv.flags = SYNX_IMPORT_REUSABLE | SYNX_IMPORT_SYNX_FENCE;
		import_params.indv.fence = (void *)&lsr_h_synx;
		import_params.type = SYNX_IMPORT_INDV_PARAMS;

		ret = synx_import(session, &import_params);
		if (ret) {
			SDE_ERROR("dcp failed to import lsr reusable hw fence: %d\n", ret);
			return ret;
		}

		SDE_DEBUG("dcp imported lsr reusable hw fence: %d\n", *new_h_synx);
		reproj_conn->reusable_fence_cnt++;
	}

	reusable_fence_count = reproj_conn->reusable_fence_cnt;
	if (reusable_fence_count) {
		u64 val = reproj_conn->lsr_reusable_hsynx;

		size = 2 * sizeof(u32) + reusable_fence_count * 2 * sizeof(u32);
		payload = kzalloc(size, GFP_KERNEL);
		payload[0] = HFI_LSR_REUSABLE_FENCE_GCX_OUT_BUFFERS;
		payload[1] = reusable_fence_count;
		payload[2] = HFI_VAL_L32(val);
		payload[3] =  HFI_VAL_H32(val);
		hfi_util_u32_prop_helper_add_prop(base_props,
				HFI_PROPERTY_DISPLAY_REUSABLE_FENCE,
				HFI_VAL_U32_ARRAY, payload, size);

		SDE_DEBUG("lsr reusable hw fence: 0x%llx\n", val);
		kfree(payload);
	}

	return ret;
}
#else
static int _hfi_wb_setup_reusable_fence(struct drm_connector *drm_conn,
		struct sde_reproj *reproj_conn, u32 *new_h_synx,
		struct hfi_util_u32_prop_helper *base_props)
{
	return -EINVAL;
}
#endif

int hfi_wb_display_lsr_enable(struct drm_connector *drm_conn, bool enable)
{
	int ret = 0;
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	struct sde_connector *sde_conn;
	struct sde_wb_device *wb_dev;
	struct hfi_connector *hfi_conn = NULL;
	struct sde_reproj *reproj_conn = NULL;
	struct drm_encoder *drm_enc;
	struct sde_encoder_virt *sde_enc = NULL;
	u32 disp_id;
	int flags = 0;

	sde_conn = to_sde_connector(drm_conn);
	wb_dev = sde_conn->display;
	disp_id = sde_conn_get_display_obj_id(drm_conn);
	reproj_conn = sde_conn->reproj_conn;

	/* Get the encoder connected to this connector */
	list_for_each_entry(drm_enc, &drm_conn->dev->mode_config.encoder_list, head) {
		if (drm_enc->crtc) {
			struct drm_connector *conn;

			conn = sde_encoder_get_connector(drm_conn->dev, drm_enc);
			if (conn == drm_conn) {
				sde_enc = to_sde_encoder_virt(drm_enc);
				break;
			}
		}
	}

	if (!reproj_conn) {
		SDE_ERROR("Invalid reroj connector");
		return -EINVAL;
	}

	if (enable) {
		u32 new_h_synx = 0;

		ret = reproj_conn->get_info(reproj_conn, wb_dev->wb_cfg->opmode);
		if (ret) {
			SDE_ERROR("Failed to get reproj info\n");
			return ret;
		}

		if (reproj_conn->on)
			reproj_conn->on(reproj_conn);

		cmd_buf = hfi_connector_get_cmd_buf(drm_conn, HFI_CMDBUF_TYPE_ATOMIC_COMMIT);
		hfi_conn = sde_conn->hfi_conn;
		mutex_lock(&hfi_conn->hfi_lock);
		hfi_util_u32_prop_helper_reset(hfi_conn->base_props);

		ret = hfi_wb_add_lsr_init_props(wb_dev, drm_conn, hfi_conn->base_props);
		if (ret) {
			SDE_ERROR("failed to add drm-prop HFI prop\n");
			goto end;
		}

		if (wb_dev->wb_cfg->opmode == WB_REPRO) {
			ret = _hfi_wb_setup_reusable_fence(drm_conn, reproj_conn,
					&new_h_synx, hfi_conn->base_props);

			if (ret)
				SDE_ERROR("Failed to setup reusable fence\n");
		}

		if (!hfi_util_u32_prop_helper_prop_count(hfi_conn->base_props))
			goto end;

		ret = hfi_adapter_add_set_property(cmd_buf->ctx,
			cmd_buf, HFI_COMMAND_DISPLAY_SET_PROPERTY, disp_id,
			HFI_PAYLOAD_TYPE_U32_ARRAY,
			hfi_util_u32_prop_helper_get_payload_addr(hfi_conn->base_props),
			hfi_util_u32_prop_helper_get_size(hfi_conn->base_props), flags);
		if (ret)
			SDE_ERROR("failed to send HFI commands\n");

#if IS_ENABLED(CONFIG_QTI_HW_FENCE)
		/* LSR usecases always have hw-fences enabled */
		ret =  synx_enable_resources(SYNX_CLIENT_HW_FENCE_LSR0_CTX0, SYNX_RESOURCE_SOCCP,
				true);
		if (ret) {
			SDE_ERROR("failed to enable hw-fence resources for lsr: %d\n", ret);
			return ret;
		}
#endif

end:
		mutex_unlock(&hfi_conn->hfi_lock);
	} else {
		if (reproj_conn->off)
			reproj_conn->off(reproj_conn, true);

#if IS_ENABLED(CONFIG_QTI_HW_FENCE)
		ret =  synx_enable_resources(SYNX_CLIENT_HW_FENCE_LSR0_CTX0, SYNX_RESOURCE_SOCCP,
				false);
		if (ret) {
			SDE_ERROR("failed to disable hw-fence resources for lsr: %d\n", ret);
			return ret;
		}
#endif
	}
	return ret;
}

int hfi_conn_send_lsr_display_ctrl_cmd(struct hfi_kms *hfi_kms, struct hfi_connector *hfi_conn,
		struct hfi_cmdbuf_t *cmd_buf, u32 *flags, bool enable)
{
	u32 display_id;
	int ret = 0;
	struct sde_connector *sde_conn = NULL;
	struct drm_connector *conn = NULL;

	sde_conn = hfi_conn->sde_base;
	if (!sde_conn)
		return -EINVAL;

	conn = &sde_conn->base;
	display_id = sde_conn_get_display_obj_id(conn);

	if (enable) {
		ret = hfi_wb_display_lsr_enable(conn, true);
	} else {
		*flags |= HFI_HOST_FLAGS_RESPONSE_REQUIRED;
		ret = hfi_adapter_add_get_property(&hfi_kms->hfi_client, cmd_buf,
				HFI_COMMAND_DISPLAY_DISABLE, display_id, HFI_PAYLOAD_TYPE_NONE,
				NULL, 0, &hfi_conn->disable_listener, *flags);
		if (ret) {
			SDE_ERROR("failed to register LSR-WB disable response listener\n");
			return ret;
		}
	}
	return 0;
}

void hfi_lsr_display_disable_handler(u32 obj_id, u32 cmd_id,
		void *payload, u32 size, struct hfi_prop_listener *listener)
{
	struct drm_connector *conn;
	struct hfi_connector *hfi_conn;
	struct sde_connector *sde_conn;
	int ret = 0;

	hfi_conn = container_of(listener, struct hfi_connector, disable_listener);
	if (!hfi_conn) {
		SDE_ERROR("Invalid HFI connector\n");
		return;
	}

	sde_conn = hfi_conn->sde_base;
	if (!sde_conn) {
		SDE_ERROR("Invalid SDE connector\n");
		return;
	}
	conn = &sde_conn->base;

	if (sde_conn->conn_id == obj_id)
		ret = hfi_wb_display_lsr_enable(conn, false);

	SDE_DEBUG("LSR disable response received for obj_id:%d with cmd_id:0x%x\n",
			obj_id, cmd_id);
}
