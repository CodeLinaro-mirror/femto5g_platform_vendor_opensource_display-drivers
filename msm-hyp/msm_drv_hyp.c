/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2017-2018,2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
/*
 * Copyright (C) 2014 Red Hat
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
/* Copyright (c) 2016 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission. The copyright holders make no representations
 * about the suitability of this software for any purpose. It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */
/* Copyright (C) 2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/of_platform.h>
#include <linux/version.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_drv.h>
#include <uapi/linux/sched/types.h>
#include "msm_drv_hyp.h"
#include "msm_hyp_utils.h"
#include "msm_hyp_trace.h"
#include "msm_drv.h"

#define CRTC_INPUT_FENCE_TIMEOUT    10000
#define MSM_HYP_MAX_PLANES          MAX_PLANES
#define MAX_CONNECTORS              16
#define MAX_CRTCS                   16
#define HPD_STRING_SIZE             30

#if (KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE)
#define DRM_UNLOCKED 0
#endif

struct msm_hyp_commit {
	struct drm_device *dev;
	struct drm_atomic_state *state;
	uint32_t crtc_mask;
	bool nonblock;
	struct kthread_work commit_work;
};

enum topology_name {
	TOPOLOGY_UNKNOWN = 0,
	TOPOLOGY_SINGLEPIPE,
	TOPOLOGY_SINGLEPIPE_DSC,
	TOPOLOGY_DUALPIPE,
	TOPOLOGY_DUALPIPE_DSC,
	TOPOLOGY_DUALPIPEMERGE,
	TOPOLOGY_DUALPIPEMERGE_DSC,
	TOPOLOGY_DUALPIPE_DSCMERGE,
	TOPOLOGY_PPSPLIT,
};

enum topology_control {
	TOPCTL_RESERVE_LOCK,
	TOPCTL_RESERVE_CLEAR,
	TOPCTL_DSPP,
	TOPCTL_FORCE_TILING,
	TOPCTL_PPSPLIT,
};

enum multirect_mode {
	DRM_MULTIRECT_NONE,
	DRM_MULTIRECT_PARALLEL,
	DRM_MULTIRECT_TIME_MX,
};

static const struct drm_prop_enum_list e_topology_name[] = {
	{TOPOLOGY_UNKNOWN,            "sde_none"},
	{TOPOLOGY_SINGLEPIPE,         "sde_singlepipe"},
	{TOPOLOGY_SINGLEPIPE_DSC,     "sde_singlepipe_dsc"},
	{TOPOLOGY_DUALPIPE,           "sde_dualpipe"},
	{TOPOLOGY_DUALPIPE_DSC,       "sde_dualpipe_dsc"},
	{TOPOLOGY_DUALPIPEMERGE,      "sde_dualpipemerge"},
	{TOPOLOGY_DUALPIPEMERGE_DSC,  "sde_dualpipemerge_dsc"},
	{TOPOLOGY_DUALPIPE_DSCMERGE,  "sde_dualpipe_dscmerge"},
	{TOPOLOGY_PPSPLIT,            "sde_ppsplit"}
};

static const struct drm_prop_enum_list e_topology_control[] = {
	{TOPCTL_RESERVE_LOCK,    "reserve_lock"},
	{TOPCTL_RESERVE_CLEAR,   "reserve_clear"},
	{TOPCTL_DSPP,            "dspp"},
	{TOPCTL_FORCE_TILING,    "force_tiling"},
	{TOPCTL_PPSPLIT,         "ppsplit"}
};

static const struct drm_prop_enum_list e_blend_op[] = {
	{SDE_DRM_BLEND_OP_NOT_DEFINED,    "not_defined"},
	{SDE_DRM_BLEND_OP_OPAQUE,         "opaque"},
	{SDE_DRM_BLEND_OP_PREMULTIPLIED,  "premultiplied"},
	{SDE_DRM_BLEND_OP_COVERAGE,       "coverage"}
};

static const struct drm_prop_enum_list e_fb_translation_mode[] = {
	{SDE_DRM_FB_NON_SEC,           "non_sec"},
	{SDE_DRM_FB_SEC,               "sec"},
	{SDE_DRM_FB_NON_SEC_DIR_TRANS, "non_sec_direct_translation"},
	{SDE_DRM_FB_SEC_DIR_TRANS,     "sec_direct_translation"},
};

static const struct drm_prop_enum_list e_multirect_mode[] = {
	{DRM_MULTIRECT_NONE,     "none"},
	{DRM_MULTIRECT_PARALLEL, "parallel"},
};

struct msm_hyp_topology {
	const char *topology_name;
	int num_lm;
	int num_comp_enc;
	int num_intf;
	int num_ctl;
	int needs_split_display;
};

static void msm_hyp_display_thread_priority_worker(struct kthread_work *work)
{
	int ret = 0;
	struct sched_param param = { 0 };
	struct task_struct *task = current->group_leader;

	/**
	 * this priority was found during empiric testing to have appropriate
	 * realtime scheduling to process display updates and interact with
	 * other real time and normal priority task
	 */
	param.sched_priority = 16;
	ret = sched_setscheduler(task, SCHED_FIFO, &param);
	if (ret)
		DRM_WARN("pid:%d name:%s priority update failed: %d\n",
			current->tgid, task->comm, ret);
}

static const char *_msm_hyp_get_topology(struct msm_hyp_mode_info *modeinfo)
{
	static const struct msm_hyp_topology top_table[] = {
		{"sde_none",                    0, 0, 0, 0, false},
		{"sde_singlepipe",              1, 0, 1, 1, false},
		{"sde_singlepipe_dsc",          1, 1, 1, 1, false},
		{"sde_dualpipe",                2, 0, 2, 1, false},
		{"sde_dualpipe_dsc",            2, 2, 2, 1, false},
		{"sde_dualpipemerge",           2, 0, 1, 1, false},
		{"sde_dualpipemerge_dsc",       2, 1, 1, 1, false},
		{"sde_dualpipe_dscmerge",       2, 2, 1, 1, false},
		{"sde_ppsplit",                 1, 0, 2, 1, false},
		{"sde_triplepipe",              3, 0, 3, 1, false},
		{"sde_triplepipe_dsc",          3, 3, 3, 1, false},
		{"sde_quadpipemerge",           4, 0, 2, 1, false},
		{"sde_quadpipe_dscmerge",       4, 3, 2, 1, false},
		{"sde_quadpipe_3dmerge_dsc",    4, 4, 2, 1, false},
		{"sde_sixpipemerge",            6, 0, 3, 1, false},
		{"sde_sixpipe_dscmerge",        6, 6, 3, 1, false},
	};

	int i;

	for (i = 0; i < ARRAY_SIZE(top_table); i++) {
		if (top_table[i].num_lm == modeinfo->num_lm &&
			top_table[i].num_comp_enc == modeinfo->num_enc &&
			top_table[i].num_intf == modeinfo->num_intf)
			return top_table[i].topology_name;
	}

	return top_table[0].topology_name;
}

void msm_hyp_set_kms(struct drm_device *dev, struct msm_hyp_kms *kms)
{
	struct msm_hyp_drm_private *priv;

	if (!dev || !dev->dev_private)
		return;

	priv = dev->dev_private;

	priv->kms = kms;
}

static int _msm_hyp_mode_create_properties(struct drm_device *ddev)
{
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct drm_property *prop;

	/* special connecotr properties */
	prop = drm_property_create_range(ddev, 0,
				"RETIRE_FENCE", 0, ~0/*4096*/);
	if (!prop)
		return -ENOMEM;
	priv->prop_retire_fence = prop;

	prop = drm_property_create_enum(ddev,
			DRM_MODE_PROP_ENUM | DRM_MODE_PROP_IMMUTABLE,
			"topology_name", e_topology_name,
			ARRAY_SIZE(e_topology_name));
	if (!prop)
		return -ENOMEM;
	priv->prop_topology_name = prop;

	prop = drm_property_create_enum(ddev,
			DRM_MODE_PROP_ENUM,
			"fb_translation_mode", e_fb_translation_mode,
			ARRAY_SIZE(e_fb_translation_mode));
	if (!prop)
		return -ENOMEM;
	priv->prop_fb_translation_mode = prop;

	prop = drm_property_create(ddev,
			DRM_MODE_PROP_BLOB | DRM_MODE_PROP_IMMUTABLE,
			"capabilities", 0);
	if (!prop)
		return -ENOMEM;
	priv->prop_connector_caps = prop;

	prop = drm_property_create(ddev,
			DRM_MODE_PROP_BLOB | DRM_MODE_PROP_IMMUTABLE,
			"mode_properties", 0);
	if (!prop)
		return -ENOMEM;
	priv->prop_mode_info = prop;

	/* special plane properties */
	prop = drm_property_create_range(ddev, 0,
				"zpos", 0, 255);
	if (!prop)
		return -ENOMEM;
	priv->prop_zpos = prop;

	prop = drm_property_create_range(ddev, 0,
				"alpha", 0, 255);
	if (!prop)
		return -ENOMEM;
	priv->prop_alpha = prop;

	prop = drm_property_create_range(ddev, 0,
				"input_fence", 0, 4096);
	if (!prop)
		return -ENOMEM;
	priv->prop_input_fence = prop;

	prop = drm_property_create_range(ddev, 0,
				"scaler_v2", 0, ~0);
	if (!prop)
		return -ENOMEM;
	priv->prop_scaler = prop;

	prop = drm_property_create_range(ddev, 0,
				"csc_v1", 0, ~0);
	if (!prop)
		return -ENOMEM;
	priv->prop_csc = prop;

	prop = drm_property_create(ddev,
			DRM_MODE_PROP_BLOB | DRM_MODE_PROP_IMMUTABLE,
			"capabilities", 0);
	if (!prop)
		return -ENOMEM;
	priv->prop_plane_caps = prop;

	prop = drm_property_create_enum(ddev, DRM_MODE_PROP_ENUM,
			"blend_op",  e_blend_op, ARRAY_SIZE(e_blend_op));
	if (!prop)
		return -ENOMEM;
	priv->prop_blend_op = prop;

	prop = drm_property_create_enum(ddev, DRM_MODE_PROP_ENUM,
			"multirect_mode",  e_multirect_mode,
			ARRAY_SIZE(e_multirect_mode));
	if (!prop)
		return -ENOMEM;
	priv->prop_multirect_mode = prop;

	/* special crtc properties */
	prop = drm_property_create_range(ddev, 0,
			"input_fence_timeout", 0, 10000);
	if (!prop)
		return -ENOMEM;
	priv->prop_input_fence_timeout = prop;

	prop = drm_property_create_range(ddev, 0,
			"output_fence", 0, ~0/*4096*/);

	if (!prop)
		return -ENOMEM;
	priv->prop_output_fence = prop;

	prop = drm_property_create_range(ddev, 0,
			"output_fence_offset", 0, 1);

	if (!prop)
		return -ENOMEM;
	priv->prop_output_fence_offset = prop;

	prop = drm_property_create(ddev,
			DRM_MODE_PROP_BLOB | DRM_MODE_PROP_IMMUTABLE,
			"capabilities", 0);
	if (!prop)
		return -ENOMEM;
	priv->prop_crtc_caps = prop;

	prop = drm_property_create_range(ddev, 0,
				"inverse_pma", 0, 1);
	if (!prop)
		return -ENOMEM;
	priv->prop_inverse_pma = prop;

	prop = drm_property_create_range(ddev, 0,
				"csc_dma_v1", 0, ~0);
	if (!prop)
		return -ENOMEM;
	priv->prop_dma_csc = prop;

	prop = drm_property_create(ddev,
			DRM_MODE_PROP_BLOB,
			"SDE_DGM_1D_LUT_IGC_V5", 1);
	if (!prop)
		return -ENOMEM;
	priv->prop_dma_igc = prop;

	prop = drm_property_create(ddev,
			DRM_MODE_PROP_BLOB,
			"SDE_DGM_1D_LUT_GC_V5", 1);
	if (!prop)
		return -ENOMEM;
	priv->prop_dma_gc = prop;

	prop = drm_property_create(ddev,
			DRM_MODE_PROP_BLOB,
			"SDE_VIG_1D_LUT_IGC_V5", 1);
	if (!prop)
		return -ENOMEM;
	priv->prop_vig_igc = prop;

	prop = drm_property_create(ddev,
			DRM_MODE_PROP_BLOB,
			"SDE_VIG_3D_LUT_GAMUT_V5", 1);
	if (!prop)
		return -ENOMEM;
	priv->prop_vig_gamut = prop;

	/* color process : HSIC property on CRTC */
	prop = drm_property_create(ddev,
			DRM_MODE_PROP_BLOB,
			"SDE_DSPP_PA_HSIC_V1", 0);
	if (!prop)
		return -ENOMEM;
	priv->prop_crtc_cp_hsic = prop;

	return 0;
}

