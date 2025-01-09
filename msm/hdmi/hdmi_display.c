// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>

#include "msm_drv.h"
#include "sde_connector.h"
#include "hdmi_display.h"
#include "hdmi_parser.h"
#include "hdmi_power.h"
#include "hdmi_debug.h"
#include "hdmi_pll.h"
#include "sde_hdcp.h"

#define hdmi_display_state_show(x) { \
	HDMI_ERR("%s: state (0x%x): %s\n", x, hdmi->state, \
		hdmi_display_state_name(hdmi->state)); \
	SDE_EVT32_EXTERNAL(hdmi->state); }

#define hdmi_display_state_warn(x) { \
	HDMI_WARN("%s: state (0x%x): %s\n", x, hdmi->state, \
		hdmi_display_state_name(hdmi->state)); \
	SDE_EVT32_EXTERNAL(hdmi->state); }

#define hdmi_display_state_log(x) { \
	HDMI_DEBUG("%s: state (0x%x): %s\n", x, hdmi->state, \
		hdmi_display_state_name(hdmi->state)); \
	SDE_EVT32_EXTERNAL(hdmi->state); }

#define hdmi_display_state_is(x) (hdmi->state & (x))
#define hdmi_display_state_add(x) { \
	(hdmi->state |= (x)); \
	hdmi_display_state_log("add "#x); }
#define hdmi_display_state_remove(x) { \
	(hdmi->state &= ~(x)); \
	hdmi_display_state_log("remove "#x); }

#define HPD_STRING_SIZE 30

enum hdmi_display_states {
	HDMI_STATE_DISCONNECTED           = 0,
	HDMI_STATE_CONFIGURED             = BIT(0),
	HDMI_STATE_INITIALIZED            = BIT(1),
	HDMI_STATE_READY                  = BIT(2),
	HDMI_STATE_CONNECTED              = BIT(3),
	HDMI_STATE_CONNECT_NOTIFIED       = BIT(4),
	HDMI_STATE_DISCONNECT_NOTIFIED    = BIT(5),
	HDMI_STATE_ENABLED                = BIT(6),
	HDMI_STATE_SUSPENDED              = BIT(7),
	HDMI_STATE_ABORTED                = BIT(8),
	HDMI_STATE_HDCP_ABORTED           = BIT(9),
	HDMI_STATE_SRC_PWRDN              = BIT(10),
	HDMI_STATE_TUI_ACTIVE             = BIT(11),
};

static struct hdmi_display *g_hdmi_display[MAX_HDMI_ACTIVE_DISPLAY];

static char *hdmi_display_state_name(enum hdmi_display_states state)
{
	static char buf[SZ_1K];
	u32 len = 0;

	memset(buf, 0, SZ_1K);

	if (state & HDMI_STATE_CONFIGURED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"CONFIGURED");

	if (state & HDMI_STATE_INITIALIZED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"INITIALIZED");

	if (state & HDMI_STATE_READY)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"READY");

	if (state & HDMI_STATE_CONNECTED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"CONNECTED");

	if (state & HDMI_STATE_CONNECT_NOTIFIED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"CONNECT_NOTIFIED");

	if (state & HDMI_STATE_DISCONNECT_NOTIFIED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"DISCONNECT_NOTIFIED");

	if (state & HDMI_STATE_ENABLED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"ENABLED");

	if (state & HDMI_STATE_SUSPENDED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"SUSPENDED");

	if (state & HDMI_STATE_ABORTED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"ABORTED");

	if (state & HDMI_STATE_HDCP_ABORTED)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"HDCP_ABORTED");

	if (state & HDMI_STATE_SRC_PWRDN)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"SRC_PWRDN");

	if (state & HDMI_STATE_TUI_ACTIVE)
		len += scnprintf(buf + len, sizeof(buf) - len, "|%s|",
			"TUI_ACTIVE");

	if (!strlen(buf))
		return "DISCONNECTED";

	return buf;
}


struct hdmi_hdcp_dev {
	void *fd;
	struct sde_hdcp_ops *ops;
	enum sde_hdcp_version ver;
};

struct hdmi_hdcp {
	void *data;
	struct sde_hdcp_ops *ops;

	u32 source_cap;

	struct hdmi_hdcp_dev dev[HDCP_VERSION_MAX];
};

struct hdmi_display_private {
	char *name;
	int irq;

	enum hdmi_display_states state;

	struct platform_device *pdev;

	struct hdmi_parser  *parser;
	struct hdmi_power   *power;
	struct hdmi_panel   *panel;
	struct hdmi_debug   *debug;
	struct hdmi_pll     *pll;

