/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DSI_HFI_HEADER_H_
#define _DSI_HFI_HEADER_H_

#include <linux/types.h>

#include "msm_drv.h"
#include "dsi_defs.h"
#include "dsi_display.h"
#include "dsi_display_hfi.h"
#include "hfi_utils.h"
#include "hfi_defs_panel.h"

#define MAX_NUM_CTRLS_AND_LENGTH 3
#define MAX_NUM_PHYS_AND_LENGTH 3
#define CLK_RATE_SIZE 2
#define JITTER_SIZE 2
#define NUM_VARIABLE_DPHY_TIMINGS 14

/*
 * MAX_ALLOWED_DCS_CMD_TYPES - maximum number of DCS command types supported over HFI.
 *
 * The dsize field in HFI_PACKKEY is 8-bit (bits[31:24], max = U8_MAX = 255).
 * When sending DCS cmd info, dsize is computed as:
 *
 *   dsize = (sizeof(struct dsi_hfi_panel_per_cmd_type) / sizeof(u32)) * count + 1
 *
 * where:
 *   - sizeof(struct dsi_hfi_panel_per_cmd_type) / sizeof(u32) = 5  (5 u32 fields per entry)
 *   - +1 accounts for the 'count' header field in dsi_hfi_per_cmd_type_payload
 *
 * For dsize to not overflow the 8-bit field:
 *   5 * count + 1 <= 255  =>  count <= 50
 *
 */
#define MAX_ALLOWED_DCS_CMD_TYPES  50

/**
 * struct dsi_value_to_prop_lookup - contains map with hfi properties and
 *                                   corresponding values
 * @value:               value
 * @hfi_prop:            hfi property
 * @use_default_val:     if true, property takes default value even if not present;
 *                       if false, property needs to be checked for presence
 */
struct dsi_value_to_prop_lookup {
	u32 value;
	u32 hfi_prop;
	bool use_default_val;
};

/**
 * struct dsi_panel_init_caps - contains properties to be sent as part of
 * HFI_COMMAND_PANEL_INIT_PANEL_CAPS
 * @num_timing_modes:               HFI_PROPERTY_PANEL_TIMING_MODE_COUNT
 * @dcs_cmd_tx_buf_dva:             HFI_PROPERTY_PANEL_DCS_CMD_TX_BUF_DVA
 * @dcs_cmd_tx_buf_iova:            HFI_PROPERTY_PANEL_DCS_CMD_TX_BUF_IOVA
 */
struct dsi_panel_init_caps {
	u32 num_timing_modes;
	u64 dcs_cmd_tx_buf_dva;
	u64 dcs_cmd_tx_buf_iova;
};

/**
 * struct dsi_hfi_panel_cmd_info - contains information per command
 * @cmd_offset:         offset in dpu mapped buffer
 * @size:               size of command
 * @delay:              delay for command
 * @ctrl_flags:         panel ctrl flags
 * @mode:               panel sending mode (LP/HS)
 */
struct dsi_hfi_panel_cmd_info {
	u32 cmd_offset;
	u32 size;
	u32 delay;
	u32 ctrl_flags;
	u32 mode;
	u32 reserved1;
	u32 reserved2;
};

/**
 * struct dsi_hfi_panel_per_cmd_type - contains information per command type
 * @cmd_offset:              offset in dpu mapped buffer for commands
 * @cmd_type:                type of command
 * @count_cmds:              count of command
 * @hfi_buff_struct_offset:  offset in dcp mapped buffer for structs
 */
struct dsi_hfi_panel_per_cmd_type {
	u32 sde_buff_type_offset;
	enum hfi_panel_dcs_command_type cmd_type;
	u32 count_cmds;
	u32 hfi_buff_struct_offset;
	u32 reserved_key;
};

/**
 * struct dsi_hfi_per_cmd_type_payload - payload with all DCS command types
 *
 * @count:                         count
 * @hfi_per_type_array:            payload per type
 */
struct dsi_hfi_per_cmd_type_payload  {
	u32 count;
	struct dsi_hfi_panel_per_cmd_type hfi_per_type_array[DSI_CMD_SET_TOTAL_SIZE];
};

