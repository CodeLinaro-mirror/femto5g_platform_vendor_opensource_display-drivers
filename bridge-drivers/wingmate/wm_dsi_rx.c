// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "wm_dsi_rx.h"
#include <linux/delay.h>
#include <linux/bits.h>

/*TODO
 * 1. configure mipi interrupts
 * 2. debugfs
 * 3. handle error status
 */

struct des_en_config_table des_en_config[] = {
	{0, 12, 0},
	{13, 38, 1},
	{39, 65, 2},
	{66, 92, 3},
	{93, 118, 4},
	{119, 140, 5}
};

struct op_freq_table op_freq_val[] = {
	{2500, 2550, 0x49, 438},
	{2300, 2350, 0x45, 403},
	{2250, 2300, 0x44, 394},
	{2200, 2250, 0x43, 385},
	{2150, 2200, 0x42, 376},
	{2100, 2150, 0x41, 368},
	{2050, 2100, 0x40, 359},
	{2000, 2050, 0xF,  350},
	{1500, 1550, 0x2C, 438},
};

static void wm_dsi_rx_power_init(struct wm_dsi_rx *dsi_rx)
{
	/*will implemented later
	 * once powergrid is available*/

	return;
}

static void wm_dsi_rx_sysfs_init(struct wm_dsi_rx *dsi_rx)
{
	/*will implemented later
	 * once sysfs is available from wm_display*/
	return;
}

static int dsi_reg_read(struct wm_dsi_rx *dsi_rx, u32 off)
{
	struct wm_display *display;

	display = dsi_rx->display;

	/*TODO: modify the func once regmap info is confirmed*/
	return display->read_register(display, off);
}

static int dsi_reg_write(struct wm_dsi_rx *dsi_rx, u32 off, u32 val)
{
	struct wm_display *display;
	display = dsi_rx->display;

	/*TODO: modify the func once regmap info is confirmed*/
	return display->write_register(display, off, val);
}

int wm_dsi_rx_mask_interrupts(struct wm_dsi_rx *dsi_rx)
{
	/*func may split classifying what type of interrupts to be masked*/
	return 0;
}

static int wm_dsi_rx_phy_lanes_status(struct wm_dsi_rx *dsi_rx)
{
	u32 lanes = 0xF;/*four lanes*/
	u32 reg = 0;
	u32 rc = 0;

	reg = dsi_reg_read(dsi_rx, DSI_RX_PHY_DATA_STATUS);
	if (!((reg & 0xF) == lanes))
		return -EINVAL;

	return rc;
}

static int wm_dsi_rx_check_state(struct wm_dsi_rx *dsi_rx, enum wm_dsi_rx_ops op)
{
	int rc = 0;
	u32 reg;

	/* revisit again*/
	switch (op) {
	case DSI_RX_OP_TPG:
		/*IPI_PG_ACTIVE register should be not active*/
		reg = dsi_reg_read(dsi_rx, DSI_RX_IPI_PG_ACTIVE);
		if (!(reg & BIT(0)))
			rc = -EINVAL;
	break;
	case DSI_RX_OP_PHY_READY:
		/*check for PHY data and clk lane status*/
		reg = dsi_reg_read(dsi_rx, DSI_RX_PHY_CLK_STATUS);
		if(!(reg & BIT(0)))
			rc = -EINVAL;
		rc = wm_dsi_rx_phy_lanes_status(dsi_rx);
	break;
	}

	return rc;
}

static void wm_dsi_rx_toggle_phy_testclk(struct wm_dsi_rx *dsi_rx)
{
	dsi_reg_write(dsi_rx, DSI_RX_PHY_TST_CTRL0, 0x2);
	ndelay(15);
	dsi_reg_write(dsi_rx, DSI_RX_PHY_TST_CTRL0, 0x0);
}

