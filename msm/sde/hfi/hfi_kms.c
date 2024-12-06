// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include "sde_connector.h"
#include "hfi_crtc.h"
#include "hfi_kms.h"
#include "hfi_msm_drv.h"
#include "hfi_catalog.h"
#include "hfi_msm_drv.h"
#include "sde_encoder.h"
#include "sde_formats.h"
#include "hfi_utils.h"

#define DWORDS_TO_BYTES(x) (x * 4)
#define BYTES_TO_DWORDS(x) (x / 4)

/*
 * minimum required size for commn caps is 1 dword for number of caps &
 * at least 2 dwords to hold each key-value pair
 */
#define MIN_BYTES_FOR_COMMON_CAPS(x) DWORDS_TO_BYTES(x * 2)

/*
 * minimum required size for pipe caps is size of formats list + size of caps
 * size of fromats is 1 dword for format count + dword each for every format
 * and in best case with 0 property 1 dword for property count
 */
#define MIN_BYTES_FOR_PIPE_CAPS(x) DWORDS_TO_BYTES(1 + x[0] +  1)

static int hfi_kms_atomic_check(struct sde_kms *kms, struct drm_atomic_state *state)
{
	struct drm_connector *conn;
	struct drm_connector_state *conn_state;
	struct hfi_cmdbuf_t *cmd_buf;
	struct hfi_kms *hfi_kms;
	int check_status;
	u32 disp_id;
	int i, ret;

	if (!kms)
		return -EINVAL;

	hfi_kms = to_hfi_kms(kms);

	SDE_DEBUG("invoking atomic check helper\n");
	check_status = drm_atomic_helper_check(kms->dev, state);
	/* if atomic check failed return */
	if (check_status) {
		SDE_ERROR("failed DRM atomic check\n");
		return check_status;
	}

	for_each_new_connector_in_state(state, conn, conn_state, i) {
		if (!conn_state->crtc && !conn->state->crtc)
			continue;

		disp_id = sde_conn_get_display_obj_id(conn_state->connector);
		SDE_DEBUG("after atomic_check for disp_id:%d\n", disp_id);

		cmd_buf = hfi_kms_get_cmd_buf(hfi_kms, disp_id, HFI_CMDBUF_TYPE_ATOMIC_CHECK);
		if (!cmd_buf) {
			SDE_ERROR("failed to get cmd_buf for conn:%d disp_id:%d\n",
					DRMID(conn), disp_id);
			return -EINVAL;
		}

		ret = hfi_adapter_set_cmd_buf(cmd_buf);
		if (ret) {
			SDE_ERROR("failed to send atomic check\n");
			return ret;
		}
	}

	return check_status ? check_status : ret;
}

static int hfi_kms_prepare_commit(struct sde_kms *kms,
		struct drm_atomic_state *state)
{
	int i, ret = 0;
	u32 disp_id;
	u32 encoder_mask;
	struct hfi_cmdbuf_t *cmd_buf;
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;
	struct sde_crtc *sde_crtc;
	struct drm_crtc_state *cstate;
	struct hfi_kms *hfi_kms;

	if (!kms)
		return -EINVAL;

	hfi_kms = to_hfi_kms(kms);

	for_each_new_crtc_in_state(state, crtc, cstate, i) {
		sde_crtc = to_sde_crtc(crtc);

		encoder_mask = cstate->encoder_mask ?
			cstate->encoder_mask : sde_crtc->cached_encoder_mask;
		SDE_DEBUG("crtc:%d encoder_mask:0x%x\n", DRMID(crtc), encoder_mask);

		drm_for_each_encoder_mask(encoder, kms->dev, encoder_mask) {
			disp_id = hfi_crtc_get_display_id(crtc, cstate);
			SDE_DEBUG("creating cmd buffer for disp_id:%d\n", disp_id);

			cmd_buf = hfi_adapter_get_cmd_buf(&hfi_kms->hfi_client,
					disp_id, HFI_CMDBUF_TYPE_ATOMIC_COMMIT);
			if (!cmd_buf) {
				SDE_ERROR("failed to get cmd_buf for crtc:%d disp_id:%d\n",
						DRMID(crtc), disp_id);
				return -EINVAL;
			}
		}
	}

	SDE_DEBUG("done\n");

	return ret;
}

static int hfi_kms_trigger_commit(struct sde_kms *kms,
		struct drm_atomic_state *state)
{
	int i, ret;
	u32 disp_id;
	u32 payload = HFI_COMMIT;
	struct hfi_cmdbuf_t *cmd_buf;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct hfi_kms *hfi_kms;
	struct drm_encoder *encoder;
	struct drm_device *dev;
	u32 pending_commit_count;

	if (!kms || !state)
		return -EINVAL;

