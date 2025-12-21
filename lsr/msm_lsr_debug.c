// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/debugfs.h>
#include "msm_lsr_debug.h"
#include "lsr_core.h"
#include "lsr_hfi.h"

#define MAX_SSR_STRING_LEN 10

int msm_lsr_debug = !1;
EXPORT_SYMBOL_GPL(msm_lsr_debug);

int msm_lsr_debug_out = LSR_OUT_PRINTK;
EXPORT_SYMBOL_GPL(msm_lsr_debug_out);

int msm_lsr_fw_debug = 0x80003F;
int msm_lsr_fw_low_power_mode = !1;

bool msm_lsr_auto_pil = true;

int msm_lsr_fw_debug_mode = 1;
bool msm_lsr_fw_coverage = !true;
bool msm_lsr_cacheop_enabled = true;
bool msm_lsr_cacheop_disabled = !true;
int msm_lsr_clock_voting = !1;
bool msm_lsr_syscache_disable = true;

bool msm_lsr_dcvs_disable = !true;
int msm_lsr_hw_wd_recovery = 1;
int msm_lsr_smmu_fault_recovery = !1;

#define MAX_DBG_BUF_SIZE 4096

struct dentry *msm_lsr_debugfs_init_drv(void)
{
	struct dentry *dir = NULL;

	dir = debugfs_create_dir("msm_lsr", NULL);
	if (IS_ERR_OR_NULL(dir)) {
		dir = NULL;
		goto failed_create_dir;
	}

	debugfs_create_x32("debug_level", 0644, dir, &msm_lsr_debug);
	debugfs_create_x32("fw_level", 0644, dir, &msm_lsr_fw_debug);
	debugfs_create_u32("fw_debug_mode", 0644, dir, &msm_lsr_fw_debug_mode);
	debugfs_create_u32("fw_low_power_mode", 0644, dir,
		&msm_lsr_fw_low_power_mode);
	debugfs_create_u32("debug_output", 0644, dir, &msm_lsr_debug_out);
	debugfs_create_bool("fw_coverage", 0644, dir, &msm_lsr_fw_coverage);
	debugfs_create_bool("auto_pil", 0644, dir, &msm_lsr_auto_pil);
	debugfs_create_bool("enable_cacheop", 0644, dir,
			&msm_lsr_cacheop_enabled);
	debugfs_create_bool("disable_lsr_syscache", 0644, dir,
			&msm_lsr_syscache_disable);
	debugfs_create_bool("disable_dcvs", 0644, dir,
			&msm_lsr_dcvs_disable);

	return dir;

failed_create_dir:
	if (dir)
		debugfs_remove_recursive(lsr_driver->debugfs_root);

	dprintk(LSR_WARN, "Failed to create debugfs\n");
	return NULL;
}

static int _clk_rate_set(void *data, u64 val)
{
	struct msm_lsr_core *core;
	struct lsr_hfi_ops *ops_tbl;
	struct allowed_clock_rates_table *tbl = NULL;
	unsigned int tbl_size, i;

	core = lsr_driver->lsr_core;
	ops_tbl = core->dev_ops;
	tbl = core->resources.allowed_clks_tbl;
	tbl_size = core->resources.allowed_clks_tbl_size;

	if (val == 0) {
		struct lsr_device *hdev = ops_tbl->hfi_device_data;

		msm_lsr_clock_voting = 0;
		call_hfi_op(ops_tbl, scale_clocks, hdev, hdev->clk_freq);
		return 0;
	}

	for (i = 0; i < tbl_size; i++)
		if (val <= tbl[i].clock_rate)
			break;

	if (i == tbl_size)
		msm_lsr_clock_voting = tbl[tbl_size-1].clock_rate;
	else
		msm_lsr_clock_voting = tbl[i].clock_rate;

	dprintk(LSR_WARN, "Override lsr_clk_rate with %d\n",
			msm_lsr_clock_voting);

	call_hfi_op(ops_tbl, scale_clocks, ops_tbl->hfi_device_data,
		msm_lsr_clock_voting);

	return 0;
}

static int _clk_rate_get(void *data, u64 *val)
{
	struct msm_lsr_core *core;
	struct lsr_device *hdev;

	core = lsr_driver->lsr_core;
	hdev = core->dev_ops->hfi_device_data;
	if (msm_lsr_clock_voting)
		*val = msm_lsr_clock_voting;
	else
		*val = hdev->clk_freq;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(clk_rate_fops, _clk_rate_get, _clk_rate_set, "%llu\n");

struct dentry *msm_lsr_debugfs_init_core(struct msm_lsr_core *core,
		struct dentry *parent)
{
	struct dentry *dir = NULL;
	char debugfs_name[MAX_DEBUGFS_NAME];

	if (!core) {
		dprintk(LSR_ERR, "Invalid params, core: %pK\n", core);
		goto failed_create_dir;
	}

	snprintf(debugfs_name, MAX_DEBUGFS_NAME, "core%d", 0);
	dir = debugfs_create_dir(debugfs_name, parent);
	if (IS_ERR_OR_NULL(dir)) {
		dir = NULL;
		dprintk(LSR_ERR, "Failed to create debugfs for msm_lsr\n");
		goto failed_create_dir;
	}

	if (!debugfs_create_file("clock_rate", 0644, dir,
			NULL, &clk_rate_fops)) {
		dprintk(LSR_ERR, "debugfs_create_file: clock_rate fail\n");
		goto failed_create_dir;
	}

	debugfs_create_u32("hw_wd_recovery", 0644, dir,
		&msm_lsr_hw_wd_recovery);
	debugfs_create_u32("smmu_fault_recovery", 0644, dir,
		&msm_lsr_smmu_fault_recovery);
failed_create_dir:
	return dir;
}
