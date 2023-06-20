// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include "wm_audio_registers.h"
#include "wm_audio.h"

#define BYTE(val, offset)  (((val) & (0xFF << (8 * offset))) >> (8 * offset))

#define MAX_REC_CHANNELS 4
#define WM_I2S_REC_DATA_OVERRUN (1 << 1)
#define WM_I2S_REC_DATA_AVAIL (1 << 0)
#define CLK_NAME_LENGTH 30

enum sample_rate_idx {
	AUDIO_SAMPLE_RATE_32KHZ,
	AUDIO_SAMPLE_RATE_44P1KHZ,
	AUDIO_SAMPLE_RATE_48KHZ,
	AUDIO_SAMPLE_RATE_64KHZ,
	AUDIO_SAMPLE_RATE_88P2KHZ,
	AUDIO_SAMPLE_RATE_96KHZ,
	AUDIO_SAMPLE_RATE_128KHZ,
	AUDIO_SAMPLE_RATE_176P2KHZ,
	AUDIO_SAMPLE_RATE_192KHZ,
	AUDIO_SAMPLE_RATE_256KHZ,
	AUDIO_SAMPLE_RATE_352P8KHZ,
	AUDIO_SAMPLE_RATE_384KHZ,
	SAMPLE_RATE_COUNT,
};

uint32_t sample_rate[SAMPLE_RATE_COUNT] = {32000, 44100, 48000,
		64000, 88200, 96000, 128000, 176200,
		192000, 256000, 352800, 384000};

enum tmds_clk_idx {
	TMDS_CLK_25P2MHZ,
	TMDS_CLK_27MHZ,
	TMDS_CLK_54MHZ,
	TMDS_CLK_74P25MHZ,
	TMDS_CLK_148P5MHZ,
	TMDS_CLK_297MHZ,
	TMDS_CLK_597MHZ,
	CLK_COUNT,
};

uint32_t tmds_clk[CLK_COUNT] = {25200000, 27000000, 54000000,
		74250000, 148500000, 297000000, 597000000};

struct hdmi_audn_cts {
	uint32_t n;
	uint32_t cts;
};

static char clk_src_name[CLK_NAME_LENGTH] = "wm_audio_pll";

