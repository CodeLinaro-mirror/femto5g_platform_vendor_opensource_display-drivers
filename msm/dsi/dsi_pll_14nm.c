// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/iopoll.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include "dsi_pll_14nm.h"

#define VCO_DELAY_USEC 1

#define MHZ_250	 250000000UL
#define MHZ_500	 500000000UL
#define MHZ_1000	1000000000UL
#define MHZ_1100	1100000000UL
#define MHZ_1900	1900000000UL
#define MHZ_3000	3000000000UL

#define CEIL(x, y)      (((x) + ((y)-1)) / (y))
struct dsi_pll_regs {
	u32 decimal_div_start;
        u32 pll_txclk_en;       /* reg: 0x04c0 */
        u32 dec_start;          /* reg: 0x0490 */
        u32 div_frac_start;     /* reg: 0x04b4, 0x4b8, 0x04bc */
        u32 ssc_period;         /* reg: 0x04a0, 0x04a4 */
        u32 ssc_step_size;      /* reg: 0x04a8, 0x04ac */
        u32 plllock_cmp;        /* reg: 0x047c, 0x0480, 0x0484 */
        u32 pll_vco_div_ref;    /* reg: 0x046c, 0x0470 */
        u32 pll_vco_count;      /* reg: 0x0474, 0x0478 */
        u32 pll_kvco_div_ref;   /* reg: 0x0440, 0x0444 */
        u32 pll_kvco_count;     /* reg: 0x0448, 0x044c */
        u32 pll_misc1;          /* reg: 0x04e8 */
        u32 pll_lpf2_postdiv;   /* reg: 0x0504 */
        u32 pll_resetsm_cntrl;  /* reg: 0x042c */
        u32 pll_resetsm_cntrl2; /* reg: 0x0430 */
        u32 pll_resetsm_cntrl5; /* reg: 0x043c */
        u32 pll_kvco_code;              /* reg: 0x0458 */

        u32 cmn_clk_cfg0;       /* reg: 0x0010 */
        u32 cmn_clk_cfg1;       /* reg: 0x0014 */
        u32 cmn_ldo_cntrl;      /* reg: 0x004c */

        u32 pll_postdiv;        /* vco */
};

struct dsi_pll_config {
	u32 ref_freq;
	u32 output_div;
	bool ignore_frac;
	bool disable_prescaler;
	bool enable_ssc;
	u32 dec_bits;
	u32 frac_bits;
	u32 ssc_offset;

        u32 fref;       /* 19.2 Mhz, reference clk */
        u32 fdata;      /* bit clock rate */
        u32 dsiclk_sel; /* 1, reg: 0x0014 */
        u32 n2div;      /* 1, reg: 0x0010, bit 4-7 */
        u32 ldo_en;     /* 0,  reg: 0x004c, bit 0 */

        /* fixed  */
        u32 refclk_dbler_en;    /* 0, reg: 0x04c0, bit 1 */
        u32 vco_measure_time;   /* 5, unknown */
        u32 kvco_measure_time;  /* 5, unknown */
        u32 bandgap_timer;      /* 4, reg: 0x0430, bit 3 - 5 */
        u32 pll_wakeup_timer;   /* 5, reg: 0x043c, bit 0 - 2 */
        u32 plllock_cnt;        /* 1, reg: 0x0488, bit 1 - 2 */
        u32 plllock_rng;        /* 1, reg: 0x0488, bit 3 - 4 */
        u32 ssc_center;         /* 0, reg: 0x0494, bit 1 */
        u32 ssc_adj_period;     /* 37, reg: 0x498, bit 0 - 9 */
        u32 ssc_spread;         /* 0.005  */
        u32 ssc_freq;           /* unknown */
        u32 pll_ie_trim;        /* 4, reg: 0x0400 */
        u32 pll_ip_trim;        /* 4, reg: 0x0404 */
        u32 pll_iptat_trim;     /* reg: 0x0410 */
        u32 pll_cpcset_cur;     /* 1, reg: 0x04f0, bit 0 - 2 */
        u32 pll_cpmset_cur;     /* 1, reg: 0x04f0, bit 3 - 5 */

        u32 pll_icpmset;        /* 4, reg: 0x04fc, bit 3 - 5 */
        u32 pll_icpcset;        /* 4, reg: 0x04fc, bit 0 - 2 */

        u32 pll_icpmset_p;      /* 0, reg: 0x04f4, bit 0 - 2 */
        u32 pll_icpmset_m;      /* 0, reg: 0x04f4, bit 3 - 5 */

        u32 pll_icpcset_p;      /* 0, reg: 0x04f8, bit 0 - 2 */
        u32 pll_icpcset_m;      /* 0, reg: 0x04f8, bit 3 - 5 */

        u32 pll_lpf_res1;       /* 3, reg: 0x0504, bit 0 - 3 */
        u32 pll_lpf_cap1;       /* 11, reg: 0x0500, bit 0 - 3 */
        u32 pll_lpf_cap2;       /* 1, reg: 0x0500, bit 4 - 7 */
        u32 pll_c3ctrl;         /* 2, reg: 0x04c4 */
        u32 pll_r3ctrl;         /* 1, reg: 0x04c4 */
};

struct dsi_pll_14nm {
	struct dsi_pll_resource *rsc;
	struct dsi_pll_config pll_configuration;
	struct dsi_pll_regs reg_setup;
	bool cphy_enabled;
};

static inline bool dsi_pll_14nm_is_hw_revision(
		struct dsi_pll_resource *rsc)
{
	return (rsc->pll_revision == DSI_PLL_14NM) ?
		true : false;
}

static inline void dsi_pll_set_pll_post_div(struct dsi_pll_resource *pll, u32
		pll_post_div)
{
	u32 pll_post_div_val = 0;

	if (pll_post_div == 1)
		pll_post_div_val = 0;
	if (pll_post_div == 2)
		pll_post_div_val = 1;
	if (pll_post_div == 4)
		pll_post_div_val = 2;
	if (pll_post_div == 8)
		pll_post_div_val = 3;

}

static inline int dsi_pll_get_pll_post_div(struct dsi_pll_resource *pll)
{
	u32 reg_val = 0;

	return (1 << reg_val);
}

static inline void dsi_pll_set_phy_post_div(struct dsi_pll_resource *pll, u32
		phy_post_div)
{
	u32 reg_val = 0;

	reg_val = DSI_PLL_REG_R(pll->phy_base, PHY_CMN_CLK_CFG0);
	reg_val &= ~0x0F;
	reg_val |= phy_post_div;
	DSI_PLL_REG_W(pll->phy_base, PHY_CMN_CLK_CFG0, reg_val);
	/* For slave PLL, this divider always should be set to 1 */
	if (pll->slave) {
		reg_val = DSI_PLL_REG_R(pll->phy_base, PHY_CMN_CLK_CFG0);
		reg_val &= ~0x0F;
		reg_val |= 0x1;
		DSI_PLL_REG_W(pll->slave->phy_base, PHY_CMN_CLK_CFG0, reg_val);
	}
}


