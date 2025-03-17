/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2017-2018,2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
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
#include <soc/qcom/boot_stats.h>
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
#include "sde_connector.h"
#include "sde_plane.h"
#include "sde_crtc.h"
#include "sde_encoder.h"

#define CRTC_INPUT_FENCE_TIMEOUT    10000
#define HPD_STRING_SIZE             30

static struct msm_hyp_kms *g_hyp_kms = NULL;


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

void msm_hyp_set_kms(struct drm_device *dev, struct msm_hyp_kms *kms)
{
	if (!g_hyp_kms)
		g_hyp_kms = kms;
	else
		pr_warn("Set hyp kms twice?\n");
}

struct msm_hyp_kms *msm_hyp_get_kms(void)
{
	return g_hyp_kms;
}

int hyp_drm_bridge_init(struct drm_device *ddev, struct drm_encoder *encoder,
		struct msm_hyp_display *display)
{
	int rc = 0;
	struct drm_bridge *bridge;
	struct msm_drm_private *priv = NULL;

	bridge = &display->bridge;
	memset(bridge, 0, sizeof(*bridge));

	bridge->funcs = display->info->bridge_funcs;
	bridge->encoder = encoder;

	rc = drm_bridge_attach(encoder, bridge, NULL, 0);
	if (rc) {
		SDE_ERROR("failed to attach bridge, rc=%d\n", rc);
		goto error_free_bridge;
	}

	priv = ddev->dev_private;
	priv->bridges[priv->num_bridges++] = bridge;

	return rc;
error_free_bridge:
	kfree(bridge);
	return rc;
}

void hyp_drm_bridge_deinit(struct msm_hyp_display *display)
{
	return;
}

static int _msm_hyp_planes_init(struct drm_device *ddev)
{
	struct msm_drm_private *priv = ddev->dev_private;
	struct sde_kms *sde_kms = to_sde_kms(priv->kms);
	struct msm_hyp_kms *hyp_kms = sde_kms->hyp_kms;
	struct sde_mdss_cfg *catalog;
	struct msm_hyp_plane_info *plane_infos[MAX_PLANES];
	struct msm_hyp_connector_info *connector_infos[MAX_CONNECTORS];
	struct drm_plane *plane;
	int conn_num;
	int num = 0, i;
	u32 sspp_id[MAX_PLANES];
	u32 master_plane_id[MAX_PLANES];
	u32 num_virt_planes = 0;
	int ret;

	if (!hyp_kms->funcs || !hyp_kms->funcs->get_plane_infos)
		return -EINVAL;

	memset(sspp_id, 0, sizeof(sspp_id));

	catalog = sde_kms->catalog;

	ret = hyp_kms->funcs->get_plane_infos(sde_kms, NULL, &num);
	if (ret)
		return ret;

	pr_debug("hyp kms get_planes_infos num: %d\n", num);

	if (num >= MAX_PLANES)
		return -EINVAL;

	ret = hyp_kms->funcs->get_plane_infos(sde_kms, plane_infos, &num);
	if (ret)
		return ret;

	ret = hyp_kms->funcs->get_connector_infos(sde_kms, connector_infos, &conn_num);
	if (ret)
		return ret;

	/* Create the planes */
	for (i = 0; i < catalog->sspp_count; i++) {
		if (sde_hw_sspp_multirect_rec1_only(&catalog->sspp[i]))
			plane = sde_plane_init(ddev, catalog->sspp[i].id,
					plane_infos[i]->plane_type == DRM_PLANE_TYPE_PRIMARY,
					plane_infos[i]->possible_crtcs, (u32)-1);
		else
			plane = sde_plane_init(ddev, catalog->sspp[i].id,
					plane_infos[i]->plane_type == DRM_PLANE_TYPE_PRIMARY,
					plane_infos[i]->possible_crtcs, 0);
		if (IS_ERR(plane)) {
			SDE_ERROR("sde_plane_init failed\n");
			ret = PTR_ERR(plane);
			goto fail;
		}
		priv->planes[priv->num_planes++] = plane;
		plane->possible_crtcs = plane_infos[i]->possible_crtcs;

		if (sde_hw_sspp_multirect_enabled(&catalog->sspp[i]) &&
			sde_is_custom_client()) {
			int priority =
				catalog->sspp[i].sblk->smart_dma_priority;
			sspp_id[priority - 1] = catalog->sspp[i].id;
			master_plane_id[priority - 1] = plane->base.id;
			num_virt_planes++;
		}
	}

	for (i = 0; i < catalog->sspp_count; i++) {
		if (!sspp_id[i])
			continue;
		plane = sde_plane_init(ddev, sspp_id[i], false,
			plane_infos[i]->possible_crtcs, master_plane_id[i]);
		if (IS_ERR(plane)) {
			SDE_ERROR("sde_plane for virtual SSPP init failed\n");
			ret = PTR_ERR(plane);
			goto fail;
		}
		priv->planes[priv->num_planes++] = plane;
		plane->possible_crtcs = plane_infos[i]->possible_crtcs;
	}

	return 0;
fail:
	// TODO: release objects
	return ret;
}

