# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(DISPLAY_ROOT),)
DISPLAY_ROOT := $(srctree)/techpack/$(src)
endif

# gen4.5 HGY GVM builds: CONFIG_ARCH_QTI_VM=y
# Include hgygvmdisp.conf which sets CONFIG_DRM_MSM_HYP and CONFIG_DRM_MSM_CFG.
# Since hgygvmdisp.conf does NOT set CONFIG_DRM_MSM, msm/ will NOT be compiled.
ifeq (y, $(findstring y, $(CONFIG_ARCH_QTI_VM)))
ifeq ($(GEN5_LVGVM), y)
	include $(DISPLAY_ROOT)/config/gen5lvgvm.conf
	LINUXINCLUDE += -include $(DISPLAY_ROOT)/config/gen5lvgvmconf.h
else
	include $(DISPLAY_ROOT)/config/hgygvmdisp.conf
	LINUXINCLUDE += -include $(DISPLAY_ROOT)/config/hgygvmdispconf.h
endif
endif

# msm/     — compiled only when CONFIG_DRM_MSM is set (gen5 builds)
# msm-hyp/ — compiled when CONFIG_DRM_MSM_HYP is set (gen4.5 hgygvmdisp builds)
# msm-cfg/ — compiled when CONFIG_DRM_MSM_CFG is set (gen4.5 hgygvmdisp builds)
obj-$(CONFIG_DRM_MSM) += msm/
obj-$(CONFIG_DRM_MSM_HYP) += msm-hyp/
obj-$(CONFIG_DRM_MSM_CFG) += msm-cfg/
