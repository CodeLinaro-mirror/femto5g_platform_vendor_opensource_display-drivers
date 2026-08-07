// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <drm/drm_edid.h>
#include <drm/display/drm_hdmi_helper.h>

#include "hdmi_panel.h"
#include "hdmi_debug.h"
#include "sde_edid_parser.h"
#include "sde_connector.h"
#include "hdmi_regs.h"

#define FULL_COLORIMETRY_MASK           0x1FF
#define NORMAL_COLORIMETRY_MASK         0x3
#define EXTENDED_COLORIMETRY_MASK       0x7
#define EXTENDED_ACE_COLORIMETRY_MASK   0xF

#define C(x)	((x) << 0)
#define EC(x)	((x) << 2)
#define ACE(x)	((x) << 5)

#define HDMI_COLORIMETRY_NO_DATA		0x0
#define HDMI_COLORIMETRY_SMPTE_170M_YCC		(C(1) | EC(0) | ACE(0))
#define HDMI_COLORIMETRY_BT709_YCC		(C(2) | EC(0) | ACE(0))
#define HDMI_COLORIMETRY_XVYCC_601		(C(3) | EC(0) | ACE(0))
#define HDMI_COLORIMETRY_XVYCC_709		(C(3) | EC(1) | ACE(0))
#define HDMI_COLORIMETRY_SYCC_601		(C(3) | EC(2) | ACE(0))
#define HDMI_COLORIMETRY_OPYCC_601		(C(3) | EC(3) | ACE(0))
#define HDMI_COLORIMETRY_OPRGB			(C(3) | EC(4) | ACE(0))
#define HDMI_COLORIMETRY_BT2020_CYCC		(C(3) | EC(5) | ACE(0))
#define HDMI_COLORIMETRY_BT2020_RGB		(C(3) | EC(6) | ACE(0))
#define HDMI_COLORIMETRY_BT2020_YCC		(C(3) | EC(6) | ACE(0))
#define	HDMI_COLORIMETRY_DCI_P3_RGB_D65		(C(3) | EC(7) | ACE(0))
#define HDMI_COLORIMETRY_DCI_P3_RGB_THEATER	(C(3) | EC(7) | ACE(1))

static const u32 hdmi_panel_colorimetry_val[] = {
	[DRM_MODE_COLORIMETRY_NO_DATA]		= HDMI_COLORIMETRY_NO_DATA,
	[DRM_MODE_COLORIMETRY_SMPTE_170M_YCC]	= HDMI_COLORIMETRY_SMPTE_170M_YCC,
	[DRM_MODE_COLORIMETRY_BT709_YCC]	= HDMI_COLORIMETRY_BT709_YCC,
	[DRM_MODE_COLORIMETRY_XVYCC_601]	= HDMI_COLORIMETRY_XVYCC_601,
	[DRM_MODE_COLORIMETRY_XVYCC_709]	= HDMI_COLORIMETRY_XVYCC_709,
	[DRM_MODE_COLORIMETRY_SYCC_601]		= HDMI_COLORIMETRY_SYCC_601,
	[DRM_MODE_COLORIMETRY_OPYCC_601]	= HDMI_COLORIMETRY_OPYCC_601,
	[DRM_MODE_COLORIMETRY_OPRGB]		= HDMI_COLORIMETRY_OPRGB,
	[DRM_MODE_COLORIMETRY_BT2020_CYCC]	= HDMI_COLORIMETRY_BT2020_CYCC,
	[DRM_MODE_COLORIMETRY_BT2020_RGB]	= HDMI_COLORIMETRY_BT2020_RGB,
	[DRM_MODE_COLORIMETRY_BT2020_YCC]	= HDMI_COLORIMETRY_BT2020_YCC,
};

#undef C
#undef EC
#undef ACE

#define HDMI_VS_INFOFRAME_BUFFER_SIZE	(HDMI_INFOFRAME_HEADER_SIZE + 6)
#define LEFT_SHIFT_BYTE(x)	((x) << 8)
#define LEFT_SHIFT_WORD(x)	((x) << 16)
#define LEFT_SHIFT_24BITS(x)	((x) << 24)

#define HDMI_GET_MSB(x)	(x >> 8)
#define HDMI_GET_LSB(x)	(x & 0xff)

#define HDMI_DEFAULT_PRODUCT_NAME	"msm"
#define HDMI_DEFAULT_VENDOR_NAME	"unknown"


#define	MAX_REG_HDMI_GENERIC1_INDEX	6

#define hdmi_read(off) ({ \
	readl_relaxed(panel->io_data->io.base + off); \
})

#define hdmi_write(off, value) ({ \
	writel_relaxed(value, panel->io_data->io.base + off); \
})

enum {
	HDMI_AVI_IFRAME_LINE_NUMBER	= 0x1,
	HDMI_VENDOR_IFRAME_LINE_NUMBER	= 0x3,
};

enum hdmi_panel_hdr_state {
	HDR_DISABLED,
	HDR_ENABLED,
};

enum hdmi_panel_dhdr_state {
	DHDR_IDLE,
	DHDR_SENDING,
};

struct hdmi_panel_private {
	bool is_hdmi_mode;
	struct device *dev;
	struct hdmi_panel hdmi_panel;
	struct hdmi_io_data *io_data;

	enum hdmi_panel_hdr_state hdr_state;
	/*
	 * Cached static HDR metadata.
	 * Replayed when setup_hdr() is called with a NULL hdr_meta pointer
	 * (e.g. on post_enable after a modeset while HDR is active).
	 */
	struct drm_msm_ext_hdr_metadata cached_hdr_meta;

	/*
	 * Dynamic HDR (DHDR) state and cached metadata.
	 * On HDMI 4.0.0 HW (pre-4.1.0), the EMP/DHDR_mem_pool path
	 * introduced in HDMI 4.1.0 is not available. DHDR metadata is
	 * therefore carried via the legacy VENSPEC_INFO (VSIF) channel.
	 */
	enum hdmi_panel_dhdr_state dhdr_state;
	struct drm_msm_ext_hdr_metadata cached_dhdr_meta;

	/* DRM Connector associated with this panel. */
	struct drm_connector *connector;
};


/* Add these register definitions to support the latest chipsets. These
 * are going to be replaced by a chipset-based mask approach.
 */
#define HDMI_ACTIVE_HSYNC_START__MASK	0x00001fff
#define HDMI_ACTIVE_HSYNC_START__SHIFT	0
static inline u32 HDMI_ACTIVE_HSYNC_START(u32 val)
{
	return ((val) << HDMI_ACTIVE_HSYNC_START__SHIFT) &
		HDMI_ACTIVE_HSYNC_START__MASK;
}

#define HDMI_ACTIVE_HSYNC_END__MASK	0x1fff0000
#define HDMI_ACTIVE_HSYNC_END__SHIFT	16
static inline u32 HDMI_ACTIVE_HSYNC_END(u32 val)
{
	return ((val) << HDMI_ACTIVE_HSYNC_END__SHIFT) &
		HDMI_ACTIVE_HSYNC_END__MASK;
}

#define HDMI_ACTIVE_VSYNC_START__MASK	0x00001fff
#define HDMI_ACTIVE_VSYNC_START__SHIFT	0
static inline u32 HDMI_ACTIVE_VSYNC_START(u32 val)
{
	return ((val) << HDMI_ACTIVE_VSYNC_START__SHIFT) &
		HDMI_ACTIVE_VSYNC_START__MASK;
}

#define HDMI_ACTIVE_VSYNC_END__MASK	0x1fff0000
#define HDMI_ACTIVE_VSYNC_END__SHIFT	16
static inline u32 HDMI_ACTIVE_VSYNC_END(u32 val)
{
	return ((val) << HDMI_ACTIVE_VSYNC_END__SHIFT) &
		HDMI_ACTIVE_VSYNC_END__MASK;
}

#define HDMI_VSYNC_ACTIVE_F2_START__MASK	0x00001fff
#define HDMI_VSYNC_ACTIVE_F2_START__SHIFT	0
static inline u32 HDMI_VSYNC_ACTIVE_F2_START(u32 val)
{
	return ((val) << HDMI_VSYNC_ACTIVE_F2_START__SHIFT) &
		HDMI_VSYNC_ACTIVE_F2_START__MASK;
}

#define HDMI_VSYNC_ACTIVE_F2_END__MASK		0x00001fff
#define HDMI_VSYNC_ACTIVE_F2_END__SHIFT		16
static inline u32 HDMI_VSYNC_ACTIVE_F2_END(u32 val)
{
	return ((val) << HDMI_VSYNC_ACTIVE_F2_END__SHIFT) &
		HDMI_VSYNC_ACTIVE_F2_END__MASK;
}

#define HDMI_TOTAL_H_TOTAL__MASK	0x00001fff
#define HDMI_TOTAL_H_TOTAL__SHIFT	0
static inline u32 HDMI_TOTAL_H_TOTAL(u32 val)
{
	return ((val) << HDMI_TOTAL_H_TOTAL__SHIFT) &
		HDMI_TOTAL_H_TOTAL__MASK;
}

#define HDMI_TOTAL_V_TOTAL__MASK	0x1fff0000
#define HDMI_TOTAL_V_TOTAL__SHIFT	16
static inline u32 HDMI_TOTAL_V_TOTAL(u32 val)
{
	return ((val) << HDMI_TOTAL_V_TOTAL__SHIFT) &
		HDMI_TOTAL_V_TOTAL__MASK;
}

#define HDMI_VSYNC_TOTAL_F2_V_TOTAL__MASK	0x00001fff
#define HDMI_VSYNC_TOTAL_F2_V_TOTAL__SHIFT	0
static inline u32 HDMI_VSYNC_TOTAL_F2_V_TOTAL(u32 val)
{
	return ((val) << HDMI_VSYNC_TOTAL_F2_V_TOTAL__SHIFT) &
		HDMI_VSYNC_TOTAL_F2_V_TOTAL__MASK;
}

