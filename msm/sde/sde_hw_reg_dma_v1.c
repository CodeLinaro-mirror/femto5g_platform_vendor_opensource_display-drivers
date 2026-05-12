// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[reg-dma:%s:%d] " fmt, __func__, __LINE__
#include <linux/iopoll.h>
#include "sde_hw_mdss.h"
#include "sde_hw_ctl.h"
#include "sde_hw_reg_dma_v1.h"
#include "sde_hw_vatran.h"
#include "msm_drv.h"
#include "msm_mmu.h"
#include "sde_dbg.h"
#include "sde_vbif.h"
#include "sde_rm.h"


#define GUARD_BYTES (BIT(8) - 1)
#define ALIGNED_OFFSET (U32_MAX & ~(GUARD_BYTES))
#define ADDR_ALIGN BIT(8)
#define MAX_RELATIVE_OFF (BIT(22) - 1)
#define ABSOLUTE_RANGE BIT(27)

#define DECODE_SEL_OP (BIT(HW_BLK_SELECT))
#define REG_WRITE_OP ((BIT(REG_SINGLE_WRITE)) | (BIT(REG_BLK_WRITE_SINGLE)) | \
	(BIT(REG_BLK_WRITE_INC)) | (BIT(REG_BLK_WRITE_MULTIPLE)) | \
	(BIT(REG_SINGLE_MODIFY)) | (BIT(REG_BLK_LUT_WRITE)))

#define REG_DMA_OPS (DECODE_SEL_OP | REG_WRITE_OP)
#define IS_OP_ALLOWED(op, buf_op) (BIT(op) & buf_op)

#define SET_UP_REG_DMA_REG(hw, reg_dma, i) \
	do { \
		if ((reg_dma)->caps->reg_dma_blks[(i)].valid == false) \
			break; \
		(hw).base_off = (reg_dma)->addr; \
		(hw).blk_off = (reg_dma)->caps->reg_dma_blks[(i)].base; \
		(hw).hw_rev = (reg_dma)->caps->version; \
		(hw).log_mask = SDE_DBG_MASK_REGDMA; \
} while (0)

#define SET_UP_REG_DMA_VQ_REG(hw, reg_dma, i, vq) \
				do { \
					if ((reg_dma)->caps->reg_dma_vq_blks[(vq)][(i)].valid == false) {\
						SDE_ERROR("Invalid VQ buffer  vq %d type %d?\n", vq, i); \
						break; \
					} \
					(hw).base_off = (reg_dma)->addr; \
					(hw).blk_off = (reg_dma)->caps->reg_dma_vq_blks[(vq)][(i)].base; \
					(hw).hw_rev = (reg_dma)->caps->version; \
					(hw).log_mask = SDE_DBG_MASK_REGDMA; \
			} while (0)

#define SIZE_DWORD(x) ((x) / (sizeof(u32)))
#define NOT_WORD_ALIGNED(x) ((x) & 0x3)


#define GRP_VIG_HW_BLK_SELECT (VIG0 | VIG1 | VIG2 | VIG3 | VIG4 | VIG5 | VIG6 | VIG7)
#define GRP_DMA_HW_BLK_SELECT (DMA0 | DMA1 | DMA2 | DMA3 | DMA4 | DMA5)
#define GRP_DSPP_HW_BLK_SELECT (DSPP0 | DSPP1 | DSPP2 | DSPP3)
#define GRP_LTM_HW_BLK_SELECT (LTM0 | LTM1 | LTM2 | LTM3)
#define GRP_MDSS_HW_BLK_SELECT (MDSS)
#define BUFFER_SPACE_LEFT(cfg) ((cfg)->dma_buf->buffer_size - \
			(cfg)->dma_buf->index)

#define REL_ADDR_OPCODE (BIT(27))
#define NO_OP_OPCODE (0)
#define SINGLE_REG_WRITE_OPCODE (BIT(28))
#define SINGLE_REG_MODIFY_OPCODE (BIT(29))
#define HW_INDEX_REG_WRITE_OPCODE (BIT(28) | BIT(29))
#define AUTO_INC_REG_WRITE_OPCODE (BIT(30))
#define BLK_REG_WRITE_OPCODE (BIT(30) | BIT(28))
#define LUTBUS_WRITE_OPCODE (BIT(30) | BIT(29))
#define OPCODE_MASK		(BIT(30) | BIT(29) | BIT(28))
#define ADDR_MASK		MAX_RELATIVE_OFF

#define IMMEDIATE_0_DONE	(BIT(0))
#define IMMEDIATE_1_DONE	(BIT(1))
#define IMMEDIATE_2_DONE	(BIT(2))
#define TRIGGER_0_DONE		(BIT(3))
#define TRIGGER_1_DONE		(BIT(4))
#define TRIGGER_2_DONE		(BIT(5))
#define ACCESS_FAIL			(BIT(6))

#define WRAP_MIN_SIZE 2
#define WRAP_MAX_SIZE (BIT(4) - 1)
#define MAX_DWORDS_SZ (BIT(14) - 1)
#define REG_DMA_HEADERS_BUFFER_SZ (sizeof(u32) * 128)

#define LUTBUS_TABLE_SEL_MASK 0x10000
#define LUTBUS_BLOCK_SEL_MASK 0xffff
#define LUTBUS_TRANS_SZ_MASK 0xff0000
#define LUTBUS_LUT_SIZE_MASK 0x3fff

#define PMU_CLK_CTRL  0x1F0
/*
 * Single or double buffer model for the payload buffer.
 * In theory, next commit has to wait for previous commit done.
 * So single buffer should be enough.
 */
#define NUM_BUFFERS		1

static uint32_t reg_dma_register_count;
static uint32_t reg_dma_decode_sel;
static uint32_t reg_dma_opmode_offset;
static uint32_t reg_dma_ctl0_queue0_cmd0_offset;
static uint32_t reg_dma_ctl0_queue1_cmd0_offset;
static uint32_t reg_dma_intr_0_enable_offset[DPU_MAX][CTL_MAX][DMA_CTL_QUEUE_MAX];
static uint32_t reg_dma_intr_0_status_offset[DPU_MAX][CTL_MAX][DMA_CTL_QUEUE_MAX];
static uint32_t reg_dma_intr_0_clear_offset[DPU_MAX][CTL_MAX][DMA_CTL_QUEUE_MAX];
static uint32_t reg_dma_intr_4_status_offset;
static uint32_t reg_dma_intr_4_clear_offset;
static uint32_t reg_dma_ctl_trigger_offset;
static uint32_t reg_dma_ctl0_reset_offset[DPU_MAX][CTL_MAX][DMA_CTL_QUEUE_MAX];
static uint32_t reg_dma_ctl0_busy_offset[DPU_MAX][CTL_MAX][DMA_CTL_QUEUE_MAX];
static uint32_t reg_dma_error_clear_mask;
static uint32_t reg_dma_ctl_queue_off[DPU_MAX][CTL_MAX];
static uint32_t reg_dma_ctl_queue1_off[DPU_MAX][CTL_MAX];
static uint32_t reg_dma_intr_5_enable_offset[DPU_MAX][CTL_MAX][DMA_CTL_QUEUE_MAX];
static uint32_t reg_dma_intr_5_status_offset[DPU_MAX][CTL_MAX][DMA_CTL_QUEUE_MAX];
static uint32_t reg_dma_intr_5_clear_offset[DPU_MAX][CTL_MAX][DMA_CTL_QUEUE_MAX];
static int reg_dma_ctl_to_vq_map[DPU_MAX][CTL_MAX][MAX_DISPLAYNODES];

typedef int (*reg_dma_internal_ops) (struct sde_reg_dma_setup_ops_cfg *cfg);
typedef int (*validate_queue_type)(struct sde_reg_dma_kickoff_cfg *cfg);

typedef void (*reg_dma_read_clear_status)(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw);
typedef void (*reg_dma_trigger)(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw);
typedef void (*reg_dma_cmd_queue)(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw, u32 cmd);

static struct sde_hw_reg_dma *reg_dma[DPU_MAX];
static u32 ops_mem_size[REG_DMA_SETUP_OPS_MAX] = {
	[REG_BLK_WRITE_SINGLE] = sizeof(u32) * 2,
	[REG_BLK_WRITE_INC] = sizeof(u32) * 2,
	[REG_BLK_WRITE_MULTIPLE] = sizeof(u32) * 2,
	[HW_BLK_SELECT] = sizeof(u32) * 2,
	[REG_SINGLE_WRITE] = sizeof(u32) * 2,
	[REG_SINGLE_MODIFY] = sizeof(u32) * 3,
	[REG_BLK_LUT_WRITE] = sizeof(u32) * 2,
};

static u32 queue_sel[DMA_CTL_QUEUE_MAX] = {
	[DMA_CTL_QUEUE0] = BIT(0),
	[DMA_CTL_QUEUE1] = BIT(4),
};

static u32 dspp_read_sel[DSPP_HIST_MAX] = {
	[DSPP0_HIST] = 0,
	[DSPP1_HIST] = 1,
	[DSPP2_HIST] = 2,
	[DSPP3_HIST] = 3,
};

static u64 v1_supported[REG_DMA_FEATURES_MAX]  = {
	[GAMUT] = GRP_VIG_HW_BLK_SELECT | GRP_DSPP_HW_BLK_SELECT,
	[VLUT] = GRP_DSPP_HW_BLK_SELECT,
	[GC] = GRP_DSPP_HW_BLK_SELECT,
	[IGC] = DSPP_IGC | GRP_DSPP_HW_BLK_SELECT,
	[PCC] = GRP_DSPP_HW_BLK_SELECT,
};

static u32 ctl_trigger_done_mask[DPU_MAX][CTL_MAX][MAX_DISPLAYNODES][DMA_CTL_QUEUE_MAX] = {
	{
		[CTL_0][0][0] = BIT(16),
		[CTL_0][1][0] = BIT(16),
		[CTL_0][2][0] = BIT(16),
		[CTL_0][3][0] = BIT(16),
		[CTL_0][0][1] = BIT(21),
		[CTL_0][1][1] = BIT(21),
		[CTL_0][2][1] = BIT(21),
		[CTL_0][3][1] = BIT(21),
		[CTL_1][0][0] = BIT(17),
		[CTL_1][1][0] = BIT(17),
		[CTL_1][2][0] = BIT(17),
		[CTL_1][3][0] = BIT(17),
		[CTL_1][0][1] = BIT(22),
		[CTL_1][1][1] = BIT(22),
		[CTL_1][2][1] = BIT(22),
		[CTL_1][3][1] = BIT(22),
		[CTL_2][0][0] = BIT(18),
		[CTL_2][1][0] = BIT(18),
		[CTL_2][2][0] = BIT(18),
		[CTL_2][3][0] = BIT(18),
		[CTL_2][0][1] = BIT(23),
		[CTL_2][1][1] = BIT(23),
		[CTL_2][2][1] = BIT(23),
		[CTL_2][3][1] = BIT(23),
		[CTL_3][0][0] = BIT(19),
		[CTL_3][1][0] = BIT(19),
		[CTL_3][2][0] = BIT(19),
		[CTL_3][3][0] = BIT(19),
		[CTL_3][0][1] = BIT(24),
		[CTL_3][1][1] = BIT(24),
		[CTL_3][2][1] = BIT(24),
		[CTL_3][3][1] = BIT(24),
		[CTL_4][0][0] = BIT(25),
		[CTL_4][1][0] = BIT(25),
		[CTL_4][2][0] = BIT(25),
		[CTL_4][3][0] = BIT(25),
		[CTL_4][0][1] = BIT(27),
		[CTL_4][1][1] = BIT(27),
		[CTL_4][2][1] = BIT(27),
		[CTL_4][3][1] = BIT(27),
		[CTL_5][0][0] = BIT(26),
		[CTL_5][1][0] = BIT(26),
		[CTL_5][2][0] = BIT(26),
		[CTL_5][3][0] = BIT(26),
		[CTL_5][0][1] = BIT(28),
		[CTL_5][1][1] = BIT(28),
		[CTL_5][2][1] = BIT(28),
		[CTL_5][3][1] = BIT(28),
	},
	{
		[CTL_0][0][0] = BIT(16),
		[CTL_0][1][0] = BIT(16),
		[CTL_0][2][0] = BIT(16),
		[CTL_0][3][0] = BIT(16),
		[CTL_0][0][1] = BIT(21),
		[CTL_0][1][1] = BIT(21),
		[CTL_0][2][1] = BIT(21),
		[CTL_0][3][1] = BIT(21),
		[CTL_1][0][0] = BIT(17),
		[CTL_1][1][0] = BIT(17),
		[CTL_1][2][0] = BIT(17),
		[CTL_1][3][0] = BIT(17),
		[CTL_1][0][1] = BIT(22),
		[CTL_1][1][1] = BIT(22),
		[CTL_1][2][1] = BIT(22),
		[CTL_1][3][1] = BIT(22),
		[CTL_2][0][0] = BIT(18),
		[CTL_2][1][0] = BIT(18),
		[CTL_2][2][0] = BIT(18),
		[CTL_2][3][0] = BIT(18),
		[CTL_2][0][1] = BIT(23),
		[CTL_2][1][1] = BIT(23),
		[CTL_2][2][1] = BIT(23),
		[CTL_2][3][1] = BIT(23),
		[CTL_3][0][0] = BIT(19),
		[CTL_3][1][0] = BIT(19),
		[CTL_3][2][0] = BIT(19),
		[CTL_3][3][0] = BIT(19),
		[CTL_3][0][1] = BIT(24),
		[CTL_3][1][1] = BIT(24),
		[CTL_3][2][1] = BIT(24),
		[CTL_3][3][1] = BIT(24),
		[CTL_4][0][0] = BIT(25),
		[CTL_4][1][0] = BIT(25),
		[CTL_4][2][0] = BIT(25),
		[CTL_4][3][0] = BIT(25),
		[CTL_4][0][1] = BIT(27),
		[CTL_4][1][1] = BIT(27),
		[CTL_4][2][1] = BIT(27),
		[CTL_4][3][1] = BIT(27),
		[CTL_5][0][0] = BIT(26),
		[CTL_5][1][0] = BIT(26),
		[CTL_5][2][0] = BIT(26),
		[CTL_5][3][0] = BIT(26),
		[CTL_5][0][1] = BIT(28),
		[CTL_5][1][1] = BIT(28),
		[CTL_5][2][1] = BIT(28),
		[CTL_5][3][1] = BIT(28),
	},
};

static validate_queue_type validate_queue_func;
static reg_dma_read_clear_status read_clear_reg_dma_status;
static reg_dma_trigger trigger_reg_dma;
static reg_dma_cmd_queue reg_dma_submit_payload;

static int validate_dma_cfg(struct sde_reg_dma_setup_ops_cfg *cfg);
static int validate_write_decode_sel(struct sde_reg_dma_setup_ops_cfg *cfg);
static int validate_write_reg(struct sde_reg_dma_setup_ops_cfg *cfg);
static int validate_blk_lut_write(struct sde_reg_dma_setup_ops_cfg *cfg);
static int validate_write_multi_lut_reg(struct sde_reg_dma_setup_ops_cfg *cfg);
static int validate_last_cmd(struct sde_reg_dma_setup_ops_cfg *cfg);
static int write_decode_sel(struct sde_reg_dma_setup_ops_cfg *cfg);
static int write_single_reg(struct sde_reg_dma_setup_ops_cfg *cfg);
static int write_multi_reg_index(struct sde_reg_dma_setup_ops_cfg *cfg);
static int write_multi_reg_inc(struct sde_reg_dma_setup_ops_cfg *cfg);
static int write_multi_lut_reg(struct sde_reg_dma_setup_ops_cfg *cfg);
static int write_single_modify(struct sde_reg_dma_setup_ops_cfg *cfg);
static int write_block_lut_reg(struct sde_reg_dma_setup_ops_cfg *cfg);
static int write_last_cmd(struct sde_reg_dma_setup_ops_cfg *cfg);
static int reset_reg_dma_buffer_v1(struct sde_reg_dma_buffer *lut_buf);
static int check_support_v1(enum sde_reg_dma_features feature,
		enum sde_reg_dma_blk blk, bool *is_supported);
static int setup_payload_v1(struct sde_reg_dma_setup_ops_cfg *cfg);
static int kick_off_v1(struct sde_reg_dma_kickoff_cfg *cfg, u32 dpu_idx);
static int reset_v1(struct sde_hw_ctl *ctl);
static int last_cmd_v1(struct sde_hw_ctl *ctl, enum sde_reg_dma_queue q,
		enum sde_reg_dma_last_cmd_mode mode);
static struct sde_reg_dma_buffer *alloc_reg_dma_buf_v1(u32 size, u32 dpu_idx);
static int dealloc_reg_dma_v1(struct sde_reg_dma_buffer *lut_buf, u32 dpu_idx);
static void dump_regs_v1(u32 dpu_idx);
static int last_cmd_sb_v2(struct sde_hw_ctl *ctl, enum sde_reg_dma_queue q,
		enum sde_reg_dma_last_cmd_mode mode);

static int validate_queue_type_v1_to_3(struct sde_reg_dma_kickoff_cfg *cfg);
static int validate_queue_type_v4(struct sde_reg_dma_kickoff_cfg *cfg);

static enum sde_reg_dma_queue reg_dma_select_queue_sb_v1_to_3(void);
static enum sde_reg_dma_queue reg_dma_select_queue_sb_v4(void);

static void reg_dma_read_clear_status_v1_to_v3(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw);
static void reg_dma_read_clear_status_v4(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw);

static void reg_dma_trigger_v1_to_v3(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw);
static void reg_dma_trigger_v4(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw);

static void reg_dma_submit_queue_v1_to_v3(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw, u32 cmd);
static void reg_dma_submit_queue_v4(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw, u32 cmd);

static struct sde_reg_dma_buffer *get_reg_dma_vq_buf_v4(struct sde_kms *sde_kms,
		enum sde_reg_dma_buffer_type type,
		enum sde_hw_blk_type hw_type, u32 idx, u32 dpu_idx, u32 display_idx);
static int reset_reg_dma_buffer_v4(struct sde_reg_dma_buffer *lut_buf);
static int validate_kick_off_v4(struct sde_reg_dma_kickoff_cfg *cfg, u32 dpu_idx);
static int write_kick_off_v4(struct sde_reg_dma_kickoff_cfg *cfg, u32 dpu_idx);
static int kick_off_v4_dummy(struct sde_reg_dma_kickoff_cfg *cfg, u32 dpu_idx);
static int kick_off_v4(struct sde_reg_dma_kickoff_cfg *cfg, u32 dpu_idx);
static int last_cmd_v4(struct sde_hw_ctl *ctl, enum sde_reg_dma_queue q,
		enum sde_reg_dma_last_cmd_mode mode);
static int last_cmd_sb_v4_dummy(struct sde_hw_ctl *ctl, enum sde_reg_dma_queue q,
		enum sde_reg_dma_last_cmd_mode mode);

int reset_v4(struct sde_hw_ctl *ctl);
int flush_v4(struct sde_hw_ctl *ctl, u32 dpu_idx);
void dump_hyp_config(struct sde_hw_ctl *ctl);
bool check_engine_status_v4(struct sde_hw_ctl *ctl);


/* Preserve large enough buffer for multiple SSPP/DSPP tables */
const static int vq_buf_size[REG_DMA_PAYLOAD_BUF_MAX] = {
	0x10000,	// MDSS DB 64KB
	0x2000,		// MDSS SB 8KB
	0x40000,	// LUTs/tables DB 256KB, SSPP 3DLUT, VIG QSEED, DSPP PCC, PGC, LTM VLUT
	0x20000,	// LUTs/tables SB 128KB, DSPP 3DLUT, SixZone, IGC
};

struct sde_reg_dma_buffer *
	vq_reg_dma_bufs[DPU_MAX][NUM_BUFFERS][REG_DMA_VQ_MAX][REG_DMA_PAYLOAD_BUF_MAX];

static reg_dma_internal_ops write_dma_op_params[REG_DMA_SETUP_OPS_MAX] = {
	[HW_BLK_SELECT] = write_decode_sel,
	[REG_SINGLE_WRITE] = write_single_reg,
	[REG_BLK_WRITE_SINGLE] = write_multi_reg_inc,
	[REG_BLK_WRITE_INC] = write_multi_reg_index,
	[REG_BLK_WRITE_MULTIPLE] = write_multi_lut_reg,
	[REG_SINGLE_MODIFY] = write_single_modify,
	[REG_BLK_LUT_WRITE] = write_block_lut_reg,
};

static reg_dma_internal_ops validate_dma_op_params[REG_DMA_SETUP_OPS_MAX] = {
	[HW_BLK_SELECT] = validate_write_decode_sel,
	[REG_SINGLE_WRITE] = validate_write_reg,
	[REG_BLK_WRITE_SINGLE] = validate_write_reg,
	[REG_BLK_WRITE_INC] = validate_write_reg,
	[REG_BLK_WRITE_MULTIPLE] = validate_write_multi_lut_reg,
	[REG_SINGLE_MODIFY] = validate_write_reg,
	[REG_BLK_LUT_WRITE] = validate_blk_lut_write,
};

static struct sde_reg_dma_buffer *last_cmd_buf_db[CTL_MAX][DPU_MAX];
static struct sde_reg_dma_buffer *last_cmd_buf_sb[CTL_MAX][DPU_MAX];

