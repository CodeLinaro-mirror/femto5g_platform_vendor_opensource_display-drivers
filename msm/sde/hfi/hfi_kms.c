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

static void hfi_kms_hw_destroy(struct sde_kms *kms)
{
	if (!kms)
		return;

	kvfree(kms->catalog->vig_formats);
	kvfree(kms->catalog->dma_formats);
	kvfree(kms->catalog->virt_vig_formats);
	kvfree(kms->catalog->virt_dma_formats);
	kvfree(kms->catalog->wb_formats);

	kvfree(kms->catalog);
}

static int hfi_kms_register_hfi_resources(struct hfi_kms *hfi_kms)
{
	struct hfi_resources_register *resources;
	struct msm_drm_private *priv;
	struct drm_device *ddev;
	struct msm_power_handle *phandle;
	struct hfi_cmdbuf_t *hfi_cmd_buf;
	struct sde_kms *sde_kms = &hfi_kms->base;
	struct dss_module_power *mp;
	int rc = 0;
	u32 count = 0;
	u32 bus_id;

	resources = kzalloc(sizeof(struct hfi_resources_register), GFP_KERNEL);
	if (!resources) {
		SDE_ERROR("failed to allocate resources\n");
		return ERR_PTR(-ENOMEM);
	}

	ddev = sde_kms->dev;
	priv = ddev->dev_private;
	phandle = &priv->phandle;
	mp = &phandle->mp;

	for (bus_id = 0; bus_id < MSM_POWER_HANDLE_DBUS_ID_MAX; bus_id++) {
		if (phandle->data_bus_handle[bus_id].data_paths_cnt) {
			resources->num_of_resources += 2;
			resources->resource_property_id[count++] =
				bw_resources_hfi_props[bus_id*2];
			resources->resource_property_id[count++] =
				bw_resources_hfi_props[bus_id*2+1];
		}
	}

	if (mp->num_vreg) {
		resources->num_of_resources++;
		resources->resource_property_id[count++] = HFI_PROPERTY_DEVICE_CORE_POWER_RAIL;
	}

	hfi_cmd_buf = hfi_adapter_get_cmd_buf(&hfi_kms->hfi_client,
			MSM_DRV_HFI_ID, HFI_CMDBUF_TYPE_DEVICE_INFO);

	hfi_kms->resource_vote_listener.hfi_prop_handler = &hfi_kms_resource_vote_hfi_prop_handler;

	rc = hfi_adapter_add_get_property(hfi_cmd_buf,
			HFI_COMMAND_DEVICE_RESOURCE_REGISTER,
			MSM_DRV_HFI_ID,
			HFI_PAYLOAD_TYPE_U64_ARRAY,
			resources,
			sizeof(*resources),
			&hfi_kms->resource_vote_listener,
			HFI_HOST_FLAGS_RESPONSE_REQUIRED | HFI_HOST_FLAGS_NON_DISCARDABLE);

	rc = hfi_adapter_set_cmd_buf(hfi_cmd_buf);

	return rc;
}

