// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2019-2020. Linaro Limited.
 */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/types.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/component.h>
#include <linux/workqueue.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <linux/firmware.h>
#include <linux/hdmi.h>
#include <drm/drm_print.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_file.h>
#include <drm/drm_device.h>
#include <linux/string.h>

#define EDID_SEG_SIZE 256
#define READ_BUF_MAX_SIZE 128
#define WRITE_BUF_MAX_SIZE (LT9611UXD_SRAM_PAGE_SIZE + 1)
#define EDID_TIMEOUT_MS 10000
#define LT9611UXD_FW_BUFF_SIZE (64 * 1024)
#define LT9611UXD_SRAM_PAGE_SIZE 256
#define LT9611UXD_FW_BIN "lt9611uxd_fw.bin"

struct lt9611uxd_reg_cfg {
	u8 reg;
	u8 val;
};

enum lt9611uxd_fw_upgrade_status {
	UPDATE_SUCCESS = 0,
	UPDATE_RUNNING = 1,
	UPDATE_FAILED = 2,
};

enum lt9611uxd_power_mode {
	DISABLED_MODE = 0,
	NORMAL_MODE,
	STANDBY_MODE,
	SLEEP_MODE,
	POWER_MODE_MAX,
};

enum lt9611uxd_ports {
	PORT_SWAP_A = 0,
	PORT_SWAP_B,
	PORT_SWAP_AB,
	PORT_MAX,
};

struct lt9611uxd_vreg {
	struct regulator *vreg; /* vreg handle */
	char vreg_name[32];
	int min_voltage;
	int max_voltage;
	int enable_load;
	int disable_load;
	int pre_on_sleep;
	int post_on_sleep;
	int pre_off_sleep;
	int post_off_sleep;
};

struct lt9611uxd_mode {
	u16 hdisplay;
	u16 vdisplay;
	u8 vrefresh;
};

struct lt9611uxd {
	struct device *dev;
	struct drm_bridge bridge;

	struct device_node *host_node;
	struct mipi_dsi_device *dsi;
	struct edid *edid;
	struct mutex lock;
	struct drm_connector connector;

	int irq;

	u32 irq_gpio;
	u32 reset_gpio;
	u32 hdmi_en_gpio;

	bool hdmi_power_on;

	unsigned int num_vreg;
	struct lt9611uxd_vreg *vreg_config;

	struct i2c_client *i2c_client;

	enum drm_connector_status status;

	u32 num_of_modes;
	struct list_head mode_list;

	struct drm_display_mode curr_mode;
	struct lt9611uxd_mode debug_mode;

	struct workqueue_struct *hpd_wq;
	struct work_struct edid_work;
	struct work_struct hpd_work;
	wait_queue_head_t edid_wq;

	u8 edid_buf[EDID_SEG_SIZE];
	u8 i2c_wbuf[WRITE_BUF_MAX_SIZE];
	u8 i2c_rbuf[READ_BUF_MAX_SIZE];
	bool edid_with_ext_blk;

	bool hdmi_mode;
	bool fix_mode;
	bool bridge_attach;
	enum lt9611uxd_fw_upgrade_status fw_status;

	bool init_when_fw_ok_done;

	enum lt9611uxd_power_mode power_mode;

	bool bridge_enabled;
};

struct CrcInfoTypeS {
	u8 Width;
	u32  Poly;
	u32  CrcInit;
	u32  XorOut;
	bool RefIn;
	bool RefOut;
};

static int cont_splash_en;

static u8 detect;
static int lt9611uxd_init_when_fw_ok(struct lt9611uxd *pdata);
static void lt9611uxd_reset(struct lt9611uxd *pdata, bool on_off);
static void lt9611uxd_set_5v(struct lt9611uxd *pdata, bool enable);

static void lt9611uxd_ctl_en(struct lt9611uxd *pdata);
static void lt9611uxd_ctl_disable(struct lt9611uxd *pdata);

static int lt9611uxd_read_edid(struct lt9611uxd *pdata);
static int lt9611uxd_get_edid_block(void *data, u8 *buf, unsigned int block,
					size_t len);
static int lt9611uxd_write_byte(struct lt9611uxd *pdata, const u8 reg, u8 value);
static int lt9611uxd_read(struct lt9611uxd *pdata, u8 reg, char *buf, u32 size);
static bool lt9611uxd_interactive_cmd(struct lt9611uxd *pdata, u8 *params, unsigned int param_count,
	u8 *return_buffer, unsigned int return_count);

void lt9611uxd_helper_read_edid(struct lt9611uxd *pdata)
{
	pr_info("Reading edid.\n");
	lt9611uxd_read_edid(pdata);
#if KERNEL_VERSION(6, 11, 0) <= LINUX_VERSION_CODE
	const struct drm_edid *drm_edid = NULL;
	const struct edid *edid_raw = NULL;

	drm_edid = drm_edid_read_custom(&pdata->connector,
			lt9611uxd_get_edid_block, pdata);

	if (!drm_edid)
		return;

	edid_raw = drm_edid_raw(drm_edid);

	if (edid_raw)
		pdata->edid = drm_edid_duplicate(edid_raw);

	drm_edid_free(drm_edid);
#else
	pdata->edid = drm_do_get_edid(&pdata->connector,
					lt9611uxd_get_edid_block, pdata);
#endif
}

int lt9611uxd_read_hpd_status(struct lt9611uxd *pdata)
{
	u8 conn_status = connector_status_disconnected;
	u8 get_hpd_status_cmd[5] = {0x52, 0x48, 0x31, 0x3A, 0x00};
	u8 get_hpd_status_ret[5];

	lt9611uxd_interactive_cmd(pdata, get_hpd_status_cmd, 5, get_hpd_status_ret, 5);
	if (get_hpd_status_ret[4] == 0x02)
		conn_status = connector_status_connected;

	return conn_status;
}

void lt9611uxd_edid_work(struct work_struct *work)
{
	struct lt9611uxd *pdata = container_of(work, struct lt9611uxd, edid_work);

	lt9611uxd_helper_read_edid(pdata);
}

void lt9611uxd_hpd_work(struct work_struct *work)
{
	char name[32], status[32];
	char *envp[5];
	char *event_string = "HOTPLUG=1";
	enum drm_connector_status last_status;
	struct drm_device *dev = NULL;
	struct lt9611uxd *pdata = container_of(work, struct lt9611uxd, hpd_work);

	if (!pdata || !pdata->connector.funcs ||
		!pdata->connector.funcs->detect)
		return;

	dev = pdata->connector.dev;
	last_status = pdata->connector.status;

	pdata->connector.status = lt9611uxd_read_hpd_status(pdata);

	if (last_status == pdata->connector.status && pdata->edid)
		return;

	if (pdata->connector.status == connector_status_connected) {
		if (!pdata->edid)
			lt9611uxd_helper_read_edid(pdata);
	} else {
		pr_debug("release edid\n");
		cont_splash_en = 0;
		kfree(pdata->edid);
		pdata->edid = NULL;
	}

	scnprintf(name, 32, "name=%s",
		  pdata->connector.name);
	scnprintf(status, 32, "status=%s",
		  drm_get_connector_status_name(pdata->connector.status));
	pr_err("[%s]:[%s]\n", name, status);
	envp[0] = name;
	envp[1] = status;
	envp[2] = event_string;
	envp[3] = NULL;
	envp[4] = NULL;
	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE,
			   envp);
}

static struct lt9611uxd *bridge_to_lt9611(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lt9611uxd, bridge);
}

static struct lt9611uxd *connector_to_lt9611(struct drm_connector *connector)
{
	return container_of(connector, struct lt9611uxd, connector);
}

/*
 * Write one reg with more values;
 * Reg -> value0, value1, value2.
 */
static int lt9611uxd_write(struct lt9611uxd *pdata, u8 reg,
		const u8 *buf, int size)
{
	struct i2c_client *client = pdata->i2c_client;
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = size + 1,
		.buf = pdata->i2c_wbuf,
	};

	pdata->i2c_wbuf[0] = reg;
	if (size > (WRITE_BUF_MAX_SIZE - 1)) {
		pr_err("invalid write buffer size %d\n", size);
		return -EINVAL;
	}

	memcpy(pdata->i2c_wbuf + 1, buf, size);

	if (i2c_transfer(client->adapter, &msg, 1) < 1) {
		pr_err("i2c write failed\n");
		return -EIO;
	}

	return 0;
}

/*
 * Write one reg with one value;
 * Reg -> value
 */
static int lt9611uxd_write_byte(struct lt9611uxd *pdata, const u8 reg, u8 value)
{
	struct i2c_client *client = pdata->i2c_client;
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 2,
		.buf = pdata->i2c_wbuf,
	};

	memset(pdata->i2c_wbuf, 0, WRITE_BUF_MAX_SIZE);
	pdata->i2c_wbuf[0] = reg;
	pdata->i2c_wbuf[1] = value;

	if (i2c_transfer(client->adapter, &msg, 1) < 1) {
		pr_err("i2c write failed\n");
		return -EIO;
	}

	return 0;
}

/*
 * Write more regs with more values;
 * Reg1 -> value1
 * Reg2 -> value2
 */
static void lt9611uxd_write_array(struct lt9611uxd *pdata,
	struct lt9611uxd_reg_cfg *reg_arry, int size)
{
	int i = 0;

	for (i = 0; i < size; i++)
		lt9611uxd_write_byte(pdata, reg_arry[i].reg, reg_arry[i].val);
}

static int lt9611uxd_read(struct lt9611uxd *pdata, u8 reg, char *buf, u32 size)
{
	struct i2c_client *client = pdata->i2c_client;
	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = pdata->i2c_wbuf,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = size,
			.buf = pdata->i2c_rbuf,
		}
	};

	if (size > READ_BUF_MAX_SIZE) {
		pr_err("invalid read buff size %d\n", size);
		return -EINVAL;
	}

	memset(pdata->i2c_wbuf, 0x0, WRITE_BUF_MAX_SIZE);
	memset(pdata->i2c_rbuf, 0x0, READ_BUF_MAX_SIZE);
	pdata->i2c_wbuf[0] = reg;

	if (i2c_transfer(client->adapter, msg, 2) != 2) {
		pr_err("i2c read failed\n");
		return -EIO;
	}

	memcpy(buf, pdata->i2c_rbuf, size);

	return 0;
}

