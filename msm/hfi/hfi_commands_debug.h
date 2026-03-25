/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_COMMANDS_DEBUG_H__
#define __H_HFI_COMMANDS_DEBUG_H__

/*
 * This is documentation file. Not used for header inclusion.
 */

/*
 * All Debug level commands begin here.
 * "1st MSB byte = 0xFF"
 */
#define HFI_COMMAND_DEBUG_BEGIN                                      0xFF000000

/*
 * hfi_header sample for any debug command:
 * hfi_header.cmd_buff_info.size          : (sizeof(hfi_header) +
 *                                            sizeof(hfi_packet)
 *           .cmd_buff_info.type          : HFI_DEBUG
 *           .device_id                   : 0
 *           .object_id                   : n/a
 *           .timestamp_hi                : ts_hi
 *           .timestamp_lo                : ts_lo
 *           .header_id                   : unique id
 *           .num_packets                 : 'n' packets depending upon the
 *                                          packet cmd
 *
 * hfi_packet sample for any debug packet:
 * hfi_packet.payload_info.size           : sizeof(hfi_packet)
 *                                          (including payload size)
 *           .payload_info.type           : one of enum
 *                                          'hfi_packet_payload_type'
 *           .cmd                         : HFI_COMMAND_DEBUG_XXX
 *           .flags (Host to DCP)         : HFI_TX_FLAGS_XXX
 *                                          (enum hfi_packet_host_flags)
 *                  (DCP to Host)         : HFI_RX_FLAGS_XXX
 *                                          (enum hfi_fw_host_flags)
 *           .id                          : 0
 *           .packet_id                   : unique id
 */

/*
 * HFI_COMMAND_DEBUG_LOOPBACK_NONE - This is the LOOPBACK command
 * This command does not hold any payload.
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_NONE
 *           .cmd                      : HFI_COMMAND_DEBUG_LOOPBACK_NONE
 *           .flags                    : HFI_TX_FLAGS_RESPONSE_REQUIRED
 */
#define HFI_COMMAND_DEBUG_LOOPBACK_NONE                              0xFF000001
/*
 * HFI_COMMAND_DEBUG_LOOPBACK_U32 is the U32 LOOPBACK command,
 * DCP Receives LOOPBACK command with the u32 test pattern and
 * upon receiving this command, the same data is returned
 * to HLOS.
 *
 * hfi_header.num_packets                 : 1
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_U32
 *           .cmd                      : HFI_COMMAND_DEBUG_LOOPBACK_U32
 *           .flags                    : HFI_TX_FLAGS_RESPONSE_REQUIRED
 *           .payload                  : u32 test pattern
 */
#define HFI_COMMAND_DEBUG_LOOPBACK_U32                               0xFF000002
/*
 * HFI_COMMAND_DEBUG_LOOPBACK_U64 is the U64 LOOPBACK command,
 * DCP Receives LOOPBACK command with the u64 test pattern and
 * upon receiving this command, the same data is returned
 * to HLOS.
 *
 * hfi_header.num_packets                 : 1
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_U64
 *           .cmd                      : HFI_COMMAND_DEBUG_LOOPBACK_U64
 *           .flags                    : HFI_TX_FLAGS_RESPONSE_REQUIRED
 *           .payload                  : u64 test pattern
 */
#define HFI_COMMAND_DEBUG_LOOPBACK_U64                               0xFF000003
/*
 * HFI_COMMAND_DEBUG_LOOPBACK_BLOB is the BLOB LOOPBACK command,
 * DCP Receives LOOPBACK command with the blob of data and
 * upon receiving this command, the same data is returned
 * to HLOS.
 *
 * hfi_header.num_packets                 : 1
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_BLOB
 *           .cmd                      : HFI_COMMAND_DEBUG_LOOPBACK_BLOB
 *           .flags                    : HFI_TX_FLAGS_RESPONSE_REQUIRED
 *           .payload                  : blob of data
 */