static u32 _hfi_kms_read_init_caps(struct sde_catalog_base *catalog,
		u32 hfi_prop, u32 *payload, u32 max_words)
{
	u32 read = 0;
	u32 prop_id = HFI_PROP_ID(hfi_prop);
	u32 payload_size = HFI_PROP_SZ(hfi_prop);
	struct hfi_catalog *hfi_catalog;
	u32 vig_index_count;
	u32 dma_index_count;

	if (!catalog || !payload)
		return -EINVAL;

	hfi_catalog = to_hfi_catalog(catalog);
	SDE_INFO("read prop:0x%x size:%d\n", prop_id, payload_size);
	switch (prop_id) {
	case HFI_PROPERTY_DEVICE_INIT_HFI_HW_VERSION:
		hfi_catalog->hw_rev = payload[read++];
		break;
	case HFI_PROPERTY_DEVICE_INIT_MDSS_HW_VERSION:
		catalog->hw_rev = payload[read++];
		break;
	case HFI_PROPERTY_DEVICE_INIT_HFI_FW_VERSION:
		hfi_catalog->fw_rev = payload[read++];
		break;
	case HFI_PROPERTY_DEVICE_INIT_VIG_INDICES:
		if (payload[0] > SDE_MAX_SSPP_COUNT) {
			SDE_ERROR("invalid vig indices in the packet\n");
			return read;
		}
		vig_index_count = payload[read++];
		for (int i = 0; i < vig_index_count; i++, read++)
			if (REC_ID(payload[read]) == 0) {
				catalog->vig_indices[catalog->vig_count] = payload[read];
				catalog->vig_count++;
			} else {
				catalog->vig_r1_indices[catalog->virt_vig_count] = payload[read];
				catalog->virt_vig_count++;
			}
		break;
	case HFI_PROPERTY_DEVICE_INIT_DMA_INDICES:
		if (!max_words || max_words <=  payload[0] || payload[0] > SDE_MAX_SSPP_COUNT) {
			SDE_ERROR("invalid dma indices in the packet\n");
			return read;
		}

		dma_index_count = payload[read++];
		for (int i = 0; i < dma_index_count; i++, read++)
			if (REC_ID(payload[read]) == 0) {
				catalog->dma_indices[catalog->dma_count] = payload[read];
				catalog->dma_count++;
			} else {
				catalog->dma_r1_indices[catalog->virt_dma_count] = payload[read];
				catalog->virt_dma_count++;
			}
		return read;
	case HFI_PROPERTY_DEVICE_INIT_MAX_DISPLAY_COUNT:
		catalog->max_display_count = payload[read++];
		break;
	case HFI_PROPERTY_DEVICE_INIT_WB_INDICES:
		if (payload[0] > MAX_BLOCKS) {
			SDE_ERROR("invalid wb indices in the packet\n");
			return read;
		}

		catalog->wb_count = payload[read++];
		for (int i = 0; i < catalog->wb_count; i++, read++)
			catalog->wb_indices[i] = payload[read];
		break;
	case HFI_PROPERTY_DEVICE_INIT_MAX_WB_LINEAR_RESOLUTION:
		catalog->max_wb_linear_resolution = payload[read++];
		break;
	case HFI_PROPERTY_DEVICE_INIT_MAX_WB_UBWC_RESOLUTION:
		catalog->max_wb_ubwc_resolution = payload[read++];
		break;
	case HFI_PROPERTY_DEVICE_INIT_DSI_INDICES:
		if (payload[0] > MAX_BLOCKS) {
			SDE_ERROR("invalid dsi indices in the packet\n");
			return read;
		}

		catalog->dsi_count = payload[read++];
		for (int i = 0; i < catalog->dsi_count; i++, read++)
			catalog->dsi_indices[i] = payload[read];
		break;
	case HFI_PROPERTY_DEVICE_INIT_MAX_DSI_RESOLUTION:
		catalog->max_dsi_resolution = payload[read++];
		break;
	case HFI_PROPERTY_DEVICE_INIT_DP_INDICES:
		if (payload[0] > MAX_BLOCKS) {
			SDE_ERROR("invalid dp indices in the packet\n");
			return read;
		}

		catalog->dp_count = payload[read++];
		for (int i = 0; i < catalog->dp_count; i++, read++)
			catalog->dp_indices[i] = payload[read];
		break;
	case HFI_PROPERTY_DEVICE_INIT_MAX_DP_RESOLUTION:
		catalog->max_dp_resolution = payload[read++];
		break;
	case HFI_PROPERTY_DEVICE_INIT_DS_INDICES:
		if (payload[0] > MAX_BLOCKS) {
			SDE_ERROR("invalid ds indices in the packet\n");
			return read;
		}

		catalog->ds_count = payload[read++];
		for (int i = 0; i < catalog->ds_count; i++, read++)
			catalog->ds_indices[i] = payload[read];
		return read;
	case HFI_PROPERTY_DEVICE_INIT_MAX_DS_RESOLUTION:
		catalog->max_ds_resolution = payload[read++];
		break;
	default:
		SDE_DEBUG("Unknown device cap key (%u)\n", prop_id);
	}

	if (read > payload_size) {
		SDE_ERROR("mismatch in read vs payload_size, prop:0x%x read:%d payload_size:%d\n",
				prop_id, read, payload_size);
	}
	return payload_size;
}

static int _hfi_kms_init_device_caps(struct sde_catalog_base *catalog,
		void *payload, u32 size)
{
	int ret;
	u32 *value_ptr;
	u32 prop, max_words, last_read = 0;
	struct hfi_util_kv_parser kv_parser;

	ret  = hfi_util_kv_parser_init(&kv_parser, size, payload);
	if (ret) {
		SDE_ERROR("failed to get int prop parser\n");
		return ret;
	}

	ret = hfi_util_kv_parser_get_next(&kv_parser, last_read, &prop, &value_ptr, &max_words);
	if (ret) {
		SDE_ERROR("failed to get next prop\n");
		return ret;
	}

	SDE_INFO("prop:0x%x, max_words:%d\n", prop, max_words);

	while (prop && payload) {
		last_read = _hfi_kms_read_init_caps(catalog, prop, value_ptr, max_words);
		if (!last_read) {
			SDE_ERROR("failed to get next prop\n");
			return ret;
		}

		ret = hfi_util_kv_parser_get_next(&kv_parser, last_read, &prop,
				&value_ptr, &max_words);
		if (ret) {
			SDE_ERROR("failed to get next prop\n");
			return ret;
		}
	}
	return ret;
}

static int _hfi_kms_copy_formats(struct sde_format_extended *sde_fmts,
		u32 *hfi_fmts, u32 num_fmts)
{
	int i = 0;

	if (!hfi_fmts || !sde_fmts)
		return i;

	for (i = 0; i < num_fmts; i++) {
		sde_fmts[i] = hfi_get_extened_format(hfi_fmts[i]);
		SDE_INFO("format :%d is 0x%x\n", i, hfi_fmts[i]);
	}

	return i;
}

