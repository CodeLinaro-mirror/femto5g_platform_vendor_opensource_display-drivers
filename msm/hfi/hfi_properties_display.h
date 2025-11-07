/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_PROPERTIES_DISPLAY_H__
#define __H_HFI_PROPERTIES_DISPLAY_H__

/*
 * This is documentation file. Not used for header inclusion.
 */

/*
 * All display property IDs begin here.
 * For display properties: 16 - 19 bits of the property id = 0x2
 */
#define HFI_PROPERTY_DISPLAY_BEGIN                                   0x00020000

/*
 * HFI_PROPERTY_DISPLAY_HAS_DIM_LAYER - Gets id DIM layer is supported.
 *                                      Firmware is expected to send this
 *                                      property as part of
 *                                      HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                                      command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_HAS_DIM_LAYER
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_HAS_DIM_LAYER |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_DISPLAY_HAS_DIM_LAYER                           0x00020001

/*
 * HFI_PROPERTY_DISPLAY_HAS_IDLE_PC - Gets if Idle PC is supported.
 *                                    Firmware is expected to send this
 *                                    property as part of
 *                                    HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                                    command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_HAS_IDLE_PC
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_HAS_IDLE_PC |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_DISPLAY_HAS_IDLE_PC                             0x00020002

/*
 * HFI_PROPERTY_DISPLAY_LM_NOISE - Gets if LM Noise is supported.
 *                                 Firmware is expected to send this
 *                                 property as part of
 *                                 HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                                 command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_LM_NOISE
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_LM_NOISE |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_DISPLAY_LM_NOISE                                0x00020003

/*
 * HFI_PROPERTY_DISPLAY_NOISE_VERSION - Gets Noise layer version.
 *                                    Firmware is expected to send this
 *                                    property as part of
 *                                    HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                                    command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_NOISE_VERSION
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_NOISE_VERSION |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : noise layer version
 */
#define HFI_PROPERTY_DISPLAY_NOISE_VERSION                           0x00020004

/*
 * HFI_PROPERTY_DISPLAY_FEATURE_DEDICATED_CWB - Gets if dedicated CWB feature
 *                                    is supported. Firmware is expected
 *                                    to send this property as part of
 *                                    HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                                    command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_FEATURE_DEDICATED_CWB
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_FEATURE_DEDICATED_CWB |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_DISPLAY_FEATURE_DEDICATED_CWB                   0x00020005

/*
 * HFI_PROPERTY_DISPLAY_FEATURE_CWB - Gets if CWB feature is supported.
 *                                    Firmware is expected to send this
 *                                    property as part of
 *                                    HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                                    command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_FEATURE_CWB
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_FEATURE_CWB |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_DISPLAY_FEATURE_CWB                             0x00020006

/*
 * HFI_PROPERTY_DISPLAY_SYS_CACHE_DISP - Gets if system cache for static
 *                                      display read / write path is supported.
 *                                      Firmware is expected to send this
 *                                      property as part of
 *                                      HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                                      command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_SYS_CACHE_DISP
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_SYS_CACHE_DISP |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_DISPLAY_SYS_CACHE_DISP                          0x00020007

/*
 * HFI_PROPERTY_DISPLAY_FEATURE_TRUSTED_DISPLAY_MODE - Gets if Trusted VM is
 *                                    supported. Firmware is expected to send
 *                                    this property as part of
 *                                    HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                                    command packet payload.
 *
 * @BasicFuntionality @Display -
 *              HFI_PROPERTY_DISPLAY_FEATURE_TRUSTED_DISPLAY_MODE
 *     (u32_key) payload [0]   :
 *                          HFI_PROPERTY_DISPLAY_FEATURE_TRUSTED_DISPLAY_MODE |
 *                          (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_DISPLAY_FEATURE_TRUSTED_DISPLAY_MODE            0x00020008

/*
 * HFI_PROPERTY_DISPLAY_UBWC_STATS - Gets if UBWC Statistics is supported.
 *                                   Firmware is expected to send this
 *                                   property as part of
 *                                   HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                                   command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_UBWC_STATS
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_UBWC_STATS |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_DISPLAY_UBWC_STATS                              0x00020009

/*
 * HFI_PROPERTY_DISPLAY_DYN_CLK_SUPPORT - Gets if dynamic clock enablement
 *                                        is supported. Firmware is expected
 *                                        to send this property as part of
 *                                        HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                                        command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_DYN_CLK_SUPPORT
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_DYN_CLK_SUPPORT |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_DISPLAY_DYN_CLK_SUPPORT                         0x0002000A

/*
 * HFI_PROPERTY_DISPLAY_QSYNC - Gets if qsync mode is supported.
 *                              Firmware is expected to send this
 *                              property as part of
 *                              HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                              command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_QSYNC
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_QSYNC |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_DISPLAY_QSYNC                                   0x0002000B

/*
 * HFI_PROPERTY_DISPLAY_AVR_STEP - Gets if AVR Step is supported.
 *                                 Firmware is expected to send this
 *                                 property as part of
 *                                 HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                                 command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_AVR_STEP
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_AVR_STEP |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_DISPLAY_AVR_STEP                                0x0002000C

/*
 * HFI_PROPERTY_DISPLAY_WB_CWB_DITHER - Gets if CWB Dither is supported.
 *                                      Firmware is expected to send this
 *                                      property as part of
 *                                      HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                                      command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_WB_CWB_DITHER
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_WB_CWB_DITHER |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_DISPLAY_WB_CWB_DITHER                           0x0002000D

/*
 * HFI_PROPERTY_DISPLAY_WB_SYS_CACHE_DISP - Gets if system cache for IWE use
 *                                         cases is supported. Firmware is
 *                                         expected to send this property as
 *                                         part of
 *                                         HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                                         command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_WB_SYS_CACHE_DISP
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_WB_SYS_CACHE_DISP |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_DISPLAY_WB_SYS_CACHE_DISP                       0x0002000E

/*
 * HFI_PROPERTY_DISPLAY_WB_LINEAR_ROTATION - Gets if writeback block supports
 *                                         line mode image rotation. Firmware
 *                                         is expected to send this property as
 *                                         part of
 *                                         HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS
 *                                         command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_WB_LINEAR_ROTATION
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_WB_LINEAR_ROTATION |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_DISPLAY_WB_LINEAR_ROTATION                      0x0002000F

/*
 * HFI_PROPERTY_DISPLAY_POWER_MODE - Gets the extended power modes supported by the Display.
 *                                   Host is expected to send this property as part
 *                                   of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                   command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_POWER_MODE
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_POWER_MODE |
 *                               (dsize=1 << 16 ) | (version=0 << 24)
 *   (u32_value) payload [1]   : one of the enum values of hfi_display_power_mode
 */
#define HFI_PROPERTY_DISPLAY_POWER_MODE                              0x00020010

/*
 * HFI_PROPERTY_DISPLAY_DRAM_IB - Gets the DPU DRAM IB vote for DRAM(LLCC(SDE)->DDR) in Kbps.
 *                                Host is expected to send this packet
 *                                of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_DRAM_IB
 *     (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_DRAM_IB |
 *                                 (version=0 << 20) | (dsize=3 << 24 )
 *   (u32_value) payload [1]     : 0
 *   (u32_value) payload [2-3]   : struct hfi_prop_u64
 */
#define HFI_PROPERTY_DISPLAY_DRAM_IB                                 0x00020011

/*
 * HFI_PROPERTY_DISPLAY_DRAM_AB - Gets the DPU DRAM AB vote for DRAM(LLCC(SDE)->DDR) in Kbps.
 *                                Host is expected to send this packet
 *                                of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_DRAM_AB
 *     (u32_key) payload [0]     :  HFI_PROPERTY_DISPLAY_DRAM_AB |
 *                                  (version=0 << 20) | (dsize=3 << 24 )
 *   (u32_value) payload [1]     : 0
 *   (u32_value) payload [2-3]   : struct hfi_prop_u64
 */
#define HFI_PROPERTY_DISPLAY_DRAM_AB                                 0x00020012

/*
 * HFI_PROPERTY_DISPLAY_LLCC_IB - Gets the DPU LLCC IB vote for LLCC(SDE_master_on_MNOC->LLCC)
 *                                in Kbps. Host is expected to send this packet
 *                                of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_LLCC_IB
 *     (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_LLCC_IB |
 *                                 (version=0 << 20) | (dsize=3 << 24 )
 *   (u32_value) payload [1]     : 0
 *   (u32_value) payload [2-3]   : struct hfi_prop_u64
 */
#define HFI_PROPERTY_DISPLAY_LLCC_IB                                 0x00020013

/*
 * HFI_PROPERTY_DISPLAY_LLCC_AB - Gets the DPU LLCC IB vote for LLCC(SDE_master_on_MNOC->LLCC)
 *                                in Kbps. Host is expected to send this packet
 *                                of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_LLCC_AB
 *     (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_LLCC_AB |
 *                                 (version=0 << 20) | (dsize=3 << 24 )
 *   (u32_value) payload [1]     : 0
 *   (u32_value) payload [2-3]   : struct hfi_prop_u64
 */
#define HFI_PROPERTY_DISPLAY_LLCC_AB                                 0x00020014