static bool lt9611uxd_interactive_cmd(struct lt9611uxd *pdata, u8 *params, unsigned int param_count,
	u8 *return_buffer, unsigned int return_count)
{
	u8 i2c_status;
	int count = 0, len = 0, max_len = 0xDD - 0xB0 + 1;

	print_hex_dump_debug("lt9611uxd_cmd: ", DUMP_PREFIX_NONE,
				16, 1, params, param_count, false);

	mutex_lock(&pdata->lock);

	// Step 1: Write 0x01 to 0xE0DE
	lt9611uxd_write_byte(pdata, 0xFF, 0xE0);
	lt9611uxd_write_byte(pdata, 0xDE, 0x01);

	// Step 2: Wait for the register 0xE0AE to equal 0x01, or timeout after 100 iterations
	count = 0;
	do {
		if (lt9611uxd_read(pdata, 0xAE, &i2c_status, 1))
			pr_err("read i2c status failed\n");
		usleep_range(1000, 1100);
		count++;
	} while (count < 100 && i2c_status != 0x01);

	if (i2c_status != 0x01) {
		pr_err("failed to write start flag, i2c_status = 0x%x\n", i2c_status);
		mutex_unlock(&pdata->lock);
		return false;
	}

	// Step 3: Write the passed parameters to the specified registers
	len = param_count > max_len ? max_len : param_count;
	lt9611uxd_write(pdata, 0xB0, params, len);

	// Step 4: Write 0x02 to 0xE0DE
	lt9611uxd_write_byte(pdata, 0xDE, 0x02);

	// Step 5: Wait for the register 0xE0AE to equal 0x02, or timeout after 100 iterations
	count = 0;
	do {
		if (lt9611uxd_read(pdata, 0xAE, &i2c_status, 1))
			pr_err("read i2c status failed\n");
		usleep_range(1000, 1100);
		count++;
	} while (count < 100 && i2c_status != 0x02);

	if (i2c_status != 0x02) {
		pr_err("failed to write end flag, i2c_status = 0x%x\n", i2c_status);
		mutex_unlock(&pdata->lock);
		return false;
	}

	// Step 6: Read the returned data
	lt9611uxd_read(pdata, 0x85, return_buffer, return_count);

	mutex_unlock(&pdata->lock);

	print_hex_dump_debug("lt9611uxd_interactive_ret: ", DUMP_PREFIX_NONE,
				16, 1, return_buffer, return_count, false);

	return true;
}

static int lt9611uxd_set_power_mode(struct lt9611uxd *pdata, int power_mode)
{
	int old_power_mode, ret;
	u8 set_low_power_mode_cmd[5] = {0x57, 0x43, 0x33, 0x3A, 0x00};
	u8 set_low_power_mode_ret[5];

	if (!pdata) {
		pr_err("pdata is NULL\n");
		return -EINVAL;
	}

	old_power_mode = pdata->power_mode;

	switch (power_mode) {
	case DISABLED_MODE:
		lt9611uxd_reset(pdata, false);
		pdata->power_mode = power_mode;
		break;

	case NORMAL_MODE:
		lt9611uxd_reset(pdata, true);
		pdata->power_mode = power_mode;
		break;

	case STANDBY_MODE:
	case SLEEP_MODE:
		set_low_power_mode_cmd[4] = (power_mode == STANDBY_MODE) ? 0x01 : 0x02;

		ret = lt9611uxd_interactive_cmd(pdata, set_low_power_mode_cmd, 5,
						set_low_power_mode_ret, 5);
		if (!ret || set_low_power_mode_ret[4] == 0) {
			pr_err("failed to set power mode\n");
			return -EIO;
		}

		pdata->power_mode = power_mode;
		break;

	default:
		pr_err("power mode %d not supported\n", power_mode);
		return -EINVAL;
	}

	pr_info("set power mode from %d to %d\n", old_power_mode, power_mode);
	return 0;
}

static int lt9611uxd_select_port(struct lt9611uxd *pdata, int port_select)
{
	int ret;
	u8 set_port_swap_cmd_A[6] = {0x57, 0x4d, 0x31, 0x3a, 0x01, 0xc0};
	u8 set_port_swap_cmd_B[6] = {0x57, 0x4d, 0x31, 0x3a, 0x01, 0x40};
	u8 set_port_swap_cmd_AB[6] = {0x57, 0x4d, 0x31, 0x3a, 0x02, 0xd0};
	u8 set_port_swap_ret[5];

	if (!pdata) {
		pr_err("pdata is NULL\n");
		return -EINVAL;
	}

	switch (port_select) {
	case PORT_SWAP_A:
		ret = lt9611uxd_interactive_cmd(pdata, set_port_swap_cmd_A,
				6, set_port_swap_ret, 5);

		if (!ret || set_port_swap_ret[4] == 0) {
			pr_err("failed to set port\n");
			return -EIO;
		}
		break;

	case PORT_SWAP_B:
		ret = lt9611uxd_interactive_cmd(pdata, set_port_swap_cmd_B,
				6, set_port_swap_ret, 5);

		if (!ret || set_port_swap_ret[4] == 0) {
			pr_err("failed to set port\n");
			return -EIO;
		}
		break;

	case PORT_SWAP_AB:
		ret = lt9611uxd_interactive_cmd(pdata, set_port_swap_cmd_AB,
				6, set_port_swap_ret, 5);

		if (!ret || set_port_swap_ret[4] == 0) {
			pr_err("failed to set port\n");
			return -EIO;
		}
		break;
	default:
		pr_err("port_select %d not supported\n", port_select);
		return -EINVAL;
	}
	return 0;
}

unsigned int BitsReverse(u32 inVal, u8 bits)
{
	u32 outVal = 0;
	u8 i;

	for (i = 0; i < bits; i++) {
		if (inVal & (1 << i))
			outVal |= 1 << (bits - 1 - i);
	}

	return outVal;
}

unsigned int GetCRC(struct CrcInfoTypeS type, const  u8 *buf, u64 bufLen)
{
	u8 width  = type.Width;
	u32  poly   = type.Poly;
	u32  crc    = type.CrcInit;
	u32  xorout = type.XorOut;
	bool refin  = type.RefIn;
	bool refout = type.RefOut;
	u8 n;
	u32  bits;
	u32  data;
	u8 i;

	n    =  (width < 8) ? 0 : (width-8);
	crc  =  (width < 8) ? (crc<<(8-width)) : crc;
	bits =  (width < 8) ? 0x80 : (1 << (width-1));
	poly =  (width < 8) ? (poly<<(8-width)) : poly;
	while (bufLen--) {
		data = *(buf++);
		if (refin)
			data = BitsReverse(data, 8);
		crc ^= (data << n);
		for (i = 0; i < 8; i++) {
			if (crc & bits)
				crc = (crc << 1) ^ poly;
			else
				crc = crc << 1;
		}
	}
	crc = (width < 8) ? (crc>>(8-width)) : crc;
	if (refout)
		crc = BitsReverse(crc, width);
	crc ^= xorout;

	return (crc & ((2<<(width-1)) - 1));
}

u8 calculate_crc(const u8 *upgradeData, u64 len)
{
	struct CrcInfoTypeS type = {
		.Width = 8,
		.Poly  = 0x31,
		.CrcInit = 0,
		.XorOut = 0,
		.RefOut = false,
		.RefIn = false,
	};
	u64 crc_size = LT9611UXD_FW_BUFF_SIZE - 1;
	u8 default_val = 0xFF;

	type.CrcInit = GetCRC(type, upgradeData, len);

	crc_size -= len;
	while (crc_size--)
		type.CrcInit = GetCRC(type, &default_val, 1);

	return type.CrcInit;
}

void lt9611uxd_config(struct lt9611uxd *pdata)
{
	struct lt9611uxd_reg_cfg reg_cfg[] = {
		{0xFF, 0xE0},
		{0xEE, 0x01},
		{0x5E, 0xC1},
		{0x58, 0x00},
		{0x59, 0x50},
		{0x5A, 0x10},
		{0x5A, 0x00},
		{0x58, 0x21},
	};

	lt9611uxd_write_array(pdata, reg_cfg, ARRAY_SIZE(reg_cfg));
}

u16 lt9611uxd_get_version(struct lt9611uxd *pdata)
{
	u8 revison = 0;
	u8 subversion = 0;
	u16 result = 0;

	lt9611uxd_ctl_en(pdata);
	lt9611uxd_write_byte(pdata, 0xFF, 0xE0);

	if (!lt9611uxd_read(pdata, 0x81, &revison, 1))
		pr_info("LT9611 revison: 0x%x\n", revison);
	else
		pr_err("LT9611 get revison failed\n");

	if (!lt9611uxd_read(pdata, 0x80, &subversion, 1))
		pr_info("LT9611 subversion: 0x%x\n", subversion);
	else
		pr_err("LT9611 get subversion failed\n");

	lt9611uxd_ctl_disable(pdata);
	msleep(50);

	result = (revison<<8)|subversion;

	return result;
}

void lt9611uxd_flash_write_en(struct lt9611uxd *pdata)
{
	struct lt9611uxd_reg_cfg reg_cfg[] = {
		{0xFF, 0xE0},
		{0x5A, 0x04},
		{0x5A, 0x00},
	};

	lt9611uxd_write_array(pdata, reg_cfg, ARRAY_SIZE(reg_cfg));
}

void lt9611uxd_flash_write_di(struct lt9611uxd *pdata)
{
	struct lt9611uxd_reg_cfg reg_cfg[] = {
		{0xFF, 0xE0},
		{0x5A, 0x08},
		{0x5A, 0x00},
	};

	lt9611uxd_write_array(pdata, reg_cfg, ARRAY_SIZE(reg_cfg));
}

