// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_graph.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <linux/wmmgr/wmmgr.h>

#include "wm_audio.h"
#include "wm_cec.h"
#include "wm_debug.h"
#include "wm_drm.h"
#include "wm_dsi_rx.h"
#include "wm_hdcp.h"
#include "wm_hdmi_tx.h"
#include "wm_pll.h"
#include "wm_vtg.h"

struct wm_display_private {
	char *name;
	struct platform_device *pdev;

	struct wm_display display;
	struct wm_dt_props dt_props;

	struct wm_audio *audio;
	struct wm_cec *cec;
	struct wm_debug *debug;
	struct wm_drm *drm;
	struct wm_dsi_rx *dsi_rx;
	struct wm_hdcp *hdcp;
	struct wm_hdmi_tx *hdmi_tx;
	struct wm_pll *pll;
	struct wm_vtg *vtg;

	struct clk *pixel_clk;

	struct platform_device *wmmgr_dev;
	struct wmmgr_client_context wmmgr_cl_ctxt;
	struct wmmgr_context *wmmgr_ctxt;

	unsigned int static_vregs_count;
	struct wm_display_vreg *static_vregs;
	unsigned int ondemand_vregs_count;
	struct wm_display_vreg *ondemand_vregs;
	bool ondemand_supplies_enabled;
};

int wm_display_read_register(struct wm_display *display, unsigned int reg)
{
	struct wm_display_private *display_priv = container_of(display,
							struct wm_display_private, display);
	unsigned int val;
	int ret;

	ret = regmap_read(display_priv->wmmgr_ctxt->regmap, reg, &val);
	if (ret < 0) {
		pr_err("%s: failed to read register %d\n", __func__, reg);
		return ret;
	}

	return val;
}

static int wm_display_write_register(struct wm_display *display,
				unsigned int reg, unsigned int val)
{
	struct wm_display_private *display_priv = container_of(display,
							struct wm_display_private, display);

	return regmap_write(display_priv->wmmgr_ctxt->regmap,
				reg, val);
}

static int wm_display_update_register_bits(struct wm_display *display,
				unsigned int reg, unsigned int mask, unsigned int val)
{
	struct wm_display_private *display_priv = container_of(display,
							struct wm_display_private, display);

	return regmap_update_bits(display_priv->wmmgr_ctxt->regmap,
				reg, mask, val);
}

static int wm_display_enable_vregs(struct wm_display_vreg *in_vregs,
			unsigned int num_vregs, bool enable)
{
	int i = 0, ret = 0;
	bool need_sleep;

	if (enable) {
		for (i = 0; i < num_vregs; i++) {
			ret = IS_ERR_OR_NULL(in_vregs[i].vreg);
			if (ret) {
				pr_err("%s regulator error. ret=%d\n",
						in_vregs[i].vreg_name, ret);
				goto vreg_set_opt_mode_fail;
			}

			need_sleep = !regulator_is_enabled(in_vregs[i].vreg);
			if (need_sleep && in_vregs[i].pre_on_sleep)
				usleep_range(in_vregs[i].pre_on_sleep * 1000,
						in_vregs[i].pre_on_sleep * 1000);

			ret = regulator_set_load(in_vregs[i].vreg,
					in_vregs[i].enable_load);
			if (ret < 0) {
				pr_err("%s set opt m fail\n",
						in_vregs[i].vreg_name);
				goto vreg_set_opt_mode_fail;
			}

			ret = regulator_enable(in_vregs[i].vreg);
			if (need_sleep && in_vregs[i].post_on_sleep)
				usleep_range(in_vregs[i].post_on_sleep * 1000,
					in_vregs[i].post_on_sleep * 1000);
			if (ret < 0) {
				pr_err("%s enable failed\n",
						in_vregs[i].vreg_name);
				goto disable_vreg;
			}
		}
	} else {
		for (i = num_vregs - 1; i >= 0; i--) {
			if (in_vregs[i].pre_off_sleep)
				usleep_range(in_vregs[i].pre_off_sleep * 1000,
					in_vregs[i].pre_off_sleep * 1000);

			regulator_set_load(in_vregs[i].vreg,
					in_vregs[i].disable_load);
			regulator_disable(in_vregs[i].vreg);

			if (in_vregs[i].post_off_sleep)
				usleep_range(in_vregs[i].post_off_sleep * 1000,
					in_vregs[i].post_off_sleep * 1000);
		}
	}
	return 0;

disable_vreg:
	regulator_set_load(in_vregs[i].vreg, in_vregs[i].disable_load);

vreg_set_opt_mode_fail:
	for (i--; i >= 0; i--) {
		if (in_vregs[i].pre_off_sleep)
			usleep_range(in_vregs[i].pre_off_sleep * 1000,
					in_vregs[i].pre_off_sleep * 1000);

		regulator_set_load(in_vregs[i].vreg,
				in_vregs[i].disable_load);
		regulator_disable(in_vregs[i].vreg);

		if (in_vregs[i].post_off_sleep)
			usleep_range(in_vregs[i].post_off_sleep * 1000,
					in_vregs[i].post_off_sleep * 1000);
	}

	return ret;
}

