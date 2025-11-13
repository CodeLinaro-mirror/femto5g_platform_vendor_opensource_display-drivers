/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_DEFS_DISPLAY_H__
#define __H_HFI_DEFS_DISPLAY_H__

#include <hfi_defs_common.h>

/*
 * This is documentation file. Not used for header inclusion.
 */

/*
 * enum hfi_display_blend_ops - Different blend operations
 * @HFI_BLEND_OP_NOT_DEFINED     :    No blend operation defined for the layer.
 * @HFI_BLEND_OP_OPAQUE          :    Apply a constant blend operation. The layer
 *                                    would appear opaque in case fg plane alpha is
 *                                    0xff.
 * @HFI_BLEND_OP_PREMULTIPLIED   :    Apply source over blend rule. Layer already has
 *                                    alpha pre-multiplication done. If fg plane alpha
 *                                    is less than 0xff, apply modulation as well. This
 *                                    operation is intended on layers having alpha
 *                                    channel.
 * @HFI_BLEND_OP_COVERAGE        :    Apply source over blend rule. Layer is not alpha
 *                                    pre-multiplied. Apply pre-multiplication. If fg
 *                                    plane alpha is less than 0xff, apply modulation as
 *                                    well.
 * @HFI_BLEND_OP_SKIP            :   Operation to skip blending explicitly
 * @HFI_BLEND_OP_MAX             :    Used to track maximum blend operation possible.
 */
enum hfi_display_blend_ops {
	HFI_BLEND_OP_NOT_DEFINED        = 0x0,
	HFI_BLEND_OP_OPAQUE             = 0x1,
	HFI_BLEND_OP_PREMULTIPLIED      = 0x2,
	HFI_BLEND_OP_COVERAGE           = 0x3,
	HFI_BLEND_OP_SKIP               = 0x4,
	HFI_BLEND_OP_MAX                = 0x5,
};

/*
 * enum hfi_display_power_mode - extended power modes supported by the Display
 * @HFI_MODE_DPMS_OFF     :   OFF
 * @HFI_MODE_DPMS_ON      :   ON
 * @HFI_MODE_DPMS_LP1     :   Low power mode 1
 * @HFI_MODE_DPMS_LP2     :   Low power mode 2
 * @HFI_MODE_DPMS_NOLP    :   Normal mode or No Low Power mode
 */
enum hfi_display_power_mode {
	HFI_MODE_DPMS_INVALID   = 0,
	HFI_MODE_DPMS_OFF       = 0x1,
	HFI_MODE_DPMS_ON        = 0x2,
	HFI_MODE_DPMS_LP1       = 0x3,
	HFI_MODE_DPMS_LP2       = 0x4,
	HFI_MODE_DPMS_NOLP      = 0x5,
};

/*
 * enum hfi_display_power_control - Bitmask to control power supplies for Panel/Controller/PHY
 * @HFI_PANEL_POWER      :   Panel Power
 * @HFI_CTRL_POWER       :   Controller Power
 * @HFI_PHY_POWER        :   PHY Power
 */
enum hfi_display_power_control {
	HFI_PANEL_POWER      = 0x1,
	HFI_CTRL_POWER       = 0x2,
	HFI_PHY_POWER        = 0x4,
};

/*
 * enum hfi_display_commit_flag - commit flags
 * @HFI_VALIDATE      :   validation flag
 * @HFI_COMMIT        :   commit flag
 */
enum hfi_display_commit_flag {
	HFI_VALIDATE           = 0x1,
	HFI_COMMIT             = 0x2,
};

/*
 * struct hfi_display_roi
 * @x_pos    :  x position of the roi
 * @y_pos    :  y position of the roi
 * @width    :  width of the roi
 * @height   :  height of the roi
 */
struct hfi_display_roi {
	u32 x_pos;
	u32 y_pos;
	u32 width;
	u32 height;
};

/*
 * enum hfi_display_roi_type - type of destination ROI programming
 * @PANEL_ROI: ROI is panel ROI
 */
