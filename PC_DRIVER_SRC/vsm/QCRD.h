/*===========================================================================
FILE: QCRD.h

DESCRIPTION:
   This file contains definitions for USB read operations.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003, 2004 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#ifndef QCRD_H
#define QCRD_H

#include "QCMAIN.h"

#define QcExAllocateReadIOB(_iob_,_uselock_) \
        { \
           KIRQL levelOrHandle; \
           if (_uselock_ == TRUE) \
           { \
              QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle); \
           } \
           if (IsListEmpty(&pDevExt->ReadFreeQueue) == TRUE) \
           { \
              _iob_ = ExAllocatePool \
                      ( \
                         NonPagedPool, \
                         sizeof(VXD_WDM_IO_CONTROL_BLOCK) \
                      ); \
              if (_iob_ != NULL) {InterlockedIncrement(&(pDevExt->Sts.lAllocatedRdMem));} \
           } \
           else \
           { \
              PLIST_ENTRY headOfList; \
              headOfList = RemoveHeadList(&pDevExt->ReadFreeQueue); \
              _iob_ = CONTAINING_RECORD \
                      ( \
                         headOfList, \
                         VXD_WDM_IO_CONTROL_BLOCK, \
                         List \
                      ); \
           } \
           if (_iob_ != NULL) \
           { \
              InterlockedIncrement(&(pDevExt->Sts.lAllocatedReads)); \
           } \
           if (_uselock_ == TRUE) \
           { \
              QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle); \
           } \
        }

#define QcExFreeReadIOB(_iob_,_uselock_) \
        { \
           KIRQL levelOrHandle; \
           if (_uselock_ == TRUE) \
           { \
              QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle); \
           } \
           InsertTailList(&pDevExt->ReadFreeQueue, &_iob_->List); \
           InterlockedDecrement(&(pDevExt->Sts.lAllocatedReads)); \
           _iob_ = NULL; \
           if (_uselock_ == TRUE) \
           { \
              QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle); \
           } \
        }

NTSTATUS CancelReadThread
(
   PDEVICE_EXTENSION pDevExt,
   UCHAR cookie
);
NTSTATUS StartTheReadGoing
(
   PDEVICE_EXTENSION pDevExt,
   PIRP pIrp,
   UCHAR cookie
);
NTSTATUS STESerial_Read
(
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock
);
PVXD_WDM_IO_CONTROL_BLOCK FindReadIrp
(
   PDEVICE_EXTENSION pDevExt,
   PIRP pIrp
);
NTSTATUS ReadCompletionRoutine
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
);
NTSTATUS QCSER_CancelActiveIOB
(
   PDEVICE_EXTENSION pDevExt,
   PVXD_WDM_IO_CONTROL_BLOCK pCurrIOBlock,
   NTSTATUS status
);
void STESerial_ReadThread(PVOID pContext);
void QCSER_ReadThread(PDEVICE_EXTENSION pDevExt);


VOID TimeoutReadRoutine
(
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock
);
VOID CancelReadRoutine
(
   PDEVICE_OBJECT DeviceObject,
   PIRP pIrp
);
NTSTATUS QCRD_Read(IN PDEVICE_OBJECT CalledDO, IN PIRP pIrp);
NTSTATUS ReadIrpCompletion
(
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock,
   BOOLEAN                   AllowSpinlock,
   UCHAR                     cookie
);
BOOLEAN StartReadIntervalTimeout(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock, char cookie);
BOOLEAN StartReadTimeout(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock);
VOID AbortReadTimeout(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock);
VOID ReadTimeoutDpc
(
   IN PKDPC Dpc,
   IN PVOID DeferredContext,
   IN PVOID SystemArgument1,
   IN PVOID SystemArgument2
);

VOID vResetReadBuffer(PDEVICE_EXTENSION pDevExt, UCHAR cookie);
BOOLEAN bFilledReadIrpFromBuffer
(
   PDEVICE_EXTENSION pDevExt,
   PIRP irp,
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock
);
ULONG CountL1ReadQueue(PDEVICE_EXTENSION pDevExt);
ULONG CountReadQueue(PDEVICE_EXTENSION pDevExt);
VOID vPutToReadBuffer
(
   PDEVICE_EXTENSION pDevExt,
   PUCHAR pucFrom,
   ULONG ulCount
);
NTSTATUS QCRD_InitializeL2Buffers(PDEVICE_EXTENSION pDevExt);
VOID QCRD_ProcessPreTimeoutIOB(PDEVICE_EXTENSION pDevExt);
NTSTATUS QCRD_StartReadThread(PDEVICE_EXTENSION pDevExt);
NTSTATUS QCRD_L2Suspend(PDEVICE_EXTENSION pDevExt);
NTSTATUS QCRD_L2Resume(PDEVICE_EXTENSION pDevExt);
VOID QCRD_ScanForWaitMask
(
   PDEVICE_EXTENSION pDevExt,
   BOOLEAN           SpinlockNeeded,
   UCHAR             Cookie
);

#ifdef QCUSB_MULTI_READS
VOID QCMRD_L1MultiReadThread(PVOID pContext);
NTSTATUS MultiReadCompletionRoutine
(
   PDEVICE_OBJECT DO,
   PIRP           Irp,
   PVOID          Context
);
VOID QCMRD_L2MultiReadThread(PDEVICE_EXTENSION pDevExt);
VOID QCMRD_ResetL2Buffers(PDEVICE_EXTENSION pDevExt);
PQCRD_L2BUFFER QCMRD_L2NextActive(PDEVICE_EXTENSION pDevExt);
int QCMRD_L2ActiveBuffers(PDEVICE_EXTENSION pDevExt);
#endif // QCUSB_MULTI_READS

BOOLEAN QCRD_AdjustPaddingBytes(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock);

#endif // QCRD_H