static inline u32 HDMI_AVI_INFO(u32 i0)
{
	return HDMI_AVI_INFO_0 + 0x4*i0;
}

static inline u32 HDMI_GENERIC0(u32 i0)
{
	return HDMI_GENERIC0_0 + 0x4*i0;
}

static inline u32 HDMI_GENERIC1(u32 i0)
{
	return HDMI_GENERIC1_0 + 0x4*i0;
}

static inline void hdmi_panel_update_pps(
		struct hdmi_panel *hdmi_panel, char *pps_cmd)
{
}
static void _hdmi_panel_config_avi_iframe(
		struct hdmi_panel_private *panel, u8 *buffer)
{
	u32 reg_val;
	u8 checksum;
	u8 *avi_frame = &buffer[HDMI_INFOFRAME_HEADER_SIZE];

	checksum = buffer[HDMI_INFOFRAME_HEADER_SIZE - 1];
	reg_val = checksum |
		LEFT_SHIFT_BYTE(avi_frame[0]) |
		LEFT_SHIFT_WORD(avi_frame[1]) |
		LEFT_SHIFT_24BITS(avi_frame[2]);
	hdmi_write(HDMI_AVI_INFO(0), reg_val);

	reg_val = avi_frame[3] |
		LEFT_SHIFT_BYTE(avi_frame[4]) |
		LEFT_SHIFT_WORD(avi_frame[5]) |
		LEFT_SHIFT_24BITS(avi_frame[6]);
	hdmi_write(HDMI_AVI_INFO(1), reg_val);

	reg_val = avi_frame[7] |
		LEFT_SHIFT_BYTE(avi_frame[8]) |
		LEFT_SHIFT_WORD(avi_frame[9]) |
		LEFT_SHIFT_24BITS(avi_frame[10]);
	hdmi_write(HDMI_AVI_INFO(2), reg_val);

	reg_val = avi_frame[11] |
		LEFT_SHIFT_BYTE(avi_frame[12]) |
		LEFT_SHIFT_24BITS(buffer[1]);
	hdmi_write(HDMI_AVI_INFO(3), reg_val);
}

static void hdmi_panel_avi_infoframe_bars(struct hdmi_avi_infoframe *frame,
				const struct drm_connector_state *conn_state)
{
	frame->right_bar = conn_state->tv.margins.right;
	frame->left_bar = conn_state->tv.margins.left;
	frame->top_bar = conn_state->tv.margins.top;
	frame->bottom_bar = conn_state->tv.margins.bottom;
}

static void hdmi_panel_avi_infoframe_colorimetry(
		struct hdmi_avi_infoframe *frame,
		const struct drm_connector_state *conn_state)
{
	u32 colorimetry_val;
	u32 colorimetry_index = conn_state->colorspace & FULL_COLORIMETRY_MASK;

	if (colorimetry_index >= ARRAY_SIZE(hdmi_panel_colorimetry_val))
		colorimetry_val = HDMI_COLORIMETRY_NO_DATA;
	else
		colorimetry_val = hdmi_panel_colorimetry_val[colorimetry_index];

	frame->colorimetry = colorimetry_val & NORMAL_COLORIMETRY_MASK;

	frame->extended_colorimetry = (colorimetry_val >> 2) &
						EXTENDED_COLORIMETRY_MASK;

	/* extended_ace_colorimetry is not present in this kernel's
	 * struct hdmi_avi_infoframe; the ACE bits are unused here.
	 */
}

/*
 * _hdmi_panel_update_avi_colorimetry - reprogram the AVI infoframe
 * colorimetry fields in-place without a full mode set.
 *
 * Called from hdmi_panel_set_colorspace() when user-space changes the
 * DRM "Colorspace" property, and from hdmi_panel_setup_hdr() when HDR
 * is enabled or disabled so the AVI infoframe always stays consistent
 * with the active EOTF.
 *
 * CEA-861-3 Section 6.9 requirement:
 *   "When an HDR Static Metadata InfoFrame is transmitted, the AVI
 *    InfoFrame shall indicate the appropriate colorimetry."
 *
 * The AVI infoframe is re-packed from the current connector state and
 * the current display mode so all other fields (VIC, scan info, bars,
 * quant range) remain unchanged.
 */
static void _hdmi_panel_update_avi_colorimetry(
		struct hdmi_panel_private *panel, u32 colorspace)
{
	struct drm_connector *connector;
	struct drm_connector_state conn_state = {0};
	union hdmi_infoframe frame;
	u8 buffer[HDMI_INFOFRAME_SIZE(AVI)] = {0};
	const struct drm_display_mode *mode;
	int ret;

	HDMI_DEBUG("[AVI CS] requested colorspace=0x%x", colorspace);

	connector = panel->connector;
	if (!connector || !connector->state) {
		HDMI_ERR("[AVR CS] connector=%p or connector->state is NULL, aborting",
				connector);
		return;
	}

	HDMI_DEBUG("[AVI CS] connector OK, current connector->state->colorspace=0x%x",
			connector->state->colorspace);

	/*
	 * Use the requested colorspace value directly rather than reading
	 * connector->state->colorspace, because the connector state may not
	 * yet have been committed when this is called from setup_hdr().
	 */
	conn_state.colorspace = colorspace;

	if (!connector->state->crtc || !connector->state->crtc->state) {
		HDMI_ERR("[AVI CS] ERROR: crtc or crtc->state is NULL, aborting");
		return;
	}

	/* Use the current mode from the connector state */
	mode = &connector->state->crtc->state->adjusted_mode;
	HDMI_DEBUG("[AVI CS] using mode: %dx%d@%dHz",
			mode->hdisplay, mode->vdisplay,
			drm_mode_vrefresh(mode));

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame.avi,
						connector, mode);
	if (ret < 0) {
		HDMI_DEBUG("[AVI CS] drm_hdmi_avi_infoframe_from_display_mode failed: %d",
				ret);
		return;
	}

	/* Override colorimetry with the requested colorspace */
	hdmi_panel_avi_infoframe_colorimetry(&frame.avi, &conn_state);

	hdmi_panel_avi_infoframe_bars(&frame.avi, &conn_state);
	drm_hdmi_avi_infoframe_quant_range(&frame.avi, connector, mode,
				HDMI_QUANTIZATION_RANGE_LIMITED);
	HDMI_DEBUG("[AVI CS] quant range set: ycc_quantization=%u rgb_quantization=%u",
			frame.avi.ycc_quantization_range,
			frame.avi.quantization_range);

	ret = hdmi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (ret < 0) {
		HDMI_ERR("[AVI CS] hdmi_infoframe_pack failed: %d", ret);
		return;
	}
	HDMI_DEBUG("[AVI CS] infoframe packed: %d bytes", ret);

	_hdmi_panel_config_avi_iframe(panel, buffer);
}

/*
 * _hdmi_panel_hdr_colorspace - derive the correct DRM colorspace value
 * from the HDR EOTF when user-space has not explicitly set one.
 *
 * CEA-861-3 / CTA-861-H mapping:
 *   EOTF 0 (SDR gamma)   -> NO_DATA  (BT.709 implied by AVI default)
 *   EOTF 1 (HDR gamma)   -> BT2020_YCC
 *   EOTF 2 (SMPTE ST2084 / PQ / HDR10) -> BT2020_YCC
 *   EOTF 3 (HLG)         -> BT2020_YCC
 *
 * If user-space has already set a non-zero colorspace on the connector
 * state, that explicit value takes priority and this function is not
 * called.
 */
static u32 _hdmi_panel_hdr_colorspace(u32 eotf)
{
	u32 cs;

	switch (eotf) {
	case 1: /* Traditional HDR gamma */
		cs = DRM_MODE_COLORIMETRY_BT2020_YCC;
		break;
	case 2: /* SMPTE ST 2084 / PQ — HDR10 */
		cs = DRM_MODE_COLORIMETRY_BT2020_YCC;
		break;
	case 3: /* Hybrid Log-Gamma */
		cs = DRM_MODE_COLORIMETRY_BT2020_YCC;
		break;
	case 0: /* SDR — traditional gamma */
	default:
		cs = DRM_MODE_COLORIMETRY_NO_DATA;
		break;
	}

	HDMI_DEBUG("[HDR CS MAP]: eotf=%u -> colorspace=0x%x", eotf, cs);
	return cs;
}

/*
 * hdmi_panel_set_colorspace - reprogram AVI infoframe colorimetry.
 *
 * Called by sde_connector pre_kickoff via hdmi_connector_set_colorspace()
 * when user-space changes the DRM "Colorspace" connector property.
 *
 * The AVI infoframe is the only place colorimetry is signalled to the
 * sink on HDMI. This function must reprogram it immediately so the sink
 * switches color decoding on the next frame.
 */
static int hdmi_panel_set_colorspace(
		struct hdmi_panel *hdmi_panel, u32 colorspace)
{
	struct hdmi_panel_private *panel;

	if (!hdmi_panel) {
		HDMI_DEBUG("[AVI CS] ERROR: hdmi_panel is NULL");
		return -EINVAL;
	}

	panel = container_of(hdmi_panel, struct hdmi_panel_private, hdmi_panel);

	HDMI_DEBUG("[AVI CS] panel state: is_hdmi_mode=%d hdr_state=%s",
			panel->is_hdmi_mode,
			panel->hdr_state == HDR_ENABLED ? "ENABLED" : "DISABLED");

	if (!panel->is_hdmi_mode) {
		HDMI_DEBUG("[AVI CS] DVI mode — colorspace signalling not applicable, skip");
		return 0;
	}

	_hdmi_panel_update_avi_colorimetry(panel, colorspace);

	return 0;
}

static inline int hdmi_pabel_get_panel_on(struct hdmi_panel *hdmi_panel)
{
	return 0;
}

