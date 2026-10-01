// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2017, 2020 The Linux Foundation. All rights reserved.
 */

#ifndef __SDE_ENCODER_H__
#define __SDE_ENCODER_H__

#include <drm/drm_crtc.h>
#include <drm/drm_bridge.h>
#include <linux/sde_rsc.h>

#include "msm_prop.h"
#include "sde_hw_mdss.h"
#include "sde_kms.h"
#include "sde_connector.h"
#include "sde_power_handle.h"

/*
 * Two to anticipate panels that can do cmd/vid dynamic switching
 * plan is to create all possible physical encoder types, and switch between
 * them at runtime
 */
#define NUM_PHYS_ENCODER_TYPES 2
#define MAX_PHYS_ENCODERS_PER_VIRTUAL \
	(MAX_H_TILES_PER_DISPLAY * NUM_PHYS_ENCODER_TYPES)
#define MAX_CHANNELS_PER_ENC 4

#define SDE_ENCODER_FRAME_EVENT_DONE			BIT(0)
#define SDE_ENCODER_FRAME_EVENT_ERROR			BIT(1)
#define SDE_ENCODER_FRAME_EVENT_PANEL_DEAD		BIT(2)
#define SDE_ENCODER_FRAME_EVENT_SIGNAL_RELEASE_FENCE	BIT(3)
#define SDE_ENCODER_FRAME_EVENT_SIGNAL_RETIRE_FENCE	BIT(4)
#define SDE_ENCODER_FRAME_EVENT_CWB_DONE		BIT(5)

#define IDLE_POWERCOLLAPSE_DURATION	(300)
#define IDLE_POWERCOLLAPSE_IN_EARLY_WAKEUP (200 - 16/2)
#define DEFAULT_TIMEOUT_FPS_THRESHOLD 24

struct sde_encoder_kickoff_params {
	unsigned long affected_displays;
	bool recovery_events_enabled;
	enum frame_trigger_mode_type frame_trigger_mode;
	bool update_dce_pp_mux;
};
struct sde_encoder_hw_resources {
	enum sde_intf_mode intfs[INTF_MAX];
	enum sde_intf_mode wbs[WB_MAX];
	bool needs_cdm;
	u32 display_num_of_h_tiles;
	enum sde_connector_display display_type;
	struct msm_display_topology topology;
	struct msm_compression_info *comp_info;
};
struct sde_crtc;
struct sde_crtc_state;
struct sde_connector_state;
struct sde_format;
struct sde_hw_mixer;
struct sde_hw_dim_layer;
struct sde_plane_state;
struct sde_rect;