static int wm_dsi_rx_ipi_tx_delay(struct wm_dsi_rx *dsi_rx) {

	int rc = 0;
	struct dsi_ctrl_cfg *ctrl_cfg;
	struct wm_display_mode mode;
	u32 temp, frac;
	u32 hact_ipi, hact_ppi, ipi_tx_delay, hact;
	u32 ppi_bit_width, ppi_clk, ipi_clk;
	u32 nlanes = 4;

	ctrl_cfg = dsi_rx->ctrl_cfg;
	mode = dsi_rx->display->mode_info;
	hact = mode.h_active;
	ppi_clk = 1;/*TODO-get values from SDE*/

	ipi_clk = 1;/*TODO-get values from DV*/
	/* calculate hact_ipi = (Hact/ppc) * Tipi_clk (burst mode)*/
	hact_ipi = ((DIV_ROUND_UP(hact, ctrl_cfg->ppc)) * ipi_clk);

	/* calc hact_ppi = ((6 + Hact*bpp)/ppi_bit_width*nl/8))*Tppi_clk */
	ppi_bit_width = 1;/*TODO-get values from SDE*/
	temp = DIV_ROUND_UP((ppi_bit_width * nlanes),8);
	hact_ppi = (DIV_ROUND_UP((6 + hact * mode.bpp), temp)) * ppi_clk;
	frac = (20 * DIV_ROUND_UP(ppi_clk, ipi_clk));

	if (hact_ppi > hact_ipi) {
		/*delay = ((hact_ppi - hact_ipi))/ipi_clk)+
				20 *(ppi_clk/ipi_clk)+ 4 */
		temp = DIV_ROUND_UP((hact_ppi - hact_ipi), ipi_clk);
		ipi_tx_delay = temp + frac + 4;
	}
	else
		ipi_tx_delay = frac + 4;

	dsi_reg_write(dsi_rx, IPI_TX_DELAY, ipi_tx_delay);
	WM_DEBUG(" hact_ipi:%lu, hact_ppi: %lu, ipi_tx_delay:%lu",
			hact_ipi, hact_ppi, ipi_tx_delay);
	return rc;
}

static void wm_dsi_rx_phy_write(struct wm_dsi_rx *dsi_rx, u32 addr, u32 data)
{
	u32 test_en = 0x8000;
	u32 testcode;

	dsi_reg_write(dsi_rx, DSI_RX_PHY_TST_CTRL0, 0x0);
	dsi_reg_write(dsi_rx, DSI_RX_PHY_TST_CTRL1, test_en);

	wm_dsi_rx_toggle_phy_testclk(dsi_rx);

	/*set testcode MSB*/
	testcode = (addr & 0xF00) >> 8;
	dsi_reg_write(dsi_rx, DSI_RX_PHY_TST_CTRL1, testcode);

	wm_dsi_rx_toggle_phy_testclk(dsi_rx);

	/*set testcode LSB*/
	testcode = (addr & 0x00FF);
	testcode = test_en | testcode;
	dsi_reg_write(dsi_rx, DSI_RX_PHY_TST_CTRL1, testcode);

	wm_dsi_rx_toggle_phy_testclk(dsi_rx);

	dsi_reg_write(dsi_rx, DSI_RX_PHY_TST_CTRL1, data);

}