static int _msm_hyp_crtcs_init(struct drm_device *ddev)
{
	struct msm_drm_private *priv = ddev->dev_private;
	struct sde_kms *sde_kms = to_sde_kms(priv->kms);
	struct msm_hyp_kms *hyp_kms = sde_kms->hyp_kms;
	struct msm_hyp_crtc_info *crtc_infos[MAX_CRTCS];
	uint32_t num = 0, i;
	struct drm_crtc *crtc;
	int ret;

	if (!hyp_kms->funcs || !hyp_kms->funcs->get_crtc_infos)
		return -EINVAL;

	ret = hyp_kms->funcs->get_crtc_infos(sde_kms, NULL, &num);
	if (ret) {
		pr_err("hyp kms get_crtc_infos failed: %d\n", ret);
		return ret;
	}

	pr_debug("crtc number: %d\n", num);

	if (num >= MAX_CRTCS)
		return -EINVAL;

	ret = hyp_kms->funcs->get_crtc_infos(sde_kms, crtc_infos, &num);
	if (ret) {
		pr_err("hyp kms get_crtc_infos failed, ret: %d\n", ret);
		return ret;
	}

	for (i = 0; i < num; i++) {
		crtc = sde_crtc_init(ddev,
				drm_plane_from_index(ddev, crtc_infos[i]->primary_plane_index));
		if (IS_ERR(crtc)) {
			ret = PTR_ERR(crtc);
			goto fail;
		}
		priv->crtcs[priv->num_crtcs++] = crtc;
	}

	return 0;
fail:
	return ret;
}

static int _msm_hyp_obj_init(struct drm_device *ddev)
{
	struct msm_drm_private *priv = ddev->dev_private;
	struct sde_kms *sde_kms = to_sde_kms(priv->kms);
	struct msm_hyp_kms *hyp_kms =sde_kms->hyp_kms;
	int ret;

	ret = _msm_hyp_planes_init(ddev);
	if (ret) {
		pr_err("_msm_hyp_planes_init failed: %d\n", ret);
		return ret;
	}

	ret = _msm_hyp_crtcs_init(ddev);
	if (ret) {
		pr_err("_msm_hyp_crtcs_init failed: %d\n", ret);
		return ret;
	}

	/* Register for WFD events(HPD)*/
	if (hyp_kms->funcs && hyp_kms->funcs->register_event)
		hyp_kms->funcs->register_event(sde_kms);

	return 0;
}