/**
 * struct dsi_hfi_topology_payload - payload with topology info
 *
 * @count:                          count
 * @hfi_topology:                   hfi topology payload
 */
struct dsi_hfi_topology_payload {
	u32 count;
	struct hfi_panel_topology hfi_topology;
};

/**
 * struct dsi_hfi_phy_timings_payload - payload with DSI PHY Panel Timings from DT
 *
 * @count:                          count
 * @dphy_timings:                   DSI PHY Panel Timings
 */
struct dsi_hfi_phy_timings_payload {
	u32 count;
	u32 dphy_timings[NUM_VARIABLE_DPHY_TIMINGS];
};

/**
 * struct dsi_hfi_cmd_set_remap_payload - Complete payload for command set remapping
 * @count: Number of remapping entries
 * @entries: Array of remapping entries
 *
 * This structure encapsulates the complete payload for
 * HFI_COMMAND_DISPLAY_DSI_CUSTOM_DCS_CMDS_SET_REMAP.
 */
struct dsi_hfi_cmd_set_remap_payload {
	u32 count;
	struct hfi_cmd_set_remap entries[DSI_CMD_SET_MAX];
};

/**
 * struct dsi_hfi_dcs_cmd_set_replace_payload - complete payload for
 *                                              HFI_COMMAND_DISPLAY_DSI_CUSTOM_DCS_CMDS_SET_REPLACE
 *
 * Carries all DCS command type replacements in a single HFI call. The count
 * field indicates how many entries follow. Each entry describes one standard
 * command type being replaced and where its runtime-defined custom DCS commands are
 * located in the SDE and HFI shared buffers.
 *
 * @count:   number of valid entries in the entries array
 * @entries: array of per-type replacement info, one per command type being
 *           replaced (at most DSI_CMD_SET_MAX entries)
 */
struct dsi_hfi_dcs_cmd_set_replace_payload {
	u32 count;
	struct hfi_dsi_dcs_cmd_set_replace_entry entries[DSI_CMD_SET_MAX];
};

/**
 * struct dsi_panel_timing_caps - contains properties to be sent as part of
 * HFI_COMMAND_PANEL_INIT_TIMING_CAPS
 * @panel_index:                    HFI_PROPERTY_PANEL_INDEX
 * @clockrate:                      HFI_PROPERTY_PANEL_CLOCKRATE lsb/msb
 * @framerate:                      HFI_PROPERTY_PANEL_FRAMERATE
 * @panel_jitter:                   HFI_PROPERTY_PANEL_JITTER numerator/denominator
 * @hsync_pulse:                    HFI_PROPERTY_PANEL_H_SYNC_PULSE
 * @res_data:                       HFI_PROPERTY_PANEL_RESOLUTION_DATA
 * @compression_params:             HFI_PROPERTY_PANEL_COMPRESSION_DATA
 * @topology:                       topology information
 * top_index:                       index of default topology
 * running_hfi_offset:              offset of pointer in hfi mapped buffer
 * payload:                         panel cmd information
 * phy_timings_payload:             DSI PHY panel tmgs info
 * compression_rc_override:         Custom RC parameters for compression, if applicable
 * @esync_timing_caps:              HFI_PROPERTY_PANEL_ESYNC_TIMING_CAPS
 * @qsync_timing_params:            HFI_PROPERTY_PANEL_QSYNC_TIMING_PARAMS
 */
struct dsi_panel_timing_caps {
	u32 panel_index;
	u32 clockrate[CLK_RATE_SIZE];
	u32 framerate;
	u32 panel_jitter[JITTER_SIZE];
	u32 hsync_pulse;
	struct hfi_panel_res_data res_data;
	struct hfi_panel_compression_params compression_params;
	struct dsi_hfi_topology_payload topology;
	u32 top_index;
	u32 running_hfi_offset;
	struct dsi_hfi_per_cmd_type_payload payload;
	struct dsi_hfi_phy_timings_payload phy_timings_payload;
	bool rc_override_enabled;
	struct hfi_panel_compression_rc_override rc_override;
	struct hfi_panel_esync_caps esync_timing_caps;
	struct hfi_qsync_params qsync_timing_params;
};

