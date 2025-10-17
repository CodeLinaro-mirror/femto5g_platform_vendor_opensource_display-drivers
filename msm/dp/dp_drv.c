// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/component.h>
#include <linux/of_irq.h>
#include <linux/delay.h>
#include <linux/usb/phy.h>
#include <linux/jiffies.h>
#include <linux/pm_qos.h>
#include <linux/ipc_logging.h>
#include <linux/version.h>

#include "sde_connector.h"

#include "dp_drv.h"
#include "dp_debug.h"
#include "dp_client.h"

#define MAX_DP_ACTIVE_DISPLAY 8
#define HPD_STRING_SIZE 30

struct dp_drv_type_info {
	int display_type;
};

struct dp_drv_pair g_edp_pair;

static struct dp_drv *g_dp_drv[MAX_DP_ACTIVE_DISPLAY];

static const struct dp_drv_type_info dp_info = {
	.display_type = DRM_MODE_CONNECTOR_DisplayPort,
};

static const struct dp_drv_type_info edp_info = {
	.display_type = DRM_MODE_CONNECTOR_eDP,
};

static const struct of_device_id dp_dt_match[] = {
	{ .compatible = "qcom,dp-display",
	  .data = &dp_info,},
	{ .compatible = "qcom,edp-display",
	  .data = &edp_info,},
	{}
};

static int dp_drv_bind(struct device *dev, struct device *master, void *data)
{
	int rc = 0;
	struct dp_drv *dp = NULL;
	struct drm_device *drm = NULL;
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_drm_private *priv = NULL;
	int index;
	const char *op_mode;

	if (!dev || !pdev || !master) {
		DP_ERR("invalid param(s), dev %pK, pdev %pK, master %pK\n",
				dev, pdev, master);
		rc = -EINVAL;
		goto end;
	}

	drm = dev_get_drvdata(master);
	if (!drm) {
		DP_ERR("invalid drm object\n");
		rc = -EINVAL;
		goto end;
	}

	priv = drm->dev_private;
	if (!priv) {
		DP_ERR("invalid param(s)\n");
		rc = -EINVAL;
		goto end;
	}

	op_mode = of_get_property(dev->of_node, "qcom,op-label", NULL);
	if (op_mode && strlen(op_mode) &&
		((IS_DISP_OP_HWIO(priv->disp_op) && strcmp(op_mode, "hwio")) ||
		(IS_DISP_OP_HFI(priv->disp_op) && strcmp(op_mode, "hfi")))) {
		DP_DEBUG("skip mode: %s\n", op_mode);
		goto end;
	}

	dp = platform_get_drvdata(pdev);
	if (!drm || !dp) {
		DP_ERR("invalid param(s), drm %pK, dp %pK\n", drm, dp);
		rc = -EINVAL;
		goto end;
	}

	dp->drm_dev = drm;

	index = dp_drv_get_num_of_displays(NULL);
	if (index >= MAX_DP_ACTIVE_DISPLAY) {
		DP_ERR("exceeds max dp count\n");
		rc = -EINVAL;
		goto end;
	}

	if (IS_DISP_OP_HWIO(priv->disp_op))
		dp->client = dp_mgr_init(pdev);

	if (dp->client == NULL) {
		DP_ERR("Error initializing HWIO DP");
		goto end;
	}

	dp->client->bind(dev, master, dp->client);
	g_dp_drv[index] = dp;
end:
	return rc;
}

static void dp_drv_unbind(struct device *dev, struct device *master, void *data)
{
	struct dp_drv *dp = NULL;
	struct drm_device *drm = NULL;
	struct msm_drm_private *priv = NULL;
	struct platform_device *pdev = to_platform_device(dev);

	if (!dev || !pdev || !master) {
		DP_ERR("invalid param(s), dev %pK, pdev %pK, master %pK\n",
				dev, pdev, master);
		return;
	}

	dp = platform_get_drvdata(pdev);
	if (!dp) {
		DP_ERR("invalid platform drvdata\n");
		return;
	}

	drm = dev_get_drvdata(master);
	if (!drm) {
		DP_ERR("invalid master drvdata\n");
		return;
	}

	priv = drm->dev_private;
	if (!priv) {
		DP_ERR("invalid disp_op\n");
		return;
	}

	dp->client->unbind(dev, master, dp->client);

	if (IS_DISP_OP_HFI(priv->disp_op))
		dp_mgr_deinit(pdev);
}

static const struct component_ops dp_drv_comp_ops = {
	.bind = dp_drv_bind,
	.unbind = dp_drv_unbind,
};