static int wm_display_hdmi_power_on(struct wm_display_private *display_priv)
{
	struct wm_display *display = &display_priv->display;
	unsigned int val;
	int ret;

	wm_display_update_register_bits(display, REG_WM_DISP_CTRL_STATUS,
								BIT(3), BIT(3));
	wm_display_update_register_bits(display, REG_WM_DISP_CTRL_STATUS,
								BIT(1), BIT(1));

	ret = regmap_read_poll_timeout(display_priv->wmmgr_ctxt->regmap,
								REG_WM_DISP_CTRL_STATUS,
								val, val & BIT(2),
								100, WM_DISP_POWER_SETTLE_DELAY);
	if (ret) {
		pr_err("%s: hdmi power on timeout, ret %d\n",
				__func__, ret);
		return ret;
	}

	wm_display_update_register_bits(display, REG_WM_DISP_CTRL_STATUS,
								BIT(4), BIT(4));
	wm_display_update_register_bits(display, REG_WM_DISP_CTRL_STATUS,
								BIT(0), BIT(0));

	return 0;
}

static int wm_display_enable_pixel_pll(struct wm_display_private *display_priv,
			bool enable)
{
	struct wm_display *display = &display_priv->display;
	int ret, clk_rate = 0;

	if (enable) {
		clk_rate = display->drm_mode.clock * 1000;

		ret = clk_prepare_enable(display_priv->pixel_clk);
		if (ret < 0) {
			pr_err("%s: clk enable failed\n", __func__);
			return ret;
		}

		ret = clk_set_rate(display_priv->pixel_clk, clk_rate);
		if (ret) {
			pr_err("%s: clk_set_rate failed\n", __func__);
			return ret;
		}
	} else {
		clk_disable_unprepare(display_priv->pixel_clk);
	}

	return 0;
}

static void wm_display_disable_hdmi_clk(struct wm_display *display)
{
	return 0;
}

static enum drm_mode_status wm_display_mode_valid(struct wm_display *display,
			const struct drm_display_mode *drm_mode)
{
	struct wm_display_private *display_priv = container_of(display,
							struct wm_display_private, display);

	return display_priv->hdmi_tx->mode_valid(display_priv->hdmi_tx, drm_mode);
}

static int wm_display_get_modes(struct wm_display *display,
			struct drm_connector *connector)
{
	struct wm_display_private *display_priv = container_of(display,
							struct wm_display_private, display);

	return display_priv->hdmi_tx->get_modes(display_priv->hdmi_tx, connector);

}

static void _convert_drm_to_wm_mode(struct wm_display *display)
{
	struct *wm_mode = display->mode_info;
	struct *drm_mode = display->drm_mode;


	wm_mode->h_active = drm_mode->hdisplay;
	wm_mode->h_blank = drm_mode->htotal - drm_mode->hdisplay;
	wm_mode->h_back_porch = drm_mode->htotal - drm_mode->hsync_end;
	wm_mode->h_front_porch = drm_mode->hsync_start - drm_mode->hdisplay;
	wm_mode->h_sync_width = drm_mode->htotal -
			(drm_mode->hsync_start + wm_mode.h_back_porch);
	wm_mode->v_active = drm_mode->vdisplay;
	wm_mode->v_blank = drm_mode->vtotal - drm_mode->vdisplay;
	wm_mode->v_back_porch = drm_mode->vtotal - drm_mode->vsync_end;
	wm_mode->v_front_porch = drm_mode->vsync_start - drm_mode->vtotal;
	wm_mode->v_sync_width = drm_mode->vtotal -
			(drm_mode->vsync_start + wm_mode.v_back_porch);
	wm_mode->h_active_low = !!(drm_mode->flags & DRM_MODE_FLAG_NHSYNC);
	wm_mode->v_active_low = !!(drm_mode->flags & DRM_MODE_FLAG_NVSYNC);
	wm_mode->h_skew = drm_mode->hskew;
	wm_mode->refresh_rate = drm_mode_vrefresh(drm_mode);
	wm_mode->pclk_khz = drm_mode->clock;
	wm_mode->interlace = !!(drm_mode->flags & DRM_MODE_FLAG_INTERLACE);
	wm_mode->dblclk = !!(drm_mode->flags & DRM_MODE_FLAG_DBCLK);
	wm_mode->bpp = DEFAULT_BPP;
	wm_mode->dsc_enabled = (wm_mode.hactive >= 3840 && wm_mode.vactive >= 2160
				&& wm_mode.refresh_rate > 30);
}

