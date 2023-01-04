load(":display_modules.bzl", "display_driver_modules")
load(":display_driver_build.bzl", "define_consolidate_gki_modules")

def define_anorak61():
    define_consolidate_gki_modules(
        target = "anorak",
        registry = display_driver_modules,
        modules = [
            "msm_drm",
        ],
        config_options = [
	    "CONFIG_DRM_MSM",
            "CONFIG_DRM_MSM_SDE",
            "CONFIG_SYNC_FILE",
            "CONFIG_DRM_MSM_DSI",
            "CONFIG_DRM_MSM_DP",
            "CONFIG_DRM_MSM_DP_MST",
            "CONFIG_DSI_PARSER",
            "CONFIG_QCOM_MDSS_PLL",
            "CONFIG_DRM_SDE_RSC",
            "CONFIG_DRM_SDE_WB",
            "CONFIG_DRM_MSM_REGISTER_LOGGING",
            "CONFIG_MSM_MMRM",
	    "CONFIG_DISPLAY_BUILD",
            "CONFIG_DRM_SDE_SYSTEM_SLEEP_DISABLE",
            "CONFIG_DRM_SDE_IPCC",
	    "CONFIG_THERMAL_OF",
            "CONFIG_DRM_SDE_MINIDUMP_DISABLE",
        ],
)
