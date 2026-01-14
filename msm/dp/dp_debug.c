// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 0))
#include <drm/display/drm_dp_mst_helper.h>
#else
#include <drm/drm_dp_mst_helper.h>
#endif
#include <drm/drm_probe_helper.h>

#include "drm/drm_connector.h"
#include "sde_connector.h"
#include "dp_mst_sim.h"
#include "dp_mst_drm.h"
#include "dp_debug_client.h"
#include "hfi/dp_debug_client_hfi.h"

#define DEBUG_NAME "drm_dp"

struct dp_debug_private {
	struct dentry *root;

	u32 dpcd_offset;
	u32 dpcd_size;

	bool hotplug;
	u32 sim_mode;

	struct drm_connector **connector;

	char exe_mode[SZ_32];
	char reg_dump[SZ_32];

	const char *name;

	struct device *dev;
	struct dp_debug_client client;
	struct mutex lock;

	bool dsc_feature_enable;
	bool fec_feature_enable;
	bool has_widebus;
	bool fifo_error_enable;
	bool ssc_en;

	u32 max_lclk_khz;
	u32 lane_count;
	u32 link_bw_code;
	u32 max_supported_bpp;
	u32 disp_op;
};

static ssize_t dp_debug_write_edid(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	ssize_t rc = count;

	if (!priv || !priv->client.write_edid)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buff, count)) {
		rc = -EFAULT;
		goto bail;
	}

	buf[count] = '\0';

	mutex_lock(&priv->lock);
	if (priv->client.write_edid)
		priv->client.write_edid(&priv->client, buf, count);
	rc = count;
	mutex_unlock(&priv->lock);
bail:
	kfree(buf);
	return rc;
}

static ssize_t dp_debug_write_dpcd(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	ssize_t rc = count;

	if (!priv || !priv->client.write_dpcd)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buff, count)) {
		rc = -EFAULT;
		goto bail;
	}

	buf[count] = '\0';

	mutex_lock(&priv->lock);
	if (priv->client.write_dpcd)
		priv->client.write_dpcd(&priv->client, buf, count);
	rc = count;
	mutex_unlock(&priv->lock);

bail:
	kfree(buf);
	return rc;
}

static ssize_t dp_debug_read_dpcd(struct file *file,
		char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_dpcd)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);
	if (priv->client.read_dpcd)
		priv->client.read_dpcd(&priv->client, (u8 *)buf,
							buf_size, priv->dpcd_offset);
	rc = count;
	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, rc);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}

static ssize_t dp_debug_read_crc(struct file *file, char __user *user_buff, size_t count,
		loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_crc)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);
	if (priv->client.read_crc)
		priv->client.read_crc(&priv->client, buf, buf_size);
	rc = count;
	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, rc);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}

static ssize_t dp_debug_write_hpd(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	ssize_t rc = count;

	if (!priv || !priv->client.write_hpd)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buff, count)) {
		rc = -EFAULT;
		goto bail;
	}

	buf[count] = '\0';

	mutex_lock(&priv->lock);
	if (priv->client.write_hpd)
		priv->client.write_hpd(&priv->client, buf, count);
	rc = count;
	mutex_unlock(&priv->lock);

bail:
	kfree(buf);
	return rc;
}

static ssize_t dp_debug_write_edid_modes(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	ssize_t rc = count;

	if (!priv || !priv->client.write_edid_modes)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buff, count)) {
		rc = -EFAULT;
		goto bail;
	}

	buf[count] = '\0';

	mutex_lock(&priv->lock);
	if (priv->client.write_edid_modes)
		priv->client.write_edid_modes(&priv->client, buf, count);
	rc = count;
	mutex_unlock(&priv->lock);

bail:
	kfree(buf);
	return rc;
}

static ssize_t dp_debug_write_edid_modes_mst(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char buf[SZ_512];
	char *read_buf;
	size_t len = 0;

	if (!priv)
		return -ENODEV;

	mutex_lock(&priv->lock);

	if (*ppos)
		goto end;

	len = min_t(size_t, count, SZ_512 - 1);
	if (copy_from_user(buf, user_buff, len))
		goto end;

	buf[len] = '\0';
	read_buf = buf;

	if (priv->client.write_edid_modes_mst)
		priv->client.write_edid_modes_mst(&priv->client, read_buf);
end:
	mutex_unlock(&priv->lock);
	return len;
}