static int wm_dsi_rx_calc_ipi_params(struct wm_dsi_rx *dsi_rx)
{
	u32 freq_range;
	u32 cfg_clk;
	struct des_en_config_table *des_table;
	struct op_freq_table *op_freq_table;
	int table_size, i;
	u32 hs_freq_range = 0;
	u32 osc_freq = 0;
	u32 osc_freq1 = 0;/*oscfreq[11:8]*/
	u32 osc_freq2 = 0;/*oscfreq[7:0]*/
	u32 bit_rate, des_en_config_val;

	cfg_clk = dsi_rx->ctrl_cfg->Fcfg_clk;
	freq_range = (cfg_clk - 17) * 4;

	/*TODO: get bit_rate*/
	bit_rate = 2500;
	/*get des_en_conifg_value*/
	des_table = des_en_config;
	table_size = ARRAY_SIZE(des_en_config);
	for(i = 0; i < table_size; i++) {
		if((des_table[i].min_Mhz <= freq_range) &&
				(freq_range <= des_table[i].max_Mhz)) {
			des_en_config_val = des_table[i].des_en;
			break;
		}
	}

	if (i == table_size) {
		WM_ERR("DSI RX PHY freq_range is invalid \n");
		goto error;
	}

	/*get hsfreq and oscfreq*/
	op_freq_table = op_freq_val;
	table_size = ARRAY_SIZE(op_freq_val);
	for(i = 0; i < table_size; i++) {
		if((op_freq_table[i].min_rate <= bit_rate) &&
				(bit_rate < op_freq_table[i].max_rate)) {
			hs_freq_range = op_freq_table[i].hsfreqrange;
			osc_freq = op_freq_table[i].osc_freq_target;
			break;
		}
	}

	if (i == table_size) {
		WM_ERR("DSI RX PHY bit_rate is invalid \n");
		goto error;
	}

	/*Configure HS freq Range*/
	wm_dsi_rx_phy_write(dsi_rx, 0x001, 0x20);
	wm_dsi_rx_phy_write(dsi_rx, 0x002, hs_freq_range);
	dsi_reg_write(dsi_rx, VTG_MIPI_HSFREQRANGE, hs_freq_range);
	/*Configure Ref clk freq range*/

	/*Write cfgclkfreqrange in VTG registers*/
	freq_range = 9;/*taken from DV*/
	dsi_reg_write(dsi_rx, VTG_MIPI_CLKCFGFREQRANGE, freq_range);

	/*Write "counter for des in bits[7:4]"*/
	des_en_config_val = des_en_config_val << 4;
	des_en_config_val = des_en_config_val & 0xF0;
	wm_dsi_rx_phy_write(dsi_rx, 0x0E5, 0x01);
	wm_dsi_rx_phy_write(dsi_rx, 0x0E4, des_en_config_val);
	/*Configure oscillation freq[11:8] in addr 0E3
	 * osc_freq[7:0] in addr 0E2 */
	osc_freq1 = osc_freq >> 8;
	osc_freq1 = osc_freq1 & 0xFF;
	osc_freq2 = osc_freq & 0xFF;

	wm_dsi_rx_phy_write(dsi_rx, 0x0E3, osc_freq1);
	wm_dsi_rx_phy_write(dsi_rx, 0x0E2, osc_freq2);

	WM_DEBUG("DSI RX_PHY params: des_en: %lu, hsfreqrange: %lu, oscfreq: %lu\n",
		       des_en_config_val, hs_freq_range, osc_freq);

	return 0;
error:
	return -EINVAL;
}

static void wm_dsi_rx_phy_test_clr(struct wm_dsi_rx *dsi_rx)
{
	dsi_reg_write(dsi_rx, DSI_RX_PHY_TST_CTRL0, 0x1);
	ndelay(15);
	dsi_reg_write(dsi_rx, DSI_RX_PHY_TST_CTRL0, 0x0);
}



/**
 * wm_dsi_rx_soft_reset()- API to reset RX
 */
int wm_dsi_rx_soft_reset(struct wm_dsi_rx *dsi_rx)
{
	dsi_reg_write(dsi_rx, DSI_RX_SOFT_RSTN, 0x0);
	dsi_reg_write(dsi_rx, DSI_RX_SOFT_RSTN, 0x1);
	return 0;
}

/**
 * wm_dsi_rx_disable_ctrl() - API to disable RX
 * @dsi_rx: handle to wm_dsi_rx
 */
static int wm_dsi_rx_disable_ctrl(struct wm_dsi_rx *dsi_rx)
{
	dsi_reg_write(dsi_rx, DSI_RX_SOFT_RSTN, 0x0);
	dsi_reg_write(dsi_rx, DSI_RX_DSC_CTRL, 0x0);

	dsi_rx->dsc->enable = false;
	WM_DEBUG("WM DSI RX disabled\n");
	return 0;
}

/**
 * wm_dsi_rx_set_phy_shutdown() - API to shutdown PHY
 * @dsi_rx: handle to wm_dsi_rx
 */
static int wm_dsi_rx_set_phy_shutdown(struct wm_dsi_rx *dsi_rx)
{

	/*keep PHY in shutdown mode*/
	dsi_reg_write(dsi_rx, DSI_RX_PHY_SHUTDOWN, 0x0);
	dsi_reg_write(dsi_rx, DSI_RX_PHY_RST, 0x0);

	WM_DEBUG("WM DSI RX PHY in shutdown mode\n");
	return 0;
}

/**
 * wm_dsi_rx_phy_reset() - API to reset phy
 * @dsi_rx - handle to wm_dsi_rx
 */
static int wm_dsi_rx_phy_reset(struct wm_dsi_rx *dsi_rx)
{
	/* Toggle PHY reset */
	dsi_reg_write(dsi_rx, DSI_RX_PHY_SHUTDOWN, 0x0);
	dsi_reg_write(dsi_rx, DSI_RX_PHY_RST, 0x0);
	dsi_reg_write(dsi_rx, DSI_RX_PHY_SHUTDOWN, 0x1);
	dsi_reg_write(dsi_rx, DSI_RX_PHY_RST, 0x1);

	WM_DEBUG("WM DSI RX PHY Reset\n");
	return 0;
}

