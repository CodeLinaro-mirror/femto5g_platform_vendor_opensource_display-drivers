// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <asm/memory.h>
#include <linux/coresight-stm.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/hash.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/pm_qos.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/llcc-qcom.h>
#include <linux/version.h>
#include <linux/tracepoint.h>
#if (KERNEL_VERSION(6, 3, 0) <= LINUX_VERSION_CODE)
#include <linux/firmware/qcom/qcom_scm.h>
#else
#include <linux/qcom_scm.h>
#endif
#include <linux/soc/qcom/smem.h>
#include <linux/dma-mapping.h>
#include <linux/reset.h>
#include <linux/pm_wakeup.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include "msm_lsr_debug.h"
#include "lsr_core.h"
#include "lsr_hw_io.h"
#include "msm_lsr_clocks.h"
#include <linux/clk/qcom.h>
#include "msm_lsr_synx.h"

#define REG_ADDR_OFFSET_BITMASK	0x000FFFFF
#define QDSS_IOVA_START 0x80001000
#define MIN_PAYLOAD_SIZE 3

#define CSC_SCRATCH_BUF_SIZE 0x1000
/* GCX needs 16K of scratch memory to accommodate up to 16 UI layers*/
#define GCX_SCRATCH_BUF_SIZE 0x4000
#define ARP_BUF_SIZE     0x800000

/* Poll interval in uS */
#define POLL_INTERVAL_US 100

enum tzbsp_subsys_state {
	TZ_SUBSYS_STATE_SUSPEND = 0,
	TZ_SUBSYS_STATE_RESUME = 1,
	TZ_SUBSYS_STATE_RESTORE_THRESHOLD = 2,
};

const struct msm_lsr_gov_data CVP_DEFAULT_BUS_VOTE = {
	.data = NULL,
	.data_count = 0,
};

static enum lsr_irq_state cur_irq_state = LSR_IRQ_CLEAR;

static inline int __suspend(struct lsr_device *device);
static int __disable_regulator(struct lsr_device *device,
		const char *name);
static int __enable_regulator(struct lsr_device *device,
		const char *name);
static void __flush_debug_queue(struct lsr_device *device, u8 *packet);
static int __load_fw(struct lsr_device *device);
static int __power_on_init(struct lsr_device *device);
static void __unload_fw(struct lsr_device *device);
static int __tzbsp_set_lsr_state(enum tzbsp_subsys_state state);
static int __enable_subcaches(struct lsr_device *device);
static int __disable_subcaches(struct lsr_device *device);
static int __power_collapse(struct lsr_device *device);

static void interrupt_init_iris2(struct lsr_device *device);

static void power_off_iris2(struct lsr_device *device);

static int __disable_hw_power_collapse(struct lsr_device *device);

static int __hwfence_regs_map(struct lsr_device *device);
static int __hwfence_regs_unmap(struct lsr_device *device);

static int __reset_control_assert_name(struct lsr_device *device, const char *name);
static int __reset_control_deassert_name(struct lsr_device *device, const char *name);
static int __reset_control_acquire(struct lsr_device *device, const char *name);
static int __reset_control_release(struct lsr_device *device, const char *name);

int lsr_iommu_map(struct iommu_domain *domain, unsigned long iova, phys_addr_t paddr, size_t size,
		int prot)
{
	int rc = 0;
#if (KERNEL_VERSION(6, 2, 0) > LINUX_VERSION_CODE)
	rc = iommu_map(domain, iova,
		paddr,
		size,
		prot);
#else
	rc = iommu_map(domain, iova,
		paddr,
		size,
		prot,
		0);
#endif
	return rc;
}


#ifdef CONFIG_LSR_SERAPH

/* EVA 4.1 power sequence */
static int __power_on_controller_v1(struct lsr_device *device);
static int __power_on_core_v1(struct lsr_device *device);
static int __power_off_controller_v1(struct lsr_device *device);
static int __power_off_core_v1(struct lsr_device *device);
static int __check_ctl_power_on_v1(struct lsr_device *device);
static int __check_core_power_on_v1(struct lsr_device *device);
static void __print_sidebandmanager_regs_v1(struct lsr_device *device);
static int __enable_hw_power_collapse_v1(struct lsr_device *device);

static struct lsr_hal_ops hal_ops = {
	.interrupt_init = interrupt_init_iris2,
	.power_off_controller = __power_off_controller_v1,
	.power_off_core = __power_off_core_v1,
	.power_on_controller = __power_on_controller_v1,
	.power_on_core = __power_on_core_v1,
	.check_ctl_power_on = __check_ctl_power_on_v1,
	.check_core_power_on = __check_core_power_on_v1,
	.print_sbm_regs = __print_sidebandmanager_regs_v1,
	.enable_hw_power_collapse = __enable_hw_power_collapse_v1,
	.reset_control_assert_name = __reset_control_assert_name,
	.reset_control_deassert_name = __reset_control_deassert_name,
	.reset_control_acquire_name = __reset_control_acquire,
	.reset_control_release_name = __reset_control_release,
};
#endif

static inline void __set_state(struct lsr_device *device,
		enum lsr_hfi_state state)
{
	device->state = state;
}

static inline bool __core_in_valid_state(struct lsr_device *device)
{
	return device->state != IRIS_STATE_DEINIT;
}

static inline bool is_sys_cache_present(struct lsr_device *device)
{
	return device->res->sys_cache_present;
}

static int __acquire_regulator(struct regulator_info *rinfo,
				struct lsr_device *device)
{
	int rc = 0;

	if (rinfo->has_hw_power_collapse) {
		rc = regulator_set_mode(rinfo->regulator,
				REGULATOR_MODE_NORMAL);
		if (rc) {
			/*
			 * This is somewhat fatal, but nothing we can do
			 * about it. We can't disable the regulator w/o
			 * getting it back under s/w control
			 */
			dprintk(LSR_WARN,
				"Failed to acquire regulator control: %s\n",
					rinfo->name);
		} else {

			dprintk(LSR_PWR,
					"Acquire regulator control from HW: %s\n",
					rinfo->name);

		}
	}

	if (!regulator_is_enabled(rinfo->regulator)) {
		dprintk(LSR_WARN, "Regulator is not enabled %s\n",
			rinfo->name);
	}

	return rc;
}

static int __hand_off_regulator(struct regulator_info *rinfo)
{
	int rc = 0;

	if (rinfo->has_hw_power_collapse) {
		rc = regulator_set_mode(rinfo->regulator,
				REGULATOR_MODE_FAST);
		if (rc) {
			dprintk(LSR_WARN,
				"Failed to hand off regulator control: %s\n",
					rinfo->name);
		} else {
			dprintk(LSR_PWR,
					"Hand off regulator control to HW: %s\n",
					rinfo->name);
		}
	}

	return rc;
}

static int __hand_off_regulators(struct lsr_device *device)
{
	struct regulator_info *rinfo;
	int rc = 0, c = 0;

	iris_hfi_for_each_regulator(device, rinfo) {
		rc = __hand_off_regulator(rinfo);
		/*
		 * If one regulator hand off failed, driver should take
		 * the control for other regulators back.
		 */
		if (rc)
			goto err_reg_handoff_failed;
		c++;
	}

	return rc;
err_reg_handoff_failed:
	iris_hfi_for_each_regulator_reverse_continue(device, rinfo, c)
		__acquire_regulator(rinfo, device);

	return rc;
}

static int __take_back_regulators(struct lsr_device *device)
{
	struct regulator_info *rinfo;
	int rc = 0;

	iris_hfi_for_each_regulator(device, rinfo) {
		rc = __acquire_regulator(rinfo, device);
		/*
		 * if one regulator hand off failed, driver should take
		 * the control for other regulators back.
		 */
		if (rc)
			return rc;
	}

	return rc;
}

static int __read_queue(struct lsr_iface_q_info *qinfo, u8 *packet,
		u32 *pb_tx_req_is_set)
{
	struct lsr_hfi_queue_header *queue;
	u32 packet_size_in_words, new_read_idx;
	u32 *read_ptr;
	u32 receive_request = 0;
	u32 read_idx, write_idx;
		int rc = 0;

	if (!qinfo || !packet || !pb_tx_req_is_set) {
		dprintk(LSR_ERR, "Invalid Params\n");
		return -EINVAL;
	} else if (!qinfo->q_array.align_virtual_addr) {
		dprintk(LSR_WARN, "Queues have already been freed\n");
		return -EINVAL;
	}

	/*
	 * Memory barrier to make sure data is valid before
	 *reading it
	 */
	mb();
	queue = (struct lsr_hfi_queue_header *) qinfo->q_hdr;

	if (!queue) {
		dprintk(LSR_ERR, "Queue memory is not allocated\n");
		return -ENOMEM;
	}

	spin_lock(&qinfo->hfi_lock);

	read_idx = queue->qhdr_read_idx;
	write_idx = queue->qhdr_write_idx;

	if (read_idx == write_idx) {
		queue->qhdr_rx_req = receive_request;
		/*
		 * mb() to ensure qhdr is updated in main memory
		 * so that iris reads the updated header values
		 */
		mb();
		*pb_tx_req_is_set = 0;
		if (write_idx != queue->qhdr_write_idx) {
			queue->qhdr_rx_req = 0;
		} else {
			spin_unlock(&qinfo->hfi_lock);
			dprintk(LSR_HFI,
				"%s queue is empty, rx_req = %u, tx_req = %u, read_idx = %u\n",
				receive_request ? "message" : "debug",
				queue->qhdr_rx_req, queue->qhdr_tx_req,
				queue->qhdr_read_idx);
			return -ENODATA;
		}
	}

	read_ptr = (u32 *)((qinfo->q_array.align_virtual_addr) +
				(read_idx << 2));
	if (read_ptr < (u32 *)qinfo->q_array.align_virtual_addr ||
		read_ptr > (u32 *)(qinfo->q_array.align_virtual_addr +
		qinfo->q_array.mem_size - sizeof(*read_ptr))) {
		spin_unlock(&qinfo->hfi_lock);
		dprintk(LSR_ERR, "Invalid read index\n");
		return -ENODATA;
	}

	packet_size_in_words = (*read_ptr) >> 2;
	if (!packet_size_in_words) {
		spin_unlock(&qinfo->hfi_lock);
		dprintk(LSR_ERR, "Zero packet size\n");
		return -ENODATA;
	}

	new_read_idx = read_idx + packet_size_in_words;
	if (((packet_size_in_words << 2) <= LSR_IFACEQ_VAR_HUGE_PKT_SIZE)
			&& read_idx <= (qinfo->q_array.mem_size >> 2)) {
		if (new_read_idx < (qinfo->q_array.mem_size >> 2)) {
			memcpy(packet, read_ptr,
					packet_size_in_words << 2);
		} else {
			new_read_idx -= (qinfo->q_array.mem_size >> 2);
			memcpy(packet, read_ptr,
			(packet_size_in_words - new_read_idx) << 2);
			memcpy(packet + ((packet_size_in_words -
					new_read_idx) << 2),
					(u8 *)qinfo->q_array.align_virtual_addr,
					new_read_idx << 2);
		}
	} else {
		dprintk(LSR_WARN,
			"BAD packet received, read_idx: %#x, pkt_size: %d\n",
			read_idx, packet_size_in_words << 2);
		dprintk(LSR_WARN, "Dropping this packet\n");
		new_read_idx = write_idx;
		rc = -ENODATA;
	}

	if (new_read_idx != queue->qhdr_write_idx)
		queue->qhdr_rx_req = 0;
	else
		queue->qhdr_rx_req = receive_request;
	queue->qhdr_read_idx = new_read_idx;
	/*
	 * mb() to ensure qhdr is updated in main memory
	 * so that iris reads the updated header values
	 */
	mb();

	*pb_tx_req_is_set = (queue->qhdr_tx_req == 1) ? 1 : 0;

	spin_unlock(&qinfo->hfi_lock);

	return rc;
}

static int __smem_alloc(struct lsr_device *dev, struct lsr_mem_addr *mem,
			u32 size, u32 align, u32 flags, u32 smem_flags)
{
	struct msm_lsr_smem *alloc = &mem->mem_data;
	int rc = 0;

	if (!dev || !mem || !size) {
		dprintk(LSR_ERR, "Invalid Params\n");
		return -EINVAL;
	}

	dprintk(LSR_INFO, "start to alloc size: %d, flags: %d\n", size, flags);
	alloc->flags = flags;
	rc = msm_lsr_smem_alloc(size, align, 1, (void *)dev->res, alloc, smem_flags);
	if (rc) {
		dprintk(LSR_ERR, "Alloc failed\n");
		rc = -ENOMEM;
		goto fail_smem_alloc;
	}

	mem->mem_size = alloc->size;
	mem->align_virtual_addr = alloc->kvaddr;
	mem->align_device_addr = alloc->device_addr;
	if (smem_flags)
		mem->align_dcp_device_addr = alloc->dcp_device_addr;

	dprintk(LSR_MEM,
		"%s: ptr = %pK, size = %d dev_addr : 0x%x dcp_addr = 0x%x flags = %d\n",
		__func__, alloc->kvaddr, size, mem->align_device_addr, mem->align_dcp_device_addr,
		smem_flags);

	return rc;
fail_smem_alloc:
	return rc;
}

static void __smem_free(struct lsr_device *dev, struct msm_lsr_smem *mem)
{
	if (!dev || !mem) {
		dprintk(LSR_ERR, "invalid param %pK %pK\n", dev, mem);
		return;
	}

	msm_lsr_smem_free(mem);
}

static void __write_register(struct lsr_device *device,
		u32 reg, u32 value)
{
	u32 hwiosymaddr = reg;
	u8 *base_addr;

	if (!device) {
		dprintk(LSR_ERR, "Invalid params: %pK\n", device);
		return;
	}

	if (!device->power_enabled) {
		dprintk(LSR_WARN,
			"HFI Write register failed : Power is OFF\n");
		return;
	}

	base_addr = device->lsr_hal_data->register_base;
	dprintk(LSR_REG, "Base addr: %pK, written to: %#x, Value: %#x...\n",
		base_addr, hwiosymaddr, value);
	base_addr += hwiosymaddr;

	writel_relaxed(value, base_addr);

	/*
	 * Memory barrier to make sure value is written into the register.
	 */
	wmb();
}

static int __read_gcc_register(struct lsr_device *device, u32 reg)
{
	int rc = 0;
	u8 *base_addr;

	if (!device) {
		dprintk(LSR_ERR, "Invalid params: %pK\n", device);
		return -EINVAL;
	}

	if (!device->power_enabled) {
		dprintk(LSR_WARN,
			"%s HFI Read register failed : Power is OFF\n",
			__func__);
		return -EINVAL;
	}

	base_addr = device->lsr_hal_data->gcc_reg_base;

	rc = readl_relaxed(base_addr + reg);
	/*
	 * Memory barrier to make sure value is read correctly from the
	 * register.
	 */
	rmb();
	dprintk(LSR_REG,
		"GCC Base addr: %pK, read from: %#x, value: %#x...\n",
		base_addr, reg, rc);

	return rc;
}