static ssize_t dp_debug_write_mst_con_id(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char buf[SZ_32];
	size_t len = 0;
	int con_id = 0, status;

	if (!priv)
		return -ENODEV;

	mutex_lock(&priv->lock);

	if (*ppos)
		goto end;

	/* Leave room for termination char */
	len = min_t(size_t, count, SZ_32 - 1);
	if (copy_from_user(buf, user_buff, len))
		goto end;

	buf[len] = '\0';

	if (sscanf(buf, "%d %d", &con_id, &status) != 2)
		goto end;

	if (priv->client.write_mst_con_id)
		priv->client.write_mst_con_id(&priv->client, con_id, status);
end:
	mutex_unlock(&priv->lock);
	return len;
}

static ssize_t dp_debug_write_mst_con_add(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char buf[SZ_32];
	size_t len = 0;

	if (!priv)
		return -ENODEV;

	if (*ppos)
		return 0;

	/* Leave room for termination char */
	len = min_t(size_t, count, SZ_32 - 1);
	if (copy_from_user(buf, user_buff, len))
		goto end;

	if (priv->client.write_mst_con_add)
		priv->client.write_mst_con_add(&priv->client, buf, count);
end:
	return len;
}

static ssize_t dp_debug_write_mst_con_remove(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char buf[SZ_32];
	size_t len = 0;
	int con_id = 0;

	if (!priv)
		return -ENODEV;

	if (*ppos)
		return 0;

	/* Leave room for termination char */
	len = min_t(size_t, count, SZ_32 - 1);
	if (copy_from_user(buf, user_buff, len))
		goto end;

	buf[len] = '\0';

	if (sscanf(buf, "%d", &con_id) != 1) {
		len = 0;
		goto end;
	}

	if (!con_id)
		goto end;

	if (priv->client.write_mst_con_remove)
		priv->client.write_mst_con_remove(&priv->client, con_id);
end:
	return len;
}

static ssize_t dp_debug_mmrm_clk_cb_write(struct file *file,
		 const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	ssize_t rc = count;

	if (!priv || !priv->client.write_mmrm_clk_cb)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buff, count)) {
		rc = -EFAULT;
		goto bail;
	}

	buf[count] = '\0';

	mutex_lock(&priv->lock);
	if (priv->client.write_mmrm_clk_cb)
		priv->client.write_mmrm_clk_cb(&priv->client, buf, count);
	rc = count;
	mutex_unlock(&priv->lock);

bail:
	kfree(buf);
	return rc;
}

static ssize_t dp_debug_bw_code_write(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	ssize_t rc = count;

	if (!priv || !priv->client.write_bw_code)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buff, count)) {
		rc = -EFAULT;
		goto bail;
	}

	buf[count] = '\0';

	mutex_lock(&priv->lock);
	if (priv->client.write_bw_code)
		priv->client.write_bw_code(&priv->client, buf, count);
	rc = count;
	mutex_unlock(&priv->lock);

bail:
	kfree(buf);
	return rc;
}

static ssize_t dp_debug_mst_mode_read(struct file *file,
	char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_mst_mode)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);

	rc = priv->client.read_mst_mode(&priv->client, buf, buf_size);
	if (rc > 0)
		len = rc;

	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, len);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}

static ssize_t dp_debug_mst_mode_write(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	ssize_t rc = count;

	if (!priv || !priv->client.write_mst_mode)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buff, count)) {
		rc = -EFAULT;
		goto bail;
	}

	buf[count] = '\0';

	mutex_lock(&priv->lock);
	priv->client.write_mst_mode(&priv->client, buf, count);
	rc = count;
	mutex_unlock(&priv->lock);

bail:
	kfree(buf);
	return rc;
}

