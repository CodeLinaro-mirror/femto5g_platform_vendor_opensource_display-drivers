/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __WM_AUDIO_REGISTER_H__
#define __WM_AUDIO_REGISTER_H__

#define WM_I2S_BASE_ADDR		0x00484000
#define WM_ASCL_BASE_ADDR		0x00485000
#define WM_HDMI_BASE_ADDR		0x004a0000
#define WM_I2S_IER			(WM_I2S_BASE_ADDR + 0x000)
#define WM_I2S_IRER			(WM_I2S_BASE_ADDR + 0x004)
#define WM_I2S_ITER			(WM_I2S_BASE_ADDR + 0x008)
#define WM_I2S_CER			(WM_I2S_BASE_ADDR + 0x00c)
#define WM_I2S_CCR			(WM_I2S_BASE_ADDR + 0x010)
#define WM_I2S_RXFFR			(WM_I2S_BASE_ADDR + 0x014)
#define WM_I2S_TXFFR			(WM_I2S_BASE_ADDR + 0x018)
#define WM_I2S_SR			(WM_I2S_BASE_ADDR + 0x01c)
#define WM_I2S_LRBRx_BASE		(WM_I2S_BASE_ADDR + 0x020)
#define WM_I2S_LRBR(x)			(WM_I2S_LRBRx_BASE + (0x40 * x))
#define WM_I2S_LTHRx_BASE		(WM_I2S_BASE_ADDR + 0x020)
#define WM_I2S_LTHR(x)			(WM_I2S_LTHRx_BASE + (0x40 * x))
#define WM_I2S_RRBRx_BASE		(WM_I2S_BASE_ADDR + 0x024)
#define WM_I2S_RRBR(x)			(WM_I2S_RRBRx_BASE + (0x40 * x))
#define WM_I2S_RTHRx_BASE		(WM_I2S_BASE_ADDR + 0x024)
#define WM_I2S_RTHR(x)			(WM_I2S_RTHRx_BASE + (0x40 * x))
#define WM_I2S_RERx_BASE		(WM_I2S_BASE_ADDR + 0x028)
#define WM_I2S_RER(x)			(WM_I2S_RERx_BASE + (0x40 * x))
#define WM_I2S_TERx_BASE		(WM_I2S_BASE_ADDR + 0x02c)
#define WM_I2S_TER(x)			(WM_I2S_TERx_BASE + (0x40 * x))
#define WM_I2S_RCRx_BASE		(WM_I2S_BASE_ADDR + 0x030)
#define WM_I2S_RCR(x)			(WM_I2S_RCRx_BASE + (0x40 * x))
#define WM_I2S_TCRx_BASE		(WM_I2S_BASE_ADDR + 0x034)
#define WM_I2S_TCR(x)			(WM_I2S_TCRx_BASE + (0x40 * x))
#define WM_I2S_ISRx_BASE		(WM_I2S_BASE_ADDR + 0x038)
#define WM_I2S_ISR(x)			(WM_I2S_ISRx_BASE + (0x40 * x))
#define WM_I2S_IMRx_BASE		(WM_I2S_BASE_ADDR + 0x03c)
#define WM_I2S_IMR(x)			(WM_I2S_IMRx_BASE + (0x40 * x))
#define WM_I2S_RORx_BASE		(WM_I2S_BASE_ADDR + 0x040)
#define WM_I2S_ROR(x)			(WM_I2S_RORx_BASE + (0x40 * x))
#define WM_I2S_TORx_BASE		(WM_I2S_BASE_ADDR + 0x044)
#define WM_I2S_TOR(x)			(WM_I2S_TORx_BASE + (0x40 * x))
#define WM_I2S_RFCRx_BASE		(WM_I2S_BASE_ADDR + 0x048)
#define WM_I2S_RFCR(x)			(WM_I2S_RFCRx_BASE + (0x40 * x))
#define WM_I2S_TFCRx_BASE		(WM_I2S_BASE_ADDR + 0x04c)
#define WM_I2S_TFCR(x)			(WM_I2S_TFCRx_BASE + (0x40 * x))
#define WM_I2S_RFFx_BASE		(WM_I2S_BASE_ADDR + 0x050)
#define WM_I2S_RFF(x)			(WM_I2S_RFFx_BASE + (0x40 * x))
#define WM_I2S_TFFx_BASE		(WM_I2S_BASE_ADDR + 0x054)
#define WM_I2S_TFF(x)			(WM_I2S_TFFx_BASE + (0x40 * x))
#define WM_I2S_RXDMA			(WM_I2S_BASE_ADDR + 0x1c0)
#define WM_I2S_RRXDMA			(WM_I2S_BASE_ADDR + 0x1c4)
#define WM_I2S_TXDMA			(WM_I2S_BASE_ADDR + 0x1c8)
#define WM_I2S_RTXDMA			(WM_I2S_BASE_ADDR + 0x1cc)
#define WM_I2S_COMP_PARAM_2		(WM_I2S_BASE_ADDR + 0x1f0)
#define WM_I2S_COMP_PARAM_1		(WM_I2S_BASE_ADDR + 0x1f4)
#define WM_I2S_COMP_VERSION		(WM_I2S_BASE_ADDR + 0x1f8)
#define WM_I2S_COMP_TYPE		(WM_I2S_BASE_ADDR + 0x1fc)
#define WM_I2S_DMACR			(WM_I2S_BASE_ADDR + 0x200)
#define WM_I2S_RXDMA_CHx_BASE		(WM_I2S_BASE_ADDR + 0x204)
#define WM_I2S_RXDMA_CH(x)		(WM_I2S_RXDMA_CHx_BASE + (0x4 * x))
#define WM_I2S_TXDMA_CHx_BASE		(WM_I2S_BASE_ADDR + 0x214)
#define WM_I2S_TXDMA_CH(x)		(WM_I2S_TXDMA_CHx_BASE + (0x4 * x))
#define WM_I2S_RSLOTx_BASE		(WM_I2S_BASE_ADDR + 0x224)
#define WM_I2S_RSLOT(x)			(WM_I2S_RSLOTx_BASE + (0x4 * x))
#define WM_I2S_TSLOT			(WM_I2S_BASE_ADDR + 0x224)

