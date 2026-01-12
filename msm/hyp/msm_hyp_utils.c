// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/types.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_print.h>
#include <drm/drm_edid.h>
#include <drm/drm_plane.h>
#include <drm/drm_modes.h>
#include "msm_hyp_utils.h"
#include "sde_connector.h"
#if IS_ENABLED(CONFIG_DRM_MSM_HYP_VIRTIO)
#include "virtio/virtio_kms.h"
#endif
#if IS_ENABLED(CONFIG_DRM_MSM_HYP_WFD)
#include "wfd/wfd_kms.h"
#endif

/* CTA-861-H Table 60 - CTA Tag Codes */
#ifndef CTA_DB_AUDIO
#define CTA_DB_AUDIO	1
#endif
#ifndef CTA_DB_VIDEO
#define CTA_DB_VIDEO	2
#endif
#ifndef CTA_DB_VENDOR
#define CTA_DB_VENDOR	3
#endif
#ifndef CTA_DB_SPEAKER
#define CTA_DB_SPEAKER	4
#endif
#ifndef CTA_DB_EXTENDED_TAG
#define CTA_DB_EXTENDED_TAG	7
#endif

/* CTA-861-H Table 62 - CTA Extended Tag Codes */
#ifndef CTA_EXT_DB_VIDEO_CAP
#define CTA_EXT_DB_VIDEO_CAP	0
#endif
#ifndef CTA_EXT_DB_VENDOR
#define CTA_EXT_DB_VENDOR	1
#endif
#ifndef CTA_EXT_DB_HDR_STATIC_METADATA
#define CTA_EXT_DB_HDR_STATIC_METADATA	6
#endif
#ifndef CTA_EXT_DB_420_VIDEO_DATA
#define CTA_EXT_DB_420_VIDEO_DATA	14
#endif
#ifndef CTA_EXT_DB_420_VIDEO_CAP_MAP
#define CTA_EXT_DB_420_VIDEO_CAP_MAP	15
#endif
#ifndef CTA_EXT_DB_HF_EEODB
#define CTA_EXT_DB_HF_EEODB	0x78
#endif
#ifndef CTA_EXT_DB_HF_SCDB
#define CTA_EXT_DB_HF_SCDB	0x79
#endif

#ifndef VSVDB_HDR10_PLUS_IEEE_CODE
#define VSVDB_HDR10_PLUS_IEEE_CODE	0x90848b
#endif
#ifndef VSVDB_HDR10_PLUS_APP_VER_MASK
#define VSVDB_HDR10_PLUS_APP_VER_MASK	0x3
#endif

void msm_hyp_prop_info_append(
		struct msm_hyp_prop_blob_info *info,
		const char *str)
{
	uint32_t len;

	if (info) {
		len = scnprintf(info->data + info->len,
				MAX_BLOB_INFO_SIZE - info->len,
				"%s",
				str);

		if ((info->len + len) < MAX_BLOB_INFO_SIZE)
			info->len += len;
	}
}

void msm_hyp_prop_info_add_keystr(
		struct msm_hyp_prop_blob_info *info,
		const char *key, const char *value)
{
	uint32_t len;

	if (info && key && value) {
		len = scnprintf(info->data + info->len,
				MAX_BLOB_INFO_SIZE - info->len,
				"%s=%s\n",
				key,
				value);

		if ((info->len + len) < MAX_BLOB_INFO_SIZE)
			info->len += len;
	} else {
		DRM_ERROR("info, key, or value is null\n");
	}
}

void msm_hyp_prop_info_populate_plane_format(
		struct drm_plane *plane,
		struct msm_hyp_prop_blob_info *info)
{
	uint32_t i, pixel_format;
	uint64_t modifier;

	info->len = scnprintf(info->data, sizeof(info->data), "pixel_formats=");

