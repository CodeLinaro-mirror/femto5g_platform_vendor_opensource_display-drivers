// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/dma-direction.h>
#include <linux/iommu.h>
#include <linux/msm_dma_iommu_mapping.h>
#include <soc/qcom/secure_buffer.h>
#include <linux/mem-buf.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/qcom-dma-mapping.h>
#include <linux/version.h>
#include "msm_lsr_core.h"
#include "msm_lsr_debug.h"
#include "msm_lsr_res_parse.h"
#include "lsr_core.h"

#if (KERNEL_VERSION(6, 2, 0) <= LINUX_VERSION_CODE)
#define DMA_ATTR_IOMMU_USE_UPSTREAM_HINT 1
#endif

static void *__lsr_dma_buf_vmap(struct dma_buf *dbuf)
{
#if (KERNEL_VERSION(5, 16, 0) > LINUX_VERSION_CODE)
	struct dma_buf_map map;
#else
	struct iosys_map map;
#endif
	void *dma_map;
	int err;

#if (KERNEL_VERSION(6, 2, 0) > LINUX_VERSION_CODE)
	err = dma_buf_vmap(dbuf, &map);
#else
	err = dma_buf_vmap_unlocked(dbuf, &map);
#endif
	dma_map = err ? NULL : map.vaddr;
	if (!dma_map)
		dprintk(LSR_ERR, "map to kvaddr failed\n");

	return dma_map;
}

static void __lsr_dma_buf_vunmap(struct dma_buf *dbuf, void *vaddr)
{
#if (KERNEL_VERSION(5, 16, 0) > LINUX_VERSION_CODE)
	struct dma_buf_map map = {
			.vaddr = vaddr,
			.is_iomem = false,
	};
#else
	struct iosys_map map = { .vaddr = vaddr, .is_iomem = false, };
#endif
	if (vaddr)
#if (KERNEL_VERSION(6, 2, 0) > LINUX_VERSION_CODE)
		dma_buf_vunmap(dbuf, &map);
#else
		dma_buf_vunmap_unlocked(dbuf, &map);
#endif
}

static struct sg_table *__lsr_dma_buf_map_attachment(struct dma_buf_attachment *attach,
	enum dma_data_direction direction)
{
#if (KERNEL_VERSION(6, 2, 0) > LINUX_VERSION_CODE)
	return dma_buf_map_attachment(attach, direction);
#else
	return dma_buf_map_attachment_unlocked(attach, direction);
#endif
}

static void __lsr_dma_buf_unmap_attachment(struct dma_buf_attachment *attach,
	struct sg_table *sg_table, enum dma_data_direction direction)
{
#if (KERNEL_VERSION(6, 2, 0) > LINUX_VERSION_CODE)
	return dma_buf_unmap_attachment(attach, sg_table, direction);
#else
	return dma_buf_unmap_attachment_unlocked(attach, sg_table, direction);
#endif
}

static int msm_dma_get_device_address(struct dma_buf *dbuf, u32 align,
	dma_addr_t *iova, dma_addr_t *dcp_iova, u32 flags, struct msm_lsr_platform_resources *res,
	struct lsr_dma_mapping_info *mapping_info, u32 smem_flags)
{
	int rc = 0;
	struct dma_buf_attachment *attach, *dcp_attach;
	struct sg_table *table = NULL;
	struct sg_table *dcp_table = NULL;
	struct context_bank_info *cb = NULL;
	struct context_bank_info *dcp_cb = NULL;

	if (!dbuf || !iova || !mapping_info || !dcp_iova) {
		dprintk(LSR_ERR, "Invalid params: %pK, %pK, %pK, %pK\n",
			dbuf, iova, dcp_iova, mapping_info);
		return -EINVAL;
	}

