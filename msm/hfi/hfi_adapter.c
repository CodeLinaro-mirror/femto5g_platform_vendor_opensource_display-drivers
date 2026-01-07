// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hfi_adapter.h"
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#if IS_ENABLED(CONFIG_QTI_HFI_CORE)
#include "hfi_interface.h"
#include "hfi_commands_display.h"
#endif
#include "sde_dbg.h"
#include <uapi/linux/sched/types.h>

#define HFI_AD_INFO(fmt, ...)  \
	pr_info("[hfi_ad_info] %s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define HFI_AD_WARN(fmt, ...)  \
	pr_warn("[hfi_ad_warn] %s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define HFI_AD_ERROR(fmt, ...)  \
	pr_err("[hfi_ad_error] %s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define HFI_AD_DEBUG(fmt, ...)  \
	pr_debug("[hfi_ad_debug] %s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)

#define HFI_APADTER_STEP_US 50
#define MAX_TRY_COUNT 40000
#define MAX_BUFFERS 10
#define MAX_U32 0xFFFFFFFF
#define MAX_POOL_SIZE 32
#define GET_BUF_RETRY 35
#define MIN_USLEEP_RANGE 30000
#define MAX_USLEEP_RANGE 40000
#define HFI_CORE_SSR_ERROR -1

/* Buffer id specific macros */
#define CLIENT_ID_MASK 0xFF //LSB 8 bits are for storing client_id
#define UNIQUE_BUFF_MASK 0xFFFFFF00 //MSB 24 bits are for storing unique buffer id
#define GET_CLIENT_ID(data)           \
	(data & CLIENT_ID_MASK)

#if IS_ENABLED(CONFIG_QTI_HFI_CORE)
static u32 unique_id_counter = 1;
static atomic_t work_queue_pos_wr = ATOMIC_INIT(0);
static DECLARE_BITMAP(wq_inuse_bitmap, HFI_ADAPTER_WORK_QUEUE_SIZE);

static u32 hfi_cmd_type_map[HFI_CMDBUF_TYPE_MAX] = {
	[HFI_CMDBUF_TYPE_ATOMIC_CHECK] = HFI_CMD_BUFF_DISPLAY,
	[HFI_CMDBUF_TYPE_ATOMIC_COMMIT] = HFI_CMD_BUFF_DISPLAY,
	[HFI_CMDBUF_TYPE_DISPLAY_INFO_NO_BLOCK] = HFI_CMD_BUFF_DISPLAY,
	[HFI_CMDBUF_TYPE_DISPLAY_INFO_BLOCKING] = HFI_CMD_BUFF_DISPLAY,
	[HFI_CMDBUF_TYPE_DEVICE_INFO] = HFI_CMD_BUFF_DEVICE,
	[HFI_CMDBUF_TYPE_GET_DEBUG_DATA] = HFI_CMD_BUFF_DEBUG,
};

static atomic_t id_counter = ATOMIC_INIT(0);

static void hfi_thread_priority_worker(struct kthread_work *work)
{
	int ret = 0;
	struct sched_param param = { 0 };
	struct task_struct *task = current->group_leader;

	/**
	 * this priority was found during empiric testing to have appropriate
	 * realtime scheduling to process display updates and interact with
	 * other real time and normal priority task
	 */
	param.sched_priority = 16;
	ret = sched_setscheduler(task, SCHED_FIFO, &param);
	if (ret)
		pr_warn("pid:%d name:%s priority update failed: %d\n",
			current->tgid, task->comm, ret);
}

static u32 _create_buffer_id(u32 ctx_id)
{
	u32 unique_id;

	if (unique_id_counter == 0xFFFFFF)
		unique_id_counter = 1;

	/* Take MSB 24 bits */
	unique_id = (unique_id_counter++ & 0xFFFFFF) << 8;

	return unique_id | (ctx_id & CLIENT_ID_MASK);
}

static int _generate_sequential_packet_id(void)
{
	u32 id;

	do {
		id = atomic_read(&id_counter);
		if (id == MAX_U32) {
			if (atomic_cmpxchg(&id_counter, id, 0) == id)
				HFI_AD_ERROR("failed to reset packet_id counter\n");
		}

	} while ((id != MAX_U32) && atomic_cmpxchg(&id_counter, id, id + 1) != id);

	return id;
}

static void _hfi_clear_buffer(struct hfi_cmdbuf_t *buffer)
{
	if (!buffer) {
		HFI_AD_ERROR("invalid params\n");
		return;
	}

	if (!buffer->pool) {
		HFI_AD_ERROR("invalid buffer pool\n");
		return;
	}

	buffer->cmd_type = 0;
	buffer->obj_id = 0;
	buffer->size = 0;
	atomic_set(&buffer->pool->available, 1);
	atomic_set(&buffer->buffer_send_done, 0);
	buffer->virtq_type = HFI_VIRTQUEUE_TYPE_MAX;
	buffer->is_released = true;
	list_del_init(&buffer->node);
	memset(&buffer->buf, 0, sizeof(buffer->buf)); /* clear buffer */

	HFI_AD_DEBUG("done clearing buffer -- requested by %pS buff:%p\n",
		__builtin_return_address(0), buffer);
}

static int release_rx_buffer_fail(struct hfi_cmdbuf_t *cmd_buf, struct hfi_adapter_t *host)
{
	struct hfi_core_cmds_buf_desc *buff_arr[MAX_BUFFERS];
	int rc;

	if (!cmd_buf || !host)
		return -EINVAL;

	if (cmd_buf->is_released || !cmd_buf->buf.pbuf_vaddr)
		return 0;
	buff_arr[0] = &cmd_buf->buf;
	rc = hfi_core_release_rx_buffer(host->session, buff_arr, 1);
	if (rc)
		HFI_AD_ERROR("failed to release emergency rx buffer\n");

	_hfi_clear_buffer(cmd_buf);

	return rc;
}

static struct hfi_buffer_pool *get_avail_buffer(struct hfi_adapter_t *host)
{
	struct list_head *pos;
	struct hfi_buffer_pool *pool = NULL;
	struct hfi_buffer_pool *ret_pool = NULL;

	if (!host) {
		HFI_AD_ERROR("invalid params\n");
		return NULL;
	}

	/* Check if adapter is shutting down */
	if (atomic_read(&host->shutdown_in_progress)) {
		HFI_AD_DEBUG("adapter shutdown in progress, skipping buffer allocation\n");
		return NULL;
	}

	/* Note: This function now expects the caller to hold hfi_adapter_cmd_buf_list_lock */
	list_for_each(pos, &host->pool->node) {
		pool = list_entry(pos, struct hfi_buffer_pool, node);
		if (!pool) {
			HFI_AD_ERROR("pool is not initialized\n");
			return NULL;
		}

		if (atomic_read(&pool->available)) {
			HFI_AD_DEBUG("found available buffer %p\n", &pool->buffer_t);
			atomic_set(&pool->available, 0);
			ret_pool = pool;
			break;
		}
	}

	if (!ret_pool)
		HFI_AD_ERROR("could not get buffer\n");

	return ret_pool;
}

static void _process_cb_cmd_buf_work(struct kthread_work *work)
{
	struct list_head *ctx_pos;
	struct hfi_client_t *ctx;
	struct hfi_cmdbuf_t *hfi_buff;
	u32 obj_id_rx = MAX_U32;
	int client_id;
	struct hfi_adapter_t *host;
	struct callback_work *cb_cmd_buf_work;
	struct hfi_core_cmds_buf_desc *rx_buffer;
	struct hfi_header *virtio_hdr;
	struct hfi_buffer_pool *pool;
	bool client_found = false;
	int index;
	int i = 0;

	if (!work) {
		HFI_AD_ERROR("%s null work\n", __func__);
		return;
	}

	cb_cmd_buf_work = container_of(work, struct callback_work, work);
	host = cb_cmd_buf_work->host;
	index = cb_cmd_buf_work->index;
	if (!host) {
		HFI_AD_ERROR("thread %d could not match host\n", cb_cmd_buf_work->index);
		return;
	}

	/* Check if adapter is shutting down */
	if (atomic_read(&host->shutdown_in_progress)) {
		HFI_AD_DEBUG("adapter shutdown in progress, exiting worker\n");
		return;
	}

	do {
		mutex_lock(&host->hfi_adapter_cmd_buf_list_lock);
		pool = get_avail_buffer(host);
		if (!pool) {
			HFI_AD_ERROR("failed to get buffer pool\n");
			goto error;
		}

		rx_buffer = &pool->buffer_t.buf;
		hfi_buff = &pool->buffer_t;
		if (!rx_buffer) {
			HFI_AD_ERROR("buffer descriptor is null\n");
			_hfi_clear_buffer(hfi_buff);
			goto error;
		}

		if (hfi_core_cmds_rx_buf_get(host->session, rx_buffer)) {
			_hfi_clear_buffer(hfi_buff);
			goto error;
		}

		virtio_hdr = (struct hfi_header *)rx_buffer->pbuf_vaddr;
		if (!virtio_hdr) {
			HFI_AD_ERROR("virtio_hdr is null\n");
			_hfi_clear_buffer(hfi_buff);
			goto error;
		}
		mutex_unlock(&host->hfi_adapter_cmd_buf_list_lock);

		obj_id_rx = virtio_hdr->object_id;

		hfi_buff->unique_id = virtio_hdr->header_id;

		client_id = GET_CLIENT_ID(virtio_hdr->header_id);

		list_for_each(ctx_pos, &host->client_list) {
			/* Try to match buffer based on unique OBJ ID */
			ctx = list_entry(ctx_pos, struct hfi_client_t, node);
			if (ctx && ctx->client_id == client_id) {
				client_found = true;
				break;
			}
		}

		if (!client_found) {
			HFI_AD_INFO("could not match buffer client id to a client\n");
			mutex_lock(&host->hfi_adapter_cmd_buf_list_lock);
			release_rx_buffer_fail(hfi_buff, host);
			mutex_unlock(&host->hfi_adapter_cmd_buf_list_lock);
			return;
		}

		INIT_LIST_HEAD(&hfi_buff->cmd_buf_chain);
		if (ctx && hfi_buff) {
			hfi_buff->obj_id = virtio_hdr->object_id;
			hfi_buff->ctx = ctx;
			hfi_buff->virtq_type = HFI_VIRTQUEUE_TYPE_RX;
			hfi_buff->is_released = false;
			ctx->process_cmd_buf(ctx, hfi_buff);
		}  else {
			HFI_AD_ERROR("could not match buffer to a client\n");
			mutex_lock(&host->hfi_adapter_cmd_buf_list_lock);
			release_rx_buffer_fail(hfi_buff, host);
			mutex_unlock(&host->hfi_adapter_cmd_buf_list_lock);
		}
	} while (i++ <= MAX_TRY_COUNT);

	if (i >= MAX_TRY_COUNT)
		HFI_AD_ERROR("max retries exceeded: %d\n", i);

	clear_bit(index, wq_inuse_bitmap);
	return;
error:
	clear_bit(index, wq_inuse_bitmap);
	mutex_unlock(&host->hfi_adapter_cmd_buf_list_lock);
	return;
}

static int hfi_adapter_release_cmd_buf_from_pool(struct hfi_adapter_t *host,
		struct hfi_cmdbuf_t *cmd_buf)
{
	struct hfi_core_cmds_buf_desc *buff_arr[MAX_BUFFERS];
	int i = 0, rc = 0;

	if (!cmd_buf || !host) {
		HFI_AD_ERROR("invalid param\n");
		return -EINVAL;
	}

	if (cmd_buf->virtq_type == HFI_VIRTQUEUE_TYPE_MAX)
		return 0;

	if (cmd_buf->is_released || !cmd_buf->buf.pbuf_vaddr)
		return 0;

	buff_arr[i++] = &cmd_buf->buf;
	if (cmd_buf->virtq_type == HFI_VIRTQUEUE_TYPE_RX)
		rc = hfi_core_release_rx_buffer(host->session, buff_arr, i);
	else if (cmd_buf->virtq_type == HFI_VIRTQUEUE_TYPE_TX)
		rc = hfi_core_release_tx_buffer(host->session, buff_arr, i);

	if (rc)
		HFI_AD_ERROR("failed to release rx buffer(s)\n");

	/* Free main buffer head */
	_hfi_clear_buffer(cmd_buf);

	return rc;
}

static void hfi_buffer_pool_cleanup(struct hfi_adapter_t *host)
{
	struct hfi_buffer_pool *pool = NULL;
	struct list_head *pos;

	if (!host || !host->pool) {
		HFI_AD_ERROR("invalid host\n");
		return;
	}

	mutex_lock(&host->hfi_adapter_cmd_buf_list_lock);
	list_for_each(pos, &host->pool->node) {
		pool = list_entry(pos, struct hfi_buffer_pool, node);
		if (!pool)
			continue;

		hfi_adapter_release_cmd_buf_from_pool(host, &pool->buffer_t);
	}
	mutex_unlock(&host->hfi_adapter_cmd_buf_list_lock);
}

static void handle_ssr_start(struct hfi_adapter_t *adapter)
{
	struct list_head *client_pos;
	struct hfi_client_t *client;

	if (!adapter)
		return;

	HFI_AD_DEBUG("handling SSR start\n");
	list_for_each(client_pos, &adapter->client_list) {
		/* Try to match buffer based on unique OBJ ID */
		client = list_entry(client_pos, struct hfi_client_t, node);
		if (client) {
			client->process_event(client, HFI_ADAPTER_EVENT_SSR_START,
				adapter->blocking);
		}
	}

	hfi_buffer_pool_cleanup(adapter);
	HFI_AD_DEBUG("handling SSR start completed\n");
}

static void handle_ssr_end(struct hfi_adapter_t *adapter)
{
	int ret = 0;
	struct hfi_client_t *client;

	if (!adapter) {
		HFI_AD_ERROR("invalid adapter\n");
		return;
	}

	HFI_AD_DEBUG("handling SSR end\n");

	ret = hfi_core_close_session(adapter->session);
	if (ret) {
		HFI_AD_ERROR("hfi_core_close_session failed, ret: %d\n", ret);
		return;
	}
	struct hfi_core_open_params open_params = {
		HFI_CORE_CLIENT_ID_0, adapter->cb_ops, HFI_CORE_HOST};

	adapter->session = hfi_core_open_session(&open_params);
	if (!adapter->session) {
		HFI_AD_ERROR("failed to open hfi core session\n");
		return;
	}
	atomic_set(&adapter->ssr_in_progress, 0);
	HFI_AD_DEBUG("handling SSR end completed\n");

	list_for_each_entry_reverse(client, &adapter->client_list, node) {
		client->process_event(client, HFI_ADAPTER_EVENT_SSR_END,
			adapter->blocking);
	}
}

static void _process_cb_ssr_work(struct kthread_work *work)
{
	struct hfi_adapter_t *adapter;

	if (!work)
		return;

	adapter = container_of(work, struct hfi_adapter_t, cb_ssr_work);

	switch (adapter->event_type) {
	case HFI_ADAPTER_EVENT_SSR_START:
		handle_ssr_start(adapter);
		break;
	case HFI_ADAPTER_EVENT_SSR_END:
		handle_ssr_end(adapter);
		break;
	default:
		break;
	}
}

int32_t callback_function_hfi(struct hfi_core_session *hfi_session,
		const void *cb_data, enum hfi_core_event_type event_type, bool blocking)
{
	struct hfi_adapter_t *adapter = (struct hfi_adapter_t *)cb_data;
	struct callback_work *cb_cmd_buf_work;
	int ret;
	int tries, slot_found = -1;
	int work_queue_idx;

	if (!cb_data)
		return -EINVAL;

	HFI_AD_DEBUG("hfi callback called\n");

	switch (event_type) {
	case HFI_CORE_EVENT_DCP_RESPONSE:
		if (atomic_read(&adapter->ssr_in_progress))
			break;

		/* Don't queue new work if shutdown is in progress */
		if (atomic_read(&adapter->shutdown_in_progress)) {
			HFI_AD_DEBUG("adapter shutdown in progress, skipping work queue\n");
			break;
		}

		work_queue_idx = atomic_fetch_inc(&work_queue_pos_wr) &
				HFI_ADAPTER_WORK_QUEUE_MASK;

		/* Try to claim a free slot */
		for (tries = 0; tries < HFI_ADAPTER_WORK_QUEUE_SIZE; tries++) {
			int try_index = (work_queue_idx + tries) & HFI_ADAPTER_WORK_QUEUE_MASK;
			/* test_and_set_bit returns prev value: 0 means we successfully set it */
			if (!test_and_set_bit(try_index, wq_inuse_bitmap)) {
				slot_found = try_index;
				break;
			}
		}

		if (slot_found < 0) {
			HFI_AD_WARN("failed to find a free slot to queue work\n");
			return -EINVAL;
		}

		cb_cmd_buf_work = &adapter->cb_cmd_buf_work[slot_found];
		SDE_EVT32(event_type, SDE_EVTLOG_FUNC_CASE1);

		ret = kthread_queue_work(&adapter->cb_event_worker, &cb_cmd_buf_work->work);
		if (!ret)
			HFI_AD_WARN("failed to queue work at index:%d\n", slot_found);
		break;
	case HFI_CORE_EVENT_SSR_START:
		HFI_AD_DEBUG("SSR has been initiated\n");
		atomic_set(&adapter->ssr_in_progress, 1);
		/* finish processing all buffers sent by dcp */
		kthread_flush_worker(&adapter->cb_event_worker);
		adapter->event_type = HFI_ADAPTER_EVENT_SSR_START;
		adapter->blocking = blocking;
		ret = kthread_queue_work(&adapter->cb_event_ssr_worker, &adapter->cb_ssr_work);
		if (!ret)
			HFI_AD_WARN("failed to queue ssr start work\n");

		/* This event callback is in non ISR context so blocing is fine */
		if (blocking)
			kthread_flush_work(&adapter->cb_ssr_work);
		break;
	case HFI_CORE_EVENT_SSR_END:
		adapter->event_type = HFI_ADAPTER_EVENT_SSR_END;
		adapter->blocking = blocking;
		ret = kthread_queue_work(&adapter->cb_event_ssr_worker, &adapter->cb_ssr_work);
		if (!ret)
			HFI_AD_WARN("failed to queue ssr end work\n");

		/* This event callback is in non ISR context so blocing is fine */
		if (blocking)
			kthread_flush_work(&adapter->cb_ssr_work);

		HFI_AD_DEBUG("SSR completed successfully\n");
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_QTI_HW_FENCE)
static void _hfi_core_hw_fence_init(struct hfi_core_session *hfi_handle)
{
	int ret = 0;

	ret = hfi_core_hw_fence_init(hfi_handle);
	if (ret) {
		HFI_AD_INFO("failed to init DCP hw fence client\n");
		ret = hfi_core_hw_fence_deinit(hfi_handle);
		if (ret)
			HFI_AD_INFO("failed to deinit DCP hw fence client\n");
	}
}
#else
static void _hfi_core_hw_fence_init(struct hfi_core_session *hfi_handle)
{
	HFI_AD_INFO("HFI hw fence not enabled\n");
}
#endif

struct hfi_adapter_t *hfi_adapter_init(int instance)
{
	struct hfi_adapter_t *hfi_host;
	struct hfi_core_open_params open_params;
	struct hfi_core_cb_ops *cb_ops;
	struct hfi_core_session *hfi_handle;
	struct hfi_buffer_pool *pool, *link;
	int i;

	hfi_host = kmalloc(sizeof(struct hfi_adapter_t), GFP_KERNEL);
	if (!hfi_host) {
		HFI_AD_ERROR("failed to allocate memory for adapter\n");
		return NULL;
	}

	cb_ops = kmalloc(sizeof(struct hfi_core_cb_ops), GFP_KERNEL);
	if (!cb_ops) {
		HFI_AD_ERROR("failed to allocate memory for cb_ops\n");
		kfree(hfi_host);
		return NULL;
	}

	cb_ops->hfi_cb_fn = &callback_function_hfi;
	cb_ops->cb_data = hfi_host;

	open_params.client_id = HFI_CORE_CLIENT_ID_0;
	open_params.ops = cb_ops;

	hfi_handle = hfi_core_open_session(&open_params);
	if (!hfi_handle) {
		HFI_AD_ERROR("failed to open hfi core session\n");
		goto fail;
	}

	/* Initialize DCP hw fence client */
	_hfi_core_hw_fence_init(hfi_handle);

	/* Initialize hfi_adapter_t after core session is created */
	hfi_host->sde_or_vm_instance = instance;
	INIT_LIST_HEAD(&hfi_host->client_list);
	hfi_host->cb_ops = cb_ops;
	hfi_host->session = hfi_handle;

	/* Pre initialize work queues */
	for (i = 0; i < HFI_ADAPTER_WORK_QUEUE_SIZE; i++) {
		kthread_init_work(&hfi_host->cb_cmd_buf_work[i].work, _process_cb_cmd_buf_work);
		hfi_host->cb_cmd_buf_work[i].host = hfi_host;
		hfi_host->cb_cmd_buf_work[i].index = i;
	}
	kthread_init_worker(&hfi_host->cb_event_worker);
	hfi_host->cb_event_worker_thread = kthread_run(kthread_worker_fn,
			&hfi_host->cb_event_worker, "adapter_cb_event_thread");

	if (IS_ERR(hfi_host->cb_event_worker_thread)) {
		HFI_AD_ERROR("failed to create adapter_cb_thread\n");
		goto fail;
	}

	kthread_init_work(&hfi_host->hfi_thread_priority_work,
			  hfi_thread_priority_worker);
	kthread_queue_work(&hfi_host->cb_event_worker, &hfi_host->hfi_thread_priority_work);
	kthread_flush_work(&hfi_host->hfi_thread_priority_work);

	kthread_init_work(&hfi_host->cb_ssr_work, _process_cb_ssr_work);
	kthread_init_worker(&hfi_host->cb_event_ssr_worker);
	hfi_host->cb_event_worker_ssr_thread = kthread_run(kthread_worker_fn,
			&hfi_host->cb_event_ssr_worker, "adapter_cb_event_ssr_thread");

	if (IS_ERR(hfi_host->cb_event_worker_ssr_thread)) {
		HFI_AD_ERROR("failed to create adapter_cb_thread\n");
		goto fail;
	}

	idr_init(&hfi_host->client_ids);
	spin_lock_init(&hfi_host->packet_id_lock);
	mutex_init(&hfi_host->hfi_adapter_cmd_buf_list_lock);

	atomic_set(&hfi_host->ssr_in_progress, 0);
	atomic_set(&hfi_host->shutdown_in_progress, 0);

	/* Initialize buffers */
	pool = kmalloc(sizeof(struct hfi_buffer_pool), GFP_KERNEL);
	if (!pool) {
		HFI_AD_ERROR("failed to allocate memory for buffer pool\n");
		goto fail;
	}

	INIT_LIST_HEAD(&pool->node);
	INIT_LIST_HEAD(&pool->buffer_t.node);
	mutex_init(&pool->lock);
	mutex_init(&pool->buffer_t.lock);
	pool->buffer_t.pool = pool;
	_hfi_clear_buffer(&pool->buffer_t);

	for (i = 0; i < MAX_POOL_SIZE; i++) {
		link = kmalloc(sizeof(struct hfi_buffer_pool), GFP_KERNEL);
		if (!link) {
			HFI_AD_ERROR("failed to allocate memory for buffer pool\n");
			goto pool_fail;
		}
		INIT_LIST_HEAD(&link->buffer_t.node);
		list_add_tail(&link->node, &pool->node);
		atomic_set(&link->available, 1);
		mutex_init(&link->lock);
		mutex_init(&link->buffer_t.lock);
		link->buffer_t.pool = link;
		_hfi_clear_buffer(&link->buffer_t);

		/* Debug information */
		 HFI_AD_DEBUG("hfi buffer address = %p\n", &link->buffer_t);
	}

	hfi_host->pool = pool;

	return hfi_host;

pool_fail:
	kfree(pool);
fail:
	kfree(cb_ops);
	kfree(hfi_host);
	return NULL;
}

int hfi_adapter_client_register(struct hfi_adapter_t *host, struct hfi_client_t *ctx)
{
	if (!ctx->process_cmd_buf) {
		HFI_AD_ERROR("invalid client callback function pointer\n");
		return -EINVAL;
	}

	if (!host) {
		HFI_AD_ERROR("invalid host pointer\n");
		return -EINVAL;
	}

	/* Client can have multiple command buffers of different unique ID's */
	INIT_LIST_HEAD(&ctx->cmd_buf_list);

	/* Initialize client's packet litener list */
	INIT_LIST_HEAD(&ctx->packet_listeners.list_ptr);

	mutex_init(&ctx->listener_lock);
	ctx->host = host;
	list_add_tail(&ctx->node, &host->client_list);

	ctx->client_id = idr_alloc(&host->client_ids, ctx, 1, 0, GFP_KERNEL);
	if (ctx->client_id < 0) {
		HFI_AD_ERROR("failed to get id for client\n");
		return -HFI_ERROR;
	}

	mutex_init(&ctx->lock);

	return 0;
}

static struct hfi_cmdbuf_t *_hfi_adapter_get_cmd_buf_helper(struct hfi_client_t *ctx,
		u32 obj_id, u32 cmdbuf_type)
{
	struct hfi_cmdbuf_t *buffer;
	struct hfi_core_cmds_buf_desc *buff_desc;
	struct hfi_cmd_buff_hdl buff_handle;
	struct hfi_header_info header_info;
	struct hfi_buffer_pool *pool, *old_pool;
	struct hfi_adapter_t *adapter;
	int ret = 0;
	static u32 counter;
	int failed_loop = 0;

	if (!ctx) {
		HFI_AD_ERROR("invalid client callback function pointer\n");
		return NULL;
	}
	adapter = ctx->host;

	/* Acquire lock to protect the entire critical section including pool->available */
	mutex_lock(&adapter->hfi_adapter_cmd_buf_list_lock);
	pool = get_avail_buffer(adapter);
	if (!pool) {
		HFI_AD_ERROR("failed to get available buffer pool\n");
		mutex_unlock(&adapter->hfi_adapter_cmd_buf_list_lock);
		return NULL;
	}

	buff_desc = &pool->buffer_t.buf;

	if (!buff_desc) {
		HFI_AD_ERROR("failed to allocate memory for buffer descriptor\n");
		goto error;
	}

	buff_desc->prio_info = HFI_CORE_PRIO_0;

	counter++;
	do {
		if (atomic_read(&adapter->ssr_in_progress))
			break;

		old_pool = pool;
		ret = hfi_core_cmds_tx_buf_get(adapter->session, buff_desc);
		if (ret) {
			if (ret == HFI_CORE_SSR_ERROR)
				break;
			failed_loop++;

			/* If we've exhausted 3 retries with current pool, try a different pool */
			if (failed_loop % (GET_BUF_RETRY/MAX_POOL_SIZE) == 3) {

				buffer = &old_pool->buffer_t;
				/* If adapter cmd buffer alloc fails, release VIRTIO buffer */
				if (!buffer) {
					HFI_AD_ERROR("failed to alloc memory for cmd buffer\n");
					ret = hfi_core_release_tx_buffer(adapter->session,
						&buff_desc, 1);
					if (ret)
						HFI_AD_ERROR("failed to release buffer\n");
					_hfi_clear_buffer(buffer); /* Clear the buffer */
				}
				/* Try to get a different available pool */
				pool = get_avail_buffer(adapter);
				if (!pool) {
					HFI_AD_ERROR("no more available buffer pools\n");
					ret = -ENOMEM;
					break;
				}

				buff_desc = &pool->buffer_t.buf;
				if (!buff_desc) {
					HFI_AD_ERROR("failed to get buffer descriptor\n");
					goto error;
				}
				buff_desc->prio_info = HFI_CORE_PRIO_0;
				HFI_AD_DEBUG("Trying with new pool %p\n", &pool->buffer_t);
			}

			HFI_AD_ERROR("failed to get tx buff counter:%d retry:%d ret:%d\n",
				counter, failed_loop, ret);
			usleep_range(MIN_USLEEP_RANGE, MAX_USLEEP_RANGE);
		}
	} while (ret && failed_loop < GET_BUF_RETRY);

	if (ret) {
		HFI_AD_ERROR("failed to get tx buffer from hfi core error code: %d", ret);
		goto error;
	}

	buffer = &pool->buffer_t;

	if (!buffer) {
		HFI_AD_ERROR("failed to allocate memory for adapter command buffer\n");
		/* If adapter command buffer allocation fails, release VIRTIO buffer */
		ret = hfi_core_release_tx_buffer(adapter->session, &buff_desc, 1);
		if (ret)
			HFI_AD_ERROR("failed to release buffer back to virtio queue\n");
		goto error;
	}

	/* Populate structs for HFI Packer */
	buff_handle.cmd_buffer = buff_desc->pbuf_vaddr;
	buff_handle.size = buff_desc->size;

	header_info.num_packets = 0;
	header_info.cmd_buff_type = hfi_cmd_type_map[cmdbuf_type];
	header_info.object_id = obj_id;
	header_info.header_id = _create_buffer_id(ctx->client_id);

	if (!atomic_read(&adapter->ssr_in_progress)) {
		ret = hfi_create_header(&buff_handle, &header_info);
		if (ret) {
			HFI_AD_ERROR("failed to create buffer header\n");
			ret = hfi_core_release_tx_buffer(adapter->session, &buff_desc, 1);
			if (ret)
				HFI_AD_ERROR("failed to release buffer back to virtio queue\n");
			goto error;
		}
	}

	/* Populate adapter buffer structure members */
	buffer->cmd_type = cmdbuf_type;
	buffer->unique_id = header_info.header_id;
	buffer->obj_id = obj_id;
	buffer->size = 32;
	buffer->ctx = ctx;
	buffer->virtq_type = HFI_VIRTQUEUE_TYPE_TX;
	buffer->is_released = false;

	mutex_unlock(&adapter->hfi_adapter_cmd_buf_list_lock);
	return buffer;

error:
	if (pool)
		_hfi_clear_buffer(&pool->buffer_t);
	mutex_unlock(&adapter->hfi_adapter_cmd_buf_list_lock);
	return NULL;
}

struct hfi_cmdbuf_t *hfi_adapter_get_cmd_buf(struct hfi_client_t *ctx, u32 obj_id, u32 cmdbuf_type)
{
	struct hfi_cmdbuf_t *buffer;

	buffer = _hfi_adapter_get_cmd_buf_helper(ctx, obj_id, cmdbuf_type);

	if (!buffer) {
		HFI_AD_ERROR("failed to get command buffer\n");
		return NULL;
	}

	/* For new (non-chained) buffer's, initialize chain list */
	INIT_LIST_HEAD(&buffer->cmd_buf_chain);

	/* Add buffer to client context */
	mutex_lock(&ctx->lock);
	list_del_init(&buffer->node);
	list_add_tail(&buffer->node, &ctx->cmd_buf_list);
	mutex_unlock(&ctx->lock);

	return buffer;
}

static struct hfi_cmdbuf_t *_chain_new_buffer(struct hfi_cmdbuf_t *buffer_head)
{
	struct hfi_cmdbuf_t *buffer;
	struct hfi_adapter_t *host;

	if (!buffer_head || !buffer_head->ctx || !buffer_head->ctx->host) {
		HFI_AD_ERROR("invalid params\n");
		return NULL;
	}
	host = buffer_head->ctx->host;

	buffer = _hfi_adapter_get_cmd_buf_helper(buffer_head->ctx, buffer_head->obj_id,
			buffer_head->cmd_type);

	if (!buffer) {
		HFI_AD_ERROR("failed to chain command buffer\n");
		return NULL;
	}

	mutex_lock(&host->hfi_adapter_cmd_buf_list_lock);
	/* For chained buffer's, add to existing cmd_buf_chain */
	list_add_tail(&buffer->cmd_buf_chain, &buffer_head->cmd_buf_chain);
	mutex_unlock(&host->hfi_adapter_cmd_buf_list_lock);

	return buffer;
}

static struct hfi_cmdbuf_t *_check_attached_buffer(struct hfi_cmdbuf_t *cmd_buf,
		enum hfi_payload_type hfi_payload_type, u32 size)
{
	struct hfi_adapter_t *host;
	struct hfi_cmdbuf_t *current_buffer = cmd_buf;

	if (!cmd_buf || !cmd_buf->ctx) {
		HFI_AD_ERROR("invalid command buffer\n");
		return NULL;
	}

	host = cmd_buf->ctx->host;
	if (!host)
		return NULL;

	mutex_lock(&host->hfi_adapter_cmd_buf_list_lock);
	u32 available_buff_size = cmd_buf->buf.size - cmd_buf->size;
	mutex_unlock(&host->hfi_adapter_cmd_buf_list_lock);

	/* 32 bytes for packet header */
	u32 packet_size = 32;

	/* If we have a payload, add the size of the payload to packet size */
	if (hfi_payload_type != HFI_PAYLOAD_TYPE_NONE)
		packet_size += size;

	mutex_lock(&host->hfi_adapter_cmd_buf_list_lock);
	/* If there is a chained buffer, use the tail */
	if (!list_empty(&cmd_buf->cmd_buf_chain)) {
		current_buffer = list_last_entry(&cmd_buf->cmd_buf_chain, struct hfi_cmdbuf_t,
				cmd_buf_chain);
		available_buff_size = current_buffer->buf.size - current_buffer->size;
		HFI_AD_DEBUG("found a chained buffer, using it as current buffer\n");

		if (!current_buffer) {
			HFI_AD_ERROR("failed to get chained buffer tail\n");
			mutex_unlock(&host->hfi_adapter_cmd_buf_list_lock);
			return NULL;
		}
	}
	mutex_unlock(&host->hfi_adapter_cmd_buf_list_lock);

	/* Validate size, if not available then chain new buffer */
	if ((available_buff_size < packet_size) && current_buffer->size)
		current_buffer = _chain_new_buffer(cmd_buf);

	if (!current_buffer) {
		HFI_AD_ERROR("failed to chain command buffer\n");
		return NULL;
	}

	current_buffer->size += packet_size;

	return current_buffer;
}

static u32 _hfi_adapter_add_prop_helper(struct hfi_cmdbuf_t *cmd_buf, u32 cmd, u32 object_id,
		enum hfi_payload_type hfi_payload_type, void *payload, u32 size, u32 flags,
		u32 cnt, u32 *packet_id)
{
	struct hfi_cmd_buff_hdl buff_handle;
	struct hfi_packet_info packet_info;
	unsigned long lock_flags;
	int rc = 0;

	if (hfi_payload_type != HFI_PAYLOAD_TYPE_NONE && !payload) {
		HFI_AD_ERROR("payload not provided for cmd:0x%x type:%d\n",
			cmd, hfi_payload_type);
		return -EINVAL;
	} else if (hfi_payload_type == HFI_PAYLOAD_TYPE_NONE && payload) {
		HFI_AD_WARN("unexpected packet payload for cmd:0x%x\n", cmd);
	}

	/*
	 * if SSR is in progress, cannot queue buffer to hfi core,
	 * so do not create packets.
	 */
	if (atomic_read(&cmd_buf->ctx->host->ssr_in_progress))
		return rc;

	/* Populate HFI packer structs */
	buff_handle.cmd_buffer = cmd_buf->buf.pbuf_vaddr;
	buff_handle.size = cmd_buf->buf.size;

	memset(&packet_info, 0, sizeof(struct hfi_packet_info));
	packet_info.cmd = cmd;
	packet_info.id = object_id;
	packet_info.flags = flags;
	spin_lock_irqsave(&cmd_buf->ctx->host->packet_id_lock, lock_flags);
	packet_info.packet_id = _generate_sequential_packet_id();
	spin_unlock_irqrestore(&cmd_buf->ctx->host->packet_id_lock, lock_flags);
	packet_info.payload_type = (enum hfi_packet_payload_type) hfi_payload_type;
	packet_info.payload_size = size;
	packet_info.payload_ptr = payload;

	if (hfi_payload_type == HFI_PAYLOAD_TYPE_NONE || cnt > 0)
		rc = hfi_create_packet_header(&buff_handle, &packet_info);
	else
		rc = hfi_create_full_packet(&buff_handle, &packet_info);

	if (rc) {
		HFI_AD_ERROR("failed to create hfi packet. error code = %d\n", rc);
		return rc;
	}

	if (cnt != 0) {
		/* Append the key value pairs */
		rc = hfi_append_packet_with_kv_pairs(&buff_handle, cmd,
				(enum hfi_packet_payload_type) hfi_payload_type, 0,
				(struct hfi_kv_info *)payload, cnt, size);
		if (rc) {
			HFI_AD_ERROR("failed to append kv pairs. error code = %d\n", rc);
			return rc;
		}
	}

	*packet_id = packet_info.packet_id;

	return rc;
}

int hfi_adapter_add_set_property(struct hfi_client_t *ctx, struct hfi_cmdbuf_t *cmd_buf, u32 cmd,
		u32 object_id, enum hfi_payload_type hfi_payload_type, void *payload, u32 size,
		u32 flags)
{
	struct hfi_cmdbuf_t *current_buffer = cmd_buf;
	u32 packet_id;
	int rc = 0;

	if (!ctx) {
		HFI_AD_ERROR("invalid client\n");
		return -EINVAL;
	}

	current_buffer = _check_attached_buffer(cmd_buf, hfi_payload_type, size);
	if (!current_buffer)
		return -EINVAL;

	rc = _hfi_adapter_add_prop_helper(current_buffer, cmd, object_id, hfi_payload_type,
			payload, size, flags, 0, &packet_id);

	return rc;
}

/**
 * _hfi_adapter_remove_listeners_by_event - Remove listeners associated with a specific event ID
 * @ctx: Pointer to hfi_client struct
 * @event_id: Event ID for which listeners should be removed
 * @obj_id: Object ID for additional filtering (optional, use 0 to ignore)
 *
 * This function removes and frees all listeners that were registered for the specified
 * event ID. This is typically called when deregistering events to clean up registration listeners.
 */
static void _hfi_adapter_remove_listeners_by_event(struct hfi_client_t *ctx,
		u32 event_id, u32 obj_id)
{
	struct list_head *pos, *temp;
	struct listener_list *listener_entry;
	int removed_count = 0;

	if (!ctx) {
		HFI_AD_ERROR("invalid client context\n");
		return;
	}

	mutex_lock(&ctx->listener_lock);

	list_for_each_safe(pos, temp, &ctx->packet_listeners.list_ptr) {
		listener_entry = list_entry(pos, struct listener_list, list_ptr);
		if (!listener_entry || !listener_entry->listener_obj)
			continue;

		/*
		 * For deregistration, we need to match based on the event ID
		 * and object ID. The listener was originally created for a registration
		 * command with a specific event ID in the payload.
		 * We store the original event ID from the payload in listener_entry
		 * for this purpose.
		 */
		if (listener_entry->event_id == event_id &&
		    (obj_id == 0 || listener_entry->obj_id == obj_id)) {

			HFI_AD_DEBUG("%s: event_id:0x%x obj_id:0x%x packet_id:0x%x\n",
				__func__, listener_entry->event_id, listener_entry->obj_id,
				listener_entry->packet_id);

			list_del(pos);
			kfree(listener_entry);
			removed_count++;
		}
	}

	mutex_unlock(&ctx->listener_lock);

	if (removed_count > 0) {
		HFI_AD_DEBUG("removed %d registration listeners for event_id:0x%x obj_id:0x%x\n",
			     removed_count, event_id, obj_id);
	}
}

int hfi_adapter_add_get_property(struct hfi_client_t *ctx, struct hfi_cmdbuf_t *cmd_buf,
		u32 cmd_id, u32 obj_id, enum hfi_payload_type hfi_payload_type,
		void *payload, u32 size, struct hfi_prop_listener *listener, u32 flags)
{
	struct hfi_cmdbuf_t *current_buffer = cmd_buf;
	struct hfi_client_t *buff_client_ctx;
	u32 packet_id;
	int rc = 0;

	if (!ctx || !cmd_buf) {
		HFI_AD_ERROR("invalid client\n");
		return -EINVAL;
	}

	/*
	 * For deregistration commands, also clean up the corresponding registration listeners
	 * This handles the case where we're deregistering an event - we should remove
	 * the original registration listeners for that event.
	 */
	if (cmd_id == HFI_COMMAND_DISPLAY_EVENT_DEREGISTER && payload && size >= sizeof(u32)) {
		u32 event_id = *(u32 *)payload;

		_hfi_adapter_remove_listeners_by_event(ctx, event_id, obj_id);
	}

	/* Continue with normal listener creation for deregister command to get response */
	current_buffer = _check_attached_buffer(cmd_buf, hfi_payload_type, size);
	if (!current_buffer)
		return -EINVAL;

	rc = _hfi_adapter_add_prop_helper(current_buffer, cmd_id, obj_id, hfi_payload_type,
			payload, size, flags, 0, &packet_id);
	if (rc) {
		HFI_AD_ERROR("failed to populate buffer packet with cmd:0x%x\n", cmd_id);
		return rc;
	}

	buff_client_ctx = cmd_buf->ctx;

	/* Create new listener_list structure to insert. */
	struct listener_list *listener_entry = kzalloc(sizeof(struct listener_list), GFP_KERNEL);

	if (!listener_entry) {
		HFI_AD_ERROR("failed to allocate memory for listener_entry\n");
		return -ENOMEM;
	}

	listener_entry->packet_id = packet_id;
	listener_entry->listener_obj = listener;
	listener_entry->cmd_id = cmd_id;  /* Store command ID to identify deregister listeners */
	listener_entry->obj_id = obj_id;  /* Store object ID for later cleanup */

	/* Extract and store the event ID from payload for registration listeners cleanup */
	if (payload && size >= sizeof(u32))
		listener_entry->event_id = *(u32 *)payload;

	/* Add listener based on packet obj_id  */
	mutex_lock(&buff_client_ctx->listener_lock);
	list_add_tail(&listener_entry->list_ptr,
			&buff_client_ctx->packet_listeners.list_ptr);
	mutex_unlock(&buff_client_ctx->listener_lock);

	return rc;
}

int hfi_adapter_add_prop_array(struct hfi_client_t *ctx, struct hfi_cmdbuf_t *cmd_buf, u32 cmd,
		u32 object_id, enum hfi_payload_type payload_type,
		struct hfi_kv_pairs *payload, u32 cnt, u32 size)
{
	struct hfi_cmdbuf_t *current_buffer = cmd_buf;
	u32 packet_id;
	int rc = 0;

	if (!ctx) {
		HFI_AD_ERROR("invalid client\n");
		return -EINVAL;
	}

	if (!payload) {
		HFI_AD_ERROR("payload not provided\n");
		return -EINVAL;
	}

	if (payload_type == HFI_PAYLOAD_TYPE_NONE || !size || !cnt) {
		HFI_AD_ERROR("invalid payload parameters\n");
		return -EINVAL;
	}

	current_buffer = _check_attached_buffer(cmd_buf, payload_type, size);
	if (!current_buffer)
		return -EINVAL;

	rc = _hfi_adapter_add_prop_helper(current_buffer, cmd, object_id, payload_type,
			payload, size, HFI_HOST_FLAGS_NON_DISCARDABLE, cnt, &packet_id);


	return rc;
}

static void _release_tx_buffers(struct hfi_cmdbuf_t *cmd_buf)
{
	struct list_head *pos, *updated_pos;
	struct hfi_cmdbuf_t *buf_entry;
	struct hfi_client_t *ctx;
	int i = 0;
	struct hfi_core_cmds_buf_desc *buff_arr[MAX_BUFFERS];
	struct hfi_adapter_t *host;

	if (!cmd_buf) {
		HFI_AD_ERROR("invalid params\n");
		return;
	}

	ctx = cmd_buf->ctx;
	if (!ctx) {
		HFI_AD_ERROR("no valid client for cmd_buf:%p\n", cmd_buf);
		return;
	}
	host = ctx->host;
	if (!host) {
		HFI_AD_ERROR("no valid host for client id: %d\n", ctx->client_id);
		return;
	}

	mutex_lock(&ctx->lock);

	buff_arr[i++] = &cmd_buf->buf;

	if (!list_empty(&cmd_buf->cmd_buf_chain)) {
		list_for_each_prev_safe(pos, updated_pos, &cmd_buf->cmd_buf_chain) {
			buf_entry = list_entry(pos, struct hfi_cmdbuf_t, cmd_buf_chain);
			buff_arr[i++] = &buf_entry->buf;
			if (buf_entry->pool)
				_hfi_clear_buffer(buf_entry);
			list_del_init(pos);
		}
		list_del_init(&cmd_buf->cmd_buf_chain);
	}

	/*
	 * If SSR is in progress, SSR start event would have already taken care of all required
	 * buffer releases. Hence do not call release again here.
	 */
	if (atomic_read(&host->ssr_in_progress))
		cmd_buf->is_released = true;

	if (!cmd_buf->is_released)
		hfi_core_release_tx_buffer(cmd_buf->ctx->host->session, buff_arr, i);

	list_del_init(&cmd_buf->node);
	_hfi_clear_buffer(cmd_buf);
	mutex_unlock(&ctx->lock);
}

int hfi_adapter_set_cmd_buf(struct hfi_client_t *ctx, struct hfi_cmdbuf_t *cmd_buf)
{
	u32 num_buffers = 1;
	struct hfi_core_cmds_buf_desc *buff_arr[MAX_BUFFERS];
	int rc = 0;
	struct hfi_adapter_t *host;
	struct hfi_cmdbuf_t *buf_entry;
	u32 i = 1;

	if (!cmd_buf || !cmd_buf->ctx || !ctx) {
		HFI_AD_ERROR("Invalid client ctx\n");
		return -EINVAL;
	}

	if (cmd_buf->virtq_type != HFI_VIRTQUEUE_TYPE_TX) {
		HFI_AD_ERROR("invalid virtqueue type %d\n", cmd_buf->virtq_type);
		return -EINVAL;
	}

	host = cmd_buf->ctx->host;
	if (!host)
		return -EINVAL;

	if (atomic_read(&host->ssr_in_progress))
		goto exit;

	/* Append the number of chained buffers */
	struct list_head *pos;
	list_for_each(pos, &cmd_buf->cmd_buf_chain)
		num_buffers++;

	HFI_AD_DEBUG("from %pS\n", __builtin_return_address(0));

	buff_arr[0] = &cmd_buf->buf;

	list_for_each(pos, &cmd_buf->cmd_buf_chain) {
		buf_entry = list_entry(pos, struct hfi_cmdbuf_t, cmd_buf_chain);
		if (buf_entry)
			buff_arr[i++] = &buf_entry->buf;
	}

	u32 host_flags = HFI_CORE_SET_FLAGS_TRIGGER_IPC;

	if (!atomic_read(&host->ssr_in_progress)) {
		rc = hfi_core_cmds_tx_buf_send(cmd_buf->ctx->host->session,
				buff_arr, num_buffers, host_flags);
		if (rc) {
			HFI_AD_ERROR("failed to send tx buffer. error code = %d\n", rc);
		} else {
			mutex_lock(&host->hfi_adapter_cmd_buf_list_lock);
			cmd_buf->is_released = true;
			mutex_unlock(&host->hfi_adapter_cmd_buf_list_lock);
		}
	}

exit:
	mutex_lock(&host->hfi_adapter_cmd_buf_list_lock);
	_release_tx_buffers(cmd_buf);
	mutex_unlock(&host->hfi_adapter_cmd_buf_list_lock);

	return rc;
}

int hfi_adapter_set_cmd_buf_blocking(struct hfi_client_t *ctx, struct hfi_cmdbuf_t *cmd_buf)
{
	struct hfi_adapter_t *host;
	int rc = 0;
	bool response_ack = false;
	u32 wait_count = 0;
	struct list_head *pos;
	struct hfi_cmdbuf_t *buf_entry;
	struct hfi_core_cmds_buf_desc *buff_arr[MAX_BUFFERS];
	u32 num_buffers = 1;
	u32 i = 0;

	if (!cmd_buf || !cmd_buf->ctx || !ctx) {
		HFI_AD_ERROR("Invalid client ctx\n");
		return -EINVAL;
	}

	if (cmd_buf->virtq_type != HFI_VIRTQUEUE_TYPE_TX) {
		HFI_AD_ERROR("invalid virtqueue type %d\n", cmd_buf->virtq_type);
		return -EINVAL;
	}

	host = cmd_buf->ctx->host;
	if (!host)
		return -EINVAL;

	if (atomic_read(&host->ssr_in_progress)) {
		HFI_AD_DEBUG("SSR in progress\n");
		goto exit;
	}

	/* Append the number of chained buffers */
	list_for_each(pos, &cmd_buf->cmd_buf_chain)
		num_buffers++;

	HFI_AD_DEBUG("from %pS\n", __builtin_return_address(0));

	buff_arr[i++] = &cmd_buf->buf;
	list_for_each(pos, &cmd_buf->cmd_buf_chain) {
		buf_entry = list_entry(pos, struct hfi_cmdbuf_t, cmd_buf_chain);
		if (buf_entry)
			buff_arr[i++] = &buf_entry->buf;
	}

	u32 host_flags = HFI_CORE_SET_FLAGS_TRIGGER_IPC;

	if (!atomic_read(&host->ssr_in_progress)) {
		rc = hfi_core_cmds_tx_buf_send(cmd_buf->ctx->host->session,
				buff_arr, num_buffers, host_flags);
		HFI_AD_DEBUG("from %pS: host_flags:0x%x\n",
			__builtin_return_address(0), host_flags);
		if (rc) {
			HFI_AD_ERROR("failed to send tx buffer. error code = %d\n", rc);
			return rc;
		} else {
			mutex_lock(&host->hfi_adapter_cmd_buf_list_lock);
			cmd_buf->is_released = true;
			mutex_unlock(&host->hfi_adapter_cmd_buf_list_lock);
		}
	}

	HFI_AD_DEBUG("[info] tx buffer sent\n");

	atomic_set(&cmd_buf->waiting_for_rsp, 1);
	do {
		if (atomic_read(&host->ssr_in_progress))
			break;
		usleep_range(HFI_APADTER_STEP_US, HFI_APADTER_STEP_US + 10);
		if (wait_count++ > MAX_TRY_COUNT) {
			HFI_AD_ERROR("set_cmd_buf_blocking wait timed-out\n");
			rc = hfi_core_notify_rsp_timeout(host->session);
			atomic_set(&cmd_buf->waiting_for_rsp, 0);
			rc = -ETIMEDOUT;
			break;
		}
		response_ack = atomic_read(&cmd_buf->buffer_send_done);
		HFI_AD_INFO("response_ack = 0x%08X\n", response_ack);
	} while (!response_ack);
	atomic_set(&cmd_buf->waiting_for_rsp, 0);

	if (!response_ack)
		HFI_AD_ERROR("timed out waiting for response_ack for tx!\n");
	else
		HFI_AD_DEBUG("[info] buffer response received after %d ms\n", wait_count);

exit:
	mutex_lock(&host->hfi_adapter_cmd_buf_list_lock);
	_release_tx_buffers(cmd_buf);
	mutex_unlock(&host->hfi_adapter_cmd_buf_list_lock);

	return rc;
}

int hfi_adapter_unpack_cmd_buf(struct hfi_client_t *ctx, struct hfi_cmdbuf_t *cmd_buf)
{
	struct hfi_prop_listener *listener = NULL;
	struct listener_list *listener_entry = NULL;
	struct hfi_cmd_buff_hdl buff_handle;
	struct list_head *pos_to_remove = NULL;
	struct listener_list *entry_to_remove = NULL;
	struct hfi_header_info header_info;
	struct hfi_packet_info packet_info;
	struct list_head *pos = NULL;
	struct hfi_cmdbuf_t *buf_entry = NULL;
	struct list_head *updated_pos = NULL;
	u32 num_packets = 0;
	int i;
	int rc = 0;
	int ret = 0;

	/* Local list for collecting listeners */
	LIST_HEAD(local_listener_list);
	struct listener_list *temp_entry, *temp_next;

	if (!ctx || !cmd_buf || !ctx->host) {
		HFI_AD_ERROR("invalid param\n");
		return -EINVAL;
	}

	if (cmd_buf->virtq_type != HFI_VIRTQUEUE_TYPE_RX) {
		HFI_AD_ERROR("invalid virtqueue type %d\n", cmd_buf->virtq_type);
		return -EINVAL;
	}

	/* Request header info to obtain the number of packets available in the buffer */
	buff_handle.cmd_buffer = cmd_buf->buf.pbuf_vaddr;
	buff_handle.size = cmd_buf->buf.size;

	rc = hfi_unpacker_get_header_info(&buff_handle, &header_info);

	if (rc) {
		HFI_AD_ERROR("failed to unpack command buffer header info\n");
		return rc;
	}

	num_packets = header_info.num_packets;

	for (i = 0; i < num_packets; i++) {
		memset(&packet_info, 0, sizeof(struct hfi_packet_info));
		/* Request packet info for packet (i+1) since packets are 1-indexed */
		rc = hfi_unpacker_get_packet_info(&buff_handle, i+1, &packet_info);
		if (rc) {
			HFI_AD_ERROR("failed to get packet info for packet %d\n", i+1);
			continue;
		}

		/* Phase 1: Collect matching listeners into local list under lock */
		mutex_lock(&ctx->listener_lock);
		list_for_each(pos, &ctx->packet_listeners.list_ptr) {
			pos_to_remove = NULL;
			listener_entry = list_entry(pos, struct listener_list, list_ptr);
			if (!listener_entry) {
				HFI_AD_DEBUG("listener_entry is NULL\n");
				continue;
			}

			if (!listener_entry->listener_obj) {
				HFI_AD_ERROR("[warning] no listener's attached\n");
				ret = -HFI_ERROR;
				continue;
			}

			/* If packet_id's match for the response packet(s), add to local list */
			if (packet_info.packet_id == listener_entry->packet_id) {
				listener = (struct hfi_prop_listener *)
					(listener_entry->listener_obj);
				if (!listener)
					continue;

				/*
				 * If this listener was created for a deregister command,
				 * mark it for removal after we finish processing to avoid
				 * modifying list while iterating.
				 * Note: packet_info.cmd contains the event ID from response,
				 * not the original command.
				 */
				if (listener_entry->cmd_id ==
						HFI_COMMAND_DISPLAY_EVENT_DEREGISTER) {
					pos_to_remove = pos;
					entry_to_remove = listener_entry;
					HFI_AD_DEBUG("listener for removal packet:%x event:%x\n",
						listener_entry->packet_id, packet_info.cmd);
				}

				/* Allocate temporary listener_list entry and add to local list */
				temp_entry = kzalloc(sizeof(struct listener_list), GFP_KERNEL);
				if (!temp_entry) {
					HFI_AD_ERROR("failed to allocate temp listener entry\n");
					ret = -ENOMEM;
					continue;
				}
				temp_entry->listener_obj = listener;
				temp_entry->packet_id = packet_info.packet_id;
				list_add_tail(&temp_entry->list_ptr, &local_listener_list);
			}
		}
		mutex_unlock(&ctx->listener_lock);

		/* Phase 2: Invoke callbacks without holding lock */
		list_for_each_entry_safe(temp_entry, temp_next,
					 &local_listener_list, list_ptr) {
			listener = (struct hfi_prop_listener *)temp_entry->listener_obj;

			SDE_EVT32(num_packets, packet_info.id, packet_info.cmd);
			listener->hfi_prop_handler(packet_info.id,
					packet_info.cmd, packet_info.payload_ptr,
					packet_info.payload_size, listener);

			if (packet_info.flags != HFI_RX_FLAGS_NONE &&
					packet_info.flags != HFI_RX_FLAGS_SUCCESS) {
				HFI_AD_ERROR("response packet error. cmd:0x%x resp:0x%x\n",
						packet_info.cmd, packet_info.flags);
				ret = -HFI_ERROR;
			}

			/* Remove and free the temporary entry */
			list_del(&temp_entry->list_ptr);
			kfree(temp_entry);
		}

		/* Remove deregister listener after processing, outside the iteration */
		if (pos_to_remove && entry_to_remove) {
			list_del(pos_to_remove);
			kfree(entry_to_remove);
		}
	}

	/* Loop through clients list and if matching unique_id then release */
	mutex_lock(&ctx->host->hfi_adapter_cmd_buf_list_lock);
	list_for_each_safe(pos, updated_pos, &ctx->cmd_buf_list) {
		buf_entry = list_entry(pos, struct hfi_cmdbuf_t, node);
		if (!buf_entry)
			continue;

		if (buf_entry->unique_id == cmd_buf->unique_id) {
			HFI_AD_DEBUG("matched response buf 0x%x to original 0x%x at ktime:%llu\n",
					cmd_buf->unique_id, buf_entry->unique_id, ktime_get());
			atomic_inc(&buf_entry->buffer_send_done);
		}
	}
	mutex_unlock(&ctx->host->hfi_adapter_cmd_buf_list_lock);

	return ret;
}

static int hfi_adapter_release_cmd_buf_no_lock(struct hfi_client_t *ctx,
		struct hfi_cmdbuf_t *cmd_buf)
{
	struct list_head *pos, *updated_pos;
	struct hfi_cmdbuf_t *buf_entry;
	struct hfi_core_cmds_buf_desc *buff_arr[MAX_BUFFERS];
	int i = 0;
	int rc = 0;

	if (!cmd_buf || !ctx) {
		HFI_AD_ERROR("invalid param\n");
		return -EINVAL;
	}

	if (cmd_buf->virtq_type == HFI_VIRTQUEUE_TYPE_MAX) {
		HFI_AD_ERROR("invalid virtqueue type %d\n", cmd_buf->virtq_type);
		return -EINVAL;
	}

	/* Release chained buffers */
	list_for_each_prev_safe(pos, updated_pos, &cmd_buf->cmd_buf_chain) {
		buf_entry = list_entry(pos, struct hfi_cmdbuf_t, node);
		list_del_init(pos);
		if (buf_entry->is_released || !buf_entry->buf.pbuf_vaddr)
			continue;
		buff_arr[i++] = &buf_entry->buf;
		if (buf_entry->pool) {
			_hfi_clear_buffer(buf_entry);
		}
	}
	list_del_init(&cmd_buf->cmd_buf_chain);

	/* Remove from client's buffer list */
	list_for_each_safe(pos, updated_pos, &cmd_buf->ctx->cmd_buf_list) {
		buf_entry = list_entry(pos, struct hfi_cmdbuf_t, node);
		if (buf_entry == cmd_buf) {
			HFI_AD_ERROR("releasing buffer incorrectly\n");
			list_del_init(pos);
			if (cmd_buf->is_released || !cmd_buf->buf.pbuf_vaddr)
				break;
			buff_arr[i++] = &buf_entry->buf;
			break;
		}
	}

	if (i == 0) {
		if (cmd_buf->is_released || !cmd_buf->buf.pbuf_vaddr)
			goto exit;
		buff_arr[i++] = &cmd_buf->buf;
	}

	HFI_AD_DEBUG("number of buffers to release = %d\n", i);
	if (cmd_buf->virtq_type == HFI_VIRTQUEUE_TYPE_RX)
		rc = hfi_core_release_rx_buffer(ctx->host->session, buff_arr, i);
	else if (cmd_buf->virtq_type == HFI_VIRTQUEUE_TYPE_TX)
		rc = hfi_core_release_tx_buffer(ctx->host->session, buff_arr, i);

	if (rc)
		HFI_AD_ERROR("failed to release rx buffer(s)\n");

exit:
	/* Free main buffer head */
	_hfi_clear_buffer(cmd_buf);
	list_del_init(&cmd_buf->node);

	return rc;
}

int hfi_adapter_release_cmd_buf(struct hfi_client_t *ctx, struct hfi_cmdbuf_t *cmd_buf)
{
	int rc = 0;

	if (!cmd_buf || !ctx) {
		HFI_AD_ERROR("invalid param\n");
		return -EINVAL;
	}

	if (cmd_buf->virtq_type == HFI_VIRTQUEUE_TYPE_MAX) {
		HFI_AD_ERROR("invalid virtqueue type %d\n", cmd_buf->virtq_type);
		return -EINVAL;
	}

	mutex_lock(&ctx->host->hfi_adapter_cmd_buf_list_lock);
	rc = hfi_adapter_release_cmd_buf_no_lock(ctx, cmd_buf);
	mutex_unlock(&ctx->host->hfi_adapter_cmd_buf_list_lock);

	return rc;
}

/**
 * hfi_adapter_flush_workers - Flush all pending work from HFI worker threads
 * @host: HFI adapter instance
 *
 * This function ensures all pending work is completed before proceeding
 * with adapter cleanup to avoid mutex deadlocks.
 */
static void hfi_adapter_flush_workers(struct hfi_adapter_t *host)
{
	if (!host) {
		HFI_AD_ERROR("invalid host pointer\n");
		return;
	}

	HFI_AD_DEBUG("flushing HFI worker threads\n");

	/* Set shutdown flag to prevent new work from being queued */
	atomic_set(&host->shutdown_in_progress, 1);

	/* Flush all pending work in the event worker */
	HFI_AD_DEBUG("flushing cb_event_worker\n");
	kthread_flush_worker(&host->cb_event_worker);

	/* Flush all pending work in the SSR worker */
	HFI_AD_DEBUG("flushing cb_event_ssr_worker\n");
	kthread_flush_worker(&host->cb_event_ssr_worker);

	HFI_AD_DEBUG("HFI worker flush complete\n");
}

void hfi_adapter_deinit(struct hfi_client_t *ctx)
{
	struct list_head *pos, *updated_pos;
	struct listener_list *listener_entry;
	struct hfi_cmdbuf_t *buf;
	int i = 0;

	if (!ctx || !ctx->host)
		return;

	HFI_AD_DEBUG("starting HFI adapter deinit for client %d\n", ctx->client_id);

	/* Flush all pending work to prevent mutex contention */
	hfi_adapter_flush_workers(ctx->host);

	mutex_lock(&ctx->host->hfi_adapter_cmd_buf_list_lock);
	if (!list_empty(&ctx->cmd_buf_list)) {
		list_for_each_safe(pos, updated_pos, &ctx->cmd_buf_list) {
			buf = list_entry(pos, struct hfi_cmdbuf_t, node);
			if (buf) {
				i++;
				_release_tx_buffers(buf);
			}
		}
	}
	mutex_unlock(&ctx->host->hfi_adapter_cmd_buf_list_lock);

	/* Clean up any remaining listeners to prevent memory leaks */
	mutex_lock(&ctx->listener_lock);
	list_for_each_safe(pos, updated_pos, &ctx->packet_listeners.list_ptr) {
		listener_entry = list_entry(pos, struct listener_list, list_ptr);
		if (listener_entry) {
			list_del(pos);
			kfree(listener_entry);
			i++;
		}
	}
	mutex_unlock(&ctx->listener_lock);

	HFI_AD_DEBUG("Freeing %d buffers and listeners on close\n", i);
}

int hfi_adapter_buffer_alloc(struct hfi_client_t *ctx, struct hfi_shared_addr_map *addr_map)
{
	int ret = 0;

	if (!ctx || !addr_map) {
		HFI_AD_ERROR("invalid client ctx: %d or add_map: %d\n", !ctx, !addr_map);
		return -EINVAL;
	}

	if (!addr_map->size) {
		HFI_AD_ERROR("failed to get shared buffer size\n");
		return -EINVAL;
	}
	addr_map->aligned_size = ALIGN(addr_map->size, HFI_CORE_IOMMU_MAP_SIZE_ALIGNMENT);

	ret = hfi_core_allocate_shared_mem(&addr_map->alloc_info, addr_map->aligned_size,
		HFI_CORE_DMA_ALLOC_UNCACHE, HFI_CORE_MMAP_READ | HFI_CORE_MMAP_WRITE);
	if (ret) {
		HFI_AD_ERROR("failed to allocate shared buffer, ret: %d\n", ret);
		return ret;
	}

	addr_map->remote_addr = addr_map->alloc_info.mapped_iova;
	addr_map->local_addr = addr_map->alloc_info.cpu_va;

	if (!addr_map->remote_addr || !addr_map->local_addr) {
		HFI_AD_ERROR("failed to allocate shared buffer\n");
		return -EINVAL;
	}

	return ret;
}

int hfi_adapter_buffer_dealloc(struct hfi_client_t *ctx, struct hfi_shared_addr_map *addr_map)
{
	struct hfi_core_mem_alloc_info *alloc_info = &addr_map->alloc_info;
	int ret = 0;

	if (!ctx) {
		HFI_AD_ERROR("invalid client\n");
		return -EINVAL;
	}

	if (!addr_map->size) {
		HFI_AD_DEBUG("empty buf\n");
		return ret;
	}

	if (!alloc_info->mapped_iova || !alloc_info->cpu_va) {
		HFI_AD_ERROR("failed to get buffer mapping info\n");
		return -EINVAL;
	}

	ret = hfi_core_deallocate_shared_mem(alloc_info);
	if (ret)
		HFI_AD_ERROR("failed to deallocate shared buffer, ret: %d\n", ret);

	alloc_info->mapped_iova = 0;
	alloc_info->cpu_va = NULL;

	return ret;
}

int hfi_adapter_map_sg_table(struct hfi_client_t *ctx, struct sg_table *sgt,
		struct hfi_shared_addr_map *addr_map)
{
	int ret = 0;

	if (!ctx || !sgt) {
		HFI_AD_ERROR("invalid client ctx: %d or sgt: %d\n", !ctx, !sgt);
		return -EINVAL;
	}

	addr_map->sgt = sgt;
	addr_map->aligned_size = ALIGN(addr_map->size, HFI_CORE_IOMMU_MAP_SIZE_ALIGNMENT);
	ret = hfi_core_map_sg_table(&addr_map->alloc_info, sgt, addr_map->aligned_size,
		HFI_CORE_MMAP_READ | HFI_CORE_MMAP_WRITE);
	if (ret) {
		HFI_AD_ERROR("failed to map sg table to iova, ret:%d\n", ret);
		return ret;
	}
	addr_map->remote_addr = addr_map->alloc_info.mapped_iova;

	return ret;
}

int hfi_adapter_unmap_sg_table(struct hfi_client_t *ctx, unsigned long iova, size_t size)
{
	int ret = 0;

	if (!ctx) {
		HFI_AD_ERROR("invalid client\n");
		return -EINVAL;
	}

	ret = hfi_core_unmap_iova(iova, size);
	if (ret) {
		HFI_AD_ERROR("failed to unmap iova, ret:%d\n", ret);
		return ret;
	}

	return ret;
}

size_t hfi_adapter_get_shared_mem_allocated_size(struct hfi_client_t *ctx,
		struct hfi_shared_addr_map *addr_map)
{
	if (!ctx) {
		HFI_AD_ERROR("invalid client\n");
		return 0;
	}

	if (addr_map == NULL) {
		HFI_AD_ERROR("Invalid parameter, addr_map is NULL\n");
		return 0;
	}

	return addr_map->alloc_info.size_allocated;
}

int hfi_adapter_map_iova(struct hfi_client_t *ctx, struct hfi_shared_addr_map *addr_map)
{
	int ret = 0;

	if (!ctx || !addr_map) {
		HFI_AD_ERROR("invalid client ctx: %d or addr_map: %d\n", !ctx, !addr_map);
		return -EINVAL;
	}

	ret = hfi_core_map_iova(&addr_map->alloc_info, HFI_CORE_MMAP_READ | HFI_CORE_MMAP_WRITE);
	if (ret) {
		HFI_AD_ERROR("failed to map iova, ret:%d\n", ret);
		return ret;
	}

	return ret;
}

int hfi_adapter_unmap_iova(struct hfi_client_t *ctx, unsigned long iova, size_t size)
{
	int ret = 0;

	if (!ctx) {
		HFI_AD_ERROR("invalid client\n");
		return -EINVAL;
	}

	ret = hfi_core_unmap_iova(iova, size);
	if (ret) {
		HFI_AD_ERROR("failed to unmap iova, ret:%d\n", ret);
		return ret;
	}

	return ret;
}

int hfi_adapter_release_all_cmd_bufs(struct hfi_client_t *client)
{
	struct list_head *pos, *updated_pos;
	struct hfi_cmdbuf_t *cmd_buf;
	struct hfi_adapter_t *host;
	int ret = 0;

	if (!client || !client->host) {
		HFI_AD_ERROR("invalid hfi_client\n");
		return -EINVAL;
	}

	HFI_AD_DEBUG("%s: client id: %d\n", __func__, client->client_id);
	host = client->host;

	/* Loop through command buffer list of the client */
	mutex_lock(&host->hfi_adapter_cmd_buf_list_lock);
	list_for_each_safe(pos, updated_pos, &client->cmd_buf_list) {
		cmd_buf = list_entry(pos, struct hfi_cmdbuf_t, node);
		if (!cmd_buf)
			continue;

		ret = hfi_adapter_release_cmd_buf_no_lock(client, cmd_buf);
		if (ret)
			HFI_AD_ERROR("failed to release cmd buf, ret: %d\n", ret);
		list_del_init(pos);
	}
	list_del_init(&client->cmd_buf_list);
	mutex_unlock(&host->hfi_adapter_cmd_buf_list_lock);

	return 0;
}

#endif /* IS_ENABLED(CONFIG_QTI_HFI_CORE)*/
