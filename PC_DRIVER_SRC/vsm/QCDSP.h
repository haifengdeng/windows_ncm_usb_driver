/*===========================================================================
FILE: QCDSP.h

DESCRIPTION:
   This file contains definitions for dispatch routines.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#ifndef QCDSP_H
#define QCDSP_H

#include "QCMAIN.h"

VOID DispatchThread(PVOID pContext);
VOID DispatchCancelQueued(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS QCDSP_DirectDispatch(IN PDEVICE_OBJECT CalledDO, IN PIRP Irp);
NTSTATUS QCDSP_QueuedDispatch(IN PDEVICE_OBJECT CalledDO, IN PIRP Irp);
NTSTATUS QCDSP_Enqueue(PDEVICE_OBJECT DeviceObject, PDEVICE_OBJECT CalledDO, PIRP Irp, KIRQL Irql);
NTSTATUS InitDispatchThread(IN PDEVICE_OBJECT pDevObj);
NTSTATUS QCDSP_Dispatch
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PDEVICE_OBJECT FDO,
   IN PIRP Irp,
   BOOLEAN *Removed,
   BOOLEAN ForXwdm
);
NTSTATUS QCDSP_CleanUp(IN PDEVICE_OBJECT DeviceObject, PIRP pIrp);
NTSTATUS QCDSP_SendIrpToStack(IN PDEVICE_OBJECT PortDO, IN PIRP Irp, char *info);
VOID QCDSP_PurgeDispatchQueue(PDEVICE_EXTENSION pDevExt);
NTSTATUS QCDSP_RestartDeviceFromCancelStopRemove
(
   PDEVICE_OBJECT DeviceObject,
   PIRP Irp
);
BOOLEAN QCDSP_ToProcessIrp
(
   PDEVICE_EXTENSION pDevExt,
   PIRP              Irp
);

#endif // QCDSP_H