	struct hdmi_hdcp hdcp;

	struct hdmi_display hdmi_display;
	struct msm_drm_private *priv;

	struct workqueue_struct *wq;
	struct work_struct hpd_work;
	struct mutex session_lock;	// TODO same as display_lock in 4.4
	struct mutex accounting_lock;

	u32 tot_dsc_blks_in_use;
	u32 tot_lm_blks_in_use;

	bool cont_splash_enabled;

	bool process_hpd_connect;
	struct dev_pm_qos_request pm_qos_req[NR_CPUS];
	bool pm_qos_requested;

	u32 cell_idx;
	u32 phy_idx;
};

static int hdmi_init_sub_modules(struct hdmi_display_private *hdmi)
{
	int rc = 0;

	mutex_init(&hdmi->session_lock);
	mutex_init(&hdmi->accounting_lock);

	hdmi->parser = hdmi_parser_get(hdmi->pdev);
	if (IS_ERR(hdmi->parser)) {
		rc = PTR_ERR(hdmi->parser);
		HDMI_ERR("failed to initialize parser, rc = %d\n", rc);
		hdmi->parser = NULL;
		goto error;
	}

	rc = hdmi->parser->parse(hdmi->parser);
	if (rc) {
		HDMI_ERR("device tree parsing failed\n");
		goto error_parser;
	}

	return 0;

error_parser:
	hdmi_parser_put(hdmi->parser);
error:
	return rc;
}

int hdmi_display_get_num_of_displays(struct drm_device *dev)
{
	int i, j;

	for (i = 0, j = 0; i < MAX_HDMI_ACTIVE_DISPLAY; i++) {
		if (!g_hdmi_display[i])
			break;

		if (!dev || g_hdmi_display[i]->drm_dev == dev)
			j++;
	}

	return j;
}

static int hdmi_display_post_init(struct hdmi_display *hdmi_display)
{
	int rc = 0;
	struct hdmi_display_private *hdmi;

	if (!hdmi_display) {
		HDMI_ERR("invalid input\n");
		rc = -EINVAL;
		goto end;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);
	if (IS_ERR_OR_NULL(hdmi)) {
		HDMI_ERR("invalid params\n");
		rc = -EINVAL;
		goto end;
	}

	rc = hdmi_init_sub_modules(hdmi);
	if (rc)
		goto end;

	hdmi_display->post_init = NULL;
end:
	HDMI_DEBUG("%s\n", rc ? "failed" : "success");
	return rc;
}

static int hdmi_display_create_workqueue(struct hdmi_display_private *hdmi)
{
	hdmi->wq = create_singlethread_workqueue("drm_hdmi");
	if (IS_ERR_OR_NULL(hdmi->wq)) {
		HDMI_ERR("Error creating wq\n");
		return -EPERM;
	}

	return 0;
}

static void _hdmi_ctrl_irq(struct hdmi_display_private *hdmi)
{
}

static irqreturn_t hdmi_display_irq(int irq, void *dev_id)
{
	struct hdmi_display_private *hdmi = dev_id;

	if (!hdmi) {
		HDMI_ERR("invalid data\n");
		return IRQ_NONE;
	}

	/* Process HPD: */
	_hdmi_ctrl_irq(hdmi);

	return IRQ_HANDLED;
}

static int hdmi_display_enable(struct hdmi_display *hdmi_display, void *panel)
{
	int rc = 0;
	struct hdmi_display_private *hdmi;

	if (!hdmi_display || !panel) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY, hdmi->state);
	mutex_lock(&hdmi->session_lock);

	/*
	 * If HDMI_STATE_READY is not set, we should not do any HW
	 * programming.
	 */
	if (!hdmi_display_state_is(HDMI_STATE_READY)) {
		hdmi_display_state_show("[host not ready]");
		goto end;
	}

	/*
	 * It is possible that by the time we get call back to establish
	 * the HDMI pipeline e2e, the physical HDMI connection to the sink is
	 * already lost. In such cases, the HDMI_STATE_ABORTED would be set.
	 * However, it is necessary to NOT abort the display setup here so as
	 * to ensure that the rest of the system is in a stable state prior to
	 * handling the disconnect notification.
	 */
	if (hdmi_display_state_is(HDMI_STATE_ABORTED))
		hdmi_display_state_log("[aborted, but continue on]");

	hdmi_display_state_add(HDMI_STATE_ENABLED);
