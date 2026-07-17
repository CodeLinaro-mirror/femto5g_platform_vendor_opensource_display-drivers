// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/delay.h>
#include <linux/of.h>
#include <linux/slab.h>

#include "dsi_panel.h"

#if IS_ENABLED(CONFIG_ISL97900_LED)
#include <misc/isl97900_led.h>

#else
enum isl_function {
	ISL_LED_BRIGHTNESS_RGB_LEVEL,
	ISL_LED_BRIGHTNESS_RED_LEVEL,
	ISL_LED_BRIGHTNESS_GREEN_LEVEL,
	ISL_LED_BRIGHTNESS_BLUE_LEVEL,
	ISL_LED_BRIGHTNESS_EVENT_MAX,
};

static int isl97900_led_event(struct device_node *node, enum isl_function event, u32 level)
{
	return 0;
}
#endif

/*
 * JBD4040 luminance control command constants (built entirely in the driver):
 *   Slave address : 0x58
 *   Payload       : <reg_b0> <reg_b1> <reg_b2> <bl_msb> <bl_lsb>
 *   Register addr : 0x20 0x0A 0x14  (3 bytes)
 *   Brightness    : 16-bit big-endian value at bytes [3..4]
 */
#define JBD4040_BL_SLAVE_ADDR  0x58
#define JBD4040_BL_REG_B0      0x20
#define JBD4040_BL_REG_B1      0x0A
#define JBD4040_BL_REG_B2      0x14
#define JBD4040_BL_CMD_LEN     5

static int dsi_panel_i2c_tx_cmd(struct dsi_panel *panel, u8 slave_addr, const u8 *buf, u32 len)
{
	struct dsi_panel_i2c_config *cfg;
	struct i2c_msg msg;
	int rc = 0;

	if (!panel || !buf || !len || !slave_addr)
		return -EINVAL;

	cfg = &panel->i2c_config;
	msg.addr = slave_addr;
	msg.flags = 0;
	msg.len = len;
	msg.buf = (u8 *)buf;

	if (cfg->left_adapter) {
		rc = i2c_transfer(cfg->left_adapter, &msg, 1);
		if (rc != 1) {
			DSI_ERR("i2c transfer failed on left adapter: %d\n", rc);
			return -EIO;
		}
	}

	if (cfg->right_adapter) {
		rc = i2c_transfer(cfg->right_adapter, &msg, 1);
		if (rc != 1) {
			DSI_ERR("i2c transfer failed on right adapter: %d\n", rc);
			return -EIO;
		}
	}

	return 0;
}

static int dsi_panel_i2c_get_cmd_count(const u8 *data, u32 nbytes, u32 *cnt)
{
	u32 count = 0;

	if (!data || !nbytes || !cnt)
		return -EINVAL;

	while (nbytes >= DSI_PANEL_I2C_MIN_CMD_SIZE) {
		u32 packet_length = DSI_PANEL_I2C_MIN_CMD_SIZE + data[2];

		if (packet_length > nbytes) {
			DSI_ERR("malformed i2c cmds: there are %u bytes left\n", nbytes);
			return -EINVAL;
		}

		nbytes -= packet_length;
		data += packet_length;
		count++;
	}

	*cnt = count;
	return 0;
}

static int dsi_panel_i2c_create_cmd_set(const u8 *data, u32 nbytes,
					u32 count, struct dsi_panel_i2c_cmd *cmds)
{
	u32 pos = 0;
	u32 i;
	int rc = 0;

	if (!data || !nbytes || !cmds)
		return -EINVAL;

	for (i = 0; i < count; i++) {
		u8 slave, delay, plen;
		u8 *cmds_data;

		if ((nbytes - pos) < DSI_PANEL_I2C_MIN_CMD_SIZE) {
			DSI_ERR("malformed i2c cmds: short header at %u\n", pos);
			rc = -EINVAL;
			goto error;
		}

		slave = data[pos++];
		delay = data[pos++];
		plen = data[pos++];

		if ((nbytes - pos) < plen) {
			DSI_ERR("malformed i2c cmd payload overruns at %u (len=%u)\n",
				pos, plen);
			rc = -EINVAL;
			goto error;
		}

		cmds_data = kmemdup(&data[pos], plen, GFP_KERNEL);
		if (!cmds_data) {
			rc = -ENOMEM;
			goto error;
		}

		cmds[i].slave_addr = slave;
		cmds[i].post_wait_ms = delay;
		cmds[i].len = plen;
		cmds[i].data = cmds_data;

		pos += plen;
	}

	return 0;

error:
	while (i--) {
		kfree(cmds[i].data);
		cmds[i].data = NULL;
		cmds[i].len = 0;
	}
	return rc;
}

