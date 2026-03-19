/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_LSR_CORE_HFI_H__
#define __H_LSR_CORE_HFI_H__

#include "lsr_hfi.h"
#include "msm_lsr_res_parse.h"
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/version.h>

#define HFI_Q_ID_HOST_TO_CTRL_CMD_Q		0x00
#define HFI_Q_ID_CTRL_TO_HOST_MSG_Q		0x01
#define HFI_Q_ID_CTRL_TO_HOST_DEBUG_Q	0x02

#define LSR_IFACEQ_NUMQ					3
#define LSR_IFACEQ_CMDQ_IDX				0
#define LSR_IFACEQ_MSGQ_IDX				1
#define LSR_IFACEQ_DBGQ_IDX				2
#define LSR_IFACEQ_MAX_BUF_COUNT			50
#define LSR_IFACE_MAX_PARALLEL_CLNTS		16
#define LSR_IFACEQ_DFLT_QHDR				0x01010000

struct lsr_hfi_queue_table_header {
	u32 qtbl_version;
	u32 qtbl_size;
	u32 qtbl_qhdr0_offset;
	u32 qtbl_qhdr_size;
	u32 qtbl_num_q;
	u32 qtbl_num_active_q;
	void *device_addr;
	char name[256];
};

struct lsr_hfi_queue_header {
	u32 qhdr_status;
	u32 qhdr_start_addr;
	u32 qhdr_type;
	u32 qhdr_q_size;
	u32 qhdr_pkt_size;
	u32 qhdr_pkt_drop_cnt;
	u32 qhdr_rx_wm;
	u32 qhdr_tx_wm;
	u32 qhdr_rx_req;
	u32 qhdr_tx_req;
	u32 qhdr_rx_irq_status;
	u32 qhdr_tx_irq_status;
	u32 qhdr_read_idx;
	u32 qhdr_write_idx;
};

struct lsr_hfi_mem_map_table {
	u32 mem_map_num_entries;
	u32 mem_map_table_base_addr;
};

struct lsr_hfi_mem_map {
	u32 virtual_addr;
	u32 physical_addr;
	u32 size;
	u32 attr;
};

#define LSR_IFACEQ_TABLE_SIZE (sizeof(struct lsr_hfi_queue_table_header) \
	+ sizeof(struct lsr_hfi_queue_header) * LSR_IFACEQ_NUMQ)

#define LSR_IFACEQ_QUEUE_SIZE	(LSR_IFACEQ_MAX_PKT_SIZE *  \
	LSR_IFACEQ_MAX_BUF_COUNT * LSR_IFACE_MAX_PARALLEL_CLNTS)

#define LSR_IFACEQ_GET_QHDR_START_ADDR(ptr, i)     \
	((void *)((ptr + sizeof(struct lsr_hfi_queue_table_header)) + \
		(i * sizeof(struct lsr_hfi_queue_header))))

#define QDSS_SIZE 4096
#define SFR_SIZE 1048576

#define QUEUE_SIZE (LSR_IFACEQ_TABLE_SIZE + \
	(LSR_IFACEQ_QUEUE_SIZE * LSR_IFACEQ_NUMQ))

#define ALIGNED_QDSS_SIZE ALIGN(QDSS_SIZE, SZ_4K)
#define ALIGNED_SFR_SIZE ALIGN(SFR_SIZE, SZ_4K)
#define ALIGNED_QUEUE_SIZE ALIGN(QUEUE_SIZE, SZ_4K)
#define SHARED_QSIZE ALIGN(ALIGNED_SFR_SIZE + ALIGNED_QUEUE_SIZE + \
			ALIGNED_QDSS_SIZE, SZ_1M)

#define HFI_DEBUG_MODE_QDSS                                    0x00000002

struct lsr_hfi_packet_header {
	u32 size;
	u32 packet_type;
};

struct lsr_hfi_msg_sys_debug_packet {
	u32 size;
	u32 packet_type;
	u32 msg_type;
	u32 msg_size;
	u32 time_stamp_hi;
	u32 time_stamp_lo;
	u8 rg_msg_data[];
};

struct lsr_hfi_sfr_struct {
	u32 bufSize;
	u8 rg_data[];
};

