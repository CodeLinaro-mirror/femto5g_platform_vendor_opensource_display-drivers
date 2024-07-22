// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

#include <umdp/umd_power.h>

#define DEVICE_NAME	"umd_power"

struct power_clk {
	struct clk *clk;
	char clk_name[32];
	u32 rate[UMD_POWER_CNT];
};

struct power_reg {
	struct regulator *vreg;
	char vreg_name[32];
	u32 min_volt[UMD_POWER_CNT];
	u32 max_volt[UMD_POWER_CNT];
	u32 load[UMD_POWER_CNT];
};

struct power_entry {
	struct list_head node;
	u32 gpid;

	int num_clk;
	struct power_clk *clk_config;

	int num_reg;
	struct power_reg *reg_config;
};

struct umdp_ctrl {
	struct list_head ph;
	struct class *class;
	struct device *dev;
	int major;
};

static struct umdp_ctrl *umdp_ctrl;

static unsigned long pll_clk_recalc_rate_stub(struct clk_hw *hw, unsigned long rate)
{
	return 0;
}

static long pll_clk_round_rate_stub(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	return 0;
}

static int pll_clk_set_rate_stub(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate)
{
	return 0;
}

static int pll_clk_prepare_stub(struct clk_hw *hw)
{
	return 0;
}

static void pll_clk_unprepare_stub(struct clk_hw *hw)
{
}

static const struct clk_ops pll_clk_ops = {
	.recalc_rate = pll_clk_recalc_rate_stub,
	.set_rate = pll_clk_set_rate_stub,
	.round_rate = pll_clk_round_rate_stub,
	.prepare = pll_clk_prepare_stub,
	.unprepare = pll_clk_unprepare_stub,
};

static long umpd_get_status(unsigned long arg)
{
	/* pending done */
	return 0;
}

static long umpd_set_status(unsigned long arg)
{
	/* pending done */
	return 0;
}

static long umpd_get_supported_mask(unsigned long arg)
{
	/* pending done */
	return 0;
}

static long umd_power_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	switch (cmd) {
	case UMDP_GET_STATUS:
		return umpd_get_status(arg);

	case UMDP_SET_STATUS:
		return umpd_set_status(arg);

	case UMDP_GET_SUPPORTED_MASK:
		return umpd_get_supported_mask(arg);
	}

	return -ENOTTY;
}

static int umd_power_open(struct inode *inode, struct file *file)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	/*
	 * nothing special to do here
	 * We do accept multiple open files at the same time as we
	 * synchronize on the per call operation.
	 */
	return 0;
}

static int umd_power_close(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations umd_power_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= umd_power_ioctl,
	.open		= umd_power_open,
	.release	= umd_power_close,
	.llseek		= no_llseek,
};

static int umd_clock_enable_level_default(struct clk *clk, const char *clk_name, u32 clk_rate_L0)
{
	int rc = 0;

	if (clk_rate_L0) {
		rc = clk_set_rate(clk, clk_rate_L0);
		if (rc) {
			pr_err("%s set rate %d failed\n", clk_name, clk_rate_L0);
			return rc;
		}
	}

	rc = clk_prepare_enable(clk);
	if (rc) {
		pr_err("enable failed for %s: rc(%d)\n", clk_name, rc);
		return rc;
	}

	return rc;
}

static int umd_regulator_enable_level_default(struct regulator *reg, const char *reg_name,
			u32 reg_min_volt, u32 reg_max_volt, u32 reg_load)
{
	int rc = 0;

	if (regulator_count_voltages(reg) > 0) {
		rc = regulator_set_voltage(reg, reg_min_volt, reg_max_volt);
		if (rc) {
			pr_err("%s set voltage[%d,%d] failed\n", reg_name, reg_min_volt,
								reg_max_volt);
			return rc;
		}

		rc = regulator_set_load(reg, reg_load);
		if (rc) {
			pr_err("%s set load %d failed\n", reg_name, reg_load);
			return rc;
		}
	}

	rc = regulator_enable(reg);
	if (rc) {
		pr_err("%s regulator_enable failed\n", reg_name);
		return rc;
	}

	return rc;
}

