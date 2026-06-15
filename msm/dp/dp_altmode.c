// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#if __has_include(<soc/qcom/pmic_glink_altmode.h>)
    #include <linux/soc/qcom/pmic_glink_altmode.h>
    #include "qcom_display_internal.h"
#else
    #include <linux/soc/qcom/altmode-glink.h>
#endif
#include <linux/usb/dwc3-msm.h>
#include <linux/usb/pd_vdo.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/typec_retimer.h>
#include <linux/of_platform.h>
#include <linux/version.h>

#include "dp_altmode.h"
#include "dp_debug.h"
#include "sde_dbg.h"


#define ALTMODE_CONFIGURE_MASK (0x3f)
#define ALTMODE_HPD_STATE_MASK (0x40)
#define ALTMODE_HPD_IRQ_MASK (0x80)
#define NB7_TYPEC_GET_RETRIES 50
#define NB7_TYPEC_GET_DELAY_MS 20

struct dp_altmode_private {
	bool forced_disconnect;
	struct device *dev;
	struct dp_hpd_cb *dp_cb;
	struct dp_altmode dp_altmode;
	struct altmode_client *amclient;
	bool connected;
	u32 lanes;
	int orientation;
	u16 svid;
	int mode;
	int hpd_state;
	int hpd_irq;
#if IS_REACHABLE(CONFIG_TYPEC)
	bool nb7_checked;
	bool nb7_present;
	struct typec_switch *nb7_sw;
	struct typec_retimer *nb7_retimer;
#endif
};

enum dp_altmode_pin_assignment {
	DPAM_HPD_OUT,
	DPAM_HPD_A,
	DPAM_HPD_B,
	DPAM_HPD_C,
	DPAM_HPD_D,
	DPAM_HPD_E,
	DPAM_HPD_F,
};

#if IS_REACHABLE(CONFIG_TYPEC)
static enum typec_orientation dp_altmode_typec_orientation(u8 orientation)
{
	switch (orientation) {
	case 0:
		return TYPEC_ORIENTATION_NORMAL;
	case 1:
		return TYPEC_ORIENTATION_REVERSE;
	default:
		return TYPEC_ORIENTATION_NONE;
	}
}

static bool dp_altmode_is_nb7_retimer(struct device_node *node)
{
	return node && of_device_is_compatible(node, "onnn,nb7vpq904m") &&
		of_property_read_bool(node, "retimer-switch");
}

static struct typec_switch *dp_altmode_get_nb7_switch(struct fwnode_handle *fwnode,
		bool allow_sleep)
{
	struct typec_switch *sw = NULL;
	int retries = allow_sleep ? NB7_TYPEC_GET_RETRIES : 1;
	int retry;

	for (retry = 0; retry < retries; retry++) {
		sw = fwnode_typec_switch_get(fwnode);
		if (!IS_ERR(sw) || PTR_ERR(sw) != -EPROBE_DEFER)
			return sw;

		if (retry + 1 < retries)
			msleep(NB7_TYPEC_GET_DELAY_MS);
	}

	return sw;
}

static struct typec_retimer *dp_altmode_get_nb7_retimer(struct fwnode_handle *fwnode,
		bool allow_sleep)
{
	struct typec_retimer *retimer = NULL;
	int retries = allow_sleep ? NB7_TYPEC_GET_RETRIES : 1;
	int retry;

	for (retry = 0; retry < retries; retry++) {
		retimer = fwnode_typec_retimer_get(fwnode);
		if (!IS_ERR(retimer) || PTR_ERR(retimer) != -EPROBE_DEFER)
			return retimer;

		if (retry + 1 < retries)
			msleep(NB7_TYPEC_GET_DELAY_MS);
	}

	return retimer;
}

