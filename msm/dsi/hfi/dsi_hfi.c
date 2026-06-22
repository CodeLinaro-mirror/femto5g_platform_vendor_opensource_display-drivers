// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>

#include "msm_drv.h"
#include "msm_mmu.h"
#include "msm_gem.h"
#include "hfi_msm_drv.h"
#include "hfi_connector.h"
#include "dsi_drm.h"
#include "dsi_defs.h"
#include "dsi_display_hfi.h"
#include "dsi_display.h"
#include "dsi_hfi.h"
#include "dsi_panel.h"
#include "dsi_parser.h"
#include "hfi_adapter.h"
#include "hfi_props.h"
#include "hfi_kms.h"
#include "sde_dsc_helper.h"

#define to_dsi_display(x) container_of(x, struct dsi_display, host)

#define DSI_HFI_MIN_MAPPED_ADDR_SIZE                  (PAGE_SIZE * 4)
#define DSI_HFI_MAX_MAPPED_ADDR_SIZE                  (PAGE_SIZE * 64)
#define MAX_TIMING_PER_PACKET  32

static int dsi_display_hfi_power_supplies(struct dsi_display *display,
					  u32 hfi_power_control, bool hfi_power_enable)
{
	int rc = 0;

	if (!display) {
		DSI_ERR("Invalid dsi_display\n");
		goto end;
	}

	if (hfi_power_control & HFI_PANEL_POWER) {
		rc = dsi_display_hfi_panel_enable_supplies(display, hfi_power_enable);
		if (rc) {
			DSI_ERR("[%s] dsi panel power supply %s failed, rc=%d\n", display->name,
				hfi_power_enable ? "enable" : "disable", rc);
			if (hfi_power_enable)
				goto error_panel_disable;
		}
	}

	goto end;

error_panel_disable:
	if (hfi_power_control & HFI_PANEL_POWER)
		(void)dsi_display_hfi_panel_enable_supplies(display, false);
end:
	DSI_DEBUG("%s: DSI core power, hfi_pwr_mask=%d, enable=%d\n", __func__,
		hfi_power_control, hfi_power_enable);
	return rc;
}

static int _dsi_display_hfi_process_ssr_start(struct hfi_client_t *hfi_client)
{
	struct dsi_display *display;
	struct dsi_display_hfi *display_hfi;
	int rc = 0;

	display = (struct dsi_display *)hfi_client->priv;
	if (!display) {
		DSI_ERR("invalid display\n");
		return -EINVAL;
	}

	if (display->panel)
		atomic_set(&display->panel->ssr_in_progress, 1);

	display_hfi = display->dsi_hfi_info;
	if (!display_hfi) {
		DSI_ERR("invalid display hfi handle\n");
		return -EINVAL;
	}

	rc = hfi_adapter_release_all_cmd_bufs(hfi_client);
	if (rc) {
		DSI_ERR("failed to release command buffers, rc: %d\n", rc);
		return rc;
	}

	return rc;
}

static int _dsi_display_hfi_process_ssr_end(struct hfi_client_t *hfi_client)
{
	struct dsi_display *display;
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;
	int rc = 0;

	display = (struct dsi_display *)hfi_client->priv;
	if (!display) {
		DSI_ERR("invalid display\n");
		return -EINVAL;
	}

	sde_kms = sde_connector_get_kms(display->drm_conn);
	if (!sde_kms)
		return -EINVAL;

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms)
		return -EINVAL;

	rc = hfi_kms_send_trace_cfg(hfi_kms, HFI_TRUE);
	if (rc) {
		DSI_ERR("failed to send trace config to DCP, rc: %d\n", rc);
		return rc;
	}

	rc = dsi_hfi_panel_init(display, display->panel);
	if (rc) {
		DSI_ERR("failed to send panel init to DCP: %d", rc);
		return rc;
	}

	if (display->panel)
		atomic_set(&display->panel->ssr_in_progress, 0);

	return rc;
}

static int dsi_hfi_process_event(struct hfi_client_t *hfi_client, enum hfi_adapter_event_type event,
			bool blocking)
{
	if (!hfi_client) {
		DSI_ERR("invalid client\n");
		return -EINVAL;
	}

	DSI_DEBUG("%s: called\n", __func__);

	switch (event) {
	case HFI_ADAPTER_EVENT_SSR_START:
		return _dsi_display_hfi_process_ssr_start(hfi_client);
	case HFI_ADAPTER_EVENT_SSR_END:
		return _dsi_display_hfi_process_ssr_end(hfi_client);
	default:
		DSI_ERR("%s: invalid event type: %d\n", __func__, event);
		return -EINVAL;
	}

	return 0;
}