static void wm_display_set_mode(struct wm_display *display,
				const struct drm_display_mode *mode)
{
	drm_mode_copy(&display->drm_mode, mode);

	/*Convert drm mode to customised wingmate mode*/
	_convert_drm_to_wm_mode(display);

}

static void wm_display_pre_enable(struct wm_display *display)
{
	struct wm_display_private *display_priv = container_of(display,
							struct wm_display_private, display);
	int ret = 0;

	ret = wm_display_enable_vregs(display_priv->ondemand_vregs,
				display_priv->ondemand_vregs_count, true);
	if (ret) {
		pr_err("%s: failed to enable ondemand supplies\n");
		return;
	}

	display_priv->ondemand_supplies_enabled = true;

	ret = wm_display_enable_pixel_pll(display_priv, true);
	if (ret) {
		pr_err("%s: failed to enable pixel pll, ret %d\n", __func__, ret);
		return;
	}

	ret = display_priv->dsi_rx->pre_enable(display_priv->dsi_rx);
	if (ret) {
		pr_err("%s: dsi_rx pre_enable failed, ret %d\n", __func__, ret);
		return;
	}

	ret = display_priv->vtg->pre_enable(display_priv->vtg);
	if (ret) {
		pr_err("%s: vtg pre_enable failed, ret %d\n", __func__, ret);
		return;
	}

	ret = display_priv->hdmi_tx->pre_enable(display_priv->hdmi_tx);
	if (ret) {
		pr_err("%s: hdmi_tx pre_enable failed, ret %d\n", __func__, ret);
		return;
	}
}

static void wm_display_enable(struct wm_display *display)
{
	struct wm_display_private *display_priv = container_of(display,
							struct wm_display_private, display);
	int ret = 0;

	ret = display_priv->dsi_rx->enable(display_priv->dsi_rx);
	if (ret) {
		pr_err("%s: dsi_rx enable failed, ret %d\n", __func__, ret);
		return;
	}

	ret = display_priv->vtg->enable(display_priv->vtg);
	if (ret) {
		pr_err("%s: vtg enable failed, ret %d\n", __func__, ret);
		return;
	}

	ret = display_priv->hdmi_tx->enable(display_priv->hdmi_tx);
	if (ret) {
		pr_err("%s: hdmi_tx enable failed, ret %d\n", __func__, ret);
		return;
	}

	if (display_priv->audio) {
		ret = display_priv->audio->enable(display_priv->audio);
		if (ret)
			pr_err("%s: audio register failed, ret %d\n", __func__, ret);
	}
}

static void wm_display_disable(struct wm_display *display)
{
	struct wm_display_private *display_priv = container_of(display,
							struct wm_display_private, display);

	if (display_priv->audio)
		display_priv->audio->disable(display_priv->audio);

	display_priv->hdmi_tx->disable(display_priv->hdmi_tx);

	display_priv->vtg->disable(display_priv->vtg);

	display_priv->dsi_rx->disable(display_priv->dsi_rx);
}

static void wm_display_post_disable(struct wm_display *display)
{
	struct wm_display_private *display_priv = container_of(display,
							struct wm_display_private, display);
	int ret;

	display_priv->hdmi_tx->post_disable(display_priv->hdmi_tx);

	display_priv->dsi_rx->post_disable(display_priv->dsi_rx);

	display_priv->vtg->post_disable(display_priv->vtg);

	wm_display_enable_pixel_pll(display_priv, false);

	ret = wm_display_enable_vregs(display_priv->ondemand_vregs,
				display_priv->ondemand_vregs_count, false);
	if (ret)
		pr_err("%s: failed to disable ondemand supplies, ret %d\n",
				__func__, ret);

	display_priv->ondemand_supplies_enabled = false;
}

/**
 * wm_display_handle_params() - set or get a param from a sub module
 *
 * @display: pointer to display public structure
 * @id: identifier indicating the param
 * @in_param: pointer to the input value to be passed or NULL
 * @out_param: pointer to the memory where output value to be stored or NULL
 *
 */
void wm_display_handle_params(struct wm_display *display,
		wm_display_params_t id, void *in_param, void *out_param)
{
	switch (id) {
		default:
		break;
	}
}

static int wm_display_parse_dt_supplies(struct device *dev, char *supplies_str,
				struct wm_display_vreg **out_vregs, unsigned int *num_vregs)
{
	int i = 0, ret = 0;
	unsigned int tmp = 0, count = 0;
	struct device_node *supplies_np = NULL, *supply_np = NULL;
	struct device_node ;
	struct wm_display_vreg *vregs;

	if (!dev || !supplies_str || !out_vregs || !num_vregs) {
		pr_err("%s: invalid input param dev %p supplies_str %p,\
				out_vregs %p, num_vregs %p\n", __func__, dev, supplies_str,
				out_vregs, num_vregs);
		return -EINVAL;
	}

	supplies_np = of_get_child_by_name(dev->of_node, supplies_str);
	if (!supplies_np) {
		pr_info("%s: no supply entries present for %s\n",
				__func__, supplies_str);
		return 0;
	}

