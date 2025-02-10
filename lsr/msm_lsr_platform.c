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
#include <soc/qcom/of_common.h>
#include "msm_lsr_debug.h"
#include "lsr_hfi.h"

static struct msm_lsr_common_data default_common_data[] = {
	{
		.key = "qcom,auto-pil",
		.value = 1,
	},
};

static struct msm_lsr_common_data seraph_common_data[] = {
	{
		.key = "qcom,pm-qos-latency-us",
		.value = 50,
	},
	{
		.key = "qcom,sw-power-collapse",
		.value = 1,
	},
	{
		.key = "qcom,domain-attr-non-fatal-faults",
		.value = 0,
	},
	{
		.key = "qcom,max-secure-instances",
		.value = 2,
/**
 * As per design driver allows 3rd instance as well since the secure flags were updated
 * later for the current instance. Hence total secure sessions would be max-secure-instances + 1.
 */
	},
	{
		.key = "qcom,max-ssr-allowed",
		.value = 1,	/* Maximum number of SSR before BUG_ON */
	},
	{
		.key = "qcom,power-collapse-delay",
		.value = 30000000,
	},
	{
		.key = "qcom,hw-resp-timeout",
		.value = 200000000,
	},
	{
		.key = "qcom,debug-timeout",
		.value = 0,
	},
};

static struct msm_lsr_qos_setting seraph_noc_qos = {
	.axi_qos = 0x99,
	.prioritylut_low = 0x33333333,
	.prioritylut_high = 0x33333333,
	.urgency_low = 0x1003,
	.urgency_low_ro = 0x1003,
	.dangerlut_low = 0x0,
	.safelut_low = 0xffff,
};

static struct msm_lsr_platform_data default_data = {
	.common_data = default_common_data,
	.common_data_length =  ARRAY_SIZE(default_common_data),
	.noc_qos = 0x0,
	.vm_id = 1,
};

static struct msm_lsr_platform_data seraph_data = {
	.common_data = seraph_common_data,
	.common_data_length = ARRAY_SIZE(seraph_common_data),
	.noc_qos = &seraph_noc_qos,
	.vm_id = 1,
};

static const struct of_device_id msm_lsr_dt_match[] = {
	{
		.compatible = "qcom,seraph-lsr",
		.data = &seraph_data,
	},
	{},
};

MODULE_DEVICE_TABLE(of, msm_lsr_dt_match);

void *lsr_get_drv_data(struct device *dev)
{
	struct msm_lsr_platform_data *driver_data;
	const struct of_device_id *match;

	driver_data = &default_data;

	if (!IS_ENABLED(CONFIG_OF) || !dev->of_node)
		goto exit;

	match = of_match_node(msm_lsr_dt_match, dev->of_node);

	if (!match)
		return NULL;

	driver_data = (struct msm_lsr_platform_data *)match->data;

exit:
	return driver_data;
}
