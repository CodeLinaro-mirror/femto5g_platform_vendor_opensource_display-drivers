/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _SDE_WB_LSR_H_
#define _SDE_WB_LSR_H_

#include "sde_wb.h"
#include "hfi_catalog.h"
#include "hfi_connector.h"
#include "hfi_utils.h"

#if IS_ENABLED(CONFIG_DRM_SDE_LSR)
/**
 * sde_wb_lsr_connector_set_property - Set LSR properties on WB connector
 * @connector: Pointer to DRM connector
 * @state: Pointer to DRM connector state
 * @idx: Property idx
 * @va;: Property value
 * @display: Pointer to display
 */
int sde_wb_lsr_connector_set_property(struct drm_connector *connector,
		struct drm_connector_state *state, int idx, uint64_t val, void *display);
/**
 * sde_wb_lsr_install_properties - Install LSR properties on WB connector
 * @connector: Pointer to DRM connector
 * @wb_dev: Pointer to sde_wb_device
 */
int sde_wb_lsr_install_properties(struct drm_connector *connector,
		struct sde_wb_device *wb_dev);
/**
 * sde_wb_lsr_get_fb_id_list - Get FB ID list property
 * @wb_dev: Pointer to sde_wb_device
 * @out_buffers: Pointer to hfi_wb_out_buff
 * @view_desc: Pointer to sde_view_descriptor for front view
 * @back_view_desc: Pointer to sde_view_descriptor for fback view
 * @is_back_view_en: Indicates if back view is enabled
 */
int sde_wb_lsr_get_fb_id_list(struct sde_wb_device *wb_dev, struct hfi_wb_out_buff *out_buffers,
		struct sde_view_descriptor *view_desc, struct sde_view_descriptor *back_view_desc,
		bool is_back_view_en);

/**
 * hfi_wb_lsr_prop_helper_alloc - Allocates memory for prop helper to support LSR
 * @hfi_conn: Pointer to hfi_conector
 */
int hfi_wb_lsr_prop_helper_alloc(struct hfi_connector *hfi_conn);

/**
 * hfi_wb_lsr_add_props - Adds LSR properties on WB connector
 * @wb_dev: Pointer to sde_wb_device
 * @hfi_conn: Pointer to hfi_conector
 * @cstate: Pointer to sde_connector_state
 * @disp_id: Display index
 * @cmd_buf: Pointer to hfi_cmdbuf_t
 */
int hfi_wb_lsr_add_props(struct sde_wb_device *wb_dev, struct hfi_connector *hfi_conn,
		struct sde_connector_state *cstate,
		u32 disp_id, struct hfi_cmdbuf_t *cmd_buf);
/**
 * sde_wb_lsr_destroy_fb_list - clean up connector state's out_fb buffer
 * @c_conn: Pointer to dpu connector structure
 * @c_state: Pointer to dpu connector state structure
 */
void sde_wb_lsr_destroy_fb_list(struct sde_connector *c_conn,
	struct sde_connector_state *c_state);
#else
static inline
int sde_wb_lsr_connector_set_property(struct drm_connector *connector,
		struct drm_connector_state *state, int idx, uint64_t val, void *display)
{
	return 0;
}

static inline
int sde_wb_lsr_install_properties(struct drm_connector *connector,
		struct sde_wb_device *wb_dev)
{
	return 0;
}

static inline
int sde_wb_lsr_get_fb_id_list(struct sde_wb_device *wb_dev, struct hfi_wb_out_buff *out_buffers,
		struct sde_view_descriptor *view_desc, struct sde_view_descriptor *back_view_desc,
		bool is_back_view_en)
{
	return 0;
}

static inline
int hfi_wb_lsr_prop_helper_alloc(struct hfi_connector *hfi_conn)
{
	return 0;
}

static inline
int hfi_wb_lsr_add_props(struct sde_wb_device *wb_dev, struct hfi_connector *hfi_conn,
		struct sde_connector_state *cstate,
		u32 disp_id, struct hfi_cmdbuf_t *cmd_buf)
{
	return 0;
}

static inline void sde_wb_lsr_destroy_fb_list(struct sde_connector *c_conn,
	struct sde_connector_state *c_state)
{
}
#endif /* CONFIG_DRM_SDE_LSR */
#endif /* __SDE_WB_LSR_H__ */
