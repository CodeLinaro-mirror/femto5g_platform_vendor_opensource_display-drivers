/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _HDMI_CEC_H_
#define _HDMI_CEC_H_

#include "hdmi_parser.h"

#define CEC_NAME "sde-hdmi-cec"

/* CEC Register Definition */
#define CEC_INTR_MASK (BIT(1) | BIT(3) | BIT(7))
#define CEC_SUPPORTED_HW_VERSION 0x30000001

enum cec_irq_status {
	CEC_IRQ_FRAME_WR_DONE = BIT(0),
	CEC_IRQ_FRAME_RD_DONE = BIT(1),
	CEC_IRQ_FRAME_ERROR = BIT(2),
};

struct edid;

struct hdmi_cec {
	int irq;
	struct hdmi_io_data *io_data;
	irqreturn_t (*isr)(void *cec);
};

struct hdmi_cec *hdmi_cec_get(struct platform_device *pdev,
			struct hdmi_parser *parser);
void hdmi_cec_put(struct hdmi_cec *cec);
int hdmi_cec_set_phys_addr_from_edid(struct hdmi_cec *cec, const struct edid *edid);
int hdmi_cec_invalidate_phys_addr(struct hdmi_cec *cec);
#endif /* _HDMI_CEC_H_ */