/*
 * wm_dsi_rx_enable_ctrl() - API to enable/disable RX ctrl
 * @dsi_rx: handle to wm_dsi_rx
 * @enable: bool variable to enable/disable
 */
static int wm_dsi_rx_enable_ctrl(struct wm_dsi_rx *dsi_rx, bool enable)
{
	int rc = 0;

	if (enable)
		dsi_reg_write(dsi_rx, DSI_RX_SOFT_RSTN, 0x1);

	else
		dsi_reg_write(dsi_rx, DSI_RX_SOFT_RSTN, 0x0);

	return rc;
}

static void wm_dsc_update_blanking_params(struct wm_dsi_rx *dsi_rx)
{

	struct wm_display_mode *mode = &dsi_rx->display->mode_info;
	u32 vsync, vfront, vback;
	u32 hfront, hsync, htot;
	int hdly = 4 << 3; /*value from databook*/
	int hpc = 1 << 2; /*hfront*/
	int bmod = 1; /*programmed mode*/
	u32 h_pol, v_pol; /*polarity*/
	u32 hpad = 1 << 10; /*enable padding"re-visit"*/
	u32 blk;

	/* DSC_BLK0::htot[31:16] rsvd[15:11]
	 * 	     hpad[10] pol[9:8] hdly[7:3]
	 * 	     hpc[2] bmod[1:0] */

	h_pol = (mode->h_active >= 720 ? 0 : 1) << 8;
	v_pol = (mode->v_active >= 720 ? 0 : 1) << 9;
	htot = mode->h_active + mode->h_back_porch
		+ mode->h_sync_width + mode->h_front_porch;
	blk = htot | hpad | h_pol | v_pol | hdly | hpc | bmod;
	dsi_reg_write(dsi_rx, DSI_RX_DSC_BLK0, blk);

	/*DSC_BLK1::hfront[31:16] hsync[15:0]*/
	hsync = mode->h_sync_width;
	hfront = mode->h_front_porch << 16;
	dsi_reg_write(dsi_rx, DSI_RX_DSC_BLK1, (hsync | hfront));

	/*DSC_BLK2::vback[31:16] vfnt[15:8] vsyn[8:0]*/
	vback = mode->v_back_porch << 16;
	vfront = mode->v_front_porch << 8;
	vsync = mode->v_sync_width;
	dsi_reg_write(dsi_rx, DSI_RX_DSC_BLK2, (vback | vfront | vsync));

	WM_DEBUG("DSC blanking params blk0 %lu, blk1 %lu, blk2 %lu \n", blk,
			(hsync|hfront), (vback|vfront|vsync));
}

static void wm_dsi_rx_dsc_update_pps(struct wm_dsi_rx *dsi_rx)
{
	return;
}
/**
 * wm_dsi_rx_enable_dsc() - API to enable/disable dsc
 * @wm_dsc - handle for wm_dsc
 * @enable - bool to enable/disable dsc
 */
int wm_dsi_rx_enable_dsc(struct wm_dsi_rx *dsi_rx, bool enable)
{

	u32 sbo = 0 << 28; /*single burst o/p*/
	u32 pdm = 1 << 22; /*PDM = 4 [10]*/
	u32 nslc = 2 << 16;  /*2 slices per line*/
	u32 xnslc = 0 << 15; /*custom slice*/
	u32 init = 0 << 8; /*datapath init*/
	u32 epl = 0 << 7;  /*enable partial bytes*/
	u32 epb = 0 << 6;  /*enable partial bits*/
	u32 flal = 0 << 4;
	u32 rbyt = 0 << 3; /*reverse byte order*/
	u32 rbit = 0 << 2; /*reverse bit order*/
	u32 fsel = 1 << 1; /*decoder*/
	u32 data, data_msb, data_lsb;

	data_msb = sbo | pdm | nslc | xnslc | init;

	data_lsb = epl | epb | flal | rbyt | rbit | fsel;

	data = data_msb | data_lsb;

	if (enable) {
		data = data | 0x1;
		dsi_reg_write(dsi_rx, DSI_RX_DSC_CTRL0, data);
	} else {
		dsi_reg_write(dsi_rx, DSI_RX_DSC_CTRL0, 0x0);
		dsi_rx->ctrl_cfg->is_dsc_enabled = false;
	}

	WM_DEBUG("DSC ctrl enabled with 0x%x \n", data);
	return 0;
}

