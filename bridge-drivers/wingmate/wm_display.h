/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_DISPLAY_H
#define WM_DISPLAY_H

#include <linux/of_platform.h>
#include <drm/drm_bridge.h>
#include <linux/regmap.h>

typedef enum wm_display_interrupts {
	WM_DISPLAY_INT_MIN = 31,
	WM_DISPLAY_INT_WAS_OFL_INT = WM_DISPLAY_INT_MIN,
	WM_DISPLAY_INT_WAS_UFL_INT,
	WM_DISPLAY_INT_I2S_INTERRUPT,
	WM_DISPLAY_INT_SSI_TXE_INTR,
	WM_DISPLAY_INT_SSI_RXF_INTR,
	WM_DISPLAY_INT_SSI_RXO_INTR,
	WM_DISPLAY_INT_SSI_TXU_INTR,
	WM_DISPLAY_INT_SSI_AHBE_INTR,
	WM_DISPLAY_INT_SSI_SPIME_INTR,
	WM_DISPLAY_INT_I2C_TX_OVER,
	WM_DISPLAY_INT_I2C_TX_EMPTY,
	WM_DISPLAY_INT_I2C_TX_ABRT,
	WM_DISPLAY_INT_I2C_RX_UNDER,
	WM_DISPLAY_INT_I2C_RX_OVER,
	WM_DISPLAY_INT_I2C_RX_FULL_INTR,
	WM_DISPLAY_INT_I2C_RX_DONE,
	WM_DISPLAY_INT_I2C_RD_REQ_INTR,
	WM_DISPLAY_INT_I2C_ACTIVITY,
	WM_DISPLAY_INT_I2C_RESTART_DET,
	WM_DISPLAY_INT_I2C_STOP_DET,
	WM_DISPLAY_INT_I2C_START_DET,
	WM_DISPLAY_INT_I2C_GEN_CALL,
	WM_DISPLAY_INT_OTP_INT,
	WM_DISPLAY_INT_TRNG_INT,
	WM_DISPLAY_INT_HDCP_APB_INT,
	WM_DISPLAY_INT_HDMI_CEC_WAKEUP_INT,
	WM_DISPLAY_INT_HDMI_INT,
	WM_DISPLAY_INT_MIPIDSI2_APB_INT,
	WM_DISPLAY_INT_VTG_OBS_FIFO_LATENCY_GT_BOUND,
	WM_DISPLAY_INT_VTG_OBS_FIFO_LATENCY_LT_BOUND,
	WM_DISPLAY_INT_VTG_FRAME_DELTA_LPF8_GT_BOUND,
	WM_DISPLAY_INT_VTG_FRAME_DELTA_LPF8_LT_BOUND,
	WM_DISPLAY_INT_VTG_FRAME_DELTA_LPF4_GT_BOUND,
	WM_DISPLAY_INT_VTG_FRAME_DELTA_LPF4_LT_BOUND,
	WM_DISPLAY_INT_VTG_FRAME_DELTA_LPF2_GT_BOUND,
	WM_DISPLAY_INT_VTG_FRAME_DELTA_LPF2_LT_BOUND,
	WM_DISPLAY_INT_VTG_FRAME_DELTA_GT_BOUND,
	WM_DISPLAY_INT_VTG_FRAME_DELTA_LT_BOUND,
	WM_DISPLAY_INT_VTG_FRAME_BUFFER_ALMOST_FULL,
	WM_DISPLAY_INT_VTG_FRAME_BUFFER_ALMOST_EMPTY,
	WM_DISPLAY_INT_VTG_FRAME_BUFFER_FULL,
	WM_DISPLAY_INT_VTG_FRAME_BUFFER_EMPTY,
	WM_DISPLAY_INT_HDR_FIFO_INT,
	WM_DISPLAY_INT_HDR_FIFO_EMPTY_INT,
	WM_DISPLAY_INT_HDR_FIFO_FULL_INT,
	WM_DISPLAY_INT_VTG_GENERATOR_ERROR_INT,
	WM_DISPLAY_INT_VTG_HDMI_VSYNC_INT,
	WM_DISPLAY_INT_VTG_MIPI_VSYNC_INT,
	WM_DISPLAY_INT_DSC_ERR,
	WM_DISPLAY_INT_MAX,
} wm_display_interrupts_t;

typedef enum wm_display_params {
	WM_DISPLAY_PARAM_SET_AUD_INFOFRAME,
	WM_DISPLAY_PARAM_MAX
} wm_display_params_t;

struct wm_dt_props {
	struct device_node *ext_disp_np;
	unsigned int audio_supported;
	unsigned int cec_supported;
};

typedef enum wm_display_reset_reason {
	WM_DISPLAY_RESET_REASON_VTG_ERROR,
	WM_DISPLAY_RESET_REASON_MAX
} wm_display_reset_reason_t;

struct wm_display {
	struct drm_connector *connector;
	struct drm_display_mode drm_mode;

	bool hpd_status;

	int (*get_modes)(struct wm_display *display,
			struct drm_connector *connector);
	enum drm_mode_status (*mode_valid)(struct wm_display *display,
			const struct drm_display_mode *drm_mode);
	void (*set_mode)(struct wm_display *display,
			const struct drm_display_mode *drm_mode);
	void (*pre_enable)(struct wm_display *display);
	void (*enable)(struct wm_display *display);
	void (*disable)(struct wm_display *display);
	void (*post_disable)(struct wm_display *display);
	void (*handle_params)(struct wm_display *display,
			wm_display_params_t id, void *in_param, void *out_param);
	void (*enable_irq)(struct wm_display *display,
			wm_display_interrupts_t irq, bool enable);
	int (*write_register) (struct wm_display *display,
			unsigned int reg, unsigned int val);
	int (*update_register_bits) (struct wm_display *display,
			unsigned int reg, unsigned int mask, unsigned int val);
	int (*read_register) (struct wm_display *display, unsigned int reg);
	int (*reset_video_path) (struct wm_display *display,
			wm_display_reset_reason_t reason);
	int (*set_video_clk_rate) (struct wm_display *display, int rate);
};

struct wm_display_info {
	struct wm_display *display;
	struct device *dev;
	struct wm_dt_props *dt_props;
};

#endif