int dsi_hfi_process_cmd_buf(struct hfi_client_t *hfi_client, struct hfi_cmdbuf_t *cmd_buf)
{
	int rc = 0;

	if (!hfi_client || !cmd_buf) {
		DSI_ERR("Invalid client or buffer\n");
		return -EINVAL;
	}

	rc = hfi_adapter_unpack_cmd_buf(hfi_client, cmd_buf);
	if (rc) {
		DSI_ERR("[WARNING] Error in response packet or unpacking buffer\n");
		return rc;
	}

	rc = hfi_adapter_release_cmd_buf(hfi_client, cmd_buf);
	if (rc)
		DSI_ERR("[WARNING] Failed to release command buffer\n");

	return rc;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
int dsi_hfi_misr_setup(struct dsi_display *display)
{
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	struct dsi_display_hfi *display_hfi;
	struct misr_setup_data misr_data;
	u32 obj_id;
	int rc = 0;

	if (!display)
		return -EINVAL;

	display_hfi = display->dsi_hfi_info;
	if (!display_hfi)
		return -EINVAL;

	struct drm_connector *drm_conn = display->drm_conn;

	if (!drm_conn)
		return -EINVAL;

	/* TODO! Check if we want to use this as display_id or the display->display_type*/
	obj_id = sde_conn_get_display_obj_id(drm_conn);

	cmd_buf = hfi_adapter_get_cmd_buf(display_hfi->hfi_client, obj_id,
			HFI_CMDBUF_TYPE_GET_DEBUG_DATA);
	if (!cmd_buf) {
		DSI_ERR("Failed to get valid command buffer\n");
		return -EINVAL;
	}

	misr_data.display_id = obj_id;
	misr_data.enable = display->misr_enable;
	misr_data.frame_count = display->misr_frame_count;
	misr_data.module_type = HFI_DEBUG_MISR_DSI;

	rc = hfi_adapter_add_set_property(display_hfi->hfi_client, cmd_buf,
					HFI_COMMAND_DEBUG_MISR_SETUP, obj_id,
					HFI_PAYLOAD_TYPE_U32_ARRAY, &misr_data,
					sizeof(misr_data), HFI_HOST_FLAGS_NONE);
	if (rc) {
		DSI_ERR("Failed to add property\n");
		return rc;
	}

	DSI_DEBUG("misr_setup: sending cmd buf\n");
	rc = hfi_adapter_set_cmd_buf(display_hfi->hfi_client, cmd_buf);
	SDE_EVT32(obj_id, HFI_COMMAND_DEBUG_MISR_SETUP, rc, SDE_EVTLOG_FUNC_CASE1);
	if (rc)
		DSI_ERR("Failed to send misr_setup command\n");

	return rc;
}

static void dsi_hfi_process_misr_read(struct dsi_display *display, void *payload, u32 size)
{
	struct misr_read_data_ret *misr_data;
	struct dsi_misr_values *misr_read_values;
	u32 max_count = 0;
	u32 module_type = 0;
	u32 *misr_values;

	if (!payload || !size) {
		DSI_ERR("Invalid payload received from FW\n");
		return;
	}

	DSI_DEBUG("About to read MISR values from %s\n", __func__);

	misr_read_values = &display->misr_vals;
	misr_data = (struct misr_read_data_ret *)payload;

	max_count = misr_data->num_misr;
	module_type = misr_data->module_type;
	DSI_DEBUG("Module type:%d, Max_count:%d\n",
		module_type, max_count);
	misr_values = (u32 *)(payload + sizeof(u32) * 2);

	memset(&misr_read_values->misr_values, 0, sizeof(u32) * MAX_MISR_MODULES);

	for (int i = 0; i < max_count; i++)
		misr_read_values->misr_values[i] = misr_values[i];

	misr_read_values->count = max_count;

}

int dsi_hfi_misr_read(struct dsi_display *display)
{
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	struct dsi_display_hfi *display_hfi;
	struct misr_read_data misr_read;
	struct drm_connector *drm_conn;
	u32 obj_id, packet_id = 0;
	int rc = 0;

	if (!display)
		return -EINVAL;

	display_hfi = display->dsi_hfi_info;
	if (!display_hfi)
		return -EINVAL;

	drm_conn = display->drm_conn;

	obj_id = sde_conn_get_display_obj_id(drm_conn);

	cmd_buf = hfi_adapter_get_cmd_buf(display_hfi->hfi_client, obj_id,
			HFI_CMDBUF_TYPE_GET_DEBUG_DATA);
	if (!cmd_buf) {
		DSI_ERR("Failed to get valid command buffer\n");
		return -EINVAL;
	}

	misr_read.display_id = obj_id;
	misr_read.module_type = HFI_DEBUG_MISR_DSI;

	rc = hfi_adapter_add_get_property(display_hfi->hfi_client, cmd_buf,
			HFI_COMMAND_DEBUG_MISR_READ, obj_id,
			HFI_PAYLOAD_TYPE_U32_ARRAY, &misr_read, sizeof(misr_read),
			&display->hfi_cb_obj, (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
			HFI_HOST_FLAGS_NON_DISCARDABLE), false, &packet_id);
	if (rc)
		DSI_ERR("Failed to add MISR read command\n");

	SDE_EVT32(drm_conn->base.id, obj_id, HFI_COMMAND_DEBUG_MISR_READ, SDE_EVTLOG_FUNC_CASE1);
	rc = hfi_adapter_set_cmd_buf_blocking(display_hfi->hfi_client, cmd_buf);
	SDE_EVT32(drm_conn->base.id, obj_id, HFI_COMMAND_DEBUG_MISR_READ, rc,
			SDE_EVTLOG_FUNC_CASE2);
	hfi_adapter_remove_listener_by_packet_id(display_hfi->hfi_client, packet_id);

	return rc;
}

#else
int dsi_hfi_misr_setup(struct dsi_display *display)
{
	return 0;
}

static void dsi_hfi_process_misr_read(struct dsi_display *display, void *payload, u32 size)
{

}

int dsi_hfi_misr_read(struct dsi_display *display)
{
	return 0;
}

#endif /* CONFIG_DEBUG_FS */

void dsi_hfi_prop_handler(u32 hfi_uid, u32 prop, void *payload, u32 size,
			  struct hfi_prop_listener *listener)
{
	struct dsi_display_hfi *display_hfi;
	struct dsi_display *display;
	u32 dsi_display_obj_id;
	int rc = 0;

	if (!listener) {
		DSI_ERR("invalid listener\n");
		return;
	}

	display = container_of(listener, struct dsi_display,
						hfi_cb_obj);
	if (!display) {
		DSI_ERR("invalid object or listener from FW\n");
		return;
	}

	display_hfi = display->dsi_hfi_info;
	dsi_display_obj_id = sde_conn_get_display_obj_id(display->drm_conn);

	if (dsi_display_obj_id != hfi_uid) {
		DSI_ERR("Component and HFI ID mismatch (%d != %d)\n",
				dsi_display_obj_id, hfi_uid);
		return;
	}

	switch (prop) {
	case HFI_COMMAND_DISPLAY_MODE_VALIDATE:
		if (payload)
			display_hfi->mode_valid = true;
		break;
	case HFI_COMMAND_DISPLAY_POWER_CONTROL:
		if (payload && size >= 2) {
			rc = dsi_display_hfi_power_supplies(display,
								((u32 *)payload)[0],
								((bool *)payload)[1]);
			if (rc)
				DSI_ERR("Could not power on supplies rc: %d\n", rc);
		} else {
			DSI_ERR("invalid payload: 0x%pK or size: %u to power on supplies\n",
				payload, size);
		}
		break;
	case HFI_COMMAND_DISPLAY_DISABLE:
		msleep(20);
		break;
	case HFI_COMMAND_DISPLAY_POST_DISABLE:
	case HFI_COMMAND_DISPLAY_ENABLE:
	case HFI_COMMAND_DISPLAY_POST_ENABLE:
	case HFI_COMMAND_DISPLAY_SET_MODE:
	case HFI_COMMAND_DISPLAY_POWER_REGISTER:
	case HFI_COMMAND_DISPLAY_TRANSFER_DCS_CMD:
	case HFI_COMMAND_DISPLAY_DSI_CUSTOM_DCS_CMDS_SET_REMAP:
	case HFI_COMMAND_DISPLAY_DSI_CUSTOM_DCS_CMDS_SET_REPLACE:
		break;
	case HFI_COMMAND_DEBUG_MISR_READ:
		dsi_hfi_process_misr_read(display, payload, size);
		break;
	default:
		DSI_ERR("Invalid HFI property 0x%x\n", prop);
	}

}

int dsi_display_hfi_setup_hfi(struct dsi_display *display, struct hfi_adapter_t *hfi_host)
{
	struct dsi_display_hfi *display_hfi;
	int rc = 0;

	if (!display) {
		DSI_ERR("invalid display\n");
		return -EINVAL;
	}

	if (!hfi_host) {
		DSI_ERR("invalid hfi_host\n");
		return -EINVAL;
	}

	display_hfi = kvzalloc(sizeof(struct dsi_display_hfi), GFP_KERNEL);
	if (!display_hfi) {
		DSI_ERR("failed to allocate memory for display_hfi\n");
		return -ENOMEM;
	}

	display->dsi_hfi_info = display_hfi;
	display_hfi->tx_cmd_buf_fill_level = 0;
	display->hfi_cb_obj.hfi_prop_handler = dsi_hfi_prop_handler;
	display_hfi->hfi_adapter = hfi_host;
	mutex_init(&display_hfi->rt_dcs_cmd_lock);

	display_hfi->hfi_client = kmalloc(sizeof(struct hfi_client_t), GFP_KERNEL);
	if (!display_hfi->hfi_client)
		return -ENOMEM;

	display_hfi->hfi_client->process_cmd_buf = dsi_hfi_process_cmd_buf;
	display_hfi->hfi_client->process_event = dsi_hfi_process_event;
	display_hfi->hfi_client->priv = (void *) display;

	rc = hfi_adapter_client_register(hfi_host, display_hfi->hfi_client);
	if (rc) {
		DSI_ERR("unable to register hfi client\n");
		kfree(display_hfi->hfi_client);
		return -ENODEV;
	}

	return 0;
}

int dsi_display_hfi_send_cmd_buf(struct dsi_display *display,
					struct hfi_client_t *hfi_client, u32 hfi_cmd,
					const char *display_type, u32 hfi_payload_type,
					void *payload, u32 payload_size, u32 flags)
{
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	struct drm_connector *drm_conn;
	int rc = 0;
	u32 obj_id, packet_id = 0;
	bool remove_on_cb = false;

	if (!display) {
		DSI_ERR("invalid display\n");
		return -EINVAL;
	}

	drm_conn = display->drm_conn;

	switch (hfi_cmd) {
	case HFI_COMMAND_DISPLAY_MODE_VALIDATE:
		flags = HFI_HOST_FLAGS_NONE;
		break;
	case HFI_COMMAND_DISPLAY_SET_MODE:
	case HFI_COMMAND_DISPLAY_ENABLE:
	case HFI_COMMAND_DISPLAY_POST_ENABLE:
	case HFI_COMMAND_DISPLAY_POST_DISABLE:
	case HFI_COMMAND_DISPLAY_DISABLE:
		flags |= HFI_HOST_FLAGS_RESPONSE_REQUIRED;
		break;
	default:
		break;
	}

	obj_id = sde_conn_get_display_obj_id(drm_conn);

	cmd_buf = hfi_adapter_get_cmd_buf(hfi_client, obj_id,
			HFI_CMDBUF_TYPE_DISPLAY_INFO_BLOCKING);
	if (!cmd_buf) {
		DSI_ERR("could not get cmd_buf for hfi_cmd 0x%x\n", hfi_cmd);
		return -ENODEV;
	}

	if (flags & HFI_HOST_FLAGS_RESPONSE_REQUIRED) {
		remove_on_cb = ((hfi_cmd != HFI_COMMAND_DISPLAY_EVENT_REGISTER)
				&& (hfi_cmd != HFI_COMMAND_DISPLAY_EVENT_DEREGISTER));
		rc = hfi_adapter_add_get_property(hfi_client, cmd_buf, hfi_cmd, obj_id,
			hfi_payload_type, payload, payload_size, &display->hfi_cb_obj, flags,
			remove_on_cb, &packet_id);
		if (rc)
			DSI_ERR("could not set property for hfi_cmd 0x%x\n", hfi_cmd);

		SDE_EVT32(obj_id, hfi_cmd, SDE_EVTLOG_FUNC_CASE1);
		rc = hfi_adapter_set_cmd_buf_blocking(hfi_client, cmd_buf);
		SDE_EVT32(obj_id, hfi_cmd, rc, SDE_EVTLOG_FUNC_CASE2);
	} else {
		rc = hfi_adapter_add_set_property(hfi_client, cmd_buf, hfi_cmd, obj_id,
			hfi_payload_type, payload, payload_size, flags);
		if (rc)
			DSI_ERR("could not set property for hfi_cmd 0x%x\n", hfi_cmd);

		rc = hfi_adapter_set_cmd_buf(hfi_client, cmd_buf);
		SDE_EVT32(obj_id, hfi_cmd, rc, SDE_EVTLOG_FUNC_CASE3);
	}

	if (rc) {
		DSI_ERR("failed to send hfi_cmd 0x%x display_id: %d\n", hfi_cmd, obj_id);
		return rc;
	}

	return rc;
}

int dsi_display_hfi_register_pwr_supplies(struct dsi_display *display)
{
	struct dsi_display_hfi *display_hfi;
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	u32 obj_id, packet_id = 0;
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_POWER_REGISTER;
	int rc = 0;

	if (!display || !display->dsi_hfi_info) {
		DSI_ERR("invalid display\n");
		return -EINVAL;
	}

	display_hfi = display->dsi_hfi_info;

	obj_id = sde_conn_get_display_obj_id(display->drm_conn);

	cmd_buf = hfi_adapter_get_cmd_buf(display_hfi->hfi_client, obj_id,
					  HFI_CMDBUF_TYPE_DISPLAY_INFO_BLOCKING);

	rc = hfi_adapter_add_get_property(display_hfi->hfi_client, cmd_buf, hfi_cmd, obj_id,
			HFI_PAYLOAD_TYPE_NONE, NULL, 0, &display->hfi_cb_obj,
			HFI_HOST_FLAGS_RESPONSE_REQUIRED | HFI_HOST_FLAGS_NON_DISCARDABLE,
			true, &packet_id);

	rc = hfi_adapter_set_cmd_buf(display_hfi->hfi_client, cmd_buf);
	SDE_EVT32(obj_id, hfi_cmd, rc, SDE_EVTLOG_FUNC_CASE1);

	if (rc)
		DSI_ERR("Could not send HFI_COMMAND_DISPLAY_POWER_REGISTER, rc=%d\n", rc);

	return rc;
}

static void hfi_panel_get_mode_res_data(struct dsi_display_mode *mode,
					struct dsi_panel_timing_caps *timing_caps)
{
	timing_caps->res_data.active_width = mode->timing.h_active;
	timing_caps->res_data.active_height = mode->timing.v_active;
	timing_caps->res_data.h_front_porch = mode->timing.h_front_porch;
	timing_caps->res_data.h_back_porch = mode->timing.h_back_porch;
	timing_caps->res_data.h_sync_skew = mode->timing.h_skew;
	timing_caps->res_data.h_pulse_width = mode->timing.h_sync_width;
	timing_caps->res_data.v_front_porch = mode->timing.v_front_porch;
	timing_caps->res_data.v_back_porch = mode->timing.v_back_porch;
	timing_caps->res_data.v_pulse_width = mode->timing.v_sync_width;
}

static void hfi_panel_get_mode_compression_params(struct dsi_display_mode *mode,
						  struct dsi_panel_timing_caps *timing_caps)
{
	struct msm_display_dsc_info *dsc = &mode->priv_info->dsc;
	u32 v_major = dsc->config.dsc_version_major;
	u32 v_minor = dsc->config.dsc_version_minor;
	int rc;

	timing_caps->compression_params.mode = HFI_PANEL_COMPRESSION_DSC;
	timing_caps->compression_params.version = ((v_major & 0x0F) << 4) | (v_minor & 0x0F);
	timing_caps->compression_params.scr_version = dsc->scr_rev;
	timing_caps->compression_params.slice_height = dsc->config.slice_height;
	timing_caps->compression_params.slice_width = dsc->config.slice_width;
	timing_caps->compression_params.slices_per_pkt = dsc->slice_per_pkt;
	timing_caps->compression_params.bits_per_component = dsc->config.bits_per_component;
	timing_caps->compression_params.pps_delay_ms = dsc->pps_delay_ms;
	timing_caps->compression_params.bits_per_pixel = dsc->config.bits_per_pixel;
	timing_caps->compression_params.chroma_format = dsc->chroma_format;
	timing_caps->compression_params.color_space = dsc->source_color_space;
	timing_caps->compression_params.block_prediction_enable = dsc->config.block_pred_enable;

	if (dsc->rc_override_v1) {
		rc = sde_dsc_get_rc_params(dsc, (u8 *)&timing_caps->rc_override.min_qp[0],
					(u8 *)&timing_caps->rc_override.max_qp[0],
					(u8 *)&timing_caps->rc_override.offsets[0]);
		if (!rc)
			timing_caps->rc_override_enabled = true;
	}
}

static void hfi_panel_get_mode_esync_timing_caps(struct dsi_display *display,
						  struct dsi_display_mode *mode,
						  struct dsi_panel_timing_caps *timing_caps)
{
	if (!display || !display->panel ||
		!display->panel->esync_caps.esync_support)
		return;

	timing_caps->esync_timing_caps.esync_support =
		display->panel->esync_caps.esync_support;
	timing_caps->esync_timing_caps.emsync_fps =
		mode->priv_info->esync_params.emsync_fps;
	timing_caps->esync_timing_caps.emsync_milli_pulse_width =
		mode->priv_info->esync_params.emsync_milli_pulse_width;
	timing_caps->esync_timing_caps.esync_milli_skew =
		mode->priv_info->esync_params.milli_skew;
	timing_caps->esync_timing_caps.hsync_milli_pulse_width =
		mode->priv_info->esync_params.hsync_milli_pulse_width;
}

static void hfi_panel_get_mode_qsync_timing_params(struct dsi_display *display,
						    struct dsi_display_mode *mode,
						    struct dsi_panel_timing_caps *timing_caps)
{
	if (!display || !display->panel ||
		!display->panel->qsync_caps.qsync_support)
		return;

	timing_caps->qsync_timing_params.qsync_min_fps =
		mode->priv_info->qsync_min_fps;
	timing_caps->qsync_timing_params.avr_step_fps =
		mode->priv_info->avr_step_fps;
}

static enum hfi_panel_phy_type dsi_get_panel_type_helper(struct dsi_panel *panel)
{
	switch (panel->panel_type) {
	case DSI_DISPLAY_PANEL_TYPE_LCD:
		return HFI_PANEL_PHY_LCD;
	case DSI_DISPLAY_PANEL_TYPE_OLED:
		return HFI_PANEL_PHY_OLED;
	default:
		return HFI_PANEL_PHY_OLED;
	}
}

static enum hfi_panel_bpp dsi_get_panel_bpp_helper(struct dsi_panel *panel)
{
	switch (panel->host_config.dst_format) {
	case DSI_PIXEL_FORMAT_RGB111:
		return HFI_PANEL_BPP_3;
	case DSI_PIXEL_FORMAT_RGB332:
		return HFI_PANEL_BPP_8;
	case DSI_PIXEL_FORMAT_RGB444:
		return HFI_PANEL_BPP_12;
	case DSI_PIXEL_FORMAT_RGB565:
		return HFI_PANEL_BPP_16;
	case DSI_PIXEL_FORMAT_RGB666:
		return HFI_PANEL_BPP_18;
	case DSI_PIXEL_FORMAT_RGB888:
		return HFI_PANEL_BPP_24;
	case DSI_PIXEL_FORMAT_RGB101010:
		return HFI_PANEL_BPP_30;
	default:
		return HFI_PANEL_BPP_24;
	}
}

static enum hfi_panel_lane_enable dsi_get_panel_lane_state_helper(struct dsi_panel *panel)
{
	enum hfi_panel_lane_enable state = 0;

	if (panel->host_config.data_lanes & DSI_DATA_LANE_0)
		state |= HFI_PANEL_LANE_0;
	if (panel->host_config.data_lanes & DSI_DATA_LANE_1)
		state |= HFI_PANEL_LANE_1;
	if (panel->host_config.data_lanes & DSI_DATA_LANE_2)
		state |= HFI_PANEL_LANE_2;
	if (panel->host_config.data_lanes & DSI_DATA_LANE_3)
		state |= HFI_PANEL_LANE_3;

	return state;
}

static enum hfi_panel_color_order_type dsi_get_panel_color_order_type(struct dsi_panel *panel)
{
	switch (panel->host_config.swap_mode) {
	case DSI_COLOR_SWAP_RGB:
		return HFI_PANEL_COLOR_ORDER_RGB_SWAP_RGB;
	case DSI_COLOR_SWAP_RBG:
		return HFI_PANEL_COLOR_ORDER_RGB_SWAP_RBG;
	case DSI_COLOR_SWAP_BRG:
		return HFI_PANEL_COLOR_ORDER_RGB_SWAP_BRG;
	case DSI_COLOR_SWAP_GRB:
		return HFI_PANEL_COLOR_ORDER_RGB_SWAP_GRB;
	case DSI_COLOR_SWAP_GBR:
		return HFI_PANEL_COLOR_ORDER_RGB_SWAP_GBR;
	default:
		return HFI_PANEL_COLOR_ORDER_RGB_SWAP_RGB;
	}
}

static enum hfi_panel_trigger_type dsi_get_panel_trigger_type_helper(enum dsi_trigger_type type)
{
	switch (type) {
	case DSI_TRIGGER_NONE:
		return HFI_PANEL_TRIGGER_NONE;
	case DSI_TRIGGER_TE:
		return HFI_PANEL_TRIGGER_TE;
	case DSI_TRIGGER_SEOF:
		return HFI_PANEL_TRIGGER_SEOF;
	case DSI_TRIGGER_SW:
		return HFI_PANEL_TRIGGER_SW;
	case DSI_TRIGGER_SW_SEOF:
		return HFI_PANEL_TRIGGER_SW_SEOF;
	case DSI_TRIGGER_SW_TE:
		return HFI_PANEL_TRIGGER_SW_TE;
	default:
		return HFI_PANEL_TRIGGER_SW;
	}
}

enum hfi_panel_esd_status_mode dsi_get_esd_status_mode_helper(enum esd_check_status_mode mode)
{
	switch (mode) {
	case ESD_MODE_REG_READ:
		return HFI_PANEL_ESD_STATUS_MODE_REG_READ;
	case ESD_MODE_SW_BTA:
		return HFI_PANEL_ESD_STATUS_MODE_SW_BTA;
	case ESD_MODE_PANEL_TE:
		return HFI_PANEL_ESD_STATUS_MODE_PANEL_TE;
	case ESD_MODE_PANEL_RW:
		return HFI_PANEL_ESD_STATUS_MODE_PANEL_RW;
	case ESD_MODE_SW_SIM_SUCCESS:
		return HFI_PANEL_ESD_STATUS_MODE_SW_SIM_SUCCESS;
	case ESD_MODE_SW_SIM_FAILURE:
		return HFI_PANEL_ESD_STATUS_MODE_SW_SIM_FAILURE;
	default:
		return HFI_PANEL_ESD_STATUS_MODE_SW_SIM_SUCCESS;
	}
}

static void dsi_get_panel_esd_config_helper(struct dsi_display *display,
	struct hfi_panel_esd_config *esd_config)
{
	struct hfi_shared_addr_map *addr_map;
	struct hfi_dsi_cmd_desc *cmd_desc;
	struct dsi_cmd_desc *cmds;
	u8 *local_addr_ptr;
	u64 remote_addr_ptr;
	u32 status_len = 0;
	u32 *lenp;

	esd_config->size = sizeof(struct hfi_panel_esd_config);
	esd_config->status_mode = dsi_get_esd_status_mode_helper(
			display->panel->esd_config.status_mode);

	if (esd_config->status_mode == HFI_PANEL_ESD_STATUS_MODE_REG_READ) {

		addr_map = display->dsi_hfi_info->esd_addr_map;
		if (!addr_map || !addr_map->local_addr || !addr_map->remote_addr) {
			DSI_ERR("Invalid ESD address map\n");
			return;
		}

		esd_config->groups = display->panel->esd_config.groups;
		esd_config->count = display->panel->esd_config.status_cmd.count;

		lenp = display->panel->esd_config.status_valid_params ?:
					display->panel->esd_config.status_cmds_rlen;

		cmd_desc = (struct hfi_dsi_cmd_desc *)addr_map->local_addr;
		local_addr_ptr = (u8 *)addr_map->local_addr + sizeof(struct hfi_dsi_cmd_desc);
		remote_addr_ptr = (u64)addr_map->remote_addr + sizeof(struct hfi_dsi_cmd_desc);

		cmds = display->panel->esd_config.status_cmd.cmds;

		/* Populate command descriptor for ESD commands.*/
		for (int i = 0 ; i < display->panel->esd_config.status_cmd.count ; i++) {
			cmd_desc->tx_len =       cmds[i].msg.tx_len;
			cmd_desc->type =         cmds[i].msg.type;
			cmd_desc->flags =        cmds[i].msg.flags | MIPI_DSI_MSG_UNICAST_COMMAND;
			cmd_desc->ctrl_idx =     cmds[i].ctrl;
			cmd_desc->channel =      cmds[i].msg.channel;
			cmd_desc->last_command = cmds[i].last_command;
			cmd_desc->post_wait_ms = cmds[i].post_wait_ms;
			cmd_desc->ctrl_flags =   cmds[i].ctrl_flags | DSI_CTRL_CMD_READ;
			cmd_desc->rx_len =       display->panel->esd_config.status_cmds_rlen[i];
			cmd_desc->tx_buff_addr_lsb = HFI_VAL_L32(remote_addr_ptr);
			cmd_desc->tx_buff_addr_msb = HFI_VAL_H32(remote_addr_ptr);
			memcpy(local_addr_ptr, cmds[i].msg.tx_buf, cmds[i].msg.tx_len);

			cmd_desc = (struct hfi_dsi_cmd_desc *)(local_addr_ptr + cmds[i].msg.tx_len);
			local_addr_ptr += cmds[i].msg.tx_len + sizeof(struct hfi_dsi_cmd_desc);
			remote_addr_ptr += cmds[i].msg.tx_len + sizeof(struct hfi_dsi_cmd_desc);

			status_len += lenp[i];
		}

		esd_config->hfi_dsi_cmd_desc_lsb = HFI_VAL_L32((u64)addr_map->remote_addr);
		esd_config->hfi_dsi_cmd_desc_msb = HFI_VAL_H32((u64)addr_map->remote_addr);

		/* Populate ESD status values. */
		local_addr_ptr = (u8 *)cmd_desc;
		remote_addr_ptr = (u64)addr_map->remote_addr +
			((u64)cmd_desc - (u64)addr_map->local_addr);

		memcpy(local_addr_ptr, display->panel->esd_config.status_value,
				status_len * sizeof(u32) * display->panel->esd_config.groups);

		esd_config->status_values_lsb =	HFI_VAL_L32(remote_addr_ptr);
		esd_config->status_values_msb =	HFI_VAL_H32(remote_addr_ptr);

		/* Populate ESD valid params. */
		local_addr_ptr = local_addr_ptr +
				(status_len * sizeof(u32) * display->panel->esd_config.groups);
		remote_addr_ptr = remote_addr_ptr +
				(status_len * sizeof(u32) * display->panel->esd_config.groups);

		if (display->panel->esd_config.status_valid_params) {
			memcpy(local_addr_ptr,  display->panel->esd_config.status_valid_params,
					display->panel->esd_config.status_cmd.count * sizeof(u32));

			esd_config->valid_params_lsb = HFI_VAL_L32(remote_addr_ptr);
			esd_config->valid_params_msb = HFI_VAL_H32(remote_addr_ptr);
		}
	}
}

static enum hfi_panel_fps_traffic_mode dsi_get_panel_traffic_mode_helper(struct dsi_panel *panel)
{
	switch (panel->video_config.traffic_mode) {
	case DSI_VIDEO_TRAFFIC_SYNC_PULSES:
		return HFI_PANEL_TRAFFIC_NON_BURST_SYNC_PULSE_MODE;
	case DSI_VIDEO_TRAFFIC_SYNC_START_EVENTS:
		return HFI_PANEL_TRAFFIC_NON_BURST_SYNC_EVENT_MODE;
	case DSI_VIDEO_TRAFFIC_BURST_MODE:
		return HFI_PANEL_TRAFFIC_BURST_MODE;
	default:
		return HFI_PANEL_TRAFFIC_NON_BURST_SYNC_PULSE_MODE;
	}
}

static enum hfi_panel_modes dsi_get_panel_op_mode_helper(struct dsi_panel *panel)
{
	enum hfi_panel_modes mode = 0;
	switch (panel->panel_mode) {
	case DSI_OP_VIDEO_MODE:
		mode = HFI_PANEL_VIDEO_MODE_8_BIT;
		break;
	case DSI_OP_CMD_MODE:
		mode = HFI_PANEL_CMD_MODE_8_BIT;
		break;
	default:
		mode = HFI_PANEL_VIDEO_MODE_8_BIT;
	}

	if (panel->vrr_caps.video_psr_support)
		mode = HFI_PANEL_VIDEO_MODE_PSR_8_BIT;

	return mode;
}

static enum hfi_panel_display_type dsi_get_display_type_helper(struct dsi_display *display)
{
	if (!display || !display->display_type)
		return HFI_PANEL_DISPLAY_TYPE_NONE;

	if (!strcmp(display->display_type, "primary"))
		return HFI_PANEL_DISPLAY_TYPE_BUILT_IN_0;
	else if (!strcmp(display->display_type, "secondary"))
		return HFI_PANEL_DISPLAY_TYPE_BUILT_IN_1;
	else
		return HFI_PANEL_DISPLAY_TYPE_BUILT_IN_2;
}

static enum hfi_panel_vsync_source dsi_get_panel_vsync_src(struct dsi_display *display)
{
	if (display->panel->te_using_watchdog_timer)
		return HFI_PANEL_VSYNC_SOURCE_WD;
	else
		return (enum hfi_panel_vsync_source)display->te_source;
}

static enum hfi_panel_lane_map dsi_get_panel_lane_map_helper(struct dsi_panel *panel)
{
	return HFI_PANEL_LANE_MAP_0123;
}

static void dsi_get_panel_ctrl_nums_helper(struct dsi_display *display, u32 *ctrl_array)
{
	char *dsi_ctrl_name;
	int cnt, i;

	if (!strcmp(display->display_type, "primary"))
		dsi_ctrl_name = "qcom,dsi-ctrl-num";
	else
		dsi_ctrl_name = "qcom,dsi-sec-ctrl-num";

	cnt = dsi_display_get_phandle_count(display,
					dsi_ctrl_name);
	ctrl_array[0] = cnt;

	for (i = 0; i < cnt; i++)
		ctrl_array[i+1] = dsi_display_get_phandle_index(display, dsi_ctrl_name, cnt, i);
}

static void dsi_get_panel_phy_nums_helper(struct dsi_display *display, u32 *phy_array)
{
	char *dsi_phy_name;
	int cnt, i;

	if (!strcmp(display->display_type, "primary"))
		dsi_phy_name = "qcom,dsi-phy-num";
	else
		dsi_phy_name = "qcom,dsi-sec-phy-num";

	cnt = dsi_display_get_phandle_count(display,
					dsi_phy_name);
	phy_array[0] = cnt;

	for (i = 0; i < cnt; i++)
		phy_array[i+1] = dsi_display_get_phandle_index(display, dsi_phy_name, cnt, i);
}

static enum hfi_panel_backlight_ctrl dsi_get_panel_backlight_type(struct dsi_panel *panel,
									char *panel_type)
{
	char *bl_name = NULL;
	const char *bl_type = NULL;
	struct dsi_parser_utils *utils = &panel->utils;

	if (!strcmp(panel_type, "primary"))
		bl_name = "qcom,mdss-dsi-bl-pmic-control-type";
	else
		bl_name = "qcom,mdss-dsi-sec-bl-pmic-control-type";

	bl_type = utils->get_property(utils->data, bl_name, NULL);
	if (!bl_type)
		return HFI_PANEL_BACKLIGHT_CTRL_UNKNOWN;
	else if (!strcmp(bl_type, "bl_ctrl_pwm"))
		return HFI_PANEL_BACKLIGHT_CTRL_PWM;
	else if (!strcmp(bl_type, "bl_ctrl_wled"))
		return HFI_PANEL_BACKLIGHT_CTRL_WLED;
	else if (!strcmp(bl_type, "bl_ctrl_dcs"))
		return HFI_PANEL_BACKLIGHT_CTRL_DCS;
	else if (!strcmp(bl_type, "bl_ctrl_external"))
		return HFI_PANEL_BACKLIGHT_CTRL_EXTERNAL;
	else
		return HFI_PANEL_BACKLIGHT_CTRL_UNKNOWN;
}

static void dsi_hfi_populate_esync_caps(struct dsi_panel *panel,
					struct hfi_panel_esync_caps *hfi_esync_caps)
{
	if (!panel || !hfi_esync_caps) {
		DSI_ERR("null pointer");
		return;
	}

	struct esync_params *eparams = &panel->esync_caps.default_esync_params;

	hfi_esync_caps->esync_support = panel->esync_caps.esync_support;
	hfi_esync_caps->emsync_fps = eparams->emsync_fps;
	hfi_esync_caps->emsync_milli_pulse_width = eparams->emsync_milli_pulse_width;
	hfi_esync_caps->esync_milli_skew = eparams->milli_skew;
	hfi_esync_caps->hsync_milli_pulse_width = eparams->hsync_milli_pulse_width;
}

static void dsi_hfi_populate_dfps_caps(struct dsi_panel *panel,
					struct hfi_panel_dfps_caps *hfi_dfps_caps)
{
	if (!panel || !hfi_dfps_caps) {
		DSI_ERR("null pointer");
		return;
	}

	hfi_dfps_caps->dfps_support = panel->dfps_caps.dfps_support;
	hfi_dfps_caps->min_refresh_rate = panel->dfps_caps.min_refresh_rate;
	hfi_dfps_caps->max_refresh_rate = panel->dfps_caps.max_refresh_rate;
	hfi_dfps_caps->type = (enum hfi_panel_dfps_type)panel->dfps_caps.type;
}

static void dsi_hfi_populate_poms_caps(struct dsi_panel *panel,
					struct hfi_panel_operating_mode_caps *hfi_poms_caps)
{
	if (!panel || !hfi_poms_caps) {
		DSI_ERR("null pointer");
		return;
	}

	hfi_poms_caps->panel_mode_switch_enabled = panel->panel_mode_switch_enabled;
	hfi_poms_caps->vsync_aligned_switch = panel->poms_align_vsync;
}

/**
 * dsi_hfi_fill_dcs_cmds_sde_region_precheck() - pre-validate that a set of DCS commands fits within
 *                                               a given SDE buffer region
 * @cmds:           array of dsi_cmd_desc to validate
 * @count:          number of commands in the array
 * @current_offset: current write position within the SDE buffer (bytes used)
 * @region_end:     byte offset marking the exclusive end of the allowed region
 *
 * Calculates the total 8-byte-aligned SDE buffer space needed for all commands
 * and verifies they fit within [current_offset, region_end).
 *
 * Must be called BEFORE writing any commands to the SDE buffer so that either
 * all commands are written or none — preventing partial writes that would leave
 * the buffer in an inconsistent state.
 *
 * Return: 0 if the command set fits, -EINVAL if it would overflow, or a
 *         negative error code if packet creation fails.
 */
static int dsi_hfi_fill_dcs_cmds_sde_region_precheck(struct dsi_cmd_desc *cmds,
						      u32 count,
						      u32 current_offset,
						      u32 region_end)
{
	u32 total_size = 0;
	int k, rc;

	for (k = 0; k < count; k++) {
		struct mipi_dsi_packet pkt;
		u32 aligned_size;

		rc = mipi_dsi_create_packet(&pkt, &cmds[k].msg);
		if (rc) {
			DSI_ERR("failed to create packet for size check cmd %d, rc=%d\n",
				k, rc);
			return rc;
		}
		aligned_size = (((pkt.size + 7) >> 3) << 3);
		total_size += aligned_size;
	}

	if (current_offset + total_size > region_end) {
		DSI_ERR("SDE region overflow: cmds need %u bytes at offset %u, region ends at %u\n",
			total_size, current_offset, region_end);
		return -EINVAL;
	}

	return 0;
}

static int hfi_panel_fill_dcs_cmds_sub(struct dsi_display *display,
				struct dsi_panel_cmd_set *cmd_set,
				void **sde_vaddr, void **hfi_vaddr)
{
	struct dsi_hfi_panel_cmd_info *panel_cmd_info = *hfi_vaddr;
	struct dsi_display_hfi *dsi_hfi;
	int i, rc = 0;
	u32 size_of_indv_cmd;
	size_t hfi_map_size;

	dsi_hfi = display->dsi_hfi_info;
	if (!dsi_hfi || !dsi_hfi->shared_addr_map) {
		DSI_ERR("failed to get DSI HFI\n");
		return -EINVAL;
	}

	hfi_map_size = dsi_hfi->shared_addr_map->size;

	/* Ensure DT DCS command metadata does not overflow the HFI shared buffer */
	if (dsi_hfi->running_hfi_offset + (sizeof(struct dsi_hfi_panel_cmd_info)
			* cmd_set->count) > hfi_map_size) {
		DSI_ERR("over HFI mapped buffer size: needed=%zu, available=%zu, total=%zu\n",
			sizeof(struct dsi_hfi_panel_cmd_info) * cmd_set->count,
			hfi_map_size - dsi_hfi->running_hfi_offset, hfi_map_size);
		return -EINVAL;
	}

	/*
	 * Pre-validate that all commands fit within the DT command SDE region
	 * before writing any of them, preventing partial writes.
	 * DT commands are bounded to: [0, DSI_TX_CMD_BUF_DT_CMD_SIZE)
	 */
	rc = dsi_hfi_fill_dcs_cmds_sde_region_precheck(
			cmd_set->cmds, cmd_set->count,
			dsi_hfi->running_sde_offset,
			DSI_TX_CMD_BUF_DT_CMD_SIZE);
	if (rc) {
		DSI_ERR("DT cmd set SDE region precheck failed, rc=%d\n", rc);
		goto error;
	}

	for (i = 0; i < cmd_set->count; i++) {
		size_of_indv_cmd = 0;

		panel_cmd_info->delay = cmd_set->cmds[i].post_wait_ms;
		panel_cmd_info->ctrl_flags = cmd_set->cmds[i].ctrl_flags;
		panel_cmd_info->reserved1 = cmd_set->cmds[i].msg.flags;
		panel_cmd_info->mode = cmd_set->state;

		rc = dsi_hfi_packetize_panel_cmd(&cmd_set->cmds[i], &size_of_indv_cmd, *sde_vaddr);
		if (rc) {
			DSI_ERR("failed to packetize command %d, rc=%d\n", i, rc);
			goto error;
		}

		panel_cmd_info->size = size_of_indv_cmd;

		size_of_indv_cmd = (((size_of_indv_cmd+7)>>3)<<3);
		*sde_vaddr += size_of_indv_cmd;

		panel_cmd_info->cmd_offset = dsi_hfi->running_sde_offset + display->cmd_buffer_iova;

		dsi_hfi->running_sde_offset += size_of_indv_cmd;

		panel_cmd_info++;
		dsi_hfi->running_hfi_offset += sizeof(struct dsi_hfi_panel_cmd_info);
	}

	*hfi_vaddr += (sizeof(struct dsi_hfi_panel_cmd_info) * cmd_set->count);

error:
	return rc;
}

static int hfi_panel_fill_dcs_cmds(struct dsi_display *display,
				struct dsi_display_mode_priv_info *priv_info,
				struct dsi_panel_timing_caps *panel_timing_caps,
				void **sde_vaddr, void **hfi_vaddr)
{
	struct dsi_display_hfi *dsi_hfi;
	int i;
	int j = 0;
	int rc = 0;
	int cmd_type;

	dsi_hfi = display->dsi_hfi_info;
	if (!dsi_hfi) {
		DSI_ERR("Failed to get DSI HFI\n");
		return -EINVAL;
	}

	for (i = 0; i < DSI_CMD_SET_TOTAL_SIZE; i++) {
		cmd_type = priv_info->cmd_sets[i].type;
		if (cmd_type == DSI_CMD_SET_PPS || cmd_type == DSI_CMD_SET_ROI)
			continue;

		if (!priv_info->cmd_sets[i].count)
			continue;

		if (j >= MAX_ALLOWED_DCS_CMD_TYPES) {
			DSI_ERR("DCS cmd type count exceeds HFI dsize 8-bit limit (%d)\n",
				MAX_ALLOWED_DCS_CMD_TYPES);
			return -EINVAL;
		}

		panel_timing_caps->payload.hfi_per_type_array[j].hfi_buff_struct_offset =
							dsi_hfi->running_hfi_offset;
		panel_timing_caps->payload.hfi_per_type_array[j].sde_buff_type_offset =
							dsi_hfi->running_sde_offset;
		panel_timing_caps->payload.hfi_per_type_array[j].cmd_type = cmd_type;
		panel_timing_caps->payload.hfi_per_type_array[j].count_cmds =
							priv_info->cmd_sets[i].count;

		rc = hfi_panel_fill_dcs_cmds_sub(display, &priv_info->cmd_sets[i],
				sde_vaddr, hfi_vaddr);
		if (rc)
			DSI_ERR("Failed to fill panel cmds into memory for cmd type %d", i);

		j++;
	}

	/*
	 * Advance tx_cmd_buf_fill_level past the fixed reserved regions for DT
	 * commands and runtime custom DCS commands. This informs DCP that the free
	 * buffer space (available for its own use) starts after both reserved regions.
	 */
	dsi_hfi->tx_cmd_buf_fill_level = DSI_TX_CMD_BUF_DT_CMD_SIZE +
					  DSI_RUNTIME_CUSTOM_DCS_CMD_RESERVED_SIZE;
	panel_timing_caps->payload.count = j;

	return 0;
}

int dsi_hfi_host_transfer_sub(struct mipi_dsi_host *host, struct dsi_cmd_desc *cmd)
{
	struct dsi_display *display = to_dsi_display(host);
	struct dsi_display_hfi *display_hfi;
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;
	struct hfi_client_t *hfi_client;
	struct hfi_dsi_cmd_desc *dsi_cmd_desc;
	struct hfi_shared_addr_map *tx_cmd_buf_map;
	struct hfi_shared_addr_map *rx_cmd_buf_map;
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_TRANSFER_DCS_CMD;
	int rc = 0;
	size_t mem_size = 0;
	size_t mem_size_rx = 0;
	if (!display || !display->dsi_hfi_info || !cmd || !cmd->msg.tx_buf) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	sde_kms = sde_connector_get_kms(display->drm_conn);
	if (!sde_kms)
		return -EINVAL;

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms)
		return -EINVAL;

	if (atomic_read(&display->panel->esd_recovery_pending))
		return 0;

	hfi_client = &hfi_kms->hfi_client;

	display_hfi = display->dsi_hfi_info;

	/* Get the shared address map for command payload transfer between host and DCP */
	tx_cmd_buf_map = &display_hfi->tx_cmd_buf_map;

	mem_size = hfi_adapter_get_shared_mem_allocated_size(hfi_client, tx_cmd_buf_map);

	if (!mem_size) {
		tx_cmd_buf_map->size = SZ_4K;
		rc = hfi_adapter_buffer_alloc(hfi_client, tx_cmd_buf_map);

		if (rc || !hfi_adapter_get_shared_mem_allocated_size(hfi_client, tx_cmd_buf_map)) {
			DSI_ERR("failed to allocate HFI buffer for command payload\n");
			return -ENOMEM;
		}

		mem_size = hfi_adapter_get_shared_mem_allocated_size(hfi_client, tx_cmd_buf_map);
	}

	if (cmd->msg.tx_len > mem_size) {
		DSI_ERR("command payload (%zu bytes) is larger than (%zu bytes)\n", cmd->msg.tx_len,
			mem_size);
		return -EINVAL;
	}

	if (cmd->ctrl_flags & DSI_CTRL_CMD_READ) {
		rx_cmd_buf_map = &display_hfi->rx_cmd_buf_map;
		mem_size_rx = hfi_adapter_get_shared_mem_allocated_size(hfi_client, rx_cmd_buf_map);

		if (!mem_size_rx) {
			rx_cmd_buf_map->size = SZ_4K;
			rc = hfi_adapter_buffer_alloc(hfi_client, rx_cmd_buf_map);

			if (rc || !hfi_adapter_get_shared_mem_allocated_size(hfi_client, rx_cmd_buf_map)) {
				DSI_ERR("failed to allocate HFI buffer for receiving payload\n");
				return -ENOMEM;
			}

			mem_size_rx = hfi_adapter_get_shared_mem_allocated_size(hfi_client, rx_cmd_buf_map);
		}

		if (cmd->msg.rx_len > mem_size_rx) {
			DSI_ERR("Receiving payload (%zu bytes) is larger than (%zu bytes)\n",
				cmd->msg.rx_len, mem_size_rx);
			return -EINVAL;
		}

		memset(rx_cmd_buf_map->local_addr, 0, rx_cmd_buf_map->size);
	}

	dsi_cmd_desc = kzalloc(sizeof(struct hfi_dsi_cmd_desc), GFP_KERNEL);
	if (!dsi_cmd_desc) {
		DSI_ERR("failed to allocate memory for hfi_dsi_cmd_desc\n");
		return -ENOMEM;
	}

	/* Populate dsi_cmd_desc */
	dsi_cmd_desc->tx_len = cmd->msg.tx_len;
	dsi_cmd_desc->type = cmd->msg.type;
	dsi_cmd_desc->flags = cmd->msg.flags;
	dsi_cmd_desc->ctrl_idx = cmd->ctrl;
	dsi_cmd_desc->channel = cmd->msg.channel;
	dsi_cmd_desc->last_command = cmd->last_command;
	dsi_cmd_desc->post_wait_ms = cmd->post_wait_ms;
	dsi_cmd_desc->ctrl_flags = cmd->ctrl_flags;
	dsi_cmd_desc->tx_buff_addr_lsb = HFI_VAL_L32((u64)tx_cmd_buf_map->remote_addr);
	dsi_cmd_desc->tx_buff_addr_msb = HFI_VAL_H32((u64)tx_cmd_buf_map->remote_addr);
	if (cmd->ctrl_flags & DSI_CTRL_CMD_READ) {
		dsi_cmd_desc->rx_len = cmd->msg.rx_len;
		dsi_cmd_desc->rx_buff_addr_lsb = HFI_VAL_L32((u64)rx_cmd_buf_map->remote_addr);
		dsi_cmd_desc->rx_buff_addr_msb = HFI_VAL_H32((u64)rx_cmd_buf_map->remote_addr);
	}

	/* Copy command payload to HFI buffer */
	memcpy(tx_cmd_buf_map->local_addr, cmd->msg.tx_buf, cmd->msg.tx_len);

	rc = dsi_display_hfi_send_cmd_buf(display, hfi_client, hfi_cmd, display->display_type,
			HFI_PAYLOAD_TYPE_U32_ARRAY, dsi_cmd_desc, sizeof(struct hfi_dsi_cmd_desc),
			(HFI_HOST_FLAGS_RESPONSE_REQUIRED | HFI_HOST_FLAGS_NON_DISCARDABLE));
	if (rc)
		DSI_ERR("Could not send HFI_COMMAND_DISPLAY_TRANSFER_DCS_CMD, rc=%d\n", rc);

	if ((!rc) && (cmd->ctrl_flags & DSI_CTRL_CMD_READ)) {
		if (cmd->msg.rx_buf) {
			memcpy(cmd->msg.rx_buf, rx_cmd_buf_map->local_addr, cmd->msg.rx_len);
		} else {
			DSI_ERR("rx buffer is NULL but rx_len is non-zero\n");
			return -EINVAL;
		}
	}

	kfree(dsi_cmd_desc);
	return rc;
}