	for (i = 0; i < plane->format_count; i++) {
		pixel_format = plane->format_types[i];

		info->len += scnprintf(info->data + info->len,
				MAX_BLOB_INFO_SIZE - info->len,
				"%c%c%c%c ",
				(pixel_format >> 0) & 0xFF,
				(pixel_format >> 8) & 0xFF,
				(pixel_format >> 16) & 0xFF,
				(pixel_format >> 24) & 0xFF);

		switch (pixel_format) {
		case DRM_FORMAT_BGR565:
		case DRM_FORMAT_ABGR8888:
		case DRM_FORMAT_XBGR8888:
		case DRM_FORMAT_ABGR2101010:
		case DRM_FORMAT_XBGR2101010:
			modifier = DRM_FORMAT_MOD_QTI_COMPRESSED;
			info->len += scnprintf(info->data + info->len,
					MAX_BLOB_INFO_SIZE - info->len,
					"%c%c%c%c/%llX/%llX ",
					(pixel_format >> 0) & 0xFF,
					(pixel_format >> 8) & 0xFF,
					(pixel_format >> 16) & 0xFF,
					(pixel_format >> 24) & 0xFF,
					(modifier >> 56) & 0xFF,
					modifier & ((1ULL << 56) - 1));
			break;
		case DRM_FORMAT_NV12:
			/* NV12 UBWC */
			modifier = DRM_FORMAT_MOD_QTI_COMPRESSED;
			info->len += scnprintf(info->data + info->len,
					MAX_BLOB_INFO_SIZE - info->len,
					"%c%c%c%c/%llX/%llX ",
					(pixel_format >> 0) & 0xFF,
					(pixel_format >> 8) & 0xFF,
					(pixel_format >> 16) & 0xFF,
					(pixel_format >> 24) & 0xFF,
					(modifier >> 56) & 0xFF,
					modifier & ((1ULL << 56) - 1));
			/* P010 */
			modifier = DRM_FORMAT_MOD_QTI_DX;
			info->len += scnprintf(info->data + info->len,
					MAX_BLOB_INFO_SIZE - info->len,
					"%c%c%c%c/%llX/%llX ",
					(pixel_format >> 0) & 0xFF,
					(pixel_format >> 8) & 0xFF,
					(pixel_format >> 16) & 0xFF,
					(pixel_format >> 24) & 0xFF,
					(modifier >> 56) & 0xFF,
					modifier & ((1ULL << 56) - 1));
			/* P010 UBWC */
			modifier = (DRM_FORMAT_MOD_QTI_COMPRESSED |
					DRM_FORMAT_MOD_QTI_DX);
			info->len += scnprintf(info->data + info->len,
					MAX_BLOB_INFO_SIZE - info->len,
					"%c%c%c%c/%llX/%llX ",
					(pixel_format >> 0) & 0xFF,
					(pixel_format >> 8) & 0xFF,
					(pixel_format >> 16) & 0xFF,
					(pixel_format >> 24) & 0xFF,
					(modifier >> 56) & 0xFF,
					modifier & ((1ULL << 56) - 1));
			/* TP10 UBWC */
			modifier = (DRM_FORMAT_MOD_QTI_COMPRESSED |
					DRM_FORMAT_MOD_QTI_DX |
					DRM_FORMAT_MOD_QTI_TIGHT);
			info->len += scnprintf(info->data + info->len,
					MAX_BLOB_INFO_SIZE - info->len,
					"%c%c%c%c/%llX/%llX ",
					(pixel_format >> 0) & 0xFF,
					(pixel_format >> 8) & 0xFF,
					(pixel_format >> 16) & 0xFF,
					(pixel_format >> 24) & 0xFF,
					(modifier >> 56) & 0xFF,
					modifier & ((1ULL << 56) - 1));
			break;
		default:
			break;
		}
	}

	info->len += scnprintf(info->data + info->len,
			MAX_BLOB_INFO_SIZE - info->len, "\n");
}

