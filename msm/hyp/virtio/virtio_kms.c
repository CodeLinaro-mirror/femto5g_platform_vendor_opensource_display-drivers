// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt)	"[virtio-kms:%s:%d] " fmt, __func__, __LINE__
#include <linux/sort.h>
#include <drm/drm_atomic.h>
#include <linux/virtio_config.h>
//#include <soc/qcom/boot_stats.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_helper.h>
#include <sde_connector.h>
#include <sde_encoder.h>
#include <sde_plane.h>
#include <sde_encoder_phys.h>
#include <sde_rm.h>
#include <sde_hw_pingpong.h>
#include "msm_hyp_trace.h"
#include "msm_hyp_utils.h"
#include "virtio_kms.h"
#include "virtio_ext.h"
#include "virtgpu_vq.h"
#include <linux/habmm.h>

#define VIRTIO_KMS_DBG(fmt, ...)		pr_debug(fmt, ##__VA_ARGS__)
#define VIRTIO_KMS_INFO(fmt, ...)		pr_info(fmt, ##__VA_ARGS__)
#define VIRTIO_KMS_WARN(fmt, ...)		pr_warn(fmt, ##__VA_ARGS__)
#define VIRTIO_KMS_ERR(fmt, ...)		pr_err(fmt, ##__VA_ARGS__)

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

#define VIRTIO_TRANSPARENCY_GLOBAL_ALPHA (1<<1)
#define VIRTIO_TRANSPARENCY_SOURCE_ALPHA (1<<2)
//#define VIRTIO_DEBUG 1

#define DUMP_FRAME_CONTENT(start, end, ptr)					\
	for (int idx = (start); idx < (end); idx++) {				\
		DRM_DEBUG_KMS("framebuffer data %x\n", ptr[idx]);	\
	}