enum hfi_display_roi_type {
	PANEL_ROI = 0x0,
};

/*
 * struct hfi_display_vsync_data - vsync data
 * @timestamp_lo    :  lower value of 64bit vsync timestamp in ns
 * @timestamp_hi    :  higher value of 64bit vsync timestamp in ns
 * @vsync_index     :  vsync index for the timestamp
 */
struct hfi_display_vsync_data {
	u32 timestamp_lo;
	u32 timestamp_hi;
	u32 vsync_index;
};

/*
 * struct hfi_display_frame_event_data - frame event data
 * @timestamp_lo         :  lower value of 64bit Buffer flip timestamp in ns
 * @timestamp_hi         :  higher value of 64bit Buffer flip timestamp in ns
 * @bufferflip_index     :  bufferflip index for the timestamp
 */
struct hfi_display_frame_event_data {
	u32 timestamp_lo;
	u32 timestamp_hi;
	u32 bufferflip_index;
};

/*
 * struct hfi_display_color - color description
 *
 * @color_0      : Green
 * @color_1      : Blue
 * @color_2      : Red
 * @color_3      : Alpha
 */
struct hfi_display_color {
	u16 color_0;
	u16 color_1;
	u16 color_2;
	u16 color_3;
};

/*
 * enum hfi_display_dim_layer_flag - Dim layer flags
 *
 * HFI_DIM_LAYER_INCLUSIVE : The color fill will be applied inside the bounds of the specified ROI
 * HFI_DIM_LAYER_EXCLUSIVE : The color fill will be applied outside the bounds of the specified ROI
 */
enum hfi_display_dim_layer_flag {
	HFI_DIM_LAYER_INCLUSIVE = 0x1,
	HFI_DIM_LAYER_EXCLUSIVE = 0x2,
};

/*
 * struct hfi_display_dim_layer - dim layer config
 *
 * @flags        : Flag to represent INCLUSIVE/EXCLUSIVE
 * @stage        : Blending stage of dim layer
 * @color_fill   : Color fill to be used for the layer
 * @rect         : Dim layer coordinates
 */
struct hfi_display_dim_layer {
	enum hfi_display_dim_layer_flag flags;
	u32 stage;
	struct hfi_display_color color_fill;
	struct hfi_display_roi rect;
};

/*
 * enum hfi_layer_cache_state - Layer cache states.
 *
 * HFI_CACHE_STATE_DISABLE: Disable cache read/write.
 * HFI_CACHE_STATE_READ: Read from DDR and allocate into system cache, in subsequent frames
 *                       read from cache (GPU Idle fallback)
 * HFI_CACHE_STATE_WRITE: Write into system cache during the last composition frame, in
 *                        subsequent frames read from cache (CWB based idle fallback)
 */
enum hfi_layer_cache_state {
	HFI_CACHE_STATE_DISABLE = 0x0,
	HFI_CACHE_STATE_READ = 0x1,
	HFI_CACHE_STATE_WRITE = 0x2,
};

/*
 * enum hfi_layer_cache_op_type - System cache read op type
 *
 * HFI_CACHE_OP_TYPE_NONE          : No SW overwrite and driven by hardware
 * HFI_CACHE_NORMAL_CACHEABLE_READ : Normal Cacheable Read
 * HFI_CACHE_READ_INVALIDATE       : Read With Invalidate (RWI)
 * HFI_CACHE_READ_EVICT            : Read With Evict (RWE)
 * HFI_CACHE_PREFETCH_READ         : Prefetch Read (PRE)
 */
enum hfi_layer_cache_op_type {
	HFI_CACHE_OP_TYPE_NONE = 0x0,
	HFI_CACHE_NORMAL_CACHEABLE_READ = 0x1,
	HFI_CACHE_READ_INVALIDATE = 0x2,
	HFI_CACHE_READ_EVICT = 0x3,
	HFI_CACHE_PREFETCH_READ = 0x4,
};

