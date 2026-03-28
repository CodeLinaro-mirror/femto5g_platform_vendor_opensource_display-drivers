/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_CLIENT_H_
#define _DP_CLIENT_H_

#include "msm_drv.h"

#include <linux/types.h>
#include <drm/drm_mode.h>
#include <drm/sde_drm.h>

#if (KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE)
#include <drm/display/drm_dp_helper.h>
#else
#include <drm/drm_dp_helper.h>
#endif

#define MAX_DP_ACTIVE_DISPLAY 8
#define DP_STREAMS_MAX 2

enum dp_output_format {
	DP_OUTPUT_FORMAT_RGB,
	DP_OUTPUT_FORMAT_YCBCR420,
	DP_OUTPUT_FORMAT_YCBCR422,
	DP_OUTPUT_FORMAT_YCBCR444,
	DP_OUTPUT_FORMAT_INVALID,
};

struct dp_panel_info {
	u32 h_active;
	u32 v_active;
	u32 h_back_porch;
	u32 h_front_porch;
	u32 h_sync_width;
	u32 h_active_low;
	u32 v_back_porch;
	u32 v_front_porch;
	u32 v_sync_width;
	u32 v_active_low;
	u32 h_skew;
	u32 refresh_rate;
	u32 pixel_clk_khz;
	u32 bpp;
	int aspect_ratio;
	bool widebus_en;
	struct msm_compression_info comp_info;
	s64 dsc_overhead_fp;
	u32 pbn_no_overhead;
	u32 pbn;
};

struct dp_display_mode {
	struct dp_panel_info timing;
	struct dp_panel_info override_timing;
	u32 capabilities;
	s64 fec_overhead_fp;
	s64 dsc_overhead_fp;
	enum dp_output_format output_format;
	u32 lm_count;
	bool mst_hide;
	bool mode_override;
	bool widebus_en;
	u32 pclk_factor;
};

enum dp_drv_state {
	PM_DEFAULT,
	PM_SUSPEND,
};

struct dp_mst_drm_cbs {
	void (*hpd)(void *display, bool hpd_status);
	void (*hpd_irq)(void *display);
	void (*set_drv_state)(void *dp_drv,
			enum dp_drv_state mst_state);
	int (*set_mgr_state)(void *dp_drv, bool state);
	void (*set_mst_mode_params)(void *dp_drv, struct dp_display_mode *mode);
};

struct dp_mst_drm_install_info {
	void *dp_mst_prv_info;
	const struct dp_mst_drm_cbs *cbs;
};

struct dp_mst_caps {
	bool has_mst;
	u32 max_streams_supported;
	u32 max_dpcd_transaction_bytes;
	struct drm_dp_aux *drm_aux;
};

struct dp_intf_info {
	u32 cell_idx;
	u32 intf_idx[DP_STREAMS_MAX];
	u32 phy_idx;
	u32 stream_cnt;
};

struct dp_client {
	struct drm_device *drm_dev;
	struct dp_bridge *bridge;
	struct drm_connector *base_connector;
	bool is_sst_connected;
	bool is_mst_supported;
	bool is_edp;
	bool dsc_cont_pps;
	u32 max_pclk_khz;
	void *dp_mst_prv_info;
	u32 max_mixer_count;
	u32 max_dsc_count;
	void *dp_ipc_log;
	void *dp_aux_ipc_log;
	bool no_backlight_support;
	bool ext_hpd_en;
	bool ctl_op_sync;
	bool is_cont_splash_enabled;
	u32 streams; /* only used in HFI mode */
	struct drm_connector *connectors[DP_STREAMS_MAX]; /* only used in HFI mode */
	struct dp_bridge *bridges[DP_STREAMS_MAX]; /* only used in HFI mode */

	struct dp_client_drm_ops {
		int (*enable)(struct dp_client *client, int panel_id);

		int (*post_enable)(struct dp_client *client, int panel_id);

		int (*pre_disable)(struct dp_client *client, int panel_id);

		int (*disable)(struct dp_client *client, int panel_id);

		int (*set_mode)(struct dp_client *client, int panel_id,
				struct dp_display_mode *mode);

		enum drm_mode_status (*validate_mode)(struct dp_client *client,
				int panel_id, struct drm_display_mode *mode,
				const struct msm_resource_caps_info *avail_res);

		int (*get_modes)(struct dp_client *client, int panel_id,
			struct dp_display_mode *dp_mode);

		int (*prepare)(struct dp_client *client, int panel_id);

		int (*unprepare)(struct dp_client *client, int panel_id);

		int (*request_irq)(struct dp_client *client);

		void (*post_open)(struct dp_client *client);

		int (*config_hdr)(struct dp_client *client, int panel_id,
					struct drm_msm_ext_hdr_metadata *hdr_meta,
					bool dhdr_update);

		int (*set_colorspace)(struct dp_client *client, int panel_id,
					u32 colorspace);

		int (*post_init)(struct dp_client *client);

		int (*ctl_init)(struct dp_client *client);

		int (*set_stream_info)(struct dp_client *client, int panel_id,
				u32 strm_id, u32 start_slot, u32 num_slots, u32 pbn,
				int vcpi);

		void (*convert_to_dp_mode)(struct dp_client *client, int panel_id,
				const struct drm_display_mode *drm_mode,
				struct dp_display_mode *dp_mode);

		int (*update_pps)(struct dp_client *client,
				struct drm_connector *connector, char *pps_cmd);

		int (*get_available_dp_resources)(struct dp_client *client,
				const struct msm_resource_caps_info *avail_res,
				struct msm_resource_caps_info *max_dp_avail_res);

		void (*clear_reservation)(struct dp_client *client, int panel_id);

		int (*get_display_type)(struct dp_client *client,
				const char **display_type);

		int (*edp_detect)(struct dp_client *client);

		struct dp_display_mode *(*get_display_mode)(struct dp_client *client,
				int panel_id);
		int (*cont_splash_config)(struct dp_client *client);
		int (*cont_splash_disable)(struct dp_client *client);
		bool (*hpd_detect)(struct dp_client *client, int panel_id);
	} drm_ops;

	struct dp_client_mst_ops {
		int (*mst_install)(struct dp_client *client,
			struct dp_mst_drm_install_info *mst_install_info);

		int (*mst_uninstall)(struct dp_client *client);

		int (*mst_connector_install)(struct dp_client *client,
				struct drm_connector *connector);

		int (*mst_connector_uninstall)(struct dp_client *client,
				struct drm_connector *connector);

		int (*mst_connector_update_edid)(struct dp_client *client,
				struct drm_connector *connector,
				struct edid *edid);

		int (*mst_connector_update_link_info)(struct dp_client *client,
				struct drm_connector *connector);

		int (*mst_get_fixed_topology_port)(struct dp_client *client,
				u32 strm_id, u32 *port_num);

		int (*get_mst_caps)(struct dp_client *client,
				struct dp_mst_caps *mst_caps);

		void (*wakeup_phy_layer)(struct dp_client *client,
				bool wakeup);

		int (*get_mst_pbn_div)(struct dp_client *dp);

		int (*mst_get_fixed_topology_display_type)(struct dp_client *client,
				u32 strm_id, const char **display_type);
	} mst_ops;

	int (*bind)(struct device *dev, struct device *master,
		struct dp_client *client);

	void (*unbind)(struct device *dev, struct device *master,
		struct dp_client *client);

	struct dp_intf_info *(*get_intf_info)(struct dp_client *client);

	int (*pm_prepare)(struct dp_client *client);
	void (*pm_complete)(struct dp_client *client);
};

#endif /* _DP_CLIENT_H_ */