	count = of_get_available_child_count(supplies_np);
	if (count == 0) {
		pr_info("%s: no vreg present\n", __func__);
		return 0;
	}

	pr_debug("%s: found %d vregs\n", __func__, count);
	vregs = devm_kzalloc(dev, sizeof(struct wm_display_vreg) * count,
				GFP_KERNEL);
	if (!vregs)
		return -ENOMEM;

	for_each_available_child_of_node(supplies_np, supply_np) {
		const char *supply_name = NULL;

		ret = of_property_read_string(supply_np,
				"wm,supply-name", &supply_name);
		if (ret) {
			pr_err("%s: error reading name, ret %d\n", __func__, ret);
			goto error;
		}

		strscpy(vregs[i].vreg_name, supply_name,
				sizeof(vregs[i].vreg_name));

		ret = of_property_read_u32(supply_np,
				"wm,supply-min-voltage", &tmp);
		if (ret) {
			pr_err("%s: error reading min volt, ret %d\n", __func__, ret);
			goto error;
		}
		vregs[i].min_voltage = tmp;

		ret = of_property_read_u32(supply_np,
				"wm,supply-max-voltage", &tmp);
		if (ret) {
			pr_err("%s: error reading max volt, ret %d\n", __func__, ret);
			goto error;
		}
		vregs[i].max_voltage = tmp;

		ret = of_property_read_u32(supply_np,
				"wm,supply-enable-load", &tmp);
		if (ret)
			pr_debug("%s: no supply enable load value, ret %d\n",
						__func__, ret);

		vregs[i].enable_load = (!ret ? tmp : 0);

		ret = of_property_read_u32(supply_np,
				"wm,supply-disable-load", &tmp);
		if (ret)
			pr_debug("%s: no supply disable load value. ret %d\n",
						__func__, ret);

		vregs[i].disable_load = (!ret ? tmp : 0);

		ret = of_property_read_u32(supply_np,
				"wm,supply-pre-on-sleep", &tmp);
		if (ret)
			pr_debug("%s: no supply pre on sleep value, ret %d\n",
						__func__, ret);

		vregs[i].pre_on_sleep = (!ret ? tmp : 0);

		ret = of_property_read_u32(supply_np,
				"wm,supply-pre-off-sleep", &tmp);
		if (ret)
			pr_debug("%s: no supply pre off sleep value, ret %d\n",
						__func__, ret);

		vregs[i].pre_off_sleep = (!ret ? tmp : 0);

		ret = of_property_read_u32(supply_np,
				"wm,supply-post-on-sleep", &tmp);
		if (ret)
			pr_debug("%s: no supply post on sleep value, ret %d\n",
						__func__, ret);

		vregs[i].post_on_sleep = (!ret ? tmp : 0);

		ret = of_property_read_u32(supply_np,
				"wm,supply-post-off-sleep", &tmp);
		if (ret)
			pr_debug("%s: no supply post off sleep value, ret %d\n",
						__func__, ret);

		vregs[i].post_off_sleep = (!ret ? tmp : 0);

		pr_debug("%s: %s min=%d, max=%d, enable=%d, disable=%d\n",
				__func__,
				vregs[i].vreg_name,
				vregs[i].min_voltage,
				vregs[i].max_voltage,
				vregs[i].enable_load,
				vregs[i].disable_load);
		i++;
	}

	*out_vregs = vregs;
	*num_vregs = count;

	return 0;

error:
	devm_kfree(dev, vregs);

	return ret;
}

static int wm_display_config_vregs(struct device *dev,
	struct wm_display_vreg *in_vregs, int num_vregs, bool config)
{
	int i = 0, ret = 0;
	struct wm_display_vreg *curr_vreg = NULL;

	if (!in_vregs || !num_vregs)
		return ret;

	if (config) {
		for (i = 0; i < num_vregs; i++) {
			curr_vreg = &in_vregs[i];
			curr_vreg->vreg = devm_regulator_get(dev,
					curr_vreg->vreg_name);
			ret = IS_ERR_OR_NULL(curr_vreg->vreg);
			if (ret) {
				pr_err("%s: %s get failed, ret %d\n",
						__func__, curr_vreg->vreg_name, ret);
				curr_vreg->vreg = NULL;
				goto vreg_get_fail;
			}

			ret = regulator_set_voltage(
					curr_vreg->vreg,
					curr_vreg->min_voltage,
					curr_vreg->max_voltage);
			if (ret < 0) {
				pr_err("%s: %s set voltage fail, ret %d\n",
						__func__, curr_vreg->vreg_name, ret);
				goto vreg_set_voltage_fail;
			}
		}
	} else {
		for (i = num_vregs - 1; i >= 0; i--) {
			curr_vreg = &in_vregs[i];
			if (curr_vreg->vreg) {
				regulator_set_voltage(curr_vreg->vreg,
						0, curr_vreg->max_voltage);

				devm_regulator_put(curr_vreg->vreg);
				curr_vreg->vreg = NULL;
			}
		}
	}

