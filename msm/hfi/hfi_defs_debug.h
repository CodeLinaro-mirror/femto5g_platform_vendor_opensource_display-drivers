/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_DEFS_DEBUG_H__
#define __H_HFI_DEFS_DEBUG_H__

/*
 * This is documentation file. Not used for header inclusion.
 */

#include "hfi_defs_common.h"

/**
 * @brief Definitions for display panic events.
 */

/**
 * @def HFI_DEBUG_EVENT_UNDERRUN
 * @brief Bitmask for event indicating an underrun.
 */
#define HFI_DEBUG_EVENT_UNDERRUN   (1 << 0)

/**
 * @def HFI_DEBUG_EVENT_HW_RESET
 * @brief Bitmask for event indicating a hardware reset.
 */
#define HFI_DEBUG_EVENT_HW_RESET   (1 << 1)

/**
 * @def HFI_DEBUG_EVENT_PP_TIMEOUT
 * @brief Bitmask for event indicating a ping-pong timeout.
 */
#define HFI_DEBUG_EVENT_PP_TIMEOUT (1 << 2)

/**
 * struct debug_display_evt_info - display panic event info
 * @display_id: Display ID
 * @events_mask: Bitwise 'OR' of panic event flags
 * @enable: Enable/Disable events
 */
struct debug_display_evt_info {
	uint32_t display_id;
	uint32_t events_mask;
	uint32_t enable;
};

/**
 * struct panic_info - display panic information
 * @display_id: Display ID
 * @events_mask: Bitwise 'OR' of panic event flags
 */
struct panic_info {
	uint32_t display_id;
	uint32_t events_mask;
};


/**
 * struct regdump_info - register dump information at a requested offset & length
 * @device_id: Device ID
 * @reg_offset: Offset from MDSS base
 * @length: Required length to be dumped
 * @buffer_info: Allocated memory for dumping
 */
struct regdump_info {
	uint32_t device_id;
	u32 reg_offset;
	u32 length;
	struct hfi_buff buffer_info;
};

/*
 * enum hfi_debug_misr_module - Module to obtain misr
 * @HFI_DEBUG_MISR_DSI      :   Dsi module
 * @HFI_DEBUG_MISR_MIXER    :   Mixer module
 * @HFI_DEBUG_MISR_INTF     :   Interface module
 */
enum hfi_debug_misr_module_type {
	HFI_DEBUG_MISR_DSI                     = 0x0,
	HFI_DEBUG_MISR_MIXER                   = 0x1,
	HFI_DEBUG_MISR_INTF                    = 0x2,
};

/*!
 * @enum hfi_debug_feature
 * @brief Features for which DCP supports different debug levels
 *
 * @var HFI_DEBUG_FEATURE_LSR: Setup debug logs levels for LSR feature
 */
enum hfi_debug_feature {
	HFI_DEBUG_FEATURE_LSR = 0x0,
};

/*!
 * @enum hfi_debug_log_level
 * @brief Defines the log levels set to DCP.
 *
 * Log levels set to DCP to get the debugs logs.
 *
 * @var HFI_DEBUG_LOG_LEVEL_NONE
 *   No debug logs.
 * @var HFI_DEBUG_LOG_LEVEL_LOW
 *   Low level debug logs.
 * @var HFI_DEBUG_LOG_LEVEL_MEDIUM
 *   Medium level debug logs.
 * @var HFI_DEBUG_LOG_LEVEL_HIGH
 *   High level debug logs.
 * @var HFI_DEBUG_LOG_LEVEL_ERROR
 *   Error level debug logs.
 */
enum hfi_debug_log_level {
	HFI_DEBUG_LOG_LEVEL_NONE      = 0,
	HFI_DEBUG_LOG_LEVEL_LOW       = 1,
	HFI_DEBUG_LOG_LEVEL_MEDIUM    = 2,
	HFI_DEBUG_LOG_LEVEL_HIGH      = 3,
	HFI_DEBUG_LOG_LEVEL_ERROR     = 4
};

/*
 * struct misr_setup_data - MISR setup information
 * @display_id      : display_id of required display
 * @enable          : Enable bit for MISR setup register
 * @frame_count     : Number of frames for MISR setup register
 * @module_type     : module to obtain MISR value from
 */
struct misr_setup_data {
	u32 display_id;
	u32 enable;
	u32 frame_count;
	enum hfi_debug_misr_module_type module_type;
};

/*
 * struct misr_read_data - MISR read information
 * @display_id      : display_id of required display
 * @module_type     : module to obtain MISR value from
 */
struct misr_read_data {
	u32 display_id;
	enum hfi_debug_misr_module_type module_type;
};

/*
 * HFI data structure definitions for DP Simulation commands.
 * These structures define the payload formats for commands and responses.
 */