#define HFI_COMMAND_DEBUG_LOOPBACK_BLOB                              0xFF000004
/*
 * HFI_COMMAND_DEBUG_LOOPBACK_U32_ARRAY is the U32_ARRAY LOOPBACK command,
 * DCP Receives LOOPBACK command with the u32 array test pattern and
 * upon receiving this command, the same data is returned
 * to HLOS.
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_U32_ARRAY
 *           .cmd                      : HFI_COMMAND_DEBUG_LOOPBACK_U32_ARRAY
 *           .flags                    : HFI_TX_FLAGS_RESPONSE_REQUIRED
 *           .payload                  : u32 test_value[32]
 */
#define HFI_COMMAND_DEBUG_LOOPBACK_U32_ARRAY                         0xFF000005
/*
 * HFI_COMMAND_DEBUG_LOOPBACK_U64_ARRAY is the U64_ARRAY LOOPBACK command,
 * DCP Receives LOOPBACK command with the u64 array test pattern and
 * upon receiving this command, the same data is returned
 * to HLOS.
 *
 * hfi_header.num_packets                 : 1
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_U64_ARRAY
 *           .cmd                      : HFI_COMMAND_DEBUG_LOOPBACK_U64_ARRAY
 *           .flags                    : HFI_TX_FLAGS_RESPONSE_REQUIRED
 *           .payload                  : u64 test_value[64]
 */
#define HFI_COMMAND_DEBUG_LOOPBACK_U64_ARRAY                         0xFF000006

/*
 * HFI_COMMAND_DEBUG_MISR_SETUP is the MISR SETUP command,
 * DCP Receives MISR command with setup instructions. Respond with
 * ack for success.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_U32_ARRAY
 *           .cmd                      : HFI_COMMAND_DEBUG_MISR_SETUP
 *           .flags                    : HFI_TX_FLAGS_RESPONSE_REQUIRED
 *           .payload                  : struct hfi_debug_misr_setup_data
 *
 * DCP to Host:
 * hfi_header.num_packets                 : 1
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_NONE
 *           .cmd                      : HFI_COMMAND_DEBUG_MISR_SETUP
 *           .flags                    : HFI_RX_FLAGS_SUCCESS
 */
#define HFI_COMMAND_DEBUG_MISR_SETUP                                 0xFF000007
/*
 * HFI_COMMAND_DEBUG_MISR_READ is the MISR READ command,
 * DCP Receives MISR command with read instructions. Respond with
 * MISR value for modules in the specified layer.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_U32_ARRAY
 *           .cmd                      : HFI_COMMAND_DEBUG_MISR_READ
 *           .flags                    : HFI_TX_FLAGS_RESPONSE_REQUIRED
 *           .payload                  : struct hfi_debug_misr_read_data
 *
 * DCP to Host:
 * hfi_header.num_packets                 : 1
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_U32_ARRAY
 *           .cmd                      : HFI_COMMAND_DEBUG_MISR_READ
 *           .flags                    : HFI_RX_FLAGS_SUCCESS
 *           .payload                  : struct misr_read_data_ret
 *
 * struct misr_read_data_ret - MISR read return data
 * @module_type     : module type
 * @num_misr        : number of modules
 * @misr_value      : misr values obtained
 *
 * struct misr_read_data_ret {
 *   enum hfi_debug_misr_module_type module_type;
 *   u32 num_misr;
 *   u32 misr_value[num_modules];
 * };
 */
#define HFI_COMMAND_DEBUG_MISR_READ                                  0xFF000008
/*
 * HFI_COMMAND_DEBUG_INIT is sent from host to DCP to obtain buffer allocation
 * size for panic events.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_U32_ARRAY
 *           .cmd                      : HFI_COMMAND_DEBUG_INIT
 *           .flags                    : HFI_TX_FLAGS_RESPONSE_REQUIRED
 *           .payload (u32 values)     : u32 device_id
 *
 * DCP to Host:
 * hfi_header.num_packets                 : 1
 *
 * Data Contents:
 *  DCP replies back with <key,value> pairs indicating the sizes required to dump panic logs.
 *  The below valid 'keys' can be passed as part of the struct property_data.
 *  - MDSS registers:   key = HFI_PROPERTY_DEBUG_REG_ALLOC
 *  - Event Log:        key = HFI_PROPERTY_DEBUG_EVT_LOG_ALLOC
 *  - Debug Bus:        key = HFI_PROPERTY_DEBUG_DBG_BUS_ALLOC
 *  - State variable:   key = HFI_PROPERTY_DEBUG_STATE_ALLOC
 *
 * Data Layout:
 *  struct debug_device_alloc - panic log dump sizes required for allocation
 *  @buffer_alloc_info: Array of <key,value> pairs indicating the hfi debug alloc property and its
 *                      size.
 *  struct debug_device_alloc {
 *      struct property_data buffer_alloc_info;
 *  }
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_U32_ARRAY
 *           .cmd                      : HFI_COMMAND_DEBUG_INIT
 *           .flags                    : HFI_RX_FLAGS_SUCCESS
 *           .payload (u32 values)     : struct debug_device_alloc
 */
