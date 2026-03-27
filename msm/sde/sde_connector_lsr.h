/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _SDE_CONNECTOR_LSR_H_
#define _SDE_CONNECTOR_LSR_H_

#include <linux/types.h>
#include <drm/drm_atomic.h>
#if IS_ENABLED(CONFIG_QTI_HW_FENCE)
#include <synx_api.h>
#endif /* CONFIG_QTI_HW_FENCE */

struct sde_lsr_hw_fence_data {
	int client_id;
#if IS_ENABLED(CONFIG_QTI_HW_FENCE)
	enum synx_client_id hw_fence_client_id;
	struct synx_session *hw_fence_handle;
	struct synx_queue_desc mem_descriptor;
#endif /* CONFIG_QTI_HW_FENCE */
	u64 dma_context;
};

struct lsr_perf {
	unsigned long lsr_csc_bw;
	unsigned long lsr_repro_bw;
	unsigned long lsr_csc_clk;
	unsigned long lsr_repro_clk;
	unsigned long lsr_csc_ib_bw;
	unsigned long lsr_repro_ib_bw;
};

struct sde_lsr_perf {
	unsigned long bw_vote;
	unsigned long ib_bw_vote;
	unsigned long clk_vote;
	unsigned long ddr_vote;
};

struct sde_reproj {
	bool engine_pwr_state;
	atomic_t *ref_count;
	u32 type;
	struct sde_lsr_perf perf;
	u32 lsr_reusable_hsynx;
	u32 reusable_fence_cnt;
	u32 queue_table_dcp_addr;
	u32 queue_table_size;
	u32 csc_scratch_dcp_addr;
	u32 csc_scratch_lsr_addr;
	u32 csc_scratch_size;
	u32 gcx_scratch_dcp_addr;
	u32 gcx_scratch_lsr_addr;
	u32 gcx_scratch_size;
	u32 arp_buf_lsr_addr;
	u32 arp_buf_size;
	atomic_t *lsr_ssr_in_progress;

	/**
	 * on()
	 *
	 * turns on the reproj engine for the given type
	 *
	 * @sde_reproj: an instance of struct sde_reproj.
	 *
	 * Returns the error code in case of failure, 0 in success case.
	 */
	int (*on)(struct sde_reproj *reproj_inst);

	/**
	 * off()
	 *
	 * turns off the reproj engine for the given type
	 *
	 * @sde_reproj: an instance of struct sde_reproj.
	 * @skip_wait: flag to skip any waits
	 *
	 * Returns the error code in case of failure, 0 in success case.
	 */
	int (*off)(struct sde_reproj *reproj_inst, bool skip_wait);

	/**
	 * get_info()
	 *
	 * gets reproj engine info for the given type
	 *
	 * @sde_reproj: an instance of struct sde_reproj.
	 *
	 * Returns the error code in case of failure, 0 in success case.
	 */
	int (*get_info)(struct sde_reproj *reproj_inst, int repro_info);

	/** TODO : Update comments
	 * get_info()
	 *
	 * gets reproj engine info for the given type
	 *
	 * @sde_reproj: an instance of struct sde_reproj.
	 *
	 * Returns the error code in case of failure, 0 in success case.
	 */
	int (*update_lsr_perf)(struct sde_reproj *reproj_inst, int repro_info,
			struct sde_lsr_perf perf);
};

int msm_reproj_disp_register_intf(struct sde_reproj *reproj_inst);

#endif
