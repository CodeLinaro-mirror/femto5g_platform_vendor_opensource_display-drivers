// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2019,The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>

#include "dp_catalog.h"
#include "dp_reg.h"
#include "dp_debug.h"

#define dp_catalog_get_priv_v200(x) ({ \
	struct dp_catalog *catalog; \
	catalog = container_of(x, struct dp_catalog, x); \
	container_of(catalog->sub, \
		struct dp_catalog_private_v200, sub); \
})

#define dp_read(x) ({ \
	catalog->sub.read(catalog->dpc, io_data, x); \
})

#define dp_write(x, y) ({ \
	catalog->sub.write(catalog->dpc, io_data, x, y); \
})

#define MAX_VOLTAGE_LEVELS 4
#define MAX_PRE_EMP_LEVELS 4

static const u8 vm_pre_emphasis[MAX_VOLTAGE_LEVELS][MAX_PRE_EMP_LEVELS] = {
	{0x00, 0x0B, 0x12, 0xFF},       /* pe0, 0 db */
	{0x00, 0x0A, 0x12, 0xFF},       /* pe1, 3.5 db */
	{0x00, 0x0C, 0xFF, 0xFF},       /* pe2, 6.0 db */
	{0xFF, 0xFF, 0xFF, 0xFF}        /* pe3, 9.5 db */
};

/* voltage swing, 0.2v and 1.0v are not support */
static const u8 vm_voltage_swing[MAX_VOLTAGE_LEVELS][MAX_PRE_EMP_LEVELS] = {
	{0x07, 0x0F, 0x14, 0xFF}, /* sw0, 0.4v  */
	{0x11, 0x1D, 0x1F, 0xFF}, /* sw1, 0.6 v */
	{0x18, 0x1F, 0xFF, 0xFF}, /* sw1, 0.8 v */
	{0xFF, 0xFF, 0xFF, 0xFF}  /* sw1, 1.2 v, optional */
};


struct dp_catalog_private_v200 {
	struct device *dev;
	struct dp_catalog_io *io;
	struct dp_catalog *dpc;
	struct dp_catalog_sub sub;
};

static void dp_catalog_aux_clear_hw_int_v200(struct dp_catalog_aux *aux)
{
	struct dp_catalog_private_v200 *catalog;
	struct dp_io_data *io_data;
	u32 data = 0;

	if (!aux) {
		DP_ERR("invalid input\n");
		return;
	}

	catalog = dp_catalog_get_priv_v200(aux);
	io_data = catalog->io->dp_phy;

	data = dp_read(DP_PHY_AUX_INTERRUPT_STATUS_V200);

	dp_write(DP_PHY_AUX_INTERRUPT_CLEAR_V200, 0x1f);
	wmb(); /* make sure 0x1f is written before next write */

	dp_write(DP_PHY_AUX_INTERRUPT_CLEAR_V200, 0x9f);
	wmb(); /* make sure 0x9f is written before next write */

	dp_write(DP_PHY_AUX_INTERRUPT_CLEAR_V200, 0);
	wmb(); /* make sure register is cleared */
}

static void dp_catalog_aux_setup_v200(struct dp_catalog_aux *aux,
		struct dp_aux_cfg *cfg)
{
	struct dp_catalog_private_v200 *catalog;
	struct dp_io_data *io_data;
	int i = 0, sw_reset = 0;

	if (!aux || !cfg) {
		DP_ERR("invalid input\n");
		return;
	}

	catalog = dp_catalog_get_priv_v200(aux);
	io_data = catalog->io->dp_ahb;

	sw_reset = dp_read(DP_SW_RESET);

	sw_reset |= BIT(0);
	dp_write(DP_SW_RESET, sw_reset);
	usleep_range(1000, 1010); /* h/w recommended delay */

	sw_reset &= ~BIT(0);
	dp_write(DP_SW_RESET, sw_reset);

	dp_write(DP_PHY_CTRL, 0x4); /* bit 2 */
	udelay(1000);
	dp_write(DP_PHY_CTRL, 0x0); /* bit 2 */
	wmb(); /* make sure programming happened */

	io_data = catalog->io->dp_tcsr;
	dp_write(0x4c, 0x1); /* bit 0 & 2 */
	wmb(); /* make sure programming happened */

	io_data = catalog->io->dp_phy;
	dp_write(DP_PHY_PD_CTL, 0x3c);
	wmb(); /* make sure PD programming happened */
	dp_write(DP_PHY_PD_CTL, 0x3d);
	wmb(); /* make sure PD programming happened */

