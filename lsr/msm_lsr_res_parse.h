/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __MSM_LSR_RES_PARSE_H__
#define __MSM_LSR_RES_PARSE_H__

#include <linux/of.h>
#include <linux/devfreq.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/soc/qcom/llcc-qcom.h>
#include "msm_lsr_core.h"

struct reg_value_pair {
	u32 reg;
	u32 value;
};

struct reg_set {
	struct reg_value_pair *reg_tbl;
	int count;
};

struct addr_range {
	u32 start;
	u32 size;
};

struct addr_set {
	struct addr_range *addr_tbl;
	int count;
};

struct context_bank_info {
	struct list_head list;
	const char *name;
	u32 buffer_type;
	bool is_secure;
	struct device *dev;
	struct iommu_domain *domain;
};

struct regulator_info {
	struct regulator *regulator;
	bool has_hw_power_collapse;
	char *name;
};

struct regulator_set {
	struct regulator_info *regulator_tbl;
	u32 count;
};

struct clock_info {
	const char *name;
	u32 clk_id;
	struct clk *clk;
	u32 count;
	bool has_scaling;
	bool has_mem_retention;
};

struct clock_set {
	struct clock_info *clock_tbl;
	u32 count;
};

struct bus_info {
	const char *name;
	unsigned int range[2];
	const char *governor;
	struct device *dev;
	struct devfreq_dev_profile devfreq_prof;
	struct devfreq *devfreq;
	struct icc_path *client;
	bool is_prfm_gov_used;
};

struct bus_set {
	struct bus_info *bus_tbl;
	u32 count;
};

enum lsr_power_state {
	LSR_POWER_INIT,
	LSR_POWER_ON,
	LSR_POWER_OFF,
	LSR_POWER_IGNORED,
};

enum action_stage {
	LSR_ON_INIT,
	LSR_ON_USE,
	LSR_ON_INVALID,
};
enum reset_clk_state {
	RESET_INIT,
	RESET_ACQUIRED,
	RESET_RELEASED,
};

struct reset_info {
	struct reset_control *rst;
	enum lsr_power_state required_state;
	enum action_stage required_stage;
	enum reset_clk_state state;
	const char *name;
};

struct reset_set {
	struct reset_info *reset_tbl;
	u32 count;
};

struct allowed_clock_rates_table {
	u32 clock_rate;
};

struct subcache_info {
	const char *name;
	bool isactive;
	bool isset;
	struct llcc_slice_desc *subcache;
};

struct subcache_set {
	struct subcache_info *subcache_tbl;
	u32 count;
};

#define MAX_SILVER_CORE_NUM 8
#define HFI_SESSION_FD 4
#define HFI_SESSION_DMM 2

struct lsr_pm_qos {
	u32 silver_count;
	u32 latency_us;
	u32 off_vote_cnt;
	spinlock_t lock;
	int silver_cores[MAX_SILVER_CORE_NUM];
	struct dev_pm_qos_request *pm_qos_hdls;
};

struct lsr_fw_reg_mappings {
	phys_addr_t ipclite_iova;
	phys_addr_t ipclite_phyaddr;
	uint32_t ipclite_size;
	phys_addr_t hwmutex_iova;
	phys_addr_t hwmutex_phyaddr;
	uint32_t hwmutex_size;
	phys_addr_t aon_iova;
	phys_addr_t aon_phyaddr;
	uint32_t aon_size;
	phys_addr_t timer_iova;
	phys_addr_t timer_phyaddr;
	uint32_t timer_size;
};

struct msm_lsr_platform_resources {
	phys_addr_t firmware_base;
	phys_addr_t register_base;
	phys_addr_t ipcc_reg_base;
	phys_addr_t gcc_reg_base;
	uint32_t register_size;
	uint32_t ipcc_reg_size;
	uint32_t gcc_reg_size;
	uint32_t ipcc_reg_base_iova;
	struct lsr_fw_reg_mappings reg_mappings;
	uint32_t irq;
	uint32_t irq_wd;
	struct allowed_clock_rates_table *allowed_clks_tbl;
	u32 allowed_clks_tbl_size;
	bool sys_cache_present;
	bool sys_cache_res_set;
	struct subcache_set subcache_set;
	struct reg_set reg_set;
	struct addr_set qdss_addr_set;
	uint32_t max_ssr_allowed;
	struct platform_device *pdev;
	struct regulator_set regulator_set;
	struct clock_set clock_set;
	struct bus_set bus_set;
	struct reset_set reset_set;
	bool use_non_secure_pil;
	bool sw_power_collapsible;
	struct list_head context_banks;
	const char *fw_name;
	bool debug_timeout;
	struct lsr_pm_qos pm_qos;
	uint32_t max_secure_inst_count;
	int msm_lsr_hw_rsp_timeout;
	uint32_t msm_lsr_pwr_collapse_delay;
	bool non_fatal_pagefaults;
	uint32_t fw_cycles;
};

static inline bool is_iommu_present(struct msm_lsr_platform_resources *res)
{
	return !list_empty(&res->context_banks);
}


void msm_lsr_free_platform_resources(struct msm_lsr_platform_resources *res);

int read_hfi_type(struct platform_device *pdev);

int lsr_read_platform_resources_from_dt(struct msm_lsr_platform_resources *res);

int lsr_read_context_bank_resources_from_dt(struct platform_device *pdev);

int lsr_read_bus_resources_from_dt(struct platform_device *pdev);

int msm_lsr_load_u32_table(struct platform_device *pdev,
		struct device_node *of_node, char *table_name, int struct_size,
		u32 **table, u32 *num_elements);

#endif