static inline int dsi_pll_get_phy_post_div(struct dsi_pll_resource *pll)
{
	u32 reg_val = 0;

	reg_val = DSI_PLL_REG_R(pll->phy_base, PHY_CMN_CLK_CFG0);

	return (reg_val & 0xF);
}


static inline void dsi_pll_set_dsi_clk(struct dsi_pll_resource *pll, u32
		dsi_clk)
{
	u32 reg_val = 0;

	reg_val = DSI_PLL_REG_R(pll->phy_base, PHY_CMN_CLK_CFG1);
	reg_val &= ~0x3;
	reg_val |= dsi_clk;
	DSI_PLL_REG_W(pll->phy_base, PHY_CMN_CLK_CFG1, reg_val);
	if (pll->slave) {
		reg_val = DSI_PLL_REG_R(pll->slave->phy_base, PHY_CMN_CLK_CFG1);
		reg_val &= ~0x3;
		reg_val |= dsi_clk;
		DSI_PLL_REG_W(pll->slave->phy_base, PHY_CMN_CLK_CFG1, reg_val);
	}
}

static inline int dsi_pll_get_dsi_clk(struct dsi_pll_resource *pll)
{
	u32 reg_val;

	reg_val = DSI_PLL_REG_R(pll->phy_base, PHY_CMN_CLK_CFG1);

	return (reg_val & 0x3);
}

static inline void dsi_pll_set_pclk_div(struct dsi_pll_resource *pll, u32
		pclk_div)
{
	u32 reg_val = 0;

	reg_val = DSI_PLL_REG_R(pll->phy_base, PHY_CMN_CLK_CFG0);
	reg_val &= ~0xF0;
	reg_val |= (pclk_div << 4);
	DSI_PLL_REG_W(pll->phy_base, PHY_CMN_CLK_CFG0, reg_val);
	if (pll->slave) {
		reg_val = DSI_PLL_REG_R(pll->slave->phy_base, PHY_CMN_CLK_CFG0);
		reg_val &= ~0xF0;
		reg_val |= (pclk_div << 4);
		DSI_PLL_REG_W(pll->slave->phy_base, PHY_CMN_CLK_CFG0, reg_val);
	}
}

static inline int dsi_pll_get_pclk_div(struct dsi_pll_resource *pll)
{
	u32 reg_val;

	reg_val = DSI_PLL_REG_R(pll->phy_base, PHY_CMN_CLK_CFG0);

	return ((reg_val & 0xF0) >> 4);
}

static struct dsi_pll_resource *pll_rsc_db[DSI_PLL_MAX];
static struct dsi_pll_14nm plls[DSI_PLL_MAX];

static void dsi_pll_config_slave(struct dsi_pll_resource *rsc)
{
	u32 reg;
	struct dsi_pll_resource *orsc = pll_rsc_db[DSI_PLL_1];

	if (!rsc)
		return;

	/* Only DSI PLL0 can act as a master */
	if (rsc->index != DSI_PLL_0)
		return;

	/* default configuration: source is either internal or ref clock */
	rsc->slave = NULL;

	if (!orsc) {
		DSI_PLL_WARN(rsc,
			"slave PLL unavilable, assuming standalone config\n");
		return;
	}

	/* check to see if the source of DSI1 PLL bitclk is set to external */
	reg = DSI_PLL_REG_R(orsc->phy_base, PHY_CMN_CLK_CFG1);
	reg &= (BIT(2) | BIT(3));
	if (reg == 0x04)
		rsc->slave = pll_rsc_db[DSI_PLL_1]; /* external source */

	DSI_PLL_DBG(rsc, "Slave PLL %s\n",
			rsc->slave ? "configured" : "absent");
}

static void dsi_pll_setup_config(struct dsi_pll_14nm *pll,
				 struct dsi_pll_resource *rsc)
{
	struct dsi_pll_config *config = &pll->pll_configuration;
	struct dsi_pll_regs *regs = &pll->reg_setup;

	config->ref_freq = 19200000;
	config->output_div = 1;
	config->dec_bits = 8;
	config->frac_bits = 20;
	config->ssc_freq = 31500;
	config->ssc_offset = 4800;

	config->ignore_frac = false;
	config->disable_prescaler = true;
	config->enable_ssc = rsc->ssc_en;
	config->ssc_center = rsc->ssc_center;

	if (config->enable_ssc) {
		if (rsc->ssc_freq)
			config->ssc_freq = rsc->ssc_freq;
		if (rsc->ssc_ppm) {
			config->ssc_offset = rsc->ssc_ppm;
			config->ssc_spread = rsc->ssc_ppm / 1000;
		}
	}

        config->fref = 19200000;        /* 19.2 Mhz*/
        config->fdata = 0;              /* bit clock rate */
        config->dsiclk_sel = 1;         /* 1, reg: 0x0014 */
        config->ldo_en = 0;             /* 0,  reg: 0x004c, bit 0 */

        /* fixed  input */
        config->refclk_dbler_en = 0;    /* 0, reg: 0x04c0, bit 1 */
        config->vco_measure_time = 5;   /* 5, unknown */
        config->kvco_measure_time = 5;  /* 5, unknown */
        config->bandgap_timer = 4;      /* 4, reg: 0x0430, bit 3 - 5 */
        config->pll_wakeup_timer = 5;   /* 5, reg: 0x043c, bit 0 - 2 */
        config->plllock_cnt = 1;        /* 1, reg: 0x0488, bit 1 - 2 */
        config->plllock_rng = 0;        /* 0, reg: 0x0488, bit 3 - 4 */
        config->ssc_adj_period = 37;    /* 37, reg: 0x498, bit 0 - 9 */

        config->pll_ie_trim = 4;        /* 4, reg: 0x0400 */
        config->pll_ip_trim = 4;        /* 4, reg: 0x0404 */
        config->pll_cpcset_cur = 0;     /* 1, reg: 0x04f0, bit 0 - 2 */
        config->pll_cpmset_cur = 1;     /* 1, reg: 0x04f0, bit 3 - 5 */
        config->pll_icpmset = 4;        /* 4, reg: 0x04fc, bit 3 - 5 */
        config->pll_icpcset = 4;        /* 4, reg: 0x04fc, bit 0 - 2 */
        config->pll_icpmset_p = 0;      /* 0, reg: 0x04f4, bit 0 - 2 */
        config->pll_icpmset_m = 0;      /* 0, reg: 0x04f4, bit 3 - 5 */
        config->pll_icpcset_p = 0;      /* 0, reg: 0x04f8, bit 0 - 2 */
        config->pll_icpcset_m = 0;      /* 0, reg: 0x04f8, bit 3 - 5 */
        config->pll_lpf_res1 = 3;       /* 3, reg: 0x0504, bit 0 - 3 */
        config->pll_lpf_cap1 = 11;      /* 11, reg: 0x0500, bit 0 - 3 */
        config->pll_lpf_cap2 = 1;       /* 1, reg: 0x0500, bit 4 - 7 */
        config->pll_iptat_trim = 7;
        config->pll_c3ctrl = 2;         /* 2 */
        config->pll_r3ctrl = 1;         /* 1 */
	regs->pll_postdiv = 1;
}

