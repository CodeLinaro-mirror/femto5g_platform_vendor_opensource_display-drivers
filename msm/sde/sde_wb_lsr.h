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
 * hfi_lsr_fw_debug_set - Sets LSR FW debug value
 * @val: value to set
 * @dev: pointer to drm device
 */
int hfi_lsr_fw_debug_set(u64 val, struct drm_device *dev);

/**
 * hfi_lsr_fw_debug_get - Gets LSR FW debug value
 * @val: pointer to store value
 */
int hfi_lsr_fw_debug_get(u64 *val);

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

/**
 * hfi_wb_add_lsr_init_props - set lsr properties required for lsr sys init
 * @wb_dev: pointer to wb device structure
 * @drm_conn: pointer to drm connector
 * @prop_collector: property utility structure for hfi properties
 */
int hfi_wb_add_lsr_init_props(struct sde_wb_device *wb_dev, struct drm_connector *drm_conn,
			struct hfi_util_u32_prop_helper *prop_collector);

/**
 * hfi_wb_display_lsr_enable - enable/disable lsr display
 * @drm_conn: pointer to drm connector
 * @enable: boolean variable to set enable/disable
 */
int hfi_wb_display_lsr_enable(struct drm_connector *drm_conn, bool enable);

/**
 * sde_wb_update_lsr_perf - set lsr perf votes
 * @drm_conn: pointer to drm connector
 * @perf: sde_lsr_perf structure to add clock/bus votes
 */
int sde_wb_update_lsr_perf(struct drm_connector *connector, void *display,
		struct sde_lsr_perf perf);
/**
 * sde_wb_connector_reproj_setup - setup sde connector with reprojection/lsr info
 * @conn: pointer to sde_connector
 * @wb_dev: pointer to sde_wb_device
 */
int sde_wb_connector_reproj_setup(struct sde_connector *conn, struct sde_wb_device *wb_dev);

/**
 * hfi_lsr_display_disable_handler - lsr display disable handler
 * @obj_id: hfi object id for the response received
 * @cmd_id: hfi command for the response
 * @listener: pointer to the listener object of the response
 */
void hfi_lsr_display_disable_handler(u32 obj_id, u32 cmd_id,
		void *payload, u32 size, struct hfi_prop_listener *listener);

/**
 * hfi_conn_send_lsr_display_ctrl_cmd - set lsr enable/disable commands
 * @hfi_kms: pointer to hfi kms
 * @hfi_conn: pointer to hfi connector
 * @cmd_buf: pointer to disapla enable/disabe cmd buf
 * @flags: pointer to flags for lsr display commands
 * @enable: boolean variable to be set enable/disable
 */
int hfi_conn_send_lsr_display_ctrl_cmd(struct hfi_kms *hfi_kms, struct hfi_connector *hfi_conn,
		struct hfi_cmdbuf_t *cmd_buf, u32 *flags, bool enable);

/**
 * sde_wb_connector_reset_reproj_state- reset reprojection state config
 * c_state: pointer to sde_connector_state
 */
void sde_wb_connector_reset_reproj_state(struct sde_connector_state *c_state);

extern int lsr_fw_reset(void);
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
int hfi_lsr_fw_debug_set(u64 val, struct drm_device *dev)
{
	return 0;
}

static inline
int hfi_lsr_fw_debug_get(u64 *val)
{
	return 0;
}

static inline
int hfi_wb_lsr_add_props(struct sde_wb_device *wb_dev, struct hfi_connector *hfi_conn,
	struct sde_connector_state *cstate, u32 disp_id, struct hfi_cmdbuf_t *cmd_buf)
{
	return 0;
}

static inline void sde_wb_lsr_destroy_fb_list(struct sde_connector *c_conn,
	struct sde_connector_state *c_state)
{
}
static inline int hfi_wb_add_lsr_init_props(struct sde_wb_device *wb_dev,
	struct drm_connector *drm_conn, struct hfi_util_u32_prop_helper *prop_collector)
{
	return 0;
}

static inline int hfi_wb_display_lsr_enable(struct drm_connector *drm_conn, bool enable)
{
	return 0;
}

static inline int sde_wb_update_lsr_perf(struct drm_connector *connector, void *display,
	struct sde_lsr_perf perf)
{
	return 0;
}

static inline int sde_wb_connector_reproj_setup(struct sde_connector *conn,
	struct sde_wb_device *wb_dev)
{
	return 0;
}

static inline void hfi_lsr_display_disable_handler(u32 obj_id, u32 cmd_id, void *payload, u32 size,
	struct hfi_prop_listener *listener)
{
}

static inline int hfi_conn_send_lsr_display_ctrl_cmd(struct hfi_kms *hfi_kms,
	struct hfi_connector *hfi_conn, struct hfi_cmdbuf_t *cmd_buf, u32 *flags, bool enable)
{
	return 0;
}

static inline void sde_wb_connector_reset_reproj_state(struct sde_connector_state *c_state)
{
}

static inline int lsr_fw_reset(void)
{
	return 0;
}
#endif /* CONFIG_DRM_SDE_LSR */
#endif /* __SDE_WB_LSR_H__ */