/*
 * struct hfi_display_autorefresh_cfg - autorefresh config data.
 * @enable        :  autorefresh enable/disable.
 * @frame_count   :  autorefresh frame number for controlling frame rate.
 */
struct hfi_display_autorefresh_cfg {
	u32 enable;
	u32 frame_count;
};

/*
 * @struct hfi_display_idle_event_data
 * @brief Idle event data
 *
 * @var timestamp_lo
 *   Lower 32 bits of the 64-bit idle event timestamp in ns.
 * @var timestamp_hi
 *   Higher 32 bits of the 64-bit idle event timestamp in ns.
 * @var idle_index
 *   Idle index for the timestamp.
 */
struct hfi_display_idle_event_data {
	u32 timestamp_lo;
	u32 timestamp_hi;
	u32 idle_index;
};

/*
 * @struct hfi_display_power_event_data
 * @brief Power event data
 *
 * @var timestamp_lo
 *   Lower 32 bits of the 64-bit power event timestamp in ns.
 * @var timestamp_hi
 *   Higher 32 bits of the 64-bit power event timestamp in ns.
 * @var power_state
 *   power_state corresponding to which power mode we are in.
 */
struct hfi_display_power_event_data {
	u32 timestamp_lo;
	u32 timestamp_hi;
	enum hfi_display_power_mode power_state;
};

/*
 * @enum hfi_display_idle_timer_control
 * @brief Enum to control idle timer.
 *
 * @var HFI_DEFAULT
 *   Restore idle timer to default state
 * @var HFI_WAKEUP
 *   Restore the display from power collapse state.
 * @var HFI_BLOCK_TIMER
 *   Block the idle timer from expiring
 * @var HFI_UNBLOCK_TIMER
 *   Unblock the idle timer from expiring
 */
enum hfi_display_idle_timer_control {
	HFI_DEFAULT          = 0x0,
	HFI_WAKEUP           = 0x1,
	HFI_BLOCK_TIMER      = 0x2,
	HFI_UNBLOCK_TIMER    = 0x3,
};

/*
 * HFI event ID.
 *
 * @HFI_EVENT_VSYNC:
 *     Event ID for vsync.
 * @HFI_EVENT_FRAME_SCAN_START:
 *     Event ID for frame scan start.
 * @HFI_EVENT_FRAME_SCAN_COMPLETE:
 *     Event ID for frame scan complete.
 * @HFI_EVENT_FRAME_IDLE:
 *     Event ID for frame idle.
 * @HFI_EVENT_DISPLAY_POWER:
 *     Event ID for display power.
 * @HFI_EVENT_FRAME_CAPTURE_COMPLETE:
 *     Event ID for frame capture complete.
 * @HFI_EVENT_PANEL_DEAD:
 *     Event ID for panel dead.
 * @HFI_EVENT_LTM:
 *     Event ID for LTM.
 * @HFI_EVENT_RGB_HIST:
 *     Event ID for RGB histogram.
 * @HFI_EVENT_PA_HIST:
 *     Event ID for PA histogram.
 * @HFI_EVENT_HPD_STATUS:
 *     Event ID for Hot Plug Detect Status
 * @HFI_EVENT_DISPLAY_EDID_INFO:
 *     Event ID for EDID info
 * @var HFI_EVENT_SPR_OPR
 *   EVENT ID for SPR OPR
 * @var HFI_EVENT_INTF_MISR
 *   EVENT ID for Interface MISR
 */
enum hfi_display_event_id {
	HFI_EVENT_VSYNC               = 0x1,
	HFI_EVENT_FRAME_SCAN_START    = 0x2,
	HFI_EVENT_FRAME_SCAN_COMPLETE = 0x3,
	HFI_EVENT_FRAME_IDLE          = 0x4,
	HFI_EVENT_DISPLAY_POWER       = 0x5,
	HFI_EVENT_HW_RECOVERY         = 0x6,
	HFI_EVENT_FRAME_CAPTURE_COMPLETE = 0x7,
	HFI_EVENT_PANEL_DEAD          = 0x8,
	HFI_EVENT_LTM                 = 0x9,
	HFI_EVENT_RGB_HIST            = 0xa,
	HFI_EVENT_PA_HIST             = 0xb,
	HFI_EVENT_HPD_STATUS          = 0xc,
	HFI_EVENT_DISPLAY_EDID_INFO   = 0xd,
	HFI_EVENT_SPR_OPR             = 0xe,
	HFI_EVENT_INTF_MISR           = 0xf,
};

