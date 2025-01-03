// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/sort.h>
#include <drm/drm_atomic.h>
#include <linux/virtio_config.h>
#include <soc/qcom/boot_stats.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_helper.h>

#include "msm_hyp_trace.h"
#include "msm_hyp_utils.h"

#include <linux/habmm.h>
#include "virtio_kms.h"
#include "virtio_ext.h"
#include "virtgpu_vq.h"

#define HAB_NO_TIMEOUT_VAL                  -1
#define MAX_SEND_RECV_PACKET_RETRY          10

struct cmd_type {
	uint32_t cmd;
	char *cmd_name;
};

//TODO chck the usage of resp size
static int virtio_hab_send_and_recv(uint32_t hab_socket,
		struct channel_map *pHab_channel,
		void *req,
		uint32_t req_size,
		void *resp,
		uint32_t resp_size,
		bool lock_flag)
{
	int rc = 0;
	unsigned long delay = jiffies + (HZ / 4);
	uint32_t size = resp_size;
	uint32_t retry_times = 0;

	if (SPIN_LOCK_CHANNEL == lock_flag)
		spin_lock(&pHab_channel->hyp_chl_spin_lock);
	else
		mutex_lock(&pHab_channel->hyp_chl_lock[CHANNEL_CMD]);

retry_send_packet:
	rc = habmm_socket_send(hab_socket, req, req_size, (lock_flag == SPIN_LOCK_CHANNEL ?
				HABMM_SOCKET_SEND_FLAGS_NON_BLOCKING : 0x00));
	if (rc) {
		if ((rc == -EAGAIN) && (retry_times < MAX_SEND_RECV_PACKET_RETRY)) {
			retry_times++;
			pr_debug("send packet retry %d\n", retry_times);
			goto retry_send_packet;
		}
		rc = -1;
		pr_err("virtio :habmm_socket_send failed <%d>\n", rc);
		goto end;
	}
	if (!resp)
		goto end;

	retry_times = 0;

retry_recv_packet:
	do {
		size = resp_size;
		/* TODO: Need handle exit hab_receive during deinit */
		rc = habmm_socket_recv(hab_socket,
			resp,
			&size,
			(uint32_t)HAB_NO_TIMEOUT_VAL,
			HABMM_SOCKET_RECV_FLAGS_NON_BLOCKING);
		if (rc) {
			if (-ENODEV == rc)
				pr_err("virtio: channel broken - no device");
			else if (-EINTR == rc) {
				/*
				 * system is closed or suspend a interrupted
				 * system call is happening on hab channel.
				 * We should try it again
				 */
				pr_err("virtio :habmm_socket_recv - \
					interrupted system call - retry");
			}
		}
	} while ((time_before(jiffies, delay)) &&
			(-EAGAIN == rc) && (size == 0));

	if (rc) {
		if ((rc == -EAGAIN) && (retry_times < MAX_SEND_RECV_PACKET_RETRY))
		{
			retry_times++;
			pr_debug("virtio : recv packet retry %d", retry_times);
			goto retry_recv_packet;
		}
		rc = -1;
		goto end;
	}
	if (resp_size != size)
		pr_err("virtio : somethign wrong in the order of req and resp\n");
end:

	if (SPIN_LOCK_CHANNEL == lock_flag)
		spin_unlock(&pHab_channel->hyp_chl_spin_lock);
	else
		mutex_unlock(&pHab_channel->hyp_chl_lock[CHANNEL_CMD]);
	return rc;
}

int virtio_hab_send_and_recv_timeout(uint32_t hab_socket,
		struct mutex *pHab_lock,
		void *req,
		uint32_t req_size,
		void *resp,
		uint32_t resp_size)
{
	int rc = 0;
	uint32_t flags = HABMM_SOCKET_RECV_FLAGS_TIMEOUT;
	uint32_t size = resp_size;
	uint32_t max_retries = 10;
	mutex_lock(pHab_lock);
retry:
	rc = habmm_socket_send(hab_socket, req, req_size, 0x00);
	if (rc) {
		pr_err("virtio :habmm_socket_send failed <%d>\n", rc);
		rc = -1;
		goto end;
	}
	if (!resp)
		goto end;

	size = resp_size;
	rc = habmm_socket_recv(hab_socket,
		resp,
		&size,
		2500, flags);
		if (rc && max_retries) {
			max_retries--;
			pr_debug("virtio : recv timout retry %d\n", max_retries);
			goto retry;
		}
		else if (rc && !max_retries) {
			size = resp_size;
			pr_debug("virtio : retries done waiting for reply\n");
			rc = habmm_socket_recv(hab_socket,
				resp,
				&size,
				(uint32_t)-1, 0);
			if (rc)
				pr_err("virtio :socket_recv failed <%d>\n",rc);
		}
end:
	mutex_unlock(pHab_lock);
	return rc;
}


