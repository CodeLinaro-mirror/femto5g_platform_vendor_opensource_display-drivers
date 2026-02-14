// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "lsr_hfi.h"
#include "msm_lsr_debug.h"
#include "msm_lsr_clocks.h"

/*
 * When syscache is enabled, DDR bandwidth requirements are reduced
 * as most traffic goes through LLCC. Use minimal DDR bandwidth.
 */
#define LSR_DDR_MIN_BW_WITH_SYSCACHE_KBPS 200

static int msm_lsr_set_data_bus_vote(struct msm_lsr_core *core)
{
	int bus_count = 0;
	struct bus_info *bus = NULL;
	struct bus_info *llcc_bus = NULL;
	struct bus_info *llcc_bus1 = NULL;
	unsigned int max_bw = 0;
	int rc = 0;

	for (bus_count = 0; bus_count < core->resources.bus_set.count; bus_count++) {
		if (!strcmp(core->resources.bus_set.bus_tbl[bus_count].name, "lsr-ddr")) {
			bus = &core->resources.bus_set.bus_tbl[bus_count];
			max_bw = bus->range[1];
		} else if (!strcmp(core->resources.bus_set.bus_tbl[bus_count].name, "lsr-llcc")) {
			llcc_bus = &core->resources.bus_set.bus_tbl[bus_count];
		} else if (!strcmp(core->resources.bus_set.bus_tbl[bus_count].name, "lsr-llcc1")) {
			llcc_bus1 = &core->resources.bus_set.bus_tbl[bus_count];
		}
	}

	if (!bus || !llcc_bus || !llcc_bus1) {
		dprintk(LSR_ERR, "bus node is NULL for ddr %d, llcc0 %d, llcc1 %d\n",
			!bus, !llcc_bus, !llcc_bus1);
		return -EINVAL;
	}

	core->bw_sum = (core->bw_sum > max_bw) ? max_bw : core->bw_sum;

	/* Vote with split voting if llcc is enabled */
	if (msm_lsr_syscache_disable) {
		rc = msm_lsr_set_bw(core, bus, core->bw_sum, core->peak_bw);
		if (rc)
			dprintk(LSR_ERR, "failed to set bw vote on %s", bus->name);
		rc = msm_lsr_set_bw(core, llcc_bus, core->bw_sum/2, core->peak_bw);
		if (rc)
			dprintk(LSR_ERR, "failed to set bw vote on %s", bus->name);
		rc = msm_lsr_set_bw(core, llcc_bus1, core->bw_sum/2, core->peak_bw);
		if (rc)
			dprintk(LSR_ERR, "failed to set bw vote on %s", bus->name);
	} else {
		rc = msm_lsr_set_bw(core, bus, LSR_DDR_MIN_BW_WITH_SYSCACHE_KBPS,
				LSR_DDR_MIN_BW_WITH_SYSCACHE_KBPS);
		if (rc)
			dprintk(LSR_ERR, "failed to set bw vote on %s", bus->name);
		rc = msm_lsr_set_bw(core, llcc_bus, core->bw_sum/2, core->peak_bw);
		if (rc)
			dprintk(LSR_ERR, "failed to set bw vote on %s", bus->name);
		rc = msm_lsr_set_bw(core, llcc_bus1, core->bw_sum/2, core->peak_bw);
		if (rc)
			dprintk(LSR_ERR, "failed to set bw vote on %s", bus->name);
	}

	return rc;
}

