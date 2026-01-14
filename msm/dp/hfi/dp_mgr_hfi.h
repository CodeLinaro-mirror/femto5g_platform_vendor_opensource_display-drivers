/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DP_MGR_HFI_H_
#define _DP_MGR_HFI_H_

#include <linux/types.h>
#include <linux/platform_device.h>

#include "msm_drv.h"
#include "dp_drv.h"
#include "dp_client.h"
#include "hfi_adapter.h"
#include "hfi_props.h"
#include "hfi_utils.h"

/**
 * dp_mgr_hfi_init() - initialize DP HFI display
 * @pdev: platform device pointer
 *
 * Return: pointer to dp_client structure on success, ERR_PTR on failure
 */
struct dp_client *dp_mgr_hfi_init(struct platform_device *pdev);

#endif /* _DP_MGR_HFI_H_ */