end:
	mutex_unlock(&hdmi->session_lock);
	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, hdmi->state, rc);
	return rc;
}

static int hdmi_display_post_enable(struct hdmi_display *hdmi_display, void *panel)
{
	return 0;
}

static int hdmi_display_pre_disable(struct hdmi_display *hdmi_display, void *panel)
{
	return 0;
}

static int hdmi_display_disable(struct hdmi_display *hdmi_display, void *panel)
{
	return 0;
}

static int hdmi_display_get_display_type(struct hdmi_display *hdmi_display,
		const char **display_type)
{
	struct hdmi_display_private *hdmi;

	if (!hdmi_display || !display_type) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	if (hdmi->parser)
		*display_type = hdmi->parser->display_type;

	return 0;
}

static int hdmi_display_get_modes(struct hdmi_display *hdmi_display, void *panel,
	struct hdmi_display_mode *hdmi_mode)
{
	struct hdmi_display_private *hdmi;
	struct hdmi_panel *hdmi_panel;
	int ret = 0;

	if (!hdmi_display || !panel) {
		HDMI_ERR("invalid params\n");
		return 0;
	}

	hdmi_panel = panel;
	if (!hdmi_panel->connector) {
		HDMI_ERR("invalid connector\n");
		return 0;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	ret = hdmi_panel->get_modes(hdmi_panel,
			hdmi_panel->connector, hdmi_mode);

	if (hdmi_mode->timing.pixel_clk_khz)
		hdmi_display->max_pclk_khz = hdmi_mode->timing.pixel_clk_khz;

	return ret;
}


static int hdmi_display_prepare(struct hdmi_display *hdmi_display, void *panel)
{
	//TODO
	return 0;
}

static int hdmi_display_unprepare(struct hdmi_display *hdmi_display, void *panel)
{
	//TODO
	return 0;
}

static int hdmi_display_set_mode(struct hdmi_display *hdmi_display, void *panel,
		struct hdmi_display_mode *mode)
{
	//TODO
	return 0;
}

static struct hdmi_debug *hdmi_get_debug(struct hdmi_display *hdmi_display)
{
	struct hdmi_display_private *hdmi;

	if (!hdmi_display) {
		HDMI_ERR("invalid input\n");
		return ERR_PTR(-EINVAL);
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	return hdmi->debug;
}

static int hdmi_display_get_available_hdmi_resources(
		struct hdmi_display *hdmi_display,
		const struct msm_resource_caps_info *avail_res,
		struct msm_resource_caps_info *max_hdmi_avail_res)
{
	if (!hdmi_display || !avail_res || !max_hdmi_avail_res) {
		HDMI_ERR("invalid arguments\n");
		return -EINVAL;
	}

	memcpy(max_hdmi_avail_res, avail_res,
			sizeof(struct msm_resource_caps_info));

	max_hdmi_avail_res->num_lm = min(avail_res->num_lm,
			hdmi_display->max_mixer_count);
	max_hdmi_avail_res->num_dsc = min(avail_res->num_dsc,
			hdmi_display->max_dsc_count);

	HDMI_DEBUG_V("max_lm:%d, avail_lm:%d, hdmi_avail_lm:%d\n",
			hdmi_display->max_mixer_count, avail_res->num_lm,
			max_hdmi_avail_res->num_lm);

	HDMI_DEBUG_V("max_dsc:%d, avail_dsc:%d, hdmi_avail_dsc:%d\n",
			hdmi_display->max_dsc_count, avail_res->num_dsc,
			max_hdmi_avail_res->num_dsc);