static void _msm_hyp_update_dtd(struct edid *edid,
		struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	uint32_t dtd_count = 0;

	list_for_each_entry(mode, &connector->probed_modes, head) {
		struct detailed_timing *dtd =
			&edid->detailed_timings[dtd_count];
		struct detailed_pixel_timing *pd = &dtd->data.pixel_data;
		uint32_t h_blank = 0;
		uint32_t v_blank = 0;
		uint32_t h_img = 0, v_img = 0;

		/* mode with width format greater then 4095 is not supported */
		if (mode->vdisplay > 4095) {
			DRM_ERROR("Mode not supported (%u X %u)\n",
					mode->vdisplay,
					mode->hdisplay);
			continue;
		}

		dtd->pixel_clock = mode->clock;

		pd->hactive_lo = mode->hdisplay & 0xFF;
		pd->hblank_lo = h_blank & 0xFF;
		pd->hactive_hblank_hi = ((h_blank >> 8) & 0xF) |
				((mode->hdisplay >> 8) & 0xF) << 4;

		pd->vactive_lo = mode->vdisplay & 0xFF;
		pd->vblank_lo = v_blank & 0xFF;
		pd->vactive_vblank_hi = ((v_blank >> 8) & 0xF) |
				((mode->vdisplay >> 8) & 0xF) << 4;

		pd->hsync_offset_lo = (mode->hsync_start -
				mode->hdisplay) & 0xFF;
		pd->hsync_pulse_width_lo = (mode->hsync_start +
				mode->hsync_end) & 0xFF;

		pd->vsync_offset_pulse_width_lo =
			(((mode->vsync_start -
				mode->vdisplay) & 0xF) << 4) |
			((mode->vsync_start +
				mode->vsync_end) & 0xF);

		pd->hsync_vsync_offset_pulse_width_hi =
			((((mode->vsync_start -
				mode->vdisplay) >> 8) & 0x3)
				<< 6) |
			((((mode->hsync_end -
				mode->hsync_start) >> 8) & 0x3)
				<< 4) |
			((((mode->hsync_start -
				mode->hdisplay) >> 4) & 0x3)
				<< 2) |
			((((mode->vsync_end -
				mode->vsync_start) >> 4) & 0x3)
				<< 0);

		pd->width_mm_lo = h_img & 0xFF;
		pd->height_mm_lo = v_img & 0xFF;
		pd->width_height_mm_hi = (((h_img >> 8) & 0xF) << 4) |
			((v_img >> 8) & 0xF);

		pd->hborder = 0;
		pd->vborder = 0;
		pd->misc = 0;

		if (dtd_count < 2)
			dtd_count++;
		else
			break;
	}
}

static void _msm_hyp_update_checksum(struct edid *edid)
{
	uint8_t *data = (uint8_t *)edid;
	uint32_t i, sum = 0;

	for (i = 0; i < EDID_LENGTH - 1; i++)
		sum += data[i];

	edid->checksum = 0x100 - (sum & 0xFF);
}

static void _msm_hyp_update_edid_name(struct edid *edid, const char *name)
{
	uint8_t *dtd = (uint8_t *)&edid->detailed_timings[3];
	uint8_t standard_header[] = {0x00, 0x00, 0x00, 0xFC, 0x00};
	uint32_t dtd_size = 18;
	uint32_t header_size = sizeof(standard_header);
	char edid_name[PANEL_NAME_LEN] = {'\0'};

	if (!name)
		return;

	snprintf(edid_name, PANEL_NAME_LEN, "%s\n", name);

	/* Fill standard header */
	memcpy(dtd, standard_header, header_size);

	dtd_size -= header_size;
	dtd_size = min_t(u32, dtd_size, strlen(edid_name));

	memcpy(dtd + header_size, edid_name, dtd_size);
}

