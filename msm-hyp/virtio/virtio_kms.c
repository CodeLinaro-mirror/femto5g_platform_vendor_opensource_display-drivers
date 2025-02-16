// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/sort.h>
#include <drm/drm_atomic.h>
#include <linux/virtio_config.h>
#include <soc/qcom/boot_stats.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_helper.h>

#include "msm_hyp_trace.h"
#include "msm_hyp_utils.h"
#include "virtio_kms.h"
#include "virtio_ext.h"
#include "virtgpu_vq.h"
#include <linux/habmm.h>
#define CLIENT_ID_LEN_IN_CHARS 5

#define DISPLAY_DEVICE_MAX_WIDTH      10240
#define DISPLAY_DEVICE_MAX_HEIGHT     4096

#define MAX_HORZ_DECIMATION    4
#define MAX_VERT_DECIMATION    4
#define SSPP_UNITY_SCALE       1
#define MAX_NUM_LIMIT_PAIRS    16
#define DBG_BUF_COUNT          50
#define DEFAULT_MAX_MDP_CLK    575
#define MAX_LAYERS_MULTIPIPE   4
#define MAX_PRE_ROT_HEIGHT_INLINE_ROT_DEFAULT   1088

#define VIRTIO_TRANSPARENCY_GLOBAL_ALPHA (1<<1)
#define VIRTIO_TRANSPARENCY_SOURCE_ALPHA (1<<2)
//#define VIRTIO_DEBUG 1

#define DUMP_FRAME_CONTENT(start, end, ptr)					\
	for (int idx = (start); idx < (end); idx++) {				\
		DRM_DEBUG_KMS("virtio: framebuffer data %x\n", ptr[idx]);	\
	}

#ifndef UINT_MAX
#define UINT_MAX 0xffffffffU  /* define this if limits.h not available */
#endif

#define POPULATE_RECT(rect, a, b, c, d, Q16_flag) \
	do {                                            \
		(rect)->x = (Q16_flag) ? (a) >> 16 : (a);    \
		(rect)->y = (Q16_flag) ? (b) >> 16 : (b);    \
		(rect)->w = (Q16_flag) ? (c) >> 16 : (c);    \
		(rect)->h = (Q16_flag) ? (d) >> 16 : (d);    \
	} while (0)

struct virtio_kms_rect {
	u16 x;
	u16 y;
	u16 w;
	u16 h;
};

struct limit_val_pair {
	const char *str;
	uint32_t val;
};

struct limit_constraints {
	uint32_t sdma_width;
	struct limit_val_pair pairs[MAX_NUM_LIMIT_PAIRS];
};

static struct limit_constraints constraints_table[] = {
	{
		/* SA6155 */
		1080,
		{
			{"sspp_linewidth_usecases", 3},
			{"vig",   0x1},
			{"dma",   0x2},
			{"scale", 0x4},
			{"sspp_linewidth_values", 3},
			{"limit_usecase", 0x1},
			{"limit_value",  2160},
			{"limit_usecase", 0x5},
			{"limit_value",  2160},
			{"limit_usecase", 0x2},
			{"limit_value",  2160},
		}
	},
	{
		/* SA8155/SA8195 */
		2048,
		{
			{"sspp_linewidth_usecases", 3},
			{"vig",   0x1},
			{"dma",   0x2},
			{"scale", 0x4},
			{"sspp_linewidth_values", 3},
			{"limit_usecase", 0x1},
			{"limit_value",  2560},
			{"limit_usecase", 0x5},
			{"limit_value",  2560},
			{"limit_usecase", 0x2},
			{"limit_value",  4096},
		}
	},
	{
		/* SA8295 */
		2560,
		{
			{"sspp_linewidth_usecases", 3},
			{"vig",   0x1},
			{"dma",   0x2},
			{"scale", 0x4},
			{"sspp_linewidth_values", 3},
			{"limit_usecase", 0x1},
			{"limit_value",  2560},
			{"limit_usecase", 0x5},
			{"limit_value",  2560},
			{"limit_usecase", 0x2},
			{"limit_value",  5120},
		}
	},
};

static const char * const disp_order_str[] = {
	"primary",
	"secondary",
	"tertiary",
	"quaternary",
	"quinary",
	"senary",
	"septenary",
	"octonary",
};

enum color_space {
	VIRTIO_COLOR_SPACE_UNCORRECTED = 0x0,
	VIRTIO_COLOR_SPACE_SRGB        = 0x1,
	VIRTIO_COLOR_SPACE_LRGB        = 0x2,
	VIRTIO_COLOR_SPACE_BT601       = 0x3,
	VIRTIO_COLOR_SPACE_BT601_FULL  = 0x4,
	VIRTIO_COLOR_SPACE_BT709       = 0x5,
	VIRTIO_COLOR_SPACE_BT709_FULL  = 0x6,
};

enum virtio_layer_type {
	VIRTIO_QDI_LAYER_NONE		= 0,
	VIRTIO_QDI_LAYER_GRAPHICS,
	VIRTIO_QDI_LAYER_OVERLAY,
	VIRTIO_QDI_LAYER_DMA,
	VIRTIO_QDI_LAYER_CURSOR,
	VIRTIO_QDI_LAYER_MAX,
	VIRTIO_QDI_LAYER_FORCE_32BIT	= 0x7FFFFFFF
};

static int virtio_kms_create_framebuffer(struct virtio_kms *kms,
		struct msm_hyp_framebuffer *fb);

static const char* virtio_get_drm_format_string(uint32_t drm_format) {
	switch (drm_format) {
		case DRM_FORMAT_ABGR1555:
			return "DRM_FORMAT_ABGR1555";
		case DRM_FORMAT_ABGR2101010:
			return "DRM_FORMAT_ABGR2101010";
		case DRM_FORMAT_ABGR4444:
			return "DRM_FORMAT_ABGR4444";
		case DRM_FORMAT_ABGR8888:
			return "DRM_FORMAT_ABGR8888";
		case DRM_FORMAT_ARGB1555:
			return "DRM_FORMAT_ARGB1555";
		case DRM_FORMAT_ARGB2101010:
			return "DRM_FORMAT_ARGB2101010";
		case DRM_FORMAT_ARGB4444:
			return "DRM_FORMAT_ARGB4444";
		case DRM_FORMAT_ARGB8888:
			return "DRM_FORMAT_ARGB8888";
		case DRM_FORMAT_AYUV:
			return "DRM_FORMAT_AYUV";
		case DRM_FORMAT_BGR233:
			return "DRM_FORMAT_BGR233";
		case DRM_FORMAT_BGR565:
			return "DRM_FORMAT_BGR565";
		case DRM_FORMAT_BGR888:
			return "DRM_FORMAT_BGR888";
		case DRM_FORMAT_BGRA1010102:
			return "DRM_FORMAT_BGRA1010102";
		case DRM_FORMAT_BGRA4444:
			return "DRM_FORMAT_BGRA4444";
		case DRM_FORMAT_BGRA5551:
			return "DRM_FORMAT_BGRA5551";
		case DRM_FORMAT_BGRA8888:
			return "DRM_FORMAT_BGRA8888";
		case DRM_FORMAT_BGRX1010102:
			return "DRM_FORMAT_BGRX1010102";
		case DRM_FORMAT_BGRX4444:
			return "DRM_FORMAT_BGRX4444";
		case DRM_FORMAT_BGRX5551:
			return "DRM_FORMAT_BGRX5551";
		case DRM_FORMAT_BGRX8888:
			return "DRM_FORMAT_BGRX8888";
		case DRM_FORMAT_C8:
			return "DRM_FORMAT_C8";
		case DRM_FORMAT_GR88:
			return "DRM_FORMAT_GR88";
		case DRM_FORMAT_NV12:
			return "DRM_FORMAT_NV12";
		case DRM_FORMAT_NV21:
			return "DRM_FORMAT_NV21";
		case DRM_FORMAT_R8:
			return "DRM_FORMAT_R8";
		case DRM_FORMAT_RG88:
			return "DRM_FORMAT_RG88";
		case DRM_FORMAT_RGB332:
			return "DRM_FORMAT_RGB332";
		case DRM_FORMAT_RGB565:
			return "DRM_FORMAT_RGB565";
		case DRM_FORMAT_RGB888:
			return "DRM_FORMAT_RGB888";
		case DRM_FORMAT_RGBA1010102:
			return "DRM_FORMAT_RGBA1010102";
		case DRM_FORMAT_RGBA4444:
			return "DRM_FORMAT_RGBA4444";
		case DRM_FORMAT_RGBA5551:
			return "DRM_FORMAT_RGBA5551";
		case DRM_FORMAT_RGBA8888:
			return "DRM_FORMAT_RGBA8888";
		case DRM_FORMAT_RGBX1010102:
			return "DRM_FORMAT_RGBX1010102";
		case DRM_FORMAT_RGBX4444:
			return "DRM_FORMAT_RGBX4444";
		case DRM_FORMAT_RGBX5551:
			return "DRM_FORMAT_RGBX5551";
		case DRM_FORMAT_RGBX8888:
			return "DRM_FORMAT_RGBX8888";
		case DRM_FORMAT_UYVY:
			return "DRM_FORMAT_UYVY";
		case DRM_FORMAT_VYUY:
			return "DRM_FORMAT_VYUY";
		case DRM_FORMAT_XBGR1555:
			return "DRM_FORMAT_XBGR1555";
		case DRM_FORMAT_XBGR2101010:
			return "DRM_FORMAT_XBGR2101010";
		case DRM_FORMAT_XBGR4444:
			return "DRM_FORMAT_XBGR4444";
		case DRM_FORMAT_XBGR8888:
			return "DRM_FORMAT_XBGR8888";
		case DRM_FORMAT_XRGB1555:
			return "DRM_FORMAT_XRGB1555";
		case DRM_FORMAT_XRGB2101010:
			return "DRM_FORMAT_XRGB2101010";
		case DRM_FORMAT_XRGB4444:
			return "DRM_FORMAT_XRGB4444";
		case DRM_FORMAT_XRGB8888:
			return "DRM_FORMAT_XRGB8888";
		case DRM_FORMAT_YUYV:
			return "DRM_FORMAT_YUYV";
		case DRM_FORMAT_YVU420:
			return "DRM_FORMAT_YVU420";
		case DRM_FORMAT_YVYU:
			return "DRM_FORMAT_YVYU";
	}
	return "Unknown";
}
static const char* virtio_get_virtio_format_string(uint32_t virtio_format)
{
	switch(virtio_format){
		case VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM:
			return"B8G8R8A8_UNORM";
		case VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM:
			return"B8G8R8X8_UNORM";
		case VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM:
			return"A8R8G8B8_UNORM";
		case VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM:
			return"X8R8G8B8_UNORM";
		case VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM:
			return"R8G8B8A8_UNORM";
		case VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM:
			return"X8B8G8R8_UNORM";
		case VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM:
			return"A8B8G8R8_UNORM";
		case VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM:
			return"R8G8B8X8_UNORM";
		case VIRTIO_GPU_FORMAT_BYTE:
			return"VIRTIO_GPU_FORMAT_BYTE";
		case VIRTIO_GPU_FORMAT_B4G4R4A4:
			return"VIRTIO_GPU_FORMAT_B4G4R4A4";
		case VIRTIO_GPU_FORMAT_B4G4R4X4:
			return"VIRTIO_GPU_FORMAT_B4G4R4X4";
		case VIRTIO_GPU_FORMAT_B5G5R5A1:
			return"VIRTIO_GPU_FORMAT_B5G5R5A1";
		case VIRTIO_GPU_FORMAT_B5G5R5X1:
			return"VIRTIO_GPU_FORMAT_B5G5R5X1";
		case VIRTIO_GPU_FORMAT_B10G10R10A2:
			return"VIRTIO_GPU_FORMAT_B10G10R10A2";
		case VIRTIO_GPU_FORMAT_B10G10R10X2:
			return"VIRTIO_GPU_FORMAT_B10G10R10X2";
		case VIRTIO_GPU_FORMAT_R10G10B10X2:
			return"VIRTIO_GPU_FORMAT_R10G10B10X2";
		case VIRTIO_GPU_FORMAT_B5G6R5:
			return"VIRTIO_GPU_FORMAT_B5G6R5";
		case VIRTIO_GPU_FORMAT_R5G6B5:
			return"VIRTIO_GPU_FORMAT_R5G6B5";
		case VIRTIO_GPU_FORMAT_B8G8R8:
			return"VIRTIO_GPU_FORMAT_B8G8R8";
		case VIRTIO_GPU_FORMAT_YVU410:
			return"VIRTIO_GPU_FORMAT_YVU410";
		case VIRTIO_GPU_FORMAT_YUV420:
			return"VIRTIO_GPU_FORMAT_YUV420";
		case VIRTIO_GPU_FORMAT_NV12:
			return"VIRTIO_GPU_FORMAT_NV12";
		case VIRTIO_GPU_FORMAT_P010:
			return"VIRTIO_GPU_FORMAT_P010";
		case VIRTIO_GPU_FORMAT_NV12_QC_TP10:
			return"VIRTIO_GPU_FORMAT_NV12_QC_TP10";
		case VIRTIO_GPU_FORMAT_YVU420:
			return"VIRTIO_GPU_FORMAT_YVU420";
		case VIRTIO_GPU_FORMAT_UYVY:
			return"VIRTIO_GPU_FORMAT_UYVY";
		case VIRTIO_GPU_FORMAT_YVYU:
			return"VIRTIO_GPU_FORMAT_YVYU";
		case VIRTIO_GPU_FORMAT_YUYV:
			return"VIRTIO_GPU_FORMAT_YUYV";
		case VIRTIO_GPU_FORMAT_VYUY:
			return"VIRTIO_GPU_FORMAT_VYUY";
		case VIRTIO_GPU_FORMAT_AYUV:
			return "VIRTIO_GPU_FORMAT_AYUV";
		default:
			return "UNKNOWN";
	}
}

