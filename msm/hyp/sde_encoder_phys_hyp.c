// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__
#include "sde_encoder_phys.h"
#include "sde_formats.h"
#include "sde_trace.h"
#include "msm_drv.h"
#include <drm/drm_fixed.h>

#define SDE_DEBUG_HYPENC(e, fmt, ...) SDE_DEBUG("enc%d intf%d " fmt, \
		(e) && (e)->base.parent ? \
		(e)->base.parent->base.id : -1, \
		(e) && (e)->base.hw_intf ? \
		(e)->base.hw_intf->idx - INTF_0 : -1, ##__VA_ARGS__)

#define SDE_ERROR_HYPENC(e, fmt, ...) SDE_ERROR("enc%d intf%d " fmt, \
		(e) && (e)->base.parent ? \
		(e)->base.parent->base.id : -1, \
		(e) && (e)->base.hw_intf ? \
		(e)->base.hw_intf->idx - INTF_0 : -1, ##__VA_ARGS__)

#define to_sde_encoder_phys_hyp(x) \
	container_of(x, struct sde_encoder_phys_hyp, base)

/* Poll time to do recovery during active region */
#define POLL_TIME_USEC_FOR_LN_CNT 500
#define MAX_POLL_CNT 10

static bool sde_encoder_phys_hyp_is_master(
		struct sde_encoder_phys *phys_enc)
{
	bool ret = false;

	if (phys_enc->split_role != ENC_ROLE_SLAVE)
		ret = true;

	return ret;
}

static bool sde_encoder_phys_hyp_mode_fixup(
		struct sde_encoder_phys *phys_enc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adj_mode)
{
	if (phys_enc)
		SDE_DEBUG_HYPENC(to_sde_encoder_phys_hyp(phys_enc), "\n");

	/*
	 * Modifying mode has consequences when the mode comes back to us
	 */
	return true;
}

static void sde_encoder_phys_hyp_vblank_irq(void *arg, int irq_idx)
{
	struct sde_encoder_phys *phys_enc = arg;
	struct sde_hw_ctl *hw_ctl;
	struct intf_status intf_status = {0};
	struct sde_cesta_scc_status scc_status = {0, };
	struct sde_cesta_client *cesta_client = sde_encoder_get_cesta_client(phys_enc->parent);
	unsigned long lock_flags;
	u32 flush_register = ~0;
	u32 reset_status = 0;
	int new_cnt = -1, old_cnt = -1;
	u32 event = 0;
	int pend_ret_fence_cnt = 0;
	u32 fence_ready = -1;

	if (!phys_enc)
		return;

	hw_ctl = phys_enc->hw_ctl;
	if (!hw_ctl)
		return;

	SDE_ATRACE_BEGIN("vblank_irq");

	/*
	 * only decrement the pending flush count if we've actually flushed
	 * hardware. due to sw irq latency, vblank may have already happened
	 * so we need to double-check with hw that it accepted the flush bits
	 */
	spin_lock_irqsave(phys_enc->enc_spinlock, lock_flags);

	old_cnt = atomic_read(&phys_enc->pending_kickoff_cnt);

	if (hw_ctl->ops.get_flush_register)
		flush_register = hw_ctl->ops.get_flush_register(hw_ctl);

	new_cnt = atomic_add_unless(&phys_enc->pending_kickoff_cnt, -1, 0);
	pend_ret_fence_cnt = atomic_read(&phys_enc->pending_retire_fence_cnt);

	/* signal only for master, where there is a pending kickoff */
	if (sde_encoder_phys_hyp_is_master(phys_enc) &&
	    atomic_add_unless(&phys_enc->pending_retire_fence_cnt, -1, 0)) {
		event = SDE_ENCODER_FRAME_EVENT_DONE |
			SDE_ENCODER_FRAME_EVENT_SIGNAL_RETIRE_FENCE |
			SDE_ENCODER_FRAME_EVENT_SIGNAL_RELEASE_FENCE;
	}

	if (hw_ctl->ops.get_reset)
		reset_status = hw_ctl->ops.get_reset(hw_ctl);

	spin_unlock_irqrestore(phys_enc->enc_spinlock, lock_flags);

	if (event && phys_enc->parent_ops.handle_frame_done)
		phys_enc->parent_ops.handle_frame_done(phys_enc->parent,
			phys_enc, event);

	if (phys_enc->parent_ops.handle_vblank_virt)
		phys_enc->parent_ops.handle_vblank_virt(phys_enc->parent,
				phys_enc);

	if (phys_enc->hw_intf->ops.get_status)
		phys_enc->hw_intf->ops.get_status(phys_enc->hw_intf,
			&intf_status);

	if (flush_register && hw_ctl->ops.get_hw_fence_status)
		fence_ready = hw_ctl->ops.get_hw_fence_status(hw_ctl);

	SDE_EVT32_IRQ(DRMID(phys_enc->parent), phys_enc->hw_intf->idx - INTF_0,
			old_cnt, atomic_read(&phys_enc->pending_kickoff_cnt),
			reset_status ? SDE_EVTLOG_ERROR : 0,
			flush_register, event,
			atomic_read(&phys_enc->pending_retire_fence_cnt),
			intf_status.frame_count, intf_status.line_count,
			fence_ready, DPUID(phys_enc->sde_kms));
	if (cesta_client)
		sde_cesta_get_status(cesta_client, &scc_status);

	/* Signal any waiting atomic commit thread */
	wake_up_all(&phys_enc->pending_kickoff_wq);
	SDE_ATRACE_END("vblank_irq");
}