	hfi_kms = to_hfi_kms(kms);

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		if (crtc->state->active || crtc_state->active || crtc_state->active_changed) {
			disp_id = hfi_crtc_get_display_id(crtc, crtc_state);
			SDE_DEBUG("getting cmd buffer for disp_id:%d\n", disp_id);

			cmd_buf = hfi_kms_get_cmd_buf(hfi_kms, disp_id,
					HFI_CMDBUF_TYPE_ATOMIC_COMMIT);
			if (!cmd_buf) {
				SDE_ERROR("failed to get cmd_buf for crtc:%d disp_id:%d\n",
						DRMID(crtc), disp_id);
				return -EINVAL;
			}

			ret = hfi_adapter_add_set_property(cmd_buf,
					HFI_COMMAND_DISPLAY_FRAME_TRIGGER, MSM_DRV_HFI_ID,
					HFI_PAYLOAD_TYPE_U32, &payload, sizeof(u32), 0);

			dev = crtc->dev;
			list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
				if (encoder->crtc != crtc)
					continue;

				pending_commit_count = sde_encoder_helper_inc_pending(encoder);
				SDE_EVT32(pending_commit_count);
			}

			ret = hfi_adapter_set_cmd_buf(cmd_buf);
			if (ret) {
				SDE_ERROR("failed to send commit buffer\n");
				return ret;
			}
		}
	}

	return ret;
}

static int hfi_kms_commit(struct sde_kms *kms,
		struct drm_atomic_state *state)
{
	int i, ret = 0;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;

	if (!kms || !state)
		return -EINVAL;

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		if (crtc->state->active || crtc_state->active) {
			SDE_DEBUG(" crtc:%d\n", DRMID(crtc));
			sde_crtc_commit_kickoff(crtc, crtc_state);
		}
	}

	return ret;
}

static int hfi_kms_process_cmd_buf(struct hfi_client_t *client, struct hfi_cmdbuf_t *cmd_buf)
{
	int rc;

	SDE_DEBUG("process cmd-buf called\n");

	/*
	 * If no crtcs and encoders are initialized, cannot switch context
	 * Currently adapter thread is used, need to schedule unpack to right event thread
	 */
	rc = hfi_adapter_unpack_cmd_buf(client, cmd_buf);
	if (rc)
		SDE_ERROR("[WARNING] error in response packet or unpacking buffer\n");

	rc = hfi_adapter_release_cmd_buf(cmd_buf);
	if (rc)
		SDE_ERROR("[WARNING] Failed to release command buffer\n");

	return rc;
}

static const struct sde_kms_hal_funcs hfi_hal_funcs = {
	.atomic_check = hfi_kms_atomic_check,
	.prepare_commit = hfi_kms_prepare_commit,
	.commit = hfi_kms_commit,
	.trigger_commit = hfi_kms_trigger_commit,
};

static int _hfi_kms_setup_hfi(struct hfi_adapter_t *adapter, struct hfi_kms *hfi_kms)
{
	int ret;

	if (!adapter || !hfi_kms)
		return -EINVAL;

	hfi_kms->hfi_adapter = adapter;
	hfi_kms->hfi_client.process_cmd_buf = hfi_kms_process_cmd_buf;

	ret = hfi_adapter_client_register(hfi_kms->hfi_adapter, &hfi_kms->hfi_client);
	if (ret) {
		SDE_ERROR("failed to register as adapter client\n");
		return ret;
	}

	return ret;
}

struct hfi_cmdbuf_t *hfi_kms_get_cmd_buf(struct hfi_kms *hfi_kms,
		u16 display_id, u32 cmd_type)
{
	struct hfi_cmdbuf_t *ret_buf = NULL;
	struct hfi_cmdbuf_t *buf;
	struct hfi_client_t *hfi_client;

	if (!hfi_kms) {
		SDE_ERROR("Invalid hfi_kms\n");
		return NULL;
	}

	hfi_client = &hfi_kms->hfi_client;

	list_for_each_entry(buf, &hfi_client->cmd_buf_list, node) {
		if (buf->cmd_type == cmd_type && buf->obj_id == display_id) {
			ret_buf = buf;
			break;
		}
	}

	return ret_buf;
}

int hfi_kms_reg_client(struct drm_device *dev)
{
	int ret;
	struct msm_drm_hfi_private *hfi_drv_priv;
	struct sde_kms *sde_kms;
	struct msm_drm_private *priv;

	if (!dev || !dev->dev_private)
		return -EINVAL;

	priv = dev->dev_private;
	hfi_drv_priv = priv->hfi_priv;
	sde_kms = to_sde_kms(priv->kms);

	ret = _hfi_kms_setup_hfi(hfi_drv_priv->hfi_adapter, sde_kms->hfi_kms);
	if (ret) {
		SDE_ERROR("failed to setup HFI client ret=%d\n", ret);
		return -HFI_ERROR;
	}

	return 0;

}