/*
 * DP event types for HFI display notifications
 *
 * @HFI_DP_EVENT_NONE:
 *     No DP event (value: 0)
 * @HFI_DP_EVENT_HPD_PLUGGED:
 *     Hot plug detect - display connected (value: 1)
 * @HFI_DP_EVENT_IRQ_HPD:
 *     Interrupt request hot plug detect - display interrupt (value: 2)
 * @HFI_DP_EVENT_SET_MODE:
 *     Set mode event (value: 3)
 * @HFI_DP_EVENT_HPD_UNPLUGGED:
 *     Hot plug detect - display disconnected (value: 4)
 */
enum hfi_display_dp_event {
	HFI_DP_EVENT_NONE         = 0x0,
	HFI_DP_EVENT_HPD_PLUGGED  = 0x1,
	HFI_DP_EVENT_IRQ_HPD      = 0x2,
	HFI_DP_EVENT_SET_MODE     = 0x3,
	HFI_DP_EVENT_HPD_UNPLUGGED = 0x4,
};

/*
 * DP states for HFI display notifications
 *
 * @HFI_DP_STATE_DISCONNECTED:
 *     Display is disconnected (value: 0)
 * @HFI_DP_STATE_HPD_IN:
 *     Hot plug detect in (value: 1)
 * @HFI_DP_STATE_CONNECTED:
 *     Display is connected and ready (value: 2)
 * @HFI_DP_STATE_HPD_OUT:
 *     Hot plug detect out (value: 3)
 */
enum hfi_display_dp_state {
	HFI_DP_STATE_DISCONNECTED = 0x0,
	HFI_DP_STATE_HPD_IN       = 0x1,
	HFI_DP_STATE_CONNECTED    = 0x2,
	HFI_DP_STATE_HPD_OUT      = 0x3,
};

/*
 * Display connection data for DP
 *
 * @dp_evt:
 *     DP event type
 * @dp_state:
 *     DP state
 */
struct hfi_display_hpd_status {
	enum hfi_display_dp_event dp_evt;
	enum hfi_display_dp_state dp_state;
};

/*
 * struct hfi_display_mode_info - hfi dcp mode info
 * @size            :  Size of hfi_dcs_mode_info structure.
 * @h_active        :  Active width of one frame in pixels.
 * @h_back_porch    :  Horizontal back porch in pixels.
 * @h_sync_width    :  HSYNC width in pixels.
 * @h_front_porch   :  Horizontal front porch in pixels.
 * @h_skew          :  Horizontal sync skew value
 * @h_sync_polarity :  Polarity of HSYNC (false is active low).
 * @v_active        :  Active height of one frame in lines.
 * @v_back_porch    :  Vertical back porch in lines.
 * @v_sync_width    :  VSYNC width in lines.
 * @v_front_porch   :  Vertical front porch in lines.
 * @v_sync_polarity :  Polarity of VSYNC (false is active low).
 * @clk_rate_hz_lo  :  Lower address value DSI bit clock rate per lane in Hz.
 * @clk_rate_hz_hi  :  Upper address value of DSI bit clock rate per lane in Hz.
 * @flags_lo        :  Lower address value of flags.
 * @flags_hi        :  Upper address value of flags.
 * @reserved1       :  Reserved for future use.
 * @reserved2       :  Reserved for future use.
 */
