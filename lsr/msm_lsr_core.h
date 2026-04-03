/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _MSM_LSR_CORE_H_
#define _MSM_LSR_CORE_H_

#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/dma-buf.h>
#include <linux/refcount.h>
#include "sde_wb_lsr.h"
#include "msm_lsr_res_parse.h"
#include "msm_lsr_synx.h"
#include "hfi_kms.h"

#define MAX_DEBUGFS_NAME 50

struct msm_lsr_platform_resources;

enum lsr_dcp_smem_prop {
	SMEM_QUEUE_TABLE = 0x1,
	SMEM_SCRATCH_PAD = 0x2,
	SMEM_ARP_BUF = 0x4,
	SMEM_SFR = 0x8,
};

enum smem_prop {
	SMEM_UNCACHED = 0x1,
	SMEM_CACHED = 0x2,
	SMEM_SECURE = 0x4,
	SMEM_NON_PIXEL = 0x10,
	SMEM_PIXEL = 0x20,
	SMEM_CAMERA = 0x40,
	SMEM_PERSIST = 0x100,
	SMEM_LSR_HFI = 0x200,
};

enum lsr_subsytem_error_type {
	LSR_SUBSYSTEM_ERROR_TYPE_SYSTEM_ERROR = 1,
	LSR_SUBSYSTEM_ERROR_TYPE_DIV_BY_ZERO = 2,
	LSR_SUBSYSTEM_ERROR_TYPE_WATCHDOG_TIMEOUT = 3,
};

struct msm_lsr_common_data {
	char key[128];
	int value;
};

struct msm_lsr_qos_setting {
	u32 axi_qos;
	u32 prioritylut_low;
	u32 prioritylut_high;
	u32 urgency_low;
	u32 urgency_low_ro;
	u32 dangerlut_low;
	u32 safelut_low;
};

struct msm_lsr_platform_data {
	struct msm_lsr_common_data *common_data;
	unsigned int common_data_length;
	unsigned int vm_id;	/* pvm: 1; tvm: 2 */
	struct msm_lsr_qos_setting *noc_qos;
};

struct msm_lsr_drv {
	struct mutex lock;
	struct msm_lsr_core *lsr_core;
	struct dentry *debugfs_root;
	struct drm_device *drm_dev;
};

struct smem_data {
	u32 size;
	u32 flags;
	u32 device_addr;
	u32 bitmap_index;
	u32 refcount;
};

struct msm_lsr_core {
	struct mutex lock;
	struct mutex clk_lock;
	dev_t dev_num;
	struct class *class;
	struct device *dev;
	struct lsr_hfi_ops *dev_ops;
	struct msm_lsr_platform_data *platform_data;
	struct dentry *debugfs_root;
	struct msm_lsr_platform_resources resources;
	u32 smmu_fault_count;
	u32 last_fault_addr;
	unsigned long curr_freq;
	unsigned long orig_core_sum;
	unsigned long bw_sum;
	unsigned long peak_bw;
	struct lsr_perf new_perf;
	struct lsr_perf old_perf;
};

extern struct msm_lsr_drv *lsr_driver;

struct lsr_dma_mapping_info {
	struct device *dev;
	struct iommu_domain *domain;
	struct sg_table *table;
	struct dma_buf_attachment *attach;
	struct dma_buf *buf;
	void *cb_info;
};

struct msm_lsr_smem {
	struct list_head list;
	atomic_t refcount;
	struct dma_buf *dma_buf;
	void *kvaddr;
	u32 device_addr;
	u32 dcp_device_addr;
	dma_addr_t dma_handle;
	u32 size;
	u32 bitmap_index;
	u32 flags;
	u32 fd;
	struct lsr_dma_mapping_info mapping_info;
};

void *lsr_get_drv_data(struct device *dev);
int lsr_read_platform_resources_from_drv_data(struct msm_lsr_core *core);

int msm_lsr_suspend(void);
int msm_lsr_comm_suspend(void);
int msm_lsr_resume(void);

/*Kernel DMA buffer and IOMMU mapping functions*/
int msm_lsr_smem_alloc(size_t size, u32 align, int map_kernel,
			void  *res, struct msm_lsr_smem *smem, u32 smem_flags);
int msm_lsr_smem_free(struct msm_lsr_smem *smem);
struct context_bank_info *msm_lsr_smem_get_context_bank(struct msm_lsr_platform_resources *res,
			unsigned int flags);

int msm_lsr_map_ipcc_regs(u32 *iova);
int msm_lsr_unmap_ipcc_regs(u32 iova);

#endif
