// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/slab.h>
#include <linux/qcom_scm.h>
#include <linux/of_address.h>
#include <linux/firmware.h>
#include <linux/soc/qcom/mdt_loader.h>

#include "msm_lsr_debug.h"
#include "lsr_core.h"

#define MAX_FIRMWARE_NAME_SIZE 128

struct lsr_hfi_ops *lsr_hfi_initialize(struct msm_lsr_platform_resources *res)
{
	struct lsr_hfi_ops *ops_tbl = NULL;
	int rc = 0;

	ops_tbl = kzalloc(sizeof(struct lsr_hfi_ops), GFP_KERNEL);
	if (!ops_tbl) {
		dprintk(LSR_ERR, "%s: failed to allocate ops_tbl\n", __func__);
		return NULL;
	}

	rc = lsr_iris_hfi_initialize(ops_tbl, res);
	if (rc) {
		if (rc != -EPROBE_DEFER)
			dprintk(LSR_ERR, "%s device init failed rc = %d", __func__, rc);
		goto err_hfi_init;
	}

	return ops_tbl;

err_hfi_init:
	kfree(ops_tbl);
	return ERR_PTR(rc);
}

void lsr_hfi_deinitialize(struct lsr_hfi_ops *ops_tbl)
{
	if (!ops_tbl) {
		dprintk(LSR_ERR, "%s invalid device %pK", __func__, ops_tbl);
		return;
	}

	lsr_iris_hfi_delete_device(ops_tbl->hfi_device_data);
	kfree(ops_tbl);
}

static int __load_fw_to_memory(struct platform_device *pdev,
		const char *fw_name)
{
	int rc = 0;
	const struct firmware *firmware = NULL;
	char firmware_name[MAX_FIRMWARE_NAME_SIZE] = {0};
	struct device_node *node = NULL;
	struct resource res = {0};
	phys_addr_t phys = 0;
	size_t res_size = 0;
	ssize_t fw_size = 0;
	void *virt = NULL;
	int pas_id = TZBSP_LSR_PAS_ID;

	if (!fw_name || !(*fw_name) || !pdev) {
		dprintk(LSR_ERR, "%s: Invalid inputs\n", __func__);
		return -EINVAL;
	}
	if (strlen(fw_name) >= MAX_FIRMWARE_NAME_SIZE - 4) {
		dprintk(LSR_ERR, "%s: Invalid fw name\n", __func__);
		return -EINVAL;
	}
	scnprintf(firmware_name, ARRAY_SIZE(firmware_name), "%s.mbn", fw_name);

	rc = of_property_read_u32(pdev->dev.of_node, "pas-id", &pas_id);
	if (rc) {
		dprintk(LSR_ERR, "%s: error %d while reading DT for \"pas-id\"\n", __func__, rc);
		goto exit;
	}

	node = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!node) {
		dprintk(LSR_ERR, "%s: DT error getting \"memory-region\" property\n", __func__);
		return -EINVAL;
	}

	rc = of_address_to_resource(node, 0, &res);
	if (rc) {
		dprintk(LSR_ERR, "%s: error %d getting \"memory-region\" resource\n", __func__, rc);
		goto exit;
	}
	phys = res.start;
	res_size = (size_t)resource_size(&res);

	rc = request_firmware(&firmware, firmware_name, &pdev->dev);
	dprintk(LSR_ERR, "%s: %d requesting \"%s\"\n", __func__, rc, firmware_name);
	if (rc) {
		dprintk(LSR_ERR, "%s: error %d requesting \"%s\"\n", __func__, rc, firmware_name);
		goto exit;
	}

	fw_size = qcom_mdt_get_size(firmware);
	if (fw_size < 0 || res_size < (size_t)fw_size) {
		rc = -EINVAL;
		dprintk(LSR_ERR, "%s: Corrupted fw image. Alloc size: %lu, fw size: %ld", __func__,
			res_size, fw_size);
		goto exit;
	}

	virt = memremap(phys, res_size, MEMREMAP_WC);
	if (!virt) {
		rc = -ENOMEM;
		dprintk(LSR_ERR, "%s: unable to remap firmware memory\n", __func__);
		goto exit;
	}

	rc = qcom_mdt_load(&pdev->dev, firmware, firmware_name,
			pas_id, virt, phys, res_size, NULL);
	if (rc) {
		dprintk(LSR_ERR, "%s: error %d loading \"%s\"\n", __func__, rc, firmware_name);
		goto exit;
	}

	rc = qcom_scm_pas_auth_and_reset(pas_id);
	if (rc) {
		dprintk(LSR_ERR, "%s: error %d authenticating %s\n", __func__, rc, firmware_name);
		goto exit;
	}

	memunmap(virt);
	release_firmware(firmware);
	dprintk(LSR_CORE, "%s: firmware %s loaded successfully\n", __func__, firmware_name);
	return pas_id;

exit:
	if (virt)
		memunmap(virt);
	if (firmware)
		release_firmware(firmware);
	return rc;
}

int load_lsr_fw_impl(struct lsr_device *device)
{
	int rc = 0;

	if (!device->resources.fw.cookie) {
		device->resources.fw.cookie =
			__load_fw_to_memory(device->res->pdev, device->res->fw_name);
		if (device->resources.fw.cookie <= 0) {
			dprintk(LSR_ERR, "Failed to download firmware\n");
			device->resources.fw.cookie = 0;
			rc = -ENOMEM;
		}
	}
	return rc;
}

int unload_lsr_fw_impl(struct lsr_device *device)
{
	qcom_scm_pas_shutdown(device->resources.fw.cookie);
	device->resources.fw.cookie = 0;
	return 0;
}
