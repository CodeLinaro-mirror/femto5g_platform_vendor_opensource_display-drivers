# Android makefile for display kernel modules
MM_DRIVER_PATH := $(call my-dir)

ifneq ($(filter gen5, $(TARGET_BOARD_PLATFORM)), $(TARGET_BOARD_PLATFORM))
	MM_DRV_DLKM_ENABLE := true
endif

ifeq ($(TARGET_KERNEL_DLKM_DISABLE), true)
	ifeq ($(TARGET_KERNEL_DLKM_DISPLAY_OVERRIDE), false)
		DISPLAY_DLKM_ENABLE := false
	endif
endif

ifeq ($(DISPLAY_DLKM_ENABLE),  true)
	LOCAL_PATH := $(call my-dir)
	include $(LOCAL_PATH)/msm/Android.mk
endif
