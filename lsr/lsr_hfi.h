/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_LSR_HFI_H__
#define __H_LSR_HFI_H__

#include <linux/log2.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/hash.h>
#include "msm_lsr_core.h"
#include "msm_lsr_res_parse.h"

#define TZBSP_LSR_PAS_ID    71

#define CONTAINS(__a, __sz, __t) (\
	(__t >= __a) && \
	(__t < __a + __sz) \
)

#define OVERLAPS(__t, __tsz, __a, __asz) (\
	(__t <= __a) && \
	(__t + __tsz >= __a + __asz) \
)

#define LSR_STATUS_SYS_ERROR 1

#define LSR_IFACEQ_MAX_PKT_SIZE       1024
#define CVP_IFACEQ_MED_PKT_SIZE       768
#define CVP_IFACEQ_MIN_PKT_SIZE       8
#define LSR_IFACEQ_VAR_SMALL_PKT_SIZE 100
#define LSR_IFACEQ_VAR_LARGE_PKT_SIZE 512
#define LSR_IFACEQ_VAR_HUGE_PKT_SIZE  (1024*12)

int iris_hfi_core_init(void *device);
int iris_hfi_resume(void *dev);

struct lsr_device;
int __resume(struct lsr_device *device);
int __response_handler(struct lsr_device *device);

enum lsr_status {
	LSR_ERR_NONE = 0x0,
	LSR_ERR_NOC_ERROR
};

enum lsr_irq_state {
	LSR_IRQ_CLEAR = 1,
	LSR_IRQ_ACCEPTED = 2,
	LSR_IRQ_PROCESSED = 3,
};

struct lsr_fw_info {
	phys_addr_t base_addr;
	int register_base;
	int register_size;
	int irq;
};

struct msm_lsr_gov_data {
	struct lsr_bus_vote_data *data;
	u32 data_count;
};

struct lsr_bus_vote_data {
	u32 domain;
	u32 ddr_bw;
	u32 sys_cache_bw;
	bool use_sys_cache;
};

#define call_hfi_op(q, op, args...)			\
	(((q) && (q)->op) ? ((q)->op(args)) : 0)

struct lsr_hfi_ops {
	void *hfi_device_data;
	/*Add function pointers for all the hfi functions below*/
	int (*core_init)(void *device);
	int (*core_release)(void *device);
	int (*scale_clocks)(void *dev, u32 freq);
	int (*vote_bus)(void *dev, struct bus_info *bus, unsigned long bw, unsigned long peak_bw);
	int (*get_fw_info)(void *dev, struct lsr_fw_info *fw_info);
	int (*suspend)(void *dev);
	int (*resume)(void *dev);
	int (*pm_qos_update)(void *device, u32 latency);
	int (*debug_hook)(void *device);
};

struct msm_lsr_fw {
	int cookie;
};

struct lsr_hfi_ops *lsr_hfi_initialize(struct msm_lsr_platform_resources *res);
void lsr_hfi_deinitialize(struct lsr_hfi_ops *hdev);

int lsr_fw_reset(void);

#endif