int wm_dsi_rx_configure_dsc(struct wm_dsi_rx *dsi_rx)
{

	/*update blanking params*/
	wm_dsc_update_blanking_params(dsi_rx);

	/*enable dsc*/
	wm_dsi_rx_enable_dsc(dsi_rx, true);

	/*update pps values*/
	wm_dsi_rx_dsc_update_pps(dsi_rx);

	dsi_rx->dsc->enable = true;
	return 0;
}

/**
 * wm_dsi_rx_configure_phy()- API to configure PHY
 * @dsi_rx: handle to wm_dsi_rx
 */
static int wm_dsi_rx_configure_phy(struct wm_dsi_rx *dsi_rx)
{

	int rc = 0;

	/* PHY in Shutdown mode*/
	wm_dsi_rx_set_phy_shutdown(dsi_rx);

	wm_dsi_rx_phy_test_clr(dsi_rx);

	/* Configure Refclk, HS freqclk, DDL oscillatorclk*/
	rc = wm_dsi_rx_calc_ipi_params(dsi_rx);

	if (rc != 0)
		goto error;

	/* Toggle PHY*/
	wm_dsi_rx_phy_reset(dsi_rx);

	/* Verify PHY clk and data lane status*/
	rc = wm_dsi_rx_check_state(dsi_rx, DSI_RX_OP_PHY_READY);

	if (rc)
		goto error;

	return rc;
error:
	WM_ERR("DSI RX PHY configure failed\n");
	return rc;
}

/**
 * wm_dsi_rx_configure_mipi_rx - API to handle mipi rx
 * @dsi_rx: handle to wm_dsi_rx
 */
static int wm_dsi_rx_configure_mipi_rx(struct wm_dsi_rx *dsi_rx)
{
	int rc = 0;

	/* Configure no. of lanes*/
	dsi_reg_write(dsi_rx, DSI_RX_N_LANES, NUM_LANES); //4lanes

	/* Configure mipi irqs*/

	/* Set IPI Video Mode*/
	dsi_reg_write(dsi_rx, DSI_RX_IPI_MODE_CFG, 0x0); //pulse mode

	/* Configure VC*/
	/*Move this to init*/
	/*see if we can get these values from dtsi or tx*/
	dsi_reg_write(dsi_rx, DSI_RX_IPI_VALID_VC_CFG, 0x0);//vc=0

	/* Configure IPI TX delay*/
	wm_dsi_rx_ipi_tx_delay(dsi_rx);

	wm_dsi_rx_configure_phy(dsi_rx);

	WM_INFO("Configured DSI RX successfully\n");

	return rc;
}

/**
 * wm_dsi_rx_mode_set() - API to set mode for RX ctrl
 * @dsi_rx: handle to wm_dsi_rx
 */
static int wm_dsi_rx_mode_set(struct wm_dsi_rx *dsi_rx)
{

	/**no operation in mode_set
	 */
	return 0;
}

/**
 * wm_dsi_rx_pre_enable() - API for pre_enable
 */
static int wm_dsi_rx_pre_enable(struct wm_dsi_rx *dsi_rx)
{
	int rc = 0;
	struct wm_display_mode *mode;

	/* check whether argument is necessary or not*/
	rc = wm_dsi_rx_configure_mipi_rx(dsi_rx);

	mode = &dsi_rx->display->mode_info;
	if (mode->dsc_enabled)
	{
		dsi_rx->ctrl_cfg->is_dsc_enabled = true;
		wm_dsi_rx_configure_dsc(dsi_rx);
	}

	/*Wake mipi core*/
	wm_dsi_rx_enable_ctrl(dsi_rx, true);
	WM_INFO("DSI RX enabled\n");
	return 0;
}

static int wm_dsi_rx_enable(struct wm_dsi_rx *dsi_rx)
{
	return 0;
}

/**
 * wm_dsi_rx_disable() - API for disable
 */
static int wm_dsi_rx_disable(struct wm_dsi_rx *dsi_rx)
{
	wm_dsi_rx_enable_dsc(dsi_rx, false);
	wm_dsi_rx_disable_ctrl(dsi_rx);
	WM_INFO("DSI RX disabled\n");

	return 0;
}