/**
 * struct dsi_panel_generic_caps - contains properties to be sent as part of
 * HFI_COMMAND_PANEL_INIT_GENERIC_CAPS
 * @panel_name:                     HFI_PROPERTY_PANEL_NAME
 * @panel_type:                     HFI_PROPERTY_PANEL_PHYSICAL_TYPE
 * @display_type:                   HFI_PROPERTY_PANEL_DISPLAY_TYPE
 * @panel_bpp:                      HFI_PROPERTY_PANEL_BPP
 * @panels_lanes_state:             HFI_PROPERTY_PANEL_LANES_STATE
 * @panel_lane_map:                 HFI_PROPERTY_PANEL_LANE_MAP
 * @color_order_type:               HFI_PROPERTY_PANEL_COLOR_ORDER
 * @dma_trigger_type:               HFI_PROPERTY_PANEL_DMA_TRIGGER
 * @mdp_trigger_type:               HFI_PROPERTY_PANEL_STREAM_TRIGGER
 * @te_mode:                        HFI_PROPERTY_PANEL_TE_MODE
 * @dma_sched_line:                 HFI_PROPERTY_PANEL_DMA_SCHEDULE_LINE
 * @dma_sched_window:               HFI_PROPERTY_PANEL_DMA_SCHEDULE_WINDOW
 * @tx_eot_append:                  HFI_PROPERTY_PANEL_TX_EOT_APPEND
 * @eof_power_mode:                 HFI_PROPERTY_PANEL_BLLP_EOF_POWER_MODE
 * @bllp_power_mode:                HFI_PROPERTY_PANEL_BLLP_POWER_MODE
 * @traffic_mode:                   HFI_PROPERTY_PANEL_TRAFFIC_MODE
 * @virtual_channel_id:             HFI_PROPERTY_PANEL_VIRTUAL_CHANNEL_ID
 * @wr_mem_start:                   HFI_PROPERTY_PANEL_WR_MEM_START
 * @wr_mem_continue:                HFI_PROPERTY_PANEL_WR_MEM_CONTINUE
 * @te_dcs_command:                 HFI_PROPERTY_PANEL_TE_DCS_COMMAND
 * @panel_op_mode:                  HFI_PROPERTY_PANEL_PANEL_OPERATING_MODE
 * @min_backlight_level:            HFI_PROPERTY_PANEL_BL_MIN_LEVEL
 * @max_backlight_level:            HFI_PROPERTY_PANEL_BL_MAX_LEVEL
 * @max_brightness_level:           HFI_PROPERTY_PANEL_BRIGHTNESS_MAX_LEVEL
 * @backlight_ctrl_prim:            HFI_PROPERTY_PANEL_BL_PMIC_CONTROL_TYPE
 * @backlight_ctrl_sec:             HFI_PROPERTY_PANEL_SEC_BL_PMIC_CONTROL_TYPE
 * @is_bl_inverted:                 HFI_PROPERTY_PANEL_BL_INVERTED_DBV
 * @vsync_src:                      HFI_PROPERTY_PANEL_VSYNC_SOURCE
 * @ctrl_nums:                      HFI_PROPERTY_PANEL_CTRL_NUM
 * @phy_nums:                       HFI_PROPERTY_PANEL_PHY_NUM
 * @cphy_enabled:                   HFI_PROPERTY_PANEL_CPHY_MODE
 * @esd_config:                     HFI_PROPERTY_PANEL_ESD_CONFIG
 * @esync_caps:                     HFI_PROPERTY_PANEL_ESYNC_CAPS
 * @dfps_caps:                      HFI_PROPERTY_PANEL_DFPS_CAPS
 * @lp11_init:                      HFI_PROPERTY_PANEL_LP11_INIT
 * @poms_caps:                      HFI_PROPERTY_PANEL_OPERATING_SWITCH_CAPABILITY
 * @custom_cmd_set_info:            HFI_PROPERTY_PANEL_DSI_CUSTOM_DCS_CMDS_SET_INFO
 * @ulps_supported:                 HFI_PROPERTY_PANEL_ULPS_SUPPORTED
 */
