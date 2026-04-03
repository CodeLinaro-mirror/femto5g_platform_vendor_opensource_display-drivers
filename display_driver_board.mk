# SPDX-License-Identifier: GPL-2.0-only

DISPLAY_DLKM_ENABLE := true
ifeq ($(TARGET_KERNEL_DLKM_DISABLE),true)
  ifeq ($(TARGET_KERNEL_DLKM_DISPLAY_OVERRIDE),false)
    DISPLAY_DLKM_ENABLE := false
  endif
endif

# Behavior-preserving: avoid is-xxx macros, use GNU make 'filter'.
# Default the list to the current platform, so the membership check is truthy.
DISPLAY_DLKM_BOARD_PLATFORMS_LIST := $(TARGET_BOARD_PLATFORM)

ifeq ($(DISPLAY_DLKM_ENABLE),true)
  ifneq ($(filter $(TARGET_BOARD_PLATFORM),$(DISPLAY_DLKM_BOARD_PLATFORMS_LIST)),)
    include vendor/qcom/opensource/display-drivers/bridge-drivers/display_kernel_bridge_modules.mk
    BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/msm_drm.ko $(BRIDGE_KERNEL_MODULES)
    BOARD_VENDOR_RAMDISK_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/msm_drm.ko $(BRIDGE_KERNEL_MODULES)
    BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD += $(KERNEL_MODULES_OUT)/msm_drm.ko $(BRIDGE_KERNEL_MODULES)
  endif
endif
