// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/errno.h>

#include "hfi_utils.h"
#include "sde_kms.h"

#define HFI_PROP_SIZE_WORD_MASK    0xFFU
#define HFI_PROP_ID_MASK           0x00FFFFFFU
#define HFI_PACK_PROP_SIZE(x, y) \
	(((x) & HFI_PROP_ID_MASK) | ((((y) / sizeof(u32)) & HFI_PROP_SIZE_WORD_MASK) << 24))

struct hfi_util_kv_helper *hfi_util_kv_helper_alloc(u32 count)
{
	struct hfi_util_kv_helper *kv_helper;
	u32 size = sizeof(struct hfi_util_kv_helper) + (sizeof(struct hfi_kv_pairs) * count);

	if (size > HFI_UTIL_MAX_ALLOC) {
		SDE_INFO("exceeding limit - restricting to max size\n");
		size = HFI_UTIL_MAX_ALLOC;
	}

	kv_helper = kvzalloc(size, GFP_KERNEL);
	if (!kv_helper) {
		SDE_ERROR("error in alloc\n");
		return ERR_PTR(-ENOMEM);
	}

	kv_helper->cur = 0;
	kv_helper->max_size = size - sizeof(struct hfi_util_kv_helper);

	return kv_helper;
}

int hfi_util_kv_helper_reset(struct hfi_util_kv_helper *kv_helper)
{
	if (!kv_helper) {
		SDE_ERROR("invalid kv helper\n");
		return -EINVAL;
	}

	kv_helper->cur = 0;
	memset(kv_helper->kv_pairs, 0, kv_helper->max_size);

	return 0;
}

int hfi_util_kv_helper_add(struct hfi_util_kv_helper *kv_helper, u32 key, u32 *value)
{
	if (!kv_helper) {
		SDE_ERROR("invalid kv helper\n");
		return -EINVAL;
	}

	if (((kv_helper->cur + 1) * (sizeof(struct hfi_kv_pairs))) > kv_helper->max_size) {
		SDE_ERROR("overflow of kv helper memory\n");
		return -EINVAL;
	}

	kv_helper->kv_pairs[kv_helper->cur].key = key;
	kv_helper->kv_pairs[kv_helper->cur].value_ptr = value;
	kv_helper->cur++;

	return 0;
}

void *hfi_util_kv_helper_get_payload_addr(struct hfi_util_kv_helper *kv_helper)
{
	if (!kv_helper || !kv_helper->cur) {
		SDE_ERROR("invalid or empty kv helper\n");
		return NULL;
	}

	return &kv_helper->kv_pairs[0];
}

u32 hfi_util_kv_helper_get_count(struct hfi_util_kv_helper *kv_helper)
{
	if (!kv_helper) {
		SDE_ERROR("invalid kv helper\n");
		return 0;
	}

	return kv_helper->cur;
}


void hfi_util_kv_helper_dump(struct hfi_util_kv_helper *kv_helper)
{
	if (!kv_helper) {
		SDE_ERROR("invalid kv helper\n");
		return;
	}

	for (int i = 0; i < kv_helper->cur; i++)
		SDE_ERROR("info  - key[%d] =%d val=%p\n", i, kv_helper->kv_pairs[i].key,
				kv_helper->kv_pairs[i].value_ptr);

}

int hfi_util_kv_parser_init(struct hfi_util_kv_parser *kv_parser, u32 bytes, u32 *payload)
{
	if (!kv_parser || !payload || bytes > HFI_UTIL_MAX_ALLOC) {
		SDE_ERROR("invalid kv helper args max_size:%d payload:%pK kv_parser:%pK\n",
				bytes, payload, kv_parser);
		return -EINVAL;
	}

	if (bytes % (sizeof(u32))) {
		SDE_ERROR("unsipported align of size:%d, expecting u32/dword aligned", bytes);
		return -EINVAL;
	}

	kv_parser->max_index = bytes / (sizeof(u32));
	kv_parser->cur_offset = 0;
	kv_parser->payload = payload;

	return 0;
}

int hfi_util_kv_parser_get_next(struct hfi_util_kv_parser *kv_parser, u32 move,
		u32 *hfi_prop, u32 **payload, u32 *max_words)
{
	if (!kv_parser || !payload) {
		SDE_ERROR("invalid kv helper args\n");
		return -EINVAL;
	}

	kv_parser->cur_offset += move;
	*hfi_prop = 0;
	*payload = NULL;
	*max_words = 0;

	if (kv_parser->cur_offset ==  kv_parser->max_index) {
		/* enf of the memory parsing, bail out */
		return 0;
	}

	if (kv_parser->cur_offset >  kv_parser->max_index) {
		SDE_ERROR("invalid access attempt of kv_parser max_u32s:%d cur_index:%d\n",
				kv_parser->max_index, kv_parser->cur_offset);
		return -EINVAL;
	} else if ((kv_parser->max_index - kv_parser->cur_offset) < 2)  {
		SDE_ERROR("min two dwords required for next key value, max_u32s:%d cur_index:%d\n",
				kv_parser->max_index, kv_parser->cur_offset);
		return -EINVAL;
	}

	*hfi_prop = kv_parser->payload[kv_parser->cur_offset++];
	*payload = &(kv_parser->payload[kv_parser->cur_offset]);
	*max_words = kv_parser->max_index - kv_parser->cur_offset;

	return 0;
}

