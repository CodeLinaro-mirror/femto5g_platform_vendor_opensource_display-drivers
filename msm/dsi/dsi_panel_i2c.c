// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/delay.h>
#include <linux/of.h>
#include <linux/slab.h>

#include "dsi_panel.h"

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

static void dsi_panel_i2c_free_config(struct dsi_panel *panel)
{
	u32 i;
	struct dsi_panel_i2c_config *cfg;

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

	if (cfg->cmd_set.cmds) {
		for (i = 0; i < cfg->cmd_set.count; i++) {
			kfree(cfg->cmd_set.cmds[i].data);
			cfg->cmd_set.cmds[i].data = NULL;
			cfg->cmd_set.cmds[i].len = 0;
		}
		kfree(cfg->cmd_set.cmds);
		cfg->cmd_set.cmds = NULL;
		cfg->cmd_set.count = 0;
	}

	cfg->i2c_support = false;
}

int dsi_panel_i2c_parse_config(struct dsi_panel *panel)
{
	struct dsi_panel_i2c_config *cfg;
	struct device_node *np = NULL, *np_left = NULL, *np_right = NULL;
	const u8 *data = NULL;
	int nbytes = 0;
	int rc = 0;
	u32 ncmds = 0;

	if (!panel || !panel->panel_of_node) {
		DSI_INFO("invalid params\n");
		return 0;
	}

	cfg = &panel->i2c_config;
	np = panel->panel_of_node;

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
	}

	data = of_get_property(np, "qcom,mdss-panel-i2c-on-command", &nbytes);
	if (!data || !nbytes) {
		rc = 0;
		goto error;
	}

	rc = dsi_panel_i2c_get_cmd_count(data, (u32)nbytes, &ncmds);
	if (rc) {
		DSI_ERR("[%s] failed to get i2c cmd count, rc=%d\n", panel->name, rc);
		goto error;
	}

	cfg->cmd_set.count = ncmds;
	cfg->cmd_set.cmds = kcalloc(ncmds, sizeof(*cfg->cmd_set.cmds), GFP_KERNEL);
	if (!cfg->cmd_set.cmds) {
		rc = -ENOMEM;
		goto error;
	}

	rc = dsi_panel_i2c_create_cmd_set(data, (u32)nbytes, ncmds, cfg->cmd_set.cmds);
	if (rc) {
		DSI_ERR("[%s] failed to create i2c cmd set, rc=%d\n", panel->name, rc);
		goto error;
	}

	if (cfg->cmd_set.count)
		cfg->i2c_support = true;

	return 0;

error:
	dsi_panel_i2c_free_config(panel);
	return rc;
}

int dsi_panel_i2c_tx_cmd_set(struct dsi_panel *panel)
{
	struct dsi_panel_i2c_cmd_set *set;
	u32 i;
	int rc = 0;
	struct dsi_panel_i2c_cmd *cmd;

	if (!panel)
		return -EINVAL;

	set = &panel->i2c_config.cmd_set;

	if (!panel->i2c_config.i2c_support || !set->count) {
		DSI_DEBUG("[%s] No commands to be sent\n", panel->name);
		return 0;
	}

	for (i = 0; i < set->count; i++) {
		cmd = &set->cmds[i];
		rc = dsi_panel_i2c_tx_cmd(panel, cmd->slave_addr, cmd->data, cmd->len);
		if (rc) {
			DSI_ERR("[%s] failed to send i2c cmd, rc=%d\n", panel->name, rc);
			break;
		}
		if (cmd->post_wait_ms) {
			usleep_range(cmd->post_wait_ms * 1000,
				cmd->post_wait_ms * 1000 + 100);
		}
	}
	return rc;
}