static int dp_altmode_cache_nb7_retimer(struct dp_altmode_private *altmode,
		bool allow_sleep)
{
	struct device_node *connector_node;
	struct device_node *redriver_node;
	struct fwnode_handle *fwnode;
	struct typec_retimer *retimer;
	struct typec_switch *sw;
	int rc = 0;

	if (!altmode || !altmode->dev || !altmode->dev->of_node) {
		DP_DEBUG("skip NB7 Type-C retimer cache: altmode=%pK dev=%pK of_node=%pK\n",
				altmode, altmode ? altmode->dev : NULL,
				(altmode && altmode->dev) ? altmode->dev->of_node : NULL);
		return 0;
	}

	if (altmode->nb7_checked)
		return 0;

	redriver_node = of_parse_phandle(altmode->dev->of_node,
		"qcom,dp-aux-switch", 0);
	if (!redriver_node) {
		altmode->nb7_checked = true;
		return 0;
	}

	if (!dp_altmode_is_nb7_retimer(redriver_node)) {
		of_node_put(redriver_node);
		altmode->nb7_checked = true;
		return 0;
	}

	/*
	 * qcom,dp-aux-switch points at NB7. The Type-C helpers must start
	 * from the usb-c-connector fwnode, because they resolve graph-connected
	 * devices carrying orientation-switch / retimer-switch.
	 *
	 * For the common onnn,nb7vpq904m binding, port@0 is the connector side.
	 */
	connector_node = of_graph_get_remote_node(redriver_node, 0, 0);
	of_node_put(redriver_node);
	if (!connector_node) {
		DP_ERR("failed to find Type-C connector for NB7 retimer\n");
		if (!allow_sleep) {
			altmode->nb7_checked = true;
			return -ENODEV;
		}
		return -EPROBE_DEFER;
	}

	fwnode = of_fwnode_handle(connector_node);

	sw = dp_altmode_get_nb7_switch(fwnode, allow_sleep);
	if (IS_ERR_OR_NULL(sw)) {
		rc = PTR_ERR_OR_ZERO(sw) ?: -ENODEV;
		DP_ERR("failed to get NB7 Type-C switch: %d\n", rc);
		goto put_connector;
	}

	retimer = dp_altmode_get_nb7_retimer(fwnode, allow_sleep);
	if (IS_ERR_OR_NULL(retimer)) {
		rc = PTR_ERR_OR_ZERO(retimer) ?: -ENODEV;
		DP_ERR("failed to get NB7 Type-C retimer: %d\n", rc);
		typec_switch_put(sw);
		goto put_connector;
	}

	altmode->nb7_sw = sw;
	altmode->nb7_retimer = retimer;
	altmode->nb7_present = true;
	altmode->nb7_checked = true;

	rc = 0;

put_connector:
	of_node_put(connector_node);
	if (rc && rc != -EPROBE_DEFER)
		altmode->nb7_checked = true;
	return rc;
}

static void dp_altmode_put_nb7_retimer(struct dp_altmode_private *altmode)
{
	if (!altmode)
		return;

	if (altmode->nb7_retimer)
		typec_retimer_put(altmode->nb7_retimer);
	if (altmode->nb7_sw)
		typec_switch_put(altmode->nb7_sw);

	altmode->nb7_retimer = NULL;
	altmode->nb7_sw = NULL;
	altmode->nb7_present = false;
	altmode->nb7_checked = false;
}