struct hfi_util_u32_prop_helper *hfi_util_u32_prop_helper_alloc(u32 size)
{
	struct hfi_util_u32_prop_helper *prop_helper;
	u32 max_sz = HFI_UTIL_MAX_ALLOC;
	u32 sz = (size + sizeof(struct hfi_util_u32_prop_helper));

	sz = min(sz, max_sz);

	prop_helper = kvzalloc(sz, GFP_KERNEL);
	if (!prop_helper) {
		SDE_ERROR("error in alloc\n");
		return ERR_PTR(-ENOMEM);
	}

	prop_helper->max_size = sz - sizeof(struct hfi_util_u32_prop_helper);
	prop_helper->cur = &prop_helper->prop_data[1];
	prop_helper->prop_count = 0;

	return prop_helper;
}

static inline int hfi_util_u32_prop_helper_validate(
	struct hfi_util_u32_prop_helper *prop_helper,
	u32 prop_id, enum hfi_util_prop_type type,
	const void *prop_value, u32 payload_sz,
	bool has_obj_id)
{
	u32 used_bytes = 0, need_bytes = 0;
	u32 total_payload_sz = 0, payload_words = 0;

	if (!prop_helper || !prop_helper->cur) {
		SDE_ERROR("invalid prop helper %pK, cur %pK, prop_id 0x%x\n",
			prop_helper, prop_helper->cur, prop_id);
		return -EINVAL;
	}

	if (!prop_value) {
		SDE_ERROR("invalid prop value for prop_id 0x%x\n", prop_id);
		return -EINVAL;
	}

	/*
	 * prop_id is packed into low 24 bits.
	 * If it exceeds 24 bits, it will overlap with size field.
	 */
	if (prop_id & ~HFI_PROP_ID_MASK) {
		SDE_ERROR("prop_id 0x%x out of range\n", prop_id);
		return -ERANGE;
	}

	switch (type) {
	case HFI_VAL_U32:
		if (payload_sz != sizeof(u32)) {
			SDE_ERROR("invalid u32 payload_sz %u, prop_id 0x%x\n", payload_sz, prop_id);
			return -EINVAL;
		}
		break;

	case HFI_VAL_U64:
		if (payload_sz != sizeof(u64)) {
			SDE_ERROR("invalid u64 payload_sz %u, prop_id 0x%x\n", payload_sz, prop_id);
			return -EINVAL;
		}
		break;

	case HFI_VAL_U32_ARRAY:
		if (payload_sz % sizeof(u32)) {
			SDE_ERROR("u32 array payload_sz %u invalid, prop_id 0x%x\n",
				payload_sz, prop_id);
			return -EINVAL;
		}
		break;

	default:
		SDE_ERROR("invalid data type %d for prop_id 0x%x\n", type, prop_id);
		return -EINVAL;
	}

	/*
	 * HFI_PACK_PROP_SIZE packs payload words into high 8 bits,
	 * so max is 255 u32 words = 1020 bytes.
	 */
	total_payload_sz = payload_sz + (has_obj_id ? sizeof(u32) : 0);
	payload_words = total_payload_sz / sizeof(u32);
	if (payload_words > HFI_PROP_SIZE_WORD_MASK) {
		SDE_ERROR("payload too large %u bytes (%u words), prop_id 0x%x\n",
			total_payload_sz, payload_words, prop_id);
		return -E2BIG;
	}

	used_bytes = hfi_util_u32_prop_helper_get_size(prop_helper);
	need_bytes = sizeof(u32) + total_payload_sz; /* header + payload */

	if (used_bytes > prop_helper->max_size ||
	    need_bytes > (prop_helper->max_size - used_bytes)) {
		SDE_ERROR("prop_helper memory is full: used=%u need=%u max=%u, prop_id 0x%x\n",
			used_bytes, need_bytes, prop_helper->max_size, prop_id);
		return -ENOSPC;
	}

	return 0;
}

