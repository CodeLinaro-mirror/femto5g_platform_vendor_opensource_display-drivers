/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _HDMI_DRM_H_
#define _HDMI_DRM_H_

#include <linux/types.h>
#include <drm/drm_bridge.h>

#include "msm_drv.h"
#include "sde_connector.h"
#include "hdmi_display.h"
#include "hdmi_panel.h"

struct hdmi_bridge {
	struct drm_bridge base;
	u32 id;

	struct drm_connector *connector;
	struct hdmi_display *display;
	struct hdmi_display_mode hdmi_mode;
	void *hdmi_panel;

};

#if IS_ENABLED(CONFIG_DRM_MSM_HDMI)
/**
 * hdmi_connector_config_hdr - callback to configure HDR
 * @connector: Pointer to drm connector structure
 * @display: Pointer to private display handle
 * @c_state: connect state data
 * Returns: Zero on success
 */
int hdmi_connector_config_hdr(struct drm_connector *connector,
		void *display,
		struct sde_connector_state *c_state);

/**
 * hdmi_connector_atomic_check - callback to perform atomic
 * check for hdmi
 * @connector: Pointer to drm connector structure
 * @display: Pointer to private display handle
 * @c_state: connect state data
 * Returns: Zero on success
 */
int hdmi_connector_atomic_check(struct drm_connector *connector,
	void *display,
	struct drm_atomic_state *state);

/**
 * hdmi_connector_set_colorspace - callback to set new colorspace
 * @connector: Pointer to drm connector structure
 * @display: Pointer to private display handle
 * Returns: Zero on success
 */
int hdmi_connector_set_colorspace(struct drm_connector *connector,
	void *display);

/**
 * hdmi_connector_post_init - callback to perform additional initialization steps
 * @connector: Pointer to drm connector structure
 * @display: Pointer to private display handle
 * Returns: Zero on success
 */
int hdmi_connector_post_init(struct drm_connector *connector, void *display);

/**
 * hdmi_connector_detect - callback to determine if connector is connected
 * @connector: Pointer to drm connector structure
 * @force: Force detect setting from drm framework
 * @display: Pointer to private display handle
 * Returns: Connector 'is connected' status
 */
enum drm_connector_status hdmi_connector_detect(struct drm_connector *conn,
		bool force,
		void *display);

/**
 * hdmi_connector_get_modes - callback to add drm modes via drm_mode_probed_add()
 * @connector: Pointer to drm connector structure
 * @display: Pointer to private display handle
 * @avail_res: Pointer with curr available resources
 * Returns: Number of modes added
 */
int hdmi_connector_get_modes(struct drm_connector *connector,
		void *display, const struct msm_resource_caps_info *avail_res);

/**
 * hdmi_connector_mode_valid - callback to determine if specified mode is valid
 * @connector: Pointer to drm connector structure
 * @mode: Pointer to drm mode structure
 * @display: Pointer to private display handle
 * @avail_res: Pointer with curr available resources
 * Returns: Validity status for specified mode
 */
enum drm_mode_status hdmi_connector_mode_valid(struct drm_connector *connector,
		struct drm_display_mode *mode,
		void *display, const struct msm_resource_caps_info *avail_res);

/**
 * hdmi_connector_get_mode_info - retrieve information of the mode selected
 * @connector: Pointer to drm connector structure
 * @drm_mode: Display mode set for the display
 * @mode_info: Out parameter. Information of the mode
 * @sub_mode: Additional mode info to drm display mode
 * @display: Pointer to private display structure
 * @avail_res: Pointer with curr available resources
 * Returns: zero on success
 */
int hdmi_connector_get_mode_info(struct drm_connector *connector,
		const struct drm_display_mode *drm_mode,
		struct msm_sub_mode *sub_mode,
		struct msm_mode_info *mode_info,
		void *display, const struct msm_resource_caps_info *avail_res);

/**
 * hdmi_connector_get_info - retrieve connector display info
 * @connector: Pointer to drm connector structure
 * @info: Out parameter. Information of the connected display
 * @display: Pointer to private display structure
 * Returns: zero on success
 */
int hdmi_connector_get_info(struct drm_connector *connector,
		struct msm_display_info *info, void *display);

/**
 * hdmi_connector_post_open - handle the post open functionalities
 * @connector: Pointer to drm connector structure
 * @display: Pointer to private display structure
 */
void hdmi_connector_post_open(struct drm_connector *connector, void *display);