static int _msm_hyp_update_cea_extension(struct edid *edid, struct drm_connector *connector)
{
	struct sde_connector *sde_conn = to_sde_connector(connector);
	uint8_t *p = (uint8_t *)edid + EDID_LENGTH;
	int cea_ext_size = 0;

	// CEA extension version 3
	*p++ = CEA_EXT;
	*p++ = 0x03;
	// Byte number, not native DTD
	*p++ = 0;
	// 0 native DTD, no underscan, audio, 444 and 422 support
	*p++ = 0;

	if (sde_conn->hdr_supported) {
		// Append VSDB for HDR10+ version
		if (sde_conn->hdr_plus_app_ver) {
			// VSVDB tag, length 5
			*p++ = CTA_DB_EXTENDED_TAG << 5 | 5;
			*p++ = CTA_EXT_DB_VENDOR;
			// IEEE code
			*p++ = (VSVDB_HDR10_PLUS_IEEE_CODE >> 16) & 0xFF;
			*p++ = (VSVDB_HDR10_PLUS_IEEE_CODE >> 8) & 0xFF;
			*p++ = VSVDB_HDR10_PLUS_IEEE_CODE & 0xFF;
			// HDR10+ version
			*p++ = sde_conn->hdr_plus_app_ver & VSVDB_HDR10_PLUS_APP_VER_MASK;
		}

		// Append HDR static metadata data block
		if (sde_conn->hdr_metadata_type_one) {
			// HDR static metadata tag, length 6
			*p++ = CTA_DB_EXTENDED_TAG << 5 | 6;
			*p++ = CTA_EXT_DB_HDR_STATIC_METADATA;

			// EOTF supported, default is SDR
			*p++ = sde_conn->hdr_eotf;

			// Static metadata type 1
			*p++ = BIT(0);

			// Luminance
			// (50.0f * powf(2.0f, (hdr_max_luminance / 32.0f)
			*p++ = sde_conn->hdr_max_luminance;
			// (50.0f * powf(2.0f, (hdr_avg_luminance / 32.0f)
			*p++ = sde_conn->hdr_avg_luminance;
			// (max_luminance * ((hdr_min_luminance / 255.0f)^2 / 100.0f)
			*p++ = sde_conn->hdr_min_luminance;
		}

		// Append Colorimetry block
		if (1) {
			// Colorimetry data tag, length 3
			*p++ = CTA_DB_EXTENDED_TAG << 5 | 3;
			*p++ = 5;
			// Support DCI-P3
			if (sde_conn->color_enc_fmt & DRM_EDID_CLRMETRY_DCI_P3) {
				*p++ = 0;
				*p++ = BIT(7);
			}
		}
	}

	cea_ext_size = p - ((uint8_t *)edid + EDID_LENGTH);

	// Ignore empty CEA extension
	return cea_ext_size <= 4 ? 0 : cea_ext_size;
}

int msm_hyp_connector_init_edid(struct drm_connector *connector,
		const char *name)
{
	uint32_t cae_ext_size;
	struct edid *edid;
	int ret;

	const uint8_t edid_buf[EDID_LENGTH] = {
		0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x44, 0x6D,
		0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1B, 0x10, 0x01, 0x03,
		0x80, 0x50, 0x2D, 0x78, 0x0A, 0x0D, 0xC9, 0xA0, 0x57, 0x47,
		0x98, 0x27, 0x12, 0x48, 0x4C, 0x00, 0x00, 0x00, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01,
	};

	// Preserve CEA extension size
	edid = kcalloc(2, EDID_LENGTH, GFP_KERNEL);

	memcpy(edid, edid_buf, EDID_LENGTH);

	_msm_hyp_update_edid_name(edid, name);
	_msm_hyp_update_dtd(edid, connector);

	cae_ext_size = _msm_hyp_update_cea_extension(edid, connector);
	_msm_hyp_update_checksum(edid + 1);
	edid->extensions += (cae_ext_size + EDID_LENGTH - 1) / EDID_LENGTH;

	_msm_hyp_update_checksum(edid);

	ret = drm_connector_update_edid_property(connector, edid);

	kfree(edid);

	return ret;
}