static char *virtio_cmd_type(uint32_t cmd)
{
	char *cmd_name = NULL;
	static struct cmd_type  s_cmd[] = {
		{VIRTIO_GPU_CMD_GET_DISPLAY_INFO,
			"VIRTIO_GPU_CMD_GET_DISPLAY_INFO"},
		{VIRTIO_GPU_CMD_GET_DISPLAY_INFO_EXT,
			"VIRTIO_GPU_CMD_GET_DISPLAY_INFO_EXT"},
		{VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
			"VIRTIO_GPU_CMD_RESOURCE_CREATE_2D"},
		{VIRTIO_GPU_CMD_RESOURCE_UNREF,
			"VIRTIO_GPU_CMD_RESOURCE_UNREF"},
		{VIRTIO_GPU_CMD_SET_SCANOUT,
			"VIRTIO_GPU_CMD_SET_SCANOUT"},
		{VIRTIO_GPU_CMD_RESOURCE_FLUSH,
			"VIRTIO_GPU_CMD_RESOURCE_FLUSH"},
		{VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
			"VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D"},
		{VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
			"VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING"},
		{VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING_EXT,
			"VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING_EXT"},
		{VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING,
			"VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING"},
		{VIRTIO_GPU_CMD_GET_CAPSET_INFO,
			"VIRTIO_GPU_CMD_GET_CAPSET_INFO"},
		{VIRTIO_GPU_CMD_GET_CAPSET,
			"VIRTIO_GPU_CMD_GET_CAPSET"},
		{VIRTIO_GPU_CMD_GET_EDID,
			"VIRTIO_GPU_CMD_GET_EDID"},
		{VIRTIO_GPU_CMD_GET_SCANOUT_ATTRIBUTES,
			"VIRTIO_GPU_CMD_GET_SCANOUT_ATTRIBUTES"},
		{VIRTIO_GPU_CMD_SET_SCANOUT_PROPERTIES,
			"VIRTIO_GPU_CMD_SET_SCANOUT_PROPERTIES"},
		{VIRTIO_GPU_CMD_GET_SCANOUT_PLANES,
			"VIRTIO_GPU_CMD_GET_SCANOUT_PLANES"},
		{VIRTIO_GPU_CMD_GET_PLANES_CAPS,
			"VIRTIO_GPU_CMD_GET_PLANES_CAPS"},
		{VIRTIO_GPU_CMD_PLANE_CREATE,
			"VIRTIO_GPU_CMD_PLANE_CREATE"},
		{VIRTIO_GPU_CMD_PLANE_DESTROY,
			"VIRTIO_GPU_CMD_PLANE_DESTROY"},
		{VIRTIO_GPU_CMD_GET_PLANE_PROPERTIES,
			"VIRTIO_GPU_CMD_GET_PLANE_PROPERTIES"},
		{VIRTIO_GPU_CMD_SET_PLANE_PROPERTIES,
			"VIRTIO_GPU_CMD_SET_PLANE_PROPERTIES"},
		{VIRTIO_GPU_CMD_SET_PLANE,
			"VIRTIO_GPU_CMD_SET_PLANE"},
		{VIRTIO_GPU_CMD_SCANOUT_FLUSH,
			"VIRTIO_GPU_CMD_SCANOUT_FLUSH"},
		{VIRTIO_GPU_CMD_PLANE_FLUSH,
			"VIRTIO_GPU_CMD_PLANE_FLUSH"},
		{VIRTIO_GPU_CMD_FULL_FLUSH,
			"VIRTIO_GPU_CMD_FULL_FLUSH"},
		{VIRTIO_GPU_CMD_EVENT_CONTROL,
			"VIRTIO_GPU_CMD_EVENT_CONTROL"},
		{VIRTIO_GPU_CMD_WAIT_EVENTS,
			"VIRTIO_GPU_CMD_WAIT_EVENTS"},
		{VIRTIO_GPU_RESP_OK_NODATA,
			"VIRTIO_GPU_RESP_OK_NODATA"},
		{VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
			"VIRTIO_GPU_RESP_OK_DISPLAY_INFO"},
		{VIRTIO_GPU_RESP_OK_DISPLAY_INFO_EXT,
			"VIRTIO_GPU_RESP_OK_DISPLAY_INFO_EXT"},
		{VIRTIO_GPU_RESP_OK_DEVICE_INFO,
			"VIRTIO_GPU_RESP_OK_DEVICE_INFO"},
		{VIRTIO_GPU_RESP_OK_CAPSET_INFO,
			"VIRTIO_GPU_RESP_OK_CAPSET_INFO"},
		{VIRTIO_GPU_RESP_OK_CAPSET,
			"VIRTIO_GPU_RESP_OK_CAPSET"},
		{VIRTIO_GPU_RESP_OK_EDID,
			"VIRTIO_GPU_RESP_OK_EDID"},
		{VIRTIO_GPU_RESP_OK_SCANOUT_ATTRIBUTES,
			"VIRTIO_GPU_RESP_OK_SCANOUT_ATTRIBUTES"},
		{VIRTIO_GPU_RESP_OK_SET_SCANOUT_PROPERTIES,
			"VIRTIO_GPU_RESP_OK_SET_SCANOUT_PROPERTIES"},
		{VIRTIO_GPU_RESP_OK_GET_SCANOUT_PLANES,
			"VIRTIO_GPU_RESP_OK_GET_SCANOUT_PLANES"},
		{VIRTIO_GPU_RESP_OK_GET_PLANES_CAPS,
			"VIRTIO_GPU_RESP_OK_GET_PLANES_CAPS"},
		{VIRTIO_GPU_RESP_OK_PLANE_CREATE,
			"VIRTIO_GPU_RESP_OK_PLANE_CREATE"},
		{VIRTIO_GPU_RESP_OK_PLANE_DESTROY,
			"VIRTIO_GPU_RESP_OK_PLANE_DESTROY"},
		{VIRTIO_GPU_RESP_OK_GET_PLANE_PROPERTIES,
			"VIRTIO_GPU_RESP_OK_GET_PLANE_PROPERTIES"},
		{VIRTIO_GPU_RESP_OK_SET_PLANE_PROPERTIES,
			"VIRTIO_GPU_RESP_OK_SET_PLANE_PROPERTIES"},
		{VIRTIO_GPU_RESP_OK_SET_PLANE,
			"VIRTIO_GPU_RESP_OK_SET_PLANE"},
		{VIRTIO_GPU_RESP_OK_SCANOUT_FLUSH,
			"VIRTIO_GPU_RESP_OK_SCANOUT_FLUSH"},
		{VIRTIO_GPU_RESP_OK_PLANE_FLUSH,
			"VIRTIO_GPU_RESP_OK_PLANE_FLUSH"},
		{VIRTIO_GPU_RESP_OK_FULL_FLUSH,
			"VIRTIO_GPU_RESP_OK_FULL_FLUSH"},
		{VIRTIO_GPU_RESP_OK_WAIT_FOR_EVENTS,
			"VIRTIO_GPU_RESP_OK_WAIT_FOR_EVENTS"},
		{VIRTIO_GPU_RESP_ERR_UNSPEC,
			"VIRTIO_GPU_RESP_ERR_UNSPEC"},
		{VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
			"VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY"},
		{VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
			"VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID"},
		{VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
			"VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID"},
		{VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
			"VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID"},
		{VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
			"VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER"},
		{VIRTIO_GPU_RESP_ERR_UNSUPPORTED_COMMAND,
			"VIRTIO_GPU_RESP_ERR_UNSUPPORTED_COMMAND"},
		{VIRTIO_GPU_RESP_ERR_BACKING_SWAP_NOT_SUPPORTED,
			"VIRTIO_GPU_RESP_ERR_BACKING_SWAP_NOT_SUPPORTED"},
		{VIRTIO_GPU_RESP_ERR_BACKING_IN_USE,
			"VIRTIO_GPU_RESP_ERR_BACKING_IN_USE"},
	};

	for (int i = 0; i < ARRAY_SIZE(s_cmd); i++) {
		if (s_cmd[i].cmd == cmd) {
			cmd_name = s_cmd[i].cmd_name;
			break;
		}
	}

	if (!cmd_name)
		cmd_name = "UNKNOWN";

	return cmd_name;
}

int virtio_gpu_cmd_set_scanout_pic_adjust(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t hue,
		uint32_t saturation,
		uint32_t contrast,
		uint32_t brightness)
{
	struct virtio_gpu_set_scanout_pic_adjust *req =
		kzalloc(sizeof(struct virtio_gpu_set_scanout_pic_adjust),
				GFP_KERNEL);
	struct virtio_gpu_resp_scanout_properties *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_scanout_pic_adjust),
				GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;
	uint32_t error_code = 0;

	if (!req || !resp) {
		pr_err("virtio :memory alloc failed req %p resp %p\n", req, resp);
		rc = -ENOMEM;
		goto error;
	}
	pr_debug("virtio: cmd VIRTIO_GPU_CMD_SET_PIC_ADJUST <%d> (%d %d %d %d)\n",
			scanout,
			hue, saturation, contrast, brightness);
	req->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_SET_PIC_ADJUST);
	req->hue = cpu_to_le32(hue);
	req->scanout_id = cpu_to_le32(scanout);
	req->saturation = cpu_to_le32(saturation);
	req->contrast = cpu_to_le32(contrast);
	req->brightness = cpu_to_le32(brightness);
	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			req,
			sizeof(struct virtio_gpu_set_scanout_pic_adjust),
			NULL,
			sizeof(struct virtio_gpu_resp_scanout_pic_adjust),
			NO_SPIN_LOCK_CHANNEL);
	if(rc) {
		pr_err("virtio :virtio_hab_send_and_recv failed\
				for SET_SCANOUT_PIC_ADJUST %d\n", rc);
		goto error;
	}
	error_code = le32_to_cpu(resp->error_code);
	if(error_code) {
		pr_err("virtio :SET_SCANOUT_PIC_ADJUST failed scanout %d error %d\n",
				scanout,
				error_code);
	}