static const struct {
         uint32_t drm_fmt;
         uint32_t virtio_fmt;
 } drm_virtio_formats[] = {
	{DRM_FORMAT_C8,       VIRTIO_GPU_FORMAT_BYTE},
	{DRM_FORMAT_XRGB8888, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM},
	{DRM_FORMAT_ARGB8888, VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM},
	{DRM_FORMAT_BGRX8888, VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM},
	{DRM_FORMAT_BGRA8888, VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM},
	{DRM_FORMAT_RGBX8888, VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM},
	{DRM_FORMAT_RGBA8888, VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM},
	{DRM_FORMAT_XBGR8888, VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM},
        {DRM_FORMAT_ABGR8888, VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM},
	{DRM_FORMAT_ARGB4444, VIRTIO_GPU_FORMAT_B4G4R4A4},
	{DRM_FORMAT_XRGB4444, VIRTIO_GPU_FORMAT_B4G4R4X4},
	{DRM_FORMAT_ARGB1555, VIRTIO_GPU_FORMAT_B5G5R5A1},
	{DRM_FORMAT_XRGB1555, VIRTIO_GPU_FORMAT_B5G5R5X1},
	{DRM_FORMAT_ARGB2101010, VIRTIO_GPU_FORMAT_B10G10R10A2},
	{DRM_FORMAT_XRGB2101010, VIRTIO_GPU_FORMAT_B10G10R10X2},
	{DRM_FORMAT_XBGR2101010, VIRTIO_GPU_FORMAT_R10G10B10X2},
	{DRM_FORMAT_ABGR2101010, VIRTIO_GPU_FORMAT_R10G10B10A2},
	{DRM_FORMAT_RGB565,   VIRTIO_GPU_FORMAT_B5G6R5},
	{DRM_FORMAT_RGB888,   VIRTIO_GPU_FORMAT_B8G8R8},
	{DRM_FORMAT_BGR565,   VIRTIO_GPU_FORMAT_R5G6B5},
	{DRM_FORMAT_BGR888,   VIRTIO_GPU_FORMAT_R8G8B8},
	{DRM_FORMAT_YVU410,   VIRTIO_GPU_FORMAT_YVU410},
	{DRM_FORMAT_YUV420,   VIRTIO_GPU_FORMAT_YUV420},
	{DRM_FORMAT_NV12,     VIRTIO_GPU_FORMAT_NV12},
	{DRM_FORMAT_NV12,     VIRTIO_GPU_FORMAT_P010},
	{DRM_FORMAT_NV12,     VIRTIO_GPU_FORMAT_NV12_QC_TP10},
	{DRM_FORMAT_YVU420,   VIRTIO_GPU_FORMAT_YVU420},
	{DRM_FORMAT_UYVY,     VIRTIO_GPU_FORMAT_UYVY},
	{DRM_FORMAT_YUYV,     VIRTIO_GPU_FORMAT_YUYV},
	{DRM_FORMAT_YVYU,     VIRTIO_GPU_FORMAT_YVYU},
	{DRM_FORMAT_VYUY,     VIRTIO_GPU_FORMAT_VYUY},
	{DRM_FORMAT_AYUV,     VIRTIO_GPU_FORMAT_AYUV},
	{0,0}
 };

uint32_t get_drm_format(uint32_t virtio_format)
{
	uint32_t format = 0;
	int i = 0;
	while (drm_virtio_formats[i].virtio_fmt || drm_virtio_formats[i].drm_fmt) {
		if (virtio_format == drm_virtio_formats[i].virtio_fmt) {
			format = drm_virtio_formats[i].drm_fmt;
			break;
		}
		i++;
	}
	pr_debug("virtio : virtio format %s to drm format %s\n",
			virtio_get_virtio_format_string(virtio_format),
			virtio_get_drm_format_string(format));

	WARN_ON(format == 0);
	return format;
}

uint32_t virtio_gpu_translate_format(uint32_t drm_fourcc, uint64_t modifier)
{
	uint32_t format = 0;
	int i = 0;

	while (drm_virtio_formats[i].virtio_fmt || drm_virtio_formats[i].drm_fmt) {
		if (drm_fourcc == drm_virtio_formats[i].drm_fmt) {
			format = drm_virtio_formats[i].virtio_fmt;
			break;
		}
                i++;
        }

	if (drm_fourcc == DRM_FORMAT_NV12) {
		if ((modifier & fourcc_mod_code(QTI, 0x7)) ==
				fourcc_mod_code(QTI, 0x7))
			format = VIRTIO_GPU_FORMAT_NV12_QC_TP10;
		else if ((modifier & fourcc_mod_code(QTI, 0x2)) ==
				fourcc_mod_code(QTI, 0x2))
			format = VIRTIO_GPU_FORMAT_P010;
		else
			format = VIRTIO_GPU_FORMAT_NV12;
	}
	pr_debug("virtio : drm format %s to virtio format %s\n",
			virtio_get_drm_format_string(drm_fourcc),
			virtio_get_virtio_format_string(format));
	WARN_ON(format == 0);

	return format;
}

static int virtio_kms_connector_detect_ctx(struct drm_connector *connector,
			  struct drm_modeset_acquire_ctx *ctx,
			  bool force)
{
	struct msm_hyp_connector *c = to_msm_hyp_connector(connector);
	struct virtio_connector_info_priv *priv = container_of(c->info,
			struct virtio_connector_info_priv, base);
	return priv->connector_status;
}

static struct drm_encoder *virtio_kms_connector_best_encoder(
		struct drm_connector *connector)
{
	struct msm_hyp_connector *c_conn = to_msm_hyp_connector(connector);
	return &c_conn->encoder;
}

static int virtio_kms_connector_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *m;
	struct msm_hyp_connector *c_conn;
	struct virtio_connector_info_priv *priv;
	uint32_t i;

	pr_debug("virtio_kms_connector_get_modes called\n");
	c_conn = to_msm_hyp_connector(connector);

	priv = container_of(c_conn->info,
                          struct virtio_connector_info_priv, base);
	for (i = 0; i < priv->mode_count; i++) {
		m = drm_mode_duplicate(connector->dev,
				&priv->modes[i]);
		if (!m)
			return i;
		drm_mode_probed_add(connector, m);
	}

	msm_hyp_connector_init_edid(connector, priv->panel_name);

	if (c_conn->info->display_info.width_mm > 0 &&
				c_conn->info->display_info.height_mm > 0) {
		connector->display_info.width_mm =
					c_conn->info->display_info.width_mm;
		connector->display_info.height_mm =
					c_conn->info->display_info.height_mm;
	}

	pr_debug("virtio_kms_connector_get_modes done %d\n",  priv->mode_count);
	return priv->mode_count;
}
static const struct drm_connector_helper_funcs virtio_conn_helper_funcs = {
	.detect_ctx = virtio_kms_connector_detect_ctx,
	.get_modes = virtio_kms_connector_get_modes,
	.best_encoder = virtio_kms_connector_best_encoder,
};

