// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/kthread.h>

#include "msm_drv_hyp.h"
#include "msm_hyp_irq.h"
#include "sde_core_irq.h"
#include "sde_hw_interrupts.h"
#include "sde_encoder_phys.h"


uint32_t g_msm_hyp_irq_status;

#define COUNTOF(x)  (sizeof(x) / sizeof(x[0]))

#pragma pack(push, 1)
struct hyp_irq_payload {
	u32 dpu_id : 2;
	u32 ctl_id : 6;
	u32 irq_type : 8;
	u32 status : 16;
};
#pragma pack(pop)

/**
 * struct sde_intr_reg - array of SDE register sets
 * @mask:	mask to the status
 * @map_idx_start   first offset in the sde_irq_map table
 * @map_idx_end    last offset in the sde_irq_map table
 */
struct sde_intr_reg {
	u32 mask;
	u32 map_idx_start;
	u32 map_idx_end;
};

/**
 * struct sde_irq_type - maps each irq with i/f
 * @intr_type:		type of interrupt listed in sde_intr_type
 * @instance_idx:	instance index of the associated HW block in SDE
 * @irq_mask:		corresponding bit in the interrupt status reg
 * @reg_idx:		index in the 'sde_irq_tbl' table, to know which
 *			registers offsets to use.
 */
struct sde_irq_type {
	enum sde_intr_type intr_type;
	u32 instance_idx;
	enum sde_intr_idx sde_irq_idx;
	u32 irq_mask;
	int reg_idx;
};

struct msm_hyp_irq {
	u32 irq_mask;
	u32 instance_idx;
	enum sde_intr_type sde_irq_type;
	enum sde_intr_idx sde_irq_idx;
};

struct msm_hyp_irq_map {
	u32 num_irqs;
	struct msm_hyp_irq *irqs;
};

struct msm_hyp_irq vsync_irqs[] = {
	{ 0x0001, INTF_0, SDE_IRQ_TYPE_INTF_VSYNC, INTR_IDX_VSYNC },
	{ 0x0002, INTF_1, SDE_IRQ_TYPE_INTF_VSYNC, INTR_IDX_VSYNC },
	{ 0x0004, INTF_2, SDE_IRQ_TYPE_INTF_VSYNC, INTR_IDX_VSYNC },
	{ 0x0008, INTF_3, SDE_IRQ_TYPE_INTF_VSYNC, INTR_IDX_VSYNC },
	{ 0x0010, INTF_4, SDE_IRQ_TYPE_INTF_VSYNC, INTR_IDX_VSYNC },
	{ 0x0020, INTF_5, SDE_IRQ_TYPE_INTF_VSYNC, INTR_IDX_VSYNC },
	{ 0x0040, INTF_6, SDE_IRQ_TYPE_INTF_VSYNC, INTR_IDX_VSYNC },
	{ 0x0080, INTF_7, SDE_IRQ_TYPE_INTF_VSYNC, INTR_IDX_VSYNC },
	{ 0x0100, INTF_8, SDE_IRQ_TYPE_INTF_VSYNC, INTR_IDX_VSYNC },
};

struct msm_hyp_irq underrun_irqs[] = {
	{ 0x0001, INTF_0, SDE_IRQ_TYPE_INTF_UNDER_RUN, INTR_IDX_UNDERRUN },
	{ 0x0002, INTF_1, SDE_IRQ_TYPE_INTF_UNDER_RUN, INTR_IDX_UNDERRUN },
	{ 0x0004, INTF_2, SDE_IRQ_TYPE_INTF_UNDER_RUN, INTR_IDX_UNDERRUN },
	{ 0x0008, INTF_3, SDE_IRQ_TYPE_INTF_UNDER_RUN, INTR_IDX_UNDERRUN },
	{ 0x0010, INTF_4, SDE_IRQ_TYPE_INTF_UNDER_RUN, INTR_IDX_UNDERRUN },
	{ 0x0020, INTF_5, SDE_IRQ_TYPE_INTF_UNDER_RUN, INTR_IDX_UNDERRUN },
	{ 0x0040, INTF_6, SDE_IRQ_TYPE_INTF_UNDER_RUN, INTR_IDX_UNDERRUN },
	{ 0x0080, INTF_7, SDE_IRQ_TYPE_INTF_UNDER_RUN, INTR_IDX_UNDERRUN },
	{ 0x0100, INTF_8, SDE_IRQ_TYPE_INTF_UNDER_RUN, INTR_IDX_UNDERRUN },
};