#define HFI_COMMAND_DEBUG_INIT                                       0xFF000009

/*
 * HFI_COMMAND_DEBUG_SETUP is sent from host to DCP to indicate buffer addresses allocated
 * to the device for dumping panic logs.
 *
 * Data Contents:
 *  Host can use the below valid 'keys' as part of struct property_data to indicate
 *  the allocated address for a given item:
 *  - MDSS registers:   key = HFI_PROPERTY_DEBUG_REG_ADDR
 *  - Event Log:        key = HFI_PROPERTY_DEBUG_EVT_LOG_ADDR
 *  - Debug Bus:        key = HFI_PROPERTY_DEBUG_DBG_BUS_ADDR
 *  - State variable:   key = HFI_PROPERTY_DEBUG_STATE_ADDR
 *
 * Data Layout:
 *  struct debug_device_setup - debug setup info for device_id
 *  @buffer_addr_info: Array of <key,value> pairs indicating the hfi debug alloc property and its
 *                     designated address.
 *  struct debug_device_setup {
 *      struct property_data buffer_addr_info;
 *  }
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_U32_ARRAY
 *           .cmd                      : HFI_COMMAND_DEBUG_SETUP
 *           .flags                    : HFI_TX_FLAGS_RESPONSE_REQUIRED
 *           .payload (u32 values)     : struct debug_device_setup
 */
#define HFI_COMMAND_DEBUG_SETUP                                       0xFF00000A

/*
 * HFI_COMMAND_DEBUG_PANIC_SUBSCRIBE is sent from Host to DCP to subscribe/unsubscribe to
 * display-fatal failure events. Events are set on a per display basis.
 *
 * Data Contents:
 *  The below valid bitmasks can be set as part of the events_mask indicating the panic event.
 *  - HFI_DEBUG_EVENT_UNDERRUN
 *  - HFI_DEBUG_EVENT_HW_RESET
 *  - HFI_DEBUG_EVENT_PP_TIMEOUT
 *
 * Data Layout:
 *  struct debug_display_evt_info - display panic event info
 *  @display_id: Display ID
 *  @events_mask: Bitwise OR of HFI_DEBUG_EVENT_XX
 *  @enable: HFI_TRUE / HFI_FALSE
 *  struct debug_display_evt_info {
 *      u32 display_id
 *      u32 events_mask;
 *      u32 enable;
 *  }
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_U32_ARRAY
 *           .cmd                      : HFI_COMMAND_DEBUG_PANIC_SUBSCRIBE
 *           .flags                    : HFI_TX_FLAGS_RESPONSE_REQUIRED
 *           .payload (u32 values)     : struct debug_display_evt_info
 *
 */
#define HFI_COMMAND_DEBUG_PANIC_SUBSCRIBE                            0xFF00000B

/*
 * HFI_COMMAND_DEBUG_PANIC_EVENT is sent from DCP to host to indicate a panic event for a
 * given display. The payload contains the display id, device id and bitwise or of events set.
 *
 * Data Contents:
 *  The below valid bitmasks can be set as part of the events_mask indicating the panic event.
 *  - HFI_DEBUG_EVENT_UNDERRUN
 *  - HFI_DEBUG_EVENT_HW_RESET
 *  - HFI_DEBUG_EVENT_PP_TIMEOUT
 *
 * Data Layout:
 *  struct panic_info - panic information
 *  @display_id: Display ID
 *  @events_mask: Bitwise OR of HFI_DEBUG_EVENT_XX
 *  struct panic_info {
 *      u32 display_id;
 *      u32 events_mask;
 *  }
 *
 * DCP to Host:
 * hfi_header.num_packets                 : 1
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_U32_ARRAY
 *           .cmd                      : HFI_COMMAND_DEBUG_PANIC_EVENT
 *           .flags                    : HFI_TX_FLAGS_RESPONSE_REQUIRED
 *           .payload (u32 values)     : struct panic_info
 */