error:
	if (req)
		kfree(req);
	if (resp)
		kfree(resp);

	return rc;

}

int virtio_gpu_cmd_set_scanout_properties(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t power_mode,
		uint32_t mode_index,
		uint32_t rotation,
		struct virtio_gpu_rect dest_rect)
{
	struct virtio_gpu_set_scanout_properties *req =
		kzalloc(sizeof(struct virtio_gpu_set_scanout_properties), GFP_KERNEL);
	struct virtio_gpu_resp_scanout_properties *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_scanout_properties), GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;
	uint32_t error_code = 0;

	if (!req || !resp) {
		pr_err("virtio :memory alloc failed req %p resp %p\n", req, resp);
		rc = -ENOMEM;
		goto error;
	}

	pr_debug("virtio: cmd set_scanout_properties scanout <%d> \
			[%d, %d, %d, %d, %d, %d,%d]\n",
			scanout, power_mode, mode_index,
			rotation, dest_rect.width,
			dest_rect.height, dest_rect.x, dest_rect.y);

	req->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_SET_SCANOUT_PROPERTIES);
	req->power_mode = cpu_to_le32(power_mode);
	req->scanout_id = cpu_to_le32(scanout);
	req->mode_index = cpu_to_le32(mode_index);
	req->rotation = cpu_to_le32(rotation);
	req->r.width = cpu_to_le32(dest_rect.width);
	req->r.height = cpu_to_le32(dest_rect.height);
	req->r.x = cpu_to_le32(dest_rect.x);
	req->r.y = cpu_to_le32(dest_rect.y);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			req,
			sizeof(struct virtio_gpu_set_scanout_properties),
			NULL,
			sizeof(struct virtio_gpu_resp_scanout_properties),
			NO_SPIN_LOCK_CHANNEL);
	if(rc) {
		pr_err("virtio_hab_send_and_recv failed\
				for SET_SCANOUT_PROPERTIES %d\n", rc);
		goto error;
	}
	error_code = le32_to_cpu(resp->error_code);
	if(error_code) {
		pr_err("virtio :SET_SCANOUT_PROPERTIES failed scanout %d error %d\n",
				scanout,
				error_code);
	}
error:
	if (req)
		kfree(req);
	if (resp)
		kfree(resp);

	return rc;
}

int virtio_gpu_cmd_set_scanout(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t res_id,
		struct virtio_gpu_rect dst_rect)
{
	struct virtio_gpu_set_scanout *req =
		kzalloc(sizeof(struct virtio_gpu_set_scanout), GFP_KERNEL);
	struct virtio_gpu_ctrl_hdr *resp =
		kzalloc(sizeof(struct virtio_gpu_ctrl_hdr),
		       GFP_KERNEL);

	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!req || !resp) {
		pr_err("memory alloc failed req %p resp %p\n", req, resp);
		rc = -ENOMEM;
		goto error;
	}

	pr_debug("virtio: VIRTIO_GPU_CMD_SET_SCANOUT scanout <%d> \
			[%d, %d, %d, %d, %d,]\n",
			scanout,
			res_id,
			dst_rect.width,
			dst_rect.height,
			dst_rect.x,
			dst_rect.y);

	req->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_SET_SCANOUT);
	req->scanout_id = cpu_to_le32(scanout);
	req->resource_id = cpu_to_le32(res_id);
	req->r.width = cpu_to_le32(dst_rect.width);
	req->r.height = cpu_to_le32(dst_rect.height);
	req->r.x = cpu_to_le32(dst_rect.x);
	req->r.y = cpu_to_le32(dst_rect.y);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			req,
			sizeof(struct virtio_gpu_set_scanout),
			NULL,
			sizeof(struct virtio_gpu_ctrl_hdr),
			NO_SPIN_LOCK_CHANNEL);
	if(rc) {
		pr_err("send_and_recv failed for SET_SCANOUT rc=%d\n", rc);
		goto error;
	}
error:
	if (req)
		kfree(req);
	if (resp)
		kfree(resp);

	return rc;
}

int virtio_gpu_cmd_resource_create_2D(struct virtio_kms *kms,
		uint32_t res_id,
		uint32_t format,
		uint32_t width,
		uint32_t height,
		uint32_t fence)
{
	struct virtio_gpu_resource_create_2d *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_resource_create_2d),
				GFP_KERNEL);
	struct virtio_gpu_ctrl_hdr *resp =
		kzalloc(sizeof(struct virtio_gpu_ctrl_hdr),
		       GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		pr_err("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}

	pr_debug("virtio: cmd VIRTIO_GPU_CMD_RESOURCE_CREATE_2D "\
			"<%d> (%d %d %d)\n", res_id, format, width, height);
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D);
	cmd_p->hdr.fence_id = cpu_to_le32(fence);
	cmd_p->hdr.flags |= cpu_to_le32(VIRTIO_GPU_FLAG_FENCE);
	cmd_p->resource_id = cpu_to_le32(res_id);
	cmd_p->format = cpu_to_le32(format);
	cmd_p->width = cpu_to_le32(width);
	cmd_p->height = cpu_to_le32(height);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_resource_create_2d),
			NULL,
			sizeof(struct virtio_gpu_ctrl_hdr),
			NO_SPIN_LOCK_CHANNEL);
	if(rc) {
		pr_err("send_and_recv failed for RESOURCE_CREATE_2D rc=%d\n", rc);
		goto error;
	}
error:
	if (cmd_p)
		kfree(cmd_p);
	if(resp)
		kfree(resp);

	return rc;
}

int virtio_gpu_cmd_resource_attach_backing(struct virtio_kms *kms,
		uint32_t resource_id,
		uint32_t shmem_id,
		uint32_t size)
{
	struct virtio_gpu_resource_attach_backing_ext *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_resource_attach_backing_ext),
				GFP_KERNEL);
	struct virtio_gpu_ctrl_hdr *resp =
		kzalloc(sizeof(struct virtio_gpu_ctrl_hdr),
		       GFP_KERNEL);

	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		pr_err("memory alloc failed req %p resp %p\n", cmd_p, resp);
		rc = -ENOMEM;
		goto error;
	}
	pr_debug("virtio: cmd VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING_EXT"
			"<%d> (%d, %d)\n", resource_id, shmem_id, size);
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING_EXT);
	cmd_p->resource_id = cpu_to_le32(resource_id);
	cmd_p->shmem_id = cpu_to_le64(shmem_id);
	cmd_p->size = cpu_to_le32(size);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_resource_attach_backing_ext),
			NULL,
			sizeof(struct virtio_gpu_ctrl_hdr),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		pr_err("virtio : send_and_recv failed for RESOURCE_ATTACH_BACKING %d\n", rc);
		goto error;
	}
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);

	return rc;
}

int virtio_gpu_cmd_resource_detach_backing(struct virtio_kms *kms,
		uint32_t resource_id)
{
	struct virtio_gpu_resource_detach_backing *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_resource_detach_backing),
				GFP_KERNEL);
	struct virtio_gpu_ctrl_hdr *resp =
		kzalloc(sizeof(struct virtio_gpu_ctrl_hdr),
		       GFP_KERNEL);

	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		pr_err("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}
	pr_debug("virtio: cmd VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING <%d>\n",
			resource_id);
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING);
	cmd_p->resource_id = cpu_to_le32(resource_id);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_resource_detach_backing),
			resp,
			sizeof(struct virtio_gpu_ctrl_hdr),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		pr_err("send_and_recv failed for RESOURCE_DETACH_BACKING rc=%d\n", rc);
		goto error;
	}

	pr_debug("virtio: resp VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING (%s)\n",
			virtio_cmd_type(le32_to_cpu(resp->type)));