static void get_decode_sel(unsigned long blk, u32 *decode_sel)
{
	int i = 0;

	*decode_sel = 0;
	for_each_set_bit(i, &blk, REG_DMA_BLK_MAX) {
		switch (BIT(i)) {
		case VIG0:
			*decode_sel |= BIT(0);
			break;
		case VIG1:
			*decode_sel |= BIT(1);
			break;
		case VIG2:
			*decode_sel |= BIT(2);
			break;
		case VIG3:
			*decode_sel |= BIT(3);
			break;
		case DMA0:
			*decode_sel |= BIT(5);
			break;
		case DMA1:
			*decode_sel |= BIT(6);
			break;
		case DMA2:
			*decode_sel |= BIT(7);
			break;
		case DMA3:
			*decode_sel |= BIT(8);
			break;
		case DMA4:
			*decode_sel |= BIT(9);
			break;
		case DMA5:
			*decode_sel |= BIT(10);
			break;
		case VIG4:
			*decode_sel |= BIT(13);
			break;
		case VIG5:
			*decode_sel |= BIT(14);
			break;
		case VIG6:
			*decode_sel |= BIT(15);
			break;
		case VIG7:
			*decode_sel |= BIT(16);
			break;
		case DSPP0:
			*decode_sel |= BIT(17);
			break;
		case DSPP1:
			*decode_sel |= BIT(18);
			break;
		case DSPP2:
			*decode_sel |= BIT(19);
			break;
		case DSPP3:
			*decode_sel |= BIT(20);
			break;
		case SSPP_IGC:
			*decode_sel |= BIT(4);
			break;
		case DSPP_IGC:
			*decode_sel |= BIT(21);
			break;
		case LTM0:
			*decode_sel |= BIT(22);
			break;
		case LTM1:
			*decode_sel |= BIT(23);
			break;
		case LTM2:
			*decode_sel |= BIT(24);
			break;
		case LTM3:
			*decode_sel |= BIT(25);
			break;
		case DSPP4:
			*decode_sel |= BIT(26);
			break;
		case DSPP5:
			*decode_sel |= BIT(27);
			break;
		case DSPP6:
			*decode_sel |= BIT(28);
			break;
		case DSPP7:
			*decode_sel |= BIT(29);
			break;
		case MDSS:
			*decode_sel |= BIT(31);
			break;
		default:
			DRM_ERROR("block not supported %zx\n", (size_t)BIT(i));
			break;
		}
	}
}

int _sde_reg_write_check_split(struct sde_reg_dma_buffer *dma_buf, u32 size)
{
	/* Reserve 11 DWORDs for possible:
	 * dec_sel (2), opcode (1), header (1-2), even writes padding (4),
	 * and last_cmd (2) in case
	 */
	if (dma_buf->index + size >= dma_buf->split_size + MAX_DWORDS_SZ - sizeof(u32) * 11) {
		/* Reached the LUTDMA workload size limit, move to next split */
		if (dma_buf->num_splits >= REG_DMA_BUFFER_MAX_SPLITS) {
			DRM_ERROR("Buf split overflow index %d max size %d splits %d\n",
				dma_buf->index, dma_buf->buffer_size,
				dma_buf->num_splits);
			return -EINVAL;
		}

		/* Pad to even writes */
		if ((dma_buf->abs_write_cnt % 2) != 0) {
			DRM_DEBUG("Padding split %d idx=%d sz=%d bufsz=%d split=%d wr=%d\n",
				dma_buf->num_splits, dma_buf->index, size, dma_buf->buffer_size,
				dma_buf->split_size, dma_buf->abs_write_cnt);
			/* Touch up buffer to avoid HW issues with odd number of abs writes */
			u32 reg = 0;
			struct sde_reg_dma_setup_ops_cfg dma_write_cfg;

			dma_write_cfg.dma_buf = dma_buf;
			dma_write_cfg.blk = MDSS;
			dma_write_cfg.feature = REG_DMA_FEATURES_MAX;
			dma_write_cfg.ops = HW_BLK_SELECT;
			if (validate_write_decode_sel(&dma_write_cfg) ||
					write_decode_sel(&dma_write_cfg)) {
				DRM_ERROR("MDSS decode select failed for LUTDMA touch up\n");
				return -EINVAL;
			}

			/* Perform dummy write on LUTDMA RO version reg */
			dma_write_cfg.ops = REG_SINGLE_WRITE;
			dma_write_cfg.blk_offset = reg_dma[dma_buf->dpu_idx]->caps->base_off;
			dma_write_cfg.data = &reg;
			dma_write_cfg.data_size = sizeof(uint32_t);
			if (validate_write_reg(&dma_write_cfg) ||
					write_single_reg(&dma_write_cfg)) {
				DRM_ERROR("Add touch up write failed to LUTDMA buffer\n");
				return -EINVAL;
			}
			dma_buf->abs_write_cnt++;
		}

		dma_buf->buf_splits[dma_buf->num_splits] = dma_buf->index;
		/* Align next workload address */
		dma_buf->index = (dma_buf->index + ADDR_ALIGN - 1) & ~(ADDR_ALIGN - 1);
		dma_buf->split_size = dma_buf->index;
		dma_buf->split_start[dma_buf->num_splits] = dma_buf->index;
		dma_buf->num_splits++;
		DRM_DEBUG("Create split idx=%d size=%d bufsize=%d splits=%d split=%d write=%d\n",
			dma_buf->index, size, dma_buf->buffer_size,
			dma_buf->num_splits, dma_buf->split_size, dma_buf->abs_write_cnt);
	}

	return 0;
}

#if ENABLE_REG_DMA_MDSS_REGISTER_WRITE

#define LUTDMA_LOG_BUF_SIZE	256

void sde_reg_write_dec_sel_mdss(struct sde_reg_dma_buffer *dma_buf)
{
	u32 *loc = NULL;

	loc =  (u32 *)((u8 *)dma_buf->vaddr + dma_buf->index);
	loc[0] = reg_dma_decode_sel;
	loc[1] = BIT(31);
	dma_buf->index += ops_mem_size[REG_SINGLE_WRITE];
	dma_buf->ops_completed |= DECODE_SEL_OP;
	dma_buf->next_op_allowed = REG_WRITE_OP;
}

void sde_reg_write_reg_dma(struct sde_hw_blk_reg_map *c,
		u32 reg_off,
		u32 val,
		const char *name)
{
	if (!c) {
		SDE_ERROR("Invalid c\n");
		return;
	}
	struct sde_reg_dma_buffer **vq_bufs = c->vq_ctx;
	if (!vq_bufs) {
		SDE_ERROR("Invalid vq_bufs [%s:0x%X] <= 0x%X\n",
				name, c->blk_off + reg_off, val);
		return;
	}
	struct sde_reg_dma_buffer *dma_buf = vq_bufs[REG_DMA_MDSS_DB];
	struct sde_hw_vatran *vatran;
	u32 map_addr = (u32)-1;
	u32 *loc = NULL;

	if (!dma_buf) {
		SDE_ERROR("invalid dma_buf [%s:0x%X] <= 0x%X\n",
				name, c->blk_off + reg_off, val);
		return;
	}

	if (_sde_reg_write_check_split(dma_buf, sizeof(u32) * 2))
		return;

	/* don't need to mutex protect this */
	if (c->log_mask & sde_hw_util_log_mask)
		SDE_DEBUG_DRIVER("REG_WRITE [%s:0x%X] <= 0x%X\n",
				name, c->blk_off + reg_off, val);

	vatran = sde_hw_get_vatran(dma_buf->dpu_idx);
	if (vatran)
		map_addr = vatran->ops.remap(vatran, dma_buf->vq_idx, c, reg_off);
	if (map_addr != (u32)-1)
		/* Register remapped to VA_TRAN space */
		c = &vatran->hw;
	else
		map_addr = c->blk_off + reg_off;

	if ((dma_buf->next_op_allowed & DECODE_SEL_OP) &&
		!(dma_buf->ops_completed & DECODE_SEL_OP))
		sde_reg_write_dec_sel_mdss(dma_buf);

	loc =  (u32 *)((u8 *)dma_buf->vaddr +
			dma_buf->index);
	loc[0] = SINGLE_REG_WRITE_OPCODE;
	loc[0] |= (map_addr & MAX_RELATIVE_OFF);
	loc[0] |= ABSOLUTE_RANGE;
	dma_buf->abs_write_cnt++;

	loc[1] = val;
	dma_buf->index += ops_mem_size[REG_BLK_WRITE_SINGLE];
	dma_buf->ops_completed |= REG_WRITE_OP;
	dma_buf->next_op_allowed = REG_WRITE_OP | DECODE_SEL_OP;

	SDE_REG_LOG(c->log_mask ? ilog2(c->log_mask)+1 : 0,
			val, c->blk_off + reg_off);
}

void sde_reg_write_reg_dma_inc(struct sde_hw_blk_reg_map *c,
		u32 reg_off,
		u32 *data, u32 size,
		const char *name)
{
	//SDE_ERROR("sde_reg_write_reg_dma_inc   %pK, %X %pK %s\n", c, reg_off, data, name);
	if (!c) {
		SDE_ERROR("Invalid c\n");
		return;
	}
	struct sde_reg_dma_buffer **vq_bufs = c->vq_ctx;
	struct sde_reg_dma_buffer *dma_buf = vq_bufs[REG_DMA_MDSS_DB];
	u32 *loc = NULL;
	char log[LUTDMA_LOG_BUF_SIZE], *p, *end;
	int i;

	if (!dma_buf) {
		SDE_ERROR("invalid dma_buf [%s:0x%X]++ <=\n", name,
						c->blk_off + reg_off);
		return;
	}

	if (_sde_reg_write_check_split(dma_buf, sizeof(u32) * (2 + size)))
		return;

	if ((dma_buf->next_op_allowed & DECODE_SEL_OP) &&
		!(dma_buf->ops_completed & DECODE_SEL_OP))
		sde_reg_write_dec_sel_mdss(dma_buf);

	loc =  (u32 *)((u8 *)dma_buf->vaddr + dma_buf->index);
	loc[0] = AUTO_INC_REG_WRITE_OPCODE;
	loc[0] |= ((c->blk_off + reg_off) & MAX_RELATIVE_OFF);
	loc[0] |= ABSOLUTE_RANGE;
	dma_buf->abs_write_cnt += size;

	loc[1] = size;
	memcpy(&loc[2], data, size * sizeof(u32));
	dma_buf->index += ops_mem_size[REG_BLK_WRITE_INC] + size * sizeof(u32);
	dma_buf->next_op_allowed = REG_WRITE_OP | DECODE_SEL_OP;
	dma_buf->ops_completed |= REG_WRITE_OP;

	/* don't need to mutex protect this */
	if (c->log_mask & sde_hw_util_log_mask) {
		end = log + LUTDMA_LOG_BUF_SIZE - 1;
		p = log;
		for (i = 0; i < size; i++) {
			if (i % 16 == 0) {
				if (p != log)
					SDE_DEBUG_DRIVER("REG_WRITE %s\n", log);
				p = log;
				p += snprintf(p, (u32)(end - p), "[%s:0x%lX]++ <=", name,
						c->blk_off + reg_off + i * sizeof(u32));
			}
			p += snprintf(p, (u32)(end - p), " 0x%8.8X", *data++);
		}
		if (p != log)
			SDE_DEBUG_DRIVER("REG_WRITE %s\n", log);
	}

	SDE_REG_LOG(c->log_mask ? ilog2(c->log_mask)+1 : 0,
			size, c->blk_off + reg_off);
}

void sde_reg_write_reg_dma_single(struct sde_hw_blk_reg_map *c,
		u32 reg_off,
		u32 *data, u32 size,
		const char *name)
{
	if (!c) {
		SDE_ERROR("Invalid c\n");
		return;
	}
	struct sde_reg_dma_buffer **vq_bufs = c->vq_ctx;
	struct sde_reg_dma_buffer *dma_buf = vq_bufs[REG_DMA_MDSS_DB];
	u32 *loc = NULL;
	char log[LUTDMA_LOG_BUF_SIZE], *p, *end;
	int i;

	if (!dma_buf) {
		SDE_ERROR("invalid dma_buf [%s:0x%X] <=\n", name, c->blk_off + reg_off);
		return;
	}

	if (_sde_reg_write_check_split(dma_buf, sizeof(u32) * (2 + size)))
		return;

	if ((dma_buf->next_op_allowed & DECODE_SEL_OP) &&
		!(dma_buf->ops_completed & DECODE_SEL_OP))
		sde_reg_write_dec_sel_mdss(dma_buf);

	loc =  (u32 *)((u8 *)dma_buf->vaddr + dma_buf->index);
	loc[0] = HW_INDEX_REG_WRITE_OPCODE;
	loc[0] |= ((c->blk_off + reg_off) & MAX_RELATIVE_OFF);
	loc[0] |= ABSOLUTE_RANGE;
	dma_buf->abs_write_cnt += size;

	loc[1] = size;
	memcpy(&loc[2], data, size * sizeof(u32));
	dma_buf->index += ops_mem_size[REG_BLK_WRITE_SINGLE] + size * sizeof(u32);
	dma_buf->next_op_allowed = REG_WRITE_OP | DECODE_SEL_OP;
	dma_buf->ops_completed |= REG_WRITE_OP;

	/* don't need to mutex protect this */
	if (c->log_mask & sde_hw_util_log_mask) {
		end = log + LUTDMA_LOG_BUF_SIZE - 1;
		p = log;
		for (i = 0; i < size; i++) {
			if (i % 16 == 0) {
				if (p != log)
					SDE_DEBUG_DRIVER("REG_WRITE %s\n", log);
				p = log;
				p += snprintf(p, (u32)(end - p), "[%s:0x%X] <=", name, c->blk_off + reg_off);
			}
			p += snprintf(p, (u32)(end - p), " 0x%8.8X", *data++);
		}
		if (p != log)
			SDE_DEBUG_DRIVER("REG_WRITE %s\n", log);
	}

	SDE_REG_LOG(c->log_mask ? ilog2(c->log_mask)+1 : 0,
			size, c->blk_off + reg_off);
}

void sde_reg_write_reg_dma_multiple(struct sde_hw_blk_reg_map *c,
		u32 reg_off,
		u32 *data, u32 size,
		bool inc, u32 wrap,
		const char *name)
{
	if (!c) {
		SDE_ERROR("Invalid c\n");
		return;
	}
	struct sde_reg_dma_buffer **vq_bufs = c->vq_ctx;
	struct sde_reg_dma_buffer *dma_buf = vq_bufs[REG_DMA_MDSS_DB];
	u32 *loc = NULL;
	char log[LUTDMA_LOG_BUF_SIZE], *p, *end;
	int i;

	if (!dma_buf) {
		SDE_ERROR("invalid dma_buf [%s:0x%X]++%d|%d <=\n", name,
						c->blk_off + reg_off, inc, wrap);
		return;
	}

	if (_sde_reg_write_check_split(dma_buf, sizeof(u32) * (2 + size)))
		return;

	if ((dma_buf->next_op_allowed & DECODE_SEL_OP) &&
		!(dma_buf->ops_completed & DECODE_SEL_OP))
		sde_reg_write_dec_sel_mdss(dma_buf);

	loc =  (u32 *)((u8 *)dma_buf->vaddr + dma_buf->index);
	loc[0] = BLK_REG_WRITE_OPCODE;
	loc[0] |= ((c->blk_off + reg_off) & MAX_RELATIVE_OFF);
	loc[0] |= ABSOLUTE_RANGE;
	dma_buf->abs_write_cnt += size;

	loc[1] = inc ? 0 : BIT(31);
	loc[1] |= (wrap & WRAP_MAX_SIZE) << 16;
	loc[1] |= ((size / wrap) & MAX_DWORDS_SZ);
	memcpy(&loc[2], data, size * sizeof(u32));
	dma_buf->index += ops_mem_size[REG_BLK_WRITE_MULTIPLE] + size * sizeof(u32);
	dma_buf->next_op_allowed = REG_WRITE_OP | DECODE_SEL_OP;
	dma_buf->ops_completed |= REG_WRITE_OP;

	/* don't need to mutex protect this */
	if (c->log_mask & sde_hw_util_log_mask) {
		end = log + LUTDMA_LOG_BUF_SIZE - 1;
		p = log;
		for (i = 0; i < size; i++) {
			if (i % 16 == 0) {
				if (p != log)
					SDE_DEBUG_DRIVER("REG_MODIFY %s\n", log);
				p = log;
				p += snprintf(p, (u32)(end - p), "[%s:0x%lX]++%d|%d <=", name,
						c->blk_off + reg_off + i * sizeof(u32), inc, wrap);
			}
			p += snprintf(p, (u32)(end - p), " 0x%8.8X", *data++);
		}
		if (p != log)
			SDE_DEBUG_DRIVER("REG_WRITE %s\n", log);
	}

	SDE_REG_LOG(c->log_mask ? ilog2(c->log_mask)+1 : 0,
			size, c->blk_off + reg_off);
}

void sde_reg_modify_reg_dma(struct sde_hw_blk_reg_map *c,
		u32 reg_off,
		u32 mask,
		u32 val,
		const char *name)
{
	if (!c) {
		SDE_ERROR("Invalid c\n");
		return;
	}
	struct sde_reg_dma_buffer **vq_bufs = c->vq_ctx;
	if (!vq_bufs) {
		SDE_ERROR("Invalid vq_bufs [%s:0x%X] <= (0x%X mask 0x%X)\n",
				name, c->blk_off + reg_off, val, mask);
		return;
	}
	struct sde_reg_dma_buffer *dma_buf = vq_bufs[REG_DMA_MDSS_DB];
	struct sde_hw_vatran *vatran;
	u32 map_addr = (u32)-1;
	u32 *loc = NULL;

	if (!dma_buf) {
		SDE_ERROR("invalid dma_buf [%s:0x%X] <= (0x%X mask 0x%X)\n",
				name, c->blk_off + reg_off, val, mask);
		return;
	}

	if (_sde_reg_write_check_split(dma_buf, sizeof(u32) * 3))
		return;

	/* don't need to mutex protect this */
	if (c->log_mask & sde_hw_util_log_mask)
		SDE_DEBUG_DRIVER("REG_WRITE [%s:0x%X] <= (0x%X mask 0x%X)\n",
				name, c->blk_off + reg_off, val, mask);

	vatran = sde_hw_get_vatran(dma_buf->dpu_idx);
	if (vatran)
		map_addr = vatran->ops.remap(vatran, dma_buf->vq_idx, c, reg_off);
	if (map_addr != (u32)-1)
		/* Register remapped to VA_TRAN space */
		c = &vatran->hw;
	else
		map_addr = c->blk_off + reg_off;

	if ((dma_buf->next_op_allowed & DECODE_SEL_OP) &&
		!(dma_buf->ops_completed & DECODE_SEL_OP))
		sde_reg_write_dec_sel_mdss(dma_buf);

	loc =  (u32 *)((u8 *)dma_buf->vaddr + dma_buf->index);
	loc[0] = SINGLE_REG_MODIFY_OPCODE;
	loc[0] |= (map_addr & MAX_RELATIVE_OFF);
	loc[0] |= ABSOLUTE_RANGE;
	dma_buf->abs_write_cnt++;

	loc[1] = ~mask;
	loc[2] = val;
	dma_buf->index += ops_mem_size[REG_SINGLE_MODIFY];
	dma_buf->ops_completed |= REG_WRITE_OP;
	dma_buf->next_op_allowed = REG_WRITE_OP | DECODE_SEL_OP;

	SDE_REG_LOG(c->log_mask ? ilog2(c->log_mask)+1 : 0,
			val, c->blk_off + reg_off);
}

uint32_t read_reg_vatran(struct sde_hw_blk_reg_map *hw, uint32_t reg_off, const char *name)
{
	struct sde_reg_dma_buffer **vq_bufs;
	struct sde_hw_vatran *vatran;
	u32 map_addr = (u32)-1;

	vq_bufs = hw->vq_ctx;
	vatran = sde_hw_get_vatran(vq_bufs[REG_DMA_MDSS_DB]->dpu_idx);
	if (vatran)
		map_addr = vatran->ops.remap(vatran, vq_bufs[REG_DMA_MDSS_DB]->vq_idx, hw, reg_off);
	if (map_addr != (u32)-1) {
		/* Register remapped to VA_TRAN space */
		hw = &vatran->hw;
		return sde_reg_read(hw, map_addr - vatran->caps->base_off, name);
	} else {
		return sde_reg_read(hw, reg_off, name);
	}
}

#endif

const char *buf_type_str[] =
{
	"DB REG",
	"SB REG",
	"DB Table",
	"SB Table",
	"Unknown",
};

static int write_multi_reg(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	u8 *loc = NULL;

	loc =  (u8 *)cfg->dma_buf->vaddr + cfg->dma_buf->index;
	memcpy(loc, cfg->data, cfg->data_size);
	cfg->dma_buf->index += cfg->data_size;
	cfg->dma_buf->next_op_allowed = REG_WRITE_OP | DECODE_SEL_OP;
	cfg->dma_buf->ops_completed |= REG_WRITE_OP;

	if (cfg->blk == MDSS)
		cfg->dma_buf->abs_write_cnt += SIZE_DWORD(cfg->data_size);

	return 0;
}

int write_multi_reg_index(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	u32 *loc = NULL;

	SDE_DEBUG_DRIVER("dpu%d vq%d %s WRITE_INDEX %X: blk %lX sz %X\n",
			cfg->dma_buf->dpu_idx, cfg->dma_buf->vq_idx,
			buf_type_str[cfg->dma_buf->buffer_type],
			cfg->blk_offset, cfg->blk, cfg->data_size);

	loc =  (u32 *)((u8 *)cfg->dma_buf->vaddr +
			cfg->dma_buf->index);
	loc[0] = HW_INDEX_REG_WRITE_OPCODE;
	loc[0] |= (cfg->blk_offset & MAX_RELATIVE_OFF);
	if (cfg->blk == MDSS)
		loc[0] |= ABSOLUTE_RANGE;

	loc[1] = SIZE_DWORD(cfg->data_size);
	cfg->dma_buf->index += ops_mem_size[cfg->ops];

	return write_multi_reg(cfg);
}

