load(":display_modules.bzl", "display_driver_modules")
load(":display_driver_build.bzl", "define_target_variant_modules")
load(":target_variants.bzl", "get_all_variants")

def define_kalama():
    for (t, v) in get_all_variants():
        if t == "kalama" or t == "kalama-le":
            define_target_variant_modules(
                target = t,
                variant = v,
                registry = display_driver_modules,
                modules = [
                    "msm_drm",
                    "lt9611uxc",
                ],
                config_options = [
                    "CONFIG_DRM_MSM_SDE",
                    "CONFIG_SYNC_FILE",
                    "CONFIG_DRM_MSM_DSI",
                    "CONFIG_DRM_MSM_DP",
                    "CONFIG_DSI_PARSER",
                    "CONFIG_DRM_SDE_WB",
                    "CONFIG_DRM_SDE_RSC",
                    "CONFIG_DRM_MSM_REGISTER_LOGGING",
                    "CONFIG_QCOM_MDSS_PLL",
                    "CONFIG_DRM_SDE_VM",
                    "CONFIG_THERMAL_OF",
                    "CONFIG_MSM_MMRM",
                    "CONFIG_QTI_HW_FENCE",
                    # CONFIG_QCOM_SPEC_SYNC disabled for kernel-6.18: sync_fence module is not built
                    "CONFIG_MSM_EXT_DISPLAY",
                ],
            )