struct msm_hyp_irq lutdma_db_irqs[] = {
	{ 0x0001, REG_DMA_VQ_0, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x0002, REG_DMA_VQ_1, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x0004, REG_DMA_VQ_2, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x0008, REG_DMA_VQ_3, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x0010, REG_DMA_VQ_4, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x0020, REG_DMA_VQ_5, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x0040, REG_DMA_VQ_6, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x0080, REG_DMA_VQ_7, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x0100, REG_DMA_VQ_8, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x0200, REG_DMA_VQ_9, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x0400, REG_DMA_VQ_10, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x0800, REG_DMA_VQ_11, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x1000, REG_DMA_VQ_12, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x2000, REG_DMA_VQ_13, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x4000, REG_DMA_VQ_14, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
	{ 0x8000, REG_DMA_VQ_15, SDE_IRQ_TYPE_LUTDMA_DB, 0 },
};

struct msm_hyp_irq lutdma_sb_irqs[] = {
	{ 0x0001, REG_DMA_VQ_0, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x0002, REG_DMA_VQ_1, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x0004, REG_DMA_VQ_2, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x0008, REG_DMA_VQ_3, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x0010, REG_DMA_VQ_4, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x0020, REG_DMA_VQ_5, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x0040, REG_DMA_VQ_6, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x0080, REG_DMA_VQ_7, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x0100, REG_DMA_VQ_8, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x0200, REG_DMA_VQ_9, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x0400, REG_DMA_VQ_10, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x0800, REG_DMA_VQ_11, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x1000, REG_DMA_VQ_12, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x2000, REG_DMA_VQ_13, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x4000, REG_DMA_VQ_14, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
	{ 0x8000, REG_DMA_VQ_15, SDE_IRQ_TYPE_LUTDMA_SB, 0 },
};

struct msm_hyp_irq roi_crc_irqs[] = {
	{ 0x0001, ROI_CRC_0, SDE_IRQ_TYPE_ROI_MISR, 0 },
	{ 0x0002, ROI_CRC_1, SDE_IRQ_TYPE_ROI_MISR, 0 },
	{ 0x0004, ROI_CRC_2, SDE_IRQ_TYPE_ROI_MISR, 0 },
	{ 0x0008, ROI_CRC_3, SDE_IRQ_TYPE_ROI_MISR, 0 },
	{ 0x0010, ROI_CRC_4, SDE_IRQ_TYPE_ROI_MISR, 0 },
	{ 0x0020, ROI_CRC_5, SDE_IRQ_TYPE_ROI_MISR, 0 },
	{ 0x0040, ROI_CRC_6, SDE_IRQ_TYPE_ROI_MISR, 0 },
	{ 0x0080, ROI_CRC_7, SDE_IRQ_TYPE_ROI_MISR, 0 },
};

struct msm_hyp_irq dspp_hist_irqs[] = {
	{ 0x0001, DSPP_0, 0, 0, },
	{ 0x0002, DSPP_1, 0, 0, },
	{ 0x0004, DSPP_2, 0, 0, },
	{ 0x0008, DSPP_3, 0, 0, },
	{ 0x0010, DSPP_4, 0, 0, },
	{ 0x0020, DSPP_5, 0, 0, },
	{ 0x0040, DSPP_6, 0, 0, },
	{ 0x0080, DSPP_7, 0, 0, },
};


#define ADD_IRQ_MAP(map) { COUNTOF(map), map }
#define ADD_RESERVED_IRQ_MAP(type) { 0, NULL }
#define ADD_UNSUPPORTED_IRQ_MAP(type) { 0, NULL }

static const struct msm_hyp_irq_map irq_map[MSM_HYP_IRQ_TYPE_MAX] = {
	ADD_RESERVED_IRQ_MAP(0),				// 0 MSM_HYP_IRQ_TYPE_NONE

	ADD_IRQ_MAP(vsync_irqs),				// 1 MSM_HYP_IRQ_TYPE_INTF_VSYNC
	ADD_IRQ_MAP(lutdma_db_irqs),			// 2 MSM_HYP_IRQ_TYPE_LUTDMA_SDB_VQ
	ADD_IRQ_MAP(lutdma_sb_irqs),			// 3 MSM_HYP_IRQ_TYPE_LUTDMA_SB_VQ
	ADD_IRQ_MAP(underrun_irqs),				// 4 MSM_HYP_IRQ_TYPE_INTF_UNDER_RUN
	ADD_IRQ_MAP(roi_crc_irqs),				// 5 MSM_HYP_IRQ_TYPE_ROI_CRC
	ADD_RESERVED_IRQ_MAP(7),

	ADD_UNSUPPORTED_IRQ_MAP(DSI),			// 8 MSM_HYP_IRQ_TYPE_DSI
	ADD_UNSUPPORTED_IRQ_MAP(DP),			// 9 MSM_HYP_IRQ_TYPE_DP
	ADD_UNSUPPORTED_IRQ_MAP(ROT),			// 10 MSM_HYP_IRQ_TYPE_ROT
	ADD_UNSUPPORTED_IRQ_MAP(INTF_DONE),		// 11 MSM_HYP_IRQ_TYPE_INTF_DONE
	ADD_UNSUPPORTED_IRQ_MAP(DSPP_DONE),		// 12 MSM_HYP_IRQ_TYPE_DSPP_DONE
	ADD_IRQ_MAP(dspp_hist_irqs),			// 13 MSM_HYP_IRQ_TYPE_DSPP_HIST
	ADD_UNSUPPORTED_IRQ_MAP(DSPP_RSTSEQ),	// 14 MSM_HYP_IRQ_TYPE_HIST_DSPP_RSTSEQ
	ADD_UNSUPPORTED_IRQ_MAP(LTM_DONE),		// 15 MSM_HYP_IRQ_TYPE_LTM_STATS_DONE
	ADD_UNSUPPORTED_IRQ_MAP(LTM_WBPB),		// 16 MSM_HYP_IRQ_TYPE_LTM_STATS_WB_PB
	ADD_UNSUPPORTED_IRQ_MAP(WB_DONE),		// 17 MSM_HYP_IRQ_TYPE_WB_ROT_DONE
	ADD_UNSUPPORTED_IRQ_MAP(WB_PROG_LINE),	// 18 MSM_HYP_IRQ_TYPE_WB_PROG_LINE
	ADD_UNSUPPORTED_IRQ_MAP(CWB_OVERFLOW), 	// 19 MSM_HYP_IRQ_TYPE_CWB_OVERFLOW
	ADD_UNSUPPORTED_IRQ_MAP(CTL_START), 	// 20 MSM_HYP_IRQ_TYPE_CTL_START
	ADD_UNSUPPORTED_IRQ_MAP(CTL_DONE),		// 21 MSM_HYP_IRQ_TYPE_CTL_DONE
	ADD_UNSUPPORTED_IRQ_MAP(FENCE_ERROR),	// 22 MSM_HYP_IRQ_TYPE_FENCE_ERROR
	ADD_UNSUPPORTED_IRQ_MAP(PINGPONG_DONE),	// 23 MSM_HYP_IRQ_TYPE_PINGPONG_DONE
	ADD_UNSUPPORTED_IRQ_MAP(DSC_ENC),		// 24 MSM_HYP_IRQ_TYPE_DSC_ENC
	ADD_UNSUPPORTED_IRQ_MAP(FETCH_DMA),		// 25 MSM_HYP_IRQ_TYPE_FETCH_DMA
	ADD_UNSUPPORTED_IRQ_MAP(FETCH_VIG),		// 26 MSM_HYP_IRQ_TYPE_FETCH_VIG
	ADD_UNSUPPORTED_IRQ_MAP(VIG_HIST_DONE),	// 27 MSM_HYP_IRQ_TYPE_HIST_VIG_DONE
	ADD_RESERVED_IRQ_MAP(28),
	ADD_RESERVED_IRQ_MAP(29),
	ADD_RESERVED_IRQ_MAP(30),
	ADD_RESERVED_IRQ_MAP(31),

	ADD_UNSUPPORTED_IRQ_MAP(INTF_PROG_LINE),// 32 MSM_HYP_IRQ_TYPE_PROG_LINE
	ADD_UNSUPPORTED_IRQ_MAP(PANEL_VSYNC),	// 33 MSM_HYP_IRQ_TYPE_INTF_PANEL_VSYNC
	ADD_UNSUPPORTED_IRQ_MAP(PANEL_UNDERRUN),// 34 MSM_HYP_IRQ_TYPE_INTF_PANEL_UNDERRUN
	ADD_UNSUPPORTED_IRQ_MAP(WB_TIMER1),		// 35 MSM_HYP_IRQ_TYPE_WB_TIMER1
	ADD_UNSUPPORTED_IRQ_MAP(EMSYNC),		// 36 MSM_HYP_IRQ_TYPE_INTF_ESYNC_EMSYNC
	ADD_UNSUPPORTED_IRQ_MAP(TEAR_RB_PTR),	// 37 MSM_HYP_IRQ_TYPE_INTF_TEAR_RB_PTR
	ADD_UNSUPPORTED_IRQ_MAP(TEAR_WR_PRT),	// 38 MSM_HYP_IRQ_TYPE_INTF_TEAR_WB_OTR
	ADD_UNSUPPORTED_IRQ_MAP(TEAR_AUTO_REF),	// 39 MSM_HYP_IRQ_TYPE_INTF_TEAR_AUTO_REF
	ADD_UNSUPPORTED_IRQ_MAP(TEAR_DETECT),	// 40 MSM_HYP_IRQ_TYPE_INTF_TEAR_TEAR_DETECT
	ADD_UNSUPPORTED_IRQ_MAP(TE_ASSERT),		// 41 MSM_HYP_IRQ_TYPE_INTF_TEAR_TE_ASSERT
	ADD_UNSUPPORTED_IRQ_MAP(TTE_DEASSERT),	// 42 MSM_HYP_IRQ_TYPE_INTF_TEAR_TE_DEASSERT
	ADD_UNSUPPORTED_IRQ_MAP(VDC_ENC),		// 43 MSM_HYP_IRQ_TYPE_VDC_ENC
	ADD_UNSUPPORTED_IRQ_MAP(VDC_SLICE),		// 44 MSM_HYP_IRQ_TYPE_VDC_SLICE
	ADD_UNSUPPORTED_IRQ_MAP(AD4_BL_DONE),	// 45 MSM_HYP_IRQ_TYPE_AD4_BL_DONE
};

static int msm_hyp_irq_idx_lookup(		struct sde_hw_intr *intr,
		enum sde_intr_type intr_type, u32 instance_idx)
{
	int i;