static void dsi_pll_calc_dec_frac(struct dsi_pll_14nm *pll,
				  struct dsi_pll_resource *rsc)
{
	struct dsi_pll_config *config = &pll->pll_configuration;
	struct dsi_pll_regs *regs = &pll->reg_setup;
	u64 fref = rsc->vco_ref_clk_rate;
	u64 pll_freq;
	u64 divider;
	u64 dec, dec_multiple;
	u32 frac;
	u64 multiplier, pll_comp_val;
	s32 duration;

	pll_freq = rsc->vco_current_rate;

	if (config->disable_prescaler)
		divider = fref;
	else
		divider = fref * 2;

	multiplier = 1 << config->frac_bits;
	dec_multiple = div_u64(pll_freq * multiplier, divider);
	div_u64_rem(dec_multiple, multiplier, &frac);

	dec = div_u64(dec_multiple, multiplier);

	regs->dec_start = (u32)dec;
	regs->div_frac_start = frac;

	if (config->plllock_cnt == 0)
                duration = 1024;
        else if (config->plllock_cnt == 1)
                duration = 256;
        else if (config->plllock_cnt == 2)
                duration = 128;
        else
                duration = 32;

        pll_comp_val =  duration * dec_multiple;
        pll_comp_val =  div_u64(pll_comp_val, multiplier);
        do_div(pll_comp_val, 10);

        regs->plllock_cmp = (u32)pll_comp_val;

        regs->pll_txclk_en = 1;
	regs->cmn_ldo_cntrl = 0x1c;
}

static u32 dsi_pll_14nm_kvco_slop(u32 vrate)
{
        u32 slop = 0;

        if (vrate > 1300000000UL && vrate <= 1800000000UL)
                slop =  600;
        else if (vrate > 1800000000UL && vrate < 2300000000UL)
                slop = 400;
        else if (vrate > 2300000000UL && vrate < 2600000000UL)
                slop = 280;

        return slop;
}

static void dsi_pll_14nm_calc_vco_count(struct dsi_pll_14nm *pll,
				struct dsi_pll_resource *rsc)
{
	struct dsi_pll_config *config = &pll->pll_configuration;
	struct dsi_pll_regs *regs = &pll->reg_setup;

        u64 data;
        u32 cnt, temp_cnt;
	s64 vco_clk_rate, fref;

        pr_info("%s:\n", __func__);
	vco_clk_rate = rsc->vco_current_rate;
	fref = rsc->vco_ref_clk_rate;
        data = fref * config->vco_measure_time;
        do_div(data, 1000000);
        data &= 0x03ff; /* 10 bits */
        data -= 2;
        regs->pll_vco_div_ref = data;
        pr_info("fref = %lld meas_time = %u vco_div_ref = %llu\n",
                fref, config->vco_measure_time, data);

        data = (unsigned long)vco_clk_rate / 1000000;   /* unit is Mhz */
        data *= config->vco_measure_time;
        do_div(data, 10);
        regs->pll_vco_count = data; /* reg: 0x0474, 0x0478 */

        data = fref * config->kvco_measure_time;
        do_div(data, 1000000);
        data &= 0x03ff; /* 10 bits */
        data -= 1;
        regs->pll_kvco_div_ref = data;

        cnt = dsi_pll_14nm_kvco_slop(vco_clk_rate);
        temp_cnt = cnt;
        cnt *= 2;
        cnt /= 100;
        cnt *= config->kvco_measure_time;
        regs->pll_kvco_count = cnt;

        pr_info("vco_clk_rate = %lld, vco_cnt = %u, kvco_div_ref = %u kvco_slop = %u, kvco_cnt = %u\n",
                vco_clk_rate, regs->pll_vco_count, regs->pll_kvco_div_ref, temp_cnt, regs->pll_kvco_count);
        regs->pll_misc1 = 16;
        regs->pll_resetsm_cntrl = 48;
        regs->pll_resetsm_cntrl2 = config->bandgap_timer << 3;
        regs->pll_resetsm_cntrl5 = config->pll_wakeup_timer;
        regs->pll_kvco_code = 0;
}

static void dsi_pll_calc_ssc(struct dsi_pll_14nm *pll,
		  struct dsi_pll_resource *rsc)
{
	struct dsi_pll_config *config = &pll->pll_configuration;
	struct dsi_pll_regs *regs = &pll->reg_setup;
	u32 period, ssc_period;
        u32 ref, rem;
        s64 step_size;

	if (!config->enable_ssc) {
		DSI_PLL_DBG(rsc, "SSC not enabled\n");
		return;
	}

	if (!rsc) {
		DSI_PLL_DBG(rsc, "rsc is null\n");
		return;
	}

        ssc_period = config->ssc_freq / 500;
        period = (unsigned long)rsc->vco_ref_clk_rate / 1000;
        ssc_period  = CEIL(period, ssc_period);
        ssc_period -= 1;
        regs->ssc_period = ssc_period;

	DSI_PLL_DBG(rsc, "ssc, freq=%d spread=%d period=%d\n",
		config->ssc_freq, config->ssc_spread, regs->ssc_period);

        step_size = (u32)rsc->vco_current_rate;
        ref = rsc->vco_ref_clk_rate;
        ref /= 1000;
        step_size = div_s64(step_size, ref);
        step_size <<= 20;
        step_size = div_s64(step_size, 1000);
        step_size *= config->ssc_spread;
        step_size = div_s64(step_size, 1000);
        step_size *= (config->ssc_adj_period + 1);

        rem = 0;
        step_size = div_s64_rem(step_size, ssc_period + 1, &rem);
        if (rem)
                step_size++;

        step_size &= 0x0ffff;   /* take lower 16 bits */

        regs->ssc_step_size = step_size;

	DSI_PLL_DBG(rsc, "SSC: div_per:0x%X, stepsize:0x%X, adjper:0x%X\n",
			ssc_period, (u32)step_size, config->ssc_adj_period);
}

static void dsi_pll_ssc_commit(struct dsi_pll_14nm *pll,
		struct dsi_pll_resource *rsc)
{
	void __iomem *pll_base = rsc->pll_base;
	struct dsi_pll_regs *regs = &pll->reg_setup;
	struct dsi_pll_config *config = &pll->pll_configuration;
	char data;

	if (!config->enable_ssc) {
		DSI_PLL_DBG(rsc, "SSC not enabled\n");
		return;
	}
	DSI_PLL_DBG(rsc, "SSC commit\n");
        data = config->ssc_adj_period;
        data &= 0x0ff;
        DSI_PLL_REG_W(pll_base, PLL_SSC_ADJ_PER1, data);
        data = (config->ssc_adj_period >> 8);
        data &= 0x03;
        DSI_PLL_REG_W(pll_base, PLL_SSC_ADJ_PER2, data);