u8 lt9611uxd_read_flash_status(struct lt9611uxd *pdata)
{
	u8 ucFlashStatusReg = 0;
	struct lt9611uxd_reg_cfg reg_cfg[] = {
		{0xFF, 0xE1},
		{0x03, 0x3F},
		{0x03, 0xFF},
		{0xFF, 0xE0},
		{0x5E, 0x40},
		{0x56, 0x05},
		{0x55, 0x25},
		{0x55, 0x01},
		{0x58, 0x21},
	};

	lt9611uxd_write_array(pdata, reg_cfg, ARRAY_SIZE(reg_cfg));

	if (!lt9611uxd_read(pdata, 0x5F, &ucFlashStatusReg, 1))
		pr_err("LT9611 get ucFlashStatusReg: 0x%x\n", ucFlashStatusReg);
	else
		pr_err("LT9611 get ucFlashStatusReg failed\n");

	return ucFlashStatusReg;
}

u8 lt9611uxd_read_fw_crc(struct lt9611uxd *pdata)
{
	u8 ucFwCrcReg = 0;

	lt9611uxd_ctl_en(pdata);
	if (!lt9611uxd_read(pdata, 0x21, &ucFwCrcReg, 1))
		pr_err("LT9611 get ucFwCrcReg: 0x%x\n", ucFwCrcReg);
	else
		pr_err("LT9611 get ucFwCrcReg failed\n");
	lt9611uxd_ctl_disable(pdata);

	return ucFwCrcReg;
}

void lt9611uxd_block_erase(struct lt9611uxd *pdata)
{
	u8 ucFlashStatus = 0;
	u8 ucBlockNum = 0x00;
	u32 i = 0;

	pr_info("LT9611 block erase\n");

	for (ucBlockNum = 0; ucBlockNum < 2; ucBlockNum++) {
		struct lt9611uxd_reg_cfg reg_cfg[] = {
			{0xFF, 0xE0},
			{0xEE, 0x01},
			{0x5A, 0x04},
			{0x5A, 0x00},
			{0x5B, ((ucBlockNum * 0x8000) >> 16) & 0xFF},
			{0x5C, ((ucBlockNum * 0x8000) >> 8) & 0xFF},
			{0x5D, (ucBlockNum * 0x8000) & 0xFF},
			{0x5A, 0x01},
			{0x5A, 0x00},
		};

		lt9611uxd_write_array(pdata, reg_cfg, ARRAY_SIZE(reg_cfg));
		msleep(100);

		i = 0;
		while (1) {
			ucFlashStatus = lt9611uxd_read_flash_status(pdata);
			if ((ucFlashStatus & 0x01) == 0)
				break;

			if (i > 50)
				break;

			i++;
			msleep(50);
		}
	}

	pr_info("LT9611 block erase done\n");
}

void lt9611uxd_crc_to_sram(struct lt9611uxd *pdata)
{
	struct lt9611uxd_reg_cfg reg_cfg[] = {
		{0xFF, 0xE0},
		{0x51, 0x00},
		{0x55, 0xC0},
		{0x55, 0x80},
		{0x5E, 0xC0},
		{0x58, 0x21},
	};

	lt9611uxd_write_array(pdata, reg_cfg, ARRAY_SIZE(reg_cfg));
}

void lt9611uxd_data_to_sram(struct lt9611uxd *pdata)
{
	struct lt9611uxd_reg_cfg reg_cfg[] = {
		{0xFF, 0xE0},
		{0x51, 0xFF},
		{0x55, 0x80},
		{0x5E, 0xC0},
		{0x58, 0x21},
	};

	lt9611uxd_write_array(pdata, reg_cfg, ARRAY_SIZE(reg_cfg));
}

void lt9611uxd_sram_to_flash(struct lt9611uxd *pdata, u32 addr)
{
	struct lt9611uxd_reg_cfg reg_cfg[] = {
		{0xFF, 0xE0},
		{0x5B, (addr >> 16) & 0xFF},
		{0x5C, (addr >> 8) & 0xFF},
		{0x5D, addr & 0xFF},
		{0x5A, 0x30},
		{0x5A, 0x00},
	};

	lt9611uxd_write_array(pdata, reg_cfg, ARRAY_SIZE(reg_cfg));
}

void lt9611uxd_firmware_write(struct lt9611uxd *pdata, const u8 *f_data,
		int size)
{
	u8 last_buf[LT9611UXD_SRAM_PAGE_SIZE];
	int i = 0, page_size = LT9611UXD_SRAM_PAGE_SIZE;
	int start_addr = 0, total_page = 0, rest_data = 0;

	total_page = size / page_size;
	rest_data = size % page_size;

	for (i = 0; i < total_page; i++) {
		lt9611uxd_flash_write_en(pdata);
		lt9611uxd_data_to_sram(pdata);
		lt9611uxd_write(pdata, 0x59, f_data, page_size);
		lt9611uxd_sram_to_flash(pdata, start_addr);
		start_addr += page_size;
		f_data += page_size;
		msleep(20);
	}

	if (rest_data > 0) {
		memset(last_buf, 0xFF, page_size);
		memcpy(last_buf, f_data, rest_data);

		lt9611uxd_flash_write_en(pdata);
		lt9611uxd_data_to_sram(pdata);
		lt9611uxd_write(pdata, 0x59, last_buf, page_size);
		lt9611uxd_sram_to_flash(pdata, start_addr);
		msleep(20);
	}

	lt9611uxd_flash_write_di(pdata);

	pr_info("LT9611 FW data write over, total size: %d, page: %d, reset: %d\n",
		size, total_page, rest_data);
}

void lt9611uxd_firmware_write_crc(struct lt9611uxd *pdata, const u8 *f_data,
		int size)
{
	lt9611uxd_flash_write_en(pdata);
	lt9611uxd_crc_to_sram(pdata);
	lt9611uxd_write_byte(pdata, 0x59, *f_data);
	lt9611uxd_sram_to_flash(pdata, LT9611UXD_FW_BUFF_SIZE-1);
	lt9611uxd_flash_write_di(pdata);

	pr_info("LT9611 FW crc write over, total size: %d\n", size);
}

void lt9611uxd_firmware_upgrade(struct lt9611uxd *pdata,
			const struct firmware *cfg)
{
	int data_len = (int)cfg->size;
	u8 data_crc = 0x00, fw_crc = 0x00;

	if (data_len >= LT9611UXD_FW_BUFF_SIZE) {
		pdata->fw_status = UPDATE_FAILED;
		pr_err("LT9611 FW size is out of range!\n");
		return;
	}

	data_crc = calculate_crc(cfg->data, data_len);
	pr_info("LT9611 FW size %d, CRC 0x%x\n", data_len, data_crc);

	mutex_lock(&pdata->lock);
	lt9611uxd_ctl_en(pdata);

	pdata->fw_status = UPDATE_RUNNING;
	lt9611uxd_config(pdata);
	lt9611uxd_block_erase(pdata);
	lt9611uxd_firmware_write(pdata, cfg->data, data_len);
	lt9611uxd_firmware_write_crc(pdata, &data_crc, 1);

	lt9611uxd_reset(pdata, true);
	msleep(1000);
	fw_crc = lt9611uxd_read_fw_crc(pdata);
	if (data_crc == fw_crc) {
		pdata->fw_status = UPDATE_SUCCESS;
		pr_info("LT9611 Firmware upgrade success.\n");
	} else {
		pdata->fw_status = UPDATE_FAILED;
		pr_err("LT9611 Firmware upgrade failed\n");
	}

	lt9611uxd_ctl_disable(pdata);
	mutex_unlock(&pdata->lock);
}

static void lt9611uxd_firmware_cb(const struct firmware *cfg, void *data)
{
	struct lt9611uxd *pdata = (struct lt9611uxd *)data;

	if (!cfg) {
		pr_err("LT9611 get firmware failed\n");
		return;
	}

	lt9611uxd_firmware_upgrade(pdata, cfg);
	release_firmware(cfg);
	lt9611uxd_reset(pdata, true);

	lt9611uxd_init_when_fw_ok(pdata);
}

static void lt9611uxd_parse_dt_modes(struct device_node *np,
					struct list_head *head,
					u32 *num_of_modes)
{
	int rc = 0;
	struct drm_display_mode *mode;
	u32 mode_count = 0;
	struct device_node *node = NULL;
	struct device_node *root_node = NULL;
	u32 h_front_porch, h_pulse_width, h_back_porch;
	u32 v_front_porch, v_pulse_width, v_back_porch;
	bool h_active_high, v_active_high;
	u32 flags = 0;

	root_node = of_get_child_by_name(np, "lt,customize-modes");
	if (!root_node) {
		root_node = of_parse_phandle(np, "lt,customize-modes", 0);
		if (!root_node) {
			pr_info("No entry present for lt,customize-modes\n");
			return;
		}
	}

	for_each_child_of_node(root_node, node) {
		rc = 0;
		mode = kzalloc(sizeof(*mode), GFP_KERNEL);
		if (!mode) {
			pr_err("Out of memory\n");
			rc =  -ENOMEM;
			continue;
		}

		rc = of_property_read_u32(node, "lt,mode-h-active",
						(u32 *)&mode->hdisplay);
		if (rc) {
			pr_err("failed to read h-active, rc=%d\n", rc);
			goto fail;
		}

		rc = of_property_read_u32(node, "lt,mode-h-front-porch",
						&h_front_porch);
		if (rc) {
			pr_err("failed to read h-front-porch, rc=%d\n", rc);
			goto fail;
		}

		rc = of_property_read_u32(node, "lt,mode-h-pulse-width",
						&h_pulse_width);
		if (rc) {
			pr_err("failed to read h-pulse-width, rc=%d\n", rc);
			goto fail;
		}

		rc = of_property_read_u32(node, "lt,mode-h-back-porch",
						&h_back_porch);
		if (rc) {
			pr_err("failed to read h-back-porch, rc=%d\n", rc);
			goto fail;
		}

		h_active_high = of_property_read_bool(node,
						"lt,mode-h-active-high");

		rc = of_property_read_u32(node, "lt,mode-v-active",
						(u32 *)&mode->vdisplay);
		if (rc) {
			pr_err("failed to read v-active, rc=%d\n", rc);
			goto fail;
		}

		rc = of_property_read_u32(node, "lt,mode-v-front-porch",
						&v_front_porch);
		if (rc) {
			pr_err("failed to read v-front-porch, rc=%d\n", rc);
			goto fail;
		}

		rc = of_property_read_u32(node, "lt,mode-v-pulse-width",
						&v_pulse_width);
		if (rc) {
			pr_err("failed to read v-pulse-width, rc=%d\n", rc);
			goto fail;
		}

		rc = of_property_read_u32(node, "lt,mode-v-back-porch",
						&v_back_porch);
		if (rc) {
			pr_err("failed to read v-back-porch, rc=%d\n", rc);
			goto fail;
		}

		v_active_high = of_property_read_bool(node,
						"lt,mode-v-active-high");

		rc = of_property_read_u32(node, "lt,mode-clock-in-khz",
						&mode->clock);
		if (rc) {
			pr_err("failed to read clock, rc=%d\n", rc);
			goto fail;
		}

		mode->hsync_start = mode->hdisplay + h_front_porch;
		mode->hsync_end = mode->hsync_start + h_pulse_width;
		mode->htotal = mode->hsync_end + h_back_porch;
		mode->vsync_start = mode->vdisplay + v_front_porch;
		mode->vsync_end = mode->vsync_start + v_pulse_width;
		mode->vtotal = mode->vsync_end + v_back_porch;
		if (h_active_high)
			flags |= DRM_MODE_FLAG_PHSYNC;
		else
			flags |= DRM_MODE_FLAG_NHSYNC;
		if (v_active_high)
			flags |= DRM_MODE_FLAG_PVSYNC;
		else
			flags |= DRM_MODE_FLAG_NVSYNC;
		mode->flags = flags;

		if (!rc) {
			mode_count++;
			list_add_tail(&mode->head, head);
		}

		drm_mode_set_name(mode);

		pr_debug("mode[%s] h[%d,%d,%d,%d] v[%d,%d,%d,%d] %d %x %dkHZ\n",
			mode->name, mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal, mode->vdisplay,
			mode->vsync_start, mode->vsync_end, mode->vtotal,
			drm_mode_vrefresh(mode), mode->flags, mode->clock);
fail:
		if (rc) {
			kfree(mode);
			continue;
		}
	}

	if (num_of_modes)
		*num_of_modes = mode_count;
}


