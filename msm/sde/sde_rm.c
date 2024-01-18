// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s] " fmt, __func__
#include "sde_kms.h"
#include "sde_hw_lm.h"
#include "sde_hw_ctl.h"
#include "sde_hw_cdm.h"
#include "sde_hw_dspp.h"
#include "sde_hw_ds.h"
#include "sde_hw_pingpong.h"
#include "sde_hw_intf.h"
#include "sde_hw_wb.h"
#include "sde_encoder.h"
#include "sde_connector.h"
#include "sde_hw_dsc.h"
#include "sde_hw_vdc.h"
#include "sde_crtc.h"
#include "sde_hw_qdss.h"
#include "sde_vbif.h"

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
#define RESERVED_BY_OTHER(h, e) \
	((h)->enc_id && ((h)->enc_id != (e)))

#define RESERVED_BY_CURRENT(h, e) \
	((h)->enc_id && ((h)->enc_id == (e)))
#else
#define RESERVED_BY_OTHER(h, r) \
	(((h)->rsvp && ((h)->rsvp->enc_id != (r)->enc_id)) ||\
		((h)->rsvp_nxt && ((h)->rsvp_nxt->enc_id != (r)->enc_id)))

#define RESERVED_BY_CURRENT(h, r) \
	(((h)->rsvp && ((h)->rsvp->enc_id == (r)->enc_id)))

#endif

#define RM_RQ_LOCK(r) ((r)->top_ctrl & BIT(SDE_RM_TOPCTL_RESERVE_LOCK))
#define RM_RQ_CLEAR(r) ((r)->top_ctrl & BIT(SDE_RM_TOPCTL_RESERVE_CLEAR))
#define RM_RQ_DSPP(r) ((r)->top_ctrl & BIT(SDE_RM_TOPCTL_DSPP))
#define RM_RQ_DS(r) ((r)->top_ctrl & BIT(SDE_RM_TOPCTL_DS))
#define RM_RQ_CWB(r) ((r)->top_ctrl & BIT(SDE_RM_TOPCTL_CWB))
#define RM_RQ_DCWB(r) ((r)->top_ctrl & BIT(SDE_RM_TOPCTL_DCWB))
#define RM_IS_TOPOLOGY_MATCH(t, r) ((t).num_lm == (r).num_lm && \
				(t).num_comp_enc == (r).num_enc && \
				(t).num_intf == (r).num_intf && \
				(t).comp_type == (r).comp_type)

#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
#define IS_COMPATIBLE_PP_DSC(p, d) (p % 2 == d % 2)
#endif

/* ~one vsync poll time for rsvp_nxt to cleared by modeset from commit thread */
#define RM_NXT_CLEAR_POLL_TIMEOUT_US 33000

/**
 * toplogy information to be used when ctl path version does not
 * support driving more than one interface per ctl_path
 */
static const struct sde_rm_topology_def g_top_table[SDE_RM_TOPOLOGY_MAX] = {
	{   SDE_RM_TOPOLOGY_NONE,                 0, 0, 0, 0, false,
			MSM_DISPLAY_COMPRESSION_NONE },
	{   SDE_RM_TOPOLOGY_SINGLEPIPE,           1, 0, 1, 1, false,
			MSM_DISPLAY_COMPRESSION_NONE },
	{   SDE_RM_TOPOLOGY_SINGLEPIPE_DSC,       1, 1, 1, 1, false,
			MSM_DISPLAY_COMPRESSION_DSC },
	{   SDE_RM_TOPOLOGY_DUALPIPE,             2, 0, 2, 2, true,
			MSM_DISPLAY_COMPRESSION_NONE },
	{   SDE_RM_TOPOLOGY_DUALPIPE_DSC,         2, 2, 2, 2, true,
			MSM_DISPLAY_COMPRESSION_DSC },
	{   SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE,     2, 0, 1, 1, false,
			MSM_DISPLAY_COMPRESSION_NONE },
	{   SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE_DSC, 2, 1, 1, 1, false,
			MSM_DISPLAY_COMPRESSION_DSC },
	{   SDE_RM_TOPOLOGY_DUALPIPE_DSCMERGE,    2, 2, 1, 1, false,
			MSM_DISPLAY_COMPRESSION_DSC },
	{   SDE_RM_TOPOLOGY_PPSPLIT,              1, 0, 2, 1, true,
			MSM_DISPLAY_COMPRESSION_NONE },
};

/**
 * topology information to be used when the ctl path version
 * is SDE_CTL_CFG_VERSION_1_0_0
 */
static const struct sde_rm_topology_def g_top_table_v1[SDE_RM_TOPOLOGY_MAX] = {
	{   SDE_RM_TOPOLOGY_NONE,                 0, 0, 0, 0, false,
			MSM_DISPLAY_COMPRESSION_NONE },
	{   SDE_RM_TOPOLOGY_SINGLEPIPE,           1, 0, 1, 1, false,
			MSM_DISPLAY_COMPRESSION_NONE },
	{   SDE_RM_TOPOLOGY_SINGLEPIPE_DSC,       1, 1, 1, 1, false,
			MSM_DISPLAY_COMPRESSION_DSC },
	{   SDE_RM_TOPOLOGY_SINGLEPIPE_VDC,       1, 1, 1, 1, false,
			MSM_DISPLAY_COMPRESSION_VDC },
	{   SDE_RM_TOPOLOGY_DUALPIPE,             2, 0, 2, 1, false,
			MSM_DISPLAY_COMPRESSION_NONE },
	{   SDE_RM_TOPOLOGY_DUALPIPE_DSC,         2, 2, 2, 1, false,
			MSM_DISPLAY_COMPRESSION_DSC },
	{   SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE,     2, 0, 1, 1, false,
			MSM_DISPLAY_COMPRESSION_NONE },
	{   SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE_DSC, 2, 1, 1, 1, false,
			MSM_DISPLAY_COMPRESSION_DSC },
	{   SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE_VDC, 2, 1, 1, 1, false,
			MSM_DISPLAY_COMPRESSION_VDC },
	{   SDE_RM_TOPOLOGY_DUALPIPE_DSCMERGE,    2, 2, 1, 1, false,
			MSM_DISPLAY_COMPRESSION_DSC },
	{   SDE_RM_TOPOLOGY_PPSPLIT,              1, 0, 2, 1, false,
			MSM_DISPLAY_COMPRESSION_NONE },
	{   SDE_RM_TOPOLOGY_QUADPIPE_3DMERGE,     4, 0, 2, 1, false,
			MSM_DISPLAY_COMPRESSION_NONE },
	{   SDE_RM_TOPOLOGY_QUADPIPE_3DMERGE_DSC, 4, 3, 2, 1, false,
			MSM_DISPLAY_COMPRESSION_DSC },
	{   SDE_RM_TOPOLOGY_QUADPIPE_DSCMERGE,    4, 4, 2, 1, false,
			MSM_DISPLAY_COMPRESSION_DSC },
	{   SDE_RM_TOPOLOGY_QUADPIPE_DSC4HSMERGE, 4, 4, 1, 1, false,
			MSM_DISPLAY_COMPRESSION_DSC },
};

char sde_hw_blk_str[SDE_HW_BLK_MAX][SDE_HW_BLK_NAME_LEN] = {
	"top",
	"sspp",
	"lm",
	"dspp",
	"ds",
	"ctl",
	"cdm",
	"pingpong",
	"intf",
	"wb",
	"dsc",
	"vdc",
	"merge_3d",
	"qdss",
};

/**
 * struct sde_rm_requirements - Reservation requirements parameter bundle
 * @top_ctrl:  topology control preference from kernel client
 * @top:       selected topology for the display
 * @hw_res:	   Hardware resources required as reported by the encoders
 * @conn_lm_mask:  preferred LM mask of cwb requested display
 */
struct sde_rm_requirements {
	uint64_t top_ctrl;
	const struct sde_rm_topology_def *topology;
	struct sde_encoder_hw_resources hw_res;
	u32 conn_lm_mask;
};

#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
/**
 * struct sde_rm_rsvp - Use Case Reservation tagging structure
 *	Used to tag HW blocks as reserved by a CRTC->Encoder->Connector chain
 *	By using as a tag, rather than lists of pointers to HW blocks used
 *	we can avoid some list management since we don't know how many blocks
 *	of each type a given use case may require.
 * @list:	List head for list of all reservations
 * @seq:	Global RSVP sequence number for debugging, especially for
 *		differentiating differenct allocations for same encoder.
 * @enc_id:	Reservations are tracked by Encoder DRM object ID.
 *		CRTCs may be connected to multiple Encoders.
 *		An encoder or connector id identifies the display path.
 * @topology:	DRM<->HW topology use case
 * @pending:	True for pending rsvp-nxt, cleared when the rsvp is committed
 */
struct sde_rm_rsvp {
	struct list_head list;
	uint32_t seq;
	uint32_t enc_id;
	enum sde_rm_topology_name topology;
	bool pending;
};
#endif

/**
 * struct sde_rm_hw_blk - hardware block tracking list member
 * @list:	List head for list of all hardware blocks tracking items
 * @enc_id:	Reservations are tracked by Encoder DRM object ID.
 *		CRTCs may be connected to multiple Encoders.
 *		An encoder or connector id identifies the display path.
 * @ext_hw:	Flag for external created HW block
 * @rsvp:	Pointer to use case reservation if reserved by a client
 * @rsvp_nxt:	Temporary pointer used during reservation to the incoming
 *		request. Will be swapped into rsvp if proposal is accepted
 * @type:	Type of hardware block this structure tracks
 * @id:		Hardware ID number, within it's own space, ie. LM_X
 * @catalog:	Pointer to the hardware catalog entry for this block
 * @hw:		Pointer to the hardware register access object for this block
 */
struct sde_rm_hw_blk {
	struct list_head list;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	uint32_t enc_id;
	bool ext_hw;
#else
	struct sde_rm_rsvp *rsvp;
	struct sde_rm_rsvp *rsvp_nxt;
#endif
	enum sde_hw_blk_type type;
	uint32_t id;
	struct sde_hw_blk *hw;
};

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
/**
 * struct sde_rm_state - SDE dynamic hardware resource manager state
 * @base: private state base
 * @rm: sde_rm handle
 * @rsvps: list of hardware reservations by each crtc->encoder->connector
 * @hw_blks: array of lists of hardware resources present in the system, one
 *	list per type of hardware block
 */
struct sde_rm_state {
	struct drm_private_state base;
	struct list_head hw_blks[SDE_HW_BLK_MAX];
};
#endif

/**
 * sde_rm_dbg_rsvp_stage - enum of steps in making reservation for event logging
 */
enum sde_rm_dbg_rsvp_stage {
	SDE_RM_STAGE_BEGIN,
	SDE_RM_STAGE_AFTER_CLEAR,
	SDE_RM_STAGE_AFTER_RSVPNEXT,
	SDE_RM_STAGE_FINAL
};

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static void _sde_rm_inc_resource_info_lm(struct sde_rm_state *state,
 	struct msm_resource_caps_info *avail_res,
 	struct sde_rm_hw_blk *blk)
#else
static void _sde_rm_inc_resource_info_lm(struct sde_rm *rm,
	struct msm_resource_caps_info *avail_res,
	struct sde_rm_hw_blk *blk)
