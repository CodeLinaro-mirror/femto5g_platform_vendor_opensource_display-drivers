// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/delay.h>
#include <linux/of.h>
#include <linux/slab.h>
#if IS_ENABLED(CONFIG_MTD)
#include <linux/mtd/mtd.h>
#endif

#include "dsi_panel.h"

/* ISL97900 RGB LED controller register map */
#define ISL97900_ENABLE_CONTROL  0x02
#define ISL97900_LED_R_LSB       0x13
#define ISL97900_LED_G_LSB       0x14
#define ISL97900_LED_B_LSB       0x15
#define ISL97900_LED_RGB_MSB     0x17
#define ISL97900_MAX_LEVEL       1023

#define ISL97900_BL_CMD_COUNT    5
#define ISL97900_BL_CMD_LEN      2

/*
 * JBD4040 luminance control command constants
 * Slave address : 0x58
 * Payload       : <reg_b0> <reg_b1> <reg_b2> <bl_msb> <bl_lsb>
 * Register addr : 0x20 0x0A 0x14  (3 bytes)
 * Brightness    : 16-bit big-endian value at bytes [3..4]
 */
#define JBD4040_BL_SLAVE_ADDR  0x58
#define JBD4040_BL_REG_B0      0x20
#define JBD4040_BL_REG_B1      0x0A
#define JBD4040_BL_REG_B2      0x14
#define JBD4040_BL_CMD_LEN     5

/**
 * dsi_panel_i2c_tx_cmd - send a single I2C message to one adapter
 * @adapter:    I2C adapter to use
 * @slave_addr: 7-bit slave address
 * @buf:        payload bytes
 * @len:        payload length in bytes
 *
 * Return: 0 on success, negative error code on failure.
 */
int dsi_panel_i2c_tx_cmd(struct i2c_adapter *adapter, u8 slave_addr, const u8 *buf, u16 len)
{
	struct i2c_msg msg = {
		.addr  = slave_addr,
		.flags = 0,
		.len   = len,
		.buf   = (u8 *)buf,
	};
	int rc;

	if (!adapter || !buf || !len || !slave_addr)
		return -EINVAL;

	rc = i2c_transfer(adapter, &msg, 1);
	return (rc == 1) ? 0 : (rc < 0 ? rc : -EIO);
}

static const char * const dsi_panel_i2c_cmd_set_prop_map[DSI_PANEL_I2C_CMD_SET_MAX] = {
	[DSI_PANEL_I2C_CMD_SET_ON]                 = "qcom,mdss-panel-i2c-on-command",
	[DSI_PANEL_I2C_CMD_SET_OFF]                = "qcom,mdss-panel-i2c-off-command",
	[DSI_PANEL_I2C_CMD_SET_BRIGHTNESS]         = "qcom,mdss-panel-i2c-bl-command",
	[DSI_PANEL_I2C_CMD_SET_CALIBRATION_LEFT]   = NULL, /* built programmatically */
	[DSI_PANEL_I2C_CMD_SET_CALIBRATION_RIGHT]  = NULL, /* built programmatically */
	[DSI_PANEL_I2C_CMD_SET_DEMURA_ON]          = NULL, /* built programmatically */
	[DSI_PANEL_I2C_CMD_SET_GAMMA_ON]           = NULL, /* built programmatically */
};

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
 * Reads the I2C backlight subtype and slave addresses from the panel
 * device-tree node, then probes the left/right I2C adapter handles and
 * parses the on/off command sets. Must be called during panel probe;
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
	 * Parse I2C-specific backlight configuration. Do this before the
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
		else if (subtype && !strcmp(subtype, "isl97900"))
			panel->bl_config.bl_i2c_subtype = DSI_BACKLIGHT_I2C_ISL97900;

		if (panel->bl_config.bl_i2c_subtype == DSI_BACKLIGHT_I2C_ISL97900) {
			u32 addr;

			/*
			 * Read the ISL97900 slave addresses from the panel node.
			 * These are paired with the left/right I2C adapters from
			 * qcom,panel-i2c-left / qcom,panel-i2c-right.
			 */
			if (!of_property_read_u32(np,
					"qcom,panel-i2c-left-slave-addr", &addr))
				cfg->left_slave_addr = (u8)addr;

			if (!of_property_read_u32(np,
					"qcom,panel-i2c-right-slave-addr", &addr))
				cfg->right_slave_addr = (u8)addr;
		}
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

		/* Skip command sets built programmatically (no DT property) */
		if (!dsi_panel_i2c_cmd_set_prop_map[t]) {
			DSI_DEBUG("[%s] i2c cmd set %u is built programmatically, skip DT parse\n",
				  panel->name, t);
			continue;
		}

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

/*
 * dsi_panel_i2c_isl97900_init_brightness_cmd - allocate and initialise the
 * ISL97900 brightness command set (called once on first use).
 *
 * Builds 5 two-byte commands covering ENABLE_CONTROL, LED_R/G/B_LSB and
 * LED_RGB_MSB.  The register address bytes are fixed; the value bytes are
 * updated in-place on every subsequent brightness call.
 */
