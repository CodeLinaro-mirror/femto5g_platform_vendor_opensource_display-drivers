// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/slab.h>

#if IS_ENABLED(CONFIG_QCOM_FSA4480_I2C)
#include <linux/soc/qcom/fsa4480-i2c.h>
#endif
#if IS_ENABLED(CONFIG_QCOM_WCD939X_I2C)
#include <linux/soc/qcom/wcd939x-i2c.h>
#endif

#include "dp_aux_switch.h"
#include "dp_debug.h"
#include "dp_hpd.h"

enum dp_aux_switch_type {
	DP_AUX_SWITCH_BYPASS,
	DP_AUX_SWITCH_FSA4480,
	DP_AUX_SWITCH_WCD939x,
};

struct dp_aux_switch_priv {
	struct device *dev;
	struct device_node *aux_switch_node;
	struct dp_aux_switch aux_switch;

	enum dp_aux_switch_type switch_type;
	bool switch_enable;
	bool aux_switch_ready;
	int switch_orientation;

	int (*switch_register_notifier)(struct notifier_block *nb, struct device_node *node);
	int (*switch_unregister_notifier)(struct notifier_block *nb, struct device_node *node);
};

#if IS_ENABLED(CONFIG_QCOM_FSA4480_I2C)
static int dp_aux_switch_configure_fsa(struct dp_aux_switch *aux_switch,
		bool enable, int orientation)
{
	int rc = 0;
	enum fsa_function event = FSA_USBC_DISPLAYPORT_DISCONNECTED;
	struct dp_aux_switch_priv *priv;

	if (!aux_switch)
		goto end;

	priv = container_of(aux_switch, struct dp_aux_switch_priv, aux_switch);

	if (!priv->aux_switch_node) {
		DP_DEBUG("undefined fsa4480 handle\n");
		rc = -EINVAL;
		goto end;
	}

	if (enable) {
		switch (orientation) {
		case ORIENTATION_CC1:
			event = FSA_USBC_ORIENTATION_CC1;
			break;
		case ORIENTATION_CC2:
			event = FSA_USBC_ORIENTATION_CC2;
			break;
		default:
			DP_ERR("invalid orientation\n");
			rc = -EINVAL;
			goto end;
		}
	}

	DP_DEBUG("enable=%d, orientation=%d, event=%d\n",
			enable, orientation, event);

	rc = fsa4480_switch_event(priv->aux_switch_node, event);
	if (rc)
		DP_ERR("failed to configure fsa4480 i2c device (%d)\n", rc);
end:
	return rc;
}
#endif

#if IS_ENABLED(CONFIG_QCOM_WCD939X_I2C)
static int dp_aux_switch_configure_wcd(struct dp_aux_switch *aux_switch,
		bool enable, int orientation)
{
	int rc = 0;
	enum wcd_usbss_cable_status status = WCD_USBSS_CABLE_DISCONNECT;
	enum wcd_usbss_cable_types event = WCD_USBSS_DP_AUX_CC1;
	struct dp_aux_switch_priv *priv;

	if (!aux_switch)
		goto end;

	priv = container_of(aux_switch, struct dp_aux_switch_priv, aux_switch);

	if (!priv->aux_switch_node) {
		DP_ERR("undefined wcd939x aux_switch handle\n");
		goto end;
	}

	DP_DEBUG("enable=%d, orientation=%d, event=%d\n",
		enable, orientation, event);

	if ((priv->switch_enable == enable) && (priv->switch_orientation == orientation))
		goto end;

	if (enable) {
		status = WCD_USBSS_CABLE_CONNECT;

		switch (orientation) {
		case ORIENTATION_CC1:
			event = WCD_USBSS_DP_AUX_CC1;
			break;
		case ORIENTATION_CC2:
			event = WCD_USBSS_DP_AUX_CC2;
			break;
		default:
			DP_ERR("invalid orientation\n");
			rc = -EINVAL;
			goto end;
		}
	}

	rc = wcd_usbss_switch_update(event, status);
	if (rc) {
		DP_ERR("failed to configure wcd939x i2c device (%d)\n", rc);
	} else {
		priv->switch_enable = enable;
		priv->switch_orientation = orientation;
	}
end:
	return rc;
}
#endif