#endif
{
	struct sde_rm_hw_blk *blk2;
	const struct sde_lm_cfg *lm_cfg, *lm_cfg2;

	lm_cfg = to_sde_hw_mixer(blk->hw)->cap;

	/* Do not track & expose dummy mixers */
	if (lm_cfg->dummy_mixer)
		return;

	avail_res->num_lm++;

	/* Check for 3d muxes by comparing paired lms */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	list_for_each_entry(blk2, &state->hw_blks[SDE_HW_BLK_LM], list) {
#else
	list_for_each_entry(blk2, &rm->hw_blks[SDE_HW_BLK_LM], list) {
#endif
		lm_cfg2 = to_sde_hw_mixer(blk2->hw)->cap;
		/*
		 * If lm2 is free, or
		 * lm1 & lm2 reserved by same enc, check mask
		 */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		if ((!blk2->enc_id || (blk->enc_id &&
				blk2->enc_id == blk->enc_id
#else
		if ((!blk2->rsvp || (blk->rsvp &&
				blk2->rsvp->enc_id == blk->rsvp->enc_id
#endif
				&& lm_cfg->id > lm_cfg2->id)) &&
				test_bit(lm_cfg->id, &lm_cfg2->lm_pair_mask))
			avail_res->num_3dmux++;
	}
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static void _sde_rm_dec_resource_info_lm(struct sde_rm_state *state,
 	struct msm_resource_caps_info *avail_res,
 	struct sde_rm_hw_blk *blk)
#else
static void _sde_rm_dec_resource_info_lm(struct sde_rm *rm,
	struct msm_resource_caps_info *avail_res,
	struct sde_rm_hw_blk *blk)
#endif
{
	struct sde_rm_hw_blk *blk2;
	const struct sde_lm_cfg *lm_cfg, *lm_cfg2;

	lm_cfg = to_sde_hw_mixer(blk->hw)->cap;

	/* Do not track & expose dummy mixers */
	if (lm_cfg->dummy_mixer)
		return;

	avail_res->num_lm--;

	/* Check for 3d muxes by comparing paired lms */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	list_for_each_entry(blk2, &state->hw_blks[SDE_HW_BLK_LM], list) {
#else
	list_for_each_entry(blk2, &rm->hw_blks[SDE_HW_BLK_LM], list) {
#endif
		lm_cfg2 = to_sde_hw_mixer(blk2->hw)->cap;
		/* If lm2 is free and lm1 is now being reserved */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		if (!blk2->enc_id &&
#else
		if (!blk2->rsvp &&
#endif
				test_bit(lm_cfg->id, &lm_cfg2->lm_pair_mask))
			avail_res->num_3dmux--;
	}
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static void _sde_rm_inc_resource_info(struct sde_rm_state *state,
 		struct msm_resource_caps_info *avail_res,
 		struct sde_rm_hw_blk *blk)
#else
static void _sde_rm_inc_resource_info(struct sde_rm *rm,
		struct msm_resource_caps_info *avail_res,
		struct sde_rm_hw_blk *blk)
#endif
{
	enum sde_hw_blk_type type = blk->type;

	if (type == SDE_HW_BLK_LM)
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		_sde_rm_inc_resource_info_lm(state, avail_res, blk);
#else
		_sde_rm_inc_resource_info_lm(rm, avail_res, blk);
#endif
	else if (type == SDE_HW_BLK_CTL)
		avail_res->num_ctl++;
	else if (type == SDE_HW_BLK_DSC)
		avail_res->num_dsc++;
	else if (type == SDE_HW_BLK_VDC)
		avail_res->num_vdc++;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static void _sde_rm_dec_resource_info(struct sde_rm_state *state,
 		struct msm_resource_caps_info *avail_res,
 		struct sde_rm_hw_blk *blk)
#else
static void _sde_rm_dec_resource_info(struct sde_rm *rm,
		struct msm_resource_caps_info *avail_res,
		struct sde_rm_hw_blk *blk)
#endif
{
	enum sde_hw_blk_type type = blk->type;

	if (type == SDE_HW_BLK_LM)
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		_sde_rm_dec_resource_info_lm(state, avail_res, blk);
#else
		_sde_rm_dec_resource_info_lm(rm, avail_res, blk);
#endif
	else if (type == SDE_HW_BLK_CTL)
		avail_res->num_ctl--;
	else if (type == SDE_HW_BLK_DSC)
		avail_res->num_dsc--;
	else if (type == SDE_HW_BLK_VDC)
		avail_res->num_vdc--;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
#define to_sde_rm_priv_state(x) \
		container_of((x), struct sde_rm_state, base)

void sde_rm_dec_resource_info(struct sde_rm *rm)
{
	struct sde_rm_hw_blk *blk;
	enum sde_hw_blk_type type;
	struct sde_rm_state *state;

	state = to_sde_rm_priv_state(rm->obj.state);

	for (type = 0; type < SDE_HW_BLK_MAX; type++) {
		list_for_each_entry(blk, &state->hw_blks[type], list) {
			if (blk->enc_id)
				_sde_rm_dec_resource_info(state,
						&rm->avail_res, blk);
		}
	}
}
#endif

void sde_rm_get_resource_info(struct sde_rm *rm,
		struct drm_encoder *drm_enc,
		struct msm_resource_caps_info *avail_res)
{
	struct sde_rm_hw_blk *blk;
	enum sde_hw_blk_type type;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_rm_state *state;
#else
	struct sde_rm_rsvp rsvp;
#endif
	const struct sde_lm_cfg *lm_cfg;
	bool is_built_in, is_pref;
	u32 lm_pref = (BIT(SDE_DISP_PRIMARY_PREF) | BIT(SDE_DISP_SECONDARY_PREF));

	/* Get all currently available resources */
	memcpy(avail_res, &rm->avail_res,
			sizeof(rm->avail_res));

	if (!drm_enc)
		return;

	is_built_in = sde_encoder_is_built_in_display(drm_enc);

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	state = to_sde_rm_priv_state(rm->obj.state);
#else
	rsvp.enc_id = drm_enc->base.id;
#endif

	for (type = 0; type < SDE_HW_BLK_MAX; type++) {
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		list_for_each_entry(blk, &state->hw_blks[type], list) {
#else
		list_for_each_entry(blk, &rm->hw_blks[type], list) {
#endif
			/* Add back resources allocated to the given encoder */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
			if (blk->enc_id == drm_enc->base.id)
				_sde_rm_inc_resource_info(state, avail_res, blk);
#else
			if (blk->rsvp && blk->rsvp->enc_id == rsvp.enc_id)
				_sde_rm_inc_resource_info(rm, avail_res, blk);
#endif

			/**
			 * Remove unallocated preferred lms that cannot reserved
			 * by non built-in displays.
			 */
			if (type == SDE_HW_BLK_LM) {
				lm_cfg = to_sde_hw_mixer(blk->hw)->cap;
				is_pref = lm_cfg->features & lm_pref;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
				if (!blk->enc_id && !is_built_in && is_pref)
					_sde_rm_dec_resource_info(state, avail_res, blk);
#else
				if (!blk->rsvp && !is_built_in && is_pref)
					_sde_rm_dec_resource_info(rm, avail_res, blk);
#endif
			}
		}
	}
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static void _sde_rm_print_rsvps(
		struct sde_rm_state *state,
		enum sde_rm_dbg_rsvp_stage stage)
#else
static void _sde_rm_print_rsvps(
		struct sde_rm *rm,
		enum sde_rm_dbg_rsvp_stage stage)
#endif
{
#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_rm_rsvp *rsvp;
#endif
	struct sde_rm_hw_blk *blk;
	enum sde_hw_blk_type type;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	SDE_DEBUG("%d state=%pK\n", stage, state);
#else
	SDE_DEBUG("%d\n", stage);
#endif

#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
	list_for_each_entry(rsvp, &rm->rsvps, list) {
		SDE_DEBUG("%d rsvp%s[s%ue%u] topology %d\n", stage, rsvp->pending ? "_nxt" : "",
				rsvp->seq, rsvp->enc_id, rsvp->topology);
		SDE_EVT32(stage, rsvp->seq, rsvp->enc_id, rsvp->topology, rsvp->pending);
	}
#endif

	for (type = 0; type < SDE_HW_BLK_MAX; type++) {
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		list_for_each_entry(blk, &state->hw_blks[type], list) {
			if (!blk->enc_id)
#else
		list_for_each_entry(blk, &rm->hw_blks[type], list) {
			if (!blk->rsvp && !blk->rsvp_nxt)
#endif
				continue;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
			SDE_DEBUG("%d rsvp[e%u] %d %d\n", stage,
				blk->enc_id,
#else
			SDE_DEBUG("%d rsvp[s%ue%u->s%ue%u] %d %d\n", stage,
				(blk->rsvp) ? blk->rsvp->seq : 0,
				(blk->rsvp) ? blk->rsvp->enc_id : 0,
				(blk->rsvp_nxt) ? blk->rsvp_nxt->seq : 0,
				(blk->rsvp_nxt) ? blk->rsvp_nxt->enc_id : 0,
#endif
				blk->type, blk->id);

			SDE_EVT32(stage,
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
				blk->enc_id,
#else
				(blk->rsvp) ? blk->rsvp->seq : 0,
				(blk->rsvp) ? blk->rsvp->enc_id : 0,
				(blk->rsvp_nxt) ? blk->rsvp_nxt->seq : 0,
				(blk->rsvp_nxt) ? blk->rsvp_nxt->enc_id : 0,
#endif
				blk->type, blk->id);
		}
	}
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static void _sde_rm_print_rsvps_by_type(
		struct sde_rm_state *state,
		enum sde_hw_blk_type type)
#else
static void _sde_rm_print_rsvps_by_type(
		struct sde_rm *rm,
		enum sde_hw_blk_type type)
#endif
{
	struct sde_rm_hw_blk *blk;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	list_for_each_entry(blk, &state->hw_blks[type], list) {
		if (!blk->enc_id)
#else
	list_for_each_entry(blk, &rm->hw_blks[type], list) {
		if (!blk->rsvp && !blk->rsvp_nxt)
#endif
			continue;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		SDE_DEBUG("rsvp[e%u] %d %d\n",
			blk->enc_id,
			blk->type, blk->id);
#else
		SDE_ERROR("rsvp[s%ue%u->s%ue%u] %d %d\n",
			(blk->rsvp) ? blk->rsvp->seq : 0,
			(blk->rsvp) ? blk->rsvp->enc_id : 0,
			(blk->rsvp_nxt) ? blk->rsvp_nxt->seq : 0,
			(blk->rsvp_nxt) ? blk->rsvp_nxt->enc_id : 0,
			blk->type, blk->id);
#endif

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		SDE_EVT32(blk->enc_id,
			blk->type, blk->id);
#else
		SDE_EVT32((blk->rsvp) ? blk->rsvp->seq : 0,
			(blk->rsvp) ? blk->rsvp->enc_id : 0,
			(blk->rsvp_nxt) ? blk->rsvp_nxt->seq : 0,
			(blk->rsvp_nxt) ? blk->rsvp_nxt->enc_id : 0,
			blk->type, blk->id);
#endif
	}
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static void sde_rm_destroy_state(struct drm_private_obj *obj,
		struct drm_private_state *base_state)
{
	struct sde_rm_state *state = to_sde_rm_priv_state(base_state);
	struct sde_rm_hw_blk *hw_blk, *tmp_hw_blk;
	int i;

	for (i = 0; i < SDE_HW_BLK_MAX; i++) {
		list_for_each_entry_safe(hw_blk, tmp_hw_blk,
				&state->hw_blks[i], list) {
			kfree(hw_blk);
		}
	}

	kfree(state);
}

static struct drm_private_state *sde_rm_duplicate_state(
		struct drm_private_obj *obj)
{
	struct sde_rm_state *state, *old_state =
			to_sde_rm_priv_state(obj->state);
	struct sde_rm_hw_blk *hw_blk, *old_hw_blk;
	int i;

	state = kmemdup(old_state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &state->base);

	for (i = 0; i < SDE_HW_BLK_MAX; i++) {
		INIT_LIST_HEAD(&state->hw_blks[i]);
		list_for_each_entry(old_hw_blk, &old_state->hw_blks[i], list) {
			hw_blk = kmemdup(old_hw_blk, sizeof(*hw_blk),
					GFP_KERNEL);
			if (!hw_blk)
				goto bail;

			list_add_tail(&hw_blk->list, &state->hw_blks[i]);
		}
	}

	return &state->base;

bail:
	sde_rm_destroy_state(obj, &state->base);
	return NULL;
}

static const struct drm_private_state_funcs sde_rm_state_funcs = {
	.atomic_duplicate_state = sde_rm_duplicate_state,
	.atomic_destroy_state = sde_rm_destroy_state,
};

static struct sde_rm_state *sde_rm_get_atomic_state(
		struct drm_atomic_state *state, struct sde_rm *rm)
{
	struct drm_device *dev = rm->dev;

	WARN_ON(!drm_modeset_is_locked(&dev->mode_config.connection_mutex));

	return to_sde_rm_priv_state(
			drm_atomic_get_private_obj_state(state, &rm->obj));
}
#endif

struct sde_hw_mdp *sde_rm_get_mdp(struct sde_rm *rm)
{
	return rm->hw_mdp;
}

void sde_rm_init_hw_iter(
		struct sde_rm_hw_iter *iter,
		uint32_t enc_id,
		enum sde_hw_blk_type type)
{
	memset(iter, 0, sizeof(*iter));
	iter->enc_id = enc_id;
	iter->type = type;
}

enum sde_rm_topology_name sde_rm_get_topology_name(struct sde_rm *rm,
		struct msm_display_topology topology)
{
	int i;

	for (i = 0; i < SDE_RM_TOPOLOGY_MAX; i++)
		if (RM_IS_TOPOLOGY_MATCH(rm->topology_tbl[i],
					topology))
			return rm->topology_tbl[i].top_name;

	return SDE_RM_TOPOLOGY_NONE;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static bool _sde_rm_get_hw_locked(struct sde_rm *rm,
		struct sde_rm_state *state,
		struct sde_rm_hw_iter *i)
#else
static bool _sde_rm_get_hw_locked(struct sde_rm *rm, struct sde_rm_hw_iter *i)
#endif
{
	struct list_head *blk_list;

	if (!rm || !i || i->type >= SDE_HW_BLK_MAX) {
		SDE_ERROR("invalid rm\n");
		return false;
	}

	i->hw = NULL;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	blk_list = &state->hw_blks[i->type];
#else
	blk_list = &rm->hw_blks[i->type];
#endif

	if (i->blk && (&i->blk->list == blk_list)) {
		SDE_DEBUG("attempt resume iteration past last\n");
		return false;
	}

	i->blk = list_prepare_entry(i->blk, blk_list, list);

	list_for_each_entry_continue(i->blk, blk_list, list) {
#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
		struct sde_rm_rsvp *rsvp = i->blk->rsvp;
#endif

		if (i->blk->type != i->type) {
			SDE_ERROR("found incorrect block type %d on %d list\n",
					i->blk->type, i->type);
			return false;
		}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		if ((i->enc_id == 0) || (i->blk->enc_id == i->enc_id)) {
#else
		if ((i->enc_id == 0) || (rsvp && rsvp->enc_id == i->enc_id)) {
#endif
			i->hw = i->blk->hw;
			SDE_DEBUG("found type %d id %d for enc %d\n",
					i->type, i->blk->id, i->enc_id);
			return true;
		}
	}

	SDE_DEBUG("no match, type %d for enc %d\n", i->type, i->enc_id);

	return false;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
bool sde_rm_request_hw_blk(struct sde_rm *rm,
 		struct sde_rm_hw_request *hw_blk_info)
#else
static bool _sde_rm_request_hw_blk_locked(struct sde_rm *rm,
		struct sde_rm_hw_request *hw_blk_info)
#endif
{
	struct list_head *blk_list;
	struct sde_rm_hw_blk *blk = NULL;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_rm_state *state;
#endif

	if (!rm || !hw_blk_info || hw_blk_info->type >= SDE_HW_BLK_MAX) {
		SDE_ERROR("invalid rm\n");
		return false;
	}

	hw_blk_info->hw = NULL;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	state = to_sde_rm_priv_state(rm->obj.state);
	blk_list = &state->hw_blks[hw_blk_info->type];
#else
	blk_list = &rm->hw_blks[hw_blk_info->type];
#endif

	blk = list_prepare_entry(blk, blk_list, list);

	list_for_each_entry_continue(blk, blk_list, list) {
		if (blk->type != hw_blk_info->type) {
			SDE_ERROR("found incorrect block type %d on %d list\n",
					blk->type, hw_blk_info->type);
			return false;
		}

		if (blk->hw->id == hw_blk_info->id) {
			hw_blk_info->hw = blk->hw;
			SDE_DEBUG("found type %d id %d\n",
					blk->type, blk->id);
			return true;
		}
	}

	SDE_DEBUG("no match, type %d id %d\n", hw_blk_info->type,
			hw_blk_info->id);

	return false;
}

bool sde_rm_get_hw(struct sde_rm *rm, struct sde_rm_hw_iter *i)
{
	bool ret;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_rm_state *state;

	state = to_sde_rm_priv_state(rm->obj.state);
	ret = _sde_rm_get_hw_locked(rm, state, i);
#else
	mutex_lock(&rm->rm_lock);
	ret = _sde_rm_get_hw_locked(rm, i);
	mutex_unlock(&rm->rm_lock);
#endif

	return ret;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
bool sde_rm_atomic_get_hw(struct sde_rm *rm,
		struct drm_atomic_state *atomic_state,
		struct sde_rm_hw_iter *i)
#else
bool sde_rm_request_hw_blk(struct sde_rm *rm, struct sde_rm_hw_request *hw)
#endif
{
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_rm_state *state;
#endif
	bool ret;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	state = sde_rm_get_atomic_state(atomic_state, rm);
	if (IS_ERR(state))
		return false;

	ret = _sde_rm_get_hw_locked(rm, state, i);
#else
	mutex_lock(&rm->rm_lock);
	ret = _sde_rm_request_hw_blk_locked(rm, hw);
	mutex_unlock(&rm->rm_lock);
#endif

	return ret;
}

static void _sde_rm_hw_destroy(enum sde_hw_blk_type type, void *hw)
{
	switch (type) {
	case SDE_HW_BLK_LM:
		sde_hw_lm_destroy(hw);
		break;
	case SDE_HW_BLK_DSPP:
		sde_hw_dspp_destroy(hw);
		break;
	case SDE_HW_BLK_DS:
		sde_hw_ds_destroy(hw);
		break;
	case SDE_HW_BLK_CTL:
		sde_hw_ctl_destroy(hw);
		break;
	case SDE_HW_BLK_CDM:
		sde_hw_cdm_destroy(hw);
		break;
	case SDE_HW_BLK_PINGPONG:
		sde_hw_pingpong_destroy(hw);
		break;
	case SDE_HW_BLK_INTF:
		sde_hw_intf_destroy(hw);
		break;
	case SDE_HW_BLK_WB:
		sde_hw_wb_destroy(hw);
		break;
	case SDE_HW_BLK_DSC:
		sde_hw_dsc_destroy(hw);
		break;
	case SDE_HW_BLK_VDC:
		sde_hw_vdc_destroy(hw);
		break;
	case SDE_HW_BLK_QDSS:
		sde_hw_qdss_destroy(hw);
		break;
	case SDE_HW_BLK_SSPP:
		/* SSPPs are not managed by the resource manager */
	case SDE_HW_BLK_TOP:
		/* Top is a singleton, not managed in hw_blks list */
	case SDE_HW_BLK_MAX:
	default:
		SDE_ERROR("unsupported block type %d\n", type);
		break;
	}
}

int sde_rm_destroy(struct sde_rm *rm)
{

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_rm_state *state;
#else
	struct sde_rm_rsvp *rsvp_cur, *rsvp_nxt;
#endif
	struct sde_rm_hw_blk *hw_cur, *hw_nxt;
	enum sde_hw_blk_type type;

	if (!rm) {
		SDE_ERROR("invalid rm\n");
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	state = to_sde_rm_priv_state(rm->obj.state);

#else
	list_for_each_entry_safe(rsvp_cur, rsvp_nxt, &rm->rsvps, list) {
		list_del(&rsvp_cur->list);
		kfree(rsvp_cur);
	}
#endif


	for (type = 0; type < SDE_HW_BLK_MAX; type++) {
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		list_for_each_entry_safe(hw_cur, hw_nxt, &state->hw_blks[type],
#else
		list_for_each_entry_safe(hw_cur, hw_nxt, &rm->hw_blks[type],
#endif
				list) {
			list_del(&hw_cur->list);
			_sde_rm_hw_destroy(hw_cur->type, hw_cur->hw);
			kfree(hw_cur);
		}
	}

	sde_hw_mdp_destroy(rm->hw_mdp);
	rm->hw_mdp = NULL;

#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
	mutex_destroy(&rm->rm_lock);
#endif

	return 0;
}

static int _sde_rm_hw_blk_create(
		struct sde_rm *rm,
		struct sde_mdss_cfg *cat,
		void __iomem *mmio,
		enum sde_hw_blk_type type,
		uint32_t id,
		void *hw_catalog_info)
{
	int rc;
	struct sde_rm_hw_blk *blk;
	struct sde_hw_mdp *hw_mdp;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_rm_state *state;
#endif
	void *hw;
	struct sde_kms *sde_kms;
	struct sde_vbif_clk_client clk_client;

	if (!rm) {
		SDE_ERROR("invalid rm\n");
		return -EINVAL;
	}

	sde_kms = to_sde_kms(ddev_to_msm_kms(rm->dev));
	if (!sde_kms || !sde_kms->catalog) {
		SDE_ERROR("invalid kms\n");
		return -EINVAL;
	}

	memset(&clk_client, 0, sizeof(clk_client));
	hw_mdp = rm->hw_mdp;

	switch (type) {
	case SDE_HW_BLK_LM:
		hw = sde_hw_lm_init(id, mmio, cat);
		break;
	case SDE_HW_BLK_DSPP:
		hw = sde_hw_dspp_init(id, mmio, cat);
		break;
	case SDE_HW_BLK_DS:
		hw = sde_hw_ds_init(id, mmio, cat);
		break;
	case SDE_HW_BLK_CTL:
		hw = sde_hw_ctl_init(id, mmio, cat);
		break;
	case SDE_HW_BLK_CDM:
		hw = sde_hw_cdm_init(id, mmio, cat, hw_mdp);
		break;
	case SDE_HW_BLK_PINGPONG:
		hw = sde_hw_pingpong_init(id, mmio, cat);
		break;
	case SDE_HW_BLK_INTF:
		hw = sde_hw_intf_init(id, mmio, cat);
		break;
	case SDE_HW_BLK_WB:
		hw = sde_hw_wb_init(id, mmio, cat, hw_mdp, &clk_client);
		break;
	case SDE_HW_BLK_DSC:
		hw = sde_hw_dsc_init(id, mmio, cat);
		break;
	case SDE_HW_BLK_VDC:
		hw = sde_hw_vdc_init(id, mmio, cat);
		break;
	case SDE_HW_BLK_QDSS:
		hw = sde_hw_qdss_init(id, mmio, cat);
		break;
	case SDE_HW_BLK_SSPP:
		/* SSPPs are not managed by the resource manager */
	case SDE_HW_BLK_TOP:
		/* Top is a singleton, not managed in hw_blks list */
	case SDE_HW_BLK_MAX:
	default:
		SDE_ERROR("unsupported block type %d\n", type);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(hw)) {
		SDE_ERROR("failed hw object creation: type %d, err %ld\n",
				type, PTR_ERR(hw));
		return -EFAULT;
	}

	blk = kzalloc(sizeof(*blk), GFP_KERNEL);
	if (!blk) {
		_sde_rm_hw_destroy(type, hw);
		return -ENOMEM;
	}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	state = to_sde_rm_priv_state(rm->obj.state);
#endif
	blk->type = type;
	blk->id = id;
	blk->hw = hw;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	list_add_tail(&blk->list, &state->hw_blks[type]);
	_sde_rm_inc_resource_info(state, &rm->avail_res, blk);
#else
	list_add_tail(&blk->list, &rm->hw_blks[type]);
	_sde_rm_inc_resource_info(rm, &rm->avail_res, blk);
#endif

	if (sde_kms->catalog->has_vbif_clk_split &&
			SDE_CLK_CTRL_VALID(clk_client.clk_ctrl)) {
		rc = sde_vbif_clk_register(sde_kms, &clk_client);
		if (rc) {
			SDE_ERROR("failed to register vbif client %d\n", clk_client.clk_ctrl);
			return -EFAULT;
		}
	}

	return 0;
}

static int _sde_rm_hw_blk_create_new(struct sde_rm *rm,
			struct sde_mdss_cfg *cat,
			void __iomem *mmio)
{
	int i, rc = 0;

	for (i = 0; i < cat->dspp_count; i++) {
		rc = _sde_rm_hw_blk_create(rm, cat, mmio, SDE_HW_BLK_DSPP,
				cat->dspp[i].id, &cat->dspp[i]);
		if (rc) {
			SDE_ERROR("failed: dspp hw not available\n");
			goto fail;
		}
	}

	if (cat->mdp[0].has_dest_scaler) {
		for (i = 0; i < cat->ds_count; i++) {
			rc = _sde_rm_hw_blk_create(rm, cat, mmio, SDE_HW_BLK_DS,
					cat->ds[i].id, &cat->ds[i]);
			if (rc) {
				SDE_ERROR("failed: ds hw not available\n");
				goto fail;
			}
		}
	}

	for (i = 0; i < cat->pingpong_count; i++) {
		rc = _sde_rm_hw_blk_create(rm, cat, mmio, SDE_HW_BLK_PINGPONG,
				cat->pingpong[i].id, &cat->pingpong[i]);
		if (rc) {
			SDE_ERROR("failed: pp hw not available\n");
			goto fail;
		}
	}

	for (i = 0; i < cat->dsc_count; i++) {
		rc = _sde_rm_hw_blk_create(rm, cat, mmio, SDE_HW_BLK_DSC,
			cat->dsc[i].id, &cat->dsc[i]);
		if (rc) {
			SDE_ERROR("failed: dsc hw not available\n");
			goto fail;
		}
	}

	for (i = 0; i < cat->vdc_count; i++) {
		rc = _sde_rm_hw_blk_create(rm, cat, mmio, SDE_HW_BLK_VDC,
			cat->vdc[i].id, &cat->vdc[i]);
		if (rc) {
			SDE_ERROR("failed: vdc hw not available\n");
			goto fail;
		}
	}

	for (i = 0; i < cat->intf_count; i++) {
		if (cat->intf[i].type == INTF_NONE) {
			SDE_DEBUG("skip intf %d with type none\n", i);
			continue;
		}

		rc = _sde_rm_hw_blk_create(rm, cat, mmio, SDE_HW_BLK_INTF,
				cat->intf[i].id, &cat->intf[i]);
		if (rc) {
			SDE_ERROR("failed: intf hw not available\n");
			goto fail;
		}
	}

	for (i = 0; i < cat->wb_count; i++) {
		rc = _sde_rm_hw_blk_create(rm, cat, mmio, SDE_HW_BLK_WB,
				cat->wb[i].id, &cat->wb[i]);
		if (rc) {
			SDE_ERROR("failed: wb hw not available\n");
			goto fail;
		}
	}

	for (i = 0; i < cat->ctl_count; i++) {
		rc = _sde_rm_hw_blk_create(rm, cat, mmio, SDE_HW_BLK_CTL,
				cat->ctl[i].id, &cat->ctl[i]);
		if (rc) {
			SDE_ERROR("failed: ctl hw not available\n");
			goto fail;
		}
	}

	for (i = 0; i < cat->cdm_count; i++) {
		rc = _sde_rm_hw_blk_create(rm, cat, mmio, SDE_HW_BLK_CDM,
				cat->cdm[i].id, &cat->cdm[i]);
		if (rc) {
			SDE_ERROR("failed: cdm hw not available\n");
			goto fail;
		}
	}

	for (i = 0; i < cat->qdss_count; i++) {
		rc = _sde_rm_hw_blk_create(rm, cat, mmio, SDE_HW_BLK_QDSS,
				cat->qdss[i].id, &cat->qdss[i]);
		if (rc) {
			SDE_ERROR("failed: qdss hw not available\n");
			goto fail;
		}
	}

fail:
	return rc;
}

#ifdef CONFIG_DEBUG_FS
static int _sde_rm_status_show(struct seq_file *s, void *data)
{
	struct sde_rm *rm;
	struct sde_rm_hw_blk *blk;
	u32 type, allocated, unallocated;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_rm_state *state;
#endif

	if (!s || !s->private)
		return -EINVAL;

	rm = s->private;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	state = to_sde_rm_priv_state(rm->obj.state);
#endif

	for (type = SDE_HW_BLK_LM; type < SDE_HW_BLK_MAX; type++) {
		allocated = 0;
		unallocated = 0;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		list_for_each_entry(blk, &state->hw_blks[type], list) {
			if (!blk->enc_id)
#else
		list_for_each_entry(blk, &rm->hw_blks[type], list) {
			if (!blk->rsvp && !blk->rsvp_nxt)
#endif
				unallocated++;
			else
				allocated++;
		}
		seq_printf(s, "type:%d blk:%s allocated:%d unallocated:%d\n",
			type, sde_hw_blk_str[type], allocated, unallocated);
	}

	return 0;
}

static int _sde_rm_debugfs_status_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, _sde_rm_status_show, inode->i_private);
}

void sde_rm_debugfs_init(struct sde_rm *sde_rm, struct dentry *parent)
{
	static const struct file_operations debugfs_rm_status_fops = {
		.open =		_sde_rm_debugfs_status_open,
		.read =		seq_read,
	};

	debugfs_create_file("rm_status", 0400, parent, sde_rm, &debugfs_rm_status_fops);
}
#else
void sde_rm_debugfs_init(struct sde_rm *rm, struct dentry *parent)
{
}
#endif

int sde_rm_init(struct sde_rm *rm,
		struct sde_mdss_cfg *cat,
		void __iomem *mmio,
		struct drm_device *dev)
{
	int i, rc = 0;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_rm_state *state;
#endif
	enum sde_hw_blk_type type;

	if (!rm || !cat || !mmio || !dev) {
		SDE_ERROR("invalid input params\n");
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;
#endif

	/* Clear, setup lists */
	memset(rm, 0, sizeof(*rm));

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	drm_atomic_private_obj_init(dev,
				    &rm->obj,
				    &state->base,
				    &sde_rm_state_funcs);
#else
	mutex_init(&rm->rm_lock);

	INIT_LIST_HEAD(&rm->rsvps);
#endif
	for (type = 0; type < SDE_HW_BLK_MAX; type++)
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		INIT_LIST_HEAD(&state->hw_blks[type]);
#else
		INIT_LIST_HEAD(&rm->hw_blks[type]);
#endif

	rm->dev = dev;

	if (IS_SDE_CTL_REV_100(cat->ctl_rev))
		rm->topology_tbl = g_top_table_v1;
	else
		rm->topology_tbl = g_top_table;

	/* Some of the sub-blocks require an mdptop to be created */
	rm->hw_mdp = sde_hw_mdptop_init(MDP_TOP, mmio, cat);
	if (IS_ERR_OR_NULL(rm->hw_mdp)) {
		rc = PTR_ERR(rm->hw_mdp);
		rm->hw_mdp = NULL;
		SDE_ERROR("failed: mdp hw not available\n");
		goto fail;
	}

	/* Interrogate HW catalog and create tracking items for hw blocks */
	for (i = 0; i < cat->mixer_count; i++) {
		struct sde_lm_cfg *lm = &cat->mixer[i];

		if (lm->pingpong == PINGPONG_MAX) {
			SDE_ERROR("mixer %d without pingpong\n", lm->id);
			goto fail;
		}

		rc = _sde_rm_hw_blk_create(rm, cat, mmio, SDE_HW_BLK_LM,
				cat->mixer[i].id, &cat->mixer[i]);
		if (rc) {
			SDE_ERROR("failed: lm hw not available\n");
			goto fail;
		}

		if (!rm->lm_max_width) {
			rm->lm_max_width = lm->sblk->maxwidth;
		} else if (rm->lm_max_width != lm->sblk->maxwidth) {
			/*
			 * Don't expect to have hw where lm max widths differ.
			 * If found, take the min.
			 */
			SDE_ERROR("unsupported: lm maxwidth differs\n");
			if (rm->lm_max_width > lm->sblk->maxwidth)
				rm->lm_max_width = lm->sblk->maxwidth;
		}
	}

	rc = _sde_rm_hw_blk_create_new(rm, cat, mmio);
	if (!rc)
		return 0;

fail:
	sde_rm_destroy(rm);

	return rc;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static bool _sde_rm_check_lm(
		struct sde_rm *rm,
		uint32_t enc_id,
		struct sde_rm_requirements *reqs,
		const struct sde_lm_cfg *lm_cfg,
		struct sde_rm_hw_blk *lm,
		struct sde_rm_hw_blk **dspp,
		struct sde_rm_hw_blk **ds,
		struct sde_rm_hw_blk **pp)
#else
static bool _sde_rm_check_lm(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		struct sde_rm_requirements *reqs,
		const struct sde_lm_cfg *lm_cfg,
		struct sde_rm_hw_blk *lm,
		struct sde_rm_hw_blk **dspp,
		struct sde_rm_hw_blk **ds,
		struct sde_rm_hw_blk **pp)
#endif
{
	bool is_valid_dspp, is_valid_ds, ret = true;

	is_valid_dspp = (lm_cfg->dspp != DSPP_MAX) ? true : false;
	is_valid_ds = (lm_cfg->ds != DS_MAX) ? true : false;

	/**
	 * RM_RQ_X: specification of which LMs to choose
	 * is_valid_X: indicates whether LM is tied with block X
	 * ret: true if given LM matches the user requirement,
	 *      false otherwise
	 */
	if (RM_RQ_DSPP(reqs) && RM_RQ_DS(reqs))
		ret = (is_valid_dspp && is_valid_ds);
	else if (RM_RQ_DSPP(reqs))
		ret = is_valid_dspp;
	else if (RM_RQ_DS(reqs))
		ret = is_valid_ds;

	if (!ret) {
		SDE_DEBUG(
			"fail:lm(%d)req_dspp(%d)dspp(%d)req_ds(%d)ds(%d)\n",
			lm_cfg->id, (bool)(RM_RQ_DSPP(reqs)),
			lm_cfg->dspp, (bool)(RM_RQ_DS(reqs)),
			lm_cfg->ds);

		return ret;
	}
	return true;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static bool _sde_rm_reserve_dspp(
		struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		const struct sde_lm_cfg *lm_cfg,
		struct sde_rm_hw_blk *lm,
		struct sde_rm_hw_blk **dspp)
#else
static bool _sde_rm_reserve_dspp(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		const struct sde_lm_cfg *lm_cfg,
		struct sde_rm_hw_blk *lm,
		struct sde_rm_hw_blk **dspp)
#endif
{
	struct sde_rm_hw_iter iter;

	if (lm_cfg->dspp != DSPP_MAX) {
		sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_DSPP);
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		while (_sde_rm_get_hw_locked(rm, state, &iter)) {
#else
		while (_sde_rm_get_hw_locked(rm, &iter)) {
#endif
			if (iter.blk->id == lm_cfg->dspp) {
				*dspp = iter.blk;
				break;
			}
		}

		if (!*dspp) {
			SDE_DEBUG("lm %d failed to retrieve dspp %d\n", lm->id,
					lm_cfg->dspp);
			return false;
		}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		if (RESERVED_BY_OTHER(*dspp, enc_id)) {
#else
		if (RESERVED_BY_OTHER(*dspp, rsvp)) {
#endif
			SDE_DEBUG("lm %d dspp %d already reserved\n",
					lm->id, (*dspp)->id);
			return false;
		}
	}

	return true;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static bool _sde_rm_reserve_ds(
		struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		const struct sde_lm_cfg *lm_cfg,
		struct sde_rm_hw_blk *lm,
		struct sde_rm_hw_blk **ds)
#else
static bool _sde_rm_reserve_ds(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		const struct sde_lm_cfg *lm_cfg,
		struct sde_rm_hw_blk *lm,
		struct sde_rm_hw_blk **ds)
#endif
{
	struct sde_rm_hw_iter iter;

	if (lm_cfg->ds != DS_MAX) {
		sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_DS);
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		while (_sde_rm_get_hw_locked(rm, state, &iter)) {
#else
		while (_sde_rm_get_hw_locked(rm, &iter)) {
#endif
			if (iter.blk->id == lm_cfg->ds) {
				*ds = iter.blk;
				break;
			}
		}

		if (!*ds) {
			SDE_DEBUG("lm %d failed to retrieve ds %d\n", lm->id,
					lm_cfg->ds);
			return false;
		}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		if (RESERVED_BY_OTHER(*ds, enc_id)) {
#else
		if (RESERVED_BY_OTHER(*ds, rsvp)) {
#endif
			SDE_DEBUG("lm %d ds %d already reserved\n",
					lm->id, (*ds)->id);
			return false;
		}
	}

	return true;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static bool _sde_rm_reserve_pp(
		struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		struct sde_rm_requirements *reqs,
		const struct sde_lm_cfg *lm_cfg,
		const struct sde_pingpong_cfg *pp_cfg,
		struct sde_rm_hw_blk *lm,
		struct sde_rm_hw_blk **dspp,
		struct sde_rm_hw_blk **ds,
		struct sde_rm_hw_blk **pp)
#else
static bool _sde_rm_reserve_pp(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		struct sde_rm_requirements *reqs,
		const struct sde_lm_cfg *lm_cfg,
		const struct sde_pingpong_cfg *pp_cfg,
		struct sde_rm_hw_blk *lm,
		struct sde_rm_hw_blk **dspp,
		struct sde_rm_hw_blk **ds,
		struct sde_rm_hw_blk **pp)
#endif
{
	struct sde_rm_hw_iter iter;

	sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_PINGPONG);
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	while (_sde_rm_get_hw_locked(rm, state, &iter)) {
#else
	while (_sde_rm_get_hw_locked(rm, &iter)) {
#endif
		if (iter.blk->id == lm_cfg->pingpong) {
			*pp = iter.blk;
			break;
		}
	}

	if (!*pp) {
		SDE_ERROR("failed to get pp on lm %d\n", lm_cfg->pingpong);
		return false;
	}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	if (RESERVED_BY_OTHER(*pp, enc_id)) {
#else
	if (RESERVED_BY_OTHER(*pp, rsvp)) {
#endif
		SDE_DEBUG("lm %d pp %d already reserved\n", lm->id,
				(*pp)->id);
		*dspp = NULL;
		*ds = NULL;
		return false;
	}

	pp_cfg = to_sde_hw_pingpong((*pp)->hw)->caps;
	if ((reqs->topology->top_name == SDE_RM_TOPOLOGY_PPSPLIT) &&
			!(test_bit(SDE_PINGPONG_SPLIT, &pp_cfg->features))) {
		SDE_DEBUG("pp %d doesn't support ppsplit\n", pp_cfg->id);
		*dspp = NULL;
		*ds = NULL;
		return false;
	}
	return true;
}

/**
 * _sde_rm_check_lm_and_get_connected_blks - check if proposed layer mixer meets
 *	proposed use case requirements, incl. hardwired dependent blocks like
 *	pingpong, and dspp.
 * @rm: sde resource manager handle
 * @rsvp: reservation currently being created
 * @enc_id: encoder id
 * @reqs: proposed use case requirements
 * @lm: proposed layer mixer, function checks if lm, and all other hardwired
 *      blocks connected to the lm (pp, dspp) are available and appropriate
 * @dspp: output parameter, dspp block attached to the layer mixer.
 *        NULL if dspp was not available, or not matching requirements.
 * @pp: output parameter, pingpong block attached to the layer mixer.
 *      NULL if dspp was not available, or not matching requirements.
 * @primary_lm: if non-null, this function check if lm is compatible primary_lm
 *              as well as satisfying all other requirements
 * @Return: true if lm matches all requirements, false otherwise
 */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static bool _sde_rm_check_lm_and_get_connected_blks(
		struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		struct sde_rm_requirements *reqs,
		struct sde_rm_hw_blk *lm,
		struct sde_rm_hw_blk **dspp,
		struct sde_rm_hw_blk **ds,
		struct sde_rm_hw_blk **pp,
		struct sde_rm_hw_blk *primary_lm,
		u32 conn_lm_mask)
#else
static bool _sde_rm_check_lm_and_get_connected_blks(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		struct sde_rm_requirements *reqs,
		struct sde_rm_hw_blk *lm,
		struct sde_rm_hw_blk **dspp,
		struct sde_rm_hw_blk **ds,
		struct sde_rm_hw_blk **pp,
		struct sde_rm_hw_blk *primary_lm,
		u32 conn_lm_mask)
#endif
{
	const struct sde_lm_cfg *lm_cfg = to_sde_hw_mixer(lm->hw)->cap;
	const struct sde_pingpong_cfg *pp_cfg;
	bool ret, is_conn_primary, is_conn_secondary;
	u32 lm_primary_pref, lm_secondary_pref, cwb_pref, dcwb_pref;

	*dspp = NULL;
	*ds = NULL;
	*pp = NULL;

	if (lm_cfg->features & BIT(SDE_MIXER_IS_VIRTUAL)) {
		SDE_DEBUG("lm %d hw block is removed and it is a virtual mixer",
				lm_cfg->id);
		return false;
	}

	lm_primary_pref = lm_cfg->features & BIT(SDE_DISP_PRIMARY_PREF);
	lm_secondary_pref = lm_cfg->features & BIT(SDE_DISP_SECONDARY_PREF);
	cwb_pref = lm_cfg->features & BIT(SDE_DISP_CWB_PREF);
	dcwb_pref = lm_cfg->features & BIT(SDE_DISP_DCWB_PREF);
	is_conn_primary = (reqs->hw_res.display_type ==
				 SDE_CONNECTOR_PRIMARY) ? true : false;
	is_conn_secondary = (reqs->hw_res.display_type ==
				 SDE_CONNECTOR_SECONDARY) ? true : false;

	SDE_DEBUG("check lm %d: dspp %d ds %d pp %d features %ld disp type %d\n",
		 lm_cfg->id, lm_cfg->dspp, lm_cfg->ds, lm_cfg->pingpong,
		 lm_cfg->features, (int)reqs->hw_res.display_type);

	/* Check if this layer mixer is a peer of the proposed primary LM */
	if (primary_lm) {
		const struct sde_lm_cfg *prim_lm_cfg =
				to_sde_hw_mixer(primary_lm->hw)->cap;

		if (!test_bit(lm_cfg->id, &prim_lm_cfg->lm_pair_mask)) {
			SDE_DEBUG("lm %d not peer of lm %d\n", lm_cfg->id,
					prim_lm_cfg->id);
			return false;
		}
	}

	/* bypass rest of the checks if LM for primary display is found */
	if (!lm_primary_pref && !lm_secondary_pref) {
		/* Check lm for valid requirements */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		ret = _sde_rm_check_lm(rm, enc_id, reqs, lm_cfg, lm,
#else
		ret = _sde_rm_check_lm(rm, rsvp, reqs, lm_cfg, lm,
#endif
				dspp, ds, pp);
		if (!ret)
			return ret;

		/**
		 * If CWB is enabled and LM is not CWB supported
		 * then return false.
		 */
		if ((RM_RQ_CWB(reqs) && !cwb_pref) ||
		    (RM_RQ_DCWB(reqs) && !dcwb_pref)) {
			SDE_DEBUG("fail: cwb/dcwb supported lm not allocated\n");
			return false;
		} else if (!RM_RQ_DCWB(reqs) && dcwb_pref) {
			SDE_DEBUG("fail: dcwb supported dummy lm incorrectly allocated\n");
			return false;
		} else if (RM_RQ_DCWB(reqs) && dcwb_pref && conn_lm_mask &&
				((ffs(conn_lm_mask) % 2) ==  ((lm_cfg->id + 1) % 2))) {
			SDE_DEBUG("fail: dcwb:%d trying to match lm:%d\n",
					lm_cfg->id, ffs(conn_lm_mask));
			return false;
		}
	} else if ((!is_conn_primary && lm_primary_pref) ||
			(!is_conn_secondary && lm_secondary_pref)) {
		SDE_DEBUG(
			"display preference is not met. display_type: %d lm_features: %lx\n",
			(int)reqs->hw_res.display_type, lm_cfg->features);
		return false;
	}

	/* Already reserved? */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	if (RESERVED_BY_OTHER(lm, enc_id)) {
#else
	if (RESERVED_BY_OTHER(lm, rsvp)) {
#endif
		SDE_DEBUG("lm %d already reserved\n", lm_cfg->id);
		return false;
	}

	/* Reserve dspp */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	ret = _sde_rm_reserve_dspp(rm, state, enc_id, lm_cfg, lm, dspp);
#else
	ret = _sde_rm_reserve_dspp(rm, rsvp, lm_cfg, lm, dspp);
#endif
	if (!ret)
		return ret;

	/* Reserve ds */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	ret = _sde_rm_reserve_ds(rm, state, enc_id, lm_cfg, lm, ds);
#else
	ret = _sde_rm_reserve_ds(rm, rsvp, lm_cfg, lm, ds);
#endif
	if (!ret)
		return ret;

	/* Reserve pp */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	ret = _sde_rm_reserve_pp(rm, state, enc_id, reqs, lm_cfg, pp_cfg, lm,
#else
	ret = _sde_rm_reserve_pp(rm, rsvp, reqs, lm_cfg, pp_cfg, lm,
#endif
			dspp, ds, pp);
	if (!ret)
		return ret;

	return true;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_reserve_lms(
		struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		struct sde_rm_requirements *reqs,
		u8 *_lm_ids)
#else
static int _sde_rm_reserve_lms(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		struct sde_rm_requirements *reqs,
		u8 *_lm_ids)
#endif
{
	struct sde_rm_hw_blk *lm[MAX_BLOCKS];
	struct sde_rm_hw_blk *dspp[MAX_BLOCKS];
	struct sde_rm_hw_blk *ds[MAX_BLOCKS];
	struct sde_rm_hw_blk *pp[MAX_BLOCKS];
	struct sde_rm_hw_iter iter_i, iter_j;
	u32 lm_mask = 0,  conn_lm_mask = 0;
	int lm_count = 0;
	int i, rc = 0;

	if (!reqs->topology->num_lm) {
		SDE_DEBUG("invalid number of lm: %d\n", reqs->topology->num_lm);
		return 0;
	}

	if (RM_RQ_DCWB(reqs))
		conn_lm_mask = reqs->conn_lm_mask;
	/* Find a primary mixer */
	sde_rm_init_hw_iter(&iter_i, 0, SDE_HW_BLK_LM);
	while (lm_count != reqs->topology->num_lm &&
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
			_sde_rm_get_hw_locked(rm, state, &iter_i)) {
#else
			_sde_rm_get_hw_locked(rm, &iter_i)) {
#endif
		if (lm_mask & (1 << iter_i.blk->id))
			continue;

		lm[lm_count] = iter_i.blk;
		dspp[lm_count] = NULL;
		ds[lm_count] = NULL;
		pp[lm_count] = NULL;

		SDE_DEBUG("blk id = %d, _lm_ids[%d] = %d\n",
			iter_i.blk->id,
			lm_count,
			_lm_ids ? _lm_ids[lm_count] : -1);

		if (_lm_ids && (lm[lm_count])->id != _lm_ids[lm_count])
			continue;

		if (!_sde_rm_check_lm_and_get_connected_blks(
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
				rm, state, enc_id, reqs, lm[lm_count],
#else
				rm, rsvp, reqs, lm[lm_count],
#endif
				&dspp[lm_count], &ds[lm_count],
				&pp[lm_count], NULL, conn_lm_mask))
			continue;

		lm_mask |= (1 << iter_i.blk->id);
		++lm_count;

		/* Return if peer is not needed */
		if (lm_count == reqs->topology->num_lm)
			break;

		if (RM_RQ_DCWB(reqs))
			conn_lm_mask = conn_lm_mask & ~BIT(ffs(conn_lm_mask) - 1);

		/* Valid primary mixer found, find matching peers */
		sde_rm_init_hw_iter(&iter_j, 0, SDE_HW_BLK_LM);

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		while (_sde_rm_get_hw_locked(rm, state, &iter_j)) {
#else
		while (_sde_rm_get_hw_locked(rm, &iter_j)) {
#endif
			if (lm_mask & (1 << iter_j.blk->id))
				continue;

			lm[lm_count] = iter_j.blk;
			dspp[lm_count] = NULL;
			ds[lm_count] = NULL;
			pp[lm_count] = NULL;

			if (!_sde_rm_check_lm_and_get_connected_blks(
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
					rm, state, enc_id, reqs, iter_j.blk,
#else
					rm, rsvp, reqs, iter_j.blk,
#endif
					&dspp[lm_count], &ds[lm_count],
					&pp[lm_count], iter_i.blk,
					conn_lm_mask))
				continue;

			SDE_DEBUG("blk id = %d, _lm_ids[%d] = %d\n",
				iter_j.blk->id,
				lm_count,
				_lm_ids ? _lm_ids[lm_count] : -1);

			if (_lm_ids && (lm[lm_count])->id != _lm_ids[lm_count])
				continue;

			lm_mask |= (1 << iter_j.blk->id);
			++lm_count;

			if (RM_RQ_DCWB(reqs))
				conn_lm_mask = conn_lm_mask & ~BIT(ffs(conn_lm_mask) - 1);

			break;
		}

		/* Rollback primary LM if peer is not found */
		if (!iter_j.hw) {
			lm_mask &= ~(1 << iter_i.blk->id);
			--lm_count;
		}
	}

	if (lm_count != reqs->topology->num_lm) {
		SDE_DEBUG("unable to find appropriate mixers\n");
		return -ENAVAIL;
	}

	for (i = 0; i < lm_count; i++) {
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		lm[i]->enc_id = enc_id;
		pp[i]->enc_id = enc_id;
#else
		lm[i]->rsvp_nxt = rsvp;
		pp[i]->rsvp_nxt = rsvp;
#endif
		if (dspp[i])
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
			dspp[i]->enc_id = enc_id;
#else
			dspp[i]->rsvp_nxt = rsvp;
#endif

		if (ds[i])
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
			ds[i]->enc_id = enc_id;
#else
			ds[i]->rsvp_nxt = rsvp;
#endif

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		SDE_EVT32(lm[i]->type, enc_id, lm[i]->id, pp[i]->id,
#else
		SDE_EVT32(lm[i]->type, rsvp->enc_id, lm[i]->id, pp[i]->id,
#endif
				dspp[i] ? dspp[i]->id : 0,
				ds[i] ? ds[i]->id : 0);
	}

	if (reqs->topology->top_name == SDE_RM_TOPOLOGY_PPSPLIT) {
		/* reserve a free PINGPONG_SLAVE block */
		rc = -ENAVAIL;
		sde_rm_init_hw_iter(&iter_i, 0, SDE_HW_BLK_PINGPONG);
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		while (_sde_rm_get_hw_locked(rm, state, &iter_i)) {
#else
		while (_sde_rm_get_hw_locked(rm, &iter_i)) {
#endif
			const struct sde_hw_pingpong *pp =
					to_sde_hw_pingpong(iter_i.blk->hw);
			const struct sde_pingpong_cfg *pp_cfg = pp->caps;

			if (!(test_bit(SDE_PINGPONG_SLAVE, &pp_cfg->features)))
				continue;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
			if (RESERVED_BY_OTHER(iter_i.blk, enc_id))
#else
			if (RESERVED_BY_OTHER(iter_i.blk, rsvp))
#endif
				continue;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
			iter_i.blk->enc_id = enc_id;
#else
			iter_i.blk->rsvp_nxt = rsvp;
#endif
			rc = 0;
			break;
		}
	}

	return rc;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_reserve_ctls(
		struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		struct sde_rm_requirements *reqs,
		const struct sde_rm_topology_def *top,
		u8 *_ctl_ids)
#else
static int _sde_rm_reserve_ctls(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		struct sde_rm_requirements *reqs,
		const struct sde_rm_topology_def *top,
		u8 *_ctl_ids)
#endif
{
	struct sde_rm_hw_blk *ctls[MAX_BLOCKS];
	struct sde_rm_hw_iter iter, curr;
	int i = 0;

	if (!top->num_ctl) {
		SDE_DEBUG("invalid number of ctl: %d\n", top->num_ctl);
		return 0;
	}

	memset(&ctls, 0, sizeof(ctls));

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	sde_rm_init_hw_iter(&curr, enc_id, SDE_HW_BLK_CTL);
#else
	sde_rm_init_hw_iter(&curr, rsvp->enc_id, SDE_HW_BLK_CTL);
#endif
	sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_CTL);
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	while (_sde_rm_get_hw_locked(rm, state, &iter)) {
#else
	while (_sde_rm_get_hw_locked(rm, &iter)) {
#endif
		const struct sde_hw_ctl *ctl = to_sde_hw_ctl(iter.blk->hw);
		unsigned long features = ctl->caps->features;
		bool has_split_display, has_ppsplit, primary_pref;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		if (RESERVED_BY_OTHER(iter.blk, enc_id))
#else
		if (RESERVED_BY_OTHER(iter.blk, rsvp))
#endif
			continue;

		has_split_display = BIT(SDE_CTL_SPLIT_DISPLAY) & features;
		has_ppsplit = BIT(SDE_CTL_PINGPONG_SPLIT) & features;
		primary_pref = BIT(SDE_CTL_PRIMARY_PREF) & features;

		SDE_DEBUG("ctl %d caps 0x%lX\n", iter.blk->id, features);

		/*
		 * bypass rest feature checks on finding CTL preferred
		 * for primary displays.
		 */
		if (!primary_pref && !_ctl_ids) {
			if (top->needs_split_display != has_split_display)
				continue;

			if (top->top_name == SDE_RM_TOPOLOGY_PPSPLIT &&
					!has_ppsplit)
				continue;
		} else if (!(reqs->hw_res.display_type ==
				SDE_CONNECTOR_PRIMARY && primary_pref) && !_ctl_ids) {
			SDE_DEBUG(
				"display pref not met. display_type: %d primary_pref: %d\n",
				reqs->hw_res.display_type, primary_pref);
			continue;
		}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		if (_sde_rm_get_hw_locked(rm, state, &curr) && (curr.blk->id != iter.blk->id)) {
#else
		if (_sde_rm_get_hw_locked(rm, &curr) && (curr.blk->id != iter.blk->id)) {
#endif
			SDE_EVT32(curr.blk->id, iter.blk->id, SDE_EVTLOG_FUNC_CASE1);
			SDE_DEBUG("ctl in use:%d avoiding new:%d\n", curr.blk->id, iter.blk->id);
			continue;
		}

		ctls[i] = iter.blk;

		SDE_DEBUG("blk id = %d, _ctl_ids[%d] = %d\n",
			iter.blk->id, i,
			_ctl_ids ? _ctl_ids[i] : -1);

		if (_ctl_ids && (ctls[i]->id != _ctl_ids[i]))
			continue;

		SDE_DEBUG("ctl %d match\n", iter.blk->id);

		if (++i == top->num_ctl)
			break;
	}

	if (i != top->num_ctl)
		return -ENAVAIL;

	for (i = 0; i < ARRAY_SIZE(ctls) && i < top->num_ctl; i++) {
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		ctls[i]->enc_id = enc_id;
		SDE_EVT32(ctls[i]->type, enc_id, ctls[i]->id);
#else
		ctls[i]->rsvp_nxt = rsvp;
		SDE_EVT32(ctls[i]->type, rsvp->enc_id, ctls[i]->id);
#endif
	}

	return 0;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static bool _sde_rm_check_dsc(struct sde_rm *rm,
		uint32_t enc_id,
		struct sde_rm_hw_blk *dsc,
		struct sde_rm_hw_blk *paired_dsc)
#else
static bool _sde_rm_check_dsc(struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		struct sde_rm_hw_blk *dsc,
		struct sde_rm_hw_blk *paired_dsc,
		struct sde_rm_hw_blk *pp_blk)
#endif
{
	const struct sde_dsc_cfg *dsc_cfg = to_sde_hw_dsc(dsc->hw)->caps;

	/* Already reserved? */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	if (RESERVED_BY_OTHER(dsc, enc_id)) {
#else
	if (RESERVED_BY_OTHER(dsc, rsvp)) {
#endif
		SDE_DEBUG("dsc %d already reserved\n", dsc_cfg->id);
		return false;
	}

#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
	/**
	 * This check is required for routing even numbered DSC
	 * blks to any of the even numbered PP blks and odd numbered
	 * DSC blks to any of the odd numbered PP blks.
	 */
	if (!pp_blk || !IS_COMPATIBLE_PP_DSC(pp_blk->id, dsc->id))
		return false;
#endif

	/* Check if this dsc is a peer of the proposed paired DSC */
	if (paired_dsc) {
		const struct sde_dsc_cfg *paired_dsc_cfg =
				to_sde_hw_dsc(paired_dsc->hw)->caps;

		if (!test_bit(dsc_cfg->id, paired_dsc_cfg->dsc_pair_mask)) {
			SDE_DEBUG("dsc %d not peer of dsc %d\n", dsc_cfg->id,
					paired_dsc_cfg->id);
			return false;
		}
	}

	return true;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static bool _sde_rm_check_vdc(struct sde_rm *rm,
		uint32_t enc_id,
		struct sde_rm_hw_blk *vdc)
#else
static bool _sde_rm_check_vdc(struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		struct sde_rm_hw_blk *vdc)
#endif
{
	const struct sde_vdc_cfg *vdc_cfg = to_sde_hw_vdc(vdc->hw)->caps;

	/* Already reserved? */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	if (RESERVED_BY_OTHER(vdc, enc_id)) {
#else
	if (RESERVED_BY_OTHER(vdc, rsvp)) {
#endif
		SDE_DEBUG("vdc %d already reserved\n", vdc_cfg->id);
		return false;
	}

	return true;
}

#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
static void sde_rm_get_rsvp_nxt_hw_blks(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		int type,
		struct sde_rm_hw_blk **blk_arr)
{
	struct sde_rm_hw_blk *blk;
	int i = 0;

	list_for_each_entry(blk, &rm->hw_blks[type], list) {
		if (blk->rsvp_nxt && blk->rsvp_nxt->seq ==
					rsvp->seq)
			blk_arr[i++] = blk;
	}
}
#endif

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_reserve_dsc(
		struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		struct sde_rm_requirements *reqs,
		u8 *_dsc_ids)
#else
static int _sde_rm_reserve_dsc(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		struct sde_rm_requirements *reqs,
		u8 *_dsc_ids)
#endif
{
	struct sde_rm_hw_iter iter_i, iter_j;
	struct sde_rm_hw_blk *dsc[MAX_BLOCKS];
	u32 reserve_mask = 0;
#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_rm_hw_blk *pp[MAX_BLOCKS];
#endif
	int alloc_count = 0;
	int num_dsc_enc;
	struct msm_display_dsc_info *dsc_info;
	int i;

	if (reqs->hw_res.comp_info->comp_type != MSM_DISPLAY_COMPRESSION_DSC) {
		SDE_DEBUG("compression blk dsc not required\n");
		return 0;
	}

	num_dsc_enc = reqs->topology->num_comp_enc;
	dsc_info = &reqs->hw_res.comp_info->dsc_info;

	if ((!num_dsc_enc) || !dsc_info) {
		SDE_DEBUG("invalid topoplogy params: %d, %d\n",
				num_dsc_enc, !(dsc_info == NULL));
		return 0;
	}

	sde_rm_init_hw_iter(&iter_i, 0, SDE_HW_BLK_DSC);
#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
	sde_rm_get_rsvp_nxt_hw_blks(rm, rsvp, SDE_HW_BLK_PINGPONG, pp);
#endif

	/* Find a first DSC */
	while (alloc_count != num_dsc_enc &&
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
			_sde_rm_get_hw_locked(rm, state, &iter_i)) {
#else
			_sde_rm_get_hw_locked(rm, &iter_i)) {
#endif
		const struct sde_hw_dsc *hw_dsc = to_sde_hw_dsc(
				iter_i.blk->hw);
		unsigned long features = hw_dsc->caps->features;
		bool has_422_420_support =
			BIT(SDE_DSC_NATIVE_422_EN) & features;

		if (reserve_mask & (1 << iter_i.blk->id))
			continue;

		if (_dsc_ids && (iter_i.blk->id != _dsc_ids[alloc_count]))
			continue;

		/* if this hw block does not support required feature */
		if (!_dsc_ids && (dsc_info->config.native_422 ||
			dsc_info->config.native_420) && !has_422_420_support)
			continue;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		if (!_sde_rm_check_dsc(rm, enc_id, iter_i.blk, NULL))
#else
		if (!_sde_rm_check_dsc(rm, rsvp, iter_i.blk, NULL,
					 pp[alloc_count]))
#endif
			continue;

		SDE_DEBUG("blk id = %d, _dsc_ids[%d] = %d\n",
			iter_i.blk->id,
			alloc_count,
			_dsc_ids ? _dsc_ids[alloc_count] : -1);

		reserve_mask |= (1 << iter_i.blk->id);
		dsc[alloc_count++] = iter_i.blk;

		/* Return if peer is not needed */
		if (alloc_count == num_dsc_enc)
			break;

		/* Valid first dsc found, find matching peers */
		sde_rm_init_hw_iter(&iter_j, 0, SDE_HW_BLK_DSC);

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		while (_sde_rm_get_hw_locked(rm, state, &iter_j)) {
#else
		while (_sde_rm_get_hw_locked(rm, &iter_j)) {
#endif
			if (reserve_mask & (1 << iter_j.blk->id))
				continue;

			if (_dsc_ids && (iter_j.blk->id !=
					_dsc_ids[alloc_count]))
				continue;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
			if (!_sde_rm_check_dsc(rm, enc_id,
					 iter_j.blk, iter_i.blk))
#else
			if (!_sde_rm_check_dsc(rm, rsvp, iter_j.blk,
					 iter_i.blk, pp[alloc_count]))
#endif
				continue;

			SDE_DEBUG("blk id = %d, _dsc_ids[%d] = %d\n",
				iter_j.blk->id,
				alloc_count,
				_dsc_ids ? _dsc_ids[alloc_count] : -1);

			reserve_mask |= (1 << iter_j.blk->id);
			dsc[alloc_count++] = iter_j.blk;
			break;
		}

		/* Rollback primary DSC if peer is not found */
		if (!iter_j.hw) {
			reserve_mask &= ~(1 << iter_i.blk->id);
			--alloc_count;
		}
	}

	if (alloc_count != num_dsc_enc) {
		SDE_ERROR("couldn't reserve %d dsc blocks for enc id %d\n",
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
			num_dsc_enc, enc_id);
#else
			num_dsc_enc, rsvp->enc_id);
#endif
		return -EINVAL;
	}

	for (i = 0; i < alloc_count; i++) {
		if (!dsc[i])
			break;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		dsc[i]->enc_id = enc_id;
		SDE_EVT32(dsc[i]->type, enc_id, dsc[i]->id);
#else
		dsc[i]->rsvp_nxt = rsvp;
		SDE_EVT32(dsc[i]->type, rsvp->enc_id, dsc[i]->id);
#endif
	}

	return 0;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_reserve_vdc(
		struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		struct sde_rm_requirements *reqs,
		const struct sde_rm_topology_def *top,
		u8 *_vdc_ids)
#else
static int _sde_rm_reserve_vdc(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		struct sde_rm_requirements *reqs,
		const struct sde_rm_topology_def *top,
		u8 *_vdc_ids)
#endif
{
	struct sde_rm_hw_iter iter_i;
	struct sde_rm_hw_blk *vdc[MAX_BLOCKS];
	int alloc_count = 0;
	int num_vdc_enc = top->num_comp_enc;
	int i;

	if (!top->num_comp_enc)
		return 0;

	if (reqs->hw_res.comp_info->comp_type != MSM_DISPLAY_COMPRESSION_VDC)
		return 0;

	sde_rm_init_hw_iter(&iter_i, 0, SDE_HW_BLK_VDC);

	/* Find a VDC */
	while (alloc_count != num_vdc_enc &&
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
			_sde_rm_get_hw_locked(rm, state, &iter_i)) {
#else
			_sde_rm_get_hw_locked(rm, &iter_i)) {
#endif

		memset(&vdc, 0, sizeof(vdc));
		alloc_count = 0;

		if (_vdc_ids && (iter_i.blk->id != _vdc_ids[alloc_count]))
			continue;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		if (!_sde_rm_check_vdc(rm, enc_id, iter_i.blk))
#else
		if (!_sde_rm_check_vdc(rm, rsvp, iter_i.blk))
#endif
			continue;

		SDE_DEBUG("blk id = %d, _vdc_ids[%d] = %d\n",
			iter_i.blk->id,
			alloc_count,
			_vdc_ids ? _vdc_ids[alloc_count] : -1);

		vdc[alloc_count++] = iter_i.blk;
	}

	if (alloc_count != num_vdc_enc) {
		SDE_ERROR("couldn't reserve %d vdc blocks for enc id %d\n",
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
			num_vdc_enc, enc_id);
#else
			num_vdc_enc, rsvp->enc_id);
#endif
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(vdc); i++) {
		if (!vdc[i])
			break;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		vdc[i]->enc_id = enc_id;
		SDE_EVT32(vdc[i]->type, enc_id, vdc[i]->id);
#else
		vdc[i]->rsvp_nxt = rsvp;
		SDE_EVT32(vdc[i]->type, rsvp->enc_id, vdc[i]->id);
#endif
	}

	return 0;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_reserve_qdss(
		struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		const struct sde_rm_topology_def *top,
		u8 *_qdss_ids)
#else
static int _sde_rm_reserve_qdss(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		const struct sde_rm_topology_def *top,
		u8 *_qdss_ids)
#endif
{
	struct sde_rm_hw_iter iter;
	struct msm_drm_private *priv = rm->dev->dev_private;
	struct sde_kms *sde_kms;

	if (!priv->kms) {
		SDE_ERROR("invalid kms\n");
		return -EINVAL;
	}
	sde_kms = to_sde_kms(priv->kms);

	sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_QDSS);

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	while (_sde_rm_get_hw_locked(rm, state, &iter)) {
		if (RESERVED_BY_OTHER(iter.blk, enc_id))
#else
	while (_sde_rm_get_hw_locked(rm, &iter)) {
		if (RESERVED_BY_OTHER(iter.blk, rsvp))
#endif
			continue;

		SDE_DEBUG("blk id = %d\n", iter.blk->id);

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		iter.blk->enc_id = enc_id;
		SDE_EVT32(iter.blk->type, enc_id, iter.blk->id);
#else
		iter.blk->rsvp_nxt = rsvp;
		SDE_EVT32(iter.blk->type, rsvp->enc_id, iter.blk->id);
#endif
		return 0;
	}

	if (!iter.hw && sde_kms->catalog->qdss_count) {
		SDE_DEBUG("couldn't reserve qdss for type %d id %d\n",
						SDE_HW_BLK_QDSS, iter.blk->id);
		return -ENAVAIL;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_reserve_cdm(
		struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		uint32_t id,
		enum sde_hw_blk_type type)
#else
static int _sde_rm_reserve_cdm(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		uint32_t id,
		enum sde_hw_blk_type type)
#endif
{
	struct sde_rm_hw_iter iter;

	sde_rm_init_hw_iter(&iter, 0, SDE_HW_BLK_CDM);
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	while (_sde_rm_get_hw_locked(rm, state, &iter)) {
#else
	while (_sde_rm_get_hw_locked(rm, &iter)) {
#endif
		const struct sde_hw_cdm *cdm = to_sde_hw_cdm(iter.blk->hw);
		const struct sde_cdm_cfg *caps = cdm->caps;
		bool match = false;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		if (RESERVED_BY_OTHER(iter.blk, enc_id))
#else
		if (RESERVED_BY_OTHER(iter.blk, rsvp))
#endif
			continue;

		if (type == SDE_HW_BLK_INTF && id != INTF_MAX)
			match = test_bit(id, &caps->intf_connect);
		else if (type == SDE_HW_BLK_WB && id != WB_MAX)
			match = test_bit(id, &caps->wb_connect);

		SDE_DEBUG("type %d id %d, cdm intfs %lu wbs %lu match %d\n",
				type, id, caps->intf_connect, caps->wb_connect,
				match);

		if (!match)
			continue;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		iter.blk->enc_id = enc_id;
		SDE_EVT32(iter.blk->type, enc_id, iter.blk->id);
#else
		iter.blk->rsvp_nxt = rsvp;
		SDE_EVT32(iter.blk->type, rsvp->enc_id, iter.blk->id);
#endif
		break;
	}

	if (!iter.hw) {
		SDE_ERROR("couldn't reserve cdm for type %d id %d\n", type, id);
		return -ENAVAIL;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_reserve_intf_or_wb(
		struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		uint32_t id,
		enum sde_hw_blk_type type,
		bool needs_cdm)
#else
static int _sde_rm_reserve_intf_or_wb(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		uint32_t id,
		enum sde_hw_blk_type type,
		bool needs_cdm)
#endif
{
	struct sde_rm_hw_iter iter;
	int ret = 0;

	/* Find the block entry in the rm, and note the reservation */
	sde_rm_init_hw_iter(&iter, 0, type);
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	while (_sde_rm_get_hw_locked(rm, state, &iter)) {
#else
	while (_sde_rm_get_hw_locked(rm, &iter)) {
#endif
		if (iter.blk->id != id)
			continue;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		if (RESERVED_BY_OTHER(iter.blk, enc_id)) {
#else
		if (RESERVED_BY_OTHER(iter.blk, rsvp)) {
#endif
			SDE_ERROR("type %d id %d already reserved\n", type, id);
			return -ENAVAIL;
		}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		iter.blk->enc_id = enc_id;
		SDE_EVT32(iter.blk->type, enc_id, iter.blk->id);
#else
		iter.blk->rsvp_nxt = rsvp;
		SDE_EVT32(iter.blk->type, rsvp->enc_id, iter.blk->id);
#endif
		break;
	}

	/* Shouldn't happen since wbs / intfs are fixed at probe */
	if (!iter.hw) {
		SDE_ERROR("couldn't find type %d id %d\n", type, id);
		return -EINVAL;
	}

	/* Expected only one intf or wb will request cdm */
	if (needs_cdm)
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		ret = _sde_rm_reserve_cdm(rm, state, enc_id, id, type);
#else
		ret = _sde_rm_reserve_cdm(rm, rsvp, id, type);
#endif

	return ret;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_reserve_intf_related_hw(
		struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		struct sde_encoder_hw_resources *hw_res)
#else
static int _sde_rm_reserve_intf_related_hw(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		struct sde_encoder_hw_resources *hw_res)
#endif
{
	int i, ret = 0;
	u32 id;

	for (i = 0; i < ARRAY_SIZE(hw_res->intfs); i++) {
		if (hw_res->intfs[i] == INTF_MODE_NONE)
			continue;
		id = i + INTF_0;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		ret = _sde_rm_reserve_intf_or_wb(rm, state, enc_id, id,
#else
		ret = _sde_rm_reserve_intf_or_wb(rm, rsvp, id,
#endif
				SDE_HW_BLK_INTF, hw_res->needs_cdm);
		if (ret)
			return ret;
	}

	for (i = 0; i < ARRAY_SIZE(hw_res->wbs); i++) {
		if (hw_res->wbs[i] == INTF_MODE_NONE)
			continue;
		id = i + WB_0;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		ret = _sde_rm_reserve_intf_or_wb(rm, state, enc_id, id,
#else
		ret = _sde_rm_reserve_intf_or_wb(rm, rsvp, id,
#endif
				SDE_HW_BLK_WB, hw_res->needs_cdm);
		if (ret)
			return ret;
	}

	return ret;
}

static bool _sde_rm_is_display_in_cont_splash(struct sde_kms *sde_kms,
		struct drm_encoder *enc)
{
	int i;
	struct sde_splash_display *splash_dpy;

	for (i = 0; i < MAX_DSI_DISPLAYS; i++) {
		splash_dpy = &sde_kms->splash_data.splash_display[i];
		if (splash_dpy->encoder ==  enc)
			return splash_dpy->cont_splash_enabled;
	}

	return false;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_make_lm_rsvp(struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		struct sde_rm_requirements *reqs,
		struct sde_splash_display *splash_display)
#else
static int _sde_rm_make_lm_rsvp(struct sde_rm *rm, struct sde_rm_rsvp *rsvp,
		struct sde_rm_requirements *reqs,
		struct sde_splash_display *splash_display)
#endif
{
	int ret, i;
	u8 *hw_ids = NULL;

	/* Check if splash data provided lm_ids */
	if (splash_display) {
		hw_ids = splash_display->lm_ids;
		for (i = 0; i < splash_display->lm_cnt; i++)
			SDE_DEBUG("splash_display->lm_ids[%d] = %d\n",
				i, splash_display->lm_ids[i]);

		if (splash_display->lm_cnt != reqs->topology->num_lm)
			SDE_DEBUG("Configured splash LMs != needed LM cnt\n");
	}

	/*
	 * Assign LMs and blocks whose usage is tied to them:
	 * DSPP & Pingpong.
	 */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	ret = _sde_rm_reserve_lms(rm, state, enc_id, reqs, hw_ids);
#else
	ret = _sde_rm_reserve_lms(rm, rsvp, reqs, hw_ids);
#endif

	return ret;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_make_ctl_rsvp(struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		struct sde_rm_requirements *reqs,
		struct sde_splash_display *splash_display)
#else
static int _sde_rm_make_ctl_rsvp(struct sde_rm *rm, struct sde_rm_rsvp *rsvp,
		struct sde_rm_requirements *reqs,
		struct sde_splash_display *splash_display)
#endif
{
	int ret, i;
	u8 *hw_ids = NULL;
	struct sde_rm_topology_def topology;

	/* Check if splash data provided ctl_ids */
	if (splash_display) {
		hw_ids = splash_display->ctl_ids;
		for (i = 0; i < splash_display->ctl_cnt; i++)
			SDE_DEBUG("splash_display->ctl_ids[%d] = %d\n",
				i, splash_display->ctl_ids[i]);
	}

	/*
	 * Do assignment preferring to give away low-resource CTLs first:
	 * - Check mixers without Split Display
	 * - Only then allow to grab from CTLs with split display capability
	 */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	ret = _sde_rm_reserve_ctls(rm, state, enc_id, reqs, reqs->topology, hw_ids);
#else
	ret = _sde_rm_reserve_ctls(rm, rsvp, reqs, reqs->topology, hw_ids);
#endif
	if (ret && !reqs->topology->needs_split_display &&
			reqs->topology->num_ctl > SINGLE_CTL) {
		memcpy(&topology, reqs->topology, sizeof(topology));
		topology.needs_split_display = true;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		ret = _sde_rm_reserve_ctls(rm, state, enc_id, reqs, &topology, hw_ids);
#else
		ret = _sde_rm_reserve_ctls(rm, rsvp, reqs, &topology, hw_ids);
#endif
	}

	return ret;
}

/*
 * Returns number of dsc hw blocks previously  owned by this encoder.
 * Returns 0 if not found  or error
 */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_find_prev_dsc(struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		u8 *prev_dsc, u32 max_cnt)
#else
static int _sde_rm_find_prev_dsc(struct sde_rm *rm, struct sde_rm_rsvp *rsvp,
		u8 *prev_dsc, u32 max_cnt)
#endif
{
	int i = 0;
	struct sde_rm_hw_iter iter_dsc;

	if ((!prev_dsc) || (max_cnt < MAX_DATA_PATH_PER_DSIPLAY))
		return 0;

	sde_rm_init_hw_iter(&iter_dsc, 0, SDE_HW_BLK_DSC);

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	while (_sde_rm_get_hw_locked(rm, state, &iter_dsc)) {
		if (RESERVED_BY_CURRENT(iter_dsc.blk, enc_id))
#else
	while (_sde_rm_get_hw_locked(rm, &iter_dsc)) {
		if (RESERVED_BY_CURRENT(iter_dsc.blk, rsvp))
#endif
			prev_dsc[i++] = iter_dsc.blk->id;

		if (i >= MAX_DATA_PATH_PER_DSIPLAY)
		       return 0;
	}

	return i;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_make_dsc_rsvp(struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		struct sde_rm_requirements *reqs,
		struct sde_splash_display *splash_display)
#else
static int _sde_rm_make_dsc_rsvp(struct sde_rm *rm, struct sde_rm_rsvp *rsvp,
		struct sde_rm_requirements *reqs,
		struct sde_splash_display *splash_display)
#endif
{
	int i;
	u8 *hw_ids = NULL;
	u8 prev_dsc[MAX_DATA_PATH_PER_DSIPLAY] = {0,};

	/* Check if splash data provided dsc_ids */
	if (splash_display) {
		hw_ids = splash_display->dsc_ids;
		if (splash_display->dsc_cnt)
			reqs->hw_res.comp_info->comp_type =
				MSM_DISPLAY_COMPRESSION_DSC;
		for (i = 0; i < splash_display->dsc_cnt; i++)
			SDE_DEBUG("splash_data.dsc_ids[%d] = %d\n",
				i, splash_display->dsc_ids[i]);
	}

	/*
	 * find if this encoder has previously allocated dsc hw blocks, use same dsc blocks
	 * if found to avoid switching dsc encoders during each modeset, as currently we
	 * dont have feasible way of decoupling previously owned dsc blocks by resetting
	 * respective dsc encoders mux control and flush them from commit path
	 */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	if (!hw_ids && _sde_rm_find_prev_dsc(rm, state, enc_id, prev_dsc, MAX_DATA_PATH_PER_DSIPLAY))
		return  _sde_rm_reserve_dsc(rm, state, enc_id, reqs, prev_dsc);
	else
		return  _sde_rm_reserve_dsc(rm, state, enc_id, reqs, hw_ids);
#else
	if (!hw_ids && _sde_rm_find_prev_dsc(rm, rsvp, prev_dsc, MAX_DATA_PATH_PER_DSIPLAY))
		return  _sde_rm_reserve_dsc(rm, rsvp, reqs, prev_dsc);
	else
		return  _sde_rm_reserve_dsc(rm, rsvp, reqs, hw_ids);
#endif

}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_make_vdc_rsvp(struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id,
		struct sde_rm_requirements *reqs,
		struct sde_splash_display *splash_display)
#else
static int _sde_rm_make_vdc_rsvp(struct sde_rm *rm, struct sde_rm_rsvp *rsvp,
		struct sde_rm_requirements *reqs,
		struct sde_splash_display *splash_display)
#endif
{
	int ret, i;
	u8 *hw_ids = NULL;

	/* Check if splash data provided vdc_ids */
	if (splash_display) {
		hw_ids = splash_display->vdc_ids;
		for (i = 0; i < splash_display->vdc_cnt; i++)
			SDE_DEBUG("splash_data.vdc_ids[%d] = %d\n",
				i, splash_display->vdc_ids[i]);
	}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	ret = _sde_rm_reserve_vdc(rm, state, enc_id, reqs, reqs->topology, hw_ids);
#else
	ret = _sde_rm_reserve_vdc(rm, rsvp, reqs, reqs->topology, hw_ids);
#endif

	return ret;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_make_next_rsvp(struct sde_rm *rm,
		struct sde_rm_state *state,
		struct drm_encoder *enc,
		struct drm_crtc_state *crtc_state,
		struct drm_connector_state *conn_state,
		struct sde_rm_requirements *reqs)
#else
static int _sde_rm_make_next_rsvp(struct sde_rm *rm, struct drm_encoder *enc,
		struct drm_crtc_state *crtc_state,
		struct drm_connector_state *conn_state,
		struct sde_rm_rsvp *rsvp,
		struct sde_rm_requirements *reqs)
#endif
{
	struct msm_drm_private *priv;
	struct sde_kms *sde_kms;
	struct sde_splash_display *splash_display = NULL;
	struct sde_splash_data *splash_data;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	uint32_t enc_id = enc->base.id;
#endif
	int i, ret;

	priv = enc->dev->dev_private;
	sde_kms = to_sde_kms(priv->kms);
	splash_data = &sde_kms->splash_data;

	if (_sde_rm_is_display_in_cont_splash(sde_kms, enc)) {
		for (i = 0; i < ARRAY_SIZE(splash_data->splash_display); i++) {
			if (enc == splash_data->splash_display[i].encoder)
				splash_display =
					&splash_data->splash_display[i];
		}
		if (!splash_display) {
			SDE_ERROR("rm is in cont_splash but data not found\n");
			return -EINVAL;
		}
	}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	ret = _sde_rm_make_lm_rsvp(rm, state, enc_id, reqs, splash_display);
#else
	/* Create reservation info, tag reserved blocks with it as we go */
	rsvp->seq = ++rm->rsvp_next_seq;
	rsvp->enc_id = enc->base.id;
	rsvp->topology = reqs->topology->top_name;
	rsvp->pending = true;
	list_add_tail(&rsvp->list, &rm->rsvps);

	ret = _sde_rm_make_lm_rsvp(rm, rsvp, reqs, splash_display);
#endif
	if (ret) {
		SDE_ERROR("unable to find appropriate mixers\n");
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		_sde_rm_print_rsvps_by_type(state, SDE_HW_BLK_LM);
#else
		_sde_rm_print_rsvps_by_type(rm, SDE_HW_BLK_LM);
#endif
		return ret;
	}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	ret = _sde_rm_make_ctl_rsvp(rm, state, enc_id, reqs, splash_display);
#else
	ret = _sde_rm_make_ctl_rsvp(rm, rsvp, reqs, splash_display);
#endif
	if (ret) {
		SDE_ERROR("unable to find appropriate CTL\n");
		return ret;
	}

	/* Assign INTFs, WBs, and blks whose usage is tied to them: CTL & CDM */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	ret = _sde_rm_reserve_intf_related_hw(rm, state, enc_id, &reqs->hw_res);
#else
	ret = _sde_rm_reserve_intf_related_hw(rm, rsvp, &reqs->hw_res);
#endif
	if (ret)
		return ret;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	ret = _sde_rm_make_dsc_rsvp(rm, state, enc_id, reqs, splash_display);
#else
	ret = _sde_rm_make_dsc_rsvp(rm, rsvp, reqs, splash_display);
#endif
	if (ret)
		return ret;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	ret = _sde_rm_make_vdc_rsvp(rm, state, enc_id, reqs, splash_display);
#else
	ret = _sde_rm_make_vdc_rsvp(rm, rsvp, reqs, splash_display);
#endif
	if (ret)
		return ret;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	ret = _sde_rm_reserve_qdss(rm, state, enc_id, reqs->topology, NULL);
#else
	ret = _sde_rm_reserve_qdss(rm, rsvp, reqs->topology, NULL);
#endif
	if (ret)
		return ret;

	return ret;
}

static int _sde_rm_update_active_only_pipes(
		struct sde_splash_display *splash_display,
		u32 active_pipes_mask)
{
	struct sde_sspp_index_info *pipe_info;
	int i;

	if (!active_pipes_mask) {
		return 0;
	} else if (!splash_display) {
		SDE_ERROR("invalid splash display provided\n");
		return -EINVAL;
	}

	pipe_info = &splash_display->pipe_info;
	for (i = SSPP_VIG0; i < SSPP_MAX; i++) {
		if (!(active_pipes_mask & BIT(i)))
			continue;

		if (test_bit(i, pipe_info->pipes) || test_bit(i, pipe_info->virt_pipes))
			continue;

		/*
		 * A pipe is active but not staged indicates a non-pixel
		 * plane. Register both rectangles as we can't differentiate
		 */
		set_bit(i, pipe_info->pipes);
		set_bit(i, pipe_info->virt_pipes);
		SDE_DEBUG("pipe %d is active:0x%x but not staged\n", i, active_pipes_mask);
	}

	return 0;
}

/**
 * _sde_rm_get_hw_blk_for_cont_splash - retrieve the LM blocks on given CTL
 * and populate the connected HW blk ids in sde_splash_display
 * @rm:	Pointer to resource manager structure
 * @state: Pointer to resource manager state structure
 * @ctl: Pointer to CTL hardware block
 * @splash_display: Pointer to struct sde_splash_display
 * return: number of active LM blocks for this CTL block
 */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static int _sde_rm_get_hw_blk_for_cont_splash(struct sde_rm *rm,
		struct sde_rm_state *state,
		struct sde_hw_ctl *ctl,
		struct sde_splash_display *splash_display)
#else
static int _sde_rm_get_hw_blk_for_cont_splash(struct sde_rm *rm,
		struct sde_hw_ctl *ctl,
		struct sde_splash_display *splash_display)
#endif
{
	u32 active_pipes_mask = 0;
	struct sde_rm_hw_iter iter_lm, iter_dsc;
	struct sde_kms *sde_kms;
	size_t pipes_per_lm;

	if (!rm || !ctl || !splash_display) {
		SDE_ERROR("invalid input parameters\n");
		return 0;
	}

	sde_kms = container_of(rm, struct sde_kms, rm);

	sde_rm_init_hw_iter(&iter_lm, 0, SDE_HW_BLK_LM);
	sde_rm_init_hw_iter(&iter_dsc, 0, SDE_HW_BLK_DSC);
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	while (_sde_rm_get_hw_locked(rm, state, &iter_lm)) {
#else
	while (_sde_rm_get_hw_locked(rm, &iter_lm)) {
#endif
		if (splash_display->lm_cnt >= MAX_DATA_PATH_PER_DSIPLAY)
			break;

		if (ctl->ops.get_staged_sspp) {
			// reset bordercolor from previous LM
			splash_display->pipe_info.bordercolor = false;
			pipes_per_lm = ctl->ops.get_staged_sspp(
					ctl, iter_lm.blk->id,
					&splash_display->pipe_info);
			if (pipes_per_lm ||
					splash_display->pipe_info.bordercolor) {
				splash_display->lm_ids[splash_display->lm_cnt++] =
					iter_lm.blk->id;
				SDE_DEBUG("lm_cnt=%d lm_id %d pipe_cnt%d\n",
						splash_display->lm_cnt,
						iter_lm.blk->id - LM_0,
						pipes_per_lm);
			}
		}
	}

	if (ctl->ops.get_active_pipes)
		active_pipes_mask = ctl->ops.get_active_pipes(ctl);

	if (_sde_rm_update_active_only_pipes(splash_display, active_pipes_mask))
		return 0;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	while (_sde_rm_get_hw_locked(rm, state, &iter_dsc)) {
#else
	while (_sde_rm_get_hw_locked(rm, &iter_dsc)) {
#endif
		if (ctl->ops.read_active_status &&
				!(ctl->ops.read_active_status(ctl,
					SDE_HW_BLK_DSC,
					iter_dsc.blk->id)))
			continue;

		splash_display->dsc_ids[splash_display->dsc_cnt++] =
				iter_dsc.blk->id;
		SDE_DEBUG("CTL[%d] path, using dsc[%d]\n",
				ctl->idx,
				iter_dsc.blk->id - DSC_0);
	}

	return splash_display->lm_cnt;
}

int sde_rm_cont_splash_res_init(struct msm_drm_private *priv,
				struct sde_rm *rm,
				struct sde_splash_data *splash_data,
				struct sde_mdss_cfg *cat)
{
	struct sde_rm_hw_iter iter_c;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_rm_state *state;
#endif
	int index = 0, ctl_top_cnt, splash_disp_count = 0;
	struct sde_kms *sde_kms = NULL;
	struct sde_hw_mdp *hw_mdp;
	struct sde_splash_display *splash_display;
	u8 intf_sel;

	if (!priv || !rm || !cat || !splash_data) {
		SDE_ERROR("invalid input parameters\n");
		return -EINVAL;
	}

	SDE_DEBUG("mixer_count=%d, ctl_count=%d, dsc_count=%d\n",
			cat->mixer_count,
			cat->ctl_count,
			cat->dsc_count);

	ctl_top_cnt = cat->ctl_count;

	if (!priv->kms) {
		SDE_ERROR("invalid kms\n");
		return -EINVAL;
	}
	sde_kms = to_sde_kms(priv->kms);

	hw_mdp = sde_rm_get_mdp(rm);

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	state = to_sde_rm_priv_state(rm->obj.state);
#endif

	sde_rm_init_hw_iter(&iter_c, 0, SDE_HW_BLK_CTL);
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	while (_sde_rm_get_hw_locked(rm, state, &iter_c)
#else
	while (_sde_rm_get_hw_locked(rm, &iter_c)
#endif
			&& (splash_disp_count < splash_data->num_splash_displays)) {
		struct sde_hw_ctl *ctl = to_sde_hw_ctl(iter_c.blk->hw);

		if (!ctl->ops.get_ctl_intf) {
			SDE_ERROR("get_ctl_intf not initialized\n");
			return -EINVAL;
		}

		intf_sel = ctl->ops.get_ctl_intf(ctl);
		if (intf_sel) {
			splash_display =
				&splash_data->splash_display[index ? 1 : 0];
			SDE_DEBUG("finding resources for display=%d ctl=%d\n",
					index, iter_c.blk->id - CTL_0);

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
			_sde_rm_get_hw_blk_for_cont_splash(rm, state,
#else
			_sde_rm_get_hw_blk_for_cont_splash(rm,
#endif
					ctl, splash_display);
			splash_display->cont_splash_enabled = true;
			splash_display->ctl_ids[splash_display->ctl_cnt++] =
				iter_c.blk->id;
			splash_disp_count++;
		}
		index++;
	}

	return 0;
}

static struct drm_connector *_sde_rm_get_connector(
		struct drm_encoder *enc)
{
	struct drm_connector *conn = NULL, *conn_search;
	struct sde_connector *c_conn = NULL;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(enc->dev, &conn_iter);
	drm_for_each_connector_iter(conn_search, &conn_iter) {
		c_conn = to_sde_connector(conn_search);
		if (c_conn->encoder == enc) {
			conn = conn_search;
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return conn;
}

static int _sde_rm_populate_requirements(
		struct sde_rm *rm,
		struct drm_encoder *enc,
		struct drm_crtc_state *crtc_state,
		struct drm_connector_state *conn_state,
		struct sde_mdss_cfg *cfg,
		struct sde_rm_requirements *reqs)
{
	const struct drm_display_mode *mode = &crtc_state->mode;
	struct drm_encoder *encoder_iter;
	struct drm_connector *conn;
	int i, num_lm;

	reqs->top_ctrl = sde_connector_get_property(conn_state,
			CONNECTOR_PROP_TOPOLOGY_CONTROL);
	sde_encoder_get_hw_resources(enc, &reqs->hw_res, conn_state);

	for (i = 0; i < SDE_RM_TOPOLOGY_MAX; i++) {
		if (RM_IS_TOPOLOGY_MATCH(rm->topology_tbl[i],
					reqs->hw_res.topology)) {
			reqs->topology = &rm->topology_tbl[i];
			break;
		}
	}

	if (!reqs->topology) {
		SDE_ERROR("invalid topology for the display\n");
		return -EINVAL;
	}

	/*
	 * select dspp HW block for all dsi displays and ds for only
	 * primary dsi display.
	 */
	if (conn_state->connector->connector_type == DRM_MODE_CONNECTOR_DSI) {
		if (!RM_RQ_DSPP(reqs))
			reqs->top_ctrl |= BIT(SDE_RM_TOPCTL_DSPP);

		if (!RM_RQ_DS(reqs) && rm->hw_mdp->caps->has_dest_scaler &&
		    sde_encoder_is_primary_display(enc))
			reqs->top_ctrl |= BIT(SDE_RM_TOPCTL_DS);
	}

	/**
	 * Set the requirement for LM which has CWB support if CWB is
	 * found enabled.
	 */
	if ((!RM_RQ_CWB(reqs) || !RM_RQ_DCWB(reqs))
				&& sde_crtc_state_in_clone_mode(enc, crtc_state)) {
		if (cfg->has_dedicated_cwb_support)
			reqs->top_ctrl |= BIT(SDE_RM_TOPCTL_DCWB);
		else
			reqs->top_ctrl |= BIT(SDE_RM_TOPCTL_CWB);

		/*
		 * topology selection based on conn mode is not valid for CWB
		 * as WB conn populates modes based on max_mixer_width check
		 * but primary can be using dual LMs. This topology override for
		 * CWB is to check number of datapath active in primary and
		 * allocate same number of LM/PP blocks reserved for CWB
		 */
		reqs->topology =
			&rm->topology_tbl[SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE];

		num_lm = sde_crtc_get_num_datapath(crtc_state->crtc,
				conn_state->connector, crtc_state);

		if (num_lm == 1)
			reqs->topology =
				&rm->topology_tbl[SDE_RM_TOPOLOGY_SINGLEPIPE];
		else if (num_lm == 0)
			SDE_ERROR("Primary layer mixer is not set\n");

		SDE_EVT32(num_lm, reqs->topology->num_lm,
			reqs->topology->top_name, reqs->topology->num_ctl);
	}

	if (RM_RQ_DCWB(reqs)) {
		drm_for_each_encoder_mask(encoder_iter, enc->dev,
					 crtc_state->encoder_mask) {
			if (drm_encoder_mask(encoder_iter) == drm_encoder_mask(enc))
				continue;

			conn = _sde_rm_get_connector(encoder_iter);
			if (conn)
				reqs->conn_lm_mask = to_sde_connector(conn)->lm_mask;
			break;
		}
	}

	SDE_DEBUG("top_ctrl: 0x%llX num_h_tiles: %d\n", reqs->top_ctrl,
			reqs->hw_res.display_num_of_h_tiles);
	SDE_DEBUG("num_lm: %d num_ctl: %d topology: %d split_display: %d mask: 0x%llX\n",
			reqs->topology->num_lm, reqs->topology->num_ctl,
			reqs->topology->top_name,
			reqs->topology->needs_split_display, reqs->conn_lm_mask);
	SDE_EVT32(mode->hdisplay, rm->lm_max_width, reqs->topology->num_lm,
			reqs->top_ctrl, reqs->topology->top_name,
			reqs->topology->num_ctl, reqs->conn_lm_mask);

	return 0;
}

#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
static struct sde_rm_rsvp *_sde_rm_get_rsvp(struct sde_rm *rm, struct drm_encoder *enc, bool nxt)
{
	struct sde_rm_rsvp *i;

	if (!rm || !enc) {
		SDE_ERROR("invalid params\n");
		return NULL;
	}

	if (list_empty(&rm->rsvps))
		return NULL;

	list_for_each_entry(i, &rm->rsvps, list)
		if (i->pending == nxt && i->enc_id == enc->base.id)
			return i;

	return NULL;
}

static struct sde_rm_rsvp *_sde_rm_get_rsvp_nxt(struct sde_rm *rm, struct drm_encoder *enc)
{
        return _sde_rm_get_rsvp(rm, enc, true);
}

static struct sde_rm_rsvp *_sde_rm_get_rsvp_cur(struct sde_rm *rm, struct drm_encoder *enc)
{
	return _sde_rm_get_rsvp(rm, enc, false);
}
#endif

int sde_rm_update_topology(struct sde_rm *rm,
	struct drm_connector_state *conn_state,
	struct msm_display_topology *topology)
{
	int i, ret = 0;
	struct msm_display_topology top;
	enum sde_rm_topology_name top_name = SDE_RM_TOPOLOGY_NONE;

	if (!conn_state)
		return -EINVAL;

	if (topology) {
		top = *topology;
		for (i = 0; i < SDE_RM_TOPOLOGY_MAX; i++)
			if (RM_IS_TOPOLOGY_MATCH(rm->topology_tbl[i], top)) {
				top_name = rm->topology_tbl[i].top_name;
				break;
			}
	}

	ret = msm_property_set_property(
			sde_connector_get_propinfo(conn_state->connector),
			sde_connector_get_property_state(conn_state),
			CONNECTOR_PROP_TOPOLOGY_NAME, top_name);

	return ret;
}

bool sde_rm_topology_is_group(struct sde_rm *rm,
		struct drm_crtc_state *state,
		enum sde_rm_topology_group group)
{
	int i, ret = 0;
	struct sde_crtc_state *cstate;
	struct drm_connector *conn;
	struct drm_connector_state *conn_state;
	struct msm_display_topology topology;
	enum sde_rm_topology_name name;

	if ((!rm) || (!state) || (!state->state)) {
		pr_err("invalid arguments: rm:%d state:%d atomic state:%d\n",
				!rm, !state, state ? (!state->state) : 0);
		return false;
	}

	cstate = to_sde_crtc_state(state);

	for (i = 0; i < cstate->num_connectors; i++) {
		conn = cstate->connectors[i];
		if (!conn) {
			SDE_DEBUG("invalid connector\n");
			continue;
		}

		conn_state = drm_atomic_get_new_connector_state(state->state,
				conn);
		if (!conn_state) {
			SDE_DEBUG("%s invalid connector state\n", conn->name);
			continue;
		}

		ret = sde_connector_state_get_topology(conn_state, &topology);
		if (ret) {
			SDE_DEBUG("%s invalid topology\n", conn->name);
			continue;
		}

		name = sde_rm_get_topology_name(rm, topology);
		switch (group) {
		case SDE_RM_TOPOLOGY_GROUP_SINGLEPIPE:
			if (TOPOLOGY_SINGLEPIPE_MODE(name))
				return true;
			break;
		case SDE_RM_TOPOLOGY_GROUP_DUALPIPE:
			if (TOPOLOGY_DUALPIPE_MODE(name))
				return true;
			break;
		case SDE_RM_TOPOLOGY_GROUP_QUADPIPE:
			if (TOPOLOGY_QUADPIPE_MODE(name))
				return true;
			break;
		case SDE_RM_TOPOLOGY_GROUP_3DMERGE:
			if (topology.num_lm > topology.num_intf &&
					!topology.num_enc)
				return true;
			break;
		case SDE_RM_TOPOLOGY_GROUP_3DMERGE_DSC:
			if (topology.num_lm > topology.num_enc &&
					topology.num_enc)
				return true;
			break;
		case SDE_RM_TOPOLOGY_GROUP_DSCMERGE:
			if (topology.num_lm == topology.num_enc &&
					topology.num_enc)
				return true;
			break;
		default:
			SDE_ERROR("invalid topology group\n");
			return false;
		}
	}

	return false;
}

/**
 * _sde_rm_release_rsvp - release resources and release a reservation
 * @rm:	KMS handle
 * @rsvp:	RSVP pointer to release and release resources for
 * @enc_id:	Reservations are tracked by Encoder DRM object ID.
 */
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static void _sde_rm_release_rsvp(
		struct sde_rm *rm,
		struct sde_rm_state *state,
		uint32_t enc_id)
#else
static void _sde_rm_release_rsvp(
		struct sde_rm *rm,
		struct sde_rm_rsvp *rsvp,
		struct drm_connector *conn)
#endif
{
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_rm_hw_blk *blk, *p;
#else
	struct sde_rm_rsvp *rsvp_c, *rsvp_n;
	struct sde_rm_hw_blk *blk;
#endif
	enum sde_hw_blk_type type;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	SDE_DEBUG("rel enc %d\n", enc_id);
#else
	if (!rsvp)
		return;

	SDE_DEBUG("rel rsvp %d enc %d\n", rsvp->seq, rsvp->enc_id);

	list_for_each_entry_safe(rsvp_c, rsvp_n, &rm->rsvps, list) {
		if (rsvp == rsvp_c) {
			list_del(&rsvp_c->list);
			break;
		}
	}
#endif

	for (type = 0; type < SDE_HW_BLK_MAX; type++) {
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		list_for_each_entry_safe(blk, p, &state->hw_blks[type], list) {
			if (blk->enc_id == enc_id) {
				/*
				 * external block is created at reserve time
				 * and destroyed at release time.
				 */
				if (blk->ext_hw) {
					SDE_DEBUG("remove enc %d %d %d\n",
							enc_id,
							blk->type, blk->id);
					list_del(&blk->list);
					kfree(blk);
					continue;
				}

				blk->enc_id = 0;
				SDE_DEBUG("rel enc %d %d %d\n",
						enc_id,
#else
		list_for_each_entry(blk, &rm->hw_blks[type], list) {
			if (blk->rsvp == rsvp) {
				blk->rsvp = NULL;
				SDE_DEBUG("rel rsvp %d enc %d %d %d\n",
						rsvp->seq, rsvp->enc_id,
#endif
						blk->type, blk->id);

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
				_sde_rm_inc_resource_info(state,
#else
				_sde_rm_inc_resource_info(rm,
#endif
						&rm->avail_res, blk);
			}
#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
			if (blk->rsvp_nxt == rsvp) {
				blk->rsvp_nxt = NULL;
				SDE_DEBUG("rel rsvp_nxt %d enc %d %d %d\n",
						rsvp->seq, rsvp->enc_id,
						blk->type, blk->id);
			}
#endif
		}
	}

#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
	kfree(rsvp);
#endif
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
int sde_rm_release(struct sde_rm *rm,
		struct drm_encoder *enc,
		struct drm_atomic_state *atomic_state)
#else
void sde_rm_release(struct sde_rm *rm, struct drm_encoder *enc, bool nxt)
#endif
{
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_rm_state *state;
#else
	struct sde_rm_rsvp *rsvp;
	struct drm_connector *conn = NULL;
#endif
	struct msm_drm_private *priv;
	struct sde_kms *sde_kms;
#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
	uint64_t top_ctrl = 0;

	if (!rm || !enc) {
		SDE_ERROR("invalid params\n");
		return;
	}
#endif

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	if (!rm || !enc || !atomic_state) {
		SDE_ERROR("invalid arguments\n");
		return -EINVAL;
 	}
#endif

	priv = enc->dev->dev_private;
	if (!priv->kms) {
		SDE_ERROR("invalid kms\n");
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		return -EINVAL;
#else
		return;
#endif
	}
	sde_kms = to_sde_kms(priv->kms);

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	state = sde_rm_get_atomic_state(atomic_state, rm);
	if (IS_ERR(state))
		return PTR_ERR(state);

	_sde_rm_print_rsvps(state, SDE_RM_STAGE_BEGIN);
#else
	mutex_lock(&rm->rm_lock);

	rsvp = _sde_rm_get_rsvp(rm, enc, nxt);
	if (!rsvp) {
		SDE_DEBUG("failed to find rsvp for enc %d, nxt %d",
				enc->base.id, nxt);
		goto end;
	}
#endif

	if (_sde_rm_is_display_in_cont_splash(sde_kms, enc)) {
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		_sde_rm_release_rsvp(rm, state, enc->base.id);
#else
		_sde_rm_release_rsvp(rm, rsvp, conn);
#endif
		goto end;
	}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	_sde_rm_release_rsvp(rm, state, enc->base.id);

	_sde_rm_print_rsvps(state, SDE_RM_STAGE_AFTER_CLEAR);
#else
	conn = _sde_rm_get_connector(enc);
	if (!conn) {
		SDE_EVT32(enc->base.id, 0x0, 0xffffffff);
		_sde_rm_release_rsvp(rm, rsvp, conn);
		SDE_DEBUG("failed to get conn for enc %d nxt %d\n",
				enc->base.id, nxt);
		goto end;
	}

	top_ctrl = sde_connector_get_property(conn->state,
			CONNECTOR_PROP_TOPOLOGY_CONTROL);

	SDE_EVT32(enc->base.id, conn->base.id, rsvp->seq, top_ctrl, nxt);
	if (top_ctrl & BIT(SDE_RM_TOPCTL_RESERVE_LOCK)) {
		SDE_DEBUG("rsvp[s%de%d] not releasing locked resources\n",
				rsvp->seq, rsvp->enc_id);
	} else {
		SDE_DEBUG("release rsvp[s%de%d]\n", rsvp->seq,
				rsvp->enc_id);
		_sde_rm_release_rsvp(rm, rsvp, conn);
	}
#endif

end:
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	return 0;
#else
	mutex_unlock(&rm->rm_lock);
#endif
}

#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
static void _sde_rm_commit_rsvp(struct sde_rm *rm, struct sde_rm_rsvp *rsvp,
		struct drm_connector_state *conn_state)
{
	struct sde_rm_hw_blk *blk;
	enum sde_hw_blk_type type;

	/* Swap next rsvp to be the active */
	for (type = 0; type < SDE_HW_BLK_MAX; type++) {
		list_for_each_entry(blk, &rm->hw_blks[type], list) {
			if (blk->rsvp_nxt && conn_state->best_encoder->base.id
					 == blk->rsvp_nxt->enc_id) {
				blk->rsvp = blk->rsvp_nxt;
				blk->rsvp_nxt = NULL;
				_sde_rm_dec_resource_info(rm,
						&rm->avail_res, blk);
			}
		}
	}

	rsvp->pending = false;
	SDE_DEBUG("rsrv enc %d topology %d\n", rsvp->enc_id, rsvp->topology);
	SDE_EVT32(rsvp->enc_id, rsvp->topology);
}
#endif

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
static void _sde_rm_populate_dp_lm_mask(struct sde_rm *rm,
		struct sde_rm_state *state,
		struct drm_connector *conn)
#else
static void _sde_rm_populate_dp_lm_mask(struct sde_rm *rm,
		struct drm_connector *conn)
#endif
{
	struct sde_connector *c_conn = NULL;
	struct sde_rm_hw_blk *blk;

	if (!rm || !conn) {
		SDE_ERROR("invalid arguments\n");
		return;
	}
	if (conn->connector_type != DRM_MODE_CONNECTOR_DisplayPort)
		return;

	c_conn =  to_sde_connector(conn);
	if (!c_conn || !c_conn->encoder)
		return;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	list_for_each_entry(blk, &state->hw_blks[SDE_HW_BLK_LM], list) {
		if (!blk->enc_id)
#else
	list_for_each_entry(blk, &rm->hw_blks[SDE_HW_BLK_LM], list) {
		if (!blk->rsvp)
#endif
			continue;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
		if (blk->enc_id == c_conn->encoder->base.id)
#else
		if (blk->rsvp->enc_id == c_conn->encoder->base.id)
#endif
			c_conn->lm_mask |= BIT(blk->id - 1);
	}

	SDE_DEBUG("conn lm_mask %d for conn %d enc %d\n", c_conn->lm_mask,
			conn->base.id, c_conn->encoder->base.id);
	SDE_EVT32(c_conn->encoder->base.id, conn->base.id, c_conn->lm_mask);
}

#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
/* call this only after rm_mutex held */
struct sde_rm_rsvp *_sde_rm_poll_get_rsvp_nxt_locked(struct sde_rm *rm,
		struct drm_encoder *enc)
{
	int i;
	u32 loop_count = 20;
	struct sde_rm_rsvp *rsvp_nxt = NULL;
	u32 sleep = RM_NXT_CLEAR_POLL_TIMEOUT_US / loop_count;

	for (i = 0; i < loop_count; i++) {
		rsvp_nxt = _sde_rm_get_rsvp_nxt(rm, enc);
		if (!rsvp_nxt)
			return rsvp_nxt;

		mutex_unlock(&rm->rm_lock);
		SDE_DEBUG("iteration i:%d sleep range:%uus to %uus\n",
				i, sleep, sleep * 2);
		usleep_range(sleep, sleep * 2);
		mutex_lock(&rm->rm_lock);
	}
	/* make sure to get latest rsvp_next to avoid use after free issues  */
	return _sde_rm_get_rsvp_nxt(rm, enc);
}
#endif

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
int sde_rm_reserve(
		struct sde_rm *rm,
		struct drm_encoder *enc,
		struct drm_crtc_state *crtc_state,
		struct drm_connector_state *conn_state)
#else
int sde_rm_reserve(
		struct sde_rm *rm,
		struct drm_encoder *enc,
		struct drm_crtc_state *crtc_state,
		struct drm_connector_state *conn_state,
		bool test_only)
#endif
{
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_rm_state *state;
	struct sde_rm_requirements reqs;
#else
	struct sde_rm_rsvp *rsvp_cur, *rsvp_nxt;
	struct sde_rm_requirements reqs = {0,};
#endif
	struct msm_drm_private *priv;
	struct sde_kms *sde_kms;
#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	struct sde_connector *sde_conn;
#endif
	struct msm_compression_info *comp_info;
	int ret = 0;

	if (!rm || !enc || !crtc_state || !conn_state) {
		SDE_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	if (!enc->dev || !enc->dev->dev_private) {
		SDE_ERROR("drm device invalid\n");
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	/* shared connector doesn't have resources */
	sde_conn = to_sde_connector(conn_state->connector);
	if (sde_conn->shared)
		return 0;

	if (conn_state->state)
		state = sde_rm_get_atomic_state(conn_state->state, rm);
	else
		state = to_sde_rm_priv_state(rm->obj.state);

	if (IS_ERR(state))
		return PTR_ERR(state);
#endif

	priv = enc->dev->dev_private;
	if (!priv->kms) {
		SDE_ERROR("invalid kms\n");
		return -EINVAL;
	}
	sde_kms = to_sde_kms(priv->kms);

#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
	/* Check if this is just a page-flip */
	if (!_sde_rm_is_display_in_cont_splash(sde_kms, enc) &&
			!msm_atomic_needs_modeset(crtc_state, conn_state))
		return 0;
#endif

	comp_info = kzalloc(sizeof(*comp_info), GFP_KERNEL);
	if (!comp_info)
		return -ENOMEM;

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	SDE_DEBUG("reserving hw for conn %d enc %d crtc %d\n",
 			conn_state->connector->base.id, enc->base.id,
			crtc_state->crtc->base.id);
	SDE_EVT32(enc->base.id, conn_state->connector->base.id);

	_sde_rm_print_rsvps(state, SDE_RM_STAGE_BEGIN);
#else
	SDE_DEBUG("reserving hw for conn %d enc %d crtc %d test_only %d\n",
			conn_state->connector->base.id, enc->base.id,
			crtc_state->crtc->base.id, test_only);
	SDE_EVT32(enc->base.id, conn_state->connector->base.id, test_only);

	mutex_lock(&rm->rm_lock);

	_sde_rm_print_rsvps(rm, SDE_RM_STAGE_BEGIN);

	rsvp_cur = _sde_rm_get_rsvp_cur(rm, enc);
	rsvp_nxt = _sde_rm_get_rsvp_nxt(rm, enc);

	/*
	 * RM currently relies on rsvp_nxt assigned to the hw blocks to
	 * commit rsvps. This rsvp_nxt can be cleared by a back to back
	 * check_only commit with modeset when its predecessor atomic
	 * commit is delayed / not committed the reservation yet.
	 * Poll for rsvp_nxt clear, allow the check_only commit if rsvp_nxt
	 * gets cleared and bailout if it does not get cleared before timeout.
	 */
	if (test_only && rsvp_nxt) {
		rsvp_nxt = _sde_rm_poll_get_rsvp_nxt_locked(rm, enc);
		rsvp_cur = _sde_rm_get_rsvp_cur(rm, enc);
		if (rsvp_nxt) {
			pr_err("poll timeout cur %d nxt %d enc %d\n",
				(rsvp_cur) ? rsvp_cur->seq : -1,
				rsvp_nxt->seq, enc->base.id);
			SDE_EVT32(enc->base.id, (rsvp_cur) ? rsvp_cur->seq : -1,
					rsvp_nxt->seq, SDE_EVTLOG_ERROR);
			ret = -EAGAIN;
			goto end;
		}
	}

	if (!test_only && rsvp_nxt)
		goto commit_rsvp;
#endif

	reqs.hw_res.comp_info = comp_info;
	ret = _sde_rm_populate_requirements(rm, enc, crtc_state,
			conn_state, sde_kms->catalog, &reqs);
	if (ret) {
		SDE_ERROR("failed to populate hw requirements\n");
		goto end;
	}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
	if (reqs.topology->top_name == SDE_RM_TOPOLOGY_NONE)
		goto end;

	ret = _sde_rm_make_next_rsvp(rm, state,
		enc, crtc_state, conn_state, &reqs);

	_sde_rm_print_rsvps(state, SDE_RM_STAGE_AFTER_RSVPNEXT);
	_sde_rm_populate_dp_lm_mask(rm, state, conn_state->connector);
#else
	/*
	 * We only support one active reservation per-hw-block. But to implement
	 * transactional semantics for test-only, and for allowing failure while
	 * modifying your existing reservation, over the course of this
	 * function we can have two reservations:
	 * Current: Existing reservation
	 * Next: Proposed reservation. The proposed reservation may fail, or may
	 *       be discarded if in test-only mode.
	 * If reservation is successful, and we're not in test-only, then we
	 * replace the current with the next.
	 */
	rsvp_nxt = kzalloc(sizeof(*rsvp_nxt), GFP_KERNEL);
	if (!rsvp_nxt) {
		ret = -ENOMEM;
		goto end;
	}

	/*
	 * User can request that we clear out any reservation during the
	 * atomic_check phase by using this CLEAR bit
	 */
	if (rsvp_cur && test_only && RM_RQ_CLEAR(&reqs)) {
		SDE_DEBUG("test_only & CLEAR: clear rsvp[s%de%d]\n",
				rsvp_cur->seq, rsvp_cur->enc_id);
		_sde_rm_release_rsvp(rm, rsvp_cur, conn_state->connector);
		rsvp_cur = NULL;
		_sde_rm_print_rsvps(rm, SDE_RM_STAGE_AFTER_CLEAR);
	}

	/* Check the proposed reservation, store it in hw's "next" field */
	ret = _sde_rm_make_next_rsvp(rm, enc, crtc_state, conn_state,
			rsvp_nxt, &reqs);

	_sde_rm_print_rsvps(rm, SDE_RM_STAGE_AFTER_RSVPNEXT);

	if (ret) {
		SDE_ERROR("failed to reserve hw resources: %d, test_only %d\n",
				ret, test_only);
		_sde_rm_release_rsvp(rm, rsvp_nxt, conn_state->connector);
		goto end;
	} else if (test_only && !RM_RQ_LOCK(&reqs)) {
		/*
		 * Normally, if test_only, test the reservation and then undo
		 * However, if the user requests LOCK, then keep the reservation
		 * made during the atomic_check phase.
		 */
		SDE_DEBUG("test_only: rsvp[s%de%d]\n",
				rsvp_nxt->seq, rsvp_nxt->enc_id);
		goto end;
	} else {
		if (test_only && RM_RQ_LOCK(&reqs))
			SDE_DEBUG("test_only & LOCK: lock rsvp[s%de%d]\n",
					rsvp_nxt->seq, rsvp_nxt->enc_id);
	}

commit_rsvp:
	_sde_rm_release_rsvp(rm, rsvp_cur, conn_state->connector);
	_sde_rm_commit_rsvp(rm, rsvp_nxt, conn_state);
	_sde_rm_populate_dp_lm_mask(rm, conn_state->connector);
#endif

end:
	kfree(comp_info);
#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
	_sde_rm_print_rsvps(rm, SDE_RM_STAGE_FINAL);
	mutex_unlock(&rm->rm_lock);
#endif

	return ret;
}

#if IS_ENABLED(CONFIG_DRM_SDE_SHD)
int sde_rm_ext_blk_create_reserve(struct sde_rm *rm,
		struct drm_atomic_state *atomic_state,
		struct sde_hw_blk *hw, struct drm_encoder *enc)
{
	struct sde_rm_state *state;
	struct sde_rm_hw_blk *blk;
	int ret = 0;

	if (!rm || !hw || !enc) {
		SDE_ERROR("invalid parameters\n");
		return -EINVAL;
	}

	if (hw->type >= SDE_HW_BLK_MAX) {
		SDE_ERROR("invalid HW type\n");
		return -EINVAL;
	}

	state = sde_rm_get_atomic_state(atomic_state, rm);
	if (IS_ERR(state))
		return PTR_ERR(state);

	blk = kzalloc(sizeof(*blk), GFP_KERNEL);
	if (!blk) {
		ret = -ENOMEM;
		goto end;
	}

	blk->type = hw->type;
	blk->id = hw->id;
	blk->hw = hw;
	blk->enc_id = enc->base.id;
	blk->ext_hw = true;
	list_add_tail(&blk->list, &state->hw_blks[hw->type]);

	SDE_DEBUG("create blk %d %d for enc %d\n", blk->type, blk->id,
			enc->base.id);

end:
	return ret;
}
#endif

#if !IS_ENABLED(CONFIG_DRM_SDE_SHD)
int sde_rm_ext_blk_destroy(struct sde_rm *rm,
               struct drm_encoder *enc)
{
       struct sde_rm_hw_blk *blk = NULL, *p;
       struct sde_rm_rsvp *rsvp;
       enum sde_hw_blk_type type;
       int ret = 0;

       if (!rm || !enc) {
               SDE_ERROR("invalid parameters\n");
               return -EINVAL;
       }

       mutex_lock(&rm->rm_lock);

       rsvp = _sde_rm_get_rsvp_cur(rm, enc);
       if (!rsvp) {
               ret = -ENOENT;
               SDE_ERROR("failed to find rsvp for enc %d\n", enc->base.id);
               goto end;
       }

       for (type = 0; type < SDE_HW_BLK_MAX; type++) {
               list_for_each_entry_safe(blk, p, &rm->hw_blks[type], list) {
                       if (blk->rsvp == rsvp) {
                               list_del(&blk->list);
                               SDE_DEBUG("del blk %d %d from rsvp %d enc %d\n",
                                               blk->type, blk->id,
                                               rsvp->seq, rsvp->enc_id);
                               kfree(blk);
                       }
               }
       }

       SDE_DEBUG("del rsvp %d\n", rsvp->seq);
       list_del(&rsvp->list);
       kfree(rsvp);
end:
       mutex_unlock(&rm->rm_lock);
       return ret;
}
#endif
