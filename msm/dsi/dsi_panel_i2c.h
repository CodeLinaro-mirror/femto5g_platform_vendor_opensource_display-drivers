/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DSI_I2C_PANEL_H_
#define _DSI_I2C_PANEL_H_

#include <linux/i2c.h>
#include <linux/types.h>

#define DSI_PANEL_I2C_MIN_CMD_SIZE 3 /* slave, delay, len */

/**
 * enum dsi_backlight_i2c_subtype - I2C backlight controller subtypes
 * @DSI_BACKLIGHT_I2C_UNKNOWN:   Unknown or unspecified I2C backlight controller
 * @DSI_BACKLIGHT_I2C_ISL97900: Intersil ISL97900 backlight controller
 * @DSI_BACKLIGHT_I2C_JBD4040:  JBD4040 backlight controller
 * @DSI_BACKLIGHT_I2C_MAX:      Sentinel / array size
 */
enum dsi_backlight_i2c_subtype {
	DSI_BACKLIGHT_I2C_UNKNOWN = 0,
	DSI_BACKLIGHT_I2C_ISL97900,
	DSI_BACKLIGHT_I2C_JBD4040,
	DSI_BACKLIGHT_I2C_MAX,
};

struct dsi_panel;

/**
 * enum dsi_panel_i2c_cmd_set_type - I2C command set types
 * @DSI_PANEL_I2C_CMD_SET_ON:         Commands sent when the panel is powered on
 * @DSI_PANEL_I2C_CMD_SET_OFF:        Commands sent when the panel is powered off
 * @DSI_PANEL_I2C_CMD_SET_BRIGHTNESS: Commands used to set backlight level (built in driver)
 * @DSI_PANEL_I2C_CMD_SET_MAX:        Sentinel / array size
 */
enum dsi_panel_i2c_cmd_set_type {
	DSI_PANEL_I2C_CMD_SET_ON = 0,
	DSI_PANEL_I2C_CMD_SET_OFF,
	DSI_PANEL_I2C_CMD_SET_BRIGHTNESS,
	DSI_PANEL_I2C_CMD_SET_MAX
};

struct dsi_panel_i2c_cmd {
	u8 *data;
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
	struct dsi_panel_i2c_cmd_set cmd_sets[DSI_PANEL_I2C_CMD_SET_MAX];
	/* ISL97900 RGB LED device-tree nodes (ISL97900 subtype only) */
	struct device_node *rgb_left_led_node;
	struct device_node *rgb_right_led_node;
};

#if IS_ENABLED(CONFIG_DSI_PANEL_I2C)
int dsi_panel_i2c_parse_config(struct dsi_panel *panel);
int dsi_panel_i2c_tx_cmd_set(struct dsi_panel *panel,
			      enum dsi_panel_i2c_cmd_set_type type);
int dsi_panel_i2c_update_backlight(struct dsi_panel *panel, u32 bl_lvl);
#else
static inline int dsi_panel_i2c_parse_config(struct dsi_panel *panel) { return 0; }
static inline int dsi_panel_i2c_tx_cmd_set(struct dsi_panel *panel,
				enum dsi_panel_i2c_cmd_set_type type) { return 0; }
static inline int dsi_panel_i2c_update_backlight(struct dsi_panel *panel, u32 bl_lvl) { return 0; }
#endif /* CONFIG_DSI_PANEL_I2C */

#endif /* _DSI_I2C_PANEL_H_ */
