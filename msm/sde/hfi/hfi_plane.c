// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include "msm_drv.h"
#include "hfi_props.h"
#include "hfi_adapter.h"
#include "hfi_kms.h"
#include "hfi_crtc.h"
#include "hfi_plane.h"
#include "hfi_catalog.h"

#define HFI_PLANE_ID(pl) ((pl)->sde_base->base.base.id)

#define HFI_DEBUG_PLANE(pl, fmt, ...) SDE_DEBUG("plane%d " fmt,\
				(pl) ? HFI_PLANE_ID(pl) : -1, ##__VA_ARGS__)

#define HFI_ERROR_PLANE(pl, fmt, ...) SDE_ERROR("plane%d " fmt,\
				(pl) ? HFI_PLANE_ID(pl) : -1, ##__VA_ARGS__)


#define to_hfi_plane(x) x->hfi_plane

#define HFI_IS_YUV_FORMAT(format) \
	(((format) >= HFI_COLOR_FORMAT_LINEAR_MIN) && ((format) <= HFI_COLOR_FORMAT_UBWC_LOSSY_MAX))

#define HFI_PLANE_MAX_PROPS 128
#define HFI_PLANE_BASE_PROP_MAX_SIZE 1024

#define DWORD_SIZE(x) (x/sizeof(u32))

/*
 * struct base_prop_lookup: tuple of drm property ID to HFI property ID
 */
struct base_prop_lookup {
	u32 drm_prop;
	u32 hfi_prop;
};

/*
 * dcp_rot_map - maps for drm rotation to hfi rotation
 *
 * @hfi_rot		HFI layer rotation
 * @drm_rot		DRM layer rotation
 */
struct dcp_rot_map {
	u32 hfi_rot;
	u32 drm_rot;
};

/*
 * dcp_cache_attr - attributes for dcp cache
 *
 * @cache_state		Cache type
 * @cache_op_type	Cache op type
 */

struct dcp_cache_attr {
	u32 cache_state;
	u32 cache_op_type;
};

static const struct dcp_rot_map dpu_rot_map[] = {
	{HFI_DISPLAY_ROTATION_0, DRM_MODE_ROTATE_0},
	{HFI_DISPLAY_ROTATION_90, DRM_MODE_ROTATE_90},
	{HFI_DISPLAY_ROTATION_180, DRM_MODE_ROTATE_180},
	{HFI_DISPLAY_ROTATION_270, DRM_MODE_ROTATE_270},
	{HFI_DISPLAY_REFLECT_X, DRM_MODE_REFLECT_X},
	{HFI_DISPLAY_REFLECT_Y, DRM_MODE_REFLECT_Y},
};

static struct base_prop_lookup hfi_plane_base_props_map[] = {
	{PLANE_PROP_ZPOS, HFI_PROPERTY_LAYER_ZPOS},
	{PLANE_PROP_ALPHA, HFI_PROPERTY_LAYER_ALPHA},
	{PLANE_PROP_BLEND_OP, HFI_PROPERTY_LAYER_BLEND_TYPE},
	{PLANE_PROP_SRC_IMG_SIZE, HFI_PROPERTY_LAYER_SRC_IMG_SIZE_H},
	{PLANE_PROP_SRC_IMG_SIZE, HFI_PROPERTY_LAYER_SRC_IMG_SIZE_W},
	{PLANE_PROP_MULTIRECT_MODE, HFI_PROPERTY_LAYER_MULTIRECT_MODE},
	{PLANE_PROP_BG_ALPHA, HFI_PROPERTY_LAYER_BG_ALPHA},
	{PLANE_PROP_FB_TRANSLATION_MODE, HFI_PROPERTY_LAYER_SECURITY_POLICY},
	{PLANE_PROP_EXCL_RECT_V1, HFI_PROPERTY_LAYER_EXCLUSION_RECTANGLE_ROI},
};

static u32 hfi_plane_blend_ops_map[] = {
	[SDE_DRM_BLEND_OP_NOT_DEFINED] = HFI_BLEND_OP_NOT_DEFINED,
	[SDE_DRM_BLEND_OP_OPAQUE] = HFI_BLEND_OP_OPAQUE,
	[SDE_DRM_BLEND_OP_PREMULTIPLIED] = HFI_BLEND_OP_PREMULTIPLIED,
	[SDE_DRM_BLEND_OP_COVERAGE] = HFI_BLEND_OP_COVERAGE,
	[SDE_DRM_BLEND_OP_MAX] = HFI_BLEND_OP_MAX,
	[SDE_DRM_BLEND_OP_SKIP] = HFI_BLEND_OP_SKIP,
};

static u32 hfi_plane_multirect_mode_map[] = {
	[SDE_SSPP_MULTIRECT_PARALLEL] = HFI_PARALLEL_FETCH,
	[SDE_SSPP_MULTIRECT_TIME_MX] = HFI_TIME_MULTIPLEX_FETCH,
};

static struct base_prop_lookup hfi_plane_repro_props_map[] = {
	{PLANE_PROP_REPROJ_RENDER_TYPE, HFI_PROPERTY_LAYER_LSR_LAYER_TYPE},
	{PLANE_PROP_REPROJ_LOCK_TYPE, HFI_PROPERTY_LAYER_LSR_LOCK_TYPE},
	{PLANE_PROP_REPROJ_RENDER_POSE, HFI_PROPERTY_LAYER_LSR_RENDER_POSE},
	{PLANE_PROP_REPROJ_PLANE_EQUATION, HFI_PROPERTY_LAYER_LSR_REPROJ_PLANE_EQ},
	{PLANE_PROP_REPROJ_RENDER_FRUSTUM, HFI_PROPERTY_LAYER_LSR_RENDER_FRUSTUM},
	{PLANE_PROP_REPROJ_LAYER_GAMMA,	HFI_PROPERTY_LAYER_GAMMA},
};