static void _hdmi_panel_config_vs_iframe(
		struct hdmi_panel_private *panel,
		struct hdmi_vendor_infoframe *frame,
		u8 *buffer)
{
	u32 reg_val;

	reg_val = LEFT_SHIFT_24BITS(frame->s3d_struct) |
		LEFT_SHIFT_WORD(frame->vic) |
		LEFT_SHIFT_BYTE(buffer[3]) |
		(buffer[7] << 5) | buffer[2];
	hdmi_write(HDMI_VENSPEC_INFO0, reg_val);
}

static void _hdmi_panel_config_spd_iframe(
		struct hdmi_panel_private *panel, u8 *buffer)
{
	int i;
	u32 packet_payload, packet_header;

	packet_header = buffer[0]
			| LEFT_SHIFT_BYTE(buffer[1] & 0x7f)
			| LEFT_SHIFT_WORD(buffer[2] & 0x7f);
	hdmi_write(HDMI_GENERIC1_HDR, packet_header);

	for (i = 0; i < MAX_REG_HDMI_GENERIC1_INDEX; i++) {
		packet_payload = buffer[3 + i * 4]
			| LEFT_SHIFT_BYTE(buffer[4 + i * 4] & 0x7f)
			| LEFT_SHIFT_WORD(buffer[5 + i * 4] & 0x7f)
			| LEFT_SHIFT_24BITS(buffer[6 + i * 4] & 0x7f);
		hdmi_write(HDMI_GENERIC1(i), packet_payload);
	}

	packet_payload = (buffer[27] & 0x7f)
			| LEFT_SHIFT_BYTE(buffer[28] & 0x7f);
	hdmi_write(HDMI_GENERIC1(MAX_REG_HDMI_GENERIC1_INDEX),
			packet_payload);
}

static void _hdmi_panel_config_mode(struct hdmi_panel_private *panel)
{
	u32 frame_ctrl = 0;
	struct hdmi_panel_info *pinfo = &panel->hdmi_panel.pinfo;

	/* TODO: Confirm the register configuration.
	 * is -1 really required, if so, WHY?
	 */
	hdmi_write(HDMI_TOTAL,
			HDMI_TOTAL_H_TOTAL(pinfo->htotal - 1) |
			HDMI_TOTAL_V_TOTAL(pinfo->vtotal - 1));

	hdmi_write(HDMI_ACTIVE_HSYNC,
			HDMI_ACTIVE_HSYNC_START(pinfo->h_start) |
			HDMI_ACTIVE_HSYNC_END(pinfo->h_end));

	hdmi_write(HDMI_ACTIVE_VSYNC,
			HDMI_ACTIVE_VSYNC_START(pinfo->v_start) |
			HDMI_ACTIVE_VSYNC_END(pinfo->v_end));

	if (pinfo->interlace) {
		hdmi_write(HDMI_VSYNC_TOTAL_F2,
			HDMI_VSYNC_TOTAL_F2_V_TOTAL(pinfo->vtotal));
		hdmi_write(HDMI_VSYNC_ACTIVE_F2,
			HDMI_VSYNC_ACTIVE_F2_START(pinfo->v_start + 1) |
			HDMI_VSYNC_ACTIVE_F2_END(pinfo->v_end + 1));
	} else {
		hdmi_write(HDMI_VSYNC_TOTAL_F2,
			HDMI_VSYNC_TOTAL_F2_V_TOTAL(0));
		hdmi_write(HDMI_VSYNC_ACTIVE_F2,
			HDMI_VSYNC_ACTIVE_F2_START(0) |
			HDMI_VSYNC_ACTIVE_F2_END(0));
	}

	if (pinfo->v_active == 480) {
		if (pinfo->h_active_low)
			frame_ctrl |= HDMI_FRAME_CTRL_HSYNC_LOW;
		if (pinfo->v_active_low)
			frame_ctrl |= HDMI_FRAME_CTRL_VSYNC_LOW;
	}

	if (pinfo->interlace)
		frame_ctrl |= HDMI_FRAME_CTRL_INTERLACED_EN;

	HDMI_DEBUG("frame_ctrl=0x%08x", frame_ctrl);
	hdmi_write(HDMI_FRAME_CTRL, frame_ctrl);
}

static void _hdmi_panel_manage_deep_color(
		struct hdmi_panel_private *panel,
		bool deep_color_en)
{
	u32 hdmi_ctrl_reg, vbi_pkt_reg;

	HDMI_DEBUG("Deep Color: %s", deep_color_en ? "On" : "Off");

	if (deep_color_en) {
		hdmi_ctrl_reg = hdmi_read(HDMI_CTRL);

		/* GC CD override */
		hdmi_ctrl_reg |= BIT(27);

		/*
		 * enable deep color for RGB888/YUV444/YUV420 30 bits
		 *
		 * TODO: Enable color depth (CD) configuration for
		 * 1. YUV422 all bpp
		 * 2. RGB888/YUV444/YUV420 36 bits
		 */
		hdmi_ctrl_reg |= BIT(24);
		hdmi_write(HDMI_CTRL, hdmi_ctrl_reg);
		/* Enable GC_CONT and GC_SEND in General Control Packet
		 * (GCP) register so that deep color data is transmitted
		 * to the sink on every frame, allowing the sink to decode
		 * the data correctly.
		 *
		 * GC_CONT: 0x1 - Send GCP on every frame
		 * GC_SEND: 0x1 - Enable GCP Transmission
		 */
		vbi_pkt_reg = hdmi_read(HDMI_VBI_PKT_CTRL);
		vbi_pkt_reg |= BIT(5) | BIT(4);
		hdmi_write(HDMI_VBI_PKT_CTRL, vbi_pkt_reg);
	} else {
		hdmi_ctrl_reg = hdmi_read(HDMI_CTRL);

		/* disable GC CD override */
		hdmi_ctrl_reg &= ~BIT(27);

		/* enable deep color for RGB888/YUV444/YUV420 30 bits */
		hdmi_ctrl_reg &= ~BIT(24);
		hdmi_write(HDMI_CTRL, hdmi_ctrl_reg);

		/* disable the GC packet sending */
		vbi_pkt_reg = hdmi_read(HDMI_VBI_PKT_CTRL);
		vbi_pkt_reg &= ~(BIT(5) | BIT(4));
		hdmi_write(HDMI_VBI_PKT_CTRL, vbi_pkt_reg);
	}
}

static u8 _hdmi_panel_hdr_set_checksum(struct drm_msm_ext_hdr_metadata *meta)
{
	u8 i, checksum = 0;
	u8 *ptr, *buff;
	u32 length, size;
	u32 const type_code = 0x87;
	u32 const version = 0x01;
	u32 const descriptor_id = 0x00;

	/* length of metadata is 26 bytes. */
	length = 0x1a;
	/* add 4 bytes for the header. */
	size = length + HDMI_INFOFRAME_HEADER_SIZE;

	HDMI_DEBUG("[HDR CHECKSUM] computing: eotf=%u max_lum=%u min_lum=%u cll=%u fall=%u",
			meta->eotf, meta->max_luminance, meta->min_luminance,
			meta->max_content_light_level,
			meta->max_average_light_level);

	buff = kzalloc(size, GFP_KERNEL);

	if (!buff)
		return checksum;

	ptr = buff;

	buff[0] = type_code;
	buff[1] = version;
	buff[2] = length;
	buff[3] = 0;

	/* Start information payload */
	buff += HDMI_INFOFRAME_HEADER_SIZE;

	buff[0] = meta->eotf;
	buff[1] = descriptor_id;

	buff[2] = HDMI_GET_LSB(meta->display_primaries_x[0]);
	buff[3] = HDMI_GET_MSB(meta->display_primaries_x[0]);

	buff[4] = HDMI_GET_LSB(meta->display_primaries_x[1]);
	buff[5] = HDMI_GET_MSB(meta->display_primaries_x[1]);

	buff[6] = HDMI_GET_LSB(meta->display_primaries_x[2]);
	buff[7] = HDMI_GET_MSB(meta->display_primaries_x[2]);

	buff[8] = HDMI_GET_LSB(meta->display_primaries_y[0]);
	buff[9] = HDMI_GET_MSB(meta->display_primaries_y[0]);

	buff[10] = HDMI_GET_LSB(meta->display_primaries_y[1]);
	buff[11] = HDMI_GET_MSB(meta->display_primaries_y[1]);

	buff[12] = HDMI_GET_LSB(meta->display_primaries_y[2]);
	buff[13] = HDMI_GET_MSB(meta->display_primaries_y[2]);

	buff[14] = HDMI_GET_LSB(meta->white_point_x);
	buff[15] = HDMI_GET_MSB(meta->white_point_x);
	buff[16] = HDMI_GET_LSB(meta->white_point_y);
	buff[17] = HDMI_GET_MSB(meta->white_point_y);

	buff[18] = HDMI_GET_LSB(meta->max_luminance);
	buff[19] = HDMI_GET_MSB(meta->max_luminance);

	buff[20] = HDMI_GET_LSB(meta->min_luminance);
	buff[21] = HDMI_GET_MSB(meta->min_luminance);

	buff[22] = HDMI_GET_LSB(meta->max_content_light_level);
	buff[23] = HDMI_GET_MSB(meta->max_content_light_level);

	buff[24] = HDMI_GET_LSB(meta->max_average_light_level);
	buff[25] = HDMI_GET_MSB(meta->max_average_light_level);


	print_hex_dump_debug("[HDR InfoFrame]: ", DUMP_PREFIX_NONE,
			16, 16, ptr, size, false);

	/* compute checksum */
	for (i = 0; i < size; i++)
		checksum += ptr[i];

	kfree(ptr);

	HDMI_DEBUG("[HDR CHECKSUM] computed=0x%02x (raw_sum=0x%02x)",
			(u8)(256 - checksum), checksum);

	return 256 - checksum;
}

