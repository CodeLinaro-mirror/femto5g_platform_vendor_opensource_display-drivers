// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>
#include <linux/of_platform.h>
#include "msm_ext_display.h"
#include "virtio/virtio_kms.h"
#include "sde_edid_parser.h"
#include "msm_hyp_dp_audio.h"

#define DP_DEBUG(fmt, ...)  pr_debug("dp_audio: "fmt, ##__VA_ARGS__)
#define DP_INFO(fmt, ...)   pr_info("dp_audio: "fmt, ##__VA_ARGS__)
#define DP_WARN(fmt, ...)   pr_warn("dp_audio: "fmt, ##__VA_ARGS__)
#define DP_ERR(fmt, ...)    pr_err("dp_audio: "fmt, ##__VA_ARGS__)

struct msm_hyp_dp_audio_private {
	struct platform_device *ext_pdev;
	struct platform_device *pdev;
	struct msm_ext_disp_init_data ext_audio_data;
	struct virtio_kms_output *panel;

	bool ack_enabled;
	atomic_t session_on;
	bool engine_on;

	struct completion hpd_comp;
	struct workqueue_struct *notify_workqueue;
	struct delayed_work notify_delayed_work;
	struct mutex ops_lock;

	struct msm_hyp_dp_audio dp_audio;

	atomic_t acked;
};

static void msm_hyp_dp_audio_enable(struct msm_hyp_dp_audio_private *audio, bool enable)
{
	audio->engine_on = enable;
	if (!atomic_read(&audio->session_on))
		DP_WARN("session inactive. enable=%d\n", enable);
}

static struct msm_hyp_dp_audio_private *msm_hyp_dp_audio_get_data(struct platform_device *pdev)
{
	struct msm_ext_disp_data *ext_data;
	struct msm_hyp_dp_audio *dp_audio;

	if (!pdev) {
		DP_ERR("invalid input\n");
		return ERR_PTR(-ENODEV);
	}

	ext_data = platform_get_drvdata(pdev);
	if (!ext_data) {
		DP_ERR("invalid ext disp data\n");
		return ERR_PTR(-EINVAL);
	}

	dp_audio = ext_data->intf_data;
	if (!dp_audio) {
		DP_ERR("invalid intf data\n");
		return ERR_PTR(-EINVAL);
	}

	return container_of(dp_audio, struct msm_hyp_dp_audio_private, dp_audio);
}

static int msm_hyp_dp_audio_info_setup(struct platform_device *pdev,
	struct msm_ext_disp_audio_setup_params *params)
{
	int rc = 0;
	struct msm_hyp_dp_audio_private *audio;

	DP_WARN("%s does not do any audio setup\n", __func__);

	audio = msm_hyp_dp_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		return rc;
	}

	mutex_lock(&audio->ops_lock);

	if (audio->panel->hw_assign.dp_stream_id >= DP_STREAM_MAX) {
		DP_ERR("invalid stream id: %d\n",
				audio->panel->hw_assign.dp_stream_id);
		rc = -EINVAL;
		mutex_unlock(&audio->ops_lock);
		return rc;
	}

	msm_hyp_dp_audio_enable(audio, true);

	mutex_unlock(&audio->ops_lock);

	DP_DEBUG("audio stream configured\n");

	return rc;
}

static int msm_hyp_dp_audio_get_edid_blk(struct platform_device *pdev,
		struct msm_ext_disp_audio_edid_blk *blk)
{
	int rc = 0;
	struct msm_hyp_dp_audio_private *audio;
	struct sde_edid_ctrl *edid;

	if (!blk) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	audio = msm_hyp_dp_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		goto end;
	}

	if (!audio->panel) {
		DP_ERR("invalid panel data\n");
		rc = -EINVAL;
		goto end;
	}

	/* Acquire lock to safely access edid_ctrl */
	mutex_lock(&audio->panel->edid_lock);

	if (!audio->panel->edid_ctrl) {
		DP_ERR("invalid edid_ctrl\n");
		rc = -EINVAL;
		mutex_unlock(&audio->panel->edid_lock);
		goto end;
	}

	edid = audio->panel->edid_ctrl;

	blk->audio_data_blk = edid->audio_data_block;
	blk->audio_data_blk_size = edid->adb_size;

	blk->spk_alloc_data_blk = edid->spkr_alloc_data_block;
	blk->spk_alloc_data_blk_size = edid->sadb_size;

	mutex_unlock(&audio->panel->edid_lock);

end:
	return rc;
}