static int _msm_hyp_object_property_get_prop_value(struct drm_mode_object *obj,
						struct drm_property *property,
						uint64_t *val)
{
	int i;

	for (i = 0; i < obj->properties->count; i++) {
		if (obj->properties->properties[i] == property) {
			*val = obj->properties->values[i];
			return 0;
		}
	}

	return -EINVAL;
}

static int msm_hyp_backlight_device_update_status(struct backlight_device *bd)
{
	return 0;
}

static int msm_hyp_backlight_device_get_brightness(struct backlight_device *bd)
{
	return 0;
}

static const struct backlight_ops msm_hyp_backlight_device_ops = {
	.update_status = msm_hyp_backlight_device_update_status,
	.get_brightness = msm_hyp_backlight_device_get_brightness,
};

static int _msm_hyp_connector_create_backlight(struct msm_hyp_connector *connector)
{
	struct backlight_properties props;
	char bl_node_name[32];

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.power = FB_BLANK_UNBLANK;
	props.max_brightness = 255;
	props.brightness = 255;

	snprintf(bl_node_name, sizeof(bl_node_name),
			"panel%u-backlight",
			connector->base.connector_type_id - 1);

	connector->bl_device = backlight_device_register(bl_node_name,
			connector->base.dev->dev,
			connector, &msm_hyp_backlight_device_ops,
			&props);

	return 0;
}

static const struct drm_connector_helper_funcs msm_hyp_connector_helper = {
	/* emptry */
};

static void msm_hyp_connector_destroy(struct drm_connector *connector)
{
	struct msm_hyp_connector *c_conn = to_msm_hyp_connector(connector);
	struct drm_device *dev = connector->dev;
	struct msm_hyp_drm_private *priv = dev->dev_private;
	struct msm_hyp_kms *kms = priv->kms;

	if (kms->funcs && kms->funcs->free_connector_port_modes)
		kms->funcs->free_connector_port_modes(c_conn);

	if (c_conn->blob_caps)
		drm_property_blob_put(c_conn->blob_caps);

	if (c_conn->blob_mode_info)
		drm_property_blob_put(c_conn->blob_mode_info);

	if (c_conn->bl_device)
		backlight_device_unregister(c_conn->bl_device);

	msm_hyp_fence_deinit(c_conn->retire_fence);
	drm_connector_cleanup(connector);
}

static struct drm_connector_state *
msm_hyp_connector_duplicate_state(struct drm_connector *connector)
{
	struct msm_hyp_connector_state *c_state, *c_oldstate;

	if (!connector || !connector->state) {
		DRM_ERROR("invalid connector %pK\n", connector);
		return NULL;
	}

	c_oldstate = to_msm_hyp_connector_state(connector->state);

	c_state = kmemdup(c_oldstate, sizeof(*c_oldstate), GFP_KERNEL);
	if (!c_state)
		return NULL;

	__drm_atomic_helper_connector_duplicate_state(connector,
			&c_state->base);

	c_state->retire_fence_ptr = NULL;

	return &c_state->base;
}

static void msm_hyp_connector_destroy_state(
		struct drm_connector *connector,
		struct drm_connector_state *state)
{
	struct msm_hyp_connector_state *c_state;

	if (!connector || !state) {
		DRM_ERROR("invalid connector %pK\n", connector);
		return;
	}

	c_state = to_msm_hyp_connector_state(state);

	__drm_atomic_helper_connector_destroy_state(&c_state->base);

	kfree(c_state);
}

static void msm_hyp_connector_reset(struct drm_connector *connector)
{
	struct msm_hyp_connector_state *conn_state =
		kzalloc(sizeof(*conn_state), GFP_KERNEL);

	if (!conn_state)
		return;

	if (connector->state)
		msm_hyp_connector_destroy_state(connector,
				connector->state);

	__drm_atomic_helper_connector_reset(connector, &conn_state->base);
}

static int msm_hyp_connector_set_property(
		struct drm_connector *connector,
		struct drm_connector_state *state,
		struct drm_property *property,
		uint64_t val)
{
	struct drm_device *ddev;
	struct msm_hyp_drm_private *priv;
	struct msm_hyp_connector_state *c_state;
	int ret = 0;

	if (!connector || !state) {
		DRM_ERROR("invalid connector %pK\n", connector);
		return -EINVAL;
	}

	ddev = connector->dev;
	priv = ddev->dev_private;

	c_state = to_msm_hyp_connector_state(state);

	if (property == priv->prop_retire_fence)
		c_state->retire_fence_ptr = (uint64_t __user *)val;
	else
		ret = -EINVAL;

	return ret;
}

static int msm_hyp_connector_get_property(
		struct drm_connector *connector,
		const struct drm_connector_state *state,
		struct drm_property *property,
		uint64_t *val)
{
	struct drm_device *ddev;
	struct msm_hyp_drm_private *priv;
	struct msm_hyp_connector_state *c_state;
	int ret = 0;

	if (!connector || !state || !val) {
		DRM_ERROR("invalid connector %pK\n", connector);
		return -EINVAL;
	}

	ddev = connector->dev;
	priv = ddev->dev_private;

	c_state = to_msm_hyp_connector_state(state);

	if (property == priv->prop_retire_fence)
		*val = ~0;
	else
		ret = -EINVAL;

	return ret;
}

static void _msm_hyp_connector_init_mode_info(
		struct msm_hyp_connector *connector)
{
	struct drm_device *ddev = connector->base.dev;
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct msm_hyp_kms *kms = priv->kms;
	struct msm_hyp_prop_blob_info *info;
	struct msm_hyp_mode_info modeinfo = {0};
	struct drm_display_mode *mode;
	int ret;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return;

	list_for_each_entry(mode, &connector->base.modes, head) {
		msm_hyp_prop_info_add_keystr(info, "mode_name", mode->name);

		if (kms->funcs && kms->funcs->get_mode_info) {
			ret = kms->funcs->get_mode_info(kms, mode, &modeinfo);
			if (ret)
				break;
		}

		msm_hyp_prop_info_add_keystr(info, "topology",
				_msm_hyp_get_topology(&modeinfo));
	}

	drm_property_blob_put(connector->blob_mode_info);

	connector->blob_mode_info = drm_property_create_blob(ddev,
			info->len, info->data);
	if (IS_ERR_OR_NULL(connector->blob_mode_info))
		connector->blob_mode_info = NULL;

	drm_object_property_set_value(&connector->base.base,
			priv->prop_mode_info,
			connector->blob_mode_info ?
			connector->blob_mode_info->base.id : 0);

	kfree(info);
}

static int msm_hyp_connector_fill_modes(struct drm_connector *connector,
		uint32_t max_width, uint32_t max_height)
{
	struct msm_hyp_connector *c = to_msm_hyp_connector(connector);
	int mode_count;

	mode_count = drm_helper_probe_single_connector_modes(connector,
			max_width, max_height);

	_msm_hyp_connector_init_mode_info(c);

	return mode_count;
}

static const struct drm_connector_funcs msm_hyp_connector_ops = {
	.reset =                  msm_hyp_connector_reset,
	.destroy =                msm_hyp_connector_destroy,
	.fill_modes =             msm_hyp_connector_fill_modes,
	.atomic_duplicate_state = msm_hyp_connector_duplicate_state,
	.atomic_destroy_state =   msm_hyp_connector_destroy_state,
	.atomic_set_property =    msm_hyp_connector_set_property,
	.atomic_get_property =    msm_hyp_connector_get_property,
};

static int _msm_hyp_connector_init_caps(
		struct msm_hyp_connector *connector)
{
	struct drm_device *ddev = connector->base.dev;
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct msm_hyp_prop_blob_info *info;
	int ret;
	char buf[32] = {0};

	info = devm_kzalloc(ddev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (connector->info->display_type)
		msm_hyp_prop_info_add_keystr(info, "display type",
				connector->info->display_type);

	switch (connector->info->panel_orientation) {
		case PANEL_ROTATE_NONE:
			snprintf(buf, sizeof(buf), "%s", "none");
			break;
		case PANEL_ROTATE_180:
			snprintf(buf, sizeof(buf), "%s", "horz & vert flip");
			break;
		case PANEL_ROTATE_H_FLIP:
			snprintf(buf, sizeof(buf), "%s", "horz flip");
			break;
		case PANEL_ROTATE_V_FLIP:
			snprintf(buf, sizeof(buf), "%s", "vert flip");
			break;
		default:
			snprintf(buf, sizeof(buf), "%s", "none");
			break;
	}
	msm_hyp_prop_info_add_keystr(info, "panel orientation", buf);

	if (connector->info->extra_caps)
		msm_hyp_prop_info_append(info, connector->info->extra_caps);

	connector->blob_caps = drm_property_create_blob(ddev,
			info->len, info->data);
	if (IS_ERR_OR_NULL(connector->blob_caps)) {
		ret = PTR_ERR(connector->blob_caps);
		DRM_ERROR("failed to create blob, %d\n", ret);
		return ret;
	}

	drm_object_attach_property(&connector->base.base,
			priv->prop_connector_caps,
			connector->blob_caps->base.id);

	return 0;
}

static const struct drm_encoder_funcs msm_hyp_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int _msm_hyp_connector_encoder_init(struct drm_device *ddev,
		struct msm_hyp_connector_info *info)
{
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct msm_hyp_connector *connector;
	int encoder_type;
	int ret;

	if (!info)
		return -EINVAL;

	connector = devm_kzalloc(ddev->dev, sizeof(*connector), GFP_KERNEL);
	if (!connector)
		return -ENOMEM;

	ret = drm_connector_init(ddev, &connector->base,
			&msm_hyp_connector_ops,
			info->connector_type);
	if (ret)
		return ret;