int dsi_hfi_add_dsi_cmd_remap(struct dsi_display *display,
		u32 *cmd_remap_table, u32 table_size, bool resp_req)
{
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;
	struct hfi_client_t *hfi_client;
	struct dsi_display_mode_priv_info *priv_info;
	struct dsi_hfi_cmd_set_remap_payload *hfi_remap_payload = NULL;
	u32 hfi_payload_size;
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_DSI_CUSTOM_DCS_CMDS_SET_REMAP;
	u32 flags = HFI_HOST_FLAGS_NON_DISCARDABLE;
	u32 obj_id, i, hfi_remap_count = 0;
	int rc = 0;

	if (resp_req)
		flags |= HFI_HOST_FLAGS_RESPONSE_REQUIRED;

	if (!display || !display->dsi_hfi_info || !display->drm_conn) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (!cmd_remap_table || table_size != DSI_CMD_SET_MAX) {
		DSI_ERR("Invalid cmd_remap_table: ptr=%p, table_size=%d, expected=%d\n",
			cmd_remap_table, table_size, DSI_CMD_SET_MAX);
		return -EINVAL;
	}

	/* Validate panel and mode configuration */
	if (!display->panel || !display->panel->cur_mode || !display->panel->cur_mode->priv_info) {
		DSI_ERR("Invalid panel or mode configuration\n");
		return -EINVAL;
	}

	priv_info = display->panel->cur_mode->priv_info;
	obj_id = sde_conn_get_display_obj_id(display->drm_conn);

	/*
	 * Validate each entry and count valid mappings.
	 * Entries set to DSI_CMD_SET_MAX are treated as "no remap" markers.
	 */
	for (i = 0; i < DSI_CMD_SET_MAX; i++) {
		u32 custom_cmd_type = cmd_remap_table[i];
		int custom_idx;

		if (custom_cmd_type == DSI_CMD_SET_MAX)
			continue;

		/* Validate custom_cmd_type is within valid range */
		if (custom_cmd_type >= DSI_CUSTOM_CMD_SET_MAX) {
			DSI_ERR("Entry %u: custom_cmd_type=%u out of range (max=%u)\n",
				i, custom_cmd_type, DSI_CUSTOM_CMD_SET_MAX);
			return -EINVAL;
		}

		/*
		 * Validate custom command is in the custom range OR
		 * equals the standard command (pointing back to original mapping)
		 */
		if (custom_cmd_type < DSI_CUSTOM_CMD_SET_START_IDX && custom_cmd_type != i) {
			DSI_ERR("Entry %u: custom_cmd_type=%u must be >= %u or = to cmd_type=%u\n",
				i, custom_cmd_type, DSI_CUSTOM_CMD_SET_START_IDX, i);
			return -EINVAL;
		}

		/* Validate command set at custom_cmd_type exists and is non-empty */
		custom_idx = dsi_cmd_type_to_index(custom_cmd_type);
		if (custom_idx < 0 || custom_idx >= DSI_CMD_SET_TOTAL_SIZE) {
			DSI_ERR("Entry %u: invalid custom_idx=%d for custom_cmd_type=%u\n",
				i, custom_idx, custom_cmd_type);
			return -EINVAL;
		}

		if (!priv_info->cmd_sets[custom_idx].count) {
			DSI_ERR("Entry %u: empty cmd set at custom_idx=%d for custom_cmd_type=%u\n",
				i, custom_idx, custom_cmd_type);
			return -EINVAL;
		}

		hfi_remap_count++;
	}

	SDE_EVT32(obj_id, hfi_cmd, hfi_remap_count, resp_req, SDE_EVTLOG_FUNC_CASE1);
	if (!hfi_remap_count) {
		DSI_INFO("No valid DSI cmd remap entries found\n");
		return 0;
	}

	hfi_payload_size = sizeof(u32) + (hfi_remap_count * sizeof(struct hfi_cmd_set_remap));
	hfi_remap_payload = kzalloc(hfi_payload_size, GFP_KERNEL);
	if (!hfi_remap_payload) {
		DSI_ERR("Failed to allocate HFI remap payload\n");
		return -ENOMEM;
	}

	/* Build HFI payload from cmd_remap_table */
	hfi_remap_payload->count = hfi_remap_count;
	hfi_remap_count = 0;
	for (i = 0; i < DSI_CMD_SET_MAX; i++) {
		if (cmd_remap_table[i] != DSI_CMD_SET_MAX) {
			hfi_remap_payload->entries[hfi_remap_count].cmd_type = i;
			hfi_remap_payload->entries[hfi_remap_count].custom_cmd_type =
				cmd_remap_table[i];
			SDE_EVT32(obj_id, i, cmd_remap_table[i], SDE_EVTLOG_FUNC_CASE2);
			DSI_DEBUG("DSI cmd remap: cmd_type=%u remapped to custom_cmd_type=%u\n",
				i, cmd_remap_table[i]);
			hfi_remap_count++;
		}
	}

	/* Get KMS and HFI client */
	sde_kms = sde_connector_get_kms(display->drm_conn);
	if (!sde_kms) {
		DSI_ERR("Failed to get sde_kms\n");
		rc = -EINVAL;
		goto cleanup;
	}

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms) {
		DSI_ERR("Failed to get hfi_kms\n");
		rc = -EINVAL;
		goto cleanup;
	}

	hfi_client = &hfi_kms->hfi_client;

	/* Send the HFI payload to firmware */
	SDE_EVT32(obj_id, hfi_cmd, hfi_remap_count, SDE_EVTLOG_FUNC_CASE3);
	rc = dsi_display_hfi_send_cmd_buf(display, hfi_client, hfi_cmd, display->display_type,
			HFI_PAYLOAD_TYPE_U32_ARRAY, hfi_remap_payload, hfi_payload_size,
			flags);
	SDE_EVT32(obj_id, hfi_cmd, rc, SDE_EVTLOG_FUNC_CASE4);
	if (rc)
		DSI_ERR("Could not send HFI_COMMAND_DISPLAY_DSI_CUSTOM_DCS_CMDS_SET_REMAP, rc=%d\n",
				rc);