static int msm_hyp_dp_audio_get_cable_status(struct platform_device *pdev, u32 vote)
{
	int rc = 0;
	struct msm_hyp_dp_audio_private *audio;

	audio = msm_hyp_dp_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		goto end;
	}

	return atomic_read(&audio->session_on);
end:
	return rc;
}

static int msm_hyp_dp_audio_get_intf_id(struct platform_device *pdev)
{
	return EXT_DISPLAY_TYPE_DP;
}

static void msm_hyp_dp_audio_teardown_done(struct platform_device *pdev)
{
	struct msm_hyp_dp_audio_private *audio;

	audio = msm_hyp_dp_audio_get_data(pdev);
	if (IS_ERR(audio))
		return;

	if (audio->panel->hw_assign.dp_stream_id >= DP_STREAM_MAX) {
		DP_WARN("invalid stream id: %d\n",
				audio->panel->hw_assign.dp_stream_id);
		return;
	}

	mutex_lock(&audio->ops_lock);
	msm_hyp_dp_audio_enable(audio, false);
	mutex_unlock(&audio->ops_lock);

	atomic_set(&audio->acked, 1);
	complete_all(&audio->hpd_comp);

	DP_DEBUG("audio engine disabled\n");
}

static int msm_hyp_dp_audio_ack_done(struct platform_device *pdev, u32 ack)
{
	int rc = 0;
	int ack_hpd;
	struct msm_hyp_dp_audio_private *audio;

	audio = msm_hyp_dp_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		rc = PTR_ERR(audio);
		goto end;
	}

	if (ack & AUDIO_ACK_SET_ENABLE) {
		audio->ack_enabled = ack & AUDIO_ACK_ENABLE ?
			true : false;

		DP_DEBUG("audio ack feature %s\n",
			audio->ack_enabled ? "enabled" : "disabled");
		goto end;
	}

	if (!audio->ack_enabled)
		goto end;

	ack_hpd = ack & AUDIO_ACK_CONNECT;

	DP_DEBUG("acknowledging audio (%d)\n", ack_hpd);

	if (!audio->engine_on) {
		atomic_set(&audio->acked, 1);
		complete_all(&audio->hpd_comp);
	}
end:
	return rc;
}

static int msm_hyp_dp_audio_codec_ready(struct platform_device *pdev)
{
	int rc = 0;
	struct msm_hyp_dp_audio_private *audio;

	audio = msm_hyp_dp_audio_get_data(pdev);
	if (IS_ERR(audio)) {
		DP_ERR("invalid input\n");
		rc = PTR_ERR(audio);
		goto end;
	}

	queue_delayed_work(audio->notify_workqueue,
			&audio->notify_delayed_work, HZ/4);
end:
	return rc;
}