static int __read_register(struct lsr_device *device, u32 reg)
{
	int rc = 0;
	u8 *base_addr;

	if (!device) {
		dprintk(LSR_ERR, "Invalid params: %pK\n", device);
		return -EINVAL;
	}

	if (!device->power_enabled) {
		dprintk(LSR_WARN,
			"HFI Read register failed : Power is OFF\n");
		return -EINVAL;
	}

	base_addr = device->lsr_hal_data->register_base;

	rc = readl_relaxed(base_addr + reg);
	/*
	 * Memory barrier to make sure value is read correctly from the
	 * register.
	 */
	rmb();
	dprintk(LSR_REG, "Base addr: %pK, read from: %#x, value: %#x...\n",
		base_addr, reg, rc);

	return rc;
}

/*
 * The existence of this function is a hack for 8996 (or certain Iris versions)
 * to overcome a hardware bug.  Whenever the GDSCs momentarily power collapse
 * (after calling __hand_off_regulators()), the values of the threshold
 * registers (typically programmed by TZ) are incorrectly reset.  As a result
 * reprogram these registers at certain agreed upon points.
 */
static void __set_threshold_registers(struct lsr_device *device)
{
	u32 version = __read_register(device, LSR_WRAPPER_HW_VERSION);

	version &= ~GENMASK(15, 0);
	if (version != (0x3 << 28 | 0x43 << 16))
		return;

	if (__tzbsp_set_lsr_state(TZ_SUBSYS_STATE_RESTORE_THRESHOLD))
		dprintk(LSR_ERR, "Failed to restore threshold values\n");
}

static int __unvote_buses(struct lsr_device *device)
{
	int rc = 0;
	struct bus_info *bus = NULL;
	struct msm_lsr_core *core;

	kfree(device->bus_vote.data);
	device->bus_vote.data = NULL;
	device->bus_vote.data_count = 0;
	core = lsr_driver->lsr_core;

	iris_hfi_for_each_bus(device, bus) {
		rc = lsr_set_bw(bus, 0);
		if (rc) {
			dprintk(LSR_ERR,
			"%s: Failed unvoting bus\n", __func__);
			goto err_unknown_device;
		}
	}

	core->old_perf.lsr_csc_bw = 0;
	core->old_perf.lsr_repro_bw = 0;

err_unknown_device:
	return rc;
}

static int iris_hfi_vote_buses(void *dev, struct bus_info *bus, unsigned long bw)
{
	int rc = 0;
	struct lsr_device *device = dev;

	if (!device)
		return -EINVAL;

	rc = lsr_set_bw(bus, bw);

	return rc;
}

static int __tzbsp_set_lsr_state(enum tzbsp_subsys_state state)
{
	int rc = 0;

	rc = qcom_scm_set_remote_state(state, TZBSP_LSR_PAS_ID);
	dprintk(LSR_CORE, "Set state %d, resp %d\n", state, rc);

	if (rc) {
		dprintk(LSR_ERR, "Failed qcom_scm_set_remote_state %d\n", rc);
		return rc;
	}

	return 0;
}

/*
 * Based on fal10_veto, X2RPMh, core_pwr_on and PWAitMode value, infer
 * value of xtss_sw_reset. xtss_sw_reset is a TZ register bit. Driver
 * cannot access it directly.
 *
 * In __boot_firmware() function, the caller of this function. It checks
 * "core_pwr_on" == false, basically core powered off. So this function
 * doesn't check core_pwr_on. Assume core_pwr_on = false.
 *
 * fal10_veto = VPU_CPU_CS_X2RPMh[2] |
 *		( ~VPU_CPU_CS_X2RPMh[1] & core_pwr_on ) |
 *		( ~VPU_CPU_CS_X2RPMh[0] & ~( xtss_sw_reset | PWaitMode ) ) ;
 */
static inline void check_tensilica_in_reset(struct lsr_device *device)
{
	u32 xtss_reset_ro = 1;

	xtss_reset_ro = __read_register(device, LSR_WRAPPER_XTSS_SW_RESET_RO);
	dprintk(LSR_WARN, "tensilica xtss_reset_ro %#x\n", xtss_reset_ro);
}

static const char boot_states[0x40][32] = {
	"NOT INIT",
	"RST_START",
	"INIT_MEMCTL",
	"INTENABLE_RST",
	"LITBASE_RST",
	"PREFETCH_EN",
	"MPU_INIT",
	"CTRL_INIT_READ",
	"MEMCTL_L1_FIX",
	"RESTORE_EXTRA_NW",
	"CORE_RESTORE",
	"COLD_BOOT",
	"DISABLE_CACHE",
	"BEFORE_MPU_C",
	"RET_MPU_C",
	"IN_MPU_C",
	"IN_MPU_DEFAULT",
	"IN_MPU_SYNX",
	"UCR_SIZE_FAIL",
	"UCR_ADDR_FAIL",
	"UCR1_SIZE_FAIL",
	"UCR1_ADDR_FAIL",
	"UCR_OVERLAPPED_UCR1",
	"UCR1_OVERLAPPED_UCR",
	"UCR_EQ_UCR1",
	"MPU_CHECK_DONE",
	"BEFORE_INT_LOCK",
	"AFTER_INT_LOCK",
	"BEFORE_INT_UNLOCK",
	"AFTER_INT_UNLOCK",
	"CALL_START",
	"MAIN_ENTRY",
	"VENUS_INIT_ENTRY",
	"VSYS_INIT_ENTRY",
	"BEFORE_XOS_CLK",
	"AFTER_XOS_CLK",
	"LOG_MUTEX_INIT",
	"CREATE_FRAMEWORK_ENTRY",
	"DTG_INIT",
	"IDLE_TASK_INIT",
	"VENUS_CORE_INIT",
	"HW_CORES_INIT",
	"RST_THREAD_INIT",
	"HOST_THREAD_INIT",
	"ALL_THREADS_INIT",
	"TASK_MEMPOOL",
	"SESSION_MUTEX",
	"SIGNALS_INIT",
	"RST_SIGNAL_INIT",
	"INTR_EN_HOST",
	"INTR_REG_HOST",
	"INTR_EN_DSP",
	"INTR_REG_DSP",
	"X2HSOFTINTEN",
	"H2XSOFTINTEN",
	"CPU2DSPINTEN",
	"DSP2CPUINT_SWRESET",
	"THREADS_START",
	"RST_THREAD_START",
	"HST_THREAD_START",
	"HST_THREAD_ENTRY"
};

static inline int __boot_firmware(struct lsr_device *device)
{
	int rc = 0;
	u32 ctrl_init_val = 0, ctrl_status = 0, count = 0, max_tries = 5000;

	/*
	 * Hand off control of regulators to h/w _after_ enabling clocks.
	 * Note that the GDSC will turn off when switching from normal
	 * (s/w triggered) to fast (HW triggered) unless the h/w vote is
	 * present. Since Iris isn't up yet, the GDSC will be off briefly.
	 */
	if (call_iris_op(device, enable_hw_power_collapse, device))
		dprintk(LSR_ERR, "Failed to enabled inter-frame PC\n");

	ctrl_init_val = BIT(0)|BIT(1);
	//TODO : set BIT(3) to disable synx based on lsr_synx config

	/* RUMI: LSR_CTRL_INIT in MPTest has bit 0 and 3 set */
	__write_register(device, LSR_CTRL_INIT, ctrl_init_val);
	while (!(ctrl_status & LSR_CTRL_INIT_STATUS__M) && count < max_tries) {
		ctrl_status = __read_register(device, LSR_CTRL_STATUS);
		if ((ctrl_status & LSR_CTRL_ERROR_STATUS__M) == 0x4) {
			dprintk(LSR_ERR, "invalid setting for UC_REGION\n");
			rc = -ENODATA;
			break;
		}
		usleep_range(50, 100);
		count++;
	}

	if (!(ctrl_status & LSR_CTRL_INIT_STATUS__M)) {
		ctrl_init_val = __read_register(device, LSR_CTRL_INIT);
		dprintk(LSR_ERR,
			"Failed to boot FW status: %x %x %s\n",
			ctrl_status, ctrl_init_val,
			boot_states[(ctrl_status >> 9) & 0x3f]);
		check_tensilica_in_reset(device);
		rc = -ENODEV;
	}

	/* Enable interrupt before sending commands to tensilica */
	__write_register(device, LSR_CPU_CS_H2XSOFTINTEN, 0x1);
	__write_register(device, LSR_CPU_CS_X2RPMh, 0x0);

	return rc;
}

int iris_hfi_resume(void *dev)
{
	int rc = 0;
	struct lsr_device *device = (struct lsr_device *) dev;

	if (!device) {
		dprintk(LSR_ERR, "%s invalid device\n", __func__);
		return -EINVAL;
	}

	dprintk(LSR_CORE, "Resuming LSR\n");
	mutex_lock(&device->lock);
	rc = __resume(device);
	mutex_unlock(&device->lock);

	return rc;
}

static int iris_hfi_suspend(void *dev)
{
	int rc = 0;
	struct lsr_device *device = (struct lsr_device *) dev;

	if (!device) {
		dprintk(LSR_ERR, "%s invalid device\n", __func__);
		return -EINVAL;
	} else if (!device->res->sw_power_collapsible) {
		dprintk(LSR_ERR, "%s SW power collapse disabled\n", __func__);
		return -EOPNOTSUPP;
	}

	dprintk(LSR_CORE, "Suspending LSR\n");
	mutex_lock(&device->lock);
	rc = __power_collapse(device);
	if (rc) {
		dprintk(LSR_WARN, "%s: LSR is busy\n", __func__);
		rc = -EBUSY;
	}
	mutex_unlock(&device->lock);

	return rc;
}

static void lsr_dump_csr(struct lsr_device *dev)
{
	u32 reg;

	if (!dev)
		return;
	if (!dev->power_enabled || dev->reg_dumped)
		return;

	reg = __read_register(dev, LSR_WRAPPER_CPU_STATUS);
	dprintk(LSR_ERR, "LSR_WRAPPER_CPU_STATUS: %x\n", reg);
	reg = __read_register(dev, LSR_CPU_CS_SCIACMDARG0);
	dprintk(LSR_ERR, "LSR_CPU_CS_SCIACMDARG0: %x\n", reg);
	reg = __read_register(dev, LSR_WRAPPER_INTR_STATUS);
	dprintk(LSR_ERR, "LSR_WRAPPER_INTR_STATUS: %x\n", reg);
	reg = __read_register(dev, LSR_CPU_CS_H2ASOFTINT);
	dprintk(LSR_ERR, "LSR_CPU_CS_H2ASOFTINT: %x\n", reg);
	reg = __read_register(dev, LSR_CPU_CS_A2HSOFTINT);
	dprintk(LSR_ERR, "LSR_CPU_CS_A2HSOFTINT: %x\n", reg);

	if (!call_iris_op(dev, check_ctl_power_on, dev)) {
		dprintk(LSR_ERR, "LSR control power on\n");
	} else {
		dprintk(LSR_ERR, "LSR control power off\n");
		return;
	}

	reg = __read_register(dev, LSR_WRAPPER_CPU_CLOCK_CONFIG);
	dprintk(LSR_ERR, "LSR_WRAPPER_CPU_CLOCK_CONFIG: %x\n", reg);
	reg = __read_register(dev, LSR_WRAPPER_CORE_CLOCK_CONFIG);
	dprintk(LSR_ERR, "LSR_WRAPPER_CORE_CLOCK_CONFIG: %x\n", reg);

	dev->reg_dumped = true;
}

static int iris_hfi_scale_clocks(void *dev, u32 freq)
{
	int rc = 0;
	struct lsr_device *device = dev;

	if (!device) {
		dprintk(LSR_ERR, "Invalid args: %pK\n", device);
		return -EINVAL;
	}

	rc = msm_lsr_set_clocks_impl(device, freq);

	return rc;
}

static int __iface_dbgq_read(struct lsr_device *device, void *pkt)
{
	u32 tx_req_is_set = 0;
	int rc = 0;
	struct lsr_iface_q_info *q_info;

	if (!pkt) {
		dprintk(LSR_ERR, "Invalid Params\n");
		return -EINVAL;
	}

	q_info = &device->iface_queues[LSR_IFACEQ_DBGQ_IDX];
	if (q_info->q_array.align_virtual_addr == NULL) {
		dprintk(LSR_ERR, "cannot read from shared DBG Q's\n");
		rc = -ENODATA;
		goto dbg_error_null;
	}

	if (!__read_queue(q_info, (u8 *)pkt, &tx_req_is_set)) {
		if (tx_req_is_set) {
			if (call_iris_op(device, check_ctl_power_on, device))
				dprintk(LSR_ERR, "%s power off, don't access reg\n", __func__);
			__write_register(device, LSR_CPU_CS_H2ASOFTINT, 1);
		}
		rc = 0;
	} else
		rc = -ENODATA;

dbg_error_null:
	return rc;
}

static void __set_queue_hdr_defaults(struct lsr_hfi_queue_header *q_hdr)
{
	q_hdr->qhdr_status = 0x1;
	q_hdr->qhdr_type = LSR_IFACEQ_DFLT_QHDR;
	q_hdr->qhdr_q_size = LSR_IFACEQ_QUEUE_SIZE / 4;
	q_hdr->qhdr_pkt_size = 0;
	q_hdr->qhdr_rx_wm = 0x1;
	q_hdr->qhdr_tx_wm = 0x1;
	q_hdr->qhdr_rx_req = 0x1;
	q_hdr->qhdr_tx_req = 0x0;
	q_hdr->qhdr_rx_irq_status = 0x0;
	q_hdr->qhdr_tx_irq_status = 0x0;
	q_hdr->qhdr_read_idx = 0x0;
	q_hdr->qhdr_write_idx = 0x0;
}

static void __interface_queues_release(struct lsr_device *device)
{
#ifdef CONFIG_EVA_TVM
	int i;
	struct lsr_hfi_mem_map_table *qdss;
	struct lsr_hfi_mem_map *mem_map;
	int num_entries = device->res->qdss_addr_set.count;
	unsigned long mem_map_table_base_addr;
	struct context_bank_info *cb;

	if (device->qdss.align_virtual_addr) {
		qdss = (struct lsr_hfi_mem_map_table *)
			device->qdss.align_virtual_addr;
		qdss->mem_map_num_entries = num_entries;
		mem_map_table_base_addr =
			device->qdss.align_device_addr +
			sizeof(struct lsr_hfi_mem_map_table);
		qdss->mem_map_table_base_addr =
			(u32)mem_map_table_base_addr;
		if ((unsigned long)qdss->mem_map_table_base_addr !=
			mem_map_table_base_addr) {
			dprintk(LSR_ERR,
				"Invalid mem_map_table_base_addr %#lx",
				mem_map_table_base_addr);
		}

		mem_map = (struct lsr_hfi_mem_map *)(qdss + 1);
		cb = msm_lsr_smem_get_context_bank(device->res, 0);

		for (i = 0; cb && i < num_entries; i++) {
			iommu_unmap(cb->domain,
						mem_map[i].virtual_addr,
						mem_map[i].size);
		}

		__smem_free(device, &device->qdss.mem_data);
	}

	__smem_free(device, &device->iface_q_table.mem_data);
	__smem_free(device, &device->sfr.mem_data);

	for (i = 0; i < LSR_IFACEQ_NUMQ; i++) {
		device->iface_queues[i].q_hdr = NULL;
		device->iface_queues[i].q_array.align_virtual_addr = NULL;
		device->iface_queues[i].q_array.align_device_addr = 0;
	}

	device->iface_q_table.align_virtual_addr = NULL;
	device->iface_q_table.align_device_addr = 0;

	device->qdss.align_virtual_addr = NULL;
	device->qdss.align_device_addr = 0;

	device->sfr.align_virtual_addr = NULL;
	device->sfr.align_device_addr = 0;

	device->mem_addr.align_virtual_addr = NULL;
	device->mem_addr.align_device_addr = 0;
#endif
}