error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);

	return rc;
}

int virtio_gpu_cmd_resource_unref(struct virtio_kms *kms,
		uint32_t resource_id)
{
	struct virtio_gpu_resource_unref *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_resource_unref),
				GFP_KERNEL);
	struct virtio_gpu_ctrl_hdr *resp =
		kzalloc(sizeof(struct virtio_gpu_ctrl_hdr),
		       GFP_KERNEL);

	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		pr_err("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}
	pr_debug("virtio: cmd VIRTIO_GPU_CMD_RESOURCE_UNREF <%d>\n",
			resource_id);

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_UNREF);
	cmd_p->resource_id = cpu_to_le32(resource_id);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_resource_unref),
			resp,
			sizeof(struct virtio_gpu_ctrl_hdr),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {

		pr_err("send_and_recv failed for RESOURCE_UNREF rc=%d\n", rc);
		goto error;
	}

	pr_debug("virtio: resp VIRTIO_GPU_CMD_RESOURCE_UNREF (%s)\n",
			virtio_cmd_type(le32_to_cpu(resp->type)));
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);
	return rc;
}

int virtio_gpu_cmd_plane_flush(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t plane_id,
		bool sync)
{
	struct virtio_gpu_plane_flush *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_plane_flush),
				GFP_KERNEL);
	struct virtio_gpu_resp_plane_flush *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_plane_flush),
				GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;
	uint32_t error = 0;

	if (!cmd_p || !resp) {
		pr_err("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}
	pr_debug("virtio: cmd VIRTIO_GPU_CMD_PLANE_FLUSH <%d> (%d, %d)\n",
			scanout, plane_id, sync);
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_PLANE_FLUSH);
	cmd_p->scanout_id = cpu_to_le32(scanout);
	cmd_p->plane_id = cpu_to_le32(plane_id);
	cmd_p->async_mode = cpu_to_le32(sync);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_plane_flush),
			sync ? resp : NULL,
			sizeof(struct virtio_gpu_resp_plane_flush),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		pr_err("send_and_recv failed for PLANE_FLUSH rc=%d\n", rc);
		goto error;
	}

	if (sync){
		pr_err("virtio: resp  VIRTIO_GPU_CMD_PLANE_FLUSH <%d> (%s)\n",
				le32_to_cpu(resp->scanout_id),
				virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

		error = le32_to_cpu(resp->error_code);
		if(error)
			pr_err("plane flush failed for scanout %d plane error%d\n",
					le32_to_cpu(resp->scanout_id),
					error);
	}

error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);

	return rc;
}


int virtio_gpu_cmd_scanout_flush(struct virtio_kms *kms,
		uint32_t scanout,
		bool sync)
{
	struct virtio_gpu_scanout_flush *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_scanout_flush),
				GFP_KERNEL);
	struct virtio_gpu_resp_scanout_flush *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_scanout_flush),
				GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;
//	uint32_t error_code = 0;

	if (!cmd_p || !resp) {
		pr_err("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}
	pr_debug("virtio: cmd VIRTIO_GPU_CMD_SCANOUT_FLUSH <%d> (%d)\n",
			scanout, sync);
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_SCANOUT_FLUSH);
	cmd_p->scanout_id = cpu_to_le32(scanout);
	cmd_p->async_mode = cpu_to_le32(sync);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_scanout_flush),
			NULL,
			sizeof(struct virtio_gpu_resp_scanout_flush),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		pr_err("send_and_recv failed for SCANOUT_FLUSH rc=%d\n", rc);
		goto error;
	}
/*
	if (!sync) {
		pr_debug("virtio: resp VIRTIO_GPU_CMD_SCANOUT_FLUSH <%d>(%s)\n",
			le32_to_cpu(resp->scanout_id),
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

		error_code = le32_to_cpu(resp->error_code);
		if(error_code)
			pr_err("scanout flush failed for %d error%d\n",
				resp->scanout_id,
				error_code);

		virtio_gpu_cmd_event_control(kms,
				scanout,
				VIRTIO_COMMIT_COMPLETE,
				true);

		virtio_gpu_cmd_event_wait(kms,
				scanout,
				1);
	}
*/
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);

	return rc;
}

int virtio_gpu_cmd_event_control(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t event_type,
		bool enable)
{
	struct virtio_gpu_event_control *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_event_control),
				GFP_KERNEL);
	struct virtio_gpu_ctrl_hdr *resp =
		kzalloc(sizeof(struct virtio_gpu_ctrl_hdr),
		       GFP_KERNEL);

	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		pr_err("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}
	pr_debug("virtio: cmd VIRTIO_GPU_CMD_EVENT_CONTROL <%d> (%d %d)\n",
			scanout, event_type, enable);
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_EVENT_CONTROL);
	cmd_p->scanout_id = cpu_to_le32(scanout);
	cmd_p->event_type = cpu_to_le32(event_type);
	cmd_p->enable = cpu_to_le32(enable);

	/* To avoid kernel panic, making use of spinlock instead of mutex because
	 * drm_vblank_enable use spinlock to call wfd communcation API.
	 * Holding spinlock then acquiring mutexlock would cause the kernel panic.
	 * */

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_event_control),
			NULL,
			sizeof(struct virtio_gpu_ctrl_hdr),
			SPIN_LOCK_CHANNEL);
	if (rc) {
		pr_err("send_and_recv failed for EVENT_CONTROL rc=%d\n", rc);
		goto error;
	}
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);
	return rc;
}

int virtio_gpu_cmd_event_wait(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t max_num_events)
{
	return 0;

}

static int virtio_get_edid_block(struct virtio_kms *kms, uint32_t scanout,
		void *buf, size_t len)
{
	kms->outputs[scanout].edid = kzalloc(len, GFP_KERNEL);
	memcpy(kms->outputs[scanout].edid, buf, len);
	return 0;
}

int virtio_gpu_cmd_get_edid(struct virtio_kms *kms,
		uint32_t scanout)
{
	struct virtio_gpu_cmd_get_edid *cmd_p;
	struct virtio_gpu_resp_edid *resp;
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;
	cmd_p = kzalloc(sizeof(struct virtio_gpu_cmd_get_edid), GFP_KERNEL);
	resp = kzalloc(sizeof(struct virtio_gpu_resp_edid), GFP_KERNEL);

	if (!cmd_p || !resp) {
		rc = -ENOMEM;
		goto error;
	}
	pr_debug("virtio: cmd VIRTIO_GPU_CMD_GET_EDID <%d>\n", scanout);
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_EDID);
	cmd_p->scanout = cpu_to_le32(scanout);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_cmd_get_edid),
			resp,
			sizeof(struct virtio_gpu_resp_edid),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		pr_err("send_and_recv failed for EVENT_CONTROL rc=%d\n", rc);
	}

	pr_debug("virtio: resp VIRTIO_GPU_CMD_GET_EDID (%s)\n",
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	virtio_get_edid_block(kms,
			scanout,
			resp->edid,
			le32_to_cpu(resp->size));

error:
	kfree(cmd_p);
	kfree(resp);
	return rc;
}