cleanup:
	kfree(hfi_remap_payload);
	return rc;
}

int dsi_hfi_add_rt_custom_dcs_cmd(struct dsi_display *display,
				      enum dsi_cmd_set_type cmd_type,
				      const u8 *data, u32 length,
				      enum dsi_cmd_set_state state)
{
	struct dsi_display_hfi *dsi_hfi;
	struct dsi_display_mode_priv_info *priv_info;
	struct dsi_rt_custom_dcs_cmd_entry *cmd_entry;
	struct dsi_hfi_panel_cmd_info *cmd_info;
	struct dsi_rt_custom_dcs_cmd_entry saved_index_entry;
	struct dsi_cmd_desc *cmds = NULL;
	u8 *sde_vaddr;
	u32 packet_count = 0;
	u32 size_of_indv_cmd;
	u32 aligned_size;
	u32 hfi_needed;
	u32 cached_sde_running;
	u32 cached_hfi_running;
	size_t hfi_remaining;
	int std_idx;
	int i, rc = 0;

	if (!display || !data || !length) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (cmd_type >= DSI_CMD_SET_MAX) {
		DSI_ERR("Invalid cmd_type %d, must be < DSI_CMD_SET_MAX (%d)\n",
			cmd_type, DSI_CMD_SET_MAX);
		return -EINVAL;
	}

