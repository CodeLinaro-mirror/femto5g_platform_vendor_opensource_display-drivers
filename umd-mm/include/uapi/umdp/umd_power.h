/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DRIVERS_UMD_POWER_H_
#define _DRIVERS_UMD_POWER_H_

enum umdp_level {
	UMD_POWER_OFF = 0,
	UMD_POWER_ON,
	UMD_POWER_L1,
	UMD_POWER_L2,
	UMD_POWER_L3,
	UMD_POWER_MAX,
	UMD_POWER_CNT,
};

#define UMDP_MAGIC  0x09050207
#define PRE_GPID_DISP  0x1
#define PRE_GPID_VIDEO  0x2
#define PRE_GPID_CAM  0x3
#define PRE_GPID_GFX  0x4

#define CONVERT_UMD_GROUP(x, y)   ((x) << 8 | (y))

struct umdp_payload {
	__u32 magic;
	__u16 group; /* CONVERT_UMD_GROUP(PRE_GPID_XX, group_id) */
	__u16 level;
} __packed;

#define UMDP_GET_STATUS \
	_IOWR('u', 0x01, struct umdp_payload)
#define UMDP_SET_STATUS \
	_IOW('u', 0x02, struct umdp_payload)
#define UMDP_GET_SUPPORTED_MASK \
	_IOR('u', 0x03, unsigned int)


#endif /* _DRIVERS_UMD_POWER_H_ */
