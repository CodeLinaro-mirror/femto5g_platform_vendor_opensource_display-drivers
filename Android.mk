DISPLAY_DRIVER := $(call my-dir)
# Android makefile for display kernel modules
DISPLAY_DLKM_ENABLE := true
ifeq ($(TARGET_KERNEL_DLKM_DISABLE), true)
	ifeq ($(TARGET_KERNEL_DLKM_DISPLAY_OVERRIDE), false)
		DISPLAY_DLKM_ENABLE := false
	endif
endif

ifeq ($(DISPLAY_DLKM_ENABLE),  true)
	LOCAL_PATH := $(call my-dir)
	include $(LOCAL_PATH)/msm/Android.mk
	include $(CLEAR_VARS)

	ifneq (,$(call is-board-platform-in-list2, sun shikra))
		LOCAL_PATH := $(DISPLAY_DRIVER)
		include $(LOCAL_PATH)/bridge-drivers/Android.mk
	endif

endif
