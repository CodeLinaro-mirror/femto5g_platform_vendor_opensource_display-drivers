// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/of_platform.h>
#include <linux/extcon.h>
#include <linux/version.h>

#if (KERNEL_VERSION(6, 3, 0) <= LINUX_VERSION_CODE)
#include <msm_ext_display.h>
#else
#include <linux/soc/qcom/msm_ext_display.h>
#endif

#include "dp_hfi_audio.h"
#include "dp_debug.h"
#include "hfi_defs_display.h"
#include "hfi_commands_display.h"

#define AUDIO_ACK_CONNECT_TIMEOUT_MS	500

struct dp_hfi_audio_private {
	struct platform_device *pdev;
	struct platform_device *ext_pdev;
	struct msm_ext_disp_init_data ext_audio_data;

	/* HFI specific */
	struct dp_client *client;

	/* Audio state */
	bool ack_enabled;
	atomic_t session_on;
	bool engine_on;
	u32 channels;
	atomic_t acked;
	struct completion hpd_comp;
	struct workqueue_struct *notify_workqueue;
	struct delayed_work notify_delayed_work;
	struct mutex ops_lock;

	/* External display interface */
	struct dp_audio dp_audio;
};

static struct dp_hfi_audio_private *dp_hfi_audio_get_data(struct platform_device *pdev)
{
	struct msm_ext_disp_data *ext_data;
	struct dp_audio *dp_audio;

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

	return container_of(dp_audio, struct dp_hfi_audio_private, dp_audio);
}