/*
 * _hdmi_panel_config_hdr_iframe - program the static HDR infoframe payload
 * into the GENERIC0 registers.
 *
 * Implements HPG Section 4.3.2.6 steps 1-3 of the "sequence for sending
 * packets with flush capabilities":
 *   Step 1: Program GENERIC0_LINE (which line to send on; not line 0)
 *   Step 2: Set GENERIC0_CONT = 1 (send every frame)
 *   Step 3: Program packet header + payload contents
 *
 * Steps 4-6 (UPDATE toggle + SEND) are done in _hdmi_panel_flush_hdr_iframe.
 *
 * CEA-861-3 HDR Static Metadata InfoFrame byte layout:
 *   Header:      HB0=0x87 (type), HB1=0x01 (version), HB2=0x1A (length=26)
 *   GENERIC0(0): [7:0]   checksum
 *                [15:8]  DB0: EOTF[2:0] | metadata_descriptor[5:3]
 *                [23:16] DB1: display_primaries_x[0] LSB
 *                [31:24] DB2: display_primaries_x[0] MSB
 *   GENERIC0(1): DB3..DB6   primaries_y[0] LSB/MSB, primaries_x[1] LSB/MSB
 *   GENERIC0(2): DB7..DB10  primaries_y[1] LSB/MSB, primaries_x[2] LSB/MSB
 *   GENERIC0(3): DB11..DB14 primaries_y[2] LSB/MSB, white_point_x LSB/MSB
 *   GENERIC0(4): DB15..DB18 white_point_y LSB/MSB,  max_luminance LSB/MSB
 *   GENERIC0(5): DB19..DB22 min_luminance LSB/MSB,  max_content_light_level LSB/MSB
 *   GENERIC0(6): DB23..DB25 max_average_light_level LSB/MSB, 0x00 (padding)
 */
static void _hdmi_panel_config_hdr_iframe(struct hdmi_panel_private *panel,
		struct drm_msm_ext_hdr_metadata *meta)
{
	u32 packet_payload = 0;
	u32 packet_header = 0;
	u32 packet_control = 0;
	u32 const type_code = 0x87;
	u32 const version = 0x01;
	u32 const length = 0x1a;
	u8 checksum;

	HDMI_DEBUG("[HDR IFRAME] programming static HDR infoframe: eotf=%u", meta->eotf);
	HDMI_DEBUG("[HDR IFRAME] primaries_x: [0]=%u [1]=%u [2]=%u",
			meta->display_primaries_x[0],
			meta->display_primaries_x[1],
			meta->display_primaries_x[2]);
	HDMI_DEBUG("[HDR IFRAME] primaries_y: [0]=%u [1]=%u [2]=%u",
			meta->display_primaries_y[0],
			meta->display_primaries_y[1],
			meta->display_primaries_y[2]);
	HDMI_DEBUG("[HDR IFRAME] white_point: x=%u y=%u",
			meta->white_point_x, meta->white_point_y);
	HDMI_DEBUG("[HDR IFRAME] luminance: max=%u min=%u cll=%u fall=%u",
			meta->max_luminance, meta->min_luminance,
			meta->max_content_light_level,
			meta->max_average_light_level);

	/*
	 * Step 1 (HPG): Program GENERIC0_LINE.
	 * GEN_PKT_CTRL bits [21:16] = GENERIC0_LINE[5:0].
	 * BIT(16) sets line 1 — the first available line after vsync.
	 * HPG Table 61 note: "Line 0 in general should not be used."
	 */
	packet_control = hdmi_read(HDMI_GEN_PKT_CTRL);
	packet_control |= BIT(16);
	hdmi_write(HDMI_GEN_PKT_CTRL, packet_control);
	HDMI_DEBUG("[HDR IFRAME] step1 LINE: GEN_PKT_CTRL=0x%08x", packet_control);

	/*
	 * Step 2 (HPG): Set GENERIC0_CONT so the packet is sent every frame.
	 * HPG Table 13 note: "CONT is recommended to not be toggled once set."
	 */
	packet_control = hdmi_read(HDMI_GEN_PKT_CTRL);
	packet_control |= HDMI_GEN_PKT_CTRL_GENERIC0_CONT;
	hdmi_write(HDMI_GEN_PKT_CTRL, packet_control);
	HDMI_DEBUG("[HDR IFRAME] step2 CONT: GEN_PKT_CTRL=0x%08x", packet_control);

	/*
	 * Step 3 (HPG): Program packet header then payload.
	 * Write GENERIC0_HDR first, then GENERIC0_0 through GENERIC0_6.
	 */
	packet_header = type_code
		| LEFT_SHIFT_BYTE(version)
		| LEFT_SHIFT_WORD(length);
	hdmi_write(HDMI_GENERIC0_HDR, packet_header);
	HDMI_DEBUG("[HDR IFRAME] step3 HDR: GENERIC0_HDR=0x%08x", packet_header);

	/*
	 * Checksum is not mandatory per CEA-861-3 but many sinks require
	 * it for correct decoding. Compute and include it.
	 */
	checksum = _hdmi_panel_hdr_set_checksum(meta);

	/*
	 * GENERIC0(0): checksum | DB0 (EOTF) | DB1 | DB2
	 * DB0[2:0] = EOTF, DB0[5:3] = metadata_descriptor (0 = type 1)
	 * DB1 = display_primaries_x[0] LSB
	 * DB2 = display_primaries_x[0] MSB
	 */
	packet_payload = checksum
		| LEFT_SHIFT_BYTE(meta->eotf & 0x7)
		| LEFT_SHIFT_WORD(HDMI_GET_LSB(meta->display_primaries_x[0]))
		| LEFT_SHIFT_24BITS(HDMI_GET_MSB(meta->display_primaries_x[0]));
	hdmi_write(HDMI_GENERIC0(0), packet_payload);
	HDMI_DEBUG("[HDR IFRAME] GENERIC0[0]=0x%08x (cksum=0x%02x eotf=0x%02x)",
			packet_payload, checksum, meta->eotf & 0x7);

	/*
	 * GENERIC0(1): DB3..DB6
	 * DB3 = primaries_y[0] LSB, DB4 = primaries_y[0] MSB
	 * DB5 = primaries_x[1] LSB, DB6 = primaries_x[1] MSB
	 */
	packet_payload =
		HDMI_GET_LSB(meta->display_primaries_y[0])
		| LEFT_SHIFT_BYTE(HDMI_GET_MSB(meta->display_primaries_y[0]))
		| LEFT_SHIFT_WORD(HDMI_GET_LSB(meta->display_primaries_x[1]))
		| LEFT_SHIFT_24BITS(HDMI_GET_MSB(meta->display_primaries_x[1]));
	hdmi_write(HDMI_GENERIC0(1), packet_payload);
	HDMI_DEBUG("[HDR IFRAME] GENERIC0[1]=0x%08x (py0=%u px1=%u)",
			packet_payload, meta->display_primaries_y[0],
			meta->display_primaries_x[1]);

	/*
	 * GENERIC0(2): DB7..DB10
	 * DB7 = primaries_y[1] LSB, DB8  = primaries_y[1] MSB
	 * DB9 = primaries_x[2] LSB, DB10 = primaries_x[2] MSB
	 */
	packet_payload =
		HDMI_GET_LSB(meta->display_primaries_y[1])
		| LEFT_SHIFT_BYTE(HDMI_GET_MSB(meta->display_primaries_y[1]))
		| LEFT_SHIFT_WORD(HDMI_GET_LSB(meta->display_primaries_x[2]))
		| LEFT_SHIFT_24BITS(HDMI_GET_MSB(meta->display_primaries_x[2]));
	hdmi_write(HDMI_GENERIC0(2), packet_payload);
	HDMI_DEBUG("[HDR IFRAME] GENERIC0[2]=0x%08x (py1=%u px2=%u)",
			packet_payload, meta->display_primaries_y[1],
			meta->display_primaries_x[2]);

	/*
	 * GENERIC0(3): DB11..DB14
	 * DB11 = primaries_y[2] LSB, DB12 = primaries_y[2] MSB
	 * DB13 = white_point_x LSB,  DB14 = white_point_x MSB
	 */
	packet_payload =
		HDMI_GET_LSB(meta->display_primaries_y[2])
		| LEFT_SHIFT_BYTE(HDMI_GET_MSB(meta->display_primaries_y[2]))
		| LEFT_SHIFT_WORD(HDMI_GET_LSB(meta->white_point_x))
		| LEFT_SHIFT_24BITS(HDMI_GET_MSB(meta->white_point_x));
	hdmi_write(HDMI_GENERIC0(3), packet_payload);
	HDMI_DEBUG("[HDR IFRAME] GENERIC0[3]=0x%08x (py2=%u wpx=%u)",
			packet_payload, meta->display_primaries_y[2],
			meta->white_point_x);

	/*
	 * GENERIC0(4): DB15..DB18
	 * DB15 = white_point_y LSB,  DB16 = white_point_y MSB
	 * DB17 = max_luminance LSB,  DB18 = max_luminance MSB
	 */
	packet_payload =
		HDMI_GET_LSB(meta->white_point_y)
		| LEFT_SHIFT_BYTE(HDMI_GET_MSB(meta->white_point_y))
		| LEFT_SHIFT_WORD(HDMI_GET_LSB(meta->max_luminance))
		| LEFT_SHIFT_24BITS(HDMI_GET_MSB(meta->max_luminance));
	hdmi_write(HDMI_GENERIC0(4), packet_payload);
	HDMI_DEBUG("[HDR IFRAME] GENERIC0[4]=0x%08x (wpy=%u max_lum=%u)",
			packet_payload, meta->white_point_y, meta->max_luminance);

