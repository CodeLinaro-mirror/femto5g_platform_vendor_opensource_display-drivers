/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DP_MGR_HFI_H_
#define _DP_MGR_HFI_H_

#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include "msm_drv.h"
#include "dp_drv.h"
#include "dp_client.h"
#include "hfi_adapter.h"
#include "hfi_props.h"
#include "hfi_utils.h"
#include "dp_hfi.h"
#include "dp_hpd.h"
#include "dp_aux_switch.h"
#include "sde_edid_parser.h"
#include "hfi_defs_display.h"

/**
 * dp_mgr_hfi_init() - initialize DP HFI display
 * @pdev: platform device pointer
 * @debug: pointer to debug client
 *
 * Return: pointer to dp_client structure on success, ERR_PTR on failure
 */
struct dp_client *dp_mgr_hfi_init(struct platform_device *pdev,
				struct dp_debug_client *debug);

/**
 * dp_mgr_hfi_deinit() - deinitialize DP HFI display
 * @pdev: platform device pointer
 *
 * Return: 0 on success, negative error code on failure
 */
int dp_mgr_hfi_deinit(struct platform_device *pdev);

/**
 * dp_mgr_hfi_init_shared_addr() - allocate shared memory buffer
 * @ctx: HFI client context
 * @size: buffer size to allocate
 *
 * Return: pointer to hfi_shared_addr_map on success, NULL on failure
 */
struct hfi_shared_addr_map *dp_mgr_hfi_init_shared_addr(struct hfi_client_t *ctx, u32 size);

/**
 * dp_mgr_init_deinit_shared_addr() - free shared memory buffer
 * @ctx: HFI client context
 * @map: shared address map to free
 */
void dp_mgr_init_deinit_shared_addr(struct hfi_client_t *ctx, struct hfi_shared_addr_map *map);

/**
 * dp_mgr_hfi_init_hfi_buff() - initialize HFI buffer structure
 * @buff: HFI buffer structure to initialize
 * @map: shared address map containing buffer addresses
 */
void dp_mgr_hfi_init_hfi_buff(struct hfi_buff *buff, struct hfi_shared_addr_map *map);

/**
 * dp_mgr_hfi_hpd_configure_cb() - HPD configure callback
 * @data: pointer to dp_mgr_hfi_priv structure
 *
 * Return: 0 on success, negative error code on failure
 */
int dp_mgr_hfi_hpd_configure_cb(void *data);

/**
 * dp_mgr_hfi_hpd_disconnect_cb() - HPD disconnect callback
 * @data: pointer to dp_mgr_hfi_priv structure
 *
 * Return: 0 on success, negative error code on failure
 */
int dp_mgr_hfi_hpd_disconnect_cb(void *data);

/**
 * dp_mgr_hfi_set_mode() - set display mode via HFI
 * @client: pointer to dp_client structure
 * @panel_id: panel identifier
 * @mode: pointer to dp_display_mode structure
 *
 * Return: 0 on success, negative error code on failure
 */
int dp_mgr_hfi_set_mode(struct dp_client *client, int panel_id, struct dp_display_mode *mode);

/* Structure definitions that were in dp_mgr_hfi.c */
struct dpcd_info {
	u32 lane_count;
	u32 link_rate_khz;
	u32 pclk_factor;
	bool fec_en;
};

/* Forward declaration of dp_mgr_hfi_priv for cross-component access */
struct dp_mgr_hfi_priv {
	char *name;
	struct platform_device *pdev;
	struct dp_client client;
	struct msm_drm_private *priv;
	struct dp_hfi *hfi[DP_STREAMS_MAX];
	struct dp_intf_info intf_info;
	struct dp_hpd *hpd;
	struct dp_hpd_cb hpd_cb;

	struct dp_aux_switch *aux_switch;
	struct dp_display_mode default_mode;

	struct completion hpd_comp;

	u32 mode_count;
	u32 link_rate;
	u32 lane_count;
	u32 tgt_bpp;
	u32 fec_en;
	u32 mst_en;
	u32 max_streams;

	struct clk *usb3_tcsr_clk;
	struct clk *usb3_pipe_clk;

	struct dpcd_info dpcd;
	struct dp_debug_client *debug;
	struct device *pd_dp_phy_gdsc;

	bool connected;
	bool configured;
	bool soft_unplug;

	struct dp_audio *audio;
	u8 min_enc_level;

	u32 active_streams;

	/* TUI state */
	bool tui_active;
};

int dp_mgr_hfi_send_audio_config(struct dp_client *client,
		struct hfi_audio_config *audio_config);

int dp_mgr_hfi_send_audio_control(struct dp_client *client, u32 enable);

#endif /* _DP_MGR_HFI_H_ */
