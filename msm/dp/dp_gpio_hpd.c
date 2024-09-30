// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/sde_io_util.h>
#include <linux/of_gpio.h>
#include "dp_gpio_hpd.h"
#include "dp_debug.h"

struct dp_gpio_hpd_private {
	struct device *dev;
	struct dp_hpd base;
	struct dp_parser *parser;
	struct dss_gpio gpio_cfg;
	struct workqueue_struct *wq;
	struct work_struct work;
	struct workqueue_struct *connect_wq;
	struct work_struct connect;
	struct work_struct disconnect;
	struct work_struct attention;
	struct dp_hpd_cb *cb;
	int irq;
	int edge;
};

static void dp_gpio_hpd_attention_work(struct work_struct *work)
{
	struct dp_gpio_hpd_private *gpio_hpd = container_of(work,
				struct dp_gpio_hpd_private, attention);

	if (!gpio_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	gpio_hpd->base.hpd_irq = true;

	if (gpio_hpd->cb && gpio_hpd->cb->attention)
		gpio_hpd->cb->attention(gpio_hpd->dev);
}

static void dp_gpio_hpd_connect_work(struct work_struct *work)
{
	struct dp_gpio_hpd_private *gpio_hpd = container_of(work,
				struct dp_gpio_hpd_private, connect);

	if (!gpio_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	gpio_hpd->base.hpd_high = true;
	gpio_hpd->base.alt_mode_cfg_done = true;
	gpio_hpd->base.hpd_irq = false;

	if (gpio_hpd->cb && gpio_hpd->cb->configure)
		gpio_hpd->cb->configure(gpio_hpd->dev);
}

static void dp_gpio_hpd_disconnect_work(struct work_struct *work)
{
	struct dp_gpio_hpd_private *gpio_hpd = container_of(work,
				struct dp_gpio_hpd_private, disconnect);

	if (!gpio_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	gpio_hpd->base.hpd_high = false;
	gpio_hpd->base.alt_mode_cfg_done = false;
	gpio_hpd->base.hpd_irq = false;

	if (gpio_hpd->cb && gpio_hpd->cb->disconnect)
		gpio_hpd->cb->disconnect(gpio_hpd->dev);
}

static int dp_gpio_hpd_connect(struct dp_gpio_hpd_private *gpio_hpd, bool hpd)
{
	int rc = 0;

	if (!gpio_hpd) {
		DP_ERR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	if (!gpio_hpd->cb ||
		!gpio_hpd->cb->configure ||
		!gpio_hpd->cb->disconnect) {
		DP_ERR("DP%d invalid cb\n", gpio_hpd->parser->cell_idx);
		rc = -EINVAL;
		goto error;
	}

	DP_DEBUG("%s hpd=%d\n", gpio_hpd->gpio_cfg.gpio_name, hpd);
	if (hpd) {
		rc = queue_work(gpio_hpd->connect_wq,
				&gpio_hpd->connect);
		if (!rc)
			DP_DEBUG("DP%d connect not queued\n",
					gpio_hpd->parser->cell_idx);
	} else {
		gpio_hpd->base.hpd_high = false;
		gpio_hpd->base.alt_mode_cfg_done = false;
		gpio_hpd->base.hpd_irq = false;

		rc = queue_work(gpio_hpd->connect_wq,
				&gpio_hpd->disconnect);
		if (!rc)
			DP_DEBUG("DP%d disconnect not queued\n",
					gpio_hpd->parser->cell_idx);
	}

error:
	return rc;
}

static int dp_gpio_hpd_attention(struct dp_gpio_hpd_private *gpio_hpd)
{
	int rc = 0;

	if (!gpio_hpd) {
		DP_ERR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	if (gpio_hpd->cb && gpio_hpd->cb->attention) {
		rc = queue_work(gpio_hpd->connect_wq, &gpio_hpd->attention);
		if (!rc)
			DP_DEBUG("DP%d attention not queued\n",
					gpio_hpd->parser->cell_idx);
	}

error:
	return rc;
}

static irqreturn_t dp_gpio_isr(int unused, void *data)
{
	struct dp_gpio_hpd_private *gpio_hpd = data;

	if (!gpio_hpd)
		return IRQ_NONE;

	queue_work(gpio_hpd->wq, &gpio_hpd->work);

	return IRQ_HANDLED;
}

static inline
int dp_gpio_set_edge(struct dp_gpio_hpd_private *gpio_hpd, int edge)
{
	const int retry_count = 10;
	int orig_edge = edge;
	int ret, i;
	bool hpd;

	if (gpio_hpd->edge == edge)
		return 0;

	for (i = 0; i < retry_count; i++) {
		if (gpio_hpd->edge)
			devm_free_irq(gpio_hpd->dev,
					gpio_hpd->irq, gpio_hpd);

		ret = devm_request_threaded_irq(gpio_hpd->dev,
				gpio_hpd->irq, NULL,
				dp_gpio_isr,
				edge | IRQF_ONESHOT,
				"dp-gpio-intp", gpio_hpd);

		if (WARN_ON(ret)) {
			DP_ERR("failed to request irq\n");
			break;
		}

		gpio_hpd->edge = edge;

		/* check if hpd is changed */
		hpd = gpio_get_value_cansleep(gpio_hpd->gpio_cfg.gpio);

		if (edge == IRQF_TRIGGER_FALLING) {
			if (!hpd) {
				DP_INFO("hpd bounce to low\n");
				edge = IRQF_TRIGGER_RISING;
				continue;
			}
			DP_INFO("%s switch to falling edge\n",
					gpio_hpd->gpio_cfg.gpio_name);
		} else {
			if (hpd) {
				DP_INFO("hpd bounce to high\n");
				edge = IRQF_TRIGGER_FALLING;
				continue;
			}
			DP_INFO("%s switch to rising edge\n",
					gpio_hpd->gpio_cfg.gpio_name);
		}

		break;
	}

	return (orig_edge != edge);
}

static void dp_gpio_hpd_work(struct work_struct *work)
{
	struct dp_gpio_hpd_private *gpio_hpd = container_of(work,
		struct dp_gpio_hpd_private, work);
	u32 const disconnect_timeout_retry = 50;
	int ret = 0;
	int i;
	bool hpd;

	switch (gpio_hpd->edge) {
	case IRQF_TRIGGER_NONE:
		hpd = gpio_get_value_cansleep(gpio_hpd->gpio_cfg.gpio);
		if (hpd) {
			ret = dp_gpio_set_edge(gpio_hpd, IRQF_TRIGGER_FALLING);
			if (ret) {
				DP_INFO("found hpd high glitch %d\n", ret);
				break;
			}
		} else {
			ret = dp_gpio_set_edge(gpio_hpd, IRQF_TRIGGER_RISING);
			if (ret)
				DP_INFO("found hpd low glitch %d\n", ret);
			else
				break;
		}
		ret = dp_gpio_hpd_connect(gpio_hpd, true);
		break;
	case IRQF_TRIGGER_RISING:
		if (gpio_hpd->base.hpd_high)
			break;

		/*
		 * According to the DP spec, HPD high event can be confirmed
		 * only after the HPD line has een asserted continuously for
		 * more than 100ms
		 */
		usleep_range(99000, 100000);

		hpd = gpio_get_value_cansleep(gpio_hpd->gpio_cfg.gpio);

		/* if HPD is low, ignore this event */
		if (!hpd) {
			DP_INFO("ignore false hpd high\n");
			break;
		}

		ret = dp_gpio_set_edge(gpio_hpd, IRQF_TRIGGER_FALLING);
		if (ret) {
			DP_ERR("failed to set falling hpd edge %d\n", ret);
			break;
		}

		ret = dp_gpio_hpd_connect(gpio_hpd, true);
		break;
	case IRQF_TRIGGER_FALLING:
		if (!gpio_hpd->base.hpd_high)
			break;

		/* In DP 1.2 spec, 100msec is recommended for the detection
		 * of HPD connect event. Here we'll poll HPD status for
		 * 50x2ms = 100ms and if HPD is always low, we know DP is
		 * disconnected. If HPD is high, HPD_IRQ will be handled
		 */
		for (i = 0; i < disconnect_timeout_retry; i++) {
			hpd = gpio_get_value_cansleep(gpio_hpd->gpio_cfg.gpio);
			if (hpd) {
				dp_gpio_hpd_attention(gpio_hpd);
				return;
			}
			usleep_range(2000, 2100);
		}

		ret = dp_gpio_set_edge(gpio_hpd, IRQF_TRIGGER_RISING);
		if (ret) {
			DP_ERR("failed to set falling hpd edge %d\n", ret);
			break;
		}

		ret = dp_gpio_hpd_connect(gpio_hpd, false);
		break;
	default:
		break;
	}

	if (ret < 0)
		DP_ERR("DP%d Cannot claim IRQ dp-gpio-intp\n",
				gpio_hpd->parser->cell_idx);
}

static int dp_gpio_hpd_simulate_connect(struct dp_hpd *dp_hpd, bool hpd)
{
	int rc = 0;
	struct dp_gpio_hpd_private *gpio_hpd;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	gpio_hpd = container_of(dp_hpd, struct dp_gpio_hpd_private, base);

	dp_gpio_hpd_connect(gpio_hpd, hpd);
error:
	return rc;
}

static int dp_gpio_hpd_simulate_attention(struct dp_hpd *dp_hpd, int vdo)
{
	int rc = 0;
	struct dp_gpio_hpd_private *gpio_hpd;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	gpio_hpd = container_of(dp_hpd, struct dp_gpio_hpd_private, base);

	dp_gpio_hpd_attention(gpio_hpd);
error:
	return rc;
}

static void dp_gpio_hpd_host_init(struct dp_hpd *dp_hpd,
		struct dp_catalog_hpd *catalog)
{
	struct dp_gpio_hpd_private *gpio_hpd;
	bool hpd;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	gpio_hpd = container_of(dp_hpd, struct dp_gpio_hpd_private, base);

	hpd = gpio_get_value_cansleep(gpio_hpd->gpio_cfg.gpio);

	if (hpd != gpio_hpd->base.hpd_high) {
		DP_INFO("DP%d missed HPD %s edge?\n",
				gpio_hpd->parser->cell_idx,
				hpd ? "raising" : "falling");
		queue_work(gpio_hpd->wq, &gpio_hpd->work);
	}
}

static void dp_gpio_hpd_host_deinit(struct dp_hpd *dp_hpd,
		struct dp_catalog_hpd *catalog)
{
	struct dp_gpio_hpd_private *gpio_hpd;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	gpio_hpd = container_of(dp_hpd, struct dp_gpio_hpd_private, base);

	// Do nothing
}

int dp_gpio_hpd_register(struct dp_hpd *dp_hpd)
{
	struct dp_gpio_hpd_private *gpio_hpd;

	if (!dp_hpd)
		return -EINVAL;

	gpio_hpd = container_of(dp_hpd, struct dp_gpio_hpd_private, base);

	queue_work(gpio_hpd->wq, &gpio_hpd->work);

	return 0;
}

struct dp_hpd *dp_gpio_hpd_get(struct device *dev,
	struct dp_parser *parser, struct dp_hpd_cb *cb)
{
	int rc = 0;
	const char *hpd_gpio_name = "qcom,dp-hpd-gpio";
	struct dp_gpio_hpd_private *gpio_hpd;
	struct dp_pinctrl pinctrl = {0};
	unsigned int gpio;

	if (!dev || !cb) {
		DP_ERR("invalid device\n");
		rc = -EINVAL;
		goto error;
	}

	gpio = of_get_named_gpio(dev->of_node, hpd_gpio_name, 0);
	if (!gpio_is_valid(gpio)) {
		DP_DEBUG("%s gpio not specified\n", hpd_gpio_name);
		rc = -EINVAL;
		goto error;
	}

	gpio_hpd = devm_kzalloc(dev, sizeof(*gpio_hpd), GFP_KERNEL);
	if (!gpio_hpd) {
		rc = -ENOMEM;
		goto error;
	}

	pinctrl.pin = devm_pinctrl_get(dev);
	if (!IS_ERR_OR_NULL(pinctrl.pin)) {
		pinctrl.state_hpd_active = pinctrl_lookup_state(pinctrl.pin,
						"mdss_dp_hpd_active");
		if (!IS_ERR_OR_NULL(pinctrl.state_hpd_active)) {
			rc = pinctrl_select_state(pinctrl.pin,
					pinctrl.state_hpd_active);
			if (rc) {
				DP_ERR("DP%d failed to set hpd active state\n",
						gpio_hpd->parser->cell_idx);
				goto gpio_error;
			}
		}
	}

	gpio_hpd->gpio_cfg.gpio = gpio;
	strlcpy(gpio_hpd->gpio_cfg.gpio_name, hpd_gpio_name,
		sizeof(gpio_hpd->gpio_cfg.gpio_name));
	gpio_hpd->gpio_cfg.value = 0;

	rc = gpio_request(gpio_hpd->gpio_cfg.gpio,
		gpio_hpd->gpio_cfg.gpio_name);
	if (rc) {
		DP_ERR("%s: failed to request gpio\n", hpd_gpio_name);
		goto gpio_error;
	}
	gpio_direction_input(gpio_hpd->gpio_cfg.gpio);

	gpio_hpd->dev = dev;
	gpio_hpd->parser = parser;
	gpio_hpd->cb = cb;
	gpio_hpd->irq = gpio_to_irq(gpio_hpd->gpio_cfg.gpio);

	gpio_hpd->wq = create_singlethread_workqueue("dp-gpio-hpd-wq");
	if (!gpio_hpd->wq) {
		DP_ERR("failed to create work\n");
		goto gpio_error;
	}

	INIT_WORK(&gpio_hpd->work, dp_gpio_hpd_work);

	gpio_hpd->connect_wq = create_singlethread_workqueue("dp-gpio-conn-eq");
	if (!gpio_hpd->connect_wq) {
		DP_ERR("DP%d Error creating connect_wq\n", gpio_hpd->parser->cell_idx);
		goto gpio_error;
	}

	INIT_WORK(&gpio_hpd->connect, dp_gpio_hpd_connect_work);
	INIT_WORK(&gpio_hpd->disconnect, dp_gpio_hpd_disconnect_work);
	INIT_WORK(&gpio_hpd->attention, dp_gpio_hpd_attention_work);

	gpio_hpd->base.host_init = dp_gpio_hpd_host_init;
	gpio_hpd->base.host_deinit = dp_gpio_hpd_host_deinit;
	gpio_hpd->base.simulate_connect = dp_gpio_hpd_simulate_connect;
	gpio_hpd->base.simulate_attention = dp_gpio_hpd_simulate_attention;
	gpio_hpd->base.register_hpd = dp_gpio_hpd_register;

	return &gpio_hpd->base;

gpio_error:
	devm_kfree(dev, gpio_hpd);
error:
	return ERR_PTR(rc);
}

void dp_gpio_hpd_put(struct dp_hpd *dp_hpd)
{
	struct dp_gpio_hpd_private *gpio_hpd;

	if (!dp_hpd)
		return;

	gpio_hpd = container_of(dp_hpd, struct dp_gpio_hpd_private, base);

	gpio_free(gpio_hpd->gpio_cfg.gpio);
	devm_kfree(gpio_hpd->dev, gpio_hpd);
}