static int _msm_hyp_setup_displays(struct drm_device *ddev)
{
	struct msm_drm_private *priv;
	struct sde_kms *sde_kms;
	struct msm_hyp_kms *hyp_kms;
	struct msm_hyp_connector_info *conn_info, *connector_infos[MAX_CONNECTORS];
	int conn_num;
	void *displays[MAX_CONNECTORS];
	struct msm_hyp_display *display;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int ret;
	int i;

	priv = ddev->dev_private;
	sde_kms = to_sde_kms(priv->kms);
	hyp_kms = sde_kms->hyp_kms;
	if (!hyp_kms)
		return -EINVAL;

	if (!hyp_kms->funcs || !hyp_kms->funcs->get_connector_infos)
		return -EINVAL;

	ret = hyp_kms->funcs->get_displays(sde_kms, NULL, &sde_kms->hyp_display_count);
	if (ret)
		return ret;

	pr_debug("hyp display count: %d\n", sde_kms->hyp_display_count);
	if (sde_kms->hyp_display_count >= MAX_CONNECTORS)
		return -EINVAL;

	sde_kms->hyp_displays = kcalloc(sde_kms->hyp_display_count,
			sizeof(void *), GFP_KERNEL);
	if (!sde_kms->hyp_displays) {
		SDE_ERROR("failed to allocate hyp displays\n");
		return -EINVAL;
	}

	ret = hyp_kms->funcs->get_displays(sde_kms, displays, &sde_kms->hyp_display_count);

	if (sde_kms->hyp_display_count <= 0) {
		SDE_ERROR("invalid number of displays %d\n", sde_kms->hyp_display_count);
		return -EINVAL;
	}

	ret = hyp_kms->funcs->get_connector_infos(sde_kms, connector_infos, &conn_num);

	for (i = 0; i < sde_kms->hyp_display_count &&
			priv->num_encoders < MAX_ENCODERS; ++i) {
		display = devm_kzalloc(ddev->dev, sizeof(struct msm_hyp_display),
				GFP_KERNEL);
		sde_kms->hyp_displays[i] = display;
		encoder = NULL;
		conn_info = connector_infos[i];
		display->info = conn_info;

		encoder = sde_encoder_init(ddev, &conn_info->display_info, NULL);/* virtual encoder init */
		if (IS_ERR_OR_NULL(encoder)) {
			SDE_ERROR("hyp encoder init failed\n");
			continue;
		}
		// 1:1 map encoder to CRTC
		encoder->possible_crtcs = (1 << i);

		ret = hyp_drm_bridge_init(ddev, encoder, display);
		if (ret) {
			SDE_ERROR("bridge %d init failed %d\n", i, ret);
			sde_encoder_destroy(encoder);
			continue;
		}

		connector = sde_connector_init(ddev,
				encoder,
				NULL,
				display,
				conn_info->connector_funcs,
				DRM_CONNECTOR_POLL_HPD,
				conn_info->display_info.intf_type, false);
		if (connector) {
			display->display = displays[i];
			display->connector = connector;
			display->encoder = encoder;
			display->sde_kms = sde_kms;
			priv->encoders[priv->num_encoders++] = encoder;
			priv->connectors[priv->num_connectors++] = connector;
		} else {
			SDE_ERROR("%d connector init failed\n", i);
			hyp_drm_bridge_deinit(display);
			sde_encoder_destroy(encoder);
		}
	}

	return ret;
}

static int _msm_hyp_update_hw_reservation(struct drm_device *ddev)
{
	struct msm_drm_private *priv;
	struct sde_kms *sde_kms;

	priv = ddev->dev_private;
	sde_kms = to_sde_kms(priv->kms);
	if (!sde_kms->hyp_kms)
		return -EINVAL;

	return sde_kms->hyp_kms->funcs->update_hw_reservation(sde_kms);
}

void msm_hyp_crtc_commit_done(struct drm_crtc *crtc)
{
}