static ssize_t dp_debug_max_pclk_khz_write(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	ssize_t rc = count;

	if (!priv || !priv->client.write_max_pclk_khz)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buff, count)) {
		rc = -EFAULT;
		goto bail;
	}

	buf[count] = '\0';

	mutex_lock(&priv->lock);
	priv->client.write_max_pclk_khz(&priv->client, buf, count);
	rc = count;
	mutex_unlock(&priv->lock);

bail:
	kfree(buf);
	return rc;
}

static ssize_t dp_debug_max_pclk_khz_read(struct file *file,
	char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_max_pclk_khz)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);

	rc = priv->client.read_max_pclk_khz(&priv->client, buf, buf_size);
	if (rc > 0)
		len = rc;

	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, len);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}

static ssize_t dp_debug_mst_sideband_mode_write(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char buf[SZ_8];
	size_t len = 0;
	int mst_sideband_mode = 0;
	u32 mst_port_cnt = 0;
	ssize_t rc = count;

	if (!priv)
		return -ENODEV;

	if (*ppos)
		return 0;

	/* Leave room for termination char */
	len = min_t(size_t, count, SZ_8 - 1);
	if (copy_from_user(buf, user_buff, len))
		return -EFAULT;

	buf[len] = '\0';

	if (sscanf(buf, "%d %u", &mst_sideband_mode, &mst_port_cnt) != 2) {
		DP_ERR("invalid input\n");
		return -EINVAL;
	}

	mutex_lock(&priv->lock);
	if (priv->client.write_mst_sideband_mode) {
		priv->client.write_mst_sideband_mode(&priv->client,
			mst_sideband_mode, mst_port_cnt);
		rc = count;
	}
	mutex_unlock(&priv->lock);

	return rc;
}

static ssize_t dp_debug_tpg_write(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char buf[SZ_8];
	size_t len = 0;
	u32 tpg_pattern = 0;

	if (!priv)
		return -ENODEV;

	if (*ppos)
		return 0;

	/* Leave room for termination char */
	len = min_t(size_t, count, SZ_8 - 1);
	if (copy_from_user(buf, user_buff, len))
		goto bail;

	buf[len] = '\0';

	if (kstrtoint(buf, 10, &tpg_pattern) != 0)
		goto bail;

	DP_DEBUG("tpg_pattern: %d\n", tpg_pattern);

	if (priv->client.write_tpg)
		priv->client.write_tpg(&priv->client, tpg_pattern);

bail:
	return len;
}

static ssize_t dp_debug_write_exe_mode(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	ssize_t rc = count;

	if (!priv || !priv->client.write_exe_mode)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buff, count)) {
		rc = -EFAULT;
		goto bail;
	}

	buf[count] = '\0';

	mutex_lock(&priv->lock);
	priv->client.write_exe_mode(&priv->client, buf, count);
	rc = count;
	mutex_unlock(&priv->lock);

bail:
	kfree(buf);
	return rc;
}

static ssize_t dp_debug_read_connected(struct file *file,
		char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_connected)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);

	rc = priv->client.read_connected(&priv->client, buf, buf_size);
	if (rc > 0)
		len = rc;

	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, len);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}

static ssize_t dp_debug_write_hdcp(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	ssize_t rc = count;

	if (!priv || !priv->client.write_hdcp)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buff, count)) {
		rc = -EFAULT;
		goto bail;
	}

	buf[count] = '\0';

	mutex_lock(&priv->lock);
	priv->client.write_hdcp(&priv->client, buf, count);
	rc = count;
	mutex_unlock(&priv->lock);

bail:
	kfree(buf);
	return rc;
}

static ssize_t dp_debug_read_hdcp(struct file *file,
		char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_hdcp)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);

	rc = priv->client.read_hdcp(&priv->client, buf, buf_size);
	if (rc > 0)
		len = rc;

	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, len);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}


static ssize_t dp_debug_read_edid_modes(struct file *file,
		char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_edid_modes)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);

	rc = priv->client.read_edid_modes(&priv->client, buf, buf_size);
	if (rc > 0)
		len = rc;

	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, len);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}

static ssize_t dp_debug_read_edid_modes_mst(struct file *file,
		char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_edid_modes_mst)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);

	rc = priv->client.read_edid_modes_mst(&priv->client, buf, buf_size);
	if (rc > 0)
		len = rc;

	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, len);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}