static void sde_encoder_phys_hyp_underrun_irq(void *arg, int irq_idx)
{
	struct sde_encoder_phys *phys_enc = arg;

	if (!phys_enc)
		return;

	if (phys_enc->parent_ops.handle_underrun_virt)
		phys_enc->parent_ops.handle_underrun_virt(phys_enc->parent,
			phys_enc);
}

static void _sde_encoder_phys_hyp_setup_irq_hw_idx(
		struct sde_encoder_phys *phys_enc)
{
	struct sde_encoder_irq *irq;

	/*
	 * Initialize irq->hw_idx only when irq is not registered.
	 * Prevent invalidating irq->irq_idx as modeset may be
	 * called many times during dfps.
	 */

	irq = &phys_enc->irq[INTR_IDX_VSYNC];
	if (irq->irq_idx < 0)
		irq->hw_idx = phys_enc->intf_idx;

	irq = &phys_enc->irq[INTR_IDX_UNDERRUN];
	if (irq->irq_idx < 0)
		irq->hw_idx = phys_enc->intf_idx;
}

static void sde_encoder_phys_hyp_mode_set(
		struct sde_encoder_phys *phys_enc,
		struct drm_display_mode *mode,
		struct drm_display_mode *adj_mode, bool *reinit_mixers)
{
	struct sde_rm *rm;
	struct sde_rm_hw_iter iter;
	int i, instance;
	struct sde_encoder_phys_hyp *hyp_enc;

	if (!phys_enc || !phys_enc->sde_kms) {
		SDE_ERROR("invalid encoder/kms\n");
		return;
	}

	rm = &phys_enc->sde_kms->rm;
	hyp_enc = to_sde_encoder_phys_hyp(phys_enc);

	if (adj_mode) {
		phys_enc->cached_mode = *adj_mode;
		drm_mode_debug_printmodeline(adj_mode);
		SDE_DEBUG_HYPENC(hyp_enc, "caching mode:\n");
	}

	instance = phys_enc->split_role == ENC_ROLE_SLAVE ? 1 : 0;

	/* Retrieve previously allocated HW Resources. Shouldn't fail */
	sde_rm_init_hw_iter(&iter, phys_enc->parent->base.id, SDE_HW_BLK_CTL);
	for (i = 0; i <= instance; i++) {
		if (sde_rm_get_hw(rm, &iter)) {
			if (phys_enc->hw_ctl && phys_enc->hw_ctl != to_sde_hw_ctl(iter.hw)) {
				*reinit_mixers =  true;
				SDE_EVT32(phys_enc->hw_ctl->idx,
						to_sde_hw_ctl(iter.hw)->idx);
			}
			phys_enc->hw_ctl = to_sde_hw_ctl(iter.hw);
		}
	}
	if (IS_ERR_OR_NULL(phys_enc->hw_ctl)) {
		SDE_ERROR_HYPENC(hyp_enc, "failed to init ctl, %ld\n",
				PTR_ERR(phys_enc->hw_ctl));
		phys_enc->hw_ctl = NULL;
		return;
	}

	sde_rm_init_hw_iter(&iter, phys_enc->parent->base.id, SDE_HW_BLK_INTF);
	for (i = 0; i <= instance; i++) {
		if (sde_rm_get_hw(rm, &iter))
			phys_enc->hw_intf = to_sde_hw_intf(iter.hw);
	}

	if (IS_ERR_OR_NULL(phys_enc->hw_intf)) {
		SDE_ERROR_HYPENC(hyp_enc, "failed to init intf: %ld\n",
				PTR_ERR(phys_enc->hw_intf));
		phys_enc->hw_intf = NULL;
		return;
	}