static const struct hdmi_audn_cts audn_audcts[SAMPLE_RATE_COUNT][CLK_COUNT] = {
	{ /* 32KHZ */
		{ 0x1000, 0x6270 }, /* 25P2MHz */
		{ 0x1000, 0x6978 }, /* 27MHZ */
		{ 0x1000, 0xD2F0 }, /* 54MHZ */
		{ 0x1000, 0x1220A }, /*74P25MHZ */
		{ 0x1000, 0x24414 }, /* 148P5MHZ */
		{ 0xC00,  0x3661E }, /* 297MHZ */
		{ 0xC00, 0x6CC3C }, /*597MHZ */
	},
	{ /* 44P1KHZ */
		{ 0x1880, 0x6D60 },
		{ 0x1880, 0x7530 },
		{ 0x1880, 0xEA60 },
		{ 0x1880, 0x14244 },
		{ 0x1880, 0x28488 },
		{ 0x1260, 0x3C6CC },
		{ 0x24C0, 0xF1B30 },
	},
	{ /* 48KHZ */
		{ 0x1800, 0x6270 },
		{ 0x1800, 0x6978 },
		{ 0x1800, 0xD2F0 },
		{ 0x1800, 0x1220A },
		{ 0x1800, 0x24414 },
		{ 0x1400, 0x3C6CC },
		{ 0x1800, 0x78D98 },
	},
	{ /* 64KHZ */
		{ 0x2000, 0x6270 },
		{ 0x2000, 0x6978 },
		{ 0x2000, 0xD2F0 },
		{ 0x2000, 0x1220A },
		{ 0x2000, 0x24414 },
		{ 0x2000, 0x3C6CC },
		{ 0x2000, 0x91050 },
	},
	{ /* 88P2KHZ */
		{ 0x3100, 0x6D60 },
		{ 0x3100, 0x7530 },
		{ 0x3100, 0xEA60 },
		{ 0x3100, 0x14244 },
		{ 0x3100, 0x28488 },
		{ 0x24C0, 0x3C6CC },
		{ 0x4980, 0xF1B30 },
	},
	{ /* 96KHZ */
		{ 0x3000, 0x6270 },
		{ 0x3000, 0x6978 },
		{ 0x3000, 0xD2F0 },
		{ 0x3000, 0x1220A },
		{ 0x3000, 0x24414 },
		{ 0x2800, 0x3C6CC },
		{ 0x3000, 0x78D98 },
	},
	{ /* 128KHZ */
		{ 0x4000, 0x6270 },
		{ 0x4000, 0x6978 },
		{ 0x4000, 0xD2F0 },
		{ 0x4000, 0x1220A },
		{ 0x4000, 0x24414 },
		{ 0x4000, 0x3C6CC },
		{ 0x4000, 0x91050 },
	},
	{ /* 176P2KHZ */
		{ 0x6200, 0x6D60 },
		{ 0x6200, 0x7530 },
		{ 0x6200, 0xEA60 },
		{ 0x6200, 0x14244 },
		{ 0x6200, 0x28488 },
		{ 0x4980, 0x3C6CC },
		{ 0x9300, 0xF1B30 },
	},
	{ /* 192KHZ */
		{ 0x6000, 0x6270 },
		{ 0x6000, 0x6978 },
		{ 0x6000, 0xD2F0 },
		{ 0x6000, 0x1220A },
		{ 0x6000, 0x24414 },
		{ 0x5000, 0x3C6CC },
		{ 0x6000, 0x4B87F0 },
	},
	{ /* 256HZ */
		{ 0x8000, 0x6270 },
		{ 0x8000, 0x6978 },
		{ 0x8000, 0xD2F0 },
		{ 0x8000, 0x1220A },
		{ 0x8000, 0x24414 },
		{ 0x8000, 0x48828 },
		{ 0x8000, 0x91050 },
	},
	{ /* 352P8KHZ */
		{ 0xC400, 0x6D60 },
		{ 0xC400, 0x7530 },
		{ 0xC400, 0xEA60 },
		{ 0xC400, 0x14244 },
		{ 0xC400, 0x28488 },
		{ 0x9300, 0x3C6CC },
		{ 0x12600, 0xF1B30 },
	},
	{ /* 384KHZ */
		{ 0xC000, 0x6270 },
		{ 0xC000, 0x6978 },
		{ 0xC000, 0xD2F0 },
		{ 0xC000, 0x1220A },
		{ 0xC000, 0x24414 },
		{ 0xA000, 0x3C6CC },
		{ 0xC000, 0x91050 },
	},
};

static int audio_update_register(struct wm_audio *wm_audio, uint32_t reg,
				 uint32_t mask, uint32_t value)
{
	struct wm_display *display;

	dev_dbg(wm_audio->disp_dev, "%s: reg %d mask %d, value %d\n", __func__, reg, mask, value);

	display = dev_get_drvdata(wm_audio->disp_dev);
	display->update_register_bits(display, reg, mask, value);

	return 0;
}

static int audio_read_register(struct wm_audio *wm_audio, uint32_t reg)
{
	struct wm_display *display;

	display = dev_get_drvdata(wm_audio->disp_dev);
	return display->read_register(display, reg);
}

/*TODO: update interrupt register */
static int wm_audio_intr_handler(struct wm_audio *audio, int irq)
{
	int val = 0;
	int i = 0;

	for (i = 0; i < MAX_REC_CHANNELS; i++) {
		val = audio_read_register(audio, WM_I2S_ISR(i));

		if (val & WM_I2S_REC_DATA_OVERRUN)
			pr_err_ratelimited("%s: overflow 0x%x", __func__, val);

		if (val & WM_I2S_REC_DATA_AVAIL)
			pr_err_ratelimited("%s: data avail 0x%x", __func__, val);

		/*TODO: clear/handle interrupt */
	}

	return IRQ_HANDLED;
}