/*
 * HFI_PROPERTY_DISPLAY_CORE_IB - Gets the DPU IB vote for MNOC(virt_master-> virt_slave(NOC))
 *                                in Kbps. Host is expected to send this packet
 *                                of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_CORE_IB
 *     (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_CORE_IB |
 *                                 (version=0 << 20) | (dsize=3 << 24 )
 *   (u32_value) payload [1]     : 0
 *   (u32_value) payload [2-3]   : struct hfi_prop_u64
 */
#define HFI_PROPERTY_DISPLAY_CORE_IB                                 0x00020015

/*
 * HFI_PROPERTY_DISPLAY_CORE_AB - Gets the DPU AB vote for MNOC(virt_master-> virt_slave(NOC))
 *                                in Kbps. Host is expected to send this packet
 *                                of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_CORE_AB
 *     (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_CORE_AB |
 *                                 (version=0 << 20) | (dsize=3 << 24 )
 *   (u32_value) payload [1]     : 0
 *   (u32_value) payload [2-3]   : struct hfi_prop_u64
 */
#define HFI_PROPERTY_DISPLAY_CORE_AB                                 0x00020016

/*
 * HFI_PROPERTY_DISPLAY_CORE_CLK - Gets the Mdp core clock in hertz.
 *                                 Host is expected to send this packet
 *                                 of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                 command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_CORE_CLK
 *     (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_CORE_CLK |
 *                                 (version=0 << 20) | (dsize=3 << 24 )
 *   (u32_value) payload [1]     : 0
 *   (u32_value) payload [2-3]   : struct hfi_prop_u64
 */
#define HFI_PROPERTY_DISPLAY_CORE_CLK                                0x00020017

/*
 * HFI_PROPERTY_DISPLAY_ATTACH_LAYER - This property is to attach layer before setting layer
 *                                     properties. The payload has layer id attached to the
 *                                     corresponding layer properties set. Host is expected
 *                                     to send this packet of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                     command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_ATTACH_LAYER
 *     (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_ATTACH_LAYER |
 *                                 (version=0 << 20) | (dsize=1 << 24 )
 *   (u32_value) payload [1]     : layer id
 */
#define HFI_PROPERTY_DISPLAY_ATTACH_LAYER                            0x00020018

/*
 * HFI_PROPERTY_DISPLAY_DETACH_LAYER - This property is to detach the layer from layer properties.
 *                                     The payload has the layer id to detach.
 *                                     Host is expected to send this packet
 *                                     of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                     command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_DETACH_LAYER
 *     (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_DETACH_LAYER |
 *                                 (version=0 << 20) | (dsize=1 << 24 )
 *   (u32_value) payload [1]     : layer id
 */
#define HFI_PROPERTY_DISPLAY_DETACH_LAYER                            0x00020019

/*
 * HFI_PROPERTY_DISPLAY_SCAN_SEQUENCE_ID - This property is to add a 'sequence number' identifier
 *                                         to a commit that DCP can return along with the
 *                                         HFI_COMMAND_DISPLAY_EVENT_FRAME_SCAN_START event.
 *                                         Host is expected to send this packet as part of
 *                                         HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_SCAN_SEQUENCE_ID
 * (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_SCAN_SEQUENCE_ID |
 *                             (version=0 << 20) | (dsize=1 << 24 )
 * (u32_value) payload [1]     : sequence_id
 */
#define HFI_PROPERTY_DISPLAY_SCAN_SEQUENCE_ID                        0x0002001A

/*
 * HFI_PROPERTY_DISPLAY_AUTOREFRESH_CFG  - This property is to enable/disable auto-refresh for the
 *                                         interface block. Host is expected to send this packet
 *                                         as part of HFI_COMMAND_DISPLAY_SET_PROPERTY command
 *                                         packet.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_AUTOREFRESH_CFG
 * (u32_key) payload [0]         : HFI_PROPERTY_DISPLAY_AUTOREFRESH_CFG |
 *                                (version=0 << 20) | (dsize=2 << 24 )
 * (u32_value) payload [1-2]     : struct hfi_display_autorefresh_cfg
 */
#define HFI_PROPERTY_DISPLAY_AUTOREFRESH_CFG                         0x0002001B

/**
 * HFI_PROPERTY_DISPLAY_LSR_WB_HFI_CONFIG - This property is set to configure the HFI queue
 *                                          parameters of the LSR WB CSC/Reprojection displays.
 *                                          Host is expected to send this packet as part of
 *                                          HFI_COMMAND_DISPLAY_SET_PROPERTY command packet
 *                                          payload. HFI_PROPERTY_DISPLAY_LSR_WB_NEEDS_SCRATCH_MEM
 *                                          must be set before this property is set.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_LSR_WB_HFI_CONFIG
 *     (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_LSR_WB_HFI_CONFIG
 *                                 (version=0 << 20) | (dsize=5 << 24)
 *     (u32_value) payload [1-6] : struct hfi_buff
 */
#define HFI_PROPERTY_DISPLAY_LSR_WB_HFI_CONFIG                       0x0002001C

/*
 * HFI_PROPERTY_DISPLAY_LSR_WB_NEEDS_SCRATCH_MEM - This property is to get the size of scratch
 *                                                 buffer needed by LSR WB CSC/Reprojection
 *                                                 displays. Host is expected to send this
 *                                                 property as part of response to the
 *                                                 HFI_COMMAND_DISPLAY_GET_PROPERTY command packed
 *                                                 payload. The scratch memory should be allocated
 *                                                 and mapped using the firmware addresses
 *                                                 with the HFI_PROPERTY_DISPLAY_SET_SCRATCH_MEM.
 *                                                 The scratch memory should be freed after the
 *                                                 display is closed.
 *
 * @BasicFuntionality @DeviceInit - HFI_PROPERTY_DISPLAY_LSR_WB_NEEDS_SCRATCH_MEM
 *     (u32_key) payload [0]    : HFI_PROPERTY_DISPLAY_LSR_WB_NEEDS_SCRATCH_MEM |
 *                                (version=0 << 20) | (dsize=1 << 24 )
 *   (u32_value) payload [1]    : scratch_buff_size_in_bytes
 */
#define HFI_PROPERTY_DISPLAY_LSR_WB_NEEDS_SCRATCH_MEM            0x0002001D

/*
 * HFI_PROPERTY_DISPLAY_SET_SCRATCH_MEM - This property is set to configure the scratch memory
 *                                        that can be used by LSR WB CSC/Reprojection displays.
 *                                        Host is expected to send this packet as part of
 *                                        HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *                                        Host needs to query the scratch memory size requirement
 *                                        as part of HFI_COMMAND_DISPLAY_GET_PROPERTY, before
 *                                        setting the scratch memory.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_SET_SCRATCH_MEM
 *     (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_SET_SCRATCH_MEM
 *                                 (version=0 << 20) | (dsize=5 << 24)
 *     (u32_value) payload [1-6] : struct hfi_buff
 */
#define HFI_PROPERTY_DISPLAY_SET_SCRATCH_MEM                         0x0002001E

/*
 * HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_SYNC_TO - This property is set to configure the sync display
 *                                              for LSR WB Reprojection display.
 *                                              Host is expected to send this packet as part of
 *                                              HFI_COMMAND_DISPLAY_SET_PROPERTY command packet
 *                                              payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_SYNC_TO
 *     (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_SYNC_TO
 *                                 (version=0 << 20) | (dsize=1 << 24)
 *     (u32_value) payload [1]   : display id to sync with
 */
#define HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_SYNC_TO                   0x0002001F

/*
 * HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_MATRIX - This property is set to configure the LSR
 *                                                    WB Reprojection display's reprojection
 *                                                    matrices. Host is expected to send
 *                                                    this packet as part of
 *                                                    HFI_COMMAND_DISPLAY_SET_PROPERTY command
 *                                                    packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_MATRIX
 *     (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_MATRIX |
 *                                   (version=0 << 20) | (dsize=33 << 24)
 *     (u32_value) payload [1]     : View Number (0 = left (or mono), 1 = right, ...)
 *     (u32_value) payload [2-17]  : struct hfi_lsr_reproj_matrix
 *     (u32_value) payload [18-33] : struct hfi_lsr_reproj_matrix - Inverse
 */
#define HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_MATRIX                0x00020020

/*
 * HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT - This property is set to configure
 *                                                 the reprojection display configuration
 *                                                 for LSR WB Reprojection display.
 *                                                 Host is expected to send this packet
 *                                                 as part of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                                 command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT
 *     (u32_key) payload [0]      : HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT |
 *                                  (version=0 << 20) | (dsize=(2 + count) << 24)
 *     (u32_value) payload [1]    : lsr_reproj_display_config_ext.key
 *     (u32_value) payload [2]    : lsr_reproj_display_config_ext.count
 *     (u32_value) payload [3-..] : lsr_reproj_display_config_ext.values
 */
#define HFI_PROPERTY_DISPLAY_LSR_WB_REPROJ_CONFIG_EXT                0x00020021

/*
 * HFI_PROPERTY_DISPLAY_INPUT_FENCE - This property is set to provide the input/acquire fence
 *                                    representing every input frame layer of the
 *                                    LSR WB CSC/Reprojection displays.
 *                                    Host is expected to send this packet of
 *                                    HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_INPUT_FENCE
 *     (u32_key) payload [0]   : HFI_PROPERTY_DISPLAY_INPUT_FENCE |
 *                               (version=0 << 20) | (dsize=2 << 24)
 *     (u32_value) payload [1] : Acquire Fence Low
 *     (u32_value) payload [2] : Acquire Fence High
 */
