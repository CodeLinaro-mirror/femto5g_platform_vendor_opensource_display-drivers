// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "msm_lsr_debug.h"
#include "msm_lsr_core.h"
#include "lsr_core.h"
#include <linux/iommu.h>

#define LSR_SYNX_SOCCP_QUEUE_MEMORY_MAP 0xFE700000
//Hardcoded region VA at FW end for HW fence Queue - LSR client

#if LSR_SYNX_ENABLED

static int lsr_sess_init_synx_v2(struct lsr_device *dev)
{
	struct synx_initialization_params params = {0};
	struct sde_lsr_hw_fence_data *hwfence_data;
	struct context_bank_info *cb;
	struct synx_import_params import_params = {0};
	struct synx_session *session = NULL;
	void *lsr_hw_fence_handle = NULL;
	u32 h_synx;
	int ret = 0;

	if (!dev)
		return -EFAULT;

	hwfence_data = &dev->hwfence_data;

	params.name = "lsr-kernel-client";
	params.id = SYNX_CLIENT_HW_FENCE_LSR0_CTX0;
	params.ptr = &hwfence_data->mem_descriptor;
	hwfence_data->hw_fence_handle = synx_initialize(&params);
	hwfence_data->dma_context = dma_fence_context_alloc(1);
	hwfence_data->client_id = SYNX_CLIENT_HW_FENCE_LSR0_CTX0;

	if (IS_ERR_OR_NULL(hwfence_data->hw_fence_handle)) {
		dprintk(LSR_ERR, "%s synx_init: hw_fence_initialize failed\n", __func__);
		return -EFAULT;
	}

	if (params.ptr) {
		cb = msm_lsr_smem_get_context_bank(dev->res, 0);
		if (!cb) {
			dprintk(LSR_ERR, "%s: fail to get cb\n", __func__);
			synx_uninitialize(hwfence_data->hw_fence_handle);
			hwfence_data->hw_fence_handle = NULL;
			return -EINVAL;
		}

		ret = lsr_iommu_map(cb->domain,
			LSR_SYNX_SOCCP_QUEUE_MEMORY_MAP,
			params.ptr->dev_addr,
			params.ptr->size,
			IOMMU_READ | IOMMU_WRITE);
		if (ret) {
			dprintk(LSR_ERR, "%s: failed to map memory: %d\n", __func__, ret);
			synx_uninitialize(hwfence_data->hw_fence_handle);
			hwfence_data->hw_fence_handle = NULL;
			return ret;
		}
	} else {
		dprintk(LSR_ERR, "synx_int failed to get queue address\n");
		return -EINVAL;
	}

	lsr_hw_fence_handle = hwfence_data->hw_fence_handle;
	session = (struct synx_session *)lsr_hw_fence_handle;

	import_params.indv_v2.new_h_synx = &h_synx;
	import_params.indv_v2.fence = NULL;
	import_params.indv_v2.flags = SYNX_IMPORT_REUSABLE | SYNX_IMPORT_SYNX_FENCE;
	import_params.type = SYNX_IMPORT_INDV_PARAMS_V2;

	ret = synx_import(session, &import_params);
	if (ret) {
		dprintk(LSR_ERR, "failed to create reusable hw fence: %d\n", ret);
		return ret;
	}

	dev->lsr_reusable_hsynx = h_synx;
	return 0;
}

static int lsr_sess_deinit_synx_v2(struct lsr_device *dev)
{
	int ret = 0;

	if (!dev) {
		dprintk(LSR_ERR, "Used invalid sess in deinit_synx\n");
		return -EINVAL;
	}

	ret = synx_release(dev->hwfence_data.hw_fence_handle, dev->lsr_reusable_hsynx);
	if (ret)
		dprintk(LSR_ERR, "Failed to release reusable hw fence\n");

	synx_uninitialize(dev->hwfence_data.hw_fence_handle);
	return 0;
}

static struct msm_lsr_synx_ops lsr_synx = {
	.lsr_sess_init_synx = lsr_sess_init_synx_v2,
	.lsr_sess_deinit_synx = lsr_sess_deinit_synx_v2,
};


#else
static int lsr_sess_init_synx_stub(struct lsr_device *dev)
{
	return 0;
}

static int lsr_sess_deinit_synx_stub(struct lsr_device *dev)
{
	return 0;
}

static struct msm_lsr_synx_ops lsr_synx = {
	.lsr_sess_init_synx = lsr_sess_init_synx_stub,
	.lsr_sess_deinit_synx = lsr_sess_deinit_synx_stub,
};

#endif	/* End of LSR_SYNX_ENABLED */

void lsr_synx_ftbl_init(struct lsr_device *dev)
{
	if (!dev)
		return;

	/* Synx API version check below if needed */
	dev->synx_ftbl = &lsr_synx;
}
