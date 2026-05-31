# Android makefile for display kernel modules
MM_DRIVER_PATH := $(call my-dir)

ifneq ($(filter gen5 auto_gen, $(TARGET_BOARD_PLATFORM)), $(TARGET_BOARD_PLATFORM))
	MM_DRV_DLKM_ENABLE := true
endif

ifeq ($(TARGET_KERNEL_DLKM_DISABLE), true)
	ifeq ($(TARGET_KERNEL_DLKM_DISPLAY_OVERRIDE), false)
		DISPLAY_DLKM_ENABLE := false
	endif
endif

ifeq ($(DISPLAY_DLKM_ENABLE),  true)
	LOCAL_PATH := $(call my-dir)

	ifeq ($(TARGET_BOARD_PLATFORM),gen4)
		# gen4.5 hgygvmdisp builds: compile msm-hyp and msm-cfg
		# Pass CONFIG flags so the root Kbuild obj-$(CONFIG_...) directives work correctly
		DISPLAY_SELECT_HYP := CONFIG_DRM_MSM_HYP=m
		DISPLAY_SELECT_CFG := CONFIG_DRM_MSM_CFG=m

		ifneq (,$(filter 6.12, $(TARGET_KERNEL_VERSION)))
			# DDK build path (kernel 6.12+) — build msm_hyp.ko and msm_cfg.ko directly
			LOCAL_MODULE_DDK_BUILD := true
			LOCAL_MODULE_DDK_ALLOW_UNSAFE_HEADERS := true
			# build_module.sh maps both gen4 and gen5 to btgt="autogvm", so the default
			# filter regex "autogvm_perf_.*_dist$" matches both gen4gvm and nordau dist
			# targets. Set a specific subtarget regex so only the gen4gvm dist target is
			# selected when building for gen4 (TARGET_BOARD_PLATFORM=gen4).
			LOCAL_MODULE_DDK_SUBTARGET_REGEX := "gen4gvm.*"

			ifneq ($(findstring vendor,$(LOCAL_PATH)),)
				ifneq ($(findstring opensource,$(LOCAL_PATH)),)
					DISPLAY_BLD_DIR := $(TOP)/vendor/qcom/opensource/display-drivers
				endif

				DLKM_DIR := $(TOP)/device/qcom/common/dlkm
				LOCAL_ADDITIONAL_DEPENDENCIES := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)

				KBUILD_OPTIONS := DISPLAY_ROOT=$(DISPLAY_BLD_DIR)
				KBUILD_OPTIONS += BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)

				# Build msm_hyp.ko
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

				# Build msm_cfg.ko
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
			endif # vendor check
		else
			# Legacy path (kernel < 6.12)
			include $(LOCAL_PATH)/msm-hyp/Android.mk
			include $(LOCAL_PATH)/msm-cfg/Android.mk
		endif # kernel version

	else
		# gen5 builds: compile msm/
		include $(LOCAL_PATH)/msm/Android.mk
	endif
endif