static int __get_qdss_iommu_virtual_addr(struct lsr_device *dev,
		struct lsr_hfi_mem_map *mem_map,
		struct iommu_domain *domain)
{
	int i;
	int rc = 0;
	dma_addr_t iova = QDSS_IOVA_START;
	int num_entries = dev->res->qdss_addr_set.count;
	struct addr_range *qdss_addr_tbl = dev->res->qdss_addr_set.addr_tbl;

	if (!num_entries)
		return -ENODATA;

	for (i = 0; i < num_entries; i++) {
		if (domain) {
			rc = lsr_iommu_map(domain, iova,
					qdss_addr_tbl[i].start,
					qdss_addr_tbl[i].size,
					IOMMU_READ | IOMMU_WRITE);

			if (rc) {
				dprintk(LSR_ERR, "IOMMU QDSS mapping failed for addr %#x\n",
						qdss_addr_tbl[i].start);
				rc = -ENOMEM;
				break;
			}
		} else {
			iova =  qdss_addr_tbl[i].start;
		}

		mem_map[i].virtual_addr = (u32)iova;
		mem_map[i].physical_addr = qdss_addr_tbl[i].start;
		mem_map[i].size = qdss_addr_tbl[i].size;
		mem_map[i].attr = 0x0;

		iova += mem_map[i].size;
	}

	if (i < num_entries) {
		dprintk(LSR_ERR, "QDSS mapping failed, Freeing other entries %d\n", i);

		for (--i; domain && i >= 0; i--)
			iommu_unmap(domain,	mem_map[i].virtual_addr, mem_map[i].size);
	}

	return rc;
}

static void __setup_ucregion_memory_map(struct lsr_device *device)
{
	__write_register(device, LSR_UC_REGION_ADDR, (u32)device->iface_q_table.align_device_addr);
	__write_register(device, LSR_UC_REGION_SIZE, SHARED_QSIZE);
	__write_register(device, LSR_QTBL_ADDR,	(u32)device->iface_q_table.align_device_addr);
	__write_register(device, LSR_QTBL_INFO, 0x01);
	if (device->sfr.align_device_addr)
		__write_register(device, LSR_SFR_ADDR, (u32)device->sfr.align_device_addr);
}

static void __hfi_queue_init(struct lsr_device *dev)
{
	int i, offset = 0;
	struct lsr_hfi_queue_table_header *q_tbl_hdr;
	struct lsr_iface_q_info *iface_q;
	struct lsr_hfi_queue_header *q_hdr;

	if (!dev)
		return;

	offset += LSR_IFACEQ_TABLE_SIZE;

	for (i = 0; i < LSR_IFACEQ_NUMQ; i++) {
		iface_q = &dev->iface_queues[i];
		iface_q->q_array.align_device_addr =
			dev->iface_q_table.align_device_addr + offset;
		iface_q->q_array.align_virtual_addr =
			dev->iface_q_table.align_virtual_addr + offset;
		iface_q->q_array.mem_size = LSR_IFACEQ_QUEUE_SIZE;
		offset += iface_q->q_array.mem_size;
		iface_q->q_hdr = LSR_IFACEQ_GET_QHDR_START_ADDR(
				dev->iface_q_table.align_virtual_addr, i);
		__set_queue_hdr_defaults(iface_q->q_hdr);
		spin_lock_init(&iface_q->hfi_lock);
	}

	q_tbl_hdr = (struct lsr_hfi_queue_table_header *)
			dev->iface_q_table.align_virtual_addr;
	q_tbl_hdr->qtbl_version = 0;
	q_tbl_hdr->device_addr = (void *)dev;
	strscpy(q_tbl_hdr->name, "msm_lsr", sizeof(q_tbl_hdr->name));
	q_tbl_hdr->qtbl_size = LSR_IFACEQ_TABLE_SIZE;
	q_tbl_hdr->qtbl_qhdr0_offset =
				sizeof(struct lsr_hfi_queue_table_header);
	q_tbl_hdr->qtbl_qhdr_size = sizeof(struct lsr_hfi_queue_header);
	q_tbl_hdr->qtbl_num_q = LSR_IFACEQ_NUMQ;
	q_tbl_hdr->qtbl_num_active_q = LSR_IFACEQ_NUMQ;

	iface_q = &dev->iface_queues[LSR_IFACEQ_CMDQ_IDX];
	q_hdr = iface_q->q_hdr;
	q_hdr->qhdr_start_addr = iface_q->q_array.align_device_addr;
	q_hdr->qhdr_type |= HFI_Q_ID_HOST_TO_CTRL_CMD_Q;


	iface_q = &dev->iface_queues[LSR_IFACEQ_MSGQ_IDX];
	q_hdr = iface_q->q_hdr;
	q_hdr->qhdr_start_addr = iface_q->q_array.align_device_addr;
	q_hdr->qhdr_type |= HFI_Q_ID_CTRL_TO_HOST_MSG_Q;

	iface_q = &dev->iface_queues[LSR_IFACEQ_DBGQ_IDX];
	q_hdr = iface_q->q_hdr;
	q_hdr->qhdr_start_addr = iface_q->q_array.align_device_addr;
	q_hdr->qhdr_type |= HFI_Q_ID_CTRL_TO_HOST_DEBUG_Q;

	/*
	 * Set receive request to zero on debug queue as there is no
	 * need of interrupt from lsr hardware for debug messages
	 */
	q_hdr->qhdr_rx_req = 0;

}

static void __sfr_init(struct lsr_device *dev)
{
	struct lsr_hfi_sfr_struct *vsfr;

	if (!dev)
		return;

	vsfr = (struct lsr_hfi_sfr_struct *) dev->sfr.align_virtual_addr;
	if (vsfr)
		vsfr->bufSize = ALIGNED_SFR_SIZE;

}

static int __interface_queues_init(struct lsr_device *dev)
{
	int rc = 0;
	struct lsr_hfi_mem_map_table *qdss;
	struct lsr_hfi_mem_map *mem_map;
	struct lsr_mem_addr *mem_addr;
	int num_entries = dev->res->qdss_addr_set.count;
	phys_addr_t fw_bias = 0;
	size_t q_size;
	unsigned long mem_map_table_base_addr;
	struct context_bank_info *cb;

	q_size = SHARED_QSIZE - ALIGNED_SFR_SIZE - ALIGNED_QDSS_SIZE;
	mem_addr = &dev->mem_addr;
	if (!is_iommu_present(dev->res))
		fw_bias = dev->lsr_hal_data->firmware_base;

	if (dev->iface_q_table.align_virtual_addr) {
		memset((void *)dev->iface_q_table.align_virtual_addr,
				0, q_size);
		goto arp_buf__init;
	}
	rc = __smem_alloc(dev, mem_addr, q_size, 1, SMEM_UNCACHED, SMEM_QUEUE_TABLE);
	if (rc) {
		dprintk(LSR_ERR, "iface_q_table_alloc_fail\n");
		goto fail_alloc_queue;
	}

	dev->iface_q_table.align_virtual_addr = mem_addr->align_virtual_addr;
	dev->iface_q_table.align_device_addr = mem_addr->align_device_addr -
					fw_bias;
	dev->iface_q_table.align_dcp_device_addr = mem_addr->align_dcp_device_addr;
	dev->iface_q_table.mem_size = q_size;
	dev->iface_q_table.mem_data = mem_addr->mem_data;

arp_buf__init:

	if (dev->lsr_arp_buf.align_virtual_addr) {
		memset((void *)dev->lsr_arp_buf.align_virtual_addr,
				0, q_size);
		goto csc_scratch_init;
	}
	rc = __smem_alloc(dev, mem_addr, ARP_BUF_SIZE, 1, SMEM_UNCACHED, SMEM_ARP_BUF);
	if (rc) {
		dprintk(LSR_ERR, "lsr_arp_buf_alloc_fail\n");
		goto fail_alloc_queue;
	}

	dev->lsr_arp_buf.align_virtual_addr = mem_addr->align_virtual_addr;
	dev->lsr_arp_buf.align_device_addr = mem_addr->align_device_addr -
					fw_bias;
	dev->lsr_arp_buf.mem_size = ARP_BUF_SIZE;
	dev->lsr_arp_buf.mem_data = mem_addr->mem_data;

csc_scratch_init:

	if (dev->csc_scratch_pad.align_virtual_addr) {
		memset((void *)dev->csc_scratch_pad.align_virtual_addr,
				0, dev->csc_scratch_pad.mem_size);
		goto gcx_scratch_init;
	}
	rc = __smem_alloc(dev, mem_addr, CSC_SCRATCH_BUF_SIZE, 1, SMEM_UNCACHED, SMEM_QUEUE_TABLE);
	if (rc) {
		dprintk(LSR_ERR, "iface_q_table_alloc_fail\n");
		goto fail_alloc_queue;
	}

	dev->csc_scratch_pad.align_virtual_addr = mem_addr->align_virtual_addr;
	dev->csc_scratch_pad.align_device_addr = mem_addr->align_device_addr -
					fw_bias;
	dev->csc_scratch_pad.align_dcp_device_addr = mem_addr->align_dcp_device_addr;
	dev->csc_scratch_pad.mem_size = CSC_SCRATCH_BUF_SIZE;
	dev->csc_scratch_pad.mem_data = mem_addr->mem_data;

gcx_scratch_init:
	if (dev->gcx_scratch_pad.align_virtual_addr) {
		memset((void *)dev->gcx_scratch_pad.align_virtual_addr,
				0, dev->gcx_scratch_pad.mem_size);
		goto hfi_queue_init;
	}
	rc = __smem_alloc(dev, mem_addr, GCX_SCRATCH_BUF_SIZE, 1, SMEM_UNCACHED, SMEM_QUEUE_TABLE);
	if (rc) {
		dprintk(LSR_ERR, "iface_q_table_alloc_fail\n");
		goto fail_alloc_queue;
	}

	dev->gcx_scratch_pad.align_virtual_addr = mem_addr->align_virtual_addr;
	dev->gcx_scratch_pad.align_device_addr = mem_addr->align_device_addr -
					fw_bias;
	dev->gcx_scratch_pad.align_dcp_device_addr = mem_addr->align_dcp_device_addr;
	dev->gcx_scratch_pad.mem_size = GCX_SCRATCH_BUF_SIZE;
	dev->gcx_scratch_pad.mem_data = mem_addr->mem_data;

hfi_queue_init:
	__hfi_queue_init(dev);

	if (dev->sfr.align_virtual_addr) {
		memset((void *)dev->sfr.align_virtual_addr,
				0, ALIGNED_SFR_SIZE);
		goto sfr_init;
	}
	rc = __smem_alloc(dev, mem_addr, ALIGNED_SFR_SIZE, 1, SMEM_UNCACHED, SMEM_SFR);
	if (rc) {
		dprintk(LSR_WARN, "sfr_alloc_fail: SFR not will work\n");
		dev->sfr.align_device_addr = 0;
	} else {
		dev->sfr.align_device_addr = mem_addr->align_device_addr -
					fw_bias;
		dev->sfr.align_virtual_addr = mem_addr->align_virtual_addr;
		dev->sfr.mem_size = ALIGNED_SFR_SIZE;
		dev->sfr.mem_data = mem_addr->mem_data;
	}
sfr_init:
	__sfr_init(dev);

	if (dev->qdss.align_virtual_addr)
		goto dsp_hfi_queue_init;

	if ((msm_lsr_fw_debug_mode & HFI_DEBUG_MODE_QDSS) && num_entries) {
		rc = __smem_alloc(dev, mem_addr, ALIGNED_QDSS_SIZE, 1,
				SMEM_UNCACHED, 0);
		if (rc) {
			dprintk(LSR_WARN,
				"qdss_alloc_fail: QDSS messages logging will not work\n");
			dev->qdss.align_device_addr = 0;
		} else {
			dev->qdss.align_device_addr =
				mem_addr->align_device_addr - fw_bias;
			dev->qdss.align_virtual_addr =
				mem_addr->align_virtual_addr;
			dev->qdss.mem_size = ALIGNED_QDSS_SIZE;
			dev->qdss.mem_data = mem_addr->mem_data;
		}
	}


	if (dev->qdss.align_virtual_addr) {
		qdss =
		(struct lsr_hfi_mem_map_table *)dev->qdss.align_virtual_addr;
		qdss->mem_map_num_entries = num_entries;
		mem_map_table_base_addr = dev->qdss.align_device_addr +
			sizeof(struct lsr_hfi_mem_map_table);
		qdss->mem_map_table_base_addr = mem_map_table_base_addr;

		mem_map = (struct lsr_hfi_mem_map *)(qdss + 1);
		cb = msm_lsr_smem_get_context_bank(dev->res, 0);
		if (!cb) {
			dprintk(LSR_ERR,
				"%s: failed to get context bank\n", __func__);
			return -EINVAL;
		}

		rc = __get_qdss_iommu_virtual_addr(dev, mem_map, cb->domain);
		if (rc) {
			dprintk(LSR_ERR,
				"IOMMU mapping failed, Freeing qdss memdata\n");
			__smem_free(dev, &dev->qdss.mem_data);
			dev->qdss.align_virtual_addr = NULL;
			dev->qdss.align_device_addr = 0;
		}
	}

dsp_hfi_queue_init:
	__setup_ucregion_memory_map(dev);
	return 0;
fail_alloc_queue:
	return -ENOMEM;
}

static void lsr_pm_qos_update(struct lsr_device *device, u32 latency)
{
	int i, err = 0;

	if (device->res->pm_qos.latency_us && device->res->pm_qos.pm_qos_hdls)
		for (i = 0; i < device->res->pm_qos.silver_count; i++) {
			if (!cpu_possible(device->res->pm_qos.silver_cores[i]))
				continue;
			err = dev_pm_qos_update_request(
				&device->res->pm_qos.pm_qos_hdls[i],
				latency);
			if (err < 0) {
				dprintk(LSR_WARN,
					"pm qos on failed err %d for latency = %d\n ",
					err, latency);
			} else {
				dprintk(LSR_PWR,
					"pm qos update with latency %d on core = %d\n", latency, i);
			}
	}
}