#define HFI_PROPERTY_DISPLAY_INPUT_FENCE                             0x00020022

/*
 * HFI_PROPERTY_DISPLAY_REUSABLE_FENCE - This property is set to provide a reusable fence
 *                                       of LSR WB CSC/Reprojection displays.
 *                                       Host is expected to send this packet as part of
 *                                       HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_REUSABLE_FENCE
 *     (u32_key) payload [0]      : HFI_PROPERTY_DISPLAY_REUSABLE_FENCE |
 *                                  (version=0 << 20) | (dsize = 1 + (2 x count) << 24)
 *     (u32_value) payload [1]    : Reusable Fence Count
 *     (u32_value) payload [2]    : Reusable Fence Low
 *     (u32_value) payload [3-..] : Reusable Fence High
 */
#define HFI_PROPERTY_DISPLAY_REUSABLE_FENCE                          0x00020023

/*
 * HFI_PROPERTY_DISPLAY_SYS_CACHE_INFO - This property is to configure the sys cache info of LSR
 *                                       WB displays. Syscache info includes number of entries,
 *                                       cache ID's and the size of cache associated with it.
 *                                       Host is expected to send this packet as part of
 *                                       HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality @Display - HFI_PROPERTY_DISPLAY_SYS_CACHE_INFO
 *     (u32_key) payload [0]    : HFI_PROPERTY_DISPLAY_SYS_CACHE_INFO |
 *                               (version=0 << 20) | (dsize=1 + (2 x count) << 24)
 *   (u32_value) payload [1]    : No. of entries in the system cache
 *   (u32_value) payload [2-..] : struct hfi_display_sys_cache_info
 */
#define HFI_PROPERTY_DISPLAY_SYS_CACHE_INFO                       0x00020024

/*
 * HFI_PROPERTY_DISPLAY_LSR_WB_CVP_BUFF - This property is set to configure the HFI queue
 *                                        parameters of the LSR WB CSC/Reprojection displays.
 *                                        Host is expected to send this packet as part of
 *                                        HFI_COMMAND_DISPLAY_SET_PROPERTY command packet
 *                                        payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_LSR_WB_CVP_BUFF
 *     (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_LSR_WB_CVP_BUFF
 *                                 (version=0 << 20) | (dsize=5 << 24)
 *     (u32_value) payload [1-6] : struct hfi_buff
 */
#define HFI_PROPERTY_DISPLAY_LSR_WB_CVP_BUFF                       0x00020025

/*
 * HFI_PROPERTY_DISPLAY_LSR_WB_OUT_BUFFERS - This property is set to configure the output buffers
 *                                           of LSR WB CSC/Reprojection displays. Host is expected
 *                                           to send this packet as part of
 *                                           HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                           command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_LSR_WB_OUT_BUFFERS
 *     (u32_key) payload [0]      : HFI_PROPERTY_DISPLAY_LSR_WB_OUT_BUFFERS |
 *                                  (version=0 << 20) |
 *                                         (dsize=1 + (count x struct hfi_wb_out_buff) << 24)
 *     (u32_value) payload [1]    : count of hfi_wb_out_buff buffers (Number of color fields)
 *     (u32_value) payload [2-..] : struct hfi_wb_out_buff
 */
#define HFI_PROPERTY_DISPLAY_LSR_WB_OUT_BUFFERS                       0x00020026

/*
 * HFI_PROPERTY_DISPLAY_LSR_MAPPED_HFI_BASE_ADDR - This property is set to configure the LSR
 *                                                 mapped HFI queue base address. Host is
 *                                                 expected to send this packet of as part of
 *                                                 HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                                 command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_LSR_MAPPED_HFI_BASE_ADDR
 *     (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_LSR_MAPPED_HFI_BASE_ADDR
 *                                 (version=0 << 20) | (dsize=5 << 24)
 *     (u32_value) payload [1-6] : struct hfi_buff
 */
#define HFI_PROPERTY_DISPLAY_LSR_MAPPED_HFI_BASE_ADDR                  0x00020027

/*
 * HFI_PROPERTY_DISPLAY_HRP_HFI_CONFIG - This property is set to configure the shared memory
 *                                       between DCP and HRP. Host is expected to send this
 *                                       packet as part of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                       command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_HRP_HFI_CONFIG
 *     (u32_key) payload [0]     : HFI_PROPERTY_DISPLAY_HRP_HFI_CONFIG
 *                                 (version=0 << 20) | (dsize=5 << 24)
 *     (u32_value) payload [1-6] : struct hfi_buff
 */
#define HFI_PROPERTY_DISPLAY_HRP_HFI_CONFIG                            0x00020028

/*
 * HFI_PROPERTY_DISPLAY_ATTACH_OUTPUT_LAYER - This property is to attach output layer before
 *                                            setting output layer properties.
 *                                            The corresponding wb_id will be passed as the
 *                                            payload.
 *                                            Host is expected to send this packet as part of
 *                                            HFI_COMMAND_DISPLAY_SET_PROPERTY command packet
 *                                            payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_ATTACH_OUTPUT_LAYER
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_DISPLAY_ATTACH_OUTPUT_LAYER \|
 * ^                             | (version=0 << 20) \| (dsize=1 << 24 ) \|
 *   (u32_value) payload [1]     | wb_id
 */
#define HFI_PROPERTY_DISPLAY_ATTACH_OUTPUT_LAYER                     0x00020029

/*
 * HFI_PROPERTY_DISPLAY_DETACH_OUTPUT_LAYER - This property is to detach the output layer, which
 *                                            must be received at the end of a CWB session.
 *                                            The corresponding wb_id will be passed as the
 *                                            payload.
 *                                            Output layer dirty flag will be set to perform the
 *                                            required cleanup in host.
 *                                            Host is expected to send this packet as part of
 *                                            HFI_COMMAND_DISPLAY_SET_PROPERTY command packet
 *                                            payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_DETACH_OUTPUT_LAYER
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_DISPLAY_DETACH_OUTPUT_LAYER \|
 * ^                             | (version=0 << 20) \| (dsize=1 << 24 ) \|
 *   (u32_value) payload [1]     | wb_id
 */
#define HFI_PROPERTY_DISPLAY_DETACH_OUTPUT_LAYER                     0x0002002A

/*!
 * HFI_PROPERTY_DISPLAY_DEST_ROI - This property is used to set destination ROI for display.
 *                                 Host is expected to send this packet as part of
 *                                 HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                 command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_DEST_ROI
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_DISPLAY_DEST_ROI |
 *                               | (version=0 << 20) |
 *                               | (dsize=2 + (count x struct hfi_display_roi) << 24)
 *     (u32_value) payload [1]   | obj id
 *     (u32_value) payload [2]   | roi_type (default value is PANEL_ROI)
 *     (u32_value) payload [3]   | num of rois
 *     (u32_value) payload [4-..]| array of struct hfi_display_roi
 */
#define HFI_PROPERTY_DISPLAY_DEST_ROI                                0x0002002B

/*
 * All display color properties begin here
 */
#define HFI_PROPERTY_DISPLAY_COLOR_BEGIN                             0x00020100

/*
 * HFI_PROPERTY_DISPLAY_COLOR_3D_LUT - This property is to setup 3D LUT.
 *                                     Host is expected to send this packet of
 *                                     HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_3D_LUT
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_3D_LUT |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_3D_LUT                            0x00020101

/*
 * HFI_PROPERTY_DISPLAY_COLOR_IGC - This property is to setup IGC.
 *                                  Host is expected to send this packet
 *                                  of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                  command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_IGC
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_IGC |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_IGC                               0x00020102

/*
 * HFI_PROPERTY_DISPLAY_COLOR_GC - This property is to setup GC.
 *                                 Host is expected to send this packet of
 *                                 HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_GC
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_GC |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_GC                                0x00020103

/*
 * HFI_PROPERTY_DISPLAY_COLOR_PCC - This property is to setup PCC.
 *                                  Host is expected to send this packet of
 *                                  HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_PCC
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_PCC |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_PCC                               0x00020104

/*
 * HFI_PROPERTY_DISPLAY_COLOR_HSIC - This property is to setup PA HSIC.
 *                                   Host is expected to send this packet of
 *                                   HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_HSIC
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_HSIC |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_HSIC                              0x00020105

/*
 * HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_SKIN - This property is to setup PA MEMCOLOR SKIN.
 *                                            Host is expected to send this packet of
 *                                            HFI_COMMAND_DISPLAY_SET_PROPERTY command packet
 *                                            payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_SKIN
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_SKIN |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_SKIN                     0x00020106

/*
 * HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_SKY - This property is to setup PA MEMCOLOR SKY.
 *                                           Host is expected to send this packet of
 *                                           HFI_COMMAND_DISPLAY_SET_PROPERTY command packet
 *                                           payload.
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_SKY
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_SKY |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_SKY                      0x00020107

/*
 * HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_FOLIAGE - This property is to setup PA MEMCOLOR FOLIAGE.
 *                                               Host is expected to send this packet of
 *                                               HFI_COMMAND_DISPLAY_SET_PROPERTY command packet
 *                                               payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_FOLIAGE
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_FOLIAGE |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_FOLIAGE                  0x00020108

/*
 * HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_PROT - This property is to setup PA MEMCOLOR PROTECTION.
 *                                            Host is expected to send this packet of
 *                                            HFI_COMMAND_DISPLAY_SET_PROPERTY command packet
 *                                            payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_PROT
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_PROT |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_MEMCOLOR_PROT                     0x00020109

/*
 * HFI_PROPERTY_DISPLAY_COLOR_SIXZONE - This property is to setup PA SIXZONE.
 *                                      Host is expected to send this packet of
 *                                      HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_SIXZONE
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_SIXZONE |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_SIXZONE                           0x0002010A

/*
 * HFI_PROPERTY_DISPLAY_COLOR_VLUT - This property is to setup PA VLUT.
 *                                   Host is expected to send this packet of
 *                                   HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_VLUT
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_VLUT |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_VLUT                              0x0002010B

/*
 * HFI_PROPERTY_DISPLAY_COLOR_DITHER - This property is to setup PPB DITHER.
 *                                     Host is expected to send this packet of
 *                                     HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_DITHER
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_DITHER |
 *                                 (version=0 << 20) |
 *                                 (dsize=(sizeof(struct hfi_display_dither)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_display_dither
 */