int write_multi_reg_inc(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	u32 *loc = NULL;

	SDE_DEBUG_DRIVER("dpu%d vq%d %s WRITE_INC %X: blk %lX sz %X\n",
			cfg->dma_buf->dpu_idx, cfg->dma_buf->vq_idx,
			buf_type_str[cfg->dma_buf->buffer_type],
			cfg->blk_offset, cfg->blk, cfg->data_size);

	loc =  (u32 *)((u8 *)cfg->dma_buf->vaddr +
			cfg->dma_buf->index);
	loc[0] = AUTO_INC_REG_WRITE_OPCODE;
	if (cfg->blk == MDSS)
		loc[0] |= ABSOLUTE_RANGE;

	loc[0] |= (cfg->blk_offset & MAX_RELATIVE_OFF);
	loc[1] = SIZE_DWORD(cfg->data_size);
	cfg->dma_buf->index += ops_mem_size[cfg->ops];

	return write_multi_reg(cfg);
}

static int write_multi_lut_reg(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	u32 *loc = NULL;

	SDE_DEBUG_DRIVER("dpu%d vq%d %s WRITE_LUT %X: blk %lX sz %X\n",
			cfg->dma_buf->dpu_idx, cfg->dma_buf->vq_idx,
			buf_type_str[cfg->dma_buf->buffer_type],
			cfg->blk_offset, cfg->blk, cfg->data_size);

	loc =  (u32 *)((u8 *)cfg->dma_buf->vaddr +
			cfg->dma_buf->index);
	loc[0] = BLK_REG_WRITE_OPCODE;
	loc[0] |= (cfg->blk_offset & MAX_RELATIVE_OFF);
	if (cfg->blk == MDSS)
		loc[0] |= ABSOLUTE_RANGE;

	loc[1] = (cfg->inc) ? 0 : BIT(31);
	loc[1] |= (cfg->wrap_size & WRAP_MAX_SIZE) << 16;
	loc[1] |= ((SIZE_DWORD(cfg->data_size)) & MAX_DWORDS_SZ);
	cfg->dma_buf->next_op_allowed = REG_WRITE_OP;
	cfg->dma_buf->index += ops_mem_size[cfg->ops];

	return write_multi_reg(cfg);
}

static int write_single_reg(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	u32 *loc = NULL;

	SDE_DEBUG_DRIVER("dpu%d vq%d %s WRITE_SINGLE %X: blk %lX %X\n",
			cfg->dma_buf->dpu_idx, cfg->dma_buf->vq_idx,
			buf_type_str[cfg->dma_buf->buffer_type],
			cfg->blk_offset, cfg->blk, *cfg->data);

	loc =  (u32 *)((u8 *)cfg->dma_buf->vaddr +
			cfg->dma_buf->index);
	loc[0] = SINGLE_REG_WRITE_OPCODE;
	loc[0] |= (cfg->blk_offset & MAX_RELATIVE_OFF);
	if (cfg->blk == MDSS) {
		loc[0] |= ABSOLUTE_RANGE;
		cfg->dma_buf->abs_write_cnt++;
	}

	loc[1] = *cfg->data;
	cfg->dma_buf->index += ops_mem_size[cfg->ops];
	cfg->dma_buf->ops_completed |= REG_WRITE_OP;
	cfg->dma_buf->next_op_allowed = REG_WRITE_OP | DECODE_SEL_OP;

	return 0;
}

static int write_single_modify(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	u32 *loc = NULL;

	SDE_DEBUG_DRIVER("dpu%d vq%d %s MODIFY_SINGLE %X: blk %lX mask %X %X\n",
			cfg->dma_buf->dpu_idx, cfg->dma_buf->vq_idx,
			buf_type_str[cfg->dma_buf->buffer_type],
			cfg->blk_offset, cfg->blk, cfg->mask, *cfg->data);

	loc =  (u32 *)((u8 *)cfg->dma_buf->vaddr +
			cfg->dma_buf->index);
	loc[0] = SINGLE_REG_MODIFY_OPCODE;
	loc[0] |= (cfg->blk_offset & MAX_RELATIVE_OFF);
	if (cfg->blk == MDSS)
		loc[0] |= ABSOLUTE_RANGE;

	loc[1] = cfg->mask;
	loc[2] = *cfg->data;
	cfg->dma_buf->index += ops_mem_size[cfg->ops];
	cfg->dma_buf->ops_completed |= REG_WRITE_OP;
	cfg->dma_buf->next_op_allowed = REG_WRITE_OP | DECODE_SEL_OP;

	return 0;
}

static int write_block_lut_reg(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	u32 *loc = NULL;
	int rc = -EINVAL;

	SDE_DEBUG_DRIVER("dpu%d vq%d %s BLOCK_LUT %X: blk %lX tbl %d blk_sel %X sz %X x %X\n",
			cfg->dma_buf->dpu_idx, cfg->dma_buf->vq_idx,
			buf_type_str[cfg->dma_buf->buffer_type],
			cfg->blk_offset, cfg->blk, cfg->table_sel, cfg->block_sel,
			cfg->trans_size, cfg->lut_size);

	loc =  (u32 *)((u8 *)cfg->dma_buf->vaddr +
			cfg->dma_buf->index);
	loc[0] = LUTBUS_WRITE_OPCODE;
	loc[0] |= (cfg->table_sel << 16) & LUTBUS_TABLE_SEL_MASK;
	loc[0] |= (cfg->block_sel & LUTBUS_BLOCK_SEL_MASK);
	loc[1] = (cfg->trans_size << 16) & LUTBUS_TRANS_SZ_MASK;
	loc[1] |= (cfg->lut_size & LUTBUS_LUT_SIZE_MASK);
	cfg->dma_buf->index += ops_mem_size[cfg->ops];

	rc = write_multi_reg(cfg);
	if (rc)
		return rc;

	/* adding 3 NO OPs as SW workaround for REG_BLK_LUT_WRITE
	 * HW limitation that requires the residual data plus the
	 * following opcode to exceed 4 DWORDs length.
	 */
	loc =  (u32 *)((u8 *)cfg->dma_buf->vaddr +
			cfg->dma_buf->index);
	loc[0] = NO_OP_OPCODE;
	loc[1] = NO_OP_OPCODE;
	loc[2] = NO_OP_OPCODE;
	cfg->dma_buf->index += sizeof(u32) * 3;

	return 0;
}

static int write_decode_sel(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	u32 *loc = NULL;

	SDE_DEBUG_DRIVER("dpu%d vq%d %s WRITE_DEC_SEL %X: blk %lX\n",
			cfg->dma_buf->dpu_idx, cfg->dma_buf->vq_idx,
			buf_type_str[cfg->dma_buf->buffer_type],
			reg_dma_decode_sel, cfg->blk);

	loc =  (u32 *)((u8 *)cfg->dma_buf->vaddr +
			cfg->dma_buf->index);
	loc[0] = reg_dma_decode_sel;
	get_decode_sel(cfg->blk, &loc[1]);
	cfg->dma_buf->index += ops_mem_size[cfg->ops];
	cfg->dma_buf->ops_completed |= DECODE_SEL_OP;
	cfg->dma_buf->next_op_allowed = REG_WRITE_OP;

	return 0;
}

static int validate_write_multi_lut_reg(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	int rc;

	rc = validate_write_reg(cfg);
	if (rc)
		return rc;

	if (cfg->wrap_size < WRAP_MIN_SIZE || cfg->wrap_size > WRAP_MAX_SIZE) {
		DRM_ERROR("invalid wrap sz %d min %d max %zd\n",
			cfg->wrap_size, WRAP_MIN_SIZE, (size_t)WRAP_MAX_SIZE);
		rc = -EINVAL;
	}

	return rc;
}

static int validate_blk_lut_write(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	int rc;

	rc = validate_write_reg(cfg);
	if (rc)
		return rc;

	if (cfg->table_sel >= LUTBUS_TABLE_SELECT_MAX ||
			cfg->block_sel >= LUTBUS_BLOCK_MAX ||
			(cfg->trans_size != LUTBUS_IGC_TRANS_SIZE &&
			cfg->trans_size != LUTBUS_GAMUT_TRANS_SIZE &&
			cfg->trans_size != LUTBUS_SIXZONE_TRANS_SIZE)) {
		DRM_ERROR("invalid table_sel %d block_sel %d trans_size %d\n",
				cfg->table_sel, cfg->block_sel,
				cfg->trans_size);
		rc = -EINVAL;
	}

	return rc;
}

static int validate_write_reg(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	u32 remain_len, write_len;

	remain_len = BUFFER_SPACE_LEFT(cfg);
	write_len = ops_mem_size[cfg->ops] + cfg->data_size;
	if (remain_len < write_len) {
		DRM_ERROR("buffer is full sz %d needs %d bytes\n",
				remain_len, write_len);
		return -EINVAL;
	}

	if (!cfg->data) {
		DRM_ERROR("invalid data %pK size %d exp sz %d\n", cfg->data,
			cfg->data_size, write_len);
		return -EINVAL;
	}
	if ((SIZE_DWORD(cfg->data_size)) > MAX_DWORDS_SZ ||
	    NOT_WORD_ALIGNED(cfg->data_size)) {
		DRM_ERROR("Invalid data size %d max %zd align %x\n",
			cfg->data_size, (size_t)MAX_DWORDS_SZ,
			NOT_WORD_ALIGNED(cfg->data_size));
		return -EINVAL;
	}

	if (cfg->blk_offset > MAX_RELATIVE_OFF ||
			NOT_WORD_ALIGNED(cfg->blk_offset)) {
		DRM_ERROR("invalid offset %d max %zd align %x\n",
				cfg->blk_offset, (size_t)MAX_RELATIVE_OFF,
				NOT_WORD_ALIGNED(cfg->blk_offset));
		return -EINVAL;
	}

	return 0;
}

static int validate_write_decode_sel(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	u32 remain_len;
	bool vig_blk, dma_blk, dspp_blk, mdss_blk;

	remain_len = BUFFER_SPACE_LEFT(cfg);
	if (remain_len < ops_mem_size[HW_BLK_SELECT]) {
		DRM_ERROR("buffer is full needs %d bytes\n",
				ops_mem_size[HW_BLK_SELECT]);
		return -EINVAL;
	}

	if (!cfg->blk) {
		DRM_ERROR("blk set as 0\n");
		return -EINVAL;
	}

	vig_blk = (cfg->blk & GRP_VIG_HW_BLK_SELECT) ? true : false;
	dma_blk = (cfg->blk & GRP_DMA_HW_BLK_SELECT) ? true : false;
	dspp_blk = (cfg->blk & GRP_DSPP_HW_BLK_SELECT) ? true : false;
	mdss_blk = (cfg->blk & MDSS) ? true : false;

	if ((vig_blk && dspp_blk) || (dma_blk && dspp_blk) ||
			(vig_blk && dma_blk) ||
			(mdss_blk && (vig_blk | dma_blk | dspp_blk))) {
		DRM_ERROR("invalid blk combination %lx\n", cfg->blk);
		return -EINVAL;
	}

	return 0;
}

static int validate_dma_cfg(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	int rc = 0;
	bool supported;

	if (!cfg || cfg->ops >= REG_DMA_SETUP_OPS_MAX || !cfg->dma_buf) {
		DRM_ERROR("invalid param cfg %pK ops %d dma_buf %pK\n",
			cfg, ((cfg) ? cfg->ops : REG_DMA_SETUP_OPS_MAX),
			((cfg) ? cfg->dma_buf : NULL));
		return -EINVAL;
	}

	rc = check_support_v1(cfg->feature, cfg->blk, &supported);
	if (rc || !supported) {
		DRM_ERROR("check support failed rc %d supported %d\n",
				rc, supported);
		rc = -EINVAL;
		return rc;
	}

	if (cfg->dma_buf->index >= cfg->dma_buf->buffer_size ||
	    NOT_WORD_ALIGNED(cfg->dma_buf->index)) {
		DRM_ERROR("Buf Overflow index %d max size %d align %x\n",
			cfg->dma_buf->index, cfg->dma_buf->buffer_size,
			NOT_WORD_ALIGNED(cfg->dma_buf->index));
		return -EINVAL;
	}

	if (_sde_reg_write_check_split(cfg->dma_buf, cfg->data_size))
		return -EINVAL;

	if (cfg->dma_buf->iova & GUARD_BYTES || !cfg->dma_buf->vaddr) {
		DRM_ERROR("iova not aligned to %zx iova %llx kva %pK",
				(size_t)ADDR_ALIGN, cfg->dma_buf->iova,
				cfg->dma_buf->vaddr);
		return -EINVAL;
	}
	if (!IS_OP_ALLOWED(cfg->ops, cfg->dma_buf->next_op_allowed)) {
		DRM_ERROR("invalid op %x allowed %x\n", cfg->ops,
				cfg->dma_buf->next_op_allowed);
		return -EINVAL;
	}

	if (!validate_dma_op_params[cfg->ops] ||
	    !write_dma_op_params[cfg->ops]) {
		DRM_ERROR("invalid op %d validate %pK write %pK\n", cfg->ops,
			validate_dma_op_params[cfg->ops],
			write_dma_op_params[cfg->ops]);
		return -EINVAL;
	}
	return rc;
}

