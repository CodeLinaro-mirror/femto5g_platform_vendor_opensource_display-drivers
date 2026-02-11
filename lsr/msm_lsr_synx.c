// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/sync_file.h>
#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>
#include <linux/file.h>
#include <linux/version.h>
#if IS_ENABLED(CONFIG_QTI_HW_FENCE)
#if (KERNEL_VERSION(6, 3, 0) <= LINUX_VERSION_CODE)
#include <msm_hw_fence.h>
#else
#include <linux/soc/qcom/msm_hw_fence.h>
#endif
#endif
#include "msm_lsr_debug.h"
#include "msm_lsr_core.h"
#include "lsr_core.h"
#include "sde_fence.h"
#include "sde_connector_lsr.h"
#include <linux/iommu.h>

//Hardcoded region VA at FW end for HW fence Queue - LSR client
#define LSR_SYNX_SOCCP_QUEUE_MEMORY_MAP 0xFE700000

#if LSR_SYNX_ENABLED
static int lsr_import_retire_fence_for_dcp(void *waiting_client_hw_fence_handle,
		struct sde_fence_context *ctx, u32 hwfence_index)
{
	struct synx_import_params import_params = {0};
	u32 handle, ret;

	if (!ctx) {
		SDE_ERROR("invalid fence ctx\n");
		return -EINVAL;
	}

	import_params.indv_v2.new_h_synx = &handle;
	import_params.indv_v2.flags = SYNX_IMPORT_SYNX_FENCE | SYNX_IMPORT_GLOBAL_FENCE;
	import_params.indv_v2.fence = (void *)&hwfence_index;

	ret = synx_import(waiting_client_hw_fence_handle, &import_params);
	if (ret) {
		SDE_ERROR("dcp failed to import retire hw fence ret:%d\n", ret);
	} else {
		/* release reference held by synx_import */
		ret = synx_release(waiting_client_hw_fence_handle, handle);
		if (ret)
			SDE_ERROR("failed to release dcp import retire fence ret:%d\n", ret);
	}
	return ret;
}

static int lsr_sde_fence_create_hw_fence(struct sde_fence *sde_fence,
		void *waiting_client_hw_fence_handle)
{
	struct synx_create_params params;
	u32 hwfence_index, client_id;
	int ret;
	struct msm_lsr_core *core;
	struct lsr_device *dev = NULL;
	void *lsr_hw_fence_handle;

	core = lsr_driver->lsr_core;
	if (core)
		dev = core->dev_ops->hfi_device_data;

	if (!dev || !dev->hwfence_data.hw_fence_handle) {
		SDE_ERROR("invalid device or hw_fence_handle\n");
		return -EINVAL;
	}

	lsr_hw_fence_handle = dev->hwfence_data.hw_fence_handle;

	client_id = SYNX_CLIENT_HW_FENCE_LSR0_CTX0;
	params.flags = SYNX_CREATE_DMA_FENCE | SYNX_CREATE_GLOBAL_FENCE;
	params.h_synx = &hwfence_index;
	params.fence = &sde_fence->base;
	/* Create the HW fence */
	ret = synx_create(lsr_hw_fence_handle, &params);

	if (ret) {
		SDE_ERROR("failed to create hw_fence for ctx:%llu seqno:%llu\n",
			sde_fence->base.context, sde_fence->base.seqno);
		return ret;
	}

	sde_fence->hw_fence_handle = lsr_hw_fence_handle;
	sde_fence->hwfence_index = hwfence_index;

	dprintk(LSR_SYNX, "created hfence index:0x%llx client: %d, ctx:%llu seqno:%llu name:%s\n",
		sde_fence->hwfence_index, client_id, sde_fence->base.context,
		sde_fence->base.seqno, sde_fence->name);

	/*
	 * DCP does an additional import on every LSR CSC retire fence
	 * created as default. Handle as part of sequnce below.
	 */
	ret = lsr_import_retire_fence_for_dcp(waiting_client_hw_fence_handle,
			sde_fence->ctx, sde_fence->hwfence_index);

	if (ret) {
		struct synx_signal_n_params signal_params = {0};

		SDE_ERROR("import by dcp failed rc:%d, cleaning up fence\n", ret);

		/* Signal with SYNX_SIGNAL_IMMEDIATE to avoid fctl refcount */
		signal_params.type = SYNX_SIGNAL_INDV_PARAMS;
		signal_params.indv.h_synx = hwfence_index;
		signal_params.indv.flags = SYNX_SIGNAL_IMMEDIATE;
		signal_params.indv.status = SYNX_STATE_SIGNALED_ERROR;
		synx_signal_n(lsr_hw_fence_handle, &signal_params);

		/* Release the created fence on import failure */
		synx_release(lsr_hw_fence_handle, hwfence_index);
		sde_fence->hw_fence_handle = NULL;
		sde_fence->hwfence_index = 0;
	}

	return ret;
}

