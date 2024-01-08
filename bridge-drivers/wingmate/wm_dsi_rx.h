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

#define DSI_RX_BASE			0x480000
#define DSI_RX_N_LANES			0x08
#define DSI_RX_SOFT_RSTN		0x04
#define DSI_RX_INT_ST_MAIN		0x0C

/*PHY*/
#define DSI_RX_PHY_SHUTDOWN		0x100
#define DSI_RX_PHY_RST			0x104
#define DSI_RX_PHY_TST_CTRL0    	0x118
#define DSI_RX_PHY_TST_CTRL1    	0x11C
#define DSI_RX_PHY_CLK_STATUS		0x120
#define DSI_RX_PHY_DATA_STATUS		0x124

/*IPI*/
#define DSI_RX_IPI_PG_ACTIVE		0x4C4
#define DSI_RX_IPI_PG_EN		0x4C0
#define DSI_RX_IPI_MODE_CFG		0x400
#define DSI_RX_IPI_VALID_VC_CFG 	0x404
#define DSI_RX_IPI_TX_DELAY		0x408
#define DSI_RX_IPI_PG_CFG		0x4C8
#define DSI_RX_IPI_PG_PIXEL_NUM 	0x4D0
#define DSI_RX_IPI_PG_HSA_TIME		0x4D4
#define DSI_RX_IPI_PG_HBP_TIME		0x4D8
#define DSI_RX_IPI_PG_HLINE_TIME 	0x4DC
#define DSI_RX_IPI_PG_VSA_LINES		0x4E0
#define DSI_RX_IPI_PG_VBP_LINES		0x4E4
#define DSI_RX_IPI_PG_VFP_LINES		0x4E8
#define DSI_RX_IPI_PG_VACTIVE_LINES 	0x4EC

/*DSC*/
#define DSI_RX_DSC_CTRL0		0x20A0 /*0x4820A0*/
#define DSI_RX_DSC_CTRL1		0x20A4
#define DSI_RX_DSC_BLK0			0x20D0
#define DSI_RX_DSC_BLK1			0x20D4
#define DSI_RX_DSC_BLK2			0x20D8

/*PPS*/
#define DSI_RX_DSC_PPS0_3     		0x2000/*0x482000*/
#define DSI_RX_DSC_PPS4_7     		0x2004
#define DSI_RX_DSC_PPS8_11    		0x2008
#define DSI_RX_DSC_PPS12_15   		0x200C
#define DSI_RX_DSC_PPS16_19   		0x2010
#define DSI_RX_DSC_PPS20_23   		0x2014
#define DSI_RX_DSC_PPS24_27   		0x2018
#define DSI_RX_DSC_PPS28_31   		0x201C
#define DSI_RX_DSC_PPS32_35   		0x2020
#define DSI_RX_DSC_PPS36_39   		0x2024
#define DSI_RX_DSC_PPS40_43   		0x2028
#define DSI_RX_DSC_PPS44_47   		0x202C
#define DSI_RX_DSC_PPS48_51   		0x2030
#define DSI_RX_DSC_PPS52_55   		0x2034
#define DSI_RX_DSC_PPS56_59   		0x2038
#define DSI_RX_DSC_PPS60_63   		0x203C
#define DSI_RX_DSC_PPS64_67   		0x2040
#define DSI_RX_DSC_PPS68_71   		0x2044
#define DSI_RX_DSC_PPS72_75   		0x2048
#define DSI_RX_DSC_PPS76_79   		0x204C
#define DSI_RX_DSC_PPS80_83   		0x2050
#define DSI_RX_DSC_PPS84_87   		0x2054
#define DSI_RX_DSC_PPS88_91   		0x2058
#define DSI_RX_DSC_PPS92_95   		0x205C
#define DSI_RX_DSC_PPS96_99   		0x2060
#define DSI_RX_DSC_PPS100_103 		0x2064
#define DSI_RX_DSC_PPS104_107 		0x2068
#define DSI_RX_DSC_PPS108_111 		0x206C
#define DSI_RX_DSC_PPS112_115 		0x2070
#define DSI_RX_DSC_PPS116_119 		0x2074
#define DSI_RX_DSC_PPS120_123 		0x2078
#define DSI_RX_DSC_PPS124_127 		0x207C

/*INT*/
#define DSI_RX_INT_ST_PHY_FATAL		0x200
#define DSI_RX_INT_ST_PHY		0x210
#define DSI_RX_INT_ST_DSI_FATAL		0x220
#define DSI_RX_INT_ST_DSI		0x230
#define DSI_RX_INT_ST_IPI_FATAL		0x264
#define DSI_RX_INT_ST_IPI		0x270
#define DSI_RX_INT_ST_FIFO_FATAL 	0x280
#define DSI_RX_INT_ST_DESKEW_FATAL 	0x290

/*INT MASK*/
#define DSI_RX_MASK_IPI_FATAL		0x274
#define DSI_RX_MASK_FIFO_FATAL		0x284
#define DSI_RX_MASK_DESKEW_FATAL	0x294

/*VTG*/
#define VTG_MIPI_CLKCFGFREQRANGE 	0x30B0/*0x4830B0*/
#define VTG_MIPI_HSFREQRANGE	 	0x30B4

#define WM_DEBUG(fmt, ...)      DRM_DEV_DEBUG(NULL, "[wm-debug]: "fmt, \
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

struct dsi_rx_phy {
	u32 hsfreqrange;
	u32 cfgclkfreqrange;
	u32 osc_freq_target;
};

enum wm_dsc_pps_type {
	DSC_PPS_4k60,
	DSC_PPS_4k50,
	DSC_PPS_TYPE_MAX
};

struct wm_dsc {
	struct pps_info *pps_info;
	char pps[128];
	u32 pps_len;
	u32 pps_word[32];
	u32 pps_word_len;
	struct drm_dsc_config *dsc_config;
	bool enable;
};

struct wm_dsi_rx {
	struct dsi_ctrl_cfg *ctrl_cfg;
	struct dsi_rx_mode_info *mode;
	struct wm_display *display;
	struct wm_dsc *dsc;
	struct workqueue_struct *workq;
	struct work_struct err_work;
	int (*enable)(struct wm_dsi_rx *dsi_rx);
	int (*pre_enable)(struct wm_dsi_rx *dsi_rx);
	int (*pre_disable)(struct wm_dsi_rx *dsi_rx);
	int (*disable)(struct wm_dsi_rx *dsi_rx);
	int (*set_mode)(struct wm_dsi_rx *dsi_rx);
	int (*deinit) (struct wm_dsi_rx *dsi_rx);
	int (*irq_handler)(struct wm_dsi_rx *dsi_rx, int irq);
};

struct wm_dsi_rx *wm_dsi_rx_init(struct wm_display_info *display_info);

#endif