	/*
	 * GENERIC0(5): DB19..DB22
	 * DB19 = min_luminance LSB,           DB20 = min_luminance MSB
	 * DB21 = max_content_light_level LSB, DB22 = max_content_light_level MSB
	 */
	packet_payload =
		HDMI_GET_LSB(meta->min_luminance)
		| LEFT_SHIFT_BYTE(HDMI_GET_MSB(meta->min_luminance))
		| LEFT_SHIFT_WORD(HDMI_GET_LSB(meta->max_content_light_level))
		| LEFT_SHIFT_24BITS(HDMI_GET_MSB(meta->max_content_light_level));
	hdmi_write(HDMI_GENERIC0(5), packet_payload);
	HDMI_DEBUG("[HDR IFRAME] GENERIC0[5]=0x%08x (min_lum=%u cll=%u)",
			packet_payload, meta->min_luminance,
			meta->max_content_light_level);

	/*
	 * GENERIC0(6): DB23..DB25
	 * DB23 = max_average_light_level LSB, DB24 = max_average_light_level MSB
	 * DB25 = 0x00 padding (declared length is 26 bytes = DB0..DB25)
	 */
	packet_payload =
		HDMI_GET_LSB(meta->max_average_light_level)
		| LEFT_SHIFT_BYTE(HDMI_GET_MSB(meta->max_average_light_level));
	hdmi_write(HDMI_GENERIC0(6), packet_payload);
	HDMI_DEBUG("[HDR IFRAME] GENERIC0[6]=0x%08x (fall=%u)",
			packet_payload, meta->max_average_light_level);
	HDMI_DEBUG("[HDR IFRAME] payload programming complete");
}

/*
 * _hdmi_panel_flush_hdr_iframe - flush and enable GENERIC0 packet transmission.
 *
 * Implements HPG Section 4.3.2.6 steps 4, 5 and 6:
 *   Step 4: Set GENERIC0_UPDATE = 1  (write trigger into HW shadow)
 *   Step 5: Set GENERIC0_UPDATE = 0  (HPG: "important to do this step because
 *                                     other register writes to the register
 *                                     containing any UPDATE fields will flush
 *                                     the register contents")
 *   Step 6: Set GENERIC0_SEND = 1    (start packet transmission)
 *
 * GENERIC0_CONT must already be set by _hdmi_panel_config_hdr_iframe
 * (step 2) before this function is called.
 */

static void _hdmi_panel_flush_hdr_iframe(struct hdmi_panel_private *panel)
{
	u32 packet_control;

	/* Step 4: assert UPDATE to latch shadow registers into HW transmit path */
	packet_control = hdmi_read(HDMI_GEN_PKT_CTRL);
	packet_control |= HDMI_GEN_PKT_CTRL_GENERIC0_UPDATE__MASK;
	hdmi_write(HDMI_GEN_PKT_CTRL, packet_control);
	HDMI_DEBUG("[HDR FLUSH] step4 UPDATE=1: GEN_PKT_CTRL=0x%08x", packet_control);

	/* Step 5: de-assert UPDATE immediately (HPG: "important to do this step") */
	packet_control = hdmi_read(HDMI_GEN_PKT_CTRL);
	packet_control &= ~HDMI_GEN_PKT_CTRL_GENERIC0_UPDATE__MASK;
	hdmi_write(HDMI_GEN_PKT_CTRL, packet_control);
	HDMI_DEBUG("[HDR FLUSH] step5 UPDATE=0: GEN_PKT_CTRL=0x%08x", packet_control);

	/* Step 6: assert SEND to begin packet transmission */
	packet_control = hdmi_read(HDMI_GEN_PKT_CTRL);
	packet_control |= HDMI_GEN_PKT_CTRL_GENERIC0_SEND;
	hdmi_write(HDMI_GEN_PKT_CTRL, packet_control);
	HDMI_DEBUG("[HDR FLUSH] step6 SEND=1: GEN_PKT_CTRL=0x%08x", packet_control);
}

/*
 * _hdmi_panel_config_dhdr_vsif - program DHDR metadata as a VSIF into GENERIC1.
 *
 * HPG Table 52 shows that VENSPEC_INFO registers contain named hardware fields
 * (VENSPEC_CHECKSUM, HDMI_VIC, HDMI_3D_STRUCT) with the IEEE OUI auto-inserted
 * by hardware. VENSPEC_INFO cannot carry an arbitrary VSIF payload and must NOT
 * be used for DHDR.
 *
 * GENERIC1 is a byte-accurate packet container (no named fields, no auto-OUI).
 * HPG Table 62: GENERIC1 has no flush mechanism; use the no-flush sequence:
 *   LINE -> CONT -> payload -> SEND=1
 * GENERIC1 SEND+CONT+LINE are set at panel enable by _hdmi_panel_manage_iframe.
 *
 * VSIF header written to GENERIC1_HDR:
 *   [7:0]   type    = 0x81
 *   [15:8]  version = 0x01
 *   [23:16] length  = 0x1b (27 payload bytes: 3 OUI + 24 data)
 *
 * VSIF payload PB0..PB27 written to GENERIC1_0..6 (7 regs × 4 bytes = 28 B):
 *   PB0     checksum (sum of all header+payload bytes = 0 mod 256)
 *   PB1..3  IEEE OUI 0x000C03 LSB-first: 0x03, 0x0C, 0x00
 *   PB4     application_version = 0x01 (HDR10+)
 *   PB5     EOTF[2:0]
 *   PB6     metadata_descriptor_id = 0x00 (type 1)
 *   PB7..8  display_primaries_x[0] LSB/MSB
 *   PB9..10 display_primaries_y[0] LSB/MSB
 *   PB11..12 display_primaries_x[1] LSB/MSB
 *   PB13..14 display_primaries_y[1] LSB/MSB
 *   PB15..16 display_primaries_x[2] LSB/MSB
 *   PB17..18 display_primaries_y[2] LSB/MSB
 *   PB19..20 white_point_x LSB/MSB
 *   PB21..22 white_point_y LSB/MSB
 *   PB23..24 max_luminance LSB/MSB
 *   PB25..26 min_luminance LSB/MSB
 *   PB27    padding (0x00)
 */
static void _hdmi_panel_config_dhdr_vsif(
		struct hdmi_panel_private *panel,
		struct drm_msm_ext_hdr_metadata *meta)
{
	/* PB0 = checksum placeholder, PB1..PB27 = 27 VSIF payload bytes */
	u8 pb[28];
	u8 checksum = 0;
	int i;

	memset(pb, 0, sizeof(pb));

	pb[1]  = 0x03;  /* IEEE OUI[0]: HDMI LLC, LSB first */
	pb[2]  = 0x0C;  /* IEEE OUI[1] */
	pb[3]  = 0x00;  /* IEEE OUI[2] */
	pb[4]  = 0x01;  /* application_version: HDR10+ */
	pb[5]  = meta->eotf & 0x7;
	pb[6]  = 0x00;  /* metadata_descriptor_id: static type 1 */
	pb[7]  = HDMI_GET_LSB(meta->display_primaries_x[0]);
	pb[8]  = HDMI_GET_MSB(meta->display_primaries_x[0]);
	pb[9]  = HDMI_GET_LSB(meta->display_primaries_y[0]);
	pb[10] = HDMI_GET_MSB(meta->display_primaries_y[0]);
	pb[11] = HDMI_GET_LSB(meta->display_primaries_x[1]);
	pb[12] = HDMI_GET_MSB(meta->display_primaries_x[1]);
	pb[13] = HDMI_GET_LSB(meta->display_primaries_y[1]);
	pb[14] = HDMI_GET_MSB(meta->display_primaries_y[1]);
	pb[15] = HDMI_GET_LSB(meta->display_primaries_x[2]);
	pb[16] = HDMI_GET_MSB(meta->display_primaries_x[2]);
	pb[17] = HDMI_GET_LSB(meta->display_primaries_y[2]);
	pb[18] = HDMI_GET_MSB(meta->display_primaries_y[2]);
	pb[19] = HDMI_GET_LSB(meta->white_point_x);
	pb[20] = HDMI_GET_MSB(meta->white_point_x);
	pb[21] = HDMI_GET_LSB(meta->white_point_y);
	pb[22] = HDMI_GET_MSB(meta->white_point_y);
	pb[23] = HDMI_GET_LSB(meta->max_luminance);
	pb[24] = HDMI_GET_MSB(meta->max_luminance);
	pb[25] = HDMI_GET_LSB(meta->min_luminance);
	pb[26] = HDMI_GET_MSB(meta->min_luminance);
	/* pb[27] = 0x00 padding */

	/* Checksum: HB0+HB1+HB2+PB0..PB27 = 0 mod 256 */
	checksum = 0x81 + 0x01 + 0x1b;
	for (i = 1; i < 28; i++)
		checksum += pb[i];
	pb[0] = (u8)(256 - checksum);

	/* Write 3-byte VSIF header into GENERIC1_HDR */
	hdmi_write(HDMI_GENERIC1_HDR,
		0x81 | LEFT_SHIFT_BYTE(0x01) | LEFT_SHIFT_WORD(0x1b));

	/* Write 28 payload bytes (PB0..PB27) into GENERIC1_0..6 */
	for (i = 0; i < 7; i++) {
		hdmi_write(HDMI_GENERIC1(i),
			pb[i * 4]
			| LEFT_SHIFT_BYTE(pb[i * 4 + 1])
			| LEFT_SHIFT_WORD(pb[i * 4 + 2])
			| LEFT_SHIFT_24BITS(pb[i * 4 + 3]));
	}

	HDMI_DEBUG("[DHDR VSIF] GENERIC1 payload complete (cksum=0x%02x eotf=%u)",
			pb[0], meta->eotf & 0x7);
}