/**
 * hdmi_drm_bridge_init- drm hdmi bridge initialize
 * @display: Pointer to private display structure
 * @encoder: encoder for this hdmi bridge
 * @max_mixer_count: max available mixers for hdmi display
 * @max_dsc_count: max available dsc for hdmi display
 */

/**
 * hdmi_conn_set_info_blob - callback to perform info blob initialization
 * @connector: Pointer to drm connector structure
 * @info: Pointer to sde connector info structure
 * @display: Pointer to private display handle
 * @mode_info: Pointer to mode info structure
 * Returns: Zero on success
 */
int hdmi_connector_set_info_blob(struct drm_connector *connector,
		void *info, void *display, struct msm_mode_info *mode_info);

int hdmi_drm_bridge_init(void *display, struct drm_encoder *encoder,
	u32 max_mixer_count, u32 max_dsc_count);

void hdmi_drm_bridge_deinit(void *display);

/**
 * hdmi_convert_to_drm_mode - convert hdmi mode to drm mode
 * @hdmi_mode: Point to hdmi mode
 * @drm_mode: Pointer to drm mode
 */
void hdmi_convert_to_drm_mode(const struct hdmi_display_mode *hdmi_mode,
				struct drm_display_mode *drm_mode);

/**
 * hdmi_connector_update_pps - update pps for given connector
 * @hdmi_mode: Point to hdmi mode
 * @pps_cmd: PPS packet
 * @display: Pointer to private display structure
 */
int hdmi_connector_update_pps(struct drm_connector *connector,
		char *pps_cmd, void *display);

/**
 * hdmi_connector_install_properties - install drm properties
 * @display: Pointer to private display structure
 * @conn: Pointer to connector
 */
int hdmi_connector_install_properties(void *display,
		struct drm_connector *conn);

/**
 * hdmi_connector_add_custom_mode - add edid mode to connector
 * @conn: Pointer to connector
 * @hdmi_mode: Pointer to mode
 */
int hdmi_connector_add_custom_mode(struct drm_connector *conn, struct hdmi_display_mode *hdmi_mode);

#else
static inline int hdmi_connector_config_hdr(struct drm_connector *connector,
		void *display, struct sde_connector_state *c_state)
{
	return 0;
}

static inline int hdmi_connector_atomic_check(struct drm_connector *connector,
		void *display, struct drm_atomic_state *state)
{
	return 0;
}

static inline int hdmi_connector_set_colorspace(struct drm_connector *connector,
		void *display)
{
	return 0;
}

static inline int hdmi_connector_post_init(struct drm_connector *connector,
		void *display)
{
	return 0;
}

static inline enum drm_connector_status hdmi_connector_detect(
		struct drm_connector *conn,
		bool force,
		void *display)
{
	return 0;
}


static inline int hdmi_connector_get_modes(struct drm_connector *connector,
		void *display, const struct msm_resource_caps_info *avail_res)
{
	return 0;
}

static inline enum drm_mode_status hdmi_connector_mode_valid(
		struct drm_connector *connector,
		struct drm_display_mode *mode,
		void *display, const struct msm_resource_caps_info *avail_res)
{
	return MODE_OK;
}

static inline int hdmi_connector_get_mode_info(struct drm_connector *connector,
		const struct drm_display_mode *drm_mode,
		struct msm_sub_mode *sub_mode,
		struct msm_mode_info *mode_info,
		void *display, const struct msm_resource_caps_info *avail_res)
{
	return 0;
}

static inline int hdmi_connector_get_info(struct drm_connector *connector,
		struct msm_display_info *info, void *display)
{
	return 0;
}

static inline void hdmi_connector_post_open(struct drm_connector *connector,
		void *display)
{
}

static inline int hdmi_connector_set_info_blob(struct drm_connector *connector,
			void *info, void *display, struct msm_mode_info *mode_info)
{
	return 0;
}

static inline int hdmi_drm_bridge_init(void *display, struct drm_encoder *encoder,
		u32 max_mixer_count, u32 max_dsc_count)
{
	return 0;
}

static inline void hdmi_drm_bridge_deinit(void *display)
{
}

static inline void hdmi_convert_to_drm_mode(
		const struct hdmi_display_mode *hdmi_mode,
		struct drm_display_mode *drm_mode)
{
}

static inline int hdmi_connector_install_properties(void *display,
		struct drm_connector *conn)
{
	return 0;
}
#endif /* CONFIG_DRM_MSM_HDMI */

#endif /* _HDMI_DRM_H_ */
