// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/of_irq.h>
#include <drm/drm_edid.h>
#include <drm/drm_file.h>

#include "wm_debug.h"
#include "wm_hdmi_tx.h"

#define WM_CEA_EXT 0x02
#define TAG_CODE 0x07
#define PHY_PLL_TIMEOUT_MS 100 // Need to confirm this value.

#define spi_read8(reg) \
	hdmi->display->read_register(hdmi->display, (reg))

#define spi_write8(reg, data) \
	hdmi->display->update_register_bits(hdmi->display, (reg), \
			WM_HDMI_REG_MASK, (data))

#define HDMI_MPLL(ds, cur, gmp) \
	.opmode_pllcfg = (ds), .pllcurrctrl = (cur), .pllgmpctrl = (gmp)

#define _wm_for_each_cea_db(cea, i, start, end) \
	for ((i) = (start); \
	(i) < (end) && (i) + _wm_cea_db_payload_len(&(cea)[(i)]) < (end); \
	(i) += _wm_cea_db_payload_len(&(cea)[(i)]) + 1)

enum pll_groups {
	GROUP_0,
	GROUP_1,
	GROUP_2,
	GROUP_3,
	GROUP_4,
	GROUP_5,
};

enum wm_hdmi_phy_conf {
	PDZ = BIT(7),
	ENTMDS = BIT(6),
	SVSRET = BIT(5),
	PDDQ = BIT(4),
	TXPWRON = BIT(3),
	ENHPDRXSENSE = BIT(2),
	SELDATAENPOL = BIT(1),
	SELDIPIF = BIT(0),
};

enum wm_hdmi_phy {
	TXPHYLOCK = BIT(0),
	HPD = BIT(1),
	RXSENSE_0 = BIT(4),
	RXSENSE_1 = BIT(5),
	RXSENSE_2 = BIT(6),
	RXSENSE_3 = BIT(7),
};

enum wm_hdmi_ih_mute {
	IH_HPD = BIT(0),
	IH_TXPHYLOCK = BIT(1),
	IH_RXSENSE_0 = BIT(2),
	IH_RXSENSE_1 = BIT(3),
	IH_RXSENSE_2 = BIT(4),
	IH_RXSENSE_3 = BIT(5),
};

enum drm_edid_colorimetry {
	xvYCC601 = BIT(0),
	xvYCC709 = BIT(1),
	sYCC601 = BIT(2),
	opYCC601 = BIT(3),
	opRGB = BIT(4),
	BT2020cYCC = BIT(5),
	BT2020YCC = BIT(6),
	BT2020RGB = BIT(7),
};


struct wm_hdmi_tx_mpll {
	u16 opmode_pllcfg;
	u16 pllcurrctrl;
	u16 pllgmpctrl;
};

/* This list is subject to extension in future,
 * based on the necessity to parse new
 * E-EDID extension blocks.
 */
enum extended_data_block_types {
	COLORIMETRY_EXTENDED_DATA_BLOCK = 0x05,
};

/* MPLL LUT for 8bpc color depth without pixel repetition.
 *
 * TODO: Update table and LUT mapper accordingly for Clocks with
 * Pixel Repetition.
 */
static const struct wm_hdmi_tx_mpll divider_list[] = {
	/* GROUP_0
	 * PClk: 25175KHz, 27000KHz
	 */
	{ HDMI_MPLL(0x00b3, 050, 0b10) },

	/* GROUP_1
	 * PClk: 54000KHz, 59400KHz, 72000KHz,
	 * 	 74250KHz, 82500KHz, 90000KHz
	 */
	{ HDMI_MPLL(0x0072, 032, 0b10) },

	/* GROUP_2
	 * PClk: 99000KHz, 108000KHz,
	 * 	 118800KHz, 148500KHz, 165000KHz
	 */
	{ HDMI_MPLL(0x0051, 012, 0b10) },

	/* GROUP_3
	 * PClk: 186250KHz, 297000KHz
	 */
	{ HDMI_MPLL(0x0040, 001, 0b10) },

	/* GROUP_4
	 * PClk: 198000KHz
	 */
	{ HDMI_MPLL(0x0100, 001, 0b10) },

	/* GROUP_5
	 * PClk: 371250KHz, 396000KHz,
	 * 	 495000KHz, 594000KHz
	 */
	{ HDMI_MPLL(0x1a40, 001, 0b11) },
};

/* MPLL LUT for 10boc color depth withour pixel repetition.
 *
 * TODO: Update table and LUT mapper accordingly for Clocks with
 * Pixel Repetition.
 */
static const struct wm_hdmi_tx_mpll divider_list_dc[] = {
	/* GROUP_0
	 * PClk: 25175KHz, 27000KHz
	 */
	{ HDMI_MPLL(0x2153, 060, 0b10) },

	/* GROUP_1
	 * PClk: 54000KHz, 59400KHz, 72000KHz
	 */
	{ HDMI_MPLL(0x2142, 032, 0b10) },

	/* GROUP_2
	 * PClk: 74250KHz, 82500KHz, 90000KHz,
	 * 	 99000KHz, 108000KHz, 118800KHz
	 */
	{ HDMI_MPLL(0x2145, 032, 0b10) },

	/* GROUP_3
	 * PClk: 148500KHz, 165000KHz,
	 * 	 186250KHz, 198000KHz
	 */
	{ HDMI_MPLL(0x214c, 032, 0b10) },

	/* GROUP_4
	 * PClk: 297000KHz, 371250KHz,
	 * 	 396000KHz
	 */
	{ HDMI_MPLL(0x3b4c, 061, 0b11) },
};

static u8 *_wm_find_edid_extension(struct edid *edid, int ext_id)
{
	u8 *edid_ext = NULL;
	int i;

	/* No EDID or EDID extensions */
	if (!edid || !edid->extensions)
		return NULL;

	/* Find CEA extension */
	for (i = 0; i < edid->extensions; i++) {
		edid_ext = (u8 *)edid + EDID_LENGTH * (i + 1);
		if (edid_ext[0] == ext_id)
			break;
	}

	if (i == edid->extensions)
		return NULL;

	return edid_ext;
}