struct hfi_display_mode_info {
	u32 size;
	u32 h_active;
	u32 h_back_porch;
	u32 h_sync_width;
	u32 h_front_porch;
	u32 h_skew;
	u32 h_sync_polarity;
	u32 v_active;
	u32 v_back_porch;
	u32 v_sync_width;
	u32 v_front_porch;
	u32 v_sync_polarity;
	u32 refresh_rate;
	u32 clk_rate_hz_lo;
	u32 clk_rate_hz_hi;
	u32 flags_lo;
	u32 flags_hi;
	u32 reserved1;
	u32 reserved2;
};

/*
 * struct hfi_dsi_cmd_desc - hfi dcp transfer dcs data
 * @size             :  Size of this struct used for backward compatibility.
 * @channel          :  DSI virtual channel id
 * @type             :  MIPI DSI data type of the DCS command.
 * @flags            :  MIPI flags controlling this message transmission.
 *                      Ex: MIPI_DSI_MSG_UNICAST_COMMAND
 * @tx_len           :  Transfer buffer length.
 * @tx_buff_addr_lsb :  Tx command buffer DCP address location (lo).
 * @tx_buff_addr_msb :  Tx command buffer DCP address location (hi).
 * @rx_len           :  Receiving buffer length.
 * @rx_buff_addr_lsb :  Rx command buffer DCP address location (lo).
 * @rx_buff_addr_msb :  Rx command buffer DCP address location (hi).
 * @ctrl_idx         :  DSI controller index
 * @ctrl_flags       :  CTRL flags.
 * @last_command     :  Is last DCS command.
 * @post_wait_ms     :  Wait time in milliseconds.
 * @reserved1        :  Reserved for future use.
 * @reserved2        :  Reserved for future use.
 */
struct hfi_dsi_cmd_desc {
	u32 size;

	u8 channel;
	u8 type;
	u16 flags;

	/* Transmit buffer information */
	u32 tx_len;
	u32 tx_buff_addr_lsb;
	u32 tx_buff_addr_msb;

	/* Receive buffer information */
	u32 rx_len;
	u32 rx_buff_addr_lsb;
	u32 rx_buff_addr_msb;

	/* Control information */
	u32 ctrl_idx;
	u32 ctrl_flags;
	u32 last_command;
	u32 post_wait_ms;

	/* Reserved for future use */
	u32 reserved1;
	u32 reserved2;
};

/*
 * enum hfi_display_blend_stage - Defines blending stages
 * @HFI_BLEND_STAGE_BASE    :  base layer
 * @HFI_BLEND_STAGE_0       :  Blend Stage #0(One base layer + one foreground layer)
 * @HFI_BLEND_STAGE_1       :  Blend Stage #1(Output of blend stage#0 + one foreground layer)
 * @HFI_BLEND_STAGE_2       :  Blend Stage #2(Output of blend stage#1 + one foreground layer)
 * @HFI_BLEND_STAGE_3       :  Blend Stage #3(Output of blend stage#2 + one foreground layer)
 * @HFI_BLEND_STAGE_4       :  Blend Stage #4(Output of blend stage#3 + one foreground layer)
 * @HFI_BLEND_STAGE_5       :  Blend Stage #5(Output of blend stage#4 + one foreground layer)
 * @HFI_BLEND_STAGE_6       :  Blend Stage #6(Output of blend stage#5 + one foreground layer)
 * @HFI_BLEND_STAGE_7       :  Blend Stage #7(Output of blend stage#6 + one foreground layer)
 * @HFI_BLEND_STAGE_8       :  Blend Stage #8(Output of blend stage#7 + one foreground layer)
 * @HFI_BLEND_STAGE_9       :  Blend Stage #9(Output of blend stage#8 + one foreground layer)
 * @HFI_BLEND_STAGE_10      :  Blend Stage #10(Output of blend stage#9 + one foreground layer)
 */
enum hfi_display_blend_stage {
	HFI_BLEND_STAGE_BASE,
	HFI_BLEND_STAGE_0,
	HFI_BLEND_STAGE_1,
	HFI_BLEND_STAGE_2,
	HFI_BLEND_STAGE_3,
	HFI_BLEND_STAGE_4,
	HFI_BLEND_STAGE_5,
	HFI_BLEND_STAGE_6,
	HFI_BLEND_STAGE_7,
	HFI_BLEND_STAGE_8,
	HFI_BLEND_STAGE_9,
	HFI_BLEND_STAGE_10,
};

