/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_DEFS_DEVICE_H__
#define __H_HFI_DEFS_DEVICE_H__

/*
 * This is documentation file. Not used for header inclusion.
 */



/*!
 * @enum hfi_device_res_state
 * @brief Display resource state enumeration for virtualization resource management.
 *
 * This enumeration defines the possible states for display resources in virtualized
 * environments, controlling how display hardware is allocated and shared between
 * virtual machines.
 *
 * State Transitions:
 *   - ACQUIRE: VM takes exclusive control of display resources
 *   - RELEASE: VM relinquishes control, returning resources to host
 *   - SHARED: Display resources are shared between multiple VMs
 *
 * @var HFI_DEVICE_RESOURCE_ACQUIRE
 *   Acquire exclusive control of the display resource.
 *
 * @var HFI_DEVICE_RESOURCE_RELEASE
 *   Release the display resource back to the host.
 *
 * @var HFI_DEVICE_RESOURCE_SHARED
 *   Enable shared display mode between multiple VMs.
 */
enum hfi_device_res_state {
	HFI_DEVICE_RESOURCE_ACQUIRE = 0x0,
	HFI_DEVICE_RESOURCE_RELEASE = 0x1,
	HFI_DEVICE_RESOURCE_SHARED  = 0x2,
};

/*!
 * @enum hfi_device_resource_type
 * @brief Resource type identifiers for virtualization resource management.
 *
 * This enumeration defines the types of hardware resources that can be managed,
 * allocated, and shared in virtualized environments. It is used in conjunction
 * with hfi_resource_config to specify which type of resource is being acquired,
 * released, or shared between virtual machines.
 *
 * @var HFI_DEVICE_RESOURCE_DISPLAY
 *   Display Resource is being lent across VMs.
 */
enum hfi_device_resource_type {
	HFI_DEVICE_RESOURCE_DISPLAY,
};

/*!
 * @struct hfi_device_resource_config
 * @brief Resource configuration structure for virtualization resource management.
 *
 * This structure provides comprehensive configuration for managing hardware resources
 * in virtualized environments. It enables dynamic allocation, sharing, and control of
 * hardware resources (such as displays) between different VMs.
 *
 * @var disp_mask
 *   Bitmask specifying which displays need to be managed across VMs.
 *
 * @var vm_state
 *   Desired resource state for the virtualization operation.
 *
 * @var resource_type
 *   Type of hardware resource being managed.
 *
 * @var reserved
 *   Reserved fields for future extensions.
 */
struct hfi_device_resource_config {
	u32 disp_mask;
	enum hfi_device_res_state vm_state;
	u32 resource_type;
	u32 reserved[2];
};
#endif // __H_HFI_DEFS_DEVICE_H__