	_sde_encoder_phys_hyp_setup_irq_hw_idx(phys_enc);

	phys_enc->kickoff_timeout_ms =
		sde_encoder_helper_get_kickoff_timeout_ms(phys_enc->parent);
}

static int sde_encoder_phys_hyp_control_vblank_irq(
		struct sde_encoder_phys *phys_enc,
		bool enable)
{
	int ret = 0;
	struct sde_encoder_phys_hyp *hyp_enc;
	struct sde_encoder_virt *sde_enc;
	int refcount;

	if (!phys_enc) {
		SDE_ERROR("invalid encoder\n");
		return -EINVAL;
	}

	mutex_lock(phys_enc->vblank_ctl_lock);
	refcount = atomic_read(&phys_enc->vblank_refcount);
	hyp_enc = to_sde_encoder_phys_hyp(phys_enc);
	sde_enc = to_sde_encoder_virt(phys_enc->parent);

	/* Slave encoders don't report vblank */
	if (!sde_encoder_phys_hyp_is_master(phys_enc))
		goto end;

	/* protect against negative */
	if (!enable && refcount == 0) {
		ret = -EINVAL;
		goto end;
	}

	SDE_DEBUG_HYPENC(hyp_enc, "[%pS] enable=%d/%d\n",
			__builtin_return_address(0),
			enable, atomic_read(&phys_enc->vblank_refcount));

	SDE_EVT32(DRMID(phys_enc->parent), enable,
			atomic_read(&phys_enc->vblank_refcount));

	if (enable && atomic_inc_return(&phys_enc->vblank_refcount) == 1) {
		ret = sde_encoder_helper_register_irq(phys_enc, INTR_IDX_VSYNC);
		if (ret)
			atomic_dec_return(&phys_enc->vblank_refcount);
	} else if (!enable &&
			atomic_dec_return(&phys_enc->vblank_refcount) == 0) {
		ret = sde_encoder_helper_unregister_irq(phys_enc,
				INTR_IDX_VSYNC);
		if (ret)
			atomic_inc_return(&phys_enc->vblank_refcount);
	}

end:
	if (ret) {
		SDE_ERROR_HYPENC(hyp_enc,
				"control vblank irq error %d, enable %d\n",
				ret, enable);
		SDE_EVT32(DRMID(phys_enc->parent),
				phys_enc->hw_intf->idx - INTF_0,
				enable, refcount, SDE_EVTLOG_ERROR);
	}
	mutex_unlock(phys_enc->vblank_ctl_lock);
	return ret;
}

static void sde_encoder_phys_hyp_enable(struct sde_encoder_phys *phys_enc)
{
	struct msm_drm_private *priv;
	struct sde_encoder_phys_hyp *hyp_enc;
	struct sde_hw_intf *intf;
	struct sde_hw_ctl *ctl;

	if (!phys_enc || !phys_enc->parent || !phys_enc->parent->dev ||
			!phys_enc->parent->dev->dev_private ||
			!phys_enc->sde_kms) {
		SDE_ERROR("invalid encoder/device\n");
		return;
	}
	priv = phys_enc->parent->dev->dev_private;

	hyp_enc = to_sde_encoder_phys_hyp(phys_enc);
	intf = phys_enc->hw_intf;
	ctl = phys_enc->hw_ctl;
	if (!phys_enc->hw_intf || !phys_enc->hw_ctl || !phys_enc->hw_pp) {
		SDE_ERROR("invalid hw_intf %d hw_ctl %d hw_pp %d\n",
				!phys_enc->hw_intf, !phys_enc->hw_ctl,
				!phys_enc->hw_pp);
		return;
	}

	SDE_DEBUG_HYPENC(hyp_enc, "\n");

	//FIXME: send message to host VM to turn on the display

	SDE_DEBUG_HYPENC(hyp_enc, "update pending flush ctl %d intf %d role %d\n",
		ctl->idx - CTL_0, intf->idx, phys_enc->split_role);
	SDE_EVT32(DRMID(phys_enc->parent), phys_enc->split_role,
		atomic_read(&phys_enc->pending_retire_fence_cnt), phys_enc->enable_state);

	/* ctl_flush & timing engine enable will be triggered by framework */
	if (phys_enc->enable_state == SDE_ENC_DISABLED)
		phys_enc->enable_state = SDE_ENC_ENABLING;
}

