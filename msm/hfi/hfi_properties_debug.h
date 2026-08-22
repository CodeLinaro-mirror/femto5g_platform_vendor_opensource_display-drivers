/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_PROPERTIES_DEBUG_H__
#define __H_HFI_PROPERTIES_DEBUG_H__

/*
 * All debug property IDs begin here.
 */

 #define HFI_PROPERTY_DEBUG_BEGIN                                    0x00050000

 /*
  * HFI_PROPERTY_DEBUG_REG_ALLOC - DCP informs HLOS the size required for dumping registers.
  * Firmware is expected to send this property as part of HFI_COMMAND_DEBUG_INIT
  * command packet payload.
  *
  * @BasicFuntionality @DeviceInit - HFI_PROPERTY_DEBUG_REG_ALLOC
  *     (u32_key) payload [0]   : HFI_PROPERTY_DEBUG_REG_ALLOC |
  *                               (version=0 << 20) | (dsize=1 << 24)
  *   (u32_value) payload [1]   : u32 size
  */
#define HFI_PROPERTY_DEBUG_REG_ALLOC                                0x00050001

 /*
  * HFI_PROPERTY_DEBUG_DBG_BUS_ALLOC - DCP informs HLOS the size required for dumping debug bus
  * Firmware is expected to send this property as part of HFI_COMMAND_DEBUG_INIT
  * command packet payload.
  *
  * @BasicFuntionality @DeviceInit - HFI_PROPERTY_DEBUG_DBG_BUS_ALLOC
  *     (u32_key) payload [0]   : HFI_PROPERTY_DEBUG_DBG_BUS_ALLOC |
  *                               (version=0 << 20) | (dsize=1 << 24)
  *   (u32_value) payload [1]   : u32 size
  */
#define HFI_PROPERTY_DEBUG_DBG_BUS_ALLOC                            0x00050002

 /*
  * HFI_PROPERTY_DEBUG_EVT_LOG_ALLOC - DCP informs HLOS the size required for dumping event log
  * Firmware is expected to send this property as part of HFI_COMMAND_DEBUG_INIT
  * command packet payload.
  *
  * @BasicFuntionality @DeviceInit - HFI_PROPERTY_DEBUG_EVT_LOG_ALLOC
  *     (u32_key) payload [0]   : HFI_PROPERTY_DEBUG_EVT_LOG_ALLOC |
  *                               (version=0 << 20) | (dsize=1 << 24)
  *   (u32_value) payload [1]   : u32 size
  */
#define HFI_PROPERTY_DEBUG_EVT_LOG_ALLOC                           0x00050003

 /*
  * HFI_PROPERTY_DEBUG_STATE_ALLOC - DCP informs HLOS the size required for dumping state variable
  * Firmware is expected to send this property as part of HFI_COMMAND_DEBUG_INIT
  * command packet payload.
  *
  * @BasicFuntionality @DeviceInit - HFI_PROPERTY_DEBUG_STATE_ALLOC
  *     (u32_key) payload [0]   : HFI_PROPERTY_DEBUG_STATE_ALLOC |
  *                               (version=0 << 20) | (dsize=1 << 24)
  *   (u32_value) payload [1]   : u32 size
  */
#define HFI_PROPERTY_DEBUG_STATE_ALLOC                             0x00050004

 /*
  * HFI_PROPERTY_DEBUG_REG_ADDR - HLOS informs DCP the buffer address for dumping registers
  * HLOS is expected to send this property as part of HFI_COMMAND_DEBUG_SETUP
  * command packet payload.
  *
  * @BasicFuntionality @DeviceInit - HFI_PROPERTY_DEBUG_REG_ADDR
  *     (u32_key) payload [0]   : HFI_PROPERTY_DEBUG_REG_ADDR |
  *                               (version=0 << 20) | (dsize=5 << 24)
  *   (u32_value) payload [1-5]   : struct hfi_buff
  */
#define HFI_PROPERTY_DEBUG_REG_ADDR                                0x00050005

 /*
  * HFI_PROPERTY_DEBUG_DBG_BUS_ADDR - HLOS informs DCP the buffer address for dumping debug bus
  * HLOS is expected to send this property as part of HFI_COMMAND_DEBUG_SETUP
  * command packet payload.
  *
  * @BasicFuntionality @DeviceInit - HFI_PROPERTY_DEBUG_DBG_BUS_ADDR
  *     (u32_key) payload [0]   : HFI_PROPERTY_DEBUG_DBG_BUS_ADDR |
  *                               (version=0 << 20) | (dsize=5 << 24)
  *   (u32_value) payload [1-5]   : struct hfi_buff
  */
#define HFI_PROPERTY_DEBUG_DBG_BUS_ADDR                            0x00050006

 /*
  * HFI_PROPERTY_DEBUG_EVT_LOG_ADDR - HLOS informs DCP the buffer address for dumping event log
  * HLOS is expected to send this property as part of HFI_COMMAND_DEBUG_SETUP
  * command packet payload.
  *
  * @BasicFuntionality @DeviceInit - HFI_PROPERTY_DEBUG_EVT_LOG_ADDR
  *     (u32_key) payload [0]   : HFI_PROPERTY_DEBUG_EVT_LOG_ADDR |
  *                               (version=0 << 20) | (dsize=5 << 24)
  *   (u32_value) payload [1-5]   : struct hfi_buff
  */
