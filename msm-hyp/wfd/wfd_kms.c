/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

/* Copyright (C) 2014 Red Hat
 * Copyright (C) 2014 Intel Corp.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Daniel Vetter <daniel.vetter@ffwll.ch>
 */

/*
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2008 Red Hat Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 *
 * Authors:
 *      Keith Packard
 *      Eric Anholt <eric@anholt.net>
 *      Dave Airlie <airlied@linux.ie>
 *      Jesse Barnes <jesse.barnes@intel.com>
 */

/*
 * Copyright © 1997-2003 by The XFree86 Project, Inc.
 * Copyright © 2007 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright 2005-2006 Luc Verhaegen
 * Copyright (c) 2001, Andy Ritger  aritger@nvidia.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */

/* Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

#define pr_fmt(fmt)    "[drm] WFD_KMS [%s:%d] " fmt, __func__, __LINE__

#include <linux/sort.h>
#include <linux/habmm.h>
#include <drm/drm_atomic.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_helper.h>
#include "msm_hyp_trace.h"
#include "msm_hyp_utils.h"
#include "wfd_kms.h"
#include "user_os_utils.h"

#define MASTER_PIPE_IDX        0
#define CLIENT_ID_LEN_IN_CHARS 5
#define MAX_MDP_CLK_KHZ        412500
#define MAX_HORZ_DECIMATION    4
#define MAX_VERT_DECIMATION    4
#define SSPP_UNITY_SCALE       1
#define MAX_RECTS_PER_PIPE     2
#define MAX_NUM_LIMIT_PAIRS    16
#define MAX_NUM_STAGES         11
#define MAX_PRE_ROT_HEIGHT_INLINE_ROT_DEFAULT	1088
#define MAX_IMG_WIDTH 0x3fff
#define MAX_IMG_HEIGHT 0x3fff

#define POPULATE_RECT(rect, a, b, c, d, Q16_flag) \
	do {						\
		(rect)->x = (Q16_flag) ? (a) >> 16 : (a);    \
		(rect)->y = (Q16_flag) ? (b) >> 16 : (b);    \
		(rect)->w = (Q16_flag) ? (c) >> 16 : (c);    \
		(rect)->h = (Q16_flag) ? (d) >> 16 : (d);    \
	} while (0)

#define CHECK_LAYER_BOUNDS(offset, size, max_size) \
	(((size) > (max_size)) || ((offset) > ((max_size) - (size))))

static struct color_buffer  color_buffer[MAX_PIPELINES_GVM];

static void get_available_color_buff(int *index)
{
	int i;

	for (i = 0; i < MAX_PIPELINES_GVM; i++) {
		if (!color_buffer[i].valid) {
			*index = i;
			break;
		}
	}
}

struct wfd_kms_rect {
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

static struct hsic_parameter_convertion hsic_parameter_tab[PA_HSIC_MAX] = {
	/* user_min, user_max, reg_min, reg_max, mask, value_max */
	{-180, 180, -1536, 1536, 0xFFF, 4096}, /* HUE, s12 */
	{-100, 100, -128, 127, 0xFF, 256}, /* SAT, s8 */
	{-100, 100, -128, 127, 0xFF, 256}, /* VAL, s8 */
	{-100, 100, -128, 127, 0xFF, 256}, /* CONT, s8 */
	{0, 100, 0, 255, 0xFF, 256}, /* SAT_THRESHOLD, u8 (sat_adjust: [15:8]-thres, [7:0]-sat) */
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

static const struct {
	uint32_t drm_fmt;
	WFDint wfd_fmt;
	WFDint wfd_comp_fmt;
} drm_wfd_formats[] = {
	{ DRM_FORMAT_C8, WFD_FORMAT_BYTE, WFD_FORMAT_BYTE },
	{ DRM_FORMAT_ARGB4444, WFD_FORMAT_RGBA4444, WFD_FORMAT_RGBA4444 },
	{ DRM_FORMAT_XRGB4444, WFD_FORMAT_RGBX4444, WFD_FORMAT_RGBX4444 },
	{ DRM_FORMAT_ARGB1555, WFD_FORMAT_RGBA5551, WFD_FORMAT_RGBA5551 },
	{ DRM_FORMAT_XRGB1555, WFD_FORMAT_RGBX5551, WFD_FORMAT_RGBX5551 },
	{ DRM_FORMAT_RGB565, WFD_FORMAT_RGB565, WFD_FORMAT_RGB565 },
	{ DRM_FORMAT_RGB888, WFD_FORMAT_RGB888, WFD_FORMAT_RGB888 },
	{ DRM_FORMAT_ARGB8888, WFD_FORMAT_RGBA8888, WFD_FORMAT_RGBA8888 },
	{ DRM_FORMAT_XRGB8888, WFD_FORMAT_RGBX8888, WFD_FORMAT_RGBX8888 },
	{ DRM_FORMAT_YVU410, WFD_FORMAT_YVU9, WFD_FORMAT_YVU9 },
	{ DRM_FORMAT_YUV420, WFD_FORMAT_YUV420, WFD_FORMAT_YUV420 },
	{ DRM_FORMAT_NV12, WFD_FORMAT_NV12, WFD_FORMAT_NV12 },
	{ DRM_FORMAT_YVU420, WFD_FORMAT_YV12, WFD_FORMAT_YV12 },
	{ DRM_FORMAT_UYVY, WFD_FORMAT_UYVY, WFD_FORMAT_UYVY },
	{ DRM_FORMAT_YUYV, WFD_FORMAT_YUY2, WFD_FORMAT_YUY2 },
	{ DRM_FORMAT_YVYU, WFD_FORMAT_YVYU, WFD_FORMAT_YVYU },
	{ DRM_FORMAT_VYUY, WFD_FORMAT_V422, WFD_FORMAT_V422 },
	{ DRM_FORMAT_AYUV, WFD_FORMAT_AYUV, WFD_FORMAT_AYUV },
	{ DRM_FORMAT_NV12, WFD_FORMAT_P010, WFD_FORMAT_P010 },
	{ DRM_FORMAT_NV12, WFD_FORMAT_NV12_QC_TP10, WFD_FORMAT_NV12_QC_TP10 },
	{ DRM_FORMAT_ABGR8888, WFD_FORMAT_BGRA8888, WFD_FORMAT_RGBA8888 },
	{ DRM_FORMAT_XBGR8888, WFD_FORMAT_BGRX8888, WFD_FORMAT_RGBA8888 },
	{ DRM_FORMAT_BGR565, WFD_FORMAT_BGR565, WFD_FORMAT_RGB565 },
	{ DRM_FORMAT_ARGB2101010, WFD_FORMAT_RGBA1010102, WFD_FORMAT_RGBA1010102 },
	{ DRM_FORMAT_XRGB2101010, WFD_FORMAT_RGBX1010102, WFD_FORMAT_RGBX1010102 },
	{ DRM_FORMAT_ABGR2101010, WFD_FORMAT_BGRA1010102, WFD_FORMAT_RGBA1010102 },
	{ DRM_FORMAT_XBGR2101010, WFD_FORMAT_BGRX1010102, WFD_FORMAT_RGBA1010102 },
	{ DRM_FORMAT_BGR888, WFD_FORMAT_BGR888, WFD_FORMAT_RGB888 },
	{ 0, 0, 0 },
};
static void *wfd_kms_complete_handler_cb(enum event_types type,
		union event_info *info, void *params);

static int wfd_kms_send_hpd_event(struct wfd_kms *kms, WFDDevice wfd_dev, int port_idx,
		WFDint port_id, bool status, struct drm_connector *connector)
{
	struct drm_device *dev = kms->dev;
	struct wfd_connector_info_priv *priv;
	struct msm_hyp_connector *conn;
	struct drm_display_mode *mode;
	int m;
	WFDint physical_size[2];
	WFDPortMode port_mode[MAX_PORT_MODES_CNT];
	int num_mode;
	int ret = 0;

	if (!connector) {
		pr_err("HPDLOG No connector");
		ret = -EINVAL;
		return ret;
	}
	conn = to_msm_hyp_connector(connector);
	if (!conn) {
		ret = -EINVAL;
		return ret;
	}
	priv = container_of(conn->info, struct wfd_connector_info_priv,
		base);
	if (!priv) {
		ret = -EINVAL;
		return ret;
	}
	pr_debug("HPDLOG port %d name %s connector type id %d, status %d",
			priv->wfd_port_id,
			connector->name,
			connector->connector_type_id,
			connector->status);
	if (priv->wfd_device == wfd_dev && priv->wfd_port_id == port_id) {
		ret = 1;
		/* Handle HPD connect event*/
		if (status == WFD_HPD_CONNECT && priv->connector_status ==
				connector_status_disconnected) {
			pr_debug("HPDLOG port connect: %x, connector name %s, "
					"connector id %d, port %d",
					port_id, connector->name,
					connector->status,
					priv->wfd_port);
			priv->base.possible_crtcs = 1 << port_idx;
			wfdGetPortAttribiv_User(priv->wfd_device,
				priv->wfd_port,
				WFD_PORT_PHYSICAL_SIZE,
				2, physical_size);
			priv->base.display_info.width_mm =
				(uint32_t)physical_size[0];
			priv->base.display_info.height_mm =
				(uint32_t)physical_size[1];

			num_mode = wfdGetPortModes_User(wfd_dev,
				priv->wfd_port,
				0, 0);
			if (!num_mode) {
				ret = -EINVAL;
				return ret;
			}
			pr_debug("HPDLOG GetPortMode %d, %d %d %d\n",
				num_mode, priv->wfd_device,
				wfd_dev, priv->wfd_port);
			priv->mode_count = num_mode;

			wfdGetPortModes_User(priv->wfd_device,
				priv->wfd_port, port_mode,
				num_mode);
			if (num_mode > 0) {
				if (priv->modes) {
					pr_debug("HPDLOG free old priv->modes\n");
					kfree(priv->modes);
				}
				priv->modes = kcalloc(num_mode,
					sizeof(struct drm_display_mode),
					GFP_KERNEL);
				if (!priv->modes) {
					ret = -ENOMEM;
					return ret;
				}
			}

			for (m = 0; m < num_mode; m++) {
				mode = &priv->modes[m];
				mode->hdisplay =
					wfdGetPortModeAttribi_User(
					priv->wfd_device,
					priv->wfd_port,
					port_mode[m],
					WFD_PORT_MODE_WIDTH);
				mode->vdisplay =
					wfdGetPortModeAttribi_User(
					priv->wfd_device,
					priv->wfd_port,
					port_mode[m],
					WFD_PORT_MODE_HEIGHT);
				priv->port_modes[m] = (int *)port_mode[m];
				mode->hsync_end = mode->hdisplay;
				mode->htotal = mode->hdisplay;
				mode->hsync_start = mode->hdisplay;
				mode->vsync_end = mode->vdisplay;
				mode->vtotal = mode->vdisplay;
				mode->vsync_start = mode->vdisplay;
				mode->clock = wfdGetPortModeAttribi_User(
						priv->wfd_device,
						priv->wfd_port,
						port_mode[m],
						WFD_PORT_MODE_REFRESH_RATE) *
					mode->vtotal *
					mode->htotal / 1000LL;
				drm_mode_set_name(mode);
				pr_debug("HPDLOG information, port[%d] "
						"hdisplay[%d] vdisplay[%d] clock[%d] "
						"private[%d] name[%s]",
						priv->wfd_port_id, mode->hdisplay,
						mode->vdisplay, mode->clock,
						priv->port_modes[m], mode->name);
			}
			priv->connector_status = connector_status_connected;
			connector->status = connector_status_connected;
			msm_hyp_send_hpd_event(dev, connector);
		}
		/* Handle HPD disconnect event*/
		else if (status == WFD_HPD_DISCONNECT &&
				priv->connector_status ==
				connector_status_connected) {
			pr_debug("HPDLOG port disconnect: %x, "
					"connector name %s connector id %d",
					port_id, connector->name, connector->status);
			priv->connector_status = connector_status_disconnected;
			connector->status = connector_status_disconnected;
			msm_hyp_send_hpd_event(dev, connector);
		}
		/*Handle repeated event */
		else {
			pr_debug("HPDLOG HPD redundant event port %x, status %d, conn status : %d",
					port_id, status, priv->connector_status);
		}
	}
	return ret;
}