	return 0;
}

static int hdmi_display_validate_pixel_clock(struct hdmi_display_mode hdmi_mode,
		u32 max_pclk_khz, u32 pclk_factor)
{
	u32 pclk_khz = hdmi_mode.timing.pixel_clk_khz;

	pclk_khz = pclk_khz / pclk_factor;
	if (pclk_khz > max_pclk_khz) {
		HDMI_DEBUG("clk: %d kHz, max: %d kHz\n", pclk_khz, max_pclk_khz);
		return -EPERM;
	}

	return 0;
}

static enum drm_mode_status hdmi_display_validate_mode(
		struct hdmi_display *hdmi_display,
		void *panel, struct drm_display_mode *mode,
		const struct msm_resource_caps_info *avail_res)
{
	struct hdmi_display_private *hdmi;
	struct hdmi_panel *hdmi_panel;
	struct hdmi_debug *debug;
	enum drm_mode_status mode_status = MODE_BAD;
	struct hdmi_display_mode hdmi_mode;
	int rc = 0;

	if (!hdmi_display || !mode || !panel ||
			!avail_res || !avail_res->max_mixer_width) {
		HDMI_ERR("invalid params\n");
		return mode_status;
	}

	hdmi = container_of(hdmi_display, struct hdmi_display_private, hdmi_display);

	mutex_lock(&hdmi->session_lock);

	hdmi_panel = panel;
	if (!hdmi_panel->connector) {
		HDMI_ERR("invalid connector\n");
		goto end;
	}

	debug = hdmi->debug;
	if (!debug)
		goto end;

	hdmi_display->convert_to_hdmi_mode(hdmi_display, panel, mode, &hdmi_mode);

	/* As per spec, 640x480 mode should always be present as fail-safe */
	if ((hdmi_mode.timing.h_active == 640) &&
			(hdmi_mode.timing.v_active == 480) &&
			(hdmi_mode.timing.pixel_clk_khz == 25175)) {
		goto skip_validation;
	}

	rc = hdmi_display_validate_pixel_clock(hdmi_mode,
			hdmi_display->max_pclk_khz,
			hdmi_panel->pclk_factor);
	if (rc)
		goto end;

skip_validation:
	mode_status = MODE_OK;

	if (!avail_res->num_lm_in_use) {
		mutex_lock(&hdmi->accounting_lock);
		hdmi->tot_lm_blks_in_use -= hdmi_panel->max_lm;
		hdmi_panel->max_lm = max(hdmi_panel->max_lm, hdmi_mode.lm_count);
		hdmi->tot_lm_blks_in_use += hdmi_panel->max_lm;
		mutex_unlock(&hdmi->accounting_lock);
	}

end:
	mutex_unlock(&hdmi->session_lock);

	HDMI_DEBUG_V("[%s clk:%d] mode is %s\n", mode->name, mode->clock,
			(mode_status == MODE_OK) ? "valid" : "invalid");

	return mode_status;
}

static void hdmi_display_convert_to_hdmi_mode(struct hdmi_display *hdmi_display,
		void *panel,
		const struct drm_display_mode *drm_mode,
		struct hdmi_display_mode *hdmi_mode)
{
	int rc;
	struct hdmi_display_private *hdmi;
	struct hdmi_panel *hdmi_panel;

	if (!hdmi_display || !drm_mode || !hdmi_mode || !panel) {
		HDMI_ERR("invalid input\n");
		return;
	}

	hdmi = container_of(hdmi_display, struct hdmi_display_private, hdmi_display);
	hdmi_panel = panel;

	memset(hdmi_mode, 0, sizeof(*hdmi_mode));

	rc = hdmi_panel->convert_to_hdmi_mode(hdmi_panel, drm_mode, hdmi_mode);
	if (rc == -EAGAIN)
		hdmi_panel->convert_to_hdmi_mode(hdmi_panel, drm_mode, hdmi_mode);
}

static int hdmi_display_setup_colospace(struct hdmi_display *hdmi_display,
		void *panel,
		u32 colorspace)
{
	struct hdmi_panel *hdmi_panel;
	struct hdmi_display_private *hdmi;

	if (!hdmi_display || !panel) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	if (!hdmi_display_state_is(HDMI_STATE_ENABLED)) {
		hdmi_display_state_show("[not enabled]");
		return 0;
	}

	hdmi_panel = panel;

	return hdmi_panel->set_colorspace(hdmi_panel, colorspace);
}

static int hdmi_display_config_hdr(struct hdmi_display *hdmi_display, void *panel,
			struct drm_msm_ext_hdr_metadata *hdr, bool dhdr_update)
{
	struct hdmi_panel *hdmi_panel;
	struct sde_connector *sde_conn;
	struct hdmi_display_private *hdmi;
	u64 core_clk_rate;
	bool flush_hdr;

	if (!hdmi_display || !panel) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi_panel = panel;
	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);
	sde_conn =  to_sde_connector(hdmi_panel->connector);

	if (sde_cesta_is_enabled(DPUID(hdmi_display->drm_dev)))
		core_clk_rate = sde_cesta_get_core_clk_rate(
					DPUID(hdmi_display->drm_dev));
	else
		core_clk_rate = hdmi->power->clk_get_rate(hdmi->power,
						"core_clk");
	if (!core_clk_rate) {
		HDMI_ERR("invalid rate for core_clk\n");
		return -EINVAL;
	}

	if (!hdmi_display_state_is(HDMI_STATE_ENABLED)) {
		hdmi_display_state_show("[not enabled]");
		return 0;
	}