	/* Validate panel and mode configuration */
	if (!display->panel || !display->panel->cur_mode || !display->panel->cur_mode->priv_info) {
		DSI_ERR("Invalid panel or mode configuration\n");
		return -EINVAL;
	}

	priv_info = display->panel->cur_mode->priv_info;

	dsi_hfi = display->dsi_hfi_info;
	if (!dsi_hfi || !dsi_hfi->shared_addr_map) {
		DSI_ERR("HFI not initialized\n");
		return -EINVAL;
	}

	SDE_EVT32(cmd_type, length, state, SDE_EVTLOG_FUNC_ENTRY);

	/* Reject if the standard command type is not defined in DT */
	std_idx = dsi_cmd_type_to_index(cmd_type);
	if (std_idx < 0 || std_idx >= DSI_CMD_SET_TOTAL_SIZE) {
		DSI_ERR("cmd_type %u: invalid index %d\n", cmd_type, std_idx);
		return -EINVAL;
	}

	if (!priv_info->cmd_sets[std_idx].count) {
		DSI_ERR("cmd_type %u not defined in DT, cannot add rt custom DCS cmd\n",
			cmd_type);
		return -EINVAL;
	}

	/* Count packets in the raw DT-format byte stream */
	rc = dsi_panel_get_cmd_pkt_count(data, length, &packet_count);
	if (rc || !packet_count) {
		DSI_ERR("Failed to count DCS cmd packets rc=%d count=%d\n", rc, packet_count);
		return rc ? rc : -EINVAL;
	}

	SDE_EVT32(cmd_type, packet_count, SDE_EVTLOG_FUNC_CASE1);

	/* Allocate array of dsi_cmd_desc, one per packet */
	cmds = kcalloc(packet_count, sizeof(*cmds), GFP_KERNEL);
	if (!cmds)
		return -ENOMEM;

	/* Parse raw bytes into dsi_cmd_desc structs (same as DT parsing) */
	rc = dsi_panel_create_cmd_packets(data, length, packet_count, cmds);
	if (rc) {
		DSI_ERR("Failed to create DCS cmd packets rc=%d\n", rc);
		goto destroy_cmds;
	}

	/* Acquire the lock before touching any shared rt_custom_dcs_cmd_* state. */
	mutex_lock(&dsi_hfi->rt_dcs_cmd_lock);

	cmd_entry = &dsi_hfi->rt_custom_dcs_cmd_map[cmd_type];

	if (cmd_entry->valid) {
		DSI_DEBUG("Overriding existing rt custom DCS cmd for type %d\n", cmd_type);
		SDE_EVT32(cmd_type, cmd_entry->sde_offset, cmd_entry->hfi_meta_offset,
			  SDE_EVTLOG_FUNC_CASE2);
	}

	/* Check the remaining HFI space has enough room for per-command metadata */
	hfi_needed = sizeof(struct dsi_hfi_panel_cmd_info) * packet_count;
	hfi_remaining = dsi_hfi->shared_addr_map->size -
			dsi_hfi->running_hfi_offset;
	if (hfi_needed > hfi_remaining) {
		DSI_ERR("HFI reserved space full: need %u bytes, have %zu\n",
			hfi_needed, hfi_remaining);
		rc = -ENOMEM;
		goto unlock_and_destroy_cmds;
	}

	/*
	 * Pre-validate that all commands fit within the reserved SDE region
	 * before writing any of them, preventing partial writes.
	 * Runtime custom DCS commands are bounded to:
	 *   [rt_custom_dcs_cmd_sde_base,
	 *    rt_custom_dcs_cmd_sde_base + DSI_RUNTIME_CUSTOM_DCS_CMD_RESERVED_SIZE)
	 */
	rc = dsi_hfi_fill_dcs_cmds_sde_region_precheck(
			cmds, packet_count,
			dsi_hfi->rt_custom_dcs_cmd_sde_running,
			dsi_hfi->rt_custom_dcs_cmd_sde_base +
			DSI_RUNTIME_CUSTOM_DCS_CMD_RESERVED_SIZE);
	if (rc) {
		DSI_ERR("rt custom DCS cmd SDE region precheck failed, rc=%d\n", rc);
		goto unlock_and_destroy_cmds;
	}

	/*
	 * Cache the current running offsets and the existing index entry before
	 * starting any writes.  If dsi_hfi_packetize_panel_cmd() fails mid-loop
	 * both the running pointers and the index entry are restored so the
	 * partial write is discarded and the buffer remains in a consistent state.
	 * Without restoring the index entry a failed override would leave a
	 * still-valid entry whose count/sde_offset/hfi_meta_offset point to the
	 * new (unwritten) location instead of the previous successful capture.
	 */
	cached_sde_running = dsi_hfi->rt_custom_dcs_cmd_sde_running;
	cached_hfi_running = dsi_hfi->running_hfi_offset;
	saved_index_entry  = *cmd_entry;

	/* Record the start offsets for this command type before writing */
	cmd_entry->sde_offset = dsi_hfi->rt_custom_dcs_cmd_sde_running;
	cmd_entry->hfi_meta_offset = dsi_hfi->running_hfi_offset;
	cmd_entry->count = packet_count;

	/* Get write pointers into SDE buffer and HFI shared buffer */
	sde_vaddr = (u8 *)display->vaddr + dsi_hfi->rt_custom_dcs_cmd_sde_running;
	cmd_info  = (struct dsi_hfi_panel_cmd_info *)
		    ((u8 *)dsi_hfi->shared_addr_map->local_addr +
		     dsi_hfi->running_hfi_offset);

	for (i = 0; i < packet_count; i++) {
		size_of_indv_cmd = 0;

		/* Fill per-command metadata */
		cmd_info->delay      = cmds[i].post_wait_ms;
		cmd_info->ctrl_flags = cmds[i].ctrl_flags;
		cmd_info->reserved1  = cmds[i].msg.flags;
		cmd_info->mode       = state;

		/* Packetize into MIPI DSI wire format and write into SDE buffer */
		rc = dsi_hfi_packetize_panel_cmd(&cmds[i], &size_of_indv_cmd, sde_vaddr);
		if (rc) {
			DSI_ERR("Failed to packetize DCS cmd %d rc=%d\n", i, rc);
			SDE_EVT32(cmd_type, i, rc, SDE_EVTLOG_FUNC_CASE3);
			dsi_hfi->rt_custom_dcs_cmd_sde_running = cached_sde_running;
			dsi_hfi->running_hfi_offset = cached_hfi_running;
			*cmd_entry = saved_index_entry;
			goto unlock_and_destroy_cmds;
		}

		/* 8-byte aligned size for SDE buffer advancement */
		aligned_size = (((size_of_indv_cmd + 7) >> 3) << 3);

		/* Fill remaining metadata fields */
		cmd_info->size       = size_of_indv_cmd;
		cmd_info->cmd_offset = dsi_hfi->rt_custom_dcs_cmd_sde_running +
				       display->cmd_buffer_iova;

		/* Advance SDE write pointer */
		sde_vaddr += aligned_size;
		dsi_hfi->rt_custom_dcs_cmd_sde_running += aligned_size;

		/* Advance HFI metadata write pointer */
		cmd_info++;
		dsi_hfi->running_hfi_offset += sizeof(struct dsi_hfi_panel_cmd_info);
	}

	/* Flush SDE buffer so DCP can see the newly captured command bytes */
	msm_gem_sync(display->tx_cmd_buf);

	cmd_entry->valid = true;

	SDE_EVT32(cmd_type, packet_count, cmd_entry->sde_offset, cmd_entry->hfi_meta_offset,
		  SDE_EVTLOG_FUNC_CASE4);
	DSI_DEBUG("Captured %d rt cust DCS cmd for type %d: sde_offset=0x%x hfi_meta_offset=0x%x\n",
		  packet_count, cmd_type,
		  cmd_entry->sde_offset,
		  cmd_entry->hfi_meta_offset);

unlock_and_destroy_cmds:
	mutex_unlock(&dsi_hfi->rt_dcs_cmd_lock);
destroy_cmds:
	for (i = 0; i < packet_count; i++)
		kfree(cmds[i].msg.tx_buf);
	kfree(cmds);
	SDE_EVT32(cmd_type, rc, SDE_EVTLOG_FUNC_EXIT);
	return rc;
}

int dsi_hfi_send_dcs_cmd_set_replace_cmd(struct dsi_display *display, bool resp_req)
{
	struct dsi_display_hfi *dsi_hfi;
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;
	struct hfi_client_t *hfi_client;
	struct dsi_hfi_dcs_cmd_set_replace_payload *payload = NULL;
	u32 hfi_cmd = HFI_COMMAND_DISPLAY_DSI_CUSTOM_DCS_CMDS_SET_REPLACE;
	u32 flags = HFI_HOST_FLAGS_NON_DISCARDABLE;
	u32 payload_size;
	u32 count = 0;
	u32 obj_id;
	int i, rc = 0;

	if (resp_req)
		flags |= HFI_HOST_FLAGS_RESPONSE_REQUIRED;

	if (!display || !display->dsi_hfi_info || !display->drm_conn) {
		DSI_ERR("Invalid params\n");
		return -EINVAL;
	}

	dsi_hfi = display->dsi_hfi_info;
	obj_id = sde_conn_get_display_obj_id(display->drm_conn);

	SDE_EVT32(obj_id, resp_req, SDE_EVTLOG_FUNC_ENTRY);

	mutex_lock(&dsi_hfi->rt_dcs_cmd_lock);

	/* Count how many standard command types have runtime custom DCS commands */
	for (i = 0; i < DSI_CMD_SET_MAX; i++) {
		if (dsi_hfi->rt_custom_dcs_cmd_map[i].valid &&
		    dsi_hfi->rt_custom_dcs_cmd_map[i].count)
			count++;
	}

	SDE_EVT32(obj_id, count, SDE_EVTLOG_FUNC_CASE1);

	if (!count) {
		DSI_ERR("No runtime custom DCS commands captured, nothing to replace\n");
		rc = -ENOENT;
		goto unlock_and_cleanup;
	}

	payload_size = sizeof(u32) + count * sizeof(struct hfi_dsi_dcs_cmd_set_replace_entry);
	payload = kzalloc(payload_size, GFP_KERNEL);
	if (!payload) {
		rc = -ENOMEM;
		goto unlock_and_cleanup;
	}

	/* Build the array payload: count + one entry per captured command type */
	payload->count = count;
	count = 0;
	for (i = 0; i < DSI_CMD_SET_MAX; i++) {
		if (!dsi_hfi->rt_custom_dcs_cmd_map[i].valid ||
		    !dsi_hfi->rt_custom_dcs_cmd_map[i].count)
			continue;

		/* Validate sde_offset and hfi_meta_offset before sending to firmware */
		if (dsi_hfi->rt_custom_dcs_cmd_map[i].sde_offset <
				dsi_hfi->rt_custom_dcs_cmd_sde_base ||
		    dsi_hfi->rt_custom_dcs_cmd_map[i].sde_offset >=
				dsi_hfi->rt_custom_dcs_cmd_sde_base +
				DSI_RUNTIME_CUSTOM_DCS_CMD_RESERVED_SIZE) {
			DSI_ERR("cmd_t %d: sde_off 0x%x outside rt DCS SDE region [0x%x, 0x%x)\n",
				i, dsi_hfi->rt_custom_dcs_cmd_map[i].sde_offset,
				dsi_hfi->rt_custom_dcs_cmd_sde_base,
				dsi_hfi->rt_custom_dcs_cmd_sde_base +
				DSI_RUNTIME_CUSTOM_DCS_CMD_RESERVED_SIZE);
			rc = -EINVAL;
			goto unlock_and_cleanup;
		}

		if (dsi_hfi->rt_custom_dcs_cmd_map[i].hfi_meta_offset <
				dsi_hfi->rt_custom_dcs_cmd_hfi_base ||
		    dsi_hfi->rt_custom_dcs_cmd_map[i].hfi_meta_offset >=
				dsi_hfi->shared_addr_map->size) {
			DSI_ERR("cmd_t %d: hfi_off 0x%x outside rt DCS HFI region [0x%x, 0x%x)\n",
				i, dsi_hfi->rt_custom_dcs_cmd_map[i].hfi_meta_offset,
				dsi_hfi->rt_custom_dcs_cmd_hfi_base,
				(u32)dsi_hfi->shared_addr_map->size);
			rc = -EINVAL;
			goto unlock_and_cleanup;
		}

		payload->entries[count].dpu_buff_type_offset =
			dsi_hfi->rt_custom_dcs_cmd_map[i].sde_offset;
		payload->entries[count].cmd_type = i;
		payload->entries[count].count_cmds = dsi_hfi->rt_custom_dcs_cmd_map[i].count;
		payload->entries[count].hfi_buff_struct_offset =
			dsi_hfi->rt_custom_dcs_cmd_map[i].hfi_meta_offset;
		payload->entries[count].reserved = 0;

		DSI_DEBUG("Replace entry[%d]: cmd_type=%d sde_offset=0x%x cnt=%d hfi_offset=0x%x\n",
			  count, i,
			  payload->entries[count].dpu_buff_type_offset,
			  payload->entries[count].count_cmds,
			  payload->entries[count].hfi_buff_struct_offset);
		SDE_EVT32(obj_id, i, payload->entries[count].dpu_buff_type_offset,
			  payload->entries[count].count_cmds,
			  payload->entries[count].hfi_buff_struct_offset, SDE_EVTLOG_FUNC_CASE2);
		count++;
	}

	sde_kms = sde_connector_get_kms(display->drm_conn);
	if (!sde_kms) {
		DSI_ERR("Failed to get sde_kms\n");
		rc = -EINVAL;
		goto unlock_and_cleanup;
	}

	hfi_kms = to_hfi_kms(sde_kms);
	if (!hfi_kms) {
		DSI_ERR("Failed to get hfi_kms\n");
		rc = -EINVAL;
		goto unlock_and_cleanup;
	}

	hfi_client = &hfi_kms->hfi_client;

	SDE_EVT32(obj_id, hfi_cmd, payload->count, SDE_EVTLOG_FUNC_CASE3);
	rc = dsi_display_hfi_send_cmd_buf(display, hfi_client, hfi_cmd,
					  display->display_type,
					  HFI_PAYLOAD_TYPE_U32_ARRAY,
					  payload, payload_size,
					  flags);
	SDE_EVT32(obj_id, hfi_cmd, rc, SDE_EVTLOG_FUNC_CASE4);
	if (rc) {
		DSI_ERR("Failed to send DSI_CUSTOM_DCS_CMDS_SET_REPLACE command, rc=%d\n", rc);
		goto unlock_and_cleanup;
	}

	DSI_DEBUG("Sent replace cmd for %d rt custom DCS command types\n", payload->count);

	/*
	 * Invalidate all cached runtime custom DCS command entries now that
	 * DCP has been notified. This prevents stale entries from being
	 * re-sent and ensures a subsequent call without new captures
	 * correctly returns -ENOENT.
	 */
	for (i = 0; i < DSI_CMD_SET_MAX; i++)
		dsi_hfi->rt_custom_dcs_cmd_map[i].valid = false;

unlock_and_cleanup:
	mutex_unlock(&dsi_hfi->rt_dcs_cmd_lock);
	kfree(payload);
	SDE_EVT32(obj_id, rc, SDE_EVTLOG_FUNC_EXIT);
	return rc;
}