int wm_dsi_rx_get_interrupt_status(struct wm_dsi_rx *dsi_rx)
{
	return 0;
}

int wm_dsi_rx_handle_error_status(struct wm_dsi_rx *dsi_rx)
{
	return 0;
}

static void wm_dsi_rx_configure_tpg_frame(struct wm_dsi_rx *dsi_rx)
{
	u32 h_total = 0;
	struct wm_display_mode mode;

	mode = dsi_rx->display->mode_info;

	/*Configure TPG frame*/
	dsi_reg_write(dsi_rx, DSI_RX_IPI_PG_CFG, 0x4);
	dsi_reg_write(dsi_rx, DSI_RX_IPI_PG_PIXEL_NUM, mode.h_active);
	dsi_reg_write(dsi_rx, DSI_RX_IPI_PG_HSA_TIME, mode.h_sync_width);
	dsi_reg_write(dsi_rx,DSI_RX_IPI_PG_HBP_TIME, mode.h_back_porch);

	h_total = mode.h_active + mode.h_back_porch
		+ mode.h_sync_width + mode.h_front_porch;

	dsi_reg_write(dsi_rx, DSI_RX_IPI_PG_HLINE_TIME, h_total);
	dsi_reg_write(dsi_rx, DSI_RX_IPI_PG_VSA_LINES, mode.v_sync_width);
	dsi_reg_write(dsi_rx, DSI_RX_IPI_PG_VBP_LINES, mode.v_back_porch);
	dsi_reg_write(dsi_rx, DSI_RX_IPI_PG_VFP_LINES, mode.v_front_porch);
	dsi_reg_write(dsi_rx, DSI_RX_IPI_PG_VACTIVE_LINES, mode.v_active );

}

/**
 * wm_dsi_rx_set_tpg() - API to enable TPG
 * @wm_dsi_rx: RX controller handle
 * @enable: variable to enable/disable TPG
 */
int wm_dsi_rx_set_tpg(struct wm_dsi_rx *dsi_rx, bool enable)
{

	int rc = 0;

	if (enable) {
		rc = wm_dsi_rx_check_state(dsi_rx, DSI_RX_OP_TPG);
		if (rc) {
			WM_ERR("Controller state check failed, rc = %d\n");
			goto error;
		}

		/*configure pattern generator frame*/
		wm_dsi_rx_configure_tpg_frame(dsi_rx);

		/*Enable PG*/
		dsi_reg_write(dsi_rx, DSI_RX_IPI_PG_EN, 0x1);
		dsi_rx->ctrl_cfg->is_tpg = true;
		WM_INFO("DSI RX TPG enabled\n");

	} else {
		dsi_reg_write(dsi_rx, DSI_RX_IPI_PG_EN, 0x0);
		dsi_rx->ctrl_cfg->is_tpg = false;
		WM_INFO("DSI RX TPG disabled\n");
	}

	WM_INFO("Set DSI_RX test pattern state = %d\n", enable);

error:
	return 0;
}

static int wm_dsi_rx_deinit(struct wm_display_info *display_info)
{
	/*minor changes need to be done*/

	return 0;
}

struct wm_dsi_rx *wm_dsi_rx_init(struct wm_display_info *display_info)
{
	struct wm_dsi_rx *dsi_rx = NULL;
	int rc = 0;

	if (!display_info || !display_info->dev
		         || !display_info->display) {
		WM_ERR("DSI_RX: invalid arguments");
		rc = -ENODEV;
		goto error;
	}

	dsi_rx = devm_kzalloc(display_info->dev, sizeof(*dsi_rx), GFP_KERNEL);
	if (!dsi_rx) {
		rc = -ENOMEM;
		goto error;
	}
	dsi_rx->display = display_info->display;

	dsi_rx->set_mode = wm_dsi_rx_mode_set;
	dsi_rx->pre_enable = wm_dsi_rx_pre_enable;
	dsi_rx->enable = wm_dsi_rx_enable;
	dsi_rx->disable = wm_dsi_rx_disable;
	dsi_rx->deinit = wm_dsi_rx_deinit;

	wm_dsi_rx_power_init(dsi_rx);

	wm_dsi_rx_sysfs_init(dsi_rx);

	return dsi_rx;


error:
	WM_ERR("DSI RX initialization failed\n");
	return ERR_PTR(rc);
}


