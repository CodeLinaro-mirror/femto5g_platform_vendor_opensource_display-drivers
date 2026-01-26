// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/version.h>
#include <drm/drm_bridge.h>
#include <drm/drm_modes.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include "dsi_drm.h"
#include "dsi_sim_bridge.h"

struct dsi_sim_bridge {
	struct drm_bridge bridge;
	struct device *dev;
	struct i2c_client *client;
	struct device_node *host_node;
	struct mipi_dsi_device *dsi;
};

#if (KERNEL_VERSION(6, 16, 0) > LINUX_VERSION_CODE)
static int dsi_sim_bridge_attach(struct drm_bridge *bridge,
		enum drm_bridge_attach_flags flags)
#else
static int dsi_sim_bridge_attach(struct drm_bridge *bridge,
		struct drm_encoder *encoder, enum drm_bridge_attach_flags flags)
#endif
{
	struct dsi_sim_bridge *pdata;
	struct mipi_dsi_host *host;
	struct mipi_dsi_device *dsi;
	int ret;
	const struct mipi_dsi_device_info info = {
		.type = "dsi-sim-bridge",
		.channel = 0,
		.node = NULL,
	};

	if (!bridge) {
		DSI_ERR("invalid bridge pointer\n");
		return -EINVAL;
	}

	/* Get bridge private data */
	pdata = container_of(bridge, struct dsi_sim_bridge, bridge);

	/* Find DSI host */
	host = of_find_mipi_dsi_host_by_node(pdata->host_node);
	if (!host) {
		DSI_ERR("failed to find DSI host\n");
		return -EPROBE_DEFER;
	}

	/* Register DSI device */
	dsi = devm_mipi_dsi_device_register_full(pdata->dev, host, &info);
	if (IS_ERR(dsi)) {
		ret = PTR_ERR(dsi);
		DSI_ERR("failed to register DSI device, ret=%d\n", ret);
		return ret;
	}

	/* Configure DSI parameters */
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO;

	/* Attach to DSI host */
	ret = devm_mipi_dsi_attach(pdata->dev, dsi);
	if (ret < 0) {
		DSI_ERR("failed to attach DSI device to host, ret=%d\n", ret);
		return ret;
	}

	pdata->dsi = dsi;
	DSI_DEBUG("DSI sim bridge attached\n");
	return 0;
}

static bool dsi_sim_bridge_mode_fixup(struct drm_bridge *bridge,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	if (!bridge || !mode || !adjusted_mode) {
		DSI_ERR("invalid parameters\n");
		return false;
	}

	DSI_DEBUG("DSI sim bridge mode fixup passed\n");
	return true;
}

static void dsi_sim_bridge_mode_set(struct drm_bridge *bridge,
		const struct drm_display_mode *mode,
		const struct drm_display_mode *adj_mode)
{
	if (!bridge || !adj_mode) {
		DSI_ERR("invalid parameters\n");
		return;
	}

	DSI_DEBUG("DSI sim bridge mode set for %dx%d, %dHZ, clock %d\n",
		 adj_mode->hdisplay, adj_mode->vdisplay,
		 drm_mode_vrefresh(adj_mode), adj_mode->clock);
}

static void dsi_sim_bridge_pre_enable(struct drm_bridge *bridge)
{
	if (!bridge) {
		DSI_ERR("invalid bridge pointer\n");
		return;
	}

	DSI_DEBUG("DSI sim bridge pre enabled\n");
}

static void dsi_sim_bridge_enable(struct drm_bridge *bridge)
{
	if (!bridge) {
		DSI_ERR("invalid bridge pointer\n");
		return;
	}

	DSI_DEBUG("DSI sim bridge enabled\n");
}

static void dsi_sim_bridge_disable(struct drm_bridge *bridge)
{
	if (!bridge) {
		DSI_ERR("invalid bridge pointer\n");
		return;
	}

	DSI_DEBUG("DSI sim bridge disabled\n");
}

static void dsi_sim_bridge_post_disable(struct drm_bridge *bridge)
{
	if (!bridge) {
		DSI_ERR("invalid bridge pointer\n");
		return;
	}

	DSI_DEBUG("DSI sim bridge post disabled\n");
}

static const struct drm_bridge_funcs dsi_sim_bridge_funcs = {
	.attach = dsi_sim_bridge_attach,
	.mode_fixup = dsi_sim_bridge_mode_fixup,
	.mode_set = dsi_sim_bridge_mode_set,
	.pre_enable = dsi_sim_bridge_pre_enable,
	.enable = dsi_sim_bridge_enable,
	.disable = dsi_sim_bridge_disable,
	.post_disable = dsi_sim_bridge_post_disable,
};

#if (KERNEL_VERSION(6, 3, 0) <= LINUX_VERSION_CODE)
static int dsi_sim_bridge_probe(struct i2c_client *client)
#else
static int dsi_sim_bridge_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct dsi_sim_bridge *pdata;
	struct device_node *end_node;

	if (!client || !client->dev.of_node) {
		DSI_ERR("invalid input\n");
		return -EINVAL;
	}

	pdata = devm_kzalloc(&client->dev, sizeof(struct dsi_sim_bridge), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	end_node = of_graph_get_endpoint_by_regs(client->dev.of_node, 0, 0);
	if (!end_node) {
		DSI_ERR("can't find remote endpoint\n");
		return -ENODEV;
	}

	pdata->host_node = of_graph_get_remote_port_parent(end_node);
	of_node_put(end_node);
	if (!pdata->host_node) {
		DSI_ERR("can't find remote port parent\n");
		return -ENODEV;
	}
	of_node_put(pdata->host_node);

	pdata->dev = &client->dev;
	pdata->client = client;

	i2c_set_clientdata(client, pdata);
	dev_set_drvdata(&client->dev, pdata);

	pdata->bridge.of_node = client->dev.of_node;
	pdata->bridge.funcs = &dsi_sim_bridge_funcs;

	drm_bridge_add(&pdata->bridge);

	DSI_DEBUG("DSI sim bridge probed\n");

	return 0;
}

static void dsi_sim_bridge_remove(struct i2c_client *client)
{
	struct dsi_sim_bridge *pdata = i2c_get_clientdata(client);

	if (!pdata)
		return;

	drm_bridge_remove(&pdata->bridge);
	DSI_DEBUG("DSI sim bridge removed\n");
}

static const struct of_device_id dsi_sim_bridge_match_table[] = {
	{.compatible = "qcom,dsi-sim-bridge"},
	{}
};
MODULE_DEVICE_TABLE(of, dsi_sim_bridge_match_table);

static const struct i2c_device_id dsi_sim_bridge_id[] = {
	{ "dsi-sim-bridge", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, dsi_sim_bridge_id);

static struct i2c_driver dsi_sim_bridge_driver = {
	.driver = {
		.name = "dsi-sim-bridge",
		.of_match_table = dsi_sim_bridge_match_table,
	},
	.probe = dsi_sim_bridge_probe,
	.remove = dsi_sim_bridge_remove,
	.id_table = dsi_sim_bridge_id,
};

int dsi_sim_bridge_register(void)
{
	return i2c_add_driver(&dsi_sim_bridge_driver);
}

void dsi_sim_bridge_unregister(void)
{
	i2c_del_driver(&dsi_sim_bridge_driver);
}
