load(":display_driver_build.bzl", "display_module_entry")

display_driver_modules = display_module_entry([":display_drivers_headers"])
module_entry = display_driver_modules.register

#---------- MSM-HYP MODULE -------------------------

module_entry(
      name = "msm_hyp",
      config_option = "CONFIG_DRM_MSM_HYP",
      path = None,
      config_srcs = {
         "CONFIG_DRM_MSM_HYP" : [
            "msm-hyp/msm_drv_hyp.c",
            "msm-hyp/msm_hyp_fence.c",
            "msm-hyp/msm_hyp_notifier.c",
            "msm-hyp/msm_hyp_trace_point.c",
            "msm-hyp/msm_hyp_utils.c",
            "msm-hyp/wfd/user_hab_utils.c",
            "msm-hyp/wfd/wfd_kms.c",
            "msm-hyp/wfd/wire_user.c",
         ],
      }
)