static int dp_hfi_audio_ack_done(struct platform_device *pdev, u32 ack)
{
	int rc = 0, ack_hpd;
	struct dp_hfi_audio_private *audio;

	audio = dp_hfi_audio_get_data(pdev);
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

static int dp_hfi_audio_config(struct dp_hfi_audio_private *audio, u32 state)
{
	int rc = 0;
	struct msm_ext_disp_init_data *ext = &audio->ext_audio_data;

	if (!ext || !ext->intf_ops.audio_config) {
		DP_ERR("audio_config not defined\n");
		goto end;
	}

	/*
	 * DP Audio sets default STREAM_0 only, other streams are
	 * set by audio driver based on the hardware/software support.
	 */
	rc = ext->intf_ops.audio_config(audio->ext_pdev,
			&ext->codec, state);
	if (rc)
		DP_WARN("failed to config audio, err=%d\n", rc);
end:
	return rc;
}

static int dp_hfi_audio_notify_internal(struct dp_hfi_audio_private *audio, u32 state,
		bool skip_wait)
{
	int rc = 0;
	struct msm_ext_disp_init_data *ext = &audio->ext_audio_data;

	atomic_set(&audio->acked, 0);

	if (!ext->intf_ops.audio_notify) {
		DP_ERR("audio notify not defined\n");
		goto end;
	}

	reinit_completion(&audio->hpd_comp);
	rc = ext->intf_ops.audio_notify(audio->ext_pdev,
			&ext->codec, state);
	if (rc)
		goto end;

	if (skip_wait || atomic_read(&audio->acked))
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

static int dp_hfi_audio_get_edid_blk(struct platform_device *pdev,
		struct msm_ext_disp_audio_edid_blk *blk)
{
	struct dp_hfi_audio_private *audio;

	if (!blk) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	audio = dp_hfi_audio_get_data(pdev);
	if (IS_ERR(audio))
		return PTR_ERR(audio);

	/* TODO: Implement EDID retrieval via HFI if needed */
	blk->audio_data_blk = NULL;
	blk->audio_data_blk_size = 0;
	blk->spk_alloc_data_blk = NULL;
	blk->spk_alloc_data_blk_size = 0;

	return 0;
}

static int dp_hfi_audio_deregister_ext_disp(struct dp_hfi_audio_private *audio)
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

	pd = of_parse_phandle(audio->pdev->dev.of_node, phandle, 0);
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

static int dp_hfi_audio_info_setup(struct platform_device *pdev,
		struct msm_ext_disp_audio_setup_params *params)
{
	int rc = 0;
	struct dp_hfi_audio_private *audio;
	struct hfi_audio_config audio_config;
	u32 stream_id;

	audio = dp_hfi_audio_get_data(pdev);
	if (!audio) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	if (audio->dp_audio.tui_active) {
		DP_DEBUG("TUI session active\n");
		return 0;
	}

	mutex_lock(&audio->ops_lock);

	audio->channels = params->num_of_channels;

	/* TODO: Determine stream_id properly - for now use 0 */
	stream_id = 0;

	/* Prepare HFI audio config structure */
	memset(&audio_config, 0, sizeof(audio_config));
	audio_config.sample_rate = params->sample_rate_hz;
	audio_config.num_of_channels = params->num_of_channels;
	audio_config.channel_allocation = params->channel_allocation;
	audio_config.level_shift = params->level_shift;
	audio_config.down_mix = params->down_mix;
	audio_config.sample_present = params->sample_present;
	audio_config.stream_id = stream_id;

	DP_DEBUG("Audio config: rate=%u, channels=%u, stream=%u\n",
		 audio_config.sample_rate, audio_config.num_of_channels,
		 audio_config.stream_id);

	/* Send audio configuration to DCP via HFI */
	rc = dp_mgr_hfi_send_audio_config(audio->client, &audio_config);
	if (rc) {
		DP_ERR("Failed to send audio config via HFI, rc=%d\n", rc);
		mutex_unlock(&audio->ops_lock);
		return rc;
	}

	/* Enable audio on DCP via HFI */
	// rc = dp_mgr_hfi_send_audio_control(audio->client, HFI_TRUE);
	if (rc) {
		DP_ERR("Failed to enable audio via HFI, rc=%d\n", rc);
		mutex_unlock(&audio->ops_lock);
		return rc;
	}

	/*
	 * Note: All hardware setup (SDP, ACR, enable) is now handled
	 * by DCP firmware. No local hardware programming needed.
	 */

	mutex_unlock(&audio->ops_lock);

	DP_DEBUG("audio stream configured via HFI\n");

	return rc;
}

static int dp_hfi_audio_get_cable_status(struct platform_device *pdev, u32 vote)
{
	struct dp_hfi_audio_private *audio;

	audio = dp_hfi_audio_get_data(pdev);
	if (IS_ERR(audio))
		return PTR_ERR(audio);

	return atomic_read(&audio->session_on);
}

static int dp_hfi_audio_get_intf_id(struct platform_device *pdev)
{
	return EXT_DISPLAY_TYPE_DP;
}

static int dp_hfi_audio_acknowledge(struct platform_device *pdev, u32 ack)
{
	return dp_hfi_audio_ack_done(pdev, ack);
}

static int dp_hfi_audio_codec_ready(struct platform_device *pdev)
{
	int rc = 0;
	struct dp_hfi_audio_private *audio;

	audio = dp_hfi_audio_get_data(pdev);
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

static void dp_hfi_audio_teardown_done(struct platform_device *pdev)
{
	struct dp_hfi_audio_private *audio;
	int rc;

	audio = dp_hfi_audio_get_data(pdev);
	if (!audio) {
		DP_ERR("invalid input\n");
		return;
	}

	if (audio->dp_audio.tui_active) {
		DP_DEBUG("TUI session active\n");
		return;
	}

	mutex_lock(&audio->ops_lock);

	/* Send audio OFF command to DCP via HFI */
	rc = dp_mgr_hfi_send_audio_control(audio->client, HFI_FALSE);
	if (rc)
		DP_ERR("Failed to disable audio via HFI, rc=%d\n", rc);

	/*
	 * Note: All hardware teardown is handled by DCP firmware.
	 * No local hardware programming needed.
	 */

	mutex_unlock(&audio->ops_lock);

	atomic_set(&audio->acked, 1);
	complete_all(&audio->hpd_comp);

	DP_DEBUG("audio engine disabled via HFI\n");
}

static int dp_hfi_audio_register_ext_disp(struct dp_hfi_audio_private *audio)
{
	int rc = 0;
	struct device_node *pd = NULL;
	const char *phandle = "qcom,ext-disp";
	struct msm_ext_disp_init_data *ext;
	struct msm_ext_disp_audio_codec_ops *ops;

	ext = &audio->ext_audio_data;
	ops = &ext->codec_ops;

	ext->codec.type = EXT_DISPLAY_TYPE_DP;
	ext->codec.ctrl_id = 0;
	ext->codec.stream_id = 0; /* Default to stream 0 for HFI */
	ext->pdev = audio->pdev;
	ext->intf_data = &audio->dp_audio;

	ops->audio_info_setup = dp_hfi_audio_info_setup;
	ops->get_audio_edid_blk = dp_hfi_audio_get_edid_blk;
	ops->cable_status = dp_hfi_audio_get_cable_status;
	ops->get_intf_id = dp_hfi_audio_get_intf_id;
	ops->teardown_done = dp_hfi_audio_teardown_done;
	ops->acknowledge = dp_hfi_audio_acknowledge;
	ops->ready = dp_hfi_audio_codec_ready;

	if (!audio->pdev->dev.of_node) {
		DP_ERR("cannot find audio dev.of_node\n");
		rc = -ENODEV;
		goto end;
	}

	pd = of_parse_phandle(audio->pdev->dev.of_node, phandle, 0);
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
	rc = msm_ext_disp_register_intf(audio->ext_pdev, ext);
	if (rc)
		DP_ERR("failed to register disp\n");
#endif

end:
	if (pd)
		of_node_put(pd);

	return rc;
}

static int dp_hfi_audio_on(struct dp_audio *dp_audio)
{
	int rc = 0;
	struct dp_hfi_audio_private *audio;

	if (!dp_audio) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	audio = container_of(dp_audio, struct dp_hfi_audio_private, dp_audio);
	if (IS_ERR(audio)) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	dp_hfi_audio_register_ext_disp(audio);

	atomic_set(&audio->session_on, 1);

	rc = dp_hfi_audio_config(audio, EXT_DISPLAY_CABLE_CONNECT);
	if (rc)
		goto end;

	queue_delayed_work(audio->notify_workqueue, &audio->notify_delayed_work, HZ/4);

	DP_DEBUG("success\n");
end:
	return rc;
}

static int dp_hfi_audio_off(struct dp_audio *dp_audio, bool skip_wait)
{
	int rc = 0;
	struct dp_hfi_audio_private *audio;
	bool work_pending = false;

	if (!dp_audio) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	audio = container_of(dp_audio, struct dp_hfi_audio_private, dp_audio);

	if (!atomic_read(&audio->session_on)) {
		DP_DEBUG("audio already off\n");
		return rc;
	}

	work_pending = cancel_delayed_work_sync(&audio->notify_delayed_work);
	if (work_pending)
		DP_DEBUG("pending notification work completed\n");

	rc = dp_hfi_audio_notify_internal(audio, EXT_DISPLAY_CABLE_DISCONNECT, skip_wait);
	if (rc)
		goto end;

	DP_DEBUG("success\n");
end:
	dp_hfi_audio_config(audio, EXT_DISPLAY_CABLE_DISCONNECT);

	atomic_set(&audio->session_on, 0);
	audio->engine_on = false;

	dp_hfi_audio_deregister_ext_disp(audio);

	return rc;
}

static void dp_hfi_audio_notify_work_fn(struct work_struct *work)
{
	struct dp_hfi_audio_private *audio;
	struct delayed_work *dw = to_delayed_work(work);

	audio = container_of(dw, struct dp_hfi_audio_private, notify_delayed_work);

	dp_hfi_audio_notify_internal(audio, EXT_DISPLAY_CABLE_CONNECT, true);
}

static int dp_hfi_audio_create_notify_workqueue(struct dp_hfi_audio_private *audio)
{
	audio->notify_workqueue = create_workqueue("sdm_dp_hfi_audio_notify");
	if (IS_ERR_OR_NULL(audio->notify_workqueue)) {
		DP_ERR("Error creating notify_workqueue\n");
		return -EPERM;
	}

	INIT_DELAYED_WORK(&audio->notify_delayed_work, dp_hfi_audio_notify_work_fn);

	return 0;
}

static void dp_hfi_audio_destroy_notify_workqueue(struct dp_hfi_audio_private *audio)
{
	if (audio->notify_workqueue)
		destroy_workqueue(audio->notify_workqueue);
}

struct dp_audio *dp_hfi_audio_get(struct platform_device *pdev,
			struct dp_client *client)
{
	int rc = 0;
	struct dp_hfi_audio_private *audio;
	struct dp_audio *dp_audio;

	if (!pdev || !client) {
		DP_ERR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	audio = devm_kzalloc(&pdev->dev, sizeof(*audio), GFP_KERNEL);
	if (!audio) {
		rc = -ENOMEM;
		goto error;
	}

	audio->pdev = pdev;
	audio->client = client;

	rc = dp_hfi_audio_create_notify_workqueue(audio);
	if (rc)
		goto error_notify_workqueue;

	init_completion(&audio->hpd_comp);
	atomic_set(&audio->acked, 0);
	atomic_set(&audio->session_on, 0);
	mutex_init(&audio->ops_lock);

	dp_audio = &audio->dp_audio;

	dp_audio->on  = dp_hfi_audio_on;
	dp_audio->off = dp_hfi_audio_off;

	DP_DEBUG("HFI audio initialized\n");

	return dp_audio;

error_notify_workqueue:
	devm_kfree(&pdev->dev, audio);
error:
	return ERR_PTR(rc);
}

void dp_hfi_audio_put(struct dp_audio *dp_audio)
{
	struct dp_hfi_audio_private *audio;

	if (!dp_audio)
		return;

	audio = container_of(dp_audio, struct dp_hfi_audio_private, dp_audio);

	dp_hfi_audio_deregister_ext_disp(audio);

	mutex_destroy(&audio->ops_lock);

	dp_hfi_audio_destroy_notify_workqueue(audio);

	devm_kfree(&audio->pdev->dev, audio);
}