static int lt9611uxd_parse_dt(struct device *dev,
	struct lt9611uxd *pdata)
{
	struct device_node *np = dev->of_node;
	struct device_node *end_node;
	int ret = 0;

	end_node = of_graph_get_endpoint_by_regs(dev->of_node, 0, 0);
	if (!end_node) {
		pr_err("remote endpoint not found\n");
		return -ENODEV;
	}

	pdata->host_node = of_graph_get_remote_port_parent(end_node);
	of_node_put(end_node);
	if (!pdata->host_node) {
		pr_err("remote node not found\n");
		return -ENODEV;
	}
	of_node_put(pdata->host_node);

	pdata->irq_gpio =
		of_get_named_gpio(np, "lt,irq-gpio", 0);
	if (!gpio_is_valid(pdata->irq_gpio)) {
		pr_err("irq gpio not specified\n");
		ret = -EINVAL;
	}
	pr_err("irq_gpio=%d\n", pdata->irq_gpio);

	pdata->reset_gpio =
		of_get_named_gpio(np, "lt,reset-gpio", 0);
	if (!gpio_is_valid(pdata->reset_gpio)) {
		pr_err("reset gpio not specified\n");
		ret = -EINVAL;
	}
	pr_err("reset_gpio=%d\n", pdata->reset_gpio);

	pdata->hdmi_en_gpio =
		of_get_named_gpio(np, "lt,hdmi-en-gpio", 0);
	if (!gpio_is_valid(pdata->hdmi_en_gpio))
		pr_err("hdmi en gpio not specified\n");
	else
		pr_err("hdmi_en_gpio=%d\n", pdata->hdmi_en_gpio);

	/*get display modes from device tree*/
	INIT_LIST_HEAD(&pdata->mode_list);
	lt9611uxd_parse_dt_modes(np,
			&pdata->mode_list, &pdata->num_of_modes);

	return ret;
}

static int lt9611uxd_gpio_configure(struct lt9611uxd *pdata, bool on)
{
	int ret = 0;

	if (on) {
		ret = gpio_request(pdata->reset_gpio,
			"lt9611-reset-gpio");
		if (ret) {
			pr_err("lt9611 reset gpio request failed\n");
			goto error;
		}

		ret = gpio_direction_output(pdata->reset_gpio, 1);
		if (ret) {
			pr_err("lt9611 reset gpio direction failed\n");
			goto reset_error;
		}


		ret = gpio_request(pdata->irq_gpio, "lt9611-irq-gpio");
		if (ret) {
			pr_err("lt9611 irq gpio request failed\n");
			goto reset_error;
		}

		ret = gpio_direction_input(pdata->irq_gpio);
		if (ret) {
			pr_err("lt9611 irq gpio direction failed\n");
			goto irq_error;
		}

		if (gpio_is_valid(pdata->hdmi_en_gpio)) {
			ret = gpio_request(pdata->hdmi_en_gpio,
					"lt9611-hdmi-en-gpio");
			if (ret) {
				pr_err("lt9611 hdmi en gpio request failed\n");
				goto irq_error;
			}

			ret = gpio_direction_output(pdata->hdmi_en_gpio, 1);
			if (ret) {
				pr_err("lt9611 hdmi en gpio direction failed\n");
				goto hdmi_en_error;
			}
		}
	} else {
		if (gpio_is_valid(pdata->irq_gpio))
			gpio_free(pdata->irq_gpio);
		if (gpio_is_valid(pdata->hdmi_en_gpio))
			gpio_free(pdata->hdmi_en_gpio);
		if (gpio_is_valid(pdata->reset_gpio))
			gpio_free(pdata->reset_gpio);
	}

	return ret;


hdmi_en_error:
	if (gpio_is_valid(pdata->hdmi_en_gpio))
		gpio_free(pdata->hdmi_en_gpio);
irq_error:
	gpio_free(pdata->irq_gpio);
reset_error:
	gpio_free(pdata->reset_gpio);
error:
	return ret;
}

static void lt9611uxd_ctl_en(struct lt9611uxd *pdata)
{
	lt9611uxd_write_byte(pdata, 0xFF, 0xE0);
	lt9611uxd_write_byte(pdata, 0xEE, 0x01);
}

static void lt9611uxd_ctl_disable(struct lt9611uxd *pdata)
{
	lt9611uxd_write_byte(pdata, 0xFF, 0xE0);
	lt9611uxd_write_byte(pdata, 0xEE, 0x00);
}

static int lt9611uxd_read_device_id(struct lt9611uxd *pdata)
{
	u8 rev0 = 0, rev1 = 0;
	int ret = 0;

	lt9611uxd_ctl_en(pdata);
	lt9611uxd_write_byte(pdata, 0xFF, 0xE1);

	if (!lt9611uxd_read(pdata, 0x00, &rev0, 1) &&
		!lt9611uxd_read(pdata, 0x01, &rev1, 1)) {
		pr_info("LT9611 id: 0x%x\n", (rev0 << 8) | rev1);
	} else {
		pr_err("LT9611 get id failed\n");
		ret = -1;
	}

	lt9611uxd_ctl_disable(pdata);
	msleep(50);

	return ret;
}

static irqreturn_t lt9611uxd_irq_thread_handler(int irq, void *dev_id)
{
	u8 irq_type = 0;
	bool irq_hpd_flag = false;
	struct lt9611uxd *pdata = (struct lt9611uxd *)dev_id;

	mutex_lock(&pdata->lock);

	lt9611uxd_ctl_en(pdata);
	lt9611uxd_write_byte(pdata, 0xFF, 0xE0);
	if (!lt9611uxd_read(pdata, 0x84, &irq_type, 1)) {
		pr_info("lt9611uxd irq_type 0x%x\n", irq_type);
		if (irq_type)
			irq_hpd_flag = irq_type & BIT(0);
		else
			pr_err("invalid irq\n");
	} else
		pr_err("get irq status failed\n");

	msleep(50);

	// clear interrput flag
	lt9611uxd_write_byte(pdata, 0xFF, 0xE0);
	lt9611uxd_write_byte(pdata, 0xDF, irq_type);
	msleep(20);
	lt9611uxd_write_byte(pdata, 0xDF, 0x00);
	lt9611uxd_ctl_disable(pdata);

	mutex_unlock(&pdata->lock);

	if (irq_hpd_flag) {
		pr_info("hpd changed\n");
		if (!pdata->bridge_attach)
			return IRQ_HANDLED;
		queue_work(pdata->hpd_wq, &pdata->hpd_work);
	}

	return IRQ_HANDLED;
}

static void lt9611uxd_reset(struct lt9611uxd *pdata, bool on_off)
{
	pr_debug("reset: %d\n", on_off);
	if (on_off) {
		gpio_set_value(pdata->reset_gpio, 1);
		msleep(20);
		gpio_set_value(pdata->reset_gpio, 0);
		msleep(20);
		gpio_set_value(pdata->reset_gpio, 1);
		msleep(300);
	} else {
		gpio_set_value(pdata->reset_gpio, 0);
	}
}

static void lt9611uxd_set_5v(struct lt9611uxd *pdata, bool enable)
{
	if (gpio_is_valid(pdata->hdmi_en_gpio)) {
		gpio_set_value(pdata->hdmi_en_gpio, enable ? 1 : 0);
		pdata->hdmi_power_on = enable;
		msleep(20);
	}
}

static int lt9611uxd_config_vreg(struct device *dev,
	struct lt9611uxd_vreg *in_vreg, int num_vreg, bool config)
{
	int i = 0, rc = 0;
	struct lt9611uxd_vreg *curr_vreg = NULL;

	if (!in_vreg || !num_vreg)
		return rc;