#define HFI_PROPERTY_DEBUG_EVT_LOG_ADDR                            0x00050007

 /*
  * HFI_PROPERTY_DEBUG_STATE_ADDR - HLOS informs DCP the buffer address for dumping state variable
  * HLOS is expected to send this property as part of HFI_COMMAND_DEBUG_SETUP
  * command packet payload.
  *
  * @BasicFuntionality @DeviceInit - HFI_PROPERTY_DEBUG_STATE_ADDR
  *     (u32_key) payload [0]   : HFI_PROPERTY_DEBUG_STATE_ADDR |
  *                               (version=0 << 20) | (dsize=5 << 24)
  *   (u32_value) payload [1-5]   : struct hfi_buff
  */
#define HFI_PROPERTY_DEBUG_STATE_ADDR                              0x00050008

/*
 * HFI_PROPERTY_DEBUG_ENABLE - Enable or disable FW debug feature capture
 *
 * Generic debug feature enable/disable property that controls capture of trace events
 * or debug log messages. Host informs DCP whether to capture structured debug data to
 * the configured buffer. When enabled, FW writes debug data to the buffer specified by
 * HFI_PROPERTY_DEBUG_BUFFER_ADDR. This buffer address must be set before enable, unless
 * pre-configured in the device tree. When disabled, buffer configuration is ignored and
 * no debug data is captured.
 *
 * Host is expected to send this property as part of
 * HFI_COMMAND_DEBUG_SET_COMMON_PROPERTY command packet payload.
 *
 * @BasicFunctionality @DebugControl
 *
 *   (u32_key) payload [N]     : HFI_PROPERTY_DEBUG_ENABLE |
 *                               (version=0 << 20) | (dsize=2 << 24)
 *   (u32_val) payload [N+1]   : struct hfi_debug_enable_payload.feature
 *                               (enum hfi_debug_feature)
 *   (u32_val) payload [N+2]   : struct hfi_debug_enable_payload.enable
 *                               HFI_TRUE  (1) = enable
 *                               HFI_FALSE (0) = disable
 *   (u32_key) payload [N+3..] : Additional properties (if any)
 *
 * @note
 *   - When enable is HFI_FALSE, buffer configuration from
 *     HFI_PROPERTY_DEBUG_BUFFER_ADDR is ignored.
 *   - Feature type values:
 *     - HFI_DEBUG_FEATURE_TRACE (0x1): Enables trace event capture
 *     - HFI_DEBUG_FEATURE_LOG (0x2): Enables debug log string capture
 *   - Log buffers operate as circular rings; oldest entries are
 *     overwritten when buffer is full.
 */
#define HFI_PROPERTY_DEBUG_ENABLE                                   0x00050009

/*
 * HFI_PROPERTY_DEBUG_BUFFER_ADDR - Configure buffer for debug data capture
 *
 * Generic debug buffer property that configures buffer address, size, and feature type
 * for trace or log data. Host is expected to send this property as part of
 * HFI_COMMAND_DEBUG_SET_COMMON_PROPERTY command packet payload.
 *
 * The feature type is specified explicitly in payload[N+1], matching the
 * uniform layout of HFI_PROPERTY_DEBUG_ENABLE.
 * @BasicFunctionality @DebugControl
 *
 *   (u32_key) payload [N]     : HFI_PROPERTY_DEBUG_BUFFER_ADDR |
 *                               (version=0 << 20) | (dsize=6 << 24)
 *   (u32_val) payload [N+1]   : struct hfi_debug_buffer_addr_payload.feature
 *                               HFI_DEBUG_FEATURE_TRACE (0x1) = trace event buffer
 *                               HFI_DEBUG_FEATURE_LOG   (0x2) = debug log string buffer
 *   (u32_val) payload [N+2]   : struct hfi_debug_buffer_addr_payload.buff.addr_l
 *                               (buffer address LSB)
 *   (u32_val) payload [N+3]   : struct hfi_debug_buffer_addr_payload.buff.addr_h
 *                               (buffer address MSB)
 *   (u32_val) payload [N+4]   : struct hfi_debug_buffer_addr_payload.buff.size
 *                               (buffer size in bytes)
 *   (u32_val) payload [N+5]   : struct hfi_debug_buffer_addr_payload.buff.version
 *                               (reserved, set to 0)
 *   (u32_val) payload [N+6]   : struct hfi_debug_buffer_addr_payload.buff.flags
 *                               (reserved, set to 0)
 *   (u32_key) payload [N+7..] : Additional properties (if any)
 * @note
 *   - Buffer address must be page-aligned
 *   - Only used when corresponding enable property
 *     (HFI_PROPERTY_DEBUG_ENABLE with matching feature_type)
 *     is set to HFI_TRUE.
 */
#define HFI_PROPERTY_DEBUG_BUFFER_ADDR                             0x0005000A

#define HFI_PROPERTY_DEBUG_END                                     0x0005FFFF

#endif // __H_HFI_PROPERTIES_DEBUG_H__
