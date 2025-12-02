// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "sde_kms.h"
#include "sde_wb_lsr.h"

#define FLIP_VIEWS 2

void _sde_wb_lsr_destroy_fb_list(struct sde_connector *c_conn,
	struct sde_connector_state *c_state)
{
	if (!c_state) {
		SDE_ERROR("invalid state %pK\n", c_state);
		return;
	}

	for (int i = 0; i < MAX_VIEWS; i++) {
		for (int j = 0; j < c_state->view_descriptor[i].num_fbs; j++) {
			drm_framebuffer_put(c_state->view_descriptor[i].fb_id[j]);
			c_state->view_descriptor[i].fb_id[j] = NULL;
		}
	}

	if (c_conn)
		c_state->property_values[CONNECTOR_PROP_OUT_FB_LIST].value =
			msm_property_get_default(&c_conn->property_info,
				CONNECTOR_PROP_OUT_FB_LIST);
	else
		c_state->property_values[CONNECTOR_PROP_OUT_FB_LIST].value = ~0;
}

static struct sde_view_descriptor _sde_wb_lsr_get_view_descriptor(struct drm_connector *connector,
		struct drm_connector_state *state, struct sde_drm_view_descriptor *view_desc)
{
	struct sde_view_descriptor *desc = NULL;
	struct sde_connector *c_conn;
	struct sde_connector_state *c_state;
	const struct msm_format *format;
	int rc = 0;
	uint32_t add;

	c_conn = to_sde_connector(connector);
	c_state = to_sde_connector_state(state);

	if (!c_conn || !c_state || !view_desc) {
		SDE_ERROR("invalid args\n");
		rc = -EINVAL;
		goto exit;
	}

	desc = kzalloc(sizeof(struct sde_view_descriptor), GFP_KERNEL);

	desc->num_fbs = view_desc->num_fbs;
	for (int j = 0; j < view_desc->num_fbs; j++) {
		format = NULL;
		desc->fb_id[j] =
			drm_framebuffer_lookup(connector->dev, NULL, view_desc->fb_id[j]);
		if (!desc->fb_id[j] && view_desc->fb_id[j]) {
			SDE_ERROR("failed to look up fb %u\n", view_desc->fb_id[j]);
			rc = -EFAULT;
		} else if (!desc->fb_id[j] && !view_desc->fb_id[j]) {
			SDE_DEBUG("Invalid fb_id\n");
			continue;
		}
		struct sde_kms *sde_kms;
		struct msm_gem_address_space *aspace;

		sde_kms = sde_connector_get_kms(connector);
		/**
		 * TODO, assumed default as non secure for now,
		 * secure cases will be enabled with LSR secure cases.
		 */
		aspace = sde_kms->aspace[SDE_IOMMU_DOMAIN_UNSECURE];
		if (!aspace) {
			SDE_ERROR("invalid aspace\n");
			rc = -EINVAL;
		}

		rc = msm_framebuffer_prepare(desc->fb_id[j], aspace);
		if (rc) {
			SDE_ERROR("failed to prepare framebuffer %d\n", rc);
			rc = -EINVAL;
		}

		drm_framebuffer_get(desc->fb_id[j]);
		format = msm_framebuffer_format(desc->fb_id[j]);
		if (!format) {
			SDE_ERROR("invalid fb fmt\n");
			rc = -EINVAL;
		}

		if (aspace) {
			add = msm_framebuffer_iova(desc->fb_id[j], aspace, 0);
			if (!add) {
				DRM_ERROR("failed to retrieve base addr\n");
				rc = -EFAULT;
			}
		}
	}
exit:
	if (rc)
		desc = NULL;

	return *desc;
}

static void _sde_wb_lsr_set_reproj_matrix(struct sde_connector *c_conn,
	struct sde_connector_state *c_state, void __user *usr_ptr)
{
	struct sde_drm_reproj_matrix_list reproj_matrix_list = {0};

	if (!c_conn || !c_state) {
		SDE_ERROR("invalid argument(s)\n");
		return;
	}

	if (!usr_ptr) {
		SDE_ERROR("reproj_matrix isn't set\n");
		return;
	}

	if (copy_from_user(&reproj_matrix_list, usr_ptr, sizeof(reproj_matrix_list))) {
		SDE_ERROR("failed to copy reproj_matrix data\n");
		return;
	}