static int validate_kick_off_v1(struct sde_reg_dma_kickoff_cfg *cfg, u32 dpu_idx)
{

	if (!cfg || !cfg->ctl || !cfg->dma_buf ||
			cfg->dma_type >= REG_DMA_TYPE_MAX) {
		DRM_ERROR("invalid cfg %pK ctl %pK dma_buf %pK dma type %d\n",
				cfg, ((!cfg) ? NULL : cfg->ctl),
				((!cfg) ? NULL : cfg->dma_buf),
				((!cfg) ? 0 : cfg->dma_type));
		return -EINVAL;
	}

	if (dpu_idx >= DPU_MAX) {
		DRM_ERROR("invalid dpu idx %d\n", dpu_idx);
		return -EINVAL;
	}

	if (reg_dma[dpu_idx]->caps->reg_dma_blks[cfg->dma_type].valid == false) {
		DRM_DEBUG("REG dma type %d is not supported\n", cfg->dma_type);
		return -EOPNOTSUPP;
	}

	if (cfg->ctl->idx < CTL_0 || cfg->ctl->idx >= CTL_MAX) {
		DRM_ERROR("invalid ctl idx %d\n", cfg->ctl->idx);
		return -EINVAL;
	}

	if (cfg->op >= REG_DMA_OP_MAX) {
		DRM_ERROR("invalid op %d\n", cfg->op);
		return -EINVAL;
	}

	if ((cfg->op == REG_DMA_WRITE) &&
	     (!(cfg->dma_buf->ops_completed & DECODE_SEL_OP) ||
	     !(cfg->dma_buf->ops_completed & REG_WRITE_OP))) {
		DRM_ERROR("incomplete write ops %x\n",
				cfg->dma_buf->ops_completed);
		return -EINVAL;
	}

	if (cfg->op == REG_DMA_READ && cfg->block_select >= DSPP_HIST_MAX) {
		DRM_ERROR("invalid block for read %d\n", cfg->block_select);
		return -EINVAL;
	}

	/* Only immediate triggers are supported now hence hardcode */
	cfg->trigger_mode = (cfg->op == REG_DMA_READ) ? (READ_TRIGGER) :
				(WRITE_TRIGGER);

	if (cfg->dma_buf->iova & GUARD_BYTES) {
		DRM_ERROR("Address is not aligned to %zx iova %llx",
				(size_t)ADDR_ALIGN, cfg->dma_buf->iova);
		return -EINVAL;
	}

	if (cfg->queue_select >= DMA_CTL_QUEUE_MAX) {
		DRM_ERROR("invalid queue selected %d\n", cfg->queue_select);
		return -EINVAL;
	}

	if (SIZE_DWORD(cfg->dma_buf->index) > MAX_DWORDS_SZ ||
			!cfg->dma_buf->index) {
		DRM_ERROR("invalid dword size %zd max %zd\n",
			(size_t)SIZE_DWORD(cfg->dma_buf->index),
				(size_t)MAX_DWORDS_SZ);
		return -EINVAL;
	}

	if (!validate_queue_func || validate_queue_func(cfg)) {
		DRM_ERROR("invalid queue selected %d or op %d for SB LUTDMA\n",
				cfg->queue_select, cfg->op);
		return -EINVAL;
	}

	if ((cfg->dma_buf->abs_write_cnt % 2) != 0) {
		/* Touch up buffer to avoid HW issues with odd number of abs writes */
		u32 reg = 0;
		struct sde_reg_dma_setup_ops_cfg dma_write_cfg;

		dma_write_cfg.dma_buf = cfg->dma_buf;
		dma_write_cfg.blk = MDSS;
		dma_write_cfg.feature = REG_DMA_FEATURES_MAX;
		dma_write_cfg.ops = HW_BLK_SELECT;
		if (validate_write_decode_sel(&dma_write_cfg) || write_decode_sel(&dma_write_cfg)) {
			DRM_ERROR("Failed setting MDSS decode select for LUTDMA touch up\n");
			return -EINVAL;
		}

		/* Perform dummy write on LUTDMA RO version reg */
		dma_write_cfg.ops = REG_SINGLE_WRITE;
		dma_write_cfg.blk_offset = reg_dma[dpu_idx]->caps->base_off +
				reg_dma[dpu_idx]->caps->reg_dma_blks[cfg->dma_type].base;
		dma_write_cfg.data = &reg;
		dma_write_cfg.data_size = sizeof(uint32_t);
		if (validate_write_reg(&dma_write_cfg) || write_single_reg(&dma_write_cfg)) {
			DRM_ERROR("Failed to add touch up write to LUTDMA buffer\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int write_kick_off_v1(struct sde_reg_dma_kickoff_cfg *cfg, u32 dpu_idx)
{
	u32 cmd1;
	struct sde_hw_blk_reg_map hw;

	if (dpu_idx >= DPU_MAX) {
		DRM_ERROR("invalid dpu idx %d\n", dpu_idx);
		return -EINVAL;
	}

	memset(&hw, 0, sizeof(hw));
	msm_gem_sync(cfg->dma_buf->buf);
	cmd1 = (cfg->op == REG_DMA_READ) ?
		(dspp_read_sel[cfg->block_select] << 30) : 0;
	cmd1 |= (cfg->last_command) ? BIT(24) : 0;
	cmd1 |= (cfg->op == REG_DMA_READ) ? (2 << 22) : 0;
	cmd1 |= (cfg->op == REG_DMA_WRITE) ? (BIT(22)) : 0;
	cmd1 |= (SIZE_DWORD(cfg->dma_buf->index) & MAX_DWORDS_SZ);

	if (cfg->dma_type == REG_DMA_TYPE_DB)
		SET_UP_REG_DMA_REG(hw, reg_dma[dpu_idx], REG_DMA_TYPE_DB);
	else if (cfg->dma_type == REG_DMA_TYPE_SB)
		SET_UP_REG_DMA_REG(hw, reg_dma[dpu_idx], REG_DMA_TYPE_SB);

	if (hw.hw_rev == 0) {
		DRM_ERROR("DMA type %d is unsupported\n", cfg->dma_type);
		return -EOPNOTSUPP;
	}

	SDE_REG_WRITE(&hw, reg_dma_opmode_offset, BIT(0));
	read_clear_reg_dma_status(cfg, &hw);

	if (cfg->last_command) {
		/* ensure all packets are queued in packet queue before
		 * queuing last command descriptor (last command)
		 */
		wmb();
	}

	reg_dma_submit_payload(cfg, &hw, cmd1);

	if (cfg->last_command) {
		/* ensure last command is queued before lut dma trigger */
		wmb();
		trigger_reg_dma(cfg, &hw);
	}

	SDE_EVT32(cfg->feature, cfg->dma_type,
			((uint64_t)cfg->dma_buf) >> 32,
			((uint64_t)cfg->dma_buf) & 0xFFFFFFFF,
			(cfg->dma_buf->iova) >> 32,
			(cfg->dma_buf->iova) & 0xFFFFFFFF,
			cfg->op,
			cfg->queue_select, cfg->ctl->idx,
			SIZE_DWORD(cfg->dma_buf->index),
			dpu_idx);
	return 0;
}

static bool setup_clk_force_ctrl(struct sde_hw_blk_reg_map *hw,
		enum sde_clk_ctrl_type clk_ctrl, bool enable)
{
	u32 reg_val, new_val;

	if (!hw)
		return false;

	if (!SDE_CLK_CTRL_LUTDMA_VALID(clk_ctrl))
		return false;

	reg_val = SDE_REG_READ(hw, PMU_CLK_CTRL);

	if (enable)
		new_val = reg_val | (BIT(0) | BIT(16));
	else
		new_val = reg_val & ~(BIT(0) | BIT(16));

	SDE_REG_WRITE(hw, PMU_CLK_CTRL, new_val);
	wmb(); /* ensure write finished before progressing */

	return !(reg_val & (BIT(0) | BIT(16)));
}

static int validate_queue_type_v1_to_3(struct sde_reg_dma_kickoff_cfg *cfg)
{
	if (cfg->dma_type == REG_DMA_TYPE_SB && (cfg->queue_select != DMA_CTL_QUEUE1 ||
			cfg->op == REG_DMA_READ))
		return -EINVAL;

	return 0;
}

static int validate_queue_type_v4(struct sde_reg_dma_kickoff_cfg *cfg)
{
	if (cfg->dma_type == REG_DMA_TYPE_SB && (cfg->queue_select != DMA_CTL_QUEUE0 ||
			cfg->op == REG_DMA_READ))
		return -EINVAL;

	return 0;
}

static enum sde_reg_dma_queue reg_dma_select_queue_sb_v1_to_3(void)
{
	return DMA_CTL_QUEUE1;
}

static enum sde_reg_dma_queue reg_dma_select_queue_sb_v4(void)
{
	return DMA_CTL_QUEUE0;
}

static void reg_dma_read_clear_status_v1_to_v3(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw)
{
	u32 val, mask;

	val = SDE_REG_READ(hw, reg_dma_intr_4_status_offset);
	if (val) {
		DRM_DEBUG("LUT dma status %x\n", val);
		mask = reg_dma_error_clear_mask;
		SDE_REG_WRITE(hw, reg_dma_intr_4_clear_offset, mask);
		SDE_EVT32(val);
	}
}

static void reg_dma_read_clear_status_v4(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw)
{
	u32 val, mask, dpu_idx = cfg->dma_buf->dpu_idx;

	val = SDE_REG_READ(hw, reg_dma_intr_5_status_offset[dpu_idx][cfg->ctl->idx][cfg->dma_type]);
	if (val) {
		DRM_DEBUG("LUT dma status %x\n", val);
		mask = reg_dma_error_clear_mask;
		SDE_REG_WRITE(hw,
				reg_dma_intr_5_status_offset[dpu_idx][cfg->ctl->idx][cfg->dma_type],
				mask);
		SDE_EVT32(val);
	}
}

static void reg_dma_trigger_v1_to_v3(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw)
{
	u32 mask, dpu_idx = cfg->dma_buf->dpu_idx;

	mask = ctl_trigger_done_mask[dpu_idx][cfg->ctl->idx]
		[cfg->ctl->display_idx][cfg->queue_select];
	SDE_REG_WRITE(hw, reg_dma_intr_0_clear_offset[dpu_idx][cfg->ctl->idx][cfg->queue_select],
				mask);
	/* DB LUTDMA use SW trigger while SB LUTDMA uses DSPP_SB
	 * flush as its trigger event.
	 */
	if (cfg->dma_type == REG_DMA_TYPE_DB) {
		SDE_REG_WRITE(&cfg->ctl->hw, reg_dma_ctl_trigger_offset,
				queue_sel[cfg->queue_select]);
	}
}

int reg_dump_dump_raw(char *str, u32 *p, int len, int width, int wrap, char *end)
{
	int i, j, c = 0;
	char *pstr;

	for (i = 0; i < len; i++) {
		if (i % width == 0) {
			if (i)
				SDE_DEBUG("%s\n", str);
			pstr = str;
			pstr += snprintf(pstr, sizeof(str), "\t");
		}
		for (j = 0; j < wrap; j++) {
			pstr += snprintf(pstr, (int)(end - pstr), " %8.8X", *p);
			p++;
			c++;
		}
	}
	SDE_DEBUG("%s\n", str);

	return c;
}

void reg_dma_dump_payload(struct sde_reg_dma_kickoff_cfg *cfg, u32 offset, u32 sz)
{
	u32 *p = cfg->dma_buf->vaddr + offset;
	void *pp;
	int size = (sz ? sz : cfg->dma_buf->index) / sizeof(u32);
	u32 addr, mask, len, wrap, inc, tbl, blk;
	bool abs_addr;
	u32 dec_sel = BIT(31);
	char str[1024], *end = str + sizeof(str) - 1;

	SDE_DEBUG("VQ%d CTL%d payload dump sz 0x%X/0x%lX  %pK %pK  %u\n",
			cfg->dma_buf->vq_idx, cfg->ctl->idx, size,
			cfg->dma_buf->index / sizeof(u32), p, cfg->dma_buf->vaddr, offset);
	SDE_DEBUG("===============================\n");
	while (size) {
		pp = p;
		switch (*p & OPCODE_MASK) {
		case NO_OP_OPCODE:
			SDE_DEBUG("[%5X] NO-OP\n", (int)((void *)pp - cfg->dma_buf->vaddr));
			p++;
			size --;
			break;
		case SINGLE_REG_WRITE_OPCODE:
			addr = *p & ADDR_MASK;
			abs_addr = (*p & REL_ADDR_OPCODE) ? true : false;
			if (*p == reg_dma_decode_sel) {
				p++;
				dec_sel = *p;
				SDE_DEBUG("[%5X] DECODE_SEL %8.8X\n",
						(int)((void *)pp - cfg->dma_buf->vaddr), dec_sel);
			} else {
				p++;
				SDE_DEBUG("[%5X] WRITE%s @0x%6.6X %8.8X\n",
						(int)((void *)pp - cfg->dma_buf->vaddr),
						abs_addr ? "_ABS" : "_REL", addr, *p);
			}
			p++;
			size -= 2;
			break;
		case SINGLE_REG_MODIFY_OPCODE:
			addr = *p & ADDR_MASK;
			abs_addr = (*p & REL_ADDR_OPCODE) ? true : false;
			p++;
			mask = *p++;
			SDE_DEBUG("[%5X] WRITE%s @0x%6.6X  mask %8.8X  val %8.8X\n",
					(int)((void *)pp - cfg->dma_buf->vaddr),
					abs_addr ? "_ABS" : "_REL", addr, ~mask, *p);
			p++;
			size -= 3;
			break;
		case HW_INDEX_REG_WRITE_OPCODE:
			addr = *p & ADDR_MASK;
			abs_addr = (*p & REL_ADDR_OPCODE) ? true : false;
			p++;
			len = *p++;
			SDE_DEBUG("[%5X] WRITE%s @0x%6.6X  len %d:\n",
					(int)((void *)pp - cfg->dma_buf->vaddr),
					abs_addr ? "_ABS" : "_REL", addr, len);
			p += reg_dump_dump_raw(str, p, len, 16, 1, end);
			size -= 2 + len;
			break;
		case AUTO_INC_REG_WRITE_OPCODE:
			addr = *p & ADDR_MASK;
			abs_addr = (*p & REL_ADDR_OPCODE) ? true : false;
			p++;
			len = *p++;
			SDE_DEBUG("[%5X] WRITE%s @0x%6.6X++  len %d:\n",
					(int)((void *)pp - cfg->dma_buf->vaddr),
					abs_addr ? "_ABS" : "_REL", addr, len);
			p += reg_dump_dump_raw(str, p, len, 16, 1, end);
			size -= 2 + len;
			break;
		case BLK_REG_WRITE_OPCODE:
			addr = *p & ADDR_MASK;
			abs_addr = (*p & REL_ADDR_OPCODE) ? true : false;
			p++;
			inc = *p & BIT(31);
			wrap = (*p >> 16)  & WRAP_MAX_SIZE;
			len = *p & MAX_DWORDS_SZ;
			p++;
			SDE_DEBUG("[%5X] WRITE%s @0x%6.6X%s%d  len %d:\n",
					(int)((void *)pp - cfg->dma_buf->vaddr),
					abs_addr ? "_ABS" : "_REL", addr,
					inc ? "++" : "--", wrap, len);
			p += reg_dump_dump_raw(str, p, len, wrap * 4, wrap, end);
			size -= 2 + len * wrap;
			break;
		case LUTBUS_WRITE_OPCODE:
			tbl = (*p & LUTBUS_TABLE_SEL_MASK) >> 16;
			blk = *p & LUTBUS_BLOCK_SEL_MASK;
			p++;
			wrap = (*p & LUTBUS_TRANS_SZ_MASK) >> 16;
			len = *p & LUTBUS_LUT_SIZE_MASK;
			p++;
			SDE_DEBUG("[%5X] WRITE LUT %4.4X TBL %s  TRANS %d  len %d:\n",
					(int)((void *)pp - cfg->dma_buf->vaddr),
					blk, tbl ? "B" : "A", wrap, len);
			p += reg_dump_dump_raw(str, p, len, wrap * 4, wrap, end);
			size -= 2 + len * wrap * 4;
			break;
		default:
			SDE_DEBUG("[%5X] UNKNOWN\n",
					(int)((void *)pp - cfg->dma_buf->vaddr));
			p++;
			size --;
			break;
		}
	}

	if (cfg->last_command)
		SDE_DEBUG("LAST command\n");
	SDE_DEBUG("===============================\n");
}

void reg_dma_workload_dump(struct sde_reg_dma_kickoff_cfg *cfg)
{
	u32 *p = cfg->dma_buf->vaddr;
	int size = cfg->dma_buf->index / sizeof(u32);
	u32 opcode, data, data2, len, wrap, offset;
	char str[1024], *pstr, *end = str + sizeof(str) - 1;
	int i;

	SDE_DEBUG("VQ%d CTL%d workload dump sz 0x%X  %pK\n",
			cfg->dma_buf->vq_idx, cfg->ctl->idx, size, p);
	SDE_DEBUG("===============================\n");
	while (size) {
		opcode = *p;
		offset = (u64)p - (u64)cfg->dma_buf->vaddr;
		switch (opcode & OPCODE_MASK) {
		case NO_OP_OPCODE:
			SDE_DEBUG("%05X: %08X\tNO-OP\n", offset, opcode);
			p++;
			size --;
			break;
		case SINGLE_REG_WRITE_OPCODE:
			data = p[1];
			if (data == reg_dma_decode_sel) {
				SDE_DEBUG("%05X: %08X %08X\tDECODE_SEL\n", offset, opcode, data);
			} else {
				SDE_DEBUG("%05X: %08X %08X\tWRITE\n", offset, opcode, data);
			}
			p+=2;
			size -= 2;
			break;
		case SINGLE_REG_MODIFY_OPCODE:
			data = p[1];
			data2 = p[2];
			SDE_DEBUG("%05X: %08X %08X %08X\tMODIFY\n", offset, opcode, data, data2);
			p+=3;
			size -= 3;
			break;
		case HW_INDEX_REG_WRITE_OPCODE:
			data = p[1];
			len = data;
			SDE_DEBUG("%05X: %08X %08X\tINDEX WRITE:\n", offset, opcode, data);
			p+=2;
			for (i = 0; i < len; i++) {
				if (i % 16 == 0) {
					if (i)
						SDE_DEBUG("%s\n", str);
					pstr = str;
					pstr += snprintf(pstr, sizeof(str), "\t");
				}
				pstr += snprintf(pstr, (int)(end - pstr), " %8.8X", *p);
				p++;
			}
			SDE_DEBUG("%s\n", str);
			size -= 2 + len;
			break;
		case AUTO_INC_REG_WRITE_OPCODE:
			data = p[1];
			len = data;
			SDE_DEBUG("%05X: %08X %08X\tINC WRITE:\n", offset, opcode, data);
			p+=2;
			for (i = 0; i < len; i++) {
				if (i % 16 == 0) {
					if (i)
						SDE_DEBUG("%s\n", str);
					pstr = str;
					pstr += snprintf(pstr, sizeof(str), "\t");
				}
				pstr += snprintf(pstr, (int)(end - pstr), " %8.8X", *p);
				p++;
			}
			SDE_DEBUG("%s\n", str);
			size -= 2 + len;
			break;
		case BLK_REG_WRITE_OPCODE:
			data = p[1];
			wrap = (data >> 16)  & WRAP_MAX_SIZE;
			len = data & MAX_DWORDS_SZ;
			SDE_DEBUG("%05X: %08X %08X\tMULTI WRITE:\n", offset, opcode, data);
			p+=2;
			for (i = 0; i < len*wrap; i++) {
				if (i % 16 == 0) {
					if (i)
						SDE_DEBUG("%s\n", str);
					pstr = str;
					pstr += snprintf(pstr, sizeof(str), "\t");
				}
				pstr += snprintf(pstr, (int)(end - pstr), " %8.8X", *p);
				p++;
			}
			SDE_DEBUG("%s\n", str);
			size -= 2 + len * wrap;
			break;
		case LUTBUS_WRITE_OPCODE:
			data = p[1];
			wrap = (data & LUTBUS_TRANS_SZ_MASK) >> 16;
			len = data & LUTBUS_LUT_SIZE_MASK;
			SDE_DEBUG("%05X: %08X %08X\tLUT WRITE:\n", offset, opcode, data);
			p+=2;
			for (i = 0; i < len*wrap; i++) {
				if (i % 16 == 0) {
					if (i)
						SDE_DEBUG("%s\n", str);
					pstr = str;
					pstr += snprintf(pstr, sizeof(str), "\t");
				}
				pstr += snprintf(pstr, (int)(end - pstr), " %8.8X", *p);
				p++;
			}
			SDE_DEBUG("%s\n", str);
			size -= 2 + len * wrap * 4;
			break;
		default:
			SDE_DEBUG("UNKNOWN\n");
			p++;
			size --;
			break;
		}
	}

	if (cfg->last_command)
		SDE_DEBUG("LAST command\n");
	SDE_DEBUG("===============================\n");
}

inline u32 reg_dma_readback(u32 addr, bool abs_addr, struct sde_hw_ctl *ctl,
		struct sde_hw_vatran *vatran)
{
	struct sde_hw_blk_reg_map hw;

	/* TODO: readback for relative address */
	if (!abs_addr)
		return 0;

	if (addr >= vatran->caps->base_off + vatran->caps->len) {
		// Invalid address
		return 0;
	} else if (addr >= vatran->caps->base_off) {
		return SDE_REG_READ(&vatran->hw, addr - vatran->caps->base_off);
	} else if (addr >= reg_dma[ctl->dpu_idx]->caps->base_off) {
		SET_UP_REG_DMA_VQ_REG(hw, reg_dma[ctl->dpu_idx], REG_DMA_TYPE_DB, 1);
		hw.blk_off = 0;
		return SDE_REG_READ(&hw, addr);
	} else {
		memcpy(&hw, &ctl->hw, sizeof(hw));
		hw.blk_off = 0;
		return SDE_REG_READ(&hw, addr);
	}
}

void reg_dma_readback_payload(struct sde_reg_dma_kickoff_cfg *cfg)
{
	u32 *p = cfg->dma_buf->vaddr;
	int size = cfg->dma_buf->index / sizeof(u32);
	u32 addr, mask, len, wrap, inc, tbl, blk, data;
	bool abs_addr;
	int i, j;
	char str[1024], *pstr, *end = str + sizeof(str) - 1;
	u32 dec_sel = 0;
	struct sde_hw_vatran *vatran;

	vatran = sde_hw_get_vatran(cfg->ctl->dpu_idx);

	SDE_DEBUG("VQ%d CTL%d payload readback sz 0x%X  %pK\n",
			cfg->dma_buf->vq_idx, cfg->ctl->idx, size, p);
	SDE_DEBUG("===============================\n");
	while (size) {
		switch (*p & OPCODE_MASK) {
		case NO_OP_OPCODE:
			SDE_DEBUG("NO-OP\n");
			p++;
			size --;
			break;
		case SINGLE_REG_WRITE_OPCODE:
			addr = *p & ADDR_MASK;
			abs_addr = (*p & REL_ADDR_OPCODE) ? true : false;
			if (*p == reg_dma_decode_sel)
				dec_sel = p[1];
			p++;
			data = reg_dma_readback(addr, abs_addr, cfg->ctl, vatran);
			SDE_DEBUG("WRITE%s @0x%6.6X %8.8X : [%8.8X]%s\n",
					abs_addr ? "_ABS" : "_REL", addr, *p, data,
					(*p == data) ? "" : " ***");
			p++;
			size -= 2;
			break;
		case SINGLE_REG_MODIFY_OPCODE:
			addr = *p & ADDR_MASK;
			abs_addr = (*p & REL_ADDR_OPCODE) ? true : false;
			p++;
			mask = *p++;
			data = reg_dma_readback(addr, abs_addr, cfg->ctl, vatran);
			SDE_DEBUG("WRITE%s @0x%6.6X  mask %8.8X  val %8.8X : [%8.8X]%s\n",
					abs_addr ? "_ABS" : "_REL", addr, ~mask, *p, data,
					(*p == (data & ~mask)) ? "" : " ***");
			p++;
			size -= 3;
			break;
		case HW_INDEX_REG_WRITE_OPCODE:
			addr = *p & ADDR_MASK;
			abs_addr = (*p & REL_ADDR_OPCODE) ? true : false;
			p++;
			len = *p++;
			SDE_DEBUG("WRITE%s @0x%6.6X  len %d:\n",
					abs_addr ? "_ABS" : "_REL", addr, len);
			for (i = 0; i < len; i++) {
				if (i % 16 == 0) {
					if (i)
						SDE_DEBUG("%s\n", str);
					pstr = str;
					pstr += snprintf(pstr, sizeof(str), "\t");
				}
				data = reg_dma_readback(addr, abs_addr, cfg->ctl, vatran);
				pstr += snprintf(pstr, (int)(end - pstr), " %8.8X : [%8.8X]%s",
						*p, data, (*p == data) ? "" : " ***");
				p++;
			}
			SDE_DEBUG("%s\n", str);
			size -= 2 + len;
			break;
		case AUTO_INC_REG_WRITE_OPCODE:
			addr = *p & ADDR_MASK;
			abs_addr = (*p & REL_ADDR_OPCODE) ? true : false;
			p++;
			len = *p++;
			SDE_DEBUG("WRITE%s @0x%6.6X++  len %d:\n",
					abs_addr ? "_ABS" : "_REL", addr, len);
			for (i = 0; i < len; i++) {
				if (i % 16 == 0) {
					if (i)
						SDE_DEBUG("%s\n", str);
					pstr = str;
					pstr += snprintf(pstr, sizeof(str), "\t");
				}
				data = reg_dma_readback(addr, abs_addr, cfg->ctl, vatran);
				pstr += snprintf(pstr, (int)(end - pstr), " %8.8X : [%8.8X]%s",
						*p, data, (*p == data) ? "" : " ***");
				addr += 4;
				p++;
			}
			SDE_DEBUG("%s\n", str);
			size -= 2 + len;
			break;
		case BLK_REG_WRITE_OPCODE:
			addr = *p & ADDR_MASK;
			abs_addr = (*p & REL_ADDR_OPCODE) ? true : false;
			p++;
			inc = *p & BIT(31);
			wrap = (*p >> 16)  & WRAP_MAX_SIZE;
			len = *p & MAX_DWORDS_SZ;
			p++;
			SDE_DEBUG("WRITE%s @0x%6.6X%s%d  len %d:\n",
					abs_addr ? "_ABS" : "_REL", addr, inc ? "++" : "--", wrap, len);
			if (!inc)
				addr -= wrap * sizeof(u32);
			for (i = 0; i < len; i++) {
				pstr = str;
				pstr += snprintf(pstr, sizeof(str), "\t");
				if (!inc)
					addr += (wrap * 2 - 1) * sizeof(u32);
				for (j = 0; j < wrap; j++) {
					data = reg_dma_readback(addr, abs_addr, cfg->ctl, vatran);
					pstr += snprintf(pstr, (int)(end - pstr), " %8.8X : [%8.8X]%s",
							*p, data, (*p == data) ? "" : " ***");
					addr += inc ? sizeof(u32) : -sizeof(u32);
					p++;
				}
				SDE_DEBUG("%s\n", str);
			}
			size -= 2 + len * wrap;
			break;
		case LUTBUS_WRITE_OPCODE:
			tbl = (*p & LUTBUS_TABLE_SEL_MASK) >> 16;
			blk = *p & LUTBUS_BLOCK_SEL_MASK;
			p++;
			wrap = (*p & LUTBUS_TRANS_SZ_MASK) >> 16;
			len = *p & LUTBUS_LUT_SIZE_MASK;
			p++;
			SDE_DEBUG("WRITE LUT %4.4X TBL %s  TRANS %d  len %d SKIP READ BACK!\n",
					blk, tbl ? "B" : "A", wrap, len);
			p += len * wrap * 4;
			size -= 2 + len * wrap * 4;
			break;
		default:
			SDE_DEBUG("UNKNOWN\n");
			p++;
			size --;
			break;
		}
	}

	if (cfg->last_command)
		SDE_DEBUG("LAST command\n");
	SDE_DEBUG("===============================\n");
}

static void reg_dma_trigger_v4(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw)
{
	u32 mask, dpu_idx = cfg->dma_buf->dpu_idx;

	mask = ctl_trigger_done_mask[dpu_idx][cfg->ctl->idx]
		[cfg->ctl->display_idx][cfg->queue_select];
	SDE_REG_WRITE(hw, reg_dma_intr_0_clear_offset[dpu_idx][cfg->ctl->idx][cfg->queue_select],
				mask);
	/* DB LUTDMA use SW trigger while SB LUTDMA uses DSPP_SB
	 * flush as its trigger event.
	 */
	if (cfg->dma_type == REG_DMA_TYPE_DB) {
		SDE_REG_WRITE(&cfg->ctl->hw, reg_dma_ctl_trigger_offset,
				ctl_trigger_done_mask[dpu_idx][cfg->ctl->idx][cfg->ctl->display_idx]
							[DMA_CTL_QUEUE0]);
	}

	SDE_DEBUG("tigger CTL%d VQ%d %s %s  DPU%d\n", cfg->ctl->idx,
			ctl_trigger_done_mask[dpu_idx][cfg->ctl->idx]
			[cfg->ctl->display_idx][DMA_CTL_QUEUE0],
			(cfg->dma_type == REG_DMA_TYPE_DB) ? "DB" : "SB",
			cfg->queue_select ? "Q0" : "Q1",
			cfg->dma_buf->dpu_idx);
	if (SDE_DBG_MASK_REGDMA & sde_hw_util_log_mask)
		reg_dma_dump_payload(cfg, 0, 0);
}

static void reg_dma_submit_queue_v1_to_v3(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw, u32 cmd)
{
	u32 dpu_idx = cfg->dma_buf->dpu_idx;

	if (cfg->dma_type == REG_DMA_TYPE_DB) {
		SDE_REG_WRITE(hw, reg_dma_ctl_queue_off[dpu_idx][cfg->ctl->idx],
				cfg->dma_buf->iova);
		SDE_REG_WRITE(hw, reg_dma_ctl_queue_off[dpu_idx][cfg->ctl->idx] + 0x4,
				cmd);
	} else if (cfg->dma_type == REG_DMA_TYPE_SB) {
		SDE_REG_WRITE(hw, reg_dma_ctl_queue1_off[dpu_idx][cfg->ctl->idx],
				cfg->dma_buf->iova);
		SDE_REG_WRITE(hw, reg_dma_ctl_queue1_off[dpu_idx][cfg->ctl->idx] + 0x4,
				cmd);
	}
}

static void reg_dma_submit_queue_v4(struct sde_reg_dma_kickoff_cfg *cfg,
					struct sde_hw_blk_reg_map *hw, u32 cmd)
{
	u32 dpu_idx = cfg->dma_buf->dpu_idx;

	SDE_REG_WRITE_CPU(hw, reg_dma_ctl_queue_off[dpu_idx][cfg->ctl->idx],
			cfg->dma_buf->iova);
	SDE_REG_WRITE_CPU(hw, reg_dma_ctl_queue_off[dpu_idx][cfg->ctl->idx] + 0x4,
			cmd);
}

int init_v1(struct sde_hw_reg_dma *cfg, u32 dpu_idx)
{
	int i = 0, rc = 0;

	if (!cfg || (dpu_idx >= DPU_MAX))
		return -EINVAL;

	reg_dma[dpu_idx] = cfg;
	for (i = CTL_0; i < CTL_MAX; i++) {
		if (!last_cmd_buf_db[i][dpu_idx]) {
			last_cmd_buf_db[i][dpu_idx] =
			    alloc_reg_dma_buf_v1(REG_DMA_HEADERS_BUFFER_SZ, dpu_idx);
			if (IS_ERR_OR_NULL(last_cmd_buf_db[i][dpu_idx])) {
				/*
				 * This will allow reg dma to fall back to
				 * AHB domain
				 */
				pr_info("Failed to allocate reg dma, ret:%lu\n",
						PTR_ERR(last_cmd_buf_db[i][dpu_idx]));
				return 0;
			}
		}
		if (!last_cmd_buf_sb[i][dpu_idx]) {
			last_cmd_buf_sb[i][dpu_idx] =
			    alloc_reg_dma_buf_v1(REG_DMA_HEADERS_BUFFER_SZ, dpu_idx);
			if (IS_ERR_OR_NULL(last_cmd_buf_sb[i][dpu_idx])) {
				/*
				 * This will allow reg dma to fall back to
				 * AHB domain
				 */
				pr_info("Failed to allocate reg dma, ret:%lu\n",
						PTR_ERR(last_cmd_buf_sb[i][dpu_idx]));
				return 0;
			}
		}
	}
	if (rc) {
		for (i = 0; i < CTL_MAX; i++) {
			if (!last_cmd_buf_db[i][dpu_idx])
				continue;
			dealloc_reg_dma_v1(last_cmd_buf_db[i][dpu_idx], dpu_idx);
			last_cmd_buf_db[i][dpu_idx] = NULL;
		}
		for (i = 0; i < CTL_MAX; i++) {
			if (!last_cmd_buf_sb[i][dpu_idx])
				continue;
			dealloc_reg_dma_v1(last_cmd_buf_sb[i][dpu_idx], dpu_idx);
			last_cmd_buf_sb[i][dpu_idx] = NULL;
		}
		return rc;
	}

	reg_dma[dpu_idx]->ops.check_support = check_support_v1;
	reg_dma[dpu_idx]->ops.setup_payload = setup_payload_v1;
	reg_dma[dpu_idx]->ops.kick_off = kick_off_v1;
	reg_dma[dpu_idx]->ops.reset = reset_v1;
	reg_dma[dpu_idx]->ops.alloc_reg_dma_buf = alloc_reg_dma_buf_v1;
	reg_dma[dpu_idx]->ops.dealloc_reg_dma = dealloc_reg_dma_v1;
	reg_dma[dpu_idx]->ops.reset_reg_dma_buf = reset_reg_dma_buffer_v1;
	reg_dma[dpu_idx]->ops.last_command = last_cmd_v1;
	reg_dma[dpu_idx]->ops.dump_regs = dump_regs_v1;
	reg_dma[dpu_idx]->ops.select_queue_sb = reg_dma_select_queue_sb_v1_to_3;

	reg_dma_register_count = 60;
	reg_dma_decode_sel = 0x180ac060;
	reg_dma_opmode_offset = 0x4;
	reg_dma_ctl0_queue0_cmd0_offset = 0x14;
	reg_dma_intr_4_status_offset = 0xa0;
	reg_dma_ctl_trigger_offset = 0xd4;
	reg_dma_error_clear_mask = BIT(0) | BIT(1) | BIT(2) | BIT(16);
	reg_dma_intr_4_clear_offset = 0xc0;

	for (i = 0; i < CTL_MAX; i++) {
		reg_dma_intr_0_status_offset[dpu_idx][i][DMA_CTL_QUEUE0] = 0x90;
		reg_dma_intr_0_status_offset[dpu_idx][i][DMA_CTL_QUEUE1] = 0x90;
		reg_dma_intr_0_clear_offset[dpu_idx][i][DMA_CTL_QUEUE0] = 0xb0;
		reg_dma_intr_0_clear_offset[dpu_idx][i][DMA_CTL_QUEUE1] = 0xb0;
		reg_dma_ctl0_reset_offset[dpu_idx][i][DMA_CTL_QUEUE0] = 0xe4 + i * 4;
		reg_dma_ctl0_reset_offset[dpu_idx][i][DMA_CTL_QUEUE1] = 0xe4 + i * 4;
	}

	reg_dma_ctl_queue_off[dpu_idx][CTL_0] = reg_dma_ctl0_queue0_cmd0_offset;
	for (i = CTL_1; i < ARRAY_SIZE(reg_dma_ctl_queue_off[dpu_idx]); i++)
		reg_dma_ctl_queue_off[dpu_idx][i] = reg_dma_ctl_queue_off[dpu_idx][i - 1] +
			(sizeof(u32) * 4);
	validate_queue_func = validate_queue_type_v1_to_3;
	reg_dma_submit_payload = reg_dma_submit_queue_v1_to_v3;
	read_clear_reg_dma_status = reg_dma_read_clear_status_v1_to_v3;
	trigger_reg_dma = reg_dma_trigger_v1_to_v3;
	return 0;
}

int init_v11(struct sde_hw_reg_dma *cfg, u32 dpu_idx)
{
	int ret = 0, i = 0;

	ret = init_v1(cfg, dpu_idx);
	if (ret) {
		DRM_ERROR("failed to initialize v1: ret %d\n", ret);
		return -EINVAL;
	}

	/* initialize register offsets and v1_supported based on version */
	reg_dma_register_count = 133;
	reg_dma_decode_sel = 0x180ac114;
	reg_dma_opmode_offset = 0x4;
	reg_dma_ctl0_queue0_cmd0_offset = 0x14;
	reg_dma_intr_4_status_offset = 0x170;
	reg_dma_ctl_trigger_offset = 0xd4;
	reg_dma_intr_4_clear_offset = 0x1b0;
	reg_dma_error_clear_mask = BIT(0) | BIT(1) | BIT(2) | BIT(16) |
		BIT(17) | BIT(18);

	reg_dma_ctl_queue_off[dpu_idx][CTL_0] = reg_dma_ctl0_queue0_cmd0_offset;
	for (i = CTL_1; i < ARRAY_SIZE(reg_dma_ctl_queue_off[dpu_idx]); i++)
		reg_dma_ctl_queue_off[dpu_idx][i] = reg_dma_ctl_queue_off[dpu_idx][i - 1] +
			(sizeof(u32) * 4);
	for (i = 0; i < CTL_MAX; i++) {
		reg_dma_intr_0_status_offset[dpu_idx][i][DMA_CTL_QUEUE0] = 0x160;
		reg_dma_intr_0_status_offset[dpu_idx][i][DMA_CTL_QUEUE1] = 0x160;
		reg_dma_intr_0_clear_offset[dpu_idx][i][DMA_CTL_QUEUE0] = 0x1a0;
		reg_dma_intr_0_clear_offset[dpu_idx][i][DMA_CTL_QUEUE1] = 0x1a0;
		reg_dma_ctl0_reset_offset[dpu_idx][i][DMA_CTL_QUEUE0] = 0x200 + i * 4;
		reg_dma_ctl0_reset_offset[dpu_idx][i][DMA_CTL_QUEUE1] = 0x200 + i * 4;
	}

	v1_supported[IGC] = DSPP_IGC | GRP_DSPP_HW_BLK_SELECT |
				GRP_VIG_HW_BLK_SELECT | GRP_DMA_HW_BLK_SELECT;
	v1_supported[GC] = GRP_DMA_HW_BLK_SELECT | GRP_DSPP_HW_BLK_SELECT;
	v1_supported[HSIC] = GRP_DSPP_HW_BLK_SELECT;
	v1_supported[SIX_ZONE] = GRP_DSPP_HW_BLK_SELECT;
	v1_supported[MEMC_SKIN] = GRP_DSPP_HW_BLK_SELECT;
	v1_supported[MEMC_SKY] = GRP_DSPP_HW_BLK_SELECT;
	v1_supported[MEMC_FOLIAGE] = GRP_DSPP_HW_BLK_SELECT;
	v1_supported[MEMC_PROT] = GRP_DSPP_HW_BLK_SELECT;
	v1_supported[QSEED] = GRP_VIG_HW_BLK_SELECT;

	return 0;
}

int init_v12(struct sde_hw_reg_dma *cfg, u32 dpu_idx)
{
	int ret = 0;

	ret = init_v11(cfg, dpu_idx);
	if (ret) {
		DRM_ERROR("failed to initialize v11: ret %d\n", ret);
		return ret;
	}

	v1_supported[LTM_INIT] = GRP_LTM_HW_BLK_SELECT;
	v1_supported[LTM_ROI] = GRP_LTM_HW_BLK_SELECT;
	v1_supported[LTM_VLUT] = GRP_LTM_HW_BLK_SELECT;
	v1_supported[RC_MASK_CFG] = (GRP_DSPP_HW_BLK_SELECT |
			GRP_MDSS_HW_BLK_SELECT);
	v1_supported[RC_PU_CFG] = (GRP_DSPP_HW_BLK_SELECT |
			GRP_MDSS_HW_BLK_SELECT);
	v1_supported[SPR_INIT] = (GRP_DSPP_HW_BLK_SELECT |
			GRP_MDSS_HW_BLK_SELECT);
	v1_supported[SPR_UDC] = (GRP_DSPP_HW_BLK_SELECT |
			GRP_MDSS_HW_BLK_SELECT);
	v1_supported[SPR_PU_CFG] = (GRP_DSPP_HW_BLK_SELECT |
			GRP_MDSS_HW_BLK_SELECT);
	v1_supported[DEMURA_CFG] = MDSS | DSPP0 | DSPP1;
	v1_supported[DEMURA_CFG0_PARAM2] = MDSS | DSPP0 | DSPP1;

	return 0;
}

static int init_reg_dma_vbif(struct sde_hw_reg_dma *cfg)
{
	int ret = 0;
	struct sde_hw_blk_reg_map *hw;
	struct sde_vbif_clk_client clk_client;
	struct msm_drm_private *priv = cfg->drm_dev->dev_private;
	struct msm_kms *kms = priv->kms;
	struct sde_kms *sde_kms = to_sde_kms(kms);

	if (cfg->caps->clk_ctrl != SDE_CLK_CTRL_LUTDMA) {
		SDE_ERROR("invalid lutdma clk ctrl type %d\n", cfg->caps->clk_ctrl);
		return -EINVAL;
	}

	hw = kzalloc(sizeof(*hw), GFP_KERNEL);
	if (!hw) {
		SDE_ERROR("failed to create hw block\n");
		return -ENOMEM;
	}

	hw->base_off = cfg->addr;
	hw->blk_off = cfg->caps->reg_dma_blks[REG_DMA_TYPE_DB].base;

	clk_client.hw = hw;
	clk_client.clk_ctrl = cfg->caps->clk_ctrl;
	clk_client.ops.setup_clk_force_ctrl = setup_clk_force_ctrl;

	ret = sde_vbif_clk_register(sde_kms, &clk_client);
	if (ret) {
		SDE_ERROR("failed to register vbif client %d\n", cfg->caps->clk_ctrl);
		kfree(hw);
	}

	return ret;
}

#define BASE_REG_SIZE 0x400
int init_v2(struct sde_hw_reg_dma *cfg, u32 dpu_idx)
{
	int ret = 0, i = 0;

	ret = init_v12(cfg, dpu_idx);
	if (ret) {
		DRM_ERROR("failed to initialize v12: ret %d\n", ret);
		return ret;
	}

	/* initialize register offsets based on version delta */
	reg_dma_register_count = 0x91;
	reg_dma_ctl0_queue1_cmd0_offset = 0x1c;
	reg_dma_error_clear_mask |= BIT(19);

	reg_dma_ctl_queue1_off[dpu_idx][CTL_0] = reg_dma_ctl0_queue1_cmd0_offset;
	for (i = CTL_1; i < ARRAY_SIZE(reg_dma_ctl_queue_off[dpu_idx]); i++)
		reg_dma_ctl_queue1_off[dpu_idx][i] = reg_dma_ctl_queue1_off[dpu_idx][i - 1] +
				(sizeof(u32) * 4);

	v1_supported[IGC] = GRP_DSPP_HW_BLK_SELECT | GRP_VIG_HW_BLK_SELECT |
			GRP_DMA_HW_BLK_SELECT;
	if (cfg->caps->reg_dma_blks[REG_DMA_TYPE_SB].valid == true) {
		char name[20];
		uint32_t base = cfg->caps->reg_dma_blks[REG_DMA_TYPE_SB].base;

		snprintf(name, sizeof(name), "REG_DMA_SB");
		sde_dbg_reg_register_dump_range(LUTDMA_DBG_NAME, name, base,
				base + BASE_REG_SIZE, cfg->caps->xin_id);
		reg_dma[dpu_idx]->ops.last_command_sb = last_cmd_sb_v2;
	}

	if (cfg->caps->reg_dma_blks[REG_DMA_TYPE_DB].valid == true) {
		char name[20];
		uint32_t base = cfg->caps->reg_dma_blks[REG_DMA_TYPE_DB].base;

		snprintf(name, sizeof(name), "REG_DMA_DB");
		sde_dbg_reg_register_dump_range(LUTDMA_DBG_NAME, name, base,
				base + BASE_REG_SIZE, cfg->caps->xin_id);
	}

	if (cfg->caps->split_vbif_supported)
		ret = init_reg_dma_vbif(cfg);

	return ret;
}

#define CTL_REG_SIZE 0x80
int init_v3(struct sde_hw_reg_dma *cfg, u32 dpu_idx)
{
	char name[20];
	int ret = 0, i;

	ret = init_v2(cfg, dpu_idx);
	if (ret) {
		DRM_ERROR("failed to initialize v12: ret %d\n", ret);
		return ret;
	}
	reg_dma_register_count = 0x7000;
	reg_dma_decode_sel = 0x18180114;
	reg_dma_ctl0_queue0_cmd0_offset = 0x1000;
	reg_dma_ctl0_queue1_cmd0_offset = 0x1000;

	for (i = CTL_0; i < ARRAY_SIZE(reg_dma_ctl_queue_off[dpu_idx]); i++) {
		reg_dma_ctl_queue_off[dpu_idx][i] = reg_dma_ctl0_queue0_cmd0_offset * i;
		reg_dma_ctl_queue1_off[dpu_idx][i] = reg_dma_ctl0_queue1_cmd0_offset * i + 8;
	}

	/* Register DBG DUMP RANGES - CTL paths are 0x80 in size */
	if (cfg->caps->reg_dma_blks[REG_DMA_TYPE_DB].valid) {
		for (i = CTL_0; i < ARRAY_SIZE(reg_dma_ctl_queue_off[dpu_idx]); i++) {
			u32 base = cfg->caps->reg_dma_blks[REG_DMA_TYPE_DB].base +
					reg_dma_ctl_queue_off[dpu_idx][i];

			snprintf(name, sizeof(name), "REG_DMA_DB_CTL%d", i);
			sde_dbg_reg_register_dump_range(LUTDMA_DBG_NAME, name, base,
					base + CTL_REG_SIZE, cfg->caps->xin_id);
		}
	}

	if (cfg->caps->reg_dma_blks[REG_DMA_TYPE_SB].valid) {
		for (i = CTL_0; i < ARRAY_SIZE(reg_dma_ctl_queue_off[dpu_idx]); i++) {
			u32 base = cfg->caps->reg_dma_blks[REG_DMA_TYPE_SB].base +
					reg_dma_ctl_queue_off[dpu_idx][i];

			snprintf(name, sizeof(name), "REG_DMA_SB_CTL%d", i);
			sde_dbg_reg_register_dump_range(LUTDMA_DBG_NAME, name, base,
					base + CTL_REG_SIZE, cfg->caps->xin_id);
		}
	}

	for (i = CTL_0; i < CTL_MAX; i++) {
		ctl_trigger_done_mask[dpu_idx][i][0][DMA_CTL_QUEUE0] = BIT(3);
		ctl_trigger_done_mask[dpu_idx][i][1][DMA_CTL_QUEUE0] = BIT(3);
		ctl_trigger_done_mask[dpu_idx][i][2][DMA_CTL_QUEUE0] = BIT(3);
		ctl_trigger_done_mask[dpu_idx][i][3][DMA_CTL_QUEUE0] = BIT(3);
		ctl_trigger_done_mask[dpu_idx][i][0][DMA_CTL_QUEUE1] = BIT(4);
		ctl_trigger_done_mask[dpu_idx][i][1][DMA_CTL_QUEUE1] = BIT(4);
		ctl_trigger_done_mask[dpu_idx][i][2][DMA_CTL_QUEUE1] = BIT(4);
		ctl_trigger_done_mask[dpu_idx][i][3][DMA_CTL_QUEUE1] = BIT(4);
		reg_dma_intr_0_status_offset[dpu_idx][i][DMA_CTL_QUEUE0] = 4096 * i + 0x44;
		reg_dma_intr_0_status_offset[dpu_idx][i][DMA_CTL_QUEUE1] = 4096 * i + 0x44;
		reg_dma_intr_0_clear_offset[dpu_idx][i][DMA_CTL_QUEUE0] =
			reg_dma_intr_0_status_offset[dpu_idx][i][DMA_CTL_QUEUE0] + 4;
		reg_dma_intr_0_clear_offset[dpu_idx][i][DMA_CTL_QUEUE1] =
			reg_dma_intr_0_status_offset[dpu_idx][i][DMA_CTL_QUEUE1] + 4;
		reg_dma_ctl0_reset_offset[dpu_idx][i][DMA_CTL_QUEUE0] = 4096 * i + 0x54;
		reg_dma_ctl0_reset_offset[dpu_idx][i][DMA_CTL_QUEUE1] = 4096 * i + 0x54;
	}

	v1_supported[DEMURA_CFG] = v1_supported[DEMURA_CFG] | DSPP2 | DSPP3;
	v1_supported[DEMURA_CFG0_PARAM2] = v1_supported[DEMURA_CFG0_PARAM2] | DSPP2 | DSPP3;
	v1_supported[AIQE_MDNIE] = MDSS | DSPP0 | DSPP2;
	v1_supported[AIQE_SSRC_CONFIG] = MDSS | DSPP0 | DSPP2;
	v1_supported[AIQE_SSRC_DATA] = MDSS | DSPP0 | DSPP2;
	return 0;
}

#define CTL_REG_SIZE_V4 0x68
int init_v4(struct sde_hw_reg_dma *reg_dma, u32 dpu_idx, struct sde_mdss_cfg *m)
{
	char name[20];
	int rc, i, j, k;
	int ctl_idx, vq_idx, display_idx;
	struct sde_reg_dma_cfg *cfg = (struct sde_reg_dma_cfg *)reg_dma->caps;
	struct sde_reg_dma_buffer *reg_dma_buf;
	u32 off_db, off_sb;

	rc = init_v3(reg_dma, dpu_idx);
	if (rc) {
		DRM_ERROR("failed to initialize v3: ret %d\n", rc);
		return rc;
	}

	reg_dma->ops.kick_off = kick_off_v4_dummy;
	reg_dma->ops.reset = reset_v4;
	reg_dma->ops.last_command = last_cmd_v4;
	reg_dma->ops.last_command_sb = last_cmd_sb_v4_dummy;
	reg_dma->ops.get_reg_dma_vq_buf = get_reg_dma_vq_buf_v4;
	reg_dma->ops.reset_reg_dma_buf = reset_reg_dma_buffer_v4;
	reg_dma->ops.check_engine_status = check_engine_status_v4;

	reg_dma_register_count = CTL_REG_SIZE_V4;
	reg_dma_register_count = 0x10880;
	reg_dma_decode_sel = 0x18180114;

	reg_dma_opmode_offset = 0x4;
	reg_dma_ctl0_queue0_cmd0_offset = 0x0;
	reg_dma_intr_4_status_offset = 0xa0;
	reg_dma_ctl_trigger_offset = 0x900;
	reg_dma_error_clear_mask = BIT(0) | BIT(1) | BIT(2) | BIT(16) | BIT(17) | BIT(18) | BIT(19);
	reg_dma_intr_4_clear_offset = 0xc0;

	memset(vq_reg_dma_bufs[dpu_idx], 0, sizeof(vq_reg_dma_bufs[dpu_idx]));
	for (i = 0; i < m->ctl_count; i ++) {
		ctl_idx = m->ctl[i].id;
		display_idx = m->ctl[i].display_idx;
		vq_idx = m->ctl[i].vq_idx;
		off_db = m->dma_cfg.reg_dma_vq_blks[vq_idx][REG_DMA_TYPE_DB].base;
		off_sb = m->dma_cfg.reg_dma_vq_blks[vq_idx][REG_DMA_TYPE_SB].base;
		DRM_DEBUG("[%d] Config LUTDMA for dpu %d  ctl %d  VQ %d  base %X %X\n", i,
				dpu_idx, ctl_idx, vq_idx, off_db, off_sb);
		reg_dma_ctl_to_vq_map[dpu_idx][ctl_idx][display_idx] = vq_idx;
		reg_dma_ctl_queue_off[dpu_idx][ctl_idx] = 0x0;
		reg_dma_ctl_queue1_off[dpu_idx][ctl_idx] = 0x0;

		ctl_trigger_done_mask[dpu_idx][ctl_idx][display_idx][DMA_CTL_QUEUE0] =
					BIT(vq_idx - REG_DMA_VQ_0);
		reg_dma_intr_0_enable_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_DB] = 0x40;
		reg_dma_intr_0_enable_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_SB] = 0x40;
		reg_dma_intr_0_status_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_DB] = 0x44;
		reg_dma_intr_0_status_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_SB] = 0x44;
		reg_dma_intr_0_clear_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_DB] = 0x48;
		reg_dma_intr_0_clear_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_SB] = 0x48;
		reg_dma_ctl0_reset_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_DB] = 0x54;
		reg_dma_ctl0_reset_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_SB] = 0x54;
		reg_dma_ctl0_busy_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_DB] = 0x10;
		reg_dma_ctl0_busy_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_SB] = 0x10;

		reg_dma_intr_5_enable_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_DB] = 0x58;
		reg_dma_intr_5_enable_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_SB] = 0x58;
		reg_dma_intr_5_clear_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_DB] = 0x5c;
		reg_dma_intr_5_clear_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_SB] = 0x5c;
		reg_dma_intr_5_status_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_DB] = 0x60;
		reg_dma_intr_5_status_offset[dpu_idx][ctl_idx][REG_DMA_TYPE_SB] = 0x60;

		/* Allocate buffers for MDSS/LUT DB/SB */
		for (j = 0; j < REG_DMA_PAYLOAD_BUF_MAX; j++) {
			for (k = 0; k < NUM_BUFFERS; k++) {
				reg_dma_buf = reg_dma->ops.alloc_reg_dma_buf(vq_buf_size[j], dpu_idx);
				if (IS_ERR_OR_NULL(reg_dma_buf)) {
					DRM_ERROR("Failed to allocate buffer for VQ dpu %d  idx %d  vq %d  type %d\n",
							dpu_idx, k, vq_idx, j);
					return -EINVAL;
				}
				reg_dma_buf->buffer_type = j;
				reg_dma_buf->dpu_idx = dpu_idx;
				reg_dma_buf->vq_idx = vq_idx;
				DRM_DEBUG("Allocated buffer for VQ dpu %d  idx %d  vq %d  type %d  sz %X  %pK  buf 0x%llX\n",
						dpu_idx, k, vq_idx, j, vq_buf_size[j],
						reg_dma_buf, reg_dma_buf->iova);
				vq_reg_dma_bufs[dpu_idx][k][vq_idx][j] = reg_dma_buf;
			}
		}
	}

	if (cfg->reg_dma_blks[REG_DMA_TYPE_DB].valid && reg_dma->vm_based_queue) {
		for (i = 0; i < REG_DMA_VQ_MAX; i++) {
			u32 base = cfg->reg_dma_blks[REG_DMA_TYPE_DB].base +
					reg_dma_ctl0_queue0_cmd0_offset * (i + 1);
			cfg->reg_dma_vq_blks[i][REG_DMA_TYPE_DB].valid = true;
			cfg->reg_dma_vq_blks[i][REG_DMA_TYPE_DB].base = base;
			cfg->reg_dma_vq_blks[i][REG_DMA_TYPE_DB].features =
					cfg->reg_dma_blks[REG_DMA_TYPE_DB].features;
			snprintf(name, sizeof(name), "REG_DMA_DB_VQ%d-%d", dpu_idx, i - 1);
			sde_dbg_reg_register_dump_range(LUTDMA_DBG_NAME, name, base,
					base + CTL_REG_SIZE, cfg->xin_id);
		}
	}

	if (cfg->reg_dma_blks[REG_DMA_TYPE_SB].valid && reg_dma->vm_based_queue) {
		for (i = 0; i < REG_DMA_VQ_MAX; i++) {
			u32 base = cfg->reg_dma_blks[REG_DMA_TYPE_SB].base +
					reg_dma_ctl0_queue1_cmd0_offset * (i + 1);
			cfg->reg_dma_vq_blks[i][REG_DMA_TYPE_SB].valid = true;
			cfg->reg_dma_vq_blks[i][REG_DMA_TYPE_SB].base = base;
			cfg->reg_dma_vq_blks[i][REG_DMA_TYPE_SB].features =
					cfg->reg_dma_blks[REG_DMA_TYPE_SB].features;
			snprintf(name, sizeof(name), "REG_DMA_SB_VQ%d-%d", dpu_idx, i - 1);
			sde_dbg_reg_register_dump_range(LUTDMA_DBG_NAME, name, base,
					base + CTL_REG_SIZE, cfg->xin_id);
		}
	}

	validate_queue_func = validate_queue_type_v4;
	reg_dma->ops.select_queue_sb = reg_dma_select_queue_sb_v4;
	reg_dma_ctl_trigger_offset = 0x900;
	read_clear_reg_dma_status = reg_dma_read_clear_status_v4;
	trigger_reg_dma = reg_dma_trigger_v4;
	reg_dma_submit_payload = reg_dma_submit_queue_v4;

	v1_supported[MDSS_REG] = MDSS;

	return 0;
}

