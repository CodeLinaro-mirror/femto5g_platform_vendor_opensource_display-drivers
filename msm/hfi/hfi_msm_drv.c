// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "hfi_msm_drv.h"

#define MSM_DRV_HFI_ADAPTER 1

int hfi_msm_drv_hfi_init(struct msm_drm_private *priv)
{
	struct msm_drm_hfi_private *hfi_priv;
	int rc = 0;

	hfi_priv = container_of(priv, struct msm_drm_hfi_private, base);
	if (!hfi_priv)
		return -EINVAL;

	hfi_priv->hfi_adapter = hfi_adapter_init(MSM_DRV_HFI_ADAPTER);
	if (!hfi_priv->hfi_adapter) {
		rc = -EPROBE_DEFER;
		DRM_ERROR("failed to initialize HFI adapter: %d\n", rc);
		return rc;
	}

	return rc;
}

struct msm_drm_private *hfi_msm_drv_init(struct drm_device *ddev)
{
	struct msm_drm_hfi_private *hfi_priv;
	struct msm_drm_private *priv;

	if ((!ddev))
		return NULL;

	hfi_priv = kzalloc(sizeof(*hfi_priv), GFP_KERNEL);
	if (!hfi_priv)
		return NULL;

	priv = &hfi_priv->base;
	ddev->dev_private = priv;
	priv->dev = ddev;

	return priv;
}