	memcpy(&c_state->repro_conn_cfg.reproj_matrix_list,
			&reproj_matrix_list, sizeof(reproj_matrix_list));
	SDE_DEBUG("reproj_matrix: set\n");
}

static int _sde_wb_lsr_set_prop_out_fb_list(struct drm_connector *connector,
	struct drm_connector_state *state, void __user *usr_ptr)
{
	struct sde_drm_fb_id_list fb_id_list;
	struct sde_connector *c_conn;
	struct sde_connector_state *c_state;
	int view_idx = 0, rc = 0, iter;
	bool is_back_view = false;

	c_conn = to_sde_connector(connector);
	c_state = to_sde_connector_state(state);

	if (!c_conn || !c_state) {
		SDE_ERROR("invalid args\n");
		return -EINVAL;
	}

	memset(&c_state->fb_id_list, 0, sizeof(c_state->fb_id_list));

	if (!usr_ptr) {
		SDE_DEBUG("fb_id_list cleared\n");
		return 0;
	}

	if (copy_from_user(&fb_id_list, usr_ptr, sizeof(fb_id_list))) {
		SDE_ERROR("failed to copy fb_id_list data\n");
		return -EINVAL;
	}

	memcpy(&c_state->fb_id_list, &fb_id_list, sizeof(fb_id_list));
	SDE_DEBUG("fb_id_list is set\n");

	for (iter = 0; iter < MAX_VIEWS; iter++) {
		if (fb_id_list.back_views[iter].num_fbs > 0 &&
				fb_id_list.back_views[iter].num_fbs < MAX_BUFFERS_PER_VIEW) {
			is_back_view = true;
		}
	}

	for (iter = 0; iter < MAX_VIEWS * FLIP_VIEWS; iter++) {
		view_idx = iter % MAX_VIEWS;
		if ((iter / MAX_VIEWS) && is_back_view)
			c_state->back_view_descriptor[view_idx] =
						_sde_wb_lsr_get_view_descriptor(connector, state,
								&fb_id_list.back_views[view_idx]);
		else if (iter < MAX_VIEWS)
			c_state->view_descriptor[view_idx] =
						_sde_wb_lsr_get_view_descriptor(connector, state,
								&fb_id_list.views[view_idx]);
	}
	return rc;
}

static void _sde_wb_lsr_set_optical_axis_offset(struct sde_connector *c_conn,
	struct sde_connector_state *c_state, void __user *usr_ptr)
{
	struct sde_drm_lsr_point optical_axis_offset = {0};

	if (!c_conn || !c_state) {
		SDE_ERROR("invalid argument(s)\n");
		return;
	}

	if (!usr_ptr) {
		SDE_ERROR("optical_axis_offset isn't set\n");
		return;
	}

	if (copy_from_user(&optical_axis_offset, usr_ptr, sizeof(optical_axis_offset))) {
		SDE_ERROR("failed to copy optical_axis_offset data\n");
		return;
	}

	memcpy(&c_state->optical_axis_offset, &optical_axis_offset, sizeof(optical_axis_offset));
	SDE_DEBUG("optical_axis_offset: set\n");
}

int _sde_wb_lsr_set_reproj_info(struct sde_connector *c_conn,
	struct sde_connector_state *c_state, int idx,
	struct sde_drm_opaque_config *opq_state_config)
{
	struct drm_msm_opaque_config *opq_blob;
	struct drm_device *dev = c_conn->base.dev;
	size_t sz = 0;
	int rc = 0;

	opq_blob = msm_property_get_blob(&c_conn->property_info,
				&c_state->property_state, &sz, idx);

	if (opq_blob == NULL) {
		SDE_DEBUG("opq_blob is NULL\n");
		return 0;
	}

	opq_state_config->usr_cfg.size = opq_blob->size;
	opq_state_config->usr_cfg.flags = opq_blob->flags;
	opq_state_config->usr_cfg.crc = opq_blob->crc;