static void sde_encoder_phys_hyp_destroy(struct sde_encoder_phys *phys_enc)
{
	struct sde_encoder_phys_hyp *hyp_enc;

	if (!phys_enc) {
		SDE_ERROR("invalid encoder\n");
		return;
	}

	hyp_enc = to_sde_encoder_phys_hyp(phys_enc);
	SDE_DEBUG_HYPENC(hyp_enc, "\n");
	kfree(hyp_enc);
}

static void sde_encoder_phys_hyp_get_hw_resources(
		struct sde_encoder_phys *phys_enc,
		struct sde_encoder_hw_resources *hw_res,
		struct drm_connector_state *conn_state)
{
	struct sde_encoder_phys_hyp *hyp_enc;

	if (!phys_enc || !hw_res) {
		SDE_ERROR("invalid arg(s), enc %d hw_res %d conn_state %d\n",
				!phys_enc, !hw_res, !conn_state);
		return;
	}

	if ((phys_enc->intf_idx - INTF_0) >= INTF_MAX) {
		SDE_ERROR("invalid intf idx:%d\n", phys_enc->intf_idx);
		return;
	}

	hyp_enc = to_sde_encoder_phys_hyp(phys_enc);
	SDE_DEBUG_HYPENC(hyp_enc, "\n");
	hw_res->intfs[phys_enc->intf_idx - INTF_0] = INTF_MODE_VIDEO;
}

static int _sde_encoder_handle_flush_sync_timeout(
		struct sde_encoder_phys *phys_enc)
{
	struct sde_encoder_wait_info wait_info = {0};
	struct sde_hw_ctl *hw_ctl;
	int ret;
	u32 flush_register;

	if (!phys_enc || !phys_enc->hw_ctl)
		return -EINVAL;
	/*
	 * When flush sync is enabled, flush register will be cleared only once
	 * flush is successful on both the cores. In those cases, where flush is
	 * not cleared and HW is in sync mode, add an additional wait to check
	 * for flush in the second core. If there is still no flush in the
	 * other core, force async mode for this core and wait for vsync.
	 */
	flush_register = sde_encoder_helper_get_ctl_flush(phys_enc);

	if (!flush_register)
		return 0;

	wait_info.wq = &phys_enc->pending_kickoff_wq;
	wait_info.atomic_cnt = &phys_enc->pending_kickoff_cnt;
	wait_info.timeout_ms = phys_enc->kickoff_timeout_ms;
	hw_ctl = phys_enc->hw_ctl;

	SDE_EVT32(flush_register, SDE_EVTLOG_FUNC_CASE1);
	ret = sde_encoder_helper_wait_for_irq(phys_enc, INTR_IDX_VSYNC, &wait_info);
	flush_register = sde_encoder_helper_get_ctl_flush(phys_enc);
	if (!flush_register)
		return 0;

	SDE_EVT32(ret, flush_register, SDE_EVTLOG_FUNC_CASE2);
	if (hw_ctl->ops.enable_sync_mode) {
		hw_ctl->ops.enable_sync_mode(hw_ctl, true);
		ret = sde_encoder_helper_wait_for_irq(phys_enc, INTR_IDX_VSYNC,
			&wait_info);
	}

	return ret;
}

static int _sde_encoder_phys_hyp_wait_for_vblank(
		struct sde_encoder_phys *phys_enc, bool notify)
{
	struct sde_encoder_wait_info wait_info = {0};
	int ret = 0, new_cnt;
	u32 event = SDE_ENCODER_FRAME_EVENT_ERROR |
		SDE_ENCODER_FRAME_EVENT_SIGNAL_RELEASE_FENCE |
		SDE_ENCODER_FRAME_EVENT_SIGNAL_RETIRE_FENCE;
	struct drm_connector *conn;
	struct sde_hw_ctl *hw_ctl;
	u32 flush_register = 0xebad;
	bool timeout = false;

	if (!phys_enc || !phys_enc->hw_ctl) {
		pr_err("invalid encoder\n");
		return -EINVAL;
	}

	hw_ctl = phys_enc->hw_ctl;
	conn = phys_enc->connector;

	wait_info.wq = &phys_enc->pending_kickoff_wq;
	wait_info.atomic_cnt = &phys_enc->pending_kickoff_cnt;
	wait_info.timeout_ms = phys_enc->kickoff_timeout_ms;

	/* Wait for kickoff to complete */
	ret = sde_encoder_helper_wait_for_irq(phys_enc, INTR_IDX_VSYNC,
			&wait_info);

