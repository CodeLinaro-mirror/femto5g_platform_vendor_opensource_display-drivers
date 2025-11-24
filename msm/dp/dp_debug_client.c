// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#if (KERNEL_VERSION(5, 19, 0) <= LINUX_VERSION_CODE)
#include <drm/display/drm_dp_mst_helper.h>
#else
#include <drm/drm_dp_mst_helper.h>
#endif
#include <drm/drm_probe_helper.h>

#include "dp_power.h"
#include "drm/drm_connector.h"
#include "sde_connector.h"
#include "dp_pll.h"
#include "dp_mst_sim.h"
#include "dp_mst_drm.h"
#include "dp_mgr.h"
#include "dp_aux_bridge.h"
#include "dp_drv.h"
#include "dp_debug.h"

static void dp_debug_client_set_sim_mode(struct dp_debug_client *client, bool sim)
{
	/* Call client handler for sim mode logic */
	if (client && client->write_sim_mode)
		client->write_sim_mode(client, sim);

	DP_INFO("%s\n", sim ? "[ON]" : "[OFF]");
}

static void dp_debug_client_abort(struct dp_debug_client *client)
{
	if (!client)
		return;

	client->hotplug = false;
	dp_debug_client_set_sim_mode(client, false);
}

/* Read operations */
int dp_debug_client_read_dpcd(struct dp_debug_client *client, u8 *dpcd, u32 size, u32 offset)
{
	struct dp_aux *aux;
	struct dp_panel *panel;
	struct dp_aux_bridge *sim_bridge;
	u32 dpcd_size = 0;

	if (!client || !client->aux || !client->panel)
		return -ENODEV;

	aux = client->aux;
	panel = client->panel;

	/*
	 * In simulation mode, this function returns the last written DPCD node.
	 * For a real monitor plug in, it dumps the first byte at the last written DPCD address
	 * unless the address is 0, in which case the first 20 bytes are dumped
	 */
	if (client->sim_enable) {
		sim_bridge = (struct dp_aux_bridge *)client->sim_bridge;
		if (sim_bridge) {
			dp_sim_read_dpcd_reg(sim_bridge, dpcd, size, offset);
			dpcd_size = size;
		}
	} else {
		if (offset) {
			dpcd_size = 1;
			if (drm_dp_dpcd_read(aux->drm_aux, offset, dpcd, dpcd_size) != 1)
				return -EIO;
		} else {
			dpcd_size = sizeof(panel->dpcd);
			memcpy(dpcd, panel->dpcd, dpcd_size);
		}
	}

	return dpcd_size;
}

int dp_debug_client_read_crc(struct dp_debug_client *client, char *buf, u32 size)
{
	struct dp_ctrl *ctrl;
	struct dp_panel *panel;
	u32 len = 0;
	u16 src_crc[3] = {0};
	u16 sink_crc[3] = {0};
	struct dp_misr40_data misr40 = {0};
	u32 retries = 2;
	int i, rc;

	if (!client || !buf || !client->panel || !client->ctrl)
		return -ENODEV;

	panel = client->panel;
	ctrl = client->ctrl;

	if (!panel->pclk_on)
		return 0;

	panel->get_sink_crc(panel, sink_crc);
	if (!(sink_crc[0] + sink_crc[1] + sink_crc[2])) {
		panel->sink_crc_enable(panel, true);
		msleep(30);
		panel->get_sink_crc(panel, sink_crc);
	}

	panel->get_src_crc(panel, src_crc);

	len += scnprintf(buf + len, size - len, "FRAME_CRC:\nSource vs Sink\n");

	len += scnprintf(buf + len, size - len, "CRC_R: %04X %04X\n", src_crc[0], sink_crc[0]);
	len += scnprintf(buf + len, size - len, "CRC_G: %04X %04X\n", src_crc[1], sink_crc[1]);
	len += scnprintf(buf + len, size - len, "CRC_B: %04X %04X\n", src_crc[2], sink_crc[2]);

	ctrl->setup_misr(ctrl);

	while (retries--) {
		msleep(30);

		rc = ctrl->read_misr(ctrl, &misr40);
		if (rc != -EAGAIN)
			break;
	}

	len += scnprintf(buf + len, size - len, "\nMISR40:\nCTLR vs PHY\n");
	for (i = 0; i < 4; i++) {
		len += scnprintf(buf + len, size - len, "Lane%d %08X%08X %08X%08X\n", i,
				misr40.ctrl_misr[2 * i], misr40.ctrl_misr[(2 * i) + 1],
				misr40.phy_misr[2 * i], misr40.phy_misr[(2 * i) + 1]);
	}

	return len;
}

int dp_debug_client_read_connected(struct dp_debug_client *client, char *buf, u32 size)
{
	struct dp_hpd *hpd;
	u32 len = 0;

	if (!client || !buf || !client->hpd)
		return -ENODEV;

	hpd = client->hpd;

	len += scnprintf(buf, size, "%d\n", hpd->hpd_high);

	return len;
}