static void virtio_kms_bridge_mode_set(struct drm_bridge *drm_bridge,
		const struct drm_display_mode *mode,
		const struct drm_display_mode *adjusted_mode)
{
	struct msm_hyp_connector *connector;
	struct virtio_connector_info_priv *priv;
	struct virtio_gpu_rect dest_rect = {0,0,0,0};
	int i, mode_index = 0;
	uint32_t scanout;
	int rc = 0;

	connector = container_of(drm_bridge, struct msm_hyp_connector, bridge);
	priv = container_of(connector->info, struct virtio_connector_info_priv, base);
	scanout = priv->scanout;

	for (i = 0; i < priv->mode_count; i++) {
		mode = &priv->modes[i];
		if ((adjusted_mode->hdisplay == mode->hdisplay) &&
		    (adjusted_mode->vdisplay == mode->vdisplay)) {
//			mode_index = *mode->private;
			dest_rect.width = mode->hdisplay;
			dest_rect.height = mode->vdisplay;
			dest_rect.x = 0;
			dest_rect.y = 0;
			break;
		}
	}
/*	if (mode_index < 0) {
		pr_err("mode set failed %d for mode h-%d v-%d",
				priv->scanout,
				adjusted_mode->hdisplay,
				adjusted_mode->vdisplay);
		mode = NULL;
		return;
	}*/
	priv->mode_index = 0;//mode_index;
	priv->mode_rect.width = mode->hdisplay;
	priv->mode_rect.height = mode->vdisplay;
	priv->mode_rect.x = 0;
	priv->mode_rect.y = 0;

	rc = virtio_gpu_cmd_set_scanout_properties(priv->kms,
			scanout,
			0x7680,//FALSE,
			mode_index,
			0,
			dest_rect);
	if (rc) {
		pr_err("scanout set properties for mode failed %d\n",
				mode_index);
	}
}

static void virtio_kms_bridge_enable(struct drm_bridge *drm_bridge)
{
	struct msm_hyp_connector *connector;
	struct virtio_connector_info_priv *priv;
	struct virtio_gpu_rect dest_rect;
	uint32_t scanout;

	connector = container_of(drm_bridge, struct msm_hyp_connector, bridge);
	priv = container_of(connector->info,struct virtio_connector_info_priv, base);
	dest_rect.width = priv->mode_rect.width;
        dest_rect.height = priv->mode_rect.height;
        dest_rect.x = priv->mode_rect.x,
        dest_rect.y = priv->mode_rect.y;
	scanout = priv->scanout;
	virtio_gpu_cmd_set_scanout_properties(priv->kms,
			scanout,
			0x7683,//TRUE,
			priv->mode_index,
			0,
			dest_rect);
}

static void virtio_kms_bridge_disable(struct drm_bridge *drm_bridge)
{
	struct msm_hyp_connector *connector;
	struct virtio_connector_info_priv *priv;
	struct virtio_gpu_rect dest_rect;
	uint32_t scanout;

	connector = container_of(drm_bridge, struct msm_hyp_connector, bridge);
	priv = container_of(connector->info, struct virtio_connector_info_priv, base);
	dest_rect.width = priv->mode_rect.width;
        dest_rect.height = priv->mode_rect.height;
        dest_rect.x = priv->mode_rect.x,
        dest_rect.y = priv->mode_rect.y;

        scanout = priv->scanout;
	virtio_gpu_cmd_set_scanout_properties(priv->kms,
			scanout,
			0x7680,//FALSE,
			priv->mode_index,
			0,
			dest_rect);
}

static const struct drm_bridge_funcs virtio_bridge_ops = {
	.enable       = virtio_kms_bridge_enable,
	.disable      = virtio_kms_bridge_disable,
	.mode_set     = virtio_kms_bridge_mode_set,
};

static int virtio_kms_connector_get_type(
		uint32_t port_type,
		uint32_t scanout,
		char *name)
{
	int connector_type;

	switch (port_type) {
	case VIRTIO_PORT_TYPE_INTERNAL:
	case VIRTIO_PORT_TYPE_HDMI:
		connector_type = DRM_MODE_CONNECTOR_HDMIA;
		snprintf(name, PANEL_NAME_LEN, "%s_%d\n", "HDMI", scanout);
		break;
	case VIRTIO_PORT_TYPE_DSI:
		connector_type = DRM_MODE_CONNECTOR_DSI;
		snprintf(name, PANEL_NAME_LEN, "%s_%d\n", "DSI", scanout);
		break;
	case VIRTIO_PORT_TYPE_DP:
		connector_type = DRM_MODE_CONNECTOR_DisplayPort;
		snprintf(name, PANEL_NAME_LEN, "%s_%d\n", "DP", scanout);
		break;
	default:
		connector_type = DRM_MODE_CONNECTOR_Unknown;
		snprintf(name, PANEL_NAME_LEN, "%s_%d\n", "Unknown", scanout);
		break;
	}

	pr_debug("%s - port_type = %x name = %s\n", __func__, port_type, name);

	return connector_type;
}

static int virtio_kms_get_connector_infos(struct msm_hyp_kms *hyp_kms,
		struct msm_hyp_connector_info **connector_infos,
		int *connector_num)
{
	struct virtio_kms *kms = to_virtio_kms(hyp_kms);
	struct drm_device *ddev = kms->dev;
	int i;
	uint64_t j;
	struct virtio_connector_info_priv *priv;
	struct virtio_display_modes *info;
	struct drm_display_mode *mode;
	struct scanout_attrib *attr;

	if (!connector_infos) {
		*connector_num = kms->num_scanouts;
		return 0;
	}
	if (!ddev) {
		pr_err("ddev failed \n");
		return 0;
	}

	ddev->mode_config.min_width = 0;
	ddev->mode_config.max_width = DISPLAY_DEVICE_MAX_WIDTH;
	ddev->mode_config.min_height = 0;
	ddev->mode_config.max_height = DISPLAY_DEVICE_MAX_HEIGHT;

	for (i = 0; i < kms->num_scanouts; i++) {
		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
		if (!priv)
			return -ENOMEM;

		attr = &kms->outputs[i].attr;
		info = &kms->outputs[i].info[0];
		priv->connector_status = attr->connection_status ?
				connector_status_connected :
				connector_status_disconnected;
		priv->base.connector_type =
			virtio_kms_connector_get_type(attr->type,
					i,
					priv->panel_name);
		priv->base.display_info.width_mm = attr->width_mm;
		priv->base.display_info.height_mm = attr->height_mm;
		priv->base.panel_orientation = attr->panel_orientation;
		priv->scanout = i;
		priv->base.possible_crtcs = 1 << i;
		if (!kms->outputs[i].num_modes) {
			kfree(priv);
			pr_err("number of modes 0\n");
			return -EINVAL;
		}

		if (kms->outputs[i].num_modes > 0) {
			priv->modes = kcalloc(kms->outputs[i].num_modes,
					sizeof(struct drm_display_mode),
					GFP_KERNEL);
			if (!priv->modes) {
				pr_err("Mode allocation failed\n");
				kfree(priv);
				return -ENOMEM;
			}
		}

		for (j = 0; j < kms->outputs[i].num_modes; j++) {
			mode = &priv->modes[j];
			mode->hdisplay = info[j].r.width;
			mode->vdisplay = info[j].r.height;
//			mode->vrefresh = info[j].refresh;
//TODO find a way to pass mode index
//			mode->private = (int *)j;
			mode->hsync_end = mode->hdisplay;
			mode->htotal = mode->hdisplay;
			mode->hsync_start = mode->hdisplay;
			mode->vsync_end = mode->vdisplay;
			mode->vtotal = mode->vdisplay;
			mode->vsync_start = mode->vdisplay;
			mode->clock =
				info[j].refresh * mode->vtotal *
				mode->htotal / 1000LL;

			drm_mode_set_name(mode);
		}
		priv->mode_count = kms->outputs[i].num_modes;

		if (i < ARRAY_SIZE(disp_order_str))
			priv->base.display_type = disp_order_str[i];
		pr_debug("virtio : display(%d) order %s\n",
				i, priv->base.display_type);
		priv->base.connector_funcs = &virtio_conn_helper_funcs;
		priv->base.bridge_funcs = &virtio_bridge_ops;
		priv->kms = kms;
		connector_infos[i] = &priv->base;
	}
	return 0;
}

static bool virtio_kms_plane_is_rect_changed(struct drm_plane_state *pre,
	struct drm_plane_state *cur, bool src)
{
	bool ret = false;

	if (src) {
		if ((pre->src_x != cur->src_x) ||
			(pre->src_y != cur->src_y) ||
			(pre->src_w != cur->src_w) ||
			(pre->src_h != cur->src_h))
			ret = true;
	} else {
		if ((pre->crtc_x != cur->crtc_x) ||
			(pre->crtc_y != cur->crtc_y) ||
			(pre->crtc_w != cur->crtc_w) ||
			(pre->crtc_h != cur->crtc_h))
			ret = true;
	}

	return ret;
}

static int virtio_kms_plane_cmp(const void *a, const void *b)
{
	struct msm_hyp_plane_state *pa = *(struct msm_hyp_plane_state **)a;
	struct msm_hyp_plane_state *pb = *(struct msm_hyp_plane_state **)b;
	int rc = 0;

	if (pa->zpos != pb->zpos)
		rc = pa->zpos - pb->zpos;
	else
		rc = pa->base.crtc_x - pb->base.crtc_x;

	return rc;
}