static int dp_altmode_configure_nb7_retimer(struct dp_altmode_private *altmode,
	bool enable, u8 orientation, u8 pin, bool hpd_state, bool hpd_irq)
{
	struct typec_displayport_data dp_data = { };
	struct typec_retimer_state retimer_state = { };
	struct typec_altmode dp_alt = { };
	unsigned long mode = TYPEC_STATE_USB;
	int rc = 0;

	rc = dp_altmode_cache_nb7_retimer(altmode, false);
	if (rc || !altmode->nb7_present)
		return rc;

	rc = typec_switch_set(altmode->nb7_sw, enable ?
		dp_altmode_typec_orientation(orientation) :
		TYPEC_ORIENTATION_NONE);
	if (rc) {
		DP_ERR("failed to set NB7 Type-C orientation: %d\n", rc);
		if (enable)
			return rc;
	}

	if (enable) {
		if (pin < DPAM_HPD_A || pin > DPAM_HPD_F) {
			rc = -EINVAL;
			DP_ERR("invalid DP pin assignment for NB7: %u\n", pin);
			return rc;
		}

		mode = TYPEC_MODAL_STATE(pin - DPAM_HPD_A);

		dp_alt.svid = USB_TYPEC_DP_SID;
		dp_alt.mode = USB_TYPEC_DP_MODE;
		dp_alt.active = true;

		dp_data.status = DP_STATUS_ENABLED;
		if (hpd_state)
			dp_data.status |= DP_STATUS_HPD_STATE;
		if (hpd_irq)
			dp_data.status |= DP_STATUS_IRQ_HPD;
		dp_data.conf = DP_CONF_SET_PIN_ASSIGN(BIT(pin - DPAM_HPD_A));

		retimer_state.alt = &dp_alt;
		retimer_state.data = &dp_data;
	}

	retimer_state.mode = mode;

	rc = typec_retimer_set(altmode->nb7_retimer, &retimer_state);
	if (rc)
		DP_ERR("failed to set NB7 Type-C retimer: %d\n", rc);
	else
		DP_INFO("configured NB7 Type-C retimer enable=%d orientation=%u pin=%u "
			"mode=%lu hpd=%u irq=%u\n",
			enable, orientation, pin, mode, hpd_state, hpd_irq);

	return rc;
}
#else
static int dp_altmode_configure_nb7_retimer(struct dp_altmode_private *altmode,
	bool enable, u8 orientation, u8 pin, bool hpd_state, bool hpd_irq)
{
	return 0;
}

static void dp_altmode_put_nb7_retimer(struct dp_altmode_private *altmode)
{
}
#endif

static int dp_altmode_set_usb_dp_mode(struct dp_altmode_private *altmode)
{
	int rc = 0;
	struct device_node *np;
	struct device_node *usb_node;
	struct platform_device *usb_pdev;
	int timeout = 250;

	if (!altmode || !altmode->dev) {
		DP_ERR("invalid args\n");
		return -EINVAL;
	}

	np = altmode->dev->of_node;

	usb_node = of_parse_phandle(np, "usb-controller", 0);
	if (!usb_node) {
		DP_ERR("unable to get usb node\n");
		return -EINVAL;
	}

	usb_pdev = of_find_device_by_node(usb_node);
	if (!usb_pdev) {
		of_node_put(usb_node);
		DP_ERR("unable to get usb pdev\n");
		return -EINVAL;
	}

	while (timeout) {
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6,12,0))
			rc = dwc3_msm_set_dp_mode(&usb_pdev->dev, altmode->connected,
					altmode->lanes, altmode->orientation, altmode->svid,
					altmode->mode, altmode->hpd_state, altmode->hpd_irq);
		#else
			rc = dwc3_msm_set_dp_mode(&usb_pdev->dev, altmode->connected, altmode->lanes);
		#endif
		if (rc != -EBUSY && rc != -EAGAIN)
			break;

		DP_WARN("USB busy, retry\n");

		/* wait for hw recommended delay for usb */
		msleep(20);
		timeout--;
	}
	of_node_put(usb_node);
	platform_device_put(usb_pdev);

	if (rc)
		DP_ERR("Error releasing SS lanes: %d\n", rc);

	return rc;
}

static void dp_altmode_send_pan_ack(struct altmode_client *amclient,
		u8 port_index)
{
	int rc;
	struct altmode_pan_ack_msg ack;

	ack.cmd_type = ALTMODE_PAN_ACK;
	ack.port_index = port_index;

	rc = altmode_send_data(amclient, &ack, sizeof(ack));
	if (rc < 0) {
		DP_ERR("failed: %d\n", rc);
		return;
	}

	DP_DEBUG("port=%d\n", port_index);
}

