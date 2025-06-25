// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__
#include "sde_hw_vatran.h"
#include "sde_dbg.h"
#include "sde_kms.h"


/* VA_TRAN TOP registers */
#define VA_TRAN_TERM_CTRL       0x00
#define VA_TRAN_TERM_ADDR       0x04
#define VA_TRAN_TERMINATE       0x08
#define VA_TRAN_CTL_VQ0         0x20
#define VA_TRAN_CTL_VQx         0x400
#define VA_TRAN_CTL_VQx_SIZE    0x80
#define VA_TRAN_LUT_OFFSET      0x4
#define VA_TRAN_WR_MASK_OFFSET  0x8
#define VA_TRAN_SLOT_SIZE       0xC
#define VA_TRAN_VA_REMAP_OFFSET 0x1000
#define VA_TRAN_VA_REMAP_SIZE   0x1000


u32 sde_hw_vatran_remap(struct sde_hw_vatran *hw_vatran, int vq_idx,
		struct sde_hw_blk_reg_map *hw, u32 offset)
{
	u32 org_addr;
	int i;

	if (vq_idx < REG_DMA_VQ_0 || vq_idx >= hw_vatran->remap.num_vq + REG_DMA_VQ_0)
		return (u32)-1;

	org_addr = hw->blk_off + offset;
	for (i = 0; i < hw_vatran->remap.num_slot; i++)
		if (hw_vatran->remap.remap[vq_idx][i].enabled &&
			hw_vatran->remap.remap[vq_idx][i].org_addr == org_addr) {
			SDE_DEBUG("Remap %X to %X [%X]\n", hw->blk_off + offset,
					hw_vatran->remap.remap[vq_idx][i].new_addr,
					hw_vatran->remap.remap[vq_idx][i].new_addr - hw_vatran->caps->base_off);
			return hw_vatran->remap.remap[vq_idx][i].new_addr;
		}

	return (u32)-1;
}

bool sde_hw_vatran_check_remap(struct sde_hw_vatran *hw_vatran, int vq_idx,
		struct sde_hw_blk_reg_map *hw, u32 offset)
{
	u32 org_addr;
	int i;

	if (vq_idx < REG_DMA_VQ_0 || vq_idx >= hw_vatran->remap.num_vq + REG_DMA_VQ_0)
		return false;

	org_addr = hw->blk_off + offset;
	for (i = 0; i < hw_vatran->remap.num_slot; i++)
		if (hw_vatran->remap.remap[vq_idx][i].enabled &&
			hw_vatran->remap.remap[vq_idx][i].org_addr == org_addr)
			return true;

	return false;
}

u32 sde_hw_vatran_get_mask(struct sde_hw_vatran *hw_vatran, int vq_idx,
		struct sde_hw_blk_reg_map *hw, u32 offset)
{
	u32 org_addr;
	int i;

	if (vq_idx < REG_DMA_VQ_0 || vq_idx >= hw_vatran->remap.num_vq + REG_DMA_VQ_0)
		return false;

	org_addr = hw->blk_off + offset;
	for (i = 0; i < hw_vatran->remap.num_slot; i++)
		if (hw_vatran->remap.remap[vq_idx][i].enabled &&
			hw_vatran->remap.remap[vq_idx][i].org_addr == org_addr)
			return hw_vatran->remap.remap[vq_idx][i].mask;

	return false;
}

bool sde_hw_vatran_check_violation(struct sde_hw_vatran *hw_vatran)
{
	u32 val;

	val = SDE_REG_READ(&hw_vatran->hw, VA_TRAN_TERM_CTRL);
	if (val & 0x01) {
		SDE_DEBUG("VA_TRAN_TERM_ADDR = %X\n", SDE_REG_READ(&hw_vatran->hw, VA_TRAN_TERM_ADDR));
		SDE_DEBUG("VA_TRAN_TERMINATE = %X\n", SDE_REG_READ(&hw_vatran->hw, VA_TRAN_TERMINATE));
		// Can't write the control registers from GVM
		//SDE_REG_WRITE_CPU(&hw_vatran->hw, VA_TRAN_TERM_CTRL, 0x02);
		return true;;
	}

	return false;
}

static void _setup_vatran_ops(struct sde_hw_vatran_ops *ops, unsigned long features)
{
	ops->remap = sde_hw_vatran_remap;
	ops->check_remap = sde_hw_vatran_check_remap;
	ops->get_mask = sde_hw_vatran_get_mask;
	ops->check_violation = sde_hw_vatran_check_violation;
}

