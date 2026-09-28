// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/io.h>
#include <linux/component.h>
#include "msm_lsr_core.h"
#include "msm_lsr_debug.h"
#include "msm_lsr_res_parse.h"
#include "lsr_hfi.h"
#include "msm_lsr_clocks.h"

#define CLASS_NAME              "lsr"
#define DRIVER_NAME             "lsr"

struct msm_lsr_drv *lsr_driver;

static int msm_lsr_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = NULL;
	int rc = 0;

	if (!dev || !master) {
		dprintk(LSR_ERR, "invalid param(s), dev %pK, master %pK\n", dev, master);
		rc = -EINVAL;
		goto end;
	}

	drm = dev_get_drvdata(master);
	if (!drm) {
		dprintk(LSR_ERR, "invalid drm object\n");
		rc = -EINVAL;
		goto end;
	}

	if (lsr_driver) {
		lsr_driver->drm_dev = drm;
		pr_info("msm_lsr : LSR component bind successful\n");
	} else {
		dprintk(LSR_ERR, "LSR driver init failed\n");
		rc = -EINVAL;
	}

end:
	return rc;
}

static void msm_lsr_unbind(struct device *dev, struct device *master, void *data)
{
	pr_info("msm_lsr : LSR component unbind\n");
}

static const struct component_ops msm_lsr_comp_ops = {
	.bind = msm_lsr_bind,
	.unbind = msm_lsr_unbind,
};

static int read_platform_resources(struct msm_lsr_core *core,
		struct platform_device *pdev)
{
	int rc = 0;

	if (!core || !pdev) {
		dprintk(LSR_ERR, "%s: Invalid params %pK %pK\n", __func__, core, pdev);
		return -EINVAL;
	}

	core->resources.pdev = pdev;
	if (pdev->dev.of_node) {
		rc = lsr_read_platform_resources_from_drv_data(core);
		rc = lsr_read_platform_resources_from_dt(&core->resources);
	} else {
		dprintk(LSR_ERR, "pdev node is NULL\n");
		rc = -EINVAL;
	}
	return rc;
}

static int msm_lsr_initialize_core(struct platform_device *pdev,
				struct msm_lsr_core *core)
{
	int rc = 0;

	if (!core)
		return -EINVAL;
	rc = read_platform_resources(core, pdev);
	if (rc) {
		dprintk(LSR_ERR, "Failed to get platform resources\n");
		return rc;
	}

	mutex_init(&core->lock);
	mutex_init(&core->clk_lock);

	return rc;
}

static const struct of_device_id msm_lsr_plat_match[] = {
	{.compatible = "qcom,msm-lsr"},
	{.compatible = "qcom,msm-lsr,context-bank"},
	{.compatible = "qcom,msm-lsr,bus"},
	{}
};

/*
 * of_node_in_subtree - is @node inside (or equal to) the subtree rooted at @root?
 */
static bool of_node_in_subtree(struct device_node *root, struct device_node *node)
{
	struct device_node *p = of_node_get(node);

	while (p) {
		struct device_node *parent;

		if (p == root) {
			of_node_put(p);
			return true;
		}
		parent = of_get_parent(p);
		of_node_put(p);
		p = parent;
	}

	return false;
}

/*
 * msm_lsr_smmu_group_peers_ready - are the non-LSR peers sharing @group bound?
 * @lsr_np: qcom,msm-lsr node; peers inside its own subtree are not a dependency,
 *	since they are not created (of_platform_populate() has not run yet) or
 *	probed until after this very check succeeds
 * @group: iommu group node shared by @self and, possibly, an external peer
 * @self: context bank node on whose behalf the check is made, for logging
 */
static bool msm_lsr_smmu_group_peers_ready(struct device_node *lsr_np,
		struct device_node *group, struct device_node *self)
{
	struct device_node *peer, *peer_group;
	struct platform_device *pdev;
	bool ready = true;

	for_each_node_with_property(peer, "qcom,iommu-group") {
		if (!of_device_is_available(peer) || of_node_in_subtree(lsr_np, peer))
			continue;

		peer_group = of_parse_phandle(peer, "qcom,iommu-group", 0);
		if (peer_group != group) {
			of_node_put(peer_group);
			continue;
		}
		of_node_put(peer_group);

		pdev = of_find_device_by_node(peer);
		if (pdev) {
			ready = !!platform_get_drvdata(pdev);
			platform_device_put(pdev);
		} else {
			ready = false;
		}

		if (!ready) {
			dprintk(LSR_CORE, "%pOFn: %pOFn sharing iommu group %pOFn not probed yet\n",
					self, peer, group);
			of_node_put(peer);
			break;
		}
	}

	return ready;
}