int dp_debug_client_read_info(struct dp_debug_client *client, char *buf, u32 size)
{
	struct dp_aux *aux;
	struct dp_panel *panel;
	struct dp_link *link;
	u32 len = 0, max_size = size;
	int rc;

	if (!client || !buf || !client->aux || !client->panel || !client->link)
		return -ENODEV;

	aux = client->aux;
	panel = client->panel;
	link = client->link;

	rc = scnprintf(buf + len, max_size, "\tstate=0x%x\n", aux->state);
	if (rc >= max_size)
		return len;
	len += rc;
	max_size = size - len;

	rc = scnprintf(buf + len, max_size, "\tlink_rate=%u\n",
		panel->link_info.rate);
	if (rc >= max_size)
		return len;
	len += rc;
	max_size = size - len;

	rc = scnprintf(buf + len, max_size, "\tnum_lanes=%u\n",
		panel->link_info.num_lanes);
	if (rc >= max_size)
		return len;
	len += rc;
	max_size = size - len;

	rc = scnprintf(buf + len, max_size, "\tresolution=%dx%d@%dHz\n",
		panel->pinfo.h_active,
		panel->pinfo.v_active,
		panel->pinfo.refresh_rate);
	if (rc >= max_size)
		return len;
	len += rc;
	max_size = size - len;

	rc = scnprintf(buf + len, max_size, "\tpclock=%dKHz\n",
		panel->pinfo.pixel_clk_khz);
	if (rc >= max_size)
		return len;
	len += rc;
	max_size = size - len;

	rc = scnprintf(buf + len, max_size, "\tbpp=%d\n",
		panel->pinfo.bpp);
	if (rc >= max_size)
		return len;
	len += rc;
	max_size = size - len;

	/* Link Information */
	rc = scnprintf(buf + len, max_size, "\ttest_req=%s\n",
		dp_link_get_test_name(link->sink_request));
	if (rc >= max_size)
		return len;
	len += rc;
	max_size = size - len;

	rc = scnprintf(buf + len, max_size,
		"\tlane_count=%d\n", link->link_params.lane_count);
	if (rc >= max_size)
		return len;
	len += rc;
	max_size = size - len;

	rc = scnprintf(buf + len, max_size,
		"\tbw_code=%d\n", link->link_params.bw_code);
	if (rc >= max_size)
		return len;
	len += rc;
	max_size = size - len;

	rc = scnprintf(buf + len, max_size,
		"\tv_level=%d\n", link->phy_params.v_level);
	if (rc >= max_size)
		return len;
	len += rc;
	max_size = size - len;

	rc = scnprintf(buf + len, max_size,
		"\tp_level=%d\n", link->phy_params.p_level);
	if (rc >= max_size)
		return len;
	len += rc;

	return len;
}

int dp_debug_client_read_bw_code(struct dp_debug_client *client, char *buf, u32 size)
{
	struct dp_panel *panel;
	u32 len = 0;

	if (!client || !buf || !client->panel)
		return -ENODEV;

	panel = client->panel;

	len += scnprintf(buf, size, "max_bw_code = %d\n", panel->max_bw_code);

	return len;
}

int dp_debug_client_read_tpg(struct dp_debug_client *client, char *buf, u32 size)
{
	u32 len = 0;

	if (!client || !buf)
		return -ENODEV;

	len += scnprintf(buf, SZ_8, "%d\n", client->tpg_pattern);

	return len;
}

int dp_debug_client_read_dump(struct dp_debug_client *client, char *buf,
		u32 size, const char *reg_name)
{
	struct dp_catalog *catalog;
	struct dp_hpd *hpd;
	u8 *reg_buf = NULL;
	u32 len = 0;
	int rc;
	char prefix[SZ_32];

	if (!client || !buf || !reg_name || !client->catalog || !client->hpd)
		return -ENODEV;

	catalog = client->catalog;
	hpd = client->hpd;

	if (!hpd->hpd_high || !strlen(reg_name))
		return 0;

	rc = catalog->get_reg_dump(catalog, (char *)reg_name, &reg_buf, &len);
	if (rc)
		return rc;

	scnprintf(prefix, sizeof(prefix), "%s: ", reg_name);
	print_hex_dump_debug(prefix, DUMP_PREFIX_NONE,
		16, 4, reg_buf, len, false);

	len = min_t(u32, size, len);
	memcpy(buf, reg_buf, len);

	return len;
}

int dp_debug_client_read_mst_mode(struct dp_debug_client *client, char *buf, u32 size)
{
	struct dp_parser *parser;
	struct dp_panel *panel;
	u32 len = 0;

	if (!client || !buf || !client->parser || !client->panel)
		return -ENODEV;

	parser = client->parser;
	panel = client->panel;

	len = scnprintf(buf, size, "mst_mode = %d, mst_state = %d\n",
			parser->has_mst, panel->mst_state);

	return len;
}

int dp_debug_client_read_max_pclk_khz(struct dp_debug_client *client, char *buf, u32 size)
{
	struct dp_parser *parser;
	u32 len = 0;

	if (!client || !buf || !client->parser)
		return -ENODEV;

	parser = client->parser;

	len += scnprintf(buf + len, (SZ_4K - len),
				"max_pclk_khz = %d, org: %d\n",
				client->max_pclk_khz,
				client->parser->max_pclk_khz);
	return len;
}

static int dp_debug_client_check_buffer_overflow(int rc, int *max_size, int *len)
{
	if (rc >= *max_size) {
		DP_ERR("buffer overflow\n");
		return -EINVAL;
	}
	*len += rc;
	*max_size = SZ_4K - *len;

	return 0;
}