static u32 _hfi_kms_read_vig_props(struct sde_catalog_base *catalog,
		u32 hfi_prop, u32 *payload, u32 max_words)
{
	u32 read = 0;
	u32 prop_id =  HFI_PROP_ID(hfi_prop);

	if (!max_words || max_words <= payload[0]) {
		SDE_ERROR("invalid packet max_words (%d) for prop id (%u)\n",
				max_words, prop_id);
		return 0;
	}

	// currently no VIG specific properties within HFI headers
	switch (prop_id) {
	default:
		SDE_ERROR("Unknown device cap key (%u)\n", prop_id);
	}

	return read;
}

static int _hfi_kms_init_vig_caps(struct sde_catalog_base *catalog,
		u32 *payload, u32 size)
{
	int ret, read = 0;
	u32 *value_ptr;
	u32 prop, max_words, last_read = 0;
	struct hfi_util_kv_parser kv_parser;
	u32 num_vig_fmts, num_props, i;

	if (!catalog || !payload)
		return -EINVAL;

	if (!size || !payload[0]) {
		SDE_ERROR("invalid or empty caps\n");
		return -EINVAL;
	} else if (size < MIN_BYTES_FOR_PIPE_CAPS(payload)) {
		SDE_ERROR("invalid command size for caps size:%d num_fmts:%d",  size, payload[0]);
		return -EINVAL;
	}

	SDE_DEBUG("size:%d payload:%pK, paload[0]:%d\n", size, payload, payload[0]);

	num_vig_fmts = payload[read++];
	catalog->vig_formats = kcalloc(num_vig_fmts,
			sizeof(struct sde_format_extended), GFP_KERNEL);
	read += _hfi_kms_copy_formats(catalog->vig_formats, &payload[read], num_vig_fmts);

	for (i = 0; i < catalog->vig_count; i++) {
		catalog->sspp_info[i].format_list = catalog->vig_formats;
		catalog->sspp_info[i].fmt_count = num_vig_fmts;
		catalog->sspp_info[i].sspp_type = SSPP_TYPE_VIG;
		catalog->sspp_info[i].obj_id = catalog->vig_indices[i];
	}

	num_props =  payload[read++];
	if ((size - DWORDS_TO_BYTES(read)) < MIN_BYTES_FOR_COMMON_CAPS(num_props)) {
		SDE_ERROR("remaining size:%d invalid for num_props:%d\n",
				size - DWORDS_TO_BYTES(read), num_props);
		return -EINVAL;
	}

	if (!num_props)
		return 0;

	ret  = hfi_util_kv_parser_init(&kv_parser, size - DWORDS_TO_BYTES(read), &payload[read]);
	if (ret) {
		SDE_ERROR("failed to get kv parser\n");
		return ret;
	}

	for (i = 0; i < num_props; i++) {
		ret = hfi_util_kv_parser_get_next(&kv_parser, last_read, &prop,
				&value_ptr, &max_words);
		if (ret) {
			SDE_ERROR("failed to get prop index:%d\n", i);
			return ret;
		}

		last_read = _hfi_kms_read_vig_props(catalog, prop, value_ptr, max_words);
	}

	return ret;
}

static u32 _hfi_kms_read_dma_props(struct sde_catalog_base *catalog,
		u32 hfi_prop, u32 *payload, u32 max_words)
{
	u32 read = 0;
	u32 prop_id =  HFI_PROP_ID(hfi_prop);

	if (!max_words || max_words <= payload[0]) {
		SDE_ERROR("invalid packet max_words (%d) for prop id (%u)\n",
				max_words, prop_id);
		return 0;
	}

	/* currently no DMA specific properties within HFI headers */
	switch (prop_id) {
	default:
		SDE_ERROR("Unknown device cap key (%u)\n", prop_id);
	}

	return read;
}

static int _hfi_kms_init_dma_caps(struct sde_catalog_base *catalog,
		u32 *payload, u32 size)
{
	int ret, read = 0;
	u32 *value_ptr;
	u32 prop, max_words, last_read = 0;
	struct hfi_util_kv_parser kv_parser;
	u32 num_dma_fmts, num_props, i;

	if (!catalog || !payload)
		return -EINVAL;

	if (!size || !payload[0]) {
		SDE_ERROR("invalid or empty caps\n");
		return -EINVAL;
	} else if (size < MIN_BYTES_FOR_PIPE_CAPS(payload)) {
		SDE_ERROR("invalid command size for caps size:%d num_fmts:%d",  size, payload[0]);
		return -EINVAL;
	}

	SDE_DEBUG("size:%d payload:%pK, paload[0]:%d\n", size, payload, payload[0]);

	num_dma_fmts = payload[read++];
	catalog->dma_formats = kcalloc(num_dma_fmts,
			sizeof(struct sde_format_extended), GFP_KERNEL);
	read += _hfi_kms_copy_formats(catalog->dma_formats, &payload[read], num_dma_fmts);

	for (i = 0; i < catalog->dma_count; i++) {
		catalog->sspp_info[i + catalog->vig_count].format_list = catalog->dma_formats;
		catalog->sspp_info[i + catalog->vig_count].fmt_count = num_dma_fmts;
		catalog->sspp_info[i + catalog->vig_count].sspp_type = SSPP_TYPE_DMA;
		catalog->sspp_info[i + catalog->vig_count].obj_id = catalog->dma_indices[i];
	}

	num_props =  payload[read++];
	if ((size - DWORDS_TO_BYTES(read)) < MIN_BYTES_FOR_COMMON_CAPS(num_props)) {
		SDE_ERROR("remaining size:%d invalid for num_props:%d\n",
				size - DWORDS_TO_BYTES(read), num_props);
		return -EINVAL;
	}

	if (!num_props)
		return 0;

	ret  = hfi_util_kv_parser_init(&kv_parser, size - DWORDS_TO_BYTES(read), &payload[read]);
	if (ret) {
		SDE_ERROR("failed to get kv parser\n");
		return ret;
	}

	for (i = 0; i < num_props; i++) {
		ret = hfi_util_kv_parser_get_next(&kv_parser, last_read, &prop,
				&value_ptr, &max_words);
		if (ret) {
			SDE_ERROR("failed to get prop index:%d\n", i);
			return ret;
		}

		last_read = _hfi_kms_read_dma_props(catalog, prop, value_ptr, max_words);
	}

	return ret;
}