int dp_drv_get_displays(struct drm_device *dev, void **displays, int count)
{
	int i, j;

	if (!displays) {
		DP_ERR("invalid data\n");
		return -EINVAL;
	}

	for (i = 0, j = 0; i < MAX_DP_ACTIVE_DISPLAY && j < count; i++) {
		if (!g_dp_drv[i])
			break;

		if (g_dp_drv[i]->drm_dev == dev) {
			displays[j] = g_dp_drv[i];
			j++;
		}
	}

	return j;
}

int edp_drv_get_num_of_displays(struct drm_device *dev)
{
	int i, j;

	for (i = 0, j = 0; i < MAX_DP_ACTIVE_DISPLAY; i++) {
		if (!g_dp_drv[i])
			break;

		if ((g_dp_drv[i]->drm_dev == dev) && g_dp_drv[i]->client->is_edp)
			j++;
	}

	return j;
}

int dp_drv_get_num_of_displays(struct drm_device *dev)
{
	int i, j;

	for (i = 0, j = 0; i < MAX_DP_ACTIVE_DISPLAY; i++) {
		if (!g_dp_drv[i])
			break;

		if (!dev || g_dp_drv[i]->drm_dev == dev)
			j++;
	}

	return j;
}

int dp_drv_get_num_of_streams(struct drm_device *dev)
{
	struct dp_drv *dp;
	int i, count = 0;
	struct dp_intf_info *dp_info = NULL;

	for (i = 0; i < MAX_DP_ACTIVE_DISPLAY; i++) {
		if (!g_dp_drv[i])
			break;

		if (g_dp_drv[i]->drm_dev != dev)
			continue;

		dp = g_dp_drv[i];

		dp_info = dp->client->get_intf_info(dp->client);
		if (dp_info)
			count += dp_info->stream_cnt;
	}

	return count;
}

struct dp_intf_info *dp_drv_get_info(void *drv)
{
	struct dp_drv *dp;

	dp = drv;

	if (!dp) {
		DP_DEBUG("dp display not initialized\n");
		return NULL;
	}

	return dp->client->get_intf_info(dp->client);
}

#if (KERNEL_VERSION(6, 10, 0) <= LINUX_VERSION_CODE)
static void dp_drv_remove(struct platform_device *pdev)
#else
static int dp_drv_remove(struct platform_device *pdev)
#endif
{
	int rc = 0;
	struct dp_drv *dp;

	if (!pdev) {
		rc = -EINVAL;
		goto end;
	}

	dp = platform_get_drvdata(pdev);
end:
#if (KERNEL_VERSION(6, 10, 0) <= LINUX_VERSION_CODE)
	return;
#else
	return rc;
#endif
}

static int dp_pm_prepare(struct device *dev)
{
	struct dp_drv *dp;

	if (!dev)
		return -EINVAL;

	dp = dev_get_drvdata(dev);

	return dp->client->pm_prepare(dp->client);
}

static void dp_pm_complete(struct device *dev)
{
	struct dp_drv *dp;

	if (!dev)
		return;

	dp = dev_get_drvdata(dev);

	dp->client->pm_complete(dp->client);
}

static int dp_drv_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct dp_drv *dp;
	const struct of_device_id *id;

	if (!pdev || !pdev->dev.of_node) {
		DP_ERR("pdev not found\n");
		rc = -ENODEV;
		goto bail;
	}

	id = of_match_node(dp_dt_match, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	dp = devm_kzalloc(&pdev->dev, sizeof(*dp), GFP_KERNEL);
	if (!dp) {
		rc = -ENOMEM;
		goto bail;
	}
	dp->pdev = pdev;

	platform_set_drvdata(pdev, dp);

	rc = component_add(&pdev->dev, &dp_drv_comp_ops);
	if (rc) {
		DP_ERR("component add failed, rc=%d\n", rc);
		goto bail;
	}

	return 0;
bail:
	return rc;
}

void *get_ipc_log_context(void)
{
	if (g_dp_drv[0] && g_dp_drv[0]->client &&
		g_dp_drv[0]->client->dp_ipc_log)
		return g_dp_drv[0]->client->dp_ipc_log;

	return NULL;
}

static const struct dev_pm_ops dp_pm_ops = {
	.prepare = dp_pm_prepare,
	.complete = dp_pm_complete,
};

static struct platform_driver dp_driver = {
	.probe  = dp_drv_probe,
	.remove = dp_drv_remove,
	.driver = {
		.name = "msm-dp-display",
		.of_match_table = dp_dt_match,
		.suppress_bind_attrs = true,
		.pm = &dp_pm_ops,
	},
};

void __init dp_drv_register(void)
{
	dp_pll_drv_register();
	platform_driver_register(&dp_driver);
}

void __exit dp_drv_unregister(void)
{
	platform_driver_unregister(&dp_driver);
	dp_pll_drv_unregister();
}