static void virtio_kms_plane_zpos_adj_fe(struct drm_crtc *crtc,
		struct drm_atomic_state *old_state)
{
	struct drm_device *ddev = crtc->dev;
	int cnt = 0;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state;
	struct msm_hyp_plane *p;
	struct msm_hyp_plane_state *old_pstate, *new_pstate;
	struct drm_crtc_state *old_crtc_state;
	bool zpos_update = false;
	struct msm_hyp_plane_state *sorted_pstate[VIRTIO_GPU_MAX_PLANES];
	struct virtio_plane_info_priv *priv;
	int i, rc;
	struct msm_hyp_crtc *c = to_msm_hyp_crtc(crtc);
	struct virtio_crtc_info_priv *crtc_priv = container_of(c->info,
			struct virtio_crtc_info_priv,
			base);
	struct plane_properties prop;

	drm_for_each_plane_mask(plane, ddev, crtc->state->plane_mask) {
		new_pstate = to_msm_hyp_plane_state(plane->state);
		sorted_pstate[cnt++] = new_pstate;

		if (zpos_update)
			continue;

		old_plane_state = drm_atomic_get_old_plane_state(
				old_state, plane);
		if (old_plane_state) {
			old_pstate = to_msm_hyp_plane_state(old_plane_state);
			if (old_pstate->zpos != new_pstate->zpos)
				zpos_update = true;
		}
	}
	old_crtc_state = drm_atomic_get_old_crtc_state(old_state, crtc);

	if (cnt && (zpos_update || (old_crtc_state->plane_mask !=
			crtc->state->plane_mask))) {
		sort(sorted_pstate, cnt, sizeof(sorted_pstate[0]),
				virtio_kms_plane_cmp, NULL);
		for (i = 0; i < cnt; i++) {
			p = to_msm_hyp_plane(sorted_pstate[i]->base.plane);
			priv = container_of(p->info,
					struct virtio_plane_info_priv, base);

			prop.z_order = i + 1;
			prop.mask |= Z_ORDER;
			rc = virtio_gpu_cmd_set_plane_properties(priv->kms,
				crtc_priv->scanout,
				priv->plane_id,
				prop);
			if (rc) {
				pr_err("set plane properties failed \n");
			}
		}
	}
}

static bool virtio_kms_plane_is_csc_matrix_changed(
		struct msm_hyp_plane_state *pre,
		struct msm_hyp_plane_state *cur,
		uint32_t *color_space)
{
	bool ret = false;

	/*
	 * The ctm_coeff[4] value is unique for each CSC matrix. We can use
	 * this to identify the color space associated with each matrix. The
	 * index of each element corresponds to the associated VIRTIO color space
	 * enum value.
	 */
	static const int64_t msm_hyp_csc_unique_coeffs[] = {
		0x0,		/* VIRTIO_COLOR_SPACE_UNCORRECTED */
		0x0,		/* VIRTIO_COLOR_SPACE_SRGB */
		0x0,		/* VIRTIO_COLOR_SPACE_LRGB */
		0x7F9B800000,	/* VIRTIO_COLOR_SPACE_BT601 */
		0x7fa8000000,	/* VIRTIO_COLOR_SPACE_BT601_FULL */
		0x7fc9800000,	/* VIRTIO_COLOR_SPACE_BT709 */
		0x7fd0000000	/* VIRTIO_COLOR_SPACE_BT709_FULL */
	};

	/* ctm_coeff[4] is unique for each matrix */
	uint32_t unique_coeff_idx = 4;

	if (pre && cur) {
		/*
		 * Do not need to compare the entire matrix. It should be
		 * sufficient to only check the uniqe coefficient.
		 */
		if (pre->csc.ctm_coeff[unique_coeff_idx] !=
			cur->csc.ctm_coeff[unique_coeff_idx])
			ret = true;
		pr_debug("virtio : color space %llx %llx\n",
				pre->csc.ctm_coeff[unique_coeff_idx],
				cur->csc.ctm_coeff[unique_coeff_idx]);
	}

	if (color_space && ret) {
		if (msm_hyp_csc_unique_coeffs[VIRTIO_COLOR_SPACE_BT601] ==
				cur->csc.ctm_coeff[unique_coeff_idx])
			*color_space = VIRTIO_COLOR_SPACE_BT601;
		else if (msm_hyp_csc_unique_coeffs[VIRTIO_COLOR_SPACE_BT601_FULL] ==
				cur->csc.ctm_coeff[unique_coeff_idx])
			*color_space = VIRTIO_COLOR_SPACE_BT601_FULL;
		else if (msm_hyp_csc_unique_coeffs[VIRTIO_COLOR_SPACE_BT709] ==
				cur->csc.ctm_coeff[unique_coeff_idx])
			*color_space = VIRTIO_COLOR_SPACE_BT709;
		else if (msm_hyp_csc_unique_coeffs[VIRTIO_COLOR_SPACE_BT709_FULL] ==
				cur->csc.ctm_coeff[unique_coeff_idx])
			*color_space = VIRTIO_COLOR_SPACE_BT709_FULL;
		else
			*color_space = VIRTIO_COLOR_SPACE_BT601;
	}

	pr_debug("virtio : csc_matrix_changed %d\n", *color_space);
	return ret;
}

static void virtio_kms_plane_atomic_update(struct drm_plane *plane,
		struct drm_atomic_state *old_atomic_state)
{
	struct msm_hyp_plane *p;
	struct virtio_plane_info_priv *plane_priv;
	struct msm_hyp_framebuffer *fb;
	struct virtio_framebuffer_priv *fb_priv;
	struct drm_plane_state *old_state;
	struct msm_hyp_plane_state *old_pstate, *new_pstate;
	struct plane_properties prop;
	struct msm_hyp_crtc *crtc;
	struct virtio_crtc_info_priv *crtc_priv;
	int rc = 0;
	struct virtio_kms *kms;

	p = to_msm_hyp_plane(plane);
	plane_priv = container_of(p->info, struct virtio_plane_info_priv, base);
	kms = plane_priv ? plane_priv->kms : NULL;

	if (!kms) {
		return;
	}

        old_state = drm_atomic_get_old_plane_state(old_atomic_state, plane);
	new_pstate = to_msm_hyp_plane_state(plane->state);
	old_pstate = to_msm_hyp_plane_state(old_state);

	memset(&prop, 0x00, sizeof(struct plane_properties));

	if (!plane->state->crtc) {

		pr_debug("virtio_kms_plane_atomic_update crtc removed\n");
		crtc = to_msm_hyp_crtc(old_state->crtc);
		crtc_priv = container_of(crtc->info,
				struct virtio_crtc_info_priv,
				base);

		rc = virtio_gpu_cmd_set_plane(kms, crtc_priv->scanout,
				plane_priv->plane_id, 0);
		if (rc) {
			pr_err("set plane properties failed \n");
		}
	} else if (!plane->state->fb) {
		crtc = to_msm_hyp_crtc(plane->state->crtc);
		crtc_priv = container_of(crtc->info,
				struct virtio_crtc_info_priv,
				base);

		pr_debug("virtio_kms_plane_atomic_update fb removed plane id %d\n",plane_priv->plane_id);
		rc = virtio_gpu_cmd_set_plane(kms,
				crtc_priv->scanout,
				plane_priv->plane_id,
				0);
		if (rc) {
			pr_err("set plane properties failed %d\n", plane_priv->plane_id);
		}
	} else {
		fb = to_msm_hyp_fb(plane->state->fb);
		fb_priv = container_of(fb->info,
				struct virtio_framebuffer_priv,
				base);
		crtc = to_msm_hyp_crtc(plane->state->crtc);
		crtc_priv = container_of(crtc->info,
					struct virtio_crtc_info_priv,
					base);
		if (!fb_priv || !crtc_priv) {
			pr_err("virtio : Something failed in commit\n");
			return;
		}

		if ((old_state->crtc != plane->state->crtc) ||
			(old_state->fb != plane->state->fb)) {
			fb_priv->secure = new_pstate->fb_mode == SDE_DRM_FB_SEC ?
				true : false;
			rc = virtio_kms_create_framebuffer(kms,	fb);
			if (rc)
				pr_err("virtio : create frame buffer failed\n");

			rc = virtio_gpu_cmd_set_plane(kms,
					crtc_priv->scanout,
					plane_priv->plane_id,
					fb_priv->hw_res_handle);
			if (rc)
				pr_err("virtio : set plane failed \n");
		}
	}

	if (virtio_kms_plane_is_rect_changed(old_state, plane->state, true)) {

		pr_debug("virtio_kms_plane_atomic_update send src_rect %d %d %d %d\n",
				plane->state->src_x >> 16,
				plane->state->src_y >> 16,
				plane->state->src_w >> 16,
				plane->state->src_h >> 16);
		prop.src_rect.x = plane->state->src_x >> 16;
		prop.src_rect.y = plane->state->src_y >> 16;
		prop.src_rect.width = plane->state->src_w >> 16;
		prop.src_rect.height = plane->state->src_h >> 16;
		prop.mask |= SRC_RECT;
	}

	if (virtio_kms_plane_is_rect_changed(old_state, plane->state, false)) {

		pr_debug("virtio_kms_plane_atomic_update send dest_rect %d %d %d %d\n",
				plane->state->crtc_x,
				plane->state->crtc_y,
				plane->state->crtc_w,
				plane->state->crtc_h);
		prop.dst_rect.x = plane->state->crtc_x;
		prop.dst_rect.y = plane->state->crtc_y;
		prop.dst_rect.width = plane->state->crtc_w;
		prop.dst_rect.height = plane->state->crtc_h;
		prop.mask |= DST_RECT;
	}

	if (old_pstate->alpha != new_pstate->alpha || !plane_priv->committed) {
		prop.global_alpha = new_pstate->alpha;
		 prop.mask |= GLOBAL_ALPHA;
	}
	if (old_pstate->blend_op != new_pstate->blend_op ||
			!plane_priv->committed) {
		pr_debug("virtio :%s ALPHA %d\n",
				__func__,
				new_pstate->blend_op);
		prop.blend_mode = (new_pstate->blend_op ==
				SDE_DRM_BLEND_OP_OPAQUE) ?
			VIRTIO_TRANSPARENCY_GLOBAL_ALPHA :
			VIRTIO_TRANSPARENCY_SOURCE_ALPHA;
		prop.mask |= BLEND_MODE;
	}

	if (old_state->rotation != plane->state->rotation || !plane_priv->committed) {
		prop.rotation = plane->state->rotation;
		prop.mask |= ROTATION;
	}