int msm_lsr_update_power(struct msm_lsr_core *core)
{
	int rc = 0;
	struct clock_set *clocks;
	struct clock_info *cl;
	struct lsr_device *hdev;
	struct allowed_clock_rates_table *tbl = NULL;
	unsigned int tbl_size;
	unsigned int lsr_min_rate, lsr_max_rate;
	unsigned long tmp = 0, core_sum = 0, bw_sum = 0;

	if (!core) {
		dprintk(LSR_ERR, "%s: invalid params\n", __func__);
		return -EINVAL;
	}

	clocks = &core->resources.clock_set;
	cl = &clocks->clock_tbl[clocks->count - 1];
	if (!cl->has_scaling) {
		dprintk(LSR_ERR, "Cannot scale CVP clock\n");
		rc = -EINVAL;
		goto adjust_exit;
	}

	hdev = core->dev_ops->hfi_device_data;
	tbl = core->resources.allowed_clks_tbl;
	tbl_size = core->resources.allowed_clks_tbl_size;
	lsr_min_rate = tbl[0].clock_rate;
	lsr_max_rate = tbl[tbl_size - 1].clock_rate;

	mutex_lock(&core->clk_lock);
	core_sum = core->new_perf.lsr_csc_clk > core->new_perf.lsr_repro_clk ?
		core->new_perf.lsr_csc_clk : core->new_perf.lsr_repro_clk;

	bw_sum = core->new_perf.lsr_csc_bw + core->new_perf.lsr_repro_bw;
	bw_sum = Bps_to_icc(bw_sum);
	dprintk(LSR_PWR, "%s %d : %lu %lu\n", __func__, __LINE__,	core_sum, bw_sum);

	if (core_sum > lsr_max_rate) {
		dprintk(LSR_WARN, "%s clk vote out of range %lu\n", __func__, core_sum);
		core_sum = lsr_max_rate;
	}

	tmp = core->curr_freq;
	core->curr_freq = core_sum;
	core->orig_core_sum = tmp;

	hdev->clk_freq = core->curr_freq;
	core->bw_sum = bw_sum;

	core->peak_bw = core->new_perf.lsr_csc_ib_bw > core->new_perf.lsr_repro_ib_bw ?
			core->new_perf.lsr_csc_ib_bw : core->new_perf.lsr_repro_ib_bw;
	dprintk(LSR_PWR, "%s %d : clk : %lu bw : %lu kBps peak_bw = %lu kBps\n",
		__func__, __LINE__, core->curr_freq, core->bw_sum, core->peak_bw);

	rc = msm_lsr_set_clocks(core);
	if (rc) {
		dprintk(LSR_ERR, "Failed to set clock rate %lu %s: %d %s\n",
			core->curr_freq, cl->name, rc, __func__);
		core->curr_freq = core->orig_core_sum;
		mutex_unlock(&core->clk_lock);
		goto adjust_exit;
	}

	rc = msm_lsr_set_data_bus_vote(core);
	if (rc) {
		dprintk(LSR_ERR, "Failed to set BW vote on data bus path : %d %s\n",
			rc, __func__);
		mutex_unlock(&core->clk_lock);
		goto adjust_exit;
	}

	core->old_perf = core->new_perf;
	mutex_unlock(&core->clk_lock);
adjust_exit:
	return rc;
}

int msm_lsr_set_clocks(struct msm_lsr_core *core)
{
	struct lsr_hfi_ops *ops_tbl;
	int rc;

	if (!core || !core->dev_ops) {
		dprintk(LSR_ERR, "%s Invalid args: %pK\n", __func__, core);
		return -EINVAL;
	}

	ops_tbl = core->dev_ops;
	rc = call_hfi_op(ops_tbl, scale_clocks, ops_tbl->hfi_device_data, core->curr_freq);
	return rc;
}

int msm_lsr_set_clocks_impl(struct lsr_device *device, u32 freq)
{
	struct clock_info *cl;
	int rc = 0;
	u32 scaled_freq = 0;

	dprintk(LSR_PWR, "%s: entering with freq : %u\n", __func__, freq);

	iris_hfi_for_each_clock(device, cl) {
		if (cl->has_scaling) {/* has_scaling */
			device->clk_freq = freq;
			if (msm_lsr_clock_voting)
				freq = msm_lsr_clock_voting;

			scaled_freq = freq;
			// scale tensilica core clk by factor of 2
			// Recommended by LSR FW team as Work around
			if (!strcmp(cl->name, "lsr_clk"))
				scaled_freq *= 2;
			dprintk(LSR_PWR, "%s: clock source rate set to: %u\n",
				__func__, scaled_freq);

			rc = clk_set_rate(cl->clk, scaled_freq);
			if (rc) {
				dprintk(LSR_ERR, "Failed set clock %u %s: %d\n",
					scaled_freq, cl->name, rc);
				return rc;
			}

			dprintk(LSR_PWR, "Scaling clock %s to %u\n", cl->name, scaled_freq);
		}
	}
	return 0;
}

int msm_lsr_scale_clocks(struct lsr_device *device)
{
	int rc = 0;
	struct allowed_clock_rates_table *allowed_clks_tbl = NULL;
	u32 rate = 0;

	allowed_clks_tbl = device->res->allowed_clks_tbl;

	rate = device->clk_freq ? device->clk_freq :
		allowed_clks_tbl[0].clock_rate;

	dprintk(LSR_PWR, "%s: scale clock rate %d\n", __func__, rate);
	rc = msm_lsr_set_clocks_impl(device, rate);
	return rc;
}