        data = regs->ssc_period;
        data &= 0x0ff;
        DSI_PLL_REG_W(pll_base, PLL_SSC_PER1, data);
        data = (regs->ssc_period >> 8);
        data &= 0x0ff;
        DSI_PLL_REG_W(pll_base, PLL_SSC_PER2, data);

        data = regs->ssc_step_size;
        data &= 0x0ff;
        DSI_PLL_REG_W(pll_base, PLL_SSC_STEP_SIZE1, data);
        data = (regs->ssc_step_size >> 8);
        data &= 0x0ff;
        DSI_PLL_REG_W(pll_base, PLL_SSC_STEP_SIZE2, data);

        data = (config->ssc_center & 0x01);
        data <<= 1;
        data |= 0x01; /* enable */
        DSI_PLL_REG_W(pll_base, PLL_SSC_EN_CENTER, data);

        wmb();  /* make sure register committed */
}


static void dsi_pll_detect_phy_mode(struct dsi_pll_14nm *pll,
				    struct dsi_pll_resource *rsc)
{
	pll->cphy_enabled = false;
}

static void dsi_pll_commit_common(struct dsi_pll_14nm *pll,
				struct dsi_pll_resource *rsc)
{
        void __iomem *pll_base = rsc->pll_base;
        struct dsi_pll_regs *reg = &pll->reg_setup;
        struct dsi_pll_config *config = &pll->pll_configuration;
        char data;

        /* confgiure the non frequency dependent pll registers */
        data = 0;
        DSI_PLL_REG_W(pll_base, PLL_SYSCLK_EN_RESET, data);

        /* PLL_CLKBUFLR_EN updated at dsi phy */

        data = reg->pll_txclk_en;
        DSI_PLL_REG_W(pll_base, PLL_TXCLK_EN, data);

        data = reg->pll_resetsm_cntrl;
        DSI_PLL_REG_W(pll_base, PLL_RESETSM_CNTRL, data);
        data = reg->pll_resetsm_cntrl2;
        DSI_PLL_REG_W(pll_base, PLL_RESETSM_CNTRL2, data);
        data = reg->pll_resetsm_cntrl5;
        DSI_PLL_REG_W(pll_base, PLL_RESETSM_CNTRL5, data);

        data = reg->pll_vco_div_ref;
        data &= 0x0ff;
        DSI_PLL_REG_W(pll_base, PLL_VCO_DIV_REF1, data);
        data = (reg->pll_vco_div_ref >> 8);
        data &= 0x03;
        DSI_PLL_REG_W(pll_base, PLL_VCO_DIV_REF2, data);

        data = reg->pll_kvco_div_ref;
        data &= 0x0ff;
        DSI_PLL_REG_W(pll_base, PLL_KVCO_DIV_REF1, data);
        data = (reg->pll_kvco_div_ref >> 8);
        data &= 0x03;
        DSI_PLL_REG_W(pll_base, PLL_KVCO_DIV_REF2, data);

        data = reg->pll_misc1;
        DSI_PLL_REG_W(pll_base, PLL_PLL_MISC1, data);

        data = config->pll_ie_trim;
	    DSI_PLL_REG_W(pll_base, PLL_IE_TRIM, data);

        data = config->pll_ip_trim;
        DSI_PLL_REG_W(pll_base, PLL_IP_TRIM, data);

        data = ((config->pll_cpmset_cur << 3) | config->pll_cpcset_cur);
        DSI_PLL_REG_W(pll_base, PLL_CP_SET_CUR, data);

        data = ((config->pll_icpcset_p << 3) | config->pll_icpcset_m);
        DSI_PLL_REG_W(pll_base, PLL_PLL_ICPCSET, data);

        data = ((config->pll_icpmset_p << 3) | config->pll_icpcset_m);
        DSI_PLL_REG_W(pll_base, PLL_PLL_ICPMSET, data);

        data = ((config->pll_icpmset << 3) | config->pll_icpcset);
        DSI_PLL_REG_W(pll_base, PLL_PLL_ICP_SET, data);

        data = ((config->pll_lpf_cap2 << 4) | config->pll_lpf_cap1);
        DSI_PLL_REG_W(pll_base, PLL_PLL_LPF1, data);

        data = config->pll_iptat_trim;
        DSI_PLL_REG_W(pll_base, PLL_IPTAT_TRIM, data);

        data = (config->pll_c3ctrl | (config->pll_r3ctrl << 4));
        DSI_PLL_REG_W(pll_base, PLL_PLL_CRCTRL, data);
}


static void dsi_pll_commit(struct dsi_pll_14nm *pll,
			   struct dsi_pll_resource *rsc)
{
	void __iomem *pll_base = rsc->pll_base;
	struct dsi_pll_regs *reg = &pll->reg_setup;
	struct dsi_pll_config *config = &pll->pll_configuration;
        char data;

        data = reg->cmn_ldo_cntrl;
        DSI_PLL_REG_W(pll_base, PHY_CMN_LDO_CNTRL, data);

        dsi_pll_commit_common(pll, rsc);

        /* de assert pll start and apply pll sw reset */
        /* stop pll */
        DSI_PLL_REG_W(pll_base, PHY_CMN_PLL_CNTRL, 0);

        /* pll sw reset */
        DSI_PLL_REG_W(pll_base, PHY_CMN_CTRL_1, 0x20);
        wmb();  /* make sure register committed */
        udelay(10);

        DSI_PLL_REG_W(pll_base, PHY_CMN_CTRL_1, 0);
        wmb();  /* make sure register committed */

        data = config->dsiclk_sel; /* set dsiclk_sel = 1  */
        DSI_PLL_REG_W(pll_base, PHY_CMN_CLK_CFG1, data);

        data = 0xff; /* data, clk, pll normal operation */
        DSI_PLL_REG_W(pll_base, PHY_CMN_CTRL_0, data);

        /* confgiure the frequency dependent pll registers */
        data = reg->dec_start;
        DSI_PLL_REG_W(pll_base, PLL_DEC_START, data);

        data = reg->div_frac_start;
        data &= 0x0ff;
        DSI_PLL_REG_W(pll_base, PLL_DIV_FRAC_START1, data);
        data = (reg->div_frac_start >> 8);
        data &= 0x0ff;
        DSI_PLL_REG_W(pll_base, PLL_DIV_FRAC_START2, data);
        data = (reg->div_frac_start >> 16);
        data &= 0x0f;
        DSI_PLL_REG_W(pll_base, PLL_DIV_FRAC_START3, data);

        data = reg->plllock_cmp;
	data &= 0x0ff;
	DSI_PLL_REG_W(pll_base, PLL_PLLLOCK_CMP1, data);
        data = (reg->plllock_cmp >> 8);
        data &= 0x0ff;
        DSI_PLL_REG_W(pll_base, PLL_PLLLOCK_CMP2, data);
        data = (reg->plllock_cmp >> 16);
        data &= 0x03;
        DSI_PLL_REG_W(pll_base, PLL_PLLLOCK_CMP3, data);

