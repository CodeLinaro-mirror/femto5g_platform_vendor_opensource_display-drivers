/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DEBUG_CLIENT_H_
#define _DP_DEBUG_CLIENT_H_

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ipc_logging.h>

#include "dp_aux.h"
#include "dp_panel.h"
#include "dp_debug.h"
#include "dp_ctrl.h"
#include "dp_link.h"
#include "dp_hpd.h"
#include "dp_parser.h"
#include "dp_catalog.h"
#include "dp_aux_bridge.h"
#include "dp_drv.h"

struct dp_debug_client {
	u32 sim_mode;
	bool sim_enable;
	bool psm_enabled;
	bool hdcp_disabled;
	bool hdcp_wait_sink_sync;
	bool force_encryption;
	bool skip_uevent;
	bool hotplug;
	bool force_multi_func;

	u32 max_pclk_khz;
	u32 disconnect_delay_ms;
	u32 tpg_pattern;
	u32 mst_edid_idx;
	u32 mst_con_id;
	bool mst_sim_add_con;
	bool mst_sim_remove_con;
	u32 mst_sim_remove_con_id;

	unsigned long connect_notification_delay_ms;

	char hdcp_status[SZ_128];

	struct dp_aux *aux;
	struct dp_panel *panel;
	struct dp_ctrl *ctrl;
	struct dp_link *link;
	struct dp_hpd *hpd;
	struct dp_parser *parser;
	struct dp_catalog *catalog;
	struct dp_aux_bridge *sim_bridge;
	struct drm_connector *connector;
	struct device *dev;

	void *tpg_config;
	void *pll;
	void *priv;

	int (*read_dpcd)(struct dp_debug_client *client, u8 *dpcd, u32 size, u32 offset);
	int (*read_crc)(struct dp_debug_client *client, char *buf, u32 size);
	int (*read_connected)(struct dp_debug_client *client, char *buf, u32 size);
	int (*read_info)(struct dp_debug_client *client, char *buf, u32 size);
	int (*read_bw_code)(struct dp_debug_client *client, char *buf, u32 size);
	int (*read_tpg)(struct dp_debug_client *client, char *buf, u32 size);
	int (*read_hdr)(struct dp_debug_client *client, char *buf, u32 size, int panel_id);
	int (*read_dump)(struct dp_debug_client *client, char *buf, u32 size, const char *reg_name);
	int (*read_mst_mode)(struct dp_debug_client *client, char *buf, u32 size);
	int (*read_max_pclk_khz)(struct dp_debug_client *client, char *buf, u32 size);
	int (*read_hdcp)(struct dp_debug_client *client, char *buf, u32 size);
	int (*read_edid_modes)(struct dp_debug_client *client, char *buf, u32 size);
	int (*read_edid_modes_mst)(struct dp_debug_client *client, char *buf, u32 size);
	int (*read_mst_conn_info)(struct dp_debug_client *client, char *buf, u32 size);

	int (*write_edid)(struct dp_debug_client *client, const char *buf, size_t count);
	int (*write_dpcd)(struct dp_debug_client *client, const char *buf, size_t count);
	int (*write_hpd)(struct dp_debug_client *client, const char *buf, size_t count);
	int (*write_edid_modes)(struct dp_debug_client *client, const char *buf, size_t count);
	int (*write_edid_modes_mst)(struct dp_debug_client *client, const char *buf);
	int (*write_mst_con_id)(struct dp_debug_client *client, int con_id, int status);
	int (*write_mst_con_add)(struct dp_debug_client *client, const char *buf, size_t count);
	int (*write_mst_con_remove)(struct dp_debug_client *client, int con_id);
	int (*write_bw_code)(struct dp_debug_client *client, const char *buf, size_t count);
	int (*write_mst_mode)(struct dp_debug_client *client, const char *buf, size_t count);
	int (*write_max_pclk_khz)(struct dp_debug_client *client, const char *buf, size_t count);
	int (*write_mst_sideband_mode)(struct dp_debug_client *client,
			int mst_sideband_mode, u32 mst_port_cnt);
	int (*write_tpg)(struct dp_debug_client *client, u32 tpg);
	int (*write_exe_mode)(struct dp_debug_client *client, const char *buf, size_t count);
	int (*write_hdcp)(struct dp_debug_client *client, const char *buf, size_t count);
	int (*write_sim)(struct dp_debug_client *client, const char *buf, size_t count);
	int (*write_attention)(struct dp_debug_client *client, const char *buf, size_t count);
	int (*write_dump)(struct dp_debug_client *client, const char *buf, size_t count);
	int (*write_mmrm_clk_cb)(struct dp_debug_client *client, const char *buf, size_t count);
	int (*write_sim_mode)(struct dp_debug_client *client, bool sim);
	int (*simulate_attention)(struct dp_debug_client *client, int vdo);

	void (*abort)(struct dp_debug_client *client);
};

/**
 * dp_debug_client_get() - get the debug client instance
 *
 * @client: client to be filled
 * return: error code
 *
 * This function returns a pointer to the debug client operations structure
 */
int dp_debug_client_get(struct dp_debug_client *client);

/**
 * dp_debug_client_put() - release the debug client instance
 * @client: pointer to dp_debug_client structure to be freed
 *
 * This function frees the memory allocated for the debug client
 */
void dp_debug_client_put(struct dp_debug_client *client);

#endif /* _DP_DEBUG_CLIENT_H_ */
