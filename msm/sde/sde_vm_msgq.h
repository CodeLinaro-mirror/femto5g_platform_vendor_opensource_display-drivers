/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */
#ifndef __SDE_VM_MSGQ_H__
#define __SDE_VM_MSGQ_H__

#include <linux/types.h>
#include "sde_vm.h"

/**
 * sde_vm_msgq_init - initialize display message queue: both TX and RX
 * @sde_kms - handle to sde_kms
 */
extern int sde_vm_msgq_init(struct sde_vm *sde_vm);

/**
 * sde_vm_msgq_deinit - deinitialize display message queue: both TX and RX
 * @sde_kms - handle to sde_kms
 */
extern void sde_vm_msgq_deinit(struct sde_vm *sde_vm);

/**
 * sde_vm_msgq_send - send custom messages across VM's
 * @sde_vm - handle to vm base struct
 * @msg - payload data
 * @msg_size - size of the payload_data
 */
extern int sde_vm_msgq_send(struct sde_vm *sde_vm, void *msg, size_t msg_size);

#endif // __SDE_VM_MSGQ_H__