        data = ((config->plllock_cnt << 1) | (config->plllock_rng << 3));
        DSI_PLL_REG_W(pll_base, PLL_PLLLOCK_CMP_EN, data);

        data = reg->pll_vco_count;
        data &= 0x0ff;
        DSI_PLL_REG_W(pll_base, PLL_VCO_COUNT1, data);
        data = (reg->pll_vco_count >> 8);
        data &= 0x0ff;
        DSI_PLL_REG_W(pll_base, PLL_VCO_COUNT2, data);

        data = reg->pll_kvco_count;
        data &= 0x0ff;
        DSI_PLL_REG_W(pll_base, PLL_KVCO_COUNT1, data);
        data = (reg->pll_kvco_count >> 8);
        data &= 0x03;
        DSI_PLL_REG_W(pll_base, PLL_KVCO_COUNT2, data);

        /*
         * tx_band = pll_postdiv
         * 0: divided by 1 <== for now
         * 1: divided by 2
         * 2: divided by 4
         * 3: divided by 8
         */
        data = (((reg->pll_postdiv) << 4) | config->pll_lpf_res1);
        DSI_PLL_REG_W(pll_base, PLL_PLL_LPF2_POSTDIV, data);

	wmb();  /* make sure register committed */
}

static int dsi_pll_14nm_lock_status(struct dsi_pll_resource *pll)
{
	int rc = 0;
	u32 status;
	u32 const delay_us = 15;
	u32 const timeout_us = 1000;
	/* poll for PLL ready status */
	if ((rc = DSI_READ_POLL_TIMEOUT_ATOMIC_GEN(pll->pll_base, pll->index,
                        PLL_RESET_SM_READY_STATUS,
                        status,
                        ((status & BIT(5)) > 0),
			delay_us,
			timeout_us)) == -ETIMEDOUT) {
		DSI_PLL_ERR(pll, "DSI PLL ndx=%d status=%x failed to Lock\n",
			pll->index, status);
        } else if ((rc = DSI_READ_POLL_TIMEOUT_ATOMIC_GEN(pll->pll_base,
				pll->index,
                                PLL_RESET_SM_READY_STATUS,
                                status,
                                ((status & BIT(0)) > 0),
				delay_us,
				timeout_us)) == -ETIMEDOUT) {
			DSI_PLL_ERR(pll, "DSI PLL ndx=%d status=%x PLL not ready\n",
				pll->index, status);
	}

	if (rc)
		DSI_PLL_ERR(pll, "lock failed, status=0x%08x\n", status);

	return rc;
}

static void dsi_pll_disable_pll_bias(struct dsi_pll_resource *rsc)
{
	u32 data = DSI_PLL_REG_R(rsc->phy_base, PHY_CMN_CTRL_0);

	DSI_PLL_REG_W(rsc->phy_base, PHY_CMN_CTRL_0, data & ~BIT(7));
	ndelay(250);
}

static void dsi_pll_enable_pll_bias(struct dsi_pll_resource *rsc)
{
	u32 data = DSI_PLL_REG_R(rsc->phy_base, PHY_CMN_CTRL_0);

	DSI_PLL_REG_W(rsc->phy_base, PHY_CMN_CTRL_0, data | BIT(7));
	ndelay(250);
}

static void dsi_pll_disable_sub(struct dsi_pll_resource *rsc)
{
	dsi_pll_disable_pll_bias(rsc);
}

static void dsi_pll_unprepare_stub(struct clk_hw *hw)
{
	return;
}

static int dsi_pll_prepare_stub(struct clk_hw *hw)
{
	return 0;
}

static int dsi_pll_set_rate_stub(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	return 0;
}

static long dsi_pll_byteclk_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *parent_rate)
{
	struct dsi_pll_clk *pll = to_pll_clk_hw(hw);
	struct dsi_pll_resource *pll_res = pll->priv;

	return pll_res->byteclk_rate;
}

static long dsi_pll_pclk_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *parent_rate)
{
	struct dsi_pll_clk *pll = to_pll_clk_hw(hw);
	struct dsi_pll_resource *pll_res = pll->priv;

	return pll_res->pclk_rate;
}

static unsigned long dsi_pll_vco_recalc_rate(struct dsi_pll_resource *pll)
{
	u64 ref_clk;
	u64 multiplier;
	u32 frac;
	u32 dec;
	u32 pll_post_div;
	u64 pll_freq, tmp64;
	u64 vco_rate;
	struct dsi_pll_14nm *pll_14nm;
	struct dsi_pll_config *config;

	ref_clk = pll->vco_ref_clk_rate;
	pll_14nm = pll->priv;
	if (!pll_14nm) {
		DSI_PLL_ERR(pll, "pll configuration not found\n");
		return -EINVAL;
	}

	config = &pll_14nm->pll_configuration;

	dec = DSI_PLL_REG_R(pll->pll_base, PLL_DEC_START);
	dec &= 0xFF;

	frac = DSI_PLL_REG_R(pll->pll_base, PLL_DIV_FRAC_START1);
	frac |= ((DSI_PLL_REG_R(pll->pll_base, PLL_DIV_FRAC_START2) & 0xFF)
					<< 8);
	frac |= ((DSI_PLL_REG_R(pll->pll_base, PLL_DIV_FRAC_START3) & 0x3)
					<< 16);

	multiplier = 1 << config->frac_bits;
	pll_freq = dec * (ref_clk * 2);
	tmp64 = (ref_clk * 2 * frac);
	pll_freq += div_u64(tmp64, multiplier);

	pll_post_div = dsi_pll_get_pll_post_div(pll);

	vco_rate = div_u64(pll_freq, pll_post_div);

	return vco_rate;
}

static unsigned long dsi_pll_byteclk_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct dsi_pll_clk *byte_pll = to_pll_clk_hw(hw);
	struct dsi_pll_resource *pll = NULL;
	u64 vco_rate = 0;
	u64 byte_rate = 0;
	u32 phy_post_div;

	if (!byte_pll->priv) {
		DSI_PLL_INFO(pll, "pll priv is null\n");
		return 0;
	}

	pll = byte_pll->priv;

	/*
	 * In the case when byteclk rate is set, the recalculation function
	 * should  return the current rate. Recalc rate is also called during
	 * clock registration, during which the function should reverse
	 * calculate clock rates that were set as part of UEFI.
	 */
	if (pll->byteclk_rate != 0) {
		DSI_PLL_DBG(pll, "returning byte clk rate = %lld %lld\n",
				pll->byteclk_rate, parent_rate);
		return  pll->byteclk_rate;
	}

	vco_rate = dsi_pll_vco_recalc_rate(pll);

	phy_post_div = dsi_pll_get_phy_post_div(pll);
	byte_rate = div_u64(vco_rate, phy_post_div);

	if (pll->type == DSI_PHY_TYPE_DPHY)
		byte_rate = div_u64(byte_rate, 8);
	else
		byte_rate = div_u64(byte_rate, 7);

	return byte_rate;
}

