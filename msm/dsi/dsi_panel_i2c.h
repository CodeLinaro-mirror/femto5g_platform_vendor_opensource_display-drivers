/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DSI_I2C_PANEL_H_
#define _DSI_I2C_PANEL_H_

#include <linux/i2c.h>
#include <linux/types.h>

#define DSI_PANEL_I2C_MIN_CMD_SIZE 3 /* slave, delay, len */

struct dsi_panel;

struct dsi_panel_i2c_cmd {
	const u8 *data;
	u32 len;
	u32 post_wait_ms;
	u8 slave_addr;
};

struct dsi_panel_i2c_cmd_set {
	struct dsi_panel_i2c_cmd *cmds;
	u32 count;
};

struct dsi_panel_i2c_config {
	bool i2c_support;
	struct i2c_adapter *left_adapter;
	struct i2c_adapter *right_adapter;
	struct dsi_panel_i2c_cmd_set cmd_set;
};

#if IS_ENABLED(CONFIG_DSI_PANEL_I2C)
int dsi_panel_i2c_parse_config(struct dsi_panel *panel);
int dsi_panel_i2c_tx_cmd_set(struct dsi_panel *panel);
#else
static inline int dsi_panel_i2c_parse_config(struct dsi_panel *panel) { return 0; }
static inline int dsi_panel_i2c_tx_cmd_set(struct dsi_panel *panel) { return 0; }
#endif /* CONFIG_DSI_PANEL_I2C */

#endif /* _DSI_I2C_PANEL_H_ */
