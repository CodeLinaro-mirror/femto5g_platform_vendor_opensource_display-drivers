// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_file.h>

#include "wm_drm.h"

static int wm_drm_connector_get_modes(struct drm_connector *connector)
{
	struct wm_drm *drm = container_of(connector, struct wm_drm, connector);
	struct wm_display *display = drm->display;

	return display->get_modes(display, connector);
}

static enum drm_mode_status wm_drm_connector_mode_valid(
	struct drm_connector *connector, struct drm_display_mode *drm_mode)
{
	struct wm_drm *drm = container_of(connector, struct wm_drm, connector);
	struct wm_display *display = drm->display;

	return display->mode_valid(display, drm_mode);
}

static enum drm_connector_status
wm_drm_connector_detect(struct drm_connector *connector, bool force)
{
	struct wm_drm *drm = container_of(connector, struct wm_drm, connector);
	struct wm_display *display = drm->display;
	enum drm_connector_status status;

	status = display->hpd_status ? connector_status_connected
				: connector_status_disconnected;

	return status;
}

static const struct drm_connector_helper_funcs
		wm_drm_connector_helper_funcs = {
	.get_modes = wm_drm_connector_get_modes,
	.mode_valid = wm_drm_connector_mode_valid,
};

static const struct drm_connector_funcs wm_drm_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = wm_drm_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int wm_drm_bridge_attach(struct drm_bridge *bridge,
		enum drm_bridge_attach_flags flags)
{
	struct wm_drm *drm = container_of(bridge, struct wm_drm, bridge);
	struct wm_dt_props *dt_props = drm->dt_props;
	struct mipi_dsi_host *host;
	struct mipi_dsi_device *dsi;
	const struct mipi_dsi_device_info info = { .type = "wm",
						   .channel = 0,
						   .node = NULL,
						 };
	int ret;

	pr_debug("%s: bridge %p\n", __func__, bridge);

	if (!bridge->encoder) {
		pr_err("Parent encoder object not found");
		return -ENODEV;
	}

	ret = drm_connector_init(bridge->dev, &drm->connector,
				 &wm_drm_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		pr_err("%s: Failed to initialize connector: %d\n", __func__, ret);
		return ret;
	}

	drm_connector_helper_add(&drm->connector,
				 &wm_drm_connector_helper_funcs);

	ret = drm_connector_register(&drm->connector);
	if (ret) {
		pr_err("%s: Failed to register connector: %d\n", __func__, ret);
		return ret;
	}

	drm->connector.polled = DRM_CONNECTOR_POLL_CONNECT;

	ret = drm_connector_attach_encoder(&drm->connector,
						bridge->encoder);
	if (ret) {
		pr_err("%s: Failed to link up connector to encoder: %d\n", __func__, ret);
		return ret;
	}

	host = of_find_mipi_dsi_host_by_node(dt_props->host_np);
	if (!host) {
		pr_err("%s: Failed to find dsi host\n", __func__);
		return -EPROBE_DEFER;
	}

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		pr_err("%s: Failed to create dsi device\n", __func__);
		ret = PTR_ERR(dsi);
		return ret;
	}

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_VIDEO_HSE;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		pr_err("%s: Failed to attach dsi to host\n", __func__);
		goto err_dsi_attach;
	}

	drm->dsi = dsi;

	return 0;

err_dsi_attach:
	mipi_dsi_device_unregister(dsi);

	return ret;
}

static bool wm_drm_bridge_mode_fixup(struct drm_bridge *bridge,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	pr_debug("%s: bridge %p\n", __func__, bridge);
	return true;
}

static void wm_drm_bridge_mode_set(struct drm_bridge *bridge,
				    const struct drm_display_mode *mode,
				    const struct drm_display_mode *adj_mode)
{
	struct wm_drm *drm = container_of(bridge, struct wm_drm, bridge);
	struct wm_display *display = drm->display;

	pr_debug(" hdisplay=%d, vdisplay=%d, clock=%d\n",
		adj_mode->hdisplay, adj_mode->vdisplay,
		adj_mode->clock);

	display->set_mode(display, adj_mode);
}

static void wm_drm_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct wm_drm *drm = container_of(bridge, struct wm_drm, bridge);
	struct wm_display *display = drm->display;

	pr_debug("%s: bridge %p\n", __func__, bridge);

	display->pre_enable(display);
}

static void wm_drm_bridge_enable(struct drm_bridge *bridge)
{
	struct wm_drm *drm = container_of(bridge, struct wm_drm, bridge);
	struct wm_display *display = drm->display;

	pr_debug("%s: bridge %p\n", __func__, bridge);

	display->enable(display);
}

static void wm_drm_bridge_disable(struct drm_bridge *bridge)
{
	struct wm_drm *drm = container_of(bridge, struct wm_drm, bridge);
	struct wm_display *display = drm->display;

	pr_debug("%s: bridge %p\n", __func__, bridge);

	display->disable(display);
}

static void wm_drm_bridge_post_disable(struct drm_bridge *bridge)
{
	struct wm_drm *drm = container_of(bridge, struct wm_drm, bridge);
	struct wm_display *display = drm->display;

	pr_debug("%s: bridge %p\n", __func__, bridge);

	display->post_disable(display);
}

static const struct drm_bridge_funcs wm_drm_bridge_funcs = {
	.attach = wm_drm_bridge_attach,
	.mode_fixup   = wm_drm_bridge_mode_fixup,
	.pre_enable   = wm_drm_bridge_pre_enable,
	.enable = wm_drm_bridge_enable,
	.disable = wm_drm_bridge_disable,
	.post_disable = wm_drm_bridge_post_disable,
	.mode_set = wm_drm_bridge_mode_set,
};

struct wm_drm *wm_drm_init(struct wm_display_info *display_info)
{
	struct wm_drm *drm;

	drm = devm_kzalloc(display_info->dev, sizeof(struct wm_drm), GFP_KERNEL);
	if (!drm) {
		pr_err("%s: failed to allocate wm_drm\n", __func__);
		return NULL;
	}

	drm->display = display_info->display;
	drm->dev = display_info->dev;
	drm->dt_props = display_info->dt_props;

	drm->bridge.funcs = &wm_drm_bridge_funcs;
	drm_bridge_add(&drm->bridge);

	return drm;
}

void wm_drm_deinit(struct wm_drm *drm)
{
	mipi_dsi_detach(drm->dsi);
	mipi_dsi_device_unregister(drm->dsi);

	drm_bridge_remove(&drm->bridge);
}