#define HFI_COMMAND_DEBUG_PANIC_EVENT                                0xFF00000C
/*
 * HFI_COMMAND_DEBUG_DUMP_REGS is sent from DCP to read MDSS registers at a given offset
 * for a given length.
 *
 * Data Layout:
 *  struct regdump_info - register dump information at a requested offset & length
 *  @device_id: Device ID
 *  @reg_offset: Offset from MDSS base
 *  @length: Required length to be dumped
 *  @buffer_info: Allocated memory for dumping
 *  struct regdump_info {
 *      u32 device_id;
 *      u32 reg_offset;
 *      u32 length;
 *      struct hfi_buff buffer_info;
 *  }
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * hfi_packet.payload_info.type        : HFI_PAYLOAD_U32_ARRAY
 *           .cmd                      : HFI_COMMAND_DEBUG_DUMP_REGS
 *           .flags                    : HFI_TX_FLAGS_RESPONSE_REQUIRED
 *           .payload (u32 values)     : struct regdump_info
 */
#define HFI_COMMAND_DEBUG_DUMP_REGS                                  0xFF00000D

/*!
 * HFI_COMMAND_DEBUG_TRACE_CFG - This command is used to enable/disable the trace.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * Hfi packet layout                      | Value
 *----------------------------------------|------------------------------------------
 * hfi_packet.payload_info (type)         | HFI_PAYLOAD_U32
 * hfi_packet.cmd                         | HFI_COMMAND_DEBUG_TRACE_CFG
 * hfi_packet.flags                       | HFI_TX_FLAGS_INTR_REQUIRED |
 * ^                                      | HFI_TX_FLAGS_RESPONSE_REQUIRED |
 * ^                                      | HFI_TX_FLAGS_NON_DISCARDABLE
 * hfi_packet.payload                     | u32 flag to enable/disable trace logs.
 *
 * DCP to Host:
 * hfi_header.num_packets                 : 1
 *
 * Hfi packet layout                      | Value
 *----------------------------------------|------------------------------------------
 * hfi_packet.payload_info (type)         | HFI_PAYLOAD_NONE
 * hfi_packet.cmd                         | HFI_COMMAND_DEBUG_TRACE_CFG
 * hfi_packet.flags                       | HFI_RX_FLAGS_SUCCESS
 *
 */
#define HFI_COMMAND_DEBUG_TRACE_CFG                                  0xFF00000E

/*!
 * HFI_COMMAND_DEBUG_IDLE_TIMEOUT is sent from Host to DCP to modify the duration of Idle timeout
 * for all displays associated with device.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * Hfi packet layout                   | Value
 *-------------------------------------|-------------------------------------
 * hfi_packet.payload_info (type)      | HFI_PAYLOAD_U32_ARRAY
 * hfi_packet.cmd                      | HFI_COMMAND_DEBUG_IDLE_TIMEOUT
 * hfi_packet.flags                    | HFI_TX_FLAGS_RESPONSE_REQUIRED
 * hfi_packet.payload[0]               | uint32 device_id
 * hfi_packet.payload[1]               | uint32 idle_timeout_ms
 */
#define HFI_COMMAND_DEBUG_IDLE_TIMEOUT                               0xFF00000F

/*!
 * HFI_COMMAND_DEBUG_SET_DISPLAY_PROPERTY is sent from Host to DCP to modify a display debug
 * property.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * Hfi packet layout                      | Value
 *----------------------------------------|---------------------------------
 * hfi_packet.payload_info.type           | HFI_PAYLOAD_U32_ARRAY
 * hfi_packet.cmd                         | HFI_COMMAND_DEBUG_SET_DISPLAY_PROPERTY
 * hfi_packet.flags                       | HFI_TX_FLAGS_NONE
 * hfi_packet.payload[0]                  | struct hfi_display_dbg_property
 */