static ssize_t dp_debug_read_mst_con_id(struct file *file,
		char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	u32 len = 0, ret = 0, max_size = SZ_4K;
	int rc = 0;

	if (!priv) {
		DP_ERR("invalid data\n");
		rc = -ENODEV;
		goto error;
	}

	if (*ppos)
		goto error;

	buf = kzalloc(SZ_4K, GFP_KERNEL);
	if (!buf) {
		rc = -ENOMEM;
		goto error;
	}

	ret = scnprintf(buf, max_size, "%u\n", priv->client.mst_con_id);
	len += ret;

	len = min_t(size_t, count, len);
	if (copy_to_user(user_buff, buf, len)) {
		kfree(buf);
		rc = -EFAULT;
		goto error;
	}

	*ppos += len;
	kfree(buf);

	return len;
error:
	return rc;
}

static ssize_t dp_debug_read_mst_conn_info(struct file *file,
		char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_mst_conn_info)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);

	rc = priv->client.read_mst_conn_info(&priv->client, buf, buf_size);
	if (rc > 0)
		len = rc;

	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, len);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}

static ssize_t dp_debug_read_info(struct file *file, char __user *user_buff,
		size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_info)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);

	rc = priv->client.read_info(&priv->client, buf, buf_size);
	if (rc > 0)
		len = rc;

	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, len);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}

static ssize_t dp_debug_bw_code_read(struct file *file,
	char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_bw_code)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);

	rc = priv->client.read_bw_code(&priv->client, buf, buf_size);
	if (rc > 0)
		len = rc;

	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, len);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}

static ssize_t dp_debug_tpg_read(struct file *file,
	char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_tpg)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);

	rc = priv->client.read_tpg(&priv->client, buf, buf_size);
	if (rc > 0)
		len = rc;

	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, len);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}


static ssize_t dp_debug_read_hdr(struct file *file,
		char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_hdr)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);

	rc = priv->client.read_hdr(&priv->client, buf, buf_size, 0);
	if (rc > 0)
		len = rc;

	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, len);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}

static ssize_t dp_debug_read_hdr_mst(struct file *file,
		char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_hdr)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);

	rc = priv->client.read_hdr(&priv->client, buf, buf_size, 1);
	if (rc > 0)
		len = rc;

	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, len);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}

static ssize_t dp_debug_write_sim(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	ssize_t rc = count;

	if (!priv || !priv->client.write_sim)
		return -ENODEV;

	if (*ppos)
		return 0;

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buff, count)) {
		rc = -EFAULT;
		goto bail;
	}

	buf[count] = '\0';

	mutex_lock(&priv->lock);
	if (priv->client.write_sim)
		priv->client.write_sim(&priv->client, buf, count);
	rc = count;
	mutex_unlock(&priv->lock);

bail:
	kfree(buf);
	return rc;
}

static ssize_t dp_debug_write_attention(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char buf[SZ_8];
	size_t len = 0;
	int vdo;

	if (!priv)
		return -ENODEV;

	if (*ppos)
		return 0;

	/* Leave room for termination char */
	len = min_t(size_t, count, SZ_8 - 1);
	if (copy_from_user(buf, user_buff, len))
		goto end;

	buf[len] = '\0';

	if (kstrtoint(buf, 10, &vdo) != 0)
		goto end;

	/* Call simulate_attention through client */
	if (priv->client.simulate_attention)
		priv->client.simulate_attention(&priv->client, vdo);
end:
	return len;
}

static ssize_t dp_debug_write_dump(struct file *file,
		const char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char buf[SZ_32];
	size_t len = 0;

	if (!priv)
		return -ENODEV;

	if (*ppos)
		return 0;

	/* Leave room for termination char */
	len = min_t(size_t, count, SZ_32 - 1);
	if (copy_from_user(buf, user_buff, len))
		goto end;

	buf[len] = '\0';

	if (sscanf(buf, "%31s", priv->reg_dump) != 1)
		goto end;

	/* qfprom register dump not supported */
	if (!strcmp(priv->reg_dump, "qfprom_physical"))
		strscpy(priv->reg_dump, "clear", sizeof(priv->reg_dump));
end:
	return len;
}