	if (is_iommu_present(res)) {
		cb = msm_lsr_smem_get_context_bank(res, flags);
		if (!cb) {
			dprintk(LSR_ERR,
				"%s: Failed to get context bank device\n",
				 __func__);
			rc = -EIO;
			goto mem_map_failed;
		}

		/* Prepare a dma buf for dma on the given device */
		attach = dma_buf_attach(dbuf, cb->dev);
		if (IS_ERR_OR_NULL(attach)) {
			rc = PTR_ERR(attach) ?: -ENOMEM;
			dprintk(LSR_ERR, "Failed to attach dmabuf\n");
			goto mem_buf_attach_failed;
		}

		/*
		 * Get the scatterlist for the given attachment
		 * Mapping of sg is taken care by map attachment
		 */
		/*
		 * We do not need dma_map function to perform cache operations
		 * on the whole buffer size and hence pass skip sync flag.
		 * We do the required cache operations separately for the
		 * required buffer size
		 */
		attach->dma_map_attrs |= DMA_ATTR_SKIP_CPU_SYNC;
		if (flags & SMEM_CAMERA)
			attach->dma_map_attrs |= DMA_ATTR_QTI_SMMU_PROXY_MAP;
		if (res->sys_cache_present)
			attach->dma_map_attrs |= DMA_ATTR_IOMMU_USE_UPSTREAM_HINT;

		table = __lsr_dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
		if (IS_ERR_OR_NULL(table)) {
			dprintk(LSR_ERR, "Failed to map table %d\n", PTR_ERR(table));
			dprintk(LSR_ERR, "Mapping detail dma_buf 0x%llx, %s, size %#x\n",
				dbuf, dbuf->name, dbuf->size);
			rc = PTR_ERR(table) ?: -ENOMEM;
			goto mem_map_table_failed;
		}

		if ((smem_flags & SMEM_ARP_BUF) || (smem_flags & SMEM_QUEUE_TABLE) ||
				(smem_flags & SMEM_SCRATCH_PAD)) {
			dcp_cb = msm_lsr_smem_get_context_bank(res, SMEM_LSR_HFI);
			dcp_attach = dma_buf_attach(dbuf, dcp_cb->dev);
			if (IS_ERR_OR_NULL(dcp_attach)) {
				rc = PTR_ERR(dcp_attach) ?: -ENOMEM;
				dprintk(LSR_ERR, "Failed to attach dmabuf with dcp domain\n");
			}

			dcp_table = __lsr_dma_buf_map_attachment(dcp_attach, DMA_BIDIRECTIONAL);
			*dcp_iova = dcp_table->sgl->dma_address;
		}

		if (table->sgl) {
			*iova = table->sgl->dma_address;
		} else {
			dprintk(LSR_ERR, "sgl is NULL\n");
			rc = -ENOMEM;
			goto mem_map_sg_failed;
		}

		mapping_info->dev = cb->dev;
		mapping_info->domain = cb->domain;
		mapping_info->table = table;
		mapping_info->attach = attach;
		mapping_info->buf = dbuf;
		mapping_info->cb_info = (void *)cb;
	} else {
		dprintk(LSR_MEM, "iommu not present, use phys mem addr\n");
	}

	return 0;
mem_map_sg_failed:
	__lsr_dma_buf_unmap_attachment(attach, table, DMA_BIDIRECTIONAL);
mem_map_table_failed:
	dma_buf_detach(dbuf, attach);
mem_buf_attach_failed:
mem_map_failed:
	return rc;
}

static int msm_dma_put_device_address(u32 flags,
	struct lsr_dma_mapping_info *mapping_info)
{
	int rc = 0;

	if (!mapping_info) {
		dprintk(LSR_WARN, "Invalid mapping_info\n");
		return -EINVAL;
	}

	if (!mapping_info->dev || !mapping_info->table ||
		!mapping_info->buf || !mapping_info->attach ||
		!mapping_info->cb_info) {
		dprintk(LSR_WARN, "Invalid params\n");
		return -EINVAL;
	}

	dma_buf_unmap_attachment(mapping_info->attach,
		mapping_info->table, DMA_BIDIRECTIONAL);
	dma_buf_detach(mapping_info->buf, mapping_info->attach);

	mapping_info->dev = NULL;
	mapping_info->domain = NULL;
	mapping_info->table = NULL;
	mapping_info->attach = NULL;
	mapping_info->buf = NULL;
	mapping_info->cb_info = NULL;


	return rc;
}

struct dma_buf *msm_lsr_smem_get_dma_buf(int fd)
{
	struct dma_buf *dma_buf;

	dma_buf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dma_buf)) {
		dprintk(LSR_ERR, "Failed to get dma_buf for %d, error %ld\n",
				fd, PTR_ERR(dma_buf));
		dma_buf = NULL;
	}

	return dma_buf;
}

void msm_lsr_smem_put_dma_buf(void *dma_buf)
{
	if (!dma_buf) {
		dprintk(LSR_ERR, "%s: NULL dma_buf\n", __func__);
		return;
	}

	dma_heap_buffer_free((struct dma_buf *)dma_buf);
}

static int alloc_dma_mem(size_t size, u32 align, int map_kernel,
	struct msm_lsr_platform_resources *res, struct msm_lsr_smem *mem, u32 smem_flags)
{
	dma_addr_t iova = 0;
	dma_addr_t dcp_iova = 0;
	int rc = 0;
	struct dma_buf *dbuf = NULL;
	struct dma_heap *heap = NULL;
	struct mem_buf_lend_kernel_arg arg;
	int vmids[1];
	int perms[1];