int hfi_util_u32_prop_helper_add_prop(struct hfi_util_u32_prop_helper *prop_helper,
		u32 prop_id, enum hfi_util_prop_type type,
		const void *prop_value, u32 payload_sz)
{
	int ret = 0;

	ret = hfi_util_u32_prop_helper_validate(prop_helper, prop_id, type,
					prop_value, payload_sz, false);
	if (ret) {
		SDE_ERROR("add prop failed, ret %d type %d prop_id 0x%x\n", ret, type, prop_id);
		WARN_ON_ONCE(ret);
		return ret;
	}

	*prop_helper->cur = HFI_PACK_PROP_SIZE(prop_id, payload_sz);
	prop_helper->cur++;

	switch (type) {
	case HFI_VAL_U32:
		*(prop_helper->cur) = *((u32 *)prop_value);
		 prop_helper->cur++;
		break;
	case HFI_VAL_U64:
		memcpy(prop_helper->cur, prop_value, sizeof(u64));
		prop_helper->cur += (sizeof(u64) / sizeof(u32));
		break;
	case HFI_VAL_U32_ARRAY:
		memcpy(prop_helper->cur, prop_value, payload_sz);
		prop_helper->cur += (payload_sz / (sizeof(u32)));
		break;
	default:
		SDE_ERROR("invalid data type %d for prop_id 0x%x\n", type, prop_id);
		WARN_ON(1);
		return -EINVAL;
	}

	prop_helper->prop_count++;
	prop_helper->prop_data[0] = prop_helper->prop_count;

	return 0;
}

int hfi_util_u32_prop_helper_add_prop_by_obj(struct hfi_util_u32_prop_helper *prop_helper,
		u32 prop_id, u32 obj_id, enum hfi_util_prop_type type,
		const void *prop_value, u32 payload_sz)
{
	int ret = 0;
	u32 payload_size = 0;

	ret = hfi_util_u32_prop_helper_validate(prop_helper, prop_id, type,
					prop_value, payload_sz, true);
	if (ret) {
		SDE_ERROR("add prop failed, ret %d obj_id %u type %d prop_id 0x%x\n",
			ret, obj_id, type, prop_id);
		WARN_ON_ONCE(ret);
		return ret;
	}

	payload_size = sizeof(u32) + payload_sz; /* one u32 for object id */
	*prop_helper->cur = HFI_PACK_PROP_SIZE(prop_id, payload_size);
	prop_helper->cur++;

	*prop_helper->cur = obj_id;
	prop_helper->cur++;

	switch (type) {
	case HFI_VAL_U32:
		*(prop_helper->cur) = *((u32 *)prop_value);
		 prop_helper->cur++;
		break;
	case HFI_VAL_U64:
		memcpy(prop_helper->cur, prop_value, sizeof(u64));
		prop_helper->cur += (sizeof(u64) / sizeof(u32));
		break;
	case HFI_VAL_U32_ARRAY:
		memcpy(prop_helper->cur, prop_value, payload_sz);
		prop_helper->cur += (payload_sz / (sizeof(u32)));
		break;
	default:
		SDE_ERROR("invalid data type %d for prop_id 0x%x\n", type, prop_id);
		WARN_ON(1);
		return -EINVAL;
	}

	prop_helper->prop_count++;
	prop_helper->prop_data[0] = prop_helper->prop_count;

	return 0;
}

int hfi_util_u32_prop_helper_reset(struct hfi_util_u32_prop_helper *prop_helper)
{
	if (!prop_helper) {
		SDE_ERROR("invalid prop helper\n");
		return -EINVAL;
	}

	prop_helper->prop_count = 0;
	prop_helper->cur = &prop_helper->prop_data[1];
	memset(prop_helper->prop_data, 0, prop_helper->max_size);

	return 0;
}

u32 hfi_util_u32_prop_helper_get_size(struct hfi_util_u32_prop_helper *prop_helper)
{
	if (!prop_helper || !prop_helper->cur) {
		SDE_ERROR("invalid prop helper\n");
		return 0;
	}

	if (prop_helper->cur < prop_helper->prop_data) {
		SDE_ERROR("prop helper cur pointer underflow, cur %pK prop_data %pK\n",
			prop_helper->cur, prop_helper->prop_data);
		return 0;
	}

	return (u32)(sizeof(u32) * (prop_helper->cur - prop_helper->prop_data));
}

u32 hfi_util_u32_prop_helper_prop_count(struct hfi_util_u32_prop_helper *prop_helper)
{
	if (!prop_helper || !prop_helper->cur) {
		SDE_ERROR("invalid prop helper\n");
		return 0;
	} else {
		return prop_helper->prop_count;
	}
}

void *hfi_util_u32_prop_helper_get_payload_addr(struct hfi_util_u32_prop_helper *prop_helper)
{
	if (!prop_helper || !prop_helper->max_size) {
		SDE_ERROR("invalid prop helper\n");
		return NULL;
	} else {
		return prop_helper->prop_data;
	}
}