static int check_support_v1(enum sde_reg_dma_features feature,
		     enum sde_reg_dma_blk blk,
		     bool *is_supported)
{
	int ret = 0;

	if (!is_supported)
		return -EINVAL;

	if (feature >= REG_DMA_FEATURES_MAX
		|| blk >= BIT_ULL(REG_DMA_BLK_MAX)) {
		*is_supported = false;
		return ret;
	}

	*is_supported = (blk & v1_supported[feature]) ? true : false;
	return ret;
}

static int setup_payload_v1(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	int rc = 0;

	rc = validate_dma_cfg(cfg);

	if (!rc)
		rc = validate_dma_op_params[cfg->ops](cfg);

	if (!rc)
		rc = write_dma_op_params[cfg->ops](cfg);

	return rc;
}


static int kick_off_v1(struct sde_reg_dma_kickoff_cfg *cfg, u32 dpu_idx)
{
	int rc = 0;

	rc = validate_kick_off_v1(cfg, dpu_idx);
	if (rc)
		return rc;

	rc = write_kick_off_v1(cfg, dpu_idx);
	return rc;
}

int reset_v1(struct sde_hw_ctl *ctl)
{
	struct sde_hw_blk_reg_map hw;
	u32 val, i = 0, k = 0, dpu_idx = ctl->dpu_idx;

	if (!ctl || ctl->idx >= CTL_MAX) {
		DRM_ERROR("invalid ctl %pK ctl idx %d\n",
			ctl, ((ctl) ? ctl->idx : 0));
		return -EINVAL;
	}

	if (dpu_idx >= DPU_MAX) {
		DRM_ERROR("invalid dpu idx %d\n", dpu_idx);
		return -EINVAL;
	}

	for (k = 0; k < REG_DMA_TYPE_MAX; k++) {
		memset(&hw, 0, sizeof(hw));
		SET_UP_REG_DMA_REG(hw, reg_dma[dpu_idx], k);
		if (hw.hw_rev == 0)
			continue;

		SDE_REG_WRITE(&hw, reg_dma_opmode_offset, BIT(0));
		SDE_REG_WRITE(&hw, reg_dma_ctl0_reset_offset[dpu_idx][ctl->idx][k], BIT(0));

		i = 0;
		do {
			udelay(1000);
			i++;
			val = SDE_REG_READ(&hw, reg_dma_ctl0_reset_offset[dpu_idx][ctl->idx][k]);
		} while (i < 2 && val);
	}

	return 0;
}

