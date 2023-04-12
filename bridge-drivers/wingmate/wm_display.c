// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>

/** TODO: Include WM Manager header file
 * #include <linux/wmmgr/wmmgr.h>
 */

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

	struct drm_display_mode cur_drm_mode;

	struct wmmgr_client_context *wmmgr_ctxt;
};

static void wm_display_set_mode(struct wm_display *dispaly, struct drm_display_mode *mode)
{
	/** TODO: Store current mode to private structure.
	 * Check if custom mode structure is required to accommodate additional params if any.
	 */
}

static void wm_display_pre_enable(struct wm_display *display)
{
	struct wm_display_private *display_priv = container_of(display,
							struct wm_display_private, display);

	/** TODO: Handle failure cases */

	display_priv->pll->configure_pixel_pll(display_priv->pll);

	display_priv->dsi_rx->configure_mipi_rx(display_priv->dsi_rx);

	display_priv->dsi_rx->configure_video_path(display_priv->dsi_rx);
}

static void wm_display_enable(struct wm_display *display)
{
	struct wm_display_private *display_priv = container_of(display,
							struct wm_display_private, display);

	/** TODO: Handle failure cases */

	display_priv->hdcp->configure(display_priv->hdcp);

	display_priv->hdmi_tx->configure_video_mode(display_priv->hdmi_tx,
							&display_priv->cur_drm_mode);

	display_priv->hdmi_tx->configure_info_frames(display_priv->hdmi_tx);
}

static void wm_display_disable(struct wm_display *display)
{
	/** TODO: Define WM disable sequence */
}

static void wm_display_post_disable(struct wm_display *display)
{

}

static int wm_display_parse_dt(struct wm_display_private *display_priv)
{
	struct wm_dt_props *dt_props = &display_priv->dt_props;
	struct device *dev = &display_priv->pdev->dev;

	char *ext_disp_str = "qcom,ext-disp";

	/** TODO: change wingmate property names from "qcom,*" to "wm<id>,*" */
	char *audio_support_str = "qcom,audio-support";
	char *cec_support_str = "qcom,cec-support";

	dt_props->ext_disp_np = of_parse_phandle(dev->of_node, ext_disp_str, 0);
	if (!dt_props->ext_disp_np) {
		pr_err("Cannot parse %s handle\n", ext_disp_str);
		return -ENODEV;
	}

	dt_props->audio_supported = of_property_read_bool(dev->of_node, audio_support_str);
	dt_props->cec_supported = of_property_read_bool(dev->of_node, cec_support_str);

	/** TODO: Add IP level properties */

	return 0;
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

	/** wm_display function pointers.
	 * sub modules can use them to make calls to wm_display.
	 * TODO: add remaining functions required.
	 */

	display->set_mode = wm_display_set_mode;
	display->pre_enable = wm_display_pre_enable;
	display->enable = wm_display_enable;
	display->disable = wm_display_disable;
	display->post_disable = wm_display_post_disable;

	/* Fill info structure to be passed to sub module during init */
	display_info.display = &display_priv->display;
	display_info.dev = &display_priv->pdev->dev;
	display_info.dt_props = &display_priv->dt_props;
	display_info.wmmgr_ctxt = display_priv->wmmgr_ctxt;

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

/** TODO: Handler for display ss IRQs
 * static irqreturn_t wm_display_irq_handler(int irq, void *data)
 * {
 *	return IRQ_HANDLED;
 * }
 */

static int wm_display_setup_irq(struct wm_display_private *display_priv)
{
	/** TODO: Setup IRQ for interrupts (eg. HPD, CEC etc.)
	 * Call handler function of respective IRQ owner sub module.
	 * int ret = 0;
	 * unsigned int virq = 0;

	 * virq = irq_create_mapping(display_priv->wmmgr_ctxt->irq_domain, WM_DISPLAY_HDP_INT);
	 * if (!virq) {
	 *	pr_err("failed to map IRQ\n");
	 *	return -EINVAL;
	 * }

	 * ret = devm_request_threaded_irq(&display_priv->pdev->dev, virq, NULL,
	 *					wm_display_irq_handler, 0,
	 *					"wm-display", display_priv);
	 * if (ret)
	 *	pr_err("failed to request IRQ\n");
	 */

	return 0;
}

static int wm_display_hdmi_power_on(struct wm_display_private *display_priv)
{
	/** TODO: Turn on HDMI power domain for HPD */
	return 0;
}

static int wm_display_probe(struct platform_device *pdev)
{
	struct wm_display_private *display_priv;
	int ret = 0;

	if (!pdev || pdev->dev.of_node) {
		pr_err("pdev not found\n");
		ret = -ENODEV;
		goto error;
	}

	display_priv = devm_kzalloc(&pdev->dev, sizeof(struct wm_display_private), GFP_KERNEL);
	if (!display_priv) {
		ret = -ENOMEM;
		goto error;
	}

	dev_set_drvdata(&pdev->dev, display_priv);

	ret = wm_display_parse_dt(display_priv);
	if (ret) {
		pr_err("failed to parse device tree\n");
		goto error;
	}

	ret = wm_display_init_modules(display_priv);
	if (ret) {
		pr_err("failed to init modules\n");
		goto error;
	}

	/** TODO: WM Manager Registration
	 * Allocate wmmgr_cient_context structure
	 * Fill unique client ID/Name
	 * display_priv->wmmgr_ctxt = devm_kzalloc(&pdev->dev, sizeof(struct wmmgr_client_context),
	 *						GFP_KERNEL);
	 * wmmgr_register_client(&pdev->dev, display_priv->wmmgr_ctxt);
	 */

	ret = wm_display_setup_irq(display_priv);
	if (ret) {
		pr_err("failed to setup irq\n");
		goto error;
	}

	ret = wm_display_hdmi_power_on(display_priv);
	if (ret) {
		pr_err("failed to turn on hdmi power domain\n");
		goto error;
	}

	ret = display_priv->hdcp->download_firmware(display_priv->hdcp);
	if (ret) {
		pr_err("failed to download hdcp firmware\n");
		goto error;
	}

	return 0;

error:
	return ret;
}

static int wm_display_remove(struct platform_device *pdev)
{
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
