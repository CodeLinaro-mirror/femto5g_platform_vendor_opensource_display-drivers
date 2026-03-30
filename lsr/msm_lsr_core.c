// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/dma-direction.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include "msm_lsr_core.h"
#include "msm_lsr_debug.h"
#include <linux/delay.h>
#include "lsr_hfi.h"
#include "msm_lsr_clocks.h"
#include <linux/dma-buf.h>

static int lsr_display_enable(struct sde_reproj *reproj_inst)
{
	int rc = 0;

	atomic_inc(reproj_inst->ref_count);
	if (atomic_read(reproj_inst->ref_count) > 0) {
		rc = msm_lsr_resume();
		if (rc)
			dprintk(LSR_ERR, "LSR resume failed with rc = %d", rc);
		else
			reproj_inst->engine_pwr_state = 1;
	}

	dprintk(LSR_PWR, "Enable LSR instance of type %d, ref_count = %d, eng state = %d",
		reproj_inst->type, atomic_read(reproj_inst->ref_count),
		reproj_inst->engine_pwr_state);
	return rc;
}

static int lsr_display_disable(struct sde_reproj *reproj_inst, bool skip_wait)
{
	int rc = 0;
	struct msm_lsr_core *core = NULL;
	struct lsr_device *dev = NULL;
	struct drm_device *drm_dev = NULL;
	struct msm_drm_private *priv = NULL;
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms = NULL;

	core = lsr_driver->lsr_core;
	if (core)
		dev = core->dev_ops->hfi_device_data;
	else {
		dprintk(LSR_ERR, "LSR Core is not created\n");
		return -EINVAL;
	}

	drm_dev = lsr_driver->drm_dev;
	if (!drm_dev) {
		dprintk(LSR_ERR, "Invalid drm_dev\n");
		return -EINVAL;
	}
	if (drm_dev->dev_private) {
		priv = drm_dev->dev_private;

		sde_kms = to_sde_kms(priv->kms);
		if (!sde_kms) {
			SDE_ERROR("Invalid sde_kms\n");
			return -EINVAL;
		}
		hfi_kms = to_hfi_kms(sde_kms);
		if (!hfi_kms) {
			SDE_ERROR("Invalid hfi_kms\n");
			return -EINVAL;
		}
	}

	if ((atomic_read(&dev->lsr_ssr_in_progress) > 0) ||
			(atomic_read(&hfi_kms->ssr_in_progress) > 0)) {
		atomic_add_unless(reproj_inst->ref_count, -1, 0);
		dprintk(LSR_PWR, "SSR: Disable LSR instance of type %d, ref_count = %d\n",
			reproj_inst->type, atomic_read(reproj_inst->ref_count));
			return rc;
	}
	atomic_dec(reproj_inst->ref_count);
	if (reproj_inst->engine_pwr_state && (atomic_read(reproj_inst->ref_count) == 0)) {
		rc = msm_lsr_suspend();
		if (rc) {
			dprintk(LSR_ERR, "LSR suspend failed rc = %d", rc);
		} else {
			reproj_inst->engine_pwr_state = 0;
			dprintk(LSR_PWR, "LSR Power collapse succesfull");
		}
	}

	dprintk(LSR_PWR, "Disable LSR instance of type %d, ref_count = %d, eng state = %d",
		reproj_inst->type, atomic_read(reproj_inst->ref_count),
		reproj_inst->engine_pwr_state);
	return rc;
}