static int dp_altmode_notify(void *priv, void *data, size_t len)
{
	int rc = 0;
	int usb_rc = 0;
	struct dp_altmode_private *altmode =
			(struct dp_altmode_private *) priv;
	u8 port_index, dp_data, orientation, raw_orientation;
	u8 *payload = (u8 *) data;
	u8 pin, hpd_state, hpd_irq;
	bool force_multi_func = altmode->dp_altmode.base.force_multi_func;

	port_index = payload[0];
	orientation = payload[1];
	raw_orientation = orientation;
	dp_data = payload[8];

	pin = dp_data & ALTMODE_CONFIGURE_MASK;
	hpd_state = (dp_data & ALTMODE_HPD_STATE_MASK) >> 6;
	hpd_irq = (dp_data & ALTMODE_HPD_IRQ_MASK) >> 7;

	altmode->svid = USB_SID_DISPLAYPORT;
	altmode->mode = pin;
	altmode->hpd_state = hpd_state;
	altmode->hpd_irq = hpd_irq;

	altmode->dp_altmode.base.hpd_high = !!hpd_state;
	altmode->dp_altmode.base.hpd_irq = !!hpd_irq;
	altmode->dp_altmode.base.multi_func = force_multi_func ? true :
		!(pin == DPAM_HPD_C || pin == DPAM_HPD_E || pin == DPAM_HPD_OUT);

	DP_DEBUG("payload=0x%x\n", dp_data);
	DP_DEBUG("port_index=%d, orientation=%d, pin=%d, hpd_state=%d\n",
			port_index, orientation, pin, hpd_state);
	DP_DEBUG("multi_func=%d, hpd_high=%d, hpd_irq=%d\n",
			altmode->dp_altmode.base.multi_func,
			altmode->dp_altmode.base.hpd_high,
			altmode->dp_altmode.base.hpd_irq);
	DP_DEBUG("connected=%d\n", altmode->connected);
	SDE_EVT32_EXTERNAL(dp_data, port_index, orientation, pin, hpd_state,
			altmode->dp_altmode.base.multi_func,
			altmode->dp_altmode.base.hpd_high,
			altmode->dp_altmode.base.hpd_irq, altmode->connected);

	if (!pin) {
		/* Cable detach */
		if (altmode->connected) {
			altmode->connected = false;
			altmode->dp_altmode.base.alt_mode_cfg_done = false;
			altmode->dp_altmode.base.orientation = ORIENTATION_NONE;
			altmode->orientation = ORIENTATION_NONE;
			rc = dp_altmode_configure_nb7_retimer(altmode, false,
				raw_orientation, pin, hpd_state, hpd_irq);
			if (rc)
				DP_ERR("NB7 retimer configure failed on disconnect: %d\n", rc);
			if (altmode->dp_cb && altmode->dp_cb->disconnect)
				altmode->dp_cb->disconnect(altmode->dev);

			usb_rc = dp_altmode_set_usb_dp_mode(altmode);
			if (usb_rc)
				DP_ERR("failed to clear usb dp mode, rc: %d\n", usb_rc);
			if (!rc)
				rc = usb_rc;
		}
		goto ack;
	}

	/* Configure */
	if (!altmode->connected) {
		altmode->connected = true;
		altmode->dp_altmode.base.alt_mode_cfg_done = true;
		altmode->forced_disconnect = false;
		altmode->lanes = 4;

		if (altmode->dp_altmode.base.multi_func)
			altmode->lanes = 2;

		DP_DEBUG("Connected=%d, lanes=%d\n",altmode->connected,altmode->lanes);

		switch (orientation) {
		case 0:
			orientation = ORIENTATION_CC1;
			break;
		case 1:
			orientation = ORIENTATION_CC2;
			break;
		case 2:
			orientation = ORIENTATION_NONE;
			break;
		default:
			orientation = ORIENTATION_NONE;
			break;
		}

		altmode->dp_altmode.base.orientation = orientation;
		altmode->orientation = orientation;

		rc = dp_altmode_set_usb_dp_mode(altmode);
		if (rc)
			goto ack;

		rc = dp_altmode_configure_nb7_retimer(altmode, true,
			raw_orientation, pin, hpd_state, hpd_irq);
		if (rc)
			goto ack;

		if (altmode->dp_cb && altmode->dp_cb->configure)
			altmode->dp_cb->configure(altmode->dev);
		goto ack;
	}

	/* Attention */
	if (altmode->forced_disconnect)
		goto ack;

	rc = dp_altmode_configure_nb7_retimer(altmode, true,
		raw_orientation, pin, hpd_state, hpd_irq);
	if (rc)
		goto ack;

	if (altmode->dp_cb && altmode->dp_cb->attention)
		altmode->dp_cb->attention(altmode->dev);
ack:
	dp_altmode_send_pan_ack(altmode->amclient, port_index);
	return rc;
}

