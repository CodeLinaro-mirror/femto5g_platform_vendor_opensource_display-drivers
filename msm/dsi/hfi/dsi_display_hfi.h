/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
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

/**
 * struct dsi_rt_custom_dcs_cmd_entry - metadata for one runtime custom DCS command type override
 *
 * Tracks the location and size of runtime custom DCS commands captured into
 * the reserved space in the SDE buffer (tx_cmd_buf).
 *
 * @valid:            true if runtime custom DCS commands have been captured for this type
 * @sde_offset:       absolute byte offset from the start of the SDE buffer
 *                    (tx_cmd_buf) where this type's packetized MIPI commands
 *                    begin.
 * @count:            number of individual custom DCS commands captured for this type
 * @hfi_meta_offset:  absolute byte offset in the HFI shared buffer
 *                    (shared_addr_map) where the array of
 *                    dsi_hfi_panel_cmd_info structs for this type begins
 */
struct dsi_rt_custom_dcs_cmd_entry {
	bool valid;
	u32  sde_offset;
	u32  count;
	u32  hfi_meta_offset;
};

/**
 * struct dsi_display_hfi - dsi display hfi structure
 * @hfi_adapter:                      Pointer to hfi adapter structure
 * @hfi_client:                       Pointer to hfi client structure
 * @kv_props:                         Pointer to hfi util kv helper structure
 * @cmd_buf_worker:                   kthread worker
 * @shared_addr_map:                  Pointer to hold dcp shared buffer map addr
 * @esd_addr_map:                     Pointer to hold esd shared buffer map addr
 * @mode_valid:                       Indicate whether mode is valid
 * @tx_cmd_buf_dva:                   DCP virtual address of the DCS cmd tx buffer
 * @tx_cmd_buf_fill_level:            Tracks fill level of the DCS cmd tx buffer
 * @tx_cmd_buf_map:                   Address map of DCS command payload HFI buffer
 * @rx_cmd_buf_map:                   Address map of DCS command read HFI buffer
 * @running_sde_offset:               Offset for sde virtual address for dcs cmds.
 * @running_hfi_offset:               Offset for hfi shared memory address for dcs cmds.
 * @rt_custom_dcs_cmd_sde_base:       Fixed byte offset in the SDE buffer (tx_cmd_buf)
 *                                    where the runtime custom DCS command reserved
 *                                    region begins.
 * @rt_custom_dcs_cmd_sde_running:    Current write position within the reserved SDE
 *                                    space; advances as runtime custom DCS commands
 *                                    are captured.
 * @rt_custom_dcs_cmd_hfi_base:       Fixed byte offset in the HFI shared buffer
 *                                    (shared_addr_map) where the runtime custom DCS
 *                                    command metadata reserved region begins.
 * @rt_custom_dcs_cmd_hfi_running:    Current write position in HFI shared buffer for
 *                                    runtime custom DCS command metadata; advances as
 *                                    commands are captured.
 * @rt_custom_dcs_cmd_map:            Fixed-size array of dsi_rt_custom_dcs_cmd_entry,
 *                                    one entry per standard command type
 *                                    (DSI_CMD_SET_MAX entries), indexed by
 *                                    enum dsi_cmd_set_type.
 * @rt_dcs_cmd_lock:                  Mutex protecting all runtime custom DCS
 *                                    command shared state:
 *                                    rt_custom_dcs_cmd_sde_base/running,
 *                                    rt_custom_dcs_cmd_hfi_base/running, and
 *                                    rt_custom_dcs_cmd_map.
 */
struct dsi_display_hfi {
	struct hfi_adapter_t *hfi_adapter;
	struct hfi_client_t *hfi_client;
	struct hfi_util_kv_helper *kv_props;

	struct kthread_worker cmd_buf_worker;
	struct hfi_shared_addr_map *shared_addr_map;
	struct hfi_shared_addr_map *esd_addr_map;

	bool mode_valid;
	struct hfi_shared_addr_map sgt_tx_cmd_buf_map;
	u32 tx_cmd_buf_fill_level;
	struct hfi_shared_addr_map tx_cmd_buf_map;
	struct hfi_shared_addr_map rx_cmd_buf_map;

	u32 running_sde_offset;
	u32 running_hfi_offset;

	/* Runtime custom DCS command support */
	u32 rt_custom_dcs_cmd_sde_base;
	u32 rt_custom_dcs_cmd_sde_running;
	u32 rt_custom_dcs_cmd_hfi_base;
	u32 rt_custom_dcs_cmd_hfi_running;
	struct dsi_rt_custom_dcs_cmd_entry rt_custom_dcs_cmd_map[DSI_CMD_SET_MAX];
	struct mutex rt_dcs_cmd_lock;
};

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

/**
 * dsi_display_setup_ops() - setup hlos/hfi display ops
 * @display: Pointer to dsi_display structure
 */
void dsi_display_setup_ops(struct dsi_display *display);

#endif /* _DSI_DISPLAY_HFI_H_ */