	if (virtio_kms_plane_is_csc_matrix_changed(old_pstate, new_pstate, &prop.color_space)) {
		prop.mask |= COLOR_SPACE;
	}

	rc = virtio_gpu_cmd_set_plane_properties(kms,
			crtc_priv->scanout,
			plane_priv->plane_id,
			prop);
	if (rc) {
		pr_err("virtio : set plane properties failed \n");
	}

	plane_priv->committed = true;
}

static bool virtio_kms_plane_enabled(const struct drm_plane_state *state)
{
	return state && state->fb && state->crtc;
}

static bool is_ubwc_supported_format(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR16161616:
	case DRM_FORMAT_NV12:
		return true;
	default:
		break;
	}

	return false;
}

static bool is_inline_rot_supported_format(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR16161616:
	case DRM_FORMAT_NV12:
		return true;
	default:
		break;
	}

	return false;
}

static int _virtio_kms_plane_rot_atomic_check(struct drm_plane *plane,
		struct drm_atomic_state *atomic_state)
{
	struct drm_plane_state *state = NULL;
	struct drm_plane *slave_plane = NULL;
	struct msm_hyp_plane *slave_hyp_plane = NULL;
	u32 rotation = 0;
	int ret = 0;
	bool format_support = false;

	state = drm_atomic_get_new_plane_state(atomic_state, plane);
	if (!state) {
		pr_err("invalid plane state\n");
		return -EINVAL;
	}

	/* check inline rotation and simplify the transform */
	rotation = drm_rotation_simplify(
					state->rotation,
					DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_90 |
					DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y);

	if ((rotation & DRM_MODE_ROTATE_180) || (rotation & DRM_MODE_ROTATE_270)) {
		pr_err("invalid rotation transform must be simplified 0x%x\n",
			rotation);
		ret = -EINVAL;
		goto exit;
	}

	/* check for valid formats supported by inline rot */
	//TODO, get this ubwc supported format information from HGY
	if ((rotation & DRM_MODE_ROTATE_90) || (rotation & DRM_MODE_ROTATE_270)) {
		if (((state->fb->modifier & DRM_FORMAT_MOD_QTI_COMPRESSED)
			== DRM_FORMAT_MOD_QTI_COMPRESSED)
			&& (is_ubwc_supported_format(state->fb->format->format)))
			format_support =
				is_inline_rot_supported_format(state->fb->format->format);

		if (!format_support) {
			pr_err("invalid format for inline rot\n");
			ret = -EINVAL;
			goto exit;
		}
	}

	if (rotation & DRM_MODE_ROTATE_90) {
		struct virtio_kms_rect src;
		bool q16_data = true;
		/* check if the slave pipline is using */
		drm_for_each_plane(slave_plane, plane->dev) {
			slave_hyp_plane = to_msm_hyp_plane(slave_plane);

			if ((plane == slave_hyp_plane->primary_plane)
				&& virtio_kms_plane_enabled(slave_plane->state)) {
				pr_err("slave plane %d is using, master plane %d can not do 90 rotation\n",
					slave_plane->base.id, plane->base.id);
				goto exit;
			}
		}

		POPULATE_RECT(&src, state->src_x, state->src_y,
			state->src_w, state->src_h, q16_data);

		/* check for valid height */
		if (src.h > MAX_PRE_ROT_HEIGHT_INLINE_ROT_DEFAULT) {
			pr_err("invalid height for inline rot:%d max:%d\n",
				src.h, MAX_PRE_ROT_HEIGHT_INLINE_ROT_DEFAULT);
			ret = -EINVAL;
			goto exit;
		}
	}

	state->rotation = rotation;
exit:
	return ret;
}

static int virtio_kms_plane_atomic_check(struct drm_plane *plane,
	struct drm_atomic_state *atomic_state)
{
	struct drm_plane_state *state = NULL;
	int ret = 0;

	if (!plane || !atomic_state) {
		pr_err("invalid arg(s), plane %d atomic_state %d\n",
			!plane, !atomic_state);
		return -EINVAL;
	}

	state = drm_atomic_get_new_plane_state(atomic_state, plane);
	if (!virtio_kms_plane_enabled(state))
		goto exit;

	ret = _virtio_kms_plane_rot_atomic_check(plane, atomic_state);
	if (ret)
		goto exit;
exit:
	return ret;
}

static const struct drm_plane_helper_funcs virtio_plane_helper_funcs = {
	.atomic_update = virtio_kms_plane_atomic_update,
	.atomic_check = virtio_kms_plane_atomic_check,
};

static int virtio_kms_get_plane_infos(struct msm_hyp_kms *hyp_kms,
		struct msm_hyp_plane_info **plane_infos,
		int *plane_num)
{
	struct virtio_kms *kms = to_virtio_kms(hyp_kms);
	struct virtio_plane_info_priv *priv;
	int i, j, pipe_cnt = 0;
	int fmt_idx = 0;
	uint32_t *formats;
	uint32_t drm_format;
	uint32_t num_formats = 0;
	uint32_t plane_type;
	int32_t master_idx = -1;
	bool support_rotation = false;

	if (!kms || !plane_num)
		return -EINVAL;

	if (!plane_infos) {
		*plane_num = 0;
		for (i = 0; i < kms->num_scanouts; i++)
			*plane_num += kms->outputs[i].plane_cnt;
		return 0;
	}
	for (i = 0; i < kms->num_scanouts; i++) {

		for (j = 0; j < kms->outputs[i].plane_cnt; j++) {
			priv = kzalloc(sizeof(struct virtio_plane_info_priv), GFP_KERNEL);
			if (priv == NULL) {
				return -ENOMEM;
			}

			if (j == 0)
				plane_type = DRM_PLANE_TYPE_PRIMARY;
			else
				plane_type = DRM_PLANE_TYPE_OVERLAY;

//			plane_type = kms->outputs[i].plane_caps[j].plane_type;

			priv->plane_type = plane_type;
			priv->base.plane_type = plane_type;
			priv->scanout = i;
			support_rotation = kms->outputs[i].plane_caps[j].support_rotation;
			priv->base.support_rotation = support_rotation;
			num_formats = kms->outputs[i].plane_caps[j].num_formats;
			formats = kms->outputs[i].plane_caps[j].formats;

			if (!num_formats) {
				pr_err("virtio :formats for plane ID %d\
						for scan out %d failed\n",
						j, i);
				kfree(priv);
				return -EINVAL;
			}
			priv->base.format_types = kcalloc(num_formats, sizeof(uint32_t),
							GFP_KERNEL);
			if (priv->base.format_types == NULL) {
				pr_err("virtio : base.format_types Memory allocation failed\n");
				return -ENOMEM;
			}
			priv->base.format_count = 0;
			for (fmt_idx = 0; fmt_idx < num_formats; fmt_idx++) {
				drm_format = get_drm_format(formats[fmt_idx]);
				if(!drm_format)
					continue;
				priv->base.format_types[priv->base.format_count] =
					drm_format;
				priv->base.format_count++;
			}

			priv->base.support_scale = false;
			priv->base.support_csc = false;
			if (kms->outputs[i].plane_caps[j].plane_type == VIRTIO_QDI_LAYER_GRAPHICS
				|| kms->outputs[i].plane_caps[j].plane_type ==
				VIRTIO_QDI_LAYER_OVERLAY)
				priv->base.support_scale = true;

			if (kms->outputs[i].plane_caps[j].plane_type == VIRTIO_QDI_LAYER_OVERLAY)
				priv->base.support_csc = true;

			master_idx = kms->outputs[i].plane_caps[j].master_plane_id;
			if (master_idx >= 0) {
				pr_debug("virtio : Master plane %d master %d\n",
						kms->outputs[i].plane_caps[j].plane_id,
						master_idx + pipe_cnt);
				priv->base.support_multirect = true;
				priv->base.support_scale = false;
				priv->base.support_csc = false;
				priv->base.support_rotation = false;
				priv->base.master_plane_index = master_idx + pipe_cnt;
			}

			priv->base.possible_crtcs = 1 << i;
			if (priv->base.support_scale) {
				if (kms->outputs[i].plane_caps[j].max_scale > 0 &&
					kms->outputs[i].plane_caps[j].min_scale > 0) {
					priv->base.maxdwnscale =
						kms->outputs[i].plane_caps[j].min_scale;
					priv->base.maxupscale =
						kms->outputs[i].plane_caps[j].max_scale;
				} else {
					priv->base.maxdwnscale = SSPP_UNITY_SCALE;
					priv->base.maxupscale = SSPP_UNITY_SCALE;
				}
			} else {
				priv->base.maxdwnscale = SSPP_UNITY_SCALE;
				priv->base.maxupscale = SSPP_UNITY_SCALE;
			}

			priv->base.maxhdeciexp = MAX_HORZ_DECIMATION;
			priv->base.maxvdeciexp = MAX_VERT_DECIMATION;
			priv->base.max_width =
				kms->outputs[i].plane_caps[j].max_width;
			priv->base.max_bandwidth = 4500000000;

			if (!kms->max_sdma_width && master_idx >= 0)
				kms->max_sdma_width = priv->base.max_width;

			priv->base.plane_funcs = &virtio_plane_helper_funcs;
			priv->kms = kms;
			priv->plane_id = kms->outputs[i].plane_caps[j].plane_id;
			plane_infos[j + pipe_cnt] = &priv->base;
		}
		pipe_cnt += kms->outputs[i].plane_cnt;
	}
	return 0;
}

static void _virtio_kms_set_crtc_limit(struct virtio_kms *kms,
		struct virtio_crtc_info_priv *crtc_priv)
{
	struct limit_constraints *constraints = NULL;
	struct limit_val_pair *pair;
	char buf[16];
	int i;

	for (i = 0; i < ARRAY_SIZE(constraints_table); i++) {
		if (constraints_table[i].sdma_width == kms->max_sdma_width) {
			constraints = &constraints_table[i];
			break;
		}
	}

	pr_debug("virtio : max_sdma_width: %d\n",  kms->max_sdma_width);
	if (!constraints)
		return;