static u32 _hfi_kms_read_vig_r1_props(struct sde_catalog_base *catalog,
		u32 hfi_prop, u32 *payload, u32 max_words)
{
	u32 read = 0;
	u32 prop_id =  HFI_PROP_ID(hfi_prop);

	if (!max_words || max_words <= payload[0]) {
		SDE_ERROR("invalid packet max_words (%d) for prop id (%u)\n",
				max_words, prop_id);
		return 0;
	}

	return read;
}

static int _hfi_kms_init_vig_r1_caps(struct sde_catalog_base *catalog,
		u32 *payload, u32 size)
{
	int ret, read = 0;
	u32 *value_ptr;
	u32 prop, max_words, last_read = 0;
	struct hfi_util_kv_parser kv_parser;
	u32 num_vig_r1_fmts, num_props, i;
	u32 vig_r1_base;

	if (!catalog || !payload)
		return -EINVAL;

	if (!size || !payload[0]) {
		SDE_ERROR("invalid or empty caps\n");
		return -EINVAL;
	} else if (size < MIN_BYTES_FOR_PIPE_CAPS(payload)) {
		SDE_ERROR("invalid command size for caps size:%d num_fmts:%d",  size, payload[0]);
		return -EINVAL;
	}

	SDE_DEBUG("size:%d payload:%pK, paload[0]:%d\n", size, payload, payload[0]);

	num_vig_r1_fmts = payload[read++];
	catalog->virt_vig_formats = kcalloc(num_vig_r1_fmts,
			sizeof(struct sde_format_extended), GFP_KERNEL);
	read += _hfi_kms_copy_formats(catalog->virt_vig_formats, &payload[1], num_vig_r1_fmts);

	for (i = 0; i < catalog->vig_count; i++)
		catalog->sspp_info[i].virt_format_list = catalog->virt_vig_formats;

	vig_r1_base = catalog->vig_count + catalog->dma_count;
	for (i = 0; i < catalog->virt_vig_count; i++) {
		catalog->sspp_info[i + vig_r1_base].format_list = catalog->virt_vig_formats;
		catalog->sspp_info[i + vig_r1_base].fmt_count = num_vig_r1_fmts;
		catalog->sspp_info[i + vig_r1_base].sspp_type = SSPP_TYPE_VIG;
		catalog->sspp_info[i + vig_r1_base].obj_id = catalog->vig_r1_indices[i];
	}

	num_props =  payload[read++];
	if ((size - DWORDS_TO_BYTES(read)) < MIN_BYTES_FOR_COMMON_CAPS(num_props)) {
		SDE_ERROR("remaining size:%d invalid for num_props:%d\n",
				size - DWORDS_TO_BYTES(read), num_props);
		return -EINVAL;
	}

	if (!num_props)
		return 0;

	ret  = hfi_util_kv_parser_init(&kv_parser, size - DWORDS_TO_BYTES(read), &payload[read]);
	if (ret) {
		SDE_ERROR("failed to get kv parser\n");
		return ret;
	}

	for (i = 0; i < num_props; i++) {
		ret = hfi_util_kv_parser_get_next(&kv_parser, last_read, &prop,
				&value_ptr, &max_words);
		if (ret) {
			SDE_ERROR("failed to get prop index:%d\n", i);
			return ret;
		}

		last_read = _hfi_kms_read_vig_r1_props(catalog, prop, value_ptr, max_words);
	}

	return ret;
}

static u32 _hfi_kms_read_dma_r1_props(struct sde_catalog_base *catalog,
		u32 hfi_prop, u32 *payload, u32 max_words)
{
	u32 read = 0;
	u32 prop_id =  HFI_PROP_ID(hfi_prop);

	if (!max_words || max_words <= payload[0]) {
		SDE_ERROR("invalid packet max_words (%d) for prop id (%u)\n",
				max_words, prop_id);
		return 0;
	}

	return read;
}

