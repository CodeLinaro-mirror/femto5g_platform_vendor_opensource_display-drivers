/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __MSM_HYP_IRQ_H__
#define __MSM_HYP_IRQ_H__

#include <linux/kernel.h>

#include "msm_kms.h"

enum msm_hyp_irq_type {
	MSM_HYP_IRQ_TYPE_NONE = 0,

	/* Mandaroty support */
	MSM_HYP_IRQ_TYPE_INTF_VSYNC = 1,
	MSM_HYP_IRQ_TYPE_LUTDMA_SDB_VQ,
	MSM_HYP_IRQ_TYPE_LUTDMA_SB_VQ,
	MSM_HYP_IRQ_TYPE_INTF_UNDER_RUN,
	MSM_HYP_IRQ_TYPE_ROI_CRC,
	MSM_HYP_IRQ_TYPE_DSPP_HIST,

	/* Optional support */
	MSM_HYP_IRQ_TYPE_DSI = 8,
	MSM_HYP_IRQ_TYPE_DP,
	MSM_HYP_IRQ_TYPE_ROT,
	MSM_HYP_IRQ_TYPE_INTF_DONE,
	MSM_HYP_IRQ_TYPE_DSPP_DONE,
	MSM_HYP_IRQ_TYPE_HIST_DSPP_RSTSEQ,
	MSM_HYP_IRQ_TYPE_LTM_STATS_DONE,
	MSM_HYP_IRQ_TYPE_LTM_STATS_WB_PB,
	MSM_HYP_IRQ_TYPE_WB_ROT_DONE,
	MSM_HYP_IRQ_TYPE_WB_PROG_LINE,
	MSM_HYP_IRQ_TYPE_CWB_OVERFLOW,
	MSM_HYP_IRQ_TYPE_CTL_START,
	MSM_HYP_IRQ_TYPE_CTL_DONE,
	MSM_HYP_IRQ_TYPE_FENCE_ERROR,
	MSM_HYP_IRQ_TYPE_PINGPONG_DONE,
	MSM_HYP_IRQ_TYPE_DSC_ENC,
	MSM_HYP_IRQ_TYPE_FETCH_DMA,
	MSM_HYP_IRQ_TYPE_FETCH_VIG,
	MSM_HYP_IRQ_TYPE_HIST_VIG_DONE,

	/* Most likely will not need */
	MSM_HYP_IRQ_TYPE_PROG_LINE = 32,
	MSM_HYP_IRQ_TYPE_INTF_PANEL_VSYNC,
	MSM_HYP_IRQ_TYPE_INTF_PANEL_UNDERRUN,
	MSM_HYP_IRQ_TYPE_WB_TIMER1,
	MSM_HYP_IRQ_TYPE_INTF_ESYNC_EMSYNC,
	MSM_HYP_IRQ_TYPE_INTF_TEAR_RB_PTR,
	MSM_HYP_IRQ_TYPE_INTF_TEAR_WB_OTR,
	MSM_HYP_IRQ_TYPE_INTF_TEAR_AUTO_REFR,
	MSM_HYP_IRQ_TYPE_INTF_TEAR_AUTO_REF,
	MSM_HYP_IRQ_TYPE_INTF_TEAR_TEAR_DETECT,
	MSM_HYP_IRQ_TYPE_INTF_TEAR_TE_ASSERT,
	MSM_HYP_IRQ_TYPE_INTF_TEAR_TE_DEASSERT,
	MSM_HYP_IRQ_TYPE_VDC_ENC,
	MSM_HYP_IRQ_TYPE_VDC_SLICE,
	MSM_HYP_IRQ_TYPE_AD4_BL_DONE,

	/* Make sure last type is < 64 */
	MSM_HYP_IRQ_TYPE_MAX,