int sde_hw_vatran_parse(struct sde_hw_vatran *hw_vatran)
{
	struct sde_hw_blk_reg_map *hw = &hw_vatran->hw;
	u32 offset;
	u32 ctl, addr, mask, map_addr;
	int i, j, slots;

	for (i = 0; i < hw_vatran->remap.num_vq; i++) {
		if (i == 0)
			offset = VA_TRAN_CTL_VQ0;
		else
			offset = VA_TRAN_CTL_VQx + (i - 1) * VA_TRAN_CTL_VQx_SIZE;

		map_addr = hw_vatran->caps->base_off + VA_TRAN_VA_REMAP_OFFSET  + i * VA_TRAN_VA_REMAP_SIZE;
		slots = i ? hw_vatran->remap.num_slot : hw_vatran->remap.num_slot0;
		SDE_DEBUG("VA_TRAN %d  slots %d\n", i, slots);
		for (j = 0; j < slots; j ++) {
			ctl = SDE_REG_READ(hw, offset);
			if (ctl & 0x1) {
				addr = SDE_REG_READ(hw, offset + 4);
				mask = SDE_REG_READ(hw, offset + 8);
				hw_vatran->remap.remap[i + REG_DMA_VQ_0][j].enabled = true;
				hw_vatran->remap.remap[i + REG_DMA_VQ_0][j].org_addr = addr;
				hw_vatran->remap.remap[i + REG_DMA_VQ_0][j].new_addr = map_addr;
				hw_vatran->remap.remap[i + REG_DMA_VQ_0][j].mask = mask;
				SDE_DEBUG("VA_TRAN_%d - %d EN %8.8X %8.8X %8.8X\n", i, j, addr, map_addr, mask);
			} else {
				hw_vatran->remap.remap[i + REG_DMA_VQ_0][j].enabled = false;
			}
			map_addr += sizeof(u32);
			offset += VA_TRAN_SLOT_SIZE;
		}
	}

	return 0;
}

struct sde_hw_vatran vatran[DPU_MAX];

struct sde_hw_vatran *sde_hw_get_vatran(int dpu_idx)
{
	if (dpu_idx >= DPU_MAX) {
		DRM_ERROR("invalid dpu idx %d\n", dpu_idx);
		return NULL;
	}

	return &vatran[dpu_idx];
}

int sde_hw_vatran_init(void __iomem *addr, struct sde_mdss_cfg *m,
		struct drm_device *dev)
{
	struct sde_hw_vatran *hw_vatran;
	struct sde_vatran_cfg *cfg = &m->vatran;
	u32 dpu_idx = 0;

	if (!addr || !m)
		return -EINVAL;

	if (!m->vatran_count)
		return -EINVAL;

	if (!dev->primary) {
		DRM_DEBUG("invalid primary dev %pK\n", dev->primary);
		return -EINVAL;
	}

	dpu_idx = m->mdp[0].id - MDP_TOP;
	if (dpu_idx >= DPU_MAX) {
		DRM_DEBUG("invalid dpu idx %u\n", dpu_idx);
		return -EINVAL;
	}

	hw_vatran = &vatran[dpu_idx];

	if (hw_vatran->enabled) {
		DRM_DEBUG("already enabled dpu idx %u\n", dpu_idx);
		return -EINVAL;
	}

	hw_vatran->enabled = true;
	hw_vatran->dpu_idx = dpu_idx;
	hw_vatran->hw.base_off = addr;
	hw_vatran->hw.blk_off = cfg->base;
	hw_vatran->hw.length = cfg->len;
	hw_vatran->hw.hw_rev = m->hw_rev;
	hw_vatran->hw.log_mask = SDE_DBG_MASK_VATRAN;

	/* Assign ops */
	hw_vatran->idx = VA_TRAN_0;
	hw_vatran->caps = cfg;
	hw_vatran->remap.num_vq = cfg->num_vm;
	hw_vatran->remap.num_slot0 = cfg->vm0_slots;
	hw_vatran->remap.num_slot = cfg->vmx_slots;

	sde_hw_vatran_parse(hw_vatran);

	_setup_vatran_ops(&hw_vatran->ops, hw_vatran->caps->features);

	return 0;
}

void sde_hw_vatran_deinit(int dpu_idx)
{
	if (dpu_idx >= DPU_MAX) {
		DRM_ERROR("invalid dpu idx %d\n", dpu_idx);
		return;
	}

	if (!vatran[dpu_idx].enabled) {
		DRM_DEBUG("not enabled dpu idx %d\n", dpu_idx);
		return;
	}

	memset(&vatran[dpu_idx], 0, sizeof(struct sde_hw_vatran));
}