static int dsi_panel_i2c_isl97900_init_brightness_cmd(struct dsi_panel_i2c_config *cfg)
{
	static const u8 regs[5] = {
		ISL97900_ENABLE_CONTROL,
		ISL97900_LED_R_LSB,
		ISL97900_LED_G_LSB,
		ISL97900_LED_B_LSB,
		ISL97900_LED_RGB_MSB,
	};
	struct dsi_panel_i2c_cmd_set *bset;
	u32 i;

	bset = &cfg->cmd_sets[DSI_PANEL_I2C_CMD_SET_BRIGHTNESS];

	bset->cmds = kcalloc(ISL97900_BL_CMD_COUNT, sizeof(*bset->cmds), GFP_KERNEL);
	if (!bset->cmds)
		return -ENOMEM;

	for (i = 0; i < 5; i++) {
		bset->cmds[i].data = kcalloc(ISL97900_BL_CMD_LEN, sizeof(u8), GFP_KERNEL);
		if (!bset->cmds[i].data) {
			while (i--) {
				kfree(bset->cmds[i].data);
				bset->cmds[i].data = NULL;
			}
			kfree(bset->cmds);
			bset->cmds = NULL;
			return -ENOMEM;
		}
		/* Register address (fixed); value byte filled on each update */
		bset->cmds[i].data[0]      = regs[i];
		bset->cmds[i].data[1]      = (i == 0) ? 0x04 : 0x00;
		bset->cmds[i].len          = ISL97900_BL_CMD_LEN;
		bset->cmds[i].post_wait_ms = 0;
		/*
		 * slave_addr = 0: dsi_panel_i2c_tx_cmd_set() will substitute
		 * cfg->left_slave_addr / cfg->right_slave_addr at send time.
		 */
		bset->cmds[i].slave_addr   = 0;
	}
	bset->count = 5;

	return 0;
}