	for (i = 0; i < intr->sde_irq_map_size; i++) {
		if (intr_type == intr->sde_irq_map[i].intr_type &&
			instance_idx == intr->sde_irq_map[i].instance_idx)
			return i;
	}

	pr_debug("IRQ lookup fail!! intr_type=%d, instance_idx=%d\n",
			intr_type, instance_idx);
	return -EINVAL;
}

static int msm_hyp_enable_irq_nolock(		struct sde_hw_intr *intr,
		int irq_idx)
{
	return 0;
}

static int msm_hyp_disable_irq_nolock(		struct sde_hw_intr *intr,
		int irq_idx)
{
	return 0;
}

static int msm_hyp_clear_all_irqs(		struct sde_hw_intr *intr)
{
	return 0;
}

static int msm_hyp_disable_all_irqs(struct sde_hw_intr *intr)
{
	return 0;
}

static void msm_hyp_dispatch_irqs(		struct sde_hw_intr *intr,
		void msm_hyp_cbfunc(void *arg, int irq_idx), void *arg)
{
	return;
}

static void msm_hyp_clear_interrupt_status(		struct sde_hw_intr *intr,
		int irq_idx)
{
	return;
}

static void msm_hyp_clear_intr_status_nolock(struct sde_hw_intr *intr,
		int irq_idx)
{
	return;
}

static u32 msm_hyp_get_interrupt_status(		struct sde_hw_intr *intr,
		int irq_idx, bool clear)
{
	return 0;
}

static u32 msm_hyp_get_intr_status_nolock(		struct sde_hw_intr *intr,
		int irq_idx, bool clear)
{
	return 0;
}

static int msm_hyp_get_interrupt_sources(		struct sde_hw_intr *intr,
		uint32_t *sources)
{
	return 0;
}

void msm_hyp_irq_update(struct msm_kms *msm_kms, bool enable)
{
	struct sde_kms *sde_kms = to_sde_kms(msm_kms);

	if (!msm_kms || !sde_kms) {
		SDE_ERROR("invalid kms arguments\n");
		return;
	}

	sde_kms->irq_enabled = enable;

	if (enable) {
		// TODO: Enable overall IRQ mask for DPU sde_kms->irq_num
	} else {
		// TODO: Disable overall IRQ mask for DPU sde_kms->irq_num
	}
}

irqreturn_t msm_hyp_irq(struct msm_kms *kms)
{
	struct sde_kms *sde_kms = to_sde_kms(kms);
	u32 interrupts;

	// TODO: get next interrupt source
	sde_kms->hw_intr->ops.get_interrupt_sources(sde_kms->hw_intr,
			&interrupts);

	/* store irq status in case of irq-storm debugging */
	g_msm_hyp_irq_status = interrupts;

	/*
	 * Taking care of MDP interrupt
	 */
	if (interrupts & IRQ_SOURCE_MDP) {
		interrupts &= ~IRQ_SOURCE_MDP;
		sde_core_irq(sde_kms);
	}

	/*
	 * Routing all other interrupts to external drivers
	 */
	while (interrupts) {
		irq_hw_number_t hwirq = fls(interrupts) - 1;
		unsigned int mapping;
		int rc;

		mapping = irq_find_mapping(sde_kms->irq_controller.domain,
				hwirq);
		if (mapping == 0) {
			SDE_EVT32(hwirq, SDE_EVTLOG_ERROR);
			goto error;
		}

		rc = generic_handle_irq(mapping);
		if (rc < 0) {
			SDE_EVT32(hwirq, mapping, rc, SDE_EVTLOG_ERROR);
			goto error;
		}

		interrupts &= ~(1 << hwirq);
	}

	return IRQ_HANDLED;

error:
	/* bad situation, inform irq system, it may disable overall MDSS irq */
	return IRQ_NONE;
}

static const struct sde_hw_intr_ops msm_hyp_irq_ops =
{
	.irq_idx_lookup = msm_hyp_irq_idx_lookup,
	.enable_irq_nolock = msm_hyp_enable_irq_nolock,
	.disable_irq_nolock = msm_hyp_disable_irq_nolock,
	.dispatch_irqs = msm_hyp_dispatch_irqs,
	.clear_all_irqs = msm_hyp_clear_all_irqs,
	.disable_all_irqs = msm_hyp_disable_all_irqs,
	.get_interrupt_sources = msm_hyp_get_interrupt_sources,
	.clear_interrupt_status = msm_hyp_clear_interrupt_status,
	.clear_intr_status_nolock = msm_hyp_clear_intr_status_nolock,
	.get_interrupt_status = msm_hyp_get_interrupt_status,
	.get_intr_status_nolock = msm_hyp_get_intr_status_nolock,
};

void msm_hyp_irq_preinstall(struct msm_kms *kms)
{
	struct sde_kms *sde_kms = to_sde_kms(kms);

	if (!sde_kms->dev || !sde_kms->dev->dev) {
		pr_err("invalid device handles\n");
		return;
	}

	if (!sde_kms->hw_intr) {
		pr_err("invalid hw_intr context\n");
		return;
	}

	sde_core_irq_preinstall(sde_kms);

	/* Borrow ire_num to store DPU id */
	sde_kms->irq_num = DPUID(sde_kms);

	/* disable irq until power event enables it */
	if (!sde_kms->irq_enabled) {
		// TODO:
	}
}

int msm_hyp_irq_postinstall(struct msm_kms *kms)
{
	struct sde_kms *sde_kms = to_sde_kms(kms);
	int rc;

	if (!kms) {
		SDE_ERROR("invalid parameters\n");
		return -EINVAL;
	}

	rc = sde_core_irq_postinstall(sde_kms);

	return rc;
}

void msm_hyp_irq_uninstall(struct msm_kms *kms)
{
	struct sde_kms *sde_kms = to_sde_kms(kms);

	if (!kms) {
		SDE_ERROR("invalid parameters\n");
		return;
	}

	sde_core_irq_uninstall(sde_kms);
}

void msm_hyp_irq_destroy(struct msm_hyp_kms *hyp_kms, int dpu_id)
{
	struct msm_hyp_irq_controller *irq = NULL;
	struct sde_hw_intr *intr = NULL;

	if (hyp_kms)
		irq = hyp_kms->hyp_irq[dpu_id];
	if (irq)
		intr = &irq->sde_irq;

	if (intr) {
		kfree(intr->sde_irq_tbl);
		kfree(intr->sde_irq_map);
		kfree(intr->cache_irq_mask);
		kfree(intr->save_irq_status);
		kfree(irq);
	}
}

struct sde_hw_intr *msm_hyp_irq_init(struct msm_hyp_kms *hyp_kms, int dpu_id)
{
	struct msm_hyp_irq_controller *irq = NULL;
	struct sde_hw_intr *intr;
	u32 irq_regs_count = 0;
	u32 irq_map_count = 0;
	u32 i, j, reg_idx, irq_idx, size;
	int ret = 0;

