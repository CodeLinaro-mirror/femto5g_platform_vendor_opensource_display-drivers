// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt)	"[drm:virtgpu-vq:%s:%d] " fmt, __func__, __LINE__
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/sort.h>
#include <drm/drm_atomic.h>
#include <linux/virtio_config.h>
//#include <soc/qcom/boot_stats.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_helper.h>

#include "msm_hyp_trace.h"
#include "msm_hyp_utils.h"

#include <linux/habmm.h>
#include "virtio_kms.h"
#include "virtio_ext.h"
#include "virtgpu_vq.h"

#define VIRTGPU_VQ_DBG(fmt, ...)		pr_debug(fmt, ##__VA_ARGS__)
#define VIRTGPU_VQ_INFO(fmt, ...)		pr_info(fmt, ##__VA_ARGS__)
#define VIRTGPU_VQ_WARN(fmt, ...)		pr_warn(fmt, ##__VA_ARGS__)
#define VIRTGPU_VQ_ERR(fmt, ...)		pr_err(fmt, ##__VA_ARGS__)
#define VIRTGPU_VQ_CMD_DBG(fmt, ...)	pr_debug(fmt, ##__VA_ARGS__)
#define VIRTGPU_VQ_RSP_DBG(fmt, ...)	pr_debug(fmt, ##__VA_ARGS__)

#define HAB_NO_TIMEOUT_VAL		-1
#define MAX_SEND_RECV_PACKET_RETRY	10

//#define UNIT_TEST

#ifdef UNIT_TEST
static int virtio_hab_send_and_recv_ext(		uint32_t hab_socket,
		struct channel_map *phab_channel,
		void *req, uint32_t req_size, void *resp,uint32_t resp_size,
		bool lock_flag);

int virtio_hab_send_and_recv_timeout_ext(		uint32_t hab_socket,
		struct mutex *phab_lock,
		void *req, uint32_t req_size, void *resp, uint32_t resp_size);

static char *virtio_cmd_type(uint32_t cmd);


#include "virtgpu_vq_test.c"
#endif

struct cmd_type {
	uint32_t cmd;
	char *cmd_name;
};

//TODO chck the usage of resp size
#ifdef UNIT_TEST
static int virtio_hab_send_and_recv_ext(
#else
static int virtio_hab_send_and_recv(
#endif
		uint32_t hab_socket,
		struct channel_map *phab_channel,
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
		spin_lock(&phab_channel->hyp_chl_spin_lock);
	else
		mutex_lock(&phab_channel->hyp_chl_lock[CHANNEL_CMD]);

retry_send_packet:
	rc = habmm_socket_send(hab_socket, req, req_size, (lock_flag == SPIN_LOCK_CHANNEL ?
				HABMM_SOCKET_SEND_FLAGS_NON_BLOCKING : 0x00));
	if (rc) {
		if ((rc == -EAGAIN) && (retry_times < MAX_SEND_RECV_PACKET_RETRY)) {
			retry_times++;
			VIRTGPU_VQ_DBG("send packet retry %d\n", retry_times);
			goto retry_send_packet;
		}
		rc = -1;
		VIRTGPU_VQ_ERR("virtio: habmm_socket_send failed <%d>\n", rc);
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
				VIRTGPU_VQ_ERR("channel broken - no device");
			else if (-EINTR == rc) {
				/*
				 * system is closed or suspend a interrupted
				 * system call is happening on hab channel.
				 * We should try it again
				 */
				VIRTGPU_VQ_ERR("habmm_socket_recv - \
					interrupted system call - retry");
			}
		}
	} while ((time_before(jiffies, delay)) &&
			(-EAGAIN == rc) && (size == 0));

	if (rc) {
		if ((rc == -EAGAIN) && (retry_times < MAX_SEND_RECV_PACKET_RETRY))
		{
			retry_times++;
			VIRTGPU_VQ_RSP_DBG("recv packet retry %d", retry_times);
			goto retry_recv_packet;
		}
		rc = -1;
		goto end;
	}
	if (resp_size != size)
		VIRTGPU_VQ_ERR("something wrong in the order of req and resp\n");
end:

	if (SPIN_LOCK_CHANNEL == lock_flag)
		spin_unlock(&phab_channel->hyp_chl_spin_lock);
	else
		mutex_unlock(&phab_channel->hyp_chl_lock[CHANNEL_CMD]);
	return rc;
}