	return 0;

vreg_unconfig:
	regulator_set_load(curr_vreg->vreg, 0);

vreg_set_voltage_fail:
	regulator_put(curr_vreg->vreg);
	curr_vreg->vreg = NULL;

vreg_get_fail:
	for (i--; i >= 0; i--) {
		curr_vreg = &in_vregs[i];
		goto vreg_unconfig;
	}

	return ret;
}

static int wm_display_get_dt_supplies(struct wm_display_private *display_priv)
{
	int ret;
	struct device *dev = &display_priv->pdev->dev;

	display_priv->static_vregs = NULL;
	display_priv->static_vregs_count = 0;

	display_priv->ondemand_vregs = NULL;
	display_priv->ondemand_vregs_count = 0;

	/* Parse static and ondemand supplies from dt */
	ret = wm_display_parse_dt_supplies(dev, "wm,static-supplies",
				&display_priv->static_vregs,
				&display_priv->static_vregs_count);
	if (ret) {
		pr_err("%s: Failed to parse static supplies\n", __func__);
		return ret;
	}

	ret = wm_display_parse_dt_supplies(dev, "wm,ondemand-supplies",
				&display_priv->ondemand_vregs,
				&display_priv->ondemand_vregs_count);
	if (ret) {
		pr_err("%s: Failed to parse ondemand supplies\n", __func__);
		goto free_static_vregs;
	}

	/* config and get static and ondemand supplies */
	ret = wm_display_config_vregs(dev, display_priv->static_vregs,
				display_priv->static_vregs_count, true);
	if (ret) {
		pr_err("%s: falied to configure static vregs, ret %d\n",
				__func__, ret);
		goto free_ondemand_vregs;
	}

	ret = wm_display_config_vregs(dev, display_priv->ondemand_vregs,
				display_priv->ondemand_vregs_count, true);
	if (ret) {
		pr_err("%s: falied to configure static vregs, ret %d\n",
				__func__, ret);
		goto unconfig_static_supplies;
	}

	return 0;

unconfig_static_supplies:
	wm_display_config_vregs(dev, display_priv->static_vregs,
				display_priv->static_vregs_count, false);
free_ondemand_vregs:
	devm_kfree(dev, display_priv->ondemand_vregs);
	display_priv->ondemand_vregs = NULL;
free_static_vregs:
	devm_kfree(dev, display_priv->static_vregs);
	display_priv->static_vregs = NULL;

	return ret;
}

static void wm_display_put_dt_supplies(struct wm_display_private *display_priv)
{
	struct device *dev = &display_priv->pdev->dev;
	int ret;

	if (display_priv->static_vregs) {
		ret = wm_display_config_vregs(dev, display_priv->static_vregs,
					display_priv->static_vregs_count, false);
		if (ret)
			pr_err("%s: falied to unconfig static vregs, ret %d\n",
				__func__, ret);

		devm_kfree(dev, display_priv->static_vregs);
		display_priv->static_vregs = NULL;
	}

	if (display_priv->ondemand_vregs) {
		ret = wm_display_config_vregs(dev, display_priv->ondemand_vregs,
					display_priv->ondemand_vregs_count, false);
		if (ret)
			pr_err("%s: falied to unconfig static vregs, ret %d\n",
				__func__, ret);

		devm_kfree(dev, display_priv->ondemand_vregs);
		display_priv->ondemand_vregs = NULL;
	}
}

static int wm_display_parse_dt(struct wm_display_private *display_priv)
{
	struct wm_dt_props *dt_props = &display_priv->dt_props;
	struct device *dev = &display_priv->pdev->dev;
	struct device_node *wmmgr_np, *end_np;

	int ret;

	wmmgr_np = of_parse_phandle(dev->of_node, "wm,wmmgr", 0);
	if (!wmmgr_np) {
		pr_err("%s: Failed to parse wmmgr handle\n", __func__);
		return -ENODEV;
	}

	display_priv->wmmgr_dev = of_find_device_by_node(wmmgr_np);
	if (!display_priv->wmmgr_dev) {
		pr_err("%s: Failed to get wmmgr device\n", __func__);
		return -EPROBE_DEFER;
	}

	end_np = of_graph_get_endpoint_by_regs(dev->of_node, 0, 0);
	if (!end_np) {
		pr_err("%s: remote endpoint not found\n", __func__);
		return -ENODEV;
	}

	dt_props->host_np = of_graph_get_remote_port_parent(end_np);
	of_node_put(end_np);
	if (!dt_props->host_np) {
		pr_err("%s: remote node not found\n", __func__);
		return -ENODEV;
	}
	of_node_put(dt_props->host_np);

	dt_props->audio_supported = of_property_read_bool(dev->of_node,
									"wm,audio-support");
	dt_props->cec_supported = of_property_read_bool(dev->of_node,
								"wm,cec-support");

	ret = wm_display_get_dt_supplies(display_priv);
	if (ret) {
		pr_err("%s: Failed to get dt supplies\n", __func__);
		return ret;
	}

	display_priv->pixel_clk = devm_clk_get(dev, "wm_pixel_pll");
	if (IS_ERR(display_priv->pixel_clk)) {
		ret = PTR_ERR(display_priv->pixel_clk);
		pr_err("%s: clk get failed for wm_pixel_pll, ret %d\n",
					__func__, ret);
		display_priv->pixel_clk = NULL;
		goto put_supplies;
	}

	/** TODO: Add IP level properties */

	return 0;

put_supplies:
	wm_display_put_dt_supplies(display_priv);

	return ret;
}