static u8 *_wm_find_cea_extension(struct edid *edid)
{
	return _wm_find_edid_extension(edid, WM_CEA_EXT);
}

static int _wm_cea_db_payload_len(const u8 *db)
{
	return db[0] & 0x1f;
}

static int _wm_cea_db_tag(const u8 *db)
{
	return db[0] >> 5;
}

static int _wm_cea_revision(const u8 *cea)
{
	return cea[1];
}

static int _wm_cea_db_offsets(const u8 *cea, int *start, int *end)
{
	/* Data block offset in CEA extension block */
	*start = 4;
	*end = cea[2];
	if (*end == 0)
		*end = 127;
	if (*end < 4 || *end > 127)
		return -ERANGE;
	return 0;
}

static void _wm_parse_colorimetry_db(struct wm_hdmi_tx *hdmi, const u8 *db)
{
	int i;

	for (i = 0; i < 8; i++)
		if (BIT(i) & db[2])
			hdmi->edid_ctrl.colorimetry |= BIT(i);
}

static void _wm_edid_parse_ext_blk(struct wm_hdmi_tx *hdmi, struct edid *edid)
{
	const u8 *cea = _wm_find_cea_extension(edid);
	const u8 *db = NULL;

	if (cea && _wm_cea_revision(cea) >= 3) {
		int i, start, end;

		if (_wm_cea_db_offsets(cea, &start, &end))
			return;

		_wm_for_each_cea_db(cea, i , start, end) {
			db = &cea[i];

			if (_wm_cea_db_tag(db) == TAG_CODE) {
				WM_DEBUG("found ext tag block: 0x%x", db[1]);

				switch (db[1]) {
				case COLORIMETRY_EXTENDED_DATA_BLOCK:
					_wm_parse_colorimetry_db(hdmi, db);
					break;
				default:
					break;
				}
			}
		}
	}
}

static inline char *_wm_hdmi_tx_intr_string(u8 intr_event)
{
	switch (intr_event) {
		case 0:
			return "I2CM_PHY";
		case 1:
			return "HPD";
		case 2:
			return "PHY_TX_LOCK";
		case 3:
			return "EDID";
		case 4:
			return "HPD & PHY_TX_LOCK";
		default:
			return "FAUX";
	}
}

static inline char *_wm_phy_state(enum wm_hdmi_phy_state state)
{
	switch (state) {
		case PHY_I2C_NONE:
			return "none";
		case PHY_I2C_PLL:
			return "phy_pll";
		case PHY_I2C_TXTM:
			return "tx_term";
		case PHY_I2C_VLEC:
			return "tx_voltage";
		case PHY_I2C_CKSYM:
			return "ck_sym";
		case PHY_I2C_PWR:
			return "pwr";
	}
}

static u8 _wm_hdmi_tx_decode_pclk_dc(int pclk)
{
	switch (pclk) {
		case 297000:
		case 371250:
		case 396000:
			return GROUP_4;
		case 148500:
		case 165000:
		case 186250:
		case 198000:
			return GROUP_3;
		case 74250:
		case 82500:
		case 90000:
		case 99000:
		case 108000:
		case 118800:
			return GROUP_2;
		case 54000:
		case 59400:
		case 72000:
			return GROUP_1;
		default:
		case 25175:
		case 27000:
			return GROUP_0;
	}
}

static u8 _wm_hdmi_tx_decode_pclk(int pclk)
{
	switch (pclk) {
		case 371250:
		case 396000:
		case 495000:
		case 594000:
			return GROUP_5;
		case 186250:
		case 297000:
			return GROUP_3;
		case 198000:
			return GROUP_4;
		case 99000:
		case 108000:
		case 118800:
		case 148500:
		case 165000:
			return GROUP_2;
		case 54000:
		case 59400:
		case 72000:
		case 74250:
		case 82500:
		case 90000:
			return GROUP_1;
		default:
		case 25175:
		case 27000:
			return GROUP_0;
	}
}

static inline struct wm_hdmi_tx_mpll _wm_hdmi_tx_mpll_fill(u32 bpp, u32 pclk)
{
	switch (bpp) {
	default:
	case 24:
		return divider_list[_wm_hdmi_tx_decode_pclk(pclk)];
	case 30:
		return divider_list_dc[_wm_hdmi_tx_decode_pclk_dc(pclk)];
	}
}

static inline void _wm_hdmi_tx_phy_rstz(struct wm_hdmi_tx *hdmi)
{
       spi_write8(WM_HDMI_MC_PHYRSTZ, BIT(0));
}

static inline void _wm_hdmi_tx_pause_clk(struct wm_hdmi_tx *hdmi)
{
	/** Disable CLK using secure write. */
	hdmi->display->disable_hdmi_clk(hdmi->display);
}

static void _wm_hdmi_tx_conf_mode_param(struct wm_hdmi_tx *hdmi)
{
	u8 value = BIT(4);
	struct wm_display_mode *wm_mode;

	wm_mode = &hdmi->display->mode_info;

	if (wm_mode->interlace)
		value |= 0x3;

	if (wm_mode->h_active_low)
		value |= BIT(5);

	if (wm_mode->v_active_low)
		value |= BIT(6);

	if (wm_mode->h_active == 640 &&
		wm_mode->v_active == 480)
		value |= BIT(3);

	spi_write8(WM_HDMI_FC_INVIDCONF, value);
}

static void _wm_hdmi_tx_conf_ctrl_period(struct wm_hdmi_tx *hdmi)
{
	spi_write8(WM_HDMI_FC_CTRLDUR, 0x0c);
	spi_write8(WM_HDMI_FC_EXCTRLDUR, 0x20);
	spi_write8(WM_HDMI_FC_EXCTRLSPAC, 0x01);
}
/* TODO: Check the requirement and configure accordingly
 * at the later stage of development.
 * static void _wm_hdmi_tx_conf_mode_param_3d(struct wm_hdmi_tx *hdmi);
*/