static int _hfi_kms_init_dma_r1_caps(struct sde_catalog_base *catalog,
		u32 *payload, u32 size)
{
	int ret, read = 0;
	u32 *value_ptr;
	u32 prop, max_words, last_read = 0;
	struct hfi_util_kv_parser kv_parser;
	u32 num_dma_r1_fmts, num_props, i;
	u32 dma_r1_base;

	if (!catalog || !payload)
		return -EINVAL;

	if (!size || !payload[0]) {
		SDE_ERROR("invalid or empty caps\n");
		return -EINVAL;
	} else if (size < MIN_BYTES_FOR_PIPE_CAPS(payload)) {
		SDE_ERROR("invalid command size for caps size:%d num_fmts:%d",  size, payload[0]);
		return -EINVAL;
	}

	SDE_DEBUG("size:%d payload:%pK, paload[0]:%d\n", size, payload, payload[0]);

	num_dma_r1_fmts = payload[read++];
	catalog->virt_dma_formats = kcalloc(num_dma_r1_fmts,
			sizeof(struct sde_format_extended), GFP_KERNEL);
	read += _hfi_kms_copy_formats(catalog->virt_dma_formats, &payload[1], num_dma_r1_fmts);

	for (i = 0; i < catalog->dma_count; i++)
		catalog->sspp_info[i + catalog->vig_count].virt_format_list =
			catalog->virt_dma_formats;

	dma_r1_base = catalog->vig_count + catalog->dma_count + catalog->virt_vig_count;
	for (i = 0; i < catalog->virt_dma_count; i++) {
		catalog->sspp_info[i + dma_r1_base].format_list = catalog->virt_dma_formats;
		catalog->sspp_info[i + dma_r1_base].fmt_count = num_dma_r1_fmts;
		catalog->sspp_info[i + dma_r1_base].sspp_type = SSPP_TYPE_DMA;
		catalog->sspp_info[i + dma_r1_base].obj_id = catalog->dma_r1_indices[i];
	}

	num_props =  payload[read++];
	if ((size - DWORDS_TO_BYTES(read)) < MIN_BYTES_FOR_COMMON_CAPS(num_props)) {
		SDE_ERROR("remaining size:%d invalid for num_props:%d\n",
				size - DWORDS_TO_BYTES(read), num_props);
		return -EINVAL;
	}

	if (!num_props)
		return 0;

	ret  = hfi_util_kv_parser_init(&kv_parser, size - DWORDS_TO_BYTES(read), &payload[read]);
	if (ret) {
		SDE_ERROR("failed to get kv parser\n");
		return ret;
	}

	for (i = 0; i < num_props; i++) {
		ret = hfi_util_kv_parser_get_next(&kv_parser, last_read, &prop,
				&value_ptr, &max_words);
		if (ret) {
			SDE_ERROR("failed to get prop index:%d\n", i);
			return ret;
		}

		last_read = _hfi_kms_read_dma_r1_props(catalog, prop, value_ptr, max_words);
	}

	return ret;
}

static u32 _hfi_kms_read_layer_caps(struct sde_catalog_base *catalog,
		u32 hfi_prop, u32 *payload, u32 max_words)
{
	u32 read = 0;
	u32 prop_id = HFI_PROP_ID(hfi_prop);

	switch (prop_id) {
	case HFI_PROPERTY_LAYER_FEATURE_DECIMATION:
		if (payload[read++])
			set_bit(SDE_FEATURE_DECIMATION, catalog->features);
		break;
	default:
		SDE_INFO("unknown device cap key (%u)\n", prop_id);
	}

	return read;
}

static int _hfi_kms_init_layer_caps(struct sde_catalog_base *catalog,
		u32 *payload, u32 size)
{
	int i, ret;
	u32 *value_ptr;
	u32 prop, max_words, last_read = 0;
	u32 num_props, read = 0;
	struct hfi_util_kv_parser kv_parser;

	if (!catalog)
		return -EINVAL;

	if (!size || !payload) {
		SDE_ERROR("invalid payload or size\n");
		return -EINVAL;
	} else if (!payload[0]) {
		SDE_INFO("empty payload\n");
		return 0;
	}

	num_props =  payload[read++];
	if (!num_props)
		return 0;

	if ((size - DWORDS_TO_BYTES(read)) < MIN_BYTES_FOR_COMMON_CAPS(num_props)) {
		SDE_ERROR("remaining size:%d invalid for num_props:%d\n",
				size - DWORDS_TO_BYTES(read), num_props);
		return -EINVAL;
	}

	ret  = hfi_util_kv_parser_init(&kv_parser, size - DWORDS_TO_BYTES(read), &payload[read]);
	if (ret) {
		SDE_ERROR("failed to get kv parser\n");
		return ret;
	}

	for (i = 0; i < num_props; i++) {
		ret = hfi_util_kv_parser_get_next(&kv_parser, last_read, &prop,
				&value_ptr, &max_words);
		if (ret) {
			SDE_ERROR("failed to get prop index:%d\n", i);
			return ret;
		}

		last_read = _hfi_kms_read_layer_caps(catalog, prop, value_ptr, max_words);
	}

	return ret;
}