#if __has_include(<soc/qcom/pmic_glink_altmode.h>)
static int dp_altmode_pmic_notify(void *priv, struct typec_displayport_data data, int orientation)
{
	int rc = 0;
	struct dp_altmode_private *altmode =
			(struct dp_altmode_private *) priv;
	u8 hpd_state, hpd_irq;
	bool force_multi_func = altmode->dp_altmode.base.force_multi_func;

	hpd_state = (data.status & DP_STATUS_HPD_STATE) >> 7;
	hpd_irq = (data.status & DP_STATUS_IRQ_HPD) >> 8;

	altmode->dp_altmode.base.hpd_high = !!hpd_state;
	altmode->dp_altmode.base.hpd_irq = !!hpd_irq;
	/* Multi func is enabled by default here to support USB3 and DP concurrency */
	altmode->dp_altmode.base.multi_func = true;

	DP_DEBUG("orientation=%d, hpd_state=%d\n", orientation, hpd_state);
	DP_DEBUG("multi_func=%d, hpd_high=%d, hpd_irq=%d\n",
			altmode->dp_altmode.base.multi_func,
			altmode->dp_altmode.base.hpd_high,
			altmode->dp_altmode.base.hpd_irq);
	DP_DEBUG("connected=%d\n", altmode->connected);
	SDE_EVT32_EXTERNAL(data, orientation, hpd_state,
			altmode->dp_altmode.base.multi_func,
			altmode->dp_altmode.base.hpd_high,
			altmode->dp_altmode.base.hpd_irq, altmode->connected);

	if (!orientation) {
		/* Cable detach */
		if (altmode->connected) {
			altmode->connected = false;
			altmode->dp_altmode.base.alt_mode_cfg_done = false;
			altmode->dp_altmode.base.orientation = ORIENTATION_NONE;
			if (altmode->dp_cb && altmode->dp_cb->disconnect)
				altmode->dp_cb->disconnect(altmode->dev);
		}
		goto ack;
	}

	/* Configure */
	if (!altmode->connected) {
		altmode->connected = true;
		altmode->dp_altmode.base.alt_mode_cfg_done = true;
		altmode->forced_disconnect = false;
		altmode->lanes = 4;

		if (altmode->dp_altmode.base.multi_func)
			altmode->lanes = 2;

		DP_DEBUG("Connected=%d, lanes=%d\n",altmode->connected,altmode->lanes);

		switch (orientation) {
		case 1:
			orientation = ORIENTATION_CC1;
			break;
		case 2:
			orientation = ORIENTATION_CC2;
			break;
		default:
			orientation = ORIENTATION_NONE;
			break;
		}

		altmode->dp_altmode.base.orientation = orientation;

		DP_DEBUG("orientation = %d\n", altmode->dp_altmode.base.orientation);

		if (altmode->dp_cb && altmode->dp_cb->configure)
			altmode->dp_cb->configure(altmode->dev);
		goto ack;
	}

	/* Attention */
	if (altmode->forced_disconnect)
		goto ack;

	if (altmode->dp_cb && altmode->dp_cb->attention)
		altmode->dp_cb->attention(altmode->dev);
ack:
	return rc;
}
#else
static void dp_altmode_register(void *priv)
{
	struct dp_altmode_private *altmode = priv;
	struct altmode_client_data cd = {
		.callback	= &dp_altmode_notify,
	};

	cd.name = "displayport";
	cd.svid = USB_SID_DISPLAYPORT;
	cd.priv = altmode;

	altmode->amclient = altmode_register_client(altmode->dev, &cd);
	if (IS_ERR_OR_NULL(altmode->amclient))
		DP_ERR("failed to register as client: %ld\n",
				PTR_ERR(altmode->amclient));
	else
		DP_DEBUG("success\n");
}