#define HFI_PROPERTY_DISPLAY_COLOR_DITHER                            0x0002010C

/*
 * HFI_PROPERTY_DISPLAY_COLOR_RC - This property is to setup RC.
 *                                 Host is expected to send this packet of
 *                                 HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_RC
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_RC |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_RC                                0x0002010D

/*
 * HFI_PROPERTY_DISPLAY_COLOR_SPR_INIT   -   This property is to setup SPR_INIT config.
 *                                           Host is expected to send this packet
 *                                           of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                           command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_SPR_INIT
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_SPR_INIT |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_SPR_INIT                          0x0002010E

/*
 * HFI_PROPERTY_DISPLAY_COLOR_SPR_UDC   -    This property is to setup SPR_UDC config.
 *                                           Host is expected to send this packet
 *                                           of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                           command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_SPR_UDC
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_SPR_UDC |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_SPR_UDC                           0x0002010F

/*
 * HFI_PROPERTY_DISPLAY_COLOR_SPR_DITHER - This property is to setup DSPP SPR DITHER.
 *                                     Host is expected to send this packet of
 *                                     HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_SPR_DITHER
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_SPR_DITHER |
 *                                 (version=0 << 20) |
 *                                 (dsize=(sizeof(struct hfi_buff)/4) << 24)
 *   (u32_value) payload [1]     : struct hfi_buff of array of hfi_display_dither
 */
#define HFI_PROPERTY_DISPLAY_COLOR_SPR_DITHER                        0X00020110

/*
 * HFI_PROPERTY_DISPLAY_COLOR_PA_DITHER - This property is to setup DSPP PA DITHER.
 *                                     Host is expected to send this packet of
 *                                     HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_PA_DITHER
 *   (u32_key) payload [0]       : HFI_PROPERTY_DISPLAY_COLOR_PA_DITHER |
 *                                 (version=0 << 20) | (dsize=
 *                                 (sizeof(struct hfi_display_pa_dither)/4 * num_of_dspps) << 24)
 *   (u32_value) payload [1]     : array of struct hfi_display_pa_dither
 */
#define HFI_PROPERTY_DISPLAY_COLOR_PA_DITHER                         0x00020111

/*
 * HFI_PROPERTY_DISPLAY_COLOR_DEMURA_CFG - This property is to setup Demura.
 *                                  Host is expected to send this packet of
 *                                  HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_DEMURA_CFG
 *   (u32_key) payload       : HFI_PROPERTY_DISPLAY_COLOR_DEMURA_CFG |
 *                             (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload     : struct hfi_buff_dpu
 *
 * | Major        | Minor        | Payload               |
 * |--------------|--------------|-----------------------|
 * | 4            | 0            | hfi_buff_dpu          |
 */
#define HFI_PROPERTY_DISPLAY_COLOR_DEMURA_CFG                        0x00020112

/*
 * HFI_PROPERTY_DISPLAY_COLOR_DEMURA_TABLE - This property is to setup Demura tables.
 *                                  Host is expected to send this packet of
 *                                  HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_DEMURA_TABLE
 *   (u32_key) payload       : HFI_PROPERTY_DISPLAY_COLOR_DEMURA_TABLE |
 *                             (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload     : struct hfi_buff_dpu
 *
 * | Major        | Minor        | Payload               |
 * |--------------|--------------|-----------------------|
 * | 4            | 0            | hfi_buff_dpu          |
 */
#define HFI_PROPERTY_DISPLAY_COLOR_DEMURA_TABLE                      0x00020113

/*
 * HFI_PROPERTY_DISPLAY_COLOR_DEMURA_BACKLIGHT - This property is to setup Demura backlight.
 *                                  Host is expected to send this packet of
 *                                  HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_DEMURA_BACKLIGHT
 *   (u32_key) payload       : HFI_PROPERTY_DISPLAY_COLOR_DEMURA_BACKLIGHT |
 *                             (version=0 << 20) | (dsize=(sizeof(u32)/4 * num_of_dspps) << 24)
 *   (u32_value) payload     : array of u32
 *
 * | Major        | Minor        | Payload               |
 * |--------------|--------------|-----------------------|
 * | 4            | 0            | u32                   |
 */
#define HFI_PROPERTY_DISPLAY_COLOR_DEMURA_BACKLIGHT                  0x00020114

/*
 * HFI_PROPERTY_DISPLAY_DEST_SCALER - This property is to setup destination scaler.
 *                                  Host is expected to send this packet of
 *                                  HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_DEST_SCALER
 *   (u32_key) payload       : HFI_PROPERTY_DISPLAY_DEST_SCALER |
 *                             (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload     : struct hfi_buff_dpu
 *
 * | Major        | Minor        | Payload               |
 * |--------------|--------------|-----------------------|
 * | 4            | 0            | hfi_buff_dpu          |
 */
#define HFI_PROPERTY_DISPLAY_DEST_SCALER                             0x00020115

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_QRTC_CONFIG - This property is to setup QRTC config.
 *                                     Host is expected to send this packet of
 *                                     HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_QRTC_CONFIG
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_DISPLAY_COLOR_QRTC_CONFIG \|
 * ^                             | (version=0 << 20) \|
 * ^                             | (dsize=(sizeof(struct hfi_qrtc_config)/4 * num_of_dspps) << 24)
 *   (u32_value) payload [1]     | array of struct hfi_qrtc_config
 *
 * | Major        | Minor        | Payload                         |
 * |--------------|--------------|---------------------------------|
 * | 1            | 0            | array of struct hfi_qrtc_config |
 */
#define HFI_PROPERTY_DISPLAY_COLOR_QRTC_CONFIG                       0x00020116

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_LTM_INIT - This property is to setup LTM initialization params.
 *                                     Host is expected to send this packet of
 *                                     HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_LTM_INIT
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_DISPLAY_COLOR_LTM_INIT \|
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(struct hfi_display_ltm_init_param)/4)
 * ^                             | * num_dspps) << 24)
 *   (u32_value) payload [1]     | array of struct hfi_display_ltm_init_param
 */
#define HFI_PROPERTY_DISPLAY_COLOR_LTM_INIT                          0x00020117

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_LTM_CFG - This property is to setup LTM configuration params.
 *                                     Host is expected to send this packet of
 *                                     HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_LTM_CFG
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_DISPLAY_COLOR_LTM_CFG \|
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(struct hfi_display_ltm_cfg_param)/4)
 * ^                             | * num_dspps) << 24)
 *   (u32_value) payload [1]     | array of struct hfi_display_ltm_cfg_param
 */
#define HFI_PROPERTY_DISPLAY_COLOR_LTM_CFG                           0x00020118

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_LTM_NOISE_THRESH - This property is to setup LTM noise threshold.
 *                                     Host is expected to send this packet of
 *                                     HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_LTM_NOISE_THRESH
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_DISPLAY_COLOR_LTM_NOISE_THRESH \|
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(u32)/4) * num_dspps) << 24)
 *   (u32_value) payload [1]     | array of u32
 */
#define HFI_PROPERTY_DISPLAY_COLOR_LTM_NOISE_THRESH                  0x00020119

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_LTM_HIST_CTRL - This property is to setup LTM histogram control.
 *                                     Host is expected to send this packet of
 *                                     HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_LTM_HIST_CTRL
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_DISPLAY_COLOR_LTM_HIST_CTRL \|
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(u32)/4 * num_dspps) << 24)
 *   (u32_value) payload [1]     | array of u32
 */
#define HFI_PROPERTY_DISPLAY_COLOR_LTM_HIST_CTRL                     0x0002011A

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_LTM_VLUT - This property is to setup LTM VLUT table.
 *                                     Host is expected to send this packet of
 *                                     HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_LTM_VLUT
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_DISPLAY_COLOR_LTM_VLUT \|
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     | struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_LTM_VLUT                          0x0002011B

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_LTM_QUEUE_BUF - This property is to setup LTM initial params.
 *                                     Host is expected to send this packet of
 *                                     HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_LTM_QUEUE_BUF
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_DISPLAY_COLOR_LTM_QUEUE_BUF \|
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(struct hfi_display_ltm_buffer)/4) << 24)
 *   (u32_value) payload [1]     | struct hfi_display_ltm_buffer
 */
