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
 * @DSI_PANEL_I2C_CMD_SET_ON:                Panel power-on commands (from DT)
 * @DSI_PANEL_I2C_CMD_SET_OFF:               Panel power-off commands (from DT)
 * @DSI_PANEL_I2C_CMD_SET_BRIGHTNESS:        Backlight level commands (built in driver)
 * @DSI_PANEL_I2C_CMD_SET_CALIBRATION_LEFT:  Left-eye gamma/demura/offset/flip (built in driver)
 * @DSI_PANEL_I2C_CMD_SET_CALIBRATION_RIGHT: Right-eye gamma/demura/offset/flip (built in driver)
 * @DSI_PANEL_I2C_CMD_SET_DEMURA_ON:         Enable demura correction (built in driver)
 * @DSI_PANEL_I2C_CMD_SET_GAMMA_ON:          Enable gamma correction (built in driver)
 * @DSI_PANEL_I2C_CMD_SET_MAX:               Sentinel / array size
 */
enum dsi_panel_i2c_cmd_set_type {
	DSI_PANEL_I2C_CMD_SET_ON = 0,
	DSI_PANEL_I2C_CMD_SET_OFF,
	DSI_PANEL_I2C_CMD_SET_BRIGHTNESS,
	DSI_PANEL_I2C_CMD_SET_CALIBRATION_LEFT,
	DSI_PANEL_I2C_CMD_SET_CALIBRATION_RIGHT,
	DSI_PANEL_I2C_CMD_SET_DEMURA_ON,
	DSI_PANEL_I2C_CMD_SET_GAMMA_ON,
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
	/*
	 * Slave addresses on the left/right I2C adapters.
	 * Populated from qcom,panel-i2c-left-slave-addr /
	 * qcom,panel-i2c-right-slave-addr; used by the ISL97900 brightness
	 * path.  Zero means no slave address configured for that side.
	 */
	u8 left_slave_addr;
	u8 right_slave_addr;
	struct completion calibration_done;
	struct dsi_panel_i2c_cmd_set cmd_sets[DSI_PANEL_I2C_CMD_SET_MAX];
};

#if IS_ENABLED(CONFIG_DSI_PANEL_I2C)
int dsi_panel_i2c_parse_config(struct dsi_panel *panel);
int dsi_panel_i2c_tx_cmd_set(struct dsi_panel *panel,
			      enum dsi_panel_i2c_cmd_set_type type,
			      struct i2c_adapter *adapter);
int dsi_panel_i2c_enable(struct dsi_panel *panel);
int dsi_panel_i2c_disable(struct dsi_panel *panel);
int dsi_panel_i2c_calibrate(struct dsi_panel *panel);
int dsi_panel_i2c_update_backlight(struct dsi_panel *panel, u32 bl_lvl);
#else
static inline int dsi_panel_i2c_parse_config(struct dsi_panel *panel) { return 0; }
static inline int dsi_panel_i2c_tx_cmd_set(struct dsi_panel *panel,
					    enum dsi_panel_i2c_cmd_set_type type,
					    struct i2c_adapter *adapter) { return 0; }
static inline int dsi_panel_i2c_enable(struct dsi_panel *panel) { return 0; }
static inline int dsi_panel_i2c_disable(struct dsi_panel *panel) { return 0; }
static inline int dsi_panel_i2c_calibrate(struct dsi_panel *panel) { return 0; }
static inline int dsi_panel_i2c_update_backlight(struct dsi_panel *panel, u32 bl_lvl) { return 0; }
#endif /* CONFIG_DSI_PANEL_I2C */

#endif /* _DSI_I2C_PANEL_H_ */