/*
 * enum hfi_layer_fetch_mode - Layer fetch modes
 * @HFI_PARALLEL_FETCH       :       Parallel Fetch mode
 * @HFI_TIME_MULTIPLEX_FETCH :       Time-Multiplexed (Serial) Fetch mode
 */
enum hfi_layer_fetch_mode {
	HFI_PARALLEL_FETCH        = 0x0,
	HFI_TIME_MULTIPLEX_FETCH  = 0x1,
};

/*
 * @enum hfi_layer_security_policy
 * @brief Security policies for layers.
 *
 * @var HFI_LAYER_SECURITY_POLICY_NON_SECURE
 *   Default security mode with no security restrictions.
 * @var HFI_LAYER_SECURITY_POLICY_SECURE
 *   Secure mode with S1 and S2 translation.
 * @var HFI_LAYER_SECURITY_POLICY_SECURE_DIR_TRANSLATION
 *   Secure mode with S2 translation.
 * @var HFI_LAYER_SECURITY_POLICY_MAX
 *   Used to track the maximum security policy value possible.
 */
enum hfi_layer_security_policy {
	HFI_LAYER_SECURITY_POLICY_NON_SECURE                  = 0x0,
	HFI_LAYER_SECURITY_POLICY_SECURE                      = 0x1,
	HFI_LAYER_SECURITY_POLICY_SECURE_DIR_TRANSLATION      = 0x2,
	HFI_LAYER_SECURITY_POLICY_MAX
};

/**
 * @def HFI_DISPLAY_ROTATION_0
 * @brief Set when layer is not rotated.
 */
#define HFI_DISPLAY_ROTATION_0   (1 << 0)

/**
 * @def HFI_DISPLAY_ROTATION_90
 * @brief Set when layer is rotated by 90 degrees.
 */
#define HFI_DISPLAY_ROTATION_90   (1 << 1)

/**
 * @def HFI_DISPLAY_ROTATION_180
 * @brief Set when layer is rotated by 180 degrees.
 */
#define HFI_DISPLAY_ROTATION_180     (1 << 2)

/**
 * @def HFI_DISPLAY_ROTATION_270
 * @brief Set when layer is rotated by 270 degrees.
 */
#define HFI_DISPLAY_ROTATION_270   (1 << 3)

/**
 * @def HFI_DISPLAY_REFLECT_X
 * @brief Set when layer is reflected along X-axis.
 */
#define HFI_DISPLAY_REFLECT_X   (1 << 4)

/**
 * @def HFI_DISPLAY_REFLECT_Y
 * @brief Set when layer is reflected along Y-axis.
 */
#define HFI_DISPLAY_REFLECT_Y   (1 << 5)

/**
 * @enum hfi_cwb_tap_points - CWB tap points.
 * @HFI_TAP_POINT_NONE    :  CWB is disabled
 * @HFI_TAP_POINT_LM    :  Tap point at the LM stage
 * @HFI_TAP_POINT_DSPP    :  Tap point at the DSPP stage
 * @HFI_TAP_POINT_DEMURA   :  Tap point after Demura correction
 * @HFI_TAP_POINT_MAX    :  Maximum number of tap points
 */
enum hfi_cwb_tap_points {
	HFI_TAP_POINT_NONE,
	HFI_TAP_POINT_LM,
	HFI_TAP_POINT_DSPP,
	HFI_TAP_POINT_DEMURA,
	HFI_TAP_POINT_MAX,
};

/*!
 * @enum hfi_fence_type
 * @brief Defines the types of fences used for synchronization.
 *
 * Fence types indicate specific synchronization points in the pipeline.
 *
 * @var HFI_FENCE_SCAN_START
 * Fence that signals that the frame is picked up by hardware.
 *
 * @var HFI_FENCE_SCAN_COMPLETE
 * Fence that signals the release of input buffers.
 */