	/* DP AUX CFG register programming */
	io_data = catalog->io->dp_phy;
	for (i = 0; i < PHY_AUX_CFG_MAX; i++)
		dp_write(cfg[i].offset, cfg[i].lut[cfg[i].current_index]);

	dp_write(DP_PHY_AUX_INTERRUPT_MASK_V200, 0x1F);
	wmb(); /* make sure AUX configuration is done before enabling it */
}

static void dp_catalog_panel_config_msa_v200(struct dp_catalog_panel *panel,
					u32 rate, u32 stream_rate_khz)
{
	u32 pixel_m, pixel_n;
	u32 mvid, nvid;
	u32 const nvid_fixed = 0x8000;
	u32 const link_rate_hbr2 = 540000;
	u32 const link_rate_hbr3 = 810000;
	struct dp_catalog_private_v200 *catalog;
	struct dp_io_data *io_data;
	u32 strm_reg_off = 0;
	u32 mvid_reg_off = 0, nvid_reg_off = 0;

	if (!panel) {
		DP_ERR("invalid input\n");
		return;
	}

	if (panel->stream_id >= DP_STREAM_MAX) {
		DP_ERR("invalid stream_id:%d\n", panel->stream_id);
		return;
	}

	catalog = dp_catalog_get_priv_v200(panel);
	io_data = catalog->io->dp_mmss_cc;

	if (panel->stream_id == DP_STREAM_1)
		strm_reg_off = MMSS_DP_PIXEL1_M_V200 -
					MMSS_DP_PIXEL_M_V200;

	pixel_m = dp_read(MMSS_DP_PIXEL_M_V200 + strm_reg_off);
	pixel_n = dp_read(MMSS_DP_PIXEL_N_V200 + strm_reg_off);
	DP_DEBUG("pixel_m=0x%x, pixel_n=0x%x\n", pixel_m, pixel_n);

	mvid = (pixel_m & 0xFFFF) * 5;
	nvid = (0xFFFF & (~pixel_n)) + (pixel_m & 0xFFFF);

	if (nvid < nvid_fixed) {
		u32 temp;

		temp = (nvid_fixed / nvid) * nvid;
		mvid = (nvid_fixed / nvid) * mvid;
		nvid = temp;
	}

	DP_DEBUG("rate = %d\n", rate);

	if (panel->widebus_en)
		mvid <<= 1;

	if (link_rate_hbr2 == rate)
		nvid *= 2;

	if (link_rate_hbr3 == rate)
		nvid *= 3;

	io_data = catalog->io->dp_link;

	if (panel->stream_id == DP_STREAM_1) {
		mvid_reg_off = DP1_SOFTWARE_MVID - DP_SOFTWARE_MVID;
		nvid_reg_off = DP1_SOFTWARE_NVID - DP_SOFTWARE_NVID;
	}

	DP_DEBUG("mvid=0x%x, nvid=0x%x\n", mvid, nvid);
	dp_write(DP_SOFTWARE_MVID + mvid_reg_off, mvid);
	dp_write(DP_SOFTWARE_NVID + nvid_reg_off, nvid);
}

static void dp_catalog_ctrl_update_vx_px_v200(struct dp_catalog_ctrl *ctrl,
	u8 v_level, u8 p_level, bool high)
{
	struct dp_catalog_private_v200 *catalog;
	struct dp_io_data *io_data;
	u8 value0, value1;

	if (!ctrl || !((v_level < MAX_VOLTAGE_LEVELS)
		&& (p_level < MAX_PRE_EMP_LEVELS))) {
		pr_err("invalid input\n");
		return;
	}

	catalog = dp_catalog_get_priv_v200(ctrl);

	pr_err("hw: v=%d p=%d\n", v_level, p_level);

	value0 = vm_voltage_swing[v_level][p_level];
	value1 = vm_pre_emphasis[v_level][p_level];

	/* program default setting first */
	io_data = catalog->io->dp_ln_tx0;
	dp_write(TXn_TX_DRV_LVL, 0x2A);
	dp_write(TXn_TX_EMP_POST1_LVL, 0x20);

	io_data = catalog->io->dp_ln_tx1;
	dp_write(TXn_TX_DRV_LVL, 0x2A);
	dp_write(TXn_TX_EMP_POST1_LVL, 0x20);

	/* Enable MUX to use Cursor values from these registers */
	value0 |= BIT(5);
	value1 |= BIT(5);

	/* Configure host and panel only if both values are allowed */
	if (value0 != 0xFF && value1 != 0xFF) {
		io_data = catalog->io->dp_ln_tx0;
		dp_write(TXn_TX_DRV_LVL, value0);
		dp_write(TXn_TX_EMP_POST1_LVL, value1);

		io_data = catalog->io->dp_ln_tx1;
		dp_write(TXn_TX_DRV_LVL, value0);
		dp_write(TXn_TX_EMP_POST1_LVL, value1);

		pr_info("hw: vx_value=0x%x px_value=0x%x\n",
			value0, value1);
	} else {
		pr_info("invalid vx (0x%x=0x%x), px (0x%x=0x%x\n",
			v_level, value0, p_level, value1);
	}
}