	/*
	 * if hwfencing enabled, try again to wait for up to the extended timeout time in
	 * increments as long as fence has not been signaled.
	 */
	if (ret == -ETIMEDOUT && phys_enc->sde_kms->catalog->hw_fence_rev)
		ret = sde_encoder_helper_hw_fence_extended_wait(phys_enc, phys_enc->hw_ctl,
			&wait_info, INTR_IDX_VSYNC);

	if (ret == -ETIMEDOUT && sde_encoder_has_dpu_ctl_op_sync(phys_enc->parent) &&
		sde_encoder_helper_flush_in_sync_mode(phys_enc)) {
		ret = _sde_encoder_handle_flush_sync_timeout(phys_enc);
	}

	if (ret == -ETIMEDOUT) {
		new_cnt = atomic_add_unless(&phys_enc->pending_kickoff_cnt, -1, 0);
		timeout = true;

		/*
		 * Reset ret when flush register is consumed. This handles a race condition between
		 * irq wait timeout handler reading the register status and the actual IRQ handler
		 */
		flush_register = sde_encoder_helper_get_ctl_flush(phys_enc);

		//FIXME: how to determine the local flush is done
		if (!flush_register)
			ret = 0;

		/* if we timeout after the extended wait, reset mixers and do sw override */
		if (ret && phys_enc->sde_kms->catalog->hw_fence_rev)
			sde_encoder_helper_hw_fence_sw_override(phys_enc, hw_ctl);

		SDE_EVT32(DRMID(phys_enc->parent), new_cnt, flush_register, ret,
				SDE_EVTLOG_FUNC_CASE1);
	}

	if (notify && timeout && atomic_add_unless(&phys_enc->pending_retire_fence_cnt, -1, 0)
			&& phys_enc->parent_ops.handle_frame_done) {
		phys_enc->parent_ops.handle_frame_done(phys_enc->parent, phys_enc, event);

		/* notify only on actual timeout cases */
		if ((ret == -ETIMEDOUT) && sde_encoder_recovery_events_enabled(phys_enc->parent))
			sde_connector_event_notify(conn, DRM_EVENT_SDE_HW_RECOVERY,
				sizeof(uint8_t), SDE_RECOVERY_HARD_RESET);
	}

	SDE_EVT32(DRMID(phys_enc->parent), event, notify, timeout, ret,
			ret ? SDE_EVTLOG_FATAL : 0, SDE_EVTLOG_FUNC_EXIT);

	if (!ret)
		sde_encoder_clear_fence_error_in_progress(phys_enc);

	return ret;
}

static int sde_encoder_phys_hyp_wait_for_vblank(
		struct sde_encoder_phys *phys_enc)
{
	return _sde_encoder_phys_hyp_wait_for_vblank(phys_enc, true);
}

static void sde_encoder_phys_hyp_update_txq(struct sde_encoder_phys *phys_enc)
{
	struct sde_encoder_virt *sde_enc;

	if (!phys_enc)
		return;

	sde_enc = to_sde_encoder_virt(phys_enc->parent);
	if (!sde_enc)
		return;

	sde_encoder_helper_update_out_fence_txq(sde_enc, true);
}

static int sde_encoder_phys_hyp_wait_for_commit_done(
		struct sde_encoder_phys *phys_enc)
{
	int rc = 0;

	/*
	 * With Interface sync, Master DPU will send signal to enable the Slave DPU Timing engine.
	 * Hence, Slave DPU should not wait for vsync during power on commit.
	 * For all other commits, wait_for_vsync is still needed.
	 */
	if (sde_encoder_has_dpu_ctl_op_sync(phys_enc->parent) &&
			phys_enc->enable_state == SDE_ENC_POST_ENABLING) {
		phys_enc->enable_state = SDE_ENC_ENABLED;
		return rc;
	}

	rc =  _sde_encoder_phys_hyp_wait_for_vblank(phys_enc, true);
	if (rc)
		sde_encoder_helper_phys_reset(phys_enc);

	/* Update TxQ for the incoming frame */
	sde_encoder_phys_hyp_update_txq(phys_enc);

	return rc;
}

static int sde_encoder_phys_hyp_wait_for_vblank_no_notify(
		struct sde_encoder_phys *phys_enc)
{
	return _sde_encoder_phys_hyp_wait_for_vblank(phys_enc, false);
}

static int sde_encoder_phys_hyp_prepare_for_kickoff(
		struct sde_encoder_phys *phys_enc,
		struct sde_encoder_kickoff_params *params)
{
	struct sde_encoder_virt *sde_enc;
	struct sde_encoder_phys_hyp *hyp_enc;
	struct sde_hw_ctl *ctl;
	bool recovery_events;
	struct drm_connector *conn;
	int rc = 0;

