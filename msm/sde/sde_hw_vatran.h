// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _SDE_HW_VATRAN_H
#define _SDE_HW_VATRAN_H

#include "sde_hw_mdss.h"
#include "sde_hw_util.h"
#include "sde_hw_catalog.h"

/* Per VA_TRAN remap slots,
 * VQ0 has 64 slots, VQ1-15 has 8 or 16 slots.
 * VQ0 should be reserved for PVM use only.
 */
#if IS_ENABLED(CONFIG_DRM_MSM_HYP)
#define SDE_HW_VATRAN_REMAP_SLOT_VQ0	64
#define SDE_HW_VATRAN_REMAP_SLOT_MAX	8
#else
#define SDE_HW_VATRAN_REMAP_SLOT_VQ0	64
#define SDE_HW_VATRAN_REMAP_SLOT_MAX	16
#endif

struct sde_hw_vatran;

/* struct sde_hw_vatran_map - VA_TRAN remap slot
 * @enabled      : VA_TRAN slot enabled
 * @org_addr     : Orginal register address
 * @new_addr     : Remapped new register address
 * @mask         : Write mask
 */
struct sde_hw_vatran_map {
	bool enabled;
	u32 org_addr;
	u32 new_addr;
	u32 mask;
};

/* struct sde_hw_vatran_cfg - VA_TRAN config
 * @num_vq       : Number of VQs
 * @num_slot0    : Number of slots for VM0
 * @num_slot     : Number of slots
 * @remap        : Remap registers
 */
struct sde_hw_vatran_cfg {
	int num_vq;
	int num_slot0;
	int num_slot;
	struct sde_hw_vatran_map remap[REG_DMA_VQ_MAX][SDE_HW_VATRAN_REMAP_SLOT_VQ0];
};

/**
 * struct sde_hw_ds_ops - interface to the destination scaler
 * hardware driver functions
 * Caller must call the init function to get the ds context for each ds
 * Assumption is these functions will be called after clocks are enabled
 */
struct sde_hw_vatran_ops {
	/**
	 * remap - remap given original address to new address
	 * @hw_vatran   : Pointer to va_tran context
	 * @vq_idx : VQ index
	 * @hw : HW block
	 * @offset : orginal register offset
	 * return: remapped new address, if not remapped, return (u32)-1
	 */
	u32 (*remap)(struct sde_hw_vatran *hw_vatran, int vq_idx,
			struct sde_hw_blk_reg_map *hw, u32 offset);

	/**
	 * check_remap - check if register is VA_TRAN remapped
	 * @hw_vatran   : Pointer to va_tran context
	 * @vq_idx : VQ index
	 * @hw : HW block
	 * @offset : orginal register offset
	 * return: true if the register is remapped
	 */
	bool (*check_remap)(struct sde_hw_vatran *hw_vatran, int vq_idx,
			struct sde_hw_blk_reg_map *hw, u32 offset);

	/**
	 * get_mask - get the VA_TRAN remapped register write mask
	 * @hw_vatran   : Pointer to va_tran context
	 * @vq_idx : VQ index
	 * @hw : HW block
	 * @offset : orginal register offset
	 * return: write mask or 0xFFFFFFFF if not remapped
	 */
	u32 (*get_mask)(struct sde_hw_vatran *hw_vatran, int vq_idx,
			struct sde_hw_blk_reg_map *hw, u32 offset);

	/**
	 * check_violation - check if there is VA_TRAN violation
	 * @hw_vatran   : Pointer to va_tran context
	 * return: true for violation false for no
	 */
	bool (*check_violation)(struct sde_hw_vatran *hw_vatran);
};

/**
 * struct sde_hw_vatran - VA_TRAN description
 * @enabled : VA_TRAN enabled
 * @dpu_idx : DPU index
 * @hw   : Block hardware details
 * @idx  : VA_TRAN index
 * @caps : Pointer to VA_TRAN config
 * @ops  : Pointer to operations for this VA_TRAN
 */
struct sde_hw_vatran {
	bool enabled;
	u32 dpu_idx;
	struct sde_hw_blk_reg_map hw;
	enum sde_vatran idx;
	struct sde_vatran_cfg *caps;
	struct sde_hw_vatran_cfg remap;
	struct sde_hw_vatran_ops ops;
};


/**
 * sde_hw_get_vatran - get va_tran context
 * @dpu_idx: DPU index
 * return: pointer to va_tran context
 */
struct sde_hw_vatran *sde_hw_get_vatran(int dpu_idx);

/**
 * sde_hw_vatran_init - initializes the VA_TRAN
 * hw driver object and should be called once before
 * accessing every VA_TRAN
 * @addr: Mapped register io address of VA_TRAN
 * @m   : MDSS catalog information
 * @dev: drm driver device handle
 * @Return: error code
 */
int sde_hw_vatran_init(void __iomem *addr, struct sde_mdss_cfg *m,
		struct drm_device *dev);

/**
 * sde_hw_vatran_destroy - destroys VA_TRAN driver context
 * @dpu_idx: DPU index
 */
void sde_hw_vatran_deinit(int dpu_idx);


#endif /*_SDE_HW_VATRAN_H */