	opq_state_config->buf =  msm_gem_new(dev, opq_blob->size, MSM_BO_UNCACHED);
	if (!opq_state_config->buf) {
		SDE_ERROR("Failed to allocate reproj buf memory\n");
		rc = -ENOMEM;
		goto free_gem;
	}
	/**
	 * TODO, assumed default as non secure for now,
	 * secure cases will be enabled with LSR secure cases.
	 */
	rc = msm_gem_get_iova(opq_state_config->buf, c_conn->aspace[SDE_IOMMU_DOMAIN_UNSECURE],
					&(opq_state_config->remote_iova));
	if (rc) {
		SDE_ERROR("failed to get iova rc %d\n", rc);
		goto put_iova;
	}

	opq_state_config->usr_cfg.data =
		(__u64) msm_gem_get_vaddr(opq_state_config->buf);
	if (!opq_state_config->usr_cfg.data) {
		SDE_ERROR("failed to get va rc %d\n", rc);
		rc = -EINVAL;
		goto put_iova;
	}

	if (copy_from_user((void *)(opq_state_config->usr_cfg.data),
					(void __user *)opq_blob->data,
					opq_blob->size)) {
		SDE_ERROR(" failed to copy reproj blob data\n");
		msm_gem_put_vaddr(opq_state_config->buf);
		goto put_iova;
	}

	SDE_DEBUG("size:%llx flags:%llx crc:%x\n id:%d", opq_blob->size,
						opq_blob->flags, opq_blob->crc, idx);

	return rc;

put_iova:
	msm_gem_put_iova(opq_state_config->buf, c_conn->aspace[SDE_IOMMU_DOMAIN_UNSECURE]);
free_gem:
	msm_gem_free_object(opq_state_config->buf);

	return rc;
}

int sde_wb_lsr_connector_set_property(struct drm_connector *connector,
	struct drm_connector_state *state, int idx, uint64_t val, void *display)
{
	struct sde_wb_device *wb_dev = display;
	struct sde_connector_state *c_state;
	struct sde_connector *c_conn;
	int rc = 0;

	if (!connector || !state || !wb_dev) {
		SDE_ERROR("invalid argument(s)\n");
		return -EINVAL;
	}

	c_state = to_sde_connector_state(state);
	c_conn = to_sde_connector(connector);

	switch (idx) {
	case CONNECTOR_PROP_LSR_WB_REPROJ_CONFIG_MATRIX:
		_sde_wb_lsr_set_reproj_matrix(c_conn, c_state,
			(void *)(uintptr_t)val);
		break;
	case CONNECTOR_PROP_OUT_FB_LIST:
		rc = _sde_wb_lsr_set_prop_out_fb_list(connector, state,
				(void *)(uintptr_t)val);
		break;
	case CONNECTOR_PROP_REPROJ_SPARSE_GRID:
		_sde_wb_lsr_set_reproj_info(c_conn, c_state, idx,
			&c_state->reproj_sparse_grid);
		break;
	case CONNECTOR_PROP_REPROJ_RADIAL_DISTORTION_GRID:
		_sde_wb_lsr_set_reproj_info(c_conn, c_state, idx,
			&c_state->reproj_radial_dis_grid);
		break;
	case CONNECTOR_PROP_REPROJ_OPTICAL_AXIS_OFFSET:
		_sde_wb_lsr_set_optical_axis_offset(c_conn, c_state,
			(void *)(uintptr_t)val);
		break;
	case CONNECTOR_PROP_REPROJ_DISPLAY_GAMMA:
		_sde_wb_lsr_set_reproj_info(c_conn, c_state, idx,
			&c_state->reproj_display_gamma);
		break;
	case CONNECTOR_PROP_REPROJ_GCX_SESSION_CONFIG:
		_sde_wb_lsr_set_reproj_info(c_conn, c_state, idx,
			&c_state->reproj_gcx_session_config);
		break;
	case CONNECTOR_PROP_REPROJ_GCX_SESSION_CONFIG_DATA:
		_sde_wb_lsr_set_reproj_info(c_conn, c_state, idx,
			&c_state->reproj_gcx_session_config_data);
		break;
	}

	return rc;
}

int sde_wb_lsr_install_properties(struct drm_connector *connector,
	struct sde_wb_device *wb_dev)
{
	struct sde_connector *conn;

	if (!wb_dev || !wb_dev->wb_cfg || !connector) {
		SDE_ERROR("wb_cfg isn't populated\n");
		return -EINVAL;
	}

	conn = to_sde_connector(connector);