	/*
	 * In rare cases where HDR metadata is updated independently
	 * flush the HDR metadata immediately instead of relying on
	 * the colorspace
	 */
	flush_hdr = !sde_conn->colorspace_updated;

	if (flush_hdr)
		HDMI_DEBUG("flushing the HDR metadata\n");
	else
		HDMI_DEBUG("piggy-backing with colorspace\n");

	return hdmi_panel->setup_hdr(hdmi_panel, hdr, dhdr_update,
		core_clk_rate, flush_hdr);
}

static int hdmi_display_update_pps(struct hdmi_display *hdmi_display,
		struct drm_connector *connector, char *pps_cmd)
{
	struct sde_connector *sde_conn;
	struct hdmi_panel *hdmi_panel;
	struct hdmi_display_private *hdmi;

	hdmi = container_of(hdmi_display,
			struct hdmi_display_private, hdmi_display);

	sde_conn = to_sde_connector(connector);
	if (!sde_conn->drv_panel) {
		HDMI_ERR("invalid panel for connector:%d\n",
				connector->base.id);
		return -EINVAL;
	}

	if (!hdmi_display_state_is(HDMI_STATE_ENABLED)) {
		hdmi_display_state_show("[not enabled]");
		return 0;
	}

	hdmi_panel = sde_conn->drv_panel;
	hdmi_panel->update_pps(hdmi_panel, pps_cmd);
	return 0;
}


static int hdmi_request_irq(struct hdmi_display *hdmi_display)
{
	int rc = 0;
	struct hdmi_display_private *hdmi;

	if (!hdmi_display) {
		HDMI_ERR("invalid input\n");
		return -EINVAL;
	}

	hdmi = container_of(hdmi_display, struct hdmi_display_private, hdmi_display);

	hdmi->irq = irq_of_parse_and_map(hdmi->pdev->dev.of_node, 0);
	if (hdmi->irq < 0) {
		rc = hdmi->irq;
		HDMI_ERR("failed to get irq: %d\n", rc);
		return rc;
	}

	rc = devm_request_irq(&hdmi->pdev->dev, hdmi->irq, hdmi_display_irq,
	IRQF_TRIGGER_HIGH, "hdmi_display_isr", hdmi);
	if (rc < 0) {
		HDMI_ERR("failed to request IRQ%u: %d\n",
					hdmi->irq, rc);
		return rc;
	}

	disable_irq(hdmi->irq);

	return 0;

}


int hdmi_display_get_displays(struct drm_device *dev, void **displays,
		int count)
{
	int i, j;

	if (!displays) {
		HDMI_ERR("invalid data\n");
		return -EINVAL;
	}

	for (i = 0, j = 0; i < MAX_HDMI_ACTIVE_DISPLAY && j < count; i++) {
		if (!g_hdmi_display[i])
			break;

		if (g_hdmi_display[i]->drm_dev == dev) {
			displays[j] = g_hdmi_display[i];
			j++;
		}
	}

	return j;
}

static void hdmi_display_deinit_sub_modules(struct hdmi_display_private *hdmi)
{
	hdmi_parser_put(hdmi->parser);
	mutex_destroy(&hdmi->session_lock);
}

static int hdmi_display_bind(struct device *dev, struct device *master,
		void *data)
{
	int rc = 0;
	struct hdmi_display_private *hdmi;
	struct drm_device *drm;
	struct platform_device *pdev = to_platform_device(dev);

	if (!dev || !pdev || !master) {
		HDMI_ERR("invalid param(s), dev %pK, pdev %pK, master %pK\n",
				dev, pdev, master);
		rc = -EINVAL;
		goto end;
	}

	drm = dev_get_drvdata(master);
	hdmi = platform_get_drvdata(pdev);
	if (!drm || !hdmi) {
		HDMI_ERR("invalid param(s), drm %pK, hdmi %pK\n",
				drm, hdmi);
		rc = -EINVAL;
		goto end;
	}

	hdmi->hdmi_display.drm_dev = drm;
	hdmi->priv = drm->dev_private;
end:
	return rc;
}

static void hdmi_display_unbind(struct device *dev, struct device *master,
		void *data)
{
	struct hdmi_display_private *hdmi;
	struct platform_device *pdev = to_platform_device(dev);

	if (!dev || !pdev) {
		HDMI_ERR("invalid param(s)\n");
		return;
	}

	hdmi = platform_get_drvdata(pdev);
	if (!hdmi) {
		HDMI_ERR("Invalid params\n");
		return;
	}

	if (hdmi->power)
		(void)hdmi->power->power_client_deinit(hdmi->power);
}

static const struct component_ops hdmi_display_comp_ops = {
	.bind = hdmi_display_bind,
	.unbind = hdmi_display_unbind,
};

static const struct of_device_id hdmi_dt_match[] = {
	{ .compatible = "qcom,hdmi-display"},
	{}
};


static int hdmi_display_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct hdmi_display_private *hdmi;
	struct hdmi_display *hdmi_display;
	const struct of_device_id *id;
	int index;