static int _lsr_sde_fence_create_fd(void *fence_ctx, uint32_t val,
	void *waiting_client_hw_fence_handle)
{
	struct sde_fence *sde_fence;
	struct sync_file *sync_file;
	signed int fd = -EINVAL;
	struct sde_fence_context *ctx = fence_ctx;

	if (!ctx) {
		SDE_ERROR("invalid context\n");
		goto exit;
	}

	sde_fence = kzalloc(sizeof(*sde_fence), GFP_KERNEL);
	if (!sde_fence)
		return -ENOMEM;

	sde_fence->ctx = fence_ctx;
	snprintf(sde_fence->name, SDE_FENCE_NAME_SIZE, "sde_fence:%s:%u",
		sde_fence->ctx->name, val);
	dma_fence_init(&sde_fence->base, &sde_fence_ops, &ctx->lock, ctx->context, val);
	kref_get(&ctx->kref);

	ctx->sde_fence_error_ctx.curr_frame_fence_seqno = val;

	/* create fd */
	fd = get_unused_fd_flags(0);
	if (fd < 0) {
		SDE_ERROR("failed to get_unused_fd_flags(), %s\n", sde_fence->name);
		dma_fence_put(&sde_fence->base);
		goto exit;
	}

	/* create fence */
	sync_file = sync_file_create(&sde_fence->base);
	if (sync_file == NULL) {
		put_unused_fd(fd);
		fd = -EINVAL;
		SDE_ERROR("couldn't create fence, %s\n", sde_fence->name);
		dma_fence_put(&sde_fence->base);
		goto exit;
	}

	/* If ctl_id is valid, try to create a hw-fence */
	lsr_sde_fence_create_hw_fence(sde_fence, waiting_client_hw_fence_handle);

	fd_install(fd, sync_file->file);
	sde_fence->fd = fd;

	spin_lock(&ctx->list_lock);
	list_add_tail(&sde_fence->fence_list, &ctx->fence_list_head);
	spin_unlock(&ctx->list_lock);

exit:
	return fd;
}

int lsr_sde_fence_create_and_import(struct sde_fence_context *ctx, uint64_t *val,
				uint32_t offset, void *waiting_client_hw_fence_handle)
{
	uint32_t trigger_value;
	int fd, rc = -EINVAL;
	unsigned long flags;

	if (!ctx || !val) {
		SDE_ERROR("invalid argument(s), fence %d, pval %d\n", ctx != NULL, val != NULL);
		return rc;
	}

	/*
	 * Allow created fences to have a constant offset with respect
	 * to the timeline. This allows us to delay the fence signalling
	 * w.r.t. the commit completion (e.g., an offset of +1 would
	 * cause fences returned during a particular commit to signal
	 * after an additional delay of one commit, rather than at the
	 * end of the current one.
	 */
	spin_lock_irqsave(&ctx->lock, flags);
	trigger_value = ctx->commit_count + offset;
	spin_unlock_irqrestore(&ctx->lock, flags);

	fd = _lsr_sde_fence_create_fd(ctx, trigger_value, waiting_client_hw_fence_handle);
	*val = fd;

	SDE_EVT32(ctx->drm_id, trigger_value, fd, -1);
	rc = (fd >= 0) ? 0 : fd;

	return rc;
}

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

	if (hwfence_data->hw_fence_handle) {
		dprintk(LSR_WARN, "synx already initialized, skipping re-init\n");
		return 0;
	}

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

int lsr_sde_fence_create_and_import(struct sde_fence_context *ctx, uint64_t *val,
				uint32_t offset, struct sde_hw_fence_data *hwfence_data)
{
	return -EINVAL;
}

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
