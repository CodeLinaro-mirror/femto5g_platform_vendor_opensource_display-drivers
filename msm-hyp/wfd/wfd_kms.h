/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

/* Copyright © 2006 Keith Packard
 * Copyright © 2007-2008 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __WFD_KMS_H__
#define __WFD_KMS_H__

#include "wire_user.h"
#include "wire_format.h"
#include <msm_drv_hyp.h>

#define PANEL_NAME_LEN 13

enum WFD_QDILayerType {
	WFD_QDI_LAYER_NONE		= 0,
	WFD_QDI_LAYER_GRAPHICS,
	WFD_QDI_LAYER_OVERLAY,
	WFD_QDI_LAYER_DMA,
	WFD_QDI_LAYER_CURSOR,
	WFD_QDI_LAYER_MAX,
	WFD_QDI_LAYER_FORCE_32BIT	= 0x7FFFFFFF
};

struct wfd_connector_info_priv {
	struct msm_hyp_connector_info base;
	struct drm_crtc *crtc;
	WFDDevice wfd_device;
	WFDPort wfd_port;
	WFDint wfd_port_id;
	int wfd_port_idx;

	int connector_status;
	uint32_t mode_count;
	struct drm_display_mode *modes;
	char panel_name[PANEL_NAME_LEN];
	WFDPortMode * port_modes;
};

struct wfd_plane_info_priv {
	struct msm_hyp_plane_info base;
	WFDDevice wfd_device;
	WFDPort wfd_port;
	WFDPipeline wfd_pipeline;
	enum WFD_QDILayerType wfd_type;
	bool committed;
};

struct wfd_crtc_info_priv {
	struct msm_hyp_crtc_info base;
	WFDDevice wfd_device;
	WFDPort wfd_port;
	WFDint wfd_port_id;
	int wfd_port_idx;
	bool vblank_enable;
	struct msm_hyp_prop_blob_info extra_info;
};

struct wfd_framebuffer_priv {
	struct msm_hyp_framebuffer_info base;
	WFDDevice wfd_device;
	WFDEGLImage wfd_image;
	WFDint wfd_format;
	WFDint wfd_usage;
	WFDSource wfd_source;
};

struct wfd_kms {
	struct msm_hyp_kms base;
	struct drm_device *dev;
	uint32_t client_id;
	WFDDevice wfd_device[MAX_DEVICE_CNT];
	int wfd_device_cnt;

	WFDPort ports[MAX_PORT_CNT];
	WFDint port_ids[MAX_PORT_CNT];
	WFDDevice port_devs[MAX_PORT_CNT];
	int port_cnt;

	WFDPipeline pipelines[MAX_PORT_CNT][MAX_PIPELINE_CNT];
	int master_idx[MAX_PORT_CNT][MAX_PIPELINE_CNT];
	int pipeline_cnt[MAX_PORT_CNT];

	uint32_t max_sdma_width;
};

struct wfd_kms_port {
	WFDDevice wfd_device;
	WFDPort wfd_port;
	WFDint wfd_port_id;
};

typedef khronos_uint16_t			 WFDuint16;

#define WFD_CSC_LUT_ENTRIES                                  3
#define WFD_GAMMA_LUT_ENTRIES                                256
#define WFD_WGM_MAX_COLOR_COMPONENTS                         3
#define WFD_WGM_MAX_COMPONENT_TABLES_FINE_COARSE             4
#define WFD_WGM_TABLE_0_ENTRIES                              1229
#define WFD_WGM_TABLE_1_ENTRIES                              1228
#define WFD_WGM_TABLE_2_ENTRIES                              1228
#define WFD_WGM_TABLE_3_ENTRIES                              1228
#define WFD_WGM_MAX_NON_UNIFORM_MAP_TABLE_ENTRIES_A          16
#define WFD_NUM_COLOR_CONFIG_BUFFERS                         2

/*
 * Color support related definations and variables
 *
 */
#define MAX_PIPELINES_GVM 16