static void sde_reg_dma_aspace_cb_locked(void *cb_data, bool is_detach)
{
	struct sde_reg_dma_buffer *dma_buf = NULL;
	struct msm_gem_address_space *aspace = NULL;
	u32 iova_aligned, offset;
	int rc;

	if (!cb_data) {
		DRM_ERROR("aspace cb called with invalid dma_buf\n");
		return;
	}

	dma_buf = (struct sde_reg_dma_buffer *)cb_data;
	aspace = dma_buf->aspace;

	if (is_detach) {
		/* invalidate the stored iova */
		dma_buf->iova = 0;

		/* return the virtual address mapping */
		msm_gem_put_vaddr(dma_buf->buf);
		msm_gem_vunmap(dma_buf->buf, OBJ_LOCK_NORMAL);

	} else {
		rc = msm_gem_get_iova(dma_buf->buf, aspace,
				&dma_buf->iova);
		if (rc) {
			DRM_ERROR("failed to get the iova rc %d\n", rc);
			return;
		}

		dma_buf->vaddr = msm_gem_get_vaddr(dma_buf->buf);
		if (IS_ERR_OR_NULL(dma_buf->vaddr)) {
			DRM_ERROR("failed to get va rc %d\n", rc);
			return;
		}

		iova_aligned = (dma_buf->iova + GUARD_BYTES) & ALIGNED_OFFSET;
		offset = iova_aligned - dma_buf->iova;
		dma_buf->iova = dma_buf->iova + offset;
		dma_buf->vaddr = (void *)(((u8 *)dma_buf->vaddr) + offset);
		dma_buf->next_op_allowed = DECODE_SEL_OP;
	}
}

static struct sde_reg_dma_buffer *alloc_reg_dma_buf_v1(u32 size, u32 dpu_idx)
{
	struct sde_reg_dma_buffer *dma_buf = NULL;
	u32 iova_aligned, offset;
	u32 rsize = size + GUARD_BYTES;
	struct msm_gem_address_space *aspace = NULL;
	int rc = 0;

	if (!size || SIZE_DWORD(size) > MAX_DWORDS_SZ * REG_DMA_BUFFER_MAX_SPLITS) {
		DRM_ERROR("invalid buffer size %lu, max %lu\n",
				SIZE_DWORD(size), MAX_DWORDS_SZ * REG_DMA_BUFFER_MAX_SPLITS);
		return ERR_PTR(-EINVAL);
	}

	if (dpu_idx >= DPU_MAX) {
		DRM_ERROR("invalid dpu idx %d\n", dpu_idx);
		return ERR_PTR(-EINVAL);
	}

	dma_buf = kzalloc(sizeof(*dma_buf), GFP_KERNEL);
	if (!dma_buf)
		return ERR_PTR(-ENOMEM);

	dma_buf->buf = msm_gem_new(reg_dma[dpu_idx]->drm_dev,
				    rsize, MSM_BO_UNCACHED);
	if (IS_ERR_OR_NULL(dma_buf->buf)) {
		rc = -EINVAL;
		goto fail;
	}

	aspace = msm_gem_smmu_address_space_get(reg_dma[dpu_idx]->drm_dev,
			MSM_SMMU_DOMAIN_UNSECURE);

	if (PTR_ERR(aspace) == -ENODEV) {
		aspace = NULL;
		DRM_DEBUG("IOMMU not present, relying on VRAM\n");
	} else if (IS_ERR_OR_NULL(aspace)) {
		rc = PTR_ERR(aspace);
		aspace = NULL;
		DRM_ERROR("failed to get aspace %d", rc);
		goto free_gem;
	} else if (aspace) {
		/* register to aspace */
		rc = msm_gem_address_space_register_cb(aspace,
				sde_reg_dma_aspace_cb_locked,
				(void *)dma_buf);
		if (rc) {
			DRM_ERROR("failed to register callback %d", rc);
			goto free_gem;
		}
	}

	dma_buf->aspace = aspace;
	rc = msm_gem_get_iova(dma_buf->buf, aspace, &dma_buf->iova);
	if (rc) {
		DRM_ERROR("failed to get the iova rc %d\n", rc);
		goto free_aspace_cb;
	}

	dma_buf->vaddr = msm_gem_get_vaddr(dma_buf->buf);
	if (IS_ERR_OR_NULL(dma_buf->vaddr)) {
		DRM_ERROR("failed to get va rc %d\n", rc);
		rc = -EINVAL;
		goto put_iova;
	}

	dma_buf->buffer_size = size;
	iova_aligned = (dma_buf->iova + GUARD_BYTES) & ALIGNED_OFFSET;
	offset = iova_aligned - dma_buf->iova;
	dma_buf->iova = dma_buf->iova + offset;
	dma_buf->vaddr = (void *)(((u8 *)dma_buf->vaddr) + offset);
	dma_buf->next_op_allowed = DECODE_SEL_OP;
	dma_buf->num_splits = 0;
	dma_buf->split_size = 0;

	return dma_buf;

put_iova:
	msm_gem_put_iova(dma_buf->buf, aspace);
free_aspace_cb:
	msm_gem_address_space_unregister_cb(aspace,
			sde_reg_dma_aspace_cb_locked, dma_buf);
free_gem:
	mutex_lock(&reg_dma[dpu_idx]->drm_dev->struct_mutex);
	msm_gem_free_object(dma_buf->buf);
	mutex_unlock(&reg_dma[dpu_idx]->drm_dev->struct_mutex);
fail:
	kfree(dma_buf);
	return ERR_PTR(rc);
}

static int dealloc_reg_dma_v1(struct sde_reg_dma_buffer *dma_buf, u32 dpu_idx)
{
	if (!dma_buf) {
		DRM_ERROR("invalid param reg_buf %pK\n", dma_buf);
		return -EINVAL;
	}

	if (dpu_idx >= DPU_MAX) {
		DRM_ERROR("invalid dpu idx %d\n", dpu_idx);
		return -EINVAL;
	}

	if (dma_buf->buf) {
		msm_gem_put_iova(dma_buf->buf, 0);
		msm_gem_address_space_unregister_cb(dma_buf->aspace,
				sde_reg_dma_aspace_cb_locked, dma_buf);
		mutex_lock(&reg_dma[dpu_idx]->drm_dev->struct_mutex);
		msm_gem_free_object(dma_buf->buf);
		mutex_unlock(&reg_dma[dpu_idx]->drm_dev->struct_mutex);
	}

	kfree(dma_buf);
	return 0;
}

static int reset_reg_dma_buffer_v1(struct sde_reg_dma_buffer *lut_buf)
{
	if (!lut_buf)
		return -EINVAL;

	lut_buf->index = 0;
	lut_buf->ops_completed = 0;
	lut_buf->next_op_allowed = DECODE_SEL_OP;
	lut_buf->abs_write_cnt = 0;
	lut_buf->num_splits = 0;
	lut_buf->split_size = 0;
	return 0;
}

static int validate_last_cmd(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	u32 remain_len, write_len;

	remain_len = BUFFER_SPACE_LEFT(cfg);
	write_len = sizeof(u32);
	if (remain_len < write_len) {
		DRM_ERROR("buffer is full sz %d needs %d bytes\n",
				remain_len, write_len);
		return -EINVAL;
	}
	return 0;
}

static int write_last_cmd(struct sde_reg_dma_setup_ops_cfg *cfg)
{
	u32 *loc = NULL;

	loc =  (u32 *)((u8 *)cfg->dma_buf->vaddr +
			cfg->dma_buf->index);
	loc[0] = reg_dma_decode_sel;
	loc[1] = 0;
	cfg->dma_buf->index += sizeof(u32) * 2;
	cfg->dma_buf->ops_completed = REG_WRITE_OP | DECODE_SEL_OP;
	cfg->dma_buf->next_op_allowed = REG_WRITE_OP;

	return 0;
}

static int last_cmd_v1(struct sde_hw_ctl *ctl, enum sde_reg_dma_queue q,
		enum sde_reg_dma_last_cmd_mode mode)
{
	struct sde_reg_dma_setup_ops_cfg cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_blk_reg_map hw;
	u32 val;
	int rc;

	if (!ctl || ctl->idx >= CTL_MAX || q >= DMA_CTL_QUEUE_MAX) {
		DRM_ERROR("ctl %pK q %d index %d\n", ctl, q,
				((ctl) ? ctl->idx : -1));
		return -EINVAL;
	}

	if (!last_cmd_buf_db[ctl->idx][ctl->dpu_idx]
		|| !last_cmd_buf_db[ctl->idx][ctl->dpu_idx]->iova) {
		DRM_ERROR("invalid last cmd buf for idx %d\n", ctl->idx);
		return -EINVAL;
	}

	if (ctl->dpu_idx >= DPU_MAX) {
		DRM_ERROR("invalid dpu idx %d\n", ctl->dpu_idx);
		return -EINVAL;
	}

	cfg.dma_buf = last_cmd_buf_db[ctl->idx][ctl->dpu_idx];
	reset_reg_dma_buffer_v1(last_cmd_buf_db[ctl->idx][ctl->dpu_idx]);
	if (validate_last_cmd(&cfg)) {
		DRM_ERROR("validate buf failed\n");
		return -EINVAL;
	}

	if (write_last_cmd(&cfg)) {
		DRM_ERROR("write buf failed\n");
		return -EINVAL;
	}

	kick_off.ctl = ctl;
	kick_off.queue_select = q;
	kick_off.trigger_mode = WRITE_IMMEDIATE;
	kick_off.last_command = 1;
	kick_off.op = REG_DMA_WRITE;
	kick_off.dma_type = REG_DMA_TYPE_DB;
	kick_off.dma_buf = last_cmd_buf_db[ctl->idx][ctl->dpu_idx];
	kick_off.feature = REG_DMA_FEATURES_MAX;
	rc = kick_off_v1(&kick_off, ctl->dpu_idx);
	if (rc) {
		DRM_ERROR("kick off last cmd failed\n");
		return rc;
	}

	//Lack of block support will be caught by kick_off
	memset(&hw, 0, sizeof(hw));
	SET_UP_REG_DMA_REG(hw, reg_dma[ctl->dpu_idx], kick_off.dma_type);

	SDE_EVT32(SDE_EVTLOG_FUNC_ENTRY, mode, ctl->idx, kick_off.queue_select,
			kick_off.dma_type, kick_off.op, ctl->dpu_idx);
	if (mode == REG_DMA_WAIT4_COMP) {
		rc = read_poll_timeout(sde_reg_read, val,
				(val & ctl_trigger_done_mask[ctl->dpu_idx]
				[ctl->idx][ctl->display_idx][q]), 10, false, 20000,
				&hw, reg_dma_intr_0_status_offset[ctl->dpu_idx][ctl->idx][q],
				"INTR_0_STATUS");
		if (rc)
			DRM_ERROR("poll wait failed %d val %x mask %x\n",
			    rc, val, ctl_trigger_done_mask[ctl->dpu_idx][ctl->idx]
			    [ctl->display_idx][q]);
		SDE_EVT32(SDE_EVTLOG_FUNC_EXIT, mode, ctl->dpu_idx, rc);
	}

	return rc;
}

void deinit_v1(u32 dpu_idx)
{
	int i = 0;

	for (i = CTL_0; i < CTL_MAX; i++) {
		if (last_cmd_buf_db[i][dpu_idx])
			dealloc_reg_dma_v1(last_cmd_buf_db[i][dpu_idx], dpu_idx);
		last_cmd_buf_db[i][dpu_idx] = NULL;
		if (last_cmd_buf_sb[i][dpu_idx])
			dealloc_reg_dma_v1(last_cmd_buf_sb[i][dpu_idx], dpu_idx);
		last_cmd_buf_sb[i][dpu_idx] = NULL;
	}
}

static void dump_regs_v1(u32 dpu_idx)
{
	uint32_t i = 0, k = 0;
	u32 val;
	struct sde_hw_blk_reg_map hw;

	if (dpu_idx >= DPU_MAX) {
		DRM_ERROR("invalid dpu idx %d\n", dpu_idx);
		return;
	}

	for (k = 0; k < REG_DMA_TYPE_MAX; k++) {
		memset(&hw, 0, sizeof(hw));
		SET_UP_REG_DMA_REG(hw, reg_dma[dpu_idx], k);
		if (hw.hw_rev == 0)
			continue;

		for (i = 0; i < reg_dma_register_count; i++) {
			val = SDE_REG_READ(&hw, i * sizeof(u32));
			DRM_ERROR("offset %x val %x\n", (u32)(i * sizeof(u32)),
					val);
		}
	}

}

static int last_cmd_sb_v2(struct sde_hw_ctl *ctl, enum sde_reg_dma_queue q,
		enum sde_reg_dma_last_cmd_mode mode)
{
	struct sde_reg_dma_setup_ops_cfg cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	int rc = 0;

	if (!ctl || ctl->idx >= CTL_MAX || q >= DMA_CTL_QUEUE_MAX) {
		DRM_ERROR("ctl %pK q %d index %d\n", ctl, q,
				((ctl) ? ctl->idx : -1));
		return -EINVAL;
	}

	if (!last_cmd_buf_sb[ctl->idx][ctl->dpu_idx]
		|| !last_cmd_buf_sb[ctl->idx][ctl->dpu_idx]->iova) {
		DRM_ERROR("invalid last cmd buf for idx %d\n", ctl->idx);
		return -EINVAL;
	}

