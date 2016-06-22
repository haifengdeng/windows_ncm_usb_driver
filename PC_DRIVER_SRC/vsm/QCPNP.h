/*===========================================================================
FILE: QCPNP.h

DESCRIPTION:


INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#ifndef QCPNP_H
#define QCPNP_H

#include "QCMAIN.h"

NTSTATUS QCPNP_AddDevice
(
   IN PDRIVER_OBJECT pDriverObject,
   IN PDEVICE_OBJECT pdo
);
NTSTATUS QCPNP_GetDeviceCapabilities
(
   PDEVICE_EXTENSION deviceExtension,
   BOOLEAN bPowerManagement
);
NTSTATUS BulkUsb_QueryCapabilities
(
   IN PDEVICE_OBJECT PdoDeviceObject,
   IN PDEVICE_CAPABILITIES DeviceCapabilities
);
NTSTATUS BulkUsb_IrpCompletionRoutine
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PIRP Irp,
   IN PVOID Context
);
NTSTATUS QCSER_VendorRegistryProcess
(
   IN PDRIVER_OBJECT pDriverObject,
   IN PDEVICE_OBJECT PhysicalDeviceObject
);
NTSTATUS QCSER_PostVendorRegistryProcess
(
   IN PDRIVER_OBJECT pDriverObject,
   IN PDEVICE_OBJECT PhysicalDeviceObject,
   IN PDEVICE_OBJECT DeviceObject
);
NTSTATUS QCPNP_StartDevice(IN PDEVICE_OBJECT DeviceObject, IN UCHAR cookie);
NTSTATUS QCPNP_ConfigureDevice
(
   IN PDEVICE_OBJECT DeviceObject,
   IN UCHAR ConfigIndex
);
NTSTATUS QCPNP_SelectInterfaces
(
   IN PDEVICE_OBJECT pDevObj,
   IN PUSB_CONFIGURATION_DESCRIPTOR pConfigDesc
);
NTSTATUS QCPNP_HandleRemoveDevice
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PDEVICE_OBJECT CalledDO,
   IN PIRP Irp
);
NTSTATUS QCPNP_StopDevice( IN  PDEVICE_OBJECT DeviceObject );
BOOLEAN QCPNP_CreateSymbolicLink(PDEVICE_OBJECT DeviceObject);
DEVICE_TYPE QCPNP_GetDeviceType(PDEVICE_OBJECT PortDO);
NTSTATUS QCPNP_RegisterDeviceInterface(PDEVICE_OBJECT DeviceObject);

BOOLEAN QCPNP_ValidateConfigDescriptor
(
   PDEVICE_EXTENSION pDevExt,
   PUSB_CONFIGURATION_DESCRIPTOR ConfigDesc
);

BOOLEAN QCPNP_ValidateDeviceDescriptor
(
   PDEVICE_EXTENSION      pDevExt,
   PUSB_DEVICE_DESCRIPTOR DevDesc
);

#endif // QCPNP_H