static const char * const dsi_panel_i2c_cmd_set_prop_map[DSI_PANEL_I2C_CMD_SET_MAX] = {
	[DSI_PANEL_I2C_CMD_SET_ON]         = "qcom,mdss-panel-i2c-on-command",
	[DSI_PANEL_I2C_CMD_SET_OFF]        = "qcom,mdss-panel-i2c-off-command",
	[DSI_PANEL_I2C_CMD_SET_BRIGHTNESS] = "qcom,mdss-panel-i2c-bl-command",
};

static void dsi_panel_i2c_free_config(struct dsi_panel *panel)
{
	u32 i, t;
	struct dsi_panel_i2c_config *cfg;
	struct dsi_panel_i2c_cmd_set *set;

	if (!panel)
		return;

	cfg = &panel->i2c_config;

	if (cfg->left_adapter) {
		i2c_put_adapter(cfg->left_adapter);
		cfg->left_adapter = NULL;
	}

	if (cfg->right_adapter) {
		i2c_put_adapter(cfg->right_adapter);
		cfg->right_adapter = NULL;
	}

	for (t = 0; t < DSI_PANEL_I2C_CMD_SET_MAX; t++) {
		set = &cfg->cmd_sets[t];
		if (set->cmds) {
			for (i = 0; i < set->count; i++) {
				kfree(set->cmds[i].data);
				set->cmds[i].data = NULL;
				set->cmds[i].len = 0;
			}
			kfree(set->cmds);
			set->cmds = NULL;
			set->count = 0;
		}
	}

	cfg->i2c_support = false;
}

/**
 * dsi_panel_i2c_parse_config - parse I2C backlight configuration from DT
 * @panel: DSI panel handle
 *
 * Reads the I2C backlight subtype and RGB LED node handles from the panel
 * device-tree node, then probes the left/right I2C adapter handles and
 * parses the on/off command sets.  Must be called during panel probe;
 * returns -EPROBE_DEFER if the I2C adapters are not yet available.
 *
 * Return: 0 on success, negative error code on failure.
 */
int dsi_panel_i2c_parse_config(struct dsi_panel *panel)
{
	struct dsi_panel_i2c_config *cfg;
	struct device_node *np = NULL, *np_left = NULL, *np_right = NULL;
	const u8 *data = NULL;
	int nbytes = 0;
	int rc = 0;
	u32 ncmds = 0;
	u32 t;

	if (!panel || !panel->panel_of_node) {
		DSI_INFO("invalid params\n");
		return 0;
	}

	cfg = &panel->i2c_config;
	np = panel->panel_of_node;

	/*
	 * Parse I2C-specific backlight configuration.  Do this before the
	 * adapter probe so the subtype and LED nodes are always populated,
	 * even if the adapters are not yet ready (-EPROBE_DEFER).
	 */
	if (panel->bl_config.type == DSI_BACKLIGHT_I2C) {
		const char *subtype = of_get_property(np,
				"qcom,mdss-dsi-bl-ctrl-i2c-subtype", NULL);

		/* Default to ISL97900 for backward compatibility */
		panel->bl_config.bl_i2c_subtype = DSI_BACKLIGHT_I2C_ISL97900;
		if (subtype && !strcmp(subtype, "jbd4040"))
			panel->bl_config.bl_i2c_subtype = DSI_BACKLIGHT_I2C_JBD4040;

		cfg->rgb_left_led_node = of_parse_phandle(np,
				"qcom,panel-rgb-left-led", 0);
		cfg->rgb_right_led_node = of_parse_phandle(np,
				"qcom,panel-rgb-right-led", 0);
	}

	np_left = of_parse_phandle(np, "qcom,panel-i2c-left", 0);
	np_right = of_parse_phandle(np, "qcom,panel-i2c-right", 0);

	if (!np_left && !np_right) {
		DSI_DEBUG("[%s] no panel i2c bus provided\n", panel->name);
		return 0;
	}

	if (np_left) {
		cfg->left_adapter = of_find_i2c_adapter_by_node(np_left);
		of_node_put(np_left);
	}

	if (np_right) {
		cfg->right_adapter = of_find_i2c_adapter_by_node(np_right);
		of_node_put(np_right);
	}

	if (!cfg->left_adapter && !cfg->right_adapter) {
		DSI_DEBUG("[%s] i2c adapter(s) not ready\n", panel->name);
		rc = -EPROBE_DEFER;
		goto error;
	}

	for (t = 0; t < DSI_PANEL_I2C_CMD_SET_MAX; t++) {
		struct dsi_panel_i2c_cmd_set *set = &cfg->cmd_sets[t];

		data = of_get_property(np, dsi_panel_i2c_cmd_set_prop_map[t], &nbytes);
		if (!data || !nbytes) {
			DSI_DEBUG("[%s] i2c cmd set %u (%s) not defined\n",
				  panel->name, t,
				  dsi_panel_i2c_cmd_set_prop_map[t]);
			continue;
		}

		rc = dsi_panel_i2c_get_cmd_count(data, (u32)nbytes, &ncmds);
		if (rc) {
			DSI_ERR("[%s] failed to get i2c cmd count for set %u, rc=%d\n",
				panel->name, t, rc);
			goto error;
		}

		if (!ncmds) {
			DSI_ERR("[%s] i2c cmd set %u (%s) has no valid commands\n",
				panel->name, t, dsi_panel_i2c_cmd_set_prop_map[t]);
			rc = -EINVAL;
			goto error;
		}

		set->cmds = kcalloc(ncmds, sizeof(*set->cmds), GFP_KERNEL);
		if (!set->cmds) {
			rc = -ENOMEM;
			goto error;
		}

		rc = dsi_panel_i2c_create_cmd_set(data, (u32)nbytes, ncmds, set->cmds);
		if (rc) {
			DSI_ERR("[%s] failed to create i2c cmd set %u, rc=%d\n",
				panel->name, t, rc);
			goto error;
		}

		set->count = ncmds;
	}

	cfg->i2c_support = true;

	return 0;

error:
	dsi_panel_i2c_free_config(panel);
	return rc;
}