#endif

static int dp_altmode_simulate_connect(struct dp_hpd *dp_hpd, bool hpd)
{
	struct dp_altmode *dp_altmode;
	struct dp_altmode_private *altmode;

	dp_altmode = container_of(dp_hpd, struct dp_altmode, base);
	altmode = container_of(dp_altmode, struct dp_altmode_private,
			dp_altmode);

	dp_altmode->base.hpd_high = hpd;
	altmode->forced_disconnect = !hpd;
	altmode->dp_altmode.base.alt_mode_cfg_done = hpd;

	if (hpd)
		altmode->dp_cb->configure(altmode->dev);
	else
		altmode->dp_cb->disconnect(altmode->dev);

	return 0;
}

static int dp_altmode_simulate_attention(struct dp_hpd *dp_hpd, int vdo)
{
	struct dp_altmode *dp_altmode;
	struct dp_altmode_private *altmode;
	struct dp_altmode *status;

	dp_altmode = container_of(dp_hpd, struct dp_altmode, base);
	altmode = container_of(dp_altmode, struct dp_altmode_private,
			dp_altmode);

	status = &altmode->dp_altmode;

	status->base.hpd_high  = (vdo & BIT(7)) ? true : false;
	status->base.hpd_irq   = (vdo & BIT(8)) ? true : false;

	if (altmode->dp_cb && altmode->dp_cb->attention)
		altmode->dp_cb->attention(altmode->dev);

	return 0;
}

struct dp_hpd *dp_altmode_get(struct device *dev, struct dp_hpd_cb *cb)
{
	int rc = 0;
	struct dp_altmode_private *altmode;
	struct dp_altmode *dp_altmode;
#if IS_REACHABLE(CONFIG_TYPEC)
	int cache_rc;
#endif

	if (!cb) {
		DP_ERR("invalid cb data\n");
		return ERR_PTR(-EINVAL);
	}

	altmode = kzalloc(sizeof(*altmode), GFP_KERNEL);
	if (!altmode)
		return ERR_PTR(-ENOMEM);

	altmode->dev = dev;
	altmode->dp_cb = cb;

	dp_altmode = &altmode->dp_altmode;
	dp_altmode->base.register_hpd = NULL;
	dp_altmode->base.simulate_connect = dp_altmode_simulate_connect;
	dp_altmode->base.simulate_attention = dp_altmode_simulate_attention;
#if __has_include(<soc/qcom/pmic_glink_altmode.h>)
	rc = pmic_glink_altmode_register_client((void *) dp_altmode_pmic_notify, altmode);
#else
	rc = altmode_register_notifier(dev, dp_altmode_register, altmode);
#endif
	if (rc < 0) {
		DP_ERR("altmode probe notifier registration failed: %d\n", rc);
		goto error;
	}

#if IS_REACHABLE(CONFIG_TYPEC)
	cache_rc = dp_altmode_cache_nb7_retimer(altmode, true);
	if (cache_rc && cache_rc != -EPROBE_DEFER)
		DP_ERR("NB7 Type-C retimer cache failed: %d\n", cache_rc);
	else if (cache_rc)
		DP_DEBUG("NB7 Type-C cache warmup failed: %d\n", cache_rc);
#endif
	DP_DEBUG("success\n");

	return &dp_altmode->base;
error:
	kfree(altmode);
	return ERR_PTR(rc);
}

void dp_altmode_put(struct dp_hpd *dp_hpd)
{
	struct dp_altmode *dp_altmode;
	struct dp_altmode_private *altmode;

	dp_altmode = container_of(dp_hpd, struct dp_altmode, base);
	if (!dp_altmode)
		return;

	altmode = container_of(dp_altmode, struct dp_altmode_private,
			dp_altmode);
#if __has_include(<soc/qcom/altmode_glink.h>)
	altmode_deregister_client(altmode->amclient);
	altmode_deregister_notifier(altmode->dev, altmode);
#endif
	dp_altmode_put_nb7_retimer(altmode);
	kfree(altmode);
}