static u32 _hfi_kms_read_disp_caps(struct sde_catalog_base *catalog,
		u32 hfi_prop, u32 *payload, u32 max_words)
{
	u32 read = 0;
	u32 prop_id = HFI_PROP_ID(hfi_prop);
	u32 prop_sz = HFI_PROP_SZ(hfi_prop);
	bool feature_enabled;
	enum sde_mdss_features feature;

	SDE_DEBUG("reading prop_id:0x%x prop_sz:%d\n", prop_id, prop_sz);

	switch (prop_id) {
	case HFI_PROPERTY_DISPLAY_HAS_DIM_LAYER:
		feature = SDE_FEATURE_DIM_LAYER;
		break;
	case HFI_PROPERTY_DISPLAY_HAS_IDLE_PC:
		feature = SDE_FEATURE_IDLE_PC;
		break;
	case HFI_PROPERTY_DISPLAY_NOISE_VERSION:
		catalog->noise_rev = payload[read++];
		return read;
	case HFI_PROPERTY_DISPLAY_FEATURE_DEDICATED_CWB:
		feature = SDE_FEATURE_DEDICATED_CWB;
		break;
	case HFI_PROPERTY_DISPLAY_FEATURE_CWB:
		feature = SDE_FEATURE_CWB;
		break;
	case HFI_PROPERTY_DISPLAY_FEATURE_TRUSTED_DISPLAY_MODE:
		feature = SDE_FEATURE_TRUSTED_VM;
		break;
	case HFI_PROPERTY_DISPLAY_UBWC_STATS:
		feature = SDE_FEATURE_UBWC_STATS;
		break;
	case HFI_PROPERTY_DISPLAY_QSYNC:
		feature = SDE_FEATURE_QSYNC;
		break;
	case HFI_PROPERTY_DISPLAY_AVR_STEP:
		feature = SDE_FEATURE_AVR_STEP;
		break;
	default:
		SDE_INFO("unknown device capability property 0x%x\n", prop_id);
		break;
	}

	feature_enabled = payload[read++];
	if (feature_enabled)
		set_bit(feature, catalog->features);

	if (prop_sz < read) {
		SDE_ERROR("mismatch in prop size prop_id:0x%x prop_sz:%d read:%d",
			       prop_id, prop_sz, read);
	}

	return prop_sz;
}

static int _hfi_kms_init_disp_caps(struct sde_catalog_base *catalog,
		u32 *payload, u32 size)
{
	int i, ret;
	u32 *value_ptr;
	u32 num_props, prop, max_words, last_read = 0;
	u32 read = 0;
	struct hfi_util_kv_parser kv_parser;

	if (!payload || !size) {
		SDE_ERROR("invalid payload for display caps\n");
		return -EINVAL;
	}

	num_props =  payload[read++];
	if (!num_props)
		return 0;

	if ((size - DWORDS_TO_BYTES(read)) < MIN_BYTES_FOR_COMMON_CAPS(num_props)) {
		SDE_ERROR("remaining size:%d invalid for num_props:%d\n",
				size - DWORDS_TO_BYTES(read), num_props);
		return -EINVAL;
	}

	ret  = hfi_util_kv_parser_init(&kv_parser, size - DWORDS_TO_BYTES(read), &payload[read]);
	if (ret) {
		SDE_ERROR("failed to get kv parser\n");
		return ret;
	}

	for (i = 0; i < num_props; i++) {
		ret = hfi_util_kv_parser_get_next(&kv_parser, last_read, &prop,
				&value_ptr, &max_words);
		if (ret) {
			SDE_ERROR("failed to get next prop :%d\n", i);
			return ret;
		}

		last_read = _hfi_kms_read_disp_caps(catalog, prop, value_ptr, max_words);
	}

	return ret;
}

static u32 _hfi_kms_read_wb_props(struct sde_catalog_base *catalog,
		u32 hfi_prop, u32 *payload, u32 max_words)
{
	u32 read = 0;
	u32 prop_id = hfi_prop & 0xFFFF;

	if (!max_words || max_words <= payload[0]) {
		SDE_ERROR("invalid packet max_words (%d) for prop id (%u)\n",
				max_words, prop_id);
		return 0;
	}

	switch (prop_id) {
	default:
		SDE_ERROR("Unknown device cap key (%u)\n", prop_id);
	}

	return read;
}

static int _hfi_kms_init_wb_caps(struct sde_catalog_base *catalog,
		u32 *payload, u32 size)
{
	int i, ret;
	u32 *value_ptr;
	u32 prop, max_words, last_read = 0;
	struct hfi_util_kv_parser kv_parser;
	u32 num_wb_fmts = *payload++;

	catalog->vig_formats = kcalloc(num_wb_fmts,
			sizeof(struct sde_format_extended), GFP_KERNEL);
	i = _hfi_kms_copy_formats(catalog->vig_formats, payload, num_wb_fmts);
	payload += i;

	ret  = hfi_util_kv_parser_init(&kv_parser, size - i, payload);
	if (ret) {
		SDE_ERROR("failed to get kv parser\n");
		return ret;
	}

	ret = hfi_util_kv_parser_get_next(&kv_parser, last_read, &prop,
			&value_ptr, &max_words);
	if (ret)
		goto err;

	while (prop && payload) {
		last_read = _hfi_kms_read_wb_props(catalog, prop,
				value_ptr, max_words);

		ret = hfi_util_kv_parser_get_next(&kv_parser, last_read, &prop,
				&value_ptr, &max_words);
		if (ret)
			goto err;
	}

	return ret;
err:
	SDE_ERROR("failed to get next prop\n");
	return ret;
}