static u32 *dsi_hfi_pack_freq_patterns(struct dsi_display *display, u32 *total_size)
{
	struct dsi_display_mode_priv_info *priv_info;
	struct msm_freq_step_pattern *freq_pattern;
	u32 *buffer = NULL;
	u32 *buffer_ptr;
	u32 buffer_size = 0;
	u32 pattern_count;
	u32 i;
	*total_size = 0;

	if (!display || !display->modes || !display->modes[0].priv_info ||
	    display->modes[0].priv_info->freq_step_list.count == 0 ||
	    !display->modes[0].priv_info->freq_step_list.freq_pattern) {
		DSI_ERR("Invalid params\n");
		return NULL;
	}

	priv_info = display->modes[0].priv_info;
	pattern_count = priv_info->freq_step_list.count;

	/* Calculate total buffer size needed */
	buffer_size = sizeof(u32);

	/* Pack struct hfi_freq_step_pattern - 6 fixed fields + variable-length array */
	for (i = 0; i < pattern_count; i++) {
		freq_pattern = &priv_info->freq_step_list.freq_pattern[i];
		if (!freq_pattern->freq_stepping_seq) {
			DSI_ERR("Invalid frequency stepping sequence\n");
			return NULL;
		}

		buffer_size += (6 + freq_pattern->length) * sizeof(u32);
	}

	buffer = kzalloc(buffer_size, GFP_KERNEL);
	if (!buffer) {
		DSI_ERR("Failed to allocate %u bytes for freq patterns\n", buffer_size);
		return NULL;
	}

	buffer_ptr = buffer;
	*buffer_ptr++ = pattern_count;

	/* pack each pattern */
	for (i = 0; i < pattern_count; i++) {
		freq_pattern = &priv_info->freq_step_list.freq_pattern[i];

		/* write 6 fixed fields */
		*buffer_ptr++ = freq_pattern->frame_interval;
		*buffer_ptr++ = freq_pattern->num_freq_steps;
		*buffer_ptr++ = freq_pattern->usecase_idx;
		*buffer_ptr++ = freq_pattern->frame_pattern_seq_idx;
		*buffer_ptr++ = freq_pattern->needs_ap_refresh;
		*buffer_ptr++ = freq_pattern->length;

		/* write variable-length frequency stepping sequence */
		memcpy(buffer_ptr, freq_pattern->freq_stepping_seq,
			freq_pattern->length * sizeof(u32));
		buffer_ptr += freq_pattern->length;
	}

	*total_size = buffer_size;
	DSI_DEBUG("Packed %u frequency patterns into %u bytes\n", pattern_count, buffer_size);

	return buffer;
}

static void dsi_hfi_populate_panel_generic_caps(struct dsi_display *display,
					struct dsi_panel *panel,
					struct dsi_panel_generic_caps *panel_generic_caps)
{
	panel_generic_caps->panel_type = dsi_get_panel_type_helper(panel);
	panel_generic_caps->display_type = dsi_get_display_type_helper(display);
	panel_generic_caps->color_order_type = dsi_get_panel_color_order_type(panel);
	panel_generic_caps->dma_trigger_type =
		dsi_get_panel_trigger_type_helper(panel->host_config.dma_cmd_trigger);
	panel_generic_caps->mdp_trigger_type =
		dsi_get_panel_trigger_type_helper(panel->host_config.mdp_cmd_trigger);
	panel_generic_caps->te_mode = panel->host_config.te_mode;
	panel_generic_caps->dma_sched_line = panel->host_config.dma_sched_line;
	panel_generic_caps->dma_sched_window = panel->host_config.dma_sched_window;
	panel_generic_caps->traffic_mode = dsi_get_panel_traffic_mode_helper(panel);
	panel_generic_caps->virtual_channel_id = panel->video_config.vc_id;
	panel_generic_caps->wr_mem_start = panel->cmd_config.wr_mem_start;
	panel_generic_caps->wr_mem_continue = panel->cmd_config.wr_mem_continue;
	panel_generic_caps->te_dcs_command = panel->cmd_config.insert_dcs_command;
	panel_generic_caps->panel_op_mode = dsi_get_panel_op_mode_helper(panel);
	panel_generic_caps->min_backlight_level = panel->bl_config.bl_min_level;
	panel_generic_caps->max_backlight_level = panel->bl_config.bl_max_level;
	panel_generic_caps->max_brightness_level = panel->hdr_props.peak_brightness;
	panel_generic_caps->vsync_src = dsi_get_panel_vsync_src(display);
	panel_generic_caps->cphy_enabled = (panel->host_config.phy_type == DSI_PHY_TYPE_CPHY);
	panel_generic_caps->panel_name = (*(u32 *)panel->name);
	panel_generic_caps->panel_bpp = dsi_get_panel_bpp_helper(panel);
	panel_generic_caps->panels_lanes_state = dsi_get_panel_lane_state_helper(panel);
	panel_generic_caps->panel_lane_map = dsi_get_panel_lane_map_helper(panel);
	panel_generic_caps->tx_eot_append = (u32)(panel->host_config.append_tx_eot);
	panel_generic_caps->eof_power_mode = panel->video_config.eof_bllp_lp11_en;
	panel_generic_caps->bllp_power_mode = panel->video_config.bllp_lp11_en;
	panel_generic_caps->backlight_ctrl_prim = dsi_get_panel_backlight_type(panel, "primary");
	panel_generic_caps->backlight_ctrl_sec = dsi_get_panel_backlight_type(panel, "secondary");
	panel_generic_caps->is_bl_inverted = panel->bl_config.bl_inverted_dbv;
	dsi_get_panel_ctrl_nums_helper(display, panel_generic_caps->ctrl_nums);
	dsi_get_panel_phy_nums_helper(display, panel_generic_caps->phy_nums);

	if (display->panel->esd_config.esd_enabled &&
		!display->panel->esd_config.esd_host_controlled) {
		dsi_get_panel_esd_config_helper(display, &panel_generic_caps->esd_config);
	}

	if (panel->esync_caps.esync_support) {
		dsi_hfi_populate_esync_caps(panel, &panel_generic_caps->esync_caps);
	}

	if (panel->dfps_caps.dfps_support) {
		dsi_hfi_populate_dfps_caps(panel, &panel_generic_caps->dfps_caps);
	}

	panel_generic_caps->lp11_init = panel->lp11_init;

	if (panel->panel_mode_switch_enabled) {
		dsi_hfi_populate_poms_caps(panel, &panel_generic_caps->poms_caps);
	}

	/* Populate DSI custom command set info */
	panel_generic_caps->custom_cmd_set_info[0] = DSI_CUSTOM_CMD_SET_START_IDX;
	panel_generic_caps->custom_cmd_set_info[1] = DSI_CUSTOM_CMD_SET_COUNT;

	panel_generic_caps->ulps_supported = panel->ulps_feature_enabled;
}

static void dsi_hfi_populate_panel_timing_caps(struct dsi_display *display,
					struct dsi_display_mode *mode,
					struct dsi_panel_timing_caps *panel_timing_caps,
					void **sde_vaddr, void **hfi_vaddr)
{
	int i;

	if (!mode || !mode->priv_info) {
		DSI_ERR("Invalid params %d\n", !mode);
		return;
	}

	panel_timing_caps->panel_index = mode->mode_idx;
	panel_timing_caps->clockrate[0] = HFI_VAL_L32(mode->timing.clk_rate_hz);
	panel_timing_caps->clockrate[1] = HFI_VAL_H32(mode->timing.clk_rate_hz);
	panel_timing_caps->framerate = (mode->timing.refresh_rate);
	panel_timing_caps->panel_jitter[0] = mode->priv_info->panel_jitter_numer;
	panel_timing_caps->panel_jitter[1] = mode->priv_info->panel_jitter_denom;
	panel_timing_caps->hsync_pulse = mode->timing.h_sync_width;
	hfi_panel_get_mode_res_data(mode, panel_timing_caps);
	if (mode->timing.dsc_enabled)
		hfi_panel_get_mode_compression_params(mode, panel_timing_caps);
	else
		panel_timing_caps->compression_params.mode = HFI_PANEL_COMPRESSION_NONE;
	panel_timing_caps->topology.count = 1;
	panel_timing_caps->topology.hfi_topology.mixer_count = mode->priv_info->topology.num_lm;
	panel_timing_caps->topology.hfi_topology.enc_count = mode->priv_info->topology.num_enc;
	panel_timing_caps->topology.hfi_topology.display_count = mode->priv_info->topology.num_intf;
	panel_timing_caps->top_index = 0;
	hfi_panel_fill_dcs_cmds(display, mode->priv_info, panel_timing_caps, sde_vaddr, hfi_vaddr);
	panel_timing_caps->phy_timings_payload.count = mode->priv_info->phy_timing_len;
	if (mode->priv_info->phy_timing_val) {
		for (i = 0; i < NUM_VARIABLE_DPHY_TIMINGS; i++)
			panel_timing_caps->phy_timings_payload.dphy_timings[i] =
				mode->priv_info->phy_timing_val[i];
	}
	hfi_panel_get_mode_esync_timing_caps(display, mode, panel_timing_caps);
	hfi_panel_get_mode_qsync_timing_params(display, mode, panel_timing_caps);
}

static int dsi_hfi_append_panel_init_caps(struct hfi_cmdbuf_t *buffer,
					struct dsi_display *display,
					struct dsi_panel_init_caps panel_init_caps,
					struct hfi_shared_addr_map *addr_map)
{
	int rc = 0;
	u32 object_id = 0x0;
	struct dsi_display_hfi *display_hfi;
	u32 kv_count;
	u32 reserved_key = 0;
	u32 kv_size = 0;
	u32 payload_size = 0;
	u32 sde_addr[3];
	u32 hfi_addr[3];
	u64 rem_prop_val = (u64) addr_map->remote_addr;
	struct hfi_buff dcs_cmd_tx_buf_dva;
	struct hfi_buff dcs_cmd_tx_buf_iova;
	u32 pixel_format = 0;

	if (!display)
		return -EINVAL;

	sde_addr[0] = reserved_key;
	sde_addr[1] = HFI_VAL_L32(display->cmd_buffer_iova);
	sde_addr[2] = HFI_VAL_H32(display->cmd_buffer_iova);

	hfi_addr[0] = reserved_key;
	hfi_addr[1] = HFI_VAL_L32(rem_prop_val);
	hfi_addr[2] = HFI_VAL_H32(rem_prop_val);

	DSI_DEBUG("hfi val l32:0x%8x hfi val h32:0x%8x\n", hfi_addr[1], hfi_addr[2]);

	display_hfi = display->dsi_hfi_info;
	if (!display_hfi)
		return -EINVAL;

	panel_init_caps.dcs_cmd_tx_buf_dva =
			display_hfi->sgt_tx_cmd_buf_map.remote_addr +
			display_hfi->tx_cmd_buf_fill_level;
	panel_init_caps.dcs_cmd_tx_buf_iova =
			display->cmd_buffer_iova + display_hfi->tx_cmd_buf_fill_level;

	dcs_cmd_tx_buf_dva.addr_l = HFI_VAL_L32(panel_init_caps.dcs_cmd_tx_buf_dva);
	dcs_cmd_tx_buf_dva.addr_h = HFI_VAL_H32(panel_init_caps.dcs_cmd_tx_buf_dva);
	dcs_cmd_tx_buf_dva.size = display->cmd_buffer_size - display_hfi->tx_cmd_buf_fill_level;

	dcs_cmd_tx_buf_iova.addr_l = HFI_VAL_L32(panel_init_caps.dcs_cmd_tx_buf_iova);
	dcs_cmd_tx_buf_iova.addr_h = HFI_VAL_H32(panel_init_caps.dcs_cmd_tx_buf_iova);
	dcs_cmd_tx_buf_iova.size = display->cmd_buffer_size - display_hfi->tx_cmd_buf_fill_level;

	hfi_util_kv_helper_reset(display_hfi->kv_props);

	hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_TIMING_MODE_COUNT, 0,
			(sizeof(panel_init_caps.num_timing_modes) / sizeof(u32))),
			(void *)&panel_init_caps.num_timing_modes);
	kv_size += sizeof(panel_init_caps.num_timing_modes);

	hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_DPU_ADDRESS, 0,
			((ARRAY_SIZE(sde_addr) * sizeof(sde_addr[0])) / sizeof(u32))),
			(void *)sde_addr);
	kv_size += sizeof(sde_addr);

	hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_DCP_ADDRESS, 0,
			((ARRAY_SIZE(hfi_addr) * sizeof(hfi_addr[0])) / sizeof(u32))),
			(void *)hfi_addr);
	kv_size += sizeof(hfi_addr);

	hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_DCS_CMD_TX_BUF_DVA, 0,
			(sizeof(dcs_cmd_tx_buf_dva)) / sizeof(u32)),
			(void *)&dcs_cmd_tx_buf_dva);
	kv_size += sizeof(dcs_cmd_tx_buf_dva);

	hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_DCS_CMD_TX_BUF_IOVA, 0,
			(sizeof(dcs_cmd_tx_buf_iova) / sizeof(u32))),
			(void *)&dcs_cmd_tx_buf_iova);
	kv_size += sizeof(dcs_cmd_tx_buf_iova);

	if (display->panel && display->panel->host_config.dpu_dma_enabled) {
		pixel_format = HFI_COLOR_FORMAT_RGB888_BYPASS;
		hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_COLOR_FORMAT, 0,
			(sizeof(pixel_format) / sizeof(u32))),
			&pixel_format);
		kv_size += sizeof(pixel_format);
	}

	kv_count = hfi_util_kv_helper_get_count(display_hfi->kv_props);

	payload_size = (kv_count * sizeof(u32)) + kv_size;

	rc = hfi_adapter_add_prop_array(buffer->ctx,
				buffer,
				HFI_COMMAND_PANEL_INIT_PANEL_CAPS,
				object_id,
				HFI_PAYLOAD_TYPE_U32,
				hfi_util_kv_helper_get_payload_addr(display_hfi->kv_props),
				kv_count,
				payload_size);
	if (rc)
		DSI_ERR("Failed to add caps to buffer, rc = %d", rc);

	return rc;
}

static int dsi_hfi_append_panel_generic_caps(struct hfi_cmdbuf_t *buffer,
				struct dsi_display *display,
				struct dsi_panel_generic_caps panel_generic_caps)
{
	int rc = 0;
	int i = 0;
	u32 kv_count;
	u32 kv_size = 0;
	u32 payload_size = 0;
	u32 object_id = 0x0;
	u32 dfps_payload[5]; /* 1 + (sizeof(panel_generic_caps.dfps_caps)/sizeof(u32)) */
	struct dsi_display_hfi *display_hfi;
	u32 *freq_patterns = NULL;
	u32 freq_pattern_size = 0;

	if (!display)
		return -EINVAL;

	display_hfi = display->dsi_hfi_info;
	if (!display_hfi)
		return -EINVAL;

	hfi_util_kv_helper_reset(display_hfi->kv_props);

	/*
	 * Property mapping for panel generic capabilities
	 *
	 * use_default_val behavior:
	 * - true:  Property is always sent to firmware, even if value is 0.
	 *          These are mandatory properties required by the HFI protocol.
	 * - false: Property is only sent if value is non-zero.
	 *          These are optional properties that may not be present on all panels.
	 */
	struct dsi_value_to_prop_lookup dsi_hfi_gen_props_map[] = {
		{panel_generic_caps.panel_type, HFI_PROPERTY_PANEL_PHYSICAL_TYPE, true},
		{panel_generic_caps.display_type, HFI_PROPERTY_PANEL_DISPLAY_TYPE, true},
		{panel_generic_caps.color_order_type, HFI_PROPERTY_PANEL_COLOR_ORDER, true},
		{panel_generic_caps.dma_trigger_type, HFI_PROPERTY_PANEL_DMA_TRIGGER, true},
		{panel_generic_caps.mdp_trigger_type, HFI_PROPERTY_PANEL_STREAM_TRIGGER, true},
		{panel_generic_caps.te_mode, HFI_PROPERTY_PANEL_TE_MODE, true},
		{panel_generic_caps.dma_sched_line, HFI_PROPERTY_PANEL_DMA_SCHEDULE_LINE, true},
		{panel_generic_caps.dma_sched_window, HFI_PROPERTY_PANEL_DMA_SCHEDULE_WINDOW, true},
		{panel_generic_caps.traffic_mode, HFI_PROPERTY_PANEL_TRAFFIC_MODE, true},
		{panel_generic_caps.virtual_channel_id, HFI_PROPERTY_PANEL_VIRTUAL_CHANNEL_ID,
			true},
		{panel_generic_caps.wr_mem_start, HFI_PROPERTY_PANEL_WR_MEM_START, true},
		{panel_generic_caps.wr_mem_continue, HFI_PROPERTY_PANEL_WR_MEM_CONTINUE, true},
		{panel_generic_caps.te_dcs_command, HFI_PROPERTY_PANEL_TE_DCS_COMMAND, true},
		{panel_generic_caps.panel_op_mode, HFI_PROPERTY_PANEL_OPERATING_MODE, true},
		{panel_generic_caps.min_backlight_level, HFI_PROPERTY_PANEL_BL_MIN_LEVEL, true},
		{panel_generic_caps.max_backlight_level, HFI_PROPERTY_PANEL_BL_MAX_LEVEL, true},
		{panel_generic_caps.vsync_src, HFI_PROPERTY_PANEL_VSYNC_SOURCE, true},
		{panel_generic_caps.max_brightness_level, HFI_PROPERTY_PANEL_BRIGHTNESS_MAX_LEVEL,
			true},
		{panel_generic_caps.cphy_enabled, HFI_PROPERTY_PANEL_CPHY_MODE, true},
		{panel_generic_caps.panel_name, HFI_PROPERTY_PANEL_NAME, false},
		{panel_generic_caps.panel_bpp, HFI_PROPERTY_PANEL_BPP, false},
		{panel_generic_caps.panels_lanes_state, HFI_PROPERTY_PANEL_LANES_STATE, false},
		{panel_generic_caps.panel_lane_map, HFI_PROPERTY_PANEL_LANE_MAP, false},
		{panel_generic_caps.tx_eot_append, HFI_PROPERTY_PANEL_TX_EOT_APPEND, false},
		{panel_generic_caps.eof_power_mode, HFI_PROPERTY_PANEL_BLLP_EOF_POWER_MODE, false},
		{panel_generic_caps.bllp_power_mode, HFI_PROPERTY_PANEL_BLLP_POWER_MODE, false},
		{panel_generic_caps.backlight_ctrl_prim, HFI_PROPERTY_PANEL_BL_PMIC_CONTROL_TYPE,
			false},
		{panel_generic_caps.backlight_ctrl_sec,
						HFI_PROPERTY_PANEL_SEC_BL_PMIC_CONTROL_TYPE, false},
		{panel_generic_caps.is_bl_inverted, HFI_PROPERTY_PANEL_BL_INVERTED_DBV, false},
		{panel_generic_caps.lp11_init, HFI_PROPERTY_PANEL_LP11_INIT, false},
	};

	/* populate properties based on use_default_val flag */
	for (i = 0; i < ARRAY_SIZE(dsi_hfi_gen_props_map); i++) {
		/* add the property based on default‑value usage or a non‑zero value. */
		if (dsi_hfi_gen_props_map[i].use_default_val || dsi_hfi_gen_props_map[i].value) {
			hfi_util_kv_helper_add(display_hfi->kv_props,
					HFI_PACKKEY(dsi_hfi_gen_props_map[i].hfi_prop, 0,
					(sizeof(dsi_hfi_gen_props_map[i].value) / sizeof(u32))),
					(void *)&dsi_hfi_gen_props_map[i].value);
			kv_size += sizeof(dsi_hfi_gen_props_map[i].value);
		}
	}

	/* Special case */
	if (panel_generic_caps.ctrl_nums[0]) {
		hfi_util_kv_helper_add(display_hfi->kv_props,
					HFI_PACKKEY(HFI_PROPERTY_PANEL_CTRL_NUM, 0,
					(sizeof(panel_generic_caps.ctrl_nums) / sizeof(u32))),
					(void *)&panel_generic_caps.ctrl_nums);
		kv_size += sizeof(panel_generic_caps.ctrl_nums);
	}

	if (panel_generic_caps.phy_nums[0]) {
		hfi_util_kv_helper_add(display_hfi->kv_props,
					HFI_PACKKEY(HFI_PROPERTY_PANEL_PHY_NUM, 0,
					(sizeof(panel_generic_caps.phy_nums) / sizeof(u32))),
					(void *)&panel_generic_caps.phy_nums);
		kv_size += sizeof(panel_generic_caps.phy_nums);
	}

	if (display->panel->esd_config.esd_enabled && !is_sim_panel(display) &&
		!display->panel->esd_config.esd_host_controlled) {
		hfi_util_kv_helper_add(display_hfi->kv_props,
				HFI_PACKKEY(HFI_PROPERTY_PANEL_ESD_CONFIG, 0,
				(sizeof(panel_generic_caps.esd_config) / sizeof(u32))),
				(void *)&panel_generic_caps.esd_config);
		kv_size += sizeof(panel_generic_caps.esd_config);
	}


	if (panel_generic_caps.esync_caps.esync_support) {
		hfi_util_kv_helper_add(display_hfi->kv_props,
					HFI_PACKKEY(HFI_PROPERTY_PANEL_ESYNC_CAPS, 0,
					(sizeof(panel_generic_caps.esync_caps) / sizeof(u32))),
					(void *)&panel_generic_caps.esync_caps);
		kv_size += sizeof(panel_generic_caps.esync_caps);
	}

	if (display->panel->qsync_caps.qsync_support) {
		struct hfi_qsync_params qsync_params;

		qsync_params.qsync_min_fps = display->panel->qsync_caps.qsync_min_fps;
		qsync_params.avr_step_fps = display->panel->avr_caps.avr_step_fps;

		hfi_util_kv_helper_add(display_hfi->kv_props,
					HFI_PACKKEY(HFI_PROPERTY_PANEL_QSYNC_PARAMS, 0,
					(sizeof(qsync_params) / sizeof(u32))),
					(void *)&qsync_params);
		kv_size += sizeof(qsync_params);
	}

	if (panel_generic_caps.poms_caps.panel_mode_switch_enabled) {
		hfi_util_kv_helper_add(display_hfi->kv_props,
					HFI_PACKKEY(HFI_PROPERTY_PANEL_OPERATING_SWITCH_CAPABILITY,
					0, (sizeof(panel_generic_caps.poms_caps) / sizeof(u32))),
					(void *)&panel_generic_caps.poms_caps);
		kv_size += sizeof(panel_generic_caps.poms_caps);
	}

	if (panel_generic_caps.dfps_caps.dfps_support) {
		dfps_payload[0] = 1; /* Currently we support only single dfps struct */
		memcpy(&dfps_payload[1], &panel_generic_caps.dfps_caps,
			sizeof(panel_generic_caps.dfps_caps));
		hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_DFPS_CAPS, 0,
			((ARRAY_SIZE(dfps_payload) * sizeof(dfps_payload[0])) / sizeof(u32))),
					(void *)dfps_payload);
		kv_size += sizeof(dfps_payload);
	}

	if (display->modes && display->modes[0].priv_info &&
			display->modes[0].priv_info->freq_step_list.count > 0) {
		freq_patterns = dsi_hfi_pack_freq_patterns(display, &freq_pattern_size);
		if (freq_patterns && freq_pattern_size > 0) {
			hfi_util_kv_helper_add(display_hfi->kv_props,
					HFI_PACKKEY(HFI_PROPERTY_PANEL_FREQ_PATTERN, 0,
					(freq_pattern_size / sizeof(u32))),
					(void *)freq_patterns);
			kv_size += freq_pattern_size;
		}
	}

	/* Add DSI custom command set info if custom commands are present */
	if (panel_generic_caps.custom_cmd_set_info[1]) {
		hfi_util_kv_helper_add(display_hfi->kv_props,
				HFI_PACKKEY(HFI_PROPERTY_PANEL_DSI_CUSTOM_DCS_CMDS_SET_INFO, 0,
				((ARRAY_SIZE(panel_generic_caps.custom_cmd_set_info) *
				sizeof(panel_generic_caps.custom_cmd_set_info[0])) / sizeof(u32))),
				(void *)panel_generic_caps.custom_cmd_set_info);
		kv_size += sizeof(panel_generic_caps.custom_cmd_set_info);
	}

	hfi_util_kv_helper_add(display_hfi->kv_props,
				HFI_PACKKEY(HFI_PROPERTY_PANEL_ULPS_SUPPORTED, 0,
				(sizeof(panel_generic_caps.ulps_supported) / sizeof(u32))),
				(void *)&panel_generic_caps.ulps_supported);
	kv_size += sizeof(panel_generic_caps.ulps_supported);

	kv_count = hfi_util_kv_helper_get_count(display_hfi->kv_props);

	payload_size = (kv_count * sizeof(u32)) + kv_size;

	rc = hfi_adapter_add_prop_array(buffer->ctx,
				buffer,
				HFI_COMMAND_PANEL_INIT_GENERIC_CAPS,
				object_id,
				HFI_PAYLOAD_TYPE_U32_ARRAY,
				hfi_util_kv_helper_get_payload_addr(display_hfi->kv_props),
				kv_count,
				payload_size);

	kfree(freq_patterns);

	if (rc)
		DSI_ERR("Failed to add caps to buffer, rc = %d", rc);

	return rc;
}

static int _dsi_hfi_append_panel_timing_caps(struct dsi_display_hfi *display_hfi,
		struct hfi_cmdbuf_t *buffer, u32 chunk, u32 start,
		struct dsi_panel_timing_caps *timing_caps_array)
{
	int rc = 0;
	int i;
	u32 object_id = 0x0;