	if (!res) {
		dprintk(LSR_ERR, "%s: NULL res\n", __func__);
		return -EINVAL;
	}

	align = ALIGN(align, SZ_4K);
	size = ALIGN(size, SZ_4K);

	if (is_iommu_present(res)) {
		heap = dma_heap_find("qcom,system");
		dprintk(LSR_MEM, "%s size %zx align %d flag %d\n",
			__func__, size, align, mem->flags);
	} else {
		dprintk(LSR_ERR, "No IOMMU CB: allocate shared memory heap size %zx align %d\n",
			size, align);
	}

	if (!heap) {
		dprintk(LSR_ERR, "%s: Could not find qcom,system heap.", __func__);
		rc = -EINVAL;
		goto fail_shared_mem_alloc;
	}

	dbuf = dma_heap_buffer_alloc(heap, size, 0, 0);
	if (IS_ERR_OR_NULL(dbuf)) {
		dprintk(LSR_ERR,
			"Failed to allocate shared memory = %x bytes, %x %x\n",
			size, mem->flags, PTR_ERR(dbuf));
		rc = -ENOMEM;
		goto fail_shared_mem_alloc;
	}

	perms[0] = PERM_READ | PERM_WRITE;
	arg.nr_acl_entries = 1;
	arg.vmids = vmids;
	arg.perms = perms;

	if (mem->flags & SMEM_NON_PIXEL) {
		vmids[0] = VMID_CP_NON_PIXEL;
		rc = mem_buf_lend(dbuf, &arg);
	} else if (mem->flags & SMEM_PIXEL) {
		vmids[0] = VMID_CP_PIXEL;
		rc = mem_buf_lend(dbuf, &arg);
	}

	if (rc) {
		dprintk(LSR_ERR, "Failed to lend dmabuf %d, vmid %d\n",
			rc, vmids[0]);
		goto fail_device_address;
	}

	mem->size = size;
	mem->dma_buf = dbuf;
	mem->kvaddr = NULL;

	rc = msm_dma_get_device_address(dbuf, align, &iova, &dcp_iova, mem->flags,
			res, &mem->mapping_info, smem_flags);
	if (rc) {
		dprintk(LSR_ERR, "Failed to get device address: %d\n",
			rc);
		goto fail_device_address;
	}
	mem->device_addr = (u32)iova;
	if ((dma_addr_t)mem->device_addr != iova) {
		dprintk(LSR_ERR, "iova(%pa) truncated to %#x",
			&iova, mem->device_addr);
		goto fail_device_address;
	}

	if ((smem_flags & SMEM_ARP_BUF) || (smem_flags & SMEM_QUEUE_TABLE) ||
				(smem_flags & SMEM_SCRATCH_PAD))
		mem->dcp_device_addr = (u32)dcp_iova;

	if (map_kernel) {
		dma_buf_begin_cpu_access(dbuf, DMA_BIDIRECTIONAL);
		mem->kvaddr = __lsr_dma_buf_vmap(dbuf);
		if (!mem->kvaddr) {
			dprintk(LSR_ERR,
				"Failed to map shared mem in kernel\n");
			rc = -EIO;
			goto fail_map;
		}
	}

	dprintk(LSR_MEM,
		"%s: dma_buf=%pK,iova=%x,size=%d,kvaddr=%pK,flags=%#lx\n",
		__func__, mem->dma_buf, mem->device_addr, mem->size,
		mem->kvaddr, mem->flags);
	return rc;

fail_map:
	if (map_kernel)
		dma_buf_end_cpu_access(dbuf, DMA_BIDIRECTIONAL);
fail_device_address:
	dma_heap_buffer_free(dbuf);
fail_shared_mem_alloc:
	return rc;
}

static int free_dma_mem(struct msm_lsr_smem *mem)
{
	dprintk(LSR_MEM,
		"%s: dma_buf = %pK, device_addr = %x, size = %d, kvaddr = %pK\n",
		__func__, mem->dma_buf, mem->device_addr, mem->size, mem->kvaddr);

	if (mem->device_addr) {
		msm_dma_put_device_address(mem->flags, &mem->mapping_info);
		mem->device_addr = 0x0;
	}

	if (mem->kvaddr) {
		__lsr_dma_buf_vunmap(mem->dma_buf, mem->kvaddr);
		mem->kvaddr = NULL;
		dma_buf_end_cpu_access(mem->dma_buf, DMA_BIDIRECTIONAL);
	}

	if (mem->dma_buf) {
		dma_heap_buffer_free(mem->dma_buf);
		mem->dma_buf = NULL;
	}

	return 0;
}