	if (info->connector_funcs)
		connector->base.helper_private = info->connector_funcs;
	else
		connector->base.helper_private = &msm_hyp_connector_helper;

	connector->base.display_info = info->display_info;
	connector->info = info;

	drm_object_attach_property(&connector->base.base,
			priv->prop_retire_fence, 0);

	connector->retire_fence = msm_hyp_fence_init(connector->base.name);
	if (IS_ERR_OR_NULL(connector->retire_fence))
		return PTR_ERR(connector->retire_fence);

	ret = _msm_hyp_connector_init_caps(connector);
	if (ret)
		return ret;

	drm_object_attach_property(&connector->base.base,
			priv->prop_mode_info, 0);

	if (connector->base.connector_type == DRM_MODE_CONNECTOR_DSI) {
		ret = _msm_hyp_connector_create_backlight(connector);
		if (ret)
			return ret;
	}

	switch (info->connector_type) {
	case DRM_MODE_CONNECTOR_HDMIA:
	case DRM_MODE_CONNECTOR_HDMIB:
	case DRM_MODE_CONNECTOR_DisplayPort:
		encoder_type = DRM_MODE_ENCODER_TMDS;
		break;
	case DRM_MODE_CONNECTOR_DSI:
		encoder_type = DRM_MODE_ENCODER_DSI;
		break;
	default:
		encoder_type = DRM_MODE_ENCODER_NONE;
		break;
	}

	ret = drm_encoder_init(ddev, &connector->encoder,
			&msm_hyp_encoder_funcs, encoder_type, NULL);
	if (ret)
		return ret;

	connector->encoder.possible_crtcs = info->possible_crtcs;

	ret = drm_connector_attach_encoder(&connector->base,
			&connector->encoder);
	if (ret)
		return ret;

	if (info->bridge_funcs) {
		connector->bridge.funcs = info->bridge_funcs;
		ret = drm_bridge_attach(&connector->encoder,
				&connector->bridge, NULL, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static void msm_hyp_crtc_atomic_begin(struct drm_crtc *crtc,
		struct drm_atomic_state *old_state)
{
	struct drm_device *dev = crtc->dev;
	struct msm_hyp_drm_private *priv = dev->dev_private;
	struct msm_hyp_kms *kms = priv->kms;

	if (kms->funcs && kms->funcs->crtc_atomic_begin)
		kms->funcs->crtc_atomic_begin(kms, crtc, old_state);
}

static void msm_hyp_crtc_atomic_enable(struct drm_crtc *crtc,
		struct drm_atomic_state *old_state)
{
	drm_crtc_vblank_on(crtc);
}

static void msm_hyp_crtc_atomic_disable(struct drm_crtc *crtc,
		struct drm_atomic_state *old_state)
{
	drm_crtc_vblank_off(crtc);
}

const struct drm_crtc_helper_funcs msm_hyp_crtc_helper = {
	.atomic_begin = msm_hyp_crtc_atomic_begin,
	.atomic_enable = msm_hyp_crtc_atomic_enable,
	.atomic_disable = msm_hyp_crtc_atomic_disable,
};

static void msm_hyp_crtc_destroy(struct drm_crtc *crtc)
{
	struct msm_hyp_crtc *c = to_msm_hyp_crtc(crtc);

	if (c->blob_caps)
		drm_property_blob_put(c->blob_caps);

	if (c->blob_cp_hsic)
		drm_property_blob_put(c->blob_cp_hsic);

	if (c->thread) {
		kthread_flush_worker(&c->worker);
		kthread_stop(c->thread);
	}

	msm_hyp_fence_deinit(c->output_fence);
	drm_crtc_cleanup(crtc);
}

static struct drm_crtc_state *
msm_hyp_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct msm_hyp_crtc_state *c_state, *c_oldstate;

	if (!crtc || !crtc->state) {
		DRM_ERROR("invalid crtc %pK\n", crtc);
		return NULL;
	}

	c_oldstate = to_msm_hyp_crtc_state(crtc->state);

	c_state = kmemdup(c_oldstate, sizeof(*c_oldstate), GFP_KERNEL);
	if (!c_state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc,
			&c_state->base);

	c_state->output_fence_ptr = NULL;

	return &c_state->base;
}

static void msm_hyp_crtc_destroy_state(
		struct drm_crtc *crtc,
		struct drm_crtc_state *state)
{
	struct msm_hyp_crtc_state *c_state;

	if (!crtc || !state) {
		DRM_ERROR("invalid crtc %pK\n", crtc);
		return;
	}

	c_state = to_msm_hyp_crtc_state(state);

	__drm_atomic_helper_crtc_destroy_state(&c_state->base);

	kfree(c_state);
}

static void msm_hyp_crtc_reset(struct drm_crtc *crtc)
{
	struct msm_hyp_crtc_state *c_state =
		kzalloc(sizeof(*c_state), GFP_KERNEL);

	if (!c_state)
		return;

	if (crtc->state)
		msm_hyp_crtc_destroy_state(crtc,
				crtc->state);

	__drm_atomic_helper_crtc_reset(crtc, &c_state->base);

	c_state->base.no_vblank = true;

	c_state->input_fence_timeout = CRTC_INPUT_FENCE_TIMEOUT;
}

static int msm_hyp_crtc_set_pa_hsic_prop(
		struct drm_crtc *crtc,
		struct msm_hyp_crtc_state *c_state,
		uint64_t val)
{
	struct drm_device *ddev = NULL;
	struct msm_hyp_crtc *msm_crtc = NULL;
	struct drm_property_blob *blob = NULL;

	if (!crtc || !c_state) {
		DRM_ERROR("invalid crtc %pK\n", crtc);
		return -EINVAL;
	}

	ddev = crtc->dev;
	msm_crtc = to_msm_hyp_crtc(crtc);

	if (!val) {
		if (msm_crtc->blob_cp_hsic)
			drm_property_blob_put(msm_crtc->blob_cp_hsic);
		/* Disable HSIC on PA, no blob is created for disabled case */
		msm_crtc->blob_cp_hsic = NULL;
		memset(&(c_state->cp_hsic.pa_hsic), 0, sizeof(struct msm_hyp_pa_hsic));
	} else {
		blob = drm_property_lookup_blob(ddev, val);
		if (!blob) {
			DRM_ERROR("invalid blob id %lld for HSIC\n", val);
			return -EINVAL;
		}

		if (blob->length != sizeof(struct msm_hyp_pa_hsic)) {
			DRM_ERROR("invalid blob len %zu exp %lu\n", blob->length,
					sizeof(struct msm_hyp_pa_hsic));
			return -EINVAL;
		}

		if (msm_crtc->blob_cp_hsic)
			drm_property_blob_put(msm_crtc->blob_cp_hsic);
		msm_crtc->blob_cp_hsic = blob;

		memcpy(&(c_state->cp_hsic.pa_hsic), blob->data, blob->length);
	}

	return 0;
}

static int msm_hyp_crtc_set_property(
		struct drm_crtc *crtc,
		struct drm_crtc_state *state,
		struct drm_property *property,
		uint64_t val)
{
	struct drm_device *ddev;
	struct msm_hyp_drm_private *priv;
	struct msm_hyp_crtc_state *c_state;
	int ret = 0;

	if (!crtc || !state) {
		DRM_ERROR("invalid crtc %pK\n", crtc);
		return -EINVAL;
	}

	ddev = crtc->dev;
	priv = ddev->dev_private;

	c_state = to_msm_hyp_crtc_state(state);

	if (property == priv->prop_output_fence)
		c_state->output_fence_ptr = (uint64_t __user *)val;
	else if (property == priv->prop_input_fence_timeout)
		c_state->input_fence_timeout = val;
	else if (property == priv->prop_output_fence_offset)
		c_state->output_fence_offset = val;
	else if (property == priv->prop_crtc_cp_hsic) {
		c_state->cp_hsic.prop_value = val;
		ret = msm_hyp_crtc_set_pa_hsic_prop(crtc, c_state, val);
	}
	else
		ret = -EINVAL;

	return ret;
}

static int msm_hyp_crtc_get_property(
		struct drm_crtc *crtc,
		const struct drm_crtc_state *state,
		struct drm_property *property,
		uint64_t *val)
{
	struct drm_device *ddev;
	struct msm_hyp_drm_private *priv;
	struct msm_hyp_crtc_state *c_state;
	int ret = 0;

	if (!crtc || !state || !val) {
		DRM_ERROR("invalid crtc %pK\n", crtc);
		return -EINVAL;
	}

	ddev = crtc->dev;
	priv = ddev->dev_private;

	c_state = to_msm_hyp_crtc_state(state);

	if (property == priv->prop_output_fence)
		*val = ~0;
	else if (property == priv->prop_input_fence_timeout)
		*val = c_state->input_fence_timeout;
	else if (property == priv->prop_output_fence_offset)
		*val = c_state->output_fence_offset;
	else if (property == priv->prop_crtc_cp_hsic)
		*val = c_state->cp_hsic.prop_value;
	else
		ret = -EINVAL;

	return ret;
}

static int msm_hyp_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct msm_hyp_drm_private *priv = dev->dev_private;
	struct msm_hyp_kms *kms = priv->kms;

	if (kms->funcs && kms->funcs->enable_vblank)
		kms->funcs->enable_vblank(kms, crtc);

	return 0;
}

static void msm_hyp_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct msm_hyp_drm_private *priv = dev->dev_private;
	struct msm_hyp_kms *kms = priv->kms;