	irq = kzalloc(sizeof(*irq), GFP_KERNEL);
	if (!irq) {
		ret = -ENOMEM;
		goto exit;
	}

	irq->dpu_id = dpu_id;
	irq->enabled_mask = 0;
	intr = &irq->sde_irq;

	intr->ops = msm_hyp_irq_ops;

	/* check how many irq's this target supports */
	for (i = 0; i < COUNTOF(irq_map); i++) {
		size = irq_map[i].num_irqs;
		if (!size)
			continue;
		if (irq_map_count >= UINT_MAX - size) {
			pr_err("wrong map cnt idx:%d blk:%d sz:%d cnt:%d\n",
				irq_regs_count, i, size, irq_map_count);
			ret = -EINVAL;
			goto exit;
		}

		irq_regs_count++;
		irq_map_count += size;
	}

	if (irq_regs_count == 0 || irq_map_count == 0) {
		pr_err("invalid irq map: %d %d\n",
				irq_regs_count, irq_map_count);
		ret = -EINVAL;
		goto exit;
	}

	/* Allocate table for the irq registers */
	intr->sde_irq_size = irq_regs_count;
	intr->sde_irq_tbl = kcalloc(irq_regs_count, sizeof(*intr->sde_irq_tbl),
		GFP_KERNEL);
	if (intr->sde_irq_tbl == NULL) {
		ret = -ENOMEM;
		goto exit;
	}