static int iris_pm_qos_update(void *device, u32 latency)
{
	struct lsr_device *dev;

	if (!device) {
		dprintk(LSR_ERR, "%s Invalid device\n", __func__);
		return -ENODEV;
	}

	dev = device;

	mutex_lock(&dev->lock);
	lsr_pm_qos_update(dev, latency);
	mutex_unlock(&dev->lock);

	return 0;
}

static int __hwfence_regs_map(struct lsr_device *device)
{
	int rc = 0;
	struct context_bank_info *cb;

	cb = msm_lsr_smem_get_context_bank(device->res, 0);
	if (!cb) {
		dprintk(LSR_ERR, "%s: fail to get cb\n", __func__);
		return -EINVAL;
	}

	if (device->res->reg_mappings.ipclite_phyaddr != 0) {
		rc = lsr_iommu_map(cb->domain,
			device->res->reg_mappings.ipclite_iova,
			device->res->reg_mappings.ipclite_phyaddr,
			device->res->reg_mappings.ipclite_size,
			IOMMU_READ | IOMMU_WRITE);
		if (rc) {
			dprintk(LSR_ERR, "map ipclite fail %d %#llx %#llx %#x\n",
				rc, device->res->reg_mappings.ipclite_iova,
				device->res->reg_mappings.ipclite_phyaddr,
				device->res->reg_mappings.ipclite_size);
			return rc;
		}
	}
	if (device->res->reg_mappings.hwmutex_phyaddr != 0) {
		rc = lsr_iommu_map(cb->domain,
			device->res->reg_mappings.hwmutex_iova,
			device->res->reg_mappings.hwmutex_phyaddr,
			device->res->reg_mappings.hwmutex_size,
			IOMMU_MMIO | IOMMU_READ | IOMMU_WRITE);
		if (rc) {
			dprintk(LSR_ERR, "map hwmutex fail %d %#llx %#llx %#x\n",
				rc, device->res->reg_mappings.hwmutex_iova,
				device->res->reg_mappings.hwmutex_phyaddr,
				device->res->reg_mappings.hwmutex_size);
			return rc;
		}
	}
	if (device->res->reg_mappings.aon_phyaddr != 0) {
		rc = lsr_iommu_map(cb->domain,
			device->res->reg_mappings.aon_iova,
			device->res->reg_mappings.aon_phyaddr,
			device->res->reg_mappings.aon_size,
			IOMMU_MMIO | IOMMU_READ | IOMMU_WRITE);
		if (rc) {
			dprintk(LSR_ERR, "map aon fail %d %#llx %#llx %#x\n",
				rc, device->res->reg_mappings.aon_iova,
				device->res->reg_mappings.aon_phyaddr,
				device->res->reg_mappings.aon_size);
			return rc;
		}
	}
	if (device->res->reg_mappings.timer_phyaddr != 0) {
		rc = lsr_iommu_map(cb->domain,
			device->res->reg_mappings.timer_iova,
			device->res->reg_mappings.timer_phyaddr,
			device->res->reg_mappings.timer_size,
			IOMMU_MMIO | IOMMU_READ | IOMMU_WRITE);
		if (rc) {
			dprintk(LSR_ERR, "map timer fail %d %#llx %#llx %#x\n",
				rc, device->res->reg_mappings.timer_iova,
				device->res->reg_mappings.timer_phyaddr,
				device->res->reg_mappings.timer_size);
			return rc;
		}
	}

	return rc;
}

static int __hwfence_regs_unmap(struct lsr_device *device)
{
	int rc = 0;
	struct context_bank_info *cb;

	cb = msm_lsr_smem_get_context_bank(device->res, 0);
	if (!cb) {
		dprintk(LSR_ERR, "%s: fail to get cb\n", __func__);
		return -EINVAL;
	}

	if (device->res->reg_mappings.ipclite_iova != 0) {
		iommu_unmap(cb->domain,
			device->res->reg_mappings.ipclite_iova,
			device->res->reg_mappings.ipclite_size);
	}
	if (device->res->reg_mappings.hwmutex_iova != 0) {
		iommu_unmap(cb->domain,
			device->res->reg_mappings.hwmutex_iova,
			device->res->reg_mappings.hwmutex_size);
	}
	if (device->res->reg_mappings.aon_iova != 0) {
		iommu_unmap(cb->domain,
			device->res->reg_mappings.aon_iova,
			device->res->reg_mappings.aon_size);
	}
	if (device->res->reg_mappings.timer_iova != 0) {
		iommu_unmap(cb->domain,
			device->res->reg_mappings.timer_iova,
			device->res->reg_mappings.timer_size);
	}
	return rc;
}

int iris_hfi_core_init(void *device)
{
	int rc = 0;
	u32 ipcc_iova;
	struct lsr_device *dev;

	if (!device) {
		dprintk(LSR_ERR, "Invalid device\n");
		return -ENODEV;
	}

	dev = device;

	dprintk(LSR_CORE, "Core initializing\n");

	pm_stay_awake(dev->res->pdev->dev.parent);
	mutex_lock(&dev->lock);

	dev->bus_vote.data =
		kzalloc(sizeof(struct lsr_bus_vote_data), GFP_KERNEL);
	if (!dev->bus_vote.data) {
		dprintk(LSR_ERR, "Bus vote data memory is not allocated\n");
		rc = -ENOMEM;
		goto err_no_mem;
	}

	dev->bus_vote.data_count = 1;
	__hwfence_regs_map(dev);

	rc = __power_on_init(dev);
	if (rc) {
		dprintk(LSR_ERR, "Failed to power on init EVA\n");
		goto err_load_fw;
	}

	__set_state(dev, IRIS_STATE_INIT);
	dev->reg_dumped = false;

	dprintk(LSR_CORE, "Dev_Virt: %pa, Reg_Virt: %pK\n",
		&dev->lsr_hal_data->firmware_base,
		dev->lsr_hal_data->register_base);

	rc = __interface_queues_init(dev);
	if (rc) {
		dprintk(LSR_ERR, "failed to init queues\n");
		rc = -ENOMEM;
		goto err_core_init;
	}

	rc = msm_lsr_map_ipcc_regs(&ipcc_iova);
	if (!rc) {
		dprintk(LSR_CORE, "IPCC iova  : 0x%x\n", ipcc_iova);
		__write_register(dev, CVP_MMAP_ADDR, ipcc_iova);
	}

	rc = __load_fw(dev);
	if (rc) {
		dprintk(LSR_ERR, "Failed to load Iris FW\n");
		goto err_core_init;
	}

	__enable_subcaches(device);

	if (dev->res->pm_qos.latency_us) {
		int err = 0;
		u32 i, cpu;

		dprintk(LSR_PWR, "Enabling pm_qos_hdls\n");
		dev->res->pm_qos.pm_qos_hdls = kcalloc(
				dev->res->pm_qos.silver_count,
				sizeof(struct dev_pm_qos_request),
				GFP_KERNEL);

		if (!dev->res->pm_qos.pm_qos_hdls) {
			dprintk(LSR_WARN, "Failed allocate pm_qos_hdls\n");
			goto pm_qos_bail;
		}

		for (i = 0; i < dev->res->pm_qos.silver_count; i++) {
			cpu = dev->res->pm_qos.silver_cores[i];
			if (!cpu_possible(cpu))
				continue;
			err = dev_pm_qos_add_request(
				get_cpu_device(cpu),
				&dev->res->pm_qos.pm_qos_hdls[i],
				DEV_PM_QOS_RESUME_LATENCY,
				dev->res->pm_qos.latency_us);
			if (err < 0)
				dprintk(LSR_WARN,
					"%s pm_qos_add_req %d failed\n",
					__func__, i);
		}
	}

	if (dev->res->pm_qos.latency_us && dev->res->pm_qos.pm_qos_hdls)
		lsr_pm_qos_update(dev, PM_QOS_RESUME_LATENCY_DEFAULT_VALUE);

pm_qos_bail:
	mutex_unlock(&dev->lock);
	pm_relax(dev->res->pdev->dev.parent);
	dprintk(LSR_CORE, "Core inited successfully\n");
	return 0;

err_core_init:
	__set_state(dev, IRIS_STATE_DEINIT);
	__unload_fw(dev);
err_load_fw:
	__hwfence_regs_unmap(dev);
err_no_mem:
	dprintk(LSR_ERR, "Core init failed\n");
	mutex_unlock(&dev->lock);
	pm_relax(dev->res->pdev->dev.parent);
	return rc;
}

static int iris_hfi_core_release(void *dev)
{
	int rc = 0, i;
	struct lsr_device *device = dev;
	struct dev_pm_qos_request *qos_hdl;
	u32 ipcc_iova;

	if (!device) {
		dprintk(LSR_ERR, "invalid device\n");
		return -ENODEV;
	}

	mutex_lock(&device->lock);
	dprintk(LSR_WARN, "Core releasing\n");
	if (device->res->pm_qos.latency_us &&
		device->res->pm_qos.pm_qos_hdls) {
		for (i = 0; i < device->res->pm_qos.silver_count; i++) {
			if (!cpu_possible(device->res->pm_qos.silver_cores[i]))
				continue;
			qos_hdl = &device->res->pm_qos.pm_qos_hdls[i];
			if ((qos_hdl != NULL) && dev_pm_qos_request_active(qos_hdl))
				dev_pm_qos_remove_request(qos_hdl);
		}
		kfree(device->res->pm_qos.pm_qos_hdls);
		device->res->pm_qos.pm_qos_hdls = NULL;
	}

	__resume(device);
	__set_state(device, IRIS_STATE_DEINIT);
	rc = __tzbsp_set_lsr_state(TZ_SUBSYS_STATE_SUSPEND);
	if (rc)
		dprintk(LSR_WARN, "Failed to suspend lsr FW%d\n", rc);

	__disable_subcaches(device);
	ipcc_iova = __read_register(device, CVP_MMAP_ADDR);
	msm_lsr_unmap_ipcc_regs(ipcc_iova);
	__unload_fw(device);
	__hwfence_regs_unmap(device);

	dprintk(LSR_CORE, "Core released successfully\n");
	mutex_unlock(&device->lock);

	return rc;
}

static void __core_clear_interrupt(struct lsr_device *device)
{
	u32 intr_status = 0, mask = 0;

	if (!device) {
		dprintk(LSR_ERR, "%s: NULL device\n", __func__);
		return;
	}

	intr_status = __read_register(device, LSR_WRAPPER_INTR_STATUS);
	mask = (LSR_WRAPPER_INTR_MASK_A2HCPU_BMSK | CVP_FATAL_INTR_BMSK);

	if (intr_status & mask) {
		device->intr_status |= intr_status;
		device->reg_count++;
		dprintk(LSR_CORE,
			"INTERRUPT for device: %pK: times: %d status: %d\n",
			device, device->reg_count, intr_status);
	} else {
		device->spur_count++;
	}

	__write_register(device, LSR_CPU_CS_A2HSOFTINTCLR, 1);
}

static int lsr_debug_hook(void *device)
{
	struct lsr_device *dev = device;
	u32 val;

	if (!device) {
		dprintk(LSR_ERR, "%s Invalid device\n", __func__);
		return -ENODEV;
	}
	//__write_register(dev, LSR_WRAPPER_CORE_CLOCK_CONFIG, 0x11);
	//__write_register(dev, LSR_WRAPPER_TZ_CPU_CLOCK_CONFIG, 0x1);
	val = __read_register(dev, LSR_WRAPPER_CORE_CLOCK_CONFIG);
	dprintk(LSR_ERR, "Halt Tensilica and core and axi\n");
	return 0;
}


/* Ensure caller function hold device->lock!!! */
static int __power_collapse(struct lsr_device *device)
{
	int rc = 0;
	u32 wfi_status = 0, idle_status = 0, pc_ready = 0;
	int count = 0;
	const int max_tries = 150;

	if (!device) {
		dprintk(LSR_ERR, "%s: invalid params\n", __func__);
		return -EINVAL;
	}
	if (!device->power_enabled) {
		dprintk(LSR_PWR, "%s: Power already disabled\n",
				__func__);
		goto exit;
	}

	rc = __core_in_valid_state(device);
	if (!rc) {
		dprintk(LSR_WARN, "Core is in bad state, Skipping power collapse\n");
		return -EINVAL;
	}

	__flush_debug_queue(device, NULL);

	pc_ready = __read_register(device, LSR_CTRL_STATUS) &
		LSR_CTRL_STATUS_PC_READY;
	if (!pc_ready) {
		wfi_status = __read_register(device,
				LSR_WRAPPER_CPU_STATUS);
		idle_status = __read_register(device,
				LSR_CTRL_STATUS);
		if (!(wfi_status & BIT(0))) {
			dprintk(LSR_WARN,
				"Skipping PC as wfi_status (%#x) bit not set\n",
				wfi_status);
			goto skip_power_off;
		}
		if (!(idle_status & BIT(30))) {
			dprintk(LSR_WARN,
				"Skipping PC as idle_status (%#x) bit not set\n",
				idle_status);
			goto skip_power_off;
		}

		while (count < max_tries) {
			wfi_status = __read_register(device,
					LSR_WRAPPER_CPU_STATUS);
			pc_ready = __read_register(device,
					LSR_CTRL_STATUS);
			if ((wfi_status & BIT(0)) && (pc_ready &
				LSR_CTRL_STATUS_PC_READY))
				break;
			usleep_range(150, 250);
			count++;
		}

		if (count == max_tries) {
			dprintk(LSR_ERR,
				"Skip PC. Core is not ready (%#x, %#x)\n",
				wfi_status, pc_ready);
			goto skip_power_off;
		}
	} else {
		wfi_status = __read_register(device, LSR_WRAPPER_CPU_STATUS);
		if (!(wfi_status & BIT(0))) {
			dprintk(LSR_WARN,
				"Skip PC as wfi_status (%#x) bit not set\n",
				wfi_status);
			goto skip_power_off;
		}
	}

	dprintk(LSR_CORE, "Suspending LSR\n");
	rc = __suspend(device);
	if (rc)
		dprintk(LSR_ERR, "Failed __suspend\n");
	dprintk(LSR_PWR, "LSR Power collapsed\n");
exit:
	return rc;

skip_power_off:
	dprintk(LSR_PWR, "Skip PC(%#x, %#x, %#x)\n", wfi_status, idle_status, pc_ready);
	__flush_debug_queue(device, NULL);
	lsr_dump_csr(device);
	return -EAGAIN;
}