static void dp_catalog_ctrl_lane_mapping_v200(struct dp_catalog_ctrl *ctrl,
						bool flipped, char *lane_map)
{
	struct dp_catalog_private_v200 *catalog;
	struct dp_io_data *io_data;
	u8 l_map[4] = { 0 }, i = 0, j = 0;
	u32 lane_map_reg = 0;

	if (!ctrl) {
		DP_ERR("invalid input\n");
		return;
	}

	catalog = dp_catalog_get_priv_v200(ctrl);
	io_data = catalog->io->dp_link;

	/* For flip case, swap phy lanes with ML0 and ML3, ML1 and ML2 */
	if (flipped) {
		for (i = 0; i < DP_MAX_PHY_LN; i++) {
			if (lane_map[i] == DP_ML0) {
				for (j = 0; j < DP_MAX_PHY_LN; j++) {
					if (lane_map[j] == DP_ML3) {
						l_map[i] = DP_ML3;
						l_map[j] = DP_ML0;
						break;
					}
				}
			} else if (lane_map[i] == DP_ML1) {
				for (j = 0; j < DP_MAX_PHY_LN; j++) {
					if (lane_map[j] == DP_ML2) {
						l_map[i] = DP_ML2;
						l_map[j] = DP_ML1;
						break;
					}
				}
			}
		}
	} else {
		/* Normal orientation */
		for (i = 0; i < DP_MAX_PHY_LN; i++)
			l_map[i] = lane_map[i];
	}

	lane_map_reg = ((l_map[3]&3)<<6)|((l_map[2]&3)<<4)|((l_map[1]&3)<<2)
			|(l_map[0]&3);

	dp_write(DP_LOGICAL2PHYSICAL_LANE_MAPPING, lane_map_reg);
}

static void dp_catalog_ctrl_usb_reset_v200(struct dp_catalog_ctrl *ctrl,
						bool flip)
{
}

static void dp_catalog_put_v200(struct dp_catalog *catalog)
{
	struct dp_catalog_private_v200 *catalog_priv;

	if (!catalog)
		return;

	catalog_priv = container_of(catalog->sub,
			struct dp_catalog_private_v200, sub);

	devm_kfree(catalog_priv->dev, catalog_priv);
}

struct dp_catalog_sub *dp_catalog_get_v200(struct device *dev,
		struct dp_catalog *catalog, struct dp_catalog_io *io)
{
	struct dp_catalog_private_v200 *catalog_priv;

	if (!dev || !catalog) {
		DP_ERR("invalid input\n");
		return ERR_PTR(-EINVAL);
	}

	catalog_priv = devm_kzalloc(dev, sizeof(*catalog_priv), GFP_KERNEL);
	if (!catalog_priv)
		return ERR_PTR(-ENOMEM);

	catalog_priv->dev = dev;
	catalog_priv->io = io;
	catalog_priv->dpc = catalog;

	catalog_priv->sub.put = dp_catalog_put_v200;

	catalog->aux.clear_hw_interrupts = dp_catalog_aux_clear_hw_int_v200;
	catalog->aux.setup               = dp_catalog_aux_setup_v200;

	catalog->panel.config_msa        = dp_catalog_panel_config_msa_v200;

	catalog->ctrl.update_vx_px       = dp_catalog_ctrl_update_vx_px_v200;
	catalog->ctrl.lane_mapping       = dp_catalog_ctrl_lane_mapping_v200;
	catalog->ctrl.usb_reset          = dp_catalog_ctrl_usb_reset_v200;

	return &catalog_priv->sub;
}