static int wm_audio_enable_i2s(struct wm_audio *audio)
{
	uint32_t enable_value = 0;
	int rec_chnls = audio->rec_chnls;
	int i = 0, bit_width;

	dev_dbg(audio->disp_dev, "%s: bit width %d\n", __func__, audio->bit_width);

	switch (audio->bit_width) {
	case 32:
		bit_width = 5;
		break;
	case 24:
		bit_width = 4;
		break;
	case 16:
		bit_width = 2;
		break;
	case 12:
		bit_width = 1;
		break;
	default:
		dev_err(audio->disp_dev, "%s: invalid bitwidth\n", __func__);
		return -EINVAL;
	}

	/* IEN = 1 */
	audio_update_register(audio, WM_I2S_IER, BIT(0), 0x1);
	/* intf type = i2s */
	audio_update_register(audio, WM_I2S_IER, BIT(1), 0x0);
	/* frame offset = 0 - only in effect if TDM */
	audio_update_register(audio, WM_I2S_IER, BIT(5), 0);

	for (i = 0; i < rec_chnls; i++)
		audio_update_register(audio, WM_I2S_RCR(i),
			0x7, bit_width);

	for (i = 0; i < rec_chnls; i++) {
		/* Configure FIFO level to 1 */
		audio_update_register(audio, WM_I2S_RFCR(i), 0xF, 0x0);
	}

	/* enable i2s */
	audio_update_register(audio, WM_I2S_IRER, BIT(0), 0x1);
	/* enable i2s clock */
	audio_update_register(audio, WM_I2S_CER, BIT(0), 0x1);

	/* enable DMAs */
	enable_value = 0;
	for (i = 0; i < rec_chnls; i++)
		enable_value |= BIT(i);
	audio_update_register(audio, WM_I2S_DMACR, 0xf, enable_value);

	/* enable i2s receiver channels */
	for (i = 0; i < rec_chnls; i++)
		audio_update_register(audio, WM_I2S_RER(i), BIT(0), 0x1);

	return 0;
}

static int wm_audio_enable_audio_ctrl(struct wm_audio *audio)
{
	uint32_t enable_value = 0, iec_pcm = 0, msm_format = 0;
	int i = 0;

	/* msm_format
	 * 0 == 24-bit PCM audio format
	 * 1 == 32-bit QCOM 60958 format
	 *
	 * iec_pcm
	 * 0=20-bit 60958 format (NLPCM)
	 * 1=24-bit 60958 format (PCM)
	 */
	switch (audio->data_format) {
	case LINEAR_PCM_DATA:
		audio->insert_pcuv = 0x1;
		msm_format = 0x0;
		break;
	case LINEAR_PCM_DATA_PACKED_60958:
		audio->insert_pcuv = 0x0;
		msm_format = 0x1;
		iec_pcm = 0x1;
		break;
	case NON_LINEAR_DATA_PACKED_60958:
		audio->insert_pcuv = 0x0;
		msm_format = 0x1;
		iec_pcm = 0x0;
		break;
	case NON_LINEAR_DATA:
	default:
		dev_err(audio->disp_dev, "%s: data_format invalid %d\n",
				__func__, audio->data_format);
		return -EINVAL;
	}

	dev_dbg(audio->disp_dev, "%s: insert pcuv %d, iec pcm %d\n", __func__,
			audio->insert_pcuv, iec_pcm);
	/* enable audio buffer receiver channels */
	for (i = 0; i < audio->rec_chnls; i++)
		enable_value |= BIT(i);

	audio_update_register(audio, WM_ASCL_BUFF_CFG, 0xf, enable_value);
	audio_update_register(audio, WM_ASCL_BUFF_CFG, BIT(5), msm_format);
	if (msm_format)
		audio_update_register(audio, WM_ASCL_BUFF_CFG, BIT(6), iec_pcm);

	return 0;
}