#define HFI_COMMAND_DEBUG_SET_DISPLAY_PROPERTY                       0xFF000010

/*!
 * HFI_COMMAND_DEBUG_SET_LOG_LEVEL is sent from Host to DCP to set the
 * debug log level.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * Hfi packet layout                   | Value
 *-------------------------------------|-------------------------------------
 * hfi_packet.payload_info (type)      | HFI_PAYLOAD_U32_ARRAY
 * hfi_packet.cmd                      | HFI_COMMAND_DEBUG_SET_LOG_LEVEL
 * hfi_packet.flags                    | HFI_TX_FLAGS_RESPONSE_REQUIRED
 * hfi_packet.payload[0..n]            | struct hfi_debug_log_level_info
 */
#define HFI_COMMAND_DEBUG_SET_LOG_LEVEL                               0xFF000011

/*
 * DP Simulation HFI commands
 */
#define HFI_COMMAND_DEBUG_DP_BEGIN                                     0xFF000500

/*
 * hfi_header sample for any DP simulation command:
 * hfi_header.cmd_buff_info.size          : (sizeof(hfi_header) +
 *                                            sizeof(hfi_packet))
 *           .cmd_buff_info.type          : HFI_CMD_BUFF_DEBUG
 *           .device_id                   : 0
 *           .object_id                   : display id
 *           .timestamp_hi                : ts_hi
 *           .timestamp_lo                : ts_lo
 *           .header_id                   : unique id
 *           .num_packets                 : 'n' packets depending upon the
 *                                          packet cmd
 *
 * hfi_packet sample for any DP simulation packet:
 *
 *     Hfi Packet layout        : Value
 *     hfi_packet.payload_info (size): sizeof(hfi_packet) (including payload size)
 *     hfi_packet.payload_info (type): one of enum
 *     ^                        : 'hfi_packet_payload_type'
 *     hfi_packet.cmd           : HFI_COMMAND_DEBUG_DP_XXX
 *     hfi_packet.flags (Host to DCP): HFI_TX_FLAGS_XXX
 *     ^                        : (enum hfi_packet_tx_flags)
 *     hfi_packet (DCP to Host) : HFI_RX_FLAGS_XXX
 *     ^                        : (enum hfi_packet_rx_flags)
 *     hfi_packet.id            : DP instance id
 *     hfi_packet.packet_id     : unique id
 */

/*
 * HFI_COMMAND_DEBUG_DP_SIMULATION_CONTROL - This command enables or disables DP simulation mode.
 *                             From host to DCP, this command controls the overall
 *                             simulation state for a DP instance.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 *     Hfi Packet layout        : Value
 *     hfi_packet.payload_info (size): sizeof(hfi_packet) (including payload size)
 *     hfi_packet.payload_info (type): HFI_PAYLOAD_U32
 *     hfi_packet.cmd           : HFI_COMMAND_DEBUG_DP_SIMULATION_CONTROL
 *     hfi_packet.flags (Host to DCP): HFI_TX_FLAGS_RESPONSE_REQUIRED
 *                                          HFI_TX_FLAGS_NON_DISCARDABLE
 *     hfi_packet.id            : DP instance id
 *     hfi_packet.packet_id     : unique id
 *     hfi_packet.payload       : HFI_TRUE to enable, HFI_FALSE to disable
 */
#define HFI_COMMAND_DEBUG_DP_SIMULATION_CONTROL                                    0xFF000501

/*
 * HFI_COMMAND_DEBUG_DP_SET_EDID - This command writes EDID data to the simulation.
 *                                 From host to DCP, this command provides EDID data
 *                                 that will be used for subsequent hotplug events.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 *     Hfi Packet layout        : Value
 *     hfi_packet.payload_info (size): sizeof(hfi_packet) (including payload size)
 *     hfi_packet.payload_info (type): HFI_PAYLOAD_BLOB
 *     hfi_packet.cmd           : HFI_COMMAND_DEBUG_DP_SET_EDID
 *     hfi_packet.flags (Host to DCP): HFI_TX_FLAGS_RESPONSE_REQUIRED
 *                                          HFI_TX_FLAGS_NON_DISCARDABLE
 *     hfi_packet.id            : DP instance id
 *     hfi_packet.packet_id     : unique id
 *     hfi_packet.payload       : struct hfi_buff containing EDID data
 */