	if (kms->funcs && kms->funcs->disable_vblank)
		kms->funcs->disable_vblank(kms, crtc);
}

static const struct drm_crtc_funcs msm_hyp_crtc_funcs = {
	.set_config               = drm_atomic_helper_set_config,
	.destroy                  = msm_hyp_crtc_destroy,
	.page_flip                = drm_atomic_helper_page_flip,
	.atomic_set_property      = msm_hyp_crtc_set_property,
	.atomic_get_property      = msm_hyp_crtc_get_property,
	.reset                    = msm_hyp_crtc_reset,
	.atomic_duplicate_state   = msm_hyp_crtc_duplicate_state,
	.atomic_destroy_state     = msm_hyp_crtc_destroy_state,
	.enable_vblank            = msm_hyp_enable_vblank,
	.disable_vblank           = msm_hyp_disable_vblank,
};

static int _msm_hyp_crtc_init_caps(struct msm_hyp_crtc *crtc)
{
	struct drm_device *ddev = crtc->base.dev;
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct msm_hyp_prop_blob_info *info;
	char buf[16];
	int ret;

	info = devm_kzalloc(ddev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	snprintf(buf, sizeof(buf), "%d", crtc->info->max_blendstages);
	msm_hyp_prop_info_add_keystr(info, "max_blendstages", buf);

	snprintf(buf, sizeof(buf), "%lld", crtc->info->max_mdp_clk);
	msm_hyp_prop_info_add_keystr(info, "max_mdp_clk", buf);

	msm_hyp_prop_info_add_keystr(info, "qseed_type",
			crtc->info->qseed_type);

	msm_hyp_prop_info_add_keystr(info, "smart_dma_rev",
			crtc->info->smart_dma_rev);

	snprintf(buf, sizeof(buf), "%d", crtc->info->has_hdr);
	msm_hyp_prop_info_add_keystr(info, "has_hdr", buf);

	snprintf(buf, sizeof(buf), "%lld", crtc->info->max_bandwidth_low);
	msm_hyp_prop_info_add_keystr(info, "max_bandwidth_low", buf);

	snprintf(buf, sizeof(buf), "%lld", crtc->info->max_bandwidth_high);
	msm_hyp_prop_info_add_keystr(info, "max_bandwidth_high", buf);

	snprintf(buf, sizeof(buf), "%d", crtc->info->has_src_split);
	msm_hyp_prop_info_add_keystr(info, "has_src_split", buf);

	if (crtc->info->extra_caps)
		msm_hyp_prop_info_append(info, crtc->info->extra_caps);

	crtc->blob_caps = drm_property_create_blob(ddev,
			info->len, info->data);
	if (IS_ERR_OR_NULL(crtc->blob_caps)) {
		ret = PTR_ERR(crtc->blob_caps);
		DRM_ERROR("failed to create blob, %d\n", ret);
		return ret;
	}

	drm_object_attach_property(&crtc->base.base,
			priv->prop_crtc_caps,
			crtc->blob_caps->base.id);

	return 0;
}

static int _msm_hyp_crtc_init_dispatch_thread(struct msm_hyp_crtc *c,
		struct msm_hyp_drm_private *priv)
{
	int ret = 0;

	kthread_init_worker(&c->worker);
	kthread_init_work(&priv->commit_thread_priority_work,
			msm_hyp_display_thread_priority_worker);

	c->thread = kthread_run(kthread_worker_fn,
			&c->worker,
			"crtc_commit:%d", c->base.base.id);
	if (IS_ERR(c->thread)) {
		DRM_ERROR("failed to create crtc_commit kthread\n");
		ret = PTR_ERR(c->thread);
		c->thread = NULL;
		return ret;
	}

	kthread_queue_work(&c->worker, &priv->commit_thread_priority_work);
	kthread_flush_work(&priv->commit_thread_priority_work);

	return 0;
}

static int _msm_hyp_crtc_init(struct drm_device *ddev,
		struct msm_hyp_crtc_info *crtc_info)
{
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct msm_hyp_crtc *crtc;
	int ret = 0;

	crtc = devm_kzalloc(ddev->dev, sizeof(*crtc), GFP_KERNEL);
	if (!crtc)
		return -ENOMEM;

	crtc->info = crtc_info;

	ret = drm_crtc_init_with_planes(ddev, &crtc->base,
			drm_plane_from_index(ddev,
					crtc_info->primary_plane_index),
			NULL, &msm_hyp_crtc_funcs, NULL);
	if (ret)
		return ret;

	if (crtc_info->crtc_funcs)
		crtc->base.helper_private = crtc_info->crtc_funcs;
	else
		crtc->base.helper_private = &msm_hyp_crtc_helper;

	drm_object_attach_property(&crtc->base.base,
			priv->prop_input_fence_timeout,
			CRTC_INPUT_FENCE_TIMEOUT);

	drm_object_attach_property(&crtc->base.base,
			priv->prop_output_fence, 0);

	drm_object_attach_property(&crtc->base.base,
			priv->prop_output_fence_offset, 0);

	drm_object_attach_property(&crtc->base.base,
			priv->prop_crtc_cp_hsic, 0);

	ret = _msm_hyp_crtc_init_caps(crtc);
	if (ret)
		return ret;

	crtc->output_fence = msm_hyp_fence_init(crtc->base.name);
	if (IS_ERR_OR_NULL(crtc->output_fence))
		return PTR_ERR(crtc->output_fence);

	ret = _msm_hyp_crtc_init_dispatch_thread(crtc, priv);
	if (ret)
		return ret;

	return 0;
}

static void msm_hyp_plane_destroy(struct drm_plane *plane)
{
	struct msm_hyp_plane *p = to_msm_hyp_plane(plane);

	if (p->blob_caps)
		drm_property_blob_put(p->blob_caps);

	drm_plane_cleanup(plane);
}

static struct drm_plane_state *
msm_hyp_plane_duplicate_state(struct drm_plane *plane)
{
	struct msm_hyp_plane_state *p_state, *p_oldstate;

	if (!plane || !plane->state) {
		DRM_ERROR("invalid plane %pK\n", plane);
		return NULL;
	}

	p_oldstate = to_msm_hyp_plane_state(plane->state);

	p_state = kmemdup(p_oldstate, sizeof(*p_oldstate), GFP_KERNEL);
	if (!p_state)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane,
			&p_state->base);

	p_state->input_fence = NULL;

	return &p_state->base;
}

static void msm_hyp_plane_destroy_state(
		struct drm_plane *plane,
		struct drm_plane_state *state)
{
	struct msm_hyp_plane_state *p_state;

	if (!plane || !state) {
		DRM_ERROR("invalid plane %pK\n", plane);
		return;
	}

	p_state = to_msm_hyp_plane_state(state);

	__drm_atomic_helper_plane_destroy_state(&p_state->base);

	if (p_state->input_fence)
		msm_hyp_sync_put(p_state->input_fence);

