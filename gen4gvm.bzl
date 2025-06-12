load(":display_modules.bzl", "display_driver_modules")
load(":display_driver_build.bzl", "define_target_variant_modules")
load("//soc-repo:target_variants.bzl", "get_all_variants")

def define_gen4gvm():
    for (t, v) in get_all_variants():
        define_target_variant_modules(
            target = t,
            variant = v,
            registry = display_driver_modules,
            modules = [
                "msm_hyp",
                "msm_cfg",
            ],
            config_options = [
                "CONFIG_DRM_MSM_HYP",
                "CONFIG_DRM_MSM_HYP_WFD",
                "CONFIG_DRM_MSM_CFG",
                "CONFIG_DRM_MSM_HYP_VIRTIO"
            ],
        )