static void _hfi_kms_set_defaults(struct sde_catalog_base *cat)
{
	if (!cat)
		return;

	SDE_INFO("set_defaults cat:%pK\n", cat);

	cat->min_display_width = 0;
	cat->min_display_height = 0;
	cat->max_display_width = 4096;
	cat->max_display_height = 4096;
}

static void hfi_kms_populate_catalog(u32 display_id, u32 cmd_id,
		void *prop_data, u32 size, struct hfi_prop_listener *hfi_listener)
{
	struct hfi_kms *hfi_kms = container_of(hfi_listener, struct hfi_kms, device_init_listener);
	struct sde_catalog_base *catalog;

	if (!hfi_kms) {
		SDE_ERROR("invalid object or listener from FW\n");
		return;
	}

	catalog = hfi_kms->base.catalog;
	if (!catalog) {
		SDE_ERROR("Catalog not yet initialized\n");
		return;
	}

	switch (cmd_id) {
	case HFI_COMMAND_DEVICE_INIT:
		 _hfi_kms_init_device_caps(catalog, prop_data, size);
		break;
	case HFI_COMMAND_DEVICE_INIT_DEVICE_CAPS:
		_hfi_kms_init_device_caps(catalog, prop_data, size);
		break;
	case HFI_COMMAND_DEVICE_INIT_VIG_CAPS:
		_hfi_kms_init_vig_caps(catalog, prop_data, size);
		break;
	case HFI_COMMAND_DEVICE_INIT_DMA_CAPS:
		_hfi_kms_init_dma_caps(catalog, prop_data, size);
		break;
	case HFI_COMMAND_DEVICE_INIT_VIG_R1_CAPS:
		_hfi_kms_init_vig_r1_caps(catalog, prop_data, size);
		break;
	case HFI_COMMAND_DEVICE_INIT_DMA_R1_CAPS:
		_hfi_kms_init_dma_r1_caps(catalog, prop_data, size);
		break;
	case HFI_COMMAND_DEVICE_INIT_COMMON_LAYER_CAPS:
		_hfi_kms_init_layer_caps(catalog, prop_data, size);
		break;
	case HFI_COMMAND_DEVICE_INIT_DISPLAY_CAPS:
		_hfi_kms_init_disp_caps(catalog, prop_data, size);
		_hfi_kms_set_defaults(catalog);
		SDE_DEBUG("done with all commands signalling\n");
		atomic_inc(&hfi_kms->cat_init_done);
		break;
	case HFI_COMMAND_DEVICE_INIT_DISPLAY_WB_CAPS:
		_hfi_kms_init_wb_caps(catalog, prop_data, size);
		break;
	default:
		SDE_ERROR("command:0x%x not supported\n", cmd_id);
	}
}

#define CATALOG_POLL_TRY_COUNT 100
#define CATALOG_POLL_STEP_US 1000

static int _send_device_init_cmd(struct hfi_kms *hfi_kms)
{
	int ret;
	struct hfi_cmdbuf_t *cmd_buf;
	bool cat_done = false;
	bool wait_count = false;

	if (!hfi_kms)
		return -EINVAL;

	cmd_buf = hfi_adapter_get_cmd_buf(&hfi_kms->hfi_client,
			MSM_DRV_HFI_ID, HFI_CMDBUF_TYPE_DEVICE_INFO);
	if (!cmd_buf) {
		SDE_ERROR("failed to get hfi command buffer\n");
		return -EINVAL;
	}

	hfi_kms->device_init_listener.hfi_prop_handler = hfi_kms_populate_catalog;
	ret = hfi_adapter_add_get_property(cmd_buf, HFI_COMMAND_DEVICE_INIT,
			MSM_DRV_HFI_ID, HFI_PAYLOAD_TYPE_NONE, NULL, 0,
			&hfi_kms->device_init_listener,
			HFI_HOST_FLAGS_RESPONSE_REQUIRED | HFI_HOST_FLAGS_NON_DISCARDABLE);
	if (ret) {
		SDE_ERROR("failed to add device-init command\n");
		return ret;
	}

	ret = hfi_adapter_set_cmd_buf(cmd_buf);
	if (ret) {
		SDE_ERROR("failed to send device-init command\n");
		return ret;
	}

	do {
		usleep_range(CATALOG_POLL_STEP_US, CATALOG_POLL_STEP_US + 10);
		if (wait_count++ > CATALOG_POLL_TRY_COUNT) {
			SDE_ERROR("error! catalog wait timed-out\n");
			return -EINVAL;
		}
		cat_done = atomic_read(&hfi_kms->cat_init_done);
	} while (!cat_done);

	SDE_DEBUG("catalog wait success after :%d ms\n", wait_count);
	return ret;
}

void _hfi_kms_set_default_hw_cfg(struct sde_catalog_base *catalog)
{
	if (!catalog)
		return;

	set_bit(SDE_FEATURE_10_BITS_COMPONENTS, catalog->features);
}