static void wfd_kms_handle_hpd_event(union event_info *info, void *params)
{
	struct wfd_kms *kms = (struct wfd_kms *) params;
	struct drm_device *dev = NULL;
	int i, j;
	struct display_event *disp_event = (struct display_event *)info;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	WFDDevice wfd_dev = WFD_INVALID_HANDLE;
	int ret = 0;

	if (!kms || !info || !params) {
		pr_err("HPDLOG Null dev : %p, kms %p, info %p, params %p",
			dev, kms, info, params);
		return;
	}
	dev = kms->dev;
	pr_debug("HPDLOG dev : %d, port : %x, status : %d\n",
			disp_event->event_infos.hotplug_info.device,
			disp_event->event_infos.hotplug_info.port_id,
			disp_event->event_infos.hotplug_info.status);

	/* Loop to match device handle */
	for (i = 0; i < kms->wfd_device_cnt; i++) {
		wfd_dev = wire_user_get_dev_hdl(kms->wfd_device[i]);
		pr_debug("HPDLOG dev : %d hdl %d devhdl %d",
				kms->wfd_device[i],
				(WFDDevice)(uintptr_t)disp_event->event_infos.hotplug_info.device,
				wfd_dev);
		if (wfd_dev == (WFDDevice)(uintptr_t)disp_event->event_infos.hotplug_info.device) {
			wfd_dev = kms->wfd_device[i];
			pr_debug("HPDLOG dev : %d, %d", wfd_dev,
				disp_event->event_infos.hotplug_info.device);
			/* Loop to match port ID*/
			for (j = 0; j < kms->port_cnt; j++) {
				if (disp_event->event_infos.hotplug_info.port_id ==
						kms->port_ids[j]) {
					pr_debug("HPDLOG port : %x j %d",
						disp_event->event_infos.hotplug_info.port_id, j);
					drm_connector_list_iter_begin(dev, &conn_iter);
					/* Get connector information */
					drm_for_each_connector_iter(
							connector, &conn_iter) {
						ret = wfd_kms_send_hpd_event(kms,
								wfd_dev, j,
								kms->port_ids[j],
								disp_event->event_infos.hotplug_info.status,
								connector);
						if (ret)
							return;
					}
				}
			}
			return;
		}
	} /* Loop to match device handle */
}


struct wire_context {
	struct list_head head;
	struct user_os_utils_init_info init_info;
	bool wire_isr_enable;
	bool wire_isr_stop;
	struct list_head _cb_info_ctx;
	spinlock_t _event_cb_lock;
	struct task_struct *listener_thread;
	bool support_batch_mode;
};

struct wire_commit {
	struct wire_batch_packet *packet;
	u32 alloc_size;
	u32 size;
};

struct wire_device {
	WFDDevice device;
	struct wire_context *ctx;
};

struct wire_port {
	WFDPort port;
	struct wire_commit commit;
};

struct wire_pipeline {
	WFDPipeline pipeline;
	struct wire_port *port;
};

static void _wfd_kms_get_color_buff_idx(
	WFDDevice	 device,
	WFDPipeline  pipeline,
	int		 *index)
{
	int i;

	for (i = 0; i < MAX_PIPELINES_GVM; i++) {
		if ((color_buffer[i].valid) &&
			(color_buffer[i].device == device) &&
			(color_buffer[i].pipeline == pipeline))	{
			*index = i;
			break;
		}
	}
}

static int _wfd_kms_parse_dt(struct device_node *node, u32 *client_id)
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

static int _wfd_kms_connector_get_type(WFDDevice dev,
		WFDPort port, WFDint port_id, char *name)
{
	WFDint port_type;
	int connector_type;

	port_type = wfdGetPortAttribi_User(
			dev,
			port,
			WFD_PORT_TYPE);

	switch (port_type) {
	case WFD_PORT_TYPE_HDMI:
		connector_type = DRM_MODE_CONNECTOR_HDMIA;
		snprintf(name, PANEL_NAME_LEN, "%s_%d", "HDMI", port_id);
		break;
	/* creates primary display if this is the first internal display */
	case WFD_PORT_TYPE_INTERNAL:
	case WFD_PORT_TYPE_DSI:
		connector_type = DRM_MODE_CONNECTOR_DSI;
		snprintf(name, PANEL_NAME_LEN, "%s_%d", "DSI", port_id);
		break;
	case WFD_PORT_TYPE_DISPLAYPORT:
		connector_type = DRM_MODE_CONNECTOR_DisplayPort;
		snprintf(name, PANEL_NAME_LEN, "%s_%d", "DP", port_id);
		break;
	default:
		connector_type = DRM_MODE_CONNECTOR_Unknown;
		snprintf(name, PANEL_NAME_LEN, "%s_%d", "Unknown", port_id);
		break;
	}

	pr_debug("%s - port_type = %x name = %s\n", __func__, port_type, name);

	return connector_type;
}

static bool formats_exist(uint32_t *formats, int count, uint32_t fmt)
{
	int i;

	if (formats == NULL)
		return false;

	for (i = 0; i < count; i++) {
		if (formats[i] == fmt)
			return true;
	}

	return false;
}