int msm_lsr_smem_alloc(size_t size, u32 align, int map_kernel,
		void *res, struct msm_lsr_smem *smem, u32 smem_flags)
{
	int rc = 0;

	if (!smem || !size) {
		dprintk(LSR_ERR, "%s: NULL smem or %d size\n",
			__func__, (u32)size);
		return -EINVAL;
	}

	rc = alloc_dma_mem(size, align, map_kernel,
		(struct msm_lsr_platform_resources *)res, smem, smem_flags);

	return rc;
}

int msm_lsr_smem_free(struct msm_lsr_smem *smem)
{
	int rc = 0;

	if (!smem) {
		dprintk(LSR_ERR, "NULL smem passed\n");
		return -EINVAL;
	}
	rc = free_dma_mem(smem);

	return rc;
};

struct context_bank_info *msm_lsr_smem_get_context_bank(
	struct msm_lsr_platform_resources *res,
	unsigned int flags)
{
	struct context_bank_info *cb = NULL, *match = NULL;
	char *search_str;
	char *non_secure_cb = "lsr_hlos";
	char *lsr_dcp_cb = "lsr_dcp";
	char *secure_nonpixel_cb = "lsr_sec_nonpixel";
	char *secure_pixel_cb = "lsr_sec_pixel";
	char *camera_cb = "lsr_camera";
	bool is_secure = (flags & SMEM_SECURE) ? true : false;

	if (flags & SMEM_PIXEL)
		search_str = secure_pixel_cb;
	else if (flags & SMEM_NON_PIXEL)
		search_str = secure_nonpixel_cb;
	else if (flags & SMEM_CAMERA)
		/* Secure Camera pixel buffer */
		search_str = camera_cb;
	else if (flags & SMEM_LSR_HFI)
		/* Secure Camera pixel buffer */
		search_str = lsr_dcp_cb;
	else
		search_str = non_secure_cb;

	list_for_each_entry(cb, &res->context_banks, list) {
		if (cb->is_secure == is_secure &&
			!strcmp(search_str, cb->name)) {
			match = cb;
			break;
		}
	}

	if (!match)
		dprintk(LSR_ERR, "%s: cb not found for flags %x, is_secure %d\n",
			__func__, flags, is_secure);

	return match;
}

int msm_lsr_map_ipcc_regs(u32 *iova)
{
	struct context_bank_info *cb;
	struct msm_lsr_core *core;
	struct lsr_hfi_ops *ops_tbl;
	struct lsr_device *dev = NULL;
	phys_addr_t paddr;
	u32 size;

	core = lsr_driver->lsr_core;
	if (core) {
		ops_tbl = core->dev_ops;
		if (ops_tbl)
			dev = ops_tbl->hfi_device_data;
	}

	if (!dev)
		return -EINVAL;

	paddr = dev->res->ipcc_reg_base;
	size = dev->res->ipcc_reg_size;

	if (!paddr || !size)
		return -EINVAL;

	cb = msm_lsr_smem_get_context_bank(dev->res, 0);
	if (!cb) {
		dprintk(LSR_ERR, "%s: fail to get context bank\n", __func__);
		return -EINVAL;
	}
	*iova = dma_map_resource(cb->dev, paddr, size, DMA_BIDIRECTIONAL, 0);
	if (*iova == DMA_MAPPING_ERROR) {
		dprintk(LSR_WARN, "%s: fail to map IPCC regs\n", __func__);
		return -EFAULT;
	}
	dev->res->ipcc_reg_base_iova = *iova;

	return 0;
}

int msm_lsr_unmap_ipcc_regs(u32 iova)
{
	struct context_bank_info *cb;
	struct msm_lsr_core *core;
	struct lsr_hfi_ops *ops_tbl;
	struct lsr_device *dev = NULL;
	u32 size;

	core = lsr_driver->lsr_core;
	if (core) {
		ops_tbl = core->dev_ops;
		if (ops_tbl)
			dev = ops_tbl->hfi_device_data;
	}

	if (!dev)
		return -EINVAL;

	size = dev->res->ipcc_reg_size;

	if (!iova || !size)
		return -EINVAL;

	cb = msm_lsr_smem_get_context_bank(dev->res, 0);
	if (!cb) {
		dprintk(LSR_ERR, "%s: fail to get context bank\n", __func__);
		return -EINVAL;
	}
	dma_unmap_resource(cb->dev, iova, size, DMA_BIDIRECTIONAL, 0);

	return 0;
}