	if (!phys_enc || !params || !phys_enc->hw_ctl) {
		SDE_ERROR("invalid encoder/parameters\n");
		return -EINVAL;
	}
	hyp_enc = to_sde_encoder_phys_hyp(phys_enc);
	sde_enc = to_sde_encoder_virt(phys_enc->parent);

	ctl = phys_enc->hw_ctl;
	conn = phys_enc->connector;

	recovery_events = sde_encoder_recovery_events_enabled(
			phys_enc->parent);

	if (recovery_events && hyp_enc->error_count)
		sde_connector_event_notify(conn,
				DRM_EVENT_SDE_HW_RECOVERY,
				sizeof(uint8_t),
				SDE_RECOVERY_SUCCESS);
	hyp_enc->error_count = 0;

	return rc;
}

static void sde_encoder_phys_hyp_disable(struct sde_encoder_phys *phys_enc)
{
	struct msm_drm_private *priv;
	struct sde_encoder_phys_hyp *hyp_enc;
	struct sde_encoder_virt *sde_enc;
	struct msm_display_info *info;

	if (!phys_enc || !phys_enc->parent || !phys_enc->parent->dev ||
			!phys_enc->parent->dev->dev_private) {
		SDE_ERROR("invalid encoder/device\n");
		return;
	}
	priv = phys_enc->parent->dev->dev_private;
	sde_enc = to_sde_encoder_virt(phys_enc->parent);
	info = &sde_enc->disp_info;

	hyp_enc = to_sde_encoder_phys_hyp(phys_enc);
	if (!phys_enc->hw_intf || !phys_enc->hw_ctl) {
		SDE_ERROR("invalid hw_intf %d hw_ctl %d\n",
				!phys_enc->hw_intf, !phys_enc->hw_ctl);
		return;
	}

	SDE_DEBUG_HYPENC(hyp_enc, "\n");

	if (!sde_encoder_phys_hyp_is_master(phys_enc))
		goto exit;

	if (phys_enc->enable_state == SDE_ENC_DISABLED) {
		SDE_ERROR("already disabled\n");
		return;
	}

	if (sde_in_trusted_vm(phys_enc->sde_kms))
		goto exit;

	//FIXME: send message to host VM to disable display

	sde_encoder_helper_phys_disable(phys_enc, NULL);
exit:
	SDE_EVT32(DRMID(phys_enc->parent),
		atomic_read(&phys_enc->pending_retire_fence_cnt), phys_enc->split_role);
	phys_enc->vfp_cached = 0;
	phys_enc->enable_state = SDE_ENC_DISABLED;
}

static void sde_encoder_phys_hyp_handle_post_kickoff(
		struct sde_encoder_phys *phys_enc)
{
	struct sde_encoder_phys_hyp *hyp_enc;

	if (!phys_enc) {
		SDE_ERROR("invalid encoder\n");
		return;
	}

	hyp_enc = to_sde_encoder_phys_hyp(phys_enc);
	SDE_DEBUG_HYPENC(hyp_enc, "enable_state %d\n", phys_enc->enable_state);

	if (phys_enc->enable_state == SDE_ENC_ENABLING)
		SDE_EVT32(DRMID(phys_enc->parent), phys_enc->hw_intf->idx - INTF_0,
			phys_enc->split_role);

	/*
	 * Video mode must flush CTL before enabling timing engine
	 * Video encoders need to turn on their interfaces now
	 */
	if (phys_enc->enable_state == SDE_ENC_ENABLING) {
		if (sde_encoder_phys_hyp_is_master(phys_enc) &&
			!sde_encoder_phys_has_role_slave_dpu_master_intf(phys_enc)) {
			phys_enc->enable_state = SDE_ENC_ENABLED;
		/* Slave DPU Timing engine mux select from Master DPU */
		}
	}
}

static void sde_encoder_phys_hyp_irq_control(struct sde_encoder_phys *phys_enc,
		bool enable)
{
	struct sde_encoder_phys_hyp *hyp_enc;
	int ret;

	if (!phys_enc)
		return;

	hyp_enc = to_sde_encoder_phys_hyp(phys_enc);

	SDE_EVT32(DRMID(phys_enc->parent), phys_enc->hw_intf->idx - INTF_0,
			enable, atomic_read(&phys_enc->vblank_refcount));

	if (enable) {
		ret = sde_encoder_phys_hyp_control_vblank_irq(phys_enc, true);
		if (ret)
			return;

		sde_encoder_helper_register_irq(phys_enc, INTR_IDX_UNDERRUN);
	} else {
		sde_encoder_phys_hyp_control_vblank_irq(phys_enc, false);
		sde_encoder_helper_unregister_irq(phys_enc, INTR_IDX_UNDERRUN);
	}
}