static int pll_clock_register(struct platform_device *pdev)
{
	int i = 0, rc = 0;
	struct clk *clk;
	struct clk_onecell_data *clk_data;
	struct clk_init_data init;
	struct clk_hw *hw;
	const char *pll_name;
	char name[64];
	int num_pll = 0;

	memset(&init, 0, sizeof(init));

	init.ops = &pll_clk_ops;

	num_pll = of_property_count_strings(pdev->dev.of_node, "pll-names");
	if (num_pll <= 0) {
		pr_err("pll clocks are not defined\n");
		return -EINVAL;
	}

	clk_data = devm_kzalloc(&pdev->dev, sizeof(struct clk_onecell_data),
					GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->clks = devm_kzalloc(&pdev->dev, (num_pll *
				sizeof(struct clk *)), GFP_KERNEL);
	if (!clk_data->clks)
		return -ENOMEM;

	for (i = 0; i < num_pll; i++) {
		rc = of_property_read_string_index(pdev->dev.of_node, "pll-names", i, &pll_name);
		if (rc) {
			pr_err("Failed to get pll clock[%d]=%s\n", i, pll_name);
			goto clk_register_err;
		}

		strscpy(name, pll_name, sizeof(name));
		init.name = name;

		hw = devm_kzalloc(&pdev->dev, sizeof(*hw), GFP_KERNEL);
		if (!hw)
			return -ENOMEM;

		hw->init = &init;
		clk = devm_clk_register(&pdev->dev, hw);
		if (IS_ERR(clk)) {
			pr_err("%s registration failed for umd pll clock\n", pll_name);
			rc = -EINVAL;
			goto clk_register_err;
		}

		clk_data->clks[i] = clk;
	}

	rc = of_clk_add_provider(pdev->dev.of_node,
				of_clk_src_onecell_get, clk_data);

	if (!rc) {
		pr_err("Registered pll clocks successfully\n");
		return rc;
	}

clk_register_err:
	return rc;
}

static int umd_power_parse_dt(struct platform_device *pdev, struct umdp_ctrl *umdp_ctrl)
{
	struct device *dev = &pdev->dev;
	struct device_node *child = dev->of_node;
	struct power_entry *pe;
	int i = 0, rc = 0;

	const char *clock_name;
	u32 clock_rate = 0;
	int num_clk = 0;

	const char *reg_name;
	u32 rgltr_min_volt = 0;
	u32 rgltr_max_volt = 0;
	u32 load_uA = 0;
	int num_reg = 0;

	pe = devm_kzalloc(dev, sizeof(*pe), GFP_KERNEL);
	if (!pe)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, pe);

	rc = of_property_read_u32(child, "reg", &pe->gpid);

	num_clk = of_property_count_strings(child, "clock-names");
	if (num_clk <= 0) {
		pr_err("clocks are not defined\n");
		rc = -EINVAL;
		return rc;
	}
	pe->num_clk = num_clk;

	num_reg = of_property_count_strings(child, "regulator-names");
	if (num_reg <= 0) {
		pr_err("regulators are not defined\n");
		rc = -EINVAL;
		return rc;
	}
	pe->num_reg = num_reg;

	pe->clk_config = devm_kzalloc(dev,
			sizeof(struct power_clk) * num_clk, GFP_KERNEL);
	if (!pe->clk_config) {
		rc = -ENOMEM;
		pe->num_clk = 0;
		return rc;
	}

	pe->reg_config = devm_kzalloc(dev,
			sizeof(struct power_reg) * num_reg, GFP_KERNEL);
	if (!pe->reg_config) {
		rc = -ENOMEM;
		pe->num_reg = 0;
		return rc;
	}

	for (i = 0; i < num_reg; i++) {
		rc = of_property_read_string_index(child, "regulator-names",
				i, &reg_name);
		if (!rc)
			strscpy(pe->reg_config[i].vreg_name, reg_name,
					sizeof(pe->reg_config[i].vreg_name));

		rc = of_property_read_u32_index(child, "rgltr-min-voltage",
				i, &rgltr_min_volt);
		if (!rc)
			pe->reg_config[i].min_volt[UMD_POWER_ON] = rgltr_min_volt;

		rc = of_property_read_u32_index(child, "rgltr-max-voltage",
				i, &rgltr_max_volt);
		if (!rc)
			pe->reg_config[i].max_volt[UMD_POWER_ON] = rgltr_max_volt;

		rc = of_property_read_u32_index(child, "rgltr-load-current",
				i, &load_uA);
		if (!rc)
			pe->reg_config[i].load[UMD_POWER_ON] = load_uA;

		pe->reg_config[i].vreg = devm_regulator_get(dev, reg_name);
		rc = PTR_ERR_OR_ZERO(pe->reg_config[i].vreg);
		if (rc) {
			pr_err("get regulator %s failed. rc=%d\n", reg_name, rc);
			return rc;
		}

		rc = umd_regulator_enable_level_default(pe->reg_config[i].vreg, reg_name,
					rgltr_min_volt, rgltr_max_volt, load_uA);
		if (rc) {
			pr_err("reg %s enable level default failed.\n", reg_name);
			return rc;
		}
	}

	for (i = 0; i < num_clk; i++) {
		rc = of_property_read_string_index(child, "clock-names", i, &clock_name);
		if (!rc)
			strscpy(pe->clk_config[i].clk_name, clock_name,
					sizeof(pe->clk_config[i].clk_name));

		rc = of_property_read_u32_index(child, "clock-rate", i, &clock_rate);
		if (!rc)
			pe->clk_config[i].rate[UMD_POWER_ON] = clock_rate;

		rc = of_property_read_u32_index(child, "clock-rate-L1", i, &clock_rate);
		if (!rc)
			pe->clk_config[i].rate[UMD_POWER_L1] = clock_rate;

		rc = of_property_read_u32_index(child, "clock-rate-L2", i, &clock_rate);
		if (!rc)
			pe->clk_config[i].rate[UMD_POWER_L2] = clock_rate;

		rc = of_property_read_u32_index(child, "clock-rate-L3", i, &clock_rate);
		if (!rc)
			pe->clk_config[i].rate[UMD_POWER_L3] = clock_rate;

		rc = of_property_read_u32_index(child, "clock-max-rate", i, &clock_rate);
		if (!rc)
			pe->clk_config[i].rate[UMD_POWER_MAX] = clock_rate;

		pe->clk_config[i].clk = devm_clk_get(dev, clock_name);
		rc = PTR_ERR_OR_ZERO(pe->clk_config[i].clk);
		if (rc) {
			pr_err("get clock %s failed. rc=%d, Clock Addr = 0x%x\n", clock_name,
							rc, pe->clk_config[i].clk);
			return rc;
		}

		rc = umd_clock_enable_level_default(pe->clk_config[i].clk, clock_name,
							pe->clk_config[i].rate[UMD_POWER_ON]);
		if (rc) {
			pr_err("clk %s enable level default failed.\n", clock_name);
			return rc;
		}
	}

	if (umdp_ctrl)
		list_add_tail(&pe->node, &umdp_ctrl->ph);

	return rc;
}