static int dp_aux_switch_callback(struct notifier_block *self,
		unsigned long event, void *data)
{
	return 0;
}

static int dp_aux_switch_init(struct dp_aux_switch *aux_switch)
{
	int rc = -EINVAL;
	struct dp_aux_switch_priv *priv;
	struct notifier_block nb;
	const u32 max_retries = 50;
	u32 retry;

	if (!aux_switch)
		goto end;

	priv = container_of(aux_switch, struct dp_aux_switch_priv, aux_switch);

	if (priv->aux_switch_ready) {
		rc = 0;
		goto end;
	}

	if (!priv->switch_register_notifier) {
		DP_ERR("switch not registered\n");
		goto end;
	}

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_ENTRY);

	nb.notifier_call = dp_aux_switch_callback;
	nb.priority = 0;

	/*
	 * Iteratively wait for reg notifier which confirms that fsa driver is probed.
	 * Bootup DP with cable connected usecase can hit this scenario.
	 */
	for (retry = 0; retry < max_retries; retry++) {
		rc = priv->switch_register_notifier(&nb, priv->aux_switch_node);
		if (rc == 0) {
			DP_DEBUG("registered notifier successfully\n");
			priv->aux_switch_ready = true;
			break;
		}

		DP_WARN("failed to register notifier retry=%d rc=%d\n", retry, rc);
		msleep(100);
	}

	if (retry == max_retries) {
		DP_ERR("Failed to register fsa notifier\n");
		priv->aux_switch_ready = false;
		goto end;
	}

	if (priv->switch_unregister_notifier)
		priv->switch_unregister_notifier(&nb, priv->aux_switch_node);

	SDE_EVT32_EXTERNAL(SDE_EVTLOG_FUNC_EXIT, rc);
end:
	return rc;
}

struct dp_aux_switch *dp_aux_switch_get(struct device *dev)
{
	struct dp_aux_switch_priv *priv;
	const char *phandle = "qcom,dp-aux-switch";
	int rc = -EINVAL;

	if (!dev)
		goto bail;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		rc = -ENOMEM;
		goto bail;
	}

	priv->dev = dev;
	priv->aux_switch_node = of_parse_phandle(dev->of_node, phandle, 0);
	if (!priv->aux_switch_node) {
		DP_ERR("cannot parse %s handle\n", phandle);
		rc = -ENODEV;
		goto bail;
	}

	if (!strcmp(priv->aux_switch_node->name, "fsa4480"))
		priv->switch_type = DP_AUX_SWITCH_FSA4480;
	else if (!strcmp(priv->aux_switch_node->name, "wcd939x_i2c"))
		priv->switch_type = DP_AUX_SWITCH_WCD939x;
	else
		priv->switch_type = DP_AUX_SWITCH_BYPASS;

	if (priv->switch_type == DP_AUX_SWITCH_BYPASS)
		goto bail;

	DP_DEBUG("DP AUX SWITCH: %s\n", priv->aux_switch_node->name);

#if IS_ENABLED(CONFIG_QCOM_FSA4480_I2C)
	if (priv->switch_type == DP_AUX_SWITCH_FSA4480) {
		priv->aux_switch.configure = dp_aux_switch_configure_fsa;
		priv->switch_register_notifier = fsa4480_reg_notifier;
		priv->switch_unregister_notifier = fsa4480_unreg_notifier;
	}
#endif
#if IS_ENABLED(CONFIG_QCOM_WCD939X_I2C)
	if (priv->switch_type == DP_AUX_SWITCH_WCD939x) {
		priv->aux_switch.configure = dp_aux_switch_configure_wcd;
		priv->switch_register_notifier = wcd_usbss_reg_notifier;
		priv->switch_unregister_notifier = wcd_usbss_unreg_notifier;
	}
#endif

	priv->switch_enable = false;
	priv->switch_orientation = -1;

	priv->aux_switch.init = dp_aux_switch_init;

	return &priv->aux_switch;
bail:
	return ERR_PTR(rc);
}

void dp_aux_switch_put(struct dp_aux_switch *aux_switch)
{
	struct dp_aux_switch_priv *priv;

	if (!aux_switch)
		return;

	priv = container_of(aux_switch, struct dp_aux_switch_priv, aux_switch);
}