	if (config) {
		for (i = 0; i < num_vreg; i++) {
			curr_vreg = &in_vreg[i];
			curr_vreg->vreg = regulator_get(dev,
					curr_vreg->vreg_name);
			if (IS_ERR_OR_NULL(curr_vreg->vreg)) {
				pr_err("%s get failed. rc=%d\n",
						curr_vreg->vreg_name, rc);
				curr_vreg->vreg = NULL;
				goto vreg_get_fail;
			}

			rc = regulator_set_voltage(
					curr_vreg->vreg,
					curr_vreg->min_voltage,
					curr_vreg->max_voltage);
			if (rc < 0) {
				pr_err("%s set vltg fail\n",
						curr_vreg->vreg_name);
				goto vreg_set_voltage_fail;
			}
		}
	} else {
		for (i = num_vreg-1; i >= 0; i--) {
			curr_vreg = &in_vreg[i];
			if (curr_vreg->vreg) {
				regulator_set_voltage(curr_vreg->vreg,
						0, curr_vreg->max_voltage);

				regulator_put(curr_vreg->vreg);
				curr_vreg->vreg = NULL;
			}
		}
	}
	return 0;

vreg_unconfig:
	regulator_set_load(curr_vreg->vreg, 0);

vreg_set_voltage_fail:
	regulator_put(curr_vreg->vreg);
	curr_vreg->vreg = NULL;

vreg_get_fail:
	for (i--; i >= 0; i--) {
		curr_vreg = &in_vreg[i];
		goto vreg_unconfig;
	}
	return rc;
}

static int lt9611uxd_get_dt_supply(struct device *dev,
		struct lt9611uxd *pdata)
{
	int i = 0, rc = 0;
	u32 tmp = 0;
	struct device_node *of_node = NULL, *supply_root_node = NULL;
	struct device_node *supply_node = NULL;

	if (!dev || !pdata) {
		pr_err("invalid input param dev:%pK pdata:%pK\n", dev, pdata);
		return -EINVAL;
	}

	of_node = dev->of_node;

	pdata->num_vreg = 0;
	supply_root_node = of_get_child_by_name(of_node,
			"lt,supply-entries");
	if (!supply_root_node) {
		pr_info("no supply entry present\n");
		return 0;
	}

	pdata->num_vreg = of_get_available_child_count(supply_root_node);
	if (pdata->num_vreg == 0) {
		pr_info("no vreg present\n");
		return 0;
	}

	pr_err("vreg found. count=%d\n", pdata->num_vreg);
	pdata->vreg_config = devm_kzalloc(dev, sizeof(struct lt9611uxd_vreg) *
			pdata->num_vreg, GFP_KERNEL);
	if (!pdata->vreg_config)
		return -ENOMEM;

	for_each_available_child_of_node(supply_root_node, supply_node) {
		const char *st = NULL;

		rc = of_property_read_string(supply_node,
				"lt,supply-name", &st);
		if (rc) {
			pr_err("error reading name. rc=%d\n", rc);
			goto error;
		}

		strscpy(pdata->vreg_config[i].vreg_name, st,
				sizeof(pdata->vreg_config[i].vreg_name));

		rc = of_property_read_u32(supply_node,
				"lt,supply-min-voltage", &tmp);
		if (rc) {
			pr_err("error reading min volt. rc=%d\n", rc);
			goto error;
		}
		pdata->vreg_config[i].min_voltage = tmp;

		rc = of_property_read_u32(supply_node,
				"lt,supply-max-voltage", &tmp);
		if (rc) {
			pr_err("error reading max volt. rc=%d\n", rc);
			goto error;
		}
		pdata->vreg_config[i].max_voltage = tmp;

		rc = of_property_read_u32(supply_node,
				"lt,supply-enable-load", &tmp);
		if (rc)
			pr_err("no supply enable load value. rc=%d\n", rc);

		pdata->vreg_config[i].enable_load = (!rc ? tmp : 0);

		rc = of_property_read_u32(supply_node,
				"lt,supply-disable-load", &tmp);
		if (rc)
			pr_err("no supply disable load value. rc=%d\n", rc);

		pdata->vreg_config[i].disable_load = (!rc ? tmp : 0);

		rc = of_property_read_u32(supply_node,
				"lt,supply-pre-on-sleep", &tmp);
		if (rc)
			pr_err("no supply pre on sleep value. rc=%d\n", rc);

		pdata->vreg_config[i].pre_on_sleep = (!rc ? tmp : 0);

		rc = of_property_read_u32(supply_node,
				"lt,supply-pre-off-sleep", &tmp);
		if (rc)
			pr_err("no supply pre off sleep value. rc=%d\n", rc);

		pdata->vreg_config[i].pre_off_sleep = (!rc ? tmp : 0);

		rc = of_property_read_u32(supply_node,
				"lt,supply-post-on-sleep", &tmp);
		if (rc)
			pr_err("no supply post on sleep value. rc=%d\n", rc);

		pdata->vreg_config[i].post_on_sleep = (!rc ? tmp : 0);

		rc = of_property_read_u32(supply_node,
				"lt,supply-post-off-sleep", &tmp);
		if (rc)
			pr_err("no supply post off sleep value. rc=%d\n", rc);

		pdata->vreg_config[i].post_off_sleep = (!rc ? tmp : 0);

		pr_debug("%s min=%d, max=%d, enable=%d, disable=%d\n",
				pdata->vreg_config[i].vreg_name,
				pdata->vreg_config[i].min_voltage,
				pdata->vreg_config[i].max_voltage,
				pdata->vreg_config[i].enable_load,
				pdata->vreg_config[i].disable_load);
		++i;

		rc = 0;
	}

	rc = lt9611uxd_config_vreg(dev,
			pdata->vreg_config, pdata->num_vreg, true);
	if (rc)
		goto error;

	return rc;

error:
	if (pdata->vreg_config) {
		pdata->vreg_config = NULL;
		pdata->num_vreg = 0;
	}

	return rc;
}

static void lt9611uxd_put_dt_supply(struct device *dev,
		struct lt9611uxd *pdata)
{
	if (!dev || !pdata) {
		pr_err("invalid input param dev:%pK pdata:%pK\n", dev, pdata);
		return;
	}

	lt9611uxd_config_vreg(dev,
			pdata->vreg_config, pdata->num_vreg, false);

	if (pdata->vreg_config)
		pdata->vreg_config = NULL;

	pdata->num_vreg = 0;
}

static int lt9611uxd_enable_vreg(struct lt9611uxd *pdata, int enable)
{
	int i = 0, rc = 0;
	bool need_sleep;
	struct lt9611uxd_vreg *in_vreg = pdata->vreg_config;
	int num_vreg = pdata->num_vreg;

	if (enable) {
		for (i = 0; i < num_vreg; i++) {
			if (IS_ERR_OR_NULL(in_vreg[i].vreg)) {
				pr_err("%s regulator error. rc=%d\n",
						in_vreg[i].vreg_name, rc);
				goto vreg_set_opt_mode_fail;
			}

			need_sleep = !regulator_is_enabled(in_vreg[i].vreg);
			if (in_vreg[i].pre_on_sleep && need_sleep)
				usleep_range(in_vreg[i].pre_on_sleep * 1000,
						in_vreg[i].pre_on_sleep * 1000);

			rc = regulator_set_load(in_vreg[i].vreg,
					in_vreg[i].enable_load);
			if (rc < 0) {
				pr_err("%s set opt m fail\n",
						in_vreg[i].vreg_name);
				goto vreg_set_opt_mode_fail;
			}

			rc = regulator_enable(in_vreg[i].vreg);
			if (in_vreg[i].post_on_sleep && need_sleep)
				usleep_range(in_vreg[i].post_on_sleep * 1000,
					in_vreg[i].post_on_sleep * 1000);
			if (rc < 0) {
				pr_err("%s enable failed\n",
						in_vreg[i].vreg_name);
				goto disable_vreg;
			}
		}
	} else {
		for (i = num_vreg-1; i >= 0; i--) {
			if (in_vreg[i].pre_off_sleep)
				usleep_range(in_vreg[i].pre_off_sleep * 1000,
					in_vreg[i].pre_off_sleep * 1000);

			regulator_set_load(in_vreg[i].vreg,
					in_vreg[i].disable_load);
			regulator_disable(in_vreg[i].vreg);

			if (in_vreg[i].post_off_sleep)
				usleep_range(in_vreg[i].post_off_sleep * 1000,
					in_vreg[i].post_off_sleep * 1000);
		}
	}
	return rc;

disable_vreg:
	regulator_set_load(in_vreg[i].vreg, in_vreg[i].disable_load);

vreg_set_opt_mode_fail:
	for (i--; i >= 0; i--) {
		if (in_vreg[i].pre_off_sleep)
			usleep_range(in_vreg[i].pre_off_sleep * 1000,
					in_vreg[i].pre_off_sleep * 1000);

		regulator_set_load(in_vreg[i].vreg,
				in_vreg[i].disable_load);
		regulator_disable(in_vreg[i].vreg);

		if (in_vreg[i].post_off_sleep)
			usleep_range(in_vreg[i].post_off_sleep * 1000,
					in_vreg[i].post_off_sleep * 1000);
	}

	return rc;
}

/* connector funcs */
static enum drm_connector_status
lt9611uxd_connector_detect(struct drm_connector *connector, bool force)
{
	struct lt9611uxd *pdata = connector_to_lt9611(connector);

	pdata->status = connector_status_disconnected;
	if (force) {
		pdata->status = lt9611uxd_read_hpd_status(pdata);

		msleep(50);

		if ((pdata->status == connector_status_connected) && !pdata->edid)
			lt9611uxd_helper_read_edid(pdata);

	} else
		pdata->status = connector_status_connected;

	/* add for audio */
	detect = (pdata->status == connector_status_connected) ? 1 : 0;

	return pdata->status;
}

void dump_dtd(const u8 *desc)
{
	u16 pixel_clock;
	u16 h_active, h_blanking, h_total;
	u16 v_active, v_blanking, v_total;
	int refresh_rate = 0;

	pixel_clock = desc[0] | (desc[1] << 8);
	if (!pixel_clock)
		return;

	h_active = ((desc[4] & 0xF0) << 4) | desc[2];
	h_blanking = ((desc[4] & 0x0F) << 8) | desc[3];
	v_active = ((desc[7] & 0xF0) << 4) | desc[5];
	v_blanking = ((desc[7] & 0x0F) << 8) | desc[6];

	h_total = h_active + h_blanking;
	v_total = v_active + v_blanking;

	if (h_total && v_total)
		refresh_rate = (pixel_clock * 10000) / (h_total * v_total);

	pr_info("Detailed Timing: %dx%d @ %dHz (Pixel Clock: %u kHz)\n",
		h_active, v_active, refresh_rate, pixel_clock * 10);
}