#define HFI_COMMAND_DEBUG_DP_SET_EDID                                0xFF000502

/*
 * HFI_COMMAND_DEBUG_DP_SET_DPCD - This command writes DPCD register data.
 *                                 From host to DCP, this command sets DPCD register
 *                                 values that will be returned during DPCD reads.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 *     Hfi Packet layout        : Value
 *     hfi_packet.payload_info (size): sizeof(hfi_packet) (including payload size)
 *     hfi_packet.payload_info (type): HFI_PAYLOAD_BLOB
 *     hfi_packet.cmd           : HFI_COMMAND_DEBUG_DP_SET_DPCD
 *     hfi_packet.flags (Host to DCP): HFI_TX_FLAGS_RESPONSE_REQUIRED
 *                                          HFI_TX_FLAGS_NON_DISCARDABLE
 *     hfi_packet.id            : DP instance id
 *     hfi_packet.packet_id     : unique id
 *     hfi_packet.payload       : struct hfi_dp_dpcd_data
 */
#define HFI_COMMAND_DEBUG_DP_SET_DPCD                                0xFF000503

/*
 * HFI_COMMAND_DEBUG_DP_READ_DPCD - This command reads DPCD register data.
 *                                From host to DCP, this command requests DPCD
 *                                register values from the simulation using shared buffer.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * Hfi Packet layout             : Value
 * hfi_packet.payload_info (size): sizeof(hfi_packet) (including payload size)
 * hfi_packet.payload_info (type): HFI_PAYLOAD_BLOB
 * hfi_packet.cmd                : HFI_COMMAND_DEBUG_DP_READ_DPCD
 * hfi_packet.flags (Host to DCP): HFI_TX_FLAGS_INTR_REQUIRED
 *                                          HFI_TX_FLAGS_RESPONSE_REQUIRED
 *                                          HFI_TX_FLAGS_NON_DISCARDABLE
 * hfi_packet.id                 : DP instance id
 * hfi_packet.packet_id          : unique id
 * hfi_packet.payload            : struct hfi_dp_dpcd_request
 *
 * DCP to Host:
 *
 * Below table describes the memory layout of the payload written to the shared buffer
 *
 * Payload              (Size, Value): Description
 * [0..N-1]             (u32, dpcd_data): DPCD register data of size N bytes read starting
 */
#define HFI_COMMAND_DEBUG_DP_READ_DPCD                                 0xFF000504

/*
 * HFI_COMMAND_DEBUG_DP_SET_BW_CODE - This command sets the bandwidth code.
 *                                  From host to DCP, this command configures the
 *                                  link bandwidth for simulation.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * Data Contents:
 *  - bw_code: Bandwidth code (RBR=0x06, HBR=0x0A, HBR2=0x14, HBR3=0x1E)
 *
 * Hfi Packet layout             : Value
 * hfi_packet.payload_info (size): sizeof(hfi_packet) (including payload size)
 * hfi_packet.payload_info (type): HFI_PAYLOAD_U32
 * hfi_packet.cmd                : HFI_COMMAND_DEBUG_DP_SET_BW_CODE
 * hfi_packet.flags (Host to DCP): HFI_TX_FLAGS_RESPONSE_REQUIRED
 *                                          HFI_TX_FLAGS_NON_DISCARDABLE
 * hfi_packet.id                 : DP instance id
 * hfi_packet.packet_id          : unique id
 * hfi_packet.payload            : bw_code (u32)
 */
#define HFI_COMMAND_DEBUG_DP_SET_BW_CODE                               0xFF000505

/*
 * HFI_COMMAND_DEBUG_DP_SET_TPG - This command sets the test pattern generator.
 *                              From host to DCP, this command configures the
 *                              test pattern for simulation.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * Data Contents:
 *  - pattern: Test pattern type
 *
 * Hfi Packet layout             : Value
 * hfi_packet.payload_info (size): sizeof(hfi_packet) (including payload size)
 * hfi_packet.payload_info (type): HFI_PAYLOAD_U32
 * hfi_packet.cmd                : HFI_COMMAND_DEBUG_DP_SET_TPG
 * hfi_packet.flags (Host to DCP): HFI_TX_FLAGS_RESPONSE_REQUIRED
 *                                          HFI_TX_FLAGS_NON_DISCARDABLE
 * hfi_packet.id                 : DP instance id
 * hfi_packet.packet_id          : unique id
 * hfi_packet.payload            : pattern (u32)
 */