static int dp_debug_client_print_hdr_params_to_buf(struct drm_connector *connector,
		char *buf, u32 size)
{
	int rc;
	u32 i, len = 0, max_size = size;
	struct sde_connector *c_conn;
	struct sde_connector_state *c_state;
	struct drm_msm_ext_hdr_metadata *hdr;

	c_conn = to_sde_connector(connector);
	c_state = to_sde_connector_state(connector->state);

	hdr = &c_state->hdr_meta;

	rc = scnprintf(buf + len, max_size,
		"============SINK HDR PARAMETERS===========\n");
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "eotf = %d\n",
		c_conn->hdr_eotf);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "type_one = %d\n",
		c_conn->hdr_metadata_type_one);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "hdr_plus_app_ver = %d\n",
			c_conn->hdr_plus_app_ver);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "max_luminance = %d\n",
		c_conn->hdr_max_luminance);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "avg_luminance = %d\n",
		c_conn->hdr_avg_luminance);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "min_luminance = %d\n",
		c_conn->hdr_min_luminance);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size,
		"============VIDEO HDR PARAMETERS===========\n");
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "hdr_state = %d\n", hdr->hdr_state);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "hdr_supported = %d\n",
			hdr->hdr_supported);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "eotf = %d\n", hdr->eotf);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "white_point_x = %d\n",
		hdr->white_point_x);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "white_point_y = %d\n",
		hdr->white_point_y);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "max_luminance = %d\n",
		hdr->max_luminance);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "min_luminance = %d\n",
		hdr->min_luminance);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "max_content_light_level = %d\n",
		hdr->max_content_light_level);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	rc = scnprintf(buf + len, max_size, "min_content_light_level = %d\n",
		hdr->max_average_light_level);
	if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
		goto error;

	for (i = 0; i < HDR_PRIMARIES_COUNT; i++) {
		rc = scnprintf(buf + len, max_size, "primaries_x[%d] = %d\n",
			i, hdr->display_primaries_x[i]);
		if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
			goto error;

		rc = scnprintf(buf + len, max_size, "primaries_y[%d] = %d\n",
			i, hdr->display_primaries_y[i]);
		if (dp_debug_client_check_buffer_overflow(rc, &max_size, &len))
			goto error;
	}

	if (hdr->hdr_plus_payload && hdr->hdr_plus_payload_size) {
		u32 rowsize = 16, rem;
		struct sde_connector_dyn_hdr_metadata *dhdr =
				&c_state->dyn_hdr_meta;

		/**
		 * Do not use user pointer from hdr->hdr_plus_payload directly,
		 * instead use kernel's cached copy of payload data.
		 */
		for (i = 0; i < dhdr->dynamic_hdr_payload_size; i += rowsize) {
			rc = scnprintf(buf + len, max_size, "DHDR: ");
			if (dp_debug_client_check_buffer_overflow(rc, &max_size,
					&len))
				goto error;

			rem = dhdr->dynamic_hdr_payload_size - i;
			rc = hex_dump_to_buffer(&dhdr->dynamic_hdr_payload[i],
				min(rowsize, rem), rowsize, 1, buf + len,
				max_size, false);
			if (dp_debug_client_check_buffer_overflow(rc, &max_size,
					&len))
				goto error;

			rc = scnprintf(buf + len, max_size, "\n");
			if (dp_debug_client_check_buffer_overflow(rc, &max_size,
					&len))
				goto error;
		}
	}

	return len;
error:
	return -EOVERFLOW;
}

int dp_debug_client_read_hdr(struct dp_debug_client *client, char *buf, u32 size, int panel_id)
{
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector = NULL;
	bool in_list = false;
	int len = 0;

	if (!client || !buf || !client->connector)
		return -ENODEV;

	/* For panel_id == 0, use the base connector */
	if (panel_id == 0) {
		connector = client->connector;
	} else {
		/* For MST panels, find the connector by mst_con_id */
		drm_connector_list_iter_begin(client->connector->dev, &conn_iter);
		drm_for_each_connector_iter(connector, &conn_iter) {
			if (connector->base.id == client->mst_con_id) {
				in_list = true;
				break;
			}
		}
		drm_connector_list_iter_end(&conn_iter);

		if (!in_list) {
			DP_ERR("connector %u not in mst list\n", client->mst_con_id);
			return -EINVAL;
		}
	}

	if (!connector) {
		DP_ERR("connector is NULL\n");
		return -EINVAL;
	}

	len = dp_debug_client_print_hdr_params_to_buf(connector, buf, size);
	if (len == -EOVERFLOW) {
		DP_ERR("HDR buffer overflow\n");
		return len;
	}

	return len;
}

int dp_debug_client_read_edid_modes(struct dp_debug_client *client, char *buf, u32 size)
{
	struct drm_connector *connector;
	struct drm_display_mode *mode;
	u32 len = 0, ret = 0, max_size = size;

	if (!client || !buf || !client->connector)
		return -ENODEV;

	connector = client->connector;

	if (!connector) {
		DP_ERR("connector is NULL\n");
		return -EINVAL;
	}

	mutex_lock(&connector->dev->mode_config.mutex);
	list_for_each_entry(mode, &connector->modes, head) {
		ret = scnprintf(buf + len, max_size,
			"%s %d %d %d %d %d 0x%x\n",
			mode->name, drm_mode_vrefresh(mode), mode->picture_aspect_ratio,
			mode->htotal, mode->vtotal, mode->clock, mode->flags);
		if (dp_debug_client_check_buffer_overflow(ret, &max_size, &len))
			break;
	}
	mutex_unlock(&connector->dev->mode_config.mutex);

	return len;
}