	/* Allocate table with the valid interrupts bits */
	intr->sde_irq_map_size = irq_map_count;
	intr->sde_irq_map = kcalloc(irq_map_count, sizeof(*intr->sde_irq_map),
		GFP_KERNEL);
	if (intr->sde_irq_map == NULL) {
		ret = -ENOMEM;
		goto exit;
	}

	intr->cache_irq_mask = kcalloc(intr->sde_irq_size,
			sizeof(*intr->cache_irq_mask), GFP_KERNEL);
	if (intr->cache_irq_mask == NULL) {
		ret = -ENOMEM;
		goto exit;
	}

	intr->save_irq_status = kcalloc(intr->sde_irq_size,
			sizeof(*intr->save_irq_status), GFP_KERNEL);
	if (intr->save_irq_status == NULL) {
		ret = -ENOMEM;
		goto exit;
	}

	spin_lock_init(&intr->irq_lock);

	/* Initialize IRQs tables */
	reg_idx = 0;
	irq_idx = 0;
	for (i = 0; i < COUNTOF(irq_map); i++) {
		if (!irq_map[i].num_irqs || !irq_map[i].irqs)
			continue;
		intr->sde_irq_tbl[reg_idx].mask = BIT(i);
		intr->sde_irq_tbl[reg_idx].map_idx_start = irq_idx;

		for (j = 0; j < irq_map[i].num_irqs; j ++) {
			intr->sde_irq_map[irq_idx].intr_type = irq_map[i].irqs[j].sde_irq_type;
			intr->sde_irq_map[irq_idx].instance_idx = irq_map[i].irqs[j].instance_idx;
			intr->sde_irq_map[irq_idx].sde_irq_idx = irq_map[i].irqs[j].sde_irq_idx;
			intr->sde_irq_map[irq_idx].irq_mask = irq_map[i].irqs[j].irq_mask;
			intr->sde_irq_map[irq_idx].reg_idx = reg_idx;
			irq_idx++;
		}

		intr->sde_irq_tbl[reg_idx].map_idx_end = irq_idx - 1;
		reg_idx++;
	}

exit:
	if (ret) {
		kfree(intr->sde_irq_tbl);
		kfree(intr->sde_irq_map);
		kfree(intr->cache_irq_mask);
		kfree(intr->save_irq_status);
		return ERR_PTR(ret);
	}

	hyp_kms->hyp_irq[dpu_id] = irq;
	return &irq->sde_irq;
}

int msm_hyp_irq_deinit(struct msm_hyp_kms *hyp_kms, int dpu_id)
{
	return 0;
}