#ifndef UINT_MAX
#define UINT_MAX 0xffffffffU  /* define this if limits.h not available */
#endif

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
	{
		/* SA8797 */
		5120,
		{
			{"sspp_linewidth_usecases", 3},
			{"vig",   0x1},
			{"dma",   0x2},
			{"scale", 0x4},
			{"sspp_linewidth_values", 3},
			{"limit_usecase", 0x1},
			{"limit_value",  5120},
			{"limit_usecase", 0x5},
			{"limit_value",  5120},
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
	VIRTIO_KMS_DBG("virtio format %s to drm format %s\n",
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
	VIRTIO_KMS_DBG("drm format %s to virtio format %s\n",
			virtio_get_drm_format_string(drm_fourcc),
			virtio_get_virtio_format_string(format));
	WARN_ON(format == 0);

	return format;
}

static int virtio_connector_set_info_blob(struct drm_connector *connector,
        void *info, void *display, struct msm_mode_info *mode_info)
{
	struct msm_hyp_display *hyp_display = display;

	sde_kms_info_add_keystr(info, "display type", hyp_display->info->display_type);

	return 0;
}

static int virtio_connector_post_init(struct drm_connector *connector,
        void *display)
{
	return 0;
}

static int virtio_connector_get_modes(struct drm_connector *connector,
        void *display, const struct msm_resource_caps_info *avail_res)
{
	struct msm_hyp_display *hyp_display = display;
	struct drm_display_mode *m;
	struct virtio_connector_info_priv *priv;
	int i;

	priv = container_of(hyp_display->info, struct virtio_connector_info_priv, base);

	if (hyp_display->info->display_info.width_mm > 0 &&
				hyp_display->info->display_info.height_mm > 0) {
		connector->display_info.width_mm =
					hyp_display->info->display_info.width_mm;
		connector->display_info.height_mm =
					hyp_display->info->display_info.height_mm;
	}

	for (i = 0; i < priv->mode_count; i++) {
		m = drm_mode_duplicate(connector->dev, &priv->modes[i]);
		if (!m)
			return i;
		drm_mode_probed_add(connector, m);
	}

	msm_hyp_connector_init_edid(connector, priv->panel_name);

	return priv->mode_count;
}

static enum drm_mode_status virtio_connector_mode_valid(struct drm_connector *connector,
        struct drm_display_mode *mode,
        void *display,
        const struct msm_resource_caps_info *avail_res)
{
	return MODE_OK;
}


static int virtio_connector_atomic_check(struct drm_connector *connector,
		void *display,
		struct drm_atomic_state *state)
{
	return 0;
}

static int virtio_connector_get_info(struct drm_connector *connector,
		struct msm_display_info *info, void *display)
{
	struct msm_hyp_display *hyp_display = display;

	*info = hyp_display->info->display_info;

	return 0;
}

static int virtio_connector_get_mode_info(struct drm_connector *connector,
        const struct drm_display_mode *drm_mode,
        struct msm_sub_mode *sub_mode,
        struct msm_mode_info *mode_info,
        void *display,
        const struct msm_resource_caps_info *avail_res)
{
	struct msm_hyp_display *hyp_display = display;
	const u32 single_intf = 1;
	const u32 no_enc = 0;
	struct msm_display_topology *topology;
	struct sde_connector *sde_conn;
	struct msm_drm_private *priv;
	struct msm_resource_caps_info avail_dp_res;
	struct msm_display_info *info;
	int rc = 0;

	if (!drm_mode || !mode_info || !avail_res ||
			!avail_res->max_mixer_width || !connector || !display ||
			!connector->dev || !connector->dev->dev_private) {
		VIRTIO_KMS_ERR("invalid params\n");
		return -EINVAL;
	}

	memset(mode_info, 0, sizeof(*mode_info));

	sde_conn = to_sde_connector(connector);
	priv = connector->dev->dev_private;

	topology = &mode_info->topology;

	memcpy(&avail_dp_res, avail_res, sizeof(struct msm_resource_caps_info));

	info = &hyp_display->info->display_info;
	avail_dp_res.num_lm = min(avail_res->num_lm, info->lm_count);
	avail_dp_res.num_dsc = min(avail_res->num_dsc, info->dsc_count);

	rc = msm_get_mixer_count(priv, drm_mode, &avail_dp_res,
			&topology->num_lm);
	if (rc) {
		VIRTIO_KMS_ERR("error getting mixer count. rc:%d\n", rc);
		return rc;
	}
	/* reset connector lm_mask for every connection event and
	 * this will get re-populated in resource manager based on
	 * resolution and topology of display.
	 */
	sde_conn->lm_mask = 0;

	topology->num_enc = no_enc;
	topology->num_intf = single_intf;

	mode_info->frame_rate = DIV_ROUND_CLOSEST_ULL(mul_u32_u32(drm_mode->clock, 1000),
				drm_mode->htotal * drm_mode->vtotal);
	mode_info->vtotal = drm_mode->vtotal;

	//FIXME: by default wide bus is enabled
	mode_info->wide_bus_en = true;
	//FIXME: by default 2ppc
	mode_info->pclk_factor = 2;

	//FIXME: how to determine the DSC number for compression
	topology->comp_type = mode_info->comp_info.comp_type;
	if (mode_info->comp_info.comp_type)
		topology->num_enc = min(info->dsc_count, topology->num_lm);
	VIRTIO_KMS_DBG("Exit %s   mode %dx%d  %dHz  %d lm  %d enc  %d intf  comp %d  WB %d  pclk_factor %d\n", __func__,
				drm_mode->htotal, mode_info->vtotal, mode_info->frame_rate,
				topology->num_lm, topology->num_enc, topology->num_intf, topology->comp_type,
				mode_info->wide_bus_en, mode_info->pclk_factor);

	return 0;
}

static void virtio_connector_post_open(struct drm_connector *connector, void *display)
{
	return;
	// TODO:
}

static int virtio_connector_set_colorspace(struct drm_connector *connector,
                        void *display)
{
	// TODO:
	return 0;
}

static int virtio_connector_config_hdr(struct drm_connector *connector, void *display,
        struct sde_connector_state *c_state)
{
	// TODO:
	return 0;
}

static int virtio_connector_install_properties(void *display, struct drm_connector *conn)
{
	// TODO:
	return 0;
}

static int virtio_connector_detect_ctx(struct drm_connector *connector,
        struct drm_modeset_acquire_ctx *ctx,
        bool force,
        void *display)
{
	struct msm_hyp_display *hyp_display = display;
	struct virtio_connector_info_priv *priv;

	priv = container_of(hyp_display->info, struct virtio_connector_info_priv, base);

	return priv->connector_status;
}

static struct drm_encoder *virtio_connector_atomic_best_encoder(
		struct drm_connector *connector,
		void *display,
		struct drm_connector_state *c_state)
{
	struct msm_hyp_display *hyp_display = display;
	return hyp_display->encoder;
}

static const struct sde_connector_ops virtio_conn_ops = {
	.set_info_blob = virtio_connector_set_info_blob,
	.post_init	= virtio_connector_post_init,
	.detect_ctx	= virtio_connector_detect_ctx,
	.get_modes	= virtio_connector_get_modes,
	.atomic_check = virtio_connector_atomic_check,
	.mode_valid = virtio_connector_mode_valid,
	.get_info	= virtio_connector_get_info,
	.get_mode_info	= virtio_connector_get_mode_info,
	.post_open	= virtio_connector_post_open,
	.set_colorspace = virtio_connector_set_colorspace,
	.config_hdr = virtio_connector_config_hdr,
	.atomic_best_encoder = virtio_connector_atomic_best_encoder,
	.install_properties = virtio_connector_install_properties,
};

static void virtio_kms_bridge_mode_set(struct drm_bridge *drm_bridge,
		const struct drm_display_mode *mode,
		const struct drm_display_mode *adjusted_mode)
{
	struct msm_hyp_display *display;
	struct virtio_connector_info_priv *priv;
	struct virtio_gpu_rect dest_rect = {0,0,0,0};
	int i, mode_index = -1;
	uint32_t scanout;
	int rc = 0;

	display = container_of(drm_bridge, struct msm_hyp_display, bridge);
	priv = container_of(display->info, struct virtio_connector_info_priv, base);
	scanout = priv->scanout;

	for (i = 0; i < priv->mode_count; i++) {
		mode = &priv->modes[i];
		if ((adjusted_mode->hdisplay == mode->hdisplay) &&
		    (adjusted_mode->vdisplay == mode->vdisplay)) {
			mode_index = i;
			dest_rect.width = mode->hdisplay;
			dest_rect.height = mode->vdisplay;
			dest_rect.x = 0;
			dest_rect.y = 0;
			break;
		}
	}
	if (mode_index < 0) {
		VIRTIO_KMS_ERR("mode set failed %d for mode h-%d v-%d",
				priv->scanout,
				adjusted_mode->hdisplay,
				adjusted_mode->vdisplay);
		mode = NULL;
		return;
	}
	priv->mode_index = mode_index;
	priv->mode_rect.width = mode->hdisplay;
	priv->mode_rect.height = mode->vdisplay;
	priv->mode_rect.x = 0;
	priv->mode_rect.y = 0;

	rc = virtio_gpu_cmd_set_scanout_properties(priv->kms,
			scanout,
			VIRTIO_SCANOUT_POWER_MODE_OFF,
			mode_index,
			0,
			dest_rect);
	if (rc) {
		VIRTIO_KMS_ERR("scanout set properties for mode failed %d\n",
				mode_index);
	}
}

static void virtio_kms_bridge_pre_enable(struct drm_bridge *drm_bridge)
{
	struct msm_hyp_display *display;
	struct virtio_connector_info_priv *priv;
	struct virtio_gpu_rect dest_rect;
	uint32_t scanout;

	display = container_of(drm_bridge, struct msm_hyp_display, bridge);
	priv = container_of(display->info, struct virtio_connector_info_priv, base);
	dest_rect.width = priv->mode_rect.width;
        dest_rect.height = priv->mode_rect.height;
        dest_rect.x = priv->mode_rect.x,
        dest_rect.y = priv->mode_rect.y;
	scanout = priv->scanout;
#if 0	// FIXME: SKIP FOR NOW
	virtio_gpu_cmd_set_scanout_properties(priv->kms,
			scanout,
			VIRTIO_SCANOUT_POWER_MODE_PRE_ENABLE,
			priv->mode_index,
			0,
			dest_rect);
#endif
}

static void virtio_kms_bridge_enable(struct drm_bridge *drm_bridge)
{
	struct msm_hyp_display *display;
	struct virtio_connector_info_priv *priv;
	struct virtio_gpu_rect dest_rect;
	uint32_t scanout;

	display = container_of(drm_bridge, struct msm_hyp_display, bridge);
	priv = container_of(display->info, struct virtio_connector_info_priv, base);
	dest_rect.width = priv->mode_rect.width;
        dest_rect.height = priv->mode_rect.height;
        dest_rect.x = priv->mode_rect.x,
        dest_rect.y = priv->mode_rect.y;
	scanout = priv->scanout;
	virtio_gpu_cmd_set_scanout_properties(priv->kms,
			scanout,
			VIRTIO_SCANOUT_POWER_MODE_ON,
			priv->mode_index,
			0,
			dest_rect);
	virtio_gpu_cmd_scanout_flush(priv->kms, scanout, true);
}

static void virtio_kms_bridge_disable(struct drm_bridge *drm_bridge)
{
	struct msm_hyp_display *display;
	struct virtio_connector_info_priv *priv;
	struct virtio_gpu_rect dest_rect;
	uint32_t scanout;

	display = container_of(drm_bridge, struct msm_hyp_display, bridge);
	priv = container_of(display->info, struct virtio_connector_info_priv, base);
	dest_rect.width = priv->mode_rect.width;
	dest_rect.height = priv->mode_rect.height;
	dest_rect.x = priv->mode_rect.x,
	dest_rect.y = priv->mode_rect.y;

	scanout = priv->scanout;
#if 0	// FIXME: SKIP FOR NOW
	virtio_gpu_cmd_set_scanout_properties(priv->kms,
			scanout,
			VIRTIO_SCANOUT_POWER_MODE_PRE_DISABLE,
			priv->mode_index,
			0,
			dest_rect);
	virtio_gpu_cmd_scanout_flush(priv->kms, scanout, true);
#endif
}

static void virtio_kms_bridge_post_disable(struct drm_bridge *drm_bridge)
{
	struct msm_hyp_display *display;
	struct virtio_connector_info_priv *priv;
	struct virtio_gpu_rect dest_rect;
	uint32_t scanout;

	display = container_of(drm_bridge, struct msm_hyp_display, bridge);
	priv = container_of(display->info, struct virtio_connector_info_priv, base);
	dest_rect.width = priv->mode_rect.width;
	dest_rect.height = priv->mode_rect.height;
	dest_rect.x = priv->mode_rect.x,
	dest_rect.y = priv->mode_rect.y;

	scanout = priv->scanout;
	virtio_gpu_cmd_set_scanout_properties(priv->kms,
			scanout,
			VIRTIO_SCANOUT_POWER_MODE_OFF,
			priv->mode_index,
			0,
			dest_rect);
	virtio_gpu_cmd_scanout_flush(priv->kms, scanout, true);
}

static const struct drm_bridge_funcs virtio_bridge_ops = {
	.pre_enable   = virtio_kms_bridge_pre_enable,
	.enable       = virtio_kms_bridge_enable,
	.disable      = virtio_kms_bridge_disable,
	.post_disable = virtio_kms_bridge_post_disable,
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
		if (name)
			snprintf(name, PANEL_NAME_LEN, "%s_%d", "HDMI", scanout);
		break;
	case VIRTIO_PORT_TYPE_DSI:
		connector_type = DRM_MODE_CONNECTOR_DSI;
		if (name)
			snprintf(name, PANEL_NAME_LEN, "%s_%d", "DSI", scanout);
		break;
	case VIRTIO_PORT_TYPE_DP:
		connector_type = DRM_MODE_CONNECTOR_DisplayPort;
		if (name)
			snprintf(name, PANEL_NAME_LEN, "%s_%d", "DP", scanout);
		break;
	default:
		connector_type = DRM_MODE_CONNECTOR_Unknown;
		if (name)
			snprintf(name, PANEL_NAME_LEN, "%s_%d", "Unknown", scanout);
		break;
	}

	if (name)
		VIRTIO_KMS_DBG("%s - port_type = %x name = %s\n", __func__, port_type, name);
	else
		VIRTIO_KMS_DBG("%s - port_type = %x\n", __func__, port_type);

	return connector_type;
}

static int virtio_kms_get_displays(struct sde_kms *sde_kms,
		void **displays, int *display_num)
{
	struct msm_hyp_kms *hyp_kms = sde_kms->hyp_kms;
	struct virtio_kms *kms = to_virtio_kms(hyp_kms);
	struct virtio_kms_output *output;
	int i, num_displays = 0;

	for (i = 0; i < kms->num_scanouts; i++) {
		output = &kms->outputs[i];
		if (output->hw_assign.dpu_id != DPUID(sde_kms))
			continue;
		if (displays)
			displays[num_displays] = output;
		num_displays ++;
	}
	*display_num = num_displays;

	return 0;
}

static int virtio_kms_get_connector_infos(struct sde_kms *sde_kms,
		struct msm_hyp_connector_info **connector_infos,
		int *connector_num)
{
	struct msm_hyp_kms *hyp_kms = sde_kms->hyp_kms;
	struct virtio_kms *kms = to_virtio_kms(hyp_kms);
	//struct drm_device *ddev = sde_kms->dev;
	struct virtio_kms_output *output;
	int i, j, num_scanouts = 0;
	struct virtio_connector_info_priv *priv;
	struct virtio_display_modes *info;
	struct msm_display_info *disp_info;
	struct drm_display_mode *mode;
	struct scanout_attrib *attr;
	u32 mask;
	int count;

#if 0	// Let SDE KMS provide
	if (!ddev) {
		VIRTIO_KMS_ERR("ddev failed \n");
		return 0;
	}
	ddev->mode_config.min_width = 0;
	ddev->mode_config.max_width = DISPLAY_DEVICE_MAX_WIDTH;
	ddev->mode_config.min_height = 0;
	ddev->mode_config.max_height = DISPLAY_DEVICE_MAX_HEIGHT;
#endif

	for (i = 0; i < kms->num_scanouts; i++) {
		output = &kms->outputs[i];
		if (output->hw_assign.dpu_id != DPUID(sde_kms))
			continue;

		if (!connector_infos) {
			num_scanouts++;
			continue;
		}

		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
		if (!priv)
			return -ENOMEM;

		attr = &output->attr;
		info = &output->info[0];
		priv->connector_status = attr->connection_status ?
				connector_status_connected :
				connector_status_disconnected;
		priv->base.connector_type =
			virtio_kms_connector_get_type(attr->type,
					i,
					priv->panel_name);
		priv->scanout = i;
		priv->base.possible_crtcs = 1 << num_scanouts;
		if (!output->num_modes) {
			kfree(priv);
			VIRTIO_KMS_ERR("number of modes 0\n");
			return -EINVAL;
		}

		if (output->num_modes > 0) {
			priv->modes = kcalloc(output->num_modes,
					sizeof(struct drm_display_mode),
					GFP_KERNEL);
			if (!priv->modes) {
				VIRTIO_KMS_ERR("Mode allocation failed\n");
				kfree(priv);
				return -ENOMEM;
			}
		}

		for (j = 0; j < output->num_modes; j++) {
			mode = &priv->modes[j];
			mode->hdisplay = info[j].r.width;
			mode->vdisplay = info[j].r.height;
			mode->hsync_end = mode->hdisplay;
			mode->htotal = mode->hdisplay;
			mode->hsync_start = mode->hdisplay;
			mode->vsync_end = mode->vdisplay;
			mode->vtotal = mode->vdisplay;
			mode->vsync_start = mode->vdisplay;
			mode->clock =
				info[j].refresh * mode->vtotal *
				mode->htotal / 1000LL;
			mode->width_mm = attr->width_mm;
			mode->height_mm = attr->height_mm;

			drm_mode_set_name(mode);
		}
		priv->mode_count = output->num_modes;

		if (i < ARRAY_SIZE(disp_order_str))
			priv->base.display_type = disp_order_str[i];
		VIRTIO_KMS_DBG("display(%d) order %s\n",
				i, priv->base.display_type);
		priv->base.connector_funcs = &virtio_conn_ops;//&virtio_conn_helper_funcs;
		priv->base.bridge_funcs = &virtio_bridge_ops;
		priv->kms = kms;

		disp_info = &priv->base.display_info;
		if (output->hw_assign.intf_owner) {
			disp_info->curr_panel_mode = MSM_DISPLAY_VIDEO_MODE;
			disp_info->capabilities = MSM_DISPLAY_CAP_VID_MODE | MSM_DISPLAY_CAP_EDID;
		} else {
			disp_info->curr_panel_mode = MSM_DISPLAY_HYP_MODE;
			disp_info->capabilities = MSM_DISPLAY_HYPERVISOR_MODE | MSM_DISPLAY_CAP_EDID;
		}
		disp_info->intf_type = priv->base.connector_type;
		if (i == 0)
			disp_info->display_type = SDE_CONNECTOR_PRIMARY;
		else
			disp_info->display_type = SDE_CONNECTOR_SECONDARY;
		disp_info->capabilities |= MSM_DISPLAY_CAP_HOT_PLUG;
		disp_info->width_mm = attr->width_mm;
		disp_info->height_mm = attr->height_mm;
		disp_info->is_connected = output->attr.connection_status;
		disp_info->is_master = true;

		j = 0;
		count = 0;
		mask = output->hw_assign.intf_mask;
		while (mask) {
			if (mask & 1)
				disp_info->h_tile_instance[count++] = j + INTF_0;
			mask>>=1;
			j++;
		}
		disp_info->num_of_h_tiles = count;

		count = 0;
		mask = output->hw_assign.dsc_mask;
		while (mask) {
			if (mask & 1)
				count++;
			mask>>=1;
		}
		disp_info->dsc_count = count;

		count = 0;
		mask = output->hw_assign.lm_mask;
		while (mask) {
			if (mask & 1)
				count++;
			mask>>=1;
		}
		disp_info->lm_count = count;

		connector_infos[num_scanouts] = &priv->base;
		connector_infos[num_scanouts]->hw_assign = &output->hw_assign;
		num_scanouts++;
	}
	*connector_num = num_scanouts;

	return 0;
}

static const struct drm_plane_helper_funcs virtio_plane_helper_funcs = {
	//.atomic_update = virtio_kms_plane_atomic_update,
};

static int virtio_kms_get_plane_infos(struct sde_kms *sde_kms,
		struct msm_hyp_plane_info **plane_infos,
		int *plane_num)
{
	struct msm_hyp_kms *hyp_kms = sde_kms->hyp_kms;
	struct virtio_kms *kms = to_virtio_kms(hyp_kms);
	struct virtio_plane_info_priv *priv;
	struct virtio_kms_output *output;
	int i, j, pipe_cnt = 0, scanout_cnt = 0;
	int fmt_idx = 0;
	uint32_t *formats;
	uint32_t drm_format;
	uint32_t num_formats = 0;
	uint32_t plane_type;
	int32_t master_idx = -1;

	if (!kms || !plane_num)
		return -EINVAL;

	for (i = 0; i < kms->num_scanouts; i++) {
		output = &kms->outputs[i];
		if (output->hw_assign.dpu_id != DPUID(sde_kms))
			continue;

		if (!plane_infos) {
			pipe_cnt++;
			scanout_cnt++;
			continue;
		}

		for (j = 0; j < output->plane_cnt; j++) {
			priv = kzalloc(sizeof(struct virtio_plane_info_priv), GFP_KERNEL);
			if (priv == NULL)
				return -ENOMEM;

			if (i == 0 && j == 0)
				plane_type = DRM_PLANE_TYPE_PRIMARY;
			else
				plane_type = DRM_PLANE_TYPE_OVERLAY;

//			plane_type = output->plane_caps[j].plane_type;

			priv->plane_type = plane_type;
			priv->base.plane_type = plane_type;
			priv->scanout = i;
			num_formats = output->plane_caps[j].num_formats;
			formats = output->plane_caps[j].formats;

			if (!num_formats) {
				VIRTIO_KMS_ERR("formats for plane ID %d\
						for scan out %d failed\n",
						j, i);
				kfree(priv);
				return -EINVAL;
			}
			priv->base.format_types = kcalloc(num_formats, sizeof(uint32_t),
							GFP_KERNEL);
			if (priv->base.format_types == NULL) {
				VIRTIO_KMS_ERR("base.format_types Memory allocation failed\n");
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
			if (output->plane_caps[j].plane_type == VIRTIO_QDI_LAYER_GRAPHICS
				|| output->plane_caps[j].plane_type ==
				VIRTIO_QDI_LAYER_OVERLAY)
				priv->base.support_scale = true;

			if (output->plane_caps[j].plane_type == VIRTIO_QDI_LAYER_OVERLAY)
				priv->base.support_csc = true;

			master_idx = output->plane_caps[j].master_plane_id;
			if (master_idx >= 0) {
				VIRTIO_KMS_DBG("Master plane %d master %d\n",
						output->plane_caps[j].plane_id,
						master_idx + pipe_cnt);
				priv->base.support_multirect = true;
				priv->base.support_scale = false;
				priv->base.support_csc = false;
				priv->base.support_rotation = false;
				priv->base.master_plane_index = master_idx + pipe_cnt;
			}

			priv->base.possible_crtcs = 1 << scanout_cnt;
			if (priv->base.support_scale) {
				if (output->plane_caps[j].max_scale > 0 &&
					output->plane_caps[j].min_scale > 0) {
					priv->base.maxdwnscale =
						output->plane_caps[j].min_scale;
					priv->base.maxupscale =
						output->plane_caps[j].max_scale;
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
				output->plane_caps[j].max_width;
			priv->base.max_bandwidth = 4500000000;

			if (!kms->max_sdma_width && master_idx >= 0)
				kms->max_sdma_width = priv->base.max_width;

			priv->base.plane_funcs = &virtio_plane_helper_funcs;
			priv->kms = kms;
			priv->plane_id = output->plane_caps[j].plane_id;
			plane_infos[j + pipe_cnt] = &priv->base;
		}
		pipe_cnt += output->plane_cnt;
		scanout_cnt++;
	}
	*plane_num = pipe_cnt;

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

	VIRTIO_KMS_DBG("max_sdma_width: %d\n",  kms->max_sdma_width);
	if (!constraints)
		return;

	VIRTIO_KMS_DBG("set crtc limit\n");
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
		VIRTIO_KMS_ERR("max_mdp_clk overflow\n");
		tmp_max_mdp_clk = 0;
	} else
		tmp_max_mdp_clk = tmp_max_mdp_clk  * magnification_times * 1000000;

	return tmp_max_mdp_clk;
}

static int virtio_kms_get_crtc_infos(struct sde_kms *sde_kms,
		struct msm_hyp_crtc_info **crtc_infos,
		int *crtc_num)
{
	struct msm_hyp_kms *hyp_kms = sde_kms->hyp_kms;
	struct virtio_kms *kms = to_virtio_kms(hyp_kms);
	struct virtio_crtc_info_priv *priv;
	struct virtio_kms_output *output;
	int i, num_crtc = 0;
	int plane_cnt = 0;
	uint32_t plane_idx = 0;
	if (!kms || !crtc_num)
		return -EINVAL;

	for (i = 0; i < kms->num_scanouts; i++) {
		output = &kms->outputs[i];
		if (output->hw_assign.dpu_id != DPUID(sde_kms))
			continue;

		if (!crtc_infos) {
			num_crtc++;
			continue;
		}

		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
		if (priv == NULL) {
			return -ENOMEM;
		}

		priv->base.max_blendstages = 0;
		for (plane_idx = 0; plane_idx < output->plane_cnt; plane_idx++) {
			if (output->plane_caps[plane_idx].master_plane_id < 0)
				++priv->base.max_blendstages;
		}
		VIRTIO_KMS_DBG("blendstage %d\n", priv->base.max_blendstages);
		priv->base.primary_plane_index = plane_cnt;
		plane_cnt += output->plane_cnt;

		priv->base.max_mdp_clk = drm_calc_max_mdp_clk(hyp_kms);
		if (!priv->base.max_mdp_clk) {
			VIRTIO_KMS_ERR("calc max mdp clk failed\n");
			kfree(priv);
			return -ENOMEM;
		}

		VIRTIO_KMS_DBG("virtio set crtc limit max_mdp_clk: %llu\n", priv->base.max_mdp_clk);

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
		crtc_infos[num_crtc] = &priv->base;
		num_crtc++;
	}
	*crtc_num = num_crtc;

	return 0;
}

static int virtio_kms_get_mode_info(struct sde_kms *sde_kms,
		const struct drm_display_mode *mode,
		struct msm_hyp_mode_info *modeinfo)
{
	struct msm_hyp_kms *hyp_kms = sde_kms->hyp_kms;
	struct virtio_kms *kms = to_virtio_kms(hyp_kms);
	uint32_t max_mdp_clk;

	if (!kms || !mode || !modeinfo)
		return -EINVAL;

	max_mdp_clk = kms->device_info.max_mdp_clk * 1000;
	if (!max_mdp_clk)
		max_mdp_clk = DEFAULT_MAX_MDP_CLK * 1000;

	/*refine topology to avoid sdm check display pixel clk failure*/
	if (mode->clock <= max_mdp_clk)
		modeinfo->num_lm = 1;
	else if (mode->clock / 2 > max_mdp_clk)
		modeinfo->num_lm = 4;
	else
		modeinfo->num_lm = 2;

	VIRTIO_KMS_DBG("virtio modeinfo->num_lm %d\n", modeinfo->num_lm);

	modeinfo->num_enc = 0;
	modeinfo->num_intf = 1;

	return 0;
}

struct sde_mdss_cfg *virtio_kms_hw_catalog_init(struct sde_kms *sde_kms)
{
	int i, j, k;
	struct msm_hyp_kms *hyp_kms = sde_kms->hyp_kms;
	struct sde_mdss_cfg *sde_cfg = sde_kms->catalog;
	struct virtio_kms *kms = to_virtio_kms(hyp_kms);
	struct sde_mdss_cfg *hyp_cfg;
	struct virtio_kms_output *output;

	VIRTIO_KMS_DBG("Enter virtio_kms_hw_catalog_init\n");

	hyp_cfg = kvzalloc(sizeof(*sde_cfg), GFP_KERNEL);
	if (!hyp_cfg)
		return ERR_PTR(-ENOMEM);

	memcpy(hyp_cfg, sde_cfg, sizeof(struct sde_mdss_cfg));

	/* REGDMA (LUTDMA) remain not changed */

	/* Keep MDSS, MDP, VBIF, QDSS, UIDLE to be vritual by default */
	for (i = 0; i < hyp_cfg->mdss_count; i++)
		hyp_cfg->mdss[i].virtual = true;

	for (i = 0; i < hyp_cfg->mdp_count; i++)
		hyp_cfg->mdp[i].virtual = true;

	for (i = 0; i < hyp_cfg->vbif_count; i++)
		hyp_cfg->vbif[i].virtual = true;

	for (i = 0; i < hyp_cfg->qdss_count; i++)
		hyp_cfg->qdss[i].virtual = true;

	hyp_cfg->uidle_cfg.virtual = true;

	/* Clear all other HW blocks */
	hyp_cfg->ctl_count = 0;
	hyp_cfg->sspp_count = 0;
	hyp_cfg->mixer_count = 0;
	hyp_cfg->dspp_count = 0;
	hyp_cfg->ds_count = 0;
	hyp_cfg->pingpong_count = 0;
	hyp_cfg->dsc_count = 0;
	hyp_cfg->vdc_count = 0;
	hyp_cfg->cdm_count = 0;
	hyp_cfg->dnsc_blur_count = 0;
	hyp_cfg->intf_count = 0;
	hyp_cfg->wb_count = 0;
	hyp_cfg->vbif_count = 0;
	hyp_cfg->merge_3d_count = 0;
	hyp_cfg->qdss_count = 0;
	hyp_cfg->dcwb_count = 0;
	hyp_cfg->ad_count = 0;
	hyp_cfg->ltm_count = 0;
	hyp_cfg->rc_count = 0;
	hyp_cfg->spr_count = 0;
	hyp_cfg->demura_count = 0;
	hyp_cfg->aiqe_count = 0;
	hyp_cfg->ai_scaler_count = 0;
	hyp_cfg->abc_count = 0;

	/* Re-link all HW blocks assigned to GVM */
	for (i = 0; i < kms->num_scanouts; i++) {
		output = &kms->outputs[i];
		VIRTIO_KMS_DBG("scanout %d dpu %d ctl %d\n", output->index, output->hw_assign.dpu_id,
				output->hw_assign.ctl_id);

		/* Check DPU id first */
		if (output->hw_assign.dpu_id != DPUID(sde_kms))
			continue;

		/* CTL */
		VIRTIO_KMS_DBG("CTL %d\n", sde_cfg->ctl_count);
		for (j = 0; j < sde_cfg->ctl_count; j++) {
			if (output->hw_assign.ctl_id == sde_cfg->ctl[j].id) {
				hyp_cfg->ctl[hyp_cfg->ctl_count] = sde_cfg->ctl[j];
				if (!output->hw_assign.ctl_owner)
					hyp_cfg->ctl[hyp_cfg->ctl_count].virtual = true;
				hyp_cfg->ctl[hyp_cfg->ctl_count].fixed_ctl_id =
						output->hw_assign.ctl_id;
				hyp_cfg->ctl[hyp_cfg->ctl_count].vq_idx = output->hw_assign.vq_id;
				VIRTIO_KMS_DBG("  HYP_CTL%d=CTL%d  virtual %s  VQ %d\n", hyp_cfg->ctl_count, output->hw_assign.ctl_id,
						hyp_cfg->ctl[hyp_cfg->ctl_count].virtual ? "Yes" : "No",
						output->hw_assign.vq_id);
				hyp_cfg->ctl_count++;
				break;
			}
		}
		VIRTIO_KMS_DBG("HYP_CTL %d\n", hyp_cfg->ctl_count);

		/* LayerMixer */
		VIRTIO_KMS_DBG("LM %d  mask %X\n", sde_cfg->mixer_count, output->hw_assign.lm_mask);
		for (j = 0; j < sde_cfg->mixer_count; j++) {
			if (output->hw_assign.lm_mask & (1 << (sde_cfg->mixer[j].id - LM_0))) {
				hyp_cfg->mixer[hyp_cfg->mixer_count] = sde_cfg->mixer[j];
				if (!output->hw_assign.lm_owner) {
					hyp_cfg->mixer[hyp_cfg->mixer_count].virtual = true;
					/* Shared display shall disable noise layer */
					hyp_cfg->mixer[hyp_cfg->mixer_count].features &= ~SDE_MIXER_NOISE_LAYER;
				}
				hyp_cfg->mixer[hyp_cfg->mixer_count].fixed_ctl_id =
						output->hw_assign.ctl_id;
				hyp_cfg->mixer[hyp_cfg->mixer_count].sblk->zpos_off =
						output->hw_assign.lm_stage_start;
				hyp_cfg->mixer[hyp_cfg->mixer_count].sblk->maxblendstages =
						output->hw_assign.lm_stages;
				if (!(output->hw_assign.dspp_mask & (1 << (sde_cfg->mixer[j].id - LM_0))))
					hyp_cfg->mixer[hyp_cfg->mixer_count].dspp = DSPP_MAX;
				if (!(output->hw_assign.ds_mask & (1 << (sde_cfg->mixer[j].id - LM_0))))
					hyp_cfg->mixer[hyp_cfg->mixer_count].ds = DS_MAX;
				if (!(output->hw_assign.pingpong_mask & (1 << (sde_cfg->mixer[j].id - LM_0))))
					hyp_cfg->mixer[hyp_cfg->mixer_count].pingpong = PINGPONG_MAX;
				if (!output->hw_assign.merge3d_mask)
					hyp_cfg->mixer[hyp_cfg->mixer_count].merge_3d = MERGE_3D_MAX;
				VIRTIO_KMS_DBG("  HYP_LM%d=LM%d->CTL%d  virtual %s  z-order %d+%d\n", hyp_cfg->mixer_count,
						sde_cfg->mixer[j].id, output->hw_assign.ctl_id,
						hyp_cfg->mixer[hyp_cfg->mixer_count].virtual ? "Yes" : "No",
						output->hw_assign.lm_stage_start, output->hw_assign.lm_stages);
				hyp_cfg->mixer_count++;
			}
		}
		VIRTIO_KMS_DBG("HYP_LM %d\n", hyp_cfg->mixer_count);

		/* SSPP */
		VIRTIO_KMS_DBG("SSPP %d  planes %d\n", sde_cfg->sspp_count, output->plane_cnt);
		for (k = 0; k < output->plane_cnt; k++) {
			for (j = 0; j < sde_cfg->sspp_count; j++) {
				// FIXME: how to distinguish DMA/VIG pipe?
				if (output->plane_caps[k].sspp_id == sde_cfg->sspp[j].id) {
					hyp_cfg->sspp[hyp_cfg->sspp_count] = sde_cfg->sspp[j];
					if (output->plane_caps[k].rect_mask & 0x3) {
						// Keep original smart dma feature
						/*
						hyp_cfg->sspp[hyp_cfg->sspp_count].features &=
								~(BIT(SDE_SSPP_SMART_DMA_V1) |
								BIT(SDE_SSPP_SMART_DMA_V2) |
								BIT(SDE_SSPP_SMART_DMA_V2p5));
						hyp_cfg->sspp[hyp_cfg->sspp_count].features |=
								BIT(SDE_SSPP_SMART_DMA_V2p5);
						*/
					} else if (output->plane_caps[k].rect_mask & 0x1) {
						hyp_cfg->sspp[hyp_cfg->sspp_count].features &=
								~(BIT(SDE_SSPP_SMART_DMA_V1) |
								BIT(SDE_SSPP_SMART_DMA_V2) |
								BIT(SDE_SSPP_SMART_DMA_V2p5));
						hyp_cfg->sspp[hyp_cfg->sspp_count].features |=
								BIT(SDE_SSPP_SMART_DMA_REC0_ONLY);
					} else if (output->plane_caps[k].rect_mask & 0x2) {
						hyp_cfg->sspp[hyp_cfg->sspp_count].features &=
								~(BIT(SDE_SSPP_SMART_DMA_V1) |
								BIT(SDE_SSPP_SMART_DMA_V2) |
								BIT(SDE_SSPP_SMART_DMA_V2p5));
						hyp_cfg->sspp[hyp_cfg->sspp_count].features |=
								BIT(SDE_SSPP_SMART_DMA_REC1_ONLY);
					} else {
						VIRTIO_KMS_WARN("plane invalid rect mask %X\n",
								output->plane_caps[k].rect_mask);
					}
					hyp_cfg->sspp[hyp_cfg->sspp_count].fixed_ctl_id =
							output->hw_assign.ctl_id;
					VIRTIO_KMS_DBG("  HYP_SSPP%d=SSPP%d->CTL%d  rect_mask %X  feature %lX\n",
							hyp_cfg->sspp_count, sde_cfg->sspp[j].id, output->hw_assign.ctl_id,
							output->plane_caps[k].rect_mask,
							hyp_cfg->sspp[hyp_cfg->sspp_count].features);
					hyp_cfg->sspp_count++;
					break;
				}
			}
		}
		VIRTIO_KMS_DBG("HYP_SSPP %d\n", hyp_cfg->sspp_count);

		/* DSPP */
		VIRTIO_KMS_DBG("DSPP %d  mask %X\n", sde_cfg->dspp_count, output->hw_assign.dspp_mask);
		for (j = 0; j < sde_cfg->dspp_count; j++) {
			if (output->hw_assign.dspp_mask & (1 << (sde_cfg->dspp[j].id - DSPP_0))) {
				hyp_cfg->dspp[hyp_cfg->dspp_count] = sde_cfg->dspp[j];
				if (!output->hw_assign.dspp_owner)
					hyp_cfg->dspp[hyp_cfg->dspp_count].virtual = true;
				hyp_cfg->dspp[hyp_cfg->dspp_count].fixed_ctl_id =
						output->hw_assign.ctl_id;
				VIRTIO_KMS_DBG("  HYP_DSPP%d=DSPP%d->CTL%d  virtual %s\n", hyp_cfg->sspp_count,
						sde_cfg->dspp[j].id, output->hw_assign.ctl_id,
						hyp_cfg->dspp[hyp_cfg->dspp_count].virtual ? "Yes" : "No");
				hyp_cfg->dspp_count++;
			}
		}
		VIRTIO_KMS_DBG("HYP_DSPP %d\n", hyp_cfg->dspp_count);

		/* DS */
		VIRTIO_KMS_DBG("DS %d  mask %X\n", sde_cfg->ds_count, output->hw_assign.ds_mask);
		for (j = 0; j < sde_cfg->ds_count; j++) {
			if (output->hw_assign.ds_mask & (1 << (sde_cfg->ds[j].id - DS_0))) {
				hyp_cfg->ds[hyp_cfg->ds_count] = sde_cfg->ds[j];
				if (!output->hw_assign.ds_owner)
					hyp_cfg->ds[hyp_cfg->ds_count].virtual = true;
				hyp_cfg->ds[hyp_cfg->ds_count].fixed_ctl_id =
						output->hw_assign.ctl_id;
				VIRTIO_KMS_DBG("  HYP_DS%d=DS%d->CTL%d  virtual %s\n", hyp_cfg->ds_count,
						sde_cfg->ds[j].id, output->hw_assign.ctl_id,
						hyp_cfg->ds[hyp_cfg->ds_count].virtual ? "Yes" : "No");
				hyp_cfg->ds_count++;
			}
		}
		VIRTIO_KMS_DBG("HYP_DS %d\n", hyp_cfg->ds_count);

		/* Pingpong */
		VIRTIO_KMS_DBG("Pingpong %d  mask %X\n", sde_cfg->pingpong_count,
				output->hw_assign.pingpong_mask);
		for (j = 0; j < sde_cfg->pingpong_count; j++) {
			if (output->hw_assign.pingpong_mask & (1 << (sde_cfg->pingpong[j].id - DS_0))) {
				hyp_cfg->pingpong[hyp_cfg->pingpong_count] = sde_cfg->pingpong[j];
				if (!output->hw_assign.pingpong_owner)
					hyp_cfg->pingpong[hyp_cfg->pingpong_count].virtual = true;
				hyp_cfg->pingpong[hyp_cfg->pingpong_count].fixed_ctl_id =
						output->hw_assign.ctl_id;
				VIRTIO_KMS_DBG("  HYP_PP%d=PP%d->CTL%d  virtual %s\n", hyp_cfg->pingpong_count,
						sde_cfg->pingpong[j].id, output->hw_assign.ctl_id,
						hyp_cfg->pingpong[hyp_cfg->pingpong_count].virtual ? "Yes" : "No");
				hyp_cfg->pingpong_count++;
			}
		}
		VIRTIO_KMS_DBG("HYP_DS %d\n", hyp_cfg->pingpong_count);

		/* DSC */
		VIRTIO_KMS_DBG("DSC %d  mask %X\n", sde_cfg->dsc_count, output->hw_assign.dsc_mask);
		for (j = 0; j < sde_cfg->dsc_count; j++) {
			if (output->hw_assign.dsc_mask & (1 << (sde_cfg->dsc[j].id - DSC_0))) {
				hyp_cfg->dsc[hyp_cfg->dsc_count] = sde_cfg->dsc[j];
				if (!output->hw_assign.dsc_owner)
					hyp_cfg->dsc[hyp_cfg->dsc_count].virtual = true;
				hyp_cfg->dsc[hyp_cfg->dsc_count].fixed_ctl_id =
						output->hw_assign.ctl_id;
				VIRTIO_KMS_DBG("  HYP_DSC%d=DSC%d->CTL%d  virtual %s\n", hyp_cfg->dsc_count,
						sde_cfg->dsc[j].id, output->hw_assign.ctl_id,
						hyp_cfg->dsc[hyp_cfg->dsc_count].virtual ? "Yes" : "No");
				hyp_cfg->dsc_count++;
			}
		}
		VIRTIO_KMS_DBG("HYP_DSC %d\n", hyp_cfg->dsc_count);

		/* VDC */
		VIRTIO_KMS_DBG("VDC %d  mask %X\n", sde_cfg->vdc_count, output->hw_assign.vdc_mask);
		for (j = 0; j < sde_cfg->vdc_count; j++) {
			if (output->hw_assign.vdc_mask & (1 << (sde_cfg->vdc[j].id - VDC_0))) {
				hyp_cfg->vdc[hyp_cfg->vdc_count] = sde_cfg->vdc[j];
				if (!output->hw_assign.vdc_owner)
					hyp_cfg->vdc[hyp_cfg->vdc_count].virtual = true;
				hyp_cfg->vdc[hyp_cfg->vdc_count].fixed_ctl_id =
						output->hw_assign.ctl_id;
				VIRTIO_KMS_DBG("  HYP_VDC%d=VDC%d->CTL%d  virtual %s\n", hyp_cfg->vdc_count,
						sde_cfg->vdc[j].id, output->hw_assign.ctl_id,
						hyp_cfg->vdc[hyp_cfg->vdc_count].virtual ? "Yes" : "No");
				hyp_cfg->vdc_count++;
			}
		}
		VIRTIO_KMS_DBG("HYP_VDC %d\n", hyp_cfg->vdc_count);

		/* CDM */
		VIRTIO_KMS_DBG("CDM %d  mask %X\n", sde_cfg->cdm_count, output->hw_assign.cdm_mask);
		for (j = 0; j < sde_cfg->cdm_count; j++) {
			if (output->hw_assign.cdm_mask & (1 << (sde_cfg->cdm[j].id - CDM_0))) {
				hyp_cfg->cdm[hyp_cfg->cdm_count] = sde_cfg->cdm[j];
				if (!output->hw_assign.cdm_owner)
					hyp_cfg->cdm[hyp_cfg->cdm_count].virtual = true;
				hyp_cfg->cdm[hyp_cfg->cdm_count].fixed_ctl_id =
						output->hw_assign.ctl_id;
				VIRTIO_KMS_DBG("HYP_CDM%d=CDM%d->CTL%d  virtual %s\n", hyp_cfg->cdm_count,
						sde_cfg->cdm[j].id, output->hw_assign.ctl_id,
						hyp_cfg->cdm[hyp_cfg->cdm_count].virtual ? "Yes" : "No");
				hyp_cfg->cdm_count++;
			}
		}
		VIRTIO_KMS_DBG("HYP_CDM %d\n", hyp_cfg->cdm_count);

		/* DNSC_BLUR */
		VIRTIO_KMS_DBG("DNSC BLUR %d  mask %X\n", sde_cfg->dnsc_blur_count,
				output->hw_assign.dnsc_blur_mask);
		for (j = 0; j < sde_cfg->dnsc_blur_count; j++) {
			if (output->hw_assign.dnsc_blur_mask & (1 << (sde_cfg->dnsc_blur[j].id - DNSC_BLUR_0))) {
				hyp_cfg->dnsc_blur[hyp_cfg->dnsc_blur_count] = sde_cfg->dnsc_blur[j];
				if (!output->hw_assign.dnsc_blur_owner)
					hyp_cfg->dnsc_blur[hyp_cfg->dnsc_blur_count].virtual = true;
				hyp_cfg->dnsc_blur[hyp_cfg->dnsc_blur_count].fixed_ctl_id =
						output->hw_assign.ctl_id;
				VIRTIO_KMS_DBG("  HYP_DNSC_BLURM%d=DNSC_BLUR%d->CTL%d  virtual %s\n", hyp_cfg->dnsc_blur_count,
						sde_cfg->dnsc_blur[j].id, output->hw_assign.ctl_id,
						hyp_cfg->dnsc_blur[hyp_cfg->dnsc_blur_count].virtual ? "Yes" : "No");
				hyp_cfg->dnsc_blur_count++;
			}
		}
		VIRTIO_KMS_DBG("HYP_DNSC_BLUR %d\n", hyp_cfg->dnsc_blur_count);

		/* INTF */
		VIRTIO_KMS_DBG("INTF %d  mask %X\n", sde_cfg->intf_count, output->hw_assign.intf_mask);
		for (j = 0; j < sde_cfg->intf_count; j++) {
			if (output->hw_assign.intf_mask & (1 << (sde_cfg->intf[j].id - INTF_0))) {
				hyp_cfg->intf[hyp_cfg->intf_count] = sde_cfg->intf[j];
				if (!output->hw_assign.intf_owner)
					hyp_cfg->intf[hyp_cfg->intf_count].virtual = true;
				hyp_cfg->intf[hyp_cfg->intf_count].fixed_ctl_id =
						output->hw_assign.ctl_id;
				VIRTIO_KMS_DBG("  HYP_INTF%d=INTF%d->CTL%d  virtual %s\n", hyp_cfg->cdm_count,
						sde_cfg->intf[j].id, output->hw_assign.ctl_id,
						hyp_cfg->cdm[hyp_cfg->cdm_count].virtual ? "Yes" : "No");
				hyp_cfg->intf_count++;
			}
		}
		VIRTIO_KMS_DBG("HYP_INTF %d\n", hyp_cfg->intf_count);

		/* WB */
		VIRTIO_KMS_DBG("WB %d  mask %X\n", sde_cfg->wb_count, output->hw_assign.wb_mask);
		for (j = 0; j < sde_cfg->wb_count; j++) {
			if (output->hw_assign.wb_mask & (1 << (sde_cfg->wb[j].id - WB_0))) {
				hyp_cfg->wb[hyp_cfg->wb_count] = sde_cfg->wb[j];
				if (!output->hw_assign.wb_owner)
					hyp_cfg->wb[hyp_cfg->wb_count].virtual = true;
				hyp_cfg->wb[hyp_cfg->wb_count].fixed_ctl_id =
						output->hw_assign.ctl_id;
				VIRTIO_KMS_DBG("  HYP_WB%d=WB%d->CTL%d  virtual %s\n", hyp_cfg->wb_count,
						sde_cfg->wb[j].id, output->hw_assign.ctl_id,
						hyp_cfg->wb[hyp_cfg->wb_count].virtual ? "Yes" : "No");
				hyp_cfg->wb_count++;
			}
		}
		VIRTIO_KMS_DBG("HYP_WB %d\n", hyp_cfg->wb_count);

		/* MERGE 3D */
		/* Porpagate PP to Merge3D */
		for (j = 0; j < sde_cfg->pingpong_count / 2; j++)
			if (output->hw_assign.pingpong_mask & (3 << (j * 2)))
				output->hw_assign.merge3d_mask |= 1 << j;
		VIRTIO_KMS_DBG("Merge3D %d  mask %X\n", sde_cfg->merge_3d_count,
				output->hw_assign.merge3d_mask);
		for (j = 0; j < sde_cfg->merge_3d_count; j++) {
			if (output->hw_assign.merge3d_mask & (1 << (sde_cfg->merge_3d[j].id - MERGE_3D_0))) {
				hyp_cfg->merge_3d[hyp_cfg->merge_3d_count] = sde_cfg->merge_3d[j];
				if (!output->hw_assign.merge3d_owner)
					hyp_cfg->merge_3d[hyp_cfg->merge_3d_count].virtual = true;
				hyp_cfg->merge_3d[hyp_cfg->merge_3d_count].fixed_ctl_id =
						output->hw_assign.ctl_id;
				VIRTIO_KMS_DBG("  HYP_3dMerge%d=3dMerge%d->CTL%d  virtual %s\n", hyp_cfg->merge_3d_count,
						sde_cfg->merge_3d[j].id, output->hw_assign.ctl_id,
						hyp_cfg->merge_3d[hyp_cfg->merge_3d_count].virtual ? "Yes" : "No");
				hyp_cfg->merge_3d_count++;
			}
		}
		VIRTIO_KMS_DBG("HYP_3dMerge %d\n", hyp_cfg->merge_3d_count);

		/* CWB */
		VIRTIO_KMS_DBG("DCWB %d\n", sde_cfg->dcwb_count);
		for (j = 0; j < sde_cfg->dcwb_count; j++) {
			// TODO
		}

		/* TODO: other HW blocks */
	}

	sde_kms->perf.max_core_clk_rate = kms->device_info.max_mdp_clk * 1000000LLU;

	VIRTIO_KMS_DBG("Exit virtio_kms_hw_catalog_init\n");

	return hyp_cfg;
}

int virtio_kms_update_hw_reservation(struct sde_kms *sde_kms)
{
	struct msm_drm_private *priv = sde_kms->dev->dev_private;
	struct msm_hyp_kms *hyp_kms = sde_kms->hyp_kms;
	struct sde_mdss_cfg *sde_cfg = sde_kms->catalog;
	struct virtio_kms *kms = to_virtio_kms(hyp_kms);
	struct virtio_kms_output *output;
	struct drm_connector *conn;
	struct drm_connector_list_iter conn_iter;
	struct sde_connector *sde_conn;
	struct sde_encoder_virt *sde_enc;
	struct sde_plane *plane;
	struct sde_hw_pingpong *pingpong;
	struct sde_rm_hw_iter iter;
	int enc_id;
	int ctl_id;
	int dpu_id;
	u32 lm_mask;
	u32 intf_mask;
	int i, j, k;

	VIRTIO_KMS_DBG("Enter virtio_kms_update_hw_reservation\n");
	if (!sde_cfg)
		return -EINVAL;

	/* Re-link all HW blocks assigned to GVM */
	for (i = 0; i < kms->num_scanouts; i++) {
		output = &kms->outputs[i];
		VIRTIO_KMS_DBG("scanout %d dpu %d ctl %d\n", output->index, output->hw_assign.dpu_id,
				output->hw_assign.ctl_id);

		/* Check DPU id first */
		if (output->hw_assign.dpu_id != DPUID(sde_kms))
			continue;

		dpu_id = output->hw_assign.dpu_id;
		ctl_id = output->hw_assign.ctl_id;
		lm_mask = output->hw_assign.lm_mask;
		intf_mask = output->hw_assign.intf_mask;

		enc_id = -1;
		drm_connector_list_iter_begin(sde_kms->dev, &conn_iter);
		drm_for_each_connector_iter(conn, &conn_iter) {
			sde_conn = to_sde_connector(conn);
			if (sde_conn->encoder) {
				sde_enc = to_sde_encoder_virt(sde_conn->encoder);
				for (j = 0; j < sde_enc->num_phys_encs; j ++) {
					if (intf_mask & (1 << (sde_enc->phys_encs[j]->intf_idx - INTF_0))) {
						enc_id = sde_enc->base.base.id;
						break;
					}
				}
			}
		}
		drm_connector_list_iter_end(&conn_iter);
		if (enc_id < 0) {
			VIRTIO_KMS_WARN("Can't find INTF mask %X match encoder for output %d", intf_mask, i);
			continue;
		} else {
 			VIRTIO_KMS_DBG("INTF mask=%X  fixed enc %d\n", lm_mask, enc_id);
		}

		/* CTL */
		for (j = 0; j < sde_cfg->ctl_count; j++) {
			if (ctl_id == sde_cfg->ctl[j].id) {
				sde_cfg->ctl[j].fixed_enc_id = enc_id;
				sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_CTL);
				while (sde_rm_get_hw(&sde_kms->rm, &iter)) {
					if (sde_rm_get_hw_iter_id(&iter) == ctl_id) {
						iter.hw->vq_ctx = get_reg_dma_vq_ctx(dpu_id, ctl_id);
						VIRTIO_KMS_DBG("Update CTL%d  %X  id %d  fixed enc %d  vq_ctx %pK\n",
								j, ctl_id, iter.hw->blk_off, enc_id, iter.hw->vq_ctx);
						break;
					}
				}
			}
		}

		/* LayerMixer */
		for (j = 0; j < sde_cfg->mixer_count; j++) {
			if (lm_mask & (1 << (sde_cfg->mixer[j].id - LM_0))) {
				sde_cfg->mixer[j].fixed_enc_id = enc_id;
				sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_LM);
				while (sde_rm_get_hw(&sde_kms->rm, &iter)) {
					if (sde_rm_get_hw_iter_id(&iter) == sde_cfg->mixer[j].id) {
						iter.hw->vq_ctx = get_reg_dma_vq_ctx(dpu_id, ctl_id);
						VIRTIO_KMS_DBG("Update LM%d  %X  id %d  fixed enc %d  vq_ctx %pK\n",
								j, sde_cfg->mixer[j].id, iter.hw->blk_off, enc_id, iter.hw->vq_ctx);
						break;
					}
				}
			}
		}

		/* SSPP */
		for (j = 0; j < priv->num_planes; j++) {
			for (k = 0; k < output->plane_cnt; k++) {
				plane = to_sde_plane(priv->planes[j]);
				if (plane->pipe == output->plane_caps[k].sspp_id) {
					plane->pipe_hw->hw.vq_ctx = get_reg_dma_vq_ctx(dpu_id, ctl_id);
					VIRTIO_KMS_DBG("Update SSPP%d/%d  %X  id %d  fixed enc %d  vq_ctx %pK\n",
							j, k, output->plane_caps[k].sspp_id, iter.hw->blk_off, enc_id, plane->pipe_hw->hw.vq_ctx);
					break;
				}
			}
		}

		/* DSPP */
		for (j = 0; j < sde_cfg->dspp_count; j++) {
			if (output->hw_assign.dspp_mask & (1 << (sde_cfg->dspp[j].id - DSPP_0))) {
				sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_DSPP);
				while (sde_rm_get_hw(&sde_kms->rm, &iter)) {
					if (sde_rm_get_hw_iter_id(&iter) == sde_cfg->dspp[j].id) {
						iter.hw->vq_ctx = get_reg_dma_vq_ctx(dpu_id, ctl_id);
						VIRTIO_KMS_DBG("Update DSPP%d  %X  id %d  fixed enc %d  vq_ctx %pK\n",
								j, sde_cfg->dspp[j].id, iter.hw->blk_off, enc_id, iter.hw->vq_ctx);
						break;
					}
				}
			}
		}

		/* DS */
		for (j = 0; j < sde_cfg->ds_count; j++) {
			if (output->hw_assign.ds_mask & (1 << (sde_cfg->ds[j].id - DS_0))) {
				sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_DS);
				while (sde_rm_get_hw(&sde_kms->rm, &iter)) {
					if (sde_rm_get_hw_iter_id(&iter) == sde_cfg->ds[j].id) {
						iter.hw->vq_ctx = get_reg_dma_vq_ctx(dpu_id, ctl_id);
						VIRTIO_KMS_DBG("Update DS%d  %X  id %d  fixed enc %d  vq_ctx %pK\n",
								j, sde_cfg->ds[j].id, iter.hw->blk_off, enc_id, iter.hw->vq_ctx);
						break;
					}
				}
			}
		}

		/* Pingpong */
		for (j = 0; j < sde_cfg->pingpong_count; j++) {
			if (output->hw_assign.pingpong_mask & (1 << (sde_cfg->pingpong[j].id - DS_0))) {
				sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_PINGPONG);
				while (sde_rm_get_hw(&sde_kms->rm, &iter)) {
					if (sde_rm_get_hw_iter_id(&iter) == sde_cfg->ds[j].id) {
						iter.hw->vq_ctx = get_reg_dma_vq_ctx(dpu_id, ctl_id);
						VIRTIO_KMS_DBG("Update PP%d  %X  id %d  fixed enc %d  vq_ctx %pK\n",
								j, sde_cfg->ds[j].id, iter.hw->blk_off, enc_id, iter.hw->vq_ctx);
						break;
					}
				}
			}
		}

		/* DSC */
		for (j = 0; j < sde_cfg->dsc_count; j++) {
			if (output->hw_assign.dsc_mask & (1 << (sde_cfg->dsc[j].id - DSC_0))) {
				sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_DSC);
				while (sde_rm_get_hw(&sde_kms->rm, &iter)) {
					if (sde_rm_get_hw_iter_id(&iter) == sde_cfg->dsc[j].id) {
						iter.hw->vq_ctx = get_reg_dma_vq_ctx(dpu_id, ctl_id);
						VIRTIO_KMS_DBG("Update DSC%d  %X  id %d  fixed enc %d  vq_ctx %pK\n",
								j, sde_cfg->dsc[j].id, iter.hw->blk_off, enc_id, iter.hw->vq_ctx);
						break;
					}
				}
			}
		}

		/* VDC */
		for (j = 0; j < sde_cfg->vdc_count; j++) {
			if (output->hw_assign.vdc_mask & (1 << (sde_cfg->vdc[j].id - VDC_0))) {
				sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_VDC);
				while (sde_rm_get_hw(&sde_kms->rm, &iter)) {
					if (sde_rm_get_hw_iter_id(&iter) == sde_cfg->vdc[j].id) {
						iter.hw->vq_ctx = get_reg_dma_vq_ctx(dpu_id, ctl_id);
						VIRTIO_KMS_DBG("Update VDC%d  %X  id %d  fixed enc %d  vq_ctx %pK\n",
								j, sde_cfg->vdc[j].id, iter.hw->blk_off, enc_id, iter.hw->vq_ctx);
						break;
					}
				}
			}
		}

		/* CDM */
		for (j = 0; j < sde_cfg->cdm_count; j++) {
			if (output->hw_assign.cdm_mask & (1 << (sde_cfg->cdm[j].id - CDM_0))) {
				sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_CDM);
				while (sde_rm_get_hw(&sde_kms->rm, &iter)) {
					if (sde_rm_get_hw_iter_id(&iter) == sde_cfg->cdm[j].id) {
						iter.hw->vq_ctx = get_reg_dma_vq_ctx(dpu_id, ctl_id);
						VIRTIO_KMS_DBG("Update CDM%d  %X  id %d  fixed enc %d  vq_ctx %pK\n",
								j, sde_cfg->cdm[j].id, iter.hw->blk_off, enc_id, iter.hw->vq_ctx);
						break;
					}
				}
			}
		}

		/* DNSC_BLUR */
		for (j = 0; j < sde_cfg->dnsc_blur_count; j++) {
			if (output->hw_assign.dnsc_blur_mask & (1 << (sde_cfg->dnsc_blur[j].id - DNSC_BLUR_0))) {
				sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_DNSC_BLUR);
				while (sde_rm_get_hw(&sde_kms->rm, &iter)) {
					if (sde_rm_get_hw_iter_id(&iter) == sde_cfg->dnsc_blur[j].id) {
						iter.hw->vq_ctx = get_reg_dma_vq_ctx(dpu_id, ctl_id);
						VIRTIO_KMS_DBG("Update BLUR%d  %X  id %d  fixed enc %d  vq_ctx %pK\n",
								j, sde_cfg->dnsc_blur[j].id, iter.hw->blk_off, enc_id, iter.hw->vq_ctx);
						break;
					}
				}
			}
		}

		/* Pingpong */
		for (j = 0; j < sde_cfg->pingpong_count; j++) {
			if (output->hw_assign.pingpong_mask & (1 << (sde_cfg->pingpong[j].id - INTF_0))) {
				sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_PINGPONG);
				while (sde_rm_get_hw(&sde_kms->rm, &iter)) {
					if (sde_rm_get_hw_iter_id(&iter) == sde_cfg->pingpong[j].id) {
						iter.hw->vq_ctx = get_reg_dma_vq_ctx(dpu_id, ctl_id);
						VIRTIO_KMS_DBG("Update PP%d  %X  id %d  fixed enc %d  vq_ctx %pK\n",
								j, sde_cfg->pingpong[j].id, iter.hw->blk_off, enc_id, iter.hw->vq_ctx);
						break;
					}
				}
			}
		}

		/* INTF */
		for (j = 0; j < sde_cfg->intf_count; j++) {
			if (output->hw_assign.intf_mask & (1 << (sde_cfg->intf[j].id - INTF_0))) {
				sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_INTF);
				while (sde_rm_get_hw(&sde_kms->rm, &iter)) {
					if (sde_rm_get_hw_iter_id(&iter) == sde_cfg->intf[j].id) {
						iter.hw->vq_ctx = get_reg_dma_vq_ctx(dpu_id, ctl_id);
						VIRTIO_KMS_DBG("Update INTF%d  %X  id %d  fixed enc %d  vq_ctx %pK\n",
								j, sde_cfg->intf[j].id, iter.hw->blk_off, enc_id, iter.hw->vq_ctx);
						break;
					}
				}
			}
		}

		/* WB */
		for (j = 0; j < sde_cfg->wb_count; j++) {
			if (output->hw_assign.wb_mask & (1 << (sde_cfg->wb[j].id - WB_0))) {
				sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_WB);
				while (sde_rm_get_hw(&sde_kms->rm, &iter)) {
					if (sde_rm_get_hw_iter_id(&iter) == sde_cfg->wb[j].id) {
						iter.hw->vq_ctx = get_reg_dma_vq_ctx(dpu_id, ctl_id);
						VIRTIO_KMS_DBG("Update WB%d  %X  id %d  fixed enc %d  vq_ctx %pK\n",
								j, sde_cfg->wb[j].id, iter.hw->blk_off, enc_id, iter.hw->vq_ctx);
						break;
					}
				}
			}
		}

		/* MERGE 3D */
		for (j = 0; j < sde_cfg->merge_3d_count; j++) {
			if (output->hw_assign.merge3d_mask & (1 << (sde_cfg->merge_3d[j].id - MERGE_3D_0))) {
				sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_PINGPONG);
				while (sde_rm_get_hw(&sde_kms->rm, &iter)) {
					pingpong = to_sde_hw_pingpong(iter.hw);
					if (!pingpong->merge_3d)
						continue;
					if (pingpong->merge_3d->idx == sde_cfg->merge_3d[j].id) {
						pingpong->merge_3d->hw.vq_ctx = get_reg_dma_vq_ctx(dpu_id, ctl_id);
						VIRTIO_KMS_DBG("Update 3d Merge%d  %X  id %d  fixed enc %d  vq_ctx %pK\n",
								j, sde_cfg->merge_3d[j].id, iter.hw->blk_off, enc_id, iter.hw->vq_ctx);
						break;
					}
				}
			}
		}

		/* CWB */
		for (j = 0; j < sde_cfg->dcwb_count; j++) {
			// TODO
		}

		/* TODO: other HW blocks */
	}

	return 0;
}