#define HFI_PROPERTY_DISPLAY_COLOR_LTM_QUEUE_BUF                     0x0002011C

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_LTM_CLEAR_BUFS - This property is to setup LTM initial params.
 *                                     Host is expected to send this packet of
 *                                     HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_LTM_CLEAR_BUFS
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_DISPLAY_COLOR_LTM_CLEAR_BUFS \|
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(u32)/4) << 24)
 *   (u32_value) payload [1]     | u32
 */
#define HFI_PROPERTY_DISPLAY_COLOR_LTM_CLEAR_BUFS                    0x0002011D

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_CTRL - Property to configure RGB hist control
 *                                          parameters. Host sends this as part of
 *                                          HFI_COMMAND_DISPLAY_SET_PROPERTY.
 *
 * @BasicFunctionality - HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_CTRL
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_CTRL \|
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(struct hfi_display_rgb_hist_ctrl)/4) << 24)
 *   (u32_value) payload [1]     | struct hfi_display_rgb_hist_ctrl
 */
#define HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_CTRL                   0x0002011E

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_QUEUE_BUFFER - Property to queue RGB histogram
 *                                                  buffer. Host sends this to provide
 *                                                  buffer for histogram stats.
 *
 * @BasicFunctionality - HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_QUEUE_BUFFER
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_QUEUE_BUFFER \|
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(struct hfi_display_rgb_hist_buffer)/4) << 24)
 *   (u32_value) payload [1]     | struct hfi_display_rgb_hist_buffer
 */
#define HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_QUEUE_BUFFER           0x0002011F

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_CLEAR_BUFFERS - Property to clear all queued RGB
 *                                                   hist buffers. Host sends this to reset
 *                                                   histogram buffer state.
 *
 * @BasicFunctionality - HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_CLEAR_BUFFERS
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_CLEAR_BUFFERS \|
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(u32)/4) << 24)
 *   (u32_value) payload [1]     | u32
 */
#define HFI_PROPERTY_DISPLAY_COLOR_RGB_HIST_CLEAR_BUFFERS          0x00020120

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_AIQE_AI_SCALER - This property is to setup AI Scaler.
 *                                             Host is expected to send this packet
 *                                             of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                             command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_AIQE_AI_SCALER
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *   (u32_key) payload [0]       | HFI_PROPERTY_DISPLAY_COLOR_AIQE_AI_SCALER |
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     | struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_AI_SCALER                    0x00020121

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_AIQE_SSRC_CONFIG - This property is to setup SSRC configuration.
 *                                               Host is expected to send this packet
 *                                               of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                               command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_AIQE_SSRC_CONFIG
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *   (u32_key) payload [0]       | HFI_PROPERTY_DISPLAY_COLOR_AIQE_SSRC_CONFIG |
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     | struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_SSRC_CONFIG                  0x00020122

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_AIQE_SSRC_DATA - This property is to setup SSRC data.
 *                                             Host is expected to send this packet
 *                                             of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                             command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_AIQE_SSRC_DATA
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *   (u32_key) payload [0]       | HFI_PROPERTY_DISPLAY_COLOR_AIQE_SSRC_DATA |
 * ^                             | (version=0 << 20) |
 * ^                             |(dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     | struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_SSRC_DATA                    0x00020123

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE - This property is to setup MDNIE.
 *                                         Host is expected to send this packet
 *                                         of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                         command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *   (u32_key) payload [0]       | HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE |
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     | struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE                        0x00020124

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE_IPC - This property is to setup IPC.
 *                                             Host is expected to send this packet
 *                                             of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                             command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE_IPC
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *   (u32_key) payload [0]       | HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE_IPC |
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(u32)/4) << 24)
 *   (u32_value) payload [1]     | u32
 */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE_IPC                    0x00020125

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE_ART - This property is to setup ART.
 *                                             Host is expected to send this packet
 *                                             of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                             command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE_ART
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *   (u32_key) payload [0]       | HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE_ART |
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(u32)/4) << 24)
 *   (u32_value) payload [1]     | u32
 */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_MDNIE_ART                    0x00020126

/*!
 * HFI_PROPERTY_DISPLAY_COLOR_AIQE_ABC - This property is to setup ABC.
 *                                       Host is expected to send this packet
 *                                       of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                       command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_DISPLAY_COLOR_AIQE_ABC
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *   (u32_key) payload [0]       | HFI_PROPERTY_DISPLAY_COLOR_AIQE_ABC |
 * ^                             | (version=0 << 20) |
 * ^                             | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     | struct hfi_buff_dpu
 */
#define HFI_PROPERTY_DISPLAY_COLOR_AIQE_ABC                          0x00020127

/*
 * All display color properties end here
 */
#define HFI_PROPERTY_DISPLAY_COLOR_END                               0x000201FF

/*
 * All display property IDs end here
 */
#define HFI_PROPERTY_DISPLAY_END                                     0x0002FFFF

/*
 * All layer property IDs begin here.
 * For layer properties: 16 - 19 bits of the property id = 0x3
 */
#define HFI_PROPERTY_LAYER_BEGIN                                     0x00030000

/*
 * HFI_PROPERTY_LAYER_FEATURE_DECIMATION - Gets if decimation is supported
 *                                        This property value applies to both
 *                                        VIG and DMA layers. Firmware is
 *                                        expected to send this property as
 *                                        part of
 *                                        HFI_CMD_DEVICE_INIT_COMMON_LAYER_CAPS
 *                                        command packet payload.
 *
 * @BasicFuntionality @VIGandDMA @Layer - HFI_PROPERTY_LAYER_FEATURE_DECIMATION
 *     (u32_key) payload [0]   : HFI_PROPERTY_LAYER_FEATURE_DECIMATION |
 *                               (version=0 << 20) | (dsize=1 << 24)
 *   (u32_value) payload [1]   : HFI_TRUE / HFI_FALSE
 */
#define HFI_PROPERTY_LAYER_FEATURE_DECIMATION                        0x00030001

/*
 * HFI_PROPERTY_LAYER_BLEND_TYPE - Gets blend operations set by Host.
 *                                 Host is expected to send this packet
 *                                 of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                 command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_BLEND_TYPE
 *     (u32_key) payload [0]   : HFI_PROPERTY_LAYER_BLEND_TYPE |
 *                               (version=0 << 20) | (dsize=2 << 24 )
 *     (u32_value) payload [1] : layer id
 *     (u32_value) payload [2] : one of the enum value in hfi_display_blend_ops
 */
#define HFI_PROPERTY_LAYER_BLEND_TYPE                                0x00030002

/*
 * HFI_PROPERTY_LAYER_ALPHA - Gets blending alpha of the plane.
 *                            Host is expected to send this packet
 *                            of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                            command packet payload.
 * @BasicFuntionality - HFI_PROPERTY_LAYER_ALPHA
 *     (u32_key) payload [0]       : HFI_PROPERTY_LAYER_ALPHA |
 *                                   (version=0 << 20) | (dsize=2 << 24 )
 *     (u32_value) payload [1]     : layer id
 *     (u32_value) payload [2]     : alpha
 */
#define HFI_PROPERTY_LAYER_ALPHA                                     0x00030003

/*
 * HFI_PROPERTY_LAYER_ZPOS - Gets z-order of the plane.
 *                           Host is expected to send this packet
 *                           of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                           command packet payload.
 * @BasicFuntionality - HFI_PROPERTY_LAYER_ZPOS
 *     (u32_key) payload [0]       : HFI_PROPERTY_LAYER_ZPOS |
 *                                   (version=0 << 20) | (dsize=2 << 24 )
 *     (u32_value) payload [1]     : layer id
 *     (u32_value) payload [2]     : one of the value from enum hfi_display_blend_stage
 */
#define HFI_PROPERTY_LAYER_ZPOS                                      0x00030004

/*
 * HFI_PROPERTY_LAYER_SRC_ROI - Gets source ROI per layer.
 *                               Host is expected to send this packet
 *                               of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                               command packet payload.
 * @BasicFuntionality - HFI_PROPERTY_LAYER_SRC_ROI
 *
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_SRC_ROI |
 *                                 (version=0 << 20) | (dsize=5 << 24 )
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2-6] : struct hfi_display_roi
 */
#define HFI_PROPERTY_LAYER_SRC_ROI                                   0x00030005

/*
 * HFI_PROPERTY_LAYER_DEST_ROI - Gets destination ROI per layer.
 *                               Host is expected to send this packet
 *                               of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                               command packet payload.
 * @BasicFuntionality - HFI_PROPERTY_LAYER_DEST_ROI
 *
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_DEST_ROI |
 *                                 (version=0 << 20) | (dsize=5 << 24 )
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2-6] : struct hfi_display_roi
 */
#define HFI_PROPERTY_LAYER_DEST_ROI                                  0x00030006