void dump_edid(u8 *edid_buf, int size)
{
	u8 *edid = edid_buf;
	int i = 0;
	const u8 *desc;

	pr_info("EDID Version: %d.%d\n", edid[18], edid[19]);
	pr_info("Display Size: %d x %d cm\n", edid[21], edid[22]);

	// Detailed Timing Descriptor: 0x36, 0x48, 0x5A, 0x6C
	for (i = 0; i < 4; i++) {
		desc = edid + 0x36 + 18*i;
		if (desc[0] == 0 && desc[1] == 0)
			continue;
		dump_dtd(desc);
	}
}

static int lt9611uxd_read_edid(struct lt9611uxd *pdata)
{
	u8 *buf = pdata->edid_buf;
	u8 packets_num;
	u8 get_edid_size_cmd[5] = {0x52, 0x48, 0x32, 0x3A, 0x00};
	u8 get_edid_size_ret[6];
	u8 get_edid_data_cmd[5] = {0x52, 0x48, 0x33, 0x3A, 0x00};
	u8 get_edid_data_ret[37];
	int packet_size = 32, edid_size = 0;
	int ret, i, offset = 0;

	// read the total size of EDID
	ret = lt9611uxd_interactive_cmd(pdata, get_edid_size_cmd, 5, get_edid_size_ret, 6);
	if (!ret) {
		pr_err("failed to read the size of EDID\n");
		return -1;
	}

	edid_size = (get_edid_size_ret[4] << 8) | get_edid_size_ret[5];
	if (edid_size > EDID_SEG_SIZE)
		edid_size = EDID_SEG_SIZE;
	pdata->edid_with_ext_blk = (edid_size > EDID_LENGTH) ? true : false;

	memset(buf, 0, EDID_SEG_SIZE);

	// read EDID data
	packets_num = (edid_size % packet_size) ?
		(edid_size / packet_size + 1) : (edid_size / packet_size);
	for (i = 0; i < packets_num; i++) {
		get_edid_data_cmd[4] = (u8)i;
		ret = lt9611uxd_interactive_cmd(pdata, get_edid_data_cmd, 5, get_edid_data_ret, 37);
		if (!ret) {
			pr_err("failed to read EDID data for packet %d\n", i);
			return -1;
		}
		memcpy(&buf[offset], &get_edid_data_ret[5], packet_size);
		offset += packet_size;
	}

	dump_edid(buf, 128);

	if (buf[18] == 0)
		return -1;
	return 0;
}

static int lt9611uxd_get_edid_block(void *data, u8 *buf, unsigned int block,
				  size_t len)
{
	struct lt9611uxd *pdata = data;

	if ((!pdata->edid_with_ext_blk) && (block != 0)) {
		pr_info("No extension block, maybe connector is DVI interface.\n");
		return 0;
	}

	pr_info("get edid block: block=%d, len=%d\n", block, (int)len);
	memcpy(buf, pdata->edid_buf + block * 128, len);

	print_hex_dump(KERN_ERR, "lt9611uxd EDID: ", DUMP_PREFIX_NONE, 16, 1,
			buf, len, false);
	return 0;
}

#define MODE_SIZE(m) ((m)->hdisplay * (m)->vdisplay)
#define MODE_REFRESH_DIFF(c, t) (abs((c) - (t)))

static void lt9611uxd_choose_best_mode(struct drm_connector *connector)
{
	struct drm_display_mode *t, *cur_mode, *preferred_mode;
	int cur_vrefresh, preferred_vrefresh;
	int target_refresh = 60;

	if (list_empty(&connector->probed_modes))
		return;

	preferred_mode = list_first_entry(&connector->probed_modes,
					struct drm_display_mode, head);
	list_for_each_entry_safe(cur_mode, t, &connector->probed_modes, head) {
		cur_mode->type &= ~DRM_MODE_TYPE_PREFERRED;
		if (cur_mode == preferred_mode)
			continue;

		/* Skip 720x480 (reserved for laboratory test only) */
		if ((cur_mode->hdisplay == 720) && (cur_mode->vdisplay == 480))
			continue;

		/*Largest mode is preferred*/
		if (MODE_SIZE(cur_mode) > MODE_SIZE(preferred_mode))
			preferred_mode = cur_mode;

		cur_vrefresh = drm_mode_vrefresh(cur_mode);
		preferred_vrefresh = drm_mode_vrefresh(preferred_mode);

		/*At a given size, try to get closest to target refresh*/
		if ((MODE_SIZE(cur_mode) == MODE_SIZE(preferred_mode)) &&
			MODE_REFRESH_DIFF(cur_vrefresh, target_refresh) <
			MODE_REFRESH_DIFF(preferred_vrefresh, target_refresh) &&
			cur_vrefresh <= target_refresh) {
			preferred_mode = cur_mode;
		}
	}

	preferred_mode->type |= DRM_MODE_TYPE_PREFERRED;
}

static void lt9611uxd_set_preferred_mode(struct drm_connector *connector)
{
	struct lt9611uxd *pdata = connector_to_lt9611(connector);
	struct drm_display_mode *mode, *last_mode = NULL;
	const char *string;

	if (pdata->fix_mode) {
		list_for_each_entry(mode, &connector->probed_modes, head) {
			mode->type &= ~DRM_MODE_TYPE_PREFERRED;
			if (pdata->debug_mode.vdisplay == mode->vdisplay &&
				pdata->debug_mode.hdisplay == mode->hdisplay &&
				pdata->debug_mode.vrefresh == drm_mode_vrefresh(mode)) {
				mode->type |= DRM_MODE_TYPE_PREFERRED;
			}
		}
	} else {
		if (pdata->edid) {
			lt9611uxd_choose_best_mode(connector);
		} else {
			if (!of_property_read_string(pdata->dev->of_node,
				"lt,preferred-mode", &string)) {
				list_for_each_entry(mode, &connector->probed_modes, head) {
					mode->type &= ~DRM_MODE_TYPE_PREFERRED;
					if (!strcmp(mode->name, string))
						mode->type |= DRM_MODE_TYPE_PREFERRED;
				}
			} else {
				list_for_each_entry(mode, &connector->probed_modes, head) {
					mode->type &= ~DRM_MODE_TYPE_PREFERRED;
					last_mode = mode;
				}
				if (last_mode)
					last_mode->type |= DRM_MODE_TYPE_PREFERRED;
			}
		}
	}
}

static int lt9611uxd_connector_get_modes(struct drm_connector *connector)
{
	struct lt9611uxd *pdata = connector_to_lt9611(connector);
	struct drm_display_mode *mode, *m;
	unsigned int count = 0;

	if (pdata->edid) {
		drm_connector_update_edid_property(connector,
			pdata->edid);
		count = drm_add_edid_modes(connector, pdata->edid);

		pdata->hdmi_mode = drm_detect_hdmi_monitor(pdata->edid);
		pr_info("hdmi_mode = %d\n", pdata->hdmi_mode);
	} else {
		list_for_each_entry(mode, &pdata->mode_list, head) {
			m = drm_mode_duplicate(connector->dev, mode);
			if (!m) {
				pr_err("failed to add hdmi mode %dx%d\n",
					mode->hdisplay, mode->vdisplay);
				break;
			}
			drm_mode_probed_add(connector, m);
		}
		count = pdata->num_of_modes;
	}

	lt9611uxd_set_preferred_mode(connector);

	return count;
}

#if (KERNEL_VERSION(6, 15, 0) > LINUX_VERSION_CODE)
static enum drm_mode_status lt9611uxd_connector_mode_valid(
	struct drm_connector *connector,
	struct drm_display_mode *drm_mode)
#else
static enum drm_mode_status lt9611uxd_connector_mode_valid(
	struct drm_connector *connector,
	const struct drm_display_mode *drm_mode)
#endif
{
	struct lt9611uxd *pdata = connector_to_lt9611(connector);
	struct drm_display_mode *mode, *n;


	list_for_each_entry_safe(mode, n, &pdata->mode_list, head) {
		if (drm_mode->vdisplay == mode->vdisplay &&
			drm_mode->hdisplay == mode->hdisplay &&
			drm_mode_vrefresh(drm_mode) == drm_mode_vrefresh(mode) &&
			drm_mode->clock == mode->clock)
			return MODE_OK;
	}

	return MODE_BAD;
}

/* bridge funcs */
static void lt9611uxd_bridge_enable(struct drm_bridge *bridge)
{
	struct lt9611uxd *pdata;

	if (!bridge)
		return;

	pr_debug("bridge enable\n");

	pdata = bridge_to_lt9611(bridge);

	mutex_lock(&pdata->lock);
	pdata->bridge_enabled = true;
	mutex_unlock(&pdata->lock);
}

static void lt9611uxd_bridge_disable(struct drm_bridge *bridge)
{
	struct lt9611uxd *pdata;

	if (!bridge)
		return;

	pr_debug("bridge disable\n");

	pdata = bridge_to_lt9611(bridge);

	mutex_lock(&pdata->lock);
	pdata->bridge_enabled = false;
	mutex_unlock(&pdata->lock);
}

