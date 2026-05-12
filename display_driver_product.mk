# SPDX-License-Identifier: GPL-2.0-only

DISPLAY_DLKM_ENABLE := true
ifeq ($(TARGET_KERNEL_DLKM_DISABLE), true)
	ifeq ($(TARGET_KERNEL_DLKM_DISPLAY_OVERRIDE), false)
		DISPLAY_DLKM_ENABLE := false
	endif
endif

ifeq ($(DISPLAY_DLKM_ENABLE),  true)
	ifeq ($(TARGET_BOARD_PLATFORM),gen4)
		# gen4.5 hgygvmdisp builds: msm_hyp.ko + msm_cfg.ko (no msm_drm.ko)
		PRODUCT_PACKAGES += msm_hyp.ko
		PRODUCT_PACKAGES += msm_cfg.ko
		DISPLAY_MODULES_DRIVER := msm_hyp.ko msm_cfg.ko
	else
		# gen5 builds: msm_drm.ko
		PRODUCT_PACKAGES += msm_drm.ko
		DISPLAY_MODULES_DRIVER := msm_drm.ko
	endif
endif