static void wm_display_wmmgr_cb(void *data, struct wm_notify* notify)
{
	struct wm_display_private *display_priv = container_of(data,
							struct wm_display_private, wmmgr_cl_ctxt);
	int ret;

	pr_info("%s: reason %d\n", __func__, notify->reason);

	if (notify->reason == WM_RST_DONE) {
		ret = wm_display_enable_vregs(display_priv->static_vregs,
					display_priv->static_vregs_count, true);
		if (ret) {
			pr_err("%s: failed to enable static supplies\n", __func__);
			return;
		}

		ret = wm_display_hdmi_power_on(display_priv);
		if (ret) {
			pr_err("%s: failed to turn on hdmi power domain\n", __func__);
			wm_display_enable_vregs(display_priv->static_vregs,
					display_priv->static_vregs_count, false);
			return;
		}
	} else {
		if (display_priv->audio)
			display_priv->audio->disable(display_priv->audio);

		if (display_priv->ondemand_supplies_enabled) {
			pr_debug("%s: on demand supplies are enabled, disable them\n",
				__func__);
			wm_display_enable_vregs(display_priv->ondemand_vregs,
					display_priv->ondemand_vregs_count, false);
		}

		wm_display_enable_vregs(display_priv->static_vregs,
				display_priv->static_vregs_count, false);
	}
}

static irqreturn_t wm_display_irq_handler(int irq, void *data)
{
	struct wm_display_private *display_priv =
				(struct wm_display_private *) data;
	struct irq_data *d =
		irq_domain_get_irq_data(display_priv->wmmgr_ctxt->irq_domain, irq);
	int hwirq = d->hwirq;

	switch (hwirq) {
		case WM_DISPLAY_INT_HDMI_INT:
			display_priv->hdmi_tx->irq_handler(display_priv->hdmi_tx, hwirq);
			break;
		case WM_DISPLAY_INT_MIPIDSI2_APB_INT:
		case WM_DISPLAY_INT_DSC_ERR:
			display_priv->dsi_rx->irq_handler(display_priv->dsi_rx, hwirq);
			break;
		case WM_DISPLAY_INT_WAS_OFL_INT:
		case WM_DISPLAY_INT_WAS_UFL_INT:
		case WM_DISPLAY_INT_I2S_INTERRUPT:
			if (display_priv->audio)
				display_priv->audio->irq_handler(display_priv->audio, hwirq);
			break;
		case WM_DISPLAY_INT_VTG_OBS_FIFO_LATENCY_GT_BOUND:
		case WM_DISPLAY_INT_VTG_OBS_FIFO_LATENCY_LT_BOUND:
		case WM_DISPLAY_INT_VTG_FRAME_DELTA_LPF8_GT_BOUND:
		case WM_DISPLAY_INT_VTG_FRAME_DELTA_LPF8_LT_BOUND:
		case WM_DISPLAY_INT_VTG_FRAME_DELTA_LPF4_GT_BOUND:
		case WM_DISPLAY_INT_VTG_FRAME_DELTA_LPF4_LT_BOUND:
		case WM_DISPLAY_INT_VTG_FRAME_DELTA_LPF2_GT_BOUND:
		case WM_DISPLAY_INT_VTG_FRAME_DELTA_LPF2_LT_BOUND:
		case WM_DISPLAY_INT_VTG_FRAME_DELTA_GT_BOUND:
		case WM_DISPLAY_INT_VTG_FRAME_DELTA_LT_BOUND:
		case WM_DISPLAY_INT_VTG_FRAME_BUFFER_ALMOST_FULL:
		case WM_DISPLAY_INT_VTG_FRAME_BUFFER_ALMOST_EMPTY:
		case WM_DISPLAY_INT_VTG_FRAME_BUFFER_FULL:
		case WM_DISPLAY_INT_VTG_FRAME_BUFFER_EMPTY:
		case WM_DISPLAY_INT_VTG_GENERATOR_ERROR_INT:
		case WM_DISPLAY_INT_VTG_HDMI_VSYNC_INT:
		case WM_DISPLAY_INT_VTG_MIPI_VSYNC_INT:
			display_priv->vtg->irq_handler(display_priv->vtg, hwirq);
			break;
		default:
			pr_warn("%s: unknown irq %d\n", __func__, hwirq);
			break;
	}

	return IRQ_HANDLED;
}

