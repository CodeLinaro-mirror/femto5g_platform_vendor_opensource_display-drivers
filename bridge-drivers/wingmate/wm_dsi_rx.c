// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "wm_dsi_rx.h"
#include <linux/delay.h>
#include <linux/bits.h>
#include <drm/drm_dsc.h>

/*TODO
 * 1. debugfs
 * 2. power handling
 * 3. constant values from DV
 * 4. ipi tx delay
 */

#define WM_DSC_PPS_SIZE				128
#define WM_DSC_PPS_PARAMETER_SET_ELEMENTS	96
#define WM_DSC_PPS_WORD_LEN			24
#define ENABLE_PPS_CALC				0

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

static u32 dsc_pps_set[DSC_PPS_TYPE_MAX][WM_DSC_PPS_WORD_LEN] = {
	/*4k60*/
	{0xcd000012, 0x10007810, 0x10000010, 0x80070008,
	0x56052202, 0x1e022000, 0xc001c00,  0xbc016706,
	0x41110018, 0x20140b,   0x33131306, 0x382a1c0e,
	0x69625446, 0x7b797770, 0x9807e7d,  0x1c901a5,
	0xf1ebf9ea, 0xe20cea0c, 0xe24ce22c, 0xda6dda4c,
	0xd28eda6d, 0xd2d5d2b1, 0,          0},
	/*4k50*/
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
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

	off += DSI_RX_BASE;

	return display->read_register(display, off);
}

static int dsi_reg_write(struct wm_dsi_rx *dsi_rx, u32 off, u32 val)
{
	struct wm_display *display;
	display = dsi_rx->display;

	off += DSI_RX_BASE;

	return display->write_register(display, off, val);
}

