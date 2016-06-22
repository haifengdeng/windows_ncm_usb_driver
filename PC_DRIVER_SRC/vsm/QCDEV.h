/*===========================================================================
FILE: QCDEV.h

DESCRIPTION:
   This file contains definitions for standalone USB WDM device.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2007 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#ifndef QCDEV_H
#define QCDEV_H

#include "QCMAIN.h"

// {C88E99DE-6905-47d6-AE7E-A110AA903B98}
DEFINE_GUID(GUID_DEVINTERFACE_QCUSB_SERIAL,
0xc88e99de, 0x6905, 0x47d6, 0xae, 0x7e, 0xa1, 0x10, 0xaa, 0x90, 0x3b, 0x98);

// #define QCDEV_NAME L"\\Device\\{C88E99DE-6905-47d6-AE7E-A110AA903B98}"
#define QCDEV_NAME L"QCDEV_XWDM100"
#define QCDEV_PATH L"\\QCDEV_XWDM100"

VOID QCDEV_RegisterDeviceInterface(PDEVICE_EXTENSION pDevExt);

VOID QCDEV_DeregisterDeviceInterface(PDEVICE_EXTENSION pDevExt);

BOOLEAN QCDEV_IsXwdmRequest(PDEVICE_EXTENSION pDevExt, PIRP Irp);

VOID QCDEV_SystemToPowerDown(BOOLEAN PrepareToPowerDown);

VOID QCDEV_SetSystemPowerState(SYSTEM_POWER_STATE State);

VOID QCDEV_SetDevicePowerState(DEVICE_POWER_STATE State);

VOID QCDEV_GetSystemPowerState(PVOID Buffer);

VOID QCDEV_GetDevicePowerState(PVOID Buffer);

NTSTATUS QCDEV_CacheNotificationIrp
(
   PDEVICE_OBJECT DeviceObject,
   PVOID          ioBuffer,
   PIRP           pIrp
);

VOID QCDEV_PostNotification(PDEVICE_EXTENSION pDevExt);

NTSTATUS QCDEV_CancelNotificationIrp(PDEVICE_OBJECT pDevObj);

VOID QCDEV_CancelNotificationRoutine(PDEVICE_OBJECT CalledDO, PIRP pIrp);

#endif // QCDEV_H