/*
 * HFI_PROPERTY_LAYER_BLEND_ROI - Gets number an array of rectangular regions of interest
 *                                within the CRTC output to be mapped to regions of interest
 *                                on the display output.
 *                                Host is expected to send this packet
 *                                of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                command packet payload.
 *
 * Data layout:
 *   struct hfi_layer_blend_roi - layer blend roi configs
 *   @num_rects: number of valid rectangles in the roi array
 *   @roi: list of roi rectangles
 *   @roi_feature_flags: flags indicates that specific roi rect is valid or not
 *   @spr_roi: list of roi rectangles for spr
 *   struct hfi_layer_blend_roi {
 *     u32 num_rects;
 *     struct hfi_clip_rect roi[num_rects];
 *     u32 roi_feature_flags;
 *     struct hfi_clip_rect spr_roi[num_rects];
 *   };
 *   struct hfi_clip_rect - clip rectangle
 *   @x1    :  x1 position of the rectangle
 *   @y1    :  y1 position of the rectangle
 *   @x2    :  x2 position of the rectangle
 *   @y2    :  y2 position of the rectangle
 *   struct hfi_clip_rect {
 *      unsigned short x1;
 *      unsigned short y1;
 *      unsigned short x2;
 *      unsigned short y2;
 *   };
 * @BasicFuntionality - HFI_PROPERTY_LAYER_BLEND_ROI
 *
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_BLEND_ROI |
 *                                 (version=0 << 20) | (dsize=6 << 16 )
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2-7] : struct hfi_buff for struct hfi_layer_blend_roi
 */
#define HFI_PROPERTY_LAYER_BLEND_ROI                                 0x00030007

/*
 * HFI_PROPERTY_LAYER_SRC_IMG_SIZE_H - Gets Total Image Height.
 *                                     This refers to the surface image
 *                                     and used for pixel extensions
 *                                     calculation purposes when scaling.
 *                                     Host is expected to send this packet
 *                                     of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                     command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_SRC_IMG_SIZE_H
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_SRC_IMG_SIZE_H |
 *                                 (version=0 << 20) | (dsize=2 << 16 )
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2]   : source img height
 */
#define HFI_PROPERTY_LAYER_SRC_IMG_SIZE_H                            0x00030008

/*
 * HFI_PROPERTY_LAYER_SRC_IMG_SIZE_W - Gets Total Image Width.
 *                                     This refers to the surface image
 *                                     and used for pixel extensions
 *                                     calculation purposes when scaling.
 *                                     Host is expected to send this packet
 *                                     of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                     command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_SRC_IMG_SIZE_W
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_SRC_IMG_SIZE_W |
 *                                 (version=0 << 20) | (dsize=2 << 16 )
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2]   : source img width
 */
#define HFI_PROPERTY_LAYER_SRC_IMG_SIZE_W                            0x00030009

/*
 * HFI_PROPERTY_LAYER_SRC_ADDR - Gets SMMU mapped source Image start address.
 *                               Host is expected to send this packet
 *                               of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                               command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_SRC_ADDR
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_SRC_ADDR |
 *                                 (version=0 << 20) | (dsize=2 << 16 )
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2]   : Plane start address for Plane 0
 *     (u32_value) payload [3]   : Plane start address for Plane 1
 *     (u32_value) payload [4]   : Plane start address for Plane 2
 *     (u32_value) payload [5]   : Plane start address for Plane 3
 */
#define HFI_PROPERTY_LAYER_SRC_ADDR                                  0x0003000A

/*
 * HFI_PROPERTY_LAYER_SRC_FORMAT - Gets Source Format Configuration for each layer.
 *                                 Host is expected to send this packet
 *                                 of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                 command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_SRC_FORMAT
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_SRC_FORMAT |
 *                                 (version=0 << 20) | (dsize=2 << 16 )
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2]   : one of the formats from enum hfi_color_formats
 */
#define HFI_PROPERTY_LAYER_SRC_FORMAT                                0x0003000B

/*
 * HFI_PROPERTY_LAYER_STRIDE - Gets Stride for each layer.
 *                             Host is expected to send this packet
 *                             of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                             command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_STRIDE
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_STRIDE |
 *                                 (version=0 << 20) | (dsize=5 << 24 )
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2]   : Stride for Plane 0
 *     (u32_value) payload [3]   : Stride for Plane 1
 *     (u32_value) payload [4]   : Stride for Plane 2
 *     (u32_value) payload [5]   : Stride for Plane 3
 */
#define HFI_PROPERTY_LAYER_STRIDE                                    0x0003000C

/*
 * HFI_PROPERTY_LAYER_MULTIRECT_MODE- Multi rectangle fetch modes.
 *                                    Host is expected to send this packet
 *                                    of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                    command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_MULTIRECT_MODE
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_MULTIRECT_MODE |
 *                                 (version=0 << 20) | (dsize=1 << 16 )
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2]   : one of the modes from enum hfi_layer_fetch_mode
 */
#define HFI_PROPERTY_LAYER_MULTIRECT_MODE                            0x0003000D

/*
 * HFI_PROPERTY_LAYER_BG_ALPHA - Gets blending background alpha of the plane.
 *                               Host is expected to send this packet
 *                               of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                               command packet payload.
 * @BasicFuntionality - HFI_PROPERTY_LAYER_BG_ALPHA
 *     (u32_key) payload [0]       : HFI_PROPERTY_LAYER_BG_ALPHA |
 *                                   (version=0 << 20) | (dsize=2 << 24 )
 *     (u32_value) payload [1]     : layer id
 *     (u32_value) payload [2]     : background alpha
 */
#define HFI_PROPERTY_LAYER_BG_ALPHA                                  0x0003000E

/*
 * HFI_PROPERTY_OUTPUT_LAYER_SRC_ROI - Gets source Region of Interest (ROI) of
 *                                     the output layer, defining the area within
 *                                     the source buffer that is to be processed.
 *                                     Host is expected to send this packet
 *                                     of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                     command packet payload.
 * @BasicFuntionality - HFI_PROPERTY_OUTPUT_LAYER_SRC_ROI
 *
 *     (u32_key) payload [0]     : HFI_PROPERTY_OUTPUT_LAYER_SRC_ROI |
 *                                 (version=0 << 20) | (dsize=5 << 24)
 *     (u32_value) payload [1]   : output layer id
 *     (u32_value) payload [2-6] : struct hfi_display_roi
 */
#define HFI_PROPERTY_OUTPUT_LAYER_SRC_ROI                            0x0003000F

/*
 * HFI_PROPERTY_OUTPUT_LAYER_DST_ROI - Gets destination Region of Interest(ROI) of
 *                                     output layer where the processed content will
 *                                     appear in the destination buffer.
 *                                     Host is expected to send this packet
 *                                     of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                     command packet payload.
 * @BasicFuntionality - HFI_PROPERTY_OUTPUT_LAYER_DST_ROI
 *
 *     (u32_key) payload [0]     : HFI_PROPERTY_OUTPUT_LAYER_DST_ROI |
 *                                 (version=0 << 20) | (dsize=5 << 24)
 *     (u32_value) payload [1]   : output layer id
 *     (u32_value) payload [2-6] : struct hfi_display_roi
 */
#define HFI_PROPERTY_OUTPUT_LAYER_DST_ROI                            0x00030010

/*
 * HFI_PROPERTY_OUTPUT_LAYER_SRC_IMG_SIZE_H - Gets total image height of output buffer.
 *                                            Host is expected to send this packet
 *                                            of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                            command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_OUTPUT_LAYER_SRC_IMG_SIZE_H
 *     (u32_key) payload [0]     : HFI_PROPERTY_OUTPUT_LAYER_SRC_IMG_SIZE_H |
 *                                 (version=0 << 20) | (dsize=2 << 24)
 *     (u32_value) payload [1]   : output layer id
 *     (u32_value) payload [2]   : source img height
 */
#define HFI_PROPERTY_OUTPUT_LAYER_SRC_IMG_SIZE_H                      0x00030011

/*
 * HFI_PROPERTY_OUTPUT_LAYER_SRC_IMG_SIZE_W - Gets total image width of output buffer.
 *                                     Host is expected to send this packet
 *                                     of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                     command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_OUTPUT_LAYER_SRC_IMG_SIZE_W
 *     (u32_key) payload [0]     : HFI_PROPERTY_OUTPUT_LAYER_SRC_IMG_SIZE_W |
 *                                 (version=0 << 20) | (dsize=2 << 24)
 *     (u32_value) payload [1]   : output layer id
 *     (u32_value) payload [2]   : source img width
 */
#define HFI_PROPERTY_OUTPUT_LAYER_SRC_IMG_SIZE_W                      0x00030012

/*
 * HFI_PROPERTY_OUTPUT_LAYER_DST_ADDR - Gets SMMU mapped start address
 *                                      of output buffer.
 *                                      Host is expected to send this packet
 *                                      of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                      command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_OUTPUT_LAYER_DST_ADDR
 *     (u32_key) payload [0]     : HFI_PROPERTY_OUTPUT_LAYER_DST_ADDR |
 *                                 (version=0 << 20) | (dsize=6 << 24)
 *     (u32_value) payload [1]   : output layer id
 *     (u32_value) payload [2]   : output buffer start address
 *     (u32_value) payload [3]   : Plane start address for Plane 0
 *     (u32_value) payload [4]   : Plane start address for Plane 1
 *     (u32_value) payload [5]   : Plane start address for Plane 2
 *     (u32_value) payload [6]   : Plane start address for Plane 3
 */
#define HFI_PROPERTY_OUTPUT_LAYER_DST_ADDR                            0x00030013

/*
 * HFI_PROPERTY_OUTPUT_LAYER_DST_FORMAT - Gets Destination Format Configuration of output buffer.
 *                                        Host is expected to send this packet
 *                                        of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                        command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_OUTPUT_LAYER_DST_FORMAT
 *     (u32_key) payload [0]     : HFI_PROPERTY_OUTPUT_LAYER_DST_FORMAT |
 *                                 (version=0 << 20) | (dsize=2 << 24)
 *     (u32_value) payload [1]   : output layer id
 *     (u32_value) payload [2]   : one of the formats from enum hfi_color_formats
 */