static ssize_t dp_debug_read_dump(struct file *file,
		char __user *user_buff, size_t count, loff_t *ppos)
{
	struct dp_debug_private *priv = file->private_data;
	char *buf;
	int const buf_size = SZ_4K;
	u32 len = 0;
	int rc;

	if (!priv || !priv->client.read_dump)
		return -ENODEV;

	if (*ppos)
		return 0;

	if (!strlen(priv->reg_dump))
		return 0;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->lock);

	rc = priv->client.read_dump(&priv->client, buf, buf_size, priv->reg_dump);
	if (rc > 0)
		len = rc;

	mutex_unlock(&priv->lock);

	len = min_t(size_t, count, len);
	if (len > 0 && !copy_to_user(user_buff, buf, len))
		*ppos += len;

	kfree(buf);
	return len;
}

static const struct file_operations dp_debug_fops = {
	.open = simple_open,
	.read = dp_debug_read_info,
};

static const struct file_operations edid_modes_fops = {
	.open = simple_open,
	.read = dp_debug_read_edid_modes,
	.write = dp_debug_write_edid_modes,
};

static const struct file_operations edid_modes_mst_fops = {
	.open = simple_open,
	.read = dp_debug_read_edid_modes_mst,
	.write = dp_debug_write_edid_modes_mst,
};

static const struct file_operations mst_conn_info_fops = {
	.open = simple_open,
	.read = dp_debug_read_mst_conn_info,
};

static const struct file_operations mst_con_id_fops = {
	.open = simple_open,
	.read = dp_debug_read_mst_con_id,
	.write = dp_debug_write_mst_con_id,
};

static const struct file_operations mst_con_add_fops = {
	.open = simple_open,
	.write = dp_debug_write_mst_con_add,
};

static const struct file_operations mst_con_remove_fops = {
	.open = simple_open,
	.write = dp_debug_write_mst_con_remove,
};

static const struct file_operations hpd_fops = {
	.open = simple_open,
	.write = dp_debug_write_hpd,
};

static const struct file_operations edid_fops = {
	.open = simple_open,
	.write = dp_debug_write_edid,
};

static const struct file_operations dpcd_fops = {
	.open = simple_open,
	.write = dp_debug_write_dpcd,
	.read = dp_debug_read_dpcd,
};

static const struct file_operations crc_fops = {
	.open = simple_open,
	.read = dp_debug_read_crc,
};

static const struct file_operations connected_fops = {
	.open = simple_open,
	.read = dp_debug_read_connected,
};

static const struct file_operations bw_code_fops = {
	.open = simple_open,
	.read = dp_debug_bw_code_read,
	.write = dp_debug_bw_code_write,
};
static const struct file_operations exe_mode_fops = {
	.open = simple_open,
	.write = dp_debug_write_exe_mode,
};

static const struct file_operations tpg_fops = {
	.open = simple_open,
	.read = dp_debug_tpg_read,
	.write = dp_debug_tpg_write,
};

static const struct file_operations hdr_fops = {
	.open = simple_open,
	.read = dp_debug_read_hdr,
};

static const struct file_operations hdr_mst_fops = {
	.open = simple_open,
	.read = dp_debug_read_hdr_mst,
};

static const struct file_operations sim_fops = {
	.open = simple_open,
	.write = dp_debug_write_sim,
};

static const struct file_operations attention_fops = {
	.open = simple_open,
	.write = dp_debug_write_attention,
};

static const struct file_operations dump_fops = {
	.open = simple_open,
	.write = dp_debug_write_dump,
	.read = dp_debug_read_dump,
};

static const struct file_operations mst_mode_fops = {
	.open = simple_open,
	.write = dp_debug_mst_mode_write,
	.read = dp_debug_mst_mode_read,
};

static const struct file_operations mst_sideband_mode_fops = {
	.open = simple_open,
	.write = dp_debug_mst_sideband_mode_write,
};