struct lsr_mem_addr {
	u32 align_device_addr;
	u32 align_dcp_device_addr;
	u8 *align_virtual_addr;
	u32 mem_size;
	struct msm_lsr_smem mem_data;
};

struct lsr_iface_q_info {
	spinlock_t hfi_lock;
	void *q_hdr;
	struct lsr_mem_addr q_array;
};

/*
 * These are helper macros to iterate over various lists within
 * lsr_device->res.  The intention is to cut down on a lot of boiler-plate
 * code
 */

/* Read as "for each 'thing' in a set of 'thingies'" */
#define iris_hfi_for_each_thing(__device, __thing, __thingy) \
	iris_hfi_for_each_thing_continue(__device, __thing, __thingy, 0)

#define iris_hfi_for_each_thing_reverse(__device, __thing, __thingy) \
	iris_hfi_for_each_thing_reverse_continue(__device, __thing, __thingy, \
			(__device)->res->__thingy##_set.count - 1)

#define iris_hfi_for_each_thing_continue(__device, __thing, __thingy, __from) \
	for (__thing = &(__device)->res->\
			__thingy##_set.__thingy##_tbl[__from]; \
		__thing < &(__device)->res->__thingy##_set.__thingy##_tbl[0] + \
			((__device)->res->__thingy##_set.count - __from); \
		++__thing)

#define iris_hfi_for_each_thing_reverse_continue(__device, __thing, __thingy, \
		__from) \
	for (__thing = &(__device)->res->\
			__thingy##_set.__thingy##_tbl[__from]; \
		__thing >= &(__device)->res->__thingy##_set.__thingy##_tbl[0]; \
		--__thing)

/* Regular set helpers */
#define iris_hfi_for_each_regulator(__device, __rinfo) \
	iris_hfi_for_each_thing(__device, __rinfo, regulator)

#define iris_hfi_for_each_regulator_reverse(__device, __rinfo) \
	iris_hfi_for_each_thing_reverse(__device, __rinfo, regulator)

#define iris_hfi_for_each_regulator_reverse_continue(__device, __rinfo, \
		__from) \
	iris_hfi_for_each_thing_reverse_continue(__device, __rinfo, \
			regulator, __from)

/* Clock set helpers */
#define iris_hfi_for_each_clock(__device, __cinfo) \
	iris_hfi_for_each_thing(__device, __cinfo, clock)

#define iris_hfi_for_each_clock_reverse(__device, __cinfo) \
	iris_hfi_for_each_thing_reverse(__device, __cinfo, clock)

#define iris_hfi_for_each_clock_reverse_continue(__device, __rinfo, \
		__from) \
	iris_hfi_for_each_thing_reverse_continue(__device, __rinfo, \
			clock, __from)

/* reset set helpers */
#define iris_hfi_for_each_reset_clock(__device, __resetinfo) \
	iris_hfi_for_each_thing(__device, __resetinfo, reset)

#define iris_hfi_for_each_reset_clock_reverse(__device, __resetinfo) \
	iris_hfi_for_each_thing_reverse(__device, __resetinfo, reset)

/* Bus set helpers */
#define iris_hfi_for_each_bus(__device, __binfo) \
	iris_hfi_for_each_thing(__device, __binfo, bus)
#define iris_hfi_for_each_bus_reverse(__device, __binfo) \
	iris_hfi_for_each_thing_reverse(__device, __binfo, bus)

/* Subcache set helpers */
#define iris_hfi_for_each_subcache(__device, __sinfo) \
	iris_hfi_for_each_thing(__device, __sinfo, subcache)
#define iris_hfi_for_each_subcache_reverse(__device, __sinfo) \
	iris_hfi_for_each_thing_reverse(__device, __sinfo, subcache)

/* Power domain set helpers */
#define iris_hfi_for_each_power_domain(__device, __pdinfo) \
	iris_hfi_for_each_thing(__device, __pdinfo, power_domain)

#define iris_hfi_for_each_power_domain_reverse(__device, __pdinfo) \
	iris_hfi_for_each_thing_reverse(__device, __pdinfo, power_domain)

#define iris_hfi_for_each_power_domain_reverse_continue(__device, __pdinfo, \
		__from) \
	iris_hfi_for_each_thing_reverse_continue(__device, __pdinfo, \
			power_domain, __from)