int dp_debug_client_read_edid_modes_mst(struct dp_debug_client *client, char *buf, u32 size)
{
	struct drm_connector *connector;
	struct drm_display_mode *mode;
	u32 len = 0, ret = 0, max_size = size;

	if (!client || !buf)
		return -ENODEV;

	connector = drm_connector_lookup(client->connector->dev, NULL, client->mst_con_id);
	if (!connector) {
		DP_ERR("connector %u not in mst list\n", client->mst_con_id);
		return 0;
	}

	mutex_lock(&connector->dev->mode_config.mutex);
	list_for_each_entry(mode, &connector->modes, head) {
		ret = scnprintf(buf + len, max_size,
			"%s %d %d %d %d %d 0x%x\n",
			mode->name, drm_mode_vrefresh(mode), mode->picture_aspect_ratio,
			mode->htotal, mode->vtotal, mode->clock, mode->flags);
		if (dp_debug_client_check_buffer_overflow(ret, &max_size, &len))
			break;
	}
	mutex_unlock(&connector->dev->mode_config.mutex);

	drm_connector_put(connector);
	return len;
}

int dp_debug_client_read_mst_conn_info(struct dp_debug_client *client, char *buf, u32 size)
{
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;
	struct sde_connector *sde_conn;
	struct dp_drv *drv;
	u32 len = 0, ret = 0, max_size = size;

	if (!client || !buf)
		return -ENODEV;

	drm_connector_list_iter_begin(client->connector->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		sde_conn = to_sde_connector(connector);
		drv = sde_conn->display;
		if (!sde_conn->mst_port ||
				drv->client->base_connector != client->connector)
			continue;
		ret = scnprintf(buf + len, max_size,
				"conn name:%s, conn id:%d state:%d\n",
				connector->name, connector->base.id,
				connector->status);
		if (dp_debug_client_check_buffer_overflow(ret, &max_size, &len))
			break;
	}
	drm_connector_list_iter_end(&conn_iter);

	return len;
}

static int dp_debug_client_sim_hpd_cb(void *arg, bool hpd, bool hpd_irq)
{
	struct dp_debug_client *client = arg;
	int vdo = 0;

	if (hpd_irq) {
		vdo |= BIT(7);

		if (hpd)
			vdo |= BIT(8);

		return client->hpd->simulate_attention(client->hpd, vdo);
	} else {
		return client->hpd->simulate_connect(client->hpd, hpd);
	}
}

static int dp_debug_client_attach_sim_bridge(struct dp_debug_client *client)
{
	int ret;

	if (!client->sim_bridge) {
		ret = dp_sim_create_bridge(client->dev, &client->sim_bridge);
		if (ret)
			return ret;

		if (client->sim_bridge->register_hpd)
			client->sim_bridge->register_hpd(client->sim_bridge,
					dp_debug_client_sim_hpd_cb, client);
	}

	dp_sim_update_port_num(client->sim_bridge, 1);

	return 0;
}

static void dp_debug_client_enable_sim_mode(struct dp_debug_client *client,
		u32 mode_mask)
{
	/* return if mode is already enabled */
	if ((client->sim_mode & mode_mask) == mode_mask)
		return;

	/* create bridge if not yet */
	if (dp_debug_client_attach_sim_bridge(client))
		return;

	/* switch to bridge mode */
	if (!client->sim_mode)
		client->aux->set_sim_mode(client->aux, client->sim_bridge);

	/* update sim mode */
	client->sim_mode |= mode_mask;
	dp_sim_set_sim_mode(client->sim_bridge, client->sim_mode);
}

static void dp_debug_client_disable_sim_mode(struct dp_debug_client *client,
		u32 mode_mask)
{
	/* return if mode is already disabled */
	if (!(client->sim_mode & mode_mask))
		return;

	/* update sim mode */
	client->sim_mode &= ~mode_mask;
	dp_sim_set_sim_mode(client->sim_bridge, client->sim_mode);

	dp_sim_update_port_num(client->sim_bridge, 0);

	/* switch to normal mode */
	if (!client->sim_mode)
		client->aux->set_sim_mode(client->aux, NULL);
}

/* Write operations */
int dp_debug_client_write_edid(struct dp_debug_client *client, const char *buf, size_t count)
{
	u8 *input_buf = NULL, *buf_t = NULL, *edid = NULL;
	const int char_to_nib = 2;
	size_t edid_size = 0;
	size_t size = 0, edid_buf_index = 0;
	int rc = count;
	struct dp_drv *drv;
	struct platform_device *pdev;

	if (!client || !buf || !client->sim_bridge)
		return -ENODEV;

	size = min_t(size_t, count, SZ_1K);

	input_buf = kzalloc(size, GFP_KERNEL);
	if (!input_buf)
		return -ENOMEM;

	input_buf = kmemdup(buf, size, GFP_KERNEL);
	if (!input_buf) {
		return -ENOMEM;
		goto bail;
	}

	edid_size = size / char_to_nib;
	buf_t = input_buf;
	size = edid_size;

	edid = kzalloc(size, GFP_KERNEL);
	if (!edid) {
		rc = -ENOMEM;
		goto bail;
	}

	while (size--) {
		char t[3];
		int d;

		memcpy(t, buf_t, sizeof(char) * char_to_nib);
		t[char_to_nib] = '\0';

		if (kstrtoint(t, 16, &d)) {
			DP_ERR("kstrtoint error\n");
			rc = -EINVAL;
			goto bail;
		}

		edid[edid_buf_index++] = d;
		buf_t += char_to_nib;
	}

	print_hex_dump(KERN_INFO, "[dp-sim] EDID(Little Endian): ",
			DUMP_PREFIX_NONE, 16, 4, edid, edid_size, false);

	pdev = to_platform_device(client->dev);
	drv = platform_get_drvdata(pdev);

	dp_debug_client_enable_sim_mode(client, DP_SIM_MODE_EDID);
	dp_mst_clear_edid_cache(drv->client);
	dp_sim_update_port_edid(client->sim_bridge, client->mst_edid_idx,
			edid, edid_size);
bail:
	kfree(input_buf);
	kfree(edid);
	return rc;
}