/*
 * Read DPCD registers
 *
 * @buffer:
 *     Shared buffer info
 * @dpcd_offset:
 *     DPCD register offset
 * @bytes:
 *     Number of bytes to read
 */
struct hfi_dp_dpcd_request {
	struct hfi_buff buffer;
	u32 dpcd_offset;
	u32 bytes;
};

/*
 * Write DPCD registers
 *
 * @offset:
 *     DPCD register offset
 * @size:
 *     Number of bytes to write (up to 256 bytes)
 * @data:
 *     DPCD register data
 */
struct hfi_dp_dpcd_data {
	u32 offset;
	u32 size;
	u8  data[256];
};

/*
 * Set display mode
 *
 * @hdisplay:
 *     Horizontal display resolution
 * @vdisplay:
 *     Vertical display resolution
 * @vrefresh:
 *     Refresh rate in Hz
 * @aspect_ratio:
 *     Aspect ratio
 */
struct hfi_dp_mode_select_info {
	u32 hdisplay;
	u32 vdisplay;
	u32 vrefresh;
	u32 aspect_ratio;
};

/*
 * Read CRC values response
 *
 * @status:
 *     Status code (0 = success)
 * @src_crc:
 *     RGB CRC values from source
 * @sink_crc:
 *     RGB CRC values from sink
 */
struct hfi_dp_crc_info {
	u32 status;
	uint16_t src_crc[HFI_MAX_COLOR_COMPONENTS];
	uint16_t sink_crc[HFI_MAX_COLOR_COMPONENTS];
};

/*!
 * @struct hfi_debug_log_level_info
 * @brief Struct to configure different debug levels across features.
 *
 * @var feature
 *   Feature to enable the debug logs.
 * @var level_bitmask
 *   Log level bitmask to be enabled for a feature. Refer hfi_debug_log_level which represents
 *   the bit number to be set to enable/disable the particular debug log level.
 */
struct hfi_debug_log_level_info {
	enum hfi_debug_feature feature;
	uint32_t level_bitmask;
};

/**
 * @enum hfi_display_dbg_property_id - Defines HFI debug property ID.
 *
 * @HFI_DISPLAY_DEBUG_ESD_CHECK_MODE     :  Property ID for esd check mode update
 * @HFI_DISPLAY_DEBUG_ESD_CHECK_INTERVAL :  Property ID for status check interval update.
 * @HFI_DISPLAY_DEBUG_UIDLE              :  Property ID for micro-idle (uidle) state query.
 */
enum hfi_display_dbg_property_id {
	HFI_DISPLAY_DEBUG_ESD_CHECK_MODE           = 0x1,
	HFI_DISPLAY_DEBUG_ESD_CHECK_INTERVAL       = 0x2,
	HFI_DISPLAY_DEBUG_UIDLE                    = 0x3,
};

/**
 * struct hfi_display_dbg_property - HFI DCP transfer display debug property.
 *
 * @display_id:       display_id of required display
 * @prop_id:          Property id specified in enum hfi_display_dbg_property_id.
 * @value_lsb:        Data value / LSB of payload address.
 * @value_msb:        Data value / MSB of payload address.
 * @reserved1:        Reserved for future use.
 * @reserved2:        Reserved for future use.
 */
struct hfi_display_dbg_property {
	u32 display_id;
	enum hfi_display_dbg_property_id prop_id;

	u32 value_lsb;
	u32 value_msb;

	u32 reserved1;
	u32 reserved2;
};

/*!
 * @enum hfi_debug_subsystem_property_id
 * @brief Subsystem property id's.
 *
 * @var HFI_DEBUG_SUBSYSTEM_PROPERTY_TRIGGER_ERROR
 *   Trigger subsystem error.
 */
enum hfi_debug_subsystem_property_id  {
	HFI_DEBUG_SUBSYSTEM_PROPERTY_TRIGGER_ERROR,
};

/*!
 * @struct hfi_debug_subsystem_property
 * @brief HFI DCP receive subsystem debug property.
 *
 * @var subsystem_type
 *   Subsystem type specified in enum hfi_subsystem_type.
 * @var prop_id
 *   Property id specified in enum hfi_debug_subsystem_property_id.
 * @var value_lsb
 *   Data value / LSB of payload address.
 * @var value_msb
 *   Data value / MSB of payload address.
 * @var reserved1
 *   Reserved for future use.
 * @var reserved2
 *   Reserved for future use.
 */
struct hfi_debug_subsystem_property {
	enum hfi_subsystem_type subsystem_type;
	enum hfi_debug_subsystem_property_id prop_id;

	u32 value_lsb;
	u32 value_msb;

	u32 reserved1;
	u32 reserved2;
};

#endif // __H_HFI_DEFS_DEBUG_H__