#define call_iris_op(d, op, args...)			\
	(((d) && (d)->hal_ops && (d)->hal_ops->op) ? \
	((d)->hal_ops->op(args)):0)

struct lsr_hal_data {
	u32 irq;
	u32 irq_wd;
	phys_addr_t firmware_base;
	u8 __iomem *register_base;
	u8 __iomem *gcc_reg_base;
	u32 register_size;
	u32 gcc_reg_size;
};

struct lsr_resources {
	struct msm_lsr_fw fw;
};

enum lsr_hfi_state {
	IRIS_STATE_DEINIT = 1,
	IRIS_STATE_INIT,
};

/* Indices of hfi queues in hfi queue arrays (iface_queues) */
enum hfi_queue_idx {
	CMD_Q, /* Command queue */
	MSG_Q, /* Message queue */
	DEBUG_Q, /* Debug queue */
	MAX_Q
};

struct lsr_device;

struct lsr_hal_ops {
	void (*interrupt_init)(struct lsr_device *ptr);
	int (*power_off_controller)(struct lsr_device *device);
	int (*power_off_core)(struct lsr_device *device);
	int (*power_on_controller)(struct lsr_device *device);
	int (*power_on_core)(struct lsr_device *device);
	int (*check_ctl_power_on)(struct lsr_device *device);
	int (*check_core_power_on)(struct lsr_device *device);
	void (*print_sbm_regs)(struct lsr_device *device);
	int (*set_registers)(struct lsr_device *device);
	int (*enable_hw_power_collapse)(struct lsr_device *device);
	int (*reset_ahb2axi_bridge)(struct lsr_device *device);
	int (*reset_control_assert_name)(struct lsr_device *device, const char *name);
	int (*reset_control_deassert_name)(struct lsr_device *device, const char *name);
	int (*reset_control_acquire_name)(struct lsr_device *device, const char *name);
	int (*reset_control_release_name)(struct lsr_device *device, const char *name);
};

struct lsr_device {
	u32 version;
	u32 intr_status;
	u32 clk_freq;
	u32 error;
	unsigned long clk_bitrate;
	unsigned long scaled_rate;
	struct msm_lsr_gov_data bus_vote;
	bool power_enabled;
	bool reg_dumped;
	struct mutex lock;
	struct lsr_mem_addr iface_q_table;
	struct lsr_mem_addr lsr_arp_buf;
	struct lsr_mem_addr qdss;
	struct lsr_mem_addr sfr;
	struct lsr_mem_addr mem_addr;
	struct lsr_mem_addr csc_scratch_pad;
	struct lsr_mem_addr gcx_scratch_pad;
	struct lsr_iface_q_info iface_queues[LSR_IFACEQ_NUMQ];
	struct lsr_hal_data *lsr_hal_data;
	struct workqueue_struct *iris_pm_workq;
	int spur_count;
	int reg_count;
	struct lsr_resources resources;
	struct msm_lsr_platform_resources *res;
	enum lsr_hfi_state state;
	struct pm_qos_request qos;
	unsigned int skip_pc_count;
	struct lsr_hal_ops *hal_ops;
	u32 ref_count;
	struct msm_lsr_synx_ops *synx_ftbl;
	struct sde_lsr_hw_fence_data hwfence_data;
	u32 lsr_reusable_hsynx;
	atomic_t lsr_ssr_in_progress;
};

int msm_lsr_init_reg_and_irq(struct lsr_device *device,	struct msm_lsr_platform_resources *res);
int lsr_iris_hfi_initialize(struct lsr_hfi_ops *ops_tbl, struct msm_lsr_platform_resources *res);
irqreturn_t lsr_hfi_isr(int irq, void *dev);
irqreturn_t iris_hfi_core_work_handler(int irq, void *data);
void lsr_iris_hfi_delete_device(void *device);
int load_lsr_fw_impl(struct lsr_device *device);
int unload_lsr_fw_impl(struct lsr_device *device);

int lsr_iommu_map(struct iommu_domain *domain, unsigned long iova, phys_addr_t paddr, size_t size,
		int prot);

irqreturn_t lsr_wd_handler(int irq, void *data);

#endif  //__H_LSR_CORE_HFI_H__
