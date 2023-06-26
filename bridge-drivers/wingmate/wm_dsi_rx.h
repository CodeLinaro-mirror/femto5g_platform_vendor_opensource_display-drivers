/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WM_DSI_H
#define WM_DSI_H

#include "wm_display.h"
#include <linux/io.h>
#include <linux/math.h>
#include <linux/printk.h>
#include <drm/drm_print.h>

/* define Register read and writes*/

/*define register offsets*/

#define DSI_RX_N_LANES			0x08
#define DSI_RX_SOFT_RSTN		0x04
#define DSI_RX_INT_ST_MAIN		0x0C

/*PHY*/
#define DSI_RX_PHY_SHUTDOWN		0x100
#define DSI_RX_PHY_RST			0x104
#define DSI_RX_PHY_CLK_STATUS		0x120
#define DSI_RX_PHY_DATA_STATUS		0x124
#define DSI_RX_PHY_TST_CTRL0		0x128
#define DSI_RX_PHY_TST_CTRL1		0x13C

/*IPI*/
#define DSI_RX_IPI_PG_ACTIVE		0x4C4
#define DSI_RX_IPI_PG_EN		0x4C0
#define DSI_RX_IPI_MODE_CFG		0x400
#define DSI_RX_IPI_VALID_VC_CFG 	0x408
#define DSI_RX_IPI_PG_CFG		0x0
#define DSI_RX_IPI_PG_PIXEL_NUM 	0x0
#define DSI_RX_IPI_PG_HSA_TIME		0x0
#define DSI_RX_IPI_PG_HBP_TIME		0x0
#define DSI_RX_IPI_PG_HLINE_TIME	0x0
#define DSI_RX_IPI_PG_VSA_LINES		0x0
#define DSI_RX_IPI_PG_VBP_LINES		0x0
#define DSI_RX_IPI_PG_VFP_LINES		0x0
#define DSI_RX_IPI_PG_VACTIVE_LINES 	0x0

/*DSC*/
#define DSI_RX_DSC_CTRL			0x0/*TODO*/

/*INT*/
#define DSI_RX_INT_ST_PHY_FATAL		0x200
#define DSI_RX_INT_ST_PHY		0x210
#define DSI_RX_INT_ST_DSI_FATAL		0x220
#define DSI_RX_INT_ST_DSI		0x230
#define DSI_RX_INT_ST_IPI_FATAL		0x264
#define DSI_RX_INT_ST_IPI		0x270
#define DSI_RX_INT_ST_FIFO_FATAL 	0x280

/*VTG*/
/*TODO*/
#define VTG_MIPI_CLKCFGFREQRANGE 	0x0
#define IPI_TX_DELAY		 	0x0
#define VTG_MIPI_HSFREQRANGE	 	0x30B4

#define WM_DEBUG(fmt, ...)	DRM_DEV_DEBUG(NULL, "[msm-dsi-debug]: "fmt, \
					##__VA_ARGS__)
#define WM_INFO(fmt, ...)	DRM_DEV_INFO(NULL, "[wm-info]: "fmt, \
					##__VA_ARGS__)
#define WM_ERR(fmt, ...)	DRM_DEV_ERROR(NULL, "[wm-error]: " fmt, \
					##__VA_ARGS__)
#define NUM_LANES	0x3 /*4 lanes = b'11*/

enum wm_dsi_rx_ops {
	DSI_RX_OP_TPG = 0,
	DSI_RX_OP_PHY_READY,
};

enum video_traffic_mode {
	DSI_VIDEO_TRAFFIC_SYNC_PULSES = 0,
	DSI_VIDEO_TRAFFIC_SYNC_START_EVENTS,
	DSI_VIDEO_TRAFFIC_BURST_MODE,
};

struct des_en_config_table {
	int min_Mhz;
	int max_Mhz;
	int des_en;
};

struct op_freq_table {
	int min_rate;
	int max_rate;
	int hsfreqrange;
       	u32 osc_freq_target;
};

struct dsi_ctrl_cfg {
	u8 num_date_lanes;
	u8 bpp;
	u32 ppc;
	u64 bit_clk_rate_hz;
	u32 vc_id;
	enum video_traffic_mode traffic_mode;
	bool is_dsc_enabled;
	bool is_tpg;
	u32 Fcfg_clk;
	u32 ppi_clk;
	u32 ipi_clk;
};

struct wm_dsi_rx {
	struct dsi_ctrl_cfg *ctrl_cfg;
	struct dsi_rx_mode_info *mode;
	struct wm_display *display;
	int (*enable)(struct wm_dsi_rx *dsi_rx);
	int (*pre_enable)(struct wm_dsi_rx *dsi_rx);
	int (*pre_disable)(struct wm_dsi_rx *dsi_rx);
	int (*disable)(struct wm_dsi_rx *dsi_rx);
	int (*set_mode)(struct wm_dsi_rx *dsi_rx);
	int (*deinit) (struct wm_display_info *display_info);
	int (*irq_handler)(struct wm_dsi_rx *dsi_rx, int irq);
};

struct wm_dsi_rx *wm_dsi_rx_init(struct wm_display_info *display_info);

#endif