static void virtio_get_scanout_info(
		struct virtio_kms *kms,
		uint32_t scanout,
		struct virtio_gpu_resp_display_info_ext *resp)
{
	struct virtio_kms_output *output = &kms->outputs[scanout];
	int i = 0;
	uint32_t enabled;
	uint32_t num_modes = 0;

	for (i = 0; i < VIRTIO_GPU_MAX_MODES; i++) {
		enabled = le32_to_cpu(resp->pmodes[i].enabled);
		if (enabled) {
			output->info[num_modes].r.x =
				le32_to_cpu(resp->pmodes[i].r.x);
			output->info[num_modes].r.y =
				le32_to_cpu(resp->pmodes[i].r.y);
			output->info[num_modes].r.width =
				le32_to_cpu(resp->pmodes[i].r.width);
			output->info[num_modes].r.height =
				le32_to_cpu(resp->pmodes[i].r.height);
			output->info[num_modes].refresh =
				le32_to_cpu(resp->pmodes[i].refresh);
			output->info[num_modes].flags =
				le32_to_cpu(resp->pmodes[i].flags);
			pr_debug("virtio: scanout info <%d> <mode %d> (%dx%d+%d+%d@%d, %d)\n",
					scanout,
					i,
					output->info[num_modes].r.width,
					output->info[num_modes].r.height,
					output->info[num_modes].r.x,
					output->info[num_modes].r.y,
					output->info[num_modes].refresh,
					output->info[num_modes].flags);
			num_modes++;
		}
	}
	output->num_modes = num_modes;
}

static void virtio_get_device_info(
		struct virtio_kms *kms,
		struct virtio_gpu_resp_device_info *resp)
{
	kms->device_info.qseed_type = le32_to_cpu(resp->device_info.qseed_type);
	kms->device_info.max_mdp_clk = le32_to_cpu(resp->device_info.max_mdp_clk);
	kms->device_info.has_src_split = le32_to_cpu(resp->device_info.has_src_split);
	kms->device_info.device_version = le32_to_cpu(resp->device_info.device_version);

	pr_debug("virtio: device_info:\n");
	pr_debug("qseed_type: %d\n", kms->device_info.qseed_type);
	pr_debug("max_mdp_clk: %d\n", kms->device_info.max_mdp_clk);
	pr_debug("has_src_split: %d\n", kms->device_info.has_src_split);
	pr_debug("device_version: %d\n", kms->device_info.device_version);
}

void virio_get_scanout_numbers(struct virtio_kms *kms,
		struct virtio_gpu_resp_display_info *resp)
{
	int i;
	for (i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
		//intentionally not storing the nodes;
		if (resp->pmodes[i].enabled) {
			pr_debug("virtio: output %d: %dx%d+%d+%d\n", i,
				le32_to_cpu(resp->pmodes[i].r.width),
				le32_to_cpu(resp->pmodes[i].r.height),
				le32_to_cpu(resp->pmodes[i].r.x),
				le32_to_cpu(resp->pmodes[i].r.y));
			kms->num_scanouts++;
		} else {
			pr_debug("virtio: output %d: disabled", i);
		}
	}
}

int virtio_gpu_cmd_get_display_info(struct virtio_kms *kms)
{
	struct virtio_gpu_ctrl_hdr *cmd_p =
		kzalloc(sizeof( struct virtio_gpu_ctrl_hdr),
				GFP_KERNEL);
	struct virtio_gpu_resp_display_info *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_display_info),
			GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		pr_err("virtio :memory alloc failed\n");
		rc = -ENOMEM;
		goto error;
	}
	cmd_p->type = cpu_to_le32(VIRTIO_GPU_CMD_GET_DISPLAY_INFO);

	pr_debug("virtio: cmd VIRTIO_GPU_CMD_GET_DISPLAY_INFO\n");

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_ctrl_hdr),
			resp,
			sizeof(struct virtio_gpu_resp_display_info),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		pr_err("virtio send_and_recv failed for DISPLAY_INFO %d\n",
				rc);
	}
	pr_debug("virtio: resp VIRTIO_GPU_CMD_GET_DISPLAY_INFO (%s)\n",
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	virio_get_scanout_numbers(kms, resp);
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);
	return rc;
}

int virtio_gpu_cmd_get_display_info_ext(struct virtio_kms *kms,
		uint32_t scanout)
{
	struct virtio_gpu_get_display_info_ext *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_get_display_info_ext),
				GFP_KERNEL);
	struct virtio_gpu_resp_display_info_ext *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_display_info_ext),
				GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		pr_err("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}
	pr_debug("virtio: cmd VIRTIO_GPU_CMD_GET_DISPLAY_INFO_EXT <%d>\n",
			scanout);
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_DISPLAY_INFO_EXT);
	cmd_p->scanout_id = cpu_to_le32(scanout);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_get_display_info_ext),
			resp,
			sizeof(struct virtio_gpu_resp_display_info_ext),
			NO_SPIN_LOCK_CHANNEL);
	if (rc)
		pr_err("virtio : send_and_recv failed for DISPLAY_INFO_EXT %d\n", rc);

	pr_debug("virtio: resp VIRTIO_GPU_CMD_GET_DISPLAY_INFO_EXT <%d> (%s)\n",
			le32_to_cpu(resp->scanout_id),
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	virtio_get_scanout_info(kms, scanout, resp);
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);

	return rc;
}

int virtio_gpu_cmd_get_device_info(struct virtio_kms *kms)
{
	struct virtio_gpu_ctrl_hdr *cmd_p = NULL;
	struct virtio_gpu_resp_device_info *resp = NULL;
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	cmd_p = kzalloc(sizeof(struct virtio_gpu_ctrl_hdr),
				GFP_KERNEL);
	resp = kzalloc(sizeof(struct virtio_gpu_resp_device_info),
				GFP_KERNEL);
	if (!cmd_p || !resp) {
		pr_err("virtio :memory alloc failed\n");
		rc = -ENOMEM;
		goto error;
	}

	cmd_p->type = cpu_to_le32(VIRTIO_GPU_CMD_GET_DEVICE_INFO);

	pr_debug("virtio: cmd VIRTIO_GPU_CMD_GET_DEVICE_INFO\n");

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_ctrl_hdr),
			resp,
			sizeof(struct virtio_gpu_resp_device_info),
			NO_SPIN_LOCK_CHANNEL);
	if (rc)
		pr_err("virtio send_and_recv failed for DEVICE_INFO %d\n", rc);

	pr_debug("virtio: resp VIRTIO_GPU_CMD_GET_DEVICE_INFO (%s)\n",
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	virtio_get_device_info(kms, resp);
error:
	kfree(cmd_p);
	kfree(resp);
	return rc;
}

static void virtio_get_scanout_attribute(struct virtio_kms *kms,
		uint32_t scanout,
		struct virtio_gpu_resp_scanout_atttributes *resp)
{
	struct virtio_kms_output *output = &kms->outputs[scanout];

	output->attr.type = le32_to_cpu(resp->type);
	output->attr.connection_status = le32_to_cpu(resp->connection_status);
	output->attr.width_mm = le32_to_cpu(resp->width_mm);
	output->attr.height_mm = le32_to_cpu(resp->height_mm);
	output->attr.panel_orientation = le32_to_cpu(resp->panel_orientation);
	pr_debug("virtio : scanout %d attr <%d %d (%dX%d) panel_orientation %d>\n",
			scanout, output->attr.type,
			output->attr.connection_status,
			output->attr.width_mm,
			output->attr.height_mm,
			output->attr.panel_orientation);
}