static void wm_audio_hdmi_conf_gpa_audio_mode(struct wm_audio *audio)
{
	uint32_t enable_channel = 0;
	int i = 0;

	audio_update_register(audio, WM_HDMI_FC_PACKET_TX_EN, BIT(0), 0x0);
	/*TODO: update CLKID with I2C write */
	audio_update_register(audio, WM_HDMI_MC_CLKIDS, BIT(3), 0x1);
	audio_update_register(audio, WM_HDMI_AUD_CONF0, BIT(5), 0x0);

	for (i = 0; i < audio->channels; i++)
		enable_channel |= BIT(i);

	audio_update_register(audio, WM_HDMI_GP_CONF1, 0xff, enable_channel);

	if (audio->sample_rate >= 192000)
		audio_update_register(audio, WM_HDMI_GP_CONF2, BIT(0), 0x1);

	audio_update_register(audio, WM_HDMI_GP_CONF2, BIT(1), audio->insert_pcuv);
	audio_update_register(audio, WM_HDMI_FC_MULTISTREAM_CTRL, BIT(0), 0x1);
}

static int wm_audio_hdmi_enable_audio_tx(struct wm_audio *audio)
{
	struct wm_display *display;
	uint32_t aud_n = 0, aud_cts = 0;
	int sample_idx = -EINVAL, tmds_idx = -EINVAL, i = 0;
	uint32_t hdmi_tmds_clk = 0;

	display = dev_get_drvdata(audio->disp_dev);
	hdmi_tmds_clk = display->drm_mode.clock;

	for (i = 0; i < SAMPLE_RATE_COUNT; i++) {
		if (sample_rate[i] == audio->sample_rate) {
			sample_idx = i;
			break;
		}
	}

	if (sample_idx < 0) {
		dev_err(audio->disp_dev, "%s: can't fetch sample rate idx %d i %d\n",
			       __func__, sample_idx, i);
		return -EINVAL;
	}

	for (i = 0; i < CLK_COUNT; i++) {
		if (tmds_clk[i] == hdmi_tmds_clk) {
			tmds_idx = i;
			break;
		}
	}

	if (tmds_idx < 0) {
		dev_err(audio->disp_dev, "%s: can't fetch tmds idx %d i %d\n",
			       __func__, tmds_idx, i);
		return -EINVAL;
	}

	aud_n = audn_audcts[sample_idx][tmds_idx].n;
	aud_cts = audn_audcts[sample_idx][tmds_idx].cts;

	audio_update_register(audio, WM_HDMI_AUD_N3, BIT(7), 0x1);
	audio_update_register(audio, WM_HDMI_AUD_N3, 0xF, 0xF & BYTE(aud_n, 2));

	audio_update_register(audio, WM_HDMI_AUD_CTS3, BIT(4), 0x1);
	audio_update_register(audio, WM_HDMI_AUD_CTS3, 0xF, 0xF & BYTE(aud_cts, 2));
	audio_update_register(audio, WM_HDMI_AUD_CTS2, 0x7, BYTE(aud_cts, 1));
	audio_update_register(audio, WM_HDMI_AUD_CTS1, 0x7, BYTE(aud_cts, 0));

	audio_update_register(audio, WM_HDMI_AUD_N3, BIT(7), 0x1);
	audio_update_register(audio, WM_HDMI_AUD_N3, 0xF, 0xF & BYTE(aud_n, 2));
	audio_update_register(audio, WM_HDMI_AUD_N2, 0xFF, BYTE(aud_n, 1));
	audio_update_register(audio, WM_HDMI_AUD_N1, 0xFF, BYTE(aud_n, 0));

	return 0;
}

static int wm_audio_hdmi_conf_audio_path(struct wm_audio *audio)
{
	int rc = 0;

	wm_audio_hdmi_conf_gpa_audio_mode(audio);
	rc = wm_audio_hdmi_enable_audio_tx(audio);

	return rc;
}

static int  wm_audio_start_hdmi_audio(struct wm_audio *audio)
{
	int rc = 0;

	audio_update_register(audio, WM_HDMI_MC_CLKIDS, BIT(3), 0x0);
	audio_update_register(audio, WM_HDMI_MC_SWRSTZREQ_1, BIT(3), 0x0);
	audio_update_register(audio, WM_HDMI_MC_SWRSTZREQ_1, BIT(7), 0x0);
	audio_update_register(audio, WM_HDMI_AUD_CONF0, BIT(7), 0x1);
	/* 10usec sleep */
	usleep_range(10, 11);

	/* rewrite Audn values */
	rc = wm_audio_hdmi_enable_audio_tx(audio);
	if (rc < 0)
		goto err;

	audio_update_register(audio, WM_HDMI_FC_PACKET_TX_EN, BIT(0), 0x0);
err:
	return rc;
}