static int dsi_panel_i2c_isl97900_update_brightness(struct dsi_panel *panel, u32 bl_lvl)
{
	if (!panel)
		return -EINVAL;

	if (panel->i2c_config.rgb_left_led_node)
		isl97900_led_event(panel->i2c_config.rgb_left_led_node,
				ISL_LED_BRIGHTNESS_RGB_LEVEL, bl_lvl);

	if (panel->i2c_config.rgb_right_led_node)
		isl97900_led_event(panel->i2c_config.rgb_right_led_node,
				ISL_LED_BRIGHTNESS_RGB_LEVEL, bl_lvl);

	return 0;
}

/*
 * dsi_panel_i2c_jbd4040_init_brightness_cmd - allocate and initialise the
 * JBD4040 brightness command set (called once on first use).
 *
 * The register address bytes are fixed; the brightness bytes at [3..4] are
 * left as zero and updated in-place on every subsequent brightness call.
 */
static int dsi_panel_i2c_jbd4040_init_brightness_cmd(struct dsi_panel_i2c_config *cfg)
{
	struct dsi_panel_i2c_cmd_set *bset;
	u8 *payload;

	bset = &cfg->cmd_sets[DSI_PANEL_I2C_CMD_SET_BRIGHTNESS];

	bset->cmds = kcalloc(1, sizeof(*bset->cmds), GFP_KERNEL);
	if (!bset->cmds)
		return -ENOMEM;

	payload = kcalloc(JBD4040_BL_CMD_LEN, sizeof(*payload), GFP_KERNEL);
	if (!payload) {
		kfree(bset->cmds);
		bset->cmds = NULL;
		return -ENOMEM;
	}

	/* Register address (fixed); brightness bytes filled on each update */
	payload[0] = JBD4040_BL_REG_B0;
	payload[1] = JBD4040_BL_REG_B1;
	payload[2] = JBD4040_BL_REG_B2;
	payload[3] = 0;
	payload[4] = 0;

	bset->cmds[0].slave_addr   = JBD4040_BL_SLAVE_ADDR;
	bset->cmds[0].data         = payload;
	bset->cmds[0].len          = JBD4040_BL_CMD_LEN;
	bset->cmds[0].post_wait_ms = 0;
	bset->count = 1;

	return 0;
}