int virtio_gpu_cmd_get_scanout_attributes(struct virtio_kms *kms,
		uint32_t scanout)
{
	struct virtio_gpu_get_scanout_attributes *cmd_p =
			kzalloc(sizeof(
				struct virtio_gpu_get_scanout_attributes),
			GFP_KERNEL);
	struct virtio_gpu_resp_scanout_atttributes *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_scanout_atttributes),
				GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		pr_err("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}

	pr_debug("virtio: cmd VIRTIO_GPU_CMD_GET_SCANOUT_ATTRIBUTE <%d>\n",
			scanout);
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_SCANOUT_ATTRIBUTES);
	cmd_p->scanout_id = cpu_to_le32(scanout);
	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_get_scanout_attributes),
			resp,
			sizeof(struct virtio_gpu_resp_scanout_atttributes),
			NO_SPIN_LOCK_CHANNEL);
	if (rc)
		pr_err("virtio : send_and_recv failed for SCANOUT_ATTRIBUTE %d\n", rc);

	pr_debug("virtio: resp  VIRTIO_GPU_CMD_GET_SCANOUT_ATTRIBUTE<%d>(%s)\n",
			le32_to_cpu(resp->scanout_id),
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	virtio_get_scanout_attribute(kms, scanout, resp);
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);

	return rc;
}

static void virtio_get_scanout_planes(struct virtio_kms *kms,
		uint32_t scanout,
		struct virtio_gpu_resp_scanout_planes *resp)
{
	struct virtio_kms_output *output = &kms->outputs[scanout];
	uint32_t i = 0;

	output->plane_cnt = le32_to_cpu(resp->num_planes);
	if (output->plane_cnt > VIRTIO_GPU_MAX_PLANES) {
		pr_err("virtio: To many planes %d\n", output->plane_cnt);
		output->plane_cnt = VIRTIO_GPU_MAX_PLANES;
	}
	pr_debug("virtio: plane scanout <%d>\n", scanout);
	for(i = 0; i < output->plane_cnt; i++) {
		output->plane_caps[i].plane_id =
			le32_to_cpu(resp->plane_ids[i]);
		pr_debug("%d -> %d\n", i, output->plane_caps[i].plane_id);
	}
}

int virtio_gpu_cmd_get_scanout_planes(struct virtio_kms *kms,
		uint32_t scanout)
{
	struct virtio_gpu_get_scanout_planes *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_get_scanout_planes),
			GFP_KERNEL);
	struct virtio_gpu_resp_scanout_planes *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_scanout_planes),
				GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		rc = -ENOMEM;
		goto error;
	}
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_SCANOUT_PLANES);
	cmd_p->scanout_id = cpu_to_le32(scanout);
	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_get_scanout_planes),
			resp,
			sizeof(struct virtio_gpu_resp_scanout_planes),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		pr_err("virtio :virtio_hab_send_and_recv failed \
				for SCANOUT_PLANES %d\n", rc);
		goto error;
	}

	if (scanout != le32_to_cpu(resp->scanout_id)) {
		pr_err("virtio :Somthing wrong with scanout ID\n");
		goto error;
	}

	pr_debug("virtio: resp VIRTIO_GPU_CMD_GET_SCANOUT_PLANES<%d> (%s)\n",
			le32_to_cpu(resp->scanout_id),
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	virtio_get_scanout_planes(kms, scanout, resp);

error:
	kfree(cmd_p);
	kfree(resp);
	return rc;
}

static int virtio_get_planes_caps(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t plane_id,
		struct virtio_gpu_resp_planes_caps *resp)
{
	struct virtio_kms_output *output = &kms->outputs[scanout];
	uint32_t i = 0;
	struct virtio_plane_caps *plane_caps = NULL;
	uint32_t plane = le32_to_cpu(resp->caps.plane_id);
	uint32_t num_formats = 0;

	for (i = 0; i < output->plane_cnt; i++) {
		//This might not needed if plane Id are in sequence.
		//TODO check if needed
		if(plane == output->plane_caps[i].plane_id) {
			plane_caps = &output->plane_caps[i];
			break;
		}
	}

	if (!plane_caps) {
		pr_err("virtio :Not valid plane caps ID->%d\n", plane_id);
		return -EINVAL;
	}

	plane_caps->plane_type = le32_to_cpu(resp->caps.plane_type);
	plane_caps->max_width = le32_to_cpu(resp->caps.max_width);
	plane_caps->max_height = le32_to_cpu(resp->caps.max_height);
	plane_caps->num_formats = le32_to_cpu(resp->caps.num_formats);
	for (i = 0; i < plane_caps->num_formats; i++) {
		if (!le32_to_cpu(resp->caps.formats[i]))
			continue;
		plane_caps->formats[num_formats] = le32_to_cpu(resp->caps.formats[i]);
		num_formats++;
	}
	plane_caps->min_scale = le32_to_cpu(resp->caps.min_scale);
	plane_caps->max_scale = le32_to_cpu(resp->caps.max_scale);
	plane_caps->num_formats = num_formats;
	plane_caps->pair_plane_id = le32_to_cpu(resp->caps.pair_plane_id);
	plane_caps->support_rotation = le32_to_cpu(resp->caps.support_rotation);
	pr_debug("virtio : plane caps <%d:%d> (%d, %d, %d, %d, %d %d\n",
			scanout,
			plane_id,
			plane_caps->plane_type,
			plane_caps->max_width,
			plane_caps->max_height,
			plane_caps->num_formats,
			plane_caps->pair_plane_id,
			plane_caps->support_rotation);

	for (i = 0; i < plane_caps->num_formats; i++) {
		pr_debug("%d\n", plane_caps->formats[i]);
	}

	return 0;
}

int virtio_gpu_cmd_get_plane_caps(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t plane_id)
{
	struct virtio_gpu_get_planes_caps *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_get_planes_caps),
			GFP_KERNEL);
	struct virtio_gpu_resp_planes_caps *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_planes_caps),
				GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;
	uint32_t scanout_rep = 0;
	uint32_t plain_id_rep = 0;

	if (!cmd_p || !resp) {
		pr_err("virtio :Memory allocation failed\n");
		goto error;
	}

	pr_debug("virtio: cmd VIRTIO_GPU_CMD_GET_PLANES_CAPS <%d> (%d)\n",
			scanout, plane_id);
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_PLANES_CAPS);
	cmd_p->scanout_id = cpu_to_le32(scanout);
	cmd_p->plane_id = cpu_to_le32(plane_id);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_get_planes_caps),
			resp,
			sizeof(struct virtio_gpu_resp_planes_caps),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		pr_err("virtio : virtio_hab_send_and_recv failed \
				for PLANE_CAPS %d\n", rc);
		goto error;
	}

	scanout_rep = le32_to_cpu(resp->caps.scanout_id);
	plain_id_rep = le32_to_cpu(resp->caps.plane_id);
	pr_err("virtio: resp VIRTIO_GPU_CMD_GET_PLANES_CAPS <%d:%d> (%s)\n",
			scanout_rep, plain_id_rep,
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	if (scanout != scanout_rep ||
			plane_id != plain_id_rep) {
		pr_err("virtio : something wrong with scanout and plane ID's\n");
		pr_err("virtio :scanout required %d replied %d\n",
				scanout, scanout_rep);
		pr_err("virtio : plane Id required %d replied %d\n",
				plane_id, plain_id_rep);

		rc = -EINVAL;
		goto error;
	}
	rc = virtio_get_planes_caps(kms,
			scanout_rep,
			plain_id_rep,
			resp);
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);

	return rc;
}