struct dsi_panel_generic_caps {
	u32 panel_name;
	enum hfi_panel_phy_type panel_type;
	enum hfi_panel_display_type display_type;
	enum hfi_panel_bpp panel_bpp;
	enum hfi_panel_lane_enable panels_lanes_state;
	enum hfi_panel_lane_map panel_lane_map;
	enum hfi_panel_color_order_type color_order_type;
	enum hfi_panel_trigger_type dma_trigger_type;
	enum hfi_panel_trigger_type mdp_trigger_type;
	u32 te_mode;
	u32 dma_sched_line;
	u32 dma_sched_window;
	u32 tx_eot_append;
	u32 eof_power_mode;
	u32 bllp_power_mode;
	enum hfi_panel_fps_traffic_mode traffic_mode;
	u32 virtual_channel_id;
	u32 wr_mem_start;
	u32 wr_mem_continue;
	u32 te_dcs_command;
	enum hfi_panel_modes panel_op_mode;
	u32 min_backlight_level;
	u32 max_backlight_level;
	u32 max_brightness_level;
	enum hfi_panel_backlight_ctrl backlight_ctrl_prim;
	enum hfi_panel_backlight_ctrl backlight_ctrl_sec;
	u32 is_bl_inverted;
	enum hfi_panel_vsync_source vsync_src;
	u32 ctrl_nums[MAX_NUM_CTRLS_AND_LENGTH];
	u32 phy_nums[MAX_NUM_PHYS_AND_LENGTH];
	bool cphy_enabled;
	struct hfi_panel_esd_config esd_config;
	struct hfi_panel_esync_caps esync_caps;
	struct hfi_panel_dfps_caps dfps_caps;
	u32 lp11_init;
	struct hfi_panel_operating_mode_caps poms_caps;
	u32 custom_cmd_set_info[2]; /* [0]=start_index, [1]=count */
	u32 ulps_supported;
};

/**
 * struct dsi_hfi_cb - dsi hfi callback
 * @client:                             hfi client
 * @cmd_buf:                            hfi cmd buff
 * @cmd_buff_work:                      cmd buff worker
 */
struct dsi_hfi_cb {
	struct hfi_client_t *client;
	struct hfi_cmdbuf_t *cmd_buf;
	struct kthread_work cmd_buff_work;
};

/**
 * dsi_get_esd_status_mode_helper() - translate esd check status mode
 *		into enum hfi_panel_esd_status_mode.
 * @mode:	enum esd_check_status_mode mode
 *
 * Return: enum hfi_panel_esd_status_mode.
 */
enum hfi_panel_esd_status_mode dsi_get_esd_status_mode_helper(enum esd_check_status_mode mode);

/**
 * dsi_hfi_process_cmd_buf() - process hfi command buffer
 * @hfi_client:	handle to hfi client
 * @cmd_buf: handle to cmd buffer
 *
 * Return: error code.
 */
int dsi_hfi_process_cmd_buf(struct hfi_client_t *hfi_client, struct hfi_cmdbuf_t *cmd_buf);

/**
 * dsi_hfi_prop_handler() - handle/parse hfi properties
 * @UNIQUE_DISP_OR_OBJ_ID:	display/object ID
 * @CMD_ID: command ID
 * @payload: handle to payload
 * @size: size of payload
 * @listener: handle to hfi property listener
 */
void dsi_hfi_prop_handler(u32 UNIQUE_DISP_OR_OBJ_ID, u32 CMD_ID, void *payload, u32 size,
						struct hfi_prop_listener *listener);

/**
 * dsi_display_hfi_setup_hfi() - setup dsi as a client of hfi
 * @display: handle to dsi display structure
 * @hfi_host: handle to hfi host
 *
 * Return: error code.
 */
int dsi_display_hfi_setup_hfi(struct dsi_display *display, struct hfi_adapter_t *hfi_host);

