// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
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
#include <linux/timer.h>
#include "dp_lphw_hpd.h"
#include "dp_debug.h"

/*
 * According to the DP spec, HPD high event can be confirmed
 * only after the HPD line has been asserted continuously for
 * more than 2ms, suggested 100ms. HPD hardware timing is set
 * to 2ms. Set GPIO denoising to 20ms for checking after HPD
 * hardware decision is done.
 */
#define DENOISE_RAISE_EDGE_INTERVAL_MS	20
/*
 * In DP 1.2 spec, >2msec is recommended for the detection
 * of HPD disconnect event. Here we'll poll HPD status for
 * 5ms and if HPD is always low, we know DP is
 * disconnected. If HPD is high, could be HPD_IRQ, to be
 * ignored, since GPIO can't get the accurate timing.
 */
#define DENOISE_FALL_EDGE_INTERVAL_MS	5

enum {
	GPIO_CHECK_STATE_INIT,
	GPIO_CHECK_STATE_IDLE,
	GPIO_CHECK_STATE_RAISING,
	GPIO_CHECK_STATE_FALLING,
	GPIO_CHECK_STATE_MAX,
};

/*
 * struct dp_lphw_hpd_private - LPHW HPD driver internal context
 * @dev                     : device handle
 * @base                    : DP HPD base data structure
 * @parser                  : DP devicetree parser data structure
 * @catalog                 : DP hardware catalog
 * @gpio_cfg                : HPD pin GPIO context
 * @connect_wq              : work queue for hotplug events
 * @connect                 : work for HPD connected event
 * @disconnect              : work for HPD disconnect event
 * @attention               : work for IRQ_HPD event
 * @secondary_hpd           : work for secondary (GPIO) HPD event
 * @gpio_work               : work for GPIO interrupt handling
 * @cb:                     : DP display callback
 * @irq                     : GPIO IRQ
 * @hpd                     : previous HW HPD status
 * @configured              : is HW HPD configured
 * @gpio_timer              : GPIO debounce timer
 * @last_gpio_hpd           : previous GPIO HPD status
 * @gpio_check_state        : GPIO debouncing state machine state
 * @gpio_check_start_time   : GPIO debouncing start time
 */
struct dp_lphw_hpd_private {
	struct device *dev;
	struct dp_hpd base;
	struct dp_parser *parser;
	struct dp_catalog_hpd *catalog;
	struct dss_gpio gpio_cfg;
	struct workqueue_struct *connect_wq;
	struct work_struct connect;
	struct work_struct disconnect;
	struct work_struct attention;
	struct work_struct secondary_hpd;
	struct work_struct gpio_work;
	struct dp_hpd_cb *cb;
	int irq;
	bool hpd;
	bool configured;
	struct timer_list gpio_timer;
	bool last_gpio_hpd;
	u32 gpio_check_state;
	ktime_t gpio_check_start_time;
};

