# Android makefile for display kernel modules
LOCAL_PATH := $(call my-dir)

ifeq ($(ENABLE_HYP),true)

DISPLAY_SELECT_CFG := CONFIG_DRM_MSM_CFG=m
DISPLAY_SELECT_HYP := CONFIG_DRM_MSM_HYP=m

ifneq (,$(filter W Baklava 16, $(PLATFORM_VERSION)))
	LOCAL_MODULE_DDK_BUILD := true
	LOCAL_MODULE_DDK_ALLOW_UNSAFE_HEADERS := true

# This makefile is only for DLKM
ifneq ($(findstring vendor,$(LOCAL_PATH)),)

ifneq ($(findstring opensource,$(LOCAL_PATH)),)
	DISPLAY_BLD_DIR := $(TOP)/vendor/qcom/opensource/display-drivers
endif # opensource

DLKM_DIR := $(TOP)/device/qcom/common/dlkm

LOCAL_ADDITIONAL_DEPENDENCIES := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)

###########################################################
# This is set once per LOCAL_PATH, not per (kernel) module
KBUILD_OPTIONS := DISPLAY_ROOT=$(DISPLAY_BLD_DIR)
KBUILD_OPTIONS += BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)

# msm-hyp
##########################################################
KBUILD_OPTIONS += MODNAME=msm_hyp
KBUILD_OPTIONS += $(DISPLAY_SELECT_HYP)

include $(CLEAR_VARS)
LOCAL_SRC_FILES   := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE              := msm_hyp.ko
LOCAL_MODULE_KBUILD_NAME  := msm_hyp.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)

include $(DLKM_DIR)/Build_external_kernelmodule.mk

#LOCAL_PATH := $(LOCAL_PATH)/../

# Build display.ko as msm_cfg.ko
###########################################################
KBUILD_OPTIONS += MODNAME=msm_cfg
KBUILD_OPTIONS += $(DISPLAY_SELECT_CFG)

include $(CLEAR_VARS)
LOCAL_SRC_FILES   := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE              := msm_cfg.ko
LOCAL_MODULE_KBUILD_NAME  := msm_cfg.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)

include $(DLKM_DIR)/Build_external_kernelmodule.mk
###########################################################
endif # DLKM check
else
   include $(LOCAL_PATH)/msm-hyp/Android.mk
   LOCAL_PATH := $(LOCAL_PATH)/../
   include $(LOCAL_PATH)/msm-cfg/Android.mk

endif # Platform version
else
    include $(LOCAL_PATH)/msm/Android.mk
endif