enum hfi_fence_type {
	HFI_FENCE_SCAN_START,
	HFI_FENCE_SCAN_COMPLETE,
};

/*
 * DP Event data after HPD.
 *
 * @controller_id:
 *     Assigned controller id
 * @stream_id:
 *     Assigned stream/display id
 * @link_rate:
 *     Link rate from DPCD
 * @lane_count:
 *     Number of lanes from DPCD
 * @bits_per_pixel:
 *     Uncompressed bits per pixel supported for this
 * @fec_enabled:
 *     Forward Error Correction enabled flag
 * @edid_buf:
 *     EDID buffer: This buffer is populated by DCP with the raw EDID data.
 *     For this buffer to be populated, the Host must provide it through the parameters of the HFI
 *     command: HFI_COMMAND_DEVICE_HOT_PLUG_DETECT.
 * @modes_buf:
 *     Modes buffer: This buffer is populated by DCP with display modes parsed from the EDID.
 *     For this buffer to be populated, the Host must provide it through the parameters of the HFI
 *     command: HFI_COMMAND_DEVICE_HOT_PLUG_DETECT.
 */
struct hfi_display_event_edid_info {
	u32 controller_id;
	u32 stream_id;
	u32 link_rate;
	u32 lane_count;
	u32 bits_per_pixel;
	u32 fec_enabled;
	struct hfi_buff edid_buf;
	struct hfi_buff modes_buf;
};

/*
 * DP TU params sent during set mode.
 *
 * @dp_tu:
 *     DP tu size value
 * @valid_boundary:
 *     DP valid boundary limit
 * @valid_boundary2:
 *     DP valid boundary 2 limit
 */
struct hfi_display_dp_tu {
	u32 dp_tu;
	u32 valid_boundary;
	u32 valid_boundary2;
};

/*!
 * @struct hfi_audio_config
 * @brief HFI external display audio configuration structure.
 *
 * @var sample_rate
 *   Audio sample rate in Hz (e.g., 48000, 96000, 192000).
 * @var num_of_channels
 *   Number of audio channels (2, 6, 8).
 * @var channel_allocation
 *   Channel allocation as per CEA-861 standard.
 * @var level_shift
 *   Level shift value for dynamic range control.
 * @var down_mix
 *   Down mix inhibit flag.
 * @var sample_present
 *   Sample present flag indicating audio sample availability.
 * @var stream_id
 *   Audio stream identifier for multi-stream scenarios.
 */
struct hfi_audio_config {
	u32 sample_rate;
	u32 num_of_channels;
	u32 channel_allocation;
	u32 level_shift;
	u32 down_mix;
	u32 sample_present;
	u32 stream_id;
};

/*!
 * @struct hfi_hdr_metadata
 * @brief HDR metadata structure for HFI communication
 *
 * @var hdr_state
 *   Current HDR state
 * @var eotf
 *   Electro-optical transfer function
 * @var hdr_supported
 *   HDR support indicator
 * @var display_primaries_x
 *   Display primaries x coordinates array
 * @var display_primaries_y
 *   Display primaries y coordinates array
 * @var white_point_x
 *   White point x coordinate
 * @var white_point_y
 *   White point y coordinate
 * @var max_luminance
 *   Maximum luminance value
 * @var min_luminance
 *   Minimum luminance value
 * @var max_content_light_level
 *   Maximum content light level
 * @var max_average_light_level
 *   Maximum average light level
 */
struct hfi_hdr_metadata {
	/* Static HDR */
	u32 hdr_state;
	u32 eotf;
	u32 hdr_supported;
	u32 display_primaries_x[HFI_MAX_COLOR_COMPONENTS];
	u32 display_primaries_y[HFI_MAX_COLOR_COMPONENTS];
	u32 white_point_x;
	u32 white_point_y;
	u32 max_luminance;
	u32 min_luminance;
	u32 max_content_light_level;
	u32 max_average_light_level;
};