static struct base_prop_lookup hfi_plane_csc_props_map[] = {
	{PLANE_PROP_REPROJ_ALPHA_BUFFER, HFI_PROPERTY_LAYER_LSR_IN_A_BUFFER},
};

struct kv_prop_lookup {
	u32 drm_prop;
	u32 hfi_prop;
	void (*add_hfi_prop)(u32 hfi_prop, struct sde_plane *plane,
			struct sde_plane_state *old_state, struct hfi_cmdbuf_t *cmd_buf);
};

struct kv_prop_lookup hfi_plane_custom_kv_props_map[] = {
//	{PLANE_PROP_CSC, HFI_PROPERTY_LAYER_CSC_CFG, set_csc},
};

static struct hfi_kms *sde_plane_get_kms(struct sde_plane *plane)
{
	struct msm_drm_private *priv;

	if (plane && plane->base.dev && plane->base.dev->dev_private) {
		priv = plane->base.dev->dev_private;
		return ((priv && priv->kms) ?
				to_hfi_kms(to_sde_kms(priv->kms)) : NULL);
	}

	return NULL;
}

static u32 _hfi_plane_scale_alpha(struct sde_mdss_cfg *catalog, u32 prop_val)
{
	 if (!(test_bit(SDE_FEATURE_10_BITS_COMPONENTS, catalog->features)))
		 prop_val = prop_val << 8;

	return prop_val;
}

static u32 hfi_rot_lookup(u32 drm_rot)
{
	u32 support_rot = 0;

	for (int i = 0; i < ARRAY_SIZE(dpu_rot_map); i++) {
		if (dpu_rot_map[i].drm_rot & drm_rot)
			support_rot |= dpu_rot_map[i].hfi_rot;
	}

	return support_rot;
}

#if IS_ENABLED(CONFIG_DRM_SDE_LSR)
int _hfi_add_repro_prop_helper(u32 hfi_prop, struct sde_plane *plane,
	struct sde_plane_state *pstate,
	struct hfi_util_u32_prop_helper *prop_collector)
{
	u32 prop_id;
	struct sde_plane_state *state;
	struct hfi_plane *phfi;
	enum sde_drm_lsr_layer_type layer_type;
	enum sde_drm_lsr_layer_lock_type lock_type;
	enum sde_drm_layer_gamma_type gamma_type;

	if (!plane || !prop_collector || !plane->base.state)
		return -EINVAL;

	phfi = to_hfi_plane(plane);
	state = (struct sde_plane_state *)plane->base.state;

	switch (hfi_prop) {
	case HFI_PROPERTY_LAYER_LSR_LAYER_TYPE:
		prop_id = HFI_PROPERTY_LAYER_LSR_LAYER_TYPE;
		layer_type = sde_plane_get_property(state, PLANE_PROP_REPROJ_RENDER_TYPE);
		return hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
			phfi->hfi_pipe_id, HFI_VAL_U32, &layer_type, sizeof(u32));
	case HFI_PROPERTY_LAYER_LSR_LOCK_TYPE:
		prop_id = HFI_PROPERTY_LAYER_LSR_LOCK_TYPE;
		lock_type = sde_plane_get_property(state, PLANE_PROP_REPROJ_LOCK_TYPE);
		return hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
			phfi->hfi_pipe_id, HFI_VAL_U32, &lock_type, sizeof(u32));
	case HFI_PROPERTY_LAYER_LSR_RENDER_POSE:
		prop_id = HFI_PROPERTY_LAYER_LSR_RENDER_POSE;
		return hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
			phfi->hfi_pipe_id, HFI_VAL_U32_ARRAY, &state->repro_sspp_cfg.render_pose,
			sizeof(struct hfi_lsr_render_pose));
	case HFI_PROPERTY_LAYER_LSR_REPROJ_PLANE_EQ:
		prop_id = HFI_PROPERTY_LAYER_LSR_REPROJ_PLANE_EQ;
		return hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
			phfi->hfi_pipe_id, HFI_VAL_U32_ARRAY, &state->repro_sspp_cfg.plane_equation,
			sizeof(struct hfi_lsr_plane_equation));
	case HFI_PROPERTY_LAYER_LSR_RENDER_FRUSTUM:
		prop_id = HFI_PROPERTY_LAYER_LSR_RENDER_FRUSTUM;
		return hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
			phfi->hfi_pipe_id, HFI_VAL_U32_ARRAY, &state->repro_sspp_cfg.render_frustum,
			sizeof(struct hfi_lsr_render_frustum));
	case HFI_PROPERTY_LAYER_GAMMA:
		prop_id = HFI_PROPERTY_LAYER_GAMMA;
		gamma_type = sde_plane_get_property(state, PLANE_PROP_REPROJ_LAYER_GAMMA);
		return hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
			phfi->hfi_pipe_id, HFI_VAL_U32, &gamma_type, sizeof(u32));
	default:
		HFI_ERROR_PLANE(phfi, "unsupported HFI property\n");
		return -EINVAL;
	}

	return 0;
}