static int wm_audio_enable_audio_pll(struct wm_audio *audio, bool enable)
{
	int rc = 0, sclk_rate;

	dev_dbg(audio->disp_dev, "%s: enable %d", __func__, enable);

	if (enable) {
		sclk_rate = audio->sample_rate * audio->channels * audio->bit_width;
		rc = clk_prepare_enable(audio->clk);
		if (rc < 0) {
			dev_err_ratelimited(audio->disp_dev, "%s: clk enable failed\n",
					__func__);
			return rc;
		}

		dev_dbg(audio->disp_dev, "%s: sclk rate %d", __func__, sclk_rate);
		//TODO: check enable and set rate sequence
		rc = clk_set_rate(audio->clk, sclk_rate);
		if (rc) {
			dev_err(audio->disp_dev, "%s: clk_set_rate failed\n", __func__);
			return rc;
		}
	} else {
		clk_disable_unprepare(audio->clk);
	}

	return rc;
}

static int wm_audio_enable(struct wm_audio *audio,
	struct msm_ext_disp_audio_setup_params *params)
{
	int rc = 0;

	if (!params || !audio)
		return -EINVAL;

	dev_dbg(audio->disp_dev, "%s: sample rate %d, channels %d, witdh %d, format %d\n",
			__func__, params->sample_rate_hz,
			params->num_of_channels, params->bit_width,
			params->data_format);
	audio->sample_rate = params->sample_rate_hz;
	audio->channels = params->num_of_channels;
	audio->data_format = params->data_format;
	audio->bit_width = params->bit_width;

	/* One I2S receive channel can carry two audio channels.
	 * Round off the receive channels incase of ODD audio channels.
	 * Ex: For 2.1 channel map two receive channels will be configured.
	 */
	if (audio->channels & 1)
		audio->rec_chnls = audio->channels / 2 + 1;
	else
		audio->rec_chnls = audio->channels / 2;

	rc = wm_audio_enable_audio_pll(audio, true);
	if (rc < 0)
		goto err;

	rc = wm_audio_enable_i2s(audio);
	if (rc < 0)
		goto err;

	rc = wm_audio_enable_audio_ctrl(audio);
	if (rc < 0)
		goto err;

	rc = wm_audio_hdmi_conf_audio_path(audio);
	if (rc < 0)
		goto err;

	rc = wm_audio_start_hdmi_audio(audio);
err:
	return rc;
}

static void wm_audio_disable_i2s(struct wm_audio *audio)
{
	/* ien = 0 */
	audio_update_register(audio, WM_I2S_IER, 0x1, 0x0);
}

static void wm_audio_disable_hdmi_tx(struct wm_audio *audio)
{
	audio_update_register(audio, WM_HDMI_FC_PACKET_TX_EN, 0x0, 0x0);
	audio_update_register(audio, WM_HDMI_MC_CLKIDS, BIT(3), 0x0);
}

static void wm_audio_flush_buffer(struct wm_audio *audio)
{
	audio_update_register(audio, WM_ASCL_BUFF_CFG, BIT(4), 0x1);
}

static int wm_audio_disable(struct wm_audio *audio)
{
	if (!audio)
		return -EINVAL;

	wm_audio_disable_i2s(audio);
	wm_audio_disable_hdmi_tx(audio);
	wm_audio_flush_buffer(audio);
	wm_audio_enable_audio_pll(audio, false);

	return 0;
}

static int wm_setup_audio_infoframes(struct wm_audio *audio,
	struct msm_ext_disp_audio_setup_params *params)
{
	struct wm_display *display;
	struct hdmi_audio_infoframe frame;
	u8 buffer[14];
	ssize_t err = 0;

	memset(&frame, 0, sizeof(frame));
	memset(buffer, 0, sizeof(buffer));