static int sde_encoder_phys_hyp_get_line_count(
		struct sde_encoder_phys *phys_enc)
{
	if (!phys_enc)
		return -EINVAL;

	if (!sde_encoder_phys_hyp_is_master(phys_enc))
		return -EINVAL;

	if (!phys_enc->hw_intf || !phys_enc->hw_intf->ops.get_line_count)
		return -EINVAL;

	return phys_enc->hw_intf->ops.get_line_count(phys_enc->hw_intf);
}

static u32 sde_encoder_phys_hyp_get_underrun_line_count(
		struct sde_encoder_phys *phys_enc)
{
	u32 underrun_linecount = 0xebadebad;
	u32 intf_intr_status = 0xebadebad;
	struct intf_status intf_status = {0};

	if (!phys_enc)
		return -EINVAL;

	if (!sde_encoder_phys_hyp_is_master(phys_enc) || !phys_enc->hw_intf)
		return -EINVAL;

	if (phys_enc->hw_intf->ops.get_status)
		phys_enc->hw_intf->ops.get_status(phys_enc->hw_intf,
			&intf_status);

	if (phys_enc->hw_intf->ops.get_underrun_line_count)
		underrun_linecount =
		  phys_enc->hw_intf->ops.get_underrun_line_count(
			phys_enc->hw_intf);

	if (phys_enc->hw_intf->ops.get_intr_status)
		intf_intr_status = phys_enc->hw_intf->ops.get_intr_status(
				phys_enc->hw_intf);

	SDE_EVT32(DRMID(phys_enc->parent), underrun_linecount,
		intf_status.frame_count, intf_status.line_count,
		intf_intr_status, DPUID(phys_enc->sde_kms));

	return underrun_linecount;
}

static int sde_encoder_phys_hyp_wait_for_active(
			struct sde_encoder_phys *phys_enc)
{
	struct drm_display_mode mode;
	struct sde_encoder_phys_hyp *hyp_enc;
	u32 ln_cnt, min_ln_cnt, active_lns_cnt;
	u32 retry = MAX_POLL_CNT;

	hyp_enc =  to_sde_encoder_phys_hyp(phys_enc);

	if (!phys_enc->hw_intf || !phys_enc->hw_intf->ops.get_line_count) {
		SDE_ERROR_HYPENC(hyp_enc, "invalid hyp_enc params\n");
		return -EINVAL;
	}

	mode = phys_enc->cached_mode;

	min_ln_cnt = (mode.vtotal - mode.vsync_start) +
		(mode.vsync_end - mode.vsync_start);
	active_lns_cnt = mode.vdisplay;

	while (retry) {
		ln_cnt = phys_enc->hw_intf->ops.get_line_count(
				phys_enc->hw_intf);

		if ((ln_cnt >= min_ln_cnt) &&
			(ln_cnt < (active_lns_cnt + min_ln_cnt))) {
			SDE_DEBUG_HYPENC(hyp_enc,
					"Needed lines left line_cnt=%d\n",
					ln_cnt);
			return 0;
		}

		SDE_ERROR_HYPENC(hyp_enc, "line count is less. line_cnt = %d\n", ln_cnt);
		udelay(POLL_TIME_USEC_FOR_LN_CNT);
		retry--;
	}

	return -EINVAL;
}

void sde_encoder_phys_hyp_add_enc_to_minidump(struct sde_encoder_phys *phys_enc)
{
	struct sde_encoder_phys_hyp *hyp_enc;
	hyp_enc =  to_sde_encoder_phys_hyp(phys_enc);

	sde_mini_dump_add_va_region("sde_enc_phys_hyp", sizeof(*hyp_enc), hyp_enc);
}