int dp_debug_client_write_dpcd(struct dp_debug_client *client, const char *buf, size_t count)
{
	u8 *input_buf = NULL, *buf_t = NULL, *dpcd = NULL;
	const int char_to_nib = 2;
	size_t dpcd_size = 0;
	size_t size = 0, dpcd_buf_index = 0;
	char offset_ch[5];
	u32 offset, data_len;
	int rc = 0;

	if (!client || !buf || count < 4 || !client->sim_bridge)
		return -ENODEV;

	size = min_t(size_t, count, SZ_2K);

	if (size < 4)
		return -EINVAL;

	input_buf = kzalloc(size, GFP_KERNEL);
	if (!input_buf)
		return -ENOMEM;

	input_buf = kmemdup(buf, size, GFP_KERNEL);
	if (!input_buf) {
		return -ENOMEM;
		goto bail;
	}

	memcpy(offset_ch, input_buf, 4);
	offset_ch[4] = '\0';

	if (kstrtoint(offset_ch, 16, &offset)) {
		DP_ERR("offset kstrtoint error\n");
		rc = -EINVAL;
		goto bail;
	}

	size -= 4;
	if (size < char_to_nib) {
		rc = -EINVAL;
		goto bail;
	}

	dpcd_size = size / char_to_nib;
	data_len = dpcd_size;
	buf_t = input_buf + 4;

	dpcd = kzalloc(dpcd_size, GFP_KERNEL);
	if (!dpcd) {
		rc = -ENOMEM;
		goto bail;
	}

	while (dpcd_size--) {
		char t[3];
		int d;

		memcpy(t, buf_t, sizeof(char) * char_to_nib);
		t[char_to_nib] = '\0';

		if (kstrtoint(t, 16, &d)) {
			DP_ERR("kstrtoint error\n");
			rc = -EINVAL;
			goto bail;
		}

		dpcd[dpcd_buf_index++] = d;

		buf_t += char_to_nib;
	}

	/*
	 * if link training status registers are reprogramed,
	 * read link training status from simulator, otherwise
	 * read link training status from real aux channel.
	 */
	if (offset <= DP_LANE0_1_STATUS &&
			offset + dpcd_buf_index > DP_LANE0_1_STATUS) {
		dp_debug_client_enable_sim_mode(client,
			DP_SIM_MODE_DPCD_READ | DP_SIM_MODE_LINK_TRAIN);
	} else {
		dp_debug_client_enable_sim_mode(client, DP_SIM_MODE_DPCD_READ);
	}

	dp_sim_write_dpcd_reg(client->sim_bridge, dpcd, dpcd_buf_index, offset);

bail:
	kfree(input_buf);
	kfree(dpcd);
	return rc;
}

int dp_debug_client_write_hpd(struct dp_debug_client *client, const char *buf, size_t count)
{
	const int hpd_data_mask = 0x7;
	int hpd = 0;

	if (!client || !buf || !client->hpd)
		return -ENODEV;


	if (kstrtoint(buf, 10, &hpd) != 0)
		return -EINVAL;

	hpd &= hpd_data_mask;

	client->hotplug = !!(hpd & BIT(0));
	client->psm_enabled = !!(hpd & BIT(1));

	/*
	 * print hotplug value as this code is executed
	 * only while running in debug mode which is manually
	 * triggered by a tester or a script.
	 */
	DP_INFO("%s\n", client->hotplug ? "[CONNECT]" : "[DISCONNECT]");

	client->hpd->simulate_connect(client->hpd, client->hotplug);

	return 0;
}

int dp_debug_client_write_edid_modes(struct dp_debug_client *client, const char *buf, size_t count)
{
	struct dp_panel *panel;
	int hdisplay = 0, vdisplay = 0, vrefresh = 0, aspect_ratio = 0;

	if (!client || !buf || !client->panel)
		return -ENODEV;

	panel = client->panel;

	if (sscanf(buf, "%d %d %d %d", &hdisplay, &vdisplay, &vrefresh,
				&aspect_ratio) != 4)
		goto clear;

	if (!hdisplay || !vdisplay || !vrefresh)
		goto clear;

	panel->mode_override = true;

	panel->mode.override_timing.h_active = hdisplay;
	panel->mode.override_timing.v_active = vdisplay;
	panel->mode.override_timing.refresh_rate = vrefresh;
	panel->mode.override_timing.aspect_ratio = aspect_ratio;

	return 0;

clear:
	DP_DEBUG("clearing debug modes\n");
	panel->mode_override = false;
	return 0;
}

int dp_debug_client_write_bw_code(struct dp_debug_client *client, const char *buf, size_t count)
{
	struct dp_panel *panel;
	u32 max_bw_code = 0;

	if (!client || !buf || !client->panel)
		return -ENODEV;

	panel = client->panel;

	if (kstrtoint(buf, 10, &max_bw_code) != 0)
		return -EINVAL;

	if (!is_link_rate_valid(max_bw_code)) {
		DP_ERR("Unsupported bw code %d\n", max_bw_code);
		return -EINVAL;
	}

	panel->max_bw_code = max_bw_code;
	DP_DEBUG("max_bw_code: %d\n", max_bw_code);

	return 0;
}