static int dsi_panel_i2c_isl97900_update_brightness(struct dsi_panel *panel, u32 bl_lvl)
{
	struct dsi_panel_i2c_config *cfg;
	struct dsi_panel_i2c_cmd_set *bset;
	u32 msb;
	int rc = 0;

	if (!panel)
		return -EINVAL;

	cfg = &panel->i2c_config;
	bset = &cfg->cmd_sets[DSI_PANEL_I2C_CMD_SET_BRIGHTNESS];

	/* Allocate the command set on first use; reuse on subsequent calls */
	if (!bset->count) {
		rc = dsi_panel_i2c_isl97900_init_brightness_cmd(cfg);
		if (rc) {
			DSI_ERR("[%s] failed to init isl97900 brightness cmd, rc=%d\n",
				panel->name, rc);
			return rc;
		}
	}

	if (bl_lvl > ISL97900_MAX_LEVEL)
		bl_lvl = ISL97900_MAX_LEVEL;

	/* Pack the 2-bit MSB of each channel into the shared MSB register */
	msb  = ((bl_lvl >> 8) & 0x03) << 6; /* red   [7:6] */
	msb |= ((bl_lvl >> 8) & 0x03) << 4; /* green [5:4] */
	msb |= ((bl_lvl >> 8) & 0x03) << 2; /* blue  [3:2] */

	/* Update brightness value bytes in-place (cmd[0] ENABLE_CONTROL is fixed) */
	bset->cmds[1].data[1] = (u8)(bl_lvl & 0xFF); /* LED_R_LSB */
	bset->cmds[2].data[1] = (u8)(bl_lvl & 0xFF); /* LED_G_LSB */
	bset->cmds[3].data[1] = (u8)(bl_lvl & 0xFF); /* LED_B_LSB */
	bset->cmds[4].data[1] = (u8)msb;              /* LED_RGB_MSB */

	if (cfg->left_adapter) {
		rc = dsi_panel_i2c_tx_cmd_set(panel, DSI_PANEL_I2C_CMD_SET_BRIGHTNESS,
					      cfg->left_adapter);
		if (rc)
			return rc;
	}

	if (cfg->right_adapter) {
		rc = dsi_panel_i2c_tx_cmd_set(panel, DSI_PANEL_I2C_CMD_SET_BRIGHTNESS,
					      cfg->right_adapter);
	}

	return rc;
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

	if (cfg->left_adapter) {
		rc = dsi_panel_i2c_tx_cmd_set(panel, DSI_PANEL_I2C_CMD_SET_BRIGHTNESS,
					      cfg->left_adapter);
		if (rc)
			return rc;
	}

	if (cfg->right_adapter) {
		rc = dsi_panel_i2c_tx_cmd_set(panel, DSI_PANEL_I2C_CMD_SET_BRIGHTNESS,
					      cfg->right_adapter);
	}

	return rc;
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
 * dsi_panel_i2c_enable - send the I2C panel-on command set to both adapters
 * @panel: DSI panel handle
 *
 * Wrapper around dsi_panel_i2c_tx_cmd_set() that broadcasts
 * DSI_PANEL_I2C_CMD_SET_ON to both the left and right I2C adapters.
 *
 * Return: 0 on success, negative error code on failure.
 */
int dsi_panel_i2c_enable(struct dsi_panel *panel)
{
	struct dsi_panel_i2c_config *cfg;
	int rc = 0;

	if (!panel)
		return -EINVAL;

	cfg = &panel->i2c_config;

	rc = dsi_panel_i2c_tx_cmd_set(panel, DSI_PANEL_I2C_CMD_SET_ON,
				       cfg->left_adapter);
	if (rc) {
		DSI_ERR("[%s] failed to send i2c on cmds on left, rc=%d\n",
			panel->name, rc);
		return rc;
	}

	rc = dsi_panel_i2c_tx_cmd_set(panel, DSI_PANEL_I2C_CMD_SET_ON,
				       cfg->right_adapter);
	if (rc)
		DSI_ERR("[%s] failed to send i2c on cmds on right, rc=%d\n",
			panel->name, rc);

	return rc;
}

/**
 * dsi_panel_i2c_disable - send the I2C panel-off command set to both adapters
 * @panel: DSI panel handle
 *
 * Wrapper around dsi_panel_i2c_tx_cmd_set() that broadcasts
 * DSI_PANEL_I2C_CMD_SET_OFF to both the left and right I2C adapters.
 *
 * Return: 0 on success, negative error code on failure.
 */
int dsi_panel_i2c_disable(struct dsi_panel *panel)
{
	struct dsi_panel_i2c_config *cfg;
	int rc = 0;

	if (!panel)
		return -EINVAL;

	cfg = &panel->i2c_config;

	rc = dsi_panel_i2c_tx_cmd_set(panel, DSI_PANEL_I2C_CMD_SET_OFF,
				       cfg->left_adapter);
	if (rc) {
		DSI_ERR("[%s] failed to send i2c off cmds on left, rc=%d\n",
			panel->name, rc);
		return rc;
	}

	rc = dsi_panel_i2c_tx_cmd_set(panel, DSI_PANEL_I2C_CMD_SET_OFF,
				       cfg->right_adapter);
	if (rc)
		DSI_ERR("[%s] failed to send i2c off cmds on right, rc=%d\n",
			panel->name, rc);

	return rc;
}

/**
 * dsi_panel_i2c_tx_cmd_set - transmit an I2C command set to one adapter
 * @panel:   DSI panel handle
 * @type:    command set type to send (on, off, brightness, calibration, ...)
 * @adapter: I2C adapter to send the commands on (left or right)
 *
 * Iterates over all commands in the specified set and sends each one over
 * the supplied I2C adapter.  Applies the per-command post-wait delay after
 * each transfer.  Returns immediately on the first error.
 *
 * Callers that need to reach both adapters must invoke this function twice,
 * once with cfg->left_adapter and once with cfg->right_adapter.
 *
 * Return: 0 on success, negative error code on failure.
 */
int dsi_panel_i2c_tx_cmd_set(struct dsi_panel *panel,
			      enum dsi_panel_i2c_cmd_set_type type,
			      struct i2c_adapter *adapter)
{
	struct dsi_panel_i2c_config *cfg;
	struct dsi_panel_i2c_cmd_set *set;
	int rc = 0;

	if (!panel)
		return -EINVAL;

	/* NULL adapter is a silent no-op (adapter not present on this side) */
	if (!adapter)
		return 0;

	if (type >= DSI_PANEL_I2C_CMD_SET_MAX) {
		DSI_ERR("[%s] invalid i2c cmd set type %d\n", panel->name, type);
		return -EINVAL;
	}

	if (!panel->i2c_config.i2c_support) {
		DSI_DEBUG("[%s] i2c not supported, skipping cmd set %d\n",
			  panel->name, type);
		return 0;
	}

	cfg = &panel->i2c_config;
	set = &cfg->cmd_sets[type];

	if (!set->count) {
		DSI_DEBUG("[%s] No i2c commands defined for set %d (%s)\n",
			  panel->name, type,
			  dsi_panel_i2c_cmd_set_prop_map[type]);
		return 0;
	}

	for (u32 i = 0; i < set->count; i++) {
		struct dsi_panel_i2c_cmd *cmd = &set->cmds[i];
		u8 slave_addr;

		/*
		 * Commands with an explicit slave address (e.g. JBD4040
		 * brightness, DT on/off commands) use it directly.
		 * Commands with slave_addr == 0 (ISL97900 brightness, built
		 * in the driver) fall back to the per-adapter address stored
		 * in cfg->left_slave_addr / cfg->right_slave_addr.
		 */
		if (cmd->slave_addr) {
			slave_addr = cmd->slave_addr;
		} else if (adapter == cfg->left_adapter) {
			slave_addr = cfg->left_slave_addr;
		} else if (adapter == cfg->right_adapter) {
			slave_addr = cfg->right_slave_addr;
		} else {
			slave_addr = 0;
		}

		if (slave_addr) {
			rc = dsi_panel_i2c_tx_cmd(adapter, slave_addr,
						  cmd->data, cmd->len);
			if (rc) {
				DSI_WARN("[%s] failed cmd %u/%u (set %d), rc=%d\n",
					panel->name, i + 1, set->count, type, rc);
			}
		}

		if (cmd->post_wait_ms) {
			usleep_range(cmd->post_wait_ms * 1000,
				cmd->post_wait_ms * 1000 + 100);
		}
	}

	return 0;
}

/* ---- JBD4040 calibration ---- */

/*
 * JBD4040 DDIC (panel) memory base addresses.
 * These are the destination addresses prepended to each I2C write buffer.
 */
#define JBD4040_DDIC_DEMURA_BASE_ADDRESS    0x210000
#define JBD4040_DDIC_GAMMA_BASE_ADDRESS     0x250000
#define JBD4040_DDIC_OFFSET_BASE_ADDRESS    0x200A24
#define JBD4040_DDIC_FLIP_BASE_ADDRESS      0x20020E

/* Per-field sizes */
#define JBD4040_FLASH_GAMMA_SIZE    0x1000
#define JBD4040_FLASH_DEMURA_SIZE   0x32000
#define JBD4040_FLASH_OFFSET_SIZE   0x2
#define JBD4040_FLASH_FLIP_SIZE     0x2

/* Red channel flash offsets */
#define JBD4040_FLASH_RED_GAMMA_OFFSET    0x000000
#define JBD4040_FLASH_RED_DEMURA_OFFSET   0x008000
#define JBD4040_FLASH_RED_OFFSET_OFFSET   0x078000
#define JBD4040_FLASH_RED_FLIP_OFFSET     0x078004

/* Green channel flash offsets */
#define JBD4040_FLASH_GREEN_GAMMA_OFFSET   0x080000
#define JBD4040_FLASH_GREEN_DEMURA_OFFSET  0x088000
#define JBD4040_FLASH_GREEN_OFFSET_OFFSET  0x0F8000
#define JBD4040_FLASH_GREEN_FLIP_OFFSET    0x0F8004

/* Blue channel flash offsets */
#define JBD4040_FLASH_BLUE_GAMMA_OFFSET    0x100000
#define JBD4040_FLASH_BLUE_DEMURA_OFFSET   0x108000
#define JBD4040_FLASH_BLUE_OFFSET_OFFSET   0x178000
#define JBD4040_FLASH_BLUE_FLIP_OFFSET     0x178004

/* Minimum partition size: end of the last field (blue_flip) */
#define JBD4040_FLASH_MIN_PARTITION_SIZE \
	(JBD4040_FLASH_BLUE_FLIP_OFFSET + JBD4040_FLASH_FLIP_SIZE)

/*
 * Maximum size of a single I2C calibration command data buffer.
 * The first 3 bytes are the DDIC address; the remaining bytes are payload.
 */
#define JBD4040_I2C_CMD_MAX_SIZE     (1 << 15)             /* 32768 bytes */
/* Round down to even so 16-bit words are never split across commands */
#define JBD4040_I2C_CMD_MAX_PAYLOAD  ((JBD4040_I2C_CMD_MAX_SIZE - 3) & ~1U)

/*
 * dsi_panel_i2c_count_field_cmds - return the number of I2C commands needed
 * to transfer @payload_size bytes given the per-command payload limit.
 */
static u32 __maybe_unused dsi_panel_i2c_count_field_cmds(size_t payload_size)
{
	if (!payload_size)
		return 0;
	return (u32)((payload_size + JBD4040_I2C_CMD_MAX_PAYLOAD - 1) /
		     JBD4040_I2C_CMD_MAX_PAYLOAD);
}

/*
 * dsi_panel_i2c_fill_field_cmds - fill one or more I2C commands for a single
 * calibration field.
 */
static int __maybe_unused dsi_panel_i2c_fill_field_cmds(struct dsi_panel_i2c_cmd *cmds,
					  u32 *cmd_idx,
					  u8 *raw_data,
					  size_t payload_size,
					  u32 ddic_base_addr,
					  u8 slave_addr, bool data_big_endian)
{
	size_t offset = 0;

	if (!raw_data || !payload_size)
		return 0;

	/*
	 * Split the calibration field into one or more I2C commands, each
	 * carrying at most JBD4040_I2C_CMD_MAX_PAYLOAD bytes of payload.
	 * The DDIC destination address advances by the chunk size each iteration.
	 */
	while (offset < payload_size) {
		size_t chunk = min_t(size_t, payload_size - offset,
				     JBD4040_I2C_CMD_MAX_PAYLOAD);
		u32 chunk_addr = ddic_base_addr + (u32)offset;
		u8 *cmd_data;
		u32 i, idx = 3;

		/* Allocate: 3-byte DDIC address header + payload */
		cmd_data = kmalloc(chunk + 3, GFP_KERNEL);
		if (!cmd_data)
			return -ENOMEM;

		/* Encode the 24-bit DDIC destination address (big-endian) */
		cmd_data[0] = (chunk_addr >> 16) & 0xFF;
		cmd_data[1] = (chunk_addr >> 8)  & 0xFF;
		cmd_data[2] =  chunk_addr        & 0xFF;

		/* Copy raw flash data into the payload region of cmd_data */
		memcpy(cmd_data + 3, raw_data + offset, chunk);

		/*
		 * Re-encode each 16-bit word to little-endian as required by
		 * the DDIC.  Read the word using the source byte order
		 * (big- or little-endian) then write it back LSB-first.
		 */
		const u8 *raw_payload = cmd_data + 3;

		for (i = 0; i < chunk/2; i++) {
			u16 val;

			if (data_big_endian)
				val = ((u16)raw_payload[2 * i] << 8) | raw_payload[2 * i + 1];
			else
				val = raw_payload[2 * i] | ((u16)raw_payload[2 * i + 1] << 8);

			cmd_data[idx++] = val & 0xFF;        /* LSB */
			cmd_data[idx++] = (val >> 8) & 0xFF; /* MSB */
		}

		/* Copy any odd trailing byte verbatim (no byte-swap needed) */
		if (chunk % 2)
			cmd_data[idx++] = raw_payload[chunk - 1];

		cmds[*cmd_idx].data         = cmd_data;
		cmds[*cmd_idx].len          = (u32)(3 + chunk);
		cmds[*cmd_idx].slave_addr   = slave_addr;
		cmds[*cmd_idx].post_wait_ms = 0;
		(*cmd_idx)++;

		offset += chunk;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_MTD)
/*
 * dsi_panel_read_mtd_field - Read a single calibration field from the flash
 * at the given absolute offset and size into a newly allocated buffer.
 * The caller is responsible for kvfree()ing *data_out on success.
 * put_mtd_device() is NOT called here; the caller owns the MTD reference.
 */
static int dsi_panel_read_mtd_field(struct mtd_info *mtd,
				    loff_t offset, size_t size,
				    u8 **data_out)
{
	u8 *buf;
	size_t retlen = 0;
	int rc;

	buf = kvzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rc = mtd_read(mtd, offset, size, &retlen, buf);
	if (rc && rc != -EUCLEAN) {
		DSI_ERR("MTD read failed for %s at offset 0x%llx size %zu, rc=%d\n",
			mtd->name, offset, size, rc);
		kvfree(buf);
		return rc;
	}

	if (retlen != size) {
		DSI_ERR("MTD short read for %s at offset 0x%llx: got %zu, expected %zu\n",
			mtd->name, offset, retlen, size);
		kvfree(buf);
		return -EIO;
	}

	*data_out = buf;
	return 0;
}

/*
 * Build the JBD4040 calibration I2C command set for one eye.
 */
static int dsi_panel_i2c_jbd4040_init_calibration_cmd(
		struct dsi_panel_i2c_config *cfg,
		struct mtd_info *mtd,
		enum dsi_panel_i2c_cmd_set_type cmd_type)
{
	struct dsi_panel_i2c_cmd_set *cset;
	u32 total_cmds = 0;
	u32 cmd_idx = 0;
	u8 *field_buf = NULL;
	int rc = 0;

	/* Sanity: flash must be large enough to hold all calibration fields */
	if (mtd->size < JBD4040_FLASH_MIN_PARTITION_SIZE) {
		DSI_ERR("Flash %s too small: %llu < %u bytes\n",
			mtd->name, mtd->size, JBD4040_FLASH_MIN_PARTITION_SIZE);
		return -EINVAL;
	}

	total_cmds += dsi_panel_i2c_count_field_cmds(JBD4040_FLASH_GAMMA_SIZE);
	total_cmds += dsi_panel_i2c_count_field_cmds(JBD4040_FLASH_DEMURA_SIZE);
	total_cmds += dsi_panel_i2c_count_field_cmds(JBD4040_FLASH_OFFSET_SIZE);
	total_cmds += dsi_panel_i2c_count_field_cmds(JBD4040_FLASH_FLIP_SIZE);
	total_cmds += dsi_panel_i2c_count_field_cmds(JBD4040_FLASH_GAMMA_SIZE);
	total_cmds += dsi_panel_i2c_count_field_cmds(JBD4040_FLASH_DEMURA_SIZE);
	total_cmds += dsi_panel_i2c_count_field_cmds(JBD4040_FLASH_OFFSET_SIZE);
	total_cmds += dsi_panel_i2c_count_field_cmds(JBD4040_FLASH_FLIP_SIZE);
	total_cmds += dsi_panel_i2c_count_field_cmds(JBD4040_FLASH_GAMMA_SIZE);
	total_cmds += dsi_panel_i2c_count_field_cmds(JBD4040_FLASH_DEMURA_SIZE);
	total_cmds += dsi_panel_i2c_count_field_cmds(JBD4040_FLASH_OFFSET_SIZE);
	total_cmds += dsi_panel_i2c_count_field_cmds(JBD4040_FLASH_FLIP_SIZE);

	cset = &cfg->cmd_sets[cmd_type];
	cset->cmds = kcalloc(total_cmds, sizeof(*cset->cmds), GFP_KERNEL);
	if (!cset->cmds)
		return -ENOMEM;
	cset->count = total_cmds;

	/* Red channel (slave 0x59) */
	rc = dsi_panel_read_mtd_field(mtd, JBD4040_FLASH_RED_GAMMA_OFFSET,
				      JBD4040_FLASH_GAMMA_SIZE, &field_buf);
	if (rc)
		goto error;
	rc = dsi_panel_i2c_fill_field_cmds(cset->cmds, &cmd_idx, field_buf,
		JBD4040_FLASH_GAMMA_SIZE, JBD4040_DDIC_GAMMA_BASE_ADDRESS, 0x59, false);
	kvfree(field_buf); field_buf = NULL;
	if (rc)
		goto error;

	rc = dsi_panel_read_mtd_field(mtd, JBD4040_FLASH_RED_DEMURA_OFFSET,
				      JBD4040_FLASH_DEMURA_SIZE, &field_buf);
	if (rc)
		goto error;

	rc = dsi_panel_i2c_fill_field_cmds(cset->cmds, &cmd_idx, field_buf,
		JBD4040_FLASH_DEMURA_SIZE, JBD4040_DDIC_DEMURA_BASE_ADDRESS, 0x59, false);
	kvfree(field_buf); field_buf = NULL;
	if (rc)
		goto error;

	rc = dsi_panel_read_mtd_field(mtd, JBD4040_FLASH_RED_OFFSET_OFFSET,
				      JBD4040_FLASH_OFFSET_SIZE, &field_buf);
	if (rc)
		goto error;

	rc = dsi_panel_i2c_fill_field_cmds(cset->cmds, &cmd_idx, field_buf,
		JBD4040_FLASH_OFFSET_SIZE, JBD4040_DDIC_OFFSET_BASE_ADDRESS, 0x59, true);
	kvfree(field_buf); field_buf = NULL;
	if (rc)
		goto error;

	rc = dsi_panel_read_mtd_field(mtd, JBD4040_FLASH_RED_FLIP_OFFSET,
				      JBD4040_FLASH_FLIP_SIZE, &field_buf);
	if (rc)
		goto error;

	rc = dsi_panel_i2c_fill_field_cmds(cset->cmds, &cmd_idx, field_buf,
		JBD4040_FLASH_FLIP_SIZE, JBD4040_DDIC_FLIP_BASE_ADDRESS, 0x59, true);
	kvfree(field_buf); field_buf = NULL;
	if (rc)
		goto error;

	/* Green channel (slave 0x5a) */
	rc = dsi_panel_read_mtd_field(mtd, JBD4040_FLASH_GREEN_GAMMA_OFFSET,
				      JBD4040_FLASH_GAMMA_SIZE, &field_buf);
	if (rc)
		goto error;

	rc = dsi_panel_i2c_fill_field_cmds(cset->cmds, &cmd_idx, field_buf,
		JBD4040_FLASH_GAMMA_SIZE, JBD4040_DDIC_GAMMA_BASE_ADDRESS, 0x5a, false);
	kvfree(field_buf); field_buf = NULL;
	if (rc)
		goto error;

	rc = dsi_panel_read_mtd_field(mtd, JBD4040_FLASH_GREEN_DEMURA_OFFSET,
				      JBD4040_FLASH_DEMURA_SIZE, &field_buf);
	if (rc)
		goto error;

	rc = dsi_panel_i2c_fill_field_cmds(cset->cmds, &cmd_idx, field_buf,
		JBD4040_FLASH_DEMURA_SIZE, JBD4040_DDIC_DEMURA_BASE_ADDRESS, 0x5a, false);
	kvfree(field_buf); field_buf = NULL;
	if (rc)
		goto error;

	rc = dsi_panel_read_mtd_field(mtd, JBD4040_FLASH_GREEN_OFFSET_OFFSET,
				      JBD4040_FLASH_OFFSET_SIZE, &field_buf);
	if (rc)
		goto error;

	rc = dsi_panel_i2c_fill_field_cmds(cset->cmds, &cmd_idx, field_buf,
		JBD4040_FLASH_OFFSET_SIZE, JBD4040_DDIC_OFFSET_BASE_ADDRESS, 0x5a, true);
	kvfree(field_buf); field_buf = NULL;
	if (rc)
		goto error;

	rc = dsi_panel_read_mtd_field(mtd, JBD4040_FLASH_GREEN_FLIP_OFFSET,
				      JBD4040_FLASH_FLIP_SIZE, &field_buf);
	if (rc)
		goto error;

	rc = dsi_panel_i2c_fill_field_cmds(cset->cmds, &cmd_idx, field_buf,
		JBD4040_FLASH_FLIP_SIZE, JBD4040_DDIC_FLIP_BASE_ADDRESS, 0x5a, true);
	kvfree(field_buf); field_buf = NULL;
	if (rc)
		goto error;

	/* Blue channel (slave 0x5b) */
	rc = dsi_panel_read_mtd_field(mtd, JBD4040_FLASH_BLUE_GAMMA_OFFSET,
				      JBD4040_FLASH_GAMMA_SIZE, &field_buf);
	if (rc)
		goto error;

	rc = dsi_panel_i2c_fill_field_cmds(cset->cmds, &cmd_idx, field_buf,
		JBD4040_FLASH_GAMMA_SIZE, JBD4040_DDIC_GAMMA_BASE_ADDRESS, 0x5b, false);
	kvfree(field_buf); field_buf = NULL;
	if (rc)
		goto error;

	rc = dsi_panel_read_mtd_field(mtd, JBD4040_FLASH_BLUE_DEMURA_OFFSET,
				      JBD4040_FLASH_DEMURA_SIZE, &field_buf);
	if (rc)
		goto error;

	rc = dsi_panel_i2c_fill_field_cmds(cset->cmds, &cmd_idx, field_buf,
		JBD4040_FLASH_DEMURA_SIZE, JBD4040_DDIC_DEMURA_BASE_ADDRESS, 0x5b, false);
	kvfree(field_buf); field_buf = NULL;
	if (rc)
		goto error;

	rc = dsi_panel_read_mtd_field(mtd, JBD4040_FLASH_BLUE_OFFSET_OFFSET,
				      JBD4040_FLASH_OFFSET_SIZE, &field_buf);
	if (rc)
		goto error;

	rc = dsi_panel_i2c_fill_field_cmds(cset->cmds, &cmd_idx, field_buf,
		JBD4040_FLASH_OFFSET_SIZE, JBD4040_DDIC_OFFSET_BASE_ADDRESS, 0x5b, true);
	kvfree(field_buf); field_buf = NULL;
	if (rc)
		goto error;

	rc = dsi_panel_read_mtd_field(mtd, JBD4040_FLASH_BLUE_FLIP_OFFSET,
				      JBD4040_FLASH_FLIP_SIZE, &field_buf);
	if (rc)
		goto error;

	rc = dsi_panel_i2c_fill_field_cmds(cset->cmds, &cmd_idx, field_buf,
		JBD4040_FLASH_FLIP_SIZE, JBD4040_DDIC_FLIP_BASE_ADDRESS, 0x5b, true);
	kvfree(field_buf); field_buf = NULL;
	if (rc)
		goto error;

	DSI_INFO("calibration cmd set (%d): %u commands\n", cmd_type, total_cmds);
	return rc;

error:
	kvfree(field_buf);
	for (u32 i = 0; i < cmd_idx; i++) {
		kfree(cset->cmds[i].data);
		cset->cmds[i].data = NULL;
	}
	kfree(cset->cmds);
	cset->cmds = NULL;
	cset->count = 0;
	return rc;
}
#else
static inline int dsi_panel_i2c_jbd4040_init_calibration_cmd(
		struct dsi_panel_i2c_config *cfg,
		struct mtd_info *mtd,
		enum dsi_panel_i2c_cmd_set_type cmd_type)
{
	return -ENODEV;
}
#endif /* CONFIG_MTD */

static int dsi_panel_i2c_left_calibration_thread(void *data)
{
	struct dsi_panel *panel = data;
	struct dsi_panel_i2c_config *cfg;
	int rc = 0;

	cfg = &panel->i2c_config;

	rc = dsi_panel_i2c_tx_cmd_set(
			panel,
			DSI_PANEL_I2C_CMD_SET_CALIBRATION_LEFT,
			cfg->left_adapter);

	if (rc)
		DSI_ERR("[%s] failed to send left calibration cmds, rc=%d\n",
			panel->name, rc);

	complete(&cfg->calibration_done);

	return 0;
}

/*
 * dsi_panel_i2c_jbd4040_send_calibration_cmd - build (on first call) and send
 * the JBD4040 calibration command sets for both eyes.
 */
static int dsi_panel_i2c_jbd4040_send_calibration_cmd(struct dsi_panel *panel)
{
	struct dsi_panel_i2c_config *cfg;
	struct task_struct *task;
	int rc = 0;

	if (!panel)
		return -EINVAL;

	cfg = &panel->i2c_config;

	if (!cfg->i2c_support)
		return 0;

	if (!cfg->cmd_sets[DSI_PANEL_I2C_CMD_SET_CALIBRATION_LEFT].count &&
		panel->calibration_mtd_left) {
		rc = dsi_panel_i2c_jbd4040_init_calibration_cmd(cfg,
					panel->calibration_mtd_left,
					DSI_PANEL_I2C_CMD_SET_CALIBRATION_LEFT);
		if (rc) {
			DSI_ERR("[%s] failed to init left calibration cmd, rc=%d\n",
				panel->name, rc);
			return rc;
		}
	}

	if (!cfg->cmd_sets[DSI_PANEL_I2C_CMD_SET_CALIBRATION_RIGHT].count &&
		panel->calibration_mtd_right) {
		rc = dsi_panel_i2c_jbd4040_init_calibration_cmd(cfg,
					panel->calibration_mtd_right,
					DSI_PANEL_I2C_CMD_SET_CALIBRATION_RIGHT);
		if (rc) {
			DSI_ERR("[%s] failed to init right calibration cmd, rc=%d\n",
				panel->name, rc);
			return rc;
		}
	}

	init_completion(&cfg->calibration_done);

	/* Send left side data in a separate thread */
	task = kthread_run(dsi_panel_i2c_left_calibration_thread,
			panel, "dsi_left_calib");
	if (IS_ERR(task)) {
		rc = PTR_ERR(task);
		DSI_ERR("[%s] failed to create left calib thread, rc=%d\n",
			panel->name, rc);
		return rc;
	}

	rc = dsi_panel_i2c_tx_cmd_set(panel, DSI_PANEL_I2C_CMD_SET_CALIBRATION_RIGHT,
				       cfg->right_adapter);
	if (rc)
		DSI_ERR("[%s] failed to send right calibration cmds, rc=%d\n",
			panel->name, rc);

	/* Wait for left side transmission to complete */
	wait_for_completion(&cfg->calibration_done);

	return rc;
}

/*
 * dsi_panel_i2c_jbd4040_enable_demura_gamma - enable demura and gamma on the panel
 * after calibration data has been written.
 */
static void dsi_panel_i2c_jbd4040_enable_demura_gamma(struct dsi_panel *panel)
{
	struct dsi_panel_i2c_config *cfg = &panel->i2c_config;

	/* Demura/gamma enable command payloads (slave 0x58) */
	static const u8 jbd4040_gamma_en_cmd[]  = { 0x20, 0x02, 0x00, 0x00, 0x01 };
	static const u8 jbd4040_demura_en_cmd[] = { 0x20, 0x02, 0x02, 0x00, 0x01 };

	dsi_panel_i2c_tx_cmd(cfg->left_adapter,  0x58,
			     jbd4040_gamma_en_cmd,  sizeof(jbd4040_gamma_en_cmd));
	dsi_panel_i2c_tx_cmd(cfg->right_adapter, 0x58,
			     jbd4040_gamma_en_cmd,  sizeof(jbd4040_gamma_en_cmd));
	dsi_panel_i2c_tx_cmd(cfg->left_adapter,  0x58,
			     jbd4040_demura_en_cmd, sizeof(jbd4040_demura_en_cmd));
	dsi_panel_i2c_tx_cmd(cfg->right_adapter, 0x58,
			     jbd4040_demura_en_cmd, sizeof(jbd4040_demura_en_cmd));
}

/**
 * dsi_panel_i2c_calibration - run the full JBD4040 calibration sequence
 * @panel: DSI panel handle
 *
 * Writes per-unit calibration data (gamma, demura, offset, flip) from the
 * cal_left / cal_right MTD partitions to the panel DDIC over I2C, then
 * enables demura and gamma correction.  Skipped if the panel DT property
 * "qcom,mdss-dsi-panel-calibration-enabled" is not set.
 *
 * Return: 0 on success, negative error code on failure.
 */
int dsi_panel_i2c_calibrate(struct dsi_panel *panel)
{
	int rc = 0;

	if (!panel || !panel->i2c_config.i2c_support) {
		DSI_DEBUG("i2c not supported, skipping calibration\n");
		return 0;
	}

	if (!panel->calibration_enabled) {
		DSI_INFO("[%s] calibration not enabled for this panel, skipping\n",
			 panel->name);
		return 0;
	}

	DSI_INFO("[%s] calibration start\n", panel->name);

	/*
	 * Step 1: Write calibration data to panel memory via I2C.
	 * The data MUST be loaded before enabling demura/gamma.
	 */
	rc = dsi_panel_i2c_jbd4040_send_calibration_cmd(panel);
	if (rc) {
		DSI_ERR("[%s] failed to send calibration cmds, rc=%d\n",
			panel->name, rc);
		return rc;
	}

	/* Step 2: Enable demura and gamma */
	dsi_panel_i2c_jbd4040_enable_demura_gamma(panel);

	DSI_INFO("[%s] calibration end\n", panel->name);
	return 0;
}