int _hfi_add_csc_prop_helper(u32 hfi_prop, struct sde_plane *plane,
	struct sde_plane_state *pstate,
	struct hfi_util_u32_prop_helper *prop_collector)
{
	u32 prop_id;
	struct hfi_plane *phfi;
	struct drm_framebuffer *alpha_fb;
	struct sde_rect *bbox;
	struct sde_hw_pipe_cfg *alpha_pipe_cfg = NULL;
	struct hfi_buff *buff = NULL;
	int rc = 0;

	if (!plane || !prop_collector || !plane->base.state || !pstate)
		return -EINVAL;

	phfi = to_hfi_plane(plane);
	alpha_fb = pstate->repro_sspp_cfg.alpha_fb;
	bbox = &pstate->repro_sspp_cfg.bounding_box;

	switch (hfi_prop) {
	case HFI_PROPERTY_LAYER_LSR_IN_A_BUFFER:
		prop_id = HFI_PROPERTY_LAYER_LSR_IN_A_BUFFER;
		if (!alpha_fb)
			break;
		alpha_pipe_cfg = kzalloc(sizeof(*alpha_pipe_cfg), GFP_KERNEL);
		buff = kzalloc(sizeof(struct hfi_buff), GFP_KERNEL);
		if (!alpha_pipe_cfg || !buff) {
			SDE_ERROR("Failed to allocate memory alpha:%d buff:%d\n",
					!alpha_pipe_cfg, !buff);
			return -ENOMEM;
		}
		rc = sde_plane_get_scanout_info(plane, pstate,  alpha_fb, alpha_pipe_cfg);
		if (rc) {
			HFI_ERROR_PLANE(phfi, "Invalid alpha buffer\n");
			kfree(buff);
			kfree(alpha_pipe_cfg);
			break;
		}
		buff->size = alpha_pipe_cfg->layout.total_size;

		if (SDE_FORMAT_IS_UBWC(alpha_pipe_cfg->layout.format))
			buff->addr_l = alpha_pipe_cfg->layout.plane_addr[2];
		else
			buff->addr_l = alpha_pipe_cfg->layout.plane_addr[0];

		rc = hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
				phfi->hfi_pipe_id, HFI_VAL_U32_ARRAY, buff,
				sizeof(struct hfi_buff));
		kfree(buff);
		kfree(alpha_pipe_cfg);
		return rc;
	default:
		HFI_ERROR_PLANE(phfi, "unsupported HFI property\n");
		return -EINVAL;
	}

	return 0;
}

static bool _hfi_plane_is_prop_excluded_for_repro(u32 drm_prop, struct sde_plane *plane)
{
	int i;

	if (!sde_plane_is_lsr_enabled(plane))
		return false;

	if (!plane->catalog || !plane->catalog->repro_excluded_props ||
			!plane->catalog->repro_excluded_props[SDE_OBJ_PLANE] ||
			!plane->catalog->repro_excluded_props_count[SDE_OBJ_PLANE] ||
			(plane->catalog->repro_excluded_props_count[SDE_OBJ_PLANE]
			> HFI_PLANE_MAX_PROPS))
		return false;

	for (i = 0; i < plane->catalog->repro_excluded_props_count[SDE_OBJ_PLANE]; i++) {
		if (plane->catalog->repro_excluded_props[SDE_OBJ_PLANE][i] == drm_prop)
			return true;
	}
	return false;
}
#else
int _hfi_add_repro_prop_helper(u32 hfi_prop, struct sde_plane *plane,
	struct sde_plane_state *pstate,
	struct hfi_util_u32_prop_helper *prop_collector)
{
	return 0;
}

int _hfi_add_csc_prop_helper(u32 hfi_prop, struct sde_plane *plane,
	struct sde_plane_state *pstate,
	struct hfi_util_u32_prop_helper *prop_collector)
{
	return 0;
}

static bool _hfi_plane_is_prop_excluded_for_repro(u32 drm_prop, struct sde_plane *plane)
{
	return false;
}
#endif /* CONFIG_DRM_SDE_LSR */

static int _hfi_plane_add_drm_props(struct sde_plane *plane,
		struct sde_plane_state *pstate,
		struct hfi_util_u32_prop_helper *prop_collector)
{
	u32 prop_id, hfi_format, supported_rot, llcc_scid, adjusted_crtc_x;
	struct hfi_display_roi src, dst;
	struct drm_plane_state *state;
	struct hfi_plane *phfi;
	struct drm_framebuffer *fb;
	struct sde_format_extended fmt = {0,};
	struct dcp_cache_attr attr;
	struct sde_sc_cfg *sc_cfg;
	bool format_is_yuv;
	int rc = 0;

	if (!plane || !prop_collector || !pstate || !plane->catalog)
		return -EINVAL;

	state = &pstate->base;
	phfi = to_hfi_plane(plane);
	sc_cfg = &plane->catalog->sc_cfg[SDE_SYS_CACHE_DISP];

	fb = state->fb;
	fmt.fourcc_format = fb->format->format;
	fmt.modifier = fb->modifier;

	/*
	 * Adjust crtc_x back to global coordinates system in quadpipe usecase. This allows the
	 * driver to split the coordinates system in 2 and figure out left and right layout.
	 */
	adjusted_crtc_x = state->crtc_x;
	if (pstate->layout_offset > 0)
		adjusted_crtc_x += pstate->layout_offset;