	for (i = 0; i < chunk; ++i) {
		u32 idx = start + i;
		struct dsi_panel_timing_caps *timing_caps = &timing_caps_array[idx];
		u32 kv_count;
		u32 kv_size = 0;
		u32 payload_size = 0;

		hfi_util_kv_helper_reset(display_hfi->kv_props);

		hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_INDEX, 0,
			sizeof(timing_caps->panel_index) / sizeof(u32)),
			(void *)&timing_caps->panel_index);
		kv_size += sizeof(timing_caps->panel_index);

		hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_CLOCKRATE, 0,
			sizeof(timing_caps->clockrate) / sizeof(u32)),
			(void *)&timing_caps->clockrate);
		kv_size += sizeof(timing_caps->clockrate);

		hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_FRAMERATE, 0,
			sizeof(timing_caps->framerate) / sizeof(u32)),
			(void *)&timing_caps->framerate);
		kv_size += sizeof(timing_caps->framerate);

		hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_JITTER, 0,
			sizeof(timing_caps->panel_jitter) / sizeof(u32)),
			(void *)&timing_caps->panel_jitter);
		kv_size += sizeof(timing_caps->panel_jitter);

		hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_RESOLUTION_DATA, 0,
			sizeof(timing_caps->res_data) / sizeof(u32)),
			(void *)&timing_caps->res_data);
		kv_size += sizeof(timing_caps->res_data);

		hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_COMPRESSION_DATA, 0,
			sizeof(timing_caps->compression_params) / sizeof(u32)),
			(void *)&timing_caps->compression_params);
		kv_size += sizeof(timing_caps->compression_params);

		if (timing_caps->rc_override_enabled) {
			hfi_util_kv_helper_add(display_hfi->kv_props,
				HFI_PACKKEY(HFI_PROPERTY_PANEL_COMPRESSION_RC_OVERRIDE, 0,
				sizeof(timing_caps->rc_override) / sizeof(u32)),
				(void *)&timing_caps->rc_override);
			kv_size += sizeof(timing_caps->rc_override);
		}

		hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_DISPLAY_TOPOLOGY, 0,
			sizeof(timing_caps->topology) / sizeof(u32)),
			(void *)&timing_caps->topology);
		kv_size += sizeof(timing_caps->topology);

		hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_DEFAULT_TOPOLOGY_INDEX, 0,
			sizeof(timing_caps->top_index) / sizeof(u32)),
			(void *)&timing_caps->top_index);
		kv_size += sizeof(timing_caps->top_index);

		hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_DCS_CMD_INFO, 0,
			((sizeof(struct dsi_hfi_panel_per_cmd_type) / sizeof(u32)) *
			timing_caps->payload.count + 1)),
			(void *)&timing_caps->payload);
		kv_size += ((sizeof(struct dsi_hfi_panel_per_cmd_type) / sizeof(u32)) *
			timing_caps->payload.count + 1) * sizeof(u32);

		hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_DPHY_TIMINGS, 0,
			sizeof(timing_caps->phy_timings_payload) / sizeof(u32)),
			(void *)&timing_caps->phy_timings_payload);
		kv_size += sizeof(timing_caps->phy_timings_payload);

		hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_ESYNC_TIMING_CAPS, 0,
			sizeof(timing_caps->esync_timing_caps) / sizeof(u32)),
			(void *)&timing_caps->esync_timing_caps);
		kv_size += sizeof(timing_caps->esync_timing_caps);

		hfi_util_kv_helper_add(display_hfi->kv_props,
			HFI_PACKKEY(HFI_PROPERTY_PANEL_QSYNC_TIMING_PARAMS, 0,
			sizeof(timing_caps->qsync_timing_params) / sizeof(u32)),
			(void *)&timing_caps->qsync_timing_params);
		kv_size += sizeof(timing_caps->qsync_timing_params);

		kv_count = hfi_util_kv_helper_get_count(display_hfi->kv_props);
		payload_size = (kv_count * sizeof(u32)) + kv_size;

		rc = hfi_adapter_add_prop_array(buffer->ctx,
				buffer,
				HFI_COMMAND_PANEL_INIT_TIMING_MODE_CAPS,
				object_id,
				HFI_PAYLOAD_TYPE_U32_ARRAY,
				hfi_util_kv_helper_get_payload_addr(display_hfi->kv_props),
				kv_count,
				payload_size);

		if (rc)
			DSI_ERR("Failed to add caps for timing node:%d, rc = %d",
					idx, rc);
	}
	return rc;
}

static int dsi_hfi_send_panel_timing_modes(struct dsi_display *display,
		struct dsi_panel_timing_caps *timing_caps_array)
{
	int rc = 0;
	struct dsi_display_hfi *display_hfi;
	u32 obj_id = 0;

	if (!display)
		return -EINVAL;

	obj_id = sde_conn_get_display_obj_id(display->drm_conn);

	display_hfi = display->dsi_hfi_info;
	if (!display_hfi)
		return -EINVAL;

	/* Init timing mode cap cmd send */
	const u32 total_modes = display->panel->num_display_modes;
	u32 start = 0;
	u32 batch_idx = 0;
	struct hfi_cmdbuf_t *buffer;

	while (start < total_modes) {
		const u32 chunk = min(MAX_TIMING_PER_PACKET, total_modes - start);

		SDE_EVT32(HFI_COMMAND_PANEL_INIT_TIMING_MODE_CAPS, SDE_EVTLOG_FUNC_CASE2);
		buffer = hfi_adapter_get_cmd_buf(display_hfi->hfi_client,
				obj_id,	HFI_CMDBUF_TYPE_DISPLAY_INFO_BLOCKING);

		if (!buffer) {
			DSI_ERR("get_cmd_buf failed, batch=%u start=%u chunk=%u\n",
					batch_idx, start, chunk);
			rc = -ENOMEM;
			break;
		}

		rc = _dsi_hfi_append_panel_timing_caps(display_hfi, buffer, chunk,
				start, timing_caps_array);
		if (rc) {
			DSI_ERR("append timing caps failed, batch=%u start=%u chunk=%u rc=%d\n",
					batch_idx, start, chunk, rc);
			goto error_append;
		}

		rc = hfi_adapter_set_cmd_buf(display_hfi->hfi_client, buffer);
		if (rc) {
			DSI_ERR("Batch %u (start=%u, count=%u) failed, rc=%d\n",
					batch_idx, start, chunk, rc);
			goto error_append;
		}

		start     += chunk;
		batch_idx += 1;
	}

	return rc;

error_append:
	rc = hfi_adapter_release_cmd_buf(display_hfi->hfi_client, buffer);
	if (rc)
		DSI_ERR("failed to release command buffer\n");

	return rc;
}

/**
 * dsi_hfi_calculate_required_memory() - calculate the required memory
 * @panel: handle to dsi panel structure
 *
 * The HFI shared buffer must be large enough to hold DT defined DCS command metadata.
 * Runtime custom DCS command metadata uses whatever space remains after DT commands.
 *
 * Return: the required memory
 */
static inline size_t dsi_hfi_calculate_required_memory(struct dsi_panel *panel)
{
	size_t mem_size = 0;

	mem_size = (size_t)PAGE_SIZE * (size_t)panel->shared_cmd_buf_page_size;

	if (mem_size < DSI_HFI_MIN_MAPPED_ADDR_SIZE)
		mem_size = DSI_HFI_MIN_MAPPED_ADDR_SIZE;
	else if (mem_size > DSI_HFI_MAX_MAPPED_ADDR_SIZE)
		mem_size = DSI_HFI_MAX_MAPPED_ADDR_SIZE;

	DSI_DEBUG("calculated HFI memory requirement: %zu bytes\n", mem_size);

	return mem_size;
}

int dsi_hfi_panel_init(struct dsi_display *display, struct dsi_panel *panel)
{
	struct dsi_panel_init_caps panel_init_caps;
	struct dsi_panel_generic_caps panel_generic_caps;
	struct dsi_panel_timing_caps *timing_caps_array;
	int i;
	int rc = 0;
	u32 obj_id;
	struct hfi_shared_addr_map *addr_map;
	struct dsi_display_hfi *display_hfi;
	struct msm_gem_object *tx_cmd_buf;
	void *tx_cmd_buf_vaddr, *hfi_buff_vaddr;

	if (!display)
		return -EINVAL;


	if (display->trusted_vm_env)
		return rc;

	obj_id = sde_conn_get_display_obj_id(display->drm_conn);

	display_hfi = display->dsi_hfi_info;
	if (!display_hfi)
		return -EINVAL;

	display_hfi->running_hfi_offset = 0;
	display_hfi->running_sde_offset = 0;

	struct hfi_cmdbuf_t *buffer = hfi_adapter_get_cmd_buf(display_hfi->hfi_client,
							obj_id,
							HFI_CMDBUF_TYPE_DISPLAY_INFO_BLOCKING);

	if (!buffer) {
		DSI_ERR("failed to allocate hfi command buffer\n");
		return -EINVAL;
	}

	panel_init_caps.num_timing_modes = panel->num_display_modes;
	if (!panel_init_caps.num_timing_modes) {
		DSI_ERR("No timing modes - panel init failed");
		goto error_buff;
	}

	if (display->tx_cmd_buf == NULL) {
		rc = dsi_hfi_host_alloc_cmd_tx_buffer(display);
		if (rc) {
			DSI_ERR("failed to allocate sde mapped buffer\n");
			goto error_buff;
		}
	}

	if (!display_hfi->sgt_tx_cmd_buf_map.remote_addr) {
		tx_cmd_buf = to_msm_bo(display->tx_cmd_buf);
		if (!tx_cmd_buf || !tx_cmd_buf->sgt) {
			DSI_ERR("Invalid tx command buffer\n");
			goto error_buff;
		}

		display_hfi->sgt_tx_cmd_buf_map.size = display->cmd_buffer_size;
		rc = hfi_adapter_map_sg_table(display_hfi->hfi_client, tx_cmd_buf->sgt,
				&display_hfi->sgt_tx_cmd_buf_map);
		if (rc) {
			DSI_ERR("failed to map tx command buffer to FW, rc = %d\n", rc);
			goto error_buff;
		}
	}

	if (!display_hfi->shared_addr_map) {
		addr_map = kvzalloc(sizeof(struct hfi_shared_addr_map), GFP_KERNEL);
		if (!addr_map) {
			DSI_ERR("failed to allocate addr_map");
			goto error_unmap_dva;
		}
		display_hfi->shared_addr_map = addr_map;

		addr_map->size = dsi_hfi_calculate_required_memory(panel);

		hfi_adapter_buffer_alloc(display_hfi->hfi_client, addr_map);
		if (!addr_map->remote_addr || !addr_map->local_addr)
			goto error_addr_map;
	} else {
		addr_map = display_hfi->shared_addr_map;
	}

	if (panel->esd_config.esd_enabled && !panel->esd_config.esd_host_controlled
			&& panel->esd_config.status_mode == ESD_MODE_REG_READ) {
		if (!display_hfi->esd_addr_map) {
			display_hfi->esd_addr_map = kvzalloc(sizeof(struct hfi_shared_addr_map),
							GFP_KERNEL);
			if (!display_hfi->esd_addr_map) {
				DSI_ERR("failed to allocate addr_map");
				goto error_addr_map;
			}
			display_hfi->esd_addr_map->size = SZ_4K;

			hfi_adapter_buffer_alloc(display_hfi->hfi_client,
						display_hfi->esd_addr_map);
			if (!display_hfi->esd_addr_map->remote_addr ||
				!display_hfi->esd_addr_map->local_addr) {
				kfree(display_hfi->esd_addr_map);
				display_hfi->esd_addr_map = NULL;
				goto error_addr_map;
			}
		}
	}

	timing_caps_array = kcalloc(panel_init_caps.num_timing_modes,
					sizeof(struct dsi_panel_timing_caps),
					GFP_KERNEL);
	if (!timing_caps_array)
		goto error_array;

	dsi_hfi_populate_panel_generic_caps(display, panel, &panel_generic_caps);

	hfi_buff_vaddr = addr_map->local_addr;
	tx_cmd_buf_vaddr = display->vaddr;

	for (i = 0; i < panel_init_caps.num_timing_modes; i++)
		dsi_hfi_populate_panel_timing_caps(display,
								&display->modes[i],
								&timing_caps_array[i],
								&tx_cmd_buf_vaddr,
								&hfi_buff_vaddr);

	/*
	 * After all DT defined DCS commands are packed, record the fixed base
	 * offsets for the runtime custom DCS command reserved space in both
	 * the SDE and HFI shared buffers.  Using fixed boundaries provides a
	 * clear, static separation between the DT command regions and the
	 * runtime custom DCS command regions, regardless of how many DT
	 * commands were actually packed.
	 */
	mutex_lock(&display_hfi->rt_dcs_cmd_lock);
	display_hfi->rt_custom_dcs_cmd_sde_base    = DSI_TX_CMD_BUF_DT_CMD_SIZE;
	display_hfi->rt_custom_dcs_cmd_sde_running = DSI_TX_CMD_BUF_DT_CMD_SIZE;
	display_hfi->rt_custom_dcs_cmd_hfi_base = display_hfi->running_hfi_offset;

	/*
	 * Reset the index array so stale entries from a previous panel init
	 * (e.g. after SSR) do not persist.
	 */
	memset(display_hfi->rt_custom_dcs_cmd_map, 0,
			sizeof(display_hfi->rt_custom_dcs_cmd_map));

	mutex_unlock(&display_hfi->rt_dcs_cmd_lock);

	if (display_hfi->kv_props)
		hfi_util_kv_helper_reset(display_hfi->kv_props);
	else
		display_hfi->kv_props = hfi_util_kv_helper_alloc(HFI_UTIL_MAX_ALLOC);

	SDE_EVT32(HFI_COMMAND_PANEL_INIT_PANEL_CAPS, SDE_EVTLOG_FUNC_CASE1);
	rc = dsi_hfi_append_panel_init_caps(buffer, display, panel_init_caps, addr_map);
	if (rc) {
		DSI_ERR("failed to append HFI_COMMAND_PANEL_INIT_PANEL_CAPS: rc = %d", rc);
		goto error_array;
	}

	SDE_EVT32(HFI_COMMAND_PANEL_INIT_GENERIC_CAPS, SDE_EVTLOG_FUNC_CASE3);
	rc = dsi_hfi_append_panel_generic_caps(buffer, display, panel_generic_caps);
	if (rc) {
		DSI_ERR("failed to append HFI_COMMAND_PANEL_INIT_GENERIC_CAPS: rc = %d", rc);
		goto error_array;
	}

	rc = hfi_adapter_set_cmd_buf(display_hfi->hfi_client, buffer);
	SDE_EVT32(HFI_COMMAND_PANEL_INIT_PANEL_CAPS, HFI_COMMAND_PANEL_INIT_GENERIC_CAPS,
				rc, SDE_EVTLOG_FUNC_CASE4);
	if (rc) {
		DSI_ERR("failed to send panel init: rc = %d", rc);
		goto error_array;
	}

	rc = dsi_hfi_send_panel_timing_modes(display, timing_caps_array);
	if (rc) {
		DSI_ERR("failed to append timing modes: rc = %d", rc);
		goto error_array;
	}

	SDE_EVT32(HFI_COMMAND_PANEL_INIT_TIMING_MODE_CAPS, rc, SDE_EVTLOG_FUNC_CASE5);

	kfree(timing_caps_array);
	return rc;

error_array:
	kfree(timing_caps_array);
error_addr_map:
	kfree(addr_map);
error_unmap_dva:
	rc = hfi_adapter_unmap_iova(display_hfi->hfi_client,
			display_hfi->sgt_tx_cmd_buf_map.remote_addr,
			display->cmd_buffer_size);
	if (rc)
		DSI_ERR("failed to unmap command buffer from FW\n");
	display_hfi->sgt_tx_cmd_buf_map.remote_addr = 0;
error_buff:
	rc = hfi_adapter_release_cmd_buf(display_hfi->hfi_client, buffer);
	if (rc)
		DSI_ERR("failed to release command buffer\n");

	return rc;
}