	cfg.dma_buf = last_cmd_buf_sb[ctl->idx][ctl->dpu_idx];
	reset_reg_dma_buffer_v1(last_cmd_buf_sb[ctl->idx][ctl->dpu_idx]);
	if (validate_last_cmd(&cfg)) {
		DRM_ERROR("validate buf failed\n");
		return -EINVAL;
	}

	if (write_last_cmd(&cfg)) {
		DRM_ERROR("write buf failed\n");
		return -EINVAL;
	}

	kick_off.ctl = ctl;
	kick_off.trigger_mode = WRITE_IMMEDIATE;
	kick_off.last_command = 1;
	kick_off.op = REG_DMA_WRITE;
	kick_off.dma_type = REG_DMA_TYPE_SB;
	kick_off.queue_select = q;
	kick_off.dma_buf = last_cmd_buf_sb[ctl->idx][ctl->dpu_idx];
	kick_off.feature = REG_DMA_FEATURES_MAX;
	rc = kick_off_v1(&kick_off, ctl->dpu_idx);
	if (rc)
		DRM_ERROR("kick off last cmd failed\n");

	SDE_EVT32(ctl->idx, kick_off.queue_select, kick_off.dma_type,
			kick_off.op, ctl->dpu_idx);
	return rc;
}

struct sde_reg_dma_buffer **get_reg_dma_vq_ctx(u32 dpu_idx, u32 ctl_idx, u32 display_idx)
{
	int vq_idx;

	if (dpu_idx < DPU_0 || dpu_idx >= DPU_MAX) {
		SDE_ERROR("Invalid dpu id %d\n", dpu_idx);
		return NULL;
	}

	if (ctl_idx < CTL_0 || ctl_idx >= CTL_MAX) {
		SDE_ERROR("Invalid ctl id %d  for dpu %d\n", ctl_idx, dpu_idx);
		return NULL;
	}

	vq_idx = reg_dma_ctl_to_vq_map[dpu_idx][ctl_idx][display_idx];
	if (vq_idx < REG_DMA_VQ_0 || vq_idx >= REG_DMA_VQ_MAX) {
		SDE_ERROR("Invalid vq id %d  for dpu %d ctl %d\n", vq_idx, ctl_idx, dpu_idx);
		return NULL;
	}

	return vq_reg_dma_bufs[dpu_idx][0][vq_idx];
}

static int reset_reg_dma_buffer_v4(struct sde_reg_dma_buffer *lut_buf)
{
	/* Do NOT reset the buffer, which is shared by multiple tables */
	return 0;
}

static struct sde_reg_dma_buffer *get_reg_dma_vq_buf_v4(struct sde_kms *sde_kms,
		enum sde_reg_dma_buffer_type type,
		enum sde_hw_blk_type hw_type, u32 idx, u32 dpu_idx, u32 display_idx)
{
	int ctl_idx = -1, i, vq_idx;

	if (dpu_idx < DPU_0 || dpu_idx >= DPU_MAX)
		return NULL;

	// Find the reserved CTL id form the HW type and idx
	switch (hw_type) {
	case SDE_HW_BLK_DSPP:
		for (i = 0; i < sde_kms->catalog->dspp_count; i++) {
			if (sde_kms->catalog->dspp[i].id == idx) {
				ctl_idx = sde_kms->catalog->dspp[i].fixed_ctl_id;
				break;
			}
		}
		break;
	case SDE_HW_BLK_SSPP:
		for (i = 0; i < sde_kms->catalog->sspp_count; i++) {
			if (sde_kms->catalog->sspp[i].id == idx) {
				ctl_idx = sde_kms->catalog->sspp[i].fixed_ctl_id;
				break;
			}
		}
		break;
	default:
		break;
	}

	if (ctl_idx < CTL_0 || ctl_idx >= CTL_MAX) {
		SDE_ERROR("Invalid ctl id %d  for dpu %d\n", ctl_idx, dpu_idx);
		return NULL;
	}

	vq_idx = reg_dma_ctl_to_vq_map[dpu_idx][ctl_idx][display_idx];
	if (vq_idx < REG_DMA_VQ_0 || vq_idx >= REG_DMA_VQ_MAX) {
		SDE_ERROR("Invalid vq id %d  for dpu %d ctl %d\n", vq_idx, ctl_idx, dpu_idx);
		return NULL;
	}

	return vq_reg_dma_bufs[dpu_idx][0][vq_idx][type];
}