/**
 * msm_lsr_smmu_shared_group_ready - check non-LSR peers sharing an iommu group
 *	with a child of @np have completed probe
 * @np: parent node whose children carry the qcom,iommu-group phandles
 *
 * Devices placed in the same iommu group share a single SMMU context bank, and
 * the group latches onto a context bank when its first member attaches - which
 * happens in dma_configure(), before that member's probe runs. The display
 * context bank has to win that race: __arm_smmu_alloc_cb() only adopts the
 * context bank pinned by the bootloader for cont-splash when the device
 * initialising the group's domain is the one owning the pinned stream ID, so a
 * group led by any other member ends up on a freshly allocated context bank and
 * the display attach that follows reprograms the pinned S2CR to the wrong one.
 *
 * Children without a qcom,iommu-group phandle, and groups with no non-LSR peer
 * in them, carry no such ordering requirement and are ignored.
 *
 * Return: false if the caller should defer its probe, true otherwise.
 */
static bool msm_lsr_smmu_shared_group_ready(struct device_node *np)
{
	struct device_node *child, *group;
	bool ready = true;

	if (!np)
		return true;

	for_each_available_child_of_node(np, child) {
		group = of_parse_phandle(child, "qcom,iommu-group", 0);
		if (!group)
			continue;

		ready = msm_lsr_smmu_group_peers_ready(np, group, child);
		of_node_put(group);

		if (!ready) {
			of_node_put(child);
			break;
		}
	}

	return ready;
}