	err = hdmi_audio_infoframe_init(&frame);
	if (err < 0) {
		pr_err("Failed to setup audio infoframe: %zd\n", err);
		return err;
	}

	/* frame.coding_type */
	frame.channels = params->num_of_channels;
	frame.sample_frequency = params->sample_rate_hz;
	/* frame.sample_size */
	/* frame.coding_type_ext */
	frame.channel_allocation = params->channel_allocation;
	frame.downmix_inhibit = params->down_mix;
	frame.level_shift_value = params->level_shift;

	err = hdmi_audio_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (err < 0) {
		pr_err("Failed to pack audio infoframe: %zd\n", err);
		return err;
	}

	/* hdmi module set info frames */
	display = dev_get_drvdata(audio->disp_dev);
	display->handle_params(display, WM_DISPLAY_PARAM_SET_AUD_INFOFRAME, &frame, NULL);

	/* trigger audio enable */
	err = wm_audio_enable(audio, params);

	return err;

}

static struct wm_audio *wm_audio_get_pdata(struct platform_device *pdev)
{
	struct msm_ext_disp_data *ext_data = NULL;
	struct wm_audio *audio = NULL;

	if (!pdev) {
		pr_err("Invalid pdev\n");
		return ERR_PTR(-ENODEV);
	}

	ext_data = platform_get_drvdata(pdev);
	if (!ext_data) {
		pr_err("invalid ext disp data\n");
		return ERR_PTR(-EINVAL);
	}

	audio = ext_data->intf_data;
	if (!audio) {
		pr_err("invalid intf data\n");
		return ERR_PTR(-EINVAL);
	}

	return audio;
}

static int wm_audio_info_setup(struct platform_device *pdev,
		struct msm_ext_disp_audio_setup_params *params)
{
	struct wm_audio *wm_audio = wm_audio_get_pdata(pdev);
	int rc = 0;

	rc = wm_setup_audio_infoframes(wm_audio, params);

	return rc;
}

static int wm_audio_get_edid_blk(struct platform_device *pdev,
		struct msm_ext_disp_audio_edid_blk *blk)
{
	return 0;
}

static int wm_audio_get_cable_status(struct platform_device *pdev, u32 vote)
{
	int rc = 0;
	struct wm_audio *wm_audio = wm_audio_get_pdata(pdev);
	struct wm_display *display;

	if (IS_ERR(wm_audio)) {
		rc = PTR_ERR(wm_audio);
		goto end;
	}

	display = dev_get_drvdata(wm_audio->disp_dev);
	return display->hpd_status;

end:
	return rc;
}

static int wm_audio_get_intf_id(struct platform_device *pdev)
{
	int rc = 0;
	struct wm_audio *audio = wm_audio_get_pdata(pdev);

	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		goto end;
	}

	return EXT_DISPLAY_TYPE_HDMI;
end:
	return rc;
}

static void wm_audio_teardown_done(struct platform_device *pdev)
{
}

static int wm_audio_ack_done(struct platform_device *pdev, u32 ack)
{
	return 0;
}

static int wm_audio_codec_ready(struct platform_device *pdev)
{
	return 0;
}