static unsigned long dsi_pll_pclk_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct dsi_pll_clk *pix_pll = to_pll_clk_hw(hw);
	struct dsi_pll_resource *pll = NULL;
	u64 vco_rate = 0;
	u64 pclk_rate = 0;
	u32 phy_post_div, pclk_div;

	if (!pix_pll->priv) {
		DSI_PLL_INFO(pll, "pll priv is null\n");
		return 0;
	}

	pll = pix_pll->priv;

	/*
	 * In the case when pclk rate is set, the recalculation function
	 * should  return the current rate. Recalc rate is also called during
	 * clock registration, during which the function should reverse
	 * calculate the clock rates that were set as part of UEFI.
	 */
	if (pll->pclk_rate != 0) {
		DSI_PLL_DBG(pll, "returning pclk rate = %lld %lld\n",
				pll->pclk_rate, parent_rate);
		return pll->pclk_rate;
	}

	vco_rate = dsi_pll_vco_recalc_rate(pll);

	if (pll->type == DSI_PHY_TYPE_DPHY) {
		phy_post_div = dsi_pll_get_phy_post_div(pll);
		pclk_rate = div_u64(vco_rate, phy_post_div);
		pclk_rate = div_u64(pclk_rate, 2);
		pclk_div = dsi_pll_get_pclk_div(pll);
		pclk_rate = div_u64(pclk_rate, pclk_div);
	} else {
		pclk_rate = vco_rate * 2;
		pclk_rate = div_u64(pclk_rate, 7);
		pclk_div = dsi_pll_get_pclk_div(pll);
		pclk_rate = div_u64(pclk_rate, pclk_div);
	}

	return pclk_rate;
}

static const struct clk_ops pll_byteclk_ops = {
	.recalc_rate = dsi_pll_byteclk_recalc_rate,
	.set_rate = dsi_pll_set_rate_stub,
	.round_rate = dsi_pll_byteclk_round_rate,
	.prepare = dsi_pll_prepare_stub,
	.unprepare = dsi_pll_unprepare_stub,
};

static const struct clk_ops pll_pclk_ops = {
	.recalc_rate = dsi_pll_pclk_recalc_rate,
	.set_rate = dsi_pll_set_rate_stub,
	.round_rate = dsi_pll_pclk_round_rate,
	.prepare = dsi_pll_prepare_stub,
	.unprepare = dsi_pll_unprepare_stub,
};

/*
 * Clock tree for generating DSI byte and pclk.
 *
 *
 *  +-------------------------------+		+----------------------------+
 *  |    dsi_phy_pll_out_byteclk    |		|    dsi_phy_pll_out_dsiclk  |
 *  +---------------+---------------+		+--------------+-------------+
 *                  |                                          |
 *                  |                                          |
 *                  v                                          v
 *            dsi_byte_clk                                  dsi_pclk
 *
 *
 */

static struct dsi_pll_clk dsi0_phy_pll_out_byteclk = {
	.hw.init = &(struct clk_init_data){
			.name = "dsi0_phy_pll_out_byteclk",
			.ops = &pll_byteclk_ops,
	},
};

static struct dsi_pll_clk dsi1_phy_pll_out_byteclk = {
	.hw.init = &(struct clk_init_data){
			.name = "dsi1_phy_pll_out_byteclk",
			.ops = &pll_byteclk_ops,
	},
};

static struct dsi_pll_clk dsi0_phy_pll_out_dsiclk = {
	.hw.init = &(struct clk_init_data){
			.name = "dsi0_phy_pll_out_dsiclk",
			.ops = &pll_pclk_ops,
	},
};

static struct dsi_pll_clk dsi1_phy_pll_out_dsiclk = {
	.hw.init = &(struct clk_init_data){
			.name = "dsi1_phy_pll_out_dsiclk",
			.ops = &pll_pclk_ops,
	},
};

int dsi_pll_clock_register_14nm(struct platform_device *pdev,
				  struct dsi_pll_resource *pll_res)
{
	int rc = 0, ndx;
	struct clk *clk;
	struct clk_onecell_data *clk_data;
	int num_clks = 4;

	if (!pdev || !pdev->dev.of_node ||
			!pll_res || !pll_res->pll_base || !pll_res->phy_base) {
		DSI_PLL_ERR(pll_res, "Invalid params\n");
		return -EINVAL;
	}

	ndx = pll_res->index;

	if (ndx >= DSI_PLL_MAX) {
		DSI_PLL_ERR(pll_res, "not supported\n");
		return -EINVAL;
	}

	pll_rsc_db[ndx] = pll_res;
	plls[ndx].rsc = pll_res;
	pll_res->priv = &plls[ndx];
	pll_res->vco_delay = VCO_DELAY_USEC;
	pll_res->vco_min_rate = 1300000000;
	pll_res->vco_ref_clk_rate = 19200000UL;

	dsi_pll_setup_config(pll_res->priv, pll_res);

