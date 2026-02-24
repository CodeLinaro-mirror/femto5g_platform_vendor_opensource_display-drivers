// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <asm/memory.h>
#include <linux/coresight-stm.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/hash.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/pm_qos.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/llcc-qcom.h>
#include <linux/version.h>
#if (KERNEL_VERSION(6, 3, 0) <= LINUX_VERSION_CODE)
#include <linux/firmware/qcom/qcom_scm.h>
#else
#include <linux/qcom_scm.h>
#endif
#include <linux/soc/qcom/smem.h>
#include <linux/dma-mapping.h>
#include <linux/reset.h>
#include <linux/pm_wakeup.h>
#include "msm_lsr_debug.h"
#include "lsr_core.h"
#include "lsr_hw_io.h"
#include "msm_lsr_clocks.h"

#define FIRMWARE_SIZE			0X00A00000

static int __check_core_registered(struct lsr_device *device,
		phys_addr_t fw_addr, u8 *reg_addr, u32 reg_size,
		phys_addr_t irq)
{
	struct lsr_hal_data *lsr_hal_data;

	if (!device) {
		dprintk(LSR_INFO, "no device Registered\n");
		return -EINVAL;
	}

	lsr_hal_data = device->lsr_hal_data;
	if (!lsr_hal_data)
		return -EINVAL;

	if (lsr_hal_data->irq == irq &&
		(CONTAINS(lsr_hal_data->firmware_base, FIRMWARE_SIZE, fw_addr) ||
		CONTAINS(fw_addr, FIRMWARE_SIZE, lsr_hal_data->firmware_base) ||
		CONTAINS(lsr_hal_data->register_base, reg_size, reg_addr) ||
		CONTAINS(reg_addr, reg_size, lsr_hal_data->register_base) ||
		OVERLAPS(lsr_hal_data->register_base, reg_size, reg_addr, reg_size) ||
		OVERLAPS(reg_addr, reg_size, lsr_hal_data->register_base, reg_size) ||
		OVERLAPS(lsr_hal_data->firmware_base, FIRMWARE_SIZE, fw_addr, FIRMWARE_SIZE) ||
		OVERLAPS(fw_addr, FIRMWARE_SIZE, lsr_hal_data->firmware_base, FIRMWARE_SIZE))) {
		return 0;
	}

	dprintk(LSR_INFO, "Device not registered\n");
	return -EINVAL;
}

int msm_lsr_init_reg_and_irq(struct lsr_device *device,
		struct msm_lsr_platform_resources *res)
{
	struct lsr_hal_data *hal = NULL;
	int rc = 0;

	rc = __check_core_registered(device, res->firmware_base,
			(u8 *)(uintptr_t)res->register_base, res->register_size, res->irq);
	if (!rc) {
		dprintk(LSR_ERR, "Core present/Already added\n");
		rc = -EEXIST;
		goto err_core_init;
	}

	hal = kzalloc(sizeof(*hal), GFP_KERNEL);
	if (!hal) {
		dprintk(LSR_ERR, "Failed to alloc\n");
		rc = -ENOMEM;
		goto err_core_init;
	}

	hal->irq = res->irq;
	hal->irq_wd = res->irq_wd;
	hal->firmware_base = res->firmware_base;
	hal->register_base = ioremap(0xA900000, 0x100000);

	hal->register_size = res->register_size;
	if (!hal->register_base) {
		dprintk(LSR_ERR, "could not map reg addr %pa of size %d\n", &res->register_base,
			res->register_size);
		goto error_irq_fail;
	}

	if (res->gcc_reg_base) {
		hal->gcc_reg_base = devm_ioremap(&res->pdev->dev,
				res->gcc_reg_base, res->gcc_reg_size);
		hal->gcc_reg_size = res->gcc_reg_size;
		if (!hal->gcc_reg_base)
			dprintk(LSR_ERR, "could not map gcc reg addr %pa of size %d\n",
				&res->gcc_reg_base, res->gcc_reg_size);
	}

	device->lsr_hal_data = hal;
	rc = request_threaded_irq(res->irq, lsr_hfi_isr, iris_hfi_core_work_handler,
			IRQF_TRIGGER_HIGH, "msm_lsr", device);
	if (unlikely(rc)) {
		dprintk(LSR_ERR, "%s: request_irq failed rc: %d\n", __func__, rc);
		goto error_irq_fail;
	}

	rc = request_threaded_irq(res->irq_wd, lsr_hfi_isr, lsr_wd_handler,
			IRQF_TRIGGER_HIGH, "msm_lsr_ssr", device);
	if (unlikely(rc)) {
		dprintk(LSR_ERR, "%s: request_irq failed rc: %d\n", __func__, rc);
		goto error_irq_wd_fail;
	}

	disable_irq_nosync(res->irq);
	dprintk(LSR_INFO,
		"firmware_base = %pa, register_base = %pa, size = %d, remapped reg base = %pK\n",
		&res->firmware_base, &res->register_base, res->register_size, hal->register_base);
	return rc;

error_irq_wd_fail:
	free_irq(res->irq, device);
error_irq_fail:
	kfree(hal);
err_core_init:
	return rc;
}
