load(":display_modules.bzl", "display_driver_modules")
load(":display_driver_build.bzl", "define_target_variant_modules")
load("//soc-repo:target_variants.bzl", "get_all_la_variants")

def define_nordau():
    for (t, v) in get_all_la_variants():
        if t == "autogvm":
            define_target_variant_modules(
		target = t,
		variant = v,
		registry = display_driver_modules,
		modules = [
		    "msm_drm",
		],
		config_options = [
		    "CONFIG_DRM_MSM_SDE",
		    "CONFIG_DRM_MSM_HYP",
		    "CONFIG_DRM_MSM_HYP_VIRTIO",
		    "CONFIG_SYNC_FILE",
		    "CONFIG_DRM_MSM_REGISTER_LOGGING",
		    "CONFIG_QCOM_MDSS_PLL",
		    "CONFIG_THERMAL_OF",
	    ],
	)
