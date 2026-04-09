/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __MSM_LSR_DEBUG__
#define __MSM_LSR_DEBUG__
#include <linux/debugfs.h>
#include <linux/delay.h>
#include "msm_lsr_core.h"

#ifndef LSR_DBG_LABEL
#define LSR_DBG_LABEL "msm_lsr"
#endif

#define LSR_DBG_TAG LSR_DBG_LABEL ": %4s: "
#define LSR_PID_TAG "[%d,%d] " LSR_DBG_LABEL ": %4s: "

/* To enable messages OR these values and
 * echo the result to debugfs file.
 *
 * To enable all messages set debug_level = 0xFFF
 */

enum lsr_msg_prio {
	LSR_ERR   = 0x000001,
	LSR_WARN  = 0x000002,
	LSR_INFO  = 0x000004,
	LSR_MEM   = 0x000008,
	LSR_CORE  = 0x000010,
	LSR_REG   = 0x000020,
	LSR_PWR   = 0x000040,
	LSR_FW    = 0x000080,
	LSR_SESS  = 0x000100,
	LSR_HFI   = 0x000200,
	LSR_TRACE = 0x000400,
	LSR_SYNX  = 0x000800,
	LSR_DBG  = LSR_MEM | LSR_CORE | LSR_REG | LSR_PWR | LSR_SESS | LSR_HFI,
};

enum lsr_msg_out {
	LSR_OUT_PRINTK = 0,
};

extern int msm_lsr_debug;
extern int msm_lsr_debug_out;
extern int msm_lsr_fw_debug;
extern int msm_lsr_fw_debug_mode;
extern int msm_lsr_fw_low_power_mode;
extern bool msm_lsr_fw_coverage;
extern bool msm_lsr_auto_pil;
extern bool msm_lsr_cacheop_disabled;
extern int msm_lsr_clock_voting;
extern bool msm_lsr_syscache_disable;
extern bool msm_lsr_dcvs_disable;
extern int msm_lsr_hw_wd_recovery;
extern int msm_lsr_smmu_fault_recovery;
extern bool msm_lsr_enable_ssr;

#define dprintk(__level, __fmt, arg...)	\
	do { \
		if (msm_lsr_debug & __level) { \
			if (msm_lsr_debug_out == LSR_OUT_PRINTK) { \
				if (__level == LSR_ERR || __level == LSR_WARN) { \
					pr_info(LSR_PID_TAG __fmt, \
						current->pid, current->tgid, \
						get_debug_level_str(__level), \
						## arg); \
				} \
				else { \
					pr_info(LSR_DBG_TAG __fmt, \
						get_debug_level_str(__level), \
						## arg); \
				} \
			} \
		} \
	} while (0)

/* dprintk_rl is designed for printing frequent recurring errors */
#define dprintk_rl(__level, __fmt, arg...)	\
	do { \
		if (msm_lsr_debug & __level) { \
			if (msm_lsr_debug_out == LSR_OUT_PRINTK) { \
				pr_info_ratelimited(LSR_DBG_TAG __fmt, \
					get_debug_level_str(__level),   \
					## arg); \
			} \
		} \
	} while (0)

struct dentry *msm_lsr_debugfs_init_drv(void);
struct dentry *msm_lsr_debugfs_init_core(struct msm_lsr_core *core,
		struct dentry *parent);

static inline char *get_debug_level_str(int level)
{
	switch (level) {
	case LSR_ERR:
		return "err";
	case LSR_WARN:
		return "warn";
	case LSR_INFO:
		return "info";
	case LSR_DBG:
		return "dbg";
	case LSR_MEM:
		return "mem";
	case LSR_CORE:
		return "core";
	case LSR_REG:
		return "reg";
	case LSR_PWR:
		return "pwr";
	case LSR_FW:
		return "fw";
	case LSR_SESS:
		return "sess";
	case LSR_HFI:
		return "hfi";
	case LSR_TRACE:
		return "trace";
	case LSR_SYNX:
		return "synx";
	default:
		return "???";
	}
}

#endif
