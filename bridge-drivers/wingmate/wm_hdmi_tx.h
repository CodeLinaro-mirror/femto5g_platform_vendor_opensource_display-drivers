/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_HDMI_H
#define WM_HDMI_H

#include "wm_display.h"

#define WM_HDMI_REG_MASK	0xff
/* HDMI_TX Register Offsets */
#define WM_HDMI_IH_PHY_STAT0	0x104
#define WM_HDMI_IH_I2CM_STAT0	0x105
#define WM_HDMI_IH_I2CMPHY_STAT0	0x108
#define WM_HDMI_IH_DECODE 0x170
#define WM_HDMI_IH_MUTE_PHY_STAT0	0x184
#define WM_HDMI_IH_MUTE_I2CM_STAT0	0x185

#define WM_HDMI_FC_INVIDCONF	0x1000
#define WM_HDMI_FC_INHACTIV0	0x1001
#define WM_HDMI_FC_INHACTIV1	0x1002
#define WM_HDMI_FC_INHBLANK0	0x1003
#define WM_HDMI_FC_INHBLANK1	0x1004
#define WM_HDMI_FC_INVACTIV0	0x1005
#define WM_HDMI_FC_INVACTIV1	0x1006
#define WM_HDMI_FC_INVBLANK0	0x1007
#define WM_HDMI_FC_HSYNCINDELAY0	0x1008
#define WM_HDMI_FC_HSYNCINDELAY1	0x1009
#define WM_HDMI_FC_HSYNCINWIDTH0	0x100a
#define WM_HDMI_FC_HSYNCINWIDTH1	0x100b
#define WM_HDMI_FC_VSYNCINDELAY0	0x100c
#define WM_HDMI_FC_VSYNCINWIDTH	0x100d
#define WM_HDMI_FC_CTRLDUR	0x1011
#define WM_HDMI_FC_EXCTRLDUR	0x1012
#define WM_HDMI_FC_EXCTRLSPAC	0x1013
#define WM_HDMI_FC_INVBLANK1	0x102e

#define WM_HDMI_PHY_CONF0 	0x3000
#define WM_HDMI_PHY_STAT0 	0x3004
#define WM_HDMI_PHY_MASK0 	0x3006
#define WM_HDMI_PHY_POL0 	0x3007
#define WM_HDMI_PHY_I2CM_ADDR	0x3021
#define WM_HDMI_PHY_I2CM_DATAO_1	0x3022
#define WM_HDMI_PHY_I2CM_DATAO_0	0x3023
#define WM_HDMI_PHY_I2CM_OPERATION	0x3026

#define WM_HDMI_MC_CLKDIS 0x4001
#define WM_HDMI_MC_SWRSTZREQ_1 0x4002
#define WM_HDMI_MC_PHYRSTZ 0x4005

#define WM_HDMI_I2CM_SLAVE 	0x7e00
#define WM_HDMI_I2CM_ADDRESS 	0x7e01
#define WM_HDMI_I2CM_DATAI 	0x7e03
#define WM_HDMI_I2CM_OPERATION 	0x7e04
#define WM_HDMI_I2CM_SEGADDR 	0x7e08
#define WM_HDMI_I2CM_SEGPTR 	0x7e0a
#define WM_HDMI_I2CM_READ_BUFFx	0x7e20

/* HDMI PHY Register Address */
#define WM_HDMI_PHY_OPMODE_PLLCFG	0x06
#define WM_HDMI_PHY_CKSYMTXCTRL	0x09
#define WM_HDMI_PHY_VLEVCTRL	0x0e
#define WM_HDMI_PHY_PLLCURRCTRL	0x10
#define WM_HDMI_PHY_PLLGMPCTRL	0x15
#define WM_HDMI_PHY_TXTERM	0x19

enum wm_hdmi_phy_state {
	PHY_I2C_NONE,
	PHY_I2C_PLL,
	PHY_I2C_TXTM,
	PHY_I2C_VLEC,
	PHY_I2C_CKSYM,
	PHY_I2C_PWR,
};

enum wm_hdmi_phy_pll {
	PLLCFG = 1,
	PLLCURR = 2,
	PLLGMP = 3,
};

enum hdmi_tx_output_format
{
	HDMI_OUTPUT_FORMAT_RGB,
	HDMI_OUTPUT_FORMAT_YCBCR444,
	HDMI_OUTPUT_FORMAT_YCBCR422,
	HDMI_OUTPUT_FORMAT_YCBCR420,
	HDMI_OUTPUT_FORMAT_INVALID
};

enum wm_hdmi_tx_state
{
	HDMI_CONNECTED = BIT(0),
	HDMI_DISCONNECTED = BIT(7),
};

struct wm_hdmi_intrs {
	u8 edid_intr;
	u8 phy_intr;
	u8 phy_i2c_intr;
};

struct edid_ctrl {
	u8 *edid;
	u8 retry;
	u8 colorimetry;
	u32 count, total_ext, curr_ext;
};

struct wm_hdmi_tx {
	struct wm_display *display;
	struct wm_dt_props *dt_props;
	struct device *dev;

	/* Sink details */
	bool hpd_status;
	struct wm_hdmi_intrs intr;
	struct edid_ctrl edid_ctrl;

	// struct wm_hdmi_tx_info hdmi_info;
	u8 phy_retry;
	bool pll_lock;
	enum wm_hdmi_phy_pll phy_curr_state;
	enum wm_hdmi_phy_pll phy_next_state;
	enum hdmi_tx_output_format output_format;

	struct mutex lock;
	enum wm_hdmi_phy_state phy_i2c_state;
	enum wm_hdmi_phy_state phy_i2c_next;
	wait_queue_head_t phy_enable_wq;

	struct workqueue_struct *workq;
	struct work_struct hpd_work;
	struct work_struct edid_work;
	struct work_struct phy_work;

	int (*pre_enable)(struct wm_hdmi_tx *hdmi_tx);
	int (*enable)(struct wm_hdmi_tx *hdmi_tx);
	int (*disable)(struct wm_hdmi_tx *hdmi_tx);
	int (*post_disable)(struct wm_hdmi_tx *hdmi_tx);
	int (*get_modes)(struct wm_hdmi_tx *hdmi_tx,
			struct drm_connector *connector);
	enum drm_mode_status (*mode_valid)(struct wm_hdmi_tx *hdmi_tx,
			const struct drm_display_mode *drm_mode);
	int (*irq_handler)(struct wm_hdmi_tx *hdmi_tx, int irq);
};

struct wm_hdmi_tx *wm_hdmi_tx_init(struct wm_display_info *display_info);

void wm_hdmi_tx_deinit(struct wm_hdmi_tx *hdmi_tx);
#endif