/**
 * dsi_display_hfi_send_cmd_buf() - dsi wrapper for sending hfi cmd buffer
 * @display: handle to dsi display structure
 * @hfi_client: handle to hfi client
 * @hfi_cmd: hfi command
 * @display_type: display type string
 * @hfi_payload_type: hfi payload type
 * @payload: handle to payload
 * @payload_size: payload size
 * @flags: flags
 *
 * Return: error code.
 */
int dsi_display_hfi_send_cmd_buf(struct dsi_display *display,
					struct hfi_client_t *hfi_client, u32 hfi_cmd,
					const char *display_type, u32 hfi_payload_type,
					void *payload, u32 payload_size, u32 flags);

/**
 * dsi_hfi_panel_init() - submit dsi dtsi panel information to hfi
 * @display: handle to dsi display structure
 * @panel: handle to dsi panel structure
 *
 * Return: error code.
 */
int dsi_hfi_panel_init(struct dsi_display *display, struct dsi_panel *panel);

/**
 * dsi_display_hfi_register_pwr_supplies() - register power supplies with hfi
 * @display: handle to dsi display structure
 *
 * Return: error code.
 */
int dsi_display_hfi_register_pwr_supplies(struct dsi_display *display);

/**
 * dsi_hfi_misr_setup() - setup dsi hfi misr
 * @display: handle to dsi display structure
 *
 * Return: error code.
 */
int dsi_hfi_misr_setup(struct dsi_display *display);

/**
 * dsi_hfi_misr_read() - read dsi misr values from hfi
 * @display: handle to dsi display structure
 *
 * Return: error code.
 */
int dsi_hfi_misr_read(struct dsi_display *display);

/**
 * dsi_hfi_host_transfer_sub() - transfers DSI commands from host to DCP
 * @host:                pointer to the DSI mipi host device
 * @cmd:                 DSI command to be transferred
 *
 * This function handles the transfer of DSI commands to the Display Control
 * Processor (DCP) via the Hardware-Firmware Interface (HFI).
 *
 * Return: 0 on success, negative error code on failure
 */
int dsi_hfi_host_transfer_sub(struct mipi_dsi_host *host, struct dsi_cmd_desc *cmd);

/**
 * dsi_hfi_add_dsi_cmd_remap() - add DSI command set remapping entries
 * @display: handle to dsi display structure
 * @cmd_remap_table: pointer to array of size DSI_CMD_SET_MAX indexed by
 *                   standard command type, where each entry holds the
 *                   custom_cmd_type to remap to, or DSI_CMD_SET_MAX as a
 *                   "no remap" sentinel.
 * @table_size: size of cmd_remap_table; must equal DSI_CMD_SET_MAX
 * @resp_req: if true, HFI_HOST_FLAGS_RESPONSE_REQUIRED is appended to the
 *            flags when sending the HFI command, causing the call to block
 *            until DCP acknowledges the remapping.
 *
 * This function validates each entry in cmd_remap_table and
 * constructs an HFI payload which is sent to the DCP in a single operation.
 *
 * The function validates:
 * - table_size equals DSI_CMD_SET_MAX
 * - All custom_cmd_type values are within the valid range (< DSI_CUSTOM_CMD_SET_MAX)
 * - Each custom_cmd_type is either in the custom range (>= DSI_CUSTOM_CMD_SET_START_IDX)
 *   or equals the standard command type (pointing back to the original mapping)
 *
 * Return: 0 on success, negative error code on failure
 */
int dsi_hfi_add_dsi_cmd_remap(struct dsi_display *display,
		u32 *cmd_remap_table, u32 table_size, bool resp_req);