static void _wm_hdmi_tx_set_mode(struct wm_hdmi_tx *hdmi)
{
	struct wm_display_mode *mode;

	mode = &hdmi->display->mode_info;

	/* Horizontal Active Pixels. */
	spi_write8(WM_HDMI_FC_INHACTIV0, mode->h_active & 0xff);
	spi_write8(WM_HDMI_FC_INHACTIV1, (mode->h_active >> 8) & 0xff);
	/* Horizontal Blanking,
	 * H_BLANK = H_FRONT_PORCH + H_SYNC_WIDTH + H_BACK_PORCH
	 */
	spi_write8(WM_HDMI_FC_INHBLANK0, mode->hblank & 0xff);
	spi_write8(WM_HDMI_FC_INHBLANK1, (mode->hblank >> 8) & 0xff);
	/* Vertical Active Pixels. */
	spi_write8(WM_HDMI_FC_INVACTIV0, mode->v_active & 0xff);
	spi_write8(WM_HDMI_FC_INVACTIV1, (mode->v_active >> 8) & 0xff);
	/* Vertical Blanking,
	 * V_BLANK = V_FRONT_PORCH + V_SYNC_WIDTH + V_BACK_PORCH
	 */
	spi_write8(WM_HDMI_FC_INVBLANK0, mode->vblank & 0xff);
	spi_write8(WM_HDMI_FC_INVBLANK1, (mode->vblank >> 8) & 0xff);
	/* Horizontal Front Porch. */
	spi_write8(WM_HDMI_FC_HSYNCINDELAY0, mode->h_front_porch & 0xff);
	spi_write8(WM_HDMI_FC_HSYNCINDELAY1, (mode->h_front_porch >> 8) & 0xff);
	/* Horizontal Sync Width. */
	spi_write8(WM_HDMI_FC_HSYNCINWIDTH0, mode->h_sync_width & 0xff);
	spi_write8(WM_HDMI_FC_HSYNCINWIDTH1, (mode->h_sync_width >> 8) & 0xff);
	/* Vertical Front Porch. */
	spi_write8(WM_HDMI_FC_VSYNCINDELAY0, mode->v_front_porch & 0xff);
	/* Vertical Sync Width. */
	spi_write8(WM_HDMI_FC_VSYNCINWIDTH, mode->v_sync_width & 0xff);
}

static void _wm_hdmi_tx_conf_phy(struct wm_hdmi_tx *hdmi, u8 bits)
{
	u8 data;

	data = spi_read8(WM_HDMI_PHY_CONF0);
	data |= bits;
	spi_write8(WM_HDMI_PHY_CONF0, data);
}

static inline u8 _wm_hdmi_tx_decode_intr(struct wm_hdmi_tx *hdmi)
{
	u8 decode = 0;

	decode = spi_read8(WM_HDMI_IH_DECODE);

	return decode;
}

static inline void _wm_hdmi_tx_initiate_phy_i2c(struct wm_hdmi_tx *hdmi,
				bool write)
{
	if (write)
		spi_write8(WM_HDMI_PHY_I2CM_OPERATION, BIT(4));
	else
		spi_write8(WM_HDMI_PHY_I2CM_OPERATION, BIT(0));
}

static void _wm_hdmi_tx_compose_i2c_frame(struct wm_hdmi_tx *hdmi,
				u8 addr, u16 data)
{
	spi_write8(WM_HDMI_PHY_I2CM_ADDR, addr);
	spi_write8(WM_HDMI_PHY_I2CM_DATAO_0, data & 0xff);
	spi_write8(WM_HDMI_PHY_I2CM_DATAO_1, (data >> 8) & 0xff);
}

static void _wm_hdmi_tx_phy_i2c_intr(struct wm_hdmi_tx *hdmi, u8 *data)
{
		*data = spi_read8(WM_HDMI_IH_I2CMPHY_STAT0);
		spi_write8(WM_HDMI_IH_I2CMPHY_STAT0, *data);
}

static void _wm_hdmi_tx_config_edid_param(struct wm_hdmi_tx *hdmi,
				u8 addr, u8 segptr, bool single_read)
{
	/* Optional Configuration,
	 * E-DDC bus clear.
	 */
	spi_write8(WM_HDMI_I2CM_OPERATION, BIT(5));

	/* Mandatory Register Configurations. */
	spi_write8(WM_HDMI_I2CM_SLAVE, 0xa0);
	spi_write8(WM_HDMI_I2CM_ADDRESS, addr);

	spi_write8(WM_HDMI_I2CM_SEGPTR, 0x00 + (segptr/2));

	if (segptr % 2)
		spi_write8(WM_HDMI_I2CM_SEGADDR, 0x80);
	else
		spi_write8(WM_HDMI_I2CM_SEGADDR, 0x00);

	if (single_read)
		spi_write8(WM_HDMI_I2CM_OPERATION, 0x02);
	else
		spi_write8(WM_HDMI_I2CM_OPERATION, 0x08);

}

static void _wm_hdmi_tx_enable_edid_irq(struct wm_hdmi_tx *hdmi, bool en)
{
	u8 val;

	val = spi_read8(WM_HDMI_IH_MUTE_I2CM_STAT0);
	if (en)
		/* mute DDC Read interrupt */
		spi_write8(WM_HDMI_IH_MUTE_I2CM_STAT0, val | 0x3);
	else
		/* Unmute DDC Read interrupt. */
		spi_write8(WM_HDMI_IH_MUTE_I2CM_STAT0, val & 0x4);
}

static void _wm_hdmi_tx_enable_tx(struct wm_hdmi_tx *hdmi)
{
	u8 vsync_width;

	vsync_width = hdmi->display->mode_info.v_sync_width;

	WM_DEBUG("vsync width: %lu", vsync_width);

	spi_write8(WM_HDMI_MC_SWRSTZREQ_1, 0x00);
	spi_write8(WM_HDMI_FC_VSYNCINWIDTH, vsync_width);
}