static int msm_probe_lsr_device(struct platform_device *pdev)
{
	int rc = 0;
	struct msm_lsr_core *core;

	if (!lsr_driver) {
		dprintk(LSR_ERR, "Invalid lsr driver\n");
		return -EINVAL;
	}

	/*
	 * The LSR context banks share iommu groups, and therefore SMMU context
	 * banks, with the display context banks. A group latches onto a context
	 * bank when its first member attaches, which happens in dma_configure()
	 * before the sub-device probe runs, so waiting for the display context
	 * banks has to happen here - before of_platform_populate() below creates
	 * the sub-devices - rather than in the sub-device probe itself.
	 */
	if (!msm_lsr_smmu_shared_group_ready(pdev->dev.of_node)) {
		dprintk(LSR_CORE, "display smmu not ready, request probe defer\n");
		return -EPROBE_DEFER;
	}

	core = kzalloc(sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	core->platform_data = lsr_get_drv_data(&pdev->dev);
	dev_set_drvdata(&pdev->dev, core);
	rc = msm_lsr_initialize_core(pdev, core);
	if (rc) {
		dprintk(LSR_ERR, "Failed to init core\n");
		goto err_core_init;
	}

	rc = alloc_chrdev_region(&core->dev_num, 0, 1, DRIVER_NAME);
	if (rc < 0) {
		dprintk(LSR_ERR, "alloc_chrdev_region failed: %d\n",
				rc);
		goto err_alloc_chrdev;
	}

#if (KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE)
	core->class = class_create(CLASS_NAME);
#else
	core->class = class_create(THIS_MODULE, CLASS_NAME);
#endif
	if (IS_ERR(core->class)) {
		rc = PTR_ERR(core->class);
		dprintk(LSR_ERR, "class_create failed: %d\n",
				rc);
		goto err_class_create;
	}

	core->dev = device_create(core->class, NULL,
		core->dev_num, NULL, DRIVER_NAME);
	if (IS_ERR(core->dev)) {
		rc = PTR_ERR(core->dev);
		dprintk(LSR_ERR, "device_create failed: %d\n",
				rc);
		goto err_device_create;
	}
	dev_set_drvdata(core->dev, core);

	core->dev_ops = lsr_hfi_initialize(&core->resources);

	if (IS_ERR_OR_NULL(core->dev_ops)) {
		rc = PTR_ERR(core->dev_ops) ?: -EBADHANDLE;
		if (rc != -EPROBE_DEFER)
			dprintk(LSR_ERR, "Failed to create HFI device\n");
		else
			dprintk(LSR_CORE, "request probe defer\n");
		goto err_hfi_initialize;
	}

	mutex_lock(&lsr_driver->lock);
	lsr_driver->lsr_core = core;
	mutex_unlock(&lsr_driver->lock);

	lsr_driver->debugfs_root = msm_lsr_debugfs_init_drv();
	if (!lsr_driver->debugfs_root)
		dprintk(LSR_ERR, "Failed to create debugfs for msm_lsr\n");

	core->debugfs_root = msm_lsr_debugfs_init_core(core, lsr_driver->debugfs_root);
	dprintk(LSR_CORE, "populating sub devices\n");
	/*
	 * Trigger probe for each sub-device i.e. qcom,msm-lsr,context-bank.
	 * When msm_lsr_probe is called for each sub-device, parse the
	 * context-bank details and store it in core->resources.context_banks
	 * list.
	 */
	rc = of_platform_populate(pdev->dev.of_node, msm_lsr_plat_match, NULL, &pdev->dev);
	if (rc) {
		dprintk(LSR_ERR, "Failed to trigger probe for sub-devices\n");
		goto err_fail_sub_device_probe;
	}

	/* Register as component for MSM DRM */
	rc = component_add(&pdev->dev, &msm_lsr_comp_ops);
	if (rc) {
		dprintk(LSR_ERR, "component add failed: %d\n", rc);
		goto err_component_add;
	}

	dprintk(LSR_CORE, "LSR component registration successful\n");
	return rc;

err_component_add:
	of_platform_depopulate(&pdev->dev);

err_fail_sub_device_probe:
	lsr_hfi_deinitialize(core->dev_ops);
	debugfs_remove_recursive(lsr_driver->debugfs_root);
err_hfi_initialize:
	device_destroy(core->class, core->dev_num);
err_device_create:
	class_destroy(core->class);
err_class_create:
	unregister_chrdev_region(core->dev_num, 1);
err_alloc_chrdev:
err_core_init:
	dev_set_drvdata(&pdev->dev, NULL);
	kfree(core);
	return rc;
}

static int msm_lsr_probe_context_bank(struct platform_device *pdev)
{
	return lsr_read_context_bank_resources_from_dt(pdev);
}

static int msm_lsr_probe_bus(struct platform_device *pdev)
{
	return lsr_read_bus_resources_from_dt(pdev);
}

/*
 * Sub devices probe will be triggered by of_platform_populate() towards
 * the end of the probe function after msm-lsr device probe is
 * completed. Return immediately after completing sub-device probe.
 */
static int msm_lsr_probe(struct platform_device *pdev)
{
	if (of_device_is_compatible(pdev->dev.of_node, "qcom,msm-lsr")) {
		return msm_probe_lsr_device(pdev);
	} else if (of_device_is_compatible(pdev->dev.of_node,
		"qcom,msm-lsr,bus")) {
		return msm_lsr_probe_bus(pdev);
	} else if (of_device_is_compatible(pdev->dev.of_node,
		"qcom,msm-lsr,context-bank")) {
		return msm_lsr_probe_context_bank(pdev);
	}

	return -EINVAL;
}

#if (KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE)
static void msm_lsr_remove(struct platform_device *pdev)
#else
static int msm_lsr_remove(struct platform_device *pdev)
#endif
{
	int rc = 0;
	struct msm_lsr_core *core;

	if (!pdev) {
		dprintk(LSR_ERR, "%s invalid input %pK", __func__, pdev);
		rc = -EINVAL;
		goto exit;
	}

	/* Remove component for LSR device */
	if (of_device_is_compatible(pdev->dev.of_node, "qcom,msm-lsr")) {
		component_del(&pdev->dev, &msm_lsr_comp_ops);
		dprintk(LSR_CORE, "LSR component removed\n");
	}

	if (of_device_is_compatible(pdev->dev.of_node, "qcom,msm-lsr"))
		core = dev_get_drvdata(&pdev->dev);
	else
		core = dev_get_drvdata(pdev->dev.parent);

	if (!core) {
		dprintk(LSR_ERR, "%s invalid core", __func__);
		rc = -EINVAL;
		goto exit;
	}

	lsr_hfi_deinitialize(core->dev_ops);
	msm_lsr_free_platform_resources(&core->resources);
	dev_set_drvdata(&pdev->dev, NULL);
	mutex_destroy(&core->lock);
	mutex_destroy(&core->clk_lock);
	kfree(core);
exit:
#if (KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE)
	return;
#else
	return rc;
#endif
}

MODULE_DEVICE_TABLE(of, msm_lsr_plat_match);

static struct platform_driver msm_lsr_driver = {
	.probe = msm_lsr_probe,
	.remove = msm_lsr_remove,
	.driver = {
		.name = "msm_lsr",
		.of_match_table = msm_lsr_plat_match,
	},
};

void __init msm_lsr_init(void)
{
	int rc = 0;
	struct msm_lsr_core *core = NULL;
	struct lsr_hfi_ops *ops_tbl;

	lsr_driver = kzalloc(sizeof(*lsr_driver), GFP_KERNEL);
	if (!lsr_driver) {
		dprintk(LSR_ERR, "Failed to allocate memory for msm_lsr_drv\n");
		return;
	}

	mutex_init(&lsr_driver->lock);
	rc = platform_driver_register(&msm_lsr_driver);
	if (rc) {
		dprintk(LSR_ERR, "Failed to register LSR platform driver\n");
		kfree(lsr_driver);
		lsr_driver = NULL;
		return;
	}

	core = lsr_driver->lsr_core;
	if (!core) {
		dprintk(LSR_ERR, "%s LSR core not initialized\n", __func__);
		return;
	}

	ops_tbl = core->dev_ops;
	rc = iris_hfi_core_init(ops_tbl->hfi_device_data);
	if (rc) {
		dprintk(LSR_ERR, "Failed to move lsr instance to init state\n");
		return;
	}
}

void __exit msm_lsr_exit(void)
{
	platform_driver_unregister(&msm_lsr_driver);
	debugfs_remove_recursive(lsr_driver->debugfs_root);
	mutex_destroy(&lsr_driver->lock);
	kfree(lsr_driver);
	lsr_driver = NULL;
}

MODULE_SOFTDEP("pre: synx-driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(DMA_BUF);