/**
 * dsi_hfi_add_rt_custom_dcs_cmd() - capture runtime custom DCS commands
 *                                       into the reserved SDE buffer space
 * @display:   handle to dsi display structure
 * @cmd_type:  standard command type to override; must be < DSI_CMD_SET_MAX
 * @data:      raw DCS command bytes in device tree format:
 *             each command = [type][ctrl][chan][flags][wait][len_hi][len_lo][payload...]
 * @length:    total byte length of data
 * @state:     DSI_CMD_SET_STATE_LP or DSI_CMD_SET_STATE_HS
 *
 * Parses the raw DT-format bytes, packetizes each custom DCS command into MIPI
 * DSI wire format, and captures them into the reserved space in the SDE buffer.
 *
 * Per-command metadata (dsi_hfi_panel_cmd_info: offset, size, delay, flags,
 * mode) is captured into the reserved space in the HFI shared buffer.
 *
 * This function may be called multiple times for different cmd_type values,
 * or multiple times for the same cmd_type to override a previous capture.
 * When overriding, the new commands are appended at the current write position
 * (the old space is no longer referenced).
 *
 * dsi_hfi_send_dcs_cmd_set_replace_cmd() needs to be called once after all
 * captures are complete for the custom commands to take effect on the DCP side.
 *
 * Return: 0 on success, negative error code on failure
 */
int dsi_hfi_add_rt_custom_dcs_cmd(struct dsi_display *display,
				      enum dsi_cmd_set_type cmd_type,
				      const u8 *data, u32 length,
				      enum dsi_cmd_set_state state);

/**
 * dsi_hfi_send_dcs_cmd_set_replace_cmd() - send all captured runtime custom DCS command
 * replacements to DCP
 * @display:   handle to dsi display structure
 * @resp_req:  if true, HFI_HOST_FLAGS_RESPONSE_REQUIRED is appended to the flags
 *             when sending the HFI command, causing the call to block until DCP
 *             acknowledges the replacement.
 *
 * Sends a single HFI_COMMAND_DISPLAY_DSI_CUSTOM_DCS_CMDS_SET_REPLACE command
 * to DCP carrying all DCS command types that have been captured via
 * dsi_hfi_add_rt_custom_dcs_cmd(). DCP will replace the standard command
 * metadata for each included type with the corresponding runtime custom DCS commands.
 *
 * The payload is a dsi_hfi_dcs_cmd_set_replace_payload struct: a count field followed
 * by an array of hfi_dsi_dcs_cmd_set_replace_entry entries, one per captured type.
 *
 * Return: 0 on success, negative error code on failure
 *         -EINVAL  if params invalid
 *         -ENOENT  if no runtime custom DCS commands have been captured
 */
int dsi_hfi_send_dcs_cmd_set_replace_cmd(struct dsi_display *display, bool resp_req);

/**
 * dsi_hfi_exec_dcs_cmd_type() - instruct DCP to immediately execute a DCS command set
 * @display:   handle to dsi display structure
 * @cmd_type:  DCS command type to execute; either a standard command type
 *             value (range [0, DSI_CMD_SET_MAX)), or a custom command type index within
 *             the range [DSI_CUSTOM_CMD_SET_START_IDX, DSI_CUSTOM_CMD_SET_MAX)
 * @resp_req:  if true, HFI_HOST_FLAGS_RESPONSE_REQUIRED is appended to the flags,
 *             causing the call to block until DCP acknowledges execution
 *
 * Sends HFI_COMMAND_DISPLAY_EXEC_DCS_CMD_TYPE to DCP, instructing it to immediately
 * execute the pre-configured DCS command set identified by cmd_type.
 *
 * Return: 0 on success, negative error code on failure
 *         -EINVAL  if params are invalid or cmd_type is out of range
 */
int dsi_hfi_exec_dcs_cmd_type(struct dsi_display *display, u32 cmd_type, bool resp_req);

/**
 * dsi_hfi_tx_cmd_set() - transfers a set of DSI write commands from host to DCP
 *                        in a single HFI operation
 * @display:              pointer to the DSI display structure
 * @cmd_set:              pointer to the panel command set containing an array of DSI commands
 *
 * Packs all commands in @cmd_set into a single HFI_COMMAND_DISPLAY_TRANSFER_DCS_CMD_SET
 * packet, copying all TX payloads into one contiguous DCP-mapped shared memory region.
 * This avoids the per-command HFI round-trip overhead of dsi_hfi_host_transfer_sub().
 * Only write (TX) commands are supported; read commands are not handled by this function.
 *
 * Return: 0 on success, negative error code on failure
 */
int dsi_hfi_tx_cmd_set(struct dsi_display *display, struct dsi_panel_cmd_set *cmd_set);

#endif
