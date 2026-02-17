// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include "hfi_msm_drv.h"
#include "sde_kms.h"

int hfi_msm_drv_hfi_init(struct msm_drm_private *priv, bool in_trusted_vm)
{
	struct msm_drm_hfi_private *hfi_priv = priv->hfi_priv;
	int rc = 0;
	struct sde_kms *sde_kms;
	bool hw_fence_enabled = false;

	if (!hfi_priv)
		return -EINVAL;
	if (priv->kms) {
		sde_kms = to_sde_kms(priv->kms);
		hw_fence_enabled = (sde_kms->catalog && (sde_kms->catalog->hw_fence_rev ||
			sde_kms->catalog->lsr_hw_fence_rev));
	}

	hfi_priv->hfi_adapter = hfi_adapter_init(in_trusted_vm, hw_fence_enabled);
	if (!hfi_priv->hfi_adapter) {
		rc = -EINVAL;
		DRM_ERROR("failed to initialize HFI adapter: %d\n", rc);
		return rc;
	}
	DRM_DEBUG("success to initialize HFI adapter: %d\n", rc);

	return rc;
}

int hfi_msm_drv_init(struct drm_device *ddev)
{
	struct msm_drm_hfi_private *hfi_priv;
	struct msm_drm_private *priv;

	if (!ddev)
		return -EINVAL;

	hfi_priv = kvzalloc(sizeof(*hfi_priv), GFP_KERNEL);
	if (!hfi_priv)
		return -EINVAL;

	priv = ddev->dev_private;
	hfi_priv->base = priv;
	priv->hfi_priv = hfi_priv;

	return 0;
}
