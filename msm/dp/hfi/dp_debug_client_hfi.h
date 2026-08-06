/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _DP_DEBUG_CLIENT_HFI_H_
#define _DP_DEBUG_CLIENT_HFI_H_

#include "dp_debug_client.h"

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