	kfree(p_state);
}

static void msm_hyp_plane_reset(struct drm_plane *plane)
{
	struct drm_device *ddev;
	struct msm_hyp_drm_private *priv;
	uint64_t val;

	struct msm_hyp_plane_state *p_state =
		kzalloc(sizeof(*p_state), GFP_KERNEL);

	if (!p_state)
		return;

	ddev = plane->dev;
	priv = ddev->dev_private;

	if (plane->state)
		msm_hyp_plane_destroy_state(plane,
				plane->state);

	__drm_atomic_helper_plane_reset(plane, &p_state->base);

	if (priv->prop_alpha) {
		if (!_msm_hyp_object_property_get_prop_value(&plane->base,
								priv->prop_alpha,
								&val))
			p_state->alpha = val;
	}

	if (priv->prop_blend_op) {
		if (!_msm_hyp_object_property_get_prop_value(&plane->base,
								priv->prop_blend_op,
								&val))
			p_state->blend_op = val;
	}
}

static int msm_hyp_plane_set_property(
		struct drm_plane *plane,
		struct drm_plane_state *state,
		struct drm_property *property,
		uint64_t val)
{
	struct drm_device *ddev;
	struct msm_hyp_drm_private *priv;
	struct msm_hyp_plane_state *p_state;
	struct drm_property_blob *blob;
	int ret = 0, size = 0;

	if (!plane || !state) {
		DRM_ERROR("invalid plane %pK\n", plane);
		return -EINVAL;
	}

	ddev = plane->dev;
	priv = ddev->dev_private;

	p_state = to_msm_hyp_plane_state(state);

	if (property == priv->prop_input_fence) {
		p_state->input_fence = msm_hyp_sync_get(val);
		p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_INPUT_FENCE;
	} else if (property == priv->prop_zpos) {
		p_state->zpos = val;
		p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_ZPOS;
	} else if (property == priv->prop_blend_op) {
		p_state->blend_op = val;
		p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_BLENDOP;
	} else if (property == priv->prop_alpha) {
		p_state->alpha = val;
		p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_ALPHA;
	} else if (property == priv->prop_csc) {
		if (val)
			size = copy_from_user(&p_state->csc,
				(void __user *)val,
				sizeof(p_state->csc));
		else
			memset(&p_state->csc,
				0x00,
				sizeof(p_state->csc));
		p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_CSC;
	} else if (property == priv->prop_inverse_pma) {
		// TODO: propagate inverse pma  to host?
	} else if (property == priv->prop_dma_csc) {
		pr_debug("plane %d CSC %llX\n", plane->base.id, val);
		if (val) {
			p_state->dma_csc_en = true;
			size = copy_from_user(&p_state->dma_csc,
				(void __user *)val,
				sizeof(p_state->dma_csc));
		} else {
			p_state->dma_csc_en = false;
			memset(&p_state->dma_csc,
				0x00,
				sizeof(p_state->dma_csc));
		}
		if (!size)
			p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_DMA_CSC;
	} else if (property == priv->prop_dma_igc) {
		pr_debug("plane %d DMA IGC %llX\n", plane->base.id, val);
		priv->prop_dma_igc->values[0] = val;
		if (val) {
			blob = drm_property_lookup_blob(ddev, val);
			if (blob) {
				pr_debug("plane %d DMA IGC %llX  SZ %zX\n", plane->base.id,
						val, blob->length);
				p_state->dma_igc_en = true;
				memcpy(&p_state->dma_igc, blob->data,
					min(blob->length, sizeof(p_state->dma_igc)));
				p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_DMA_IGC;
			} else {
				DRM_WARN("invalid DMA IGC blob id %llu\n", val);
			}
		} else {
			p_state->dma_igc_en = false;
			memset(&p_state->dma_igc,
				0x00,
				sizeof(p_state->dma_igc));
			p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_DMA_IGC;
		}
	} else if (property == priv->prop_vig_igc) {
		pr_debug("plane %d VIG IGC %llX\n", plane->base.id, val);
		priv->prop_vig_igc->values[0] = val;
		if (val) {
			blob = drm_property_lookup_blob(ddev, val);
			if (blob) {
				pr_debug("plane %d VIG IGC %llX  SZ %zX\n", plane->base.id,
						val, blob->length);
				p_state->vig_igc_en = true;
				memcpy(&p_state->vig_igc, blob->data,
					min(blob->length, sizeof(p_state->vig_igc)));
				p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_VIG_IGC;
			} else {
				DRM_WARN("invalid VIG IGC blob id %llu\n", val);
			}
		} else {
			p_state->vig_igc_en = false;
			memset(&p_state->vig_igc,
				0x00,
				sizeof(p_state->vig_igc));
			p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_VIG_IGC;
		}
	} else if (property == priv->prop_dma_gc) {
		pr_debug("plane %d DMA GC %llX\n", plane->base.id, val);
		priv->prop_dma_gc->values[0] = val;
		if (val) {
			blob = drm_property_lookup_blob(ddev, val);
			if (blob) {
				pr_debug("plane %d DMA GC %llX  SZ %zX\n", plane->base.id,
						val, blob->length);
				p_state->dma_gc_en = true;
				memcpy(&p_state->dma_gc, blob->data,
					min(blob->length, sizeof(p_state->dma_gc)));
				p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_DMA_GC;
			} else {
				DRM_WARN("invalid DMA GC blob id %llu\n", val);
			}
		} else {
			p_state->dma_gc_en = false;
			memset(&p_state->dma_gc,
				0x00,
				sizeof(p_state->dma_gc));
			p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_DMA_GC;
		}
	} else if (property == priv->prop_vig_gamut) {
		pr_debug("plane %d VIG GAMUT %llX\n", plane->base.id, val);
		priv->prop_vig_gamut->values[0] = val;
		if (val) {
			blob = drm_property_lookup_blob(ddev, val);
			if (blob) {
				pr_debug("plane %d VIG GAMUT %llX	SZ %zX\n", plane->base.id,
						val, blob->length);
				p_state->gamut_en = true;
				memcpy(&p_state->gamut, blob->data,
					min(blob->length, sizeof(p_state->gamut)));
				p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_GAMUT;
			} else {
				DRM_WARN("invalid VIG GAMUT blob id %llu\n", val);
			}
		} else {
			p_state->gamut_en = false;
			memset(&p_state->gamut,
				0x00,
				sizeof(p_state->gamut));
			p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_GAMUT;
		}
	} else if (property == priv->prop_scaler) {
		if (val)
			size = copy_from_user(&p_state->scaler,
				(void __user *)val,
				sizeof(p_state->scaler));
		else
			memset(&p_state->scaler,
				0x00,
				sizeof(p_state->scaler));
		if (!size)
			p_state->dirty_flags |= MSM_HYP_PLANE_DIRTY_SCALER;
	} else if (property == priv->prop_multirect_mode) {
		p_state->multirect_mode = val;
	} else if (property == priv->prop_fb_translation_mode) {
		if (val > SDE_DRM_FB_SEC) {
			DRM_ERROR("fb_mode %d not supported\n", (int)val);
			return -EINVAL;
		}
		p_state->fb_mode = val;
	} else {
		DRM_ERROR("invalid prop %s\n", property->name);
		ret = -EINVAL;
	}

	if (size)
		ret = -EINVAL;

	return ret;
}

static int msm_hyp_plane_get_property(
		struct drm_plane *plane,
		const struct drm_plane_state *state,
		struct drm_property *property,
		uint64_t *val)
{
	struct drm_device *ddev;
	struct msm_hyp_drm_private *priv;
	struct msm_hyp_plane_state *p_state;
	int ret = 0;

	if (!plane || !state || !val) {
		DRM_ERROR("invalid plane %pK\n", plane);
		return -EINVAL;
	}

	ddev = plane->dev;
	priv = ddev->dev_private;

	p_state = to_msm_hyp_plane_state(state);

	if (property == priv->prop_input_fence) {
		*val = ~0;
	} else if (property == priv->prop_zpos) {
		*val = p_state->zpos;
	} else if (property == priv->prop_blend_op) {
		*val = p_state->blend_op;
	} else if (property == priv->prop_alpha) {
		*val = p_state->alpha;
	} else if (property == priv->prop_csc) {
		*val = 0;
	} else if (property == priv->prop_inverse_pma) {
		*val = 1;
	} else if (property == priv->prop_dma_csc) {
		*val = 0;
	} else if (property == priv->prop_dma_igc) {
		*val = priv->prop_dma_igc->values[0];
	} else if (property == priv->prop_dma_gc) {
		*val = priv->prop_dma_gc->values[0];
	} else if (property == priv->prop_vig_igc) {
		*val = priv->prop_vig_igc->values[0];
	} else if (property == priv->prop_vig_gamut) {
		*val = priv->prop_vig_gamut->values[0];
	} else if (property == priv->prop_scaler) {
		*val = 0;
	} else if (property == priv->prop_multirect_mode) {
		*val = p_state->multirect_mode;
	} else if (property == priv->prop_fb_translation_mode) {
		*val = p_state->fb_mode;
	} else {
		DRM_ERROR("invalid prop %s\n", property->name);
		ret = -EINVAL;
	}

	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
static bool msm_hyp_plane_format_mod_supported(struct drm_plane *plane,
		uint32_t format, uint64_t modifier)
{
	bool ret = false;

	if (modifier == DRM_FORMAT_MOD_LINEAR ||
		modifier == DRM_FORMAT_MOD_QTI_SECURE) {
		if (format == DRM_FORMAT_C8 ||
			format == DRM_FORMAT_ARGB4444 ||
			format == DRM_FORMAT_XRGB4444 ||
			format == DRM_FORMAT_ARGB1555 ||
			format == DRM_FORMAT_XRGB1555 ||
			format == DRM_FORMAT_RGB565 ||
			format == DRM_FORMAT_RGB888 ||
			format == DRM_FORMAT_ARGB8888 ||
			format == DRM_FORMAT_XRGB8888 ||
			format == DRM_FORMAT_YVU410 ||
			format == DRM_FORMAT_YUV420 ||
			format == DRM_FORMAT_NV12 ||
			format == DRM_FORMAT_YVU420 ||
			format == DRM_FORMAT_UYVY ||
			format == DRM_FORMAT_YUYV ||
			format == DRM_FORMAT_YVYU ||
			format == DRM_FORMAT_VYUY ||
			format == DRM_FORMAT_AYUV ||
			format == DRM_FORMAT_ABGR8888 ||
			format == DRM_FORMAT_XBGR8888 ||
			format == DRM_FORMAT_BGR565 ||
			format == DRM_FORMAT_ARGB2101010 ||
			format == DRM_FORMAT_XRGB2101010 ||
			format == DRM_FORMAT_ABGR2101010 ||
			format == DRM_FORMAT_XBGR2101010 ||
			format == DRM_FORMAT_BGR888) {
			/* Linear formats */
			ret = true;
		}
	} else if (modifier == DRM_FORMAT_MOD_QTI_COMPRESSED ||
				modifier == DRM_FORMAT_MOD_QTI_COMPRESSED_SECURE) {
		if (format == DRM_FORMAT_BGR565 ||
			format == DRM_FORMAT_ABGR8888 ||
			format == DRM_FORMAT_XBGR8888 ||
			format == DRM_FORMAT_ABGR2101010 ||
			format == DRM_FORMAT_XBGR2101010 ||
			format == DRM_FORMAT_NV12) {
			/* UBWC formats */
			ret = true;
		}
	} else if (format == DRM_FORMAT_NV12) {
		if (modifier == DRM_FORMAT_MOD_QTI_DX ||
			modifier == (DRM_FORMAT_MOD_QTI_COMPRESSED |
			DRM_FORMAT_MOD_QTI_DX) ||
			modifier == (DRM_FORMAT_MOD_QTI_COMPRESSED |
			DRM_FORMAT_MOD_QTI_DX | DRM_FORMAT_MOD_QTI_TIGHT) ||
			modifier == (DRM_FORMAT_MOD_QTI_DX |
			DRM_FORMAT_MOD_QTI_SECURE) ||
			modifier == (DRM_FORMAT_MOD_QTI_COMPRESSED_SECURE |
			DRM_FORMAT_MOD_QTI_DX) ||
			modifier == (DRM_FORMAT_MOD_QTI_COMPRESSED_SECURE |
			DRM_FORMAT_MOD_QTI_DX | DRM_FORMAT_MOD_QTI_TIGHT)) {
			/* P010/TP10 formats */
			ret = true;
		}
	}

	return ret;
}
#endif

static const struct drm_plane_funcs msm_hyp_plane_funcs = {
		.update_plane = drm_atomic_helper_update_plane,
		.disable_plane = drm_atomic_helper_disable_plane,
		.destroy = msm_hyp_plane_destroy,
		.atomic_set_property = msm_hyp_plane_set_property,
		.atomic_get_property = msm_hyp_plane_get_property,
		.reset = msm_hyp_plane_reset,
		.atomic_duplicate_state = msm_hyp_plane_duplicate_state,
		.atomic_destroy_state = msm_hyp_plane_destroy_state,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
		.format_mod_supported = msm_hyp_plane_format_mod_supported,
#endif
};

const struct drm_plane_helper_funcs msm_hyp_plane_helper = {
	/* emptry */
};

static int _msm_hyp_plane_init_caps(struct msm_hyp_plane *plane)
{
	struct drm_device *ddev = plane->base.dev;
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct msm_hyp_prop_blob_info *info;
	char buf[32];
	int ret;

	info = devm_kzalloc(ddev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	msm_hyp_prop_info_populate_plane_format(&plane->base, info);

	if (plane->primary_plane) {
		snprintf(buf, sizeof(buf), "%d",
				plane->primary_plane->base.id);
		msm_hyp_prop_info_add_keystr(info,
				"primary_smart_plane_id", buf);
	}

	snprintf(buf, sizeof(buf), "%d", plane->info->max_width);
	msm_hyp_prop_info_add_keystr(info, "max_linewidth", buf);

	snprintf(buf, sizeof(buf), "%d", plane->info->maxupscale);
	msm_hyp_prop_info_add_keystr(info, "max_upscale", buf);

	snprintf(buf, sizeof(buf), "%d", plane->info->maxdwnscale);
	msm_hyp_prop_info_add_keystr(info, "max_downscale", buf);

	snprintf(buf, sizeof(buf), "%d", plane->info->maxhdeciexp);
	msm_hyp_prop_info_add_keystr(info, "max_horizontal_deci", buf);

	snprintf(buf, sizeof(buf), "%d", plane->info->maxvdeciexp);
	msm_hyp_prop_info_add_keystr(info, "max_vertical_deci", buf);

	snprintf(buf, sizeof(buf), "%lld", plane->info->max_bandwidth);
	msm_hyp_prop_info_add_keystr(info, "max_per_pipe_bw", buf);

	if (plane->info->extra_caps)
		msm_hyp_prop_info_append(info, plane->info->extra_caps);

	plane->blob_caps = drm_property_create_blob(ddev,
			info->len, info->data);
	if (IS_ERR_OR_NULL(plane->blob_caps)) {
		ret = PTR_ERR(plane->blob_caps);
		DRM_ERROR("failed to create blob, %d\n", ret);
		return ret;
	}

	drm_object_attach_property(&plane->base.base,
			priv->prop_plane_caps,
			plane->blob_caps->base.id);

	return 0;
}

static int _msm_hyp_plane_init(struct drm_device *ddev,
		struct msm_hyp_plane_info *plane_info)
{
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct msm_hyp_plane *plane;
	unsigned int supported_rotations = DRM_MODE_ROTATE_0 |
		DRM_MODE_ROTATE_180 | DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y;
	int ret = 0;

	uint64_t modifiers[] = {DRM_FORMAT_MOD_LINEAR,
		DRM_FORMAT_MOD_QTI_COMPRESSED, DRM_FORMAT_MOD_QTI_SECURE,
		DRM_FORMAT_MOD_QTI_COMPRESSED_SECURE,
		DRM_FORMAT_MOD_QTI_DX, DRM_FORMAT_MOD_QTI_COMPRESSED |
		DRM_FORMAT_MOD_QTI_DX, DRM_FORMAT_MOD_QTI_COMPRESSED |
		DRM_FORMAT_MOD_QTI_DX | DRM_FORMAT_MOD_QTI_TIGHT,
		DRM_FORMAT_MOD_QTI_DX | DRM_FORMAT_MOD_QTI_SECURE,
		DRM_FORMAT_MOD_QTI_DX | DRM_FORMAT_MOD_QTI_COMPRESSED_SECURE,
		DRM_FORMAT_MOD_QTI_DX | DRM_FORMAT_MOD_QTI_TIGHT |
		DRM_FORMAT_MOD_QTI_COMPRESSED_SECURE, DRM_FORMAT_MOD_INVALID};

	plane = devm_kzalloc(ddev->dev, sizeof(struct msm_hyp_plane),
			GFP_KERNEL);
	if (!plane)
		return -ENOMEM;

	plane->info = plane_info;

	ret = drm_universal_plane_init(ddev,
			&plane->base,
			plane_info->possible_crtcs,
			&msm_hyp_plane_funcs,
			plane_info->format_types,
			plane_info->format_count,
			modifiers, plane_info->plane_type, NULL);
	if (ret)
		return ret;

	drm_object_attach_property(&plane->base.base,
		priv->prop_input_fence, 0);

	drm_object_attach_property(&plane->base.base,
		priv->prop_zpos, 0);

	if (plane->info->support_scale)
		drm_object_attach_property(&plane->base.base,
				priv->prop_scaler, 0);

	drm_object_attach_property(&plane->base.base,
			priv->prop_inverse_pma, 0);

	if (plane->info->support_csc)
		drm_object_attach_property(&plane->base.base,
				priv->prop_csc, 0);

	if (plane->info->vig_pipe) {
		drm_object_attach_property(&plane->base.base,
				priv->prop_vig_igc, 0);
		drm_object_attach_property(&plane->base.base,
				priv->prop_vig_gamut, 0);
	} else {
		drm_object_attach_property(&plane->base.base,
				priv->prop_dma_igc, 0);
		drm_object_attach_property(&plane->base.base,
				priv->prop_dma_gc, 0);
		drm_object_attach_property(&plane->base.base,
				priv->prop_dma_csc, 0);
	}

	if (plane->info->support_multirect) {
		drm_object_attach_property(&plane->base.base,
				priv->prop_multirect_mode,
				DRM_MULTIRECT_NONE);
		plane->primary_plane = drm_plane_from_index(ddev,
				plane_info->master_plane_index);
	}

	if (plane->info->support_rotation)
		supported_rotations |= DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270;

	/* support 180, x flip and y flip by default */
	ret = drm_plane_create_rotation_property(&plane->base,
			DRM_MODE_ROTATE_0, supported_rotations);
	if (ret)
		return ret;

	drm_object_attach_property(&plane->base.base,
			priv->prop_blend_op,
			SDE_DRM_BLEND_OP_PREMULTIPLIED);

	drm_object_attach_property(&plane->base.base,
			priv->prop_alpha, 0xFF);

	drm_object_attach_property(&plane->base.base,
			priv->prop_fb_translation_mode,
			SDE_DRM_FB_NON_SEC);

	ret = _msm_hyp_plane_init_caps(plane);
	if (ret)
		return ret;

	if (plane->info->plane_funcs)
		plane->base.helper_private = plane->info->plane_funcs;
	else
		plane->base.helper_private = &msm_hyp_plane_helper;

	return 0;
}

static int _msm_hyp_connectors_init(struct drm_device *ddev)
{
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct msm_hyp_kms *kms = priv->kms;
	struct msm_hyp_connector_info *connector_infos[MAX_CONNECTORS];
	uint32_t num, i;
	int ret;

	if (!kms->funcs || !kms->funcs->get_connector_infos)
		return -EINVAL;

	ret = kms->funcs->get_connector_infos(kms, NULL, &num);
	if (ret)
		return ret;

	if (num >= MAX_CONNECTORS)
		return -EINVAL;

	ret = kms->funcs->get_connector_infos(kms, connector_infos, &num);
	if (ret)
		return ret;

	for (i = 0; i < num; i++) {
		ret = _msm_hyp_connector_encoder_init(ddev,
				connector_infos[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int _msm_hyp_planes_init(struct drm_device *ddev)
{
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct msm_hyp_kms *kms = priv->kms;
	struct msm_hyp_plane_info *plane_infos[MSM_HYP_MAX_PLANES];
	uint32_t num = 0, i;
	int ret;

	if (!kms->funcs || !kms->funcs->get_plane_infos)
		return -EINVAL;

	ret = kms->funcs->get_plane_infos(kms, NULL, &num);
	if (ret)
		return ret;

	if (num >= MSM_HYP_MAX_PLANES)
		return -EINVAL;

	ret = kms->funcs->get_plane_infos(kms, plane_infos, &num);
	if (ret)
		return ret;

	for (i = 0; i < num; i++) {
		ret = _msm_hyp_plane_init(ddev, plane_infos[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int _msm_hyp_crtcs_init(struct drm_device *ddev)
{
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct msm_hyp_kms *kms = priv->kms;
	struct msm_hyp_crtc_info *crtc_infos[MAX_CRTCS];
	uint32_t num = 0, i;
	int ret;

	if (!kms->funcs || !kms->funcs->get_crtc_infos)
		return -EINVAL;

	ret = kms->funcs->get_crtc_infos(kms, NULL, &num);
	if (ret)
		return ret;

	if (num >= MAX_CRTCS)
		return -EINVAL;

	ret = kms->funcs->get_crtc_infos(kms, crtc_infos, &num);
	if (ret)
		return ret;

	for (i = 0; i < num; i++) {
		ret = _msm_hyp_crtc_init(ddev, crtc_infos[i]);
		if (ret)
			return ret;
	}

	ret = drm_vblank_init(ddev, num);
	if (ret < 0) {
		DRM_ERROR("failed to initialize vblank\n");
		return ret;
	}

	return 0;
}

static int _msm_hyp_obj_init(struct drm_device *ddev)
{
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct msm_hyp_kms *kms = priv->kms;
	int ret;

	ret = _msm_hyp_connectors_init(ddev);
	if (ret)
		return ret;

	ret = _msm_hyp_planes_init(ddev);
	if (ret)
		return ret;

	ret = _msm_hyp_crtcs_init(ddev);
	if (ret)
		return ret;

	init_waitqueue_head(&priv->pending_crtcs_event);
	/* Register for WFD events(HPD)*/
	if (kms->funcs && kms->funcs->register_event)
		kms->funcs->register_event(kms);

	return 0;
}

void msm_hyp_framebuffer_destroy(struct drm_framebuffer *framebuffer)
{
	struct msm_hyp_framebuffer *fb = to_msm_hyp_fb(framebuffer);

	DRM_DEBUG("destroy: FB ID: %d (%pK)", fb->base.base.id, fb);

	if (fb->info && fb->info->destroy)
		fb->info->destroy(framebuffer);
	drm_gem_fb_destroy(framebuffer);
}

static const struct drm_framebuffer_funcs msm_hyp_framebuffer_funcs = {
	.create_handle = drm_gem_fb_create_handle,
	.destroy = msm_hyp_framebuffer_destroy,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 0))
static int msm_hyp_shmem_sync_sg_for_device(struct drm_gem_object *obj)
{
	struct sg_table *sgt;
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	if(!shmem)
		return -EINVAL;

	if (shmem->base.import_attach)
		return 0;

	sgt = drm_gem_shmem_get_pages_sgt(shmem);
	if (IS_ERR(sgt))
		return PTR_ERR(sgt);

	dma_sync_sg_for_device(shmem->base.dev->dev, sgt->sgl,
		sgt->nents, DMA_BIDIRECTIONAL);

	return 0;
}
#else
static int msm_hyp_shmem_sync_sg_for_device(struct drm_gem_object *obj)
{
	struct sg_table *sgt;

	if (obj->import_attach)
		return 0;

	sgt = drm_gem_shmem_get_pages_sgt(obj);
	if (IS_ERR(sgt))
		return PTR_ERR(sgt);

	dma_sync_sg_for_device(obj->dev->dev, sgt->sgl,
			sgt->nents, DMA_BIDIRECTIONAL);

	return 0;
}
#endif

static struct drm_framebuffer *msm_hyp_framebuffer_init(struct drm_device *dev,
		const struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **bos)
{
	const struct drm_format_info *info = drm_get_format_info(dev, mode_cmd);
	struct msm_hyp_drm_private *priv = dev->dev_private;
	struct msm_hyp_kms *kms = priv->kms;
	struct msm_hyp_framebuffer *msm_hyp_fb = NULL;
	struct drm_framebuffer *fb = NULL;
	int ret, i, num_planes, width = 0, height = 0, min_size = 0;

	DRM_DEBUG("create framebuffer: dev=%pK, mode_cmd=%pK (%dx%d@%4.4s)",
			dev, mode_cmd, mode_cmd->width, mode_cmd->height,
			(char *)&mode_cmd->pixel_format);
	if (!info) {
		DRM_ERROR("drm format info is not present\n");
		return NULL;
	}

	num_planes = info->num_planes;

	msm_hyp_fb = kzalloc(sizeof(*msm_hyp_fb), GFP_KERNEL);
	if (!msm_hyp_fb) {
		ret = -ENOMEM;
		goto fail;
	}

	fb = &msm_hyp_fb->base;

	if (num_planes > ARRAY_SIZE(fb->obj)) {
		DRM_ERROR("num of planes is more than array of framebuffer objects");
		ret = -EINVAL;
		goto fail;
	}

	for (i = 0; i < num_planes; i++) {
		width = mode_cmd->width / (i ? info->hsub : 1);
		height = mode_cmd->height / (i ? info->vsub : 1);

		min_size = (height - 1) * mode_cmd->pitches[i]
			 + width * info->cpp[i]
			 + mode_cmd->offsets[i];

		if (bos[i]->size < min_size) {
			DRM_ERROR("gem obj bo size is less than min_size\n");
			ret = -EINVAL;
			goto fail;
		}

		msm_hyp_fb->base.obj[i] = bos[i];
	}

	drm_helper_mode_fill_fb_struct(dev, fb, mode_cmd);

	ret = drm_framebuffer_init(dev, fb, &msm_hyp_framebuffer_funcs);
	if (ret) {
		DRM_ERROR("framebuffer init failed: %d\n", ret);
		goto fail;
	}

	if (kms->funcs && kms->funcs->get_framebuffer_info) {
		ret = kms->funcs->get_framebuffer_info(kms, fb,
				&msm_hyp_fb->info);
		if (ret) {
			DRM_ERROR("failed to get framebuffer info\n");
			goto cleanup;
		}
	}

	return fb;
cleanup:
	drm_framebuffer_cleanup(fb);
fail:
	kfree(msm_hyp_fb);

	return ERR_PTR(ret);
}

static struct drm_framebuffer *msm_hyp_framebuffer_create(
		struct drm_device *dev, struct drm_file *file,
		const struct drm_mode_fb_cmd2 *mode_cmd)
{
	const struct drm_format_info *info = drm_get_format_info(dev, mode_cmd);
	struct drm_framebuffer *fb;
	struct drm_gem_object *bos[DRM_FORMAT_MAX_PLANES] = {0};
	int ret, i, num_planes;

	if (!info) {
		DRM_ERROR("drm format info is not present\n");
		return NULL;
	}

	num_planes = info->num_planes;
	/* TODO: this part should support multiple handles */
	for (i = 0; i < num_planes; i++) {
		bos[i] = drm_gem_object_lookup(file, mode_cmd->handles[i]);
		if (IS_ERR_OR_NULL(bos[i])) {
			DRM_ERROR("failed to find gem bo %d\n", mode_cmd->handles[i]);
			ret = -EINVAL;
			goto out_unref;
		}
	}

	fb = msm_hyp_framebuffer_init(dev, mode_cmd, bos);
	if (IS_ERR(fb)) {
		ret = PTR_ERR(fb);
		DRM_ERROR("frame buffer init is failed %d\n", ret);
		goto out_unref;
	}

	return fb;

out_unref:
	for (i = 0; i < num_planes; i++)
		drm_gem_object_put(bos[i]);
	return ERR_PTR(ret);
}

static int _msm_hyp_start_atomic(struct msm_hyp_drm_private *priv,
		uint32_t crtc_mask)
{
	int ret;

	spin_lock(&priv->pending_crtcs_event.lock);
	ret = wait_event_interruptible_locked(priv->pending_crtcs_event,
			!(priv->pending_crtcs & crtc_mask));
	if (ret == 0) {
		DRM_DEBUG("start: %08x", crtc_mask);
		priv->pending_crtcs |= crtc_mask;
	}
	spin_unlock(&priv->pending_crtcs_event.lock);

	return ret;
}

static void _msm_hyp_end_atomic(struct msm_hyp_drm_private *priv,
		uint32_t crtc_mask)
{
	HYP_ATRACE_BEGIN(__func__);

	spin_lock(&priv->pending_crtcs_event.lock);
	DRM_DEBUG("end: %08x", crtc_mask);
	priv->pending_crtcs &= ~crtc_mask;
	wake_up_all_locked(&priv->pending_crtcs_event);
	spin_unlock(&priv->pending_crtcs_event.lock);

	HYP_ATRACE_END(__func__);
}

static void _msm_hyp_prepare_fence(
		struct drm_device *dev,
		struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct msm_hyp_crtc *c;
	struct msm_hyp_crtc_state *cstate;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct msm_hyp_connector *conn;
	struct msm_hyp_connector_state *conn_state;
	int i;

	for_each_new_crtc_in_state(old_state, crtc, crtc_state, i) {
		c = to_msm_hyp_crtc(crtc);
		cstate = to_msm_hyp_crtc_state(crtc->state);

		msm_hyp_fence_prepare(c->output_fence);

		/* create output fence */
		if (cstate->output_fence_ptr) {
			msm_hyp_fence_create(c->output_fence,
					cstate->output_fence_ptr, 1);
			cstate->output_fence_ptr = NULL;
		}

		drm_connector_list_iter_begin(dev, &conn_iter);
		drm_for_each_connector_iter(connector, &conn_iter)
			if (crtc_state->connector_mask &
					drm_connector_mask(connector)) {
				conn = to_msm_hyp_connector(connector);
				conn_state = to_msm_hyp_connector_state(
						connector->state);

				msm_hyp_fence_prepare(conn->retire_fence);

				/* create retire fence */
				if (conn_state->retire_fence_ptr) {
					msm_hyp_fence_create(
						conn->retire_fence,
						conn_state->retire_fence_ptr,
						0);
					conn_state->retire_fence_ptr = NULL;
				}
			}
		drm_connector_list_iter_end(&conn_iter);
	}
}

void msm_hyp_crtc_commit_done(struct drm_crtc *crtc)
{
	struct msm_hyp_crtc *c = to_msm_hyp_crtc(crtc);
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;
	struct msm_hyp_connector *conn;
	unsigned int pipe;
	struct drm_device *dev;
	struct drm_vblank_crtc *vblank;

	if (!crtc)
		return;

	HYP_ATRACE_BEGIN(__func__);

	pipe = drm_crtc_index(crtc);
	dev = crtc->dev;
	vblank = &dev->vblank[pipe];

	complete_all(&c->commit_done);

	/* signal output fence */
	msm_hyp_fence_signal(c->output_fence);

	/* signal retire fence */
	drm_connector_list_iter_begin(c->base.dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter)
		if (c->base.state->connector_mask &
				drm_connector_mask(connector)) {
			conn = to_msm_hyp_connector(connector);
			msm_hyp_fence_signal(conn->retire_fence);
		}
	drm_connector_list_iter_end(&conn_iter);

	spin_lock(&dev->vblank_time_lock);

	write_seqlock(&vblank->seqlock);
	vblank->time = ktime_get();
	atomic64_inc(&vblank->count);
	write_sequnlock(&vblank->seqlock);

	spin_unlock(&dev->vblank_time_lock);

	/* send vblank event */
	spin_lock(&dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock(&dev->event_lock);

	HYP_ATRACE_END(__func__);

	DRM_DEBUG("crtc %d commit done\n", crtc->base.id);
}

static void _msm_hyp_atomic_prepare_commit(struct drm_device *ddev,
		struct drm_atomic_state *old_state)
{
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct msm_hyp_kms *kms = priv->kms;

	HYP_ATRACE_BEGIN(__func__);

	if (kms->funcs && kms->funcs->prepare_commit)
		kms->funcs->prepare_commit(kms, old_state);

	HYP_ATRACE_END(__func__);
}

static void _msm_hyp_atomic_commit(struct drm_device *ddev,
		struct drm_atomic_state *old_state)
{
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct msm_hyp_kms *kms = priv->kms;
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	struct drm_crtc_state *crtc_state;
	struct msm_hyp_crtc_state *cstate;
	struct msm_hyp_plane_state *pstate;
	struct drm_gem_object *obj;
	int i, j, ret;

	HYP_ATRACE_BEGIN(__func__);

	for_each_new_crtc_in_state(old_state, crtc, crtc_state, i) {
		if (!crtc_state->active)
			continue;

		cstate = to_msm_hyp_crtc_state(crtc->state);
		drm_atomic_crtc_for_each_plane(plane, crtc) {
			/*
			 * Based on DMA API guide, if same streaming DMA region would be used
			 * multiple times, and the data would be touched in between the DMA
			 * transfers, then the buffer needs to be synced properly in order for
			 * the CPU and device to see the most up-to-date and correct copy of
			 * the DMA buffer.
			 * Currently the sync logic would only work for DRM dumb buffers,
			 * since mandatory syncing for dma buffer would cost 0.35~0.45 ms each
			 * commit, which would be a huge impact.
			 * KPI impact:
			 * For dma buffers (not to sync), each commit costs 0.2~0.3 us
			 * additionally;
			 * For dumb buffers (to sync), each commit costs 0.25~0.35 ms
			 * additionally.
			 */
			for (j = 0; j < plane->state->fb->format->num_planes; ++j) {
				obj = drm_gem_fb_get_obj(plane->state->fb, j);
				ret = msm_hyp_shmem_sync_sg_for_device(obj);
				if (ret)
					DRM_ERROR("failed to do dumb buffer sync\n");
			}
			pstate = to_msm_hyp_plane_state(plane->state);

			msm_hyp_sync_wait(pstate->input_fence,
					cstate->input_fence_timeout);
		}
	}

	if (kms->funcs && kms->funcs->commit)
		kms->funcs->commit(kms, old_state);

	HYP_ATRACE_END(__func__);
}

static void _msm_hyp_atomic_complete_commit(struct drm_device *ddev,
		struct drm_atomic_state *old_state)
{
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	struct msm_hyp_kms *kms = priv->kms;

	HYP_ATRACE_BEGIN(__func__);

	if (kms->funcs && kms->funcs->complete_commit)
		kms->funcs->complete_commit(kms, old_state);

	HYP_ATRACE_END(__func__);
}

static void _msm_hyp_atomic_wait_for_commit_done(
		struct drm_device *dev,
		struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct msm_hyp_crtc *c;
	int i;

	HYP_ATRACE_BEGIN(__func__);

	for_each_new_crtc_in_state(old_state, crtc, new_crtc_state, i) {
		c = to_msm_hyp_crtc(crtc);
		wait_for_completion(&c->commit_done);
	}

	HYP_ATRACE_END(__func__);
}

static void _msm_hyp_complete_commit(struct msm_hyp_commit *c)
{
	struct drm_device *dev = c->dev;
	struct drm_atomic_state *old_state = c->state;
	static bool first_frame = true;

	HYP_ATRACE_BEGIN("Input_Fence_WAIT");

	if (first_frame) {
		pr_info("kernel_fe: First commit start\n");
		first_frame = false;
	}

	drm_atomic_helper_wait_for_fences(dev, old_state, false);

	HYP_ATRACE_END("Input_Fence_WAIT");

	HYP_ATRACE_BEGIN(__func__);

	_msm_hyp_atomic_prepare_commit(dev, old_state);

	drm_atomic_helper_commit_modeset_disables(dev, old_state);

	drm_atomic_helper_commit_planes(dev, old_state, 0);

	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	_msm_hyp_atomic_commit(dev, old_state);

	_msm_hyp_atomic_wait_for_commit_done(dev, old_state);

	drm_atomic_helper_cleanup_planes(dev, old_state);

	_msm_hyp_atomic_complete_commit(dev, old_state);

	drm_atomic_state_put(old_state);

	_msm_hyp_end_atomic(c->dev->dev_private, c->crtc_mask);

	if (c->nonblock)
		kfree(c);

	HYP_ATRACE_END(__func__);
}

static void _msm_hyp_drm_commit_work_cb(struct kthread_work *work)
{
	struct msm_hyp_commit *commit;

	if (!work) {
		DRM_ERROR("%s: Invalid commit work data!\n", __func__);
		return;
	}

	commit = container_of(work, struct msm_hyp_commit, commit_work);

	_msm_hyp_complete_commit(commit);
}

static struct msm_hyp_commit *_msm_hyp_commit_init(struct drm_atomic_state *state,
		bool nonblock)
{
	struct msm_hyp_commit *c = kzalloc(sizeof(*c), GFP_KERNEL);

	if (!c)
		return NULL;

	c->dev = state->dev;
	c->state = state;
	c->nonblock = nonblock;

	kthread_init_work(&c->commit_work, _msm_hyp_drm_commit_work_cb);

	return c;
}

static void _msm_hyp_atomic_commit_dispatch(struct drm_device *dev,
		struct drm_atomic_state *state, struct msm_hyp_commit *commit)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct msm_hyp_crtc *c;
	int ret = -EINVAL, i;
	bool nonblock;

	HYP_ATRACE_BEGIN(__func__);

	/* cache since work will kfree commit in non-blocking case */
	nonblock = commit->nonblock;

	for_each_old_crtc_in_state(state, crtc, crtc_state, i) {
		c = to_msm_hyp_crtc(crtc);
		init_completion(&c->commit_done);
	}

	for_each_old_crtc_in_state(state, crtc, crtc_state, i) {
		c = to_msm_hyp_crtc(crtc);
		kthread_queue_work(&c->worker, &commit->commit_work);
		ret = 0;
		break;
	}

	if (ret)
		_msm_hyp_complete_commit(commit);
	else if (!nonblock)
		kthread_flush_work(&commit->commit_work);

	/* free nonblocking commits in this context, after processing */
	if (!nonblock)
		kfree(commit);

	HYP_ATRACE_END(__func__);
}

static int msm_hyp_atomic_helper_check(struct drm_device *dev,
		struct drm_atomic_state *state)
{
	int ret = 0;

	ret = drm_atomic_helper_check(dev, state);
	if (ret)
	{
		DRM_ERROR("drm_atomic_helper_check - failed\n");
		return ret;
	}

	return ret;
}

static int msm_hyp_atomic_helper_commit(struct drm_device *dev,
		struct drm_atomic_state *state,
		bool nonblock)
{
	struct msm_hyp_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct msm_hyp_commit *c;
	int ret, i;

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret)
		return ret;

	c = _msm_hyp_commit_init(state, nonblock);
	if (!c) {
		ret = -ENOMEM;
		goto error;
	}

	for_each_new_crtc_in_state(state, crtc, crtc_state, i)
		c->crtc_mask |= drm_crtc_mask(crtc);

	ret = _msm_hyp_start_atomic(priv, c->crtc_mask);
	if (ret)
		goto err_free;

	ret = drm_atomic_helper_swap_state(state, false);
	if (ret)
		goto err_end;

	_msm_hyp_prepare_fence(dev, state);

	drm_atomic_state_get(state);

	_msm_hyp_atomic_commit_dispatch(dev, state, c);

	return 0;

err_end:
	_msm_hyp_end_atomic(priv, c->crtc_mask);
err_free:
	kfree(c);
error:
	drm_atomic_helper_cleanup_planes(dev, state);
	return ret;
}

static const struct drm_mode_config_funcs msm_hyp_mode_config_funcs = {
	.fb_create = msm_hyp_framebuffer_create,
	.atomic_check = msm_hyp_atomic_helper_check,
	.atomic_commit = msm_hyp_atomic_helper_commit,
};

static int _msm_hyp_hw_init(struct drm_device *ddev)
{
	int ret = 0;

	ret = _msm_hyp_mode_create_properties(ddev);
	if (ret) {
		DRM_ERROR("drm_mode_create_properties_fe - failed\n");
		goto fail;
	}

	ret = _msm_hyp_obj_init(ddev);
	if (ret) {
		DRM_ERROR("drm_connector_init_fe - failed\n");
		goto fail;
	}

	msm_drm_notify_components(ddev, MSM_COMP_OBJECT_CREATED);

	ddev->mode_config.funcs = &msm_hyp_mode_config_funcs;

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(5, 15, 0))
	ddev->mode_config.allow_fb_modifiers = true;
#endif
	drm_mode_config_reset(ddev);

fail:
	return ret;
}

static int msm_hyp_open(struct drm_device *dev, struct drm_file *file)
{
	return 0;
}

static void msm_hyp_lastclose(struct drm_device *dev)
{
	struct msm_hyp_drm_private *priv = dev->dev_private;
	int ret;

	ret = drm_client_modeset_commit_locked(&priv->client);
	if (ret)
		DRM_ERROR("client modeset commit failed: %d\n", ret);
}

static void msm_hyp_postclose(struct drm_device *dev, struct drm_file *file)
{
#if (KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE)
	if (atomic_read(&dev->open_count) == 1)
		msm_hyp_lastclose(dev);
#endif
}

void msm_hyp_crtc_vblank_done(struct drm_crtc *crtc)
{
	if (WARN_ON(!crtc))
		return;

	drm_crtc_handle_vblank(crtc);
}

static int msm_hyp_ioctl_rmfb2(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct drm_framebuffer *fb = NULL;
	struct drm_framebuffer *fbl = NULL;
	uint32_t *id = data;
	int found = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	fb = drm_framebuffer_lookup(dev, file_priv, *id);
	if (!fb)
		return -ENOENT;

	/* drop extra ref from traversing drm_framebuffer_lookup */
	drm_framebuffer_put(fb);

	mutex_lock(&file_priv->fbs_lock);
	list_for_each_entry(fbl, &file_priv->fbs, filp_head)
		if (fb == fbl)
			found = 1;
	if (!found) {
		mutex_unlock(&file_priv->fbs_lock);
		return -ENOENT;
	}

	list_del_init(&fb->filp_head);
	mutex_unlock(&file_priv->fbs_lock);

	drm_framebuffer_put(fb);

	return 0;
}

static int msm_hyp_ioctl_noop(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int msm_hyp_suspend(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	int ret = 0;

	if (priv->suspend_state)
		drm_atomic_state_put(priv->suspend_state);

	priv->suspend_state = drm_atomic_helper_suspend(ddev);
	if (IS_ERR(priv->suspend_state)) {
		ret = PTR_ERR(priv->suspend_state);
		priv->suspend_state = NULL;
		DRM_ERROR("failed to suspend %d\n", ret);
	}

	return ret;
}

static int msm_hyp_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct msm_hyp_drm_private *priv = ddev->dev_private;
	int ret;

	ret = drm_atomic_helper_resume(ddev, priv->suspend_state);
	if (ret) {
		DRM_ERROR("failed to resume %d\n", ret);
		return ret;
	}

	priv->suspend_state = NULL;
	return ret;
}
#endif

/*
 * Send HPD event to HWC Layer
 *
 * dev : drm_device pointer
 * connector : Connector information
 */
void msm_hyp_send_hpd_event(struct drm_device *dev,
		struct drm_connector *connector)
{
	char name[HPD_STRING_SIZE] = "";
	char status[HPD_STRING_SIZE] = "";
	char bpp[HPD_STRING_SIZE] = "";
	char pattern[HPD_STRING_SIZE] = "";
	char *envp[6] = {};

	snprintf(name, HPD_STRING_SIZE, "name=%s", connector->name);
	snprintf(status, HPD_STRING_SIZE, "status=%s",
		drm_get_connector_status_name(connector->status));
	snprintf(bpp, HPD_STRING_SIZE, "bpp=%d", 0);
	snprintf(pattern, HPD_STRING_SIZE, "pattern=%d", 0);

	DRM_DEBUG("HPDLOG [%s]:[%s] [%s] [%s]\n", name, status,
		bpp, pattern);
	envp[0] = name;
	envp[1] = status;
	envp[2] = bpp;
	envp[3] = pattern;
	envp[4] = "HOTPLUG=1";
	envp[5] = NULL;

	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE,
			envp);
}

static const struct dev_pm_ops msm_hyp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(msm_hyp_suspend, msm_hyp_resume)
};

static const struct drm_ioctl_desc msm_hyp_ioctls[] = {
	DRM_IOCTL_DEF_DRV(MSM_GEM_NEW,      msm_hyp_ioctl_noop,
			DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_CPU_PREP, msm_hyp_ioctl_noop,
			DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_CPU_FINI, msm_hyp_ioctl_noop,
			DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_MADVISE,  msm_hyp_ioctl_noop,
			DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(SDE_WB_CONFIG,    msm_hyp_ioctl_noop,
			DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(MSM_REGISTER_EVENT,  msm_hyp_ioctl_noop,
			DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(MSM_DEREGISTER_EVENT,  msm_hyp_ioctl_noop,
			DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(MSM_RMFB2, msm_hyp_ioctl_rmfb2,
			DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(MSM_POWER_CTRL, msm_hyp_ioctl_noop,
			DRM_RENDER_ALLOW),
};

DEFINE_DRM_GEM_FOPS(fops);

static struct drm_driver msm_hyp_driver = {
	.driver_features    = DRIVER_GEM |
				DRIVER_RENDER |
				DRIVER_ATOMIC |
				DRIVER_MODESET,
	.open               = msm_hyp_open,
	.postclose          = msm_hyp_postclose,
#if (KERNEL_VERSION(6, 12, 0) > LINUX_VERSION_CODE)
	.lastclose          = msm_hyp_lastclose,
#endif
	DRM_GEM_SHMEM_DRIVER_OPS,
	.ioctls             = msm_hyp_ioctls,
	.num_ioctls         = ARRAY_SIZE(msm_hyp_ioctls),
	.fops               = &fops,
	.name               = "msm_drm",
	.desc               = "MSM Snapdragon HYP DRM",
	.date               = "20181031",
	.major              = 1,
	.minor              = 0,
};

static int msm_hyp_bind(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *ddev;
	struct msm_hyp_drm_private *priv;
	int ret;
	const char *dev_name;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->driver = msm_hyp_driver;

	ret = of_property_read_string(pdev->dev.of_node,
			"qcom,dev-name", &dev_name);
	if (ret == 0) {
#if (KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE)
		strscpy(priv->dev_name_from_dt, dev_name, DRM_DRI_NAME_SIZE);
#else
		strlcpy(priv->dev_name_from_dt, dev_name, DRM_DRI_NAME_SIZE);
#endif
		priv->driver.name = priv->dev_name_from_dt;
	}

	ddev = drm_dev_alloc(&priv->driver, dev);
	if (IS_ERR_OR_NULL(ddev)) {
		dev_err(dev, "failed to allocate drm_device\n");
		return PTR_ERR(ddev);
	}

	drm_mode_config_init(ddev);
	platform_set_drvdata(pdev, ddev);

	ddev->dev_private = priv;
	priv->dev = ddev;

	ret = component_bind_all(dev, ddev);
	if (ret)
		goto fail;

	if (!priv->kms) {
		dev_err(dev, "failed to find kms component\n");
		goto fail;
	}

	ret = _msm_hyp_hw_init(ddev);
	if (ret) {
		dev_err(dev, "failed to create hyp device\n");
		goto fail;
	}

	/*
	 * DMA_BIT_MASK encountered compilation error,
	 * shift-count-overflow on special target
	 */
	dma_coerce_mask_and_coherent(dev, ~0ULL);

	ret = drm_dev_register(ddev, 0);
	if (ret) {
		dev_err(dev, "failed to register drm device\n");
		goto fail;
	}

	ret = drm_client_init(ddev, &priv->client, "msm_hyp_client", NULL);
	if (ret) {
		DRM_ERROR("failed to init client: %d\n", ret);
		priv->client.dev = NULL;
		goto fail;
	}

	drm_client_register(&priv->client);

	return 0;

fail:
	drm_dev_put(ddev);
	return ret;
}

static void msm_hyp_unbind(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	drm_dev_unregister(ddev);

	component_unbind_all(dev, ddev);

	ddev->dev_private = NULL;

	drm_dev_put(ddev);
}

static const struct component_master_ops msm_hyp_ops = {
	.bind = msm_hyp_bind,
	.unbind = msm_hyp_unbind,
};

static int compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int msm_hyp_pdev_probe(struct platform_device *pdev)
{
	struct component_match *match = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *node;
	unsigned int i;
	int ret;

	for (i = 0; ; i++) {
		node = of_parse_phandle(np, "qcom,kms", i);
		if (!node)
			break;

		component_match_add(&pdev->dev, &match, compare_of, node);
	}

	if (!match)
		return -ENODEV;

	ret = component_master_add_with_match(&pdev->dev, &msm_hyp_ops, match);
	if (ret)
		goto fail;

	pr_info("kernel_fe: msm_hyp probe ready\n");

	return 0;

fail:
	of_platform_depopulate(&pdev->dev);
	return ret;
}

#if (KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE)
static void msm_hyp_pdev_remove(struct platform_device *pdev)
#else
static int msm_hyp_pdev_remove(struct platform_device *pdev)
#endif
{
	component_master_del(&pdev->dev, &msm_hyp_ops);
	of_platform_depopulate(&pdev->dev);

#if (KERNEL_VERSION(6, 12, 0) > LINUX_VERSION_CODE)
	return 0;
 #endif
}

static const struct platform_device_id msm_id[] = {
	{ "mdp-hyp", 0 },
	{ }
};

static const struct of_device_id dt_match[] = {
	{ .compatible = "qcom,sde-kms-hyp" },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static struct platform_driver msm_platform_driver = {
	.probe      = msm_hyp_pdev_probe,
	.remove     = msm_hyp_pdev_remove,
	.driver     = {
		.name   = "msm_drm_hyp",
		.of_match_table = dt_match,
		.pm = &msm_hyp_pm_ops,
	},
	.id_table   = msm_id,
};

static int __init msm_drm_register(void)
{
	DRM_DEBUG("init");
	wfd_kms_register();
	virtio_kms_register();
	return platform_driver_register(&msm_platform_driver);
}

static void __exit msm_drm_unregister(void)
{
	DRM_DEBUG("fini");
	platform_driver_unregister(&msm_platform_driver);
	wfd_kms_unregister();
	virtio_kms_unregister();
}

module_init(msm_drm_register);
module_exit(msm_drm_unregister);

MODULE_DESCRIPTION("MSM DRM HYP Driver");
MODULE_LICENSE("GPL v2");