static int dsi_panel_i2c_jbd4040_update_brightness(struct dsi_panel *panel, u32 bl_lvl)
{
	struct dsi_panel_i2c_config *cfg;
	struct dsi_panel_i2c_cmd_set *bset;
	int rc;

	if (!panel)
		return -EINVAL;

	cfg = &panel->i2c_config;

	if (!cfg->i2c_support)
		return 0;

	bset = &cfg->cmd_sets[DSI_PANEL_I2C_CMD_SET_BRIGHTNESS];

	/* Allocate the command set on first use; reuse on subsequent calls */
	if (!bset->count) {
		rc = dsi_panel_i2c_jbd4040_init_brightness_cmd(cfg);
		if (rc) {
			DSI_ERR("[%s] failed to init jbd4040 brightness cmd, rc=%d\n",
				panel->name, rc);
			return rc;
		}
	}

	/* Update brightness bytes in-place (big-endian, MSB first) */
	bset->cmds[0].data[3] = (u8)((bl_lvl >> 8) & 0xFF);
	bset->cmds[0].data[4] = (u8)(bl_lvl & 0xFF);

	return dsi_panel_i2c_tx_cmd_set(panel, DSI_PANEL_I2C_CMD_SET_BRIGHTNESS);
}

/**
 * dsi_panel_i2c_update_backlight - set the panel backlight level over I2C
 * @panel:  DSI panel handle
 * @bl_lvl: backlight level to apply
 *
 * Dispatches to the controller-specific brightness handler based on
 * bl_config.bl_i2c_subtype.
 *
 * Return: 0 on success, negative error code on failure.
 */
int dsi_panel_i2c_update_backlight(struct dsi_panel *panel, u32 bl_lvl)
{
	int rc = 0;
	struct dsi_backlight_config *bl = &panel->bl_config;

	switch (bl->bl_i2c_subtype) {
	case DSI_BACKLIGHT_I2C_ISL97900:
		rc = dsi_panel_i2c_isl97900_update_brightness(panel, bl_lvl);
		if (rc)
			DSI_ERR("[%s] failed to set isl i2c brightness, rc=%d\n",
				panel->name, rc);
		break;
	case DSI_BACKLIGHT_I2C_JBD4040:
		rc = dsi_panel_i2c_jbd4040_update_brightness(panel, bl_lvl);
		if (rc)
			DSI_ERR("[%s] failed to set jbd i2c brightness, rc=%d\n",
				panel->name, rc);
		break;
	default:
		rc = 0;
		break;
	}

	return rc;
}

/**
 * dsi_panel_i2c_tx_cmd_set - transmit an I2C command set to the panel
 * @panel: DSI panel handle
 * @type:  command set type to send (on, off, or brightness)
 *
 * Iterates over all commands in the specified set and sends each one over
 * the left and/or right I2C adapter.  Applies the per-command post-wait
 * delay after each transfer.  Returns immediately on the first error.
 *
 * Return: 0 on success, negative error code on failure.
 */
int dsi_panel_i2c_tx_cmd_set(struct dsi_panel *panel,
			      enum dsi_panel_i2c_cmd_set_type type)
{
	struct dsi_panel_i2c_cmd_set *set;
	int rc = 0;

	if (!panel)
		return -EINVAL;

	if (type >= DSI_PANEL_I2C_CMD_SET_MAX) {
		DSI_ERR("[%s] invalid i2c cmd set type %d\n", panel->name, type);
		return -EINVAL;
	}

	if (!panel->i2c_config.i2c_support) {
		DSI_DEBUG("[%s] i2c not supported, skipping cmd set %d\n",
			  panel->name, type);
		return 0;
	}

	set = &panel->i2c_config.cmd_sets[type];

	if (!set->count) {
		DSI_DEBUG("[%s] No i2c commands defined for set %d (%s)\n",
			  panel->name, type,
			  dsi_panel_i2c_cmd_set_prop_map[type]);
		return 0;
	}

	for (u32 i = 0; i < set->count; i++) {
		struct dsi_panel_i2c_cmd *cmd = &set->cmds[i];
		rc = dsi_panel_i2c_tx_cmd(panel, cmd->slave_addr, cmd->data, cmd->len);
		if (rc) {
			DSI_ERR("[%s] failed to send i2c cmd %u/%u (set %d), rc=%d\n",
				panel->name, i + 1, set->count, type, rc);
			break;
		}
		if (cmd->post_wait_ms) {
			usleep_range(cmd->post_wait_ms * 1000,
				cmd->post_wait_ms * 1000 + 100);
		}
	}

	return rc;
}