static const struct msm_hyp_kms_funcs virtio_kms_funcs = {
	.get_displays = virtio_kms_get_displays,
	.get_connector_infos = virtio_kms_get_connector_infos,
	.get_plane_infos = virtio_kms_get_plane_infos,
	.get_crtc_infos = virtio_kms_get_crtc_infos,
	.get_mode_info = virtio_kms_get_mode_info,
	.hw_catalog_init = virtio_kms_hw_catalog_init,
	.update_hw_reservation = virtio_kms_update_hw_reservation,
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
			VIRTIO_KMS_ERR("timed out waiting for cap set %d\n", i);
			spin_lock(&kms->display_info_lock);
			kfree(kms->capsets);
			kms->capsets = NULL;
			spin_unlock(&kms->display_info_lock);
			return;
		}
		VIRTIO_KMS_DBG("cap set %d: id %d, max-version %d, max-size %d\n",
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
				VIRTIO_KMS_ERR("plane destroy failed %d\n", plane_id);
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
		VIRTIO_KMS_ERR("Wrong Scanout ID\n");
		goto error;
	}
	VIRTIO_KMS_DBG("scanout init, id: %d\n", scanout);

	output = &kms->outputs[scanout];
	if (kms->has_edid)
		virtio_gpu_cmd_get_edid(kms, scanout);

	rc = virtio_gpu_cmd_get_display_info_ext(kms, scanout);
	if (rc) {
		VIRTIO_KMS_ERR("get_display_info_ext failed %d\n",
				scanout);
		goto error;
	}

	rc = virtio_gpu_cmd_get_scanout_attributes(kms, scanout);
	if (rc) {
		VIRTIO_KMS_ERR("failed to get scanout attributes, rc: %d\n", rc);
		goto error;
	}

	rc = virtio_gpu_cmd_get_scanout_hw_attributes(kms, scanout);
	if (rc) {
		VIRTIO_KMS_ERR("failed to get scanout HW attributes, rc: %d\n", rc);
		goto error;
	}

	rc = virtio_gpu_cmd_get_scanout_planes(kms, scanout);
	if (rc) {
		VIRTIO_KMS_ERR("failed to get scanout planes, rc: %d\n", rc);
		goto error;
	}

	num_planes = output->plane_cnt;
	VIRTIO_KMS_DBG("scanout id: %d, planes num: %d\n", scanout, num_planes);

	if (!num_planes)
		VIRTIO_KMS_ERR("No planes passed\n");

	for (plane = 0; plane < num_planes; plane++)
		output->plane_caps[plane].master_plane_id = -1;

	for (plane = 0; plane < num_planes; plane++) {
		plane_id = output->plane_caps[plane].plane_id;
#if IS_ENABLED(CONFIG_DRM_MSM_HYP)
		rc = virtio_gpu_cmd_get_plane_caps(kms,
				scanout,
				plane_id);
		if (rc) {
			VIRTIO_KMS_ERR("scanout %d virtio_gpu_cmd_get_plane_caps failed %d\n",
				scanout, plane_id);
			goto error;
		}

		rc = virtio_gpu_cmd_get_plane_properties(kms,
				scanout,
				plane_id);
		if (rc) {
			VIRTIO_KMS_ERR("scanout %d plane_properties failed %d\n",
					scanout,
					plane_id);
			goto error;
		}

		rc = virtio_gpu_cmd_get_plane_hw_attributes(kms,
				scanout,
				plane_id);
		if (rc) {
			VIRTIO_KMS_ERR("scanout %d plane_hw_attributes failed %d\n",
					scanout,
					plane_id);
			goto error;
		}
#else
		rc = virtio_gpu_cmd_plane_create(kms,
				scanout,
				plane_id);
		if (rc) {
			VIRTIO_KMS_ERR("Plane creation failed plane-id %d\n",
					plane_id);
			continue;
		}
		rc = virtio_gpu_cmd_get_plane_caps(kms,
				scanout,
				plane_id);
		if (rc) {
			VIRTIO_KMS_ERR("scanout %d virtio_gpu_cmd_get_plane_caps failed %d\n",
				scanout, plane_id);
			goto error;
		}

		rc = virtio_gpu_cmd_get_plane_properties(kms,
				scanout,
				plane_id);
		if (rc) {
			VIRTIO_KMS_ERR("scanout %d plane_properties failed %d\n",
					scanout,
					plane_id);
			goto error;
		}
		/* get the pair plane for the multi rec support*/

		if (output->plane_caps[plane].pair_plane_id) {
			VIRTIO_KMS_DBG("setting the master plane idx %d\n",
					plane);

			output->plane_caps[num_planes].plane_id =
				output->plane_caps[plane].pair_plane_id;
			output->plane_caps[num_planes].master_plane_id = plane;
			num_planes++;
			output->plane_cnt++;
		}
#endif
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

//	virtio_kms_get_capsets(kms, kms->num_capsets);

	rc = virtio_gpu_cmd_get_device_info(kms);
	if (rc) {
		VIRTIO_KMS_ERR("get_device_info failed, rc: %d\n", rc);
		goto error;
	}

	rc = virtio_gpu_cmd_get_device_hw_attributes(kms);
	if (rc) {
		VIRTIO_KMS_ERR("get_device_hw_attributes failed, rc: %d\n", rc);
		goto error;
	}

	rc = virtio_gpu_cmd_get_display_info(kms);
	if (rc) {
		VIRTIO_KMS_ERR("get_display_info failed, rc: %d\n", rc);
		goto error;
	}

	for (scanout = 0; scanout < kms->num_scanouts; scanout++) {
		rc = virtio_kms_scanout_init(kms, scanout);
		if (rc)
			VIRTIO_KMS_ERR("scanout init failed %d\n", scanout);
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
		VIRTIO_KMS_ERR("client_id_str len(%d) is invalid\n", len);
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
	uint32_t client_id = kms->client_id;
	if (!kms)
		VIRTIO_KMS_ERR("kms NULL\n");
	ret = habmm_socket_open(
			&kms->channel[client_id].hab_socket[CHANNEL_CMD],
			kms->mmid_cmd,
			-1,
			0);
	if (!ret) {
		VIRTIO_KMS_INFO("hab socket open mmid %d OK\n", kms->mmid_cmd);

	} else {
		VIRTIO_KMS_ERR("hab open failed mmid %d ret %d\n", kms->mmid_cmd, ret);
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
		VIRTIO_KMS_INFO("hab socket open mmid %d OK\n", kms->mmid_event);
	} else {
		VIRTIO_KMS_ERR("hab open failed mmid %d ret %d\n", kms->mmid_event, ret);
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
		 VIRTIO_KMS_ERR("scanout init failed %d\n", scanout);
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
		VIRTIO_KMS_ERR("Undefine event received %d\n",event_type);
	}
}

static int virtio_kms_bind(struct device *dev,
		struct device *master,
		void *data)
{
	struct virtio_kms *kms = dev_get_drvdata(dev);
	struct drm_device *drm_dev = dev_get_drvdata(master);

	if (!kms) {
		VIRTIO_KMS_ERR("virtio_kms_bind failed ");
		return -EINVAL;
	}

	kms->dev = drm_dev;
	msm_hyp_set_kms(drm_dev, &kms->base);

	VIRTIO_KMS_INFO("virtio_kms_bind done\n");

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

	VIRTIO_KMS_DBG("virtio_kms_probe\n");

	kms = devm_kzalloc(dev, sizeof(*kms), GFP_KERNEL);
	if (!kms)
		return -ENOMEM;

//	ret = _virtio_kms_parse_client_id(dev->of_node, &kms->client_id);
//	if (ret)
//		return ret;

	kms->client_id = 0;

	kms->mmid_cmd = MM_DISP_1;
	kms->mmid_event = MM_DISP_3;

//	ret = _virtio_kms_parse_capsets(dev->of_node, &kms->num_capsets);
//	if (ret)
//		return ret;

	ret = virtio_gpu_hab_open(kms);
	if (ret) {
		VIRTIO_KMS_ERR("hab open failed, ret: %d\n", ret);
		return ret;
	}

	kms->stop = false;
	kthread_run(virtio_gpu_event_kthread, kms, "virtio gpu kthread");

	ret = _virtio_kms_hw_init(kms);
	if (ret) {
		VIRTIO_KMS_ERR("_virtio_kms_hw_init failed, ret: %d\n", ret);
		return ret;
	}

	VIRTIO_KMS_DBG("numbr of scanouts %d for client %x\n", kms->num_scanouts, kms->client_id);
	kms->base.funcs = &virtio_kms_funcs;

	 platform_set_drvdata(pdev, kms);

	ret = component_add(&pdev->dev, &virtio_kms_comp_ops);
	if (ret) {
		VIRTIO_KMS_ERR("component add failed, rc=%d\n", ret);
		return ret;
	}

	VIRTIO_KMS_DBG("virtio_kms_probe done\n");

	return 0;
}

#if (KERNEL_VERSION(6, 12, 0) > LINUX_VERSION_CODE)
static int virtio_kms_remove(struct platform_device *pdev)
#else
static void virtio_kms_remove(struct platform_device *pdev)
#endif
{
	//TODO: implement remove
	int ret;
	struct virtio_kms *kms = platform_get_drvdata(pdev);

	ret = _virtio_kms_hw_deinit(kms);
	if (ret) {
		VIRTIO_KMS_ERR("deinit failed \n");
	}
#if (KERNEL_VERSION(6, 12, 0) > LINUX_VERSION_CODE)
	return 0;
#endif
}

static const struct platform_device_id virtio_kms_id[] = {
	{ "virtio-kms", 0 },
	{ }
};

static const struct of_device_id dt_match[] = {
	{ .compatible = "qcom,virtio-kms" },
	{ }
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