#define HFI_PROPERTY_OUTPUT_LAYER_DST_FORMAT                         0x00030014

/*
 * HFI_PROPERTY_OUTPUT_LAYER_STRIDE - Gets Stride for each layer.
 *                                    Host is expected to send this packet
 *                                    of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                    command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_OUTPUT_LAYER_STRIDE
 *     (u32_key) payload [0]     : HFI_PROPERTY_OUTPUT_LAYER_STRIDE |
 *                                 (version=0 << 20) | (dsize=5 << 24)
 *     (u32_value) payload [1]   : output layer id
 *     (u32_value) payload [2]   : Stride for Plane 0
 *     (u32_value) payload [3]   : Stride for Plane 1
 *     (u32_value) payload [4]   : Stride for Plane 2
 *     (u32_value) payload [5]   : Stride for Plane 3
 */
#define HFI_PROPERTY_OUTPUT_LAYER_STRIDE                             0x00030015

/*
 * HFI_PROPERTY_LAYER_ROTATION - Gets rotation for each layer.
 *                             Host is expected to send this packet
 *                             of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                             command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_ROTATION
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_ROTATION |
 *                                 (version=0 << 20) | (dsize=2 << 24 )
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2]   : bitwise OR of rotation flags
 *
 */
#define HFI_PROPERTY_LAYER_ROTATION                                  0x00030016

/*
 * HFI_PROPERTY_LAYER_SECURITY_POLICY - Gets security policy settings applied
 *                                      to the layer by the Host.
 *                                      Host is expected to send this packet
 *                                      of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                      command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_SECURITY_POLICY
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_LAYER_SECURITY_POLICY \|
 *                               | (version=0 << 20) \| (dsize=2 << 24 )
 *     (u32_value) payload [1]   | layer id
 *     (u32_value) payload [2]   | one of the enum values in hfi_layer_security_policy
 */
#define HFI_PROPERTY_LAYER_SECURITY_POLICY                           0x00030017

/*
 * HFI_PROPERTY_LAYER_LSR_IN_A_BUFFER - Sets the LSR alpha buffer properties of the input layer.
 *                                      Host is expected to send this packet as part of
 *                                      HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_LSR_IN_A_BUFFER
 *     (u32_key) payload [0]      : HFI_PROPERTY_LAYER_LSR_IN_A_BUFFER |
 *                                  (version=0 << 20) | (dsize=6 << 24)
 *     (u32_value) payload [1]    : layer id
 *     (u32_value) payload [2-6]  : struct hfi_buff
 */
#define HFI_PROPERTY_LAYER_LSR_IN_A_BUFFER                           0x00030018

/*
 * HFI_PROPERTY_LAYER_SRC_SYS_CACHE_ID - Sets the System Cache ID of the source/input layer.
 *                                       Host is expected to send this packet as part of
 *                                       HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_SRC_SYS_CACHE_ID
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_SRC_SYS_CACHE_ID |
 *                                 (version=0 << 20) | (dsize=2 << 16 )
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2]   : System Cache ID
 */
#define HFI_PROPERTY_LAYER_SRC_SYS_CACHE_ID                          0x00030019
/*
 * HFI_PROPERTY_LAYER_LSR_REPROJ_PLANE_EQ - Sets the LSR Reprojection layer's plane equation.
 *                                          Host is expected to send this packet as part of
 *                                          HFI_COMMAND_DISPLAY_SET_PROPERTY command packet
 *                                          payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_LSR_REPROJ_PLANE_EQ
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_LSR_REPROJ_PLANE_EQ |
 *                                 (version=0 << 20) | (dsize=5 << 24)
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2-5] : struct hfi_lsr_plane_equation
 */
#define HFI_PROPERTY_LAYER_LSR_REPROJ_PLANE_EQ                       0x0003001A

/*
 * HFI_PROPERTY_LAYER_GAMMA - Sets the layer's gamma type.
 *                            Host is expected to send this packet as part of
 *                            HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_GAMMA
 *     (u32_key) payload [0]   : HFI_PROPERTY_LAYER_GAMMA |
 *                               (version=0 << 20) | (dsize=2 << 24)
 *     (u32_value) payload [1] : layer id
 *     (u32_value) payload [2] : enum hfi_layer_gamma_type
 */
#define HFI_PROPERTY_LAYER_GAMMA                                     0x0003001B

/*
 * HFI_PROPERTY_LAYER_LSR_LAYER_TYPE - Sets the LSR layer's local/remote/other type.
 *                                     Host is expected to send this packet as part of
 *                                     HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_LSR_LAYER_TYPE
 *     (u32_key) payload [0]   : HFI_PROPERTY_LAYER_LSR_LAYER_TYPE |
 *                               (version=0 << 20) | (dsize=2 << 24)
 *     (u32_value) payload [1] : layer id
 *     (u32_value) payload [2] : enum hfi_lsr_layer_type
 */
#define HFI_PROPERTY_LAYER_LSR_LAYER_TYPE                            0x0003001C

/*
 * HFI_PROPERTY_LAYER_LSR_LOCK_TYPE - Sets the LSR layer's lock type.
 *                                    Host is expected to send this packet as part of
 *                                    HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_LSR_LOCK_TYPE
 *     (u32_key) payload [0]   : HFI_PROPERTY_LAYER_LSR_LOCK_TYPE |
 *                               (version=0 << 20) | (dsize=2 << 24)
 *     (u32_value) payload [1] : layer id
 *     (u32_value) payload [2] : enum hfi_lsr_layer_lock_type
 */
#define HFI_PROPERTY_LAYER_LSR_LOCK_TYPE                             0x0003001D

/*
 * HFI_PROPERTY_LAYER_LSR_RENDER_POSE - Sets the LSR reprojection render pose properties of the
 *                                      layer.
 *                                      Host is expected to send this packet as part of
 *                                      HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_LSR_RENDER_POSE
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_LSR_RENDER_POSE |
 *                                 (version=0 << 20) | (dsize=8 << 24)
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2-8] : struct hfi_lsr_render_pose
 */
#define HFI_PROPERTY_LAYER_LSR_RENDER_POSE                           0x0003001E

/*
 * HFI_PROPERTY_LAYER_LSR_RENDER_FRUSTUM - Sets the LSR render frustum properties of the layer.
 *                                         Host is expected to send this packet as part of
 *                                         HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_LSR_RENDER_FRUSTUM
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_LSR_RENDER_FRUSTUM |
 *                                 (version=0 << 20) | (dsize=5 << 24)
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2-5] : struct hfi_lsr_render_frustum
 */
#define HFI_PROPERTY_LAYER_LSR_RENDER_FRUSTUM                        0x0003001F

/*
 * HFI_PROPERTY_LAYER_LSR_X_BUFFER - Sets custom buffers for this layer.
 *                                   Host is expected to send this packet as part of
 *                                   HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_LSR_X_BUFFER
 *     (u32_key) payload [0]      : HFI_PROPERTY_LAYER_LSR_X_BUFFER |
 *                                  (version=0 << 20) | (dsize=3 + count << 24)
 *     (u32_value) payload [1]    : layer id
 *     (u32_value) payload [2]    : lsr_x_buffer.key
 *     (u32_value) payload [3]    : lsr_x_buffer.count
 *     (u32_value) payload [4-..] : lsr_x_buffer.values
 */
#define HFI_PROPERTY_LAYER_LSR_X_BUFFER                              0x00030020

/*
 * HFI_PROPERTY_LAYER_LSR_REPROJ_POSE - Sets the LSR Reprojection pose properties of the layer.
 *                                      Reprojection pose represents raster scan correction. In
 *                                      case of FSD display, Start, Mid and End poses will be
 *                                      repurposed as R, G and B poses.
 *                                      Host is expected to send this packet as part of
 *                                      HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_LSR_REPROJ_POSE
 *     (u32_key) payload [0]       : HFI_PROPERTY_LAYER_LSR_REPROJ_POSE |
 *                                   (version=0 << 20) | (dsize=22 << 24)
 *     (u32_value) payload [1]     : layer id
 *     (u32_value) payload [2-8]   : struct hfi_lsr_render_pose - Start Pose for view
 *     (u32_value) payload [9-15]  : struct hfi_lsr_render_pose - Mid Pose for view
 *     (u32_value) payload [16-22] : struct hfi_lsr_render_pose - End Pose for view
 */
#define HFI_PROPERTY_LAYER_LSR_REPROJ_POSE                           0x00030021

/*
 * HFI_PROPERTY_LAYER_SRC_IMAGE_SIZE - Sets Source image size of each layer in bytes.
 *                                     Host is expected to send this packet
 *                                     as part of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                     command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_SRC_IMAGE_SIZE
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_SRC_IMAGE_SIZE |
 *                                 (version=0 << 20) | (dsize=2 << 16 )
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2]   : size of the source image in bytes
 */
#define HFI_PROPERTY_LAYER_SRC_IMAGE_SIZE                           0x00030022

/*
 * HFI_PROPERTY_OUTPUT_LAYER_ROTATION - Gets rotation for output layer.
 *                                      Host is expected to send this packet
 *                                      of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                      command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_OUTPUT_LAYER_ROTATION
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_OUTPUT_LAYER_ROTATION \|
 *                               | (version=0 << 20) | (dsize=2 << 24 )
 *     (u32_value) payload [1]   | output layer id
 *     (u32_value) payload [2]   | bitwise OR of rotation flags
 */