static void sde_encoder_phys_hyp_init_ops(struct sde_encoder_phys_ops *ops)
{
	ops->is_master = sde_encoder_phys_hyp_is_master;
	ops->mode_set = sde_encoder_phys_hyp_mode_set;
	ops->mode_fixup = sde_encoder_phys_hyp_mode_fixup;
	ops->enable = sde_encoder_phys_hyp_enable;
	ops->disable = sde_encoder_phys_hyp_disable;
	ops->get_hw_resources = sde_encoder_phys_hyp_get_hw_resources;
	ops->destroy = sde_encoder_phys_hyp_destroy;
	ops->wait_for_commit_done = sde_encoder_phys_hyp_wait_for_commit_done;
	ops->wait_for_vblank = sde_encoder_phys_hyp_wait_for_vblank_no_notify;
	ops->wait_for_tx_complete = sde_encoder_phys_hyp_wait_for_vblank;
	ops->irq_control = sde_encoder_phys_hyp_irq_control;
	ops->prepare_for_kickoff = sde_encoder_phys_hyp_prepare_for_kickoff;
	ops->handle_post_kickoff = sde_encoder_phys_hyp_handle_post_kickoff;
	ops->trigger_flush = sde_encoder_helper_trigger_flush;
	ops->get_line_count = sde_encoder_phys_hyp_get_line_count;
	ops->wait_for_active = sde_encoder_phys_hyp_wait_for_active;
	ops->get_underrun_line_count =
		sde_encoder_phys_hyp_get_underrun_line_count;
	ops->add_to_minidump = sde_encoder_phys_hyp_add_enc_to_minidump;
}

struct sde_encoder_phys *sde_encoder_phys_hyp_init(
		struct sde_enc_phys_init_params *p)
{
	struct sde_encoder_phys *phys_enc = NULL;
	struct sde_encoder_phys_hyp *hyp_enc = NULL;
	struct sde_hw_mdp *hw_mdp;
	struct sde_encoder_irq *irq;
	int i, ret = 0;

	if (!p) {
		ret = -EINVAL;
		goto fail;
	}

	hyp_enc = kzalloc(sizeof(*hyp_enc), GFP_KERNEL);
	if (!hyp_enc) {
		ret = -ENOMEM;
		goto fail;
	}

	phys_enc = &hyp_enc->base;

	hw_mdp = sde_rm_get_mdp(&p->sde_kms->rm);
	if (IS_ERR_OR_NULL(hw_mdp)) {
		ret = PTR_ERR(hw_mdp);
		SDE_ERROR("failed to get mdptop\n");
		goto fail;
	}
	phys_enc->hw_mdptop = hw_mdp;
	phys_enc->intf_idx = p->intf_idx;

	SDE_DEBUG_HYPENC(hyp_enc, "\n");

	sde_encoder_phys_hyp_init_ops(&phys_enc->ops);

	phys_enc->parent = p->parent;
	phys_enc->parent_ops = p->parent_ops;
	phys_enc->sde_kms = p->sde_kms;
	phys_enc->split_role = p->split_role;
	phys_enc->intf_mode = INTF_MODE_VIDEO;
	phys_enc->enc_spinlock = p->enc_spinlock;
	phys_enc->vblank_ctl_lock = p->vblank_ctl_lock;
	phys_enc->comp_type = p->comp_type;
	phys_enc->kickoff_timeout_ms = DEFAULT_KICKOFF_TIMEOUT_MS;
	for (i = 0; i < INTR_IDX_MAX; i++) {
		irq = &phys_enc->irq[i];
		INIT_LIST_HEAD(&irq->cb.list);
		irq->irq_idx = -EINVAL;
		irq->hw_idx = -EINVAL;
		irq->cb.arg = phys_enc;
	}

	irq = &phys_enc->irq[INTR_IDX_VSYNC];
	irq->name = "vsync_irq";
	irq->intr_type = SDE_IRQ_TYPE_INTF_VSYNC;
	irq->intr_idx = INTR_IDX_VSYNC;
	irq->cb.func = sde_encoder_phys_hyp_vblank_irq;

	irq = &phys_enc->irq[INTR_IDX_UNDERRUN];
	irq->name = "underrun";
	irq->intr_type = SDE_IRQ_TYPE_INTF_UNDER_RUN;
	irq->intr_idx = INTR_IDX_UNDERRUN;
	irq->cb.func = sde_encoder_phys_hyp_underrun_irq;

	atomic_set(&phys_enc->vblank_refcount, 0);
	atomic_set(&phys_enc->pending_kickoff_cnt, 0);
	atomic_set(&phys_enc->pending_retire_fence_cnt, 0);
	init_waitqueue_head(&phys_enc->pending_kickoff_wq);

	phys_enc->enable_state = SDE_ENC_DISABLED;
	phys_enc->sde_vrr_cfg.backlight_timer.function =
		sde_encoder_phys_backlight_timer_cb;

	SDE_DEBUG_HYPENC(hyp_enc, "created virtual intf idx:%d\n", p->intf_idx);

	return phys_enc;

fail:
	SDE_ERROR("failed to create encoder\n");
	if (hyp_enc)
		sde_encoder_phys_hyp_destroy(phys_enc);

	return ERR_PTR(ret);
}