static int wm_display_setup_irq(struct wm_display_private *display_priv)
{
	unsigned int hwirq, virq;
	int ret;

	if (display_priv->wmmgr_ctxt->irq_domain) {
		pr_err("%s: irq domain is NULL\n", __func__);
		return -EINVAL;
	}

	for (hwirq = WM_DISPLAY_INT_MIN; hwirq < WM_DISPLAY_INT_MAX; hwirq++) {
		virq = irq_create_mapping(display_priv->wmmgr_ctxt->irq_domain, hwirq);

		ret = devm_request_threaded_irq(&display_priv->pdev->dev, virq, NULL,
				wm_display_irq_handler, 0,
				"wm-display", display_priv);
		if (ret)
			pr_warn("%s: failed to request irq for %d\n", __func__, hwirq);
		else
			disable_irq_nosync(virq);
	}

	return 0;
}

static void wm_display_enable_irq(struct wm_display *display,
				wm_display_interrupts_t hwirq, bool enable)
{
	struct wm_display_private *display_priv = container_of(display,
							struct wm_display_private, display);
	unsigned int virq;

	if (display_priv->wmmgr_ctxt->irq_domain) {
		pr_err("%s: irq domain is NULL\n", __func__);
		return;
	}

	virq = irq_find_mapping(display_priv->wmmgr_ctxt->irq_domain, hwirq);

	enable ? enable_irq(virq) : disable_irq_nosync(virq);
}

static void wm_display_deinit_modules(struct wm_display_private *display_priv)
{
	if (display_priv->audio)
		wm_audio_deinit(display_priv->audio);
	if (display_priv->cec)
		wm_cec_deinit(display_priv->cec);
	if (display_priv->debug)
		wm_debug_deinit(display_priv->debug);
	if (display_priv->drm)
		wm_drm_deinit(display_priv->drm);
	if (display_priv->dsi_rx)
		wm_dsi_rx_deinit(display_priv->dsi_rx);
	if (display_priv->hdcp)
		wm_hdcp_deinit(display_priv->hdcp);
	if (display_priv->hdmi_tx)
		wm_hdmi_tx_deinit(display_priv->hdmi_tx);
	if (display_priv->pll)
		wm_pll_deinit(display_priv->pll);
	if (display_priv->vtg)
		wm_vtg_deinit(display_priv->vtg);
}

static int wm_display_init_modules(struct wm_display_private *display_priv)
{
	struct wm_display_info display_info;
	struct wm_display *display = &display_priv->display;
	struct wm_dt_props *dt_props = &display_priv->dt_props;

	display->mode_valid = wm_display_mode_valid;
	display->get_modes = wm_display_get_modes;
	display->set_mode = wm_display_set_mode;
	display->pre_enable = wm_display_pre_enable;
	display->enable = wm_display_enable;
	display->disable = wm_display_disable;
	display->post_disable = wm_display_post_disable;
	display->handle_params = wm_display_handle_params;
	display->enable_irq = wm_display_enable_irq;
	display->read_register = wm_display_read_register;
	display->write_register = wm_display_write_register;
	display->update_register_bits = wm_display_update_register_bits;
	display->disable_hdmi_clk = wm_display_disable_hdmi_clk;

	/* Fill the info structure to be passed to sub module during init */
	display_info.display = &display_priv->display;
	display_info.dev = &display_priv->pdev->dev;
	display_info.dt_props = &display_priv->dt_props;

	display_priv->drm = wm_drm_init(&display_info);
	if (display_priv->drm) {
		pr_err("failed to init wm_drm\n");
		goto error;
	}

	display_priv->hdmi_tx = wm_hdmi_tx_init(&display_info);
	if (display_priv->hdmi_tx) {
		pr_err("failed to init wm_hdmi_tx\n");
		goto error;
	}

	display_priv->vtg = wm_vtg_init(&display_info);
	if (display_priv->vtg) {
		pr_err("failed to init wm_vtg\n");
		goto error;
	}

	display_priv->dsi_rx = wm_dsi_rx_init(&display_info);
	if (display_priv->dsi_rx) {
		pr_err("failed to init wm_dsi_rx\n");
		goto error;
	}

	display_priv->hdcp = wm_hdcp_init(&display_info);
	if (display_priv->hdcp) {
		pr_err("failed to init wm_hdcp\n");
		goto error;
	}

	display_priv->pll = wm_pll_init(&display_info);
	if (display_priv->pll) {
		pr_err("failed to init wm_pll\n");
		goto error;
	}

	if (dt_props->audio_supported) {
		display_priv->audio = wm_audio_init(&display_info);
		if (!display_priv->audio) {
			pr_err("failed to init wm_audio\n");
			goto error;
		}
	}

	if (dt_props->cec_supported) {
		display_priv->cec = wm_cec_init(&display_info);
		if (!display_priv->cec) {
			pr_err("failed to init wm_cec\n");
			goto error;
		}
	}

	display_priv->debug = wm_debug_init(&display_info);
	if (!display_priv->debug)
		pr_err("failed to init wm_debug\n");

	return 0;

error:
	wm_display_deinit_modules(display_priv);
	return -EINVAL;
}

