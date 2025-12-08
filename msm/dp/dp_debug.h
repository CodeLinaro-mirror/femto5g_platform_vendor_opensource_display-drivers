/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DEBUG_H_
#define _DP_DEBUG_H_

#include "dp_drv.h"
#include "dp_debug_client.h"

#define DP_IPC_LOG(fmt, ...) \
	do {  \
		void *ipc_logging_context = get_ipc_log_context(); \
		ipc_log_string(ipc_logging_context, fmt, ##__VA_ARGS__); \
	} while (0)

#define DP_DEBUG(fmt, ...)                                                   \
	do {                                                                 \
		DP_IPC_LOG("[d][%-4d]"fmt, current->pid, ##__VA_ARGS__); \
		DP_DEBUG_V(fmt, ##__VA_ARGS__); \
	} while (0)

#define DP_INFO(fmt, ...)                                                   \
	do {                                                                 \
		DP_IPC_LOG("[i][%-4d]"fmt, current->pid, ##__VA_ARGS__); \
		DP_INFO_V(fmt, ##__VA_ARGS__); \
	} while (0)

#define DP_WARN(fmt, ...)                                                   \
	do {                                                                 \
		DP_IPC_LOG("[w][%-4d]"fmt, current->pid, ##__VA_ARGS__); \
		DP_WARN_V(fmt, ##__VA_ARGS__); \
	} while (0)

#define DP_ERR(fmt, ...)                                                   \
	do {                                                                 \
		DP_IPC_LOG("[e][%-4d]"fmt, current->pid, ##__VA_ARGS__); \
		DP_ERR_V(fmt, ##__VA_ARGS__); \
	} while (0)

#define DP_DEBUG_V(fmt, ...) \
	do { \
		if (drm_debug_enabled(DRM_UT_KMS))                        \
			DRM_DEBUG("[msm-dp-debug][%-4d]"fmt, current->pid,   \
					##__VA_ARGS__);                      \
		else                                                         \
			pr_debug("[drm:%s][msm-dp-debug][%-4d]"fmt, __func__,\
				       current->pid, ##__VA_ARGS__);         \
	} while (0)

#define DP_INFO_V(fmt, ...)                                                    \
	do {                                                                 \
		if (drm_debug_enabled(DRM_UT_KMS))                        \
			DRM_INFO("[msm-dp-info][%-4d]"fmt, current->pid,    \
					##__VA_ARGS__);                      \
		else                                                         \
			pr_info("[drm:%s][msm-dp-info][%-4d]"fmt, __func__, \
				       current->pid, ##__VA_ARGS__);         \
	} while (0)

#define DP_WARN_V(fmt, ...)                                    \
		pr_warn("[drm:%s][msm-dp-warn][%-4d]"fmt, __func__,  \
				current->pid, ##__VA_ARGS__)

#define DP_WARN_RATELIMITED_V(fmt, ...)                                    \
		pr_warn_ratelimited("[drm:%s][msm-dp-warn][%-4d]"fmt, __func__,  \
				current->pid, ##__VA_ARGS__)

#define DP_ERR_V(fmt, ...)                                    \
		pr_err("[drm:%s][msm-dp-err][%-4d]"fmt, __func__,   \
				current->pid, ##__VA_ARGS__)

#define DP_ERR_RATELIMITED_V(fmt, ...)                                    \
		pr_err_ratelimited("[drm:%s][msm-dp-err][%-4d]"fmt, __func__, \
				current->pid, ##__VA_ARGS__)

#define DEFAULT_DISCONNECT_DELAY_MS 0
#define MAX_DISCONNECT_DELAY_MS 10000
#define DEFAULT_CONNECT_NOTIFICATION_DELAY_MS 150
#define MAX_CONNECT_NOTIFICATION_DELAY_MS 5000

/**
 * dp_debug_get() - configure and get the DisplayPlot debug module data
 *
 * @dev: associated device
 *
 * This function sets up the debug module and provides a way
 * for debugfs input to be communicated with existing modules
 */
struct dp_debug_client *dp_debug_get(struct device *dev, u32 disp_op);

/**
 * dp_debug_put()
 *
 * Cleans up dp_debug instance
 *
 * @client: instance of dp_debug
 */
void dp_debug_put(struct dp_debug_client *client);
#endif /* _DP_DEBUG_H_ */