	HFI_POPULATE_RECT(&src, state->src_x, state->src_y, state->src_w, state->src_h, true);
	HFI_POPULATE_RECT(&dst, adjusted_crtc_x, state->crtc_y, state->crtc_w, state->crtc_h,
			false);

	hfi_format = hfi_catalog_get_hfi_format(&fmt);

	if (hfi_format == U32_MAX) {
		HFI_ERROR_PLANE(phfi, "unsupported SDE format: 0x%x mod: 0x%llx\n",
				fmt.fourcc_format, fmt.modifier);
		return -EINVAL;
	}

	prop_id = HFI_PROPERTY_LAYER_SRC_ROI;
	hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
			phfi->hfi_pipe_id, HFI_VAL_U32_ARRAY, &src, sizeof(struct hfi_display_roi));

	prop_id = HFI_PROPERTY_LAYER_DEST_ROI;
	hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
			phfi->hfi_pipe_id, HFI_VAL_U32_ARRAY, &dst, sizeof(struct hfi_display_roi));

	prop_id = HFI_PROPERTY_DISPLAY_ATTACH_LAYER;
	hfi_util_u32_prop_helper_add_prop(prop_collector, prop_id, HFI_VAL_U32,
			&phfi->hfi_pipe_id, sizeof(u32));

	rc = sde_plane_get_scanout_info(plane, pstate,  fb, &plane->pipe_cfg);
	if (rc) {
		HFI_ERROR_PLANE(phfi, "Invalid frame buffer\n");
		return -EINVAL;
	}
	prop_id = HFI_PROPERTY_LAYER_SRC_ADDR;
	hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id, phfi->hfi_pipe_id,
			HFI_VAL_U32_ARRAY, &plane->pipe_cfg.layout.plane_addr[0],
			(sizeof(u32) * SDE_MAX_PLANES));

	prop_id = HFI_PROPERTY_LAYER_STRIDE;
	hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id, phfi->hfi_pipe_id,
			HFI_VAL_U32_ARRAY, &plane->pipe_cfg.layout.plane_pitch[0],
			(sizeof(u32) * SDE_MAX_PLANES));

	format_is_yuv = HFI_IS_YUV_FORMAT(hfi_format) ? true : false;
	_sde_plane_setup_csc(plane, pstate, format_is_yuv);

	prop_id = HFI_PROPERTY_LAYER_SRC_FORMAT;
	hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id, phfi->hfi_pipe_id,
			HFI_VAL_U32_ARRAY, &hfi_format, sizeof(u32));

	prop_id = HFI_PROPERTY_LAYER_ROTATION;
	supported_rot = hfi_rot_lookup(pstate->rotation);
	SDE_EVT32(prop_id, supported_rot, phfi->hfi_pipe_id);
	hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id, phfi->hfi_pipe_id,
			HFI_VAL_U32_ARRAY, &supported_rot, sizeof(u32));

	prop_id = HFI_PROPERTY_LAYER_CACHE_ATTR;
	attr.cache_op_type = HFI_CACHE_OP_TYPE_NONE;
	if (pstate->cache_state_prop && sc_cfg)
		attr.cache_state = HFI_CACHE_STATE_READ;
	else
		attr.cache_state = HFI_CACHE_STATE_DISABLE;
	hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id, phfi->hfi_pipe_id,
			HFI_VAL_U32_ARRAY, &attr, sizeof(struct dcp_cache_attr));

	if (attr.cache_state != HFI_CACHE_STATE_DISABLE) {
		prop_id = HFI_PROPERTY_LAYER_LLCC_SCID;
		if (!sc_cfg) {
			HFI_ERROR_PLANE(phfi, "Invalid sc_cfg\n");
			return -EINVAL;
		}
		llcc_scid = sc_cfg->llcc_scid;
		hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id, phfi->hfi_pipe_id,
			HFI_VAL_U32_ARRAY, &llcc_scid, sizeof(u32));
	}

	HFI_DEBUG_PLANE(phfi, "done adding drm props\n");

	return 0;
}

int _sde_hfi_add_base_prop_helper(u32 hfi_prop, struct sde_plane *plane,
		struct sde_plane_state *pstate,
		struct hfi_util_u32_prop_helper *prop_collector)
{
	u32 prop_id, temp_val, ret = 0;
	struct sde_plane_state *state;
	struct hfi_plane *phfi;
	struct drm_plane_state *plane_state;
	struct drm_framebuffer *fb;
	struct sde_hw_fmt_layout layout;
	struct hfi_display_roi src_roi;

	if (!plane || !prop_collector || !plane->base.state)
		return -EINVAL;

	phfi = to_hfi_plane(plane);
	state = (struct sde_plane_state *)plane->base.state;
	plane_state = &pstate->base;

	fb = plane_state->fb;
	ret = sde_format_populate_layout(pstate->aspace, fb, &layout);