static void lt9611uxd_video_setup(struct lt9611uxd *pdata,
				const struct drm_display_mode *mode)
{
	int ret = 0;
	u32 h_total, hactive, hsync_len, hfront_porch, hback_porch;
	u32 v_total, vactive, vsync_len, vfront_porch, vback_porch;
	u8 framerate = drm_mode_vrefresh(mode);
	u8 vic = drm_match_cea_mode(mode);
	u8 set_video_timing_cmd[26] = {0x57, 0x4D, 0x33, 0x3A,
		// Htotal, Hactive, Hfp, Hsw, Hbp
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		// Vtotal, Vactive, Vfp, Vsw, Vbp
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		// framerate, VIC
		0x00, 0x00};
	u8 set_video_timing_ret[5];

	h_total = mode->htotal;
	v_total = mode->vtotal;

	hactive = mode->hdisplay;
	hsync_len = mode->hsync_end - mode->hsync_start;
	hfront_porch = mode->hsync_start - mode->hdisplay;
	hback_porch = mode->htotal - mode->hsync_end;

	vactive = mode->vdisplay;
	vsync_len = mode->vsync_end - mode->vsync_start;
	vfront_porch = mode->vsync_start - mode->vdisplay;
	vback_porch = mode->vtotal - mode->vsync_end;

	set_video_timing_cmd[4]  = (h_total >> 8) & 0xFF;
	set_video_timing_cmd[5]  = (h_total) & 0xFF;
	set_video_timing_cmd[6]  = (hactive >> 8) & 0xFF;
	set_video_timing_cmd[7]  = (hactive) & 0xFF;
	set_video_timing_cmd[8]  = (hfront_porch >> 8) & 0xFF;
	set_video_timing_cmd[9]  = (hfront_porch) & 0xFF;
	set_video_timing_cmd[10] = (hsync_len >> 8) & 0xFF;
	set_video_timing_cmd[11] = (hsync_len) & 0xFF;
	set_video_timing_cmd[12] = (hback_porch >> 8) & 0xFF;
	set_video_timing_cmd[13] = (hback_porch) & 0xFF;

	set_video_timing_cmd[14] = (v_total >> 8) & 0xFF;
	set_video_timing_cmd[15] = (v_total) & 0xFF;
	set_video_timing_cmd[16] = (vactive >> 8) & 0xFF;
	set_video_timing_cmd[17] = (vactive) & 0xFF;
	set_video_timing_cmd[18] = (vfront_porch >> 8) & 0xFF;
	set_video_timing_cmd[19] = (vfront_porch) & 0xFF;
	set_video_timing_cmd[20] = (vsync_len >> 8) & 0xFF;
	set_video_timing_cmd[21] = (vsync_len) & 0xFF;
	set_video_timing_cmd[22] = (vback_porch >> 8) & 0xFF;
	set_video_timing_cmd[23] = (vback_porch) & 0xFF;

	set_video_timing_cmd[24] = framerate;
	set_video_timing_cmd[25] = vic;

	if (vic == 0)
		pr_warn("lt9611uxd: VIC=0, non-standard mode, sink compatibility may vary.\n");

	// set video timing
	ret = lt9611uxd_interactive_cmd(pdata, set_video_timing_cmd, 26, set_video_timing_ret, 5);
	if (!ret)
		pr_err("failed to set video timing\n");

}

static void lt9611uxd_bridge_mode_set(struct drm_bridge *bridge,
				    const struct drm_display_mode *mode,
				    const struct drm_display_mode *adj_mode)
{
	struct lt9611uxd *pdata = bridge_to_lt9611(bridge);

	pr_info(" hdisplay=%d, vdisplay=%d, vrefresh=%d, clock=%d\n",
		adj_mode->hdisplay, adj_mode->vdisplay,
		drm_mode_vrefresh(adj_mode), adj_mode->clock);


	lt9611uxd_video_setup(pdata, adj_mode);

	drm_mode_copy(&pdata->curr_mode, adj_mode);
}


static const struct drm_connector_helper_funcs
		lt9611uxd_connector_helper_funcs = {
	.get_modes = lt9611uxd_connector_get_modes,
	.mode_valid = lt9611uxd_connector_mode_valid,
};


static const struct drm_connector_funcs lt9611uxd_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = lt9611uxd_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};


static int lt9611uxd_bridge_attach(struct drm_bridge *bridge, enum drm_bridge_attach_flags flags)
{
	struct mipi_dsi_host *host;
	struct mipi_dsi_device *dsi;
	struct lt9611uxd *pdata = bridge_to_lt9611(bridge);
	int ret;
	const struct mipi_dsi_device_info info = { .type = "lt9611",
						   .channel = 0,
						   .node = NULL,
						 };

	if (!bridge->encoder) {
		DRM_ERROR("Parent encoder object not found");
		return -ENODEV;
	}

	ret = drm_connector_init(bridge->dev, &pdata->connector,
				 &lt9611uxd_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		DRM_ERROR("Failed to initialize connector: %d\n", ret);
		return ret;
	}

	drm_connector_helper_add(&pdata->connector,
				 &lt9611uxd_connector_helper_funcs);

	ret = drm_connector_register(&pdata->connector);
	if (ret) {
		DRM_ERROR("Failed to register connector: %d\n", ret);
		return ret;
	}

	pdata->connector.polled = DRM_CONNECTOR_POLL_CONNECT;

	ret = drm_connector_attach_encoder(&pdata->connector,
						bridge->encoder);
	if (ret) {
		DRM_ERROR("Failed to link up connector to encoder: %d\n", ret);
		return ret;
	}

	host = of_find_mipi_dsi_host_by_node(pdata->host_node);
	if (!host) {
		DRM_ERROR("failed to find dsi host\n");
		return -EPROBE_DEFER;
	}

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		DRM_ERROR("failed to create dsi device\n");
		ret = PTR_ERR(dsi);
		goto err_dsi_device;
	}

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_VIDEO_HSE;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		pr_err("failed to attach dsi to host\n");
		goto err_dsi_attach;
	}

	pdata->dsi = dsi;
	pdata->bridge_attach = true;
	pr_debug("bridge_attach true\n");

	return 0;

err_dsi_attach:
	mipi_dsi_device_unregister(dsi);
err_dsi_device:
	return ret;
}

static void lt9611uxd_bridge_pre_enable(struct drm_bridge *bridge)
{
	pr_debug("bridge pre_enable\n");
}

static bool lt9611uxd_bridge_mode_fixup(struct drm_bridge *bridge,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	pr_debug(" hdisplay=%d, vdisplay=%d, vrefresh=%d, clock=%d\n",
		adjusted_mode->hdisplay, adjusted_mode->vdisplay,
		drm_mode_vrefresh(adjusted_mode), adjusted_mode->clock);

	return true;
}

static void lt9611uxd_bridge_post_disable(struct drm_bridge *bridge)
{
	pr_debug("bridge post_disable\n");

}

static const struct drm_bridge_funcs lt9611uxd_bridge_funcs = {
	.attach = lt9611uxd_bridge_attach,
	.mode_fixup   = lt9611uxd_bridge_mode_fixup,
	.pre_enable   = lt9611uxd_bridge_pre_enable,
	.enable = lt9611uxd_bridge_enable,
	.disable = lt9611uxd_bridge_disable,
	.post_disable = lt9611uxd_bridge_post_disable,
	.mode_set = lt9611uxd_bridge_mode_set,
};

/* sysfs */
static ssize_t dump_info_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t count)
{
	int num = 0;
	struct lt9611uxd *pdata = dev_get_drvdata(dev);

	if (!pdata) {
		pr_err("pdata is NULL\n");
		return -EINVAL;
	}

	for (num = 0; num < 2; num++) {
		print_hex_dump(KERN_WARNING,
				"", DUMP_PREFIX_NONE, 16, 1,
				pdata->edid_buf + num * 128,
				EDID_LENGTH, false);
	}

	return count;
}

static ssize_t get_fw_version_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	u32 fw_version;
	struct lt9611uxd *pdata = dev_get_drvdata(dev);

	if (!pdata) {
		pr_err("pdata is NULL\n");
		return -EINVAL;
	}

	if (pdata->fw_status == UPDATE_RUNNING) {
		pr_err("can't check firmware while upgrading bridge\n");
		return -EINVAL;
	}

	fw_version = lt9611uxd_get_version(pdata);
	return scnprintf(buf, PAGE_SIZE, "%#x\n", fw_version);
}

static ssize_t get_mipi_timing_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct lt9611uxd *pdata = dev_get_drvdata(dev);
	u8 get_mipi_timing_cmd[5] = {0x52, 0x4D, 0x31, 0x3A, 0x00};
	u8 get_mipi_timing_ret[9];
	u8 framerate;
	u16 h_active, v_active;

	if (!pdata) {
		pr_err("pdata is NULL\n");
		return -EINVAL;
	}

	lt9611uxd_interactive_cmd(pdata, get_mipi_timing_cmd, 5, get_mipi_timing_ret, 9);

	h_active = get_mipi_timing_ret[4] << 8 | get_mipi_timing_ret[5];
	v_active = get_mipi_timing_ret[6] << 8 | get_mipi_timing_ret[7];
	framerate = get_mipi_timing_ret[8];

	return scnprintf(buf, PAGE_SIZE, "%dx%d @ %dHz\n", h_active, v_active, framerate);
}

static ssize_t firmware_erase_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t count)
{
	struct lt9611uxd *pdata = dev_get_drvdata(dev);

	if (!pdata) {
		pr_err("pdata is NULL\n");
		return -EINVAL;
	}

	lt9611uxd_config(pdata);
	lt9611uxd_block_erase(pdata);

	return count;
}

static ssize_t firmware_upgrade_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct lt9611uxd *pdata = dev_get_drvdata(dev);
	int ret = 0;

	if (!pdata) {
		pr_err("pdata is NULL\n");
		return -EINVAL;
	}

	ret = request_firmware_nowait(THIS_MODULE, true,
		LT9611UXD_FW_BIN, &pdata->i2c_client->dev, GFP_KERNEL, pdata,
		lt9611uxd_firmware_cb);
	if (ret)
		pr_err("Failed to invoke firmware loader: %d\n", ret);
	else
		pr_info("LT9611 starts upgrade, waiting for about 40s...\n");

	return count;
}

static ssize_t firmware_upgrade_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct lt9611uxd *pdata = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pdata->fw_status);
}

static ssize_t edid_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct lt9611uxd *pdata = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%dx%d@%d\n", pdata->curr_mode.hdisplay,
		pdata->curr_mode.vdisplay, drm_mode_vrefresh(&pdata->curr_mode));
}