static int virtio_get_plane_properties(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t plane_id,
		struct virtio_gpu_resp_get_plane_properties *resp)
{
	struct virtio_kms_output *output = &kms->outputs[scanout];
	uint32_t i = 0;
	struct virtio_plane_caps *plane_caps = NULL;
	uint32_t plane = le32_to_cpu(resp->plane_id);

	for (i = 0; i < output->plane_cnt; i++) {
		//This might not needed if plane Id are in sequence.
		//TODO check if needed
		if(plane == output->plane_caps[i].plane_id) {
			plane_caps = &output->plane_caps[i];
			break;
		}
	}

	if (!plane_caps) {
		pr_err("virtio :Not valid plane caps ID->%d\n", plane_id);
		return -EINVAL;
	}
	plane_caps->zorder = le32_to_cpu(resp->zorder);
	return 0;
}

static int virtio_gpu_cmd_get_event (struct virtio_kms *kms,
		struct virtio_gpu_resp_event *resp)
{
	struct virtio_gpu_wait_events *cmd_p =
		kzalloc(sizeof(struct virtio_gpu_wait_events),
		GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_EVENTS];
	int rc = 0;

	if (!cmd_p || !resp) {
		pr_err("virtio :Memory allocation failed\n");
		goto error;
	}
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_WAIT_EVENTS);
	cmd_p->max_num_events = cpu_to_le32(1);

	pr_debug("virtio: cmd VIRTIO_GPU_CMD_WAIT_EVENTS (%d)\n",
			cmd_p->max_num_events);
	rc = virtio_hab_send_and_recv_timeout(hab_socket,
			&kms->channel[client_id].hyp_chl_lock[CHANNEL_EVENTS],
			cmd_p,
			sizeof(struct virtio_gpu_wait_events),
			resp,
			sizeof(struct virtio_gpu_resp_event));
	if (rc) {
		pr_err("virtio :send_and_recv failed \
				for VIRTIO_GPU_CMD_WAIT_EVENTS %d\n", rc);
		goto error;
	}
	pr_debug("virtio: cmd VIRTIO_GPU_CMD_WAIT_EVENTS received \n");
error:
	if (cmd_p)
		kfree(cmd_p);

	return rc;
}

int virtio_gpu_cmd_get_plane_properties(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t plane_id)

{
	struct virtio_gpu_get_plane_properties *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_get_plane_properties),
			GFP_KERNEL);
	struct virtio_gpu_resp_get_plane_properties *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_get_plane_properties),
				GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		pr_err("virtio :Memory allocation failed\n");
		goto error;
	}
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_PLANE_PROPERTIES);
	cmd_p->scanout_id = cpu_to_le32(scanout);
	cmd_p->plane_id = cpu_to_le32(plane_id);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_get_plane_properties),
			resp,
			sizeof(struct virtio_gpu_resp_get_plane_properties),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		pr_err("virtio :send_and_recv failed \
				for PLANE_PROPERTIES %d\n", rc);
		goto error;
	}

	pr_debug("virtio: resp VIRTIO_GPU_CMD_GET_PLANE_PROPERTIES \
			<%d:%d> (%s)\n",
			le32_to_cpu(resp->scanout_id),
			le32_to_cpu(resp->plane_id),
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	if (scanout != le32_to_cpu(resp->scanout_id) ||
			plane_id != le32_to_cpu(resp->plane_id)) {
		rc = -EINVAL;
		pr_err("virtio :something wrong with scanout and plane ID's\n");
		goto error;
	}

	rc = virtio_get_plane_properties(kms,
			le32_to_cpu(resp->scanout_id),
			le32_to_cpu(resp->plane_id),
			resp);
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);

	return rc;
}

int virtio_gpu_cmd_set_resource_info(struct virtio_kms *kms,
		uint32_t resource_id,
		uint32_t modifiers,
		uint32_t *offset,
		uint32_t *pitches,
		uint32_t ext_format)
{
	struct virtio_gpu_set_resource_info *cmd_p =
		kzalloc(sizeof(struct virtio_gpu_set_resource_info),
				GFP_KERNEL);
	struct virtio_gpu_ctrl_hdr *resp =
		kzalloc(sizeof(struct virtio_gpu_ctrl_hdr),
		       GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0,i;

	if (!cmd_p || !resp) {
		pr_err("virtio :Memory allocation failed\n");
		goto error;
	}

	pr_debug("virtio: cmd VIRTIO_GPU_CMD_SET_RESOURCE_INFO <%d> (%d %d)\n",
			resource_id, ext_format, modifiers);
	pr_debug("virtio: offsets -> %d %d %d %d \n",
			offset[0], offset[1], offset[2], offset[3]);
	pr_debug("virtio: pitches -> %d %d %d %d \n",
			pitches[0], pitches[1], pitches[2], pitches[3]);

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_SET_RESOURCE_INFO);
	cmd_p->resource_id = cpu_to_le32(resource_id);
	cmd_p->ext_format = cpu_to_le32(ext_format);
	cmd_p->modifiers = cpu_to_le32(modifiers);
	for (i = 0; i < 4; i++) {
		cmd_p->offsets[i] = cpu_to_le32(offset[i]);
		cmd_p->strides[i] = cpu_to_le32(pitches[i]);
	}

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_set_resource_info),
			NULL,
			sizeof(struct virtio_gpu_ctrl_hdr),
			NO_SPIN_LOCK_CHANNEL);
	if (rc)
		pr_err("virtio :send_and_recv failed for PLANE_PROPERTIES %d\n", rc);
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);
	return rc;
}

int virtio_gpu_cmd_set_plane(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t plane_id,
		uint32_t res_id)
{
	struct virtio_gpu_set_plane *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_set_plane),
					GFP_KERNEL);
	struct virtio_gpu_resp_set_plane *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_set_plane),
				GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		rc = -ENOMEM;
		goto error;
	}
	pr_debug("virtio: cmd VIRTIO_GPU_CMD_SET_PLANE <%d:%d> (%d)\n",
			scanout, plane_id, res_id);

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_SET_PLANE);
	cmd_p->scanout_id = cpu_to_le32(scanout);
	cmd_p->plane_id = cpu_to_le32(plane_id);
	cmd_p->resource_id = cpu_to_le32(res_id);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_set_plane),
			NULL,
			sizeof(struct virtio_gpu_resp_set_plane),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		pr_err("virtio :send_and_recv failed for PLANE_CAPS %d\n", rc);
		goto error;
	}
error:
	kfree(cmd_p);
	kfree(resp);

	return rc;
}