static void _wm_hdmi_tx_phy_intr(struct wm_hdmi_tx *hdmi, u8 *val, bool operate)
{
	if (operate)
		spi_write8(WM_HDMI_IH_PHY_STAT0, *val);
	else
		*val = spi_read8(WM_HDMI_IH_PHY_STAT0);
}

static bool _wm_hdmi_tx_pll_lock_intr(struct wm_hdmi_tx *hdmi)
{
	u8 mask, value = 0;
	bool ret;

	mask = 0x01;

	value = hdmi->intr.phy_intr;

	ret = value & mask ? true : false;

	return ret;
}

static inline void _wm_hdmi_tx_hpd_status(struct wm_hdmi_tx *hdmi, bool *hpd)
{
	u8 mask, data = 0;

	/* HPD and RX_SENSE_* status. */
	mask = 0xf2;

	data = hdmi->intr.phy_intr;

	if (!((data & mask) ^ mask))
		*hpd = true;
}

static inline void _wm_hdmi_tx_edid_i2c_intr(struct wm_hdmi_tx *hdmi, u8 *stat)
{
	/* read EDID interrupt status. */
	*stat = spi_read8(WM_HDMI_IH_I2CM_STAT0);
	/* clear E-EDID interrupt. */
	spi_write8(WM_HDMI_IH_I2CM_STAT0, *stat);
}

static void _wm_hdmi_tx_mute_phy(struct wm_hdmi_tx *hdmi, bool mute, u8 bits)
{
	u8 data = 0;

	data = spi_read8(WM_HDMI_IH_MUTE_PHY_STAT0);

	if (mute)
		data |= bits;
	else
		data &= ~bits;

	spi_write8(WM_HDMI_IH_MUTE_PHY_STAT0, data);
}

static void _wm_hdmi_tx_mask_phy(struct wm_hdmi_tx *hdmi, bool mask, u8 bits)
{
	u8 data = 0;

	data = spi_read8(WM_HDMI_PHY_MASK0);

	if (mask)
		data |= bits;
	else
		data &= ~bits;

	spi_write8(WM_HDMI_PHY_MASK0, data);
}

static void _wm_hdmi_tx_toggle_pol(struct wm_hdmi_tx *hdmi, bool pol, u8 bits)
{
	u8 data = 0;

	data = spi_read8(WM_HDMI_PHY_POL0);

	if (pol)
		data &= ~bits;
	else
		data |= bits;

	spi_write8(WM_HDMI_PHY_POL0, data);
}

static inline void _wm_hdmi_tx_config_hpd(struct wm_hdmi_tx *hdmi)
{
	_wm_hdmi_tx_mask_phy(hdmi, false, HPD);

	_wm_hdmi_tx_toggle_pol(hdmi, false, HPD);

	_wm_hdmi_tx_mute_phy(hdmi, false, IH_HPD);

	_wm_hdmi_tx_conf_phy(hdmi, ENHPDRXSENSE);

	WM_INFO("[OK]");
}

static void _wm_hdmi_tx_edid_fetch_bytes(struct wm_hdmi_tx *hdmi, u8 *edid,
		u8 addr, bool single_read)
{
	int i;

	if (!single_read) {
		for (i = 0; i < 8; i++)
			edid[addr + i] = spi_read8(WM_HDMI_I2CM_READ_BUFFx + i);
	} else
		edid[addr] = spi_read8(WM_HDMI_I2CM_DATAI);
}

static void _wm_hdmi_tx_send_hpd_event(struct wm_hdmi_tx *hdmi, bool state)
{
	char name[32], status[32];
	char *envp[5];
	char *event_string = "HOTPLUG=1";
	struct drm_device *dev = NULL;
	enum drm_connector_status c_status;

	c_status = state ? connector_status_connected :
		connector_status_disconnected;

	dev = hdmi->display->connector->dev;

	scnprintf(name, 32, "name=%s", hdmi->display->connector->name);
	scnprintf(status, 32, "status=%s",
		drm_get_connector_status_name(c_status));

	WM_DEBUG("[%s]:[%s]", name, status);

	envp[0] = name;
	envp[1] = status;
	envp[2] = event_string;
	envp[3] = NULL;
	envp[4] = NULL;
	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);

	WM_DEBUG("notify hpd %s UEvent", state ? "connect" : "disconnect");
}

static inline void _wm_hdmi_tx_reset_edid(struct wm_hdmi_tx *hdmi)
{
	memset(&hdmi->edid_ctrl, 0, sizeof(hdmi->edid_ctrl));
}

static int _wm_hdmi_tx_process_hpd_low(struct wm_hdmi_tx *hdmi)
{
	/* Clear and reset all the memory/values set during
	 * HPD HIGH. This will prevent conflict at the time of
	 * next HPD HIGH event.
	 */

	if (hdmi->edid_ctrl.edid) {
		kfree(hdmi->edid_ctrl.edid);
		_wm_hdmi_tx_reset_edid(hdmi);
	}

	_wm_hdmi_tx_send_hpd_event(hdmi, false);

	/* Disable HDMI Tx functionalities. */
	return 0;
}

static inline void _wm_hdmi_tx_process_hpd_high(struct wm_hdmi_tx *hdmi)
{

	_wm_hdmi_tx_send_hpd_event(hdmi, true);
	/* Parse extracted E-EDID data to extract discrete infomration,
	 * such as Audio, Video, HDR, etc.
	 */
	_wm_edid_parse_ext_blk(hdmi, (struct edid*)hdmi->edid_ctrl.edid);
}

static int _wm_hdmi_tx_edid_buff_alloc(struct wm_hdmi_tx *hdmi)
{
	u8 *new;
	int num_ext = 0;

	if (hdmi->edid_ctrl.edid)
		num_ext = hdmi->edid_ctrl.edid[0x7e];

	new = krealloc(hdmi->edid_ctrl.edid,
			(num_ext + 1) * EDID_LENGTH,
			GFP_KERNEL);

	if (new)
		hdmi->edid_ctrl.edid = new;

	return num_ext;
}