static void __flush_debug_queue(struct lsr_device *device, u8 *packet)
{
	bool local_packet = false;
	enum lsr_msg_prio log_level = LSR_FW;

	if (!device) {
		dprintk(LSR_ERR, "%s: Invalid params\n", __func__);
		return;
	}

	if (!packet) {
		packet = kzalloc(LSR_IFACEQ_VAR_HUGE_PKT_SIZE, GFP_KERNEL);
		if (!packet) {
			dprintk(LSR_ERR, "In %s() Fail to allocate mem\n",
				__func__);
			return;
		}

		local_packet = true;
	}

#define SKIP_INVALID_PKT(pkt_size, payload_size, pkt_hdr_size) ({ \
		if (pkt_size < pkt_hdr_size || \
			payload_size < MIN_PAYLOAD_SIZE || \
			payload_size > \
			(pkt_size - pkt_hdr_size + sizeof(u8))) { \
			dprintk(LSR_ERR, \
				"%s: invalid msg size - %d\n", \
				__func__, pkt->msg_size); \
			continue; \
		} \
	})

	while (!__iface_dbgq_read(device, packet)) {
		struct lsr_hfi_packet_header *pkt =
			(struct lsr_hfi_packet_header *) packet;

		if (pkt->size < sizeof(struct lsr_hfi_packet_header)) {
			dprintk(LSR_ERR, "Invalid pkt size - %s\n",
				__func__);
			continue;
		} else {
			struct lsr_hfi_msg_sys_debug_packet *pkt =
				(struct lsr_hfi_msg_sys_debug_packet *) packet;
			SKIP_INVALID_PKT(pkt->size,
				pkt->msg_size, sizeof(*pkt));
			/*
			 * All fw messages starts with new line character. This
			 * causes dprintk to print this message in two lines
			 * in the kernel log. Ignoring the first character
			 * from the message fixes this to print it in a single
			 * line.
			 */
			pkt->rg_msg_data[pkt->msg_size-1] = '\0';
			dprintk(log_level, "%s", &pkt->rg_msg_data[1]);
		}
	}
#undef SKIP_INVALID_PKT

	if (local_packet)
		kfree(packet);
}

#define _INVALID_MSG_ "Unrecognized MSG (%#x) session (%pK), discarding\n"
#define _INVALID_STATE_ "Ignore responses from %d to %d invalid state\n"
#define _DEVFREQ_FAIL_ "Failed to add devfreq device bus %s governor %s: %d\n"

int __response_handler(struct lsr_device *device)
{
	int lsr_status = 0;

	if (!device || device->state != IRIS_STATE_INIT)
		return 0;

	lsr_status = __read_register(device, LSR_CTRL_STATUS);

	if (device->intr_status & CVP_FATAL_INTR_BMSK) {
		if (device->intr_status & LSR_WRAPPER_INTR_MASK_CPU_NOC_BMSK)
			pr_err_ratelimited(LSR_PID_TAG "Received Xtensa NOC error\n",
				current->pid, current->tgid, "err");
		if (device->intr_status & LSR_WRAPPER_INTR_MASK_CORE_NOC_BMSK)
			pr_err_ratelimited(LSR_PID_TAG "Received CVP core NOC error\n",
				current->pid, current->tgid, "err");
	}

	__flush_debug_queue(device, NULL);
	return 0;
}

irqreturn_t iris_hfi_core_work_handler(int irq, void *data)
{
	struct msm_lsr_core *core;
	struct lsr_device *device;
	int num_responses = 0;
	u32 intr_status;
	static bool warning_on = true;

	cur_irq_state = LSR_IRQ_ACCEPTED;
	core = lsr_driver->lsr_core;

	if (core) {
		device = core->dev_ops->hfi_device_data;
	} else {
		WARN_ONCE(true, "LSR Core is not created\n");
		cur_irq_state = LSR_IRQ_CLEAR;
		return IRQ_HANDLED;
	}

	mutex_lock(&device->lock);
	cur_irq_state = LSR_IRQ_PROCESSED;
	if (!__core_in_valid_state(device)) {
		if (warning_on) {
			dprintk(LSR_WARN, "%s Core not in init state\n",
				__func__);
			warning_on = false;
		}
		goto err_no_work;
	}

	warning_on = true;

	if (__resume(device)) {
		dprintk(LSR_ERR, "%s: Power enable failed\n", __func__);
		goto err_no_work;
	}

	__core_clear_interrupt(device);
	num_responses = __response_handler(device);
	dprintk(LSR_HFI, "%s:: lsr_driver_debug num_responses = %d ", __func__, num_responses);

err_no_work:
	/* Keep the interrupt status before releasing device lock */
	intr_status = device->intr_status;
	mutex_unlock(&device->lock);

	/* We need re-enable the irq which was disabled in ISR handler */
	if (!(intr_status & LSR_WRAPPER_INTR_STATUS_A2HWD_BMSK))
		enable_irq(device->lsr_hal_data->irq);
	cur_irq_state = LSR_IRQ_CLEAR;
	return IRQ_HANDLED;
}

irqreturn_t lsr_hfi_isr(int irq, void *dev)
{

	disable_irq_nosync(irq);

	return IRQ_WAKE_THREAD;
}

static int __init_reset_clk(struct msm_lsr_platform_resources *res,
			int reset_index)
{
	int rc = 0;
	struct reset_control *rst;
	struct reset_info *rst_info;
	struct reset_set *rst_set = &res->reset_set;

	if (!rst_set->reset_tbl)
		return 0;

	rst_info = &rst_set->reset_tbl[reset_index];
	rst = rst_info->rst;
	dprintk(LSR_PWR, "reset_clk: name %s rst %pK required_stage=%d\n",
		rst_set->reset_tbl[reset_index].name, rst, rst_info->required_stage);

	if (rst)
		goto skip_reset_init;

	if (rst_info->required_stage == LSR_ON_USE) {
		rst = reset_control_get_exclusive_released(&res->pdev->dev,
			rst_set->reset_tbl[reset_index].name);
		if (IS_ERR(rst)) {
			rc = PTR_ERR(rst);
			dprintk(LSR_ERR, "reset get exclusive fail %d\n", rc);
			return rc;
		}
		dprintk(LSR_PWR, "reset_clk: name %s get exclusive rst %p\n",
				rst_set->reset_tbl[reset_index].name, rst);
	} else if (rst_info->required_stage == LSR_ON_INIT) {
		rst = devm_reset_control_get(&res->pdev->dev,
				rst_set->reset_tbl[reset_index].name);
		if (IS_ERR(rst)) {
			rc = PTR_ERR(rst);
			dprintk(LSR_ERR, "reset get fail %d\n", rc);
			return rc;
		}
		dprintk(LSR_PWR, "reset_clk: name %s get rst %p\n",
				rst_set->reset_tbl[reset_index].name, rst);
	} else {
		dprintk(LSR_ERR, "Invalid reset stage\n");
		return -EINVAL;
	}

	rst_set->reset_tbl[reset_index].rst = rst;
	rst_info->state = RESET_INIT;

	return 0;

skip_reset_init:
	return rc;
}

static int __reset_control_assert_name(struct lsr_device *device,
	const char *name)
{
	struct reset_info *rcinfo = NULL;
	int rc = 0;
	bool found = false;

	iris_hfi_for_each_reset_clock(device, rcinfo) {
		if (strcmp(rcinfo->name, name))
			continue;

		found = true;
		rc = reset_control_assert(rcinfo->rst);
		if (rc)
			dprintk(LSR_ERR,
				"%s: failed to assert reset control (%s), rc = %d\n",
				__func__, rcinfo->name, rc);
		else
			dprintk(LSR_PWR, "%s: assert reset control (%s)\n",
				__func__, rcinfo->name);
		break;
	}
	if (!found) {
		dprintk(LSR_PWR, "%s: reset control (%s) not found\n",
			__func__, name);
		rc = -EINVAL;
	}

	return rc;
}

static int __reset_control_deassert_name(struct lsr_device *device,
	const char *name)
{
	struct reset_info *rcinfo = NULL;
	int rc = 0;
	bool found = false;

	iris_hfi_for_each_reset_clock(device, rcinfo) {
		if (strcmp(rcinfo->name, name))
			continue;
		found = true;
		rc = reset_control_deassert(rcinfo->rst);
		if (rc)
			dprintk(LSR_ERR,
				"%s: deassert reset control for (%s) failed, rc %d\n",
				__func__, rcinfo->name, rc);
		else
			dprintk(LSR_PWR, "%s: deassert reset control (%s)\n",
				__func__, rcinfo->name);
		break;
	}
	if (!found) {
		dprintk(LSR_PWR, "%s: reset control (%s) not found\n",
			__func__, name);
		rc = -EINVAL;
	}

	return rc;
}

static int __reset_control_acquire(struct lsr_device *device,
	const char *name)
{
	struct reset_info *rcinfo = NULL;
	int rc = 0;
	bool found = false;
	int max_retries = 1000;

	iris_hfi_for_each_reset_clock(device, rcinfo) {
		if (strcmp(rcinfo->name, name))
			continue;
		found = true;
		if (rcinfo->state == RESET_ACQUIRED)
			return rc;
acquire_again:
		rc = reset_control_acquire(rcinfo->rst);
		if (rc) {
			if (rc == -EBUSY) {
				usleep_range(1000, 1500);
				max_retries--;
				if (max_retries) {
					goto acquire_again;
				} else {
					dprintk(LSR_ERR, "%s acquire %s -EBUSY\n", __func__,
							rcinfo->name);
					rc = -EINVAL;
				}
			} else {
				dprintk(LSR_ERR,
					"%s: acquire failed (%s) rc %d\n",
					__func__, rcinfo->name, rc);
				rc = -EINVAL;
			}
		} else {
			dprintk(LSR_PWR, "%s: reset acquire succeed (%s)\n",
				__func__, rcinfo->name);
			rcinfo->state = RESET_ACQUIRED;
		}
		break;
	}
	if (!found) {
		dprintk(LSR_PWR, "%s: reset control (%s) not found\n",
			__func__, name);
		rc = -EINVAL;
	}

	return rc;
}

static int __reset_control_release(struct lsr_device *device,
	const char *name)
{
	struct reset_info *rcinfo = NULL;
	int rc = 0;
	bool found = false;

	iris_hfi_for_each_reset_clock(device, rcinfo) {
		if (strcmp(rcinfo->name, name))
			continue;
		found = true;
		if (rcinfo->state != RESET_ACQUIRED) {
			dprintk(LSR_WARN, "Double releasing reset clk?\n");
			return -EINVAL;
		}
		reset_control_release(rcinfo->rst);
		dprintk(LSR_PWR, "%s: reset release succeed (%s)\n",
			__func__, rcinfo->name);
		rcinfo->state = RESET_RELEASED;
		break;
	}
	if (!found) {
		dprintk(LSR_PWR, "%s: reset control (%s) not found\n",
			__func__, name);
		rc = -EINVAL;
	}

	return rc;
}

static void __deinit_bus(struct lsr_device *device)
{
	struct bus_info *bus = NULL;

	if (!device)
		return;

	kfree(device->bus_vote.data);
	device->bus_vote = CVP_DEFAULT_BUS_VOTE;

	iris_hfi_for_each_bus_reverse(device, bus) {
		dev_set_drvdata(bus->dev, NULL);
		icc_put(bus->client);
		bus->client = NULL;
	}
}

static int __init_bus(struct lsr_device *device)
{
	struct bus_info *bus = NULL;
	int rc = 0;

	if (!device)
		return -EINVAL;

	iris_hfi_for_each_bus(device, bus) {
		/*
		 * This is stupid, but there's no other easy way to ahold
		 * of struct bus_info in iris_hfi_devfreq_*()
		 */

		if (dev_get_drvdata(bus->dev))
			dprintk(LSR_WARN, "%s's drvdata already set\n", dev_name(bus->dev));

		dev_set_drvdata(bus->dev, device);
		bus->client = of_icc_get(bus->dev, bus->name);

		if (IS_ERR_OR_NULL(bus->client)) {
			rc = PTR_ERR(bus->client) ?: -EBADHANDLE;
			dprintk(LSR_ERR, "Failed to register bus %s: %d\n", bus->name, rc);
			bus->client = NULL;
			goto err_add_dev;
		}
	}

	return 0;

err_add_dev:
	__deinit_bus(device);
	return rc;
}

static void __deinit_regulators(struct lsr_device *device)
{
	struct regulator_info *rinfo = NULL;

	iris_hfi_for_each_regulator_reverse(device, rinfo) {
		if (rinfo->regulator) {
			regulator_put(rinfo->regulator);
			rinfo->regulator = NULL;
		}
	}
}

static int __init_regulators(struct lsr_device *device)
{
	int rc = 0;
	struct regulator_info *rinfo = NULL;

	iris_hfi_for_each_regulator(device, rinfo) {
		rinfo->regulator = regulator_get(&device->res->pdev->dev,
				rinfo->name);
		if (IS_ERR_OR_NULL(rinfo->regulator)) {
			rc = PTR_ERR(rinfo->regulator) ?: -EBADHANDLE;
			dprintk(LSR_ERR, "Failed to get regulator: %s\n",
					rinfo->name);
			rinfo->regulator = NULL;
			goto err_reg_get;
		}
	}

	return 0;

err_reg_get:
	__deinit_regulators(device);
	return rc;
}

static void __deinit_subcaches(struct lsr_device *device)
{
	struct subcache_info *sinfo = NULL;

	if (!device) {
		dprintk(LSR_ERR, "deinit_subcaches: invalid device %pK\n",
			device);
		goto exit;
	}

	if (!is_sys_cache_present(device))
		goto exit;

	iris_hfi_for_each_subcache_reverse(device, sinfo) {
		if (sinfo->subcache) {
			dprintk(LSR_CORE, "deinit_subcaches: %s\n",
				sinfo->name);
			llcc_slice_putd(sinfo->subcache);
			sinfo->subcache = NULL;
		}
	}

exit:
	return;
}

static int __init_subcaches(struct lsr_device *device)
{
	int rc = 0;
	struct subcache_info *sinfo = NULL;

	if (!device) {
		dprintk(LSR_ERR, "init_subcaches: invalid device %pK\n",
			device);
		return -EINVAL;
	}

	if (msm_lsr_syscache_disable || !is_sys_cache_present(device)) {
		dprintk(LSR_ERR, "LLCC for LSR is disabled");
		return 0;
	}

	iris_hfi_for_each_subcache(device, sinfo) {
		if (!strcmp("llcc_gcx_to_dpu_left", sinfo->name)) {
			sinfo->subcache = llcc_slice_getd(LLCC_GCX_TO_DPU_LEFT);
		} else if (!strcmp("llcc_gcx_to_dpu_right", sinfo->name)) {
			sinfo->subcache = llcc_slice_getd(LLCC_GCX_TO_DPU_RIGHT);
		} else if (!strcmp("llcc_csc_layer0", sinfo->name)) {
			sinfo->subcache = llcc_slice_getd(LLCC_CSC_LAYER0);
		} else if (!strcmp("llcc_csc_layer1", sinfo->name)) {
			sinfo->subcache = llcc_slice_getd(LLCC_CSC_LAYER1);
		} else if (!strcmp("llcc_csc_layer2", sinfo->name)) {
			sinfo->subcache = llcc_slice_getd(LLCC_CSC_LAYER2);
		} else if (!strcmp("llcc_csc_layer3", sinfo->name)) {
			sinfo->subcache = llcc_slice_getd(LLCC_CSC_LAYER3);
		} else {
			dprintk(LSR_ERR, "Invalid subcache name %s\n",
					sinfo->name);
		}
		if (IS_ERR_OR_NULL(sinfo->subcache)) {
			rc = PTR_ERR(sinfo->subcache) ? PTR_ERR(sinfo->subcache) : -EBADHANDLE;
			dprintk(LSR_ERR, "init_subcaches: lsr subcache disabled: %s rc %d\n",
				sinfo->name, rc);
			sinfo->subcache = NULL;
			goto err_subcache_get;
		}
		dprintk(LSR_CORE, "init_subcaches: %s\n", sinfo->name);
	}
	return 0;

err_subcache_get:
	__deinit_subcaches(device);
	return 0;
}