	if (!pdev || !pdev->dev.of_node) {
		HDMI_ERR("pdev not found\n");
		rc = -ENODEV;
		goto bail;
	}

	id = of_match_node(hdmi_dt_match, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	index = hdmi_display_get_num_of_displays(NULL);
	if (index >= MAX_HDMI_ACTIVE_DISPLAY) {
		HDMI_ERR("exceeds max hdmi count\n");
		rc = -EINVAL;
		goto bail;
	}

	hdmi = kzalloc(sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi) {
		rc = -ENOMEM;
		goto bail;
	}

	hdmi->pdev = pdev;
	hdmi->name = "drm_hdmi";

	rc = hdmi_display_create_workqueue(hdmi);
	if (rc) {
		HDMI_ERR("Failed to create workqueue\n");
		goto error;
	}

	platform_set_drvdata(pdev, hdmi);

	hdmi_display = &hdmi->hdmi_display;
	g_hdmi_display[index] = hdmi_display;

	hdmi_display->enable        = hdmi_display_enable;
	hdmi_display->post_enable   = hdmi_display_post_enable;
	hdmi_display->pre_disable   = hdmi_display_pre_disable;
	hdmi_display->disable       = hdmi_display_disable;
	hdmi_display->set_mode      = hdmi_display_set_mode;
	hdmi_display->validate_mode = hdmi_display_validate_mode;
	hdmi_display->get_modes     = hdmi_display_get_modes;
	hdmi_display->prepare       = hdmi_display_prepare;
	hdmi_display->unprepare     = hdmi_display_unprepare;
	hdmi_display->request_irq   = hdmi_request_irq;
	hdmi_display->get_debug     = hdmi_get_debug;
	hdmi_display->post_open     = NULL;
	hdmi_display->post_init     = hdmi_display_post_init;
	hdmi_display->config_hdr    = hdmi_display_config_hdr;
	hdmi_display->update_pps = hdmi_display_update_pps;

	hdmi_display->convert_to_hdmi_mode = hdmi_display_convert_to_hdmi_mode;
	hdmi_display->set_colorspace = hdmi_display_setup_colospace;
	hdmi_display->get_available_hdmi_resources =
				hdmi_display_get_available_hdmi_resources;
	hdmi_display->get_display_type = hdmi_display_get_display_type;

	rc = component_add(&pdev->dev, &hdmi_display_comp_ops);
	if (rc) {
		HDMI_ERR("component add failed, rc=%d\n", rc);
		goto error;
	}

	return 0;
error:
	g_hdmi_display[index] = NULL;
bail:
	return rc;
}

static int hdmi_display_remove(struct platform_device *pdev)
{
	struct hdmi_display_private *hdmi;

	if (!pdev)
		return -EINVAL;

	hdmi = platform_get_drvdata(pdev);

	hdmi_display_deinit_sub_modules(hdmi);

	if (hdmi->wq)
		destroy_workqueue(hdmi->wq);

	platform_set_drvdata(pdev, NULL);
	kfree(hdmi);

	return 0;
}


static int hdmi_pm_prepare(struct device *dev)
{
	// pm_prepare
	return 0;
}
static void hdmi_pm_complete(struct device *dev)
{
	// pm_complete
}

static const struct dev_pm_ops hdmi_pm_ops = {
	.prepare = hdmi_pm_prepare,
	.complete = hdmi_pm_complete,
};

static struct platform_driver hdmi_display_driver = {
	.probe  = hdmi_display_probe,
	.remove = hdmi_display_remove,
	.driver = {
		.name = "msm-hdmi-display",
		.of_match_table = hdmi_dt_match,
		.suppress_bind_attrs = true,
		.pm = &hdmi_pm_ops,
	},
};
void __init hdmi_display_register(void)
{
	platform_driver_register(&hdmi_display_driver);
}
void __exit hdmi_display_unregister(void)
{
	platform_driver_unregister(&hdmi_display_driver);
}