#ifdef UNIT_TEST
int virtio_hab_send_and_recv_timeout_ext(
#else
int virtio_hab_send_and_recv_timeout(
#endif
		uint32_t hab_socket,
		struct mutex *phab_lock,
		void *req,
		uint32_t req_size,
		void *resp,
		uint32_t resp_size)
{
	int rc = 0;
	uint32_t flags = HABMM_SOCKET_RECV_FLAGS_TIMEOUT;
	uint32_t size = resp_size;
	uint32_t max_retries = 10;
	mutex_lock(phab_lock);
retry:
	rc = habmm_socket_send(hab_socket, req, req_size, 0x00);
	if (rc) {
		VIRTGPU_VQ_ERR("habmm_socket_send failed <%d>\n", rc);
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
			VIRTGPU_VQ_RSP_DBG("recv timout retry %d\n", max_retries);
			goto retry;
		}
		else if (rc && !max_retries) {
			size = resp_size;
			VIRTGPU_VQ_RSP_DBG("retries done waiting for reply\n");
			rc = habmm_socket_recv(hab_socket,
				resp,
				&size,
				(uint32_t)-1, 0);
			if (rc)
				VIRTGPU_VQ_ERR("socket_recv failed <%d>\n",rc);
		}
end:
	mutex_unlock(phab_lock);
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
		{VIRTIO_GPU_CMD_SET_RESOURCE_INFO,
			"VIRTIO_GPU_CMD_SET_RESOURCE_INFO"},
		{VIRTIO_GPU_CMD_WAIT_FOR_VSYNC,
			"VIRTIO_GPU_CMD_WAIT_FOR_VSYNC"},
		{VIRTIO_GPU_CMD_SET_PLANE_HDR,
			"VIRTIO_GPU_CMD_SET_PLANE_HDR"},
		{VIRTIO_GPU_CMD_SET_PIC_ADJUST,
			"VIRTIO_GPU_CMD_SET_PIC_ADJUST"},
		{VIRTIO_GPU_CMD_GET_DEVICE_INFO,
			"VIRTIO_GPU_CMD_GET_DEVICE_INFO"},
		{VIRTIO_GPU_CMD_GET_DEVICE_HW_ATTRIBUTES,
			"VIRTIO_GPU_CMD_GET_DEVICE_HW_ATTRIBUTES"},
		{VIRTIO_GPU_CMD_GET_SCANOUT_HW_ATTRIBUTES,
			"VIRTIO_GPU_CMD_GET_SCANOUT_HW_ATTRIBUTES"},
		{VIRTIO_GPU_CMD_GET_PLANE_HW_ATTRIBUTES,
			"VIRTIO_GPU_CMD_GET_PLANE_HW_ATTRIBUTES"},

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
		{VIRTIO_GPU_RESP_OK_SET_PIC_ADJUST,
			"VIRTIO_GPU_RESP_OK_SET_PIC_ADJUST"},
		{VIRTIO_GPU_RESP_OK_DEVICE_HW_ATTRIBUTES,
			"VIRTIO_GPU_RESP_OK_DEVICE_HW_ATTRIBUTES"},
		{VIRTIO_GPU_RESP_OK_SCANOUT_HW_ATTRIBUTES,
			"VIRTIO_GPU_RESP_OK_SCANOUT_HW_ATTRIBUTES"},
		{VIRTIO_GPU_RESP_OK_PLANE_HW_ATTRIBUTES,
			"VIRTIO_GPU_RESP_OK_PLANE_HW_ATTRIBUTES"},
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
		VIRTGPU_VQ_ERR("memory alloc failed req %p resp %p\n", req, resp);
		rc = -ENOMEM;
		goto error;
	}
	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_SET_PIC_ADJUST <%d> (%d %d %d %d)\n",
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
		VIRTGPU_VQ_ERR("virtio_hab_send_and_recv failed\
				for SET_SCANOUT_PIC_ADJUST %d\n", rc);
		goto error;
	}
	error_code = le32_to_cpu(resp->error_code);
	if(error_code) {
		VIRTGPU_VQ_ERR("SET_SCANOUT_PIC_ADJUST failed scanout %d error %d\n",
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
		VIRTGPU_VQ_ERR("memory alloc failed req %p resp %p\n", req, resp);
		rc = -ENOMEM;
		goto error;
	}

	VIRTGPU_VQ_CMD_DBG("cmd set_scanout_properties scanout <%d> \
			[%X, %d, %d, %d, %d, %d,%d]\n",
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
			resp,
			sizeof(struct virtio_gpu_resp_scanout_properties),
			NO_SPIN_LOCK_CHANNEL);
	if(rc) {
		VIRTGPU_VQ_ERR("virtio_hab_send_and_recv failed\
				for SET_SCANOUT_PROPERTIES %d\n", rc);
		goto error;
	}
	error_code = le32_to_cpu(resp->error_code);
	if(error_code) {
		VIRTGPU_VQ_ERR("SET_SCANOUT_PROPERTIES failed scanout %d error %d\n",
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
		VIRTGPU_VQ_ERR("memory alloc failed req %p resp %p\n", req, resp);
		rc = -ENOMEM;
		goto error;
	}

	VIRTGPU_VQ_CMD_DBG("VIRTIO_GPU_CMD_SET_SCANOUT scanout <%d> \
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
		VIRTGPU_VQ_ERR("send_and_recv failed for SET_SCANOUT rc=%d\n", rc);
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
		VIRTGPU_VQ_ERR("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}

	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_RESOURCE_CREATE_2D "\
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
		VIRTGPU_VQ_ERR("send_and_recv failed for RESOURCE_CREATE_2D rc=%d\n", rc);
		goto error;
	}
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
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
		VIRTGPU_VQ_ERR("memory alloc failed req %p resp %p\n", cmd_p, resp);
		rc = -ENOMEM;
		goto error;
	}
	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING_EXT"
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
		VIRTGPU_VQ_ERR("send_and_recv failed for RESOURCE_ATTACH_BACKING %d\n", rc);
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
		VIRTGPU_VQ_ERR("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}
	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING <%d>\n",
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
		VIRTGPU_VQ_ERR("send_and_recv failed for RESOURCE_DETACH_BACKING rc=%d\n", rc);
		goto error;
	}

	VIRTGPU_VQ_RSP_DBG("resp VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING (%s)\n",
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
		VIRTGPU_VQ_ERR("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}
	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_RESOURCE_UNREF <%d>\n",
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
		VIRTGPU_VQ_ERR("send_and_recv failed for RESOURCE_UNREF rc=%d\n", rc);
		goto error;
	}

	VIRTGPU_VQ_RSP_DBG("resp VIRTIO_GPU_CMD_RESOURCE_UNREF (%s)\n",
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
		VIRTGPU_VQ_ERR("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}
	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_PLANE_FLUSH <%d> (%d, %d)\n",
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
		VIRTGPU_VQ_ERR("send_and_recv failed for PLANE_FLUSH rc=%d\n", rc);
		goto error;
	}

	if (sync){
		VIRTGPU_VQ_ERR("resp  VIRTIO_GPU_CMD_PLANE_FLUSH <%d> (%s)\n",
				le32_to_cpu(resp->scanout_id),
				virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

		error = le32_to_cpu(resp->error_code);
		if(error)
			VIRTGPU_VQ_ERR("plane flush failed for scanout %d plane error%d\n",
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
		VIRTGPU_VQ_ERR("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}
	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_SCANOUT_FLUSH <%d> (%d)\n",
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
		VIRTGPU_VQ_ERR("send_and_recv failed for SCANOUT_FLUSH rc=%d\n", rc);
		goto error;
	}
/*
	if (!sync) {
		VIRTGPU_VQ_CMD_DBG("resp VIRTIO_GPU_CMD_SCANOUT_FLUSH <%d>(%s)\n",
			le32_to_cpu(resp->scanout_id),
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

		error_code = le32_to_cpu(resp->error_code);
		if(error_code)
			VIRTGPU_VQ_ERR("scanout flush failed for %d error%d\n",
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
		VIRTGPU_VQ_ERR("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}
	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_EVENT_CONTROL <%d> (%d %d)\n",
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
		VIRTGPU_VQ_ERR("send_and_recv failed for EVENT_CONTROL rc=%d\n", rc);
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
	void *new_edid;

	if (!kms || !buf || scanout >= kms->num_scanouts || len == 0)
		return -EINVAL;

	new_edid = vmemdup(buf, len);
	if (!new_edid)
		return -ENOMEM;

	vfree(kms->outputs[scanout].edid);
	kms->outputs[scanout].edid = new_edid;

	return 0;
}

int virtio_gpu_cmd_get_edid(struct virtio_kms *kms,
		uint32_t scanout)
{
	struct virtio_gpu_cmd_get_edid *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_cmd_get_edid),
				GFP_KERNEL);
	struct virtio_gpu_resp_edid *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_edid),
				GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p) {
		VIRTGPU_VQ_ERR("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}
	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_GET_EDID <%d>\n", scanout);
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
		VIRTGPU_VQ_ERR("send_and_recv failed for EVENT_CONTROL rc=%d\n", rc);
		goto error;
	}

	VIRTGPU_VQ_RSP_DBG("resp VIRTIO_GPU_CMD_GET_EDID (%s)\n",
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	rc = virtio_get_edid_block(kms,
			scanout,
			resp->edid,
			le32_to_cpu(resp->size));
	if (rc)
		VIRTGPU_VQ_ERR("virtio_get_edid_block failed, rc=%d\n", rc);

error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
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
			output->index = resp->scanout_id;
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
			VIRTGPU_VQ_RSP_DBG("scanout info <%d> <mode %d> (%dx%d+%d+%d@%d, %d)\n",
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

	VIRTGPU_VQ_RSP_DBG("device_info:\n");
	VIRTGPU_VQ_RSP_DBG("qseed_type: %d\n", kms->device_info.qseed_type);
	VIRTGPU_VQ_RSP_DBG("max_mdp_clk: %d\n", kms->device_info.max_mdp_clk);
	VIRTGPU_VQ_RSP_DBG("has_src_split: %d\n", kms->device_info.has_src_split);
	VIRTGPU_VQ_RSP_DBG("device_version: %d\n", kms->device_info.device_version);
}

void virio_get_scanout_numbers(struct virtio_kms *kms,
		struct virtio_gpu_resp_display_info *resp)
{
	int i;
	for (i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
		//intentionally not storing the nodes;
		if (resp->pmodes[i].enabled) {
			VIRTGPU_VQ_RSP_DBG("output %d: %dx%d+%d+%d\n", i,
				le32_to_cpu(resp->pmodes[i].r.width),
				le32_to_cpu(resp->pmodes[i].r.height),
				le32_to_cpu(resp->pmodes[i].r.x),
				le32_to_cpu(resp->pmodes[i].r.y));
			kms->num_scanouts++;
		} else {
			VIRTGPU_VQ_RSP_DBG("output %d: disabled", i);
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
		VIRTGPU_VQ_ERR("memory alloc failed\n");
		rc = -ENOMEM;
		goto error;
	}
	cmd_p->type = cpu_to_le32(VIRTIO_GPU_CMD_GET_DISPLAY_INFO);

	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_GET_DISPLAY_INFO\n");

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_ctrl_hdr),
			resp,
			sizeof(struct virtio_gpu_resp_display_info),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		VIRTGPU_VQ_ERR("virtio send_and_recv failed for DISPLAY_INFO %d\n",
				rc);
		goto error;
	}
	VIRTGPU_VQ_RSP_DBG("resp VIRTIO_GPU_CMD_GET_DISPLAY_INFO (%s)\n",
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
		VIRTGPU_VQ_ERR("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}
	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_GET_DISPLAY_INFO_EXT <%d>\n",
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
	if (rc) {
		VIRTGPU_VQ_ERR("send_and_recv failed for DISPLAY_INFO_EXT %d\n", rc);
		goto error;
	}
	VIRTGPU_VQ_RSP_DBG("resp VIRTIO_GPU_CMD_GET_DISPLAY_INFO_EXT <%d> (%s)\n",
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
		VIRTGPU_VQ_ERR("memory alloc failed\n");
		rc = -ENOMEM;
		goto error;
	}

	cmd_p->type = cpu_to_le32(VIRTIO_GPU_CMD_GET_DEVICE_INFO);

	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_GET_DEVICE_INFO\n");

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_ctrl_hdr),
			resp,
			sizeof(struct virtio_gpu_resp_device_info),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		VIRTGPU_VQ_ERR("virtio send_and_recv failed for DEVICE_INFO %d\n", rc);
		goto error;
	}
	VIRTGPU_VQ_RSP_DBG("resp VIRTIO_GPU_CMD_GET_DEVICE_INFO (%s)\n",
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
	VIRTGPU_VQ_RSP_DBG("scanout %d attr <%d %d (%dX%d)  org %d>\n",
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
		VIRTGPU_VQ_ERR("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}

	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_GET_SCANOUT_ATTRIBUTE <%d>\n",
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
	if (rc) {
		VIRTGPU_VQ_ERR("send_and_recv failed for SCANOUT_ATTRIBUTE %d\n", rc);
		goto error;
	}
	VIRTGPU_VQ_RSP_DBG("resp  VIRTIO_GPU_CMD_GET_SCANOUT_ATTRIBUTE<%d>(%s)\n",
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
		VIRTGPU_VQ_ERR("To many planes %d\n", output->plane_cnt);
		output->plane_cnt = VIRTIO_GPU_MAX_PLANES;
	}
	VIRTGPU_VQ_RSP_DBG("plane scanout <%d>\n", scanout);
	for(i = 0; i < output->plane_cnt; i++) {
		output->plane_caps[i].plane_id =
			le32_to_cpu(resp->plane_ids[i]);
		VIRTGPU_VQ_RSP_DBG("%d -> %d\n", i, output->plane_caps[i].plane_id);
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

	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_GET_SCANOUT_PLANES<%d>\n",
			le32_to_cpu(resp->scanout_id));
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
		VIRTGPU_VQ_ERR("virtio_hab_send_and_recv failed \
				for SCANOUT_PLANES %d\n", rc);
		goto error;
	}

	if (scanout != le32_to_cpu(resp->scanout_id)) {
		VIRTGPU_VQ_ERR("Somthing wrong with scanout ID\n");
		goto error;
	}

	VIRTGPU_VQ_RSP_DBG("resp VIRTIO_GPU_CMD_GET_SCANOUT_PLANES<%d> (%s)\n",
			le32_to_cpu(resp->scanout_id),
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	virtio_get_scanout_planes(kms, scanout, resp);

error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
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
		VIRTGPU_VQ_ERR("Not valid plane caps ID->%d\n", plane_id);
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
	VIRTGPU_VQ_RSP_DBG("plane caps <%d:%d> (%d, %d, %d, %d, %d\n",
			scanout,
			plane_id,
			plane_caps->plane_type,
			plane_caps->max_width,
			plane_caps->max_height,
			plane_caps->num_formats,
			plane_caps->pair_plane_id);

	for (i = 0; i < plane_caps->num_formats; i++) {
		VIRTGPU_VQ_RSP_DBG("%d\n", plane_caps->formats[i]);
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
		VIRTGPU_VQ_ERR("Memory allocation failed\n");
		goto error;
	}

	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_GET_PLANES_CAPS <%d> (%d)\n",
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
		VIRTGPU_VQ_ERR("virtio_hab_send_and_recv failed \
				for PLANE_CAPS %d\n", rc);
		goto error;
	}

	scanout_rep = le32_to_cpu(resp->caps.scanout_id);
	plain_id_rep = le32_to_cpu(resp->caps.plane_id);
	VIRTGPU_VQ_RSP_DBG("resp VIRTIO_GPU_CMD_GET_PLANES_CAPS <%d:%d> (%s)\n",
			scanout_rep, plain_id_rep,
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	if (scanout != scanout_rep ||
			plane_id != plain_id_rep) {
		VIRTGPU_VQ_ERR("something wrong with scanout and plane ID's\n");
		VIRTGPU_VQ_ERR("scanout required %d replied %d\n",
				scanout, scanout_rep);
		VIRTGPU_VQ_ERR("plane Id required %d replied %d\n",
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
		VIRTGPU_VQ_ERR("Not valid plane caps ID->%d\n", plane_id);
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
		VIRTGPU_VQ_ERR("Memory allocation failed\n");
		goto error;
	}
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_WAIT_EVENTS);
	cmd_p->max_num_events = cpu_to_le32(1);

	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_WAIT_EVENTS (%d)\n",
			cmd_p->max_num_events);
	rc = virtio_hab_send_and_recv_timeout(hab_socket,
			&kms->channel[client_id].hyp_chl_lock[CHANNEL_EVENTS],
			cmd_p,
			sizeof(struct virtio_gpu_wait_events),
			resp,
			sizeof(struct virtio_gpu_resp_event));
	if (rc) {
		VIRTGPU_VQ_ERR("send_and_recv failed \
				for VIRTIO_GPU_CMD_WAIT_EVENTS %d\n", rc);
		goto error;
	}
	VIRTGPU_VQ_RSP_DBG("cmd VIRTIO_GPU_CMD_WAIT_EVENTS received \n");
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
		VIRTGPU_VQ_ERR("Memory allocation failed\n");
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
		VIRTGPU_VQ_ERR("send_and_recv failed \
				for PLANE_PROPERTIES %d\n", rc);
		goto error;
	}

	VIRTGPU_VQ_CMD_DBG("resp VIRTIO_GPU_CMD_GET_PLANE_PROPERTIES \
			<%d:%d> (%s)\n",
			le32_to_cpu(resp->scanout_id),
			le32_to_cpu(resp->plane_id),
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	if (scanout != le32_to_cpu(resp->scanout_id) ||
			plane_id != le32_to_cpu(resp->plane_id)) {
		rc = -EINVAL;
		VIRTGPU_VQ_ERR("something wrong with scanout and plane ID's\n");
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
		VIRTGPU_VQ_ERR("Memory allocation failed\n");
		goto error;
	}

	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_SET_RESOURCE_INFO <%d> (%d %d)\n",
			resource_id, ext_format, modifiers);
	VIRTGPU_VQ_CMD_DBG("offsets -> %d %d %d %d \n",
			offset[0], offset[1], offset[2], offset[3]);
	VIRTGPU_VQ_CMD_DBG("pitches -> %d %d %d %d \n",
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
	if (rc) {
		VIRTGPU_VQ_ERR("send_and_recv failed for PLANE_PROPERTIES %d\n", rc);
		goto error;
	}

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

	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_SET_PLANE <%d:%d> (%d)\n",
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
	VIRTGPU_VQ_RSP_DBG("cmd VIRTIO_GPU_CMD_SET_PLANE <%d:%d> (%d) done\n",
			scanout, plane_id, res_id);
	if (rc) {
		VIRTGPU_VQ_ERR("send_and_recv failed for SET_PLANE %d\n", rc);
		goto error;
	}
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
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

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_PLANE_CREATE);
	cmd_p->scanout_id = cpu_to_le32(scanout);
	cmd_p->plane_id = cpu_to_le32(plane_id);

	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_PLANE_CREATE scanout %d plane_id %d\n", scanout, plane_id);
	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_create_plane),
			resp,
			sizeof(struct virtio_gpu_resp_plane_create),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		VIRTGPU_VQ_ERR("send_and_recv failed for PLANE_CREATE %d\n", rc);
		goto error;
	}
	VIRTGPU_VQ_RSP_DBG("resp VIRTIO_GPU_CMD_PLANE_CREATE<%d> (%s)\n",
			le32_to_cpu(resp->scanout_id),
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	error_code = le32_to_cpu(resp->error_code);
	if (error_code)
		VIRTGPU_VQ_ERR("scanout %dplane creation failed plane %d %d\n",
				le32_to_cpu(resp->scanout_id),
				plane_id,
				error_code);
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
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

	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_PLANE_DESTROY <%d : %d>\n",
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
		VIRTGPU_VQ_ERR("send_and_recv failed for PLANE_DESTROY %d\n", rc);
		goto error;
	}
	VIRTGPU_VQ_RSP_DBG("resp VIRTIO_GPU_CMD_PLANE_DESTROY<%d:%d> (%s)\n",
			le32_to_cpu(resp->scanout_id),
			le32_to_cpu(resp->plane_id),
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	error_code = le32_to_cpu(resp->error_code);
	if (error_code)
		VIRTGPU_VQ_ERR("plane destroy failed %d\n", error_code);

error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
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

	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_SET_PLANE_PROPERTIES" \
			"<%d:%d> (0x%llx)\n",
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

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_set_plane_properties),
			NULL,
			sizeof(struct virtio_gpu_resp_plane_properties),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		VIRTGPU_VQ_ERR("send_and_recv failed for SET_PLANE_PROPERTIES %d\n",
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

static void virtio_get_device_hw_attributes(
		struct virtio_kms *kms,
		struct virtio_gpu_resp_device_hw_attributes *resp)
{
	int i;

	VIRTGPU_VQ_RSP_DBG("virtio_get_device_hw_attributes\n");
	kms->device_info.num_virq = resp->num_vriq;
	for (i = 0; i < resp->num_vriq; i++) {
		kms->device_info.virq_shmem[i] = resp->virq_shmem[i];
		VIRTGPU_VQ_RSP_DBG("virq[%d]: %lld\n", kms->device_info.num_virq, resp->virq_shmem[i]);
	}
}

int virtio_gpu_cmd_get_device_hw_attributes(struct virtio_kms *kms)
{
	struct virtio_gpu_get_device_hw_attributes *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_get_device_hw_attributes),
			GFP_KERNEL);
	struct virtio_gpu_resp_device_hw_attributes *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_device_hw_attributes),
				GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		VIRTGPU_VQ_ERR("Memory allocation failed\n");
		goto error;
	}
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_DEVICE_HW_ATTRIBUTES);

	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_GET_DEVICE_HW_ATTRIBUTES\n");
	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_get_device_hw_attributes),
			resp,
			sizeof(struct virtio_gpu_resp_device_hw_attributes),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		VIRTGPU_VQ_ERR("send_and_recv failed \
				for DEVICE_HW_ATTRIBUTES %d\n", rc);
		goto error;
	}

	VIRTGPU_VQ_RSP_DBG("resp VIRTIO_GPU_CMD_GET_DEVICE_HW_ATTRIBUTES \
			(%s)\n",
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	virtio_get_device_hw_attributes(kms, resp);
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);

	return rc;
}

#define PARSE_VALUE(option, variable)	\
	if (sscanf(line, #option "=%u", &variable) == 1) \
		continue;
#define PARSE_MASK(option, variable)	\
	if (sscanf(line, #option "=%x", &variable) == 1) \
		continue;
#define PARSE_OWNER(option, variable)	\
	if (sscanf(line, #option "=%s", value) == 1) { \
		if (!strcasecmp(value, "true")) \
			variable = true; \
		else if (!strcasecmp(value, "false")) \
			variable = false; \
		else \
			VIRTGPU_VQ_RSP_DBG("Unknown ownership str [%s]\n", value); \
		continue; \
	}
#define DUMP_PARSED_VALUE(option)	VIRTGPU_VQ_RSP_DBG("\t" #option " = %X\n", assign->option)

struct topology_name_list {
	enum sde_rm_topology_name name;
	char *str;
} topology_names[] = {
	{SDE_RM_TOPOLOGY_NONE,					"none"},
	{SDE_RM_TOPOLOGY_SINGLEPIPE,			"singlepipe"},
	{SDE_RM_TOPOLOGY_SINGLEPIPE_DSC,		"singlepipe_dsc"},
	{SDE_RM_TOPOLOGY_SINGLEPIPE_VDC,		"singlepipe_vdc"},
	{SDE_RM_TOPOLOGY_DUALPIPE,				"dualpipe"},
	{SDE_RM_TOPOLOGY_DUALPIPE_DSC,			"dualpipe_dsc"},
	{SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE,		"dualpipemerge"},
	{SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE_DSC,	"dualpipemerge_dsc"},
	{SDE_RM_TOPOLOGY_DUALPIPE_3DMERGE_VDC,	"dualpipemerge_vdc"},
	{SDE_RM_TOPOLOGY_DUALPIPE_DSCMERGE,		"dualpipe_dscmerge"},
	{SDE_RM_TOPOLOGY_PPSPLIT,				"ppsplit"},
	{SDE_RM_TOPOLOGY_QUADPIPE_3DMERGE,		"quadpipemerge"},
	{SDE_RM_TOPOLOGY_QUADPIPE_3DMERGE_DSC,	"quadpipe_3dmerge_dsc"},
	{SDE_RM_TOPOLOGY_QUADPIPE_DSCMERGE,		"quadpipe_dscmerge"},
	{SDE_RM_TOPOLOGY_QUADPIPE_DSC4HSMERGE,	"quadpipe_dsc4hsmerge"},
	{SDE_RM_TOPOLOGY_DUALPIPE_LOOPBACK,     "dualpipe_loopback"},
	{SDE_RM_TOPOLOGY_QUADPIPE_LOOPBACK,     "quadpipe_loopback"},
	{SDE_RM_TOPOLOGY_MAX,     				NULL},
};

static void virtio_get_scanout_hw_attribute(struct virtio_kms *kms,
		uint32_t scanout,
		struct virtio_gpu_resp_scanout_hw_attributes *resp)
{
#define MAX_LINE_LENGTH 128
	struct virtio_kms_output *output = &kms->outputs[scanout];
	char line[MAX_LINE_LENGTH], value[MAX_LINE_LENGTH];
	const char *blob = resp->blob;
	const char *ptr = blob;
	struct display_hw_assigment *assign = &output->hw_assign;
	int len;
	struct topology_name_list *top;

	memset(assign, 0, sizeof(output->hw_assign));

	VIRTGPU_VQ_RSP_DBG("virtio_get_scanout_hw_attribute  blob:\n[%s]\n", blob);

	while ( (blob != NULL) &&
			((ptr = strchr(blob, ';')) != NULL ||
			(ptr = strchr(blob, '\n')) != NULL ||
			*blob != 0) ) {
		if (*blob == ' ') {
			blob++;
			continue;
		}
		if (ptr) {
			len = ptr - blob + 1;
			if (len > 0 && len < MAX_LINE_LENGTH) {
				strscpy(line, blob, len);
				line[len] = '\0';
				ptr++; // Move to the next line or section
				blob = ptr;
			}
		} else {
			// Last line
			strscpy(line, blob, MAX_LINE_LENGTH);
			blob = NULL;
		}

		PARSE_VALUE(dpu_id, assign->dpu_id)
		PARSE_VALUE(ctl_id, assign->ctl_id)
		PARSE_OWNER(ctl_owner, assign->ctl_owner)
		PARSE_VALUE(vq_id, assign->vq_id)

		PARSE_MASK(lm_mask, assign->lm_mask)
		PARSE_OWNER(lm_owner, assign->lm_owner)
		PARSE_VALUE(lm_stage_start, assign->lm_stage_start)
		PARSE_VALUE(lm_stages, assign->lm_stages)

		PARSE_MASK(roi_crc_engine_mask, assign->roi_crc_engine_mask)
		PARSE_OWNER(roi_crc_owner, assign->roi_crc_owner)
		PARSE_MASK(roi_bypass_engine_mask, assign->roi_bypass_engine_mask)
		PARSE_OWNER(roi_bypass_owner, assign->roi_bypass_owner)

		PARSE_MASK(ltm_mask, assign->ltm_mask)
		PARSE_OWNER(ltm_owner, assign->ltm_owner)

		PARSE_MASK(dspp_mask, assign->dspp_mask)
		PARSE_OWNER(dspp_owner, assign->dspp_owner)

		PARSE_MASK(ds_mask, assign->ds_mask)
		PARSE_OWNER(ds_owner, assign->ds_owner)

		PARSE_MASK(merge3d_mask, assign->merge3d_mask)
		PARSE_OWNER(merge3d_owner, assign->merge3d_owner)

		PARSE_MASK(dsc_mask, assign->dsc_mask)
		PARSE_MASK(dsc_merge_mask, assign->dsc_merge_mask)
		PARSE_MASK(dsc_4hs_merge_mask, assign->dsc_4hs_merge_mask)
		PARSE_OWNER(dsc_owner, assign->dsc_owner)

		PARSE_MASK(pingpong_mask, assign->pingpong_mask)
		PARSE_OWNER(pingpong_owner, assign->pingpong_owner)

		PARSE_MASK(intf_mask, assign->intf_mask)
		PARSE_OWNER(intf_owner, assign->intf_owner)

		PARSE_MASK(wb_mask, assign->wb_mask)
		PARSE_OWNER(wb_owner, assign->wb_owner)

		PARSE_MASK(cwb_mask, assign->cwb_mask)
		PARSE_OWNER(cwb_owner, assign->cwb_owner)

		PARSE_MASK(vdc_mask, assign->vdc_mask)
		PARSE_OWNER(vdc_owner, assign->vdc_owner)

		PARSE_MASK(cdm_mask, assign->cdm_mask)
		PARSE_OWNER(cdm_owner, assign->cdm_owner)

		PARSE_MASK(dnsc_blur_mask, assign->dnsc_blur_mask)
		PARSE_OWNER(dnsc_blur_owner, assign->dnsc_blur_owner)

		if (strncmp(line, "topology=", 9) == 0) {
			assign->top_name = SDE_RM_TOPOLOGY_MAX;
			top = topology_names;
			while (top->str) {
				if (!strcasecmp(line+9, top->str)) {
					assign->top_name = top->name;
					break;
				}
				top++;
			}
			if (assign->top_name == SDE_RM_TOPOLOGY_MAX) {
				VIRTGPU_VQ_RSP_DBG("Unknown topology, fallback to single pipe\n");
				assign->top_name = SDE_RM_TOPOLOGY_SINGLEPIPE;
			}
			continue;
		}

		VIRTGPU_VQ_RSP_DBG("Ignore unrecognized option [%s]\n", line);
	}

	assign->dpu_id += DPU_0;
	assign->ctl_id += CTL_0;
	assign->vq_id += REG_DMA_VQ_0;

	DUMP_PARSED_VALUE(dpu_id);
	DUMP_PARSED_VALUE(ctl_owner);
	DUMP_PARSED_VALUE(ctl_id);
	DUMP_PARSED_VALUE(vq_id);

	DUMP_PARSED_VALUE(lm_owner);
	DUMP_PARSED_VALUE(lm_mask);
	DUMP_PARSED_VALUE(lm_stage_start);
	DUMP_PARSED_VALUE(lm_stages);

	DUMP_PARSED_VALUE(roi_crc_owner);
	DUMP_PARSED_VALUE(roi_crc_engine_mask);
	DUMP_PARSED_VALUE(roi_bypass_owner);
	DUMP_PARSED_VALUE(roi_bypass_engine_mask);

	DUMP_PARSED_VALUE(ltm_owner);
	DUMP_PARSED_VALUE(ltm_mask);

	DUMP_PARSED_VALUE(dspp_owner);
	DUMP_PARSED_VALUE(dspp_mask);

	DUMP_PARSED_VALUE(ds_owner);
	DUMP_PARSED_VALUE(ds_mask);

	DUMP_PARSED_VALUE(merge3d_owner);
	DUMP_PARSED_VALUE(merge3d_mask);

	DUMP_PARSED_VALUE(dsc_owner);
	DUMP_PARSED_VALUE(dsc_mask);
	DUMP_PARSED_VALUE(dsc_merge_mask);
	DUMP_PARSED_VALUE(dsc_4hs_merge_mask);

	DUMP_PARSED_VALUE(pingpong_owner);
	DUMP_PARSED_VALUE(pingpong_mask);

	DUMP_PARSED_VALUE(intf_owner);
	DUMP_PARSED_VALUE(intf_mask);

	DUMP_PARSED_VALUE(wb_owner);
	DUMP_PARSED_VALUE(wb_mask);

	DUMP_PARSED_VALUE(cwb_owner);
	DUMP_PARSED_VALUE(cwb_mask);

	DUMP_PARSED_VALUE(vdc_owner);
	DUMP_PARSED_VALUE(vdc_mask);

	DUMP_PARSED_VALUE(cdm_owner);
	DUMP_PARSED_VALUE(cdm_mask);

	DUMP_PARSED_VALUE(dnsc_blur_owner);
	DUMP_PARSED_VALUE(dnsc_blur_mask);

	DUMP_PARSED_VALUE(top_name);
}

int virtio_gpu_cmd_get_scanout_hw_attributes(struct virtio_kms *kms,
		uint32_t scanout)
{
	struct virtio_gpu_get_scanout_hw_attributes *cmd_p =
			kzalloc(sizeof(
				struct virtio_gpu_get_scanout_hw_attributes),
			GFP_KERNEL);
	struct virtio_gpu_resp_scanout_hw_attributes *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_scanout_hw_attributes),
				GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		VIRTGPU_VQ_ERR("memory alloc failed \n");
		rc = -ENOMEM;
		goto error;
	}

	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_GET_SCANOUT_HW_ATTRIBUTES <%d>\n",
			scanout);
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_SCANOUT_HW_ATTRIBUTES);
	cmd_p->scanout_id = cpu_to_le32(scanout);
	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_get_scanout_hw_attributes),
			resp,
			sizeof(struct virtio_gpu_resp_scanout_hw_attributes),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		VIRTGPU_VQ_ERR("send_and_recv failed for SCANOUT_HW_ATTRIBUTE %d\n", rc);
		goto error;
	}
	VIRTGPU_VQ_RSP_DBG("resp  VIRTIO_GPU_CMD_GET_SCANOUT_HW_ATTRIBUTE<%d>(%s)\n",
			le32_to_cpu(resp->scanout_id),
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	virtio_get_scanout_hw_attribute(kms, scanout, resp);
error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);

	return rc;
}

static int virtio_get_plane_hw_attributes(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t plane_id,
		struct virtio_gpu_resp_plane_hw_attributes *resp)
{
	struct virtio_kms_output *output = &kms->outputs[scanout];
	uint32_t i = 0;
	struct virtio_plane_caps *plane_caps = NULL;
	uint32_t plane = le32_to_cpu(resp->plane_id);
	uint32_t sspp_type, sspp_index;

	VIRTGPU_VQ_RSP_DBG("virtio_get_plane_hw_attributes  scanout %d plane %d\n", scanout, plane_id);
	for (i = 0; i < output->plane_cnt; i++) {
		if(plane == output->plane_caps[i].plane_id) {
			plane_caps = &output->plane_caps[i];
			break;
		}
	}

	if (!plane_caps) {
		VIRTGPU_VQ_ERR("Not valid plane caps ID->%d\n", plane_id);
		return -EINVAL;
	}
	sspp_type = (le32_to_cpu(resp->sspp_id) & SOURCE_PIPE_TYPE_MASK) >> SOURCE_PIPE_TYPE_SHIFT;
	sspp_index = (le32_to_cpu(resp->sspp_id) & SOURCE_PIPE_INDEX_MASK) >> SOURCE_PIPE_INDEX_SHIFT;
	switch (sspp_type) {
	case SOURCE_PIPE_TYPE_VIDEO:
		if (sspp_index <= (SSPP_VIG_MAX - SSPP_VIG0))
			plane_caps->sspp_id = sspp_index + SSPP_VIG0;
		else
			plane_caps->sspp_id = SSPP_MAX;
		break;
	case SOURCE_PIPE_TYPE_DMA:
		if (sspp_index <= (SSPP_DMA_MAX - SSPP_DMA0))
			plane_caps->sspp_id = sspp_index + SSPP_DMA0;
		else
			plane_caps->sspp_id = SSPP_MAX;
		break;
	default:
		plane_caps->sspp_id = SSPP_MAX;
		break;
	}
	plane_caps->rect_mask = le32_to_cpu(resp->rect_mask);
	VIRTGPU_VQ_RSP_DBG("sspp id %d  rec_mask %X\n", plane_caps->sspp_id, plane_caps->rect_mask);

	return 0;
}

int virtio_gpu_cmd_get_plane_hw_attributes(struct virtio_kms *kms,
		uint32_t scanout,
		uint32_t plane_id)
{
	struct virtio_gpu_get_plane_hw_attributes *cmd_p =
			kzalloc(sizeof(struct virtio_gpu_get_plane_hw_attributes),
			GFP_KERNEL);
	struct virtio_gpu_resp_plane_hw_attributes *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_plane_hw_attributes),
				GFP_KERNEL);
	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];
	int rc = 0;

	if (!cmd_p || !resp) {
		VIRTGPU_VQ_ERR("Memory allocation failed\n");
		goto error;
	}
	VIRTGPU_VQ_CMD_DBG("cmd VIRTIO_GPU_CMD_GET_PLANE_HW_ATTRIBUTES \
			<%d:%d>\n",
			scanout,
			plane_id);
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_PLANE_HW_ATTRIBUTES);
	cmd_p->scanout_id = cpu_to_le32(scanout);
	cmd_p->plane_id = cpu_to_le32(plane_id);

	rc = virtio_hab_send_and_recv(hab_socket,
			&kms->channel[client_id],
			cmd_p,
			sizeof(struct virtio_gpu_get_plane_hw_attributes),
			resp,
			sizeof(struct virtio_gpu_resp_plane_hw_attributes),
			NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		VIRTGPU_VQ_ERR("send_and_recv failed \
				for PLANE_HW_ATTRIBUTES %d\n", rc);
		goto error;
	}

	VIRTGPU_VQ_RSP_DBG("resp VIRTIO_GPU_CMD_GET_PLANE_HW_ATTRIBUTES \
			<%d:%d> (%s)\n",
			le32_to_cpu(resp->scanout_id),
			le32_to_cpu(resp->plane_id),
			virtio_cmd_type(le32_to_cpu(resp->hdr.type)));

	if (scanout != le32_to_cpu(resp->scanout_id) ||
			plane_id != le32_to_cpu(resp->plane_id)) {
		rc = -EINVAL;
		VIRTGPU_VQ_ERR("something wrong with scanout and plane ID's\n");
		goto error;
	}

	rc = virtio_get_plane_hw_attributes(kms,
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

int virtio_gpu_event_kthread(void *d)
{
	struct virtio_kms *kms = (struct virtio_kms *)d;

	struct virtio_gpu_resp_event *buff;
	uint32_t sz = sizeof(struct virtio_gpu_resp_event);
	int ret = 0;
	uint32_t client_id = kms->client_id;
	uint32_t num_events;
	uint32_t i = 0;
	bool enable;
	struct mutex hyp_cbchl_lock;
	mutex_init(&hyp_cbchl_lock);

	buff = kzalloc(sizeof(struct virtio_gpu_resp_event), GFP_KERNEL);
	while (!kms->stop) {

		memset(buff, 0x00, sizeof(struct virtio_gpu_resp_event));
		mutex_lock(&hyp_cbchl_lock);
		ret = virtio_gpu_cmd_get_event(kms, buff);
		if (ret) {
			VIRTGPU_VQ_RSP_DBG("%s mmid %d failed %d size %d\n",
					__func__,
					kms->mmid_event, ret, sz);
			if (ret == -ENODEV)
				break;
		} else {
			VIRTGPU_VQ_RSP_DBG("%s mmid %d ok size %d \n",
					__func__, kms->mmid_event, sz);
		}
		mutex_unlock(&hyp_cbchl_lock);
		for ( i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
			enable = le32_to_cpu(buff->scanout[i].enabled);
			if (!enable)
				continue;
			VIRTGPU_VQ_RSP_DBG(" scanout %d Event received Vsync %d commit %d HPD %d\n",
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

	if (buff)
		kfree(buff);
	ret = habmm_socket_close(kms->channel[client_id].hab_socket[CHANNEL_EVENTS]);
	VIRTGPU_VQ_RSP_DBG("exit event kthread mmid %d\n", kms->mmid_event);
	return 0;
}

/**
 * virtio_gpu_cmd_enable_virq() - Enables virtual interrupt for a given DPU core.
 * @dev: pointer to struct device
 * @kms: pointer to virtio_kms
 * @device_id: dpu core id
 *
 * The function sends a virtio command to enable virtual interrupt
 * for the given DPU core.
 *
 * Return: integer error code
 *
 */
int virtio_gpu_cmd_enable_virq(struct device *dev, struct virtio_kms *kms, uint32_t device_id)
{
	void *va = NULL;
	int rc = 0;

	struct virtio_gpu_enable_virq *cmd_p =
		kzalloc(sizeof(struct virtio_gpu_enable_virq), GFP_KERNEL);
	struct virtio_gpu_resp_enable_virq *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_enable_virq), GFP_KERNEL);

	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];

	if (!cmd_p || !resp) {
		VIRTGPU_VQ_ERR("memory alloc failed req %p resp %p virq_shmem %p\n", cmd_p, resp, va);
		rc = -ENOMEM;
		goto error;
	}

	struct virq_shmem_t *virq_shmem = &(kms->base.virq_shmem[device_id]);

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_ENABLE_VIRQ);
	cmd_p->shmem_id = cpu_to_le32(virq_shmem->hab_export_id);
	cmd_p->shmem_size = cpu_to_le32(virq_shmem->size);
	cmd_p->device_id = cpu_to_le32(device_id);

	rc = virtio_hab_send_and_recv(hab_socket,
		&kms->channel[client_id],
		cmd_p,
		sizeof(struct virtio_gpu_enable_virq),
		resp,
		sizeof(struct virtio_gpu_resp_enable_virq),
		NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		VIRTGPU_VQ_ERR("virtio cmd to enable virq for dpu %u failed with error %d\n",
			device_id, rc);
	} else {
		VIRTGPU_VQ_INFO("virtio cmd to enable virq for dpu %u successful %d\n", device_id, rc);
	}

	error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);

	return rc;
}

/**
 * virtio_gpu_cmd_disable_virq() - Sends virtio cmd to disable virq for a given DPU core.
 * @dev: pointer to struct device
 * @kms: pointer to virtio_kms
 * @device_id: dpu core id
 *
 * Return: integer error code
 *
 */
int virtio_gpu_cmd_disable_virq(struct device *dev, struct virtio_kms *kms, uint32_t device_id)
{
	int rc = 0;
	struct virq_shmem_t *virq_shmem = &(kms->base.virq_shmem[device_id]);

	if (NULL == virq_shmem->vaddr) {
		VIRTGPU_VQ_ERR("virq for dpu %d not initialized\n", device_id);
		return -EINVAL;
	}

	struct virtio_gpu_disable_virq *cmd_p =
		kzalloc(sizeof(struct virtio_gpu_disable_virq), GFP_KERNEL);
	struct virtio_gpu_resp_disable_virq *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_disable_virq), GFP_KERNEL);

	if (!cmd_p || !resp) {
		VIRTGPU_VQ_ERR("memory alloc failed req %p resp %p\n", cmd_p, resp);
		rc = -ENOMEM;
		goto error;
	}

	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_DISABLE_VIRQ);
	cmd_p->shmem_id = cpu_to_le32(virq_shmem->hab_export_id);
	cmd_p->device_id = cpu_to_le32(device_id);

	rc = virtio_hab_send_and_recv(hab_socket,
		&kms->channel[client_id],
		cmd_p,
		sizeof(struct virtio_gpu_disable_virq),
		resp,
		sizeof(struct virtio_gpu_resp_disable_virq),
		NO_SPIN_LOCK_CHANNEL);

	if (rc) {
		VIRTGPU_VQ_ERR("virtio cmd to disable virq for dpu %u failed with error %d\n",
			device_id, rc);
	} else {
		VIRTGPU_VQ_DBG("virtio cmd to disable virq for dpu %u successful %d\n", device_id, rc);
	}

	error:
	if (cmd_p)
		kfree(cmd_p);
	if (resp)
		kfree(resp);

	return rc;
}