static int __init_synx(struct lsr_device *device)
{
	int rc = 0;

	if (!device) {
		dprintk(LSR_ERR, "init_synx: invalid device %pK\n", device);
		return -EINVAL;
	}

	lsr_synx_ftbl_init(device);
	rc = device->synx_ftbl->lsr_sess_init_synx(device);
	return rc;
}

static void __deinit_power_domains(struct lsr_device *device)
{
	struct power_domain_info *pd_info = NULL;

	iris_hfi_for_each_power_domain_reverse(device, pd_info) {
		if (pd_info->pd_dev) {
			dprintk(LSR_PWR, "Detaching power domain: %s\n",
				pd_info->name);
			dev_pm_domain_detach(pd_info->pd_dev, true);
			pd_info->pd_dev = NULL;
		}
	}
}

static int __init_power_domains(struct lsr_device *device)
{
	int rc = 0;
	struct power_domain_info *pd_info = NULL;

	iris_hfi_for_each_power_domain(device, pd_info) {
		pd_info->pd_dev = dev_pm_domain_attach_by_name(
				&device->res->pdev->dev, pd_info->name);

		if (IS_ERR_OR_NULL(pd_info->pd_dev)) {
			rc = PTR_ERR(pd_info->pd_dev) ?: -EBADHANDLE;
			dprintk(LSR_ERR, "Failed to attach power domain: %s, rc=%d\n",
					pd_info->name, rc);
			pd_info->pd_dev = NULL;
			goto err_pd_attach;
		}

		dprintk(LSR_PWR, "Attached to power domain: %s\n", pd_info->name);
	}

	return 0;

err_pd_attach:
	__deinit_power_domains(device);
	return rc;
}

static int __acquire_power_domain(struct power_domain_info *pd_info,
				struct lsr_device *device)
{
	int rc = 0;

#if (KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE)
	if (pd_info->has_hw_power_collapse) {
		rc = dev_pm_genpd_set_hwmode(pd_info->pd_dev, false);
		if (rc)
			dprintk(LSR_WARN, "Failed to acquire power domain control: %s\n",
				pd_info->name);
		else
			dprintk(LSR_PWR, "Acquired power domain control from HW: %s\n",
				pd_info->name);
	}
#else
	dprintk(LSR_ERR, "%s: not supported", __func__);
	rc = -EOPNOTSUPP;
#endif

	return rc;
}

static int __hand_off_power_domain(struct power_domain_info *pd_info)
{
	int rc = 0;

#if (KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE)
	if (pd_info->has_hw_power_collapse) {
		rc = dev_pm_genpd_set_hwmode(pd_info->pd_dev, true);
		if (rc)
			dprintk(LSR_WARN, "Failed to hand off power domain control: %s\n",
				pd_info->name);
		else
			dprintk(LSR_PWR, "Hand off power domain control to HW: %s\n",
				pd_info->name);
	}
#else
	dprintk(LSR_ERR, "%s: not supported", __func__);
	rc = -EOPNOTSUPP;
#endif

	return rc;
}

static int __hand_off_power_domains(struct lsr_device *device)
{
	struct power_domain_info *pd_info;
	int rc = 0, c = 0;

	iris_hfi_for_each_power_domain(device, pd_info) {
		rc = __hand_off_power_domain(pd_info);
		if (rc)
			goto err_pd_handoff_failed;
		c++;
	}

	return rc;

err_pd_handoff_failed:
	iris_hfi_for_each_power_domain_reverse_continue(device, pd_info, c)
		__acquire_power_domain(pd_info, device);

	return rc;
}

static int __take_back_power_domains(struct lsr_device *device)
{
	struct power_domain_info *pd_info;
	int rc = 0;

	iris_hfi_for_each_power_domain(device, pd_info) {
		rc = __acquire_power_domain(pd_info, device);
		if (rc)
			return rc;
	}

	return rc;
}

static int __enable_power_domain(struct lsr_device *device, const char *name)
{
	int rc = 0;
	struct power_domain_info *pd_info;

	iris_hfi_for_each_power_domain(device, pd_info) {
		if (strcmp(pd_info->name, name))
			continue;

		rc = pm_runtime_get_sync(pd_info->pd_dev);
		if (rc < 0) {
			dprintk(LSR_ERR, "Failed to enable power domain %s: %d\n",
					pd_info->name, rc);
			pm_runtime_put_noidle(pd_info->pd_dev);
			return rc;
		}

		dprintk(LSR_PWR, "Enabled power domain %s\n", pd_info->name);
		return 0;
	}

	dprintk(LSR_ERR, "Power domain %s not found\n", name);
	return -EINVAL;
}

static int __disable_power_domain(struct lsr_device *device, const char *name)
{
	struct power_domain_info *pd_info;
	int rc = 0;

	iris_hfi_for_each_power_domain_reverse(device, pd_info) {
		if (strcmp(pd_info->name, name))
			continue;

		rc = pm_runtime_put_sync(pd_info->pd_dev);
		if (rc < 0) {
			dprintk(LSR_WARN, "Failed to disable power domain %s: %d\n",
					pd_info->name, rc);
		}

		dprintk(LSR_PWR, "Disabled power domain %s\n", name);
		return 0;
	}

	dprintk(LSR_ERR, "Power domain %s not found\n", name);
	return -EINVAL;
}

static int __init_power_resources(struct lsr_device *device)
{
	int rc = 0;

	dprintk(LSR_PWR, "Initializing power resources, framework_type:%d\n",
		device->res->framework_type);

	if (device->res->framework_type) {
		/* Generic Power Domain framework */
		rc = __init_power_domains(device);
		if (rc) {
			dprintk(LSR_ERR, "Failed to initialize power domains\n");
			return -ENODEV;
		}
	} else {
		/* Regulator framework */
		rc = __init_regulators(device);
		if (rc) {
			dprintk(LSR_ERR, "Failed to get all regulators\n");
			return -ENODEV;
		}
	}

	return rc;
}

static void __deinit_power_resources(struct lsr_device *device)
{
	if (device->res->framework_type)
		__deinit_power_domains(device);
	else
		__deinit_regulators(device);
}

static int __init_resources(struct lsr_device *device,
				struct msm_lsr_platform_resources *res)
{
	int i, rc = 0;

	rc = __init_power_resources(device);
	if (rc) {
		dprintk(LSR_ERR, "Failed to init power resources\n");
		return -ENODEV;
	}

	rc = msm_lsr_init_clocks(device);
	if (rc) {
		dprintk(LSR_ERR, "Failed to init clocks\n");
		rc = -ENODEV;
		goto err_init_clocks;
	}

	for (i = 0; i < device->res->reset_set.count; i++) {
		rc = __init_reset_clk(res, i);
		if (rc) {
			dprintk(LSR_ERR, "Failed to init reset clocks\n");
			rc = -ENODEV;
			goto err_init_reset_clk;
		}
	}

	rc = __init_bus(device);
	if (rc) {
		dprintk(LSR_ERR, "Failed to init bus: %d\n", rc);
		goto err_init_bus;
	}

	rc = __init_subcaches(device);
	if (rc)
		dprintk(LSR_WARN, "Failed to init subcaches: %d\n", rc);

	rc = __init_synx(device);
	if (rc)
		dprintk(LSR_ERR, "Failed to init synx %d\n", rc);

	return rc;

err_init_reset_clk:
err_init_bus:
	msm_lsr_deinit_clocks(device);
err_init_clocks:
	__deinit_power_resources(device);
	return rc;
}

static void __deinit_resources(struct lsr_device *device)
{
	__deinit_subcaches(device);
	__deinit_bus(device);
	msm_lsr_deinit_clocks(device);
	__deinit_power_resources(device);
}

static int __disable_regulator_impl(struct regulator_info *rinfo,
				struct lsr_device *device)
{
	int rc = 0;

	dprintk(LSR_PWR, "Disabling regulator %s\n", rinfo->name);

	/*
	 * This call is needed. Driver needs to acquire the control back
	 * from HW in order to disable the regualtor. Else the behavior
	 * is unknown.
	 */

	rc = __acquire_regulator(rinfo, device);
	if (rc) {
		/*
		 * This is somewhat fatal, but nothing we can do
		 * about it. We can't disable the regulator w/o
		 * getting it back under s/w control
		 */
		dprintk(LSR_WARN,
			"Failed to acquire control on %s\n",
			rinfo->name);

		goto disable_regulator_failed;
	}

	rc = regulator_disable(rinfo->regulator);
	if (rc) {
		dprintk(LSR_WARN,
			"Failed to disable %s: %d\n",
			rinfo->name, rc);
		goto disable_regulator_failed;
	}

	return 0;
disable_regulator_failed:
	return rc;
}

static int __disable_hw_power_collapse(struct lsr_device *device)
{
	int rc = 0;

	if (!msm_lsr_fw_low_power_mode) {
		dprintk(LSR_PWR, "Not enabling hardware power collapse\n");
		return 0;
	}

	if (device->res->framework_type)
		rc = __take_back_power_domains(device);
	else
		rc = __take_back_regulators(device);

	if (rc)
		dprintk(LSR_WARN,
			"%s : Failed to disable HW power collapse %d\n",
				__func__, rc);
	return rc;
}

static int __enable_regulator(struct lsr_device *device,
		const char *name)
{
	int rc = 0;
	struct regulator_info *rinfo;

	iris_hfi_for_each_regulator(device, rinfo) {
		if (strcmp(rinfo->name, name))
			continue;
		qcom_clk_dump(NULL, rinfo->regulator, true);
		rc = regulator_enable(rinfo->regulator);
		if (rc) {
			dprintk(LSR_ERR, "Failed to enable %s: %d\n",
					rinfo->name, rc);
			udelay(500);
			qcom_clk_dump(NULL, rinfo->regulator, true);
			return rc;
		}
		qcom_clk_dump(NULL, rinfo->regulator, true);

		if (!regulator_is_enabled(rinfo->regulator)) {
			dprintk(LSR_ERR, "%s: regulator %s not enabled\n", __func__, rinfo->name);
			regulator_disable(rinfo->regulator);
			return -EINVAL;
		}

		dprintk(LSR_PWR, "Enabled regulator %s\n", rinfo->name);
		return 0;
	}

	dprintk(LSR_ERR, "regulator %s not found\n", name);
	return -EINVAL;
}

static int __disable_regulator(struct lsr_device *device,
		const char *name)
{
	struct regulator_info *rinfo;

	iris_hfi_for_each_regulator_reverse(device, rinfo) {

		if (strcmp(rinfo->name, name))
			continue;

		__disable_regulator_impl(rinfo, device);
		dprintk(LSR_PWR, "%s Disabled regulator %s\n", __func__, name);
		return 0;
	}

	dprintk(LSR_ERR, "%s regulator %s not found\n", __func__, name);
	return -EINVAL;
}

static int __enable_subcaches(struct lsr_device *device)
{
	int rc = 0;
	u32 c = 0;
	struct subcache_info *sinfo;

	if (msm_lsr_syscache_disable || !is_sys_cache_present(device))
		return 0;

	/* Activate subcaches */
	iris_hfi_for_each_subcache(device, sinfo) {
		rc = llcc_slice_activate(sinfo->subcache);
		if (rc) {
			dprintk(LSR_WARN, "Failed to activate %s: %d\n",
				sinfo->name, rc);
			goto err_activate_fail;
		}
		sinfo->isactive = true;
		dprintk(LSR_CORE, "Activated subcache %s\n", sinfo->name);
		c++;
	}

	dprintk(LSR_CORE, "Activated %d Subcaches to CVP\n", c);

	return 0;

err_activate_fail:
	__disable_subcaches(device);
	return 0;
}

static int __disable_subcaches(struct lsr_device *device)
{
	struct subcache_info *sinfo;
	int rc = 0;

	if (msm_lsr_syscache_disable || !is_sys_cache_present(device))
		return 0;

	/* De-activate subcaches */
	iris_hfi_for_each_subcache_reverse(device, sinfo) {
		if (sinfo->isactive) {
			dprintk(LSR_CORE, "De-activate subcache %s\n",
				sinfo->name);
			rc = llcc_slice_deactivate(sinfo->subcache);
			if (rc) {
				dprintk(LSR_WARN,
					"Failed to de-activate %s: %d\n",
					sinfo->name, rc);
			}
			sinfo->isactive = false;
		}
	}

	return 0;
}

static void interrupt_init_iris2(struct lsr_device *device)
{
	u32 mask_val = 0;

	/* All interrupts should be disabled initially 0x1F6 : Reset value */
	mask_val = __read_register(device, LSR_WRAPPER_INTR_MASK);

	/* Write 0 to unmask CPU and WD interrupts */
	mask_val &= ~(CVP_FATAL_INTR_BMSK | LSR_WRAPPER_INTR_MASK_A2HCPU_BMSK);
	__write_register(device, LSR_WRAPPER_INTR_MASK, mask_val);
	dprintk(LSR_REG, "Init irq: reg: %x, mask value %x\n",
		LSR_WRAPPER_INTR_MASK, mask_val);

	mask_val = 0;
	//mask_val = __read_register(device, CVP_SS_IRQ_MASK);
	//mask_val &= ~(CVP_SS_INTR_BMASK);
	//__write_register(device, CVP_SS_IRQ_MASK, mask_val);
	dprintk(LSR_REG, "Init irq_wd: reg: %x, mask value %x\n",
			CVP_SS_IRQ_MASK, mask_val);
}

static int __vote_cfg_bus(struct lsr_device *device)
{
	int rc = 0;
	struct bus_info *bus = NULL;
	struct msm_lsr_core *core;
	u32 bus_count;

	core = lsr_driver->lsr_core;
	if (!core) {
		dprintk(LSR_ERR, "Invalid LSR core");
		return -EINVAL;
	}

	for (bus_count = 0; bus_count < core->resources.bus_set.count; bus_count++) {
		if (!strcmp(core->resources.bus_set.bus_tbl[bus_count].name, "lsr-cfg")) {
			bus = &core->resources.bus_set.bus_tbl[bus_count];
			rc = lsr_set_bw(bus, bus->range[1]);
		}
	}

	return 0;
}