	switch (hfi_prop) {
	case HFI_PROPERTY_LAYER_ZPOS:
		prop_id = HFI_PROPERTY_LAYER_ZPOS;
		temp_val = sde_plane_get_property(state, PLANE_PROP_ZPOS);
		return hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
				phfi->hfi_pipe_id, HFI_VAL_U32, &temp_val, sizeof(u32));
	case HFI_PROPERTY_LAYER_ALPHA:
		prop_id = HFI_PROPERTY_LAYER_ALPHA;
		temp_val =  sde_plane_get_property(state, PLANE_PROP_ALPHA);
		temp_val =  _hfi_plane_scale_alpha(plane->catalog, temp_val);
		return hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
				phfi->hfi_pipe_id, HFI_VAL_U32, &temp_val, sizeof(u32));
	case HFI_PROPERTY_LAYER_BG_ALPHA:
		if (sde_plane_property_is_dirty(plane_state, PLANE_PROP_BG_ALPHA) ||
				sde_plane_is_cac_enabled(pstate) ||
				SDE_FORMAT_IS_FSC(layout.format)) {
			prop_id = HFI_PROPERTY_LAYER_BG_ALPHA;
			temp_val =  sde_plane_get_property(state, PLANE_PROP_BG_ALPHA);
			temp_val =  _hfi_plane_scale_alpha(plane->catalog, temp_val);

			ret = hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
				phfi->hfi_pipe_id, HFI_VAL_U32, &temp_val, sizeof(u32));
		}
		return ret;
	case HFI_PROPERTY_LAYER_SRC_IMG_SIZE_W:
		prop_id = HFI_PROPERTY_LAYER_SRC_IMG_SIZE_W;
		temp_val = state->src_img_rec.w;
		return hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
				phfi->hfi_pipe_id, HFI_VAL_U32, &temp_val, sizeof(u32));
	case HFI_PROPERTY_LAYER_SRC_IMG_SIZE_H:
		prop_id = HFI_PROPERTY_LAYER_SRC_IMG_SIZE_H;
		temp_val = state->src_img_rec.h;
		return hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
				phfi->hfi_pipe_id, HFI_VAL_U32, &temp_val, sizeof(u32));
	case HFI_PROPERTY_LAYER_BLEND_TYPE:
		temp_val = sde_plane_get_property(pstate, PLANE_PROP_BLEND_OP);
		if (temp_val >= ARRAY_SIZE(hfi_plane_blend_ops_map)) {
			HFI_ERROR_PLANE(phfi, "unsupported blendop %d\n", temp_val);
			return -EINVAL;
		}
		temp_val = hfi_plane_blend_ops_map[temp_val];
		prop_id = HFI_PROPERTY_LAYER_BLEND_TYPE;
		return hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
				phfi->hfi_pipe_id, HFI_VAL_U32, &temp_val, sizeof(u32));
	case HFI_PROPERTY_LAYER_MULTIRECT_MODE:
		/*
		 * Note: We are not retrieving multirect mode directly via
		 * sde_plane_get_property. The multirect mode is not always set
		 * through plane properties. It can be automatically determined
		 * by the driver based on the layers received.
		 * Function sde_plane_validate_multirect_v2 determines and
		 * validates the multirect config mode and updates it in the
		 * plane state (pstate).
		 */
		temp_val = pstate->multirect_mode;

		if (temp_val == SDE_SSPP_MULTIRECT_NONE)
			break;

		if (temp_val >= ARRAY_SIZE(hfi_plane_multirect_mode_map)) {
			HFI_ERROR_PLANE(phfi, "unsupported multirect mode %d\n", temp_val);
			return -EINVAL;
		}

		temp_val = hfi_plane_multirect_mode_map[temp_val];
		prop_id = HFI_PROPERTY_LAYER_MULTIRECT_MODE;

		return hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
				phfi->hfi_pipe_id, HFI_VAL_U32, &temp_val, sizeof(u32));
	case HFI_PROPERTY_LAYER_SECURITY_POLICY:
		prop_id = HFI_PROPERTY_LAYER_SECURITY_POLICY;
		temp_val = sde_plane_get_property(state, PLANE_PROP_FB_TRANSLATION_MODE);

		switch (temp_val) {
		case SDE_DRM_FB_SEC:
			temp_val = (u32)HFI_LAYER_SECURITY_POLICY_SECURE;
			break;
		case SDE_DRM_FB_SEC_DIR_TRANS:
			temp_val = (u32)HFI_LAYER_SECURITY_POLICY_SECURE_DIR_TRANSLATION;
			break;
		default:
			temp_val = (u32)HFI_LAYER_SECURITY_POLICY_NON_SECURE;
		}

		return hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
				phfi->hfi_pipe_id, HFI_VAL_U32, &temp_val, sizeof(u32));
	case HFI_PROPERTY_LAYER_EXCLUSION_RECTANGLE_ROI:
		HFI_POPULATE_RECT(&src_roi, pstate->excl_rect.x, pstate->excl_rect.y,
			pstate->excl_rect.w, pstate->excl_rect.h, false);

		prop_id = HFI_PROPERTY_LAYER_EXCLUSION_RECTANGLE_ROI;

		return hfi_util_u32_prop_helper_add_prop_by_obj(prop_collector, prop_id,
				phfi->hfi_pipe_id, HFI_VAL_U32_ARRAY, &src_roi,
				sizeof(struct hfi_display_roi));
	default:
		HFI_ERROR_PLANE(phfi, "unsupported HFI property\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * hfi_plane_populate_custom_kv_setter_props:  this is for all basic payloads.
 * Collects all listed props into a linear memory and voids memcopy of value by value at adapeter
 */
static int _hfi_plane_set_props_base(struct sde_plane *plane, u32 disp_id,
		struct sde_plane_state *pstate, struct hfi_cmdbuf_t *cmd_buf)
{
	u32 drm_prop;
	int i, ret = 0;
	struct hfi_plane *phfi;
	struct drm_plane_state *plane_state;
	struct drm_framebuffer *fb;

	if (!plane || !pstate) {
		SDE_ERROR("invalid plane\n");
		return -EINVAL;
	}

	phfi = to_hfi_plane(plane);

	mutex_lock(&phfi->hfi_lock);
	hfi_util_u32_prop_helper_reset(phfi->base_props);

	ret = _hfi_plane_add_drm_props(plane, pstate, phfi->base_props);
	if (ret) {
		HFI_ERROR_PLANE(phfi, "failed to add drm-prop HFI properties\n");
		goto end;
	}

	/* append msm properties */
	for (i = 0; i < ARRAY_SIZE(hfi_plane_base_props_map); i++) {
		drm_prop = hfi_plane_base_props_map[i].drm_prop;

		if (_hfi_plane_is_prop_excluded_for_repro(drm_prop, plane)) {
			HFI_DEBUG_PLANE(phfi, "Unsupported property for LSR drm_prop:%x\n",
					drm_prop);
			continue;
		}

		_sde_hfi_add_base_prop_helper(hfi_plane_base_props_map[i].hfi_prop,
				 plane, pstate, phfi->base_props);
	}

	/* Program SRC_IMAGE_SIZE property for LSR usecase */
	if (sde_plane_is_lsr_enabled(plane)) {
		plane_state = &pstate->base;
		fb = plane_state->fb;
		ret = sde_plane_get_scanout_info(plane, pstate,  fb, &plane->pipe_cfg);
		if (ret) {
			HFI_ERROR_PLANE(phfi, "Invalid frame buffer\n");
			return -EINVAL;
		}
		hfi_util_u32_prop_helper_add_prop_by_obj(phfi->base_props,
			HFI_PROPERTY_LAYER_SRC_IMAGE_SIZE, phfi->hfi_pipe_id, HFI_VAL_U32_ARRAY,
			&plane->pipe_cfg.layout.total_size, sizeof(u32));
	}
	/* append repro and csc properties */
	if (plane->pipe_hw->cap->type == SSPP_TYPE_CSC) {
		for (i = 0; i < ARRAY_SIZE(hfi_plane_csc_props_map); i++) {
			drm_prop = hfi_plane_csc_props_map[i].drm_prop;

			ret = _hfi_add_csc_prop_helper(hfi_plane_csc_props_map[i].hfi_prop,
					plane, pstate, phfi->base_props);
			if (ret) {
				HFI_ERROR_PLANE(phfi, "Failed to add LSR CSC HFI properties\n");
				goto end;
			}
		}
	} else if (plane->pipe_hw->cap->type == SSPP_TYPE_REPRO) {
		for (i = 0; i < ARRAY_SIZE(hfi_plane_repro_props_map); i++) {
			drm_prop = hfi_plane_repro_props_map[i].drm_prop;

			ret = _hfi_add_repro_prop_helper(hfi_plane_repro_props_map[i].hfi_prop,
					plane, pstate, phfi->base_props);
			if (ret) {
				HFI_ERROR_PLANE(phfi, "Failed to add LSR REPRO HFI properties\n");
				goto end;
			}
		}
	}

	if (!hfi_util_u32_prop_helper_prop_count(phfi->base_props))
		goto end;

	/*
	 * Once all the key value pairs of properties are collected invoke adapter api
	 * to add all these property array as a single HFI Packet
	 */
	ret = hfi_adapter_add_set_property(cmd_buf->ctx,
			cmd_buf,
			HFI_COMMAND_DISPLAY_SET_PROPERTY,
			disp_id,
			HFI_PAYLOAD_TYPE_U32_ARRAY,
			hfi_util_u32_prop_helper_get_payload_addr(phfi->base_props),
			hfi_util_u32_prop_helper_get_size(phfi->base_props),
			HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (ret) {
		HFI_ERROR_PLANE(phfi, "failed to send HFI commands\n");
		goto end;
	}

	HFI_DEBUG_PLANE(phfi, "done adding all base props\n");
end:
	mutex_unlock(&phfi->hfi_lock);

	return ret;
}

/**
 * hfi_plane_populate_custom_kv_setter_props:  this is for large payloads.
 * Collects all listed props to provide as key-value pairs and adapter does memcopy
 */
int hfi_plane_populate_custom_kv_setter_props(struct sde_plane *plane, u32 disp_id,
		struct sde_plane_state *pstate, struct hfi_cmdbuf_t *cmd_buf)
{
	int i, ret = 0;
	struct kv_prop_lookup *setter;
	struct hfi_plane *phfi;
	u32 kv_count;

	if (!plane) {
		SDE_ERROR("invalid plane\n");
		return -EINVAL;
	}

	phfi = to_hfi_plane(plane);

	mutex_lock(&phfi->hfi_lock);
	hfi_util_kv_helper_reset(phfi->kv_props);

	for (i = 0; i < ARRAY_SIZE(hfi_plane_custom_kv_props_map); i++) {
		setter = &hfi_plane_custom_kv_props_map[i];
		if (!sde_plane_property_is_dirty(&pstate->base, setter->drm_prop))
			continue;

		if (setter->add_hfi_prop)
			setter->add_hfi_prop(setter->hfi_prop, plane, pstate, cmd_buf);
	}

	kv_count = hfi_util_kv_helper_get_count(phfi->kv_props);
	if (!kv_count)
		goto end;

	ret = hfi_adapter_add_prop_array(cmd_buf->ctx,
			cmd_buf,
			HFI_COMMAND_DISPLAY_SET_PROPERTY,
			disp_id,
			HFI_PAYLOAD_TYPE_U32_ARRAY,
			hfi_util_kv_helper_get_payload_addr(phfi->kv_props),
			kv_count * sizeof(struct hfi_kv_pairs),
			kv_count);
	if (ret) {
		HFI_ERROR_PLANE(phfi, "failed to send HFI commands\n");
		goto end;
	}
end:
	HFI_DEBUG_PLANE(phfi, "adding kv-props count%d\n", kv_count);

	mutex_unlock(&phfi->hfi_lock);

	return ret;
}

int _hfi_plane_populate_props(struct hfi_cmdbuf_t *cmd_buf, u32 disp_id,
		struct sde_plane *plane, struct sde_plane_state *pstate)
{
	int ret = 0;
	struct hfi_plane *phfi;

	if (!plane) {
		SDE_ERROR("invalid plane\n");
		return -EINVAL;
	}

	phfi = to_hfi_plane(plane);

	ret = _hfi_plane_set_props_base(plane, disp_id, pstate, cmd_buf);
	if (ret) {
		HFI_ERROR_PLANE(phfi, "failed to add base props packet\n");
		return ret;
	}

	ret = hfi_plane_populate_custom_kv_setter_props(plane, disp_id, pstate, cmd_buf);
	if (ret) {
		HFI_ERROR_PLANE(phfi, "failed to add kv props packet\n");
		return ret;
	}

	return ret;
}

void hfi_plane_disable(struct hfi_cmdbuf_t *cmd_buf, u32 disp_id, struct sde_plane *plane,
	bool use_lock)
{
	u32 prop_id;
	int ret;
	struct hfi_plane *phfi;
	struct drm_plane *drm_plane;

	if (!plane) {
		SDE_ERROR("invalid plane args\n");
		return;
	}

	phfi = to_hfi_plane(plane);
	drm_plane = &(plane->base);

	if (use_lock)
		mutex_lock(&phfi->hfi_lock);

	hfi_util_u32_prop_helper_reset(phfi->base_props);

	prop_id = HFI_PROPERTY_DISPLAY_DETACH_LAYER;
	hfi_util_u32_prop_helper_add_prop(phfi->base_props, prop_id, HFI_VAL_U32_ARRAY,
			&phfi->hfi_pipe_id, sizeof(u32));

	ret = hfi_adapter_add_set_property(cmd_buf->ctx,
			cmd_buf,
			HFI_COMMAND_DISPLAY_SET_PROPERTY,
			disp_id,
			HFI_PAYLOAD_TYPE_U32_ARRAY,
			hfi_util_u32_prop_helper_get_payload_addr(phfi->base_props),
			hfi_util_u32_prop_helper_get_size(phfi->base_props),
			HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (ret) {
		HFI_ERROR_PLANE(phfi, "failed to send HFI commands\n");
		goto end;
	}

	HFI_DEBUG_PLANE(phfi, "adding detatch layer\n");
end:
	if (use_lock)
		mutex_unlock(&phfi->hfi_lock);
}

static void hfi_plane_destroy(struct sde_plane *plane)
{
	struct hfi_plane *phfi;

	if (!plane) {
		SDE_ERROR("invalid plane\n");
		return;
	}

	phfi = to_hfi_plane(plane);

	kfree(phfi->base_props);
	kfree(phfi->color_props);
	kfree(phfi->kv_props);

	mutex_destroy(&phfi->hfi_lock);

	kfree(phfi);
}

static int hfi_plane_add_hfi_cmds(struct hfi_cmdbuf_t *cmd_buf, u32 disp_id,
		struct sde_plane *plane, struct sde_plane_state *pstate)
{
	int ret = 0;

	if (!plane || !cmd_buf) {
		SDE_ERROR("invalid args\n");
		return -EINVAL;
	}

	if (!sde_plane_enabled(&pstate->base))
		hfi_plane_disable(cmd_buf, disp_id, plane, true);
	else
		_hfi_plane_populate_props(cmd_buf, disp_id, plane, pstate);

	return ret;
}

static int hfi_plane_atomic_update(struct sde_plane *plane, struct sde_plane_state *old_pstate)
{
	int ret = 0;
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	struct hfi_plane *phfi = NULL;
	struct sde_plane_state *pstate;
	struct hfi_kms *hfi_kms;
	struct drm_crtc *crtc = NULL;
	struct drm_crtc_state *drm_state = NULL;
	struct sde_crtc_state *cstate = NULL;
	u32 disp_id;

	if (!plane || !(plane->base.state))  {
		SDE_ERROR("invalid args\n");
		return -EINVAL;
	}

	hfi_kms = sde_plane_get_kms(plane);
	if (!hfi_kms)
		return -EINVAL;

	phfi = to_hfi_plane(plane);
	pstate = to_sde_plane_state(plane->base.state);

	if (sde_plane_enabled(&pstate->base))
		crtc = plane->base.state->crtc;
	else if (old_pstate && sde_plane_enabled(&old_pstate->base))
		crtc = old_pstate->base.crtc;
	else
		return -EINVAL;

	drm_state = crtc->state;
	cstate = to_sde_crtc_state(drm_state);
	pstate->cache_state_prop = sde_crtc_get_property(cstate, CRTC_PROP_CACHE_STATE);

	disp_id = hfi_crtc_get_display_id(crtc, crtc->state);
	if (disp_id == U32_MAX) {
		SDE_ERROR("invalid display id\n");
		return -EINVAL;
	}

	cmd_buf = hfi_kms_get_cmd_buf(hfi_kms, disp_id, HFI_CMDBUF_TYPE_ATOMIC_COMMIT);
	if (!cmd_buf) {
		SDE_ERROR("failed to get cmd_buf for crtc:%d disp_id:%d\n",
				DRMID(crtc), disp_id);
		return -EINVAL;
	}

	ret = hfi_plane_add_hfi_cmds(cmd_buf, disp_id, plane, pstate);
	if (ret) {
		SDE_ERROR("failed to add hfi cmds\n");
		return ret;
	}

	return ret;
}

static int hfi_plane_post_init(struct sde_plane *plane)
{
	int pipe;
	struct hfi_plane *phfi = to_hfi_plane(plane);
	struct hfi_kms *hfi_kms;

	hfi_kms = sde_plane_get_kms(plane);
	if (!hfi_kms)
		return -EINVAL;

	if (SDE_SSPP_VALID_VIG(plane->pipe))
		pipe = plane->pipe - SSPP_VIG0;
	else if (SDE_SSPP_VALID_DMA(plane->pipe))
		pipe = plane->pipe - SSPP_DMA0;
	else if (SDE_SSPP_VALID_CSC(plane->pipe))
		pipe = plane->pipe - SSPP_CSC0;
	else if (SDE_SSPP_VALID_REPRO(plane->pipe))
		pipe = plane->pipe - SSPP_REPRO0;
	else
		return -EINVAL;

	return hfi_kms_get_plane_indices(hfi_kms, SDE_SSPP_VALID_VIG(plane->pipe),
				pipe, plane->is_virtual, &(phfi->hfi_pipe_id),
				SDE_SSPP_VALID_CSC(plane->pipe),
				SDE_SSPP_VALID_REPRO(plane->pipe));
}

static void _sde_plane_hal_funcs_install(struct sde_plane *plane)
{
	if (!plane) {
		SDE_ERROR("invalid args\n");
		return;
	}

	plane->hal_ops.destroy[MSM_DISP_OP_HFI] = hfi_plane_destroy;
	plane->hal_ops.atomic_update[MSM_DISP_OP_HFI] = hfi_plane_atomic_update;
	plane->hal_ops.post_init[MSM_DISP_OP_HFI] = hfi_plane_post_init;
}

int hfi_plane_init(uint32_t pipe_id, struct sde_plane *pdpu)
{
	struct hfi_plane *plane = NULL;
	int ret;

	if (!pdpu) {
		SDE_ERROR("invalid plane arg\n");
		return -EINVAL;
	}

	plane = kvzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane) {
		SDE_ERROR("failed to allocate memory for hfi plane\n");
		return -ENOMEM;
	}

	_sde_plane_hal_funcs_install(pdpu);

	mutex_init(&plane->hfi_lock);

	/* alloc util storage handlers for hfi props */
	plane->kv_props = hfi_util_kv_helper_alloc(HFI_PLANE_MAX_PROPS);
	if (IS_ERR(plane->kv_props)) {
		SDE_ERROR("failed to ate memory for kv prop collctr\n");
		ret = -ENOMEM;
		goto free_plane;
	}

	plane->base_props = hfi_util_u32_prop_helper_alloc(HFI_PLANE_BASE_PROP_MAX_SIZE);
	if (IS_ERR(plane->base_props)) {
		SDE_ERROR("failed to allocate memory for base prop collector\n");
		ret = -ENOMEM;
		goto free_kv;
	}

	plane->color_props = hfi_util_u32_prop_helper_alloc(HFI_PLANE_BASE_PROP_MAX_SIZE);
	if (IS_ERR(plane->color_props)) {
		SDE_ERROR("failed to allocate memory for color prop collector\n");
		ret = -ENOMEM;
		goto free_kv;
	}

	plane->sde_base = pdpu;
	pdpu->hfi_plane = plane;
	return 0;

free_kv:
	kfree(plane->kv_props);
	kfree(plane->base_props);
free_plane:
	mutex_destroy(&plane->hfi_lock);
	kfree(plane);

	return -ENOMEM;
}

struct hfi_cmdbuf_t *hfi_plane_get_cmd_buf(struct drm_plane *plane)
{
	u32 disp_id = 0;
	struct drm_crtc *drm_crtc = NULL;
	struct hfi_cmdbuf_t *cmd_buf = NULL;
	struct hfi_kms *hfi_kms = NULL;
	struct msm_drm_private *priv = NULL;

	if (plane && plane->dev && plane->dev->dev_private) {
		priv = plane->dev->dev_private;
		hfi_kms = ((priv && priv->kms) ?
				to_hfi_kms(to_sde_kms(priv->kms)) : NULL);
	}
	if (!hfi_kms) {
		SDE_ERROR("invalid hfi_kms\n");
		return NULL;
	}

	if (plane->state && plane->state->crtc) {
		drm_crtc = plane->state->crtc;
		if (!drm_crtc) {
			SDE_ERROR("invalid drm_crtc\n");
			return NULL;
		}
	}
	disp_id = hfi_crtc_get_display_id(drm_crtc, drm_crtc->state);
	if (disp_id == U32_MAX) {
		SDE_ERROR("invalid display id\n");
		return NULL;
	}

	cmd_buf = hfi_kms_get_cmd_buf(hfi_kms, disp_id, HFI_CMDBUF_TYPE_ATOMIC_COMMIT);
	return cmd_buf;
}