static int validate_kick_off_v4(struct sde_reg_dma_kickoff_cfg *cfg, u32 dpu_idx)
{

	if (!cfg || !cfg->ctl || !cfg->dma_buf ||
			cfg->dma_type >= REG_DMA_TYPE_MAX) {
		DRM_ERROR("invalid cfg %pK ctl %pK dma_buf %pK dma type %d\n",
				cfg, ((!cfg) ? NULL : cfg->ctl),
				((!cfg) ? NULL : cfg->dma_buf),
				((!cfg) ? 0 : cfg->dma_type));
		return -EINVAL;
	}

	if (dpu_idx >= DPU_MAX) {
		DRM_ERROR("invalid dpu idx %d\n", dpu_idx);
		return -EINVAL;
	}

	if (reg_dma[dpu_idx]->caps->reg_dma_vq_blks[cfg->ctl->caps->vq_idx][cfg->dma_type].valid
			== false) {
		DRM_DEBUG("REG dma type %d is not supported\n", cfg->dma_type);
		return -EOPNOTSUPP;
	}

	if (cfg->ctl->idx < CTL_0 || cfg->ctl->idx >= CTL_MAX) {
		DRM_ERROR("invalid ctl idx %d\n", cfg->ctl->idx);
		return -EINVAL;
	}

	if (cfg->op >= REG_DMA_OP_MAX) {
		DRM_ERROR("invalid op %d\n", cfg->op);
		return -EINVAL;
	}

	/* MDSS register update doesn't required decode_sel */
	if ((cfg->op == REG_DMA_WRITE) &&
	     (!(cfg->dma_buf->ops_completed & DECODE_SEL_OP) ||
	     !(cfg->dma_buf->ops_completed & REG_WRITE_OP))) {
		DRM_ERROR("incomplete write ops %x\n",
				cfg->dma_buf->ops_completed);
		return -EINVAL;
	}

	if (cfg->op == REG_DMA_READ && cfg->block_select >= DSPP_HIST_MAX) {
		DRM_ERROR("invalid block for read %d\n", cfg->block_select);
		return -EINVAL;
	}

	/* Only immediate triggers are supported now hence hardcode */
	cfg->trigger_mode = (cfg->op == REG_DMA_READ) ? (READ_TRIGGER) :
				(WRITE_TRIGGER);

	if (cfg->dma_buf->iova & GUARD_BYTES) {
		DRM_ERROR("Address is not aligned to %zx iova %llx",
				(size_t)ADDR_ALIGN, cfg->dma_buf->iova);
		return -EINVAL;
	}

	/* Queue1 is deprecated */
	if (cfg->queue_select > DMA_CTL_QUEUE0) {
		DRM_ERROR("invalid queue selected %d\n", cfg->queue_select);
		return -EINVAL;
	}

	if (SIZE_DWORD(cfg->dma_buf->index - cfg->dma_buf->split_size) > MAX_DWORDS_SZ ||
			!cfg->dma_buf->index) {
		DRM_ERROR("invalid dword size %zd max %zd split %zd\n",
			(size_t)SIZE_DWORD(cfg->dma_buf->index),
				(size_t)MAX_DWORDS_SZ, (size_t)cfg->dma_buf->split_size);
		return -EINVAL;
	}

	/* Queue1 is deprecated */
	if (cfg->dma_type == REG_DMA_TYPE_SB &&
			(cfg->queue_select != DMA_CTL_QUEUE0 ||
			cfg->op == REG_DMA_READ)) {
		DRM_ERROR("invalid queue selected %d or op %d for SB LUTDMA\n",
				cfg->queue_select, cfg->op);
		return -EINVAL;
	}

	if ((cfg->dma_buf->abs_write_cnt % 2) != 0) {
		/* Touch up buffer to avoid HW issues with odd number of abs writes */
		u32 reg = 0;
		struct sde_reg_dma_setup_ops_cfg dma_write_cfg;

		dma_write_cfg.dma_buf = cfg->dma_buf;
		dma_write_cfg.blk = MDSS;
		dma_write_cfg.feature = REG_DMA_FEATURES_MAX;
		dma_write_cfg.ops = HW_BLK_SELECT;
		if (validate_write_decode_sel(&dma_write_cfg) || write_decode_sel(&dma_write_cfg)) {
			DRM_ERROR("Failed setting MDSS decode select for LUTDMA touch up\n");
			return -EINVAL;
		}

		/* Perform dummy write on LUTDMA RO version reg */
		dma_write_cfg.ops = REG_SINGLE_WRITE;
		dma_write_cfg.blk_offset = reg_dma[dpu_idx]->caps->base_off;
		dma_write_cfg.data = &reg;
		dma_write_cfg.data_size = sizeof(uint32_t);
		if (validate_write_reg(&dma_write_cfg) || write_single_reg(&dma_write_cfg)) {
			DRM_ERROR("Failed to add touch up write to LUTDMA buffer\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int write_kick_off_v4(struct sde_reg_dma_kickoff_cfg *cfg, u32 dpu_idx)
{
	u32 cmd0, cmd1, val = 0;
	struct sde_hw_blk_reg_map hw;
	int vq_idx;
	struct sde_hw_vatran *vatran;
	u32 map_addr = (u32)-1;
	int i, last = 0;
	u32 pos, next_pos, size;
	int ctl_id = cfg->ctl->idx, q_id = cfg->queue_select;

	if (dpu_idx >= DPU_MAX) {
		DRM_ERROR("invalid dpu idx %d\n", dpu_idx);
		return -EINVAL;
	}

	vq_idx = reg_dma_ctl_to_vq_map[dpu_idx][ctl_id][cfg->ctl->display_idx];

	vatran = sde_hw_get_vatran(dpu_idx);

	memset(&hw, 0, sizeof(hw));
	msm_gem_sync(cfg->dma_buf->buf);

	pos = 0;
	i = 0;

	/*
	 * Enqueue workload, if the workload has been split into multiple sections,
	 * process from first split to last, then the left over.
	 */
	while (pos < cfg->dma_buf->index) {
		if (i < cfg->dma_buf->num_splits) {
			size = SIZE_DWORD(cfg->dma_buf->buf_splits[i] - pos) & MAX_DWORDS_SZ;
			next_pos = cfg->dma_buf->split_start[i];
			i++;
		} else {
			size = SIZE_DWORD(cfg->dma_buf->index - pos) & MAX_DWORDS_SZ;
			next_pos = cfg->dma_buf->index;
		}

		cmd0 = cfg->dma_buf->iova + pos;

		cmd1 = (cfg->op == REG_DMA_READ) ?
			(dspp_read_sel[cfg->block_select] << 30) : 0;
		if ((i >= cfg->dma_buf->num_splits) && (next_pos ==  cfg->dma_buf->index)) {
			cmd1 |= cfg->last_command ? BIT(24) : 0;
			last = 1;
		} else {
			last = 0;
		}
		cmd1 |= (cfg->op == REG_DMA_READ) ? (3 << 22) : (1 << 22);
		cmd1 |= size;

		if (cfg->dma_type == REG_DMA_TYPE_DB)
			SET_UP_REG_DMA_VQ_REG(hw, reg_dma[dpu_idx], REG_DMA_TYPE_DB, vq_idx);
		else if (cfg->dma_type == REG_DMA_TYPE_SB)
			SET_UP_REG_DMA_VQ_REG(hw, reg_dma[dpu_idx], REG_DMA_TYPE_SB, vq_idx);

		if (hw.hw_rev == 0) {
			DRM_ERROR("DMA type %d is unsupported\n", cfg->dma_type);
			return -EOPNOTSUPP;
		}

		//SDE_REG_WRITE_CPU(&hw, reg_dma_opmode_offset, BIT(0));
		val = SDE_REG_READ(&hw,
				reg_dma_ctl0_busy_offset[dpu_idx][ctl_id][q_id]);
		if (val) {
			DRM_ERROR("LUTDMA VQ busy status %x\n", val);
			SDE_EVT32(val);
			//TODO: reset VQ
		}

		if (SDE_DBG_MASK_REGDMA & sde_hw_util_log_mask)
			reg_dma_dump_payload(cfg, pos, size * sizeof(u32));
		SDE_DEBUG("Enqueue ctl %d dpu %d vq %d @ 0x%8.8X  0x%8.8X\n",
				ctl_id, dpu_idx, vq_idx, cmd0, cmd1);
		if (cfg->dma_type == REG_DMA_TYPE_DB) {
			SDE_REG_WRITE_CPU(&hw, reg_dma_ctl_queue_off[dpu_idx][ctl_id],
					cmd0);
			SDE_REG_WRITE_CPU(&hw, reg_dma_ctl_queue_off[dpu_idx][ctl_id] + 0x4,
					cmd1);
		} else if (cfg->dma_type == REG_DMA_TYPE_SB) {
			SDE_REG_WRITE_CPU(&hw, reg_dma_ctl_queue1_off[dpu_idx][ctl_id],
					cmd0);
			SDE_REG_WRITE_CPU(&hw, reg_dma_ctl_queue1_off[dpu_idx][ctl_id] + 0x4,
					cmd1);
		}
		/* Ensure enqueued */
		wmb();

		pos = next_pos;
	}

	/* Trigger VQ for the last command */
	if (cfg->last_command) {
		/* ensure last command is queued before lut dma trigger */
		wmb();

		SDE_REG_WRITE_CPU(&hw,
				reg_dma_intr_0_clear_offset[dpu_idx][ctl_id][q_id],
				0x7F);
		SDE_REG_WRITE_CPU(&hw,
				reg_dma_intr_5_clear_offset[dpu_idx][ctl_id][q_id],
				0xF0007);

#ifdef ENABLE_LUTDMA_INTERRUPTS
		SDE_REG_WRITE_CPU(&hw,
				reg_dma_intr_0_enable_offset[dpu_idx][ctl_id][q_id],
				0x7F);
		SDE_REG_WRITE_CPU(&hw,
				reg_dma_intr_5_enable_offset[dpu_idx][ctl_id][q_id],
				0xF0007);
#endif
		/* DB LUTDMA use SW trigger while SB LUTDMA uses DSPP_SB
		 * flush as its trigger event.
		 */
		if (cfg->dma_type == REG_DMA_TYPE_DB) {
			/* Check if the trigger register is remapped */
			vatran = sde_hw_get_vatran(dpu_idx);
			if (vatran)
				map_addr = vatran->ops.remap(vatran, vq_idx,
						&cfg->ctl->hw, reg_dma_ctl_trigger_offset);
			if (map_addr != (u32)-1) {
				/* Register remapped to VA_TRAN space, for CPU access need remove the base_off */
				SDE_REG_WRITE_CPU(&vatran->hw, map_addr - vatran->caps->base_off,
						ctl_trigger_done_mask[dpu_idx][ctl_id]
							[cfg->ctl->display_idx][q_id]);
				wmb();

				if (SDE_DBG_MASK_REGDMA & sde_hw_util_log_mask)
					vatran->ops.check_violation(vatran);
			} else {
				SDE_ERROR("Fail remap VQ_TRIGGER reg %X ctl%d dpu%d vq%d\n",
						reg_dma_ctl_trigger_offset,
						ctl_id, dpu_idx, vq_idx);
				vatran->ops.check_remap(vatran, vq_idx, &cfg->ctl->hw,
						reg_dma_ctl_trigger_offset);
				/*
				 * full MDP region is RO, can't write
				SDE_REG_WRITE_CPU(&cfg->ctl->hw, reg_dma_ctl_trigger_offset,
						ctl_trigger_done_mask[dpu_idx][ctl_id][q_id]);
				 */
			}
		}
	}

	SDE_EVT32(cfg->feature, cfg->dma_type,
			((uint64_t)cfg->dma_buf) >> 32,
			((uint64_t)cfg->dma_buf) & 0xFFFFFFFF,
			(cfg->dma_buf->iova) >> 32,
			(cfg->dma_buf->iova) & 0xFFFFFFFF,
			cfg->op,
			q_id, ctl_id,
			SIZE_DWORD(cfg->dma_buf->index),
			dpu_idx);
	return 0;
}

static int kick_off_v4_dummy(struct sde_reg_dma_kickoff_cfg *cfg, u32 dpu_idx)
{
	/* Payload buffer is shared, do not kick off with partial changes */
	return 0;
}

static int kick_off_v4(struct sde_reg_dma_kickoff_cfg *cfg, u32 dpu_idx)
{
	int rc = 0;

	rc = validate_kick_off_v4(cfg, dpu_idx);
	if (rc)
		return rc;

	rc = write_kick_off_v4(cfg, dpu_idx);
	return rc;
}

#define SEND_SEPARATED_LAST_CMD		0

const struct {
	enum sde_reg_dma_type type;
	enum sde_reg_dma_buffer_type buf_type;
	enum sde_reg_dma_queue queue;
	enum sde_reg_dma_trigger_mode trigger;
	enum sde_reg_dma_features features;
	const char *name;
} vq_kickoff[] =
{
	{ REG_DMA_TYPE_DB, REG_DMA_TABLE_DB, DMA_CTL_QUEUE0,
			WRITE_TRIGGER, REG_DMA_FEATURES_MAX, "DB table" },
	{ REG_DMA_TYPE_DB, REG_DMA_MDSS_DB, DMA_CTL_QUEUE0,
			WRITE_TRIGGER, MDSS_REG, "DB regs" },
	{ REG_DMA_TYPE_SB, REG_DMA_TABLE_SB, DMA_CTL_QUEUE0,
				WRITE_TRIGGER, REG_DMA_FEATURES_MAX, "SB table" },
	{ REG_DMA_TYPE_SB, REG_DMA_MDSS_SB, DMA_CTL_QUEUE0,
				WRITE_TRIGGER, MDSS_REG, "SB regs" },
	{ REG_DMA_TYPE_MAX },
};

static int last_cmd_sb_v4_dummy(struct sde_hw_ctl *ctl, enum sde_reg_dma_queue q,
        enum sde_reg_dma_last_cmd_mode mode)
{
	return 0;
}

static int last_cmd_v4(struct sde_hw_ctl *ctl, enum sde_reg_dma_queue q,
		enum sde_reg_dma_last_cmd_mode mode)
{
#if SEND_SEPARATED_LAST_CMD
	struct sde_reg_dma_setup_ops_cfg cfg;
#else
	int last_db_idx, last_sb_idx;
#endif
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_blk_reg_map hw;
	struct sde_reg_dma_buffer *buf;
	int dpu_idx, vq_idx;
	u32 val;
	int i, rc;

	if (!ctl || ctl->idx >= CTL_MAX || q >= DMA_CTL_QUEUE_MAX) {
		DRM_ERROR("ctl %pK q %d index %d\n", ctl, q,
				((ctl) ? ctl->idx : -1));
		return -EINVAL;
	}

	dpu_idx = ctl->dpu_idx;
	if (dpu_idx >= DPU_MAX) {
		DRM_ERROR("invalid dpu idx %d\n", ctl->dpu_idx);
		return -EINVAL;
	}

	vq_idx = reg_dma_ctl_to_vq_map[dpu_idx][ctl->idx][ctl->display_idx];

#if !SEND_SEPARATED_LAST_CMD
	i = 0;
	last_db_idx = -1;
	last_sb_idx = -1;
	/* Find which buffer mark last command */
	while (vq_kickoff[i].type != REG_DMA_TYPE_MAX) {
		SDE_DEBUG("Check buffer %s, vq_reg_dma_bufs[%d][0][%d][%d] = %pK\n",
				vq_kickoff[i].name, dpu_idx, vq_idx, vq_kickoff[i].buf_type,
				vq_reg_dma_bufs[dpu_idx][0][vq_idx][vq_kickoff[i].buf_type]);
		buf = vq_reg_dma_bufs[dpu_idx][0][vq_idx][vq_kickoff[i].buf_type];
		if (buf && buf->index) {
			if ((buf->buffer_type == REG_DMA_MDSS_DB) ||
				(buf->buffer_type == REG_DMA_TABLE_DB))
				last_db_idx = i;
			else
				last_sb_idx = i;
		}
		i++;
	}

	if (last_db_idx < 0) {
		DRM_DEBUG("skip empty payload dpu %d ctl %d vq %d\n",
					dpu_idx, ctl->idx, vq_idx);
		return 0;
	}
#endif

	i = 0;
	/* Enqueue each buffer queue is not empty */
	while (vq_kickoff[i].type != REG_DMA_TYPE_MAX) {
		buf = vq_reg_dma_bufs[dpu_idx][0][vq_idx][vq_kickoff[i].buf_type];
		SDE_DEBUG("Check buffer %s %X, buf %pK [%d][0][%d][%d]\n", vq_kickoff[i].name,
			buf ? buf->index : 0, buf, dpu_idx, vq_idx, vq_kickoff[i].buf_type);
		if (buf && buf->index) {
			SDE_DEBUG("Enqueue buffer %s %X  dpu %d  vq %d  i %d  type %d  buf %pK  0x%llX\n",
					vq_kickoff[i].name, buf->index, dpu_idx, vq_idx, i,
					vq_kickoff[i].buf_type, buf, buf->iova);
			kick_off.ctl = ctl;
			kick_off.queue_select = vq_kickoff[i].queue;
			kick_off.trigger_mode = vq_kickoff[i].trigger;
#if SEND_SEPARATED_LAST_CMD
			kick_off.last_command = false;
#else
			kick_off.last_command = (i == last_db_idx) || (i == last_sb_idx);
#endif
			kick_off.op = REG_DMA_WRITE;
			kick_off.dma_type = vq_kickoff[i].type;
			kick_off.dma_buf = buf;
			kick_off.feature = vq_kickoff[i].features;
			rc = kick_off_v4(&kick_off, dpu_idx);
			if (rc) {
				DRM_ERROR("kick off %s failed\n", vq_kickoff[i].name);
				return rc;
			}
		}
		i ++;
	}

#if SEND_SEPARATED_LAST_CMD
	if (!last_cmd_buf_db[ctl->idx][ctl->dpu_idx]
		|| !last_cmd_buf_db[ctl->idx][ctl->dpu_idx]->iova) {
		DRM_ERROR("invalid last cmd buf for idx %d\n", ctl->idx);
		return -EINVAL;
	}

	cfg.dma_buf = last_cmd_buf_db[ctl->idx][ctl->dpu_idx];
	reset_reg_dma_buffer_v1(last_cmd_buf_db[ctl->idx][ctl->dpu_idx]);
	if (validate_last_cmd(&cfg)) {
		DRM_ERROR("validate buf failed\n");
		return -EINVAL;
	}

	if (write_last_cmd(&cfg)) {
		DRM_ERROR("write buf failed\n");
		return -EINVAL;
	}

	kick_off.ctl = ctl;
	kick_off.queue_select = q;
	kick_off.trigger_mode = WRITE_IMMEDIATE;
	kick_off.last_command = 1;
	kick_off.op = REG_DMA_WRITE;
	kick_off.dma_type = REG_DMA_TYPE_DB;
	kick_off.dma_buf = last_cmd_buf_db[ctl->idx][ctl->dpu_idx];
	kick_off.feature = REG_DMA_FEATURES_MAX;
	rc = kick_off_v4(&kick_off, ctl->dpu_idx);
	if (rc) {
		DRM_ERROR("kick off last cmd failed\n");
		return rc;
	}
#endif

	//Lack of block support will be caught by kick_off
	memset(&hw, 0, sizeof(hw));
	SET_UP_REG_DMA_VQ_REG(hw, reg_dma[ctl->dpu_idx], kick_off.dma_type,
			vq_idx);

	SDE_EVT32(SDE_EVTLOG_FUNC_ENTRY, mode, ctl->idx, kick_off.queue_select,
			kick_off.dma_type, kick_off.op, ctl->dpu_idx);
	if (mode == REG_DMA_WAIT4_COMP && hw.base_off) {
		rc = read_poll_timeout(sde_reg_read, val,
				(val & (TRIGGER_0_DONE | ACCESS_FAIL)), 10, 20000, false,
				&hw, reg_dma_intr_0_status_offset[dpu_idx][ctl->idx][q], "INTR_0_STATUS");
		if (rc) {
			DRM_ERROR("poll wait failed %d val %x mask 0x%lx\n",
			    rc, val, TRIGGER_0_DONE | ACCESS_FAIL);
		}
		SDE_EVT32(SDE_EVTLOG_FUNC_EXIT, mode, ctl->dpu_idx, rc);

		if (rc || val & ACCESS_FAIL) {
			DRM_ERROR("regdma fail dpu %d ctl %d vq %d\n",
					dpu_idx, ctl->idx, vq_idx);
			//TODO: error handling
		}
		if (val & TRIGGER_0_DONE) {
			DRM_DEBUG("regdma done dpu %d ctl %d vq %d\n",
					dpu_idx, ctl->idx, vq_idx);
		}

		/* debug only */
		if (0 && (SDE_DBG_MASK_REGDMA & sde_hw_util_log_mask)) {
			check_engine_status_v4(ctl);
			dump_hyp_config(ctl);
		}

		// Clear interrupt status
		SDE_REG_WRITE_CPU(&hw, reg_dma_intr_0_clear_offset[dpu_idx][ctl->idx][REG_DMA_TYPE_DB],
				0x7F);
		SDE_REG_WRITE_CPU(&hw, reg_dma_intr_5_clear_offset[dpu_idx][ctl->idx][REG_DMA_TYPE_DB],
				0xF0007);
	}

	/* debug only */
	if (0 && mode == REG_DMA_WAIT4_COMP) {
		i = 0;
		// Readback each buffer queue is not empty
		while (vq_kickoff[i].type != REG_DMA_TYPE_MAX) {
			buf = vq_reg_dma_bufs[dpu_idx][0][vq_idx][vq_kickoff[i].buf_type];
			if (buf && buf->index) {
				SDE_DEBUG("Read buffer %s  %X  dpu %d  vq %d  i %d  type %d  buf %pK  0x%llX\n",
						vq_kickoff[i].name, buf->index, dpu_idx, vq_idx, i,
						vq_kickoff[i].buf_type, buf, buf->iova);
				kick_off.ctl = ctl;
				kick_off.queue_select = vq_kickoff[i].queue;
				kick_off.trigger_mode = vq_kickoff[i].trigger;
				kick_off.dma_type = vq_kickoff[i].type;
				kick_off.dma_buf = buf;
				kick_off.feature = vq_kickoff[i].features;
				kick_off.last_command = (i == last_db_idx) || (i == last_sb_idx);
				// reg_dma_workload_dump(&kick_off);
				reg_dma_readback_payload(&kick_off);
			}
			i++;
		}
	}

	i = 0;
	// Enqueue each buffer queue is not empty
	while (vq_kickoff[i].type != REG_DMA_TYPE_MAX) {
		SDE_DEBUG("Reset buffer %s\n", vq_kickoff[i].name);
		buf = vq_reg_dma_bufs[dpu_idx][0][vq_idx][vq_kickoff[i].buf_type];
		reset_reg_dma_buffer_v1(buf);
		i++;
	}

	return rc;
}

int reset_v4(struct sde_hw_ctl *ctl)
{
	struct sde_hw_blk_reg_map hw;
	u32 val, i = 0, k = 0;

	if (!ctl || ctl->idx >= CTL_MAX) {
		DRM_ERROR("invalid ctl %pK ctl idx %d\n",
			ctl, ((ctl) ? ctl->idx : 0));
		return -EINVAL;
	}

	if (ctl->dpu_idx >= DPU_MAX) {
		DRM_ERROR("invalid dpu idx %d\n", ctl->dpu_idx);
		return -EINVAL;
	}

	for (k = 0; k < REG_DMA_TYPE_MAX; k++) {
		memset(&hw, 0, sizeof(hw));
		SET_UP_REG_DMA_VQ_REG(hw, reg_dma[ctl->dpu_idx], k,
				reg_dma_ctl_to_vq_map[ctl->dpu_idx][ctl->idx][ctl->display_idx]);
		if (hw.hw_rev == 0)
			continue;

		SDE_REG_WRITE_CPU(&hw, reg_dma_ctl0_reset_offset[ctl->dpu_idx][ctl->idx][k], BIT(0));
		udelay(1000);
		SDE_REG_WRITE_CPU(&hw, reg_dma_ctl0_reset_offset[ctl->dpu_idx][ctl->idx][k], 0);

		i = 0;
		do {
			udelay(1000);
			i++;
			val = SDE_REG_READ(&hw, reg_dma_ctl0_reset_offset[ctl->dpu_idx][ctl->idx][k]);
		} while (i < 2 && val);
	}

	return 0;
}

int flush_v4(struct sde_hw_ctl *ctl, u32 dpu_idx)
{
	/*
	 * FIXME
	 * 1. Check payload buffers, if not empty
	 * 2. call last_cmd_v4 if haven't
	 * 3. trigger the VQ
	 * 4. wait for trigger done, handle error
	 * When finish, call reset_reg_dma_buffer_v1 to reset the payload buffers
	 */

	last_cmd_v4(ctl, DMA_CTL_QUEUE0, REG_DMA_WAIT4_COMP);

	return 0;
}

void dump_hyp_config(struct sde_hw_ctl *ctl)
{
	struct sde_hw_blk_reg_map hw;
	int idx = ctl->idx - CTL_0;

	memcpy(&hw, &ctl->hw, sizeof(hw));
	hw.blk_off = 0;

	SDE_DEBUG("DPU %d  CTL %d  HYP dump\n", ctl->dpu_idx, ctl->idx);
	SDE_DEBUG("CTL_HYP_CTL_%d_VIG_RESERVE  %X", idx, SDE_REG_READ(&hw, 0x15100 + idx * 0x80));
	SDE_DEBUG("CTL_HYP_CTL_%d_DMA_RESERVE  %X", idx, SDE_REG_READ(&hw, 0x15104 + idx * 0x80));
	SDE_DEBUG("CTL_HYP_CTL_%d_DMA_RESERVE  %X", idx, SDE_REG_READ(&hw, 0x15104 + idx * 0x80));
	SDE_DEBUG("CTL_HYP_CTL_%d_LM_RESERVE  %X", idx, SDE_REG_READ(&hw, 0x15108 + idx * 0x80));
	SDE_DEBUG("CTL_HYP_CTL_%d_DSPP_RESERVE  %X", idx, SDE_REG_READ(&hw, 0x1510c + idx * 0x80));
	SDE_DEBUG("CTL_HYP_CTL_%d_LUTDMA_VQ_RESERVE  %X", idx,
			SDE_REG_READ(&hw, 0x15134 + idx * 0x80));

	SDE_DEBUG("CTL_HYP_CTL_%d_VIG_RESERVE_STATUS  %X", idx,
			SDE_REG_READ(&hw, 0x15138 + idx * 0x80));
	SDE_DEBUG("CTL_HYP_CTL_%d_DMA_RESERVE_STATUS  %X", idx,
			SDE_REG_READ(&hw, 0x1513c + idx * 0x80));
	SDE_DEBUG("CTL_HYP_CTL_%d_LM_RESERVE_STATUS  %X", idx,
			SDE_REG_READ(&hw, 0x15140 + idx * 0x80));

	for (idx = 0; idx < 4; idx++)
		SDE_DEBUG("CTL_HYP_VIG_%d_LM_BLEND_RESERVE  %X", idx,
				SDE_REG_READ(&hw, 0x15800 + idx * 0x20));

	for (idx = 0; idx < 6; idx++)
		SDE_DEBUG("CTL_HYP_DMA_%d_LM_BLEND_RESERVE  %X", idx,
				SDE_REG_READ(&hw, 0x15a00 + idx * 0x20));

	SDE_DEBUG("LUTDMA_HYP_VQ_VMID_MAPPING_R0  %X", SDE_REG_READ(&hw, 0x15c04));
	SDE_DEBUG("LUTDMA_HYP_VQ_VMID_MAPPING_R1  %X", SDE_REG_READ(&hw, 0x15c08));
	SDE_DEBUG("LUTDMA_HYP_VQ_VMID_MAPPING_R2  %X", SDE_REG_READ(&hw, 0x15c0c));
	SDE_DEBUG("LUTDMA_HYP_VQ_VMID_MAPPING_R3  %X", SDE_REG_READ(&hw, 0x15c10));

	for (idx = 0; idx < 8; idx++)
		SDE_DEBUG("LUTDMA_HYP_VQ_%d_VIG_RESERVE  %X", idx,
				SDE_REG_READ(&hw, 0x15c24 + idx * 0x20));

	for (idx = 0; idx < 8; idx++)
		SDE_DEBUG("LUTDMA_HYP_VQ_%d_DMA_RESERVE  %X", idx,
				SDE_REG_READ(&hw, 0x15c28 + idx * 0x20));

	idx = ctl->idx - CTL_0;
	SDE_DEBUG("CTL_%d_FLUSH  %X", idx, SDE_REG_READ(&hw, 0x16018 + idx * 0x1000));
	SDE_DEBUG("CTL_%d_FLUSH_MASK  %X", idx, SDE_REG_READ(&hw, 0x16090 + idx * 0x1000));
	SDE_DEBUG("CTL_%d_PIPE_ACTIVE  %X", idx, SDE_REG_READ(&hw, 0x1612c + idx * 0x1000));
	SDE_DEBUG("CTL_%d_FETCH_PIPE_ACTIVE  %X", idx, SDE_REG_READ(&hw, 0x160fc + idx * 0x1000));
	SDE_DEBUG("CTL_%d_LAYER_ACTIVE  %X", idx, SDE_REG_READ(&hw, 0x16130 + idx * 0x1000));

	SDE_DEBUG("CTL_%d_INTR_STATUS_0  %X", idx, SDE_REG_READ(&hw, 0x16804 + idx * 0x1000));
	SDE_DEBUG("CTL_%d_INTR_STATUS_1  %X", idx, SDE_REG_READ(&hw, 0x16814 + idx * 0x1000));
	SDE_DEBUG("CTL_%d_INTR_STATUS_2  %X", idx, SDE_REG_READ(&hw, 0x16804 + idx * 0x1000));
	SDE_DEBUG("CTL_%d_INTR_STATUS_3  %X", idx, SDE_REG_READ(&hw, 0x16824 + idx * 0x1000));
	SDE_DEBUG("CTL_%d_INTR_STATUS_4  %X", idx, SDE_REG_READ(&hw, 0x16834 + idx * 0x1000));
	SDE_DEBUG("CTL_%d_INTR_STATUS_5  %X", idx, SDE_REG_READ(&hw, 0x16844 + idx * 0x1000));
	SDE_DEBUG("CTL_%d_INTR_STATUS_6  %X", idx, SDE_REG_READ(&hw, 0x16864 + idx * 0x1000));
	SDE_DEBUG("CTL_%d_INTR_STATUS_7  %X", idx, SDE_REG_READ(&hw, 0x1686c + idx * 0x1000));

	SDE_DEBUG("CTL_%d_LUT_DMA_VQ_TRIGGER_0  %X", idx, SDE_REG_READ(&hw, 0x16900 + idx * 0x1000));
}

bool check_engine_status_v4(struct sde_hw_ctl *ctl)
{
	struct sde_hw_blk_reg_map hw;
	int dpu_idx, vq_idx;

	if (!ctl || ctl->idx >= CTL_MAX) {
		DRM_ERROR("invalid ctl %pK ctl idx %d\n",
			ctl, ((ctl) ? ctl->idx : 0));
		return -EINVAL;
	}

	dpu_idx = ctl->dpu_idx;
	if (dpu_idx >= DPU_MAX) {
		DRM_ERROR("invalid dpu idx %d\n", dpu_idx);
		return false;
	}

	vq_idx = ctl->caps->vq_idx;
	if (vq_idx < REG_DMA_VQ_0 || vq_idx >= REG_DMA_VQ_MAX) {
		DRM_ERROR("invalid vq idx %d\n", dpu_idx);
		return false;
	}

	SET_UP_REG_DMA_VQ_REG(hw, reg_dma[ctl->dpu_idx], REG_DMA_TYPE_DB, vq_idx);
	SDE_DEBUG("VQ%d DB CMD0 = %X\n", vq_idx,
			SDE_REG_READ(&hw, reg_dma_ctl_queue_off[dpu_idx][ctl->idx]));
	SDE_DEBUG("VQ%d DB CMD1 = %X\n", vq_idx,
			SDE_REG_READ(&hw, reg_dma_ctl_queue_off[dpu_idx][ctl->idx] + 4));
	SDE_DEBUG("VQ%d DB BUSY = %X\n", vq_idx,
			SDE_REG_READ(&hw, reg_dma_ctl0_busy_offset[dpu_idx][ctl->idx][REG_DMA_TYPE_DB]));
	SDE_DEBUG("VQ%d DB INT0 = %X\n", vq_idx,
			SDE_REG_READ(&hw, reg_dma_intr_0_status_offset[dpu_idx][ctl->idx][REG_DMA_TYPE_DB]));
	SDE_DEBUG("VQ%d DB INT5 = %X\n", vq_idx,
			SDE_REG_READ(&hw, reg_dma_intr_5_status_offset[dpu_idx][ctl->idx][REG_DMA_TYPE_DB]));

#if 0
	SET_UP_REG_DMA_VQ_REG(hw, reg_dma[ctl->dpu_idx], REG_DMA_TYPE_SB, vq_idx);
	SDE_DEBUG("VQ%d SB CMD0 = %X\n", vq_idx,
			SDE_REG_READ(&hw, reg_dma_ctl_queue1_off[dpu_idx][ctl->idx]));
	SDE_DEBUG("VQ%d SB CMD1 = %X\n", vq_idx,
			SDE_REG_READ(&hw, reg_dma_ctl_queue1_off[dpu_idx][ctl->idx] + 4));
	SDE_DEBUG("VQ%d SB BUSY = %X\n", vq_idx,
			SDE_REG_READ(&hw, reg_dma_ctl0_busy_offset[dpu_idx][ctl->idx][REG_DMA_TYPE_SB]));
	SDE_DEBUG("VQ%d DB INT0 = %X\n", vq_idx,
			SDE_REG_READ(&hw, reg_dma_intr_0_status_offset[dpu_idx][ctl->idx][REG_DMA_TYPE_SB]));
	SDE_DEBUG("VQ%d DB INT5 = %X\n", vq_idx,
			SDE_REG_READ(&hw, reg_dma_intr_5_status_offset[dpu_idx][ctl->idx][REG_DMA_TYPE_SB]));
#endif

	hw.blk_off = 0;
	SDE_DEBUG("DB ENG OPMODE = %X\n", SDE_REG_READ(&hw, 0x004));
	SDE_DEBUG("DB ENG DECODE_SELECT = %X\n", SDE_REG_READ(&hw, 0x114));
	SDE_DEBUG("DB ENG INTR1 = %X\n", SDE_REG_READ(&hw, 0x164));
	SDE_DEBUG("DB ENG INTR2 = %X\n", SDE_REG_READ(&hw, 0x168));
	SDE_DEBUG("DB ENG INTR3 = %X\n", SDE_REG_READ(&hw, 0x16C));
	SDE_DEBUG("DB ENG INTR4 = %X\n", SDE_REG_READ(&hw, 0x170));
	SDE_DEBUG("DB ENG PRIORITY_OVERIDE = %X\n", SDE_REG_READ(&hw, 0x118));
	SDE_DEBUG("DB ENG SW_RESET = %X\n", SDE_REG_READ(&hw, 0x0104));
	SDE_DEBUG("DB ENG TRAFFIC_SHAPER WRITE = %X\n", SDE_REG_READ(&hw, 0x1E0));
	SDE_DEBUG("DB ENG TRAFFIC_SHAPER READ = %X\n", SDE_REG_READ(&hw, 0x1E4));
	SDE_DEBUG("DB ENG DEBUG_BUS_CTRL = %X\n", SDE_REG_READ(&hw, 0x1E8));
	SDE_DEBUG("DB ENG DEBUG_BUS_STATUS = %X\n", SDE_REG_READ(&hw, 0x1EC));
	SDE_DEBUG("DB ENG DB_PMU_CTRL = %X\n", SDE_REG_READ(&hw, 0x1F0));
	SDE_DEBUG("DB ENG DB_LUTMODE_OPCODE_INVALID = %X\n", SDE_REG_READ(&hw, 0x240));
	SDE_DEBUG("DB ENG DB_LUTMODE_OPCODE_INVALID_DEC = %X\n", SDE_REG_READ(&hw, 0x244));

#if 0
	SDE_DEBUG("SB ENG OPMODE = %X\n", SDE_REG_READ(&hw, 0x804));
	SDE_DEBUG("SB ENG DECODE_SELECT = %X\n", SDE_REG_READ(&hw, 0x914));
	SDE_DEBUG("SB ENG INTR1 = %X\n", SDE_REG_READ(&hw, 0x964));
	SDE_DEBUG("SB ENG INTR2 = %X\n", SDE_REG_READ(&hw, 0x968));
	SDE_DEBUG("SB ENG INTR3 = %X\n", SDE_REG_READ(&hw, 0x96C));
	SDE_DEBUG("SB ENG INTR4 = %X\n", SDE_REG_READ(&hw, 0x970));
	SDE_DEBUG("SB ENG PRIORITY_OVERIDE = %X\n", SDE_REG_READ(&hw, 0x918));
	SDE_DEBUG("SB ENG SW_RESET = %X\n", SDE_REG_READ(&hw, 0x904));
	SDE_DEBUG("SB ENG TRAFFIC_SHAPER WRITE = %X\n", SDE_REG_READ(&hw, 0x9E0));
	SDE_DEBUG("SB ENG TRAFFIC_SHAPER READ = %X\n", SDE_REG_READ(&hw, 0x9E4));
	SDE_DEBUG("SB ENG DEBUG_BUS_CTRL = %X\n", SDE_REG_READ(&hw, 0x9E8));
	SDE_DEBUG("SB ENG DEBUG_BUS_STATUS = %X\n", SDE_REG_READ(&hw, 0x9EC));
	SDE_DEBUG("SB ENG DB_PMU_CTRL = %X\n", SDE_REG_READ(&hw, 0x9F0));
	SDE_DEBUG("SB ENG DB_LUTMODE_OPCODE_INVALID = %X\n", SDE_REG_READ(&hw, 0xA40));
	SDE_DEBUG("SB ENG DB_LUTMODE_OPCODE_INVALID_DEC = %X\n", SDE_REG_READ(&hw, 0xA44));
#endif

	return false;
}

void deinit_v4(u32 dpu_idx)
{
	return;
	int i, j, k;

	for (i = 0; i < REG_DMA_VQ_MAX; i ++) {
		for (j = 0; j < REG_DMA_PAYLOAD_BUF_MAX; j++) {
			for (k = 0; k < NUM_BUFFERS; k++) {
				if (vq_reg_dma_bufs[dpu_idx][k][i][j]) {
					dealloc_reg_dma_v1(vq_reg_dma_bufs[dpu_idx][k][i][j], dpu_idx);
					vq_reg_dma_bufs[dpu_idx][k][i][j] = NULL;
				}
			}
		}
	}

	deinit_v1(dpu_idx);
}