#define HFI_COMMAND_DEBUG_DP_SET_TPG                                   0xFF000506

/*
 * HFI_COMMAND_DEBUG_DP_HDCP_CONTROL - This command sets the HDCP state.
 *                               From host to DCP, this command enables or
 *                               disables HDCP for simulation.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * Hfi Packet layout             : Value
 * hfi_packet.payload_info (size): sizeof(hfi_packet) (including payload size)
 * hfi_packet.payload_info (type): HFI_PAYLOAD_U32
 * hfi_packet.cmd                : HFI_COMMAND_DEBUG_DP_HDCP_CONTROL
 * hfi_packet.flags (Host to DCP): HFI_TX_FLAGS_RESPONSE_REQUIRED
 *                                          HFI_TX_FLAGS_NON_DISCARDABLE
 * hfi_packet.id                 : DP instance id
 * hfi_packet.packet_id          : unique id
 * hfi_packet.payload            : HFI_TRUE or HFI_FALSE
 */
#define HFI_COMMAND_DEBUG_DP_HDCP_CONTROL                              0xFF000507

/*
 * HFI_COMMAND_DEBUG_DP_SET_ATTENTION - This command sends an attention event.
 *                                From host to DCP, this command simulates
 *                                an attention interrupt with VDO data.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * Data Contents:
 *  - vdo: Vendor Defined Object data
 *
 * Hfi Packet layout             : Value
 * hfi_packet.payload_info (size): sizeof(hfi_packet) (including payload size)
 * hfi_packet.payload_info (type): HFI_PAYLOAD_U32
 * hfi_packet.cmd                : HFI_COMMAND_DEBUG_DP_SET_ATTENTION
 * hfi_packet.flags (Host to DCP): HFI_TX_FLAGS_RESPONSE_REQUIRED
 *                                          HFI_TX_FLAGS_NON_DISCARDABLE
 * hfi_packet.id                 : DP instance id
 * hfi_packet.packet_id          : unique id
 * hfi_packet.payload            : vdo (u32)
 */
#define HFI_COMMAND_DEBUG_DP_SET_ATTENTION                            0xFF000508

/*
 * HFI_COMMAND_DEBUG_DP_READ_CRC - This command reads CRC values.
 *                               From host to DCP, this command requests
 *                               source and sink CRC values for comparison.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * Hfi Packet layout             : Value
 * hfi_packet.payload_info (size): sizeof(hfi_packet) (including payload size)
 * hfi_packet.payload_info (type): HFI_PAYLOAD_NONE
 * hfi_packet.cmd                : HFI_COMMAND_DEBUG_DP_READ_CRC
 * hfi_packet.flags (Host to DCP): HFI_TX_FLAGS_INTR_REQUIRED
 *                                          HFI_TX_FLAGS_RESPONSE_REQUIRED
 *                                          HFI_TX_FLAGS_NON_DISCARDABLE
 * hfi_packet.id                 : DP instance id
 * hfi_packet.packet_id          : unique id
 * hfi_packet.payload            : None
 *
 * DCP to Host:
 * hfi_header.num_packets                 : 1
 *
 * Data layout:
 *  struct hfi_dp_crc_info {
 *      u32 status;
 *      u16 src_crc[3];  // RGB CRC values from source
 *      u16 sink_crc[3]; // RGB CRC values from sink
 *  }
 *
 * Hfi Packet layout             : Value
 * hfi_packet.payload_info (size): sizeof(hfi_packet) (including payload size)
 * hfi_packet.payload_info (type): HFI_PAYLOAD_U32_ARRAY
 * hfi_packet.cmd                : HFI_COMMAND_DEBUG_DP_READ_CRC
 * hfi_packet.flags (DCP to Host): HFI_RX_FLAGS_SUCCESS
 * hfi_packet.id                 : DP instance id
 * hfi_packet.packet_id          : unique id
 * hfi_packet.payload            : struct hfi_dp_crc_info
 */