	static const struct drm_prop_enum_list functional_mode[] = {
		{SDE_LSR_WB_RENDER_MODE, "render"},
		{SDE_LSR_WB_REPROJECTION_MODE, "projection"},
	};

	enum wb_opmode opmode = wb_dev->wb_cfg->opmode;

	if (opmode == WB_CSC) {
		msm_property_install_volatile_range(&conn->property_info, "fb_id_list",
				0x0, 0, ~0, 0, CONNECTOR_PROP_OUT_FB_LIST);

		msm_property_install_range(&conn->property_info, "num_views",
			0x0, 0, UINT_MAX, 0, CONNECTOR_PROP_LSR_WB_NUM_VIEWS);
	} else if (opmode == WB_REPRO) {
		msm_property_install_volatile_range(&conn->property_info, "fb_id_list",
				0x0, 0, ~0, 0, CONNECTOR_PROP_OUT_FB_LIST);

		msm_property_install_volatile_range(&conn->property_info, "config_matrix",
				0, 0, ~0, 0, CONNECTOR_PROP_LSR_WB_REPROJ_CONFIG_MATRIX);

		msm_property_install_volatile_range(&conn->property_info,
				"reproj_optical_axis_offset",
				0, 0, ~0, 0, CONNECTOR_PROP_REPROJ_OPTICAL_AXIS_OFFSET);

		msm_property_install_range(&conn->property_info, "sync_to",
				0x0, 0, UINT_MAX, 0, CONNECTOR_PROP_LSR_WB_REPROJ_SYNC_TO);

		msm_property_install_range(&conn->property_info, "num_views",
				0x0, 0, UINT_MAX, 0, CONNECTOR_PROP_LSR_WB_NUM_VIEWS);

		msm_property_install_blob(&conn->property_info, "reproj_sparse_grid",
				0x0, CONNECTOR_PROP_REPROJ_SPARSE_GRID);

		msm_property_install_blob(&conn->property_info, "reproj_radial_dis_grid",
				0x0, CONNECTOR_PROP_REPROJ_RADIAL_DISTORTION_GRID);

		msm_property_install_blob(&conn->property_info, "reproj_display_gamma",
				0x0, CONNECTOR_PROP_REPROJ_DISPLAY_GAMMA);

		msm_property_install_volatile_enum(&conn->property_info, "reproj_mode",
				0x0, 0, functional_mode, ARRAY_SIZE(functional_mode),
				0, CONNECTOR_PROP_REPROJ_FUNCTIONAL_MODE);

		msm_property_install_range(&conn->property_info, "distort_resolution",
				0x0, 0, U32_MAX, 0, CONNECTOR_PROP_REPROJ_DISTORT_RESOLUTION);

		msm_property_install_range(&conn->property_info, "reproj_grid_size",
				0x0, 0, U32_MAX, 0, CONNECTOR_PROP_REPROJ_GRID_SIZE);

		msm_property_install_range(&conn->property_info, "reproj_grid_w",
				0x0, 0, U32_MAX, 0, CONNECTOR_PROP_REPROJ_GRID_WIDTH);

		msm_property_install_range(&conn->property_info, "reproj_grid_h",
				0x0, 0, U32_MAX, 0, CONNECTOR_PROP_REPROJ_GRID_HEIGHT);

		msm_property_install_range(&conn->property_info, "reproj_r_max",
				0x0, 0, U32_MAX, 0, CONNECTOR_PROP_REPROJ_R_MAX);

		msm_property_install_range(&conn->property_info, "reproj_to_lrgb_left",
				0x0, 0, U32_MAX, 0, CONNECTOR_PROP_REPROJ_TO_LRGB_LEFT);

		msm_property_install_range(&conn->property_info, "reproj_to_lrgb_right",
				0x0, 0, U32_MAX, 0, CONNECTOR_PROP_REPROJ_TO_LRGB_RIGHT);

		msm_property_install_range(&conn->property_info, "reproj_error_to_l",
				0x0, 0, U32_MAX, 0, CONNECTOR_PROP_REPROJ_ERROR_TO_L);

		msm_property_install_range(&conn->property_info, "reproj_disp_im_w",
				0x0, 0, U32_MAX, 0, CONNECTOR_PROP_REPROJ_DISP_IM_W);

		msm_property_install_range(&conn->property_info, "reproj_tile_w",
				0x0, 0, U32_MAX, 0, CONNECTOR_PROP_REPROJ_TILE_W);

		msm_property_install_range(&conn->property_info, "reproj_min_bbox_w",
				0x0, 0, U32_MAX, 0, CONNECTOR_PROP_REPROJ_MIN_BBOX_W);

		msm_property_install_range(&conn->property_info, "reproj_disp_im_h",
				0x0, 0, U32_MAX, 0, CONNECTOR_PROP_REPROJ_DISP_IM_H);

		msm_property_install_range(&conn->property_info, "reproj_tile_h",
				0x0, 0, U32_MAX, 0, CONNECTOR_PROP_REPROJ_TILE_H);

		msm_property_install_range(&conn->property_info, "reproj_min_bbox_h",
				0x0, 0, U32_MAX, 0, CONNECTOR_PROP_REPROJ_MIN_BBOX_H);

		msm_property_install_blob(&conn->property_info, "reproj_gcx_session_config",
				0x0, CONNECTOR_PROP_REPROJ_GCX_SESSION_CONFIG);

		msm_property_install_blob(&conn->property_info, "reproj_gcx_session_config_data",
				0x0, CONNECTOR_PROP_REPROJ_GCX_SESSION_CONFIG_DATA);
	}