static void hdmi_panel_set_spd_infoframe(struct hdmi_panel_private *panel)
{
	u8 buffer[HDMI_INFOFRAME_SIZE(SPD)] = {0};
	struct hdmi_spd_infoframe frame;
	int ret;

	if (!panel) {
		HDMI_ERR("invalid input");
		return;
	}

	ret = hdmi_spd_infoframe_init(&frame,
			HDMI_DEFAULT_VENDOR_NAME,
			HDMI_DEFAULT_PRODUCT_NAME);
	if (ret) {
		HDMI_ERR("HDMI SPD Infoframe configuration failed: %i", ret);
		return;
	}

	ret = hdmi_spd_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (ret < 0) {
		HDMI_ERR("failed to pack SPD infoframe: %i", ret);
		return;
	}

	print_hex_dump_debug("[SPD InfoFrame]: ", DUMP_PREFIX_NONE,
			16, 16, buffer, sizeof(buffer), false);

	_hdmi_panel_config_spd_iframe(panel, buffer);

	HDMI_DEBUG("OK");
}

/*
 * _hdmi_panel_enable_dhdr_vsif - ensure GENERIC1 packet transmission is active.
 *
 * GENERIC1_SEND, GENERIC1_CONT and GENERIC1_LINE are set at panel enable by
 * _hdmi_panel_manage_iframe. Re-assert them here in case they were cleared
 * by a prior _hdmi_panel_disable_dhdr_vsif call.
 *
 * The standard VSIF (VENSPEC channel, INFOFRAME_CTRL0 bits 12-13) is
 * intentionally not touched: DHDR now uses GENERIC1, so the VENSPEC channel
 * continues to carry the standard 4K / 3D VSIF uninterrupted.
 */
static void _hdmi_panel_enable_dhdr_vsif(struct hdmi_panel_private *panel)
{
	u32 reg_val;

	/* GENERIC1_LINE[24]=1, GENERIC1_CONT[5]=1, GENERIC1_SEND[4]=1 */
	reg_val = hdmi_read(HDMI_GEN_PKT_CTRL);
	reg_val |= LEFT_SHIFT_24BITS(0x1) | BIT(5) | BIT(4);
	hdmi_write(HDMI_GEN_PKT_CTRL, reg_val);
	HDMI_DEBUG("[DHDR VSIF] GENERIC1 SEND+CONT enabled: GEN_PKT_CTRL=0x%08x",
			reg_val);
}

/*
 * _hdmi_panel_disable_dhdr_vsif - stop DHDR VSIF and restore SPD infoframe.
 *
 * DHDR uses the GENERIC1 channel, which is shared with the SPD infoframe.
 * On disable: stop GENERIC1 briefly, reprogram SPD into GENERIC1, then
 * re-enable GENERIC1 so the SPD infoframe resumes on the next frame.
 *
 * The VENSPEC channel (standard 4K/3D VSIF, INFOFRAME_CTRL0 bits 12-13)
 * is NOT touched — it was never used by DHDR and must continue transmitting.
 */
static void _hdmi_panel_disable_dhdr_vsif(struct hdmi_panel_private *panel)
{
	u32 reg_val;

	/* Stop GENERIC1 to avoid a partial DHDR/SPD overlap during reprogram */
	reg_val = hdmi_read(HDMI_GEN_PKT_CTRL);
	reg_val &= ~(BIT(5) | BIT(4));  /* clear GENERIC1_CONT and GENERIC1_SEND */
	hdmi_write(HDMI_GEN_PKT_CTRL, reg_val);

	/* Restore SPD infoframe data into GENERIC1 */
	hdmi_panel_set_spd_infoframe(panel);

	/* Re-enable GENERIC1 for SPD transmission */
	reg_val = hdmi_read(HDMI_GEN_PKT_CTRL);
	reg_val |= LEFT_SHIFT_24BITS(0x1) | BIT(5) | BIT(4);
	hdmi_write(HDMI_GEN_PKT_CTRL, reg_val);
}

/*
 * _hdmi_panel_disable_hdr_iframe - stop GENERIC0 packet transmission.
 *
 * HPG Section 4.3.2.6 "To disable packets without flush capabilities":
 *   1. Clear GENERIC0_SEND = 0
 *   2. Clear GENERIC0_CONT = 0
 *   3. Clear GENERIC0_LINE = 0
 * (HPG lists these steps for the no-flush path; for the flush path the
 * same register fields apply for the disable direction.)
 */
static void _hdmi_panel_disable_hdr_iframe(struct hdmi_panel_private *panel)
{
	u32 packet_control;

	packet_control = hdmi_read(HDMI_GEN_PKT_CTRL);
	/* Clear SEND, CONT and LINE[21:16] in one read-modify-write */
	packet_control &= ~(HDMI_GEN_PKT_CTRL_GENERIC0_SEND |
			    HDMI_GEN_PKT_CTRL_GENERIC0_CONT |
			    (0x3F << 16));
	hdmi_write(HDMI_GEN_PKT_CTRL, packet_control);
}

static void _hdmi_panel_manage_iframe(struct hdmi_panel_private *panel)
{
	u32 reg_val;
	/*
	 * VENSPEC_INFO_CONT | VENSPEC_INFO_SEND
	 * | AVI_INFO_CONT | AVI_INFO_SEND
	 * Setup HDMI TX VSIF and AVI IF packet control
	 * Enable this packet to transmit every frame
	 * Enable HDMI TX engine to transmit VS and AVI packet
	 */
	reg_val = hdmi_read(HDMI_INFOFRAME_CTRL0);
	reg_val |= (BIT(13) | BIT(12) | BIT(1) | BIT(0));
	hdmi_write(HDMI_INFOFRAME_CTRL0, reg_val);
	reg_val = hdmi_read(HDMI_INFOFRAME_CTRL1);
	reg_val &= ~(LEFT_SHIFT_24BITS(0x3F) | 0x3F);
	reg_val |= HDMI_AVI_IFRAME_LINE_NUMBER |
		LEFT_SHIFT_24BITS(HDMI_VENDOR_IFRAME_LINE_NUMBER);
	hdmi_write(HDMI_INFOFRAME_CTRL1, reg_val);
	/*
	 * GENERIC1_LINE | GENERIC1_CONT | GENERIC1_SEND
	 * Setup HDMI TX generic packet control
	 * Enable this packet to transmit every frame
	 * Enable HDMI TX engine to transmit Generic packet 1
	 */
	reg_val = hdmi_read(HDMI_GEN_PKT_CTRL);
	reg_val |= (LEFT_SHIFT_24BITS(0x1) | (1 << 5) | (1 << 4));
	hdmi_write(HDMI_GEN_PKT_CTRL, reg_val);
}

static void _hdmi_panel_manage_ctrl(struct hdmi_panel_private *panel, bool en)
{
	u32 reg_val;

	/* If enable == true; then enable
	 * 1. HDMI CTRL
	 * 2. AVI IF
	 * 3. VS IF
	 * 4. SPD IF
	 */

	reg_val = hdmi_read(HDMI_CTRL);

	if (en) {
		/* Enable HDMI TX controller */
		reg_val |= HDMI_CTRL_ENABLE;

		if (!panel->is_hdmi_mode) {
			/* TODO: Understand why are we setting HDMI_CTRL_HDMI */
			reg_val |= HDMI_CTRL_HDMI;
			hdmi_write(HDMI_CTRL, reg_val);
			reg_val &= ~HDMI_CTRL_HDMI;
		} else {
			_hdmi_panel_manage_iframe(panel);
			reg_val |= HDMI_CTRL_HDMI;
		}
	} else {
		/* TODO: recheck the below step and confirm what is the
		 * correct action/bit to be set.
		 * HDMI_CTRL_ENABLE or HDMI_CTRL_HDMI
		 */
		reg_val &= ~HDMI_CTRL_ENABLE;
	}
	hdmi_write(HDMI_CTRL, reg_val);
	HDMI_DEBUG("HDMI Core: %s, HDMI_CTRL=0x%08x\n",
			en ? "Enable" : "Disable", reg_val);
}

static bool hdmi_panel_check_mode_hdmi(struct hdmi_panel_info *pinfo)
{
	if (pinfo->h_active == 640 &&
		pinfo->v_active == 480)
		return false;
	return true;
}

static int hdmi_panel_get_modes(struct hdmi_panel *hdmi_panel,
		struct drm_connector *connector)
{
	int rc = 0;

	if (!hdmi_panel || !connector) {
		HDMI_ERR("invalid input");
		return -EINVAL;
	}

	if (hdmi_panel->edid_ctrl && hdmi_panel->edid_ctrl->edid) {
		rc = _sde_edid_update_modes(connector,
					hdmi_panel->edid_ctrl);
	}

	return rc;

}

static void hdmi_panel_set_avi_infoframe(
		struct hdmi_panel_private *panel,
		const struct drm_display_mode *mode)
{
	struct drm_connector *connector;
	union hdmi_infoframe frame;
	struct drm_connector_state conn_state = {0};
	u8 buffer[HDMI_INFOFRAME_SIZE(AVI)] = {0};
	int ret;

	if (!mode || !panel) {
		HDMI_ERR("invalid input");
		return;
	}

	connector = panel->connector;
	conn_state.colorspace = connector->state->colorspace;

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame.avi,
						connector, mode);
	if (ret < 0) {
		HDMI_ERR("HDMI AVI Infoframe configuration failed: %i", ret);
		return;
	}

	hdmi_panel_avi_infoframe_bars(&frame.avi, &conn_state);
	hdmi_panel_avi_infoframe_colorimetry(&frame.avi, &conn_state);
	drm_hdmi_avi_infoframe_quant_range(&frame.avi, connector, mode,
				HDMI_QUANTIZATION_RANGE_LIMITED);

	ret = hdmi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (ret < 0) {
		HDMI_ERR("failed to pack AVI infoframe: %i", ret);
		return;
	}

	print_hex_dump_debug("[AVI InfoFrame]: ", DUMP_PREFIX_NONE,
			16, 16, buffer, sizeof(buffer), false);

	_hdmi_panel_config_avi_iframe(panel, buffer);

	HDMI_DEBUG("OK");
}