static const struct file_operations max_pclk_khz_fops = {
	.open = simple_open,
	.write = dp_debug_max_pclk_khz_write,
	.read = dp_debug_max_pclk_khz_read,
};

static const struct file_operations hdcp_fops = {
	.open = simple_open,
	.write = dp_debug_write_hdcp,
	.read = dp_debug_read_hdcp,
};

static const struct file_operations mmrm_clk_cb_fops = {
	.open = simple_open,
	.write = dp_debug_mmrm_clk_cb_write,
};

static int dp_debug_init_mst(struct dp_debug_private *priv, struct dentry *dir)
{
	int rc = 0;
	struct dentry *file;

	file = debugfs_create_file("mst_con_id", 0644, dir,
					priv, &mst_con_id_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs create mst_con_id failed, rc=%d\n",
		       priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("mst_con_info", 0644, dir,
					priv, &mst_conn_info_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs create mst_conn_info failed, rc=%d\n",
		       priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("mst_con_add", 0644, dir,
					priv, &mst_con_add_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DRM_ERROR("[%s] debugfs create mst_con_add failed, rc=%d\n",
		       priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("mst_con_remove", 0644, dir,
					priv, &mst_con_remove_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DRM_ERROR("[%s] debugfs create mst_con_remove failed, rc=%d\n",
		       priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("mst_mode", 0644, dir,
			priv, &mst_mode_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs mst_mode failed, rc=%d\n",
		       priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("mst_sideband_mode", 0644, dir,
			priv, &mst_sideband_mode_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs mst_sideband_mode failed, rc=%d\n",
		       priv->name, rc);
		return rc;
	}

	debugfs_create_u32("mst_edid_idx", 0644, dir, &priv->client.mst_edid_idx);

	return rc;
}

static int dp_debug_init_link(struct dp_debug_private *priv,
		struct dentry *dir)
{
	int rc = 0;
	struct dentry *file;

	file = debugfs_create_file("max_bw_code", 0644, dir,
			priv, &bw_code_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs max_bw_code failed, rc=%d\n",
		       priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("max_pclk_khz", 0644, dir,
			priv, &max_pclk_khz_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs max_pclk_khz failed, rc=%d\n",
		       priv->name, rc);
		return rc;
	}

	debugfs_create_u32("max_lclk_khz", 0644, dir, &priv->max_lclk_khz);

	debugfs_create_u32("lane_count", 0644, dir, &priv->lane_count);

	debugfs_create_u32("link_bw_code", 0644, dir, &priv->link_bw_code);

	debugfs_create_u32("max_bpp", 0644, dir, &priv->max_supported_bpp);

	file = debugfs_create_file("mmrm_clk_cb", 0644, dir, priv, &mmrm_clk_cb_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs mmrm_clk_cb failed, rc=%d\n", priv->name, rc);
		return rc;
	}

	return rc;
}

static int dp_debug_init_hdcp(struct dp_debug_private *priv,
		struct dentry *dir)
{
	int rc = 0;

	debugfs_create_bool("hdcp_wait_sink_sync", 0644, dir,
		&priv->client.hdcp_wait_sink_sync);
	debugfs_create_bool("force_encryption", 0644, dir,
		&priv->client.force_encryption);

	return rc;
}

static int dp_debug_init_sink_caps(struct dp_debug_private *priv,
		struct dentry *dir)
{
	int rc = 0;
	struct dentry *file;

	file = debugfs_create_file("edid_modes", 0644, dir,
					priv, &edid_modes_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs create edid_modes failed, rc=%d\n",
		       priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("edid_modes_mst", 0644, dir,
					priv, &edid_modes_mst_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs create edid_modes_mst failed, rc=%d\n",
		       priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("edid", 0644, dir,
					priv, &edid_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs edid failed, rc=%d\n",
			priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("dpcd", 0644, dir,
					priv, &dpcd_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs dpcd failed, rc=%d\n",
			priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("crc", 0644, dir, priv, &crc_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs crc failed, rc=%d\n", DEBUG_NAME, rc);
		return rc;
	}

	return rc;
}

static int dp_debug_init_status(struct dp_debug_private *priv,
		struct dentry *dir)
{
	int rc = 0;
	struct dentry *file;

	file = debugfs_create_file("dp_debug", 0444, dir,
				priv, &dp_debug_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs create file failed, rc=%d\n",
		       priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("connected", 0444, dir,
					priv, &connected_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs connected failed, rc=%d\n",
			priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("hdr", 0400, dir, priv, &hdr_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs hdr failed, rc=%d\n",
			priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("hdr_mst", 0400, dir, priv, &hdr_mst_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs hdr_mst failed, rc=%d\n",
			priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("hdcp", 0644, dir, priv, &hdcp_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs hdcp failed, rc=%d\n",
			priv->name, rc);
		return rc;
	}

	return rc;
}

static int dp_debug_init_sim(struct dp_debug_private *priv, struct dentry *dir)
{
	int rc = 0;
	struct dentry *file;

	file = debugfs_create_file("hpd", 0644, dir, priv, &hpd_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs hpd failed, rc=%d\n",
			priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("sim", 0644, dir, priv, &sim_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs sim failed, rc=%d\n",
			priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("attention", 0644, dir,
			priv, &attention_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs attention failed, rc=%d\n",
			priv->name, rc);
		return rc;
	}

	debugfs_create_bool("skip_uevent", 0644, dir, &priv->client.skip_uevent);
	debugfs_create_bool("force_multi_func", 0644, dir,
			&priv->client.force_multi_func);

	return rc;
}

static int dp_debug_init_dsc_fec(struct dp_debug_private *priv,
		struct dentry *dir)
{
	int rc = 0;

	debugfs_create_bool("dsc_feature_enable", 0644, dir, &priv->dsc_feature_enable);

	debugfs_create_bool("fec_feature_enable", 0644, dir, &priv->fec_feature_enable);

	return rc;
}

static int dp_debug_init_tpg(struct dp_debug_private *priv, struct dentry *dir)
{
	int rc = 0;
	struct dentry *file;

	file = debugfs_create_file("tpg_ctrl", 0644, dir,
			priv, &tpg_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs tpg failed, rc=%d\n",
		       priv->name, rc);
		return rc;
	}

	return rc;
}

static int dp_debug_init_reg_dump(struct dp_debug_private *priv,
		struct dentry *dir)
{
	int rc = 0;
	struct dentry *file;

	file = debugfs_create_file("exe_mode", 0644, dir,
			priv, &exe_mode_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs register failed, rc=%d\n",
		       priv->name, rc);
		return rc;
	}

	file = debugfs_create_file("dump", 0644, dir,
		priv, &dump_fops);
	if (IS_ERR_OR_NULL(file)) {
		rc = PTR_ERR(file);
		DP_ERR("[%s] debugfs dump failed, rc=%d\n",
			priv->name, rc);
		return rc;
	}

	return rc;
}

static int dp_debug_init_feature_toggle(struct dp_debug_private *priv,
		struct dentry *dir)
{
	int rc = 0;

	debugfs_create_bool("ssc_enable", 0644, dir, &priv->ssc_en);
	debugfs_create_bool("widebus_mode", 0644, dir, &priv->has_widebus);

	return rc;
}

static int dp_debug_init_configs(struct dp_debug_private *priv,
		struct dentry *dir)
{
	int rc = 0;

	debugfs_create_ulong("connect_notification_delay_ms", 0644, dir,
		&priv->client.connect_notification_delay_ms);

	priv->client.connect_notification_delay_ms =
		DEFAULT_CONNECT_NOTIFICATION_DELAY_MS;

	debugfs_create_u32("disconnect_delay_ms", 0644, dir,
		&priv->client.disconnect_delay_ms);

	priv->client.disconnect_delay_ms = DEFAULT_DISCONNECT_DELAY_MS;

	return rc;

}

static int dp_debug_init_fifo_error(struct dp_debug_private *priv,
		struct dentry *dir)
{
	int rc = 0;

	debugfs_create_bool("fifo_error_enable", 0644, dir, &priv->fifo_error_enable);
	return rc;
}

static int dp_debug_init(struct dp_debug_private *priv)
{
	int rc = 0;
	struct dentry *dir;

	if (!IS_ENABLED(CONFIG_DEBUG_FS)) {
		DP_WARN("Not creating priv root dir.");
		priv->root = NULL;
		return 0;
	}

	priv->name = of_get_property(priv->dev->of_node, "label", NULL);
	if (!priv->name)
		priv->name = DEBUG_NAME;

	dir = debugfs_create_dir(priv->name, NULL);
	if (IS_ERR_OR_NULL(dir)) {
		if (!dir)
			rc = -EINVAL;
		else
			rc = PTR_ERR(dir);
		DP_ERR("[%s] debugfs create dir failed, rc = %d\n",
		       priv->name, rc);
		goto error;
	}

	priv->root = dir;

	rc = dp_debug_init_status(priv, dir);
	if (rc)
		goto error_remove_dir;

	rc = dp_debug_init_sink_caps(priv, dir);
	if (rc)
		goto error_remove_dir;

	rc = dp_debug_init_mst(priv, dir);
	if (rc)
		goto error_remove_dir;

	rc = dp_debug_init_link(priv, dir);
	if (rc)
		goto error_remove_dir;

	rc = dp_debug_init_hdcp(priv, dir);
	if (rc)
		goto error_remove_dir;

	rc = dp_debug_init_sim(priv, dir);
	if (rc)
		goto error_remove_dir;

	rc = dp_debug_init_dsc_fec(priv, dir);
	if (rc)
		goto error_remove_dir;

	rc = dp_debug_init_tpg(priv, dir);
	if (rc)
		goto error_remove_dir;

	rc = dp_debug_init_reg_dump(priv, dir);
	if (rc)
		goto error_remove_dir;

	rc = dp_debug_init_feature_toggle(priv, dir);
	if (rc)
		goto error_remove_dir;

	rc = dp_debug_init_configs(priv, dir);
	if (rc)
		goto error_remove_dir;

	rc = dp_debug_init_fifo_error(priv, dir);
	if (rc)
		goto error_remove_dir;

	/* Get priv client instance */
	if (IS_DISP_OP_HWIO(priv->disp_op))
		rc = dp_debug_client_get(&priv->client);
	else if (IS_DISP_OP_HFI(priv->disp_op))
		rc = dp_debug_client_hfi_get(&priv->client);

	if (rc)
		goto error_remove_dir;

	return 0;

error_remove_dir:
	debugfs_remove_recursive(dir);
error:
	return rc;
}

struct dp_debug_client *dp_debug_get(struct device *dev, u32 disp_op)
{
	int rc = 0;
	struct dp_debug_private *priv;
	char *buf = NULL;
	int const buf_size = SZ_4K;

	buf = kzalloc(buf_size, GFP_KERNEL);

	if (!dev || !buf) {
		DP_ERR("invalid input\n");
		rc = -EINVAL;
		goto error;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		rc = -ENOMEM;
		goto error;
	}

	priv->dev = dev;
	priv->disp_op = disp_op;

	mutex_init(&priv->lock);

	priv->client.dev = dev;

	rc = dp_debug_init(priv);
	if (rc) {
		devm_kfree(dev, priv);
		goto error;
	}

	if (priv->client.read_max_pclk_khz) {
		priv->client.read_max_pclk_khz(&priv->client, buf, buf_size);
		priv->client.max_pclk_khz = *buf;
	}

	kfree(buf);
	return &priv->client;
error:
	return ERR_PTR(rc);
}

static int dp_debug_deinit(struct dp_debug_client *client)
{
	struct dp_debug_private *priv;

	if (!client)
		return -EINVAL;

	priv = container_of(client, struct dp_debug_private, client);

	debugfs_remove_recursive(priv->root);

	dp_debug_client_put(&priv->client);

	return 0;
}

void dp_debug_put(struct dp_debug_client *client)
{
	struct dp_debug_private *priv;

	if (!client)
		return;

	priv = container_of(client, struct dp_debug_private, client);

	dp_debug_deinit(client);

	mutex_destroy(&priv->lock);
}