int dp_debug_client_write_mst_mode(struct dp_debug_client *client, const char *buf, size_t count)
{
	struct dp_parser *parser;
	u32 mst_mode = 0;

	if (!client || !buf || !client->parser)
		return -ENODEV;

	parser = client->parser;

	if (kstrtoint(buf, 10, &mst_mode) != 0)
		return -EINVAL;

	parser->has_mst = mst_mode ? true : false;
	DP_DEBUG("mst_enable: %d\n", mst_mode);

	return 0;
}

int dp_debug_client_write_max_pclk_khz(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	struct dp_parser *parser;
	u32 max_pclk = 0;

	if (!client || !buf || !client->parser)
		return -ENODEV;

	parser = client->parser;

	if (kstrtoint(buf, 10, &max_pclk) != 0)
		return -EINVAL;

	if (max_pclk > client->parser->max_pclk_khz)
		DP_ERR("requested: %d, max_pclk_khz:%d\n", max_pclk,
				client->parser->max_pclk_khz);
	else
		client->max_pclk_khz = max_pclk;

	DP_DEBUG("max_pclk_khz: %d\n", max_pclk);

	return 0;
}

int dp_debug_client_write_tpg(struct dp_debug_client *client, u32 tpg_pattern)
{
	struct dp_panel *panel;

	if (!client || !client->panel)
		return -ENODEV;

	panel = client->panel;

	DP_DEBUG("tpg_pattern: %d\n", tpg_pattern);

	if (panel)
		panel->tpg_config(panel, tpg_pattern);

	client->tpg_pattern = tpg_pattern;

	return 0;
}

int dp_debug_client_write_exe_mode(struct dp_debug_client *client, const char *buf, size_t count)
{
	struct dp_catalog *catalog;
	char exe_mode[4];

	if (!client || !buf || !client->catalog)
		return -ENODEV;

	catalog = client->catalog;

	if (sscanf(buf, "%3s", exe_mode) != 1)
		return -EINVAL;

	if (strcmp(exe_mode, "hw") && strcmp(exe_mode, "sw") && strcmp(exe_mode, "all"))
		return -EINVAL;

	/* Set execution mode in catalog */
	catalog->set_exe_mode(catalog, exe_mode);

	return 0;
}

int dp_debug_client_write_hdcp(struct dp_debug_client *client, const char *buf, size_t count)
{
	int hdcp = 0;

	if (!client || !buf)
		return -ENODEV;

	if (kstrtoint(buf, 10, &hdcp) != 0)
		return -EINVAL;

	client->hdcp_disabled = !hdcp;

	return 0;
}

int dp_debug_client_read_hdcp(struct dp_debug_client *client, char *buf, u32 size)
{
	u32 len = 0;

	if (!client || !buf)
		return -ENODEV;

	len = sizeof(client->hdcp_status);
	len = min_t(u32, size, len);

	memcpy(buf, client->hdcp_status, len);

	return len;
}

int dp_debug_client_write_dump(struct dp_debug_client *client,
		const char *buf, size_t count)
{
	/* This function just validates the register name. */
	if (!client || !buf)
		return -ENODEV;

	/* qfprom register dump not supported */
	if (!strcmp(buf, "qfprom_physical"))
		return -EINVAL;

	return 0;
}

