/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DP_DEBUG_CLIENT_HFI_H_
#define _DP_DEBUG_CLIENT_HFI_H_

#include "dp_debug_client.h"
#include "dp_client.h"
#include "hfi_defs_device.h"

/* Response handling for HFI commands */
struct dp_hfi_response_data {
	u32 state;
	u32 bw_code;
	u32 mst_mode;
	u32 mst_state;
	u8 dpcd_data[256];
	u32 dpcd_size;
	u32 crc_data[6]; /* R, G, B for source and sink */
	bool connected;
	bool response_received;
	enum {
		HFI_RESPONSE_NONE,
		HFI_RESPONSE_BW_CODE,
		HFI_RESPONSE_MST_MODE,
		HFI_RESPONSE_DPCD,
		HFI_RESPONSE_CRC
	} response_type;
	struct completion response_complete;
	struct mutex response_lock;
};

struct dp_debug_client_hfi_priv {
	struct device *dev;
	struct hfi_client_t *hfi_client;
	struct dp_hfi_response_data response_data;
	struct hfi_prop_listener hfi_cb_obj;  /* HFI callback listener object */
	u32 hpd_pin_config;              /* Cached HPD pin config */
	u32 hpd_orientation;             /* Cached HPD orientation */
	struct hfi_shared_addr_map *dpcd_addr_map;
	struct hfi_shared_addr_map *edid_addr_map;
	struct hfi_shared_addr_map *info_addr_map;
};

/**
 * dp_debug_client_hfi_get() - get the HFI debug client instance
 *
 * @client: client structure to be filled with function pointers
 * return: error code
 *
 * This function initializes the HFI-based debug client operations.
 * The DP client is resolved dynamically through the connector chain when needed.
 */
int dp_debug_client_hfi_get(struct dp_debug_client *client);

/**
 * dp_debug_client_hfi_put() - release the HFI debug client instance
 * @client: pointer to dp_debug_client structure to be freed
 *
 * This function frees the memory allocated for the HFI debug client
 */
void dp_debug_client_hfi_put(struct dp_debug_client *client);

#endif /* _DP_DEBUG_CLIENT_HFI_H_ */