static ssize_t edid_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf,
	size_t count)
{
	int hdisplay = 0, vdisplay = 0, vrefresh = 0;
	struct lt9611uxd *pdata = dev_get_drvdata(dev);

	if (!pdata) {
		pr_err("pdata is NULL\n");
		return -EINVAL;
	}

	if (sscanf(buf, "%d %d %d", &hdisplay, &vdisplay, &vrefresh) != 3)
		goto err;

	if (!hdisplay || !vdisplay || !vrefresh)
		goto err;

	pdata->fix_mode = true;
	pdata->debug_mode.hdisplay = hdisplay;
	pdata->debug_mode.vdisplay = vdisplay;
	pdata->debug_mode.vrefresh = vrefresh;

	pr_info("fixed mode hdisplay=%d vdisplay=%d vrefresh=%d\n",
			hdisplay, vdisplay, vrefresh);
	return count;

err:
	pdata->fix_mode = false;
	return -EINVAL;
}

static ssize_t detect_info_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret = 0;
	int32_t command = 0;

	ret = kstrtoint(buf, 10, &command);
	pr_info("command = %d\n", command);

	if (command == 0)
		detect = 0;
	else
		detect = 1;

	return len;
}

static ssize_t detect_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, 4, "%d\n", detect);
}

static ssize_t hdmi_power_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t count)
{
	struct lt9611uxd *pdata = dev_get_drvdata(dev);
	int get = 0;

	if (!pdata) {
		pr_err("pdata is NULL\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &get);
	if (get)
		lt9611uxd_set_5v(pdata, true);
	else
		lt9611uxd_set_5v(pdata, false);

	return count;
}

static ssize_t hdmi_power_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct lt9611uxd *pdata = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pdata->hdmi_power_on ? 1 : 0);
}

static ssize_t power_mode_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct lt9611uxd *pdata = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pdata->power_mode);
}

static ssize_t power_mode_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t count)
{
	struct lt9611uxd *pdata = dev_get_drvdata(dev);
	int power_mode = 0;

	if (!pdata) {
		pr_err("pdata is NULL\n");
		return -EINVAL;
	}

	if (kstrtoint(buf, 10, &power_mode)) {
		pr_err("invalid input format\n");
		return -EINVAL;
	}

	if (power_mode < DISABLED_MODE || power_mode >= POWER_MODE_MAX) {
		pr_err("invalid power mode: %d\n", power_mode);
		return -EINVAL;
	}

	lt9611uxd_set_power_mode(pdata, power_mode);

	return count;
}

static DEVICE_ATTR_WO(dump_info);
static DEVICE_ATTR_RO(get_fw_version);
static DEVICE_ATTR_RO(get_mipi_timing);
static DEVICE_ATTR_WO(firmware_erase);
static DEVICE_ATTR_RW(firmware_upgrade);
static DEVICE_ATTR_RW(edid_mode);
static DEVICE_ATTR_RW(detect_info);
static DEVICE_ATTR_RW(hdmi_power);
static DEVICE_ATTR_RW(power_mode);

static struct attribute *lt9611uxd_sysfs_attrs[] = {
	&dev_attr_dump_info.attr,
	&dev_attr_get_fw_version.attr,
	&dev_attr_get_mipi_timing.attr,
	&dev_attr_firmware_erase.attr,
	&dev_attr_firmware_upgrade.attr,
	&dev_attr_edid_mode.attr,
	&dev_attr_detect_info.attr,
	&dev_attr_hdmi_power.attr,
	&dev_attr_power_mode.attr,
	NULL,
};

static struct attribute_group lt9611uxd_sysfs_attr_grp = {
	.attrs = lt9611uxd_sysfs_attrs,
};

static int lt9611uxd_sysfs_init(struct device *dev)
{
	int rc = 0;

	if (!dev) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	rc = sysfs_create_group(&dev->kobj, &lt9611uxd_sysfs_attr_grp);
	if (rc)
		pr_err("sysfs group creation failed %d\n", rc);

	return rc;
}

static void lt9611uxd_sysfs_remove(struct device *dev)
{
	if (!dev) {
		pr_err("Invalid params\n");
		return;
	}

	sysfs_remove_group(&dev->kobj, &lt9611uxd_sysfs_attr_grp);
}

static int lt9611uxd_init_when_fw_ok(struct lt9611uxd *pdata)
{
	struct i2c_client *client = pdata->i2c_client;
	int ret = -EINVAL;

	if (pdata->init_when_fw_ok_done)
		return 0;

	// Make sure LT9611 initialized, then enable irq.
	pdata->irq = gpio_to_irq(pdata->irq_gpio);
	ret = request_threaded_irq(pdata->irq, NULL, lt9611uxd_irq_thread_handler,
		IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "lt9611uxd_irq", pdata);
	if (ret) {
		pr_err("failed to request irq\n");
		goto err_request_irq;
	}

	pdata->init_when_fw_ok_done = true;
	return 0;

err_request_irq:
	disable_irq(pdata->irq);
	free_irq(pdata->irq, pdata);
	lt9611uxd_gpio_configure(pdata, false);
	lt9611uxd_put_dt_supply(&client->dev, pdata);
	return -ENODEV;

}

static int lt9611uxd_probe(struct i2c_client *client)
{
	struct lt9611uxd *pdata;
	int ret = 0;
	u32 revision;

	if (!client || !client->dev.of_node) {
		pr_err("invalid input\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("device doesn't support I2C\n");
		return -ENODEV;
	}

	pdata = devm_kzalloc(&client->dev,
		sizeof(struct lt9611uxd), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	ret = lt9611uxd_parse_dt(&client->dev, pdata);
	if (ret) {
		pr_err("failed to parse device tree\n");
		goto err_dt_parse;
	}

	ret = lt9611uxd_get_dt_supply(&client->dev, pdata);
	if (ret) {
		pr_err("failed to get dt supply\n");
		goto err_dt_parse;
	}

	pdata->dev = &client->dev;
	pdata->i2c_client = client;

	ret = lt9611uxd_gpio_configure(pdata, true);
	if (ret) {
		pr_err("failed to configure GPIOs\n");
		goto err_dt_supply;
	}

	lt9611uxd_set_5v(pdata, true);

	ret = lt9611uxd_enable_vreg(pdata, true);
	if (ret) {
		pr_err("failed to enable vreg\n");
		goto err_i2c_prog;
	}

	if (!cont_splash_en)
		lt9611uxd_reset(pdata, true);

	msleep(200);

	// set default power_mode
	pdata->power_mode = NORMAL_MODE;

	ret = lt9611uxd_read_device_id(pdata);
	if (ret) {
		pr_err("failed to read chip rev\n");
		goto err_i2c_prog;
	}
	lt9611uxd_select_port(pdata, PORT_SWAP_B);

	i2c_set_clientdata(client, pdata);
	dev_set_drvdata(&client->dev, pdata);

	ret = lt9611uxd_sysfs_init(&client->dev);
	if (ret) {
		pr_err("sysfs init failed\n");
		goto err_i2c_prog;
	}

	mutex_init(&pdata->lock);

	init_waitqueue_head(&pdata->edid_wq);
	INIT_WORK(&pdata->edid_work, lt9611uxd_edid_work);

#if IS_ENABLED(CONFIG_OF)
	pdata->bridge.of_node = client->dev.of_node;
#endif

	pdata->bridge.funcs = &lt9611uxd_bridge_funcs;
	drm_bridge_add(&pdata->bridge);

	pdata->hpd_wq = create_singlethread_workqueue("lt9611uxd_hpd_wq");
	if (!pdata->hpd_wq) {
		pr_err("Error creating lt9611uxd wq\n");
		goto err_i2c_prog;
	}
	INIT_WORK(&pdata->hpd_work, lt9611uxd_hpd_work);

	revision = lt9611uxd_get_version(pdata);
	if (revision) {
		pr_info("LT9611UXD FW Version 0x%x, no need to upgrade FW\n", revision);
	} else {
		pr_info("LT9611 upgrading fw: 0x%x\n", revision);
		ret = request_firmware_nowait(THIS_MODULE, true,
		"lt9611uxd_fw.bin", &pdata->i2c_client->dev, GFP_KERNEL, pdata,
		lt9611uxd_firmware_cb);
		if (ret) {
			pr_err("Failed to invoke firmware loader: %d\n", ret);
			goto err_i2c_prog;
		} else
			return 0;
	}

	return lt9611uxd_init_when_fw_ok(pdata);

err_i2c_prog:
	lt9611uxd_gpio_configure(pdata, false);
err_dt_supply:
	lt9611uxd_put_dt_supply(&client->dev, pdata);
err_dt_parse:
	return ret;
}

static void lt9611uxd_remove(struct i2c_client *client)
{
	int ret = -EINVAL;
	struct lt9611uxd *pdata = i2c_get_clientdata(client);
	struct drm_display_mode *mode, *n;

	if (!pdata)
		goto end;

	mipi_dsi_detach(pdata->dsi);
	mipi_dsi_device_unregister(pdata->dsi);

	drm_bridge_remove(&pdata->bridge);

	lt9611uxd_sysfs_remove(&client->dev);

	disable_irq(pdata->irq);
	free_irq(pdata->irq, pdata);

	ret = lt9611uxd_gpio_configure(pdata, false);

	lt9611uxd_put_dt_supply(&client->dev, pdata);

	list_for_each_entry_safe(mode, n, &pdata->mode_list, head) {
		list_del(&mode->head);
		kfree(mode);
	}

	if (pdata->hpd_wq)
		destroy_workqueue(pdata->hpd_wq);
end:
	return;
}


static struct i2c_device_id lt9611uxd_id[] = {
	{ "lt,lt9611uxd", 0},
	{}
};

static const struct of_device_id lt9611uxd_match_table[] = {
	{.compatible = "lt,lt9611uxd"},
	{}
};
MODULE_DEVICE_TABLE(of, lt9611uxd_match_table);

static struct i2c_driver lt9611uxd_driver = {
	.driver = {
		.name = "lt-lt9611uxd",
		.of_match_table = lt9611uxd_match_table,
	},
	.probe = lt9611uxd_probe,
	.remove = lt9611uxd_remove,
	.id_table = lt9611uxd_id,
};

static int __init lt9611uxd_init(void)
{
	return i2c_add_driver(&lt9611uxd_driver);
}

static void __exit lt9611uxd_exit(void)
{
	i2c_del_driver(&lt9611uxd_driver);
}

module_init(lt9611uxd_init);
module_exit(lt9611uxd_exit);
MODULE_LICENSE("GPL");