int msm_lsr_prepare_enable_clk(struct lsr_device *device, const char *name)
{
	struct clock_info *cl = NULL;
	int rc = 0;

	if (!device) {
		dprintk(LSR_ERR, "Invalid params: %pK\n", device);
		return -EINVAL;
	}

	iris_hfi_for_each_clock(device, cl) {
		if (strcmp(cl->name, name))
			continue;
		/*
		 * For the clocks we control, set the rate prior to preparing
		 * them.  Since we don't really have a load at this point,
		 * scale it to the lowest frequency possible
		 */
		if (!cl->clk) {
			dprintk(LSR_PWR, "%s %s already enabled by framework", __func__, cl->name);
			return 0;
		}

		if (cl->has_scaling) {
			dprintk(LSR_PWR, "%s: set clock with clk_set_rate\n", __func__);
			clk_set_rate(cl->clk, clk_round_rate(cl->clk, 0));
		}
		rc = clk_prepare_enable(cl->clk);
		if (rc) {
			dprintk(LSR_ERR, "Failed to enable clock %s\n", cl->name);
			return rc;
		}
		if (!__clk_is_enabled(cl->clk)) {
			dprintk(LSR_ERR, "%s: clock %s not enabled\n", __func__, cl->name);
			clk_disable_unprepare(cl->clk);
			return -EINVAL;
		}

		dprintk(LSR_PWR, "Clock: %s prepared and enabled\n", cl->name);
		return 0;
	}

	dprintk(LSR_ERR, "%s clock %s not found\n", __func__, name);
	return -EINVAL;
}

int msm_lsr_disable_unprepare_clk(struct lsr_device *device,
		const char *name)
{
	struct clock_info *cl;

	if (!device) {
		dprintk(LSR_ERR, "Invalid params: %pK\n", device);
		return -EINVAL;
	}

	iris_hfi_for_each_clock_reverse(device, cl) {
		if (strcmp(cl->name, name))
			continue;
		if (!cl->clk) {
			dprintk(LSR_PWR, "%s %s always enabled by framework",
				__func__, cl->name);
			return 0;
		}
		clk_disable_unprepare(cl->clk);
		dprintk(LSR_PWR, "Clock: %s disable and unprepare\n",
			cl->name);

		if (cl->has_scaling) {
			dprintk(LSR_PWR,
				"%s: set clock with clk_set_rate\n",
				__func__);
			clk_set_rate(cl->clk,
					clk_round_rate(cl->clk, 0));
		}
		return 0;
	}

	dprintk(LSR_ERR, "%s clock %s not found\n", __func__, name);
	return -EINVAL;
}

int msm_lsr_init_clocks(struct lsr_device *device)
{
	int rc = 0;
	struct clock_info *cl = NULL;

	if (!device) {
		dprintk(LSR_ERR, "Invalid params: %pK\n", device);
		return -EINVAL;
	}

	iris_hfi_for_each_clock(device, cl) {

		dprintk(LSR_PWR, "%s: scalable? %d, count %d\n",
			cl->name, cl->has_scaling, cl->count);
	}

	iris_hfi_for_each_clock(device, cl) {
		if (!cl->clk) {
			cl->clk = clk_get(&device->res->pdev->dev, cl->name);
			if (IS_ERR(cl->clk)) {
				rc = PTR_ERR(cl->clk);
				dprintk(LSR_ERR,
					"Failed to get clock: %s, rc %d\n",
					cl->name, rc);
				cl->clk = NULL;
				goto err_clk_get;
			}
		}
	}
	device->clk_freq = 0;
	return 0;

err_clk_get:
	msm_lsr_deinit_clocks(device);
	return rc;
}

void msm_lsr_deinit_clocks(struct lsr_device *device)
{
	struct clock_info *cl;

	device->clk_freq = 0;
	iris_hfi_for_each_clock_reverse(device, cl) {
		if (cl->clk) {
			clk_put(cl->clk);
			cl->clk = NULL;
		}
	}
}

int msm_lsr_set_bw(struct msm_lsr_core *core, struct bus_info *bus, unsigned long bw,
	unsigned long peak_bw)
{
	struct lsr_hfi_ops *ops_tbl;
	int rc;

	if (!core || !core->dev_ops) {
		dprintk(LSR_ERR, "%s Invalid args: %pK\n", __func__, core);
		return -EINVAL;
	}

	ops_tbl = core->dev_ops;
	rc = call_hfi_op(ops_tbl, vote_bus, ops_tbl->hfi_device_data, bus, bw, peak_bw);
	return rc;

}

int lsr_set_bw(struct bus_info *bus, unsigned long bw, unsigned long peak_bw)
{
	int rc = 0;

	if (!bus->client)
		return -EINVAL;

	dprintk(LSR_PWR, "bus->name = %s to bw = %lu KBps ib = %lu KBps\n",
			bus->name, bw, peak_bw);
	rc = icc_set_bw(bus->client, bw, peak_bw);
	if (rc)
		dprintk(LSR_ERR, "Failed voting bus %s to ab %lu ib %lu\n",
			bus->name, bw, peak_bw);

	return rc;
}

