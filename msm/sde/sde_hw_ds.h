/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _SDE_HW_DS_H
#define _SDE_HW_DS_H

#include "sde_hw_mdss.h"
#include "sde_hw_util.h"
#include "sde_hw_catalog.h"
#include "sde_kms.h"

struct sde_hw_ds;

/* Destination scaler DUAL mode operation bit */
#define SDE_DS_OP_MODE_DUAL BIT(16)

/* struct sde_hw_ds_cfg - destination scaler config
 * @idx          : DS selection index
 * @flags        : Flag to switch between mode for DS
 * @lm_width     : Layer mixer width configuration
 * @lm_heigh     : Layer mixer height configuration
 * @merge_mode   : Specify pipe merge mode for each DS block
 * @scl3_cfg     : Configuration data for scaler
 */
struct sde_hw_ds_cfg {
	u32 idx;
	int flags;
	u32 lm_width;
	u32 lm_height;
	u32 merge_mode;
	struct sde_hw_scaler3_cfg scl3_cfg;
};

/**
 * struct sde_hw_ds_ops - interface to the destination scaler
 * hardware driver functions
 * Caller must call the init function to get the ds context for each ds
 * Assumption is these functions will be called after clocks are enabled
 */
struct sde_hw_ds_ops {
	/**
	 * setup_opmode - destination scaler op mode setup
	 * @hw_ds   : Pointer to ds context
	 * @op_mode : Op mode configuration
	 * @merge_mode : Specify pipe merge mode for each DS block
	 */
	void (*setup_opmode[MSM_DISP_OP_MAX])(struct sde_hw_ds *hw_ds,
				u32 op_mode, u32 merge_mode);

	/**
	 * setup_scaler - destination scaler block setup
	 * @hw_ds          : Pointer to ds context
	 * @scaler_cfg     : Pointer to scaler data
	 * @scaler_lut_cfg : Pointer to scaler lut
	 * @disp_op        : Display operation mode
	 * @merge_mode     : Specify pipe merge mode for each DS block
	 */
	void (*setup_scaler[MSM_DISP_OP_MAX])(struct sde_hw_ds *hw_ds,
				void *scaler_cfg,
				void *scaler_lut_cfg,
				enum msm_disp_op disp_op,
				u32 merge_mode);
};

/**
 * struct sde_hw_ds - destination scaler description
 * @base : Hardware block base structure
 * @hw   : Block hardware details
 * @idx  : Destination scaler index
 * @scl  : Pointer to
 *          - scaler offset relative to top offset
 *          - capabilities
 * @ops  : Pointer to operations for this DS
 * @ctl  : Pointer to ctl path
 * @dpu_idx       : DPU index
 * @prop_helper   : Property helper for color processing features
 * @num_mixers    : Number of mixers
 * @dspp_idx      : DSPP index related to this DS
 * @dspp_start_idx: Starting dspp index for physical display
 */
struct sde_hw_ds {
	struct sde_hw_blk_reg_map hw;
	enum sde_ds idx;
	struct sde_ds_cfg *scl;
	struct sde_hw_ds_ops ops;
	struct sde_hw_ctl *ctl;
	u32 dpu_idx;

	/* HFI */
	struct hfi_util_u32_prop_helper *prop_helper;
	u32 num_mixers;
	u32 dspp_idx;
	u32 dspp_start_idx;

	bool is_qseed3_lite;
};

/**
 * to_sde_hw_ds - convert base hw object to sde_hw_ds container
 * @hw: Pointer to hardware block register map object
 * return: Pointer to hardware block container
 */
static inline struct sde_hw_ds *to_sde_hw_ds(struct sde_hw_blk_reg_map *hw)
{
	return container_of(hw, struct sde_hw_ds, hw);
}

/**
 * sde_hw_ds_init - initializes the destination scaler
 * hw driver object and should be called once before
 * accessing every destination scaler
 * @idx : DS index for which driver object is required
 * @addr: Mapped register io address of MDP
 * @m   : MDSS catalog information
 * @sde_kms: Pointer to SDE KMS structure
 * @Return: pointer to structure or ERR_PTR
 */
struct sde_hw_blk_reg_map *sde_hw_ds_init(enum sde_ds idx,
			void __iomem *addr,
			struct sde_mdss_cfg *m,
			struct sde_kms *sde_kms);

/**
 * sde_hw_ds_destroy - destroys destination scaler driver context
 * @hw: Pointer to hardware block register map object
 */
void sde_hw_ds_destroy(struct sde_hw_blk_reg_map *hw);

#endif /*_SDE_HW_DS_H */