static int _wm_hdmi_tx_read_edid(struct wm_hdmi_tx *hdmi, int *done)
{
	u8 stat, *edid;
	u8 *retry = NULL;
	int ret = 0;
	u32 *t_ext = NULL, *c_ext = NULL, *count = NULL;

	t_ext = &hdmi->edid_ctrl.total_ext;
	c_ext = &hdmi->edid_ctrl.curr_ext;
	count = &hdmi->edid_ctrl.count;
	retry = &hdmi->edid_ctrl.retry;

	edid = hdmi->edid_ctrl.edid;

	stat = hdmi->intr.edid_intr;

	/* Check E-DDC interrupt status.
	 * 1. if success, fetch the next bytes,
	 * 2. otherwise, reduce the count and retry the same
	 *    fetch.
	 */
	if (stat & BIT(1)) {
		_wm_hdmi_tx_edid_fetch_bytes(hdmi, edid, *count,
				hdmi->dt_props->edid_single_read);
		*retry = 5;

		if (hdmi->dt_props->edid_single_read)
			(*count)++;
		else
			*count += 8;
	} else {
		if (*retry > 0) {
			(*retry)--;
			WM_DEBUG("EDID read attempt %d failed", 5 - *retry);
		} else {
			WM_ERR("EDID read limit exceeded");
			ret = -ETIMEDOUT;
			goto error;
		}
	}

	/* Check if Base E-EDID Block fetch is complete,
	 * if complete, parse the number of extension blocks
	 * in this E-EDID.
	 *
	 * This condition will only be true on successful fetch
	 * of initial 128 bytes of data.
	 */
	if (*count == EDID_LENGTH) {
		if (drm_edid_block_valid(edid, *c_ext, false, NULL))
			*t_ext = _wm_hdmi_tx_edid_buff_alloc(hdmi);
		else {
			WM_ERR("invalid EDID");
			ret = -EINVAL;
			goto error;
		}
	}

	if (!(*count % EDID_LENGTH) && *count) {
		if (drm_edid_block_valid(edid, *c_ext, false, NULL))
			(*c_ext)++;
		else {
			WM_ERR("invalid EDID Extension");
			ret = -EINVAL;
			goto error;
		}
	}

	if (*c_ext == (*t_ext + 1)) {
		*done = true;
		WM_DEBUG("EDID read complete");
		print_hex_dump_debug("[HDMI EDID]: ", DUMP_PREFIX_NONE,
				16, 16, edid, *count, false);

		return 0;
	}

	_wm_hdmi_tx_config_edid_param(hdmi, *count % EDID_LENGTH, *c_ext,
				hdmi->dt_props->edid_single_read);

	return 0;

error:
	kfree(edid);
	hdmi->edid_ctrl.edid = NULL;
	WM_ERR("Terminating EDID fetch");

	return ret;
}

static void wm_hdmi_tx_read_sink_edid(struct work_struct *work)
{
	struct wm_hdmi_tx *hdmi;
	int ret = 0, complete = 0;

	hdmi = container_of(work, struct wm_hdmi_tx, edid_work);
	if (!hdmi) {
		WM_ERR("invalid input");
		return;
	}

	mutex_lock(&hdmi->lock);
	if((ret = _wm_hdmi_tx_read_edid(hdmi, &complete))) {
		/* Todo: add further conditions
		 * in case of EDID read failure.
		 */
		WM_ERR("EDID Read failed: %d", ret);
	}

	if (complete) {
		_wm_hdmi_tx_enable_edid_irq(hdmi, true);
		_wm_hdmi_tx_process_hpd_high(hdmi);
	}
	mutex_unlock(&hdmi->lock);
}

static int _wm_hdmi_tx_init_read_sink_edid(struct wm_hdmi_tx *hdmi)
{
	int blk = 0;

	if (!hdmi->edid_ctrl.edid) {
		goto reserve;
	} else {
		/* failsafe to parse fresh E-EDID on each HPD High. */
		kfree(hdmi->edid_ctrl.edid);
		hdmi->edid_ctrl.edid = NULL;
	}

reserve:
	blk = _wm_hdmi_tx_edid_buff_alloc(hdmi);
	if (!hdmi->edid_ctrl.edid) {
		WM_ERR("EDID Memory alloc failed");
		return -ENOMEM;
	}
	memset(hdmi->edid_ctrl.edid, 0,
		(blk + 1) * EDID_LENGTH * sizeof(*hdmi->edid_ctrl.edid));

	/* Enable interrupt mask and
	 * clear any previous edid read recordings.
	 */
	_wm_hdmi_tx_enable_edid_irq(hdmi, false);
	_wm_hdmi_tx_reset_edid(hdmi);

	/* Initiate fresh E-EDID parsing, therefore,
	 * address and segptr shall be 0.
	 */
	_wm_hdmi_tx_config_edid_param(hdmi, 0, 0,
			hdmi->dt_props->edid_single_read);

	return 0;
}

/* Reads HPD interrupt register to identify HPD state
 * and take respective action for high and low.
 */
static void wm_hdmi_tx_detect_hpd(struct work_struct *work)
{
	struct wm_hdmi_tx *hdmi;
	bool hpd = false;
	int ret = 0;

	hdmi = container_of(work, struct wm_hdmi_tx, hpd_work);
	if (!hdmi) {
		WM_ERR("invalid input");
		return;
	}

	/* On successful reception of HPD Interrupt,
	 * further execution should be followed in the below order
	 * 1. Sense HPD State based on set polarity.
	 * 2. Reverse current HPD Detection Polarity
	 * 3. Clear the interrupt
	 * 4. Process HPD Event
	 * 	a. Read E-EDID in case of HPD HIGH
	 * 	   i. Send UEvent on successful E-EDID read.
	 * 	b. Clean HDMI Source Configurations
	 * 4. Notify other sub-modules about the HPD (Sink's) status.
	 */

	mutex_lock(&hdmi->lock);

	/* HPD status. */
	_wm_hdmi_tx_hpd_status(hdmi, &hpd);

	/* Reverse HPD detection polarity. */
	_wm_hdmi_tx_toggle_pol(hdmi, hpd, HPD);

	/* Process HPD Event. */
	if (hpd)
		ret = _wm_hdmi_tx_init_read_sink_edid(hdmi);
	else
		ret = _wm_hdmi_tx_process_hpd_low(hdmi);

	if (ret) {
		WM_ERR("hpd %s process fail: %d", hpd ? "high" : "low",	ret);
		goto fail;
	}

	hdmi->display->hpd_status = hpd;

	mutex_unlock(&hdmi->lock);
	return;
fail:
	/* Current HPD processing failed,
	 * revert HPD detection polarity
	 * to previous state.
	 *
	 * This will permit processing of further similar
	 * HPD Event which got failed previously.
	 */
	_wm_hdmi_tx_toggle_pol(hdmi, !hpd, HPD);

	mutex_unlock(&hdmi->lock);
	return;
}