static int __lsr_power_on(struct lsr_device *device)
{
	int rc = 0;
	struct msm_lsr_core *core;

	if (device->power_enabled)
		return 0;

	dprintk(LSR_PWR, "LSR Power on\n");
	core = lsr_driver->lsr_core;
	/* Vote for all hardware resources */
	mutex_lock(&core->clk_lock);
	rc = __vote_cfg_bus(device);
	if (rc) {
		dprintk(LSR_ERR, "Failed to vote LSR cfg bus, err: %d\n", rc);
		mutex_unlock(&core->clk_lock);
		goto fail_vote_buses;
	}
	mutex_unlock(&core->clk_lock);

	rc = call_iris_op(device, power_on_controller, device);
	if (rc)
		goto fail_enable_controller;

	rc = call_iris_op(device, power_on_core, device);
	if (rc)
		goto fail_enable_core;

	mutex_lock(&core->clk_lock);
	rc = msm_lsr_scale_clocks(device);
	if (rc) {
		dprintk(LSR_WARN, "Failed to scale clocks, perf may regress\n");
		rc = 0;
	} else {
		dprintk(LSR_PWR, "Done with scaling\n");
	}
	mutex_unlock(&core->clk_lock);

	/*Do not access registers before this point!*/
	device->power_enabled = true;
#ifdef CONFIG_LSR_SERAPH
	/* Thomas input to debug CPU NoC hang */
	__write_register(device, CVP_NOC_SBM_FAULTINEN0_LOW, 0x1);
	__write_register(device, CVP_NOC_ERR_MAINCTL_LOW_OFFS, 0x3);

#endif
	/*
	 * Re-program all of the registers that get reset as a result of
	 * regulator_disable() and _enable()
	 * calling below function requires CORE powered on
	 */
	rc = call_iris_op(device, set_registers, device);
	if (rc)
		goto fail_enable_core;

	dprintk(LSR_CORE, "Done with register set\n");

	rc = call_iris_op(device, check_core_power_on, device);
	if (rc) {
		dprintk(LSR_ERR, "CORE power on failed %d\n", rc);
		rc = -EINVAL;
		goto fail_enable_core;
	}

	rc = call_iris_op(device, check_ctl_power_on, device);
	if (rc) {
		dprintk(LSR_ERR, "CTRL power on failed %d\n", rc);
		rc = -EINVAL;
		goto fail_enable_core;
	}

	call_iris_op(device, interrupt_init, device);
	dprintk(LSR_CORE, "Done with interrupt enabling\n");
	device->intr_status = 0;
	enable_irq(device->lsr_hal_data->irq);
	__write_register(device,
		LSR_WRAPPER_DEBUG_BRIDGE_LPI_CONTROL, 0x7);
	pr_info_ratelimited(LSR_PID_TAG "lsr powered on\n",
		current->pid, current->tgid, "pwr");
	return 0;

fail_enable_core:
	call_iris_op(device, power_off_controller, device);
fail_enable_controller:
	__unvote_buses(device);
fail_vote_buses:
	device->power_enabled = false;
	return rc;
}

static inline int __suspend(struct lsr_device *device)
{
	int rc = 0;

	if (!device) {
		dprintk(LSR_ERR, "Invalid params: %pK\n", device);
		return -EINVAL;
	} else if (!device->power_enabled) {
		dprintk(LSR_PWR, "Power already disabled\n");
		return 0;
	}

	dprintk(LSR_PWR, "Entering suspend\n");

	rc = __tzbsp_set_lsr_state(TZ_SUBSYS_STATE_SUSPEND);
	if (rc) {
		dprintk(LSR_WARN, "Failed to suspend lsr core %d\n", rc);
		goto err_tzbsp_suspend;
	}

	__disable_subcaches(device);

	power_off_iris2(device);

	if (device->res->pm_qos.latency_us && device->res->pm_qos.pm_qos_hdls)
		lsr_pm_qos_update(device, PM_QOS_RESUME_LATENCY_DEFAULT_VALUE);

	return rc;

err_tzbsp_suspend:
	return rc;
}

static void power_off_iris2(struct lsr_device *device)
{
	if (!device->power_enabled || !device->res->sw_power_collapsible)
		return;

	if (!(device->intr_status & LSR_WRAPPER_INTR_STATUS_A2HWD_BMSK))
		disable_irq_nosync(device->lsr_hal_data->irq);
	device->intr_status = 0;

	call_iris_op(device, power_off_core, device);

	call_iris_op(device, power_off_controller, device);

	if (__unvote_buses(device))
		dprintk(LSR_WARN, "Failed to unvote for buses\n");

	/*Do not access registers after this point!*/
	device->power_enabled = false;
}

int __resume(struct lsr_device *device)
{
	int rc = 0;
	struct msm_lsr_core *core;

	if (!device) {
		dprintk(LSR_ERR, "Invalid params: %pK\n", device);
		return -EINVAL;
	} else if (device->power_enabled) {
		dprintk(LSR_PWR, "resume avoid as power enabled already\n");
		goto exit;
	} else if (!__core_in_valid_state(device)) {
		dprintk(LSR_PWR, "lsr_device in deinit state.");
		return -EINVAL;
	}

	core = lsr_driver->lsr_core;
	dprintk(LSR_PWR, "Resuming from power collapse\n");
	rc = __lsr_power_on(device);
	if (rc) {
		dprintk(LSR_ERR, "Failed to power on lsr\n");
		goto err_iris_power_on;
	}

	__setup_ucregion_memory_map(device);

	/* Reboot the firmware */
	rc = __tzbsp_set_lsr_state(TZ_SUBSYS_STATE_RESUME);
	if (rc) {
		dprintk(LSR_ERR, "Failed to resume lsr core %d\n", rc);
		goto err_set_lsr_state;
	}

	/* Wait for boot completion */
	rc = __boot_firmware(device);
	if (rc) {
		dprintk(LSR_ERR, "Failed to reset lsr core\n");
		goto err_reset_core;
	}

	/*
	 * Work around for H/W bug, need to reprogram these registers once
	 * firmware is out reset
	 */
	__set_threshold_registers(device);

	if (device->res->pm_qos.latency_us && device->res->pm_qos.pm_qos_hdls)
		lsr_pm_qos_update(device, device->res->pm_qos.latency_us);

	__enable_subcaches(device);

	dprintk(LSR_PWR, "Resumed from power collapse\n");
exit:

	return rc;
err_reset_core:
	__tzbsp_set_lsr_state(TZ_SUBSYS_STATE_SUSPEND);
err_set_lsr_state:
	power_off_iris2(device);
err_iris_power_on:
	dprintk(LSR_ERR, "Failed to resume from power collapse\n");
	return rc;
}

static int __power_on_init(struct lsr_device *device)
{
	int rc = 0;

	/* Initialize resources */
	rc = __init_resources(device, device->res);
	if (rc)
		dprintk(LSR_ERR, "Failed to init resources: %d\n", rc);

	return rc;
}

static int __load_fw(struct lsr_device *device)
{
	int rc = 0;

	if ((!device->res->use_non_secure_pil && !device->res->firmware_base)
		|| device->res->use_non_secure_pil) {
		rc = load_lsr_fw_impl(device);
		if (rc)
			goto fail_load_fw;
	}
	return rc;

fail_load_fw:
	power_off_iris2(device);
	return rc;
}

static void __unload_fw(struct lsr_device *device)
{
	if (!device->resources.fw.cookie)
		return;

	if (device->state != IRIS_STATE_DEINIT)
		flush_workqueue(device->iris_pm_workq);

	unload_lsr_fw_impl(device);
	__interface_queues_release(device);
	power_off_iris2(device);
	__deinit_resources(device);

	dprintk(LSR_WARN, "Firmware unloaded\n");
}

static int iris_hfi_get_fw_info(void *dev, struct lsr_fw_info *fw_info)
{
	struct lsr_device *device = dev;

	if (!device || !fw_info) {
		dprintk(LSR_ERR,
			"%s Invalid parameter: device = %pK fw_info = %pK\n",
			__func__, device, fw_info);
		return -EINVAL;
	}

	mutex_lock(&device->lock);

	fw_info->base_addr = device->lsr_hal_data->firmware_base;
	fw_info->register_base = device->res->register_base;
	fw_info->register_size = device->lsr_hal_data->register_size;
	fw_info->irq = device->lsr_hal_data->irq;

	mutex_unlock(&device->lock);
	return 0;
}

void __init_lsr_ops(struct lsr_device *device)
{
	device->hal_ops = &hal_ops;
}

static struct lsr_device *__add_device(struct msm_lsr_platform_resources *res)
{
	struct lsr_device *hdevice = NULL;
	int rc = 0;

	if (!res) {
		dprintk(LSR_ERR, "Invalid Parameters\n");
		return NULL;
	}

	hdevice = kzalloc(sizeof(*hdevice), GFP_KERNEL);
	if (!hdevice) {
		dprintk(LSR_ERR, "failed to allocate new device\n");
		goto exit;
	}


	rc = msm_lsr_init_reg_and_irq(hdevice, res);
	if (rc)
		goto err_cleanup;

	hdevice->res = res;

	__init_lsr_ops(hdevice);

	hdevice->iris_pm_workq = create_singlethread_workqueue(
			"pm_workerq_iris");
	if (!hdevice->iris_pm_workq) {
		dprintk(LSR_ERR, ": create pm workq failed\n");
		goto err_cleanup;
	}

	mutex_init(&hdevice->lock);
	hdevice->ref_count = 0;

	return hdevice;

err_cleanup:
	if (hdevice->iris_pm_workq)
		destroy_workqueue(hdevice->iris_pm_workq);
	kfree(hdevice);
exit:
	return NULL;
}

static struct lsr_device *__get_device(struct msm_lsr_platform_resources *res)
{
	if (!res) {
		dprintk(LSR_ERR, "Invalid params: %pK\n", res);
		return NULL;
	}

	return __add_device(res);
}

void lsr_iris_hfi_delete_device(void *device)
{
	struct msm_lsr_core *core;
	struct lsr_device *dev = NULL;

	if (!device)
		return;

	core = lsr_driver->lsr_core;
	if (core)
		dev = core->dev_ops->hfi_device_data;

	if (!dev)
		return;

	mutex_destroy(&dev->lock);
	destroy_workqueue(dev->iris_pm_workq);

	free_irq(dev->lsr_hal_data->irq, dev);

	iounmap(dev->lsr_hal_data->register_base);
	iounmap(dev->lsr_hal_data->gcc_reg_base);
	kfree(dev->lsr_hal_data);
	kfree(dev);
}

/***************************************************************************
 *
 * Start Arch specific implementation
 *
 ***************************************************************************/

#ifdef CONFIG_LSR_SERAPH

static int __check_ctl_power_on_v1(struct lsr_device *device)
{
	u32 reg;

	reg = __read_register(device, LSR_CC_MVS0C_GDSCR);
	if (!(reg & 0x80000000))
		return -1;

	reg = __read_register(device, LSR_CC_MVS0C_CBCR);
	if (reg & 0x80000000)
		return -2;

	return 0;
}

static int __check_core_power_on_v1(struct lsr_device *device)
{
	u32 reg;

	reg = __read_register(device, LSR_CC_MVS0_GDSCR);
	if (!(reg & 0x80000000))
		return -1;

	reg = __read_register(device, LSR_CC_MVS0_CBCR);
	if (reg & 0x80000000)
		return -2;

	return 0;
}

static int __power_on_controller_v1(struct lsr_device *device)
{
	int rc = 0;

	if (device->res->framework_type) {
		/* Using GenPD - enable controller power domain */
		rc = __enable_power_domain(device, "lsr_mvs0c_gdsc");
		if (rc) {
			dprintk(LSR_ERR, "Failed to enable controller PD: %d\n", rc);
			return rc;
		}
	} else {
		/* Using regulators */
		rc = __enable_regulator(device, "lsr");
		if (rc) {
			dprintk(LSR_ERR, "Failed to enable ctrler: %d\n", rc);
			return rc;
		}
	}

	rc = msm_lsr_prepare_enable_clk(device, "sleep_clk");
	if (rc) {
		dprintk(LSR_ERR, "Failed to enable sleep clk: %d\n", rc);
		goto fail_reset_sleep;
	}

	rc = msm_lsr_prepare_enable_clk(device, "core_axi_clock");
	if (rc) {
		dprintk(LSR_ERR, "Failed to enable axi0 clk: %d\n", rc);
		goto fail_enable_axi0;
	}

	rc = msm_lsr_prepare_enable_clk(device, "lsr_axi_clock");
	if (rc) {
		dprintk(LSR_ERR, "Failed to enable axi0c clk: %d\n", rc);
		goto fail_enable_axi0c;
	}

	rc = msm_lsr_prepare_enable_clk(device, "lsr_clk");
	if (rc) {
		dprintk(LSR_ERR, "Failed to enable lsr_clk: %d\n", rc);
		goto fail_enable_lsr;
	}

	rc = msm_lsr_prepare_enable_clk(device, "lsr_freerun_clk");
	if (rc) {
		dprintk(LSR_ERR, "Failed to enable lsr_freerun_clk: %d\n", rc);
		goto fail_enable_freerun;
	}

	dprintk(LSR_PWR, "LSR controller powered on\n");
	return 0;

fail_enable_freerun:
	msm_lsr_disable_unprepare_clk(device, "lsr");
fail_enable_lsr:
	msm_lsr_disable_unprepare_clk(device, "lsr_axi_clock");
fail_enable_axi0c:
	msm_lsr_disable_unprepare_clk(device, "core_axi_clock");
fail_enable_axi0:
	msm_lsr_disable_unprepare_clk(device, "sleep_clk");
fail_reset_sleep:
	if (device->res->framework_type)
		__disable_power_domain(device, "lsr_mvs0c_gdsc");
	else
		__disable_regulator(device, "lsr");

	return rc;
}

static int __power_on_core_v1(struct lsr_device *device)
{
	int rc = 0;

	rc = msm_lsr_prepare_enable_clk(device, "core_freerun_clk");
	if (rc) {
		dprintk(LSR_PWR, "Failed to enable core_freerun_clk: %d\n", rc);
		// TODO: check with clk team for merge of fix
		// This will fail always, calling once as a workaround
	}

	if (device->res->framework_type) {
		/* Using GenPD */
		rc = __enable_power_domain(device, "lsr_noc_gdsc");
		if (rc) {
			dprintk(LSR_ERR, "Failed to enable NOC PD: %d\n", rc);
			return rc;
		}

		rc = __enable_power_domain(device, "lsr_mvs0_gdsc");
		if (rc) {
			dprintk(LSR_ERR, "Failed to enable core PD: %d\n", rc);
			return rc;
		}
	} else {
		/* Using regulators */
		rc = __enable_regulator(device, "lsr-noc");
		if (rc) {
			dprintk(LSR_ERR, "Failed to enable noc: %d\n", rc);
			return rc;
		}

		rc = __enable_regulator(device, "lsr-core");
		if (rc) {
			dprintk(LSR_ERR, "Failed to enable core: %d\n", rc);
			return rc;
		}
	}

	rc = msm_lsr_prepare_enable_clk(device, "lsr_cc_mvs0_clk_src");
	if (rc) {
		dprintk(LSR_ERR, "Failed to enable lsr_cc_mvs0_clk_src:%d\n",
			rc);
		goto fail_enable_clk_src;
	}

	rc = msm_lsr_prepare_enable_clk(device, "core_clk");
	if (rc) {
		dprintk(LSR_ERR, "Failed to enable core_clk: %d\n", rc);
		goto fail_enable_core;
	}

	rc = msm_lsr_prepare_enable_clk(device, "core_freerun_clk");
	if (rc) {
		dprintk(LSR_ERR, "Failed to enable core_freerun_clk: %d\n", rc);
		goto fail_enable_freerun;
	}

	dprintk(LSR_PWR, "LSR core powered on\n");

	return 0;
fail_enable_freerun:
	msm_lsr_disable_unprepare_clk(device, "core_clk");
fail_enable_core:
	msm_lsr_disable_unprepare_clk(device, "lsr_cc_mvs0_clk_src");
fail_enable_clk_src:
	if (device->res->framework_type) {
		__disable_power_domain(device, "lsr_noc_gdsc");
		__disable_power_domain(device, "lsr_mvs0_gdsc");
	} else {
		__disable_regulator(device, "lsr-core");
		__disable_regulator(device, "lsr-noc");
	}
	return rc;
}

