/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _HFI_CONNECTOR_H_
#define _HFI_CONNECTOR_H_

#include "sde_connector.h"
#include "hfi_adapter.h"
#include "hfi_utils.h"
#include "sde_wb_lsr.h"

#define HFI_CONNECTOR_MAX_PROPS 128
#define HFI_CONNECTOR_BASE_PROP_MAX_SIZE 1024

/**
 * struct hfi_connector - local sde connector hfi structure
 * @sde_base: Pointer to base sde connector structure
 * @hfi_lock: Mutex to protect hfi specific data
 * @base_props: prop helper object for intermediate property collection
 * @kv_props: kv pair helper object for intermediate property collection
 * @lsr_props: prop helper object for lsr property collection
 * @lsr_blob_props: prop helper object for lsr blob property collection
 * @lsr_out_buff_props: prop helper object for lsr out buffer property collection
 * @disable_listener: listener api for display disable response
 */
struct hfi_connector {
	struct sde_connector *sde_base;
	struct mutex hfi_lock;
	struct hfi_util_u32_prop_helper *base_props;
	struct hfi_util_kv_helper *kv_props;
	struct hfi_util_u32_prop_helper *lsr_props;
	struct hfi_util_u32_prop_helper *lsr_blob_props;
	struct hfi_util_u32_prop_helper *lsr_out_buffer_props;
	struct hfi_prop_listener disable_listener;
};

/**
 * struct hfi_connector_state - private hfi connector status structure
 * @sde_base: Pointer to base sde connector structure
 */
struct hfi_connector_state {
	struct sde_connector_state *sde_base;
};

#if IS_ENABLED(CONFIG_MDSS_HFI)
/**
 * hfi_connector_init - create hfi connector object for a given display
 * @connector_type: Set to appropriate DRM_MODE_CONNECTOR_ type
 * @c_conn: Pointer to sde connector struct
 * @Returns: 0 on success, or error code on failure
 */
int hfi_connector_init(int connector_type, struct sde_connector *c_conn);

/**
 * hfi_connector_get_cmd_buf - retrieve a cmd_buffr for DRM connector of cmd_type
 * @drm_conn: pointer to the DRM connector structure
 * @cmd_buf_type: type of command buffer requested
 * Return: pointer to command buffer structure on success,
 *         or NULL on failure
 */
struct hfi_cmdbuf_t *hfi_connector_get_cmd_buf(struct drm_connector *drm_conn,
		u32 cmd_buf_type);

void sde_connector_add_roi_v1(u32 hfi_prop, struct sde_connector *conn,
	struct sde_connector_state *old_state, struct hfi_cmdbuf_t *cmd_buf);

void sde_connector_add_autorefresh(u32 hfi_prop, struct sde_connector *conn,
	struct sde_connector_state *old_state, struct hfi_cmdbuf_t *cmd_buf, bool is_cont_splash);

/**
 * hfi_conn_send_panel_init - send panel config and opertaing modes to fw
 * @drm_conn: pointer to the DRM connector structure
 * Return: error on failure to send or 0 on success
 */
int hfi_conn_send_panel_init(struct drm_connector *drm_conn);

/**
 * hfi_connector_report_panel_dead - report panel dead
 * @c_conn: Pointer to sde_connector struct
 * @skip_pre_kickoff: flag to skip_pre_kickoff
 */
void hfi_connector_report_panel_dead(struct sde_connector *c_conn, bool skip_pre_kickoff);

#else
int hfi_connector_init(int connector_type, struct sde_connector *c_conn)
{
	return -HFI_ERROR;
}

struct hfi_cmdbuf_t *hfi_connector_get_cmd_buf(struct drm_connector *drm_conn,
		u32 cmd_buf_type)
{
	return NULL;
}

int hfi_conn_send_panel_init(struct drm_connector *drm_conn)
{
	return 0;
}

void hfi_connector_report_panel_dead(struct sde_connector *sde_conn, bool skip_pre_kickoff)
{
}
#endif // IS_ENABLED(CONFIG_MDSS_HFI)

#endif  // _HFI_CONNECTOR_H_