int virtio_gpu_cmd_plane_create(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t plane_id)
{
	struct virtio_gpu_create_plane *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_create_plane),
					GFP_KERNEL);
	struct virtio_gpu_resp_plane_create *resp =
			kzalloc(sizeof(struct virtio_gpu_resp_plane_create),
					GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;
	uint32_t error_code = 0;

	if (!cmd_p || !resp) {
		rc = -ENOMEM;
		goto error;
	}
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_PLANE_CREATE);
	cmd_p->scanout_id = cpu_to_le32(scanout);
	cmd_p->plane_id = cpu_to_le32(plane_id);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_create_plane),
			resp,
			sizeof(struct virtio_gpu_resp_plane_create),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		pr_err("virtio :send_and_recv failed for PLANE_CAPS %d\n", rc);
		goto error;
	}
	pr_debug("virtio: resp VIRTIO_GPU_CMD_PLANE_CREATE<%d> (%s)\n",
			le32_to_cpu(resp->scanout_id),
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	error_code = le32_to_cpu(resp->error_code);
	if (error_code)
		pr_err("virtio : scanout %dplane creation failed plane %d %d\n",
				le32_to_cpu(resp->scanout_id),
				plane_id,
				error_code);
error:
	kfree(cmd_p);
	kfree(resp);

	return rc;
}

int virtio_gpu_cmd_plane_destroy(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t plane_id)
{
	struct virtio_gpu_plane_destroy *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_plane_destroy),
					GFP_KERNEL);
	struct virtio_gpu_resp_plane_destroy *resp =
			kzalloc(sizeof(struct virtio_gpu_resp_plane_destroy),
					GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;
	uint32_t error_code = 0;

	if (!cmd_p || !resp) {
		rc = -ENOMEM;
		goto error;
	}
	pr_debug("virtio: cmd VIRTIO_GPU_CMD_PLANE_DESTROY <%d : %d>\n",
			scanout, plane_id);

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_PLANE_DESTROY);
	cmd_p->scanout_id = cpu_to_le32(scanout);
	cmd_p->plane_id = cpu_to_le32(plane_id);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_plane_destroy),
			resp,
			sizeof(struct virtio_gpu_resp_plane_destroy),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		pr_err("virtio :send_and_recv failed for PLANE_DESTROY %d\n", rc);
		goto error;
	}
	pr_debug("virtio: resp VIRTIO_GPU_CMD_PLANE_DESTROY<%d:%d> (%s)\n",
			le32_to_cpu(resp->scanout_id),
			le32_to_cpu(resp->plane_id),
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	error_code = le32_to_cpu(resp->error_code);
	if (error_code)
		pr_err("virtio :plane destroy failed %d\n", error_code);

error:
	kfree(cmd_p);
	kfree(resp);

	return rc;
}

int virtio_gpu_cmd_set_plane_properties(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t plane_id,
		struct plane_properties prop)
{
	struct virtio_gpu_set_plane_properties *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_set_plane_properties),
					GFP_KERNEL);
	struct virtio_gpu_resp_plane_properties *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_plane_properties),
			GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		rc = -ENOMEM;
		goto error;
	}
	pr_debug("virtio: cmd VIRTIO_GPU_CMD_SET_PLANE_PROPERTIES" \
			"<%d:%d> (0x%x)\n",
			scanout, plane_id, prop.mask);
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_SET_PLANE_PROPERTIES);
	cmd_p->scanout_id = cpu_to_le32(scanout);
	cmd_p->plane_id = cpu_to_le32(plane_id);
	cmd_p->mask = cpu_to_le32(prop.mask);
	cmd_p->z_order = cpu_to_le32(prop.z_order);
	cmd_p->global_alpha = cpu_to_le32(prop.global_alpha);
	cmd_p->blend_mode = cpu_to_le32(prop.blend_mode);
	cmd_p->src_rect.x = cpu_to_le32(prop.src_rect.x);
	cmd_p->src_rect.y = cpu_to_le32(prop.src_rect.y);
	cmd_p->src_rect.width = cpu_to_le32(prop.src_rect.width);
	cmd_p->src_rect.height = cpu_to_le32(prop.src_rect.height);
	cmd_p->dst_rect.x = cpu_to_le32(prop.dst_rect.x);
	cmd_p->dst_rect.y = cpu_to_le32(prop.dst_rect.y);
	cmd_p->dst_rect.width = cpu_to_le32(prop.dst_rect.width);
	cmd_p->dst_rect.height = cpu_to_le32(prop.dst_rect.height);
	cmd_p->color_space = cpu_to_le32(prop.color_space);
	cmd_p->colorimetry = cpu_to_le32(prop.colorimetry);
	cmd_p->color_range = cpu_to_le32(prop.color_range);
	cmd_p->hue = cpu_to_le32(prop.hue);
	cmd_p->saturation = cpu_to_le32(prop.saturation);
	cmd_p->contrast = cpu_to_le32(prop.contrast);
	cmd_p->brightness = cpu_to_le32(prop.brightness);
	cmd_p->rotation = cpu_to_le32(prop.rotation);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_set_plane_properties),
			NULL,
			sizeof(struct virtio_gpu_resp_plane_properties),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		pr_err("virtio :send_and_recv failed for SET_PLANE_PROPERTIES %d\n",
				rc);
		goto error;
	}
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);

	return rc;
}

int virtio_gpu_event_kthread(void *d)
{
	struct virtio_kms *kms = (struct virtio_kms *)d;

	struct virtio_gpu_resp_event *buff;
	uint32_t sz = sizeof(struct virtio_gpu_resp_event);
	int ret = 0;
	uint32_t client_id = kms->client_id;
	uint32_t num_events;
	uint32_t i = 0;
	int rc = 0;
	bool enable;
	struct mutex hyp_cbchl_lock;
	mutex_init(&hyp_cbchl_lock);

	buff = kzalloc(sizeof(struct virtio_gpu_resp_event), GFP_KERNEL);

	if (!buff) {
		rc = -ENOMEM;
		goto error;
	}

	while (!kms->stop) {

		memset(buff, 0x00, sizeof(struct virtio_gpu_resp_event));
		mutex_lock(&hyp_cbchl_lock);
		ret = virtio_gpu_cmd_get_event(kms, buff);
		if (ret) {
			pr_debug("virtio: %s mmid %d failed %d size %d\n",
					__func__,
					kms->mmid_event, ret, sz);
			if (ret == -ENODEV)
				break;
		} else {
			pr_debug("virtio: %s mmid %d ok size %d \n",
					__func__, kms->mmid_event, sz);
		}
		mutex_unlock(&hyp_cbchl_lock);
		for ( i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
			enable = le32_to_cpu(buff->scanout[i].enabled);
			if (!enable)
				continue;
			pr_debug(" virtio : scanout %d Event received Vsync %d commit %d HPD %d\n",
					i,
					le32_to_cpu(buff->scanout[i].vsync_count),
					le32_to_cpu(buff->scanout[i].commit_count),
					le32_to_cpu(buff->scanout[i].hpd_count));

			num_events = le32_to_cpu(buff->scanout[i].vsync_count);
			if (num_events)
				virtio_kms_event_handler(kms, i, num_events, VIRTIO_VSYNC);

			num_events = le32_to_cpu(buff->scanout[i].commit_count);
			if (num_events)
				virtio_kms_event_handler(kms, i, num_events, VIRTIO_COMMIT_COMPLETE);

			num_events = le32_to_cpu(buff->scanout[i].hpd_count);
			if (num_events)
				virtio_kms_event_handler(kms, i, num_events, VIRTIO_HPD);

		}
	}

error:
	kfree(buff);
	ret = habmm_socket_close(kms->channel[client_id].hab_socket[CHANNEL_EVENTS]);
	pr_debug("virtio: exit event kthread mmid %d\n", kms->mmid_event);
	return rc;
}