	pr_debug("virtio : set crtc limit\n");
	for (i = 0; i < MAX_NUM_LIMIT_PAIRS; i++) {
		pair = &constraints->pairs[i];

		if (!pair->str)
			break;

		snprintf(buf, sizeof(buf), "%d", pair->val);
		msm_hyp_prop_info_add_keystr(&crtc_priv->extra_info,
				pair->str, buf);
	}

	crtc_priv->base.extra_caps = crtc_priv->extra_info.data;
}

uint32_t drm_calc_max_mdp_clk(struct msm_hyp_kms *hyp_kms)
{
	uint32_t tmp_max_mdp_clk = 0;
	uint64_t magnification_times = 1;
	struct virtio_kms *kms = to_virtio_kms(hyp_kms);

	if (!kms)
		return 0;

	/* take MAX_LAYERS_MULTIPIPE * max_mdp_clk as max mdp clk to bypass sdm strategy manager */
	/* when max_sdma_width is not set*/
	if (!kms->max_sdma_width)
		magnification_times = MAX_LAYERS_MULTIPIPE;

	if (kms->device_info.max_mdp_clk)
		tmp_max_mdp_clk = kms->device_info.max_mdp_clk;
	else
		tmp_max_mdp_clk = DEFAULT_MAX_MDP_CLK;

	if (UINT_MAX < (uint64_t)tmp_max_mdp_clk  * magnification_times * 1000000) {
		pr_err("max_mdp_clk overflow\n");
		tmp_max_mdp_clk = 0;
	} else
		tmp_max_mdp_clk = tmp_max_mdp_clk  * magnification_times * 1000000;

	return tmp_max_mdp_clk;
}

static int virtio_kms_get_crtc_infos(struct msm_hyp_kms *hyp_kms,
		struct msm_hyp_crtc_info **crtc_infos,
		int *crtc_num)
{
	struct virtio_kms *kms = to_virtio_kms(hyp_kms);
	struct virtio_crtc_info_priv *priv;
	int i;
	int plane_cnt = 0;
	uint32_t plane_idx = 0;
	if (!kms || !crtc_num)
		return -EINVAL;

	if (!crtc_infos) {
		*crtc_num = kms->num_scanouts;
		return 0;
	}

	for (i = 0; i < kms->num_scanouts; i++) {
		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
		if (priv == NULL) {
			return -ENOMEM;
		}

		priv->base.max_blendstages = 0;
		for (plane_idx = 0; plane_idx < kms->outputs[i].plane_cnt; plane_idx++) {
			if (kms->outputs[i].plane_caps[plane_idx].master_plane_id < 0)
				++priv->base.max_blendstages;
		}
		pr_err("virtio : blendstage %d\n", priv->base.max_blendstages);
		priv->base.primary_plane_index = plane_cnt;
		plane_cnt += kms->outputs[i].plane_cnt;

		priv->base.max_mdp_clk = drm_calc_max_mdp_clk(hyp_kms);
		if (!priv->base.max_mdp_clk) {
			pr_err("virtio : calc max mdp clk failed\n");
			kfree(priv);
			return -ENOMEM;
		}

		pr_debug("virtio set crtc limit max_mdp_clk: %u\n", priv->base.max_mdp_clk);

		//TODO these attributes need be set as kms->device_info which got from host
		priv->base.qseed_type = "qseed3";
		priv->base.smart_dma_rev = "smart_dma_v2p5";
		priv->base.has_hdr = false;
		priv->base.max_bandwidth_low = 9600000000LL;
		priv->base.max_bandwidth_high = 9600000000LL;
		priv->base.has_src_split = true;
		priv->scanout = i;
		priv->kms = kms;
		_virtio_kms_set_crtc_limit(kms, priv);
		crtc_infos[i] = &priv->base;
	}
	return 0;
}

static int virtio_kms_get_mode_info(struct msm_hyp_kms *kms,
		const struct drm_display_mode *mode,
		struct msm_hyp_mode_info *modeinfo)
{
	uint32_t max_mdp_clk;

	if (!kms || !mode || !modeinfo)
		return -EINVAL;

	max_mdp_clk = ((struct virtio_kms *)kms)->device_info.max_mdp_clk * 1000;
	if (!max_mdp_clk)
		max_mdp_clk = DEFAULT_MAX_MDP_CLK * 1000;

	/*refine topology to avoid sdm check display pixel clk failure*/
	if (mode->clock <= max_mdp_clk)
		modeinfo->num_lm = 1;
	else if (mode->clock / 2 > max_mdp_clk)
		modeinfo->num_lm = 4;
	else
		modeinfo->num_lm = 2;

	pr_debug("virtio modeinfo->num_lm %d\n", modeinfo->num_lm);

	modeinfo->num_enc = 0;
	modeinfo->num_intf = 1;

	return 0;
}

#if 0
static void virtio_gpu_resource_id_put(struct virtio_kms *kms, uint32_t id)
{
	return;
}
#endif

static void virtio_gpu_resource_id_get(uint32_t *resid)
{
	static atomic_t seqno = ATOMIC_INIT(1);
	int handle = atomic_inc_return(&seqno);
	*resid = handle + 1;
}

static void virtio_check_framebuffer_contents(struct dma_buf *dma_buf_dump)
{
	int ret = 0;
	char *ptr;
	struct iosys_map map;

	dma_buf_begin_cpu_access(dma_buf_dump, DMA_BIDIRECTIONAL);

	ret =  dma_buf_vmap(dma_buf_dump, &map);
	if (ret) {
		DRM_DEBUG_KMS(" virtio : mmap failed for dma_buf_vmap\n");
	} else {
		ptr = (char *)map.vaddr;
		if (!ptr)
			DRM_DEBUG_KMS(" virtio : no memry map for da buffer\n");
		else
			DUMP_FRAME_CONTENT(0, DBG_BUF_COUNT, ptr);
	}

	DRM_DEBUG_KMS("virtio : framebuffer dma_buf_vmap done %p\n", map.vaddr);
	dma_buf_vunmap(dma_buf_dump, &map);
	dma_buf_end_cpu_access(dma_buf_dump, DMA_BIDIRECTIONAL);
}

static int virtio_kms_create_framebuffer(struct virtio_kms *kms,
		struct msm_hyp_framebuffer *fb)
{
	struct virtio_framebuffer_priv *fb_priv;
	struct dma_buf *dma_bufs[DRM_FORMAT_MAX_PLANES] = {0};
	uint32_t client_id;
	struct virtio_mem_info *mem;
	uint32_t export_id = 0;
	uint32_t export_flags = 0;
	int32_t handle;
	int ret = 0;
	uint32_t fence = 0;
	uint32_t modifiers = 0;
	int idx = 0, num_planes = 0;

	if (!fb) {
		pr_err("virtio : fb NULL\n");
		ret = -EINVAL;
		goto error;
	}
	num_planes = fb->base.format->num_planes;

	fb_priv = container_of(fb->info, struct virtio_framebuffer_priv, base);
	client_id = fb_priv->kms->client_id;
	mem = &fb_priv->mem;
	handle =  fb_priv->kms->channel[client_id].hab_socket[CHANNEL_CMD];
	pr_debug("virtio : create: FB ID: %d (%pK)\n", fb->base.base.id, fb);

	if (fb_priv->created) {
		pr_debug("virtio : fb already created shmem_id %d\n", mem->shmem_id);
		return 0;
	}

	for (idx = 0; idx < num_planes; idx++) {
		if (!fb->base.obj[idx]) {
			pr_err("no bo attached to fb\n");
			return -EINVAL;
		}

		if (fb->base.obj[idx]->import_attach) {
			dma_bufs[idx] = fb->base.obj[idx]->import_attach->dmabuf;
			if (!fb_priv->secure)
				virtio_check_framebuffer_contents(dma_bufs[idx]);
			get_dma_buf(dma_bufs[idx]);
		} else if (fb->base.obj[idx]->dma_buf) {
			dma_bufs[idx] = fb->base.obj[idx]->dma_buf;
			get_dma_buf(dma_bufs[idx]);
		} else {
			dma_bufs[idx] = drm_gem_prime_export(fb->base.obj[idx], 0);
			if (IS_ERR(dma_bufs[idx]))
				pr_err("export dma_buf from bo failed\n");
			return PTR_ERR(dma_bufs[idx]);
		}
	}

	mutex_lock(&fb_priv->kms->channel[client_id].hyp_chl_lock[CHANNEL_CMD]);
	memset((char *)mem, 0x00,
		sizeof(struct virtio_mem_info));
	mem->size = fb->base.obj[0]->size;
	mem->buffer = (void *)(dma_bufs[0]);
	export_flags |= HABMM_EXPIMP_FLAGS_DMABUF;
	ret = habmm_export(
		handle,
		mem->buffer,
		(uint32_t)mem->size,
		&export_id,
		export_flags);


	if (ret) {
		pr_err("virtio :framebuffer habmm export failed\n");
		mutex_unlock(&fb_priv->kms->channel[client_id].hyp_chl_lock[CHANNEL_CMD]);
		for (idx = 0; idx < num_planes; idx++)
			dma_buf_put(dma_bufs[idx]);
		goto error;
	}

	mem->shmem_id = export_id;

	mutex_unlock(&fb_priv->kms->channel[client_id].hyp_chl_lock[CHANNEL_CMD]);
	pr_debug("virtio :framebuffer habmm_export done %d\n",
		mem->shmem_id);
	for (idx = 0; idx < num_planes; idx++)
		dma_buf_put(dma_bufs[idx]);

	virtio_gpu_resource_id_get(&fb_priv->hw_res_handle);

	//fb hight and width are filled in drm_helper_mode_fill_fb_struct
	//TODO : get the fence
	ret = virtio_gpu_cmd_resource_create_2D(fb_priv->kms,
			fb_priv->hw_res_handle,
			fb_priv->format,
			fb->base.width,
			fb->base.height,
			fence);
	if (ret) {
		pr_err("resource_create_2D failed\n");
		goto error;
	}
	if (fb_priv->secure)
		modifiers |= SECURE_SOURCE;

	if (fb_priv->compressed)
		 modifiers |= COMPRESSED_SOURCE;

	ret = virtio_gpu_cmd_set_resource_info(fb_priv->kms,
			fb_priv->hw_res_handle,
			modifiers,
			fb->base.offsets,
			fb->base.pitches,
			fb_priv->format);
	if (ret) {
		pr_err("set_resource_info failed\n");
		goto error;
	}