struct buffer_info
{
	bool valid;
	bool in_use;
	int  export_id;
	dma_addr_t* dmabuf_handle;
	int32_t* va;
};

struct color_buffer
{
	bool		 valid;
	WFDDevice	 device;
	WFDPipeline  pipeline;
	struct buffer_info	buffer_info[2];
};

/*
 * WFD_PipelineDMAConfigBuffType defines DMA pipe LUT config
 * this is buffer struture which is passed as input to WFD layer by GVM or wfd_client_gen
 * This will come as pmem handle to openwfd server in case of QNX
 */
typedef struct	__attribute__((__packed__))
{
	WFDboolean		 bIGCEnabled;												/* True, if IGC LUT is valid */
	WFDboolean		 bCSCEnabled;												/* True, if CSC Matirx is valid */
	WFDboolean		 bGCEnabled;												/* True, if IGC LUT is valid */
	WFDuint32 		 uCscMatrix[WFD_CSC_LUT_ENTRIES][WFD_CSC_LUT_ENTRIES];	   /* CSC matrix */
	WFDuint16 		 uGCLut[WFD_GAMMA_LUT_ENTRIES]; 						   /* Gamma LUT data. */
	WFDuint16 		 uIGCLut[WFD_GAMMA_LUT_ENTRIES];						   /* Inverse Gamma LUT data. */
} WFD_PipelineDMAConfigBuffType;

/*
 * WFD_Color_VIGConfigType defines VIG pipe LUT (17*17*17) config
 * this is buffer struture which is passed as input to WFD layer by GVM or wfd_client_gen
 * This will come as pmem handle to openwfd server in case of QNX
 */
typedef struct	__attribute__((__packed__))
{
	WFDboolean   bGamutEn;				/**< Flag to enable/disable gamut mapping.	*/
	WFDboolean   bGamutMapEn; 			/**< Flag to enable/disable Non uniform map.*/
	WFDuint16    uGammutTable0Entries[WFD_WGM_MAX_COLOR_COMPONENTS][WFD_WGM_TABLE_0_ENTRIES];
	WFDuint16    uGammutTable1Entries[WFD_WGM_MAX_COLOR_COMPONENTS][WFD_WGM_TABLE_1_ENTRIES];
	WFDuint16    uGammutTable2Entries[WFD_WGM_MAX_COLOR_COMPONENTS][WFD_WGM_TABLE_2_ENTRIES];
	WFDuint16    uGammutTable3Entries[WFD_WGM_MAX_COLOR_COMPONENTS][WFD_WGM_TABLE_3_ENTRIES];
									/**< 3d gamut mapping tables' entries.
									   pGammutTableEntries[X][0] should be of size HAL_MDP_WGM_TABLE_0_ENTRIES
									   pGammutTableEntries[X][1] should be of size HAL_MDP_WGM_TABLE_1_ENTRIES
									   pGammutTableEntries[X][2] should be of size HAL_MDP_WGM_TABLE_2_ENTRIES
									   pGammutTableEntries[X][3] should be of size HAL_MDP_WGM_TABLE_3_ENTRIES */
	WFDuint32    uNonUniformMapTableEntries[WFD_WGM_MAX_COLOR_COMPONENTS][WFD_WGM_MAX_NON_UNIFORM_MAP_TABLE_ENTRIES_A];
									/**< segment table entries.
									   pWdNonUniformMapTableEntries[X] should be of size
									   HAL_MDP_WGM_MAX_NON_UNIFORM_MAP_TABLE_ENTRIES_A */
} WFD_PipelineVIGConfigBuffType;

/*
 * WFD_PipelineColorConfigBuffType defines essential parameters for getting the LUT tables for the DMA or VIG pipes
 */
typedef union
{
	WFD_PipelineDMAConfigBuffType   sDMAConfig;
	WFD_PipelineVIGConfigBuffType   sVigConfigType;
} WFD_PipelineColorConfigBuffType;

#define to_wfd_kms(x)\
	container_of((x), struct wfd_kms, base)

#endif /* __WFD_KMS_H__ */