	/* Legacy and deprecated, will not be supported */
	MSM_HYP_IRQ_TYPE_WB_WFD_DONE = 64,
	MSM_HYP_IRQ_TYPE_PINGPONG_RD_PTR,
	MSM_HYP_IRQ_TYPE_PINGPONG_WR_PTR,
	MSM_HYP_IRQ_TYPE_PINGPONG_AUTO_REF,
	MSM_HYP_IRQ_TYPE_PINGPING_TEAR_CHECK,
	MSM_HYP_IRQ_TYPE_PINGPONG_TE_CHECK,
	MSM_HYP_IRQ_TYPE_HIST_VIG_RSTSEQ,
	MSM_HYP_IRQ_TYPE_WD_TIMER,
	MSM_HYP_IRQ_TYPE_SFI_VIDEO_IN,
	MSM_HYP_IRQ_TYPE_SFI_VIDEO_OUT,
	MSM_HYP_IRQ_TYPE_SFI_CMD_0_IN,
	MSM_HYP_IRQ_TYPE_SFI_CMD_0_OUT,
	MSM_HYP_IRQ_TYPE_SFI_CMD_1_IN,
	MSM_HYP_IRQ_TYPE_SFI_CMD_1_OUT,
	MSM_HYP_IRQ_TYPE_SFI_CMD_2_IN,
	MSM_HYP_IRQ_TYPE_SFI_CMD_2_OUT,
};


/**
 * msm_hyp_irq_controller - define hypervision virtual interrupt controller context
 * @enabled_mask:	enable status of MDSS level interrupt
 */
struct msm_hyp_irq_controller {
	u32 dpu_id;
	unsigned long enabled_mask;
	struct sde_hw_intr sde_irq;
};

/**
 * msm_hyp_irq_preinstall - perform pre-installation of hypervision virtual IRQ handler
 * @kms:		pointer to kms context
 * @return:		none
 */
void msm_hyp_irq_preinstall(struct msm_kms *kms);

/**
 * msm_hyp_irq_postinstall - perform post-installation of hypervision virtual IRQ handler
 * @kms:		pointer to kms context
 * @return:		0 if success; error code otherwise
 */
int msm_hyp_irq_postinstall(struct msm_kms *kms);

/**
 * msm_hyp_irq_uninstall - uninstall hypervision virtual IRQ handler
 * @drm_dev:		pointer to kms context
 * @return:		none
 */
void msm_hyp_irq_uninstall(struct msm_kms *kms);

/**
 * msm_hyp_irq - MDSS level hypervision virtual IRQ handler
 * @kms:		pointer to kms context
 * @return:		interrupt handling status
 */
irqreturn_t msm_hyp_irq(struct msm_kms *kms);

/**
 * msm_hyp_irq_update - enable/disable virtual IRQ line
 * @kms:		pointer to kms context
 * @enable:		enable:true, disable:false
 */
void msm_hyp_irq_update(struct msm_kms *kms, bool enable);

/**
 * msm_hyp_irq_destroy - destroy hypervision virtual IRQ context
 * @hyp_kms:   pointer to hyp_kms context
 * @dpu_id:            DPU core id
 * @return:            void
 */
void msm_hyp_irq_destroy(struct msm_hyp_kms *hyp_kms, int dpu_id);

/**
 * msm_hyp_irq_init - initialize hypervision virtual IRQ context
 * @hyp_kms:	pointer to hyp_kms context
 * @dpu_id:		DPU core id
 * @return:		pointer to the interrupt context, NULL if error
 */
struct sde_hw_intr *msm_hyp_irq_init(struct msm_hyp_kms *hyp_kms, int dpu_id);

/**
 * msm_hyp_irq_deinit - de-initialize hypervision virtual IRQ context
 * @hyp_kms:	pointer to hyp_kms context
 * @dpu_id:		DPU core id
 * @return:		0 if success; error code otherwise
 */
int msm_hyp_irq_deinit(struct msm_hyp_kms *hyp_kms, int dpu_id);


#endif /* __MSM_HYP_IRQ_H__ */