	return 0;
}

int sde_wb_lsr_get_fb_id_list(struct sde_wb_device *wb_dev, struct hfi_wb_out_buff *out_buffers,
		struct sde_view_descriptor *view_desc, struct sde_view_descriptor *back_view_desc,
		bool is_back_view_en)
{
	struct msm_gem_address_space *aspace = NULL;
	struct sde_kms *sde_kms;
	const struct msm_format *format;
	struct drm_framebuffer *fb;
	struct sde_hw_fmt_layout *layout;
	uint32_t ret;
	int count = 0, view_idx = 0;
	u32 flags, hfi_format, modifier;
	struct sde_format_extended fmt;
	bool is_back_view = false;
	struct sde_view_descriptor *desc = NULL;

	if (!wb_dev || !out_buffers) {
		SDE_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (is_back_view_en && !back_view_desc) {
		SDE_ERROR("Invalid paparms\n");
		return -EINVAL;
	}

	layout = kzalloc(sizeof(struct sde_hw_fmt_layout), GFP_KERNEL);
	sde_kms = sde_connector_get_kms(wb_dev->connector);

	/**
	 * TODO, assumed default as non secure for now,
	 * secure cases will be enabled with LSR secure cases.
	 */
	aspace = sde_kms->aspace[SDE_IOMMU_DOMAIN_UNSECURE];
	if (!aspace) {
		SDE_ERROR("invalid aspace\n");
		return -EINVAL;
	}

	for (int i = 0; i < MAX_VIEWS * FLIP_VIEWS; i++) {
		view_idx = i % MAX_VIEWS;
		is_back_view = ((i / MAX_VIEWS) && is_back_view_en) ? true : false;
		if (is_back_view)
			desc = &back_view_desc[view_idx];
		else if (i < MAX_VIEWS)
			desc = &view_desc[view_idx];
		else
			continue;

		for (int j = 0; j < desc->num_fbs; j++) {
			flags = 0x0;
			fb = desc->fb_id[j];
			if (!fb) {
				SDE_ERROR("invalid fb\n");
				return -EINVAL;
			}
			ret = msm_framebuffer_prepare(fb, aspace);
			if (ret) {
				SDE_ERROR("failed to prepare framebuffer %d\n", ret);
				return -EINVAL;
			}
			drm_framebuffer_get(fb);
			format = msm_framebuffer_format(fb);
			fmt.fourcc_format = fb->format->format;
			fmt.modifier = fb->modifier;
			if (!format) {
				SDE_ERROR("invalid fb fmt\n");
				return -EINVAL;
			}
			layout->format = sde_get_sde_format_ext(format->pixel_format, fb->modifier);
			if (!layout->format) {
				SDE_ERROR("invalid fb format\n");
				return -EINVAL;
			}

			layout->width = fb->width;
			layout->height = fb->height;
			layout->num_planes = layout->format->num_planes;
			ret = sde_format_populate_layout(aspace, fb, layout);
			if (ret) {
				SDE_ERROR("failed SDE_format_populate_layout\n");
				return -EINVAL;
			}

			modifier = fb->modifier & 0x00000000ffffffff;
			if (format->pixel_format == DRM_FORMAT_C8) {
				if (modifier & DRM_FORMAT_MOD_QCOM_CAC_R)
					flags |= (HFI_LSR_COLOR_FIELD_R <<
							HFI_LSR_COLOR_FIELD_BIT_POS);
				else if (modifier & DRM_FORMAT_MOD_QCOM_CAC_G)
					flags |= (HFI_LSR_COLOR_FIELD_G <<
							HFI_LSR_COLOR_FIELD_BIT_POS);
				else if (modifier & DRM_FORMAT_MOD_QCOM_CAC_B)
					flags |= (HFI_LSR_COLOR_FIELD_B <<
							HFI_LSR_COLOR_FIELD_BIT_POS);
				else if (modifier & DRM_FORMAT_MOD_QCOM_FSC_ALPHA)
					flags |= (HFI_LSR_COLOR_FIELD_ALPHA <<
							HFI_LSR_COLOR_FIELD_BIT_POS);
			}
			if (is_back_view)
				flags |= (HFI_LSR_BUFFER_INDEX_1 << HFI_LSR_BUFFER_INDEX_BIT_POS);

			if (i % MAX_VIEWS)
				flags |= 1 << HFI_LSR_VIEW_ID_BIT_POS;

			hfi_format = hfi_catalog_get_hfi_format(&fmt);

			for (int planes = 0; planes < SDE_MAX_PLANES; planes++)
				out_buffers[count].addr_l[planes] = layout->plane_addr[planes];

			out_buffers[count].flags = flags;
			out_buffers[count].size = layout->total_size;
			out_buffers[count].format = hfi_format;
			count++;
		}
	}
	kfree(layout);
	return 0;
}

int sde_wb_update_lsr_perf(struct drm_connector *connector,
		void *display, struct sde_lsr_perf perf)
{
	int rc = 0;
	struct sde_connector *sde_conn;
	struct sde_reproj *reproj_conn = NULL;

	sde_conn = to_sde_connector(connector);
	reproj_conn = sde_conn->reproj_conn;

	if (reproj_conn) {
		rc = reproj_conn->update_lsr_perf(reproj_conn, reproj_conn->type, perf);
		SDE_DEBUG("lsr perf clk vote = %lld, bw vote = %lld for display type = %d",
			perf.bw_vote, perf.clk_vote, reproj_conn->type);
	}

	return rc;
}

int sde_wb_connector_reproj_setup(struct sde_connector *conn, struct sde_wb_device *wb_dev)
{
	int rc = 0;

	if (!wb_dev->wb_cfg) {
		SDE_ERROR("wb_cfg isn't populated\n");
		return -EINVAL;
	}

	conn->reproj_conn = kzalloc(sizeof(*conn->reproj_conn), GFP_KERNEL);
	if (!conn->reproj_conn) {
		rc = -ENOMEM;
		goto end;
	}

	conn->reproj_conn->type = wb_dev->wb_cfg->opmode;
	rc = msm_reproj_disp_register_intf(conn->reproj_conn);
	if (rc) {
		SDE_ERROR("failed to register reproj disp\n");
		return -ENODEV;
	}

	rc = conn->reproj_conn->get_info(conn->reproj_conn, conn->reproj_conn->type);
	if (rc)
		SDE_ERROR("failed to get LSR info for reproj disp\n");
end:
	return rc;
}

void sde_wb_connector_reset_reproj_state(struct sde_connector_state *c_state)
{
	if (!c_state)
		return;

	c_state->reproj_sparse_grid.usr_cfg.size = 0;
	c_state->reproj_radial_dis_grid.usr_cfg.size = 0;
	c_state->reproj_display_gamma.usr_cfg.size = 0;
	c_state->reproj_gcx_session_config.usr_cfg.size = 0;
	c_state->reproj_gcx_session_config_data.usr_cfg.size = 0;

	c_state->reproj_sparse_grid.remote_iova = 0;
	c_state->reproj_radial_dis_grid.remote_iova = 0;
	c_state->reproj_display_gamma.remote_iova = 0;
	c_state->reproj_gcx_session_config.remote_iova = 0;
	c_state->reproj_gcx_session_config_data.remote_iova = 0;
}