static int wm_audio_deregister(struct wm_audio *audio)
{
	int rc = 0;
	struct device_node *pd = NULL;
	const char *phandle = "wm,ext-disp";
	struct msm_ext_disp_init_data *ext = NULL;
	struct device *dev = audio->disp_dev;

	ext = &audio->ext_audio_data;

	if (!dev->of_node) {
		pr_err("cannot find audio dev.of_node\n");
		rc = -ENODEV;
		goto end;
	}

	pd = of_parse_phandle(dev->of_node, phandle, 0);
	if (!pd) {
		pr_err("cannot parse %s handle\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	audio->ext_pdev = of_find_device_by_node(pd);
	if (!audio->ext_pdev) {
		pr_err("cannot find %s pdev\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	wm_audio_disable(audio);

#if defined(CONFIG_MSM_EXT_DISPLAY)
	audio->ext_audio_data.intf_ops.audio_config(audio->ext_pdev,
				&audio->ext_audio_data.codec,
				EXT_DISPLAY_CABLE_DISCONNECT);
	audio->ext_audio_data.intf_ops.audio_notify(audio->ext_pdev,
				&audio->ext_audio_data.codec,
				EXT_DISPLAY_CABLE_DISCONNECT);

	rc = msm_ext_disp_deregister_intf(audio->ext_pdev, ext);
	if (rc)
		pr_err("failed to deregister ext disp\n");
#endif

end:
	return rc;
}

static int wm_audio_register(struct wm_audio *audio)
{
	struct msm_ext_disp_init_data *ext = NULL;
	struct msm_ext_disp_audio_codec_ops *ops = NULL;
	struct device_node *np = NULL;
	struct device *dev = NULL;
	const char *phandle = "wm,ext-disp";
	int rc = 0;

	if (!audio) {
		pr_err("%s: invalid argument\n", __func__);
		return -EINVAL;
	}

	dev = audio->disp_dev;
	ext = &audio->ext_audio_data;
	ops = &ext->codec_ops;

	ext->codec.type = EXT_DISPLAY_TYPE_HDMI;
	ext->codec.ctrl_id = 1;
	ext->codec.stream_id = 1;
	ext->pdev = audio->audio_pdev;
	ext->intf_data = audio;

	ops->audio_info_setup   = wm_audio_info_setup;
	ops->get_audio_edid_blk = wm_audio_get_edid_blk;
	ops->cable_status       = wm_audio_get_cable_status;
	ops->get_intf_id        = wm_audio_get_intf_id;
	ops->teardown_done      = wm_audio_teardown_done;
	ops->acknowledge        = wm_audio_ack_done;
	ops->ready              = wm_audio_codec_ready;

	if (!dev->of_node) {
		pr_err("cannot find audio dev.of_node\n");
		rc = -ENODEV;
		goto end;
	}

	np = of_parse_phandle(dev->of_node, phandle, 0);
	if (!np) {
		pr_err("cannot parse %s handle\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	audio->ext_pdev = of_find_device_by_node(np);
	if (!audio->ext_pdev) {
		pr_err("cannot find %s pdev\n", phandle);
		rc = -ENODEV;
		goto end;
	}

#if defined(CONFIG_MSM_EXT_DISPLAY)
	rc = msm_ext_disp_register_intf(audio->ext_pdev, ext);
	if (rc) {
		pr_err("failed to register ext disp\n");
		return -EINVAL;
	}

	audio->ext_audio_data.intf_ops.audio_config(audio->ext_pdev,
				&audio->ext_audio_data.codec,
				EXT_DISPLAY_CABLE_CONNECT);
	audio->ext_audio_data.intf_ops.audio_notify(audio->ext_pdev,
				&audio->ext_audio_data.codec,
				EXT_DISPLAY_CABLE_CONNECT);
#endif

end:
	return rc;
}

struct wm_audio *wm_audio_init(struct wm_display_info *display_info)
{
	struct wm_audio *audio = NULL;
	struct clk *clk;
	int ret = 0;

	if (!display_info)
		return NULL;

	audio = devm_kzalloc(display_info->dev, sizeof(struct wm_audio), GFP_KERNEL);
	audio->disp_dev = display_info->dev;
	audio->enable = wm_audio_register;
	audio->disable = wm_audio_deregister;
	audio->irq_handler = wm_audio_intr_handler;

	audio->audio_pdev =
		platform_device_register_simple("wm_audio", -1, NULL, 0);
	if (IS_ERR(audio->audio_pdev)) {
		dev_dbg(display_info->dev, "%s: Failed to register platform device\n", __func__);
		goto err;
	}

	clk = devm_clk_get(audio->disp_dev, clk_src_name);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(audio->disp_dev, "%s: clk get failed for %s with ret %d\n",
					__func__, clk_src_name, ret);
		goto err;
	}
	audio->clk = clk;
	dev_dbg(audio->disp_dev, "%s: clk get success for clk name %s\n",
		__func__, clk_src_name);

	return audio;

err:
	devm_kfree(display_info->dev, audio);
	return audio;
}

void wm_audio_deinit(struct wm_audio *audio)
{

}