static void hdmi_panel_set_vs_infoframe(
		struct hdmi_panel_private *panel,
		const struct drm_display_mode *mode)
{
	int ret;
	struct hdmi_vendor_infoframe frame;
	u8 buffer[HDMI_INFOFRAME_SIZE(VENDOR)] = {0};
	struct drm_connector *connector;

	if (!panel || !mode) {
		HDMI_ERR("invalid input");
		return;
	}

	connector =  panel->connector;

	ret = drm_hdmi_vendor_infoframe_from_display_mode(
			&frame, connector, mode);
	if (ret < 0) {
		HDMI_ERR("HDMI VENDOR Infoframe configuration failed: %i", ret);
		return;
	}

	ret = hdmi_vendor_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (ret < 0) {
		HDMI_ERR("failed to pack Vendor infoframe: %i", ret);
		return;
	}

	print_hex_dump_debug("[VENDOR InfoFrame]: ", DUMP_PREFIX_NONE,
			16, 16, buffer, sizeof(buffer), false);

	_hdmi_panel_config_vs_iframe(panel, &frame, buffer);

	HDMI_DEBUG("OK");
}

/*
 * hdmi_panel_setup_hdr - top-level HDR programming entry point.
 *
 * Handles three distinct cases:
 *
 * 1. Static HDR (hdr_state == HDR_ENABLED, dhdr_update == false)
 *    Follows HPG Section 4.3.2.6 flush sequence for GENERIC0:
 *      LINE -> CONT -> payload -> UPDATE=1 -> UPDATE=0 -> SEND=1
 *
 * 2. Dynamic HDR (dhdr_update == true)
 *    On HDMI 4.0.0 HW (pre-4.1.0), the EMP/DHDR_mem_pool path is not
 *    available. DHDR metadata is carried via the legacy VENSPEC_INFO
 *    (VSIF) channel per HPG Table 51 sequence:
 *      LINE -> CONT -> payload -> SEND=1
 *    The static HDR infoframe is also programmed so sinks that only
 *    support static HDR still receive valid metadata. Both caches are
 *    updated independently.
 *
 * 3. HDR disable (hdr_state == HDR_DISABLED)
 *    Stops both GENERIC0 (static HDR) and VENSPEC_INFO (DHDR VSIF),
 *    clears both caches, and resets LINE per HPG disable sequence.
 *
 * NULL hdr_meta (re-enable path after modeset):
 *    Replays whichever cached state was last active without modifying
 *    the caches:
 *    - DHDR active  -> replay DHDR VSIF + static infoframe
 *    - Static HDR   -> replay static infoframe only
 *    - Neither      -> no-op
 */
static int hdmi_panel_setup_hdr(struct hdmi_panel *hdmi_panel,
		struct drm_msm_ext_hdr_metadata *hdr_meta,
		bool dhdr_update, u64 core_clk_rate, bool flush)
{
	int rc = 0;
	struct hdmi_panel_private *panel;

	if (!hdmi_panel) {
		HDMI_ERR("invalid input");
		return -EINVAL;
	}

	panel = container_of(hdmi_panel, struct hdmi_panel_private, hdmi_panel);

	/*
	 * NULL hdr_meta: re-enable path (e.g. post_enable after a modeset).
	 * Replay whichever HDR mode was previously active using the cached
	 * metadata. Do not modify the caches on this path.
	 */
	if (!hdr_meta) {
		if (panel->dhdr_state == DHDR_SENDING) {
			/* Replay VSIF channel (HPG Table 51 sequence) */
			_hdmi_panel_config_dhdr_vsif(panel,
					&panel->cached_dhdr_meta);
			_hdmi_panel_enable_dhdr_vsif(panel);
			/* Replay static infoframe for dual-path sinks */
			_hdmi_panel_config_hdr_iframe(panel,
					&panel->cached_hdr_meta);
			_hdmi_panel_flush_hdr_iframe(panel);
		} else if (panel->hdr_state == HDR_ENABLED) {
			_hdmi_panel_config_hdr_iframe(panel,
					&panel->cached_hdr_meta);
			_hdmi_panel_flush_hdr_iframe(panel);
		} else {
			HDMI_DEBUG("[HDR SETUP] NULL meta: no active HDR state, skipping");
		}
		goto end;
	}

	/* Derive panel HDR state from the metadata hdr_state field */
	panel->hdr_state = hdr_meta->hdr_state ? HDR_ENABLED : HDR_DISABLED;
	HDMI_DEBUG("[HDR SETUP] hdr_meta->hdr_state=%u -> panel hdr_state=%s",
			hdr_meta->hdr_state,
			panel->hdr_state == HDR_ENABLED ? "ENABLED" : "DISABLED");

	if (panel->hdr_state == HDR_ENABLED) {
		/*
		 * Cache the static metadata for replay on re-enable.
		 * Always program the static infoframe regardless of whether
		 * this is also a DHDR update, so sinks that only support
		 * static HDR still receive valid data.
		 */
		memcpy(&panel->cached_hdr_meta, hdr_meta,
				sizeof(struct drm_msm_ext_hdr_metadata));

		/*
		 * CEA-861-3 Section 6.9: when HDR is active the AVI infoframe
		 * MUST signal the matching colorimetry. If user-space has not
		 * explicitly set a colorspace (NO_DATA == 0), derive it from
		 * the EOTF so the sink always receives a consistent pair of
		 * AVI + HDR infoframes.
		 */
		if (panel->connector && panel->connector->state) {
			u32 cs = panel->connector->state->colorspace;

			if (cs == DRM_MODE_COLORIMETRY_NO_DATA)
				cs = _hdmi_panel_hdr_colorspace(hdr_meta->eotf);

			_hdmi_panel_update_avi_colorimetry(panel, cs);
		}

		/* HPG steps 1-3: LINE, CONT, payload */
		_hdmi_panel_config_hdr_iframe(panel, hdr_meta);

		if (dhdr_update) {
			/*
			 * Dynamic HDR: program the VENSPEC_INFO channel with
			 * the per-frame dynamic metadata (HPG Table 51 sequence:
			 * LINE -> CONT -> payload -> SEND).
			 *
			 * Cache DHDR metadata separately so it can be replayed
			 * independently on a subsequent re-enable.
			 *
			 * Always flush the static infoframe immediately so both
			 * channels are synchronised at the same frame boundary.
			 */
			memcpy(&panel->cached_dhdr_meta, hdr_meta,
					sizeof(struct drm_msm_ext_hdr_metadata));
			panel->dhdr_state = DHDR_SENDING;

			_hdmi_panel_config_dhdr_vsif(panel, hdr_meta);
			_hdmi_panel_enable_dhdr_vsif(panel);

			/* HPG steps 4-6: UPDATE toggle + SEND */
			_hdmi_panel_flush_hdr_iframe(panel);
		} else {
			/*
			 * Static HDR only. If DHDR was previously active,
			 * stop the VSIF and clear the DHDR cache.
			 */
			if (panel->dhdr_state == DHDR_SENDING) {
				_hdmi_panel_disable_dhdr_vsif(panel);
				memset(&panel->cached_dhdr_meta, 0,
					sizeof(struct drm_msm_ext_hdr_metadata));
				panel->dhdr_state = DHDR_IDLE;
			}

			/* HPG steps 4-6: UPDATE toggle + SEND (conditional) */
			if (flush)
				_hdmi_panel_flush_hdr_iframe(panel);
		}
	} else {
		/*
		 * HDR is being turned off entirely.
		 * Stop both the static infoframe and the DHDR VSIF,
		 * and clear both caches so a subsequent NULL call does
		 * not re-enable HDR.
		 */
		if (panel->dhdr_state == DHDR_SENDING) {
			memset(&panel->cached_dhdr_meta, 0,
					sizeof(struct drm_msm_ext_hdr_metadata));
			panel->dhdr_state = DHDR_IDLE;
		}

		memset(&panel->cached_hdr_meta, 0,
				sizeof(struct drm_msm_ext_hdr_metadata));

		_hdmi_panel_disable_hdr_iframe(panel);

		/*
		 * HDR is off: restore AVI colorimetry to whatever the
		 * connector state says (typically NO_DATA = BT.709 default).
		 */
		if (panel->connector && panel->connector->state) {
			u32 cs = panel->connector->state->colorspace;

			_hdmi_panel_update_avi_colorimetry(panel, cs);
		}

		HDMI_DEBUG("[HDR SETUP] HDR fully disabled, all caches cleared");
	}

end:
	return rc;
}

static void hdmi_panel_save_mode(struct hdmi_panel_info *pinfo,
		const struct drm_display_mode *mode)
{
	struct hdmi_panel *hdmi_panel;
	const u32 num_components = 24;

	hdmi_panel = container_of(pinfo, struct hdmi_panel, pinfo);
	/* TODO: Confirm whether mode copy is correct. */
	pinfo->htotal = mode->htotal;
	pinfo->vtotal = mode->vtotal;

	pinfo->h_active = mode->hdisplay;
	pinfo->h_skew = mode->hskew;

	pinfo->h_front_porch =
		mode->hsync_start - mode->hdisplay;
	pinfo->h_back_porch =
		mode->htotal - mode->hsync_end;

	pinfo->h_start = mode->htotal - mode->hsync_start;
	pinfo->h_end = mode->htotal - mode->hsync_start + mode->hdisplay;

	pinfo->h_sync_width = pinfo->htotal - (pinfo->h_active +
			pinfo->h_front_porch + pinfo->h_back_porch);

	pinfo->v_active = mode->vdisplay;

	pinfo->v_front_porch =
		mode->vsync_start - mode->vdisplay;
	pinfo->v_back_porch =
		mode->vtotal - mode->vsync_end;

	pinfo->v_start = mode->vtotal - mode->vsync_start - 1;
	pinfo->v_end = mode->vtotal - mode->vsync_start + mode->vdisplay - 1;

	pinfo->v_sync_width = pinfo->vtotal - (pinfo->v_active +
			pinfo->v_front_porch + pinfo->v_back_porch);

	pinfo->refresh_rate = drm_mode_vrefresh(mode);
	pinfo->pixel_clk_khz = mode->clock;

	pinfo->v_active_low =
		!!(mode->flags & DRM_MODE_FLAG_NVSYNC);

	pinfo->h_active_low =
		!!(mode->flags & DRM_MODE_FLAG_NHSYNC);

	pinfo->interlace =
		!!(mode->flags & DRM_MODE_FLAG_INTERLACE);

	pinfo->bpp =
		hdmi_panel->connector->display_info.bpc * num_components;
}