/**
 * virtio_gpu_cmd_set_power() - set DPU core power level.
 * @kms: pointer to virtio_kms
 * @device_id: dpu core id
 * @power_level: power level
 *
 * The function sends a virtio command to change (negotiate) the power level
 * for the given DPU core. Host VM shall return the actual power level sets to.
 *
 * Return: integer error code
 *
 */
int virtio_gpu_cmd_set_power(struct virtio_kms *kms, uint32_t device_id, uint32_t power_level)
{
	int rc = 0;

	struct virtio_gpu_set_power *cmd_p =
		kzalloc(sizeof(struct virtio_gpu_set_power), GFP_KERNEL);
	struct virtio_gpu_resp_set_power *resp =
		kzalloc(sizeof(struct virtio_gpu_resp_set_power), GFP_KERNEL);

	uint32_t client_id = kms->client_id;
	int32_t hab_socket = kms->channel[client_id].hab_socket[CHANNEL_CMD];

	if (!cmd_p || !resp) {
		VIRTGPU_VQ_ERR("memory alloc failed req %p resp %p for set power\n", cmd_p, resp);
		rc = -ENOMEM;
		goto error;
	}

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_SET_POWER);
	cmd_p->device_id = cpu_to_le32(device_id);
	cmd_p->power_level = cpu_to_le32(power_level);

	rc = virtio_hab_send_and_recv(hab_socket,
		&kms->channel[client_id],
		cmd_p,
		sizeof(struct virtio_gpu_set_power),
		resp,
		sizeof(struct virtio_gpu_resp_set_power),
		NO_SPIN_LOCK_CHANNEL);
	if (rc) {
		VIRTGPU_VQ_ERR("virtio cmd to set power for dpu %u level %d failed with error %d\n",
				device_id, power_level, rc);
	} else if (resp->error_code) {
		VIRTGPU_VQ_ERR("Failed to change dpu %d power level %d! error %d  level %d\n",
				device_id, power_level, resp->error_code, resp->power_level);
		rc = -EINVAL;
	} else if (resp->power_level < power_level) {
		if (!resp->power_level) {
			VIRTGPU_VQ_ERR("Failed to power up dpu %d level %d! ret level %d\n",
					device_id, power_level, resp->power_level);
			rc = -EINVAL;
		} else {
			VIRTGPU_VQ_WARN("Set dpu %d power level %d not satisfied! ret level %d\n",
					device_id, power_level, resp->power_level);
			rc = -EPERM;
		}
	} else {
		VIRTGPU_VQ_INFO("Set power for dpu %u power level %d successful %d, level %d\n",
				device_id, power_level, rc, resp->power_level);
	}

error:
	kfree(cmd_p);
	kfree(resp);

	return rc;
}
