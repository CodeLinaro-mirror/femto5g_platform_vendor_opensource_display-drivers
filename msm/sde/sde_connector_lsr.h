/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _SDE_CONNECTOR_LSR_H_
#define _SDE_CONNECTOR_LSR_H_

#include <linux/types.h>
#include <drm/drm_atomic.h>

struct lsr_perf {
	u32 lsr_csc_bw;
	u32 lsr_repro_bw;
	u32 lsr_csc_clk;
	u32 lsr_repro_clk;
};

struct sde_reproj {
	bool engine_pwr_state;
	atomic_t *ref_count;
	u32 type;
	struct lsr_perf perf;
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
			struct lsr_perf perf);
};

int msm_reproj_disp_register_intf(struct sde_reproj *reproj_inst);

#endif

