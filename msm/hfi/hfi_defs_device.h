/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __H_HFI_DEFS_DEVICE_H__
#define __H_HFI_DEFS_DEVICE_H__

/*
 * This is documentation file. Not used for header inclusion.
 */

#include "hfi_defs_common.h"

/*
 * Generic hot-plug detection configuration structure for dynamic display interfaces.
 *
 * This structure provides comprehensive configuration details for newly connected devices
 * on interfaces that support dynamic connection changes. It serves as a unified interface
 * for various display connection types including DisplayPort, USB-C, and other pluggable
 * display technologies.
 *
 * The structure is designed to be interface-agnostic while providing specific context
 * through its field values. Different interface types interpret the fields according
 * to their specific requirements and capabilities.
 *
 * Usage Examples:
 *   - DisplayPort (DP): @orientation indicates CC1/CC2 for USB-C connectors,
 *     @pin_config maps to DP pin assignments A-F, @hpd_irq indicates HPD IRQ events.
 *   - USB-C Alt Mode: @orientation specifies connector orientation,
 *     @pin_config defines lane configuration and capabilities.
 *
 * @orientation:
 *     Cable orientation or connector role identifier.
 *     - For USB-C/DP: CC1 (0) or CC2 (1) orientation
 *     - For other interfaces: connector-specific orientation value
 *     - Range: Interface-dependent, typically 0-1 for USB-C
 * @port_index:
 *     Physical port identifier for the display interface.
 *     - Identifies which physical port/connector the device is connected to
 *     - Used for multi-port systems to distinguish between different connectors
 *     - Range: 0 to (max_ports - 1), where max_ports is system-dependent
 * @pin_config:
 *     Connector pin configuration or lane mapping specification.
 *     - For DP: Pin assignment configuration (A, B, C, D, E, F)
 *     - For USB-C: Alt mode pin assignment and lane allocation
 *     - Defines how data lanes are allocated between USB and display functions
 *     - Range: Interface-specific enumerated values
 * @hpd_state:
 *     Current hot-plug detection state of the interface connection.
 *     - Indicates whether a device is currently connected or disconnected
 *     - Values: 0 = disconnected, 1 = connected
 *     - Updated whenever connection state changes
 * @hpd_irq:
 *     Hot-plug detection interrupt or attention signal indicator.
 *     - Signals interrupt or attention events from the connected device
 *     - For DP: Indicates HPD IRQ pulse (short pulse vs. long pulse)
 *     - Used to trigger EDID re-read, capability negotiation, or link training
 *     - Values: 0 = no IRQ, 1 = IRQ/attention signal present
 */
struct hfi_device_hotplug_config {
	u32 orientation;
	u32 port_index;
	u32 pin_config;
	u32 hpd_state;
	u32 hpd_irq;
};

/*
 * Hot-plug detection information structure for display interface status reporting.
 *
 * This structure encapsulates detailed information about hot-plug detection events
 * and the current state of display interface connections. It provides comprehensive
 * status reporting for dynamic display connections, including connection state,
 * device capabilities, and event notifications.
 *
 * The structure is used primarily for status reporting and event notification
 * between the display controller and host system, providing real-time information
 * about connection changes and device capabilities.
 *
 * Usage Context:
 *   - Event notification when devices are connected/disconnected
 *   - Status queries for current connection state
 *   - Capability reporting for newly connected devices
 *   - Error reporting for connection issues
 *
 * @config:
 *     Hot-plug configuration details for the connected device.
 *     - Contains orientation, port, pin configuration, and state information
 *     - Provides interface-specific connection parameters
 *     - Updated whenever connection parameters change
 * @edid_buf:
 *     Buffer containing Extended Display Identification Data (EDID) from connected device.
 *     - Contains display capabilities, supported resolutions, and timing information
 *     - Populated when device is connected and EDID is successfully read
 *     - Used for display mode negotiation and capability determination
 *     - Buffer may be empty if EDID read fails or device doesn't support EDID
 * @modes_buf:
 *     Buffer containing list of supported display modes and resolutions.
 *     - Derived from EDID data and device capabilities
 *     - Contains timing parameters, refresh rates, and format information
 *     - Used by display subsystem for mode selection and validation
 *     - May include both standard and custom display modes
 */
struct hfi_device_hotplug_info {
	struct hfi_device_hotplug_config config;
	struct hfi_buff edid_buf;
	struct hfi_buff modes_buf;
};

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