	clk_data = devm_kzalloc(&pdev->dev, sizeof(struct clk_onecell_data),
					GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->clks = devm_kzalloc(&pdev->dev, (num_clks *
				sizeof(struct clk *)), GFP_KERNEL);
	if (!clk_data->clks)
		return -ENOMEM;

	clk_data->clk_num = num_clks;

	/* Establish client data */
	if (ndx == 0) {
		dsi0_phy_pll_out_byteclk.priv = pll_res;
		dsi0_phy_pll_out_dsiclk.priv = pll_res;

		clk = devm_clk_register(&pdev->dev,
				&dsi0_phy_pll_out_byteclk.hw);
		if (IS_ERR(clk)) {
			DSI_PLL_ERR(pll_res,
				"clk registration failed for DSI clock\n");
			rc = -EINVAL;
			goto clk_register_fail;
		}
		clk_data->clks[0] = clk;

		clk = devm_clk_register(&pdev->dev,
				&dsi0_phy_pll_out_dsiclk.hw);
		if (IS_ERR(clk)) {
			DSI_PLL_ERR(pll_res,
				"clk registration failed for DSI clock\n");
			rc = -EINVAL;
			goto clk_register_fail;
		}
		clk_data->clks[1] = clk;


		rc = of_clk_add_provider(pdev->dev.of_node,
				of_clk_src_onecell_get, clk_data);
	} else {
		dsi1_phy_pll_out_byteclk.priv = pll_res;
		dsi1_phy_pll_out_dsiclk.priv = pll_res;


		clk = devm_clk_register(&pdev->dev,
				&dsi1_phy_pll_out_byteclk.hw);
		if (IS_ERR(clk)) {
			DSI_PLL_ERR(pll_res,
				"clk registration failed for DSI clock\n");
			rc = -EINVAL;
			goto clk_register_fail;
		}
		clk_data->clks[2] = clk;

		clk = devm_clk_register(&pdev->dev,
				&dsi1_phy_pll_out_dsiclk.hw);
		if (IS_ERR(clk)) {
			DSI_PLL_ERR(pll_res,
				"clk registration failed for DSI clock\n");
			rc = -EINVAL;
			goto clk_register_fail;
		}
		clk_data->clks[3] = clk;

		rc = of_clk_add_provider(pdev->dev.of_node,
				of_clk_src_onecell_get, clk_data);
	}
	if (!rc) {
		DSI_PLL_INFO(pll_res, "Registered clocks successfully\n");

		return rc;
	}
clk_register_fail:
	return rc;
}

static int dsi_pll_14nm_set_byteclk_div(struct dsi_pll_resource *pll,
		bool commit)
{

	int i = 0;
	int table_size;
	u32 pll_post_div = 0, phy_post_div = 0;
	struct dsi_pll_div_table *table;
	u64 bitclk_rate;
	u64 const phy_rate_split = 1500000000UL;

	if (pll->type == DSI_PHY_TYPE_DPHY) {
		bitclk_rate = pll->byteclk_rate * 8;

		if (bitclk_rate <= phy_rate_split) {
			table = pll_14nm_dphy_lb;
			table_size = ARRAY_SIZE(pll_14nm_dphy_lb);
		} else {
			table = pll_14nm_dphy_hb;
			table_size = ARRAY_SIZE(pll_14nm_dphy_hb);
		}
	} else {
		bitclk_rate = pll->byteclk_rate * 7;

		if (bitclk_rate <= phy_rate_split) {
			table = pll_14nm_cphy_lb;
			table_size = ARRAY_SIZE(pll_14nm_cphy_lb);
		} else {
			table = pll_14nm_cphy_hb;
			table_size = ARRAY_SIZE(pll_14nm_cphy_hb);
		}
	}

	for (i = 0; i < table_size; i++) {
		if ((table[i].min_hz <= bitclk_rate) &&
				(bitclk_rate <= table[i].max_hz)) {
			pll_post_div = table[i].pll_div;
			phy_post_div = table[i].phy_div;
			break;
		}
	}

	DSI_PLL_DBG(pll, "bit clk rate: %llu, pll_post_div: %d, phy_post_div: %d\n",
			bitclk_rate, pll_post_div, phy_post_div);

	if (commit) {
		dsi_pll_set_phy_post_div(pll, phy_post_div);
	}

	pll->vco_rate = bitclk_rate * pll_post_div * phy_post_div;

	return 0;
}

static int dsi_pll_calc_dphy_pclk_div(struct dsi_pll_resource *pll)
{
	u32 m_val, n_val; /* M and N values of MND trio */
	u32 pclk_div;

	if (pll->bpp == 30 && pll->lanes == 4) {
		/* RGB101010 */
		m_val = 2;
		n_val = 3;
	} else if (pll->bpp == 18 && pll->lanes == 2) {
		/* RGB666_packed */
		m_val = 2;
		n_val = 9;
	} else if (pll->bpp == 18 && pll->lanes == 4) {
		/* RGB666_packed */
		m_val = 4;
		n_val = 9;
	} else if (pll->bpp == 16 && pll->lanes == 3) {
		/* RGB565 */
		m_val = 3;
		n_val = 8;
	} else {
		m_val = 1;
		n_val = 1;
	}

	/* Calculating pclk_div assuming dsiclk_sel to be 1 */
	pclk_div = pll->bpp;
	pclk_div = mult_frac(pclk_div, m_val, n_val);
	do_div(pclk_div, 2);
	do_div(pclk_div, pll->lanes);

	DSI_PLL_DBG(pll, "bpp: %d, lanes: %d, m_val: %u, n_val: %u, pclk_div: %u\n",
                          pll->bpp, pll->lanes, m_val, n_val, pclk_div);

	return pclk_div;
}

static int dsi_pll_calc_cphy_pclk_div(struct dsi_pll_resource *pll)
{
	u32 m_val, n_val; /* M and N values of MND trio */
	u32 pclk_div;
	u32 phy_post_div = dsi_pll_get_phy_post_div(pll);

	if (pll->bpp == 24 && pll->lanes == 2) {
		/*
		 * RGB888 or DSC is enabled
		 * Skipping DSC enabled check
		 */
		m_val = 2;
		n_val = 3;
	} else if (pll->bpp == 30) {
		/* RGB101010 */
		if (pll->lanes == 1) {
			m_val = 4;
			n_val = 15;
		} else {
			m_val = 16;
			n_val = 35;
		}
	} else if (pll->bpp == 18) {
		/* RGB666_packed */
		if (pll->lanes == 1) {
			m_val = 8;
			n_val = 63;
		} else if (pll->lanes == 2) {
			m_val = 16;
			n_val = 63;
		} else if (pll->lanes == 3) {
			m_val = 8;
			n_val = 21;
		} else {
			m_val = 1;
			n_val = 1;
		}
	} else if (pll->bpp == 16 && pll->lanes == 3) {
		/* RGB565 */
		m_val = 3;
		n_val = 7;
	} else {
		m_val = 1;
		n_val = 1;
	}

	/* Calculating pclk_div assuming dsiclk_sel to be 3 */
	pclk_div =  pll->bpp * phy_post_div;
	pclk_div = mult_frac(pclk_div, m_val, n_val);
	do_div(pclk_div, 8);
	do_div(pclk_div, pll->lanes);

	DSI_PLL_DBG(pll, "bpp: %d, lanes: %d, m_val: %u, n_val: %u, phy_post_div: %u pclk_div: %u\n",
                          pll->bpp, pll->lanes, m_val, n_val, phy_post_div, pclk_div);

	return pclk_div;
}

static int dsi_pll_14nm_set_pclk_div(struct dsi_pll_resource *pll, bool commit)
{

	int dsi_clk = 0, pclk_div = 0;
	u64 pclk_src_rate;
	u32 pll_post_div;
	u32 phy_post_div;

	pll_post_div = dsi_pll_get_pll_post_div(pll);
	pclk_src_rate = div_u64(pll->vco_rate, pll_post_div);
	if (pll->type == DSI_PHY_TYPE_DPHY) {
		dsi_clk = 0x1;
		phy_post_div = dsi_pll_get_phy_post_div(pll);
		pclk_src_rate = div_u64(pclk_src_rate, phy_post_div);
		pclk_src_rate = div_u64(pclk_src_rate, 2);
		pclk_div = dsi_pll_calc_dphy_pclk_div(pll);
	} else {
		dsi_clk = 0x3;
		pclk_src_rate *= 2;
		pclk_src_rate = div_u64(pclk_src_rate, 7);
		pclk_div = dsi_pll_calc_cphy_pclk_div(pll);
	}

	pll->pclk_rate = div_u64(pclk_src_rate, pclk_div);

	DSI_PLL_DBG(pll, "pclk rate: %llu, dsi_clk: %d, pclk_div: %d\n",
			pll->pclk_rate, dsi_clk, pclk_div);

	if (commit) {
		dsi_pll_set_dsi_clk(pll, dsi_clk);
		dsi_pll_set_pclk_div(pll, pclk_div);
	}

	return 0;

}

static int dsi_pll_14nm_vco_set_rate(struct dsi_pll_resource *pll_res)
{
	struct dsi_pll_14nm *pll;

	pll = pll_res->priv;
	if (!pll) {
		DSI_PLL_ERR(pll_res, "pll configuration not found\n");
		return -EINVAL;
	}

	DSI_PLL_DBG(pll_res, "rate=%lu\n", pll_res->vco_rate);

	pll_res->vco_current_rate = pll_res->vco_rate;

	dsi_pll_detect_phy_mode(pll, pll_res);

	dsi_pll_calc_dec_frac(pll, pll_res);

	dsi_pll_14nm_calc_vco_count(pll, pll_res);

	dsi_pll_calc_ssc(pll, pll_res);

	dsi_pll_commit(pll, pll_res);

	dsi_pll_ssc_commit(pll, pll_res);

	/* flush, ensure all register writes are done*/
	wmb();

	return 0;
}

static int dsi_pll_read_stored_trim_codes(struct dsi_pll_resource *pll_res,
					  unsigned long vco_clk_rate)
{
	int i;
	bool found = false;

	if (!pll_res || !pll_res->dfps) {
		DSI_PLL_ERR(pll_res, "pll configuration not found\n");
		return -EINVAL;
	}

	for (i = 0; i < pll_res->dfps->vco_rate_cnt; i++) {
		struct dfps_codes_info *codes_info =
			&pll_res->dfps->codes_dfps[i];

		DSI_PLL_DBG(pll_res, "valid=%d vco_rate=%d, code %d %d %d\n",
			codes_info->is_valid, codes_info->clk_rate,
			codes_info->pll_codes.pll_codes_1,
			codes_info->pll_codes.pll_codes_2,
			codes_info->pll_codes.pll_codes_3);

		if (vco_clk_rate != codes_info->clk_rate &&
				codes_info->is_valid)
			continue;

		pll_res->cache_pll_trim_codes[0] =
			codes_info->pll_codes.pll_codes_1;
		pll_res->cache_pll_trim_codes[1] =
			codes_info->pll_codes.pll_codes_2;
		pll_res->cache_pll_trim_codes[2] =
			codes_info->pll_codes.pll_codes_3;
		found = true;
		break;
	}

	if (!found)
		return -EINVAL;

	DSI_PLL_DBG(pll_res, "trim_code_0=0x%x trim_code_1=0x%x trim_code_2=0x%x\n",
			pll_res->cache_pll_trim_codes[0],
			pll_res->cache_pll_trim_codes[1],
			pll_res->cache_pll_trim_codes[2]);

	return 0;
}

static void dsi_pll_14nm_dynamic_refresh(struct dsi_pll_14nm *pll,
					struct dsi_pll_resource *rsc)
{
	/* TODO check if any change need for 14nm */
}

static int dsi_pll_14nm_dynamic_clk_vco_set_rate(struct dsi_pll_resource *rsc)
{
	int rc;
	struct dsi_pll_14nm *pll;
	u32 rate;

	if (!rsc) {
		DSI_PLL_ERR(rsc, "pll resource not found\n");
		return -EINVAL;
	}
	DSI_PLL_ERR(rsc, "dynamic_clk_vco_set_rate\n");
	rate = rsc->vco_rate;
	pll = rsc->priv;
	if (!pll) {
		DSI_PLL_ERR(rsc, "pll configuration not found\n");
		return -EINVAL;
	}

	rc = dsi_pll_read_stored_trim_codes(rsc, rate);
	if (rc) {
		DSI_PLL_ERR(rsc, "cannot find pll codes rate=%ld\n", rate);
		return -EINVAL;
	}

	DSI_PLL_DBG(rsc, "ndx=%d, rate=%lu\n", rsc->index, rate);
	rsc->vco_current_rate = rate;

	dsi_pll_calc_dec_frac(pll, rsc);

	/* program dynamic refresh control registers */
	dsi_pll_14nm_dynamic_refresh(pll, rsc);

	return 0;
}

static int dsi_pll_14nm_enable(struct dsi_pll_resource *rsc)
{
	int rc = 0;

	DSI_PLL_REG_W(rsc->pll_base, PLL_VREF_CFG1, 0x10);
	/* Start PLL */
	DSI_PLL_REG_W(rsc->phy_base, PHY_CMN_PLL_CNTRL, 0x01);

	/*
	 * ensure all PLL configurations are written prior to checking
	 * for PLL lock.
	 */
	wmb();

	/* Check for PLL lock */
	rc = dsi_pll_14nm_lock_status(rsc);
	if (rc)
		DSI_PLL_ERR(rsc, "lock failed\n");

	return rc;
}

static int dsi_pll_14nm_disable(struct dsi_pll_resource *rsc)
{
	int rc = 0;

	DSI_PLL_DBG(rsc, "stop PLL\n");

	DSI_PLL_REG_W(rsc->phy_base, PHY_CMN_PLL_CNTRL, 0);
	dsi_pll_disable_sub(rsc);
	if (rsc->slave) {
		dsi_pll_disable_sub(rsc->slave);
	}
	/* flush, ensure all register writes are done*/
	wmb();

	return rc;
}

int dsi_pll_14nm_configure(void *pll, bool commit)
{

	int rc = 0;
	struct dsi_pll_resource *rsc = (struct dsi_pll_resource *)pll;

	dsi_pll_config_slave(rsc);

	/* PLL power needs to be enabled before accessing PLL registers */
	dsi_pll_enable_pll_bias(rsc);
	if (rsc->slave)
		dsi_pll_enable_pll_bias(rsc->slave);

	rc = dsi_pll_14nm_set_byteclk_div(rsc, commit);

	if (commit) {
		rc = dsi_pll_14nm_set_pclk_div(rsc, commit);
		rc = dsi_pll_14nm_vco_set_rate(rsc);
	} else {
		rc = dsi_pll_14nm_dynamic_clk_vco_set_rate(rsc);
	}

	return 0;
}

int dsi_pll_14nm_toggle(void *pll, bool prepare)
{
	int rc = 0;
	struct dsi_pll_resource *pll_res = (struct dsi_pll_resource *)pll;

	if (!pll_res) {
		DSI_PLL_ERR(pll_res, "dsi pll resources are not available\n");
		return -EINVAL;
	}

	if (prepare) {
		rc = dsi_pll_14nm_enable(pll_res);
		if (rc)
			DSI_PLL_ERR(pll_res, "enable failed: %d\n", rc);
	} else {
		rc = dsi_pll_14nm_disable(pll_res);
		if (rc)
			DSI_PLL_ERR(pll_res, "disable failed: %d\n", rc);
	}

	return rc;
}