static void _wm_hdmi_tx_config_phy_pll(struct wm_hdmi_tx *hdmi, bool complete)
{
	u8 addr;
	u16 data;
	struct wm_hdmi_tx_mpll mpll;
	enum wm_hdmi_phy_pll pll_state;
	enum wm_hdmi_phy_pll *next_state, *curr_state;

	next_state = &hdmi->phy_next_state;
	curr_state = &hdmi->phy_curr_state;

	pll_state = complete ? *next_state : *curr_state;
	mpll = _wm_hdmi_tx_mpll_fill(hdmi->display->mode_info.bpp,
				hdmi->display->mode_info.pclk_khz);

	switch(pll_state) {
		case PLLCFG: {
			addr = WM_HDMI_PHY_OPMODE_PLLCFG;
			data = mpll.opmode_pllcfg;
			*next_state = PLLCURR;
			break;
		}
		case PLLCURR: {
			addr = WM_HDMI_PHY_PLLCURRCTRL;
			data = mpll.pllcurrctrl;
			*next_state = PLLGMP;
			break;
		}
		case PLLGMP: {
			addr = WM_HDMI_PHY_PLLGMPCTRL;
			data = mpll.pllgmpctrl;
			hdmi->phy_i2c_next = PHY_I2C_TXTM;
			*next_state = PLLCFG;
			break;
		}
	}
	*curr_state = pll_state;

	_wm_hdmi_tx_compose_i2c_frame(hdmi, addr, data);
}

static void _wm_hdmi_tx_config_phy_txterm(struct wm_hdmi_tx *hdmi)
{
	u16 data;
	u32 pclk;

	pclk = hdmi->display->mode_info.pclk_khz;

	if (pclk < 165000)
		data = 0x7;
	else if(pclk >= 165000 && pclk <= 340000)
		data = 0x4;
	else
		data = 0x0;

	_wm_hdmi_tx_compose_i2c_frame(hdmi, WM_HDMI_PHY_TXTERM, data);
}

static void _wm_hdmi_tx_config_phy_voltage(struct wm_hdmi_tx *hdmi)
{
	u32 pclk;
	u16 data = 0;

	pclk = hdmi->display->mode_info.pclk_khz;

	if (pclk <= 165000) {
		/* Bits 4:0. */
		data = 0x14;
		/* Bits 9:5. */
		data |= (0x14 << 5);
	} else if (pclk > 165000 && pclk <= 340000) {
		/* Bits 4:0. */
		data = 0x0d;
		/* Bits 9:5. */
		data |= (0x0d << 5);
	} else {
		/* Values need to be confirmed for
		 * PCLK > 340MHz, i.e., for HDMI 2.0.
		 */
	}

	_wm_hdmi_tx_compose_i2c_frame(hdmi, WM_HDMI_PHY_VLEVCTRL, data);
}

static inline void _wm_hdmi_tx_config_phy_txctrl(struct wm_hdmi_tx *hdmi)
{
	/* TODO: Confirm if the register is only being written over here,
	 * or should be read first before writing.
	 * In case of only write, move "compose frame" function to caller's
	 * case statement.
	 */
	_wm_hdmi_tx_compose_i2c_frame(hdmi, WM_HDMI_PHY_CKSYMTXCTRL, 0x0);
}

static inline void _wm_hdmi_tx_lock_pll_and_enable_hdmi(struct wm_hdmi_tx *hdmi)
{
	_wm_hdmi_tx_mask_phy(hdmi, true, TXPHYLOCK);
	_wm_hdmi_tx_enable_tx(hdmi);
	/* TODO: Enable infoframes as part of
	 * Infoframe gerrit.
	 * _wm_hdmi_tx_enable_infoframe(hdmi);
	 */
	WM_INFO("HDMI TX enabled");
}