static int hfi_kms_hw_init(struct sde_kms *kms)
{
	int ret;
	struct hfi_kms *hfi_kms = to_hfi_kms(kms);

	if (!hfi_kms) {
		SDE_ERROR("invalid kms arg\n");
		return -EINVAL;
	}

	kms->catalog = kzalloc(sizeof(*(kms->catalog)), GFP_KERNEL);
	if (!kms->catalog) {
		SDE_ERROR("unable to allocate sde_catalog_base\n");
		return -ENOMEM;
	}

	_hfi_kms_set_default_hw_cfg(kms->catalog);

	ret = hfi_kms_register_hfi_resources(hfi_kms);
	if (ret) {
		SDE_ERROR("failed to register resources\n");
		return ret;
	}

	ret = _send_device_init_cmd(hfi_kms);
	if (ret) {
		SDE_ERROR("failed to send device-init\n");
		return ret;
	}

	ret = sde_kms_drm_obj_init(kms);
	if (ret) {
		SDE_ERROR("failed to perform drm obj init\n");
		return ret;
	}


	return ret;
}

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

				pending_commit_count = sde_encoder_inc_pending(encoder);
				MSM_EVT32(pending_commit_count);
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
	.destroy = hfi_kms_hw_destroy,
	.hw_init = hfi_kms_hw_init,
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

struct sde_kms *hfi_kms_init(struct drm_device *dev)
{
	int ret;
	struct hfi_kms *hfi_kms;
	struct msm_drm_hfi_private *hfi_drv_priv;

	if (!dev || !dev->dev_private)
		return NULL;

	hfi_drv_priv = dev->dev_private;

	hfi_kms = kzalloc(sizeof(*hfi_kms), GFP_KERNEL);
	if (!hfi_kms) {
		SDE_ERROR("failed to allocate hfi_kms\n");
		return ERR_PTR(-ENOMEM);
	}

	atomic_set(&hfi_kms->cat_init_done, 0);
	hfi_kms->base.hal_ops = hfi_hal_funcs;

	ret = _hfi_kms_setup_hfi(hfi_drv_priv->hfi_adapter, hfi_kms);
	if (ret) {
		SDE_ERROR("failed to setup HFI client ret=%d\n", ret);
		goto free_kms;
	}

	return &hfi_kms->base;

free_kms:
	kfree(hfi_kms);

	return NULL;

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
	struct msm_power_handle *phandle;
	u32 dirty[MSM_POWER_HANDLE_DBUS_ID_MAX] = {0,};
	u32 rc;
	u32 enable;

	if (!payload || !resource_vote_listener)
		return;

	hfi_kms = container_of(resource_vote_listener, struct hfi_kms, resource_vote_listener);

	sde_kms = &hfi_kms->base;
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
				bw_config.ib_vote[MSM_POWER_HANDLE_DBUS_ID_MNOC] =
					*(u64 *)kv_pairs[i].value_ptr;
				dirty[MSM_POWER_HANDLE_DBUS_ID_MNOC] = 1;
				break;
			case HFI_PROPERTY_DEVICE_CORE_AB:
				bw_config.ab_vote[MSM_POWER_HANDLE_DBUS_ID_MNOC] =
					*(u64 *)kv_pairs[i].value_ptr;
				dirty[MSM_POWER_HANDLE_DBUS_ID_MNOC] = 1;
				break;
			case HFI_PROPERTY_DEVICE_LLCC_IB:
				bw_config.ib_vote[MSM_POWER_HANDLE_DBUS_ID_LLCC] =
					*(u64 *)kv_pairs[i].value_ptr;
				dirty[MSM_POWER_HANDLE_DBUS_ID_LLCC] = 1;
				break;
			case HFI_PROPERTY_DEVICE_LLCC_AB:
				bw_config.ab_vote[MSM_POWER_HANDLE_DBUS_ID_LLCC] =
					*(u64 *)kv_pairs[i].value_ptr;
				dirty[MSM_POWER_HANDLE_DBUS_ID_LLCC] = 1;
				break;
			case HFI_PROPERTY_DEVICE_DRAM_IB:
				bw_config.ib_vote[MSM_POWER_HANDLE_DBUS_ID_EBI] =
					*(u64 *)kv_pairs[i].value_ptr;
				dirty[MSM_POWER_HANDLE_DBUS_ID_EBI] = 1;
				break;
			case HFI_PROPERTY_DEVICE_DRAM_AB:
				bw_config.ab_vote[MSM_POWER_HANDLE_DBUS_ID_EBI] =
					*(u64 *)kv_pairs[i].value_ptr;
				dirty[MSM_POWER_HANDLE_DBUS_ID_EBI] = 1;
				break;
			case HFI_PROPERTY_DEVICE_CORE_POWER_RAIL:
				enable = *(u32 *)kv_pairs[i].value_ptr;
				rc = msm_dss_enable_vreg(mp->vreg_config, mp->num_vreg,
						enable);
				break;
			}
		}

		for (bus_id = 0; bus_id < MSM_POWER_HANDLE_DBUS_ID_MAX; bus_id++) {
			if (dirty[bus_id]) {
				rc = msm_power_data_bus_set_quota(phandle,
						bus_id,
						bw_config.ab_vote[bus_id],
						bw_config.ib_vote[bus_id]);
				if (rc)
					pr_err("set quota failed\n");
			}
		}
	}
}