	ret = virtio_gpu_cmd_resource_attach_backing(fb_priv->kms,
			fb_priv->hw_res_handle,
			mem->shmem_id,
			mem->size);
	if (ret) {
		pr_err("resource_attach_backing failed\n");
	}
	pr_debug("virtio :virtio_kms_create_framebuffer done\n");

	fb_priv->created = true;
error:
	return ret;
}

static void virtio_kms_destroy_framebuffer(struct drm_framebuffer *framebuffer)
{
	struct msm_hyp_framebuffer *fb;
	struct virtio_framebuffer_priv *fb_priv;
	int32_t rc = 0;
	uint32_t client_id;
	struct virtio_mem_info *mem;
	int32_t handle;
	uint32_t unexport_flags = 0;
	struct dma_buf *dma_buf;
//	char *ptr;
//	int i;
//	struct dma_buf_map map;
	fb = to_msm_hyp_fb(framebuffer);
	fb_priv = container_of(fb->info, struct virtio_framebuffer_priv, base);
	client_id = fb_priv->kms->client_id;
	mem = &fb_priv->mem;
	handle = fb_priv->kms->channel[client_id].hab_socket[CHANNEL_CMD];
	pr_debug("virtio : framebuffer destroy FB ID: %d (%pK) created %d shmem_id%d\n",
			fb->base.base.id, fb,
			fb_priv->created, mem->shmem_id);

	rc = virtio_gpu_cmd_resource_detach_backing(fb_priv->kms,
			fb_priv->hw_res_handle);
	if (rc) {
		pr_err("virtio : frame buffer betach backing failed %d\n",
				fb_priv->hw_res_handle);
		goto error;
	}
	mutex_lock(&fb_priv->kms->channel[client_id].hyp_chl_lock[CHANNEL_CMD]);

	unexport_flags |= HABMM_EXPIMP_FLAGS_FD;
	rc = habmm_unexport(
			handle,
			mem->shmem_id,
			unexport_flags);
	if (rc) {
		pr_err("framebuffer habmm_unexport failed");
	}
	mutex_unlock(&fb_priv->kms->channel[client_id].hyp_chl_lock[CHANNEL_CMD]);

	rc = virtio_gpu_cmd_resource_unref(fb_priv->kms,
			fb_priv->hw_res_handle);
	if (rc)
		pr_err("virtio : resource unref failed %d\n",
				fb_priv->hw_res_handle);

        dma_buf = (struct dma_buf *) mem->buffer;

	//dump the incoming data
	/*
	dma_buf_begin_cpu_access(dma_buf, DMA_BIDIRECTIONAL);
	rc =  dma_buf_vmap(dma_buf, &map);
	if (rc)
		pr_err(" mmap failed for dma_buf_vmap\n");
	else {
		ptr = (char *)map.vaddr;
		if (!ptr)
			pr_err(" no memry map for da buffer\n");
		for (i = 0; i < 50; ) {
			pr_err("framebuffer unexport data %x %x %x %x %x\n",
				ptr[i], ptr[i+1], ptr[i+2], ptr[i+3], ptr[i+4]);
			i = i + 5;
		}
	}

	pr_err("framebuffer dma_buf_vmap done %p\n", map.vaddr);
	dma_buf_vunmap(dma_buf, &map);
	dma_buf_end_cpu_access(dma_buf, DMA_BIDIRECTIONAL);
	*/
	error:
	pr_debug("virtio :destroy_framebuffer done %d\n",
			fb_priv->hw_res_handle);
	fb_priv->created = false;
	kfree(fb_priv);
	fb->info = NULL;
}

static int virtio_kms_get_framebuffer_info(struct msm_hyp_kms *hyp_kms,
		struct drm_framebuffer *framebuffer,
		struct msm_hyp_framebuffer_info **fb_info)
{
	struct virtio_framebuffer_priv *fb_priv;
	uint32_t format;
	struct virtio_kms *kms = to_virtio_kms(hyp_kms);

	format = virtio_gpu_translate_format(framebuffer->format->format,
			framebuffer->modifier);
	if (format == 0) {
		pr_err("virtio: Not valid Virtio format\n");
		return -EINVAL;
	}


	fb_priv = kzalloc(sizeof(*fb_priv), GFP_KERNEL);
	if (!fb_priv)
		return -ENOMEM;

	fb_priv->base.destroy = virtio_kms_destroy_framebuffer;
	fb_priv->format = format;
	fb_priv->mem.shmem_id = 0;
	fb_priv->kms = kms;
	if ((framebuffer->modifier & DRM_FORMAT_MOD_QTI_COMPRESSED) ==
			DRM_FORMAT_MOD_QTI_COMPRESSED)
		fb_priv->compressed = true;

	*fb_info = &fb_priv->base;
	return 0;
}

static void virtio_kms_commit(struct msm_hyp_kms *kms,
		struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct msm_hyp_crtc *c;
	struct virtio_crtc_info_priv *priv;
	int i;
	bool async = true;

	if (!old_state)
		return;
	pr_debug("virtio_kms_commit called\n");
	for_each_new_crtc_in_state(old_state, crtc, crtc_state, i) {
		c = to_msm_hyp_crtc(crtc);
		priv = container_of(c->info,
				struct virtio_crtc_info_priv,
				base);

		if (crtc_state->active) {
			virtio_kms_plane_zpos_adj_fe(crtc, old_state);
		}

		priv->kms->outputs[priv->scanout].crtc = crtc;
		virtio_gpu_cmd_event_control(priv->kms,
				priv->scanout,
				VIRTIO_COMMIT_COMPLETE,
				true);

		virtio_gpu_cmd_scanout_flush(priv->kms,
				priv->scanout,
				async);
	}
	pr_debug("virtio_kms_commit done\n");
}

static void virtio_kms_enable_vblank(struct msm_hyp_kms *hyp_kms,
		struct drm_crtc *crtc)
{
	struct msm_hyp_crtc *c;
	struct virtio_crtc_info_priv *priv;
	struct virtio_kms *kms;

	c = to_msm_hyp_crtc(crtc);
	priv = container_of(c->info, struct virtio_crtc_info_priv, base);
        kms = to_virtio_kms(hyp_kms);

	kms->outputs[priv->scanout].vblank_enabled = true;
	virtio_gpu_cmd_event_control(priv->kms,
			priv->scanout,
			VIRTIO_VSYNC,
			true);
}

static void virtio_kms_disable_vblank(struct msm_hyp_kms *hyp_kms,
		struct drm_crtc *crtc)
{
	struct msm_hyp_crtc *c;
	struct virtio_crtc_info_priv *priv;
	struct virtio_kms *kms;

	c = to_msm_hyp_crtc(crtc);
	priv = container_of(c->info, struct virtio_crtc_info_priv, base);
	kms = to_virtio_kms(hyp_kms);
	kms->outputs[priv->scanout].vblank_enabled = false;
	virtio_gpu_cmd_event_control(priv->kms,
			priv->scanout,
			VIRTIO_VSYNC,
			false);
}

static const struct msm_hyp_kms_funcs virtio_kms_funcs = {
	.get_connector_infos = virtio_kms_get_connector_infos,
	.get_plane_infos = virtio_kms_get_plane_infos,
	.get_crtc_infos = virtio_kms_get_crtc_infos,
	.get_mode_info = virtio_kms_get_mode_info,
	.get_framebuffer_info = virtio_kms_get_framebuffer_info,
	.commit = virtio_kms_commit,
	.enable_vblank = virtio_kms_enable_vblank,
	.disable_vblank = virtio_kms_disable_vblank,
};

/*
static void virtio_kms_get_capsets(struct virtio_kms *kms,
		int num_capsets)
{
	int i, ret;

	kms->capsets = kcalloc(num_capsets,
			 sizeof(struct virtio_gpu_drv_capset),
				 GFP_KERNEL);
       if (!kms->capsets) {
		DRM_ERROR("failed to allocate cap sets\n");
		return;
	}
       for (i = 0; i < num_capsets; i++) {
		virtio_cmd_get_capset_info(kms, i);
		ret = wait_event_timeout(kms->resp_wq,
				 kms->capsets[i].id > 0, 5 * HZ);
		if (ret == 0) {
			pr_err("timed out waiting for cap set %d\n", i);
			spin_lock(&kms->display_info_lock);
			kfree(kms->capsets);
			kms->capsets = NULL;
			spin_unlock(&kms->display_info_lock);
			return;
		}
		pr_debug("cap set %d: id %d, max-version %d, max-size %d\n",
			i, kms->capsets[i].id,
			kms->capsets[i].max_version,
			kms->capsets[i].max_size);
	}
	kms->num_capsets = num_capsets;
}
*/
static int _virtio_kms_hw_deinit(struct virtio_kms *kms)
{
	uint32_t scanout, plane;
	uint32_t plane_id = 0;
	int rc = 0;
	uint32_t num_planes = 0;
	struct virtio_kms_output *output;

	for (scanout = 0; scanout < kms->num_scanouts; scanout++) {
		num_planes = kms->outputs[scanout].plane_cnt;
		output = &kms->outputs[scanout];
		for (plane = 0; plane < num_planes; plane++) {
			plane_id = output->plane_caps[plane].plane_id;
			rc = virtio_gpu_cmd_plane_destroy(kms,
					scanout,
					plane_id);
			if (rc) {
				pr_err("plane destroy failed %d\n", plane_id);
			}
		}
	}
	return rc;
}