#define HFI_PROPERTY_OUTPUT_LAYER_ROTATION                           0x00030023

/*
 * HFI_PROPERTY_LAYER_CACHE_ATTR - Gets sys cache related attributes for each layer.
 *                               Host is expected to send this packet
 *                               of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                               command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_CACHE_ATTR
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_CACHE_ATTR \|
 *                                 (version=0 << 20) \| (dsize=3 << 24 )
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2]   : one of the enum values in hfi_layer_cache_state
 *     (u32_value) payload [3]   : one of the enum values in hfi_layer_cache_op_type
 */
#define HFI_PROPERTY_LAYER_CACHE_ATTR                                0x00030024

/*
 * HFI_PROPERTY_LAYER_LLCC_SCID - Gets LLCC scid of the system cache to be used
 *                                      Host is expected to send this packet as part
 *                                      of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                      command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_LLCC_SCID
 *     (u32_key) payload [0]     : HFI_PROPERTY_LAYER_LLCC_SCID \|
 *                                 (version=0 << 20) \| (dsize=2 << 24 )
 *     (u32_value) payload [1]   : layer id
 *     (u32_value) payload [2]   : LLCC scid
 */
#define HFI_PROPERTY_LAYER_LLCC_SCID                                 0x00030025

/*
 * HFI_PROPERTY_OUTPUT_LAYER_CACHE_ATTR - Gets sys cache related attributes for each
 *                                        output layer.
 *                               Host is expected to send this packet
 *                               of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                               command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_OUTPUT_LAYER_CACHE_ATTR
 *     (u32_key) payload [0]     : HFI_PROPERTY_OUTPUT_LAYER_CACHE_ATTR \|
 *                                 (version=0 << 20) \| (dsize=3 << 24 )
 *     (u32_value) payload [1]   : output layer id
 *     (u32_value) payload [2]   : one of the enum values in hfi_layer_cache_state
 *     (u32_value) payload [3]   : one of the enum values in hfi_layer_cache_op_type
 */
#define HFI_PROPERTY_OUTPUT_LAYER_CACHE_ATTR                         0x00030026

/*
 * HFI_PROPERTY_OUTPUT_LAYER_LLCC_SCID - Gets LLCC scid of the system cache to be used for
 *                                      output layer. Host is expected to send this packet as part
 *                                      of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                      command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_OUTPUT_LAYER_LLCC_SCID
 *     (u32_key) payload [0]     : HFI_PROPERTY_OUTPUT_LAYER_LLCC_SCID \|
 *                                 (version=0 << 20) \| (dsize=2 << 24 )
 *     (u32_value) payload [1]   : output layer id
 *     (u32_value) payload [2]   : LLCC scid
 */
#define HFI_PROPERTY_OUTPUT_LAYER_LLCC_SCID                          0x00030027

/*
 * HFI_PROPERTY_OUTPUT_LAYER_CWB_TAP_POINT - Gets the CWB tap point for output layer.
 *                                           Host is expected to send this packet
 *                                           of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                           command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_OUTPUT_LAYER_CWB_TAP_POINT
 *
 * Hfi packet layout             | Value
 *-------------------------------|------------------------------------------
 *     (u32_key) payload [0]     | HFI_PROPERTY_OUTPUT_LAYER_CWB_TAP_POINT \|
 * ^                             | (version=0 << 20) \| (dsize=2 << 24)
 *     (u32_value) payload [1]   | wb_id
 *     (u32_value) payload [2]   | one of the values from enum hfi_cwb_tap_points
 */
#define HFI_PROPERTY_OUTPUT_LAYER_CWB_TAP_POINT                      0x00030028

/*
 * All layer color properties begin here
 */
#define HFI_PROPERTY_LAYER_COLOR_BEGIN                               0x00030100

/*
 * HFI_PROPERTY_LAYER_COLOR_3D_LUT - This property is to setup VIG GAMUT.
 *                                   Host is expected to send this packet of
 *                                   HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_COLOR_3D_LUT
 *   (u32_key) payload [0]       : HFI_PROPERTY_LAYER_COLOR_3D_LUT |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : layer_id
 *   (u32_value) payload [2]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_LAYER_COLOR_3D_LUT                              0x00030101

/*
 * HFI_PROPERTY_LAYER_COLOR_UCSC_IGC - This property is to setup UCSC IGC.
 *                                     Host is expected to send this packet
 *                                     of HFI_COMMAND_DISPLAY_SET_PROPERTY
 *                                     command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_COLOR_UCSC_IGC
 *   (u32_key) payload [0]       : HFI_PROPERTY_LAYER_COLOR_UCSC_IGC |
 *                                 (version=0 << 20) | (dsize=(sizeof(u32)/4) << 24)
 *   (u32_value) payload [1]     : layer_id
 *   (u32_value) payload [2]     : u32
 */
#define HFI_PROPERTY_LAYER_COLOR_UCSC_IGC                            0x00030102

/*
 * HFI_PROPERTY_LAYER_COLOR_UCSC_UNMULT - This property is to setup UCSC UNMULT.
 *                                        Host is expected to send this packet of
 *                                        HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_COLOR_UCSC_UNMULT
 *   (u32_key) payload [0]       : HFI_PROPERTY_LAYER_COLOR_UCSC_UNMULT |
 *                                 (version=0 << 20) | (dsize=(sizeof(u32)/4) << 24)
 *   (u32_value) payload [1]     : layer_id
 *   (u32_value) payload [2]     : u32
 */
#define HFI_PROPERTY_LAYER_COLOR_UCSC_UNMULT                         0x00030103

/*
 * HFI_PROPERTY_LAYER_COLOR_UCSC_GC - This property is to setup UCSC GC.
 *                                    Host is expected to send this packet of
 *                                    HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_COLOR_UCSC_GC
 *   (u32_key) payload [0]       : HFI_PROPERTY_LAYER_COLOR_UCSC_GC |
 *                                 (version=0 << 20) | (dsize=(sizeof(u32)/4) << 24)
 *   (u32_value) payload [1]     : layer_id
 *   (u32_value) payload [2]     : u32
 */
#define HFI_PROPERTY_LAYER_COLOR_UCSC_GC                             0x00030104

/*
 * HFI_PROPERTY_LAYER_COLOR_UCSC_ALPHA_DITHER - This property is to setup UCSC ALPHA DITHER.
 *                                              Host is expected to send this packet of
 *                                              HFI_COMMAND_DISPLAY_SET_PROPERTY command packet
 *                                              payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_COLOR_UCSC_ALPHA_DITHER
 *   (u32_key) payload [0]       : HFI_PROPERTY_LAYER_COLOR_UCSC_ALPHA_DITHER |
 *                                 (version=0 << 20) | (dsize=(sizeof(u32)/4) << 24)
 *   (u32_value) payload [1]     : layer_id
 *   (u32_value) payload [2]     : u32
 */
#define HFI_PROPERTY_LAYER_COLOR_UCSC_ALPHA_DITHER                   0x00030105

/*
 * HFI_PROPERTY_LAYER_COLOR_UCSC_CSC - This property is to setup UCSC CSC.
 *                                     Host is expected to send this packet of
 *                                     HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_COLOR_UCSC_CSC
 *   (u32_key) payload [0]       : HFI_PROPERTY_LAYER_COLOR_UCSC_CSC |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_ucsc_csc)/4) << 24)
 *   (u32_value) payload [1]     : layer_id
 *   (u32_value) payload [2]     : struct hfi_ucsc_csc
 */
#define HFI_PROPERTY_LAYER_COLOR_UCSC_CSC                            0x00030106

/*
 * HFI_PROPERTY_LAYER_COLOR_CSC - This property is to setup CSC.
 *                                Host is expected to send this packet of
 *                                HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_COLOR_CSC
 *   (u32_key) payload [0]       : HFI_PROPERTY_LAYER_COLOR_CSC |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_csc)/4) << 24)
 *   (u32_value) payload [1]     : layer_id
 *   (u32_value) payload [2]     : struct hfi_csc
 */
#define HFI_PROPERTY_LAYER_COLOR_CSC                                 0x00030107

/*
 * HFI_PROPERTY_LAYER_COLOR_SCALER - This property is to setup VIG scaler.
 *                                   Host is expected to send this packet of
 *                                   HFI_COMMAND_DISPLAY_SET_PROPERTY command packet payload.
 *
 * @BasicFuntionality - HFI_PROPERTY_LAYER_COLOR_SCALER
 *   (u32_key) payload [0]       : HFI_PROPERTY_LAYER_COLOR_SCALER |
 *                                 (version=0 << 20) | (dsize=(sizeof(struct hfi_buff_dpu)/4) << 24)
 *   (u32_value) payload [1]     : layer_id
 *   (u32_value) payload [2]     : struct hfi_buff_dpu
 */
#define HFI_PROPERTY_LAYER_COLOR_SCALER                              0x00030108

/*
 * All layer color properties end here
 */
#define HFI_PROPERTY_LAYER_COLOR_END                                 0x000301FF

/*
 * All layer property IDs end here
 */
#define HFI_PROPERTY_LAYER_END                                       0x0003FFFF

#endif // __H_HFI_PROPERTIES_DISPLAY_H__