static int wm_display_probe(struct platform_device *pdev)
{
	struct wm_display_private *display_priv;
	int ret = 0;

	if (!pdev || pdev->dev.of_node) {
		pr_err("%s: pdev not found\n", __func__);
		return -ENODEV;
	}

	display_priv = devm_kzalloc(&pdev->dev,
					sizeof(struct wm_display_private), GFP_KERNEL);
	if (!display_priv) {
		pr_err("%s: failed to allocate display private\n", __func__);
		return -ENOMEM;
	}

	ret = wm_display_parse_dt(display_priv);
	if (ret) {
		pr_err("%s: failed to parse device tree\n", __func__);
		return -EINVAL;
	}

	/* WM Manager Registration */
	strscpy(display_priv->wmmgr_cl_ctxt.name,
			"wm-display", sizeof("wm-display"));
	display_priv->wmmgr_cl_ctxt.cbfn = wm_display_wmmgr_cb;

	ret = wmmgr_register_client(&display_priv->wmmgr_dev->dev,
				&display_priv->wmmgr_cl_ctxt);
	if (ret) {
		pr_err("%s: wm manager registration failed, ret %d\n", __func__, ret);
		goto put_supplies_clk;
	}

	display_priv->wmmgr_ctxt = display_priv->wmmgr_cl_ctxt.wmmgrctxt;

	display_priv->ondemand_supplies_enabled = false;

	ret = wm_display_init_modules(display_priv);
	if (ret) {
		pr_err("%s: failed to init modules\n", __func__);
		goto put_supplies_clk;
	}

	ret = wm_display_setup_irq(display_priv);
	if (ret) {
		pr_err("%s: failed to setup irq\n", __func__);
		goto deinit_modules;
	}

	ret = wm_display_enable_vregs(display_priv->static_vregs,
			display_priv->static_vregs_count, true);
	if (ret) {
		pr_err("%s: failed to enable static supplies\n", __func__);
		goto deinit_modules;
	}

	ret = wm_display_hdmi_power_on(display_priv);
	if (ret) {
		pr_err("%s: failed to turn on hdmi power domain\n", __func__);
		goto disable_static_supplies;
	}

	ret = display_priv->hdcp->download_firmware(display_priv->hdcp);
	if (ret) {
		pr_err("%s: failed to download hdcp firmware\n", __func__);
		goto disable_static_supplies;
	}

	dev_set_drvdata(&pdev->dev, &display_priv->display);

	return 0;

disable_static_supplies:
	wm_display_enable_vregs(display_priv->static_vregs,
			display_priv->static_vregs_count, false);
deinit_modules:
	wm_display_deinit_modules(display_priv);
put_supplies_clk:
	wm_display_put_dt_supplies(display_priv);
	devm_clk_put(&pdev->dev, display_priv->pixel_clk);

	return ret;
}

static int wm_display_remove(struct platform_device *pdev)
{
	struct wm_display *display =
		(struct wm_display *) dev_get_drvdata(&pdev->dev);
	struct wm_display_private *display_priv = container_of(display,
							struct wm_display_private, display);

	if (display_priv->ondemand_supplies_enabled)
			wm_display_enable_vregs(display_priv->ondemand_vregs,
					display_priv->ondemand_vregs_count, false);

	wm_display_enable_vregs(display_priv->static_vregs,
			display_priv->static_vregs_count, false);

	wm_display_put_dt_supplies(display_priv);

	devm_clk_put(&pdev->dev, display_priv->pixel_clk);

	wm_display_deinit_modules(display_priv);

	return 0;
}

static const struct of_device_id wm_display_dt_match[] = {
	{.compatible = "qcom,wm-display"},
	{}
};

static struct platform_driver wm_display_driver = {
	.driver = {
		.name = "wm-display",
		.of_match_table = wm_display_dt_match,
		.suppress_bind_attrs = true,
	},
	.probe = wm_display_probe,
	.remove = wm_display_remove,
};

static int __init wm_display_init(void)
{
	return platform_driver_register(&wm_display_driver);
}
module_init(wm_display_init);

static void __exit wm_display_exit(void)
{
	platform_driver_unregister(&wm_display_driver);
}
module_exit(wm_display_exit);

MODULE_DESCRIPTION("Wingmate Display Core Driver");
MODULE_LICENSE("GPL v2");