static int lsr_update_perf(struct sde_reproj *reproj_inst, int repro_info, struct sde_lsr_perf perf)
{
	int rc = 0;
	struct msm_lsr_core *core;
	bool update_perf = false;

	core = lsr_driver->lsr_core;
	if (!core) {
		dprintk(LSR_ERR, "Invalid LSR core");
		return -EINVAL;
	}

	if (repro_info == WB_CSC) {
		if (core->old_perf.lsr_csc_bw != perf.bw_vote ||
			core->old_perf.lsr_csc_clk != perf.clk_vote ||
			core->old_perf.lsr_csc_ib_bw != perf.ib_bw_vote)
			update_perf = true;
		core->new_perf.lsr_csc_bw = perf.bw_vote;
		core->new_perf.lsr_csc_clk = perf.clk_vote;
		core->new_perf.lsr_csc_ib_bw = perf.ib_bw_vote;
	} else if (repro_info == WB_REPRO) {
		if (core->old_perf.lsr_repro_bw != perf.bw_vote ||
			core->old_perf.lsr_repro_clk != perf.clk_vote ||
			core->old_perf.lsr_repro_ib_bw != perf.ib_bw_vote)
			update_perf = true;
		core->new_perf.lsr_repro_bw = perf.bw_vote;
		core->new_perf.lsr_repro_clk = perf.clk_vote;
		core->new_perf.lsr_repro_ib_bw = perf.ib_bw_vote;
	}

	if (update_perf)
		msm_lsr_update_power(core);

	return rc;
}
static int lsr_display_get_info(struct sde_reproj *reproj_inst, int repro_info)
{
	int rc = 0;
	struct msm_lsr_core *core;
	struct lsr_device *dev = NULL;

	core = lsr_driver->lsr_core;
	if (core)
		dev = core->dev_ops->hfi_device_data;

	if (!dev) {
		dprintk(LSR_ERR, "Invalid LSR device");
		return -EINVAL;
	}

	reproj_inst->queue_table_dcp_addr = dev->iface_q_table.align_dcp_device_addr;
	reproj_inst->queue_table_size = dev->iface_q_table.mem_size;

	reproj_inst->csc_scratch_dcp_addr = dev->csc_scratch_pad.align_dcp_device_addr;
	reproj_inst->csc_scratch_lsr_addr = dev->csc_scratch_pad.align_device_addr;
	reproj_inst->csc_scratch_size = dev->csc_scratch_pad.mem_size;

	reproj_inst->gcx_scratch_dcp_addr = dev->gcx_scratch_pad.align_dcp_device_addr;
	reproj_inst->gcx_scratch_lsr_addr = dev->gcx_scratch_pad.align_device_addr;
	reproj_inst->gcx_scratch_size = dev->gcx_scratch_pad.mem_size;

	reproj_inst->arp_buf_lsr_addr = dev->lsr_arp_buf.align_device_addr;
	reproj_inst->arp_buf_size = dev->lsr_arp_buf.mem_size;

	dprintk(LSR_CORE,
		"LSR info for repro = %d, qtable= 0x%x dcp_addr = 0x%x, arp addr = 0x%x",
			repro_info, reproj_inst->queue_table_dcp_addr,
			dev->iface_q_table.align_dcp_device_addr,
			reproj_inst->arp_buf_lsr_addr);
	return rc;
}

int msm_reproj_disp_register_intf(struct sde_reproj *reproj_inst)
{
	int rc = 0;
	struct msm_lsr_core *core;
	struct lsr_device *dev = NULL;

	core = lsr_driver->lsr_core;
	if (core)
		dev = core->dev_ops->hfi_device_data;

	if (!dev) {
		dprintk(LSR_ERR, "Invalid LSR device");
		return -EINVAL;
	}

	if (!reproj_inst)
		dprintk(LSR_ERR, "Invalid reproj instance");

	reproj_inst->on = lsr_display_enable;
	reproj_inst->off = lsr_display_disable;
	reproj_inst->get_info = lsr_display_get_info;
	reproj_inst->update_lsr_perf = lsr_update_perf;

	reproj_inst->ref_count = (atomic_t *)&dev->ref_count;
	reproj_inst->lsr_ssr_in_progress = &dev->lsr_ssr_in_progress;

	dprintk(LSR_PWR, "initialised LSR ref count to %d", atomic_read(reproj_inst->ref_count));
	dprintk(LSR_CORE, "Initialised LSR SSR in progress to %d\n",
		atomic_read(reproj_inst->lsr_ssr_in_progress));

	return rc;
}
EXPORT_SYMBOL_GPL(msm_reproj_disp_register_intf);

int msm_lsr_comm_suspend(void)
{
	struct lsr_hfi_ops *ops_tbl;
	struct msm_lsr_core *core;
	int rc = 0;

	dprintk(LSR_PWR, "%s : %d", __func__, __LINE__);
	core = lsr_driver->lsr_core;
	if (!core) {
		dprintk(LSR_ERR,
			"%s: Failed to find lsr core\n", __func__);
		return -EINVAL;
	}

	ops_tbl = (struct lsr_hfi_ops *)core->dev_ops;
	if (!ops_tbl) {
		dprintk(LSR_ERR, "%s Invalid device handle\n", __func__);
		return -EINVAL;
	}

	rc = call_hfi_op(ops_tbl, suspend, ops_tbl->hfi_device_data);

	return rc;
}

int msm_lsr_suspend(void)
{
	dprintk(LSR_PWR, "%s : %d : lsr suspend trigger", __func__, __LINE__);
	return msm_lsr_comm_suspend();
}
EXPORT_SYMBOL_GPL(msm_lsr_suspend);

int msm_lsr_resume(void)
{
	struct msm_lsr_core *core = NULL;
	struct lsr_hfi_ops *ops_tbl;
	int rc = 0;

	dprintk(LSR_PWR, "%s : %d : lsr resume trigger", __func__, __LINE__);
	core = lsr_driver->lsr_core;
	if (!core) {
		dprintk(LSR_ERR, "%s CVP core not initialized\n", __func__);
		return -EINVAL;
	}

	ops_tbl = core->dev_ops;
	rc = iris_hfi_resume(ops_tbl->hfi_device_data);
	return rc;
}
EXPORT_SYMBOL_GPL(msm_lsr_resume);