static int _wfd_kms_plane_get_format(struct wfd_plane_info_priv *priv)
{
	int i, j, n, ret = 0;
	int format_count = 0;
	WFDint reported_format_count = 0;
	WFDint formats[MAX_PIPELINE_ATTRIBS] = {0};

	reported_format_count = wfdGetPipelineAttribi_User(
			priv->wfd_device,
			priv->wfd_pipeline,
			WFD_PIPELINE_PIXEL_FORMATS_COUNT);

	if (reported_format_count < 1) {
		ret = -EINVAL;
		goto fail;
	}

	if (reported_format_count > MAX_PIPELINE_ATTRIBS)
		reported_format_count = MAX_PIPELINE_ATTRIBS;

	wfdGetPipelineAttribiv_User(priv->wfd_device,
				priv->wfd_pipeline,
				WFD_PIPELINE_PIXEL_FORMATS,
				reported_format_count,
				formats);

	for (i = 0; i < reported_format_count; i++) {
		if (formats[i]) {
			format_count++;
			pr_debug("%s - formats[%d] = %d\n",
					__func__, i, formats[i]);
		} else
			break;
	}

	if (format_count < 1) {
		ret = -EINVAL;
		goto fail;
	}

	priv->base.format_types = kcalloc(format_count, sizeof(uint32_t),
			GFP_KERNEL);
	if (priv->base.format_types == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	n = 0;
	for (i = 0; i < format_count; i++) {
		j = 0;
		while (drm_wfd_formats[j].wfd_fmt || drm_wfd_formats[j].drm_fmt) {
			if (formats[i] == drm_wfd_formats[j].wfd_fmt) {
				/* skip the duplicated format */
				if (!formats_exist(priv->base.format_types, n,
					drm_wfd_formats[j].drm_fmt))
					priv->base.format_types[n++] = drm_wfd_formats[j].drm_fmt;

				break;
			}
			j++;
		}
		if (!drm_wfd_formats[j].wfd_fmt && !drm_wfd_formats[j].drm_fmt)
			pr_debug("%s - formats[%d] = %d is not supported!\n",
				__func__, i, formats[i]);
	}
	priv->base.format_count = n;

fail:
	return ret;
}

static bool _wfd_kms_plane_is_rect_changed(struct drm_plane_state *pre,
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

static bool _wfd_kms_plane_is_csc_matrix_changed(
		struct msm_hyp_plane_state *pre,
		struct msm_hyp_plane_state *cur, WFDint *color_space)
{
	bool ret = false;

	/*
	 * The ctm_coeff[4] value is unique for each CSC matrix. We can use
	 * this to identify the color space associated with each matrix. The
	 * index of each element corresponds to the associated WFD color space
	 * enum value.
	 */
	static const int64_t msm_hyp_csc_unique_coeffs[] = {
		0x0,		/* WFD_COLOR_SPACE_UNCORRECTED */
		0x0,		/* WFD_COLOR_SPACE_SRGB */
		0x0,		/* WFD_COLOR_SPACE_LRGB */
		0x7F9B800000,	/* WFD_COLOR_SPACE_BT601 */
		0x7fa8000000,	/* WFD_COLOR_SPACE_BT601_FULL */
		0x7fc9800000,	/* WFD_COLOR_SPACE_BT709 */
		0x7fd0000000 	/* WFD_COLOR_SPACE_BT709_FULL */
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
	}

	if (color_space && ret) {
		if (msm_hyp_csc_unique_coeffs[WFD_COLOR_SPACE_BT601] ==
				cur->csc.ctm_coeff[unique_coeff_idx])
			*color_space = WFD_COLOR_SPACE_BT601;
		else if (msm_hyp_csc_unique_coeffs[WFD_COLOR_SPACE_BT601_FULL]
				== cur->csc.ctm_coeff[unique_coeff_idx])
			*color_space = WFD_COLOR_SPACE_BT601_FULL;
		else if (msm_hyp_csc_unique_coeffs[WFD_COLOR_SPACE_BT709] ==
				cur->csc.ctm_coeff[unique_coeff_idx])
			*color_space = WFD_COLOR_SPACE_BT709;
		else if (msm_hyp_csc_unique_coeffs[WFD_COLOR_SPACE_BT709_FULL] ==
				cur->csc.ctm_coeff[unique_coeff_idx])
			*color_space = WFD_COLOR_SPACE_BT709_FULL;
		else
			*color_space = WFD_COLOR_SPACE_BT601;
	}

	return ret;
}

static bool _wfd_kms_dma_igc_changed(
		bool pre_en, bool cur_en,
		struct drm_msm_igc_lut *pre_igc,
		struct drm_msm_igc_lut *cur_igc,
		WFD_PipelineDMAConfigBuffType *dma_config)
{
	int i = 0;
	bool changed = false;

	if (pre_en != cur_en)
		changed = true;
	else if (memcmp(pre_igc, cur_igc, sizeof(struct drm_msm_igc_lut)))
		changed = true;

	if (!changed)
		return false;

	if (!dma_config)
		return false;

	dma_config->bIGCEnabled = cur_en ? WFD_TRUE : WFD_FALSE;

	if (dma_config && cur_en)
		for (i = 0; i < WFD_GAMMA_LUT_ENTRIES; i++) {
			dma_config->uIGCLut[i] = cur_igc->c2[i];
			pr_debug("IGC[%d] = %X -> %X\n", i, cur_igc->c0[i], dma_config->uIGCLut[i]);
		}

	return true;
}

static bool _wfd_kms_dma_gc_changed(
		bool pre_en, bool cur_en,
		struct drm_msm_pgc_lut *pre_gc,
		struct drm_msm_pgc_lut *cur_gc,
		WFD_PipelineDMAConfigBuffType *dma_config)
{
	int i = 0;
	int j = 0;
	bool changed = false;

	if (pre_en != cur_en)
		changed = true;
	else if (memcmp(pre_gc, cur_gc, sizeof(struct drm_msm_pgc_lut)))
		changed = true;

	if (!changed)
		return false;

	if (!dma_config)
		return false;

	dma_config->bGCEnabled = cur_en ? WFD_TRUE : WFD_FALSE;

	if (dma_config && cur_en)
		for (i = 0; i < WFD_GAMMA_LUT_ENTRIES; i += 2, j++) {
			dma_config->uGCLut[i] = (cur_gc->c2[j] & 0xFFFF);
			dma_config->uGCLut[i+1] = (cur_gc->c2[j] >> 16);
			pr_debug("GC[%d] = %X -> %X\n", i, cur_gc->c0[j], dma_config->uGCLut[i]);
			pr_debug("GC[%d] = %X -> %X\n", i+1, cur_gc->c0[j], dma_config->uGCLut[i+1]);
		}

	return true;
}

static bool _wfd_kms_plane_is_3d_gamut_changed(
		bool pre_en, bool cur_en,
		struct drm_msm_3d_gamut *pre_gamut,
		struct drm_msm_3d_gamut *cur_gamut,
		WFD_PipelineVIGConfigBuffType *gamut)
{
	int i, j;
	uint32_t scale, offset;
	int gamut_3d_sz = GAMUT_3D_MODE17_TBL_SZ;
	bool changed = false;

	if (pre_en != cur_en)
		changed = true;
	else if (memcmp(pre_gamut, cur_gamut, sizeof(struct drm_msm_3d_gamut)))
		changed = true;

	if (!changed)
		return false;

	if (!gamut)
		return false;

	gamut->bGamutEn = cur_en ? WFD_TRUE : WFD_FALSE;

	if (gamut && cur_en) {
		gamut->bGamutMapEn = cur_gamut->flags & GAMUT_3D_MAP_EN;
		for (i = 0; i < GAMUT_3D_SCALE_OFF_TBL_NUM; i++) {
			for (j = 0; j < GAMUT_3D_SCALE_OFF_SZ; j++) {
				// 28:12 --> scale
				scale = (WFDuint32)cur_gamut->scale_off[i][j] & 0x1ffff000;
				//31:15 -->scale
				scale = scale << 3;
				//offset
				offset = (WFDuint32)cur_gamut->scale_off[i][j] & 0x00fff;
				offset = offset << 4;
				gamut->uNonUniformMapTableEntries[i][j] =
					scale | offset;
			}
		}
		changed = false;

		if (cur_gamut->mode == GAMUT_3D_MODE_5)
			gamut_3d_sz = GAMUT_3D_MODE5_TBL_SZ;
		else if (cur_gamut->mode == GAMUT_3D_MODE_13)
			gamut_3d_sz = GAMUT_3D_MODE13_TBL_SZ;

		for (i = 0; i < WFD_WGM_TABLE_0_ENTRIES; i++) {
			gamut->uGammutTable0Entries[0][i] = cur_gamut->col[0][i].c0;
			gamut->uGammutTable0Entries[2][i] =
							(cur_gamut->col[0][i].c2_c1) & 0XFFFF;
			gamut->uGammutTable0Entries[1][i] =
							(cur_gamut->col[0][i].c2_c1 >> 16);
		}

		for (i = 0; i < WFD_WGM_TABLE_1_ENTRIES; i++) {
			gamut->uGammutTable1Entries[0][i] = cur_gamut->col[1][i].c0;
			gamut->uGammutTable1Entries[2][i] =
							(cur_gamut->col[1][i].c2_c1) & 0XFFFF;
			gamut->uGammutTable1Entries[1][i] =
							(cur_gamut->col[1][i].c2_c1 >> 16);
		}

		for (i = 0; i < WFD_WGM_TABLE_2_ENTRIES; i++) {
			gamut->uGammutTable2Entries[0][i] = cur_gamut->col[2][i].c0;
			gamut->uGammutTable2Entries[2][i] =
							(cur_gamut->col[2][i].c2_c1) & 0XFFFF;
			gamut->uGammutTable2Entries[1][i] =
							(cur_gamut->col[2][i].c2_c1 >> 16);
		}

		for (i = 0; i < WFD_WGM_TABLE_3_ENTRIES; i++) {
			gamut->uGammutTable3Entries[0][i] = cur_gamut->col[3][i].c0;
			gamut->uGammutTable3Entries[2][i] =
							(cur_gamut->col[3][i].c2_c1) & 0XFFFF;
			gamut->uGammutTable3Entries[1][i] =
							(cur_gamut->col[3][i].c2_c1 >> 16);
		}
	}

	return true;
}

static bool _wfd_kms_plane_is_dma_csc_changed(
		bool pre_en, bool cur_en,
		struct sde_drm_csc_v1 *pre_csc,
		struct sde_drm_csc_v1 *cur_csc,
		WFD_PipelineDMAConfigBuffType *dma_config)
{
	int i = 0;
	bool changed = false;

	if (pre_en != cur_en)
		changed = true;
	else if (memcmp(pre_csc, cur_csc, sizeof(struct sde_drm_csc_v1)))
		changed = true;

	if (!changed)
		return false;

	if (!dma_config)
		return false;

	dma_config->bCSCEnabled = cur_en ? WFD_TRUE : WFD_FALSE;
	if (dma_config && cur_en)
		for (i = 0; i < SDE_CSC_MATRIX_COEFF_SIZE; i++) {
			dma_config->uCscMatrix[i / 3][i % 3] = cur_csc->ctm_coeff[i] >> 16;
			pr_debug("csc[%d][%d] = %d\n", i / 3, i % 3, (cur_csc->ctm_coeff[i]) >> 16);
		}

	return true;
}


void _wfd_kms_plane_is_color_changed(
		struct msm_hyp_plane_state *pre,
		struct msm_hyp_plane_state *cur,
		struct wfd_plane_info_priv *priv)
{
	int index = -1;
	int buff_idx = 0;
	int export_id = 0;
	bool gc_changed = false;
	bool igc_changed = false;
	bool csc_changed = false;
	WFD_PipelineVIGConfigBuffType *gamut       = NULL;
	WFD_PipelineDMAConfigBuffType *dma_config  = NULL;
	WFD_PipelineColorConfigBuffType *color_cfg = NULL;

	_wfd_kms_get_color_buff_idx(priv->wfd_device,
		priv->wfd_pipeline,
		&index);

	if (index < 0) {
		pr_err("Unable to find color buffer for this pipe\n");
	}
	else {
		buff_idx = color_buffer[index].curr_buff_in_use;
		if (buff_idx == 0)
			buff_idx = 1;
		else
			buff_idx = 0;

		color_cfg = (WFD_PipelineColorConfigBuffType *)
			color_buffer[index].buffer_info[buff_idx].va;
		export_id =
			color_buffer[index].buffer_info[buff_idx].export_id;
		color_buffer[index].curr_buff_in_use = buff_idx;
	}

	if (!color_cfg) {
		pr_err("VA of buffer is NULL\n");
		return;
	} else {
		if (priv->base.vig_pipe) {
			gamut = &(color_cfg->sVigConfigType);
			if (cur->dirty_flags & MSM_HYP_PLANE_DIRTY_GAMUT &&
				_wfd_kms_plane_is_3d_gamut_changed(
					pre->gamut_en, cur->gamut_en,
					&pre->gamut, &cur->gamut, gamut)) {
				pr_debug("3D LUT updated\n");
				wfdSetPipelineAttribiv_User(
					priv->wfd_device,
					priv->wfd_pipeline,
					WFD_PIPELINE_COLOR_BLOCKS_CONFIG,
					1,
					&export_id);
			}
		} else {
			dma_config = &(color_cfg->sDMAConfig);
			if (cur->dirty_flags & MSM_HYP_PLANE_DIRTY_DMA_CSC)
				csc_changed = _wfd_kms_plane_is_dma_csc_changed(
							pre->dma_csc_en, cur->dma_csc_en,
							&pre->dma_csc, &cur->dma_csc, dma_config);
			if (cur->dirty_flags & MSM_HYP_PLANE_DIRTY_DMA_GC)
				gc_changed = _wfd_kms_dma_gc_changed(
							pre->dma_gc_en, cur->dma_gc_en,
							&pre->dma_gc, &cur->dma_gc, dma_config);
			if (cur->dirty_flags & MSM_HYP_PLANE_DIRTY_DMA_IGC)
				igc_changed = _wfd_kms_dma_igc_changed(
							pre->dma_igc_en, cur->dma_igc_en,
							&pre->dma_igc, &cur->dma_igc, dma_config);
			if (csc_changed || gc_changed || igc_changed) {
				pr_debug("bGCEnabled %d\n", dma_config->bGCEnabled);
				pr_debug("bIGCEnabled %d\n", dma_config->bIGCEnabled);
				pr_debug("bCSCEnabled %d\n", dma_config->bCSCEnabled);
				wfdSetPipelineAttribiv_User(
					priv->wfd_device,
					priv->wfd_pipeline,
					WFD_PIPELINE_COLOR_BLOCKS_CONFIG,
					1,
					&export_id);
			}
		}
	}

}

static int _wfd_kms_format_to_openwfd_format(uint32_t format,
		uint64_t modifier, WFDint *wfd_format, WFDint *wfd_usage)
{
	int i;

	if ((modifier & DRM_FORMAT_MOD_QTI_COMPRESSED) ==
		DRM_FORMAT_MOD_QTI_COMPRESSED ||
		(modifier & DRM_FORMAT_MOD_QTI_COMPRESSED_SECURE) ==
		DRM_FORMAT_MOD_QTI_COMPRESSED_SECURE)
		*wfd_usage = WFD_USAGE_DISPLAY | WFD_USAGE_COMPRESSION;
	else
		*wfd_usage = WFD_USAGE_DISPLAY;

	i = 0;
	while (drm_wfd_formats[i].wfd_fmt || drm_wfd_formats[i].drm_fmt) {
		if (format == drm_wfd_formats[i].drm_fmt) {
			if (*wfd_usage & WFD_USAGE_COMPRESSION)
				*wfd_format = drm_wfd_formats[i].wfd_comp_fmt;
			else
				*wfd_format = drm_wfd_formats[i].wfd_fmt;
			break;
		}
		i++;
	}
	if (!drm_wfd_formats[i].wfd_fmt && !drm_wfd_formats[i].drm_fmt) {
		*wfd_format = WFD_FORMAT_RGBA8888;
		pr_debug("%s - format = %d is not supported, fallback to RGBA8888!\n",
			__func__, format);
	}

	if (format == DRM_FORMAT_NV12) {
		if (((modifier & DRM_FORMAT_MOD_QTI_COMPRESSED) ==
			DRM_FORMAT_MOD_QTI_COMPRESSED &&
			(modifier & DRM_FORMAT_MOD_QTI_DX) ==
			DRM_FORMAT_MOD_QTI_DX &&
			(modifier & DRM_FORMAT_MOD_QTI_TIGHT) ==
			DRM_FORMAT_MOD_QTI_TIGHT) ||
			((modifier & DRM_FORMAT_MOD_QTI_COMPRESSED_SECURE) ==
			DRM_FORMAT_MOD_QTI_COMPRESSED_SECURE &&
			(modifier & DRM_FORMAT_MOD_QTI_DX) ==
			DRM_FORMAT_MOD_QTI_DX &&
			(modifier & DRM_FORMAT_MOD_QTI_TIGHT) ==
			DRM_FORMAT_MOD_QTI_TIGHT))
			*wfd_format = WFD_FORMAT_NV12_QC_TP10;
		else if ((modifier & DRM_FORMAT_MOD_QTI_DX) ==
				DRM_FORMAT_MOD_QTI_DX)
			*wfd_format = WFD_FORMAT_P010;
		else
			*wfd_format = WFD_FORMAT_NV12;
	}

	return 0;
}

static void wfd_kms_destroy_framebuffer(struct drm_framebuffer *framebuffer)
{
	struct msm_hyp_framebuffer *fb = to_msm_hyp_fb(framebuffer);
	struct wfd_framebuffer_priv *fb_priv = container_of(fb->info,
				struct wfd_framebuffer_priv, base);
	int num_planes = fb->base.format->num_planes;
	if (!fb->info)
		return;

	if (fb_priv->wfd_source != WFD_INVALID_HANDLE)
		wfdDestroySource_User(fb_priv->wfd_device,
			fb_priv->wfd_source);

	if (fb_priv->wfd_image != WFD_INVALID_HANDLE)
		wfdDestroyWFDEGLImages_User(
			fb_priv->wfd_device,
			num_planes,
			&fb_priv->wfd_image, NULL);

	kfree(fb_priv);
	fb->info = NULL;
}

static int _wfd_kms_create_image(struct msm_hyp_framebuffer *fb)
{
	struct wfd_framebuffer_priv *fb_priv = container_of(fb->info,
				struct wfd_framebuffer_priv, base);
	WFDErrorCode wfd_err;
	struct dma_buf *dma_bufs[DRM_FORMAT_MAX_PLANES] = {0};
	int ret = 0, i = 0, num_planes = 0;

	num_planes = fb->base.format->num_planes;

	if (fb_priv->wfd_image)
		return 0;

	for (i = 0; i < num_planes; i++) {
		if (!fb->base.obj[i]) {
			pr_err("no bo attached to fb\n");
			return -EINVAL;
		}

		if (fb->base.obj[i]->import_attach) {
			dma_bufs[i] = fb->base.obj[i]->import_attach->dmabuf;
			get_dma_buf(dma_bufs[i]);
		} else if (fb->base.obj[i]->dma_buf) {
			dma_bufs[i] = fb->base.obj[i]->dma_buf;
			get_dma_buf(dma_bufs[i]);
		} else {
			dma_bufs[i] = drm_gem_prime_export(fb->base.obj[i], 0);
			if (IS_ERR(dma_bufs[i])) {
				pr_err("export dma_buf from bo failed\n");
				return PTR_ERR(dma_bufs[i]);
			}
		}
	}
	wfd_err = wfdCreateWFDEGLImagesPreAlloc_User(
			fb_priv->wfd_device,
			fb->base.width,
			fb->base.height,
			fb_priv->wfd_format,
			fb_priv->wfd_usage,
			num_planes,
			fb->base.obj[0]->size,
			&fb_priv->wfd_image,
			(void **)&dma_bufs,
			fb->base.pitches,
			fb->base.offsets,
			0x00);
	if (wfd_err != WFD_ERROR_NONE) {
		pr_err("failed to create wfd image, err = %d\n", wfd_err);
		ret = -EINVAL;
	}
	for (i = 0; i < num_planes; i++)
		dma_buf_put(dma_bufs[i]);

	return ret;
}

static int wfd_kms_get_framebuffer_info(struct msm_hyp_kms *kms,
		struct drm_framebuffer *framebuffer,
		struct msm_hyp_framebuffer_info **fb_info)
{
	struct wfd_framebuffer_priv *fb_priv;
	WFDint wfd_format, wfd_usage;
	int ret;

	ret = _wfd_kms_format_to_openwfd_format(framebuffer->format->format,
			framebuffer->modifier, &wfd_format, &wfd_usage);
	if (ret)
		return ret;

	fb_priv = kzalloc(sizeof(*fb_priv), GFP_KERNEL);
	if (!fb_priv)
		return -ENOMEM;

	fb_priv->base.destroy = wfd_kms_destroy_framebuffer;
	fb_priv->wfd_format = wfd_format;
	fb_priv->wfd_usage = wfd_usage;
	*fb_info = &fb_priv->base;

	return 0;
}

static void _wfd_kms_pipeline_init(struct wfd_kms *kms,
		WFDDevice dev, WFDPort port, int port_idx, struct device *device)
{
	WFDint pipe_ids[MAX_PIPELINE_ATTRIBS];
	WFDint pipe_id;
	WFDint color_buf[WIRE_HOST_MAX_COLOR_BUFF] = {0};
	WFDPipeline pipeline, master_pipeline;
	int i, j, num_pipeline, pipeline_idx, k;
	dma_addr_t dma_handle;
	int32_t *va = NULL;
	int buff_len = 32768;
	struct user_os_utils_mem_info mem;
	int rc = 0;
	struct wire_device *wire_dev = dev;
	void *wire_ctx = wire_dev->ctx->init_info.context;
	int color_buf_idx = -1;

	num_pipeline = wfdGetPortAttribi_User(
			dev,
			port,
			WFD_PORT_PIPELINE_ID_COUNT);

	if (num_pipeline <= 0)
		return;

	wfdGetPortAttribiv_User(dev,
		port, WFD_PORT_BINDABLE_PIPELINE_IDS,
		num_pipeline, pipe_ids);

	for (i = num_pipeline - 1; i >= 0; i--) {
		master_pipeline = WFD_INVALID_PIPELINE_ID;
		for (j = 0; j < MAX_RECTS_PER_PIPE; j++) {
			if (j == MASTER_PIPE_IDX) {
				pipe_id = pipe_ids[i];
			} else if (master_pipeline) {
				pipe_id = wfdGetPipelineAttribi_User(
					dev,
					master_pipeline,
					WFD_PIPELINE_VIRTUAL_PIPE_ID);
			}

			if (pipe_id == WFD_INVALID_PIPELINE_ID)
				continue;

			pipeline = wfdCreatePipeline_User(
					dev,
					pipe_id, NULL);
			if (pipeline == WFD_INVALID_HANDLE)
				continue;

			pipeline_idx = kms->pipeline_cnt[port_idx];
			kms->pipelines[port_idx][pipeline_idx] = pipeline;

			if (master_pipeline)
				kms->master_idx[port_idx][pipeline_idx] =
						pipeline_idx - 1;
			else
				kms->master_idx[port_idx][pipeline_idx] = -1;

			kms->pipeline_cnt[port_idx]++;

			if (j == MASTER_PIPE_IDX)
				master_pipeline = pipeline;

			color_buf[0] = 0;
			color_buf[1] = 0;

			color_buf_idx = -1;
			get_available_color_buff(&color_buf_idx);

			for (k = 0; k < WIRE_HOST_MAX_COLOR_BUFF; k++) {

				va = dma_alloc_coherent(device, buff_len,
							&dma_handle, GFP_KERNEL);
				if (!va) {
					pr_err("Memory allocation failed\n");
				} else {
					pr_debug("VA for buffer allocation is %p\n", va);
					memset(va, 0x00, buff_len);
					mem.size = buff_len;
					mem.buffer = va;
					mem.shmem_type = HAB_EXPORT_ID;

					rc = user_os_utils_shmem_export(wire_ctx, &mem, HABMM_EXP_MEM_TYPE_DMA);
					if (rc) {
						pr_err("Export failed\n");
					} else {
						color_buf[k] = (i32)mem.shmem_id;
						pr_debug("Export passed for %d\n", mem.shmem_id);
					}
				}

				if (color_buf_idx >= 0) {
					pr_debug("Filling data at location %d\n", color_buf_idx);
					color_buffer[color_buf_idx].valid = 1;
					color_buffer[color_buf_idx].device = dev;
					color_buffer[color_buf_idx].pipeline = pipeline;
					color_buffer[color_buf_idx].buffer_info[k].valid = 1;
					color_buffer[color_buf_idx].buffer_info[k].va = va;
					color_buffer[color_buf_idx].buffer_info[k].export_id = color_buf[k];
					color_buffer[color_buf_idx].buffer_info[k].dmabuf_handle =
								&dma_handle;
					wfdSetPipelineAttribiv_User(
						dev,
						pipeline,
						WFD_PIPELINE_COLOR_CONFIG_BUFFER,
						WIRE_HOST_MAX_COLOR_BUFF,
						color_buf);
				} else {
					pr_err("Color buffer not available\n");
				}
			}
		}
	}
}

static int wfd_kms_port_cmp(const void *a, const void *b)
{
	struct wfd_kms_port *pa = (struct wfd_kms_port *)a;
	struct wfd_kms_port *pb = (struct wfd_kms_port *)b;
	int rc = 0;

	rc = pa->wfd_port_id - pb->wfd_port_id;

	return rc;
}

static int _wfd_kms_hw_init(struct wfd_kms *kms, struct device *dev)
{
	WFDint wfd_ids[MAX_DEVICE_CNT];
	WFDint num_dev = 0;
	WFDDevice wfd_dev = WFD_INVALID_HANDLE;
	WFDint attribs[3];
	WFDint wfd_port_ids[MAX_PORT_CNT];
	WFDPort port;
	int i, j, num_port, port_idx;
	int rc;
	int all_ports_cnt = 0;
	struct wfd_kms_port wfd_kms_ports[MAX_PORT_CNT] = {{0, 0, 0}};
	char marker_buff[MARKER_BUFF_LENGTH] = {0};

	attribs[0] = WFD_DEVICE_CLIENT_TYPE;
	attribs[1] = kms->client_id;
	attribs[2] = WFD_NONE;

	rc = wire_user_init(kms->client_id, WIRE_INIT_EVENT_SUPPORT);
	if (rc) {
		pr_err("failed to init wire user for client %x\n", kms->client_id);
		return rc;
	}

	snprintf(marker_buff, sizeof(marker_buff),
		"kernel_fe: wire client %x ready", kms->client_id);
	place_marker(marker_buff);

	/* open a open WFD device */
	num_dev = wfdEnumerateDevices_User(NULL, 0, attribs);
	if (!num_dev) {
		pr_info("wfdEnumerateDevices_User - failed for client %x!\n",
				kms->client_id);
		/* TODO: Debug and add back wire_user_deinit(kms->client_id, 0x00) */
	}

	wfdEnumerateDevices_User(wfd_ids, num_dev, attribs);

	for (j = 0; j < num_dev; j++) {
		wfd_dev = wfdCreateDevice_User(wfd_ids[j], attribs);
		if (wfd_dev == WFD_INVALID_HANDLE) {
			pr_debug("wfdCreateDevice_User - failed\n");
			continue;
		}

		kms->wfd_device[kms->wfd_device_cnt] = wfd_dev;
		kms->wfd_device_cnt++;

		num_port = wfdEnumeratePorts_User(wfd_dev, NULL, 0, NULL);

		wfdEnumeratePorts_User(wfd_dev, wfd_port_ids, num_port, NULL);

		for (i = 0; i < num_port; i++) {
			port = wfdCreatePort_User(
					wfd_dev, wfd_port_ids[i], NULL);
			if (port == WFD_INVALID_HANDLE)
				continue;

			wfd_kms_ports[all_ports_cnt].wfd_port = port;
			wfd_kms_ports[all_ports_cnt].wfd_device = wfd_dev;
			wfd_kms_ports[all_ports_cnt].wfd_port_id = wfd_port_ids[i];
			all_ports_cnt++;
		}
	}

	if (!kms->wfd_device_cnt)
		pr_info("can't find valid WFD device\n");

	/* Sort wfd_kms_port by wfd_port_id */
	if (all_ports_cnt > 1)
		sort(wfd_kms_ports, all_ports_cnt, sizeof(wfd_kms_ports[0]),
				wfd_kms_port_cmp, NULL);

	for (port_idx = 0; port_idx < all_ports_cnt; port_idx++) {
		kms->ports[port_idx] = wfd_kms_ports[port_idx].wfd_port;
		kms->port_ids[port_idx] = wfd_kms_ports[port_idx].wfd_port_id;
		kms->port_devs[port_idx] = wfd_kms_ports[port_idx].wfd_device;
		kms->port_cnt++;

		 _wfd_kms_pipeline_init(kms, kms->port_devs[port_idx],
				kms->ports[port_idx], port_idx, dev);
	}

	return 0;
}

static int wfd_kms_connector_detect_ctx(struct drm_connector *connector,
			  struct drm_modeset_acquire_ctx *ctx,
			  bool force)
{
	struct msm_hyp_connector *c = to_msm_hyp_connector(connector);
	struct wfd_connector_info_priv *priv = container_of(c->info,
			struct wfd_connector_info_priv, base);

	return priv->connector_status;
}

static int wfd_kms_connector_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *m;
	struct msm_hyp_connector *c_conn = to_msm_hyp_connector(connector);
	struct wfd_connector_info_priv *priv = container_of(c_conn->info,
			struct wfd_connector_info_priv, base);
	uint32_t i;

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
	return priv->mode_count;
}

static struct drm_encoder *
wfd_kms_connector_best_encoder(struct drm_connector *connector)
{
	struct msm_hyp_connector *c_conn = to_msm_hyp_connector(connector);

	return &c_conn->encoder;
}

static const struct drm_connector_helper_funcs wfd_connector_helper_funcs = {
	.detect_ctx = wfd_kms_connector_detect_ctx,
	.get_modes = wfd_kms_connector_get_modes,
	.best_encoder = wfd_kms_connector_best_encoder,
};

static void wfd_kms_bridge_mode_set(struct drm_bridge *drm_bridge,
		const struct drm_display_mode *mode,
		const struct drm_display_mode *adjusted_mode)
{
	struct msm_hyp_connector *connector = container_of(drm_bridge,
			struct msm_hyp_connector, bridge);
	struct wfd_connector_info_priv *priv = container_of(connector->info,
			struct wfd_connector_info_priv, base);
	WFDPortMode wfd_port_mode = WFD_INVALID_HANDLE;
	int i;

	for (i = 0; i < priv->mode_count; i++) {
		mode = &priv->modes[i];
		if ((adjusted_mode->hdisplay == mode->hdisplay) &&
		    (adjusted_mode->vdisplay == mode->vdisplay)) {
			wfd_port_mode = priv->port_modes[i];
			break;
		}
	}

	wfdSetPortMode_User(priv->wfd_device, priv->wfd_port,
			wfd_port_mode);
}

static void wfd_kms_bridge_enable(struct drm_bridge *drm_bridge)
{
	struct msm_hyp_connector *connector = container_of(drm_bridge,
			struct msm_hyp_connector, bridge);
	struct wfd_connector_info_priv *priv = container_of(connector->info,
			struct wfd_connector_info_priv, base);
	static bool first_frame = true;

	wfdSetPortAttribi_User(priv->wfd_device,
			priv->wfd_port,
			WFD_PORT_POWER_MODE,
			WFD_POWER_MODE_ON);

	if (first_frame) {
		place_marker("kernel_fe: Set port attribute POWER ON");
		first_frame = false;
	}
}

static void wfd_kms_bridge_disable(struct drm_bridge *drm_bridge)
{
	struct msm_hyp_connector *connector = container_of(drm_bridge,
			struct msm_hyp_connector, bridge);
	struct wfd_connector_info_priv *priv = container_of(connector->info,
			struct wfd_connector_info_priv, base);

	wfdSetPortAttribi_User(priv->wfd_device,
			priv->wfd_port,
			WFD_PORT_POWER_MODE,
			WFD_POWER_MODE_OFF);

	wfdSetPortMode_User(priv->wfd_device,
			priv->wfd_port,
			WFD_INVALID_HANDLE);
}

static const struct drm_bridge_funcs wfd_bridge_ops = {
	.enable       = wfd_kms_bridge_enable,
	.disable      = wfd_kms_bridge_disable,
	.mode_set     = wfd_kms_bridge_mode_set,
};

static int wfd_kms_get_connector_infos(struct msm_hyp_kms *kms,
		struct msm_hyp_connector_info **connector_infos,
		int *connector_num)
{
	struct wfd_kms *wfd_kms = to_wfd_kms(kms);
	struct drm_device *ddev = wfd_kms->dev;
	struct wfd_connector_info_priv *priv;
	struct drm_display_mode *mode;
	WFDint data[4];
	WFDint port_connected;
	WFDint host_cap;
	WFDint physical_size[2];
	WFDPortMode port_mode[MAX_PORT_MODES_CNT];
	int num_mode;
	int i, j, ret = 0;

	if (!connector_infos) {
		*connector_num = wfd_kms->port_cnt;
		return 0;
	}

	if (!wfd_kms->wfd_device_cnt)
		return 0;

	wfdGetDeviceAttribiv_User(wfd_kms->wfd_device[0],
		WFD_DEVICE_MIN_MAX_WIDTH_HEIGHT, 4, data);

	ddev->mode_config.min_width = data[0];
	ddev->mode_config.max_width = data[1];
	ddev->mode_config.min_height = data[2];
	ddev->mode_config.max_height = data[3];

	host_cap = wfdGetDeviceAttribi_User(wfd_kms->wfd_device[0],
			WFD_DEVICE_HOST_CAPABILITIES);
	wire_user_set_host_capabilities(wfd_kms->wfd_device[0], host_cap);

	for (i = 0; i < wfd_kms->port_cnt; i++) {
		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
		if (!priv)
			return -ENOMEM;

		priv->wfd_device = wfd_kms->port_devs[i];
		priv->wfd_port = wfd_kms->ports[i];
		priv->wfd_port_id = wfd_kms->port_ids[i];
		priv->wfd_port_idx = i;

		priv->base.panel_orientation = wfdGetPortAttribi_User(
				priv->wfd_device,
				priv->wfd_port,
				WFD_PORT_ROTATION);

		priv->base.connector_type = _wfd_kms_connector_get_type(
				priv->wfd_device,
				priv->wfd_port, priv->wfd_port_id,
				priv->panel_name);

		port_connected = wfdGetPortAttribi_User(
				priv->wfd_device,
				priv->wfd_port,
				WFD_PORT_ATTACHED);

		priv->connector_status = port_connected ?
				connector_status_connected :
				connector_status_disconnected;

		wfdGetPortAttribiv_User(priv->wfd_device,
					priv->wfd_port,
					WFD_PORT_PHYSICAL_SIZE,
					2, physical_size);

		priv->base.display_info.width_mm =
				(uint32_t)physical_size[0];
		priv->base.display_info.height_mm =
				(uint32_t)physical_size[1];

		priv->base.possible_crtcs = 1 << i;

		num_mode = wfdGetPortModes_User(priv->wfd_device,
						priv->wfd_port,
						0, 0);
		if (!num_mode) {
			ret = -EINVAL;
			break;
		}

		priv->mode_count = num_mode;
		priv->port_modes = kzalloc(priv->mode_count *
					sizeof(WFDPortMode), GFP_KERNEL);
		if (!priv->port_modes) {
			ret = -ENOMEM;
			break;
		}

		wfdGetPortModes_User(priv->wfd_device,
					priv->wfd_port,
					port_mode,
					num_mode);

		if (num_mode > 0) {
			priv->modes = kcalloc(num_mode,
					sizeof(struct drm_display_mode),
					GFP_KERNEL);
			if (!priv->modes) {
				ret = -ENOMEM;
				break;
			}
		}

		for (j = 0; j < num_mode; j++) {
			mode = &priv->modes[j];
			mode->hdisplay =
					wfdGetPortModeAttribi_User(
						priv->wfd_device,
						priv->wfd_port,
						port_mode[j],
						WFD_PORT_MODE_WIDTH);
			mode->vdisplay =
					wfdGetPortModeAttribi_User(
						priv->wfd_device,
						priv->wfd_port,
						port_mode[j],
						WFD_PORT_MODE_HEIGHT);

			priv->port_modes[j] = port_mode[j];
			mode->hsync_end = mode->hdisplay;
			mode->htotal = mode->hdisplay;
			mode->hsync_start = mode->hdisplay;
			mode->vsync_end = mode->vdisplay;
			mode->vtotal = mode->vdisplay;
			mode->vsync_start = mode->vdisplay;
			mode->clock = wfdGetPortModeAttribi_User(
						priv->wfd_device,
						priv->wfd_port,
						port_mode[j],
						WFD_PORT_MODE_REFRESH_RATE) *
					mode->vtotal *
					mode->htotal / 1000LL;
			drm_mode_set_name(mode);
			/* TODO : Need to remove this condition with proper
			 * fix */
			if (mode->hdisplay == 0) {
				pr_err("HPDLOG information, port[%d],"
					"hdisplay[%d] vdisplay[%d] clock[%d],"
					"private[%d] name[%s]", priv->wfd_port_id,
					mode->hdisplay, mode->vdisplay,
					mode->clock, priv->port_modes[j], mode->name);
				priv->connector_status = connector_status_disconnected;
			}
		}

		if (i < ARRAY_SIZE(disp_order_str))
			priv->base.display_type = disp_order_str[i];

		priv->base.connector_funcs = &wfd_connector_helper_funcs;
		priv->base.bridge_funcs = &wfd_bridge_ops;
		connector_infos[i] = &priv->base;
	}

	return ret;
}

static void _wfd_kms_set_crtc_limit(struct wfd_kms *wfd_kms,
		struct wfd_crtc_info_priv *crtc_priv)
{
	struct limit_constraints *constraints = NULL;
	struct limit_val_pair *pair;
	char buf[16];
	int i;

	for (i = 0; i < ARRAY_SIZE(constraints_table); i++) {
		if (constraints_table[i].sdma_width == wfd_kms->max_sdma_width) {
			constraints = &constraints_table[i];
			break;
		}
	}

	if (!constraints)
		return;

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

static bool _wfd_kms_crtc_is_hsic_changed(struct msm_hyp_crtc_state *pre,
		struct msm_hyp_crtc_state *cur)
{
	bool ret = false;

	if (pre && cur) {
		if ((cur->cp_hsic.pa_hsic.flags != pre->cp_hsic.pa_hsic.flags) ||
			(cur->cp_hsic.pa_hsic.contrast != pre->cp_hsic.pa_hsic.contrast) ||
			(cur->cp_hsic.pa_hsic.hue != pre->cp_hsic.pa_hsic.hue) ||
			(cur->cp_hsic.pa_hsic.saturation != pre->cp_hsic.pa_hsic.saturation) ||
			(cur->cp_hsic.pa_hsic.value != pre->cp_hsic.pa_hsic.value)) {
			ret = true;
		}
	}
	return ret;
}

/* It's called by wfd_kms_convert_hsic to round the out_val of HSIC */
static int hsic_round(int out_val, int temp, u32 hsic_type)
{
	int round = 0;

	if (out_val >= 0) {
		temp = temp * hsic_parameter_tab[hsic_type].user_max * 10 /
				hsic_parameter_tab[hsic_type].reg_max;
		if (temp - out_val * 10 >= 5)
			round = 1;
	} else {
		temp = (hsic_parameter_tab[hsic_type].val_max - temp) *
				hsic_parameter_tab[hsic_type].user_min * 10 /
				hsic_parameter_tab[hsic_type].reg_min;
		if (temp + out_val * 10 >= 5)
			round = 1;
	}

	return round;
}

/*
 * SDM already transfer hsic parameters from user to reg, but pvm host has its
 * own hsic range from user to reg, then convert the reg range of GVM to user
 * range of PVM.
 * Todo, Need to reconsider it if SDM is not used.
 */
static int wfd_kms_convert_hsic(u32 input, u32 hsic_type)
{
	int out_val = 0;
	int temp = 0;

	if (hsic_type == PA_HSIC_SATURATION_THRESHOLD) {
		temp = (input >> 8) & hsic_parameter_tab[PA_HSIC_SATURATION_THRESHOLD].hsic_mask;
		out_val = temp * hsic_parameter_tab[hsic_type].user_max /
				hsic_parameter_tab[hsic_type].reg_max;
		out_val += hsic_round(out_val, temp, hsic_type);
	} else {
		temp = input & hsic_parameter_tab[hsic_type].hsic_mask;
		/* positive */
		if (temp < hsic_parameter_tab[hsic_type].val_max / 2) {
			out_val = temp * hsic_parameter_tab[hsic_type].user_max /
					hsic_parameter_tab[hsic_type].reg_max;
			out_val += hsic_round(out_val, temp, hsic_type);
		} else { /* negative */
			out_val = -((hsic_parameter_tab[hsic_type].val_max - temp) *
					hsic_parameter_tab[hsic_type].user_min /
					hsic_parameter_tab[hsic_type].reg_min);
			out_val -= hsic_round(out_val, temp, hsic_type);
		}
	}

	return out_val;
}

static void wfd_kms_crtc_atomic_begin(struct msm_hyp_kms *kms,
		struct drm_crtc *crtc,
		struct drm_atomic_state *atomic_state)
{
	struct msm_hyp_crtc *c = to_msm_hyp_crtc(crtc);
	struct wfd_crtc_info_priv *priv = container_of(c->info, struct wfd_crtc_info_priv, base);
	struct drm_crtc_state *old_state;
	struct msm_hyp_crtc_state *old_cstate, *new_cstate;
	__u64 flags = 0;
	struct WFDPortHISCSetType hsic = {0};

	/* PP feature, HSIC */
	old_state = drm_atomic_get_old_crtc_state(atomic_state, crtc);
	old_cstate = to_msm_hyp_crtc_state(old_state);
	new_cstate = to_msm_hyp_crtc_state(crtc->state);

	if (_wfd_kms_crtc_is_hsic_changed(old_cstate, new_cstate)) {
		flags = new_cstate->cp_hsic.pa_hsic.flags;
		if (flags & PA_HSIC_HUE_ENABLE)
			hsic.hueLevel = wfd_kms_convert_hsic(
					new_cstate->cp_hsic.pa_hsic.hue,
					PA_HSIC_HUE);
		if (flags & PA_HSIC_CONT_ENABLE)
			hsic.contrastLevel = wfd_kms_convert_hsic(
					new_cstate->cp_hsic.pa_hsic.contrast,
					PA_HSIC_CONTRAST);
		if (flags & PA_HSIC_VAL_ENABLE)
			hsic.intensityLevel = wfd_kms_convert_hsic(
					new_cstate->cp_hsic.pa_hsic.value,
					PA_HSIC_VALUE);
		if (flags & PA_HSIC_SAT_ENABLE) {
			hsic.saturationLevel = wfd_kms_convert_hsic(
					new_cstate->cp_hsic.pa_hsic.saturation,
					PA_HSIC_SATURATION);
			hsic.satThreshold = wfd_kms_convert_hsic(
					new_cstate->cp_hsic.pa_hsic.saturation,
					PA_HSIC_SATURATION_THRESHOLD);
		}

		if (flags & (PA_HSIC_HUE_ENABLE |
			PA_HSIC_SAT_ENABLE |
			PA_HSIC_VAL_ENABLE |
			PA_HSIC_CONT_ENABLE))
			hsic.enabled = true;

		pr_debug("Set HSIC: enable:%d, h:%d, s:%d, v:%d, c:%d, s_t:%d\n",
			hsic.enabled, hsic.hueLevel, hsic.saturationLevel,
			hsic.intensityLevel, hsic.contrastLevel, hsic.satThreshold);

		wfdSetPortAttribiv_User(
			priv->wfd_device,
			priv->wfd_port,
			WFD_PORT_PA_HSIC,
			6,
			(WFDint *)&hsic);
	}
}

static int wfd_kms_get_crtc_infos(struct msm_hyp_kms *kms,
		struct msm_hyp_crtc_info **crtc_infos,
		int *crtc_num)
{
	struct wfd_kms *wfd_kms = to_wfd_kms(kms);
	struct wfd_crtc_info_priv *priv;
	int pipe_cnt = 0;
	int i, ret;

	if (!kms || !crtc_num)
		return -EINVAL;

	if (!crtc_infos) {
		*crtc_num = wfd_kms->port_cnt;
		return 0;
	}

	for (i = 0; i < wfd_kms->port_cnt; i++) {
		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
		if (priv == NULL) {
			ret = -ENOMEM;
			break;
		}

		priv->wfd_device = wfd_kms->port_devs[i];
		priv->wfd_port = wfd_kms->ports[i];
		priv->wfd_port_id = wfd_kms->port_ids[i];
		priv->wfd_port_idx = i;

		priv->base.max_blendstages = wfd_kms->pipeline_cnt[i];

		if (priv->base.max_blendstages > MAX_NUM_STAGES) {
			priv->base.max_blendstages = MAX_NUM_STAGES;
			pr_debug("plane count %d exceeds the maximum blend stage %d of crtc\n",
					wfd_kms->pipeline_cnt[i], priv->base.max_blendstages);
		}

		priv->base.primary_plane_index = pipe_cnt;
		pipe_cnt += wfd_kms->pipeline_cnt[i];

		/* these values should read from host */
		priv->base.max_mdp_clk = 412500000LL;
		priv->base.qseed_type = "qseed3";
		priv->base.smart_dma_rev = "smart_dma_v2p5";
		priv->base.has_hdr = true;
		priv->base.max_bandwidth_low = 9600000000LL;
		priv->base.max_bandwidth_high = 9600000000LL;
		priv->base.has_src_split = true;

		_wfd_kms_set_crtc_limit(wfd_kms, priv);

		crtc_infos[i] = &priv->base;
	}

	return 0;
}

static void wfd_kms_plane_atomic_update(struct drm_plane *plane,
		struct drm_atomic_state *old_atomic_state)
{
	struct msm_hyp_plane *p = to_msm_hyp_plane(plane);
	struct wfd_plane_info_priv *priv = container_of(p->info,
			struct wfd_plane_info_priv, base);
	struct msm_hyp_framebuffer *fb = NULL;
	struct wfd_framebuffer_priv *fb_priv;
	struct drm_plane_state *old_state;
	struct msm_hyp_plane_state *old_pstate, *new_pstate;
	WFDint src_rect[4];
	WFDint dst_rect[4];
	WFDint color_space;
	WFDint trans_val = WFD_TRANSPARENCY_NONE;

	old_state = drm_atomic_get_old_plane_state(old_atomic_state, plane);
	new_pstate = to_msm_hyp_plane_state(plane->state);
	old_pstate = to_msm_hyp_plane_state(old_state);

	if (old_state->crtc != plane->state->crtc) {
		wfdBindPipelineToPort_User(
				priv->wfd_device,
				priv->wfd_port,
				priv->wfd_pipeline);
	}

	if (!plane->state->crtc || !plane->state->fb)
		wfdBindSourceToPipeline_User(
			priv->wfd_device,
			priv->wfd_pipeline,
			WFD_INVALID_HANDLE,
			WFD_TRANSITION_AT_VSYNC,
			NULL);
	else if (old_state->fb != plane->state->fb) {
		fb = to_msm_hyp_fb(plane->state->fb);
		fb_priv = container_of(fb->info,
				struct wfd_framebuffer_priv, base);

		if (!fb_priv->wfd_source) {
			WFDint attrib_list[3] = {WFD_SOURCE_TRANSLATION_MODE,
					WFD_SOURCE_TRANSLATION_UNSECURED,
					WFD_NONE};

			fb_priv->wfd_device = priv->wfd_device;

			if (_wfd_kms_create_image(fb)) {
				pr_err("failed to create wfd image\n");
				return;
			}

			attrib_list[1] = (new_pstate->fb_mode ==
					SDE_DRM_FB_SEC) ?
					WFD_SOURCE_TRANSLATION_SECURED :
					WFD_SOURCE_TRANSLATION_UNSECURED;
			if ((plane->state->fb->modifier &
				DRM_FORMAT_MOD_QTI_SECURE) ==
				DRM_FORMAT_MOD_QTI_SECURE ||
				(plane->state->fb->modifier &
				DRM_FORMAT_MOD_QTI_COMPRESSED_SECURE) ==
				DRM_FORMAT_MOD_QTI_COMPRESSED_SECURE)
				attrib_list[1] =
					WFD_SOURCE_TRANSLATION_SECURED;

			fb_priv->wfd_source = wfdCreateSourceFromImage_User(
					priv->wfd_device,
					priv->wfd_pipeline,
					fb_priv->wfd_image, attrib_list);

			if (!fb_priv->wfd_source) {
				pr_err("failed to create wfd source\n");
				return;
			}
		}

		wfdBindSourceToPipeline_User(
			priv->wfd_device,
			priv->wfd_pipeline,
			fb_priv->wfd_source,
			WFD_TRANSITION_AT_VSYNC,
			NULL);
	}

	if (_wfd_kms_plane_is_rect_changed(
		old_state, plane->state, true)) {
		src_rect[0] = plane->state->src_x >> 16;
		src_rect[1] = plane->state->src_y >> 16;
		src_rect[2] = plane->state->src_w >> 16;
		src_rect[3] = plane->state->src_h >> 16;

		fb = to_msm_hyp_fb(plane->state->fb);
		if (fb != NULL) {
			/* rect dimension must not be greater than source image dimension */
			if ((fb->base.width >= src_rect[2]) && (fb->base.height >= src_rect[3])) {
				wfdSetPipelineAttribiv_User(
					priv->wfd_device,
					priv->wfd_pipeline,
					WFD_PIPELINE_SOURCE_RECTANGLE,
					4,
					src_rect);
			}
		}
	}

	if (_wfd_kms_plane_is_rect_changed(
		old_state, plane->state, false)) {
		dst_rect[0] = plane->state->crtc_x;
		dst_rect[1] = plane->state->crtc_y;
		dst_rect[2] = plane->state->crtc_w;
		dst_rect[3] = plane->state->crtc_h;
		wfdSetPipelineAttribiv_User(
			priv->wfd_device,
			priv->wfd_pipeline,
			WFD_PIPELINE_DESTINATION_RECTANGLE,
			4,
			dst_rect);
	}

	if (old_state->rotation != plane->state->rotation || !priv->committed) {
		wfdSetPipelineAttribi_User(
			priv->wfd_device,
			priv->wfd_pipeline,
			WFD_PIPELINE_ROTATION,
			ilog2(plane->state->rotation & DRM_MODE_ROTATE_MASK) * 90);

		wfdSetPipelineAttribi_User(
			priv->wfd_device,
			priv->wfd_pipeline,
			WFD_PIPELINE_FLIP,
			(plane->state->rotation & DRM_MODE_REFLECT_Y) ? true : false);

		wfdSetPipelineAttribi_User(
			priv->wfd_device,
			priv->wfd_pipeline,
			WFD_PIPELINE_MIRROR,
			(plane->state->rotation & DRM_MODE_REFLECT_X) ? true : false);
	}

	/* special plane properties */
	if (old_pstate->alpha != new_pstate->alpha || !priv->committed) {
		wfdSetPipelineAttribi_User(
			priv->wfd_device,
			priv->wfd_pipeline,
			WFD_PIPELINE_GLOBAL_ALPHA,
			new_pstate->alpha);
	}

	if (_wfd_kms_plane_is_csc_matrix_changed(
		old_pstate, new_pstate, &color_space)) {
		wfdSetPipelineAttribi_User(
			priv->wfd_device,
			priv->wfd_pipeline,
			WFD_PIPELINE_COLOR_SPACE,
			color_space);
	}

	_wfd_kms_plane_is_color_changed(old_pstate, new_pstate, priv);

	if (old_pstate->blend_op != new_pstate->blend_op || !priv->committed) {
		switch (new_pstate->blend_op) {
		case SDE_DRM_BLEND_OP_NOT_DEFINED:
			trans_val = WFD_TRANSPARENCY_NONE;
			break;
		case SDE_DRM_BLEND_OP_OPAQUE:
			trans_val = WFD_TRANSPARENCY_GLOBAL_ALPHA;
			break;
		/* extend to support coverage by introducing a new alpha definition */
		case SDE_DRM_BLEND_OP_COVERAGE:
			trans_val = WFD_TRANSPARENCY_GLOBAL_ALPHA |
				WFD_TRANSPARENCY_SOURCE_ALPHA_NONPREMULTIPLIED;
			break;
		case SDE_DRM_BLEND_OP_PREMULTIPLIED:
			trans_val = WFD_TRANSPARENCY_SOURCE_ALPHA |
				WFD_TRANSPARENCY_GLOBAL_ALPHA;
			break;
		default:
		/* follow kernel-metal, use opaque as default */
			trans_val = WFD_TRANSPARENCY_GLOBAL_ALPHA;
			break;
		}
		wfdSetPipelineAttribi_User(
			priv->wfd_device,
			priv->wfd_pipeline,
			WFD_PIPELINE_TRANSPARENCY_ENABLE,
			trans_val);
	}

	// All updates have been sent to host, clear dirty flags
	new_pstate->dirty_flags = 0;

	priv->committed = true;
}

static bool wfd_kms_plane_enabled(const struct drm_plane_state *state)
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

static int _wfd_kms_plane_rot_atomic_check(struct drm_plane *plane,
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

	if ((rotation & DRM_MODE_ROTATE_180) ||
		(rotation & DRM_MODE_ROTATE_270)) {
		pr_err("invalid rotation transform must be simplified 0x%x\n",
				rotation);
		ret = -EINVAL;
		goto exit;
	}

	if (rotation & DRM_MODE_ROTATE_90) {
		struct wfd_kms_rect src;
		bool q16_data = true;
		/* check if the slave pipline is using */
		drm_for_each_plane(slave_plane, plane->dev) {
			slave_hyp_plane = to_msm_hyp_plane(slave_plane);

			if ((plane == slave_hyp_plane->primary_plane)
					&& wfd_kms_plane_enabled(slave_plane->state)) {
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

		/* check for valid formats supported by inline rot */
		//TODO, get this ubwc supported format information from QNX
		if ((state->fb->modifier & DRM_FORMAT_MOD_QTI_COMPRESSED)
				&& (is_ubwc_supported_format(state->fb->format->format)))
			format_support =
				is_inline_rot_supported_format(state->fb->format->format);

		if (!format_support) {
			pr_err("invalid format for inline rot\n");
			ret = -EINVAL;
			goto exit;
		}
	}

	state->rotation = rotation;
exit:
	return ret;
}

static int _wfd_kms_plane_sspp_atomic_check(struct drm_plane *plane,
		struct drm_atomic_state *atomic_state)
{
	bool q16_data = true;
	int ret = 0;
	struct wfd_kms_rect src, dst;
	struct drm_plane_state *state = NULL;
	struct drm_framebuffer *fb = NULL;
	u32 width = 0;
	u32 height = 0;
	/* YUV is 2, RGB is 1 */
	u32 min_src_size = 1;

	state = drm_atomic_get_new_plane_state(atomic_state, plane);

	/* src values are in Q16 fixed point, convert to integer */
	POPULATE_RECT(&src, state->src_x, state->src_y,
			state->src_w, state->src_h, q16_data);
	POPULATE_RECT(&dst, state->crtc_x, state->crtc_y, state->crtc_w,
			state->crtc_h, !q16_data);

	fb = state->fb;
	width = fb ? state->fb->width : 0x0;
	height = fb ? state->fb->height : 0x0;

	pr_debug("plane%d sspp:%x/%dx%d/%4.4s/%llx\n",
			plane->base.id,
			state->rotation,
			width, height,
			fb ? (char *) &state->fb->format->format : 0x0,
			fb ? state->fb->modifier : 0x0);
	pr_debug("src:%dx%d %d,%d crtc:%dx%d+%d+%d\n",
			state->src_w >> 16, state->src_h >> 16,
			state->src_x >> 16, state->src_y >> 16,
			state->crtc_w, state->crtc_h,
			state->crtc_x, state->crtc_y);

	/* check src bounds */
	if (width > MAX_IMG_WIDTH || height > MAX_IMG_HEIGHT ||
			src.w < min_src_size || src.h < min_src_size ||
			CHECK_LAYER_BOUNDS(src.x, src.w, width) ||
			CHECK_LAYER_BOUNDS(src.y, src.h, height)) {
		pr_err("invalid source %u, %u, %ux%u\n",
			src.x, src.y, src.w, src.h);
		ret = -EINVAL;
	/* min dst support */
	} else if (dst.w < 0x1 || dst.h < 0x1) {
		pr_err("invalid dest rect %u, %u, %ux%u\n",
				dst.x, dst.y, dst.w, dst.h);
		ret = -EINVAL;
	}

	return ret;
}

static int wfd_kms_plane_atomic_check(struct drm_plane *plane,
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
	if (!wfd_kms_plane_enabled(state))
		goto exit;

	ret = _wfd_kms_plane_rot_atomic_check(plane, atomic_state);
	if (ret)
		goto exit;

	ret = _wfd_kms_plane_sspp_atomic_check(plane, atomic_state);

exit:
	return ret;
}

static const struct drm_plane_helper_funcs wfd_plane_helper_funcs = {
	.atomic_update = wfd_kms_plane_atomic_update,
	.atomic_check = wfd_kms_plane_atomic_check,
};

static int wfd_kms_get_port_plane_infos(struct msm_hyp_kms *kms,
		WFDint port_idx, int base_idx,
		struct msm_hyp_plane_info **plane_infos)
{
	struct wfd_kms *wfd_kms = to_wfd_kms(kms);
	struct wfd_plane_info_priv *priv;
	int i, ret = 0, master_idx;
	WFDint val[2] = {0, 0};
	WFDint val_i[2] = {0, 0};
	WFDint max_width = wfd_kms->dev->mode_config.max_width;
	WFDRotationSupport support_rot = WFD_ROTATION_SUPPORT_NONE;

	for (i = 0; i < wfd_kms->pipeline_cnt[port_idx]; i++) {
		priv = kzalloc(sizeof(*priv), GFP_KERNEL);
		if (priv == NULL) {
			ret = -ENOMEM;
			break;
		}

		priv->wfd_pipeline = wfd_kms->pipelines[port_idx][i];
		priv->wfd_port = wfd_kms->ports[port_idx];
		priv->wfd_device = wfd_kms->port_devs[port_idx];
		priv->wfd_type = wfdGetPipelineAttribi_User(
				priv->wfd_device,
				priv->wfd_pipeline,
				WFD_PIPELINE_TYPE);
		/* query rotation capability */
		support_rot = wfdGetPipelineAttribi_User(
				priv->wfd_device,
				priv->wfd_pipeline,
				WFD_PIPELINE_ROTATION_SUPPORT);
		if (support_rot == WFD_ROTATION_SUPPORT_LIMITED)
			priv->base.support_rotation = true;

		if (i == 0)
			priv->base.plane_type = DRM_PLANE_TYPE_PRIMARY;
		else if (priv->wfd_type ==
				WFD_QDI_LAYER_CURSOR)
			priv->base.plane_type = DRM_PLANE_TYPE_CURSOR;
		else
			priv->base.plane_type = DRM_PLANE_TYPE_OVERLAY;

		ret = _wfd_kms_plane_get_format(priv);
		if (ret)
			break;

		if (priv->wfd_type == WFD_QDI_LAYER_GRAPHICS
				|| priv->wfd_type ==
				WFD_QDI_LAYER_OVERLAY)
			priv->base.support_scale = true;

		if (priv->wfd_type == WFD_QDI_LAYER_OVERLAY)
			priv->base.support_csc = true;

		if (priv->base.support_csc && priv->base.support_scale)
			priv->base.vig_pipe = true;

		master_idx = wfd_kms->master_idx[port_idx][i];
		/* overwrite value for virtual pipeline */
		if (master_idx >= 0) {
			priv->base.support_multirect = true;
			priv->base.support_scale = false;
			priv->base.support_csc = false;
			priv->base.support_rotation = false;
			priv->base.master_plane_index = master_idx + base_idx;
		}

		priv->base.possible_crtcs = 1 << port_idx;

		if (priv->base.support_scale) {
			wfdGetPipelineAttribiv_User(priv->wfd_device,
					priv->wfd_pipeline,
					WFD_PIPELINE_SCALE_RANGE,
					2,
					val);
			if (val[0] > 0 && val[1] > 0) {
				priv->base.maxdwnscale = (u32)(val[0]);
				priv->base.maxupscale = (u32)(val[1]);
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

		wfdGetPipelineAttribiv_User(priv->wfd_device,
				priv->wfd_pipeline,
				WFD_PIPELINE_MAX_SOURCE_SIZE,
				2,
				val_i);

		priv->base.max_width = (val_i[0] > 0) ? val_i[0] : max_width;
		/* if could, get the bandwidth from backend */
		priv->base.max_bandwidth = 4500000000;

		if (!wfd_kms->max_sdma_width && master_idx >= 0)
			wfd_kms->max_sdma_width = priv->base.max_width;

		priv->base.plane_funcs = &wfd_plane_helper_funcs;

		plane_infos[i + base_idx] = &priv->base;
	}

	return ret;
}

static int wfd_kms_get_plane_infos(struct msm_hyp_kms *kms,
		struct msm_hyp_plane_info **plane_infos,
		int *plane_num)
{
	struct wfd_kms *wfd_kms = to_wfd_kms(kms);
	int i, ret, pipe_cnt = 0;

	if (!kms || !plane_num)
		return -EINVAL;

	if (!plane_infos) {
		*plane_num = 0;
		for (i = 0; i < wfd_kms->port_cnt; i++)
			*plane_num += wfd_kms->pipeline_cnt[i];
		return 0;
	}

	for (i = 0; i < wfd_kms->port_cnt; i++) {
		ret = wfd_kms_get_port_plane_infos(kms, i, pipe_cnt,
				plane_infos);
		if (ret)
			return ret;

		pipe_cnt += wfd_kms->pipeline_cnt[i];
	}

	return 0;
}

static int wfd_kms_get_mode_info(struct msm_hyp_kms *kms,
		const struct drm_display_mode *mode,
		struct msm_hyp_mode_info *modeinfo)
{
	modeinfo->num_lm = (mode->clock > MAX_MDP_CLK_KHZ) ? 2 : 1;
	modeinfo->num_enc = 0;
	modeinfo->num_intf = 1;

	return 0;
}

static int wfd_kms_plane_cmp(const void *a, const void *b)
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

static void _wfd_kms_plane_zpos_adj_fe(struct drm_crtc *crtc,
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
	struct msm_hyp_plane_state *sorted_pstate[MAX_PIPELINE_CNT];
	struct wfd_plane_info_priv *priv;
	int i;

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
				wfd_kms_plane_cmp, NULL);
		for (i = 0; i < cnt; i++) {
			p = to_msm_hyp_plane(sorted_pstate[i]->base.plane);
			priv = container_of(p->info,
					struct wfd_plane_info_priv, base);
			wfdSetPipelineAttribi_User(
					priv->wfd_device,
					priv->wfd_pipeline,
					WFD_PIPELINE_LAYER,
					i + 1);
		}
	}
}

static void _wfd_kms_req_vblank(struct drm_crtc *crtc);

static void *wfd_kms_complete_handler_cb(enum event_types type,
	union event_info *info, void *params)
{
	struct drm_crtc *crtc = params;
	struct display_event *disp_event = (struct display_event *)info;
	struct msm_hyp_crtc *c = to_msm_hyp_crtc(crtc);
	struct wfd_crtc_info_priv *priv;
	static bool first_frame = true;

	if (type != DISPLAY_EVENT || !info || !params)
		return NULL;

	if (disp_event->type == COMMIT_COMPLETE) {
		msm_hyp_crtc_commit_done(crtc);

		if (first_frame) {
			place_marker("kernel_fe: Fisrt commit envent done");
			first_frame = false;
		}
	} else if (disp_event->type == VSYNC) {
		msm_hyp_crtc_vblank_done(crtc);

		/* if vblank is still enabled, schedule another one */
		priv = container_of(c->info, struct wfd_crtc_info_priv, base);
		if (priv->vblank_enable)
			_wfd_kms_req_vblank(crtc);
	} else if (disp_event->type == HPD) {
		pr_debug("HPDLOG event type %d", type);
		wfd_kms_handle_hpd_event(info, params);
	}

	return NULL;
}

static void _wfd_kms_req_vblank(struct drm_crtc *crtc)
{
	struct msm_hyp_crtc *c = to_msm_hyp_crtc(crtc);
	struct wfd_crtc_info_priv *priv = container_of(c->info,
			struct wfd_crtc_info_priv, base);
	struct display_event disp_event;
	struct cb_info cb_info;

	disp_event.event_infos.display_id = priv->wfd_port_id;
	disp_event.type = VSYNC;
	cb_info.cb = wfd_kms_complete_handler_cb;
	cb_info.user_data = crtc;

	pr_debug("register vsync event id=%d\n",
			disp_event.event_infos.display_id);

	wire_user_register_event_listener(priv->wfd_device,
			DISPLAY_EVENT,
			(union event_info *)&disp_event,
			&cb_info);

	wire_user_request_cb(priv->wfd_device,
			DISPLAY_EVENT,
			(union event_info *)&disp_event);
}

static void wfd_kms_commit(struct msm_hyp_kms *kms,
			struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct msm_hyp_crtc *c;
	struct wfd_crtc_info_priv *priv;
	struct display_event disp_event;
	struct cb_info cb_info;
	int i;
	static bool first_frame = true;

	if (!old_state)
		return;

	HYP_ATRACE_BEGIN(__func__);

	if (first_frame) {
		place_marker("kernel_fe: First commit kickoff");
		first_frame = false;
	}

	for_each_new_crtc_in_state(old_state, crtc, crtc_state, i) {
		c = to_msm_hyp_crtc(crtc);
		priv = container_of(c->info, struct wfd_crtc_info_priv, base);

		if (crtc_state->active)
			_wfd_kms_plane_zpos_adj_fe(crtc, old_state);

		disp_event.type = COMMIT_COMPLETE;
		disp_event.event_infos.display_id = priv->wfd_port_id;
		cb_info.cb = wfd_kms_complete_handler_cb;
		cb_info.user_data = crtc;

		wire_user_register_event_listener(
				priv->wfd_device,
				DISPLAY_EVENT,
				(union event_info *)&disp_event,
				&cb_info);

		wfdDeviceCommitExt_User(
				priv->wfd_device,
				WFD_COMMIT_ENTIRE_PORT,
				priv->wfd_port,
				WFD_COMMIT_ASYNC);
	}
	HYP_ATRACE_END(__func__);
}

static void wfd_kms_enable_vblank(struct msm_hyp_kms *kms,
		struct drm_crtc *crtc)
{
	struct msm_hyp_crtc *c = to_msm_hyp_crtc(crtc);
	struct wfd_crtc_info_priv *priv = container_of(c->info,
			struct wfd_crtc_info_priv, base);

	if (!crtc)
		return;

	priv->vblank_enable = true;

	_wfd_kms_req_vblank(crtc);
}

static void wfd_kms_disable_vblank(struct msm_hyp_kms *kms,
		struct drm_crtc *crtc)
{
	struct msm_hyp_crtc *c = to_msm_hyp_crtc(crtc);
	struct wfd_crtc_info_priv *priv;

	if (!crtc)
		return;

	priv = container_of(c->info, struct wfd_crtc_info_priv, base);

	priv->vblank_enable = false;
}

static void wfd_kms_free_connector_port_modes(struct msm_hyp_connector *c_conn)
{
	struct wfd_connector_info_priv *priv;

	if (!c_conn)
		return;

	priv = container_of(c_conn->info,
		struct wfd_connector_info_priv, base);

	if (priv->port_modes) {
		kfree(priv->port_modes);
		priv->port_modes = NULL;
	}
}

static void wfd_kms_register_event(struct msm_hyp_kms *kms)
{
	struct wfd_kms *wfd_kms = to_wfd_kms(kms);
	struct display_event disp_event;
	struct cb_info cb_info;
	int i = 0;

	for (i = 0; i < wfd_kms->wfd_device_cnt; i++) {
		pr_debug("HPDLOG %s i : %d, dev : %d\n", __func__,
				i, wfd_kms->wfd_device[i]);
		disp_event.type = HPD;
		disp_event.event_infos.display_id = i;
		cb_info.cb = wfd_kms_complete_handler_cb;
		cb_info.user_data = wfd_kms;
		wire_user_register_event_listener(wfd_kms->wfd_device[i],
				DISPLAY_EVENT,
				(union event_info *)&disp_event,
				&cb_info);
		wfdRegisterHotplugEvent_User(wfd_kms->wfd_device[i]);
	}
}

static const struct msm_hyp_kms_funcs wfd_kms_funcs = {
	.get_connector_infos = wfd_kms_get_connector_infos,
	.get_plane_infos = wfd_kms_get_plane_infos,
	.get_crtc_infos = wfd_kms_get_crtc_infos,
	.get_mode_info = wfd_kms_get_mode_info,
	.get_framebuffer_info = wfd_kms_get_framebuffer_info,
	.commit = wfd_kms_commit,
	.enable_vblank = wfd_kms_enable_vblank,
	.disable_vblank = wfd_kms_disable_vblank,
	.free_connector_port_modes = wfd_kms_free_connector_port_modes,
	.register_event = wfd_kms_register_event,
	.crtc_atomic_begin = wfd_kms_crtc_atomic_begin,
};

static int wfd_kms_bind(struct device *dev, struct device *master,
		void *data)
{
	struct wfd_kms *kms = dev_get_drvdata(dev);
	struct drm_device *drm_dev = dev_get_drvdata(master);

	kms->dev = drm_dev;
	msm_hyp_set_kms(drm_dev, &kms->base);

	return 0;
}

static void wfd_kms_unbind(struct device *dev, struct device *master,
		void *data)
{
	struct wfd_kms *kms = dev_get_drvdata(dev);

	msm_hyp_set_kms(kms->dev, NULL);
}

static const struct component_ops wfd_kms_comp_ops = {
	.bind = wfd_kms_bind,
	.unbind = wfd_kms_unbind,
};

static int wfd_kms_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wfd_kms *kms;
	int ret;
	char marker_buff[MARKER_BUFF_LENGTH] = {0};

	kms = devm_kzalloc(dev, sizeof(*kms), GFP_KERNEL);
	if (!kms)
		return -ENOMEM;

	ret = _wfd_kms_parse_dt(dev->of_node, &kms->client_id);
	if (ret)
		return ret;

	ret = _wfd_kms_hw_init(kms, dev);
	if (ret)
		return ret;

	kms->base.funcs = &wfd_kms_funcs;

	platform_set_drvdata(pdev, kms);

	ret = component_add(&pdev->dev, &wfd_kms_comp_ops);
	if (ret) {
		pr_err("component add failed, rc=%d\n", ret);
		return ret;
	}

	snprintf(marker_buff, sizeof(marker_buff),
		"kernel_fe: wfd_kms probe client %x", kms->client_id);
	place_marker(marker_buff);

	return 0;
}

static int wfd_kms_remove(struct platform_device *pdev)
{
	struct wfd_kms *kms = platform_get_drvdata(pdev);
	int i, j;
	int index = -1;
	int buff_idx = 0;
	struct wire_device *wire_dev = NULL;
	int export_id = 0;
	void *handle = NULL;
	struct user_os_utils_mem_info mem = { 0 };

	for (i = 0; i < kms->port_cnt; i++) {
		for (j = 0; j < kms->pipeline_cnt[i]; j++) {
			wire_dev = kms->port_devs[i];
			if (wire_dev) {
				handle = wire_dev->ctx->init_info.context;
				wfdSetPipelineAttribiv_User(kms->port_devs[i], kms->pipelines[i][j],
					WFD_PIPELINE_COLOR_CONFIG_CLEAR, 1, &i);
				wfdDestroyPipeline_User(kms->port_devs[i], kms->pipelines[i][j]);
				// unexport the buffers
				mem.shmem_type = HAB_EXPORT_ID;
				// Get export id
				_wfd_kms_get_color_buff_idx(kms->port_devs[i],
					kms->pipelines[i][j],
					&index);
				if (index < 0) {
					pr_err("Unable to find color buffer for this pipe\n");
				} else {
					for (buff_idx = 0; buff_idx < 2; buff_idx++) {
						if (color_buffer[index].buffer_info[buff_idx].valid) {
							export_id =
								color_buffer[index].buffer_info[buff_idx].export_id;
							mem.shmem_id = export_id;
							if (user_os_utils_shmem_unexport(handle, &mem, 0))
								pr_err("Failed to unexport shmem buffer\n");
							else
								pr_debug("passed to unexport shmem buffer\n");
						}
					}
				}
				mem.shmem_id = export_id;
				if (user_os_utils_shmem_unexport(handle, &mem, 0))
					pr_err("Failed to unexport shmem buffer\n");
				else
					pr_debug("passed to unexport shmem buffer\n");
			}
		}
		wfdDestroyPort_User(kms->port_devs[i], kms->ports[i]);
	}

	for (i = 0; i < kms->wfd_device_cnt; i++) {
		wfdUnregisterHotplugEvent_User(kms->wfd_device[i]);
		wfdDestroyDevice_User(kms->wfd_device[i]);
	}

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct platform_device_id wfd_kms_id[] = {
	{ "wfd-kms", 0 },
	{ }
};

static const struct of_device_id dt_match[] = {
	{ .compatible = "qcom,wfd-kms" },
	{}
};

static struct platform_driver wfd_kms_driver = {
	.probe      = wfd_kms_probe,
	.remove     = wfd_kms_remove,
	.driver     = {
		.name   = "wfd_kms",
		.of_match_table = dt_match,
	},
	.id_table   = wfd_kms_id,
};

void wfd_kms_register(void)
{
	platform_driver_register(&wfd_kms_driver);
}

void wfd_kms_unregister(void)
{
	platform_driver_unregister(&wfd_kms_driver);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 0))
MODULE_IMPORT_NS(DMA_BUF);
#endif