static void wm_hdmi_tx_phy_work(struct work_struct *work)
{
	struct wm_hdmi_tx *hdmi;
	u8 done = 0;
	bool i2c_comp = false;
	enum wm_hdmi_phy_state pll_state;

	hdmi = container_of(work, struct wm_hdmi_tx, phy_work);
	if (!hdmi) {
		WM_ERR("invalid input");
		return;
	}

	mutex_lock(&hdmi->lock);

	/* Check if interrupt is raised for PLL Lock,
	 *
	 * if PLL Locked, enable the display and exit.
	 */

	if (_wm_hdmi_tx_pll_lock_intr(hdmi)) {
		hdmi->pll_lock = true;
		wake_up(&hdmi->phy_enable_wq);
		WM_DEBUG("pll locked");
		_wm_hdmi_tx_lock_pll_and_enable_hdmi(hdmi);
		mutex_unlock(&hdmi->lock);
		return;
	}

	/* If interrupt is not raised for PLL Lock, then
	 * find the I2C status and program the HDMI PHY
	 * according to the below logic -
	 *
	 * 1. PHY PLL
	 * 2. Terminal Resistance
	 * 3. Electrical Voltage
	 * 4. Clock Skew
	 * 5. PHY Power
	 */

	done = hdmi->intr.phy_i2c_intr;

	pll_state = hdmi->phy_i2c_state;

	if ((i2c_comp = done & BIT(1))) {
		hdmi->phy_i2c_state = hdmi->phy_i2c_next;
		hdmi->phy_retry = 0;
	} else if ((done & BIT(0)) && hdmi->phy_retry < 2) {
		hdmi->phy_retry++;
		WM_DEBUG("hdmi phy %s failed retry: %d",
				_wm_phy_state(pll_state), hdmi->phy_retry);
	} else {
		WM_ERR("max retry exceeded for phy config %s",
				_wm_phy_state(pll_state));
		goto end;
	}

	WM_DEBUG("hdmi phy %s state %s", _wm_phy_state(pll_state),
			i2c_comp ? "success" : "failed");

	switch (hdmi->phy_i2c_state) {
		case PHY_I2C_PLL:
			_wm_hdmi_tx_config_phy_pll(hdmi, i2c_comp);
			break;
		case PHY_I2C_TXTM:
			_wm_hdmi_tx_config_phy_txterm(hdmi);
			hdmi->phy_i2c_next = PHY_I2C_VLEC;
			break;
		case PHY_I2C_VLEC:
			_wm_hdmi_tx_config_phy_voltage(hdmi);
			hdmi->phy_i2c_next = PHY_I2C_CKSYM;
			break;
		case PHY_I2C_CKSYM:
			_wm_hdmi_tx_config_phy_txctrl(hdmi);
			hdmi->phy_i2c_next = PHY_I2C_PWR;
			break;
		case PHY_I2C_PWR:
			WM_DEBUG("enabling phy, no further programming");
			_wm_hdmi_tx_conf_phy(hdmi, TXPWRON);
			hdmi->phy_i2c_next = PHY_I2C_NONE;
			goto end;
		case PHY_I2C_NONE:
			WM_ERR("no phy i2c triggered");
			goto end;
	}

	_wm_hdmi_tx_initiate_phy_i2c(hdmi, true);
	mutex_unlock(&hdmi->lock);
	return;
end:
	mutex_unlock(&hdmi->lock);
}

static enum drm_mode_status wm_hdmi_tx_mode_valid(struct wm_hdmi_tx *hdmi,
				const struct drm_display_mode *drm_mode)
{
	/* Only accept modes which satisfy all three below conditions.
	 *
	 * Conditions -
	 * 	1. Not defined in Y420_Video_Data_Block,
	 * 	2. Progressive Mode,
	 * 	3. Pixel Clock less than Max. TMDS Clock
	 *
	 * and mark rest as invalid.
	 */

	u8 vic;
	struct drm_hdmi_info *hdmi_info;
	struct drm_display_info *info;
	enum drm_mode_status mode_status = MODE_BAD;

	vic = drm_match_cea_mode(drm_mode);
	info = &hdmi->display->connector->display_info;
	hdmi_info = &info->hdmi;

	if ((drm_mode->clock <= info->max_tmds_clock)
		&& !(drm_mode->flags & DRM_MODE_FLAG_INTERLACE)
		&& !hdmi_info->y420_vdb_modes[vic])
		mode_status = MODE_OK;

	WM_DEBUG("[%s] mode is %s", drm_mode->name,
			(mode_status == MODE_OK) ? "valid" : "invalid");

	return mode_status;
}

static int wm_hdmi_tx_get_modes(struct wm_hdmi_tx *hdmi,
			struct drm_connector *connector)
{
	int count = 0;
	struct edid *edid;
	if (!hdmi || !connector) {
		WM_ERR("invalid input");
		return 0;
	}

	if (!hdmi->edid_ctrl.edid) {
		WM_ERR("invalid edid");
		return 0;
	}

	edid = (struct edid *)(hdmi->edid_ctrl.edid);

	drm_connector_update_edid_property(connector, edid);
	count = drm_add_edid_modes(connector, edid);
	_wm_edid_parse_ext_blk(hdmi, edid);

	WM_DEBUG("available hdmi modes: %d", count);

	return count;
}

static int wm_hdmi_tx_pre_enable(struct wm_hdmi_tx *hdmi)
{
	/* Configure the source with required mode
	 * details.
	 *
	 * 1. Configure the Resolution,
	 * 2. Configure the Mandatory Infoframes,
	 * 3. (Optional) Configure the Color Space Conversion Matrix.
	 */

	_wm_hdmi_tx_phy_rstz(hdmi);
	_wm_hdmi_tx_pause_clk(hdmi);
	_wm_hdmi_tx_conf_mode_param(hdmi);
	_wm_hdmi_tx_set_mode(hdmi);
	_wm_hdmi_tx_conf_ctrl_period(hdmi);

	/* Configure HDMI AVI Infoframe. */

	WM_DEBUG("[OK]");
	return 0;
}

static int wm_hdmi_tx_enable(struct wm_hdmi_tx *hdmi)
{
	int ret = 0;

	/* Manadatory/Required Initialization for the
	 * enable functionality to complete.
	 */
	init_waitqueue_head(&hdmi->phy_enable_wq);
	hdmi->phy_next_state = hdmi->phy_curr_state = PLLCFG;
	hdmi->phy_i2c_state = hdmi->phy_i2c_next = PHY_I2C_PLL;
	hdmi->pll_lock = false;

	_wm_hdmi_tx_config_phy_pll(hdmi, false);

	/* Interrupts activation for the HDMI PHY
	 * FSM configuration.
	 */
	_wm_hdmi_tx_toggle_pol(hdmi, false, TXPHYLOCK);
	_wm_hdmi_tx_mask_phy(hdmi, false, TXPHYLOCK);
	_wm_hdmi_tx_mute_phy(hdmi, false, TXPHYLOCK);
	_wm_hdmi_tx_initiate_phy_i2c(hdmi, true);

	if (wait_event_timeout(hdmi->phy_enable_wq, hdmi->pll_lock,
				msecs_to_jiffies(PHY_PLL_TIMEOUT_MS)))
		WM_INFO("PLL Lock OK");
	else {
		WM_ERR("PLL Lock NOK");
		ret = -EAGAIN;
	}

	return ret;
}