void msm_hyp_crtc_vblank_done(struct drm_crtc *crtc)
{
	if (WARN_ON(!crtc))
		return;

	drm_crtc_handle_vblank(crtc);
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

struct sde_mdss_cfg *msm_hyp_hw_catalog_init(struct drm_device *dev)
{
	struct msm_drm_private *priv;
	struct sde_kms *sde_kms;
	int dpu_id = -1;

	priv = dev->dev_private;
	if (!priv)
		return NULL;
	sde_kms = to_sde_kms(priv->kms);
	if (!sde_kms)
		return NULL;
	sde_kms->hyp_kms = g_hyp_kms;
	dpu_id = DPUID(sde_kms);
	if (dpu_id != -1) {
		if (dpu_id < 0 || dpu_id >= MAX_NUM_DPU_CORE) {
			DRM_ERROR("Invalid DPU cores id %d\n", dpu_id);
			goto error;
		} else if (g_hyp_kms->sde_kms[dpu_id]) {
			DRM_ERROR("DPU cores id %d already exist\n", dpu_id);
			goto error;
		} else {
			g_hyp_kms->sde_kms[dpu_id] = sde_kms;
			g_hyp_kms->num_sde_kms++;
		}
	} else if (g_hyp_kms->num_sde_kms < MAX_NUM_DPU_CORE) {
		g_hyp_kms->sde_kms[g_hyp_kms->num_sde_kms] = sde_kms;
		g_hyp_kms->num_sde_kms++;
	} else {
		DRM_ERROR("SDE_KMS exceed number of DPU cores %d\n", MAX_NUM_DPU_CORE);
		goto error;
	}

	if (g_hyp_kms->funcs->hw_catalog_init)
		return g_hyp_kms->funcs->hw_catalog_init(sde_kms);

error:
	return NULL;
}

int msm_hyp_drm_obj_init(struct drm_device *dev)
{
	struct msm_drm_private *priv;
	struct sde_kms *sde_kms;
	struct msm_hyp_kms *hyp_kms;
	int ret = 0;

	priv = dev->dev_private;
	sde_kms = to_sde_kms(priv->kms);
	hyp_kms = sde_kms->hyp_kms;

	ret = _msm_hyp_setup_displays(dev);
	if (ret) {
		DRM_ERROR("setup hyp displays - failed\n");
		goto fail;
	}

	ret = _msm_hyp_obj_init(dev);
	if (ret) {
		DRM_ERROR("hyp objects init - failed\n");
		goto fail;
	}

	ret = _msm_hyp_update_hw_reservation(dev);
	if (ret) {
		DRM_ERROR("update hyp hw reservation - failed\n");
		goto fail;
	}

fail:
	// TODO: msm_hyp_drm_obj_deinit
	return ret;
}

static int msm_hyp_bind(struct device *dev)
{
	int ret = 0;

	ret = component_bind_all(dev, NULL);

	return ret;
}

static void msm_hyp_unbind(struct device *dev)
{
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

	if (ret) {
		pr_err("%s goto fail\n", __func__);
		goto fail;
	}

	place_marker("kernel_fe: msm_hyp probe ready");
	pr_debug("msm_hyp probe done\n");

	return 0;

fail:
	of_platform_depopulate(&pdev->dev);
	return ret;
}

static int msm_hyp_pdev_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &msm_hyp_ops);
	of_platform_depopulate(&pdev->dev);

	return 0;
}

static const struct platform_device_id msm_id[] = {
	{ "mdp-hyp", 0 },
	{ }
};

static const struct of_device_id dt_match[] = {
	{ .compatible = "qcom,sde-kms-hyp" },
	{}
};

static struct platform_driver msm_hyp_platform_driver = {
	.probe      = msm_hyp_pdev_probe,
	.remove     = msm_hyp_pdev_remove,
	.driver     = {
		.name   = "msm_drm_hyp",
		.of_match_table = dt_match,
		.pm = &msm_hyp_pm_ops,
	},
	.id_table   = msm_id,
};

void __init msm_hyp_register(void)
{
	wfd_kms_register();
	virtio_kms_register();
	platform_driver_register(&msm_hyp_platform_driver);
}

void __exit msm_hyp_unregister(void)
{
	platform_driver_unregister(&msm_hyp_platform_driver);
	wfd_kms_unregister();
	virtio_kms_unregister();
}

MODULE_DESCRIPTION("MSM DRM HYP Driver");
MODULE_LICENSE("GPL v2");
