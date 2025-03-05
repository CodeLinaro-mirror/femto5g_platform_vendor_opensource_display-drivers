/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DSI_DISPLAY_HFI_H_
#define _DSI_DISPLAY_HFI_H_

#include <linux/types.h>

#include "msm_drv.h"
#include "dsi_defs.h"
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_pwr.h"
#include "hfi_adapter.h"
#include "hfi_props.h"
#include "hfi_utils.h"

#define to_dsi_display_hfi(x) container_of(x, struct dsi_display_hfi, dsi_display)

struct dsi_display_hfi {
	struct dsi_display dsi_display;

	struct dsi_power_info ctrl_pwr_info;
	struct dsi_power_info phy_pwr_info;

	struct hfi_prop_listener hfi_cb_obj;
	struct hfi_prop_listener misr_read_listener;
	struct hfi_adapter_t *hfi_adapter;
	struct hfi_client_t *hfi_client;
	struct hfi_util_kv_helper *kv_props;

	struct kthread_worker cmd_buf_worker;

	bool mode_valid;
};

struct dsi_hfi_panel_cmd_info {
	u32 cmd_offset;
	u32 size;
	u32 delay;
	u32 ctrl_flags;
	u32 mode;
	u32 reserved1;
	u32 reserved2;
};

struct dsi_hfi_panel_per_cmd_type {
	u32 sde_buff_type_offset;
	enum hfi_panel_dcs_command_type cmd_type;
	u32 count_cmds;
	u32 hfi_buff_struct_offset;
	u32 reserved_key;
};

/**
 * dsi_display_hfi_set_mode() - send info to HFI containing mode info,
 *							HFI will set mode and update dfps + bpp
 * @display: Pointer to dsi_display structure
 * @mode: contains selected mode info
 * Return: error code (0 on success)
 */
int dsi_display_hfi_set_mode(struct dsi_display *display, struct dsi_display_mode *mode);

/**
 * dsi_display_hfi_prepare() - enable clocks, send panel pre on commands, panel power on
 * @display: Pointer to dsi_display structure

 * Return: error code (0 on success)
 */
int dsi_display_hfi_prepare(struct dsi_display *display);

/**
 * dsi_display_hfi_enable() - send panel on commands
 * @display: Pointer to dsi_display structure

 * Return: error code (0 on success)
 */
int dsi_display_hfi_enable(struct dsi_display *display);

/**
 * dsi_display_hfi_post_enable() - send panel post enable + post on commands
 * @display: Pointer to dsi_display structure

 * Return: error code (0 on success)
 */
int dsi_display_hfi_post_enable(struct dsi_display *display);

/**
 * dsi_display_hfi_pre_disable() - send panel pre off commands
 * @display: Pointer to dsi_display structure

 * Return: error code (0 on success)
 */
int dsi_display_hfi_pre_disable(struct dsi_display *display);

/**
 * dsi_display_hfi_disable() - disable cmd/video engine, turn off panel supplies,
 *			     send panel off commands
 * @display: Pointer to dsi_display structure

 * Return: error code (0 on success)
 */
int dsi_display_hfi_disable(struct dsi_display *display);

/**
 * dsi_display_hfi_unprepare() - panel unprepare, deinit ctrl, disable phy
 * @display: Pointer to dsi_display structure

 * Return: error code (0 on success)
 */
int dsi_display_hfi_unprepare(struct dsi_display *display);

/**
 * dsi_display_hfi_mode_fixup() - send adjusted mode to HFI for validation and necessary operations
 * @display: Pointer to dsi_display structure
 * @mode: current mode
 * Return: true on success, else false
 */
bool dsi_display_hfi_mode_fixup(struct dsi_display *display, struct dsi_display_mode *mode);

/**
 * dsi_display_hfi_panel_enable_supplies() - control panel power supplies
 * @display: Pointer to dsi_display structure
 * @enable: enable or disable
 * Return: error code (0 on success)
 */
int dsi_display_hfi_panel_enable_supplies(struct dsi_display *display, bool enable);

/**
 * dsi_hfi_packetize_panel_cmd() - allocate memory for tx command buffer
 * @cmd_desc: individual command description structure
 * @size_of_indv_cmd: size of individual command
 * @buffer: handle to buffer
 * Return: error code (0 on success)
 */
int dsi_hfi_packetize_panel_cmd(struct dsi_cmd_desc *cmd_desc, u32 *size_of_indv_cmd, u8 *buffer);

/**
 * dsi_hfi_host_alloc_cmd_tx_buffer() - allocate memory for tx command buffer
 * @display: Pointer to dsi_display structure
 * Return: error code (0 on success)
 */
int dsi_hfi_host_alloc_cmd_tx_buffer(struct dsi_display *display);

#endif /* _DSI_DISPLAY_HFI_H_ */