/* audio interrupts */
/*TODO: update interrupt mask and max values */
#define WM_AUDIO_INTERRUPT_STATUS_MASK                              0x0002
#define WM_AUDIO_INTERRUPT_MAX                                      0x2

#define WM_ASCL_BUFF_CFG		(WM_ASCL_BASE_ADDR + 0x000)
#define WM_ASCL_BUFF_STATUS		(WM_ASCL_BASE_ADDR + 0x008)

#define WM_HDMI_FC_MULTISTREAM_CTRL	(WM_HDMI_BASE_ADDR + (0x10e2 * 4))
#define WM_HDMI_FC_PACKET_TX_EN		(WM_HDMI_BASE_ADDR + (0x10e3 * 4))

#define WM_HDMI_AUD_CONF0		(WM_HDMI_BASE_ADDR + (0x3100 * 4))
#define WM_HDMI_AUD_CONF1		(WM_HDMI_BASE_ADDR + (0x3101 * 4))

#define WM_HDMI_AUD_N1			(WM_HDMI_BASE_ADDR + (0x3200 * 4))
#define WM_HDMI_AUD_N2			(WM_HDMI_BASE_ADDR + (0x3201 * 4))
#define WM_HDMI_AUD_N3			(WM_HDMI_BASE_ADDR + (0x3202 * 4))
#define WM_HDMI_AUD_CTS1		(WM_HDMI_BASE_ADDR + (0x3203 * 4))
#define WM_HDMI_AUD_CTS2		(WM_HDMI_BASE_ADDR + (0x3204 * 4))
#define WM_HDMI_AUD_CTS3		(WM_HDMI_BASE_ADDR + (0x3205 * 4))
#define WM_HDMI_AUD_INPUTCLKFS		(WM_HDMI_BASE_ADDR + (0x3206 * 4))
#define WM_HDMI_AUD_CTS_DITHER		(WM_HDMI_BASE_ADDR + (0x3207 * 4))

#define WM_HDMI_GP_CONF0		(WM_HDMI_BASE_ADDR + (0x3500 * 4))
#define WM_HDMI_GP_CONF1		(WM_HDMI_BASE_ADDR + (0x3501 * 4))
#define WM_HDMI_GP_CONF2		(WM_HDMI_BASE_ADDR + (0x3502 * 4))
#define WM_HDMI_GP_MASK			(WM_HDMI_BASE_ADDR + (0x3506 * 4))

#define WM_HDMI_MC_CLKIDS		(WM_HDMI_BASE_ADDR + (0x4001 * 4))
#define WM_HDMI_MC_SWRSTZREQ_1		(WM_HDMI_BASE_ADDR + (0x4002 * 4))

#endif /* __WM_AUDIO_REGISTER_H__ */