static int virtio_kms_scanout_init(struct virtio_kms *kms, uint32_t scanout)
{
	int rc = 0;
	uint32_t num_planes = 0;
	uint32_t plane;
	uint32_t plane_id = 0;
	struct virtio_kms_output *output = NULL;

	if (scanout >= VIRTIO_GPU_MAX_SCANOUTS) {
		pr_err("virtio : Wrong Scanout ID\n");
		goto error;
	}
	output = &kms->outputs[scanout];
	if (kms->has_edid)
		virtio_gpu_cmd_get_edid(kms, scanout);

	rc = virtio_gpu_cmd_get_display_info_ext(kms, scanout);
	if (rc) {
		pr_err("virtio : get_display_info_ext failed %d\n",
				scanout);
		goto error;
	}

	rc = virtio_gpu_cmd_get_scanout_attributes(kms, scanout);
	if (rc) {
		goto error;
	}

	rc = virtio_gpu_cmd_get_scanout_planes(kms, scanout);
	if (rc) {
		goto error;
	}

	num_planes = output->plane_cnt;

	if (!num_planes)
		pr_err("virtio : No planes passed\n");

	for (plane = 0; plane < num_planes; plane++)
		output->plane_caps[plane].master_plane_id = -1;

	for (plane = 0; plane < num_planes; plane++) {
		plane_id = output->plane_caps[plane].plane_id;
		rc = virtio_gpu_cmd_plane_create(kms,
				scanout,
				plane_id);
		if (rc) {
			pr_err("virtio : Plane creation failed plane-id %d\n",
					plane_id);
			continue;
		}
		rc = virtio_gpu_cmd_get_plane_caps(kms,
				scanout,
				plane_id);
		if (rc) {
			pr_err("virtio :scanout %d virtio_gpu_cmd_get_plane_caps failed %d\n",
				scanout, plane_id);
			goto error;
		}

		rc = virtio_gpu_cmd_get_plane_properties(kms,
				scanout,
				plane_id);
		if (rc) {
			pr_err("virtio : scanout %d plane_properties failed %d\n",
					scanout,
					plane_id);
			goto error;
		}
		/* get the pair plane for the multi rec support*/

		if (output->plane_caps[plane].pair_plane_id) {
			pr_debug("virtio: setting the master plane idx %d\n",
					plane);

			output->plane_caps[num_planes].plane_id =
				output->plane_caps[plane].pair_plane_id;
			output->plane_caps[num_planes].master_plane_id = plane;
			num_planes++;
			output->plane_cnt++;
		}
	}
error:
	return rc;
}

static int _virtio_kms_hw_init(struct virtio_kms *kms)
{
	int rc = 0;
	uint32_t scanout;

//	if (virtio_has_feature(kms->vdev, VIRTIO_GPU_F_EDID)) {
//		kms->has_edid = true;
//		DRM_INFO("EDID support available.\n");
//	}
//	virtio_has_feature(kms->vdev, VIRTIO_GPU_F_VENDOR);

	init_waitqueue_head(&kms->resp_wq);
	spin_lock_init(&kms->display_info_lock);

	//virtio_kms_get_capsets(kms, kms->num_capsets);

	rc = virtio_gpu_cmd_get_device_info(kms);
	if (rc) {
		pr_err("get_device_info failed\n");
		goto error;
	}

	rc = virtio_gpu_cmd_get_display_info(kms);
	if (rc) {
		pr_err("get_display_info failed\n");
		goto error;
	}

	for (scanout = 0; scanout < kms->num_scanouts; scanout++) {
		rc = virtio_kms_scanout_init(kms, scanout);
		if (rc)
			pr_err("scanout init failed %d\n", scanout);
	}
error:
	return rc;
}

#if 0
static int _virtio_kms_parse_client_id(struct device_node *node,
		uint32_t *client_id)
{
	int len = 0;
	int ret = 0;
	const char *client_id_str;

	client_id_str = of_get_property(node, "qcom,client-id", &len);
	if (!client_id_str || len != CLIENT_ID_LEN_IN_CHARS) {
		pr_err("client_id_str len(%d) is invalid\n", len);
		ret = -EINVAL;
	} else {
		/* Try node as a hex value */
		ret = kstrtouint(client_id_str, 16, client_id);
		if (ret) {
			/* Otherwise, treat at 4cc code */
			*client_id = fourcc_code(client_id_str[0],
					client_id_str[1],
					client_id_str[2],
					client_id_str[3]);

			ret = 0;
		}
	}

	return ret;
}

#endif


static int virtio_gpu_hab_open(struct virtio_kms *kms)
{
	int ret = 0;
	uint32_t client_id = 0;

	if (!kms) {
		pr_err("virtio : kms NULL\n");
		ret = -EINVAL;
		goto exit;
	}
	client_id = kms->client_id;

	ret = habmm_socket_open(
			&kms->channel[client_id].hab_socket[CHANNEL_CMD],
			kms->mmid_cmd,
			-1,
			0);
	if (!ret) {
		pr_info("virtio: hab socket open mmid %d OK\n", kms->mmid_cmd);

	} else {
		pr_err("hab open failed mmid %d ret %d\n", kms->mmid_cmd, ret);
		goto exit;
	}
	spin_lock_init(&kms->channel[client_id].hyp_chl_spin_lock);
	mutex_init(&kms->channel[client_id].hyp_chl_lock[CHANNEL_CMD]);

	ret = habmm_socket_open(
			&kms->channel[client_id].hab_socket[CHANNEL_EVENTS],
			kms->mmid_event,
			-1,
			0);
	if (!ret) {
		pr_info("virtio: hab socket open mmid %d OK\n", kms->mmid_event);
	} else {
		pr_err("hab open failed mmid %d ret %d\n", kms->mmid_event, ret);
	}

	mutex_init(&kms->channel[client_id].hyp_chl_lock[CHANNEL_EVENTS]);
exit:
	return ret;
}

static int virtio_kms_service_hpd(struct virtio_kms *kms, uint32_t scanout)
{
	int rc = 0;
	rc = virtio_kms_scanout_init(kms, scanout);
	if (rc)
		 pr_err("scanout init failed %d\n", scanout);
	return 0;
}

static void virtio_kms_vsync(struct virtio_kms *kms, uint32_t scanout)
{
	struct drm_crtc *crtc = kms->outputs[scanout].crtc;
	msm_hyp_crtc_vblank_done(crtc);

	if (kms->outputs[scanout].vblank_enabled) {
		virtio_gpu_cmd_event_control(kms,
				scanout,
				VIRTIO_VSYNC,
				true);
	}
}

static void virtio_kms_service_commit_done(
		struct virtio_kms *kms,
		uint32_t scanout)
{
	struct drm_crtc *crtc = kms->outputs[scanout].crtc;
	virtio_gpu_cmd_event_control(kms,
				scanout,
				VIRTIO_COMMIT_COMPLETE,
				false);

	msm_hyp_crtc_commit_done(crtc);
}

void  virtio_kms_event_handler(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t num_event,
		uint32_t event_type)
{
	switch (event_type) {

	case VIRTIO_VSYNC:
		virtio_kms_vsync(kms, scanout);
	break;

	case VIRTIO_COMMIT_COMPLETE:
		virtio_kms_service_commit_done(kms, scanout);
	break;

	case VIRTIO_HPD:
		virtio_kms_service_hpd(kms, scanout);
	break;

	default:
		pr_err("Undefine event received %d\n",event_type);
	}
}

static int virtio_kms_bind(struct device *dev,
		struct device *master,
                void *data)
{
        struct virtio_kms *kms = dev_get_drvdata(dev);
        struct drm_device *drm_dev = dev_get_drvdata(master);
	if (!kms) {
		pr_err("virtio_kms_bind failed ");
		return 0;
	}
        kms->dev = drm_dev;
        msm_hyp_set_kms(drm_dev, &kms->base);

        return 0;
}

static void virtio_kms_unbind(struct device *dev,
		struct device *master,
                void *data)
{
        struct virtio_kms *kms = dev_get_drvdata(dev);

        msm_hyp_set_kms(kms->dev, NULL);
}

static const struct component_ops virtio_kms_comp_ops = {
        .bind = virtio_kms_bind,
        .unbind = virtio_kms_unbind,
};

static int virtio_kms_probe(struct platform_device *pdev)
{
        struct device *dev = &pdev->dev;
        struct virtio_kms *kms;
        int ret;
//        char marker_buff[MARKER_BUFF_LENGTH] = {0};


        kms = devm_kzalloc(dev, sizeof(*kms), GFP_KERNEL);
        if (!kms)
                return -ENOMEM;

//        ret = _virtio_kms_parse_client_id(dev->of_node, &kms->client_id);
//        if (ret)
//                return ret;

	kms->client_id = 0;

	kms->mmid_cmd = MM_DISP_1;
	kms->mmid_event = MM_DISP_3;

//	ret = _virtio_kms_parse_capsets(dev->of_node, &kms->num_capsets);
//	if (ret)
//		return ret;

	ret = virtio_gpu_hab_open(kms);
	if (ret)
		return ret;

	kms->stop = false;
	kthread_run(virtio_gpu_event_kthread, kms, "virtio gpu kthread");

        ret = _virtio_kms_hw_init(kms);
        if (ret)
                return ret;

	pr_debug("numbr of scanouts %d for client %x\n", kms->num_scanouts, kms->client_id);
        kms->base.funcs = &virtio_kms_funcs;

        platform_set_drvdata(pdev, kms);

        ret = component_add(&pdev->dev, &virtio_kms_comp_ops);
        if (ret) {
		pr_err("component add failed, rc=%d\n", ret);
		return ret;
	}
  //       snprintf(marker_buff, sizeof(marker_buff),
  //              "kernel_fe: virtio_kms probe client %x", kms->client_id);
//        place_marker(marker_buff);

        return 0;
}

static int virtio_kms_remove(struct platform_device *pdev)
{
	//TODO: implement remove
	int ret;
	struct virtio_kms *kms = platform_get_drvdata(pdev);

	ret = _virtio_kms_hw_deinit(kms);
	if (ret) {
		pr_err("deinit failed \n");
	}
	return 0;
}

static const struct platform_device_id virtio_kms_id[] = {
        { "virtio-kms", 0 },
        { }
};

static const struct of_device_id dt_match[] = {
        { .compatible = "qcom,virtio-kms" },
        {}
};
static struct platform_driver virtio_kms_driver = {
        .probe      = virtio_kms_probe,
        .remove     = virtio_kms_remove,
        .driver     = {
                .name   = "virtio_kms",
                .of_match_table = dt_match,
        },
        .id_table   = virtio_kms_id,
};

void virtio_kms_register(void)
{
        platform_driver_register(&virtio_kms_driver);
}

void virtio_kms_unregister(void)
{
        platform_driver_unregister(&virtio_kms_driver);
}

#if (KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE)
MODULE_IMPORT_NS(DMA_BUF);
#endif