void wm_dsi_rx_mask_interrupts(struct wm_dsi_rx *dsi_rx)
{
	dsi_reg_write(dsi_rx, DSI_RX_MASK_FIFO_FATAL, 0xFF);
	dsi_reg_write(dsi_rx, DSI_RX_MASK_IPI_FATAL, 0xFF);
	dsi_reg_write(dsi_rx, DSI_RX_MASK_DESKEW_FATAL, 0xFF);
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

static void wm_dsi_rx_handle_error_status(struct work_struct *work)
{
	struct wm_dsi_rx *dsi_rx;

	dsi_rx = container_of(work, struct wm_dsi_rx, err_work);

	wm_dsi_rx_soft_reset(dsi_rx);
}

static int wm_dsi_rx_isr(struct wm_dsi_rx *dsi_rx, int irq)
{
	u32 status = 0;
	u32 error = 0;

	if (!dsi_rx) {
		WM_ERR("invalid data");
		return IRQ_NONE;
	}
	/*check interrupts*/
	status = dsi_reg_read(dsi_rx, DSI_RX_INT_ST_MAIN);

	if (status & BIT(3)) {
		/* IPI fatal errors*/
		error = dsi_reg_read(dsi_rx, DSI_RX_INT_ST_IPI_FATAL);
		WM_ERR("DSI RX IPI FATAL error: 0x%x\n", error);
		queue_work(dsi_rx->workq, &dsi_rx->err_work);
	} else if (status & BIT(4)) {
		/*FIFO fatal errors*/
		error = dsi_reg_read(dsi_rx, DSI_RX_INT_ST_FIFO_FATAL);
		WM_ERR("DSI RX FIFO FATAL error: 0x%x\n", error);
		queue_work(dsi_rx->workq, &dsi_rx->err_work);
	} else if (status & BIT(23)) {
		/*DESKEW fatal errors*/
		error = dsi_reg_read(dsi_rx, DSI_RX_INT_ST_DESKEW_FATAL);
		WM_ERR("DSI RX DESKEW FATAL error: 0x%x\n", error);
		queue_work(dsi_rx->workq, &dsi_rx->err_work);
	} else {
		/*ignore other errors*/
		WM_DEBUG("DSI RX error 0x%x ignored\n", error);
		return IRQ_NONE;
	}

	WM_DEBUG("DSI RX interrupt status is 0x%x\n", status);
	return IRQ_HANDLED;
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

	dsi_reg_write(dsi_rx, DSI_RX_IPI_TX_DELAY, ipi_tx_delay);
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
 * wm_dsi_rx_disable_ctrl() - API to disable RX
 * @dsi_rx: handle to wm_dsi_rx
 */
static int wm_dsi_rx_disable_ctrl(struct wm_dsi_rx *dsi_rx)
{
	dsi_reg_write(dsi_rx, DSI_RX_SOFT_RSTN, 0x0);
	dsi_reg_write(dsi_rx, DSI_RX_DSC_CTRL0, 0x0);

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

static int _get_pps_table_index(struct wm_dsc *dsc)
{
	/** TODO: func will be implemented
	as per data we get for pps values**/
	return 0;
}

#if ENABLE_PPS_CALC
/**
 * if we want to get pps values via dsc_config
 * below is the implementation.
 */
static int wm_dsi_rx_dsc_create_pps_buf(wm_dsc dsc_info, u32 len)
{
	struct drm_dsc_config *dsc = &dsc_info->config;
	char *bp = dsc_info->pps;
	char data;
	u32 *pps_word;
	int i, index_4;
	u32 bpp;

	if (len < WM_DSC_PPS_SIZE)
		return -EINVAL;

	memset(buf, 0, len);
	// pps0
	*bp++ = (dsc->dsc_version_minor |
			dsc->dsc_version_major << 4);
	*bp++ = (0 & 0xff);		// pps1 pps_id =0
	bp++;					// pps2, reserved

	data = dsc->line_buf_depth & 0x0f;
	data |= ((dsc->bits_per_component & 0xf) << DSC_PPS_BPC_SHIFT);
	*bp++ = data;				// pps3

	bpp = dsc->bits_per_pixel;
	if (dsc->native_422 || dsc->native_420)
		bpp = 2 * bpp;
	data = (bpp >> DSC_PPS_MSB_SHIFT);
	data &= 0x03;				// upper two bits
	data |= ((dsc->block_pred_enable & 0x1) << 5);
	data |= ((dsc->convert_rgb & 0x1) << 4);
	data |= ((dsc->simple_422 & 0x1) << 3);
	data |= ((dsc->vbr_enable & 0x1) << 2);
	*bp++ = data;				// pps4
	*bp++ = (bpp & DSC_PPS_LSB_MASK);	// pps5

	*bp++ = ((dsc->pic_height >> 8) & 0xff); // pps6
	*bp++ = (dsc->pic_height & 0x0ff);	// pps7
	*bp++ = ((dsc->pic_width >> 8) & 0xff);	// pps8
	*bp++ = (dsc->pic_width & 0x0ff);	// pps9

	*bp++ = ((dsc->slice_height >> 8) & 0xff);// pps10
	*bp++ = (dsc->slice_height & 0x0ff);	// pps11
	*bp++ = ((dsc->slice_width >> 8) & 0xff); // pps12
	*bp++ = (dsc->slice_width & 0x0ff);	// pps13

	*bp++ = ((dsc->slice_chunk_size >> 8) & 0xff);// pps14
	*bp++ = (dsc->slice_chunk_size & 0x0ff);	// pps15

	*bp++ = (dsc->initial_xmit_delay >> 8) & 0x3; // pps16
	*bp++ = (dsc->initial_xmit_delay & 0xff);// pps17

	*bp++ = ((dsc->initial_dec_delay >> 8) & 0xff); // pps18
	*bp++ = (dsc->initial_dec_delay & 0xff);// pps19

	bp++;				// pps20, reserved

	*bp++ = (dsc->initial_scale_value & 0x3f); // pps21

	*bp++ = ((dsc->scale_increment_interval >> 8) & 0xff); // pps22
	*bp++ = (dsc->scale_increment_interval & 0xff); // pps23

	*bp++ = ((dsc->scale_decrement_interval >> 8) & 0xf); // pps24
	*bp++ = (dsc->scale_decrement_interval & 0x0ff);// pps25

	bp++;					// pps26, reserved

	*bp++ = (dsc->first_line_bpg_offset & 0x1f);// pps27

	*bp++ = ((dsc->nfl_bpg_offset >> 8) & 0xff);// pps28
	*bp++ = (dsc->nfl_bpg_offset & 0x0ff);	// pps29
	*bp++ = ((dsc->slice_bpg_offset >> 8) & 0xff);// pps30
	*bp++ = (dsc->slice_bpg_offset & 0x0ff);// pps31

	*bp++ = ((dsc->initial_offset >> 8) & 0xff);// pps32
	*bp++ = (dsc->initial_offset & 0x0ff);	// pps33

	*bp++ = ((dsc->final_offset >> 8) & 0xff);// pps34
	*bp++ = (dsc->final_offset & 0x0ff);	// pps35

	*bp++ = (dsc->flatness_min_qp & 0x1f);	// pps36
	*bp++ = (dsc->flatness_max_qp & 0x1f);	// pps37

	*bp++ = ((dsc->rc_model_size >> 8) & 0xff);// pps38
	*bp++ = (dsc->rc_model_size & 0x0ff);	// pps39

	*bp++ = (dsc->rc_edge_factor & 0x0f);	// pps40

	*bp++ = (dsc->rc_quant_incr_limit0 & 0x1f);	// pps41
	*bp++ = (dsc->rc_quant_incr_limit1 & 0x1f);	// pps42

	data = ((dsc->rc_tgt_offset_high & 0xf) << 4);
	data |= (dsc->rc_tgt_offset_low & 0x0f);
	*bp++ = data;				// pps43

	for (i = 0; i < DSC_NUM_BUF_RANGES - 1; i++)
		*bp++ = (dsc->rc_buf_thresh[i] & 0xff); // pps44 - pps57

	for (i = 0; i < DSC_NUM_BUF_RANGES; i++) {
		// pps58 - pps87
		data = (dsc->rc_range_params[i].range_min_qp & 0x1f);
		data <<= 3;
		data |= ((dsc->rc_range_params[i].range_max_qp >> 2) & 0x07);
		*bp++ = data;
		data = (dsc->rc_range_params[i].range_max_qp & 0x03);
		data <<= 6;
		data |= (dsc->rc_range_params[i].range_bpg_offset & 0x3f);
		*bp++ = data;
	}

	if (dsc->dsc_version_minor == 0x2) {
		if (dsc->native_422)
			data = BIT(0);
		else if (dsc->native_420)
			data = BIT(1);
		*bp++ = data;				// pps88
		*bp++ = dsc->second_line_bpg_offset;	// pps89

		*bp++ = ((dsc->nsl_bpg_offset >> 8) & 0xff);// pps90
		*bp++ = (dsc->nsl_bpg_offset & 0x0ff);	// pps91

		*bp++ = ((dsc->second_line_offset_adj >> 8) & 0xff); // pps92
		*bp++ = (dsc->second_line_offset_adj & 0x0ff);	// pps93

		// rest bytes are reserved and set to 0
	}

	//converting pps values to word
	pps_word = dsc_info->pps_word;
	dsc_info->pps_len = WM_DSC_PPS_PARAMETER_SET_ELEMENTS;
	dsc_info->pps_word_len = dsc->pps_len >> 2;

	for (i = 0; i < dsc_info->pps_word_len; i++) {
		index_4 = i << 2;
		pps_word[i] = bp[index_4 + 0] << 0 |
				bp[index_4 + 1] << 8 |
				bp[index_4 + 2] << 16 |
				bp[index_4 + 3] << 24;
	}
	return 0;
}
#endif

/**
 * If pps_word is directly taken from lookup table
 * depending on the mode (4k60 or 4k50) then use below
 * function.*/
static int wm_dsi_rx_dsc_create_pps_buf(struct wm_dsc *dsc_info, u32 len)
{
	u32 i;
	int index;
	int pps_word_len;

	if (len < WM_DSC_PPS_SIZE)
		return -EINVAL;

	index = _get_pps_table_index(dsc_info);
	pps_word_len = WM_DSC_PPS_WORD_LEN;

	for (i = 0; i < pps_word_len; i++)
		dsc_info->pps_word[i] = dsc_pps_set[index][i];

	return 0;
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
	u32 *pps_word;
	int i;
	struct wm_dsc *dsc;

	dsc = dsi_rx->dsc;

	/*create pps buffer with values with pps_id as zero*/
	wm_dsi_rx_dsc_create_pps_buf(dsc, sizeof(dsc->pps));

	pps_word = dsc->pps_word;
	dsc->pps_len = WM_DSC_PPS_PARAMETER_SET_ELEMENTS;
	dsc->pps_word_len = dsc->pps_len >> 2;

	/*Moved this part to creat_buf func
	for (i = 0; i < dsc->pps_word_len; i++) {
		index_4 = i << 2;
		pps_word[i] = pps[index_4 + 0] << 0 |
				pps[index_4 + 1] << 8 |
				pps[index_4 + 2] << 16 |
				pps[index_4 + 3] << 24;
	}*/

	/* write pps values into pps registers*/
	for (i = 0; i < dsc->pps_word_len; i++) {
		/* confirm spi_w32 for setting pps regs*/
		 dsi_reg_write(dsi_rx, DSI_RX_DSC_PPS0_3 + (i << 2), pps_word[i]);
	}

	WM_DEBUG("WM DSC PPS updated successfully\n");
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

static int wm_dsi_rx_dsc_set_pps(struct wm_dsi_rx *dsi_rx)
{
	u32 data;
	int i;
	int retry_count = 10;/*subject to chnage*/

	data = dsi_reg_read(dsi_rx, DSI_RX_DSC_CTRL0);
	data |= BIT(31);

	/*update PPS_UPD to 1*/
	dsi_reg_write(dsi_rx, DSI_RX_DSC_CTRL0, data);

	/*poll for PPS_UPD to clear*/
	for (i = 0; i < retry_count; i++) {
		data = dsi_reg_read(dsi_rx, DSI_RX_DSC_CTRL0);
		if (data & BIT(31))
		       return 0;
		usleep_range(20, 50);/*subject to  change*/
	}

	WM_ERR("DSC RX PPS update failed\n");
	return -1;
}

int wm_dsi_rx_configure_dsc(struct wm_dsi_rx *dsi_rx)
{

	int rc = 0;

	/*update blanking params*/
	wm_dsc_update_blanking_params(dsi_rx);

	/*enable dsc*/
	wm_dsi_rx_enable_dsc(dsi_rx, true);

	/*update pps values*/
	wm_dsi_rx_dsc_update_pps(dsi_rx);

	rc = wm_dsi_rx_dsc_set_pps(dsi_rx);
	/*for now, ignoring return value
	 * as it expected to be success
	 * all the time*/

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

static int wm_dsi_rx_create_workqueue(struct wm_dsi_rx *dsi_rx)
{
	dsi_rx->workq = create_singlethread_workqueue("wm_dsi_wq");

	if (IS_ERR_OR_NULL(dsi_rx->workq)) {
		WM_ERR("DSI RX creating workqueue failed");
		return -ENOMEM;
	}

	INIT_WORK(&dsi_rx->err_work, wm_dsi_rx_handle_error_status);

	return 0;
}
static int wm_dsi_rx_deinit(struct wm_dsi_rx *dsi_rx)
{
	/*minor changes need to be done*/

	if (dsi_rx->workq) {
		cancel_work_sync(&dsi_rx->err_work);
		destroy_workqueue(dsi_rx->workq);
	}

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
	dsi_rx->irq_handler = wm_dsi_rx_isr;

	wm_dsi_rx_power_init(dsi_rx);

	rc = wm_dsi_rx_create_workqueue(dsi_rx);

	if (rc)
		goto error;

	wm_dsi_rx_sysfs_init(dsi_rx);

	return dsi_rx;


error:
	WM_ERR("DSI RX initialization failed\n");
	return ERR_PTR(rc);
}


