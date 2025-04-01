load(":display_modules.bzl", "display_driver_modules")
load(":display_driver_build.bzl", "define_target_variant_modules")
load("//msm-kernel:target_variants.bzl", "get_all_la_variants", "get_all_le_variants", "get_all_lxc_variants")

def define_gen4gvm():
    for (t, v) in get_all_la_variants() + get_all_le_variants() + get_all_lxc_variants():
        define_target_variant_modules(
            target = t,
            variant = v,
            registry = display_driver_modules,
            modules = [
                "msm_hyp",
            ],
            config_options = [
                "CONFIG_DRM_MSM_HYP",
                "CONFIG_DRM_MSM_HYP_WFD"
            ],
        )