static int wm_hdmi_tx_disable(struct wm_hdmi_tx *hdmi)
{
	return 0;
}

static bool _wm_hdmi_tx_phy_irq_handler(struct wm_hdmi_tx *hdmi, u8 *intr_event)
{
	_wm_hdmi_tx_phy_intr(hdmi, &hdmi->intr.phy_intr, READ);

	/* Note: There exist a corner case, where both HPD
	 * and PLL interrupt gets activated at the same time.
	 *
	 * Although the probability of this is very low, but
	 * our logic considers this situation in the case 3.
	 *
	 * If both interrupts are detected at the same time,
	 * then prioritize HPD event and ignore Tx PHY Lock.
	 */

	hdmi->intr.phy_intr &= (IH_HPD | IH_TXPHYLOCK);
	switch (hdmi->intr.phy_intr) {
		/* HPD interrupt. */
		case 1:
			*intr_event = 1;
			queue_work(hdmi->workq, &hdmi->hpd_work);
			break;
		/* PLL Lock/Unlock interrupt. */
		case 2:
			*intr_event = 2;
			queue_work(hdmi->workq, &hdmi->phy_work);
			break;
		/* HPD and PLL interrupt. */
		case 3:
			*intr_event = 4;
			queue_work(hdmi->workq, &hdmi->hpd_work);
			cancel_work_sync(&hdmi->phy_work);
			break;
		/* Faux case. */
		default:
			*intr_event = 0xff;
			return false;
	}

	_wm_hdmi_tx_phy_intr(hdmi, &hdmi->intr.phy_intr, WRITE);
	return true;
}

static int wm_hdmi_tx_irq_handler(struct wm_hdmi_tx *hdmi, int irq)
{
	u8 intrp = 0, intr_event = 0xff;

	if (!hdmi) {
		WM_ERR("invalid data");
		return IRQ_NONE;
	}

	intrp = _wm_hdmi_tx_decode_intr(hdmi);

	if (intrp & BIT(3)) {
		/* BIT(3) of interrupt register is the parent bit
		 * for two separate interrupts, namely,
		 * 1. HDMI PHY Tx & HPD
		 * 2. HDMI PHY I2C
		 * Therefore, we should probe into both interrrupt
		 * sequences if interrupt on this bit is raised.
		 *
		 * If there is no interrupt in HDMI PHY Tx & HPD,
		 * then check for active interrupt in HDMI Phy's i2c.
		 */
		if (!_wm_hdmi_tx_phy_irq_handler(hdmi, &intr_event)) {
			_wm_hdmi_tx_phy_i2c_intr(hdmi, &hdmi->intr.phy_i2c_intr);
			intr_event = 0;
		}

	} else if (intrp & BIT(2)) {
		intr_event = 3;
		_wm_hdmi_tx_edid_i2c_intr(hdmi, &hdmi->intr.edid_intr);
		queue_work(hdmi->workq, &hdmi->edid_work);
	} else {
		WM_DEBUG("no active interrupt");
		return IRQ_NONE;
	}
	WM_DEBUG("hdmi %s interrupt received", _wm_hdmi_tx_intr_string(intr_event));

	return IRQ_HANDLED;
}

static int _wm_hdmi_tx_create_workqueue(struct wm_hdmi_tx *hdmi)
{
	hdmi->workq = create_singlethread_workqueue("wm_hdmi_workqueue");
	if (IS_ERR_OR_NULL(hdmi->workq)) {
		WM_ERR("error creating workqueue");
		return -EPERM;
	}

	INIT_WORK(&hdmi->hpd_work, wm_hdmi_tx_detect_hpd);
	INIT_WORK(&hdmi->phy_work, wm_hdmi_tx_phy_work);
	INIT_WORK(&hdmi->edid_work, wm_hdmi_tx_read_sink_edid);
	return 0;
}

struct wm_hdmi_tx *wm_hdmi_tx_init(struct wm_display_info *info)
{
	struct wm_hdmi_tx *hdmi_tx;
	int ret = 0;

	if (!info || !info->dev || !info->dt_props || !info->display) {
		WM_ERR("invalid arguments");
		ret = -EINVAL;
		goto error;
	}
	hdmi_tx = devm_kzalloc(info->dev, sizeof(*hdmi_tx), GFP_KERNEL);
	if (!hdmi_tx) {
		ret = -ENOMEM;
		goto error;
	}

	hdmi_tx->display = info->display;
	hdmi_tx->dt_props = info->dt_props;
	hdmi_tx->dev = info->dev;

	/* assign function pointers. */
	hdmi_tx->pre_enable = wm_hdmi_tx_pre_enable;
	hdmi_tx->enable = wm_hdmi_tx_enable;
	hdmi_tx->disable = wm_hdmi_tx_disable;
	hdmi_tx->post_disable = NULL;
	hdmi_tx->get_modes = wm_hdmi_tx_get_modes;
	hdmi_tx->mode_valid = wm_hdmi_tx_mode_valid;
	hdmi_tx->irq_handler = wm_hdmi_tx_irq_handler;

	mutex_init(&hdmi_tx->lock);

	/* initialize workqueues. */
	ret = _wm_hdmi_tx_create_workqueue(hdmi_tx);
	if (ret)
		goto fail;

	_wm_hdmi_tx_config_hpd(hdmi_tx);

	return hdmi_tx;
fail:
	mutex_destroy(&hdmi_tx->lock);
error:
	WM_ERR("HDMI TX initialization failed with error: %d", ret);
	return ERR_PTR(ret);
}

void wm_hdmi_tx_deinit(struct wm_hdmi_tx *hdmi_tx)
{
	if (!hdmi_tx) {
		WM_DEBUG("hdmi allocation failed - deinit");
		return;
	}

	if (hdmi_tx->workq) {
		cancel_work_sync(&hdmi_tx->hpd_work);
		cancel_work_sync(&hdmi_tx->edid_work);
		destroy_workqueue(hdmi_tx->workq);
	}

	mutex_destroy(&hdmi_tx->lock);


	WM_DEBUG("hdmi deinit");
}