#define HFI_COMMAND_DEBUG_DP_READ_CRC                                  0xFF000509

/*
 * HFI_COMMAND_DEBUG_DP_READ_INFO - This command reads display information.
 *                                From host to DCP, this command requests
 *                                current display configuration information using shared buffer.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * Hfi Packet layout             : Value
 * hfi_packet.payload_info (size): sizeof(hfi_packet) (including payload size)
 * hfi_packet.payload_info (type): HFI_PAYLOAD_BLOB
 * hfi_packet.cmd                : HFI_COMMAND_DEBUG_DP_READ_INFO
 * hfi_packet.flags (Host to DCP): HFI_TX_FLAGS_INTR_REQUIRED
 *                                          HFI_TX_FLAGS_RESPONSE_REQUIRED
 *                                          HFI_TX_FLAGS_NON_DISCARDABLE
 * hfi_packet.id                 : DP instance id
 * hfi_packet.packet_id          : unique id
 * hfi_packet.payload            : struct hfi_buff
 *
 * DCP to Host:
 *
 * Below table describes the memory layout of the payload written to the shared buffer.
 * Payload              (Size, Value): Description
 * [0]                  (u32, status): Status code
 * [1]                  (u32, dp_state): Display state
 * [2]                  (u32, link_rate): Link rate
 * [3]                  (u32, lane_count): Number of lanes
 * [4]                  (u32, h_active): Horizontal active pixels
 * [5]                  (u32, v_active): Vertical active pixels
 * [6]                  (u32, refresh_rate): Refresh rate (Hz)
 * [7]                  (u32, pixel_clk_khz): Pixel clock in kHz
 * [8]                  (u32, bpp): Bits per pixel
 * [9]                  (u32, test_request): Test request flag
 * [10]                 (u32, bw_code): Bandwidth code
 * [11]                 (u32, voltage_swing): Voltage swing
 * [12]                 (u32, pre_emphasis): Pre-emphasis
 */
#define HFI_COMMAND_DEBUG_DP_READ_INFO                                 0xFF00050A

/*
 * HFI_COMMAND_DEBUG_DP_READ_BW_CODE - This command reads the current bandwidth code.
 *                                   From host to DCP, this command requests
 *                                   the current link bandwidth configuration.
 *
 * Host to DCP:
 * hfi_header.num_packets                 : 1
 *
 * Hfi Packet layout             : Value
 * hfi_packet.payload_info (size): sizeof(hfi_packet) (including payload size)
 * hfi_packet.payload_info (type): HFI_PAYLOAD_NONE
 * hfi_packet.cmd                : HFI_COMMAND_DEBUG_DP_READ_BW_CODE
 * hfi_packet.flags (Host to DCP): HFI_TX_FLAGS_INTR_REQUIRED
 *                                          HFI_TX_FLAGS_RESPONSE_REQUIRED
 *                                          HFI_TX_FLAGS_NON_DISCARDABLE
 * hfi_packet.id                 : DP instance id
 * hfi_packet.packet_id          : unique id
 * hfi_packet.payload            : None
 *
 * DCP to Host:
 * hfi_header.num_packets                 : 1
 *
 * Hfi Packet layout             : Value
 * hfi_packet.payload_info (size): sizeof(hfi_packet) (including payload size)
 * hfi_packet.payload_info (type): HFI_PAYLOAD_U32_ARRAY
 * hfi_packet.cmd                : HFI_COMMAND_DEBUG_DP_READ_BW_CODE
 * hfi_packet.flags (DCP to Host): HFI_RX_FLAGS_SUCCESS
 * hfi_packet.id                 : DP instance id
 * hfi_packet.packet_id          : unique id
 * hfi_packet.payload            : bw code (u32)
 */

#define HFI_COMMAND_DEBUG_DP_READ_BW_CODE                              0xFF00050B

#define HFI_COMMAND_DEBUG_DP_END                                       0xFF0005FF

#define HFI_COMMAND_DEBUG_END                                        0xFFFFFFFF

#endif // __H_HFI_COMMANDS_DEBUG_H__