static void dp_lphw_hpd_attention(struct work_struct *work)
{
	struct dp_lphw_hpd_private *lphw_hpd = container_of(work,
				struct dp_lphw_hpd_private, attention);

	if (!lphw_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	lphw_hpd->base.hpd_irq = true;

	if (lphw_hpd->cb && lphw_hpd->cb->attention)
		lphw_hpd->cb->attention(lphw_hpd->dev);
}

static void dp_lphw_hpd_connect(struct work_struct *work)
{
	struct dp_lphw_hpd_private *lphw_hpd = container_of(work,
				struct dp_lphw_hpd_private, connect);

	if (!lphw_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	lphw_hpd->base.hpd_high = true;
	lphw_hpd->base.alt_mode_cfg_done = true;
	lphw_hpd->base.hpd_irq = false;

	if (lphw_hpd->cb && lphw_hpd->cb->configure)
		lphw_hpd->cb->configure(lphw_hpd->dev);
}

static void dp_lphw_hpd_disconnect(struct work_struct *work)
{
	struct dp_lphw_hpd_private *lphw_hpd = container_of(work,
				struct dp_lphw_hpd_private, disconnect);

	if (!lphw_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	lphw_hpd->base.hpd_high = false;
	lphw_hpd->base.alt_mode_cfg_done = false;
	lphw_hpd->base.hpd_irq = false;

	if (lphw_hpd->cb && lphw_hpd->cb->disconnect)
		lphw_hpd->cb->disconnect(lphw_hpd->dev);
}

static void dp_lphw_hpd_secondary_hpd(struct work_struct *work)
{
	struct dp_lphw_hpd_private *lphw_hpd = container_of(work,
				struct dp_lphw_hpd_private, secondary_hpd);

	if (!lphw_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	if (lphw_hpd->cb && lphw_hpd->cb->secondary_hpd)
		lphw_hpd->cb->secondary_hpd(lphw_hpd->dev, lphw_hpd->last_gpio_hpd);
}

static irqreturn_t dp_lphw_hpd_tlmm_isr(int unused, void *data)
{
	struct dp_lphw_hpd_private *lphw_hpd = data;

	if (!lphw_hpd)
		return IRQ_NONE;

	/* Wake up the handler, setup debounce timer */
	queue_work(system_highpri_wq, &lphw_hpd->gpio_work);
	DP_DEBUG("DP%d GPIO isr\n", lphw_hpd->parser->cell_idx);

	return IRQ_HANDLED;
}

static void dp_lphw_hpd_tlmm_work(struct work_struct *work)
{
	struct dp_lphw_hpd_private *lphw_hpd = container_of(work,
		struct dp_lphw_hpd_private, gpio_work);
	bool hpd;
	struct irq_data *irqd;
	int rc;
	ktime_t current_time;

	current_time = ktime_get();

	hpd = gpio_get_value_cansleep(lphw_hpd->gpio_cfg.gpio);
	DP_INFO("DP%d GPIO hpd %d->%d  state=%d\n",
			lphw_hpd->parser->cell_idx,
			lphw_hpd->last_gpio_hpd, hpd,
			lphw_hpd->gpio_check_state);

	irqd = irq_get_irq_data(lphw_hpd->irq);
	if (irqd && irqd->chip && irqd->chip->irq_set_type) {
		/* Hook up next opposite edge interrupt */
		if (hpd) {
			/* High level, falling edge */
			DP_DEBUG("DP%d GPIO hook falling edge IRQ hpd=%d state=%d\n",
					lphw_hpd->parser->cell_idx, hpd,
					lphw_hpd->gpio_check_state);
			rc = irqd->chip->irq_set_type(irqd, IRQ_TYPE_EDGE_FALLING);
		} else {
			/* Low level, raising edge */
			DP_DEBUG("DP%d GPIO hook raising edge IRQ hpd=%d state=%d\n",
					lphw_hpd->parser->cell_idx, hpd,
					lphw_hpd->gpio_check_state);
			rc = irqd->chip->irq_set_type(irqd, IRQ_TYPE_EDGE_RISING);
		}
		if (rc)
			DP_ERR("DP%d GPIO failed to flip IRQ edge: %d\n",
					lphw_hpd->parser->cell_idx, rc);
	}

repeat:
	switch (lphw_hpd->gpio_check_state) {
	case GPIO_CHECK_STATE_INIT:
		/* Should not come here */
		DP_DEBUG("DP%d GPIO get interrupt before init?\n",
				lphw_hpd->parser->cell_idx);
		break;

	case GPIO_CHECK_STATE_IDLE:
	default:
		/* Stable status, set debouncing mode and timer */
		if (hpd) {
			/* Raising edge */
			lphw_hpd->gpio_check_start_time = current_time;
			lphw_hpd->gpio_check_state = GPIO_CHECK_STATE_RAISING;
			mod_timer(&lphw_hpd->gpio_timer, jiffies +
				msecs_to_jiffies(lphw_hpd->parser->gpio_hpd_high_debounce_ms));
			DP_DEBUG("DP%d GPIO raising edge debounce started %dms\n",
					lphw_hpd->parser->cell_idx,
					lphw_hpd->parser->gpio_hpd_high_debounce_ms);
		} else {
			/* Falling edge */
			lphw_hpd->gpio_check_start_time = current_time;
			lphw_hpd->gpio_check_state = GPIO_CHECK_STATE_FALLING;
			mod_timer(&lphw_hpd->gpio_timer, jiffies +
				msecs_to_jiffies(lphw_hpd->parser->gpio_hpd_low_debounce_ms));
			DP_DEBUG("DP%d GPIO falling edge debounce started %dms\n",
					lphw_hpd->parser->cell_idx,
					lphw_hpd->parser->gpio_hpd_low_debounce_ms);
		}
		break;

	case GPIO_CHECK_STATE_RAISING:
		if (hpd) {
			DP_DEBUG("DP%d GPIO raising edge for raising debounce, ignored\n",
					lphw_hpd->parser->cell_idx);
			break;
		}
		/*
		 * Falling edge during raising deboucing,
		 * cancel timer and set to falling edge debouncing
		 */
		del_timer(&lphw_hpd->gpio_timer);
		lphw_hpd->gpio_check_state = GPIO_CHECK_STATE_IDLE;
		goto repeat;

	case GPIO_CHECK_STATE_FALLING:
		if (!hpd) {
			DP_DEBUG("DP%d GPIO falling edge for falling debounce, ignored\n",
					lphw_hpd->parser->cell_idx);
			break;
		}
		/*
		 * Raising edge during falling deboucing,
		 * cancel timer and set to raising edge debouncing
		 */
		del_timer(&lphw_hpd->gpio_timer);
		lphw_hpd->gpio_check_state = GPIO_CHECK_STATE_IDLE;
		goto repeat;
	}
}

static void dp_lphw_hpd_gpio_timer_callback(struct timer_list *t)
{
	struct dp_lphw_hpd_private *lphw_hpd =
			from_timer(lphw_hpd, t, gpio_timer);
	bool hpd;
	ktime_t current_time;
	s64 time_diff;
	int rc;

	/* Peek the GPIO status */
	hpd = gpio_get_value_cansleep(lphw_hpd->gpio_cfg.gpio);
	current_time = ktime_get();
	time_diff = ktime_to_ms(ktime_sub(current_time,
			lphw_hpd->gpio_check_start_time));

	switch (lphw_hpd->gpio_check_state) {
	case GPIO_CHECK_STATE_INIT:
	case GPIO_CHECK_STATE_IDLE:
	default:
		/* Should not come here */
		DP_DEBUG("DP%d GPIO HPD timer mis-fire? state %d\n",
				lphw_hpd->parser->cell_idx,
				lphw_hpd->gpio_check_state);
		break;

	case GPIO_CHECK_STATE_RAISING:
		if (!hpd) {
			/*
			 * There must be noise on line we missed.
			 * Hook up the interrupt to the correct edge again.
			 * Setup the timer for debounce accordingly.
			 */
			DP_DEBUG("DP%d GPIO HPD HIGH debounce %dms meet hpd=LOW\n",
					lphw_hpd->parser->cell_idx,
					(int)time_diff);
			lphw_hpd->gpio_check_state = GPIO_CHECK_STATE_IDLE;
			queue_work(system_highpri_wq, &lphw_hpd->gpio_work);
		} else if (time_diff >= lphw_hpd->parser->gpio_hpd_high_debounce_ms
				- jiffies_to_msecs(1)) {
			DP_INFO("DP%d GPIO HPD goes HIGH %dms\n",
					lphw_hpd->parser->cell_idx, (int)time_diff);
			lphw_hpd->last_gpio_hpd = hpd;
			if (lphw_hpd->base.sec_hpd_high == hpd) {
				DP_INFO("DP%d GPIO HPD goes HIGH->HIGH %dms, ignore glitch\n",
						lphw_hpd->parser->cell_idx, (int)time_diff);
				break;
			}
			lphw_hpd->base.sec_hpd_high = true;
			lphw_hpd->gpio_check_state = GPIO_CHECK_STATE_IDLE;

			if (lphw_hpd->cb->secondary_hpd) {
				rc = queue_work(lphw_hpd->connect_wq,
						&lphw_hpd->secondary_hpd);
				if (!rc)
					DP_DEBUG("DP%d secondary connect not queued\n",
							lphw_hpd->parser->cell_idx);
			}
			if (!lphw_hpd->configured) {
				lphw_hpd->hpd = true;
				rc = queue_work(lphw_hpd->connect_wq, &lphw_hpd->connect);
				if (!rc)
					DP_DEBUG("DP%d connect not queued\n",
							lphw_hpd->parser->cell_idx);
			}
		} else {
			/* Should not come here */
			DP_INFO("DP%d GPIO HPD HIGH debounce not reached %dms hpd %d\n",
					lphw_hpd->parser->cell_idx,
					(int)time_diff, hpd);
		}
		break;

	case GPIO_CHECK_STATE_FALLING:
		if (hpd) {
			/*
			 * There must be noise on line we missed.
			 * Hook up the interrupt to the correct edge again.
			 * Setup the timer for debounce accordingly.
			 */
			DP_DEBUG("DP%d GPIO HPD LOW debounce %dms meet hpd=HIGH\n",
					lphw_hpd->parser->cell_idx,
					(int)time_diff);
			lphw_hpd->gpio_check_state = GPIO_CHECK_STATE_IDLE;
			queue_work(system_highpri_wq, &lphw_hpd->gpio_work);
		} else if (time_diff >= lphw_hpd->parser->gpio_hpd_low_debounce_ms
				- jiffies_to_msecs(1)) {
			DP_INFO("DP%d GPIO HPD goes LOW %dms\n",
					lphw_hpd->parser->cell_idx,
					(int)time_diff);
			lphw_hpd->last_gpio_hpd = hpd;
			if (lphw_hpd->base.sec_hpd_high == hpd) {
				DP_INFO("DP%d GPIO HPD goes LOW->LOW %dms, ignore glitch\n",
						lphw_hpd->parser->cell_idx,
						(int)time_diff);
				break;
			}
			lphw_hpd->base.sec_hpd_high = false;
			lphw_hpd->gpio_check_state = GPIO_CHECK_STATE_IDLE;

			if (lphw_hpd->cb->secondary_hpd) {
				rc = queue_work(lphw_hpd->connect_wq,
						&lphw_hpd->secondary_hpd);
				if (!rc)
					DP_DEBUG("DP%d secondary disconnect not queued\n",
							lphw_hpd->parser->cell_idx);
			}
			if (!lphw_hpd->configured) {
				lphw_hpd->hpd = false;
				rc = queue_work(lphw_hpd->connect_wq, &lphw_hpd->disconnect);
				if (!rc)
					DP_DEBUG("DP%d disconnect not queued\n",
							lphw_hpd->parser->cell_idx);
			}
		} else {
			/* Should not come here */
			DP_DEBUG("DP%d GPIO HPD LOW debounce not reached %dms hpd %d\n",
					lphw_hpd->parser->cell_idx,
					(int)time_diff, hpd);
		}
		break;
	}
}

static void dp_lphw_hpd_host_init(struct dp_hpd *dp_hpd,
		struct dp_catalog_hpd *catalog)
{
	struct dp_lphw_hpd_private *lphw_hpd;
	bool hpd;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	hpd = gpio_get_value_cansleep(lphw_hpd->gpio_cfg.gpio);
	DP_INFO("DP%d lphw_hpd state = %d, new hpd state = %d\n",
			lphw_hpd->parser->cell_idx, lphw_hpd->hpd, hpd);
	if (lphw_hpd->hpd != hpd)
		DP_INFO("DP%d HPD has changed, lphw_hpd state = %d, gpio hpd state = %d\n",
			lphw_hpd->parser->cell_idx, lphw_hpd->hpd, hpd);

	/*
	 * When we init the HPD hardware, the hardware state machine starts from
	 * disconnected status.
	 * Update the software cache HPD status to previous status, that will be
	 * reset to low when going to suspend mode, since the sink device will
	 * be powered down during suspension time.
	 * The change of the sw HPD status to the new hardware status will be
	 * identified as hotplug event, otherwise redundent interrupt shall be
	 * ignored.
	 */
	DP_INFO("DP%d Init HPD state machine, sw status starts from %d\n",
			lphw_hpd->parser->cell_idx, lphw_hpd->base.hpd_high);
	lphw_hpd->hpd = lphw_hpd->base.hpd_high;
	lphw_hpd->catalog->config_hpd(lphw_hpd->catalog, true);

	lphw_hpd->configured = true;
}

static void dp_lphw_hpd_host_deinit(struct dp_hpd *dp_hpd,
		struct dp_catalog_hpd *catalog)
{
	struct dp_lphw_hpd_private *lphw_hpd;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	lphw_hpd->catalog->config_hpd(lphw_hpd->catalog, false);
	lphw_hpd->configured = false;

	DP_DEBUG("DP%d deinit HPD state machine, sw_hpd=%d\n",
			lphw_hpd->parser->cell_idx,
			lphw_hpd->base.hpd_high);
}

static void dp_lphw_hpd_isr(struct dp_hpd *dp_hpd)
{
	struct dp_lphw_hpd_private *lphw_hpd;
	u32 isr = 0, status;
	int rc = 0;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	isr = lphw_hpd->catalog->get_interrupt(lphw_hpd->catalog);
	/* Skip check if no interrupt */
	if (!(isr & DP_HPD_INT_STATUS_MASK))
		return;
	status = (isr >> 29) & 0x7;

	/* Check for uncommon cases */
	switch (status) {
	case DP_HPD_STATUS_DISCONNECTED:
		if (!(isr & DP_HPD_UNPLUG_INT_STATUS))
			DP_INFO("DP%d disconnect but no interrupt, hpd isr state: 0x%x\n",
					lphw_hpd->parser->cell_idx, isr);
		if (isr & (DP_HPD_PLUG_INT_STATUS | DP_HPD_REPLUG_INT_STATUS))
			DP_INFO("DP%d missed connect interrupt, hpd isr state: 0x%x\n",
					lphw_hpd->parser->cell_idx, isr);
		if (isr & DP_IRQ_HPD_INT_STATUS)
			DP_INFO("DP%d missed hpd_irq interrupt, hpd isr state: 0x%x\n",
					lphw_hpd->parser->cell_idx, isr);
		break;
	case DP_HPD_STATUS_CONNECT_PENDING:
		DP_INFO("DP%d connect pending, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);
		break;
	case DP_HPD_STATUS_CONNECTED:
		if (!(isr & (DP_HPD_PLUG_INT_STATUS | DP_HPD_REPLUG_INT_STATUS
			| DP_IRQ_HPD_INT_STATUS)) && !lphw_hpd->hpd)
			DP_INFO("DP%d connect but no interrupt, hpd isr state: 0x%x\n",
					lphw_hpd->parser->cell_idx, isr);
		if (isr & DP_HPD_UNPLUG_INT_STATUS) {
			if (lphw_hpd->base.hpd_high) {
				DP_INFO("DP%d missed disconnect interrupt, hpd isr state: 0x%x\n",
						lphw_hpd->parser->cell_idx, isr);
				lphw_hpd->hpd = false;
				lphw_hpd->base.hpd_high = false;
				lphw_hpd->base.alt_mode_cfg_done = false;
				lphw_hpd->base.hpd_irq = false;

				rc = queue_work(lphw_hpd->connect_wq,
						&lphw_hpd->disconnect);
				if (!rc)
					DP_DEBUG("DP%d disconnect not queued\n",
							lphw_hpd->parser->cell_idx);
			} else {
				DP_INFO("DP%d missed multiple interrupts, hpd isr state: 0x%x\n",
						lphw_hpd->parser->cell_idx, isr);
			}
		}
		break;
	case DP_HPD_STATUS_HPD_IO_GLITCH_COUNT:
		DP_INFO("DP%d hpd io glich counting, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);
		break;
	case DP_HPD_STATUS_IRQ_HPD_PULSE_COUNT:
		DP_INFO("DP%d hpd irq counting, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);
		break;
	case DP_HPD_STATUS_HPD_REPLUG_COUNT:
		DP_INFO("DP%d hpd replug counting, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);
		break;
	default:
		break;
	}

	/* Process based on most updated HPD status, instead of interrupt */
	if (status == DP_HPD_STATUS_DISCONNECTED) { /* disconnect status */

		DP_INFO("DP%d disconnect interrupt, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);

		if (lphw_hpd->base.hpd_high) {
			lphw_hpd->hpd = false;
			lphw_hpd->base.hpd_high = false;
			lphw_hpd->base.alt_mode_cfg_done = false;
			lphw_hpd->base.hpd_irq = false;

			rc = queue_work(lphw_hpd->connect_wq,
					&lphw_hpd->disconnect);
			if (!rc)
				DP_DEBUG("DP%d disconnect not queued\n",
						lphw_hpd->parser->cell_idx);
		} else {
			DP_INFO("DP%d already disconnected\n", lphw_hpd->parser->cell_idx);
		}

	} else if ((status == DP_HPD_STATUS_CONNECTED) &&
			!(isr & DP_IRQ_HPD_INT_STATUS)) { /* connected status */
		if (!lphw_hpd->base.hpd_high) {
			DP_INFO("DP%d connect interrupt, hpd isr state: 0x%x\n",
					lphw_hpd->parser->cell_idx, isr);
			lphw_hpd->hpd = true;
			rc = queue_work(lphw_hpd->connect_wq,
					&lphw_hpd->connect);
			if (!rc)
				DP_DEBUG("DP%d connect not queued\n",
						lphw_hpd->parser->cell_idx);
		} else {
			DP_INFO("DP%d redundent connect interrupt, hpd isr state: 0x%x\n",
					lphw_hpd->parser->cell_idx, isr);
		}

	} else if ((status == DP_HPD_STATUS_CONNECTED) &&
			(isr & DP_IRQ_HPD_INT_STATUS)) { /* attention interrupt */

		DP_INFO("DP%d hpd_irq interrupt, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);

		rc = queue_work(lphw_hpd->connect_wq, &lphw_hpd->attention);
		if (!rc)
			DP_DEBUG("DP%d attention not queued\n", lphw_hpd->parser->cell_idx);

	} else { /* intermediate status */

		DP_INFO("DP%d ignored, hpd isr state: 0x%x\n",
				lphw_hpd->parser->cell_idx, isr);

	}
}

static int dp_lphw_hpd_simulate_connect(struct dp_hpd *dp_hpd, bool hpd)
{
	struct dp_lphw_hpd_private *lphw_hpd;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	lphw_hpd->base.hpd_high = hpd;
	lphw_hpd->base.alt_mode_cfg_done = hpd;
	lphw_hpd->base.hpd_irq = false;

	if (!lphw_hpd->cb || !lphw_hpd->cb->configure ||
			!lphw_hpd->cb->disconnect) {
		DP_ERR("invalid callback\n");
		return -EINVAL;
	}

	if (hpd)
		lphw_hpd->cb->configure(lphw_hpd->dev);
	else
		lphw_hpd->cb->disconnect(lphw_hpd->dev);

	return 0;
}

static int dp_lphw_hpd_simulate_attention(struct dp_hpd *dp_hpd, int vdo)
{
	struct dp_lphw_hpd_private *lphw_hpd;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	lphw_hpd->base.hpd_irq = true;

	if (lphw_hpd->cb && lphw_hpd->cb->attention)
		lphw_hpd->cb->attention(lphw_hpd->dev);

	return 0;
}

int dp_lphw_hpd_register(struct dp_hpd *dp_hpd)
{
	struct dp_lphw_hpd_private *lphw_hpd;
	int hpd;
	int rc = 0;

	if (!dp_hpd)
		return -EINVAL;

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	hpd = gpio_get_value_cansleep(lphw_hpd->gpio_cfg.gpio);
	lphw_hpd->hpd = hpd;

	/* Hook up GPIO interrupt, set debouncing mode and timer */
	DP_INFO("DP%d GPIO initial hpd=%d\n", lphw_hpd->parser->cell_idx, hpd);
	if (hpd) {
		/* Raising edge */
		rc = devm_request_irq(lphw_hpd->dev, lphw_hpd->irq,
			dp_lphw_hpd_tlmm_isr, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"dp-gpio-intp", lphw_hpd);
		lphw_hpd->gpio_check_start_time = ktime_get();
		lphw_hpd->gpio_check_state = GPIO_CHECK_STATE_RAISING;
		mod_timer(&lphw_hpd->gpio_timer, jiffies +
				msecs_to_jiffies(lphw_hpd->parser->gpio_hpd_high_debounce_ms));
		DP_DEBUG("DP%d GPIO raising edge debounce started\n",
				lphw_hpd->parser->cell_idx);
	} else {
		/* Falling edge */
		rc = devm_request_irq(lphw_hpd->dev, lphw_hpd->irq,
			dp_lphw_hpd_tlmm_isr, IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			"dp-gpio-intp", lphw_hpd);
		lphw_hpd->gpio_check_start_time = ktime_get();
		lphw_hpd->gpio_check_state = GPIO_CHECK_STATE_FALLING;
		mod_timer(&lphw_hpd->gpio_timer, jiffies +
				msecs_to_jiffies(lphw_hpd->parser->gpio_hpd_low_debounce_ms));
		DP_DEBUG("DP%d GPIO falling edge debounce started\n",
				lphw_hpd->parser->cell_idx);
	}
	if (rc)
		DP_ERR("DP%d GPIO failed to request IRQ: %d\n",
				lphw_hpd->parser->cell_idx, rc);

	return rc;
}

static void dp_lphw_hpd_unregister(struct dp_hpd *dp_hpd)
{
	struct dp_lphw_hpd_private *lphw_hpd;

	if (!dp_hpd) {
		DP_ERR("invalid input\n");
		return;
	}

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	disable_irq(lphw_hpd->irq);
	del_timer_sync(&lphw_hpd->gpio_timer);
	DP_INFO("DP%d disable lphw_hpd irq.\n", lphw_hpd->parser->cell_idx);
	devm_free_irq(lphw_hpd->dev, lphw_hpd->irq, lphw_hpd);
}

static void dp_lphw_hpd_deinit(struct dp_lphw_hpd_private *lphw_hpd)
{
	struct dp_parser *parser = lphw_hpd->parser;
	int i = 0;

	for (i = 0; i < parser->mp[DP_PHY_PM].num_vreg; i++) {

		if (!strcmp(parser->mp[DP_PHY_PM].vreg_config[i].vreg_name,
					"hpd-pwr")) {
			/* disable the hpd-pwr voltage regulator */
			if (msm_dss_enable_vreg(
				&parser->mp[DP_PHY_PM].vreg_config[i], 1,
				false))
				DP_ERR("DP%d hpd-pwr vreg not disabled\n",
						lphw_hpd->parser->cell_idx);

			break;
		}
	}
}

static void dp_lphw_hpd_init(struct dp_lphw_hpd_private *lphw_hpd)
{
	struct dp_pinctrl pinctrl = {0};
	struct dp_parser *parser = lphw_hpd->parser;
	int i = 0, rc = 0;

	for (i = 0; i < parser->mp[DP_PHY_PM].num_vreg; i++) {

		if (!strcmp(parser->mp[DP_PHY_PM].vreg_config[i].vreg_name,
					"hpd-pwr")) {
			/* enable the hpd-pwr voltage regulator */
			if (msm_dss_enable_vreg(
				&parser->mp[DP_PHY_PM].vreg_config[i], 1,
				true))
				DP_ERR("DP%d hpd-pwr vreg not enabled\n",
						lphw_hpd->parser->cell_idx);

			break;
		}
	}

	pinctrl.pin = devm_pinctrl_get(lphw_hpd->dev);

	if (!IS_ERR_OR_NULL(pinctrl.pin)) {
		pinctrl.state_hpd_active = pinctrl_lookup_state(pinctrl.pin,
						"mdss_dp_hpd_active");

		if (!IS_ERR_OR_NULL(pinctrl.state_hpd_active)) {
			rc = pinctrl_select_state(pinctrl.pin,
					pinctrl.state_hpd_active);
			if (rc)
				DP_ERR("DP%d failed to set hpd_active state\n",
						lphw_hpd->parser->cell_idx);
		}
		pinctrl.state_hpd_tlmm = pinctrl.state_hpd_ctrl = NULL;
	}
}

static int dp_lphw_hpd_create_workqueue(struct dp_lphw_hpd_private *lphw_hpd)
{
	lphw_hpd->connect_wq = create_singlethread_workqueue("dp_lphw_work");
	if (IS_ERR_OR_NULL(lphw_hpd->connect_wq)) {
		DP_ERR("DP%d Error creating connect_wq\n", lphw_hpd->parser->cell_idx);
		return -EPERM;
	}

	INIT_WORK(&lphw_hpd->connect, dp_lphw_hpd_connect);
	INIT_WORK(&lphw_hpd->disconnect, dp_lphw_hpd_disconnect);
	INIT_WORK(&lphw_hpd->attention, dp_lphw_hpd_attention);
	INIT_WORK(&lphw_hpd->secondary_hpd, dp_lphw_hpd_secondary_hpd);
	INIT_WORK(&lphw_hpd->gpio_work, dp_lphw_hpd_tlmm_work);

	return 0;
}

struct dp_hpd *dp_lphw_hpd_get(struct device *dev, struct dp_parser *parser,
	struct dp_catalog_hpd *catalog, struct dp_hpd_cb *cb)
{
	int rc = 0;
	const char *hpd_gpio_name = "qcom,dp-hpd-gpio";
	struct dp_lphw_hpd_private *lphw_hpd = NULL;
	unsigned int gpio;

	if (!dev || !parser || !cb) {
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

	lphw_hpd = devm_kzalloc(dev, sizeof(*lphw_hpd), GFP_KERNEL);
	if (!lphw_hpd) {
		rc = -ENOMEM;
		goto error;
	}

	lphw_hpd->gpio_cfg.gpio = gpio;
	strlcpy(lphw_hpd->gpio_cfg.gpio_name, hpd_gpio_name,
		sizeof(lphw_hpd->gpio_cfg.gpio_name));
	lphw_hpd->gpio_cfg.value = 0;

	rc = gpio_request(lphw_hpd->gpio_cfg.gpio,
		lphw_hpd->gpio_cfg.gpio_name);
	if (rc) {
		DP_ERR("%s: failed to request gpio\n", hpd_gpio_name);
		goto gpio_error;
	}
	gpio_direction_input(lphw_hpd->gpio_cfg.gpio);

	lphw_hpd->dev = dev;
	lphw_hpd->cb = cb;
	lphw_hpd->irq = gpio_to_irq(lphw_hpd->gpio_cfg.gpio);
	lphw_hpd->configured = false;

	rc = dp_lphw_hpd_create_workqueue(lphw_hpd);
	if (rc) {
		DP_ERR("DP%d Failed to create a dp_hpd workqueue\n", parser->cell_idx);
		goto gpio_error;
	}

	lphw_hpd->parser = parser;
	lphw_hpd->catalog = catalog;
	lphw_hpd->base.isr = dp_lphw_hpd_isr;
	lphw_hpd->base.host_init = dp_lphw_hpd_host_init;
	lphw_hpd->base.host_deinit = dp_lphw_hpd_host_deinit;
	lphw_hpd->base.simulate_connect = dp_lphw_hpd_simulate_connect;
	lphw_hpd->base.simulate_attention = dp_lphw_hpd_simulate_attention;
	lphw_hpd->base.register_hpd = dp_lphw_hpd_register;
	lphw_hpd->base.unregister_hpd = dp_lphw_hpd_unregister;

	dp_lphw_hpd_init(lphw_hpd);

	/*
	 * At GPIO level change, the GPIO monitor will do denoise and update
	 * the secondary HPD status.
	 * Set current GPIO status to false (doesn't matter), and force the
	 * monitor start the check right away in next cycle (ASAP).
	 *
	 * Note: we can't detect the HPD IRQ accurately due to the timing
	 * inaccuracy of the timer. So HPD IRQ will be ignored!
	 *
	 * Note: with GPIO monitor enabled, we are not able to execute the
	 * software HPD simulation from the debugfs!
	 */
	if (!lphw_hpd->parser->gpio_hpd_high_debounce_ms)
		lphw_hpd->parser->gpio_hpd_high_debounce_ms =
				DENOISE_RAISE_EDGE_INTERVAL_MS;
	if (!lphw_hpd->parser->gpio_hpd_low_debounce_ms)
		lphw_hpd->parser->gpio_hpd_low_debounce_ms =
				DENOISE_FALL_EDGE_INTERVAL_MS;
	lphw_hpd->last_gpio_hpd = false;
	lphw_hpd->gpio_check_state = GPIO_CHECK_STATE_INIT;
	timer_setup(&lphw_hpd->gpio_timer, dp_lphw_hpd_gpio_timer_callback, 0);

	return &lphw_hpd->base;

gpio_error:
	devm_kfree(dev, lphw_hpd);
error:
	return ERR_PTR(rc);
}

void dp_lphw_hpd_put(struct dp_hpd *dp_hpd)
{
	struct dp_lphw_hpd_private *lphw_hpd;

	if (!dp_hpd)
		return;

	lphw_hpd = container_of(dp_hpd, struct dp_lphw_hpd_private, base);

	dp_lphw_hpd_deinit(lphw_hpd);
	/* Delete the GPIO monitor timer */
	disable_irq(lphw_hpd->irq);
	del_timer_sync(&lphw_hpd->gpio_timer);
	gpio_free(lphw_hpd->gpio_cfg.gpio);
	devm_kfree(lphw_hpd->dev, lphw_hpd);
}