static void hdmi_panel_resolution_info(struct hdmi_panel_private *panel)
{
	struct hdmi_panel_info *pinfo = &panel->hdmi_panel.pinfo;

	/*
	 * print resolution info as this is a result
	 * of the user initiated action of cable connection.
	 */
	HDMI_INFO("HDMI RESOLUTION: active(back|front|width|low)");
	HDMI_INFO("%i(%i|%i|%i|%i)x%i(%i|%i|%i|%i)@%ifps %ibpp %iKHz",
		pinfo->h_active, pinfo->h_back_porch, pinfo->h_front_porch,
		pinfo->h_sync_width, pinfo->h_active_low,
		pinfo->v_active, pinfo->v_back_porch, pinfo->v_front_porch,
		pinfo->v_sync_width, pinfo->v_active_low,
		pinfo->refresh_rate, pinfo->bpp, pinfo->pixel_clk_khz);
}

static enum drm_mode_status hdmi_panel_validate_mode(
		struct hdmi_panel *hdmi_panel,
		const struct drm_display_mode *mode)
{
	/*
	 * TODO: Confirm the mode validation criteria
	 * 1. Reject all modes with TMDS greater than Max TMDS
	 * 2. Reject all Interlace modes
	 * 3. Allow any other modes.
	 */

	struct drm_display_info *info;
	enum drm_mode_status mode_status = MODE_BAD;

	if (!hdmi_panel || !mode) {
		HDMI_ERR("invalid input");
		return mode_status;
	}

	info = &hdmi_panel->connector->display_info;

	if ((mode->clock <= info->max_tmds_clock)
		&& !(mode->flags & DRM_MODE_FLAG_INTERLACE))
		mode_status = MODE_OK;

	HDMI_DEBUG("[%s] mode is %s", mode->name,
			(mode_status == MODE_OK) ?
			"valid" : "invalid");

	return mode_status;
}

/*
 * hdmi_panel_hdr_supported - query whether the connected sink advertises
 * HDR capability in its EDID.
 *
 * Reads the CEA-861-3 HDR Static Metadata Data Block from the parsed
 * connector display_info. This block is populated by the DRM EDID layer
 * (drm_parse_hdr_metadata_block) during _sde_edid_update_modes() and
 * stored in connector->display_info.hdr_sink_metadata.
 *
 * Returns:
 *   1  - sink supports HDR (PQ or HLG EOTF advertised)
 *   0  - sink does not support HDR (HDMI 1.4 or no HDR block in EDID)
 *  -EINVAL - invalid input
 */
static int hdmi_panel_hdr_supported(struct hdmi_panel *hdmi_panel)
{
	struct hdmi_panel_private *panel;
	struct sde_edid_ctrl *edid_ctrl;
	u8 eotf;

	if (!hdmi_panel) {
		HDMI_ERR("[HDR SUPP] ERROR: hdmi_panel is NULL");
		return -EINVAL;
	}

	panel = container_of(hdmi_panel, struct hdmi_panel_private, hdmi_panel);

	/*
	 * HDR sink capability is stored in the sde_edid_ctrl parsed from
	 * the CEA extension block during _sde_edid_update_modes().
	 * hdr_data.eotf is a bitmask:
	 *   bit 0 = Traditional SDR gamma
	 *   bit 1 = Traditional HDR gamma
	 *   bit 2 = SMPTE ST 2084 / PQ  (HDR10)
	 *   bit 3 = Hybrid Log-Gamma    (HLG)
	 *
	 * A sink is HDR-capable only if it sets bit 2 (PQ) or bit 3 (HLG).
	 * HDMI 1.4 sinks never have this block so eotf == 0.
	 */
	edid_ctrl = hdmi_panel->edid_ctrl;
	if (!edid_ctrl) {
		HDMI_ERR("[HDR SUPP] ERROR: edid_ctrl is NULL - EDID not yet read?");
		return 0;
	}

	eotf = edid_ctrl->hdr_data.eotf;

	HDMI_DEBUG("[HDR SUPP] EDID HDR Static Metadata block:");
	HDMI_DEBUG("[HDR SUPP]   eotf bitmask     = 0x%02x", eotf);
	HDMI_DEBUG("[HDR SUPP]   SDR gamma  (b0)  = %d", !!(eotf & BIT(0)));
	HDMI_DEBUG("[HDR SUPP]   HDR gamma  (b1)  = %d", !!(eotf & BIT(1)));
	HDMI_DEBUG("[HDR SUPP]   PQ/HDR10   (b2)  = %d", !!(eotf & BIT(2)));
	HDMI_DEBUG("[HDR SUPP]   HLG        (b3)  = %d", !!(eotf & BIT(3)));

	if (eotf & (BIT(2) | BIT(3))) {
		HDMI_DEBUG("[HDR SUPP]: sink SUPPORTS HDR (HDMI 2.0a+, PQ or HLG set)");
		return 1;
	}

	HDMI_DEBUG("[HDR SUPP]: sink does NOT support HDR\n");

	return 0;
}

static int hdmi_panel_set_mode(struct hdmi_panel *hdmi_panel,
		const struct drm_display_mode *mode)
{
	struct hdmi_panel_private *panel;
	struct hdmi_panel_info *pinfo;

	if (!hdmi_panel || !mode) {
		HDMI_ERR("invalid input");
		return -EINVAL;
	}

	pinfo = &hdmi_panel->pinfo;
	panel = container_of(hdmi_panel, struct hdmi_panel_private, hdmi_panel);

	hdmi_panel_save_mode(pinfo, mode);
	_hdmi_panel_config_mode(panel);

	panel->is_hdmi_mode = hdmi_panel_check_mode_hdmi(pinfo);
	if (panel->is_hdmi_mode) {
		hdmi_panel_set_avi_infoframe(panel, mode);
		hdmi_panel_set_vs_infoframe(panel, mode);
		hdmi_panel_set_spd_infoframe(panel);
	}

	return 0;
}

static int hdmi_panel_enable(struct hdmi_panel *hdmi_panel)
{
	struct hdmi_panel_private *panel;

	if (!hdmi_panel) {
		HDMI_ERR("invalid input");
		return -EINVAL;
	}

	/*
	 * TODO: Ensure all the reference timers
	 * are configured before enabling the HDMI
	 * TX Controlloer.
	 */
	panel = container_of(hdmi_panel, struct hdmi_panel_private, hdmi_panel);

	_hdmi_panel_manage_deep_color(panel, hdmi_panel->dc_enable);
	_hdmi_panel_manage_ctrl(panel, true);
	hdmi_panel_resolution_info(panel);

	/*
	 * TODO: Ensure all the reference timers
	 * are enabled after enabling the HDMI
	 * TX Controller.
	 */

	return 0;
}

static int hdmi_panel_disable(struct hdmi_panel *hdmi_panel)
{
	struct hdmi_panel_private *panel;

	if (!hdmi_panel) {
		HDMI_ERR("invalid input");
		return -EINVAL;
	}

	panel = container_of(hdmi_panel, struct hdmi_panel_private, hdmi_panel);

	_hdmi_panel_manage_ctrl(panel, false);

	/*
	 * TODO: Ensure all the reference timers are disabled
	 * after disabling the HDMI TX Controller.
	 */
	return 0;
}

struct hdmi_panel *hdmi_panel_get(struct device *dev,
		struct drm_connector *connector, struct hdmi_parser *parser)
{
	struct hdmi_panel_private *panel;
	struct hdmi_panel *hdmi_panel;
	struct sde_connector *sde_conn;

	if (!dev || !connector || !parser) {
		HDMI_ERR("invalid inputs");
		return ERR_PTR(-EINVAL);
	}

	panel = kzalloc(sizeof(*panel), GFP_KERNEL);
	if (!panel) {
		HDMI_ERR("Insufficient memory");
		return ERR_PTR(-ENOMEM);
	}

	panel->dev = dev;
	panel->connector = connector;
	hdmi_panel = &panel->hdmi_panel;
	hdmi_panel->connector = connector;

	sde_conn = to_sde_connector(connector);
	sde_conn->drv_panel = hdmi_panel;

	panel->io_data = parser->get_io(parser, "hdmi_ctrl");

	hdmi_panel->hdr_supported = hdmi_panel_hdr_supported;
	hdmi_panel->set_mode	= hdmi_panel_set_mode;
	hdmi_panel->get_modes	= hdmi_panel_get_modes;
	hdmi_panel->enable	= hdmi_panel_enable;
	hdmi_panel->disable	= hdmi_panel_disable;
	hdmi_panel->setup_hdr	= hdmi_panel_setup_hdr;
	hdmi_panel->update_pps	= hdmi_panel_update_pps;
	hdmi_panel->get_panel_on = hdmi_pabel_get_panel_on;
	hdmi_panel->validate_mode = hdmi_panel_validate_mode;
	hdmi_panel->set_colorspace = hdmi_panel_set_colorspace;
	hdmi_panel->pclk_factor = 1;

	return hdmi_panel;
}

void  hdmi_panel_put(struct hdmi_panel *hdmi_panel)
{
	struct hdmi_panel_private *panel;

	if (!hdmi_panel)
		return;

	panel = container_of(hdmi_panel, struct hdmi_panel_private, hdmi_panel);

	kfree(panel);
}