static int msm_hyp_dp_audio_register_ext_disp(struct msm_hyp_dp_audio_private *audio)
{
	int rc = 0;
	struct device_node *pd = NULL;
	const char *phandle = "qcom,ext-disp";
	struct msm_ext_disp_init_data *ext;
	struct msm_ext_disp_audio_codec_ops *ops;

	uint dpu_id = audio->panel->hw_assign.dpu_id;
	uint dp_ctrl_id = audio->panel->hw_assign.dp_ctrl_id;
	uint dp_stream_id = audio->panel->hw_assign.dp_stream_id;

	ext = &audio->ext_audio_data;
	ops = &ext->codec_ops;

	ext->codec.type = EXT_DISPLAY_TYPE_DP;
	ext->codec.dpu_id = dpu_id;
	ext->codec.ctrl_id = dp_ctrl_id;
	ext->codec.stream_id = dp_stream_id;
	ext->pdev = audio->pdev;
	ext->intf_data = &audio->dp_audio;

	ops->audio_info_setup   = msm_hyp_dp_audio_info_setup;
	ops->get_audio_edid_blk = msm_hyp_dp_audio_get_edid_blk;
	ops->cable_status       = msm_hyp_dp_audio_get_cable_status;
	ops->get_intf_id        = msm_hyp_dp_audio_get_intf_id;
	ops->teardown_done      = msm_hyp_dp_audio_teardown_done;
	ops->acknowledge        = msm_hyp_dp_audio_ack_done;
	ops->ready              = msm_hyp_dp_audio_codec_ready;

	uint ext_disp_index = dpu_id * MAX_NUM_DPU_CORE + dp_ctrl_id;

	DP_DEBUG("ext_disp_index %u, codec %u, ctrl_id %u, stream_id %u\n",
		ext_disp_index, ext->codec.type, ext->codec.ctrl_id, ext->codec.stream_id);

	if (!audio->pdev->dev.of_node) {
		DP_ERR("cannot find audio dev.of_node\n");
		rc = -ENODEV;
		goto end;
	}

	pd = of_parse_phandle(audio->pdev->dev.of_node, phandle, ext_disp_index);
	if (!pd) {
		DP_ERR("cannot parse %s\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	audio->ext_pdev = of_find_device_by_node(pd);
	if (!audio->ext_pdev) {
		DP_ERR("cannot find %s pdev\n", phandle);
		rc = -ENODEV;
		goto end;
	}
#if IS_ENABLED(CONFIG_MSM_EXT_DISPLAY)
	rc = msm_ext_disp_register_intf(audio->ext_pdev, ext);
	if (rc)
		DP_ERR("failed to register disp\n");
#endif
end:
	if (pd)
		of_node_put(pd);

	return rc;
}

static int msm_hyp_dp_audio_deregister_ext_disp(struct msm_hyp_dp_audio_private *audio)
{
	int rc = 0;
	struct device_node *pd = NULL;
	const char *phandle = "qcom,ext-disp";
	struct msm_ext_disp_init_data *ext;

	ext = &audio->ext_audio_data;

	if (!audio->pdev->dev.of_node) {
		DP_ERR("cannot find audio dev.of_node\n");
		rc = -ENODEV;
		goto end;
	}

	uint dpu_id = audio->panel->hw_assign.dpu_id;
	uint dp_ctrl_id = audio->panel->hw_assign.dp_ctrl_id;
	uint ext_disp_index = dpu_id * MAX_NUM_DPU_CORE + dp_ctrl_id;

	pd = of_parse_phandle(audio->pdev->dev.of_node, phandle, ext_disp_index);
	if (!pd) {
		DP_ERR("cannot parse %s handle\n", phandle);
		rc = -ENODEV;
		goto end;
	}

	audio->ext_pdev = of_find_device_by_node(pd);
	if (!audio->ext_pdev) {
		DP_ERR("cannot find %s pdev\n", phandle);
		rc = -ENODEV;
		goto end;
	}

#if IS_ENABLED(CONFIG_MSM_EXT_DISPLAY)
	rc = msm_ext_disp_deregister_intf(audio->ext_pdev, ext);
	if (rc)
		DP_ERR("failed to deregister disp\n");
#endif

end:
	return rc;
}

static int msm_hyp_dp_audio_notify(struct msm_hyp_dp_audio_private *audio, u32 state)
{
	int rc = 0;
	struct msm_ext_disp_init_data *ext = &audio->ext_audio_data;

	atomic_set(&audio->acked, 0);

	if (!ext->intf_ops.audio_notify) {
		DP_ERR("audio notify not defined\n");
		goto end;
	}

	DP_DEBUG("audio notify initiating with state %u\n", state);

	reinit_completion(&audio->hpd_comp);
	rc = ext->intf_ops.audio_notify(audio->ext_pdev,
			&ext->codec, state);
	if (rc)
		goto end;

	if (atomic_read(&audio->acked))
		goto end;

	if (state == EXT_DISPLAY_CABLE_DISCONNECT && !audio->engine_on)
		goto end;

	if (state == EXT_DISPLAY_CABLE_CONNECT)
		goto end;

	rc = wait_for_completion_timeout(&audio->hpd_comp, HZ * 4);
	if (!rc) {
		DP_ERR("timeout. state=%d err=%d\n", state, rc);
		rc = -ETIMEDOUT;
		goto end;
	}

	DP_DEBUG("success\n");
end:
	return rc;
}

static void msm_hyp_dp_audio_notify_work_fn(struct work_struct *work)
{
	struct msm_hyp_dp_audio_private *audio;
	struct delayed_work *dw = to_delayed_work(work);

	audio = container_of(dw, struct msm_hyp_dp_audio_private, notify_delayed_work);

	msm_hyp_dp_audio_notify(audio, EXT_DISPLAY_CABLE_CONNECT);
}

static int msm_hyp_dp_audio_create_notify_workqueue(struct msm_hyp_dp_audio_private *audio)
{
	audio->notify_workqueue = create_workqueue("sdm_dp_audio_notify");
	if (IS_ERR_OR_NULL(audio->notify_workqueue)) {
		DP_ERR("Error creating notify_workqueue\n");
		return -EPERM;
	}

	INIT_DELAYED_WORK(&audio->notify_delayed_work, msm_hyp_dp_audio_notify_work_fn);

	return 0;
}

static void msm_hyp_dp_audio_destroy_notify_workqueue(struct msm_hyp_dp_audio_private *audio)
{
	if (audio->notify_workqueue)
		destroy_workqueue(audio->notify_workqueue);
}

static int msm_hyp_dp_audio_config(struct msm_hyp_dp_audio_private *audio, u32 state)
{
	int rc = 0;
	struct msm_ext_disp_init_data *ext = &audio->ext_audio_data;

	if (!ext || !ext->intf_ops.audio_config) {
		DP_ERR("audio_config not defined\n");
		goto end;
	}

	DP_DEBUG("audio config initiating with state %u\n", state);

	rc = ext->intf_ops.audio_config(audio->ext_pdev,
			&ext->codec, state);
	if (rc)
		DP_ERR("failed to config audio, err=%d\n", rc);

end:
	return rc;
}

static int msm_hyp_dp_audio_on(struct msm_hyp_dp_audio *dp_audio)
{
	int rc = 0;
	struct msm_hyp_dp_audio_private *audio;
	struct msm_ext_disp_init_data *ext;

	if (!dp_audio) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	audio = container_of(dp_audio, struct msm_hyp_dp_audio_private, dp_audio);

	msm_hyp_dp_audio_register_ext_disp(audio);

	ext = &audio->ext_audio_data;

	atomic_set(&audio->session_on, 1);

	rc = msm_hyp_dp_audio_config(audio, EXT_DISPLAY_CABLE_CONNECT);
	if (rc)
		goto end;

	rc = msm_hyp_dp_audio_notify(audio, EXT_DISPLAY_CABLE_CONNECT);
	if (rc)
		goto end;

	DP_DEBUG("%s success\n", __func__);

end:
	return rc;
}

static int msm_hyp_dp_audio_off(struct msm_hyp_dp_audio *dp_audio, bool skip_wait)
{
	int rc = 0;
	struct msm_hyp_dp_audio_private *audio;
	struct msm_ext_disp_init_data *ext;
	bool work_pending = false;

	if (!dp_audio) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	audio = container_of(dp_audio, struct msm_hyp_dp_audio_private, dp_audio);

	if (!atomic_read(&audio->session_on)) {
		DP_DEBUG("audio already off\n");
		return rc;
	}

	ext = &audio->ext_audio_data;

	work_pending = cancel_delayed_work_sync(&audio->notify_delayed_work);
	if (work_pending)
		DP_DEBUG("pending notification work completed\n");

	if (!skip_wait) {
		rc = msm_hyp_dp_audio_notify(audio, EXT_DISPLAY_CABLE_DISCONNECT);
		if (rc)
			goto end;
	}

	DP_DEBUG("%s success\n", __func__);
end:
	msm_hyp_dp_audio_config(audio, EXT_DISPLAY_CABLE_DISCONNECT);

	atomic_set(&audio->session_on, 0);
	audio->engine_on  = false;

	msm_hyp_dp_audio_deregister_ext_disp(audio);

	return rc;
}

struct msm_hyp_dp_audio *msm_hyp_dp_audio_get(struct platform_device *pdev,
			struct virtio_kms_output *panel)
{
	int rc = 0;
	struct msm_hyp_dp_audio_private *audio;
	struct msm_hyp_dp_audio *dp_audio;

	if (!pdev || !panel) {
		DP_ERR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	audio = devm_kzalloc(&pdev->dev, sizeof(*audio), GFP_KERNEL);
	if (!audio) {
		rc = -ENOMEM;
		goto error;
	}

	rc = msm_hyp_dp_audio_create_notify_workqueue(audio);
	if (rc)
		goto error_notify_workqueue;

	init_completion(&audio->hpd_comp);

	audio->pdev = pdev;
	audio->panel = panel;

	atomic_set(&audio->acked, 0);

	dp_audio = &audio->dp_audio;

	mutex_init(&audio->ops_lock);

	dp_audio->on  = msm_hyp_dp_audio_on;
	dp_audio->off = msm_hyp_dp_audio_off;

	return dp_audio;

error_notify_workqueue:
	devm_kfree(&pdev->dev, audio);
error:
	return ERR_PTR(rc);
}

void msm_hyp_dp_audio_put(struct msm_hyp_dp_audio *dp_audio)
{
	struct msm_hyp_dp_audio_private *audio;

	if (!dp_audio)
		return;

	audio = container_of(dp_audio, struct msm_hyp_dp_audio_private, dp_audio);

	mutex_destroy(&audio->ops_lock);

	msm_hyp_dp_audio_destroy_notify_workqueue(audio);

	devm_kfree(&audio->pdev->dev, audio);
}