struct sde_misr_values {
	u32 count;
	u32 misr_values[MAX_MISR_MODULES];
};
enum sde_enc_rc_states {
	SDE_ENC_RC_STATE_OFF,
	SDE_ENC_RC_STATE_PRE_OFF,
	SDE_ENC_RC_STATE_ON,
	SDE_ENC_RC_STATE_MODESET,
	SDE_ENC_RC_STATE_IDLE
};
struct sde_encoder_vrr_info {
	u32 frame_interval;
	struct msm_freq_step_pattern *curr_freq_pattern;
	u32 curr_idx;
	struct drm_atomic_state *current_state;
	struct sde_sim_arp_panel_mode {
	u32 arp_te_time_in_ms;
	u32 mode;
	} sim_arp_panel_mode;
	u32 debugfs_arp_te_in_ms;
	u32 *debugfs_freq_array;
	struct msm_debugfs_freq_pattern *debugfs_freq_pattern;
	u32 vhm_cmd_in_progress;
	atomic_t vhm_pm_wake_lock;
};
struct sde_encoder_ops {
	/**
	 * phys_init - phys initialization function
	 * @type: controller type
	 * @controller_id: controller id
	 * @phys_init_params: Pointer of structure sde_enc_phys_init_params
	 * Returns: Pointer of sde_encoder_phys, NULL if failed
	 */
	void *(*phys_init)(enum sde_intf_type type, u32 controller_id, void *phys_init_params);
};
struct sde_encoder_virt;
enum msm_disp_op {
	MSM_DISP_OP_HWIO,
	MSM_DISP_OP_HFI,
	MSM_DISP_OP_HYP,
	MSM_DISP_OP_MAX,
};
struct sde_encoder_hal_funcs {
	/**
	 * post_init - perform additional initialization steps
	 * @enc: Pointer to sde encoder structure
	 * Returns: Zero on success
	 */
	int (*post_init[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);

	/**
	 * destroy - Clean up encoder resources
	 * @enc: Pointer to sde encoder structure
	 * Returns: Zero on success, negative error code for failures
	 */
	void (*destroy[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);

	/**
	 * debugfs_init - perform debugfs node initialization
	 * @enc: Pointer to sdee encoder structure
	 * Returns: Zero on success
	 */
	int (*debugfs_init[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);

	/**
	 * debugfs_destroy - handle destroy operations for debugfs
	 * @enc: Pointer to sde encoder structure
	 * Returns: Zero on success
	 */
	void (*debugfs_destroy[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);

	/**
	 * mode_set - perform set mode operations
	 * @enc: Pointer to sde encoder structure
	 * @mode: Pointer to drm mode structure
	 * @mdj_ode: Pointer to adjusted drm mode structure
	 * Returns: Zero on success
	 */
	int (*mode_set[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc,
			struct drm_display_mode *mode, struct drm_display_mode *adj_mode);

	/**
	 * atomic_check - atomic check handling for encoder
	 * @enc: Pointer to sde encoder structure
	 * @crtc_state: Pointer to sde crtc state
	 * @conn_state:Pointer to sde connector state
	 * Returns: Zero on success
	 */
	int (*atomic_check[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc,
			struct sde_crtc_state *crtc_state, struct sde_connector_state *conn_state);

	/**
	 * encoder_enable - function for encoder enable
	 * @enc: Pointer to sde encoder structure
	 * Returns: Zero on success
	 */
	int (*encoder_enable[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);

	/**
	 * encoder_disable - function for encoder disable
	 * @enc: Pointer to sde encoder structure
	 * Returns: Zero on success
	 */
	int (*encoder_disable[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);

	/**
	 * prepare_commit - start of atomic commit sequence
	 * @enc: Pointer to sde encoder structure
	 * Returns: Zero on success
	 */
	int (*prepare_commit[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);

	/**
	 * @prepare_for_kickoff: Do any work necessary prior to a kickoff
	 * @enc: Pointer to sde encoder structure
	 * @params: info encoder requires at kickoff
	 * Returns: Zero on success
	 */
	int (*prepare_for_kickoff[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);

	/**
	 * @kickoff: kickoff processing for the encoder
	 * @enc: Pointer to sde encoder structure
	 * @cfg_changed: config change boolean
	 * Returns: Zero on success
	 */
	int (*kickoff[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc, bool cfg_changed);

	/**
	 * wait_for_event - Wait for hardware event
	 * @enc: Pointer to sde encoder structure
	 * @event: event for which response is needed
	 * Returns: Zero on success
	 */
	int (*wait_for_event[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc, u32 event);

	/**
	 * enable_hw_event - Notify display of event registration/unregistration
	 * @enc: Pointer to sde encoder structure
	 * @event: encoder event for which response is needed
	 * @enable: Whether the event is being enabled/disabled
	 */
	int (*enable_hw_event[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc,
		u32 event, bool enable);

	/**
	 * debugfs_misr_setup - Enable MISR for specified module and display
	 * @enc: Pointer to sde encoder structure
	 */
	int (*debugfs_misr_setup[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);

	/**
	 * debugfs_misr_read - Read MISR value for specified module and display
	 * @enc: Pointer to sde encoder structure
	 */
	int (*debugfs_misr_read[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);

	/**
	 * debugfs_dump_status - Read dump status for specified module and display
	 * @enc: Pointer to dpu encoder structure
	 * @s: used to writing on debugfs node
	 */
	int (*debugfs_dump_status[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc,
			struct seq_file *s);

	/**
	 * get_vblank_count - Get the current vblank counter value
	 * @enc: Pointer to sde encoder structure
	 * Returns: Counter value of last vblank event occurrence
	 */
	u32 (*get_vblank_count[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);

	/**
	 * get_vblank_timestamp - Get the last vblank timestamp
	 * @enc: Pointer to sde encoder structure
	 * Returns: timestamp of last known vblank event occurrence
	 */
	ktime_t (*get_vblank_timestamp[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);

	/**
	 * register_power_event_notify - Register for display power event notification
	 * @enc: Pointer to sde encoder structure
	 * @enable: Flag to indicate whether notification is being enabled/disabled
	 */
	int (*register_power_event_notify[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc,
			bool enable);
	/**
	 * early_wakeup_call - Command to exit idle power collapse with early wakeup hint
	 * @enc: Pointer to sde encoder structure
	 */
	int (*early_wakeup_call[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);

	/**
	 * idle_timer_immediate_expiry - Command FW to immediately expire the idle timer
	 * @enc: Pointer to sde encoder structure
	 */
	int (*idle_timer_immediate_expiry[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);

	/**
	 * register_panel_dead_event_notify - register panel dead event notification
	 * @enc: Pointer to sde encoder structure
	 * @enable: flag to regitser/deregister event.
	 * Returns: status of registration/deregistration.
	 */
	int (*register_panel_dead_event_notify[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc,
			bool enable);

	/**
	 * register_dcs_cmd_error_event_notify - register/unregister DCS command error
	 *                                       event notification from DCP firmware.
	 * @enc: Pointer to sde encoder structure
	 * @enable: flag to register/deregister event.
	 * Returns: status of registration/deregistration.
	 */
	int (*register_dcs_cmd_error_event_notify[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc,
			bool enable);

	/**
	 * misr_setup - Setup MISR module
	 * @enc: Pointer to sde encoder structure
	 * @en: enable/disable flag
	 * @frame_count: number of frames for which MISR needs to run
	 */
	int (*misr_setup[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc, bool en, u32 frame_count);

	/**
	 * deregister_cwb_events - called after wait_for_commit_done completes; used
	 * to perform teardown work (e.g. HFI event deregister) that must not
	 * block the primary commit path.
	 * @enc: Pointer to sde encoder structure
	 * Returns: Zero on success
	 */
	int (*deregister_cwb_events[MSM_DISP_OP_MAX])(struct sde_encoder_virt *enc);
};

struct sde_encoder_virt {
	struct drm_encoder base;
	spinlock_t enc_spinlock;
	struct mutex vblank_ctl_lock;
	uint32_t bus_scaling_client;
	struct drm_connector *cached_connector;

	uint32_t display_num_of_h_tiles;
	uint32_t te_source;

	unsigned int num_phys_encs;
	struct sde_encoder_phys *phys_encs[MAX_PHYS_ENCODERS_PER_VIRTUAL];
	struct sde_encoder_phys *phys_vid_encs[MAX_PHYS_ENCODERS_PER_VIRTUAL];
	struct sde_encoder_phys *phys_cmd_encs[MAX_PHYS_ENCODERS_PER_VIRTUAL];
	struct sde_encoder_phys *phys_lb_encs[MAX_PHYS_ENCODERS_PER_VIRTUAL];
	struct sde_encoder_phys *cur_master;
	struct sde_hw_pingpong *hw_pp[MAX_CHANNELS_PER_ENC];
	struct sde_hw_dsc *hw_dsc[MAX_CHANNELS_PER_ENC];
	struct sde_hw_vdc *hw_vdc[MAX_CHANNELS_PER_ENC];
	struct sde_hw_pingpong *hw_dsc_pp[MAX_CHANNELS_PER_ENC];
	enum sde_dsc dirty_dsc_ids[MAX_CHANNELS_PER_ENC];
	enum sde_vdc dirty_vdc_ids[MAX_CHANNELS_PER_ENC];
	u32 cur_channel_cnt;
	bool intfs_swapped;
	bool qdss_status;

	void (*crtc_vblank_cb)(void *data, ktime_t ts);
	void *crtc_vblank_cb_data;

	struct dentry *debugfs_root;
	struct mutex enc_lock;
	atomic_t frame_done_cnt[MAX_PHYS_ENCODERS_PER_VIRTUAL];
	atomic_t pending_commit_cnt;
	void (*crtc_frame_event_cb)(void *data, u32 event, ktime_t ts);
	struct sde_kms_frame_event_cb_data crtc_frame_event_cb_data;
	void (*crtc_power_event_cb)(void *data, u32 event);
	struct sde_kms_frame_event_cb_data crtc_power_event_cb_data;

	struct sde_rsc_client *rsc_client;
	bool rsc_state_init;
	struct msm_display_info disp_info;
	atomic_t misr_enable;
	atomic_t vsync_cnt;
	bool misr_reconfigure;
	u32 misr_frame_count;
	struct sde_misr_values misr_vals;

	bool idle_pc_enabled;
	bool input_event_enabled;
	struct mutex rc_lock;
	enum sde_enc_rc_states rc_state;
	struct mutex off_work_lock;
	struct kthread_delayed_work delayed_off_work;
	struct kthread_work early_wakeup_work;
	struct kthread_work input_event_work;
	struct kthread_work esd_trigger_work;
	struct kthread_work self_refresh_work;
	struct kthread_work backlight_cmd_work;
	struct kthread_delayed_work backlight_sr_work;

	struct input_handler *input_handler;
	bool vblank_enabled;
	bool idle_pc_restore;
	bool restore_te_rd_ptr;
	enum frame_trigger_mode_type frame_trigger_mode;
	bool dynamic_hdr_updated;

	struct sde_rsc_cmd_config rsc_config;
	struct sde_rect cur_conn_roi;
	struct sde_rect prv_conn_roi;
	struct drm_crtc *crtc;

	bool fal10_veto_override;
	bool recovery_events_enabled;
	bool elevated_ahb_vote;
	struct dev_pm_qos_request pm_qos_cpu_req[NR_CPUS];
	struct cpumask valid_cpu_mask;
	struct mutex pm_qos_lock;
	struct msm_mode_info mode_info;
	struct sde_encoder_vrr_info vrr_info;
	bool delay_kickoff;
	bool autorefresh_solver_disable;
	bool ctl_done_supported;

	unsigned long dynamic_irqs_config;

	bool dpu_ctl_op_sync;
	struct sde_encoder_ops ops;
	u32 mode_switch;
	struct sde_cesta_client *cesta_client;
	bool cesta_enable_frame;
	bool cesta_scc_override;
	struct hfi_encoder *hfi_encoder;
	struct sde_encoder_hal_funcs hal_ops;
};





struct drm_connector *sde_encoder_get_connector(struct drm_device *dev,
		struct drm_encoder *drm_enc);

#include "sde_encoder_dce.h"

/**
 * sde_encoder_dce_set_bpp : set src_bpp and target_bpp in sde_crtc
 * @msm_mode_info: Mode info
 * @crtc: Pointer to drm crtc structure
 */
void sde_encoder_dce_set_bpp(
		struct msm_mode_info mode_info, struct drm_crtc *crtc);
bool sde_encoder_is_dsi_display(struct drm_encoder *enc);
bool sde_encoder_in_clone_mode(struct drm_encoder *enc);
bool sde_encoder_is_dsc_merge(struct drm_encoder *enc);
bool sde_encoder_check_curr_mode(struct drm_encoder *enc, u32 mode);
void sde_encoder_set_clone_mode(struct drm_encoder *drm_enc,
		struct drm_crtc_state *crtc_state);
void sde_encoder_register_frame_event_callback(struct drm_encoder *encoder,
		void (*cb)(void *, u32, ktime_t), struct drm_crtc *crtc);
u32 sde_encoder_get_fps(struct drm_encoder *encoder);

bool _dce_dsc_is_dirty(struct sde_encoder_virt *sde_enc);
bool _dce_vdc_is_dirty(struct sde_encoder_virt *sde_enc);
void _dce_helper_flush_vdc(struct sde_encoder_virt *sde_enc);

/**
 * sde_encoder_dce_disable : function to disable compression
 * @sde_enc: pointer to virtual encoder structure
 */
void sde_encoder_dce_disable(struct sde_encoder_virt *sde_enc);

/**
 * sde_encoder_dce_setup : function to configure compression block
 * @sde_enc: pointer to virtual encoder structure
 * @params: pointer to kickoff params
 */
void sde_encoder_control_idle_pc(struct drm_encoder *enc, bool enable);

/**
 * sde_encoder_in_cont_splash - checks if display is in continuous splash
 * @drm_enc:    Pointer to drm encoder structure
 * @Return:     true if display in continuous splash
 */
int sde_encoder_in_cont_splash(struct drm_encoder *enc);

/**
 * sde_encoder_dce_flush :function to flush the compression configuration
 * @sde_enc: pointer to virtual encoder structure
 */
int sde_encoder_dce_flush(struct sde_encoder_virt *sde_enc);
void sde_encoder_needs_hw_reset(struct drm_encoder *enc);
bool sde_encoder_recovery_events_enabled(struct drm_encoder *encoder);
void sde_encoder_enable_recovery_event(struct drm_encoder *encoder);
bool sde_encoder_is_line_insertion_supported(struct drm_encoder *drm_enc);
int sde_encoder_poll_line_counts(struct drm_encoder *drm_enc);
void sde_encoder_control_te(struct drm_encoder *drm_enc, bool enable);
void sde_encoder_helper_update_out_fence_txq(struct sde_encoder_virt *sde_enc, bool is_vid);
void sde_encoder_uidle_enable(struct drm_encoder *drm_enc, bool enable);
ktime_t sde_encoder_calc_last_vsync_timestamp(struct drm_encoder *drm_enc);
void sde_encoder_add_data_to_minidump_va(struct drm_encoder *drm_enc);
void sde_crtc_get_frame_data(struct drm_crtc *crtc);
void sde_crtc_get_mixer_resolution(struct drm_crtc *crtc, struct drm_crtc_state *crtc_state,
		struct drm_display_mode *mode, u32 *width, u32 *height);
void sde_crtc_get_resolution(struct drm_crtc *crtc, struct drm_crtc_state *crtc_state,
		struct drm_display_mode *mode, u32 *width, u32 *height);
int sde_encoder_get_avr_status(struct drm_encoder *encoder);
void sde_encoder_misr_sign_event_notify(struct drm_encoder *encoder);
void sde_encoder_cancel_delayed_work(struct drm_encoder *encoder);
void sde_encoder_control_idle_pc(struct drm_encoder *drm_enc, bool enable);
struct sde_rsc_client *sde_encoder_get_rsc_client(struct drm_encoder *drm_enc);
void sde_encoder_irq_control(struct drm_encoder *drm_enc, bool enable);
int sde_encoder_wait_for_event(struct drm_encoder *drm_encoder,
  						enum msm_event_wait event);
void sde_encoder_destroy(struct drm_encoder *drm_enc);
bool sde_encoder_is_primary_display(struct drm_encoder *enc);
bool sde_encoder_is_built_in_display(struct drm_encoder *enc);
bool sde_encoder_is_built_in_display(struct drm_encoder *enc);
void sde_encoder_get_hw_resources(struct drm_encoder *encoder,
struct sde_encoder_hw_resources *hw_res,
	struct drm_connector_state *conn_state);
void sde_encoder_virt_restore(struct drm_encoder *encoder);
void sde_encoder_virt_reset(struct drm_encoder *drm_enc);
enum sde_intf_mode sde_encoder_get_intf_mode(struct drm_encoder *encoder);
void sde_encoder_register_vblank_callback(struct drm_encoder *encoder,
	void (*cb)(void *, ktime_t), void *data);
	int sde_encoder_idle_request(struct drm_encoder *drm_enc);
void sde_encoder_trigger_kickoff_pending(struct drm_encoder *encoder);
void sde_encoder_early_wakeup(struct drm_encoder *drm_enc);
int sde_encoder_prepare_for_kickoff(struct drm_encoder *encoder,
struct sde_encoder_kickoff_params *params);
bool sde_encoder_is_dsc_merge(struct drm_encoder *drm_enc);
bool sde_encoder_is_cwb_disabling(struct drm_encoder *drm_enc,
	struct drm_crtc *drm_crtc);
bool sde_encoder_needs_dsc_disable(struct drm_encoder *drm_enc);
void sde_encoder_kickoff(struct drm_encoder *encoder, bool config_changed);
void sde_encoder_get_transfer_time(struct drm_encoder *drm_enc,
	u32 *transfer_time_us);
uint32_t sde_encoder_get_clones(struct drm_encoder *drm_enc);
int sde_encoder_prepare_commit(struct drm_encoder *drm_enc);
struct drm_encoder *sde_encoder_init(struct drm_device *dev,
	struct msm_display_info *disp_info);
enum sde_intf_mode sde_encoder_get_intf_mode(struct drm_encoder *encoder);
u32 sde_encoder_get_frame_count(struct drm_encoder *encoder);
bool sde_encoder_get_vblank_timestamp(struct drm_encoder *encoder,
		ktime_t *tvblank);
int sde_encoder_update_caps_for_cont_splash(struct drm_encoder *encoder,
	struct sde_splash_display *splash_display, bool enable);
int sde_encoder_display_failure_notification(struct drm_encoder *enc,
	bool skip_pre_kickoff);
struct sde_hw_ctl *sde_encoder_get_hw_ctl(struct sde_connector *c_conn);

/**
 * sde_encoder_register_misr_event - register or deregister MISR event
 * @drm_enc: pointer to drm encoder
 * @val: indicates register or deregister
 */
static inline int sde_encoder_register_misr_event(struct drm_encoder *drm_enc, bool val)
{
	struct sde_encoder_virt *sde_enc = NULL;

	if (!drm_enc)
		return -EINVAL;

	sde_enc = container_of(drm_enc, struct sde_encoder_virt, base);
	atomic_set(&sde_enc->misr_enable, val);

	/*
	 * To setup MISR ctl reg, set misr_reconfigure as true.
	 * MISR is calculated for the specific number of frames.
	 */
	if (atomic_read(&sde_enc->misr_enable)) {
		sde_enc->misr_reconfigure = true;
		sde_enc->misr_frame_count = 1;
	}

	return 0;
}

#define to_sde_encoder_virt(x) container_of(x, struct sde_encoder_virt, base)

static inline bool sde_encoder_check_ctl_done_support(struct drm_encoder *drm_enc)
{
	struct sde_encoder_virt *sde_enc;

	if (!drm_enc)
		return false;

	sde_enc = to_sde_encoder_virt(drm_enc);
	return sde_enc->ctl_done_supported;
}

static inline u32 sde_encoder_get_dfps_maxfps(struct drm_encoder *drm_enc)
{
	struct sde_encoder_virt *sde_enc;

	if (!drm_enc) {
		SDE_ERROR("invalid encoder\n");
		return 0;
	}

	sde_enc = to_sde_encoder_virt(drm_enc);
	return sde_enc->mode_info.dfps_maxfps;
}

static inline struct sde_kms *sde_encoder_get_kms(struct drm_encoder *drm_enc)
{
	struct msm_drm_private *priv;

	if (!drm_enc || !drm_enc->dev) {
		SDE_ERROR("invalid encoder\n");
		return NULL;
	}

	priv = drm_enc->dev->dev_private;
	if (!priv || !priv->kms) {
		SDE_ERROR("invalid kms\n");
		return NULL;
	}

	return to_sde_kms(priv->kms);
}

static inline bool sde_encoder_is_widebus_enabled(struct drm_encoder *drm_enc)
{
	struct sde_encoder_virt *sde_enc;

	if (!drm_enc)
		return false;

	sde_enc = to_sde_encoder_virt(drm_enc);
	return sde_enc->mode_info.wide_bus_en;
}

#endif /* __SDE_ENCODER_H__ */