int hfi_kms_init(struct sde_kms *sde_kms)
{
	struct hfi_kms *hfi_kms;

	if (!sde_kms)
		return -EINVAL;

	hfi_kms = kvzalloc(sizeof(*hfi_kms), GFP_KERNEL);
	if (!hfi_kms) {
		SDE_ERROR("failed to allocate hfi_kms\n");
		return -ENOMEM;
	}

	sde_kms->hfi_kms = hfi_kms;
	sde_kms->hal_ops = hfi_hal_funcs;
	hfi_kms->base = sde_kms;

	return 0;
}

void hfi_kms_resource_vote_hfi_prop_handler(u32 obj_uid, u32 CMD_ID, void *payload, u32 size,
			struct hfi_prop_listener *resource_vote_listener)
{
	struct msm_drm_private *priv;
	struct sde_kms *sde_kms;
	struct hfi_kms *hfi_kms;
	u32 bus_id, i, prop_count;
	struct hfi_kv_pairs *kv_pairs;
	struct hfi_bw_config bw_config;
	struct dss_module_power *mp;
	struct sde_power_handle *phandle;
	u32 dirty[SDE_POWER_HANDLE_DBUS_ID_MAX] = {0,};
	u32 rc;
	u32 enable;

	if (!payload || !resource_vote_listener)
		return;

	hfi_kms = container_of(resource_vote_listener, struct hfi_kms, resource_vote_listener);

	sde_kms = hfi_kms->base;
	priv = sde_kms->dev->dev_private;
	phandle = &priv->phandle;
	mp = &phandle->mp;

	if (CMD_ID == HFI_COMMAND_DEVICE_CALLBACK_RESOURCE_VOTE) {
		prop_count = size/sizeof(struct hfi_kv_pairs);
		kv_pairs = payload;
		//HFI_UNPACK_KEY(kv_pairs[i].key, property_id, version, size);
		for (i = 0; i < prop_count; i++) {
			switch (kv_pairs[i].key) {
			case HFI_PROPERTY_DEVICE_CORE_IB:
				bw_config.ib_vote[SDE_POWER_HANDLE_DBUS_ID_MNOC] =
					*(u64 *)kv_pairs[i].value_ptr;
				dirty[SDE_POWER_HANDLE_DBUS_ID_MNOC] = 1;
				break;
			case HFI_PROPERTY_DEVICE_CORE_AB:
				bw_config.ab_vote[SDE_POWER_HANDLE_DBUS_ID_MNOC] =
					*(u64 *)kv_pairs[i].value_ptr;
				dirty[SDE_POWER_HANDLE_DBUS_ID_MNOC] = 1;
				break;
			case HFI_PROPERTY_DEVICE_LLCC_IB:
				bw_config.ib_vote[SDE_POWER_HANDLE_DBUS_ID_LLCC] =
					*(u64 *)kv_pairs[i].value_ptr;
				dirty[SDE_POWER_HANDLE_DBUS_ID_LLCC] = 1;
				break;
			case HFI_PROPERTY_DEVICE_LLCC_AB:
				bw_config.ab_vote[SDE_POWER_HANDLE_DBUS_ID_LLCC] =
					*(u64 *)kv_pairs[i].value_ptr;
				dirty[SDE_POWER_HANDLE_DBUS_ID_LLCC] = 1;
				break;
			case HFI_PROPERTY_DEVICE_DRAM_IB:
				bw_config.ib_vote[SDE_POWER_HANDLE_DBUS_ID_EBI] =
					*(u64 *)kv_pairs[i].value_ptr;
				dirty[SDE_POWER_HANDLE_DBUS_ID_EBI] = 1;
				break;
			case HFI_PROPERTY_DEVICE_DRAM_AB:
				bw_config.ab_vote[SDE_POWER_HANDLE_DBUS_ID_EBI] =
					*(u64 *)kv_pairs[i].value_ptr;
				dirty[SDE_POWER_HANDLE_DBUS_ID_EBI] = 1;
				break;
			case HFI_PROPERTY_DEVICE_CORE_POWER_RAIL:
				enable = *(u32 *)kv_pairs[i].value_ptr;
				rc = msm_dss_enable_vreg(mp->vreg_config, mp->num_vreg,
						enable);
				break;
			}
		}

		for (bus_id = 0; bus_id < SDE_POWER_HANDLE_DBUS_ID_MAX; bus_id++) {
			if (dirty[bus_id]) {
				rc = sde_power_data_bus_set_quota(phandle,
						bus_id,
						bw_config.ab_vote[bus_id],
						bw_config.ib_vote[bus_id]);
				if (rc)
					pr_err("set quota failed\n");
			}
		}
	}
}