/*!
 * @struct hfi_display_hdr_cfg
 * @brief HDR configuration payload for HFI_COMMAND_DISPLAY_CONFIG_HDR
 *
 * @var dhdr_update
 *   Dynamic HDR update flag
 * @var hdr_meta
 *   HDR metadata structure
 */
struct hfi_display_hdr_cfg {
	u32 dhdr_update;
	struct hfi_hdr_metadata hdr_meta;
};

/*!
 * @enum hfi_colorimetry
 * @brief Colorimetry standards for display content
 *
 * @var HFI_COLORIMETRY_DEFAULT
 *   No colorimetry specified (value: 0)
 * @var HFI_COLORIMETRY_SMPTE_170M_YCC
 *   SMPTE 170M YCC colorimetry (value: 1)
 * @var HFI_COLORIMETRY_BT709_YCC
 *   BT.709 YCC colorimetry (value: 2)
 * @var HFI_COLORIMETRY_XVYCC_601
 *   xvYCC 601 colorimetry (value: 3)
 * @var HFI_COLORIMETRY_XVYCC_709
 *   xvYCC 709 colorimetry (value: 4)
 * @var HFI_COLORIMETRY_SYCC_601
 *  sYCC 601 colorimetry (value: 5)
 * @var HFI_COLORIMETRY_OPYCC_601
 *  opYCC 601 colorimetry (value: 6)
 * @var HFI_COLORIMETRY_OPRGB
 * opRGB colorimetry (value: 7)
 * @var HFI_COLORIMETRY_BT2020_CYCC
 *  BT.2020 CYCC colorimetry (value: 8)
 * @var HFI_COLORIMETRY_BT2020_RGB
 * BT.2020 RGB colorimetry (value: 9)
 * @var HFI_COLORIMETRY_BT2020_YCC
 * BT.2020 YCC colorimetry (value: 10)
 * @var HFI_COLORIMETRY_DCI_P3_RGB_D65
 * DCI-P3 RGB D65 colorimetry (value: 11)
 * @var HFI_COLORIMETRY_DCI_P3_RGB_THEATER
 * DCI-P3 RGB Theater colorimetry (value: 12)
 */
enum hfi_colorimetry {
	HFI_COLORIMETRY_DEFAULT            = 0,
	HFI_COLORIMETRY_SMPTE_170M_YCC     = 1,
	HFI_COLORIMETRY_BT709_YCC          = 2,
	HFI_COLORIMETRY_XVYCC_601          = 3,
	HFI_COLORIMETRY_XVYCC_709          = 4,
	HFI_COLORIMETRY_SYCC_601           = 5,
	HFI_COLORIMETRY_OPYCC_601          = 6,
	HFI_COLORIMETRY_OPRGB              = 7,
	HFI_COLORIMETRY_BT2020_CYCC        = 8,
	HFI_COLORIMETRY_BT2020_RGB         = 9,
	HFI_COLORIMETRY_BT2020_YCC         = 10,
	HFI_COLORIMETRY_DCI_P3_RGB_D65     = 11,
	HFI_COLORIMETRY_DCI_P3_RGB_THEATER = 12,
};

/*!
 * @enum hfi_misr_block
 * @brief Module to setup MISR (Multiple Input Signature Register).
 *
 * @var HFI_MISR_DSI
 *   DSI module.
 * @var HFI_MISR_MIXER
 *   Mixer module.
 * @var HFI_MISR_INTF
 *   Interface module.
 */
enum hfi_misr_block {
	HFI_MISR_DSI   = 0x0,
	HFI_MISR_MIXER = 0x1,
	HFI_MISR_INTF  = 0x2,
};

/*!
 * @struct hfi_misr_config
 * @brief MISR setup config information
 *
 * @var enable
 *   Enable MISR
 * @var frame_count
 *   Number of frames to run before capturing
 * @var block
 *   module to obtain MISR value from
 */
struct hfi_misr_config {
	u32 enable;
	u32 frame_count;
	enum hfi_misr_block block;
};

#endif // __H_HFI_DEFS_DISPLAY_H__