int dp_debug_client_write_sim_mode(struct dp_debug_client *client, bool sim)
{
	struct dp_drv *drv = NULL;
	struct dp_panel *panel = NULL;
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;
	struct sde_connector *sde_conn;

	if (!client)
		return -ENODEV;

	if (sim) {
		client->sim_enable = true;
		dp_debug_client_enable_sim_mode(client, DP_SIM_MODE_ALL);
	} else {
		if (client->hotplug) {
			DP_WARN("sim mode off before hotplug disconnect\n");
			client->hpd->simulate_connect(client->hpd, false);
			client->hotplug = false;
		}
		client->aux->abort(client->aux, true);
		client->ctrl->abort(client->ctrl, true);

		client->sim_enable = false;
		client->mst_edid_idx = 0;
		dp_debug_client_disable_sim_mode(client, DP_SIM_MODE_ALL);
	}

	/* clear override settings in panel */
	drm_connector_list_iter_begin(client->connector->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		sde_conn = to_sde_connector(connector);
		drv = sde_conn->display;
		if (drv && drv->client && drv->client->base_connector &&
				(drv->client->base_connector == client->connector)) {
			panel = dp_mgr_get_panel(drv->client, sde_conn->panel_id);
			if (panel) {
				panel->mode_override = false;
				panel->mst_hide = false;
			}
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}

/* MST Functions */
int dp_debug_client_write_edid_modes_mst(struct dp_debug_client *client, const char *buf)
{
	struct dp_panel *panel = NULL;
	struct drm_connector *connector;
	struct sde_connector *sde_conn;
	int con_id = 0, offset = 0, debug_en = 0;
	int hdisplay, vdisplay, vrefresh, aspect_ratio;

	if (!client)
		return -ENODEV;


	while (sscanf(buf, "%d %d %d %d %d %d%n", &debug_en, &con_id,
			&hdisplay, &vdisplay, &vrefresh, &aspect_ratio,
			&offset) == 6) {
		DP_DEBUG("MST EDID modes: debug_en=%d, %dx%d@%dHz, aspect=%d\n",
			debug_en, hdisplay, vdisplay, vrefresh, aspect_ratio);

		connector = drm_connector_lookup(client->connector->dev,
				NULL, con_id);
		if (connector) {
			struct dp_drv *drv;

			sde_conn = to_sde_connector(connector);
			drv = sde_conn->display;
			panel = dp_mgr_get_panel(drv->client, sde_conn->panel_id);

			if (panel && sde_conn->mst_port) {
				panel->mode_override = true;
				panel->mode.override_timing.h_active = hdisplay;
				panel->mode.override_timing.v_active = vdisplay;
				panel->mode.override_timing.refresh_rate = vrefresh;
				panel->mode.override_timing.aspect_ratio = aspect_ratio;
			} else {
				DP_ERR("connector id %d is not mst\n", con_id);
			}
			drm_connector_put(connector);
		} else {
			DP_ERR("invalid connector id %d\n", con_id);
		}

		buf += offset;
	}

	return 0;
}

int dp_debug_client_write_mst_con_id(struct dp_debug_client *client, int con_id, int status)
{
	struct drm_connector *connector;
	struct sde_connector *sde_conn;
	struct drm_dp_mst_port *mst_port;
	struct dp_panel *dp_panel;

	if (!client)
		return -ENODEV;

	DP_DEBUG("MST connector ID: %d, status: %d\n", con_id, status);

	if (!con_id) {
		DP_DEBUG("clearing mst_con_id\n");
		client->mst_con_id = 0;
		return 0;
	}

	connector = drm_connector_lookup(client->connector->dev, NULL, con_id);
	if (!connector) {
		DP_ERR("invalid connector id %u\n", con_id);
		return -EINVAL;
	}

	sde_conn = to_sde_connector(connector);

	if (!sde_conn->drv_panel || !sde_conn->mst_port) {
		DP_ERR("invalid connector state %d\n", con_id);
		drm_connector_put(connector);
		return -EINVAL;
	}

	client->mst_con_id = con_id;

	if (status == connector_status_unknown) {
		drm_connector_put(connector);
		return 0;
	}

	if (status == connector_status_connected)
		DP_INFO("plug mst connector %d\n", con_id);
	else if (status == connector_status_disconnected)
		DP_INFO("unplug mst connector %d\n", con_id);

	mst_port = sde_conn->mst_port;
	dp_panel = sde_conn->drv_panel;

	if (client->sim_enable)
		dp_sim_update_port_status(client->sim_bridge, mst_port->port_num, status);
	else
		dp_panel->mst_hide = (status == connector_status_disconnected);

	drm_kms_helper_hotplug_event(connector->dev);

	drm_connector_put(connector);
	return 0;
}

int dp_debug_client_write_mst_con_add(struct dp_debug_client *client, const char *buf, size_t count)
{
	const int dp_en = BIT(3), hpd_high = BIT(7), hpd_irq = BIT(8);
	int vdo = dp_en | hpd_high | hpd_irq;

	if (!client || !buf || !client->hpd)
		return -ENODEV;

	DP_DEBUG("MST connector add: %s\n", buf);

	client->mst_sim_add_con = true;
	client->hpd->simulate_attention(client->hpd, vdo);

	return 0;
}

int dp_debug_client_write_mst_con_remove(struct dp_debug_client *client, int con_id)
{
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;
	bool in_list = false;
	const int dp_en = BIT(3), hpd_high = BIT(7), hpd_irq = BIT(8);
	int vdo = dp_en | hpd_high | hpd_irq;

	if (!client || !client->hpd)
		return -ENODEV;

	if (!con_id) {
		DP_ERR("Invalid connector ID: %d\n", con_id);
		return -EINVAL;
	}

	DP_DEBUG("MST connector remove: %d\n", con_id);

	drm_connector_list_iter_begin(client->connector->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->base.id == con_id) {
			in_list = true;
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	if (!in_list) {
		DP_ERR("invalid connector id %u\n", con_id);
		return -EINVAL;
	}

	client->mst_sim_remove_con = true;
	client->mst_sim_remove_con_id = con_id;
	client->hpd->simulate_attention(client->hpd, vdo);

	return 0;
}

int dp_debug_client_write_mst_sideband_mode(struct dp_debug_client *client,
		int mst_sideband_mode, u32 mst_port_cnt)
{
	u8 buf[1];

	if (!client)
		return -ENODEV;

	DP_DEBUG("MST sideband mode: %d, port count: %u\n", mst_sideband_mode, mst_port_cnt);

	if (!mst_port_cnt)
		mst_port_cnt = 1;

	client->mst_edid_idx = 0;

	if (mst_sideband_mode)
		dp_debug_client_disable_sim_mode(client, DP_SIM_MODE_MST);
	else
		dp_debug_client_enable_sim_mode(client, DP_SIM_MODE_MST);

	dp_sim_update_port_num(client->sim_bridge, mst_port_cnt);

	buf[0] = !mst_sideband_mode;
	dp_sim_write_dpcd_reg(client->sim_bridge, buf, 1, DP_MSTM_CAP);

	DP_DEBUG("mst_sideband_mode: %d port_cnt:%d\n",
			mst_sideband_mode, mst_port_cnt);

	return 0;
}

/* Simulation Functions */
int dp_debug_client_write_sim(struct dp_debug_client *client, const char *buf, size_t count)
{
	int sim_mode = 0;

	if (!client || !buf)
		return -ENODEV;

	if (kstrtoint(buf, 10, &sim_mode) != 0)
		return -EINVAL;

	DP_DEBUG("Simulation mode: %d\n", sim_mode);

	return dp_debug_client_write_sim_mode(client, !!sim_mode);
}

int dp_debug_client_write_attention(struct dp_debug_client *client, const char *buf, size_t count)
{
	int vdo = 0;

	if (!client || !buf)
		return -ENODEV;

	if (kstrtoint(buf, 10, &vdo) != 0)
		return -EINVAL;

	DP_DEBUG("Attention simulation: vdo=%d\n", vdo);

	/* Call simulate_attention through client */
	if (client->simulate_attention)
		return client->simulate_attention(client, vdo);

	return 0;
}

int dp_debug_client_simulate_attention(struct dp_debug_client *client, int vdo)
{
	if (!client || !client->hpd)
		return -ENODEV;

	DP_DEBUG("Simulating attention: vdo=0x%x\n", vdo);

	return client->hpd->simulate_attention(client->hpd, vdo);
}

/* MMRM Function */
int dp_debug_client_write_mmrm_clk_cb(struct dp_debug_client *client, const char *buf, size_t count)
{
	int cb_type = 0;
	struct dss_clk_mmrm_cb mmrm_cb_data;
	struct mmrm_client_notifier_data notifier_data;
	struct dp_drv *dp_drv;
	struct platform_device *pdev;

	if (!client || !buf)
		return -ENODEV;

	if (kstrtoint(buf, 10, &cb_type) != 0)
		return -EINVAL;

	if (cb_type != MMRM_CLIENT_RESOURCE_VALUE_CHANGE) {
		DP_ERR("Invalid MMRM callback type: %d\n", cb_type);
		return -EINVAL;
	}

	DP_DEBUG("MMRM clock callback: type=%d\n", cb_type);

	/* Get the dp_drv instance from the platform device */
	pdev = to_platform_device(client->dev);
	dp_drv = platform_get_drvdata(pdev);

	if (!dp_drv) {
		DP_ERR("dp_drv is NULL\n");
		return -ENODEV;
	}

	/* Prepare MMRM notification data */
	notifier_data.cb_type = MMRM_CLIENT_RESOURCE_VALUE_CHANGE;
	mmrm_cb_data.phandle = (void *)dp_drv;
	notifier_data.pvt_data = (void *)&mmrm_cb_data;

	/* Call the MMRM callback function */
	dp_mgr_mmrm_callback(&notifier_data);

	return 0;
}

int dp_debug_client_get(struct dp_debug_client *client)
{
	if (!client)
		return -EINVAL;

	/* Register debug handler functions */
	client->read_dpcd = dp_debug_client_read_dpcd;
	client->read_crc = dp_debug_client_read_crc;
	client->read_connected = dp_debug_client_read_connected;
	client->read_info = dp_debug_client_read_info;
	client->read_bw_code = dp_debug_client_read_bw_code;
	client->read_tpg = dp_debug_client_read_tpg;
	client->read_dump = dp_debug_client_read_dump;
	client->read_mst_mode = dp_debug_client_read_mst_mode;
	client->read_max_pclk_khz = dp_debug_client_read_max_pclk_khz;
	client->read_hdr = dp_debug_client_read_hdr;
	client->read_edid_modes = dp_debug_client_read_edid_modes;
	client->read_edid_modes_mst = dp_debug_client_read_edid_modes_mst;
	client->read_mst_conn_info = dp_debug_client_read_mst_conn_info;

	client->write_edid = dp_debug_client_write_edid;
	client->write_dpcd = dp_debug_client_write_dpcd;
	client->write_hpd = dp_debug_client_write_hpd;
	client->write_edid_modes = dp_debug_client_write_edid_modes;
	client->write_bw_code = dp_debug_client_write_bw_code;
	client->write_mst_mode = dp_debug_client_write_mst_mode;
	client->write_max_pclk_khz = dp_debug_client_write_max_pclk_khz;
	client->write_tpg = dp_debug_client_write_tpg;
	client->write_exe_mode = dp_debug_client_write_exe_mode;
	client->write_hdcp = dp_debug_client_write_hdcp;
	client->write_dump = dp_debug_client_write_dump;
	client->read_hdcp = dp_debug_client_read_hdcp;
	client->write_sim_mode = dp_debug_client_write_sim_mode;

	/* MST Functions */
	client->write_edid_modes_mst = dp_debug_client_write_edid_modes_mst;
	client->write_mst_con_id = dp_debug_client_write_mst_con_id;
	client->write_mst_con_add = dp_debug_client_write_mst_con_add;
	client->write_mst_con_remove = dp_debug_client_write_mst_con_remove;
	client->write_mst_sideband_mode = dp_debug_client_write_mst_sideband_mode;

	/* Simulation Functions */
	client->write_sim = dp_debug_client_write_sim;
	client->write_attention = dp_debug_client_write_attention;
	client->simulate_attention = dp_debug_client_simulate_attention;

	/* MMRM Function */
	client->write_mmrm_clk_cb = dp_debug_client_write_mmrm_clk_cb;

	client->abort = dp_debug_client_abort;
	return 0;
}

void dp_debug_client_put(struct dp_debug_client *client)
{
	if (!client)
		return;

	kfree(client);
}