static int umd_power_suspend(struct device *dev)
{
	struct power_entry *pe;
	int i = 0, ret = 0;

	if (of_device_is_compatible(dev->of_node, "qcom,umd-power"))
		return 0;

	pe = dev_get_drvdata(dev);
	if (!pe)
		return -ENODATA;

	for (i = 0; i < pe->num_clk; i++) {
		if (pe->clk_config[i].clk)
			clk_disable_unprepare(pe->clk_config[i].clk);
	}

	for (i = 0; i < pe->num_reg; i++) {
		if (pe->reg_config[i].vreg) {
			ret = regulator_disable(pe->reg_config[i].vreg);
			if (ret) {
				pr_err("%s regulator_disable failed\n",
						pe->reg_config[i].vreg_name);
				return ret;
			}
		}
	}

	return 0;
}

static int umd_power_resume(struct device *dev)
{
	struct power_entry *pe;
	int i = 0, ret = 0;

	if (of_device_is_compatible(dev->of_node, "qcom,umd-power"))
		return 0;

	pe = dev_get_drvdata(dev);
	if (!pe)
		return -ENODATA;

	for (i = 0; i < pe->num_reg; i++) {
		ret = umd_regulator_enable_level_default(pe->reg_config[i].vreg,
						pe->reg_config[i].vreg_name,
						pe->reg_config[i].min_volt[UMD_POWER_ON],
						pe->reg_config[i].max_volt[UMD_POWER_ON],
						pe->reg_config[i].load[UMD_POWER_ON]);
		if (ret) {
			pr_err("reg %s enable level default failed\n",
						pe->reg_config[i].vreg_name);
			return ret;
		}
	}

	for (i = 0; i < pe->num_clk; i++) {
		ret = umd_clock_enable_level_default(pe->clk_config[i].clk,
						pe->clk_config[i].clk_name,
						pe->clk_config[i].rate[UMD_POWER_ON]);
		if (ret) {
			pr_err("clk %s enable level default failed\n",
						pe->clk_config[i].clk_name);
			continue;
		}
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(umd_pm_ops, umd_power_suspend, umd_power_resume);

static const struct of_device_id umdp_match[] = {
	{ .compatible = "qcom,umd-power", },
	{ .compatible = "qcom,umd-power-multimedia", },
	{}
};

static int umd_power_probe(struct platform_device *pdev)
{
	struct umdp_ctrl *cdev_ctrl;
	int ret = 0;

	pr_info("%s: pdev=0x%x, dev name %s\n", __func__, pdev, dev_name(&pdev->dev));

	if (of_device_is_compatible(pdev->dev.of_node, "qcom,umd-power-multimedia")) {
		ret = umd_power_parse_dt(pdev, umdp_ctrl);
		if (ret) {
			pr_err("%s device tree parsing failed\n", dev_name(&pdev->dev));
			return ret;
		}
		return ret;
	}

	cdev_ctrl = devm_kzalloc(&pdev->dev, sizeof(*cdev_ctrl), GFP_KERNEL);
	if (!cdev_ctrl)
		return -ENOMEM;

	umdp_ctrl = cdev_ctrl;

	INIT_LIST_HEAD(&cdev_ctrl->ph);
	dev_set_drvdata(&pdev->dev, cdev_ctrl);

	ret = pll_clock_register(pdev);
	if (ret)
		pr_err("pll_clock_register failed, rc = %d\n", ret);

	cdev_ctrl->major = register_chrdev(0, DEVICE_NAME, &umd_power_fops);
	if (cdev_ctrl->major < 0) {
		pr_err("Unable to register chrdev: %d\n", cdev_ctrl->major);
		return -EFAULT;
	}

	cdev_ctrl->class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(cdev_ctrl->class)) {
		ret = PTR_ERR(cdev_ctrl->class);
		pr_err("Unable to create class: %d\n", ret);
		goto err_create_class;
	}

	cdev_ctrl->dev = device_create(cdev_ctrl->class, NULL,
				MKDEV(cdev_ctrl->major, 0), NULL, DEVICE_NAME);
	if (IS_ERR(cdev_ctrl->dev)) {
		ret = PTR_ERR(cdev_ctrl->dev);
		pr_err("Unable to create device: %d\n", ret);
		goto err_create_device;
	}

	ret = of_platform_populate(pdev->dev.of_node, umdp_match,
								NULL, &pdev->dev);
	if (ret) {
		pr_err("failed to populate child nodes\n");
		return ret;
	}
	goto out;

err_create_device:
	class_destroy(cdev_ctrl->class);
err_create_class:
	unregister_chrdev(cdev_ctrl->major, DEVICE_NAME);
out:
	return ret;
}

static int umd_power_remove(struct platform_device *pdev)
{
	if (of_device_is_compatible(pdev->dev.of_node, "qcom,umd-power")) {
		struct umdp_ctrl *cdev_ctrl = dev_get_drvdata(&pdev->dev);

		device_destroy(cdev_ctrl->class, MKDEV(MAJOR(cdev_ctrl->major), 0));
		class_destroy(cdev_ctrl->class);
		unregister_chrdev(cdev_ctrl->major, DEVICE_NAME);
	}

	return 0;
}

static struct platform_driver umdp_driver = {
	.driver = {
		.name		= DEVICE_NAME,
		.of_match_table = umdp_match,
		.pm = &umd_pm_ops,
	},
	.probe = umd_power_probe,
	.remove = umd_power_remove,
};

module_platform_driver(umdp_driver);

MODULE_DEVICE_TABLE(of, umdp_match);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("UMD Power Driver");