static int __power_off_core_v1(struct lsr_device *device)
{
	u32	config, value = 0, count = 0;
	u32 max_count = 10;
	u32 value0 = 0, value1 = 0;

	value = __read_register(device, LSR_CC_MVS0_GDSCR);
	if (!(value & 0x80000000)) {
		/*
		 * Core has been powered off by f/w.
		 * Check NOC reset registers to ensure
		 * NO outstanding NoC transactions
		 */
		value = __read_register(device, CVP_NOC_RESET_ACK);
		if (value) {
			dprintk(LSR_WARN, "Core off with NOC RESET ACK non-zero %x\n", value);
			call_iris_op(device, print_sbm_regs, device);
		}
		if (device->res->framework_type) {
			__disable_power_domain(device, "lsr_mvs0_gdsc");
			__disable_power_domain(device, "lsr_noc_gdsc");
		} else {
			__disable_regulator(device, "lsr-core");
			__disable_regulator(device, "lsr-noc");
		}
		msm_lsr_disable_unprepare_clk(device, "core_clk");
		return 0;
	} else if (!(value & 0x2) && msm_lsr_fw_low_power_mode) {
		/*
		 * HW_CONTROL PC disabled, then core is powered on for
		 * CVP NoC access
		 */
		if (device->res->framework_type) {
			__disable_power_domain(device, "lsr_mvs0_gdsc");
			__disable_power_domain(device, "lsr_noc_gdsc");
		} else {
			__disable_regulator(device, "lsr-core");
			__disable_regulator(device, "lsr-noc");
		}
		msm_lsr_disable_unprepare_clk(device, "core_clk");
		return 0;
	}

	dprintk(LSR_PWR, "Driver controls Core power off now\n");

	/* HPG 3.4.4 step 1 */
	/*
	 * check to make sure core clock branch enabled else
	 * we cannot read core idle register
	 */
	config = __read_register(device, LSR_WRAPPER_CORE_CLOCK_CONFIG);
	if (config) {
		dprintk(LSR_PWR,
		"core clock config not enabled, enable it to access core\n");
		__write_register(device, LSR_WRAPPER_CORE_CLOCK_CONFIG, 0);
	}

	do {
		value0 = __read_register(device, LSR0_SS_IDLE_STATUS);
		value1 = __read_register(device, LSR1_SS_IDLE_STATUS);
		if ((value0 & 0x40) && (value1 & 0x40))
			break;
		usleep_range(1000, 2000);
		count++;
	} while (count < max_count);

	if (count == max_count)
		dprintk(LSR_WARN, "Core fail to go idle %x %x\n", value0, value1);

	/* HPG 3.4.4 step 5 */
	/* Reset both sides of 2 ahb2ahb_bridges (TZ and non-TZ) */
	__write_register(device, CVP_AHB_BRIDGE_SYNC_RESET, 0x3);
	__write_register(device, LSR_WRAPPER_CORE_CLOCK_CONFIG, config);

	/* HPG 3.4.4 step 6-7 */
	__disable_hw_power_collapse(device);
	usleep_range(100, 200);
	if (device->res->framework_type) {
		__disable_power_domain(device, "lsr_mvs0_gdsc");
		__disable_power_domain(device, "lsr_noc_gdsc");
	} else {
		__disable_regulator(device, "lsr-core");
		__disable_regulator(device, "lsr-noc");
	}
	msm_lsr_disable_unprepare_clk(device, "core_clk");
	return 0;
}

static int __power_off_controller_v1(struct lsr_device *device)
{
	u32 lpi_status, count = 0, max_count = 1000;
	int rc;

	/* HPG 3.7 Step 4  */
	__write_register(device, LSR_CPU_CS_X2RPMh, 0x3);
	/* HPG 3.7 step 11 */
	__write_register(device, LSR_WRAPPER_DEBUG_BRIDGE_LPI_CONTROL, 0x0);

	/* HPG 3.7 step 12 */
	lpi_status = 0x1;
	count = 0;
	while (lpi_status && count < max_count) {
		lpi_status = __read_register(device, LSR_WRAPPER_DEBUG_BRIDGE_LPI_STATUS);
		usleep_range(50, 100);
		count++;
	}
	dprintk(LSR_PWR, "DBLP Release: lpi_status %d(count %d)\n", lpi_status, count);
	if (count == max_count)
		dprintk(LSR_WARN, "DBLP Release: lpi_status %x\n", lpi_status);

	/*
	 * Below sequence are missing from HPG Section 3.7.
	 * It disables EVA_CC clks in power on sequence
	 */
	rc = msm_lsr_disable_unprepare_clk(device, "core_freerun_clk");
	if (rc)
		dprintk(LSR_ERR, "Failed to disable core_freerun_clk: %d\n", rc);

	rc = msm_lsr_disable_unprepare_clk(device, "lsr_freerun_clk");
	if (rc)
		dprintk(LSR_ERR, "Failed to disable lsr_freerun_clk: %d\n", rc);

	rc = msm_lsr_disable_unprepare_clk(device, "lsr_clk");
	if (rc)
		dprintk(LSR_ERR, "Failed to disable lsr_clk: %d\n", rc);

	rc = msm_lsr_disable_unprepare_clk(device, "sleep_clk");
	if (rc)
		dprintk(LSR_ERR, "Failed to disable sleep clk: %d\n", rc);

	/* HPG 3.7 Step 13 and 14 */
	if (device->res->framework_type)
		__disable_power_domain(device, "lsr_mvs0c_gdsc");
	else
		__disable_regulator(device, "lsr");

	/* Step #28: Override ARCG control to allow AXI0 clock pass through */
	__write_register(device, LSR_AON_WRAPPER_CVP_NOC_ARCG_CONTROL, 0x1);

	/*
	 * Below sequence are missing from HPG Section 3.7.
	 * It disables GCC clks in power on sequence
	 */
	rc = msm_lsr_disable_unprepare_clk(device, "core_axi_clock");
	rc = msm_lsr_disable_unprepare_clk(device, "lsr_axi_clock");

	/* Section 3.8.1 */
	rc = call_iris_op(device, reset_control_assert_name, device, "gcc_lsr_cv_cpu");
	if (rc)
		dprintk(LSR_ERR, "%s: assert gcc_lsr_cv_cpu failed\n", __func__);

	rc = call_iris_op(device, reset_control_assert_name, device, "gcc_lsr_axi0");
	if (rc)
		dprintk(LSR_ERR, "%s: assert gcc_lsr_axi0 failed\n", __func__);

	rc = call_iris_op(device, reset_control_assert_name, device, "gcc_lsr_xo");
	if (rc)
		dprintk(LSR_ERR, "%s: assert gcc_lsr_xo failed\n", __func__);

	rc = call_iris_op(device, reset_control_assert_name, device, "lsr_core_freerun");
	if (rc)
		dprintk(LSR_ERR, "%s: assert lsr_core_freerun failed\n", __func__);

	rc = call_iris_op(device, reset_control_assert_name, device, "lsr_freerun");
	if (rc)
		dprintk(LSR_ERR, "%s: assert lsr_freerun failed\n", __func__);

	rc = call_iris_op(device, reset_control_assert_name, device, "lsr_xo");
	if (rc)
		dprintk(LSR_ERR, "%s: assert lsr_xo failed\n", __func__);

	usleep_range(1000, 1050);

	rc = call_iris_op(device, reset_control_deassert_name, device, "lsr_xo");
	if (rc)
		dprintk(LSR_ERR, "%s: de-assert lsr_xo failed\n", __func__);

	rc = call_iris_op(device, reset_control_deassert_name, device, "lsr_freerun");
	if (rc)
		dprintk(LSR_ERR, "%s: de-assert lsr_freerun failed\n", __func__);

		rc = call_iris_op(device, reset_control_deassert_name, device, "lsr_core_freerun");
	if (rc)
		dprintk(LSR_ERR, "%s: de-assert lsr_core_freerun failed\n", __func__);

	rc = call_iris_op(device, reset_control_deassert_name, device, "gcc_lsr_xo");
	if (rc)
		dprintk(LSR_ERR, "%s: de-assert gcc_lsr_xo failed\n", __func__);

	rc = call_iris_op(device, reset_control_deassert_name, device, "gcc_lsr_axi0");
	if (rc)
		dprintk(LSR_ERR, "%s: de-assert gcc_lsr_axi0 failed\n", __func__);

	rc = call_iris_op(device, reset_control_deassert_name, device, "gcc_lsr_cv_cpu");
	if (rc)
		dprintk(LSR_ERR, "%s: de-assert gcc_lsr_cv_cpu failed\n", __func__);

	rc = msm_lsr_disable_unprepare_clk(device, "lsr_cc_mvs0_clk_src");
	if (rc)
		dprintk(LSR_ERR, "Failed to disable lsr_cc_mvs0_clk_src: %d\n", rc);

	return 0;
}

static void __print_sidebandmanager_regs_v1(struct lsr_device *device)
{
	u32 sbm_ln0_low, axi_cbcr, val;
	u32 main_sbm_ln0_low = 0xdeadbeef, main_sbm_ln0_high = 0xdeadbeef;
	u32 main_sbm_ln1_high = 0xdeadbeef, cpu_cs_x2rpmh;

	sbm_ln0_low = __read_register(device, CVP_NOC_SBM_SENSELN0_LOW);
	cpu_cs_x2rpmh = __read_register(device, LSR_CPU_CS_X2RPMh);


	__write_register(device, LSR_CPU_CS_X2RPMh,
			(cpu_cs_x2rpmh | LSR_CPU_CS_X2RPMh_SWOVERRIDE_BMSK));
	usleep_range(500, 1000);

	val = __read_register(device, LSR_CPU_CS_X2RPMh);
	dprintk(LSR_REG, "LSR_CPU_CS_X2RPMh %#x\n", val);
	val = __read_register(device, LSR_CPU_CS_X2RPMh_STATUS);
	dprintk(LSR_REG, "LSR_CPU_CS_X2RPMh_STATUS %#x\n", val);

	cpu_cs_x2rpmh = __read_register(device, LSR_CPU_CS_X2RPMh);
	if (!(cpu_cs_x2rpmh & LSR_CPU_CS_X2RPMh_SWOVERRIDE_BMSK)) {
		dprintk(LSR_WARN,
			"failed set LSR_CPU_CS_X2RPMH mask %x\n",
			cpu_cs_x2rpmh);
		goto exit;
	}

	axi_cbcr = __read_gcc_register(device, GCC_LSR_AXI0_CBCR);
	if (axi_cbcr & 0x80000000) {
		dprintk(LSR_WARN, "failed to turn on AXI clock %x\n",
			axi_cbcr);
		goto exit;
	}

	main_sbm_ln0_low = __read_register(device, CVP_NOC_MAIN_SIDEBANDMANAGER_SENSELN0_LOW);
	main_sbm_ln0_high = __read_register(device, CVP_NOC_MAIN_SIDEBANDMANAGER_SENSELN0_HIGH);
	main_sbm_ln1_high = __read_register(device, CVP_NOC_MAIN_SIDEBANDMANAGER_SENSELN1_HIGH);

exit:
	cpu_cs_x2rpmh = cpu_cs_x2rpmh & (~LSR_CPU_CS_X2RPMh_SWOVERRIDE_BMSK);
	__write_register(device, LSR_CPU_CS_X2RPMh, cpu_cs_x2rpmh);
	dprintk(LSR_WARN, "Sidebandmanager regs %x %x %x %x %x\n", sbm_ln0_low, main_sbm_ln0_low,
		main_sbm_ln0_high, main_sbm_ln1_high, cpu_cs_x2rpmh);
}

static int __enable_hw_power_collapse_v1(struct lsr_device *device)
{
	int rc = 0, loop = 10;
	u32 reg_gdsc;

	if (!msm_lsr_fw_low_power_mode) {
		dprintk(LSR_PWR, "Not enabling hardware power collapse\n");
		return 0;
	}

	if (device->res->framework_type)
		rc = __hand_off_power_domains(device);
	else
		rc = __hand_off_regulators(device);

	if (rc) {
		dprintk(LSR_WARN,
			"%s : Failed to enable HW power collapse %d\n",
				__func__, rc);
		return rc;
	}

	while (loop) {
		reg_gdsc = __read_register(device, LSR_CC_MVS0_GDSCR);
		if (reg_gdsc & 0x80000000) {
			usleep_range(100, 200);
			loop--;
		} else {
			break;
		}
	}

	if (!loop) {
		dprintk(LSR_ERR, "fail to power off CORE during resume\n");
		return -EINVAL;
	}

	return rc;
}
#endif

static void lsr_init_hfi_callbacks(struct lsr_hfi_ops *ops_tbl)
{
	ops_tbl->core_init = iris_hfi_core_init;
	ops_tbl->core_release = iris_hfi_core_release;
	ops_tbl->scale_clocks = iris_hfi_scale_clocks;
	ops_tbl->vote_bus = iris_hfi_vote_buses;
	ops_tbl->get_fw_info = iris_hfi_get_fw_info;
	ops_tbl->suspend = iris_hfi_suspend;
	ops_tbl->resume = iris_hfi_resume;
	ops_tbl->pm_qos_update = iris_pm_qos_update;
	ops_tbl->debug_hook = lsr_debug_hook;
}


int lsr_iris_hfi_initialize(struct lsr_hfi_ops *ops_tbl,
	struct msm_lsr_platform_resources *res)
{
	int rc = 0;

	if (!ops_tbl || !res) {
		dprintk(LSR_ERR, "Invalid params: %pK %pK\n",
			ops_tbl, res);
		rc = -EINVAL;
		goto err_iris_hfi_init;
	}

	ops_tbl->hfi_device_data = __get_device(res);

	if (IS_ERR_OR_NULL(ops_tbl->hfi_device_data)) {
		rc = PTR_ERR(ops_tbl->hfi_device_data) ?: -EINVAL;
		goto err_iris_hfi_init;
	}

	lsr_init_hfi_callbacks(ops_tbl);

err_iris_hfi_init:
	return rc;
}
