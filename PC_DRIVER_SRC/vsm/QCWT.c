/*===========================================================================
FILE: QCWT.c

DESCRIPTION:
   This file contains implementations for writing data to USB device.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003-2007 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#include <stdio.h>
#include "QCMAIN.h"
#include "QCPTDO.h"
#include "QCWT.h"
#include "QCUTILS.h"
#include "QCDSP.h"
#include "QCMGR.h"
#include "QCPWR.h"

extern NTKERNELAPI VOID IoReuseIrp(IN OUT PIRP Irp, IN NTSTATUS Iostatus);

NTSTATUS QCWT_Write(IN PDEVICE_OBJECT CalledDO, IN PIRP pIrp)
{
   PDEVICE_OBJECT DeviceObject;
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation (pIrp);
   KIRQL irql = KeGetCurrentIrql();

   DeviceObject = QCPTDO_FindPortDOByFDO(CalledDO, irql);
   if (DeviceObject == NULL)
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> WIRP: 0x%p (No Port for 0x%p)\n", gDeviceName, pIrp, CalledDO)
      );
      return QcCompleteRequest(pIrp, STATUS_DELETE_PENDING, 0);
   }
   pDevExt = DeviceObject -> DeviceExtension;

   if (pDevExt->bInService == FALSE)
   {
      ntStatus = STATUS_UNSUCCESSFUL;

      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_RIRP,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> WIRP (Cus 0x%x/%ldB) 0x%p - no service\n", pDevExt->PortName, ntStatus,
          pIrp->IoStatus.Information, pIrp)
      );
      return QcCompleteRequest(pIrp, ntStatus, 0);
   }

/***

   #ifdef QCUSB_STACK_IO_ON
   if ((CalledDO == DeviceObject) && inDevState(DEVICE_STATE_PRESENT_AND_STARTED) &&
       (pDevExt->bStackOpen == TRUE))
   {
      return QCDSP_SendIrpToStack(DeviceObject, pIrp, "WIRP");
   }
   #endif // QCUSB_STACK_IO_ON
***/

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WIRP,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s 0x%p> WIRP 0x%p => t 0x%p[%d/%d:%d]\n", pDevExt->PortName, DeviceObject, pIrp,
       KeGetCurrentThread(), pIrp->CurrentLocation, pIrp->StackCount, irql)
   );

   // HG_DBG
   // DbgPrint("<%s 0x%p> WIRP 0x%p => t 0x%p[%d/%d:%d]\n", pDevExt->PortName, DeviceObject, pIrp,
   //     KeGetCurrentThread(), pIrp->CurrentLocation, pIrp->StackCount, irql);

   ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, pIrp);
   if (!NT_SUCCESS(ntStatus))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> WIRP 0x%p(Crm 0x%x)\n", pDevExt->PortName, pIrp, ntStatus)
      );
      return QcCompleteRequest(pIrp, ntStatus, 0);
   }

   QcInterlockedIncrement(2, pIrp, 10);

   if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(pIrp);

      if ((gVendorConfig.DriverResident == 0) && (pDevExt->bInService == FALSE))
      {
         pIrp->IoStatus.Information = 0;
         pIrp->IoStatus.Status = ntStatus = STATUS_DELETE_PENDING;
      }
      else
      {
         if (pIrp->MdlAddress)
         {
            pIrp->IoStatus.Information = MmGetMdlByteCount(pIrp->MdlAddress);
         }
         else
         {
            pIrp->IoStatus.Information = irpStack->Parameters.Write.Length;
         }
         pIrp->IoStatus.Status = ntStatus = STATUS_SUCCESS;
      }
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> WIRP (Cdp 0x%x/%ldB) 0x%p No DEV (0x%x)\n", pDevExt->PortName,
           ntStatus, pIrp->IoStatus.Information, pIrp, pDevExt->bmDevState)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, 2);
      IoCompleteRequest(pIrp, IO_NO_INCREMENT);
      goto Exit;
   }

   if (pDevExt->Sts.lRmlCount[4] <= 0 && pDevExt->bInService == TRUE) // device not opened
   {
      pIrp->IoStatus.Information = 0;
      pIrp->IoStatus.Status = ntStatus = STATUS_CANCELLED;

      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> WIRP 0x%p(Cop 0x%x)\n", pDevExt->PortName, pIrp, ntStatus)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, 2);
      IoCompleteRequest(pIrp, IO_NO_INCREMENT);
      goto Exit;
   }

   if ((pDevExt->bInService == FALSE) || (pDevExt->bDeviceRemoved == TRUE))
   {  //the device has not been opened (CreateFile)
      pIrp->IoStatus.Information = 0;
      pIrp->IoStatus.Status = ntStatus = STATUS_DELETE_PENDING;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> WIRP 0x%p(Cns 0x%x)\n", pDevExt->PortName, pIrp, ntStatus)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, 2);
      IoCompleteRequest(pIrp, IO_NO_INCREMENT);
      goto Exit;
   } 

   if (pDevExt->ucDeviceType >= DEVICETYPE_CTRL)
   {
      pIrp->IoStatus.Information = 0;
      pIrp->IoStatus.Status = ntStatus = STATUS_UNSUCCESSFUL;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> WIRP 0x%p (Cus 0x%x) unsupported\n", pDevExt->PortName,
          pIrp, ntStatus)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, 2);
      IoCompleteRequest(pIrp, IO_NO_INCREMENT);
      goto Exit;
   }

   QcExAllocateWriteIOB(pIOBlock, TRUE);
   if (!pIOBlock)
   {
      pIrp->IoStatus.Information = 0;
      pIrp->IoStatus.Status = ntStatus = STATUS_NO_MEMORY;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> WIRP 0x%p(Ciob 0x%x)\n", pDevExt->PortName, pIrp, ntStatus)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, 2);
      IoCompleteRequest(pIrp, IO_NO_INCREMENT);
      goto Exit;
   }
   RtlZeroMemory(pIOBlock, sizeof(VXD_WDM_IO_CONTROL_BLOCK));
   pIOBlock->pSerialDeviceObject = DeviceObject;
   pIOBlock->pCallingIrp         = pIrp;
   pIOBlock->TimerExpired        = FALSE;

   // According to Windows DDK built on July 23, 2004, KeInitializeDpc
   // can be running at any IRQL.
   KeInitializeDpc(&pIOBlock->TimeoutDpc, WriteTimeoutDpc, pIOBlock);

   if (pIrp->MdlAddress) // this IRP is direct I/O
   {
      pIOBlock->pBufferToDevice = MmGetSystemAddressForMdlSafe(pIrp->MdlAddress,HighPagePriority);
      pIOBlock->ulBTDBytes = MmGetMdlByteCount(pIrp->MdlAddress);
   }
   else
   {
      pIOBlock->pBufferToDevice = pIrp->AssociatedIrp.SystemBuffer;
      pIOBlock->ulBTDBytes = irpStack->Parameters.Write.Length;
   }
   pIOBlock->pCompletionRoutine = (STE_COMPLETIONROUTINE) WriteIrpCompletion;
   pIOBlock->bPurged = FALSE;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WIRP,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> ENQ WIRP 0x%p/0x%p =>%ldB\n", pDevExt->PortName, pIrp, pIOBlock, pIOBlock->ulBTDBytes)
   );

   ntStatus = STESerial_Write(pIOBlock);  // Enqueue IOB
   if ((ntStatus != STATUS_PENDING) && (ntStatus != STATUS_TIMEOUT))
   {
      // if ((ntStatus == STATUS_CANCELLED) || (ntStatus == STATUS_DELETE_PENDING))
      if (!NT_SUCCESS(ntStatus))
      {
         pIrp->IoStatus.Information = 0;
      }
      pIrp->IoStatus.Status = ntStatus;
      QcExFreeWriteIOB(pIOBlock, TRUE);

      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> WIRP 0x%p(C 0x%x)\n", pDevExt->PortName, pIrp, ntStatus)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, 2);
      IoCompleteRequest(pIrp, IO_NO_INCREMENT);
   }

Exit:

   return ntStatus;
}  // QCWT_Write

VOID CancelWriteRoutine(PDEVICE_OBJECT CalledDO, PIRP pIrp)
{
   KIRQL irql = KeGetCurrentIrql();
   PDEVICE_OBJECT DeviceObject = QCPTDO_FindPortDOByFDO(CalledDO, irql);
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   BOOLEAN bFreeIOBlock = FALSE;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   #ifdef QCUSB_MULTI_WRITES
   if (pDevExt->UseMultiWrites == TRUE)
   {
      QCMWT_CancelWriteRoutine(CalledDO, pIrp);
      return;
   }
   #endif

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WIRP,
      QCSER_DBG_LEVEL_ERROR,
      ("<%s> CancelWriteRoutine-0 IRP 0x%p\n", pDevExt->PortName, pIrp)
   );
   IoReleaseCancelSpinLock(pIrp->CancelIrql);

   QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
   IoSetCancelRoutine(pIrp, NULL); // not necessary

   // remove it from pIOBlock
   pIOBlock = FindWriteIrp(pDevExt, pIrp, FALSE);

   if (pIOBlock == NULL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> CxlW: no IOB for IRP 0x%p, simply complete\n", pDevExt->PortName, pIrp)
      );

      // SR392475
      pIrp->IoStatus.Status = STATUS_CANCELLED;
      pIrp->IoStatus.Information = 0;
      InsertTailList(&pDevExt->WtCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
      KeSetEvent(&pDevExt->InterruptEmptyWtQueueEvent, IO_NO_INCREMENT, FALSE);
      // End of SR392475
   }
   else
   {
      bFreeIOBlock = DeQueueIOBlock(pIOBlock, &pDevExt->pWriteHead);
      AbortWriteTimeout(pIOBlock);
      ASSERT(pIOBlock->pCallingIrp==pIrp);

      // If the IRP being cancelled is the current IRP
      if (pDevExt->pWriteCurrent == pIOBlock)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WIRP,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> CxlW: cxl curr W\n", pDevExt->PortName)
         );
         pIOBlock->TimerExpired = TRUE; // pretend to time out
         IoSetCancelRoutine(pIrp, CancelWriteRoutine);  // restore the cxl routine
         KeSetEvent(&pDevExt->CancelCurrentWriteEvent,IO_NO_INCREMENT,FALSE);
         QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
         return;
      }

      pIOBlock->pCallingIrp = NULL;

      pIrp->IoStatus.Status = STATUS_CANCELLED;
      pIrp->IoStatus.Information = 0;
      InsertTailList(&pDevExt->WtCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
      KeSetEvent(&pDevExt->InterruptEmptyWtQueueEvent, IO_NO_INCREMENT, FALSE);
   }

   if (bFreeIOBlock)
   {
      QcExFreeWriteIOB(pIOBlock, FALSE);
   }

   QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
}  // CancelWriteRoutine


NTSTATUS WriteIrpCompletion
(
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock,
   BOOLEAN                   AllowSpinlock,
   UCHAR                     cookie
)
{
   PIRP pIrp;
   NTSTATUS ntStatus;
   PDEVICE_EXTENSION pDevExt = pIOBlock->pSerialDeviceObject->DeviceExtension;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   if (AllowSpinlock == TRUE)
   {
      QcAcquireSpinLockWithLevel(&pDevExt->WriteSpinLock, &levelOrHandle, irql);
   }
   pIrp = pIOBlock->pCallingIrp;
   ntStatus = pIOBlock->ntStatus;
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> WIC<%d>: IOB 0x%p nt 0x%x IRP 0x%p\n", pDevExt->PortName, cookie,
        pIOBlock, ntStatus, pIrp)
   );

   if (ntStatus == STATUS_SUCCESS)
   {
      pDevExt->WtErrorCount = 0;
   }
   else if ((ntStatus == STATUS_DEVICE_DATA_ERROR) ||
            (ntStatus == STATUS_DEVICE_NOT_READY)  ||
            (ntStatus == STATUS_UNSUCCESSFUL))
   {
      pDevExt->WtErrorCount++;

      // after some magic number of times of failure,
      // we mark the device as 'removed'
      if ((pDevExt->WtErrorCount > pDevExt->NumOfRetriesOnError) && (pDevExt->ContinueOnDataError == FALSE))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> WC: failure %d, dev removed\n", pDevExt->PortName, pDevExt->WtErrorCount)
         );
         clearDevState(DEVICE_STATE_DEVICE_STARTED);
         pDevExt->bDeviceRemoved = TRUE;
         pDevExt->WtErrorCount = pDevExt->NumOfRetriesOnError;
         QCSER_PostRemovalNotification(pDevExt);
      }
   }
   else if (ntStatus == STATUS_DELETE_PENDING)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> WC: dev removed\n", pDevExt->PortName)
      );
      clearDevState(DEVICE_STATE_DEVICE_STARTED);
      pDevExt->bDeviceRemoved = TRUE;
   }

   if (pIrp) // if it wasn't canceled out of the block
   {
      if (!IoSetCancelRoutine(pIrp, NULL))
      {
         if (AllowSpinlock == TRUE)
         {
            QcReleaseSpinLockWithLevel(&pDevExt->WriteSpinLock, levelOrHandle, irql);
         }
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WIRP,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> WIRPC: WIRP 0x%p already cxled\n", pDevExt->PortName, pIrp)
         );
         // SR392475
         AbortWriteTimeout(pIOBlock); // make sure timer is deleted before freeing IOB
         pIOBlock->TimerExpired = TRUE; 
         // End of SR392475
         goto ExitWriteCompletion;
      }

      if (pDevExt->bEnableByteStuffing == TRUE)
      {
         pIrp->IoStatus.Information = pIOBlock->ulBTDBytes -
                                      (pIOBlock->ulActiveBytes >> 1);
      }
      else
      {
         pIrp->IoStatus.Information = pIOBlock->ulBTDBytes -
                                      pIOBlock->ulActiveBytes;
      }

      if (pIOBlock->TimerExpired == FALSE)
      {
         AbortWriteTimeout(pIOBlock);
      }
      pIrp->IoStatus.Status = ntStatus;
      pIOBlock->pCallingIrp = NULL;

      InsertTailList(&pDevExt->WtCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
      KeSetEvent(&pDevExt->InterruptEmptyWtQueueEvent, IO_NO_INCREMENT, FALSE);

      if (AllowSpinlock == TRUE)
      {
         QcReleaseSpinLockWithLevel(&pDevExt->WriteSpinLock, levelOrHandle, irql);
      }
   }
   else
   {
      if (AllowSpinlock == TRUE)
      {
         QcReleaseSpinLockWithLevel(&pDevExt->WriteSpinLock, levelOrHandle, irql);
      }
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> WC NULL IRP\n", pDevExt->PortName)
      );
   }

ExitWriteCompletion:

   if (!pDevExt->pWriteHead)
   {
      ProcessNewUartState
      (
         pDevExt,
         SERIAL_EV_TXEMPTY,
         SERIAL_EV_TXEMPTY,
         FALSE
      );
   }

   return ntStatus;
}  // WriteIrpCompletion

// **************************************************************
/*
 *
 *   WRITE THREAD ASSEMBLY
 *
 */

NTSTATUS CancelWriteThread(PDEVICE_EXTENSION pDevExt, UCHAR cookie)
{
   NTSTATUS ntStatus;
   LARGE_INTEGER delayValue;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> CancelWriteThread %d\n", pDevExt->PortName, cookie)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> CancelWriteThread: wrong IRQL::%d - %d\n", pDevExt->PortName, KeGetCurrentIrql(), cookie)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if (pDevExt->bWtCancelStarted == TRUE)
   {
      while ((pDevExt->hWriteThreadHandle != NULL) || (pDevExt->pWriteThread != NULL))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> Wth cxl in pro\n", pDevExt->PortName)
         );
         QCSER_Wait(pDevExt, -(3 * 1000 * 1000));  // 0.3 sec
      }
      return STATUS_SUCCESS;
   }
   pDevExt->bWtCancelStarted = TRUE;

   if ((pDevExt->hWriteThreadHandle != NULL) || (pDevExt->pWriteThread != NULL))
   {
      KeClearEvent(&pDevExt->WriteThreadClosedEvent);
      KeSetEvent(&pDevExt->CancelWriteEvent,IO_NO_INCREMENT,FALSE);
   
      if (pDevExt->pWriteThread != NULL)
      {
         ntStatus = KeWaitForSingleObject
                    (
                       pDevExt->pWriteThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pDevExt->pWriteThread);
         KeClearEvent(&pDevExt->WriteThreadClosedEvent);
         _closeHandle(pDevExt->hWriteThreadHandle, "W-0");
         pDevExt->pWriteThread = NULL;
      }
      else  // best effort
      {
         ntStatus = KeWaitForSingleObject
                    (
                       &pDevExt->WriteThreadClosedEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         KeClearEvent(&pDevExt->WriteThreadClosedEvent);
         _closeHandle(pDevExt->hWriteThreadHandle, "W-3");
      }
   }
   pDevExt->bWtCancelStarted = FALSE;
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> Wth: OUT\n", pDevExt->PortName)
   );

   return STATUS_SUCCESS;
}  // CancelWriteThread


NTSTATUS STESerial_Write(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock)
{
   PDEVICE_OBJECT pDO;
   PDEVICE_EXTENSION pDevExt;
   PVXD_WDM_IO_CONTROL_BLOCK pWriteQueEntry;
   PVXD_WDM_IO_CONTROL_BLOCK pNextQueEntry;
   PIRP pIrp;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   OBJECT_ATTRIBUTES objAttr; 
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();
   PIO_STACK_LOCATION irpStack;

   // get device extension
   pDO = pIOBlock->pSerialDeviceObject;
   pDevExt = pDO->DeviceExtension;

   if(!pIOBlock)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> _Write STATUS_INVALID_PARAMETER\n", pDevExt->PortName)
      );
      return STATUS_INVALID_PARAMETER;
   }

   pIrp = pIOBlock->pCallingIrp;
   irpStack = IoGetCurrentIrpStackLocation(pIrp);

   // reject request if we're in the device has been removed
   if (pDevExt->bDeviceRemoved == TRUE)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> _Write STATUS_DELETE_PENDING (irp=0x%p)\n", pDevExt->PortName, pIrp)
      );
      pIOBlock->ntStatus = STATUS_DELETE_PENDING; // STATUS_DEVICE_NOT_CONNECTED;
      return pIOBlock->ntStatus;
   }

   // setup to que write
   if ((pIOBlock->ulBTDBytes == 0) &&
       (irpStack->MajorFunction == IRP_MJ_WRITE))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> _Write 0 byte: STATUS_SUCCESS\n", pDevExt->PortName)
      );
      pIOBlock->ntStatus = STATUS_SUCCESS;  // succeeded in writing 0 bytes
      return STATUS_SUCCESS;
   }

   if ((pIOBlock->pBufferToDevice == NULL) &&
       (irpStack->MajorFunction == IRP_MJ_WRITE))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> _Write STATUS_INVALID_PARAMETER 1\n", pDevExt->PortName)
      );
      pIOBlock->ntStatus = STATUS_INVALID_PARAMETER;
      return pIOBlock->ntStatus;
   }

   W_Enqueue:

   ntStatus = pIOBlock->ntStatus = STATUS_PENDING;
   InterlockedExchange(&(pIOBlock->lPurgeForbidden), pDevExt->lPurgeBegin);

   // IoSetCancelRoutine(pIrp, CancelWriteRoutine);
   // _IoMarkIrpPending(pIrp);

   pIrp->IoStatus.Information = 0;

   QcAcquireSpinLockWithLevel(&pDevExt->WriteSpinLock, &levelOrHandle, irql);

   if (irpStack->MajorFunction == IRP_MJ_WRITE)
   {
      if (StartWriteTimeout(pIOBlock) == FALSE)
      {
         ntStatus = pIOBlock->ntStatus = STATUS_UNSUCCESSFUL;
         QcReleaseSpinLockWithLevel(&pDevExt->WriteSpinLock, levelOrHandle, irql);
         return ntStatus;
      }
   }
   else
   {
      IoSetCancelRoutine(pIrp, CancelWriteRoutine);
      _IoMarkIrpPending(pIrp);
   }

   // setup to que and/or kick write thread

   // if the IRP was cancelled between SetTimer (where the cancel routine is set)
   // and now, then we need special processing here. Note: the cancel routine
   // will complete the IRP anyway if it's running.
   if (pIOBlock->pCallingIrp != NULL)
   {
      if (pIOBlock->pCallingIrp->Cancel)
      {
         AbortWriteTimeout(pIOBlock);
         if (IoSetCancelRoutine(pIOBlock->pCallingIrp, NULL) != NULL)
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> WIRP 0x%p pre-cxl-0\n", pDevExt->PortName, pIOBlock->pCallingIrp)
            );
            // The IRP will be cancelled once this status is returned.
            ntStatus = pIOBlock->ntStatus = STATUS_CANCELLED;
         }
         else
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> WIRP 0x%p pre-cxl-1\n", pDevExt->PortName, pIOBlock->pCallingIrp)
            );
            // The cancel routine is running, do not touch the IRP anymore,
            // let the party who nullified the cancel routine complete the IRP.
            ntStatus = pIOBlock->ntStatus = STATUS_PENDING;
            QcExFreeWriteIOB(pIOBlock, FALSE);
         }
         QcReleaseSpinLockWithLevel(&pDevExt->WriteSpinLock, levelOrHandle, irql);
         return ntStatus;
      }
      else
      {
         // if the read thread has been terminated by IRP_MJ_CLEANUP,
         // we need to re-start it.
         if ((pDevExt->pWriteThread == NULL)        &&
             (pDevExt->hWriteThreadHandle == NULL))
         {
            KeSetEvent
            (
               &(DeviceInfo[pDevExt->MgrId].DspStartDataThreadsEvent),
               IO_NO_INCREMENT,
               FALSE
            );
         }
      }
   }

   pWriteQueEntry = pDevExt->pWriteHead;

   if (pWriteQueEntry != NULL)
   {
      pNextQueEntry = pWriteQueEntry->pNextEntry;
      // an outstanding write, just link this one on
      while(pNextQueEntry)
      {
         pWriteQueEntry = pNextQueEntry;
         pNextQueEntry = pWriteQueEntry->pNextEntry;
      }
      pWriteQueEntry->pNextEntry = pIOBlock;
   }
   else
   {
      pDevExt->pWriteHead = pIOBlock;
      KeSetEvent
      (
         &pDevExt->KickWriteEvent,
         IO_NO_INCREMENT,
         FALSE
      ); // kick the write thread
   }

   if (pIOBlock->TimerExpired == TRUE)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> ENQ IOB-to 0x%p/0x%p => DN\n", pDevExt->PortName,
           pIOBlock->pCallingIrp, pIOBlock)
      );
      KeSetEvent
      (
         &pDevExt->WritePreTimeoutEvent,
         IO_NO_INCREMENT,
         FALSE
      );
   }
   else
   {
      QCPWR_CheckToWakeup(pDevExt, NULL, QCUSB_BUSY_WT, 2);
   }

   QcReleaseSpinLockWithLevel(&pDevExt->WriteSpinLock, levelOrHandle, irql);

   return ntStatus; // accepted the write, should be STATUS_PENDING
}  // STESerial_Write

NTSTATUS WriteCompletionRoutine
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pContext;

   if (pIrp->IoStatus.Status == STATUS_PENDING)
   {
      KeSetEvent
      (
         &pDevExt->KickWriteEvent,
         IO_NO_INCREMENT,
         FALSE
      );
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> ERR-WCR-0: 0x%p StkLo=%d/%d (0x%x)", pDevExt->PortName, pIrp,
           pIrp->CurrentLocation, pIrp->StackCount, pIrp->IoStatus.Status)
      );
      return STATUS_MORE_PROCESSING_REQUIRED;
   }

   KeSetEvent
   (
      pDevExt->pWriteEvents[WRITE_COMPLETION_EVENT_INDEX],
      IO_NO_INCREMENT,
      FALSE
   );

   QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_WT, FALSE, 4); // WT completion

   return STATUS_MORE_PROCESSING_REQUIRED;
}  // WriteCompletionRoutine


void STESerial_WriteThread(PVOID pContext)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pContext;
   PIRP pIrp;
   PVXD_WDM_IO_CONTROL_BLOCK pCurrIOBlock = NULL;
   PIO_STACK_LOCATION irpStack, pNextStack;
   PURB pUrb;
   ULONG ulTransferBytes;
   BOOLEAN bCancelled = FALSE;
   NTSTATUS  ntStatus;
   PKWAIT_BLOCK pwbArray;
   BOOLEAN bIrpResent = FALSE;
   ULONG i;
   UCHAR *pSrc, *pSrcEnd, *pDest;
   BOOLEAN bFirstTime = TRUE;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KEVENT dummyEvent;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> Wth: wrong IRQL::%d\n", pDevExt->PortName, KeGetCurrentIrql())
      );
      #ifdef DEBUG_MSGS
      _asm int 3;
      #endif
   }

   // allocate an urb for write operations
   pUrb = ExAllocatePool
          (
             NonPagedPool,
             sizeof( struct _URB_BULK_OR_INTERRUPT_TRANSFER )
          );
   if (pUrb == NULL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> Wth: NULL URB - STATUS_NO_MEMORY\n", pDevExt->PortName)
      );
      _closeHandle(pDevExt->hWriteThreadHandle, "W-4");
      pDevExt->bWtThreadInCreation = FALSE;
      KeSetEvent(&pDevExt->WriteThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }
   
   // allocate a wait block array for the multiple wait
   pwbArray = _ExAllocatePool
              (
                 NonPagedPool,
                 (WRITE_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK),
                 "STESerial_WriteThread.pwbArray"
              );
   if (!pwbArray)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> Wth: STATUS_NO_MEMORY 1\n", pDevExt->PortName)
      );
      ExFreePool(pUrb);
      _closeHandle(pDevExt->hWriteThreadHandle, "W-1");
      pDevExt->bWtThreadInCreation = FALSE;
      KeSetEvent(&pDevExt->WriteThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   // allocate irp to use for write operations
   pIrp = IoAllocateIrp((CCHAR)(pDevExt->MyDeviceObject->StackSize), FALSE );

   if (!pIrp )
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> Wth: NULL IRP - STATUS_NO_MEMORY\n", pDevExt->PortName)
      );
      ExFreePool(pUrb);
      ExFreePool(pwbArray);
      _closeHandle(pDevExt->hWriteThreadHandle, "W-2");
      pDevExt->bWtThreadInCreation = FALSE;
      KeSetEvent(&pDevExt->WriteThreadStartedEvent, IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   #ifdef ENABLE_LOGGING
   // Create logs
   if (pDevExt->EnableLogging == TRUE)
   {
      QCSER_CreateLogs(pDevExt, QCSER_CREATE_TX_LOG);
   }
   #ifdef QCSER_ENABLE_LOG_REC
   if (pDevExt->LogLatestPkts == TRUE)
   {
      RtlZeroMemory(pDevExt->TxLogRec, sizeof(LogRecType)*NUM_LATEST_PKTS);
      pDevExt->TxLogRecIndex = 0;
   }
   #endif // QCSER_ENABLE_LOG_REC
   #endif // ENABLE_LOGGING

   // clear any outbound flow-control
   pDevExt->pSerialStatus->HoldReasons = 0;

   // Set WRITE thread priority
   KeSetPriorityThread(KeGetCurrentThread(), QCSER_WT_PRIORITY);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> W pri=%d\n", pDevExt->PortName, KeQueryPriorityThread(KeGetCurrentThread()))
   );

   pDevExt->bWriteActive = FALSE;
   pDevExt->pWriteCurrent = NULL;

   pDevExt->pWriteEvents[QC_DUMMY_EVENT_INDEX] = &dummyEvent;
   KeInitializeEvent(&dummyEvent, NotificationEvent, FALSE);
   KeClearEvent(&dummyEvent);

   pDevExt->bWriteStopped = pDevExt->PowerSuspended;
   
   while(TRUE)
   {
      if (pDevExt->pSerialStatus->HoldReasons && (bCancelled == FALSE))
      {
         goto wait_for_completion;
      }

      QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

      if ((pDevExt->pWriteHead) && (pDevExt->bWriteActive == FALSE) &&
          (inDevState(DEVICE_STATE_PRESENT_AND_STARTED)))
      {
         if (bCancelled == FALSE)
         {
            // yes we have a write to deque
            if ((QCPWR_CheckToWakeup(pDevExt, NULL, QCUSB_BUSY_WT, 1) == TRUE) ||
                (pDevExt->bWriteStopped == TRUE))
            {
               QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
               goto wait_for_completion;  // wait for the kick event
            }
         }

         pDevExt->pWriteCurrent = pCurrIOBlock = pDevExt->pWriteHead;
         pDevExt->pWriteHead = pCurrIOBlock->pNextEntry;

         QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

         if (pCurrIOBlock->pCallingIrp != NULL)
         {
            irpStack = IoGetCurrentIrpStackLocation(pCurrIOBlock->pCallingIrp);
            if (irpStack->MajorFunction == IRP_MJ_FLUSH_BUFFERS)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> Wth: buffers flushed (IRP 0x%p)\n", pDevExt->PortName,
                    pCurrIOBlock->pCallingIrp)
               );
               pCurrIOBlock->ntStatus = STATUS_SUCCESS;
               pCurrIOBlock->ulBTDBytes = pCurrIOBlock->ulActiveBytes = 0;
               pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, TRUE, 15);
               pDevExt->pWriteCurrent = NULL;  // need this before free IOB
               QcExFreeWriteIOB(pCurrIOBlock, TRUE);
               QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_WT, TRUE, 12);
               continue;
            }
         }

         // check any status' which would ask us to drain que
         if (pCurrIOBlock->TimerExpired == TRUE)
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_FORCE,
               ("<%s> Wth: pre-to 0x%p (%lu/%lu)\n", pDevExt->PortName,
                 pCurrIOBlock, pCurrIOBlock->ulActiveBytes, pCurrIOBlock->ulBTDBytes)
            );
            pCurrIOBlock->ntStatus = STATUS_TIMEOUT;
            pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, TRUE, 9);
            pDevExt->pWriteCurrent = NULL;  // need this before free IOB
            QcExFreeWriteIOB(pCurrIOBlock, TRUE);
            QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_WT, TRUE, 13);
            continue;
         }
         else if ((bCancelled == TRUE) || pCurrIOBlock->bPurged == TRUE)
         {
            // send cancel completions for each que'ed i/o
            pCurrIOBlock->ntStatus = STATUS_CANCELLED;
            pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, TRUE, 1);
            pDevExt->pWriteCurrent = NULL;  // need this before free IOB
            QcExFreeWriteIOB(pCurrIOBlock, TRUE);
            QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_WT, TRUE, 14);
            continue;
         }
         // setup irp for i/o

         // setup the URB  for this request
         // setup the pioblock for possible multiple writes
         if (pCurrIOBlock->pActiveBuffer == NULL)
         {
            if (pDevExt->bEnableByteStuffing == TRUE)
            {
               if (pDevExt->ulByteStuffingBufLen < pCurrIOBlock->ulBTDBytes*2+2)
               {
                  if (pDevExt->pByteStuffingBuffer != NULL)
                  {
                     ExFreePool(pDevExt->pByteStuffingBuffer);
                  }
                  // allocate larger buffer
                  pDevExt->ulByteStuffingBufLen = pCurrIOBlock->ulBTDBytes * 2+2;
                  pDevExt->pByteStuffingBuffer = _ExAllocatePool
                                                 (
                                                    NonPagedPool,
                                                    pDevExt->ulByteStuffingBufLen,
                                                    "WriteThread, ByteStuffingBuf"
                                                 );
                  if (pDevExt->pByteStuffingBuffer == NULL)
                  {
                     QCSER_DbgPrint
                     (
                        QCSER_DBG_MASK_WRITE,
                        QCSER_DBG_LEVEL_CRITICAL,
                        ("<%s> Wth: BTST buf - STATUS_NO_MEMORY\n", pDevExt->PortName)
                     );

                     // we cancel the write request
                     pDevExt->ulByteStuffingBufLen = 0;
                     pCurrIOBlock->ntStatus = STATUS_NO_MEMORY; // STATUS_CANCELLED;
                     pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, TRUE, 3);
                     pDevExt->pWriteCurrent = NULL;  // need this before free IOB
                     QcExFreeWriteIOB(pCurrIOBlock, TRUE);
                     QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_WT, TRUE, 15);
                     continue;
                  }

                  RtlFillMemory
                  (
                     pDevExt->pByteStuffingBuffer,
                     pDevExt->ulByteStuffingBufLen,
                     QCSER_STUFFING_BYTE
                  );
               }  // if (pDevExt->ulByteStuffingBufLen < pCurrIOBlock->ulBTDBytes*2+2)

               pSrc = (UCHAR *)pCurrIOBlock->pBufferToDevice;
               pSrcEnd = pSrc + pCurrIOBlock->ulBTDBytes;
               pDest = (UCHAR *)pDevExt->pByteStuffingBuffer;
               while (pSrc < pSrcEnd)
               {
                  pDest += 2;
                  *pDest++ = *pSrc++;
                  *pDest++ = *pSrc++;
               }
               pCurrIOBlock->pActiveBuffer = pDevExt->pByteStuffingBuffer;
               if (pCurrIOBlock->ulBTDBytes & 0x1)  // ood number of bytes
               {
                  pCurrIOBlock->ulActiveBytes = pCurrIOBlock->ulBTDBytes * 2 + 1;
               }
               else
               {
                  pCurrIOBlock->ulActiveBytes = pCurrIOBlock->ulBTDBytes * 2;
               }
            } // if (pDevExt->bEnableByteStuffing == TRUE)
            else
            {
               pCurrIOBlock->pActiveBuffer = pCurrIOBlock->pBufferToDevice;
               pCurrIOBlock->ulActiveBytes = pCurrIOBlock->ulBTDBytes;
            }
         } // if(pCurrIOBlock->pActiveBuffer == NULL)

         ulTransferBytes =
            (pDevExt->lWriteBufferUnit < pCurrIOBlock->ulActiveBytes) ?
               pDevExt->lWriteBufferUnit : pCurrIOBlock->ulActiveBytes;

         RtlZeroMemory
         (
            pUrb,
            sizeof( struct _URB_BULK_OR_INTERRUPT_TRANSFER )
         ); // clear out the urb we reuse

         UsbBuildInterruptOrBulkTransferRequest
         (
            pUrb,
            sizeof (struct _URB_BULK_OR_INTERRUPT_TRANSFER),
            pDevExt->Interface[pDevExt->DataInterface]
                ->Pipes[pDevExt->BulkPipeOutput].PipeHandle,
            pCurrIOBlock->pActiveBuffer,
            NULL,
            ulTransferBytes,
            USBD_TRANSFER_DIRECTION_OUT,
            NULL
         );

         IoReuseIrp(pIrp, STATUS_SUCCESS);  // HGUO
         /*****
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> Wth-0: 0x%p StkLo=%d/%d", pDevExt->PortName, pIrp, pIrp->CurrentLocation, pIrp->StackCount)
         );
         *****/
         pNextStack = IoGetNextIrpStackLocation( pIrp );
         pNextStack -> Parameters.DeviceIoControl.IoControlCode =
            IOCTL_INTERNAL_USB_SUBMIT_URB;
         pNextStack -> MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
         pNextStack->Parameters.Others.Argument1 = pUrb;

         IoSetCompletionRoutine
         (
            pIrp,
            WriteCompletionRoutine,
            pDevExt,
            TRUE,
            TRUE,
            TRUE
         );

         #ifdef ENABLE_LOGGING
         if ((pDevExt->EnableLogging == TRUE) && (pDevExt->hTxLogFile != NULL))
         {
            QCSER_LogData
            (
               pDevExt,
               pDevExt->hTxLogFile,
               pCurrIOBlock->pActiveBuffer, 
               ulTransferBytes,
               QCSER_LOG_TYPE_WRITE
            );
         }
         #ifdef QCSER_ENABLE_LOG_REC
         if (pDevExt->LogLatestPkts == TRUE)
         {
            QCSER_GetSystemTimeString(pDevExt->TxLogRec[pDevExt->TxLogRecIndex].TimeStamp);
            pDevExt->TxLogRec[pDevExt->TxLogRecIndex].PktLength = ulTransferBytes;
            RtlCopyBytes
            (
               (PVOID)pDevExt->TxLogRec[pDevExt->TxLogRecIndex].Data,
               (PVOID)pCurrIOBlock->pActiveBuffer,
               ulTransferBytes > 64? 64:  ulTransferBytes
            );
            if (++(pDevExt->TxLogRecIndex) >= NUM_LATEST_PKTS)
            {
               pDevExt->TxLogRecIndex = 0;
            }
         }
         #endif // QCSER_ENABLE_LOG_REC
         #endif // ENABLE_LOGGING

         pDevExt->bWriteActive = TRUE;
         ntStatus = IoCallDriver(pDevExt->StackDeviceObject,pIrp);
         if(ntStatus != STATUS_PENDING)
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_CRITICAL,
               ("<%s> Wth: IoCallDriver rtn 0x%x", pDevExt->PortName, ntStatus)
            );
         }
      } // end if WriteHead
      else
      {
         QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
      }

      if ((pDevExt->bWriteActive == FALSE) && (bCancelled == TRUE))
      {
         // if nothings active and we're cancelled, bail
         break; // goto exit_WriteThread; 
      }

     // No matter what IoCallDriver returns, we always wait on the kernel event
     // we created earlier. Our completion routine will gain control when the IRP
     // completes to signal this event. -- Walter Oney's WDM book page 228
wait_for_completion:
      // wait for action

      if (bFirstTime == TRUE)
      {
         bFirstTime = FALSE;
         KeSetEvent(&pDevExt->WriteThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      }

      // if nothing in the queue, we just wait for a KICK event
      ntStatus = KeWaitForMultipleObjects
                 (
                    WRITE_EVENT_COUNT,
                    (VOID **) &pDevExt->pWriteEvents,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE, // non-alertable // TRUE,
                    NULL,
                    pwbArray
                 );

      switch(ntStatus)
      {
         case QC_DUMMY_EVENT_INDEX:
         {
            KeClearEvent(&dummyEvent);
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> W: DUMMY_EVENT\n", pDevExt->PortName)
            );
            goto wait_for_completion;
         }

         case WRITE_COMPLETION_EVENT_INDEX:
         {
            // reset write completion event
            KeClearEvent(&pDevExt->WriteCompletionEvent);
            pDevExt->bWriteActive = FALSE;

            /*****
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_CRITICAL,
               ("<%s> Wth-1: 0x%p StkLo=%d/%d (0x%x)-%d", pDevExt->PortName, pIrp,
                 pIrp->CurrentLocation, pIrp->StackCount, pIrp->IoStatus.Status,
                 pDevExt->bWriteActive)
            );
            *****/
            // check completion status

            QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

            pDevExt->pWriteCurrent = NULL;

            // ----------- HGUO 20011003 ------------
            if (pCurrIOBlock->bPurged == TRUE)
            {
               pCurrIOBlock->ntStatus = STATUS_CANCELLED;

               QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

               pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, TRUE, 5);
               QcExFreeWriteIOB(pCurrIOBlock, TRUE);
               continue;
            }
            // ----------- End of HGUO 20011003 ------------

            ntStatus = pIrp->IoStatus.Status;

            // log status
            #ifdef ENABLE_LOGGING
            if ((pDevExt->EnableLogging == TRUE) && (pDevExt->hTxLogFile != NULL))
            {
               if (ntStatus != STATUS_SUCCESS)
               {
                  QCSER_LogData
                  (
                     pDevExt,
                     pDevExt->hTxLogFile,
                     (PVOID)&ntStatus,
                     sizeof(NTSTATUS),
                     QCSER_LOG_TYPE_RESPONSE_WT
                  );
               }
            }
            #endif // ENABLE_LOGGING

            if (ntStatus == STATUS_SUCCESS)
            {
               bIrpResent = FALSE;

               // subtract the bytes transfered from the requested bytes
               ulTransferBytes =
                  pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;
               pCurrIOBlock->ulActiveBytes -= ulTransferBytes;
               pDevExt->pPerfstats->TransmittedCount += ulTransferBytes;

               // have we written the full request yet?
               if ((pCurrIOBlock->ulActiveBytes > 0) && (pCurrIOBlock->TimerExpired == FALSE))
               {
                  // update buffer pointer
                  (PUCHAR) pCurrIOBlock->pActiveBuffer +=
                     pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;
                  // reque the io
                  pCurrIOBlock->pNextEntry = pDevExt->pWriteHead;
                  pDevExt->pWriteHead = pCurrIOBlock;
                  pCurrIOBlock = NULL; // not necessary

                  QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                  continue; // go around again
               }
            } // if STATUS_SUCCESS
            else // error???
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> Wth: TX failure 0x%x xferred %u/%u", pDevExt->PortName, ntStatus,
                    pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength,
                    pCurrIOBlock->ulActiveBytes)
               );
               if ((pDevExt->RetryOnTxError == TRUE) && (bIrpResent == FALSE) && (pDevExt->bWriteStopped == FALSE))
               {
                  QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                  if (ntStatus == STATUS_DEVICE_DATA_ERROR) // C000009C
                  {
//                   QCUSB_ResetOutput(pDevExt->MyDeviceObject);
                  }
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_WRITE,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> Wth: TX retry", pDevExt->PortName)
                  );

                  // for safety, re-build IRP
                  RtlZeroMemory
                  (
                     pUrb,
                     sizeof( struct _URB_BULK_OR_INTERRUPT_TRANSFER )
                  ); // clear out the urb we reuse

                   // Build a URB for our bulk data transfer
                  UsbBuildInterruptOrBulkTransferRequest
                  (
                     pUrb,
                     sizeof (struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                     pDevExt -> Interface[pDevExt->DataInterface]
                         -> Pipes[pDevExt->BulkPipeOutput].PipeHandle,
                     pCurrIOBlock->pActiveBuffer,
                     NULL,
                     ulTransferBytes,
                     USBD_TRANSFER_DIRECTION_OUT,
                     NULL
                  );

                  IoReuseIrp(pIrp, STATUS_SUCCESS);  // HGUO
                  pNextStack = IoGetNextIrpStackLocation( pIrp );
                  pNextStack -> Parameters.DeviceIoControl.IoControlCode =
                     IOCTL_INTERNAL_USB_SUBMIT_URB;
                  pNextStack -> MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
                  pNextStack->Parameters.Others.Argument1 = pUrb;

                  IoSetCompletionRoutine
                  (
                     pIrp,
                     WriteCompletionRoutine,
                     pDevExt,
                     TRUE,
                     TRUE,
                     TRUE
                  );

                  // resend the write once
                  #ifdef ENABLE_LOGGING
                  if ((pDevExt->EnableLogging == TRUE) && (pDevExt->hTxLogFile != NULL))
                  {
                     QCSER_LogData
                     (
                        pDevExt,
                        pDevExt->hTxLogFile,
                        pCurrIOBlock->pActiveBuffer,
                        ulTransferBytes,
                        QCSER_LOG_TYPE_RESEND
                     );
                  }
                  #endif // ENABLE_LOGGING

                  bIrpResent = TRUE;

                  pDevExt->bWriteActive = TRUE;
                  ntStatus = IoCallDriver(pDevExt->StackDeviceObject,pIrp);
                  if(ntStatus != STATUS_PENDING)
                  {
                     QCSER_DbgPrint
                     (
                        QCSER_DBG_MASK_WRITE,
                        QCSER_DBG_LEVEL_CRITICAL,
                        ("<%s> Wth: IoCallDriver rtn 0x%x -1", pDevExt->PortName, ntStatus)
                     );
                  }
                  goto wait_for_completion;
               } // Resend IRP
            }

            // if(bCancelled)
            if ((ntStatus == STATUS_CANCELLED) || (pDevExt->bDeviceRemoved == TRUE))
            {
               if (pCurrIOBlock->TimerExpired == TRUE)
               {
                  ntStatus = STATUS_TIMEOUT;
               }
               else
               {
                  ntStatus = STATUS_CANCELLED;
               }
            }

            QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

            // Reset pipe if halt, which runs at PASSIVE_LEVEL
            if ((ntStatus == STATUS_DEVICE_DATA_ERROR) ||
                (ntStatus == STATUS_DEVICE_NOT_READY)  ||
                (ntStatus == STATUS_UNSUCCESSFUL))
            {
               if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_WRITE,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> MWT: resetting pipe OUT 0x%x", pDevExt->PortName, ntStatus)
                  );
                  QCUSB_ResetOutput(pDevExt->MyDeviceObject, QCUSB_RESET_HOST_PIPE);
                  QCSER_Wait(pDevExt, -(50 * 1000L)); // 5ms
               }
            }

            pCurrIOBlock->ntStatus = ntStatus;
            pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, TRUE, 7);
            QcExFreeWriteIOB(pCurrIOBlock, TRUE);
            break;
         }
         
         case WRITE_PRE_TIMEOUT_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->WritePreTimeoutEvent);
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> WRITE_PRE_TIMEOUT\n", pDevExt->PortName)
            );

            QCWT_ProcessPreTimeoutIOB(pDevExt);
            break;
         }

         case KICK_WRITE_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->KickWriteEvent);

            if (pDevExt->bWriteActive == TRUE)
            {
               // if a write is active, we can ignore activations, but must
               // continue to wait for the completion
               goto wait_for_completion;
            }
            else if (pCurrIOBlock != NULL) // if current IOB going
            {
               if (pCurrIOBlock->ulActiveBytes > 0)
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_WRITE,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> ERR-Wth: CurrIOB - 0x%p(%luB)", pDevExt->PortName,
                       pCurrIOBlock,  pCurrIOBlock->ulActiveBytes)
                  );
                  goto wait_for_completion;
               }
            }
            // else we just go around the loop again, picking up the next
            // write entry
            continue;
         }

         case WRITE_CANCEL_CURRENT_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->CancelCurrentWriteEvent);
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> W WRITE_CANCEL_CURRENT-0", pDevExt->PortName)
            );

            if ((pDevExt->bWriteActive == TRUE) && (pCurrIOBlock != NULL))
            {
               // make sure nothing changed between signal trigger point and now
               if (pCurrIOBlock->TimerExpired == TRUE)
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_WRITE,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> Wth: Cxl curr\n", pDevExt->PortName)
                  );
                  IoCancelIrp(pIrp);

                  goto wait_for_completion;
               }
            }
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> W WRITE_CANCEL_CURRENT-1", pDevExt->PortName)
            );
            break;

         }
         case CANCEL_EVENT_INDEX:
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> W CANCEL_EVENT_INDEX-0", pDevExt->PortName)
            );

            // clear cancel event so we don't reactivate
            KeClearEvent(&pDevExt->CancelWriteEvent);

            #ifdef ENABLE_LOGGING
            QCSER_LogData
            (
               pDevExt,
               pDevExt->hTxLogFile,
               (PVOID)NULL,
               0,
               QCSER_LOG_TYPE_CANCEL_THREAD
            );
            #endif // ENABLE_LOGGING

            // signal the loop that a cancel has occurred
            bCancelled = TRUE;
            if (pDevExt->bWriteActive == TRUE)
            {
               // cancel outstanding irp
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> Wth: CANCEL - IRP\n", pDevExt->PortName)
               );
               IoCancelIrp(pIrp);

               // wait for writes to complete, don't cancel
               // if a write is active, continue to wait for the completion
               // we pick up the canceled status at the top of the service
               // loop
               goto wait_for_completion;
            }
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> W CANCEL_EVENT_INDEX-1", pDevExt->PortName)
            );
            break; // goto exit_WriteThread; // nothing active exit
         }
         case WRITE_PURGE_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->WritePurgeEvent);
            if (pDevExt->bWriteActive == TRUE)
            {
               // cancel outstanding irp
               IoCancelIrp(pIrp);

               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> Wth: - Purged active\n", pDevExt->PortName)
               );
               goto wait_for_completion;
            }
            break;
         }

         case WRITE_STOP_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->WriteStopEvent);
            pDevExt->bWriteStopped = TRUE;

            if (pDevExt->bWriteActive == TRUE)
            {
               // cancel outstanding irp
               IoCancelIrp(pIrp);

               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> Wth: - Stop IRP\n", pDevExt->PortName)
               );

               KeSetEvent(&pDevExt->WriteStopAckEvent, IO_NO_INCREMENT, FALSE);

               goto wait_for_completion;
            }

            KeSetEvent(&pDevExt->WriteStopAckEvent, IO_NO_INCREMENT, FALSE);

            break;
         }

         case WRITE_RESUME_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->WriteResumeEvent);
            if (TRUE == (pDevExt->bWriteStopped = pDevExt->PowerSuspended))
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> Wth: - Resume in suspend mode, no act\n", pDevExt->PortName)
               );
               goto wait_for_completion;
            }

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Wth: - Resume act %d\n", pDevExt->PortName, pDevExt->bWriteActive)
            );

            // Kick the write
            KeSetEvent
            (
               &pDevExt->KickWriteEvent,
               IO_NO_INCREMENT,
               FALSE
            );
            goto wait_for_completion;

            break;
         }

         default:
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> Wth: default sig 0x%x", pDevExt->PortName, ntStatus)
            );
            // log status
            #ifdef ENABLE_LOGGING
            QCSER_LogData
            (
               pDevExt,
               pDevExt->hTxLogFile,
               (PVOID)&ntStatus,
               sizeof(NTSTATUS),
               QCSER_LOG_TYPE_RESPONSE_WT
            );
            #endif // ENABLE_LOGGING

            // Ignore for now
            break;

            bCancelled = TRUE;
            if (pDevExt->bWriteActive == TRUE)
            {
               IoCancelIrp(pIrp);
               goto wait_for_completion;
            }
            break; 
         } // default
      }  // switch

      // go round again
   }  // end while forever 

exit_WriteThread:

   if (pDevExt->hTxLogFile != NULL)
   {
      #ifdef ENABLE_LOGGING
      QCSER_LogData
      (
         pDevExt,
         pDevExt->hTxLogFile,
         (PVOID)NULL,
         0,
         QCSER_LOG_TYPE_THREAD_END
      );
      #endif // ENABLE_LOGGING
      ZwClose(pDevExt->hTxLogFile);
      pDevExt->hTxLogFile = NULL;
   }

   if(pUrb)
   {
      ExFreePool(pUrb);
   }
   if(pIrp)
   {
      IoReuseIrp(pIrp, STATUS_SUCCESS);
      IoFreeIrp(pIrp);
   }
   if(pwbArray)
   {
      _ExFreePool(pwbArray);
   }

   KeSetEvent
   (
      &pDevExt->WriteThreadClosedEvent,
      IO_NO_INCREMENT,
      FALSE
   ); // signal write thread closed

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> Wth: term\n", pDevExt->PortName)
   );

   _closeHandle(pDevExt->hWriteThreadHandle, "W-6");
   PsTerminateSystemThread(STATUS_SUCCESS); // end this thread
}  // STESerial_WriteThread

ULONG CountWriteQueue (PDEVICE_EXTENSION pDevExt)
{
   PVXD_WDM_IO_CONTROL_BLOCK pWriteQueEntry;
   ULONG ulBytesQueued = 0;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   QcAcquireSpinLockWithLevel(&pDevExt->WriteSpinLock, &levelOrHandle, irql);

   // get the currently active write
   #ifdef QCUSB_MULTI_WRITES
   if (pDevExt->UseMultiWrites == TRUE)
   {
      PQCMWT_BUFFER pWtBuf;
      int           i;

      // go through pending write buffers
      for (i = 0; i < pDevExt->NumberOfMultiWrites; i++)
      {
         pWtBuf = pDevExt->pMwtBuffer[i];
         if (pWtBuf->State == MWT_BUF_PENDING)
         {
            ulBytesQueued += pWtBuf->Length;
         }
      }
   }
   else
   #endif // QCUSB_MULTI_WRITES
   {
      if (pDevExt->pWriteCurrent != NULL)
      {
         ulBytesQueued += pDevExt->pWriteCurrent->ulActiveBytes;
      }
   }

   // get queued writes
   pWriteQueEntry = pDevExt->pWriteHead;
   while(pWriteQueEntry)       
   {
      if (pWriteQueEntry->ulActiveBytes)
      {
         ulBytesQueued += pWriteQueEntry->ulActiveBytes;
      }
      else
      {
         ulBytesQueued += pWriteQueEntry->ulBTDBytes;
      }
      pWriteQueEntry = pWriteQueEntry->pNextEntry;
   }

   QcReleaseSpinLockWithLevel(&pDevExt->WriteSpinLock, levelOrHandle, irql);

   return (ulBytesQueued);
}  // CountWriteQueue

BOOLEAN StartWriteTimeout(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock)
{
   PDEVICE_EXTENSION pDevExt = pIOBlock->pSerialDeviceObject->DeviceExtension;
   PSERIAL_TIMEOUTS pSt = pDevExt->pSerialTimeouts;
   BOOLEAN inQueue;
   LARGE_INTEGER dueTime;
   PIRP pIrp = pIOBlock->pCallingIrp;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation (pIrp);
   ULONG ulCharsNeeded = irpStack->Parameters.Write.Length - pIrp->IoStatus.Information;
   NTSTATUS ntStatus;

   ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, pIOBlock);
   if (!NT_SUCCESS(ntStatus))
   {
      return FALSE;
   }
   QcInterlockedIncrement(3, pIOBlock, 11);

   pIOBlock->ulTimeout = pSt->WriteTotalTimeoutConstant +
            pSt->WriteTotalTimeoutMultiplier * ulCharsNeeded;

   if (pIOBlock->ulTimeout == 0) // no timeout
   {
      IoSetCancelRoutine(pIrp, CancelWriteRoutine);
      _IoMarkIrpPending(pIrp);
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> StartWriteTimeout NO TO\n", pDevExt->PortName)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3);
      return TRUE;
   }

StartTimer:

   // set the cancel routine and mark pending
   IoSetCancelRoutine(pIrp, CancelWriteRoutine);
   _IoMarkIrpPending(pIrp);

   KeInitializeTimer(&pIOBlock->TimeoutTimer);

   dueTime.QuadPart = (LONGLONG)(-10000) * (LONGLONG)pIOBlock->ulTimeout;

   // OK, launch the timer
   inQueue = KeSetTimer(&pIOBlock->TimeoutTimer, dueTime, &pIOBlock->TimeoutDpc);

   ASSERT(inQueue == FALSE);  // assert timer not already in system queue
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WIRP,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> StartWriteTimeout: %ldms\n", pDevExt->PortName, pIOBlock->ulTimeout)
   );
   return TRUE;
} // StartWriteTimeout

VOID AbortWriteTimeout(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock)
{
   PDEVICE_EXTENSION pDevExt;

   pDevExt = pIOBlock->pSerialDeviceObject->DeviceExtension;

   // release the remove lock when either timer expired or is running
   // no remove lock applied to an IOB without a timer
   if ((pIOBlock->TimerExpired == TRUE) || (pIOBlock->TimeoutTimer.DueTime.LowPart))
   {
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3);
   }

   if (pIOBlock->TimeoutTimer.DueTime.LowPart)
   {
      KeCancelTimer(&pIOBlock->TimeoutTimer);
      RtlZeroMemory(&pIOBlock->TimeoutTimer,sizeof(KTIMER)); // tell everyone timer's gone
   }
}  // AbortWriteTimeout

VOID WriteTimeoutDpc
( 
   IN PKDPC Dpc,
   IN PVOID DeferredContext,
   IN PVOID SystemArgument1,
   IN PVOID SystemArgument2
)
{
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock;
   PDEVICE_EXTENSION pDevExt;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   pIOBlock = (PVXD_WDM_IO_CONTROL_BLOCK)DeferredContext;
   if (pIOBlock->pCallingIrp == NULL)
   {
      #ifdef DBG
      DbgPrint("<%s> WriteTimeoutDpc: Error - purged IOB 0x%p\n",
                gDeviceName, pIOBlock);
      #endif
      return;
   }

   pDevExt = pIOBlock->pSerialDeviceObject->DeviceExtension;
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WIRP,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> WriteTimeoutDpc - 0 \n", pDevExt->PortName)
   );
   QcAcquireSpinLockAtDpcLevel(&pDevExt->WriteSpinLock, &levelOrHandle);
   if (!pIOBlock)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> WriteTimeoutDpc - ERR: NUL IOB \n", pDevExt->PortName)
      );
      QcReleaseSpinLockFromDpcLevel(&pDevExt->WriteSpinLock, levelOrHandle);
      return;
   }
   RtlZeroMemory(&pIOBlock->TimeoutTimer, sizeof(KTIMER));

   // we move this lock removal to AbortTimeout called by TimeoutWriteRoutine
   // QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3);
   if (pIOBlock->pCallingIrp != NULL)
   {
      pIOBlock->TimerExpired = TRUE;
      TimeoutWriteRoutine(pIOBlock);
   }
   else
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> WriteTimeoutDpc - ERR: NUL IRP \n", pDevExt->PortName)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3); // shouldn't happen
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WIRP,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> WriteTimeoutDpc - 1 \n", pDevExt->PortName)
   );
   QcReleaseSpinLockFromDpcLevel(&pDevExt->WriteSpinLock, levelOrHandle);
}  // WriteTimeoutDpc

VOID TimeoutWriteRoutine
(
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock
)
{
   PDEVICE_EXTENSION pDevExt;
   BOOLEAN bFreeIOBlock;
   PIRP pIrp;

   pDevExt = pIOBlock->pSerialDeviceObject->DeviceExtension;

   #ifdef QCUSB_MULTI_WRITES
   if (pDevExt->UseMultiWrites == TRUE)
   {
      QCMWT_TimeoutWriteRoutine(pIOBlock);
      return;
   }
   #endif // QCUSB_MULTI_WRITES

   pIrp = pIOBlock->pCallingIrp;

   // remove it from pIOBlock
   AbortWriteTimeout(pIOBlock);

   bFreeIOBlock = DeQueueIOBlock(pIOBlock, &pDevExt->pWriteHead);

   if ((bFreeIOBlock == FALSE) && (pDevExt->pWriteCurrent == pIOBlock)) 
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> TOWR: cxl curr W\n", pDevExt->PortName)
      );
      KeSetEvent(&pDevExt->CancelCurrentWriteEvent,IO_NO_INCREMENT,FALSE);
      return;
   }

   // Two possibilities with the following condition:
   // 1. IOB expired before being queued
   // 2. IOB is in the late stage of completion - just before WriteIrpCompletion
   //    is called. If it's a partial-write completion, then it'll be completed
   //    after re-queued because of timer expiration.
   if ((bFreeIOBlock == FALSE) && (pDevExt->pWriteCurrent != pIOBlock)) 
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> TOWR: ERR - stray away iob 0x%p/0x%p\n", pDevExt->PortName, pIrp, pIOBlock)
      );
      // do nothing to the IOB and the IRP in this case
      // the write thread will take care of this type of IOB
      return;
   }
   if (pIrp != NULL)
   {
      if (!IoSetCancelRoutine(pIrp, NULL)) // cancel the cancel routine
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WIRP,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> WIRP (TO) 0x%p - ERR: cxled already!!\n", pDevExt->PortName, pIrp)
         );
         return;
      }
      pIOBlock->pCallingIrp = NULL;
    
      pIrp->IoStatus.Status = STATUS_TIMEOUT;

      InsertTailList(&pDevExt->WtCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
      KeSetEvent(&pDevExt->InterruptEmptyWtQueueEvent, IO_NO_INCREMENT, FALSE);
   }
   else
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> TOWR: NUL IRP\n", pDevExt->PortName)
      );
   }

   if (bFreeIOBlock == TRUE)
   {
      QcExFreeWriteIOB(pIOBlock, FALSE);
   }
}  // TimeoutWriteRoutine

// if pIrp is NULL, find the next write IRP in queue
// Must be called under WriteSpinLock
PVXD_WDM_IO_CONTROL_BLOCK FindWriteIrp
(
   PDEVICE_EXTENSION pDevExt,
   PIRP              pIrp,
   BOOLEAN           IOBQueueOnly
)
{
   PVXD_WDM_IO_CONTROL_BLOCK pCurrIOBlock = pDevExt->pWriteHead;

   if (IOBQueueOnly == FALSE)
   {
      if ((pDevExt->pWriteCurrent != NULL) && (pDevExt->pWriteCurrent->pCallingIrp != NULL))
      {
         if ((pIrp == NULL) || (pIrp == pDevExt->pWriteCurrent->pCallingIrp))
         {
            return pDevExt->pWriteCurrent;
         }
      }
   }

   while (pCurrIOBlock)
   {
      if ((pIrp == NULL) && (pCurrIOBlock->pCallingIrp != NULL))
      {
         break;
      }
      if ((pIrp != NULL) && (pCurrIOBlock->pCallingIrp == pIrp))
      {
         break;
      }
      pCurrIOBlock = pCurrIOBlock->pNextEntry;
   }

   return pCurrIOBlock;
} // FindWriteIrp

VOID QCWT_ProcessPreTimeoutIOB(PDEVICE_EXTENSION pDevExt)
{
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock, preIOB;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

   pIOBlock = preIOB = pDevExt->pWriteHead;
   while (pIOBlock != NULL)
   {
      if (pIOBlock->TimerExpired == TRUE)
      {
         if (pIOBlock == pDevExt->pWriteHead)
         {
            pDevExt->pWriteHead = pIOBlock->pNextEntry;
            pIOBlock->ntStatus = STATUS_TIMEOUT;
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> WIRP 0x%p preQto-0\n", pDevExt->PortName, pIOBlock->pCallingIrp)
            );
            pIOBlock->pCompletionRoutine(pIOBlock, FALSE, 11);
            QcExFreeWriteIOB(pIOBlock, FALSE);
            pIOBlock = preIOB = pDevExt->pWriteHead;
         }
         else
         {
            preIOB->pNextEntry = pIOBlock->pNextEntry;
            pIOBlock->ntStatus = STATUS_TIMEOUT;
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> WIRP 0x%p preQto-1\n", pDevExt->PortName, pIOBlock->pCallingIrp)
            );
            pIOBlock->pCompletionRoutine(pIOBlock, FALSE, 13);
            QcExFreeWriteIOB(pIOBlock, FALSE);
            pIOBlock = preIOB->pNextEntry;
         }
      }
      else
      {
         pIOBlock = pIOBlock->pNextEntry;
         if (preIOB->pNextEntry != pIOBlock)
         {
            preIOB = preIOB->pNextEntry;
         }
      }
   }

   QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
}  // QCWT_ProcessPreTimeoutIOB

NTSTATUS QCWT_ImmediateChar(PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp)
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(pIrp);
   KIRQL irql = KeGetCurrentIrql();

   pDevExt = DeviceObject->DeviceExtension;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WIRP,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s 0x%p> WIRP 0x%p => t 0x%p\n", pDevExt->PortName, DeviceObject, pIrp,
       KeGetCurrentThread())
   );

   if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(pIrp);

      if ((gVendorConfig.DriverResident == 0) && (pDevExt->bInService == FALSE))
      {
         pIrp->IoStatus.Information = 0;
         pIrp->IoStatus.Status = ntStatus = STATUS_DELETE_PENDING;
      }
      else
      {
         pIrp->IoStatus.Information = sizeof(UCHAR);
         pIrp->IoStatus.Status = ntStatus = STATUS_SUCCESS;
      }
      goto Exit;
   }

   if ((pDevExt->bInService == FALSE) || (pDevExt->bDeviceRemoved == TRUE))
   {
      pIrp->IoStatus.Information = 0;
      pIrp->IoStatus.Status = ntStatus = STATUS_DELETE_PENDING;
      goto Exit;
   } 

   QcExAllocateWriteIOB(pIOBlock, TRUE);
   if (!pIOBlock)
   {
      pIrp->IoStatus.Information = 0;
      pIrp->IoStatus.Status = ntStatus = STATUS_NO_MEMORY;
      goto Exit;
   }
   RtlZeroMemory(pIOBlock, sizeof(VXD_WDM_IO_CONTROL_BLOCK));
   pIOBlock->pSerialDeviceObject = DeviceObject;
   pIOBlock->pCallingIrp         = pIrp;
   pIOBlock->TimerExpired        = FALSE;

   // According to Windows DDK built on July 23, 2004, KeInitializeDpc
   // can be running at any IRQL.
   KeInitializeDpc(&pIOBlock->TimeoutDpc, WriteTimeoutDpc, pIOBlock);

   pIOBlock->pBufferToDevice = pIrp->AssociatedIrp.SystemBuffer;
   pIOBlock->ulBTDBytes = irpStack->Parameters.DeviceIoControl.InputBufferLength;
   pIOBlock->pCompletionRoutine = (STE_COMPLETIONROUTINE) WriteIrpCompletion;
   pIOBlock->bPurged = FALSE;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WIRP,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> ENQ WIRP 0x%p/0x%p =>%ldB\n", pDevExt->PortName, pIrp, pIOBlock, pIOBlock->ulBTDBytes)
   );

   ntStatus = QCWT_PriorityWrite(pIOBlock);  // Enqueue IOB
   if ((ntStatus != STATUS_PENDING) && (ntStatus != STATUS_TIMEOUT))
   {
      if ((ntStatus == STATUS_CANCELLED) || (ntStatus == STATUS_DELETE_PENDING))
      {
         pIrp->IoStatus.Information = 0;
      }
      pIrp->IoStatus.Status = ntStatus;
      QcExFreeWriteIOB(pIOBlock, TRUE);
   }

Exit:

   // Try to make the statistics right
   if (ntStatus == STATUS_PENDING)
   {
      InterlockedDecrement(&(pDevExt->Sts.lRmlCount[0]));
      InterlockedIncrement(&(pDevExt->Sts.lRmlCount[2]));
   }

   return ntStatus;
}  // QCWT_ImmediateChar

NTSTATUS QCWT_PriorityWrite(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock)
{
   PDEVICE_OBJECT pDO;
   PDEVICE_EXTENSION pDevExt;
   PIRP pIrp;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   pDO = pIOBlock->pSerialDeviceObject;
   pDevExt = pDO->DeviceExtension;
   pIrp = pIOBlock->pCallingIrp;

   // has the write thead started?
   if ((pDevExt->hWriteThreadHandle == NULL) && (pDevExt->pWriteThread == NULL) &&
       inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      return STESerial_Write(pIOBlock);
   }
  
   if(pIOBlock->ulBTDBytes == 0)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> _Write2 0 byte: STATUS_SUCCESS\n", pDevExt->PortName)
      );
      pIOBlock->ntStatus = STATUS_SUCCESS;  // succeeded in writing 0 bytes
      return STATUS_SUCCESS;
   }

   W2_Enqueue:

   ntStatus = pIOBlock->ntStatus = STATUS_PENDING;
   InterlockedExchange(&(pIOBlock->lPurgeForbidden), pDevExt->lPurgeBegin);

   if (StartWriteTimeout(pIOBlock) == FALSE)
   {
      ntStatus = pIOBlock->ntStatus = STATUS_UNSUCCESSFUL;
      return ntStatus;
   }

   QcAcquireSpinLockWithLevel(&pDevExt->WriteSpinLock, &levelOrHandle, irql);

   // if the IRP was cancelled between SetTimer (where the cancel routine is set)
   // and now, then we need special processing here. Note: the cancel routine
   // will not complete the IRP in this case because it will not find the
   // IRP anywhere.
   if (pIOBlock->pCallingIrp != NULL)
   {
      if (pIOBlock->pCallingIrp->Cancel)
      {
         AbortWriteTimeout(pIOBlock);
         if (IoSetCancelRoutine(pIOBlock->pCallingIrp, NULL) != NULL)
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> WIRP2 0x%p pre-cxl-0\n", pDevExt->PortName, pIOBlock->pCallingIrp)
            );
            // The IRP will be cancelled once this status is returned.
            ntStatus = pIOBlock->ntStatus = STATUS_CANCELLED;
         }
         else
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> WIRP2 0x%p pre-cxl-1\n", pDevExt->PortName, pIOBlock->pCallingIrp)
            );
            // The cancel routine is running, do not touch the IRP anymore,
            // let the party who nullified the cancel routine complete the IRP.
            ntStatus = pIOBlock->ntStatus = STATUS_PENDING;
            QcExFreeWriteIOB(pIOBlock, FALSE);
         }
         QcReleaseSpinLockWithLevel(&pDevExt->WriteSpinLock, levelOrHandle, irql);
         return ntStatus;
      }
   }

   // Link to the head
   pIOBlock->pNextEntry = pDevExt->pWriteHead;
   pDevExt->pWriteHead = pIOBlock;

   KeSetEvent
   (
      &pDevExt->KickWriteEvent,
      IO_NO_INCREMENT,
      FALSE
   );

   if (pIOBlock->TimerExpired == TRUE)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> W_ENQ2 IOB-to 0x%p/0x%p => DN\n", pDevExt->PortName,
           pIOBlock->pCallingIrp, pIOBlock)
      );
      KeSetEvent
      (
         &pDevExt->WritePreTimeoutEvent,
         IO_NO_INCREMENT,
         FALSE
      );
   }

   QcReleaseSpinLockWithLevel(&pDevExt->WriteSpinLock, levelOrHandle, irql);

   return ntStatus; // accepted the write, should be STATUS_PENDING
}  // QCWT_PriorityWrite

NTSTATUS QCWT_StartWriteThread(PDEVICE_EXTENSION pDevExt)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   OBJECT_ATTRIBUTES objAttr; 
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   // Make sure the write thread is created with IRQL==PASSIVE_LEVEL
   QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
   if (((pDevExt->pWriteThread == NULL)        &&
        (pDevExt->hWriteThreadHandle == NULL)) &&
       (irql > PASSIVE_LEVEL))
   {
      NTSTATUS ntS;

      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> _STW IRQL high\n", pDevExt->PortName)
      );
      QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
      return STATUS_UNSUCCESSFUL;
   }

   if ((pDevExt->hWriteThreadHandle == NULL) &&
       (pDevExt->pWriteThread == NULL))
   {
      NTSTATUS ntStatus;

      if (pDevExt->bWtThreadInCreation == FALSE)
      {
         pDevExt->bWtThreadInCreation = TRUE;
         QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
      }
      else
      {
         QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> _STW th in creation\n", pDevExt->PortName)
         );
         return STATUS_SUCCESS;
      }
 
      KeClearEvent(&pDevExt->WriteThreadStartedEvent);
      InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
      ucHdlCnt++;

      #ifdef QCUSB_MULTI_WRITES
      if (pDevExt->UseMultiWrites == TRUE)
      {
         ntStatus = PsCreateSystemThread
                    (
                       OUT &pDevExt->hWriteThreadHandle,
                       IN THREAD_ALL_ACCESS,
                       IN &objAttr,     // POBJECT_ATTRIBUTES  ObjectAttributes
                       IN NULL,         // HANDLE  ProcessHandle
                       OUT NULL,        // PCLIENT_ID  ClientId
                       IN (PKSTART_ROUTINE)QCMWT_WriteThread,
                       IN (PVOID) pDevExt
                    );
      }
      else
      #endif
      {
         ntStatus = PsCreateSystemThread
                    (
                       OUT &pDevExt->hWriteThreadHandle,
                       IN THREAD_ALL_ACCESS,
                       IN &objAttr,     // POBJECT_ATTRIBUTES  ObjectAttributes
                       IN NULL,         // HANDLE  ProcessHandle
                       OUT NULL,        // PCLIENT_ID  ClientId
                       IN (PKSTART_ROUTINE)STESerial_WriteThread,
                       IN (PVOID) pDevExt
                    );
      }
      if (ntStatus != STATUS_SUCCESS)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> _STW th failure 0x%x\n", pDevExt->PortName, ntStatus)
         );
         pDevExt->pWriteThread = NULL;
         pDevExt->hWriteThreadHandle = NULL;
         pDevExt->bWtThreadInCreation = FALSE;
         return ntStatus;
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> Wth: hdl 0x%x\n", pDevExt->PortName, pDevExt->hWriteThreadHandle)
         );
      }

      ntStatus = KeWaitForSingleObject
                 (
                    &pDevExt->WriteThreadStartedEvent,
                    Executive,
                    KernelMode,
                    FALSE,
                    NULL
                 );

      if (pDevExt->bWtThreadInCreation == FALSE)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> _STW th failure 02\n", pDevExt->PortName)
         );
         pDevExt->pWriteThread = NULL;
         pDevExt->hWriteThreadHandle = NULL;
         return STATUS_UNSUCCESSFUL;
      }

      ntStatus = ObReferenceObjectByHandle
                 (
                    pDevExt->hWriteThreadHandle,
                    THREAD_ALL_ACCESS,
                    NULL,
                    KernelMode,
                    (PVOID*)&pDevExt->pWriteThread,
                    NULL
                 );
      if (!NT_SUCCESS(ntStatus))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> STW: ObReferenceObjectByHandle failed 0x%x\n", pDevExt->PortName, ntStatus)
         );
         pDevExt->pWriteThread = NULL;
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> STW handle=0x%p thOb=0x%p\n", pDevExt->PortName,
             pDevExt->hWriteThreadHandle, pDevExt->pWriteThread)
         );
         _closeHandle(pDevExt->hWriteThreadHandle, "W-5");
      }

      pDevExt->bWtThreadInCreation = FALSE;
   }
   else
   {
      QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> Wth alive-%d\n", pDevExt->PortName, KeGetCurrentIrql())
   );
  
   return ntStatus;
}  // QCWT_StartWriteThread

NTSTATUS QCWT_Suspend(PDEVICE_EXTENSION pDevExt)
{
   LARGE_INTEGER delayValue;
   NTSTATUS nts = STATUS_UNSUCCESSFUL;

   if ((pDevExt->hWriteThreadHandle != NULL) || (pDevExt->pWriteThread != NULL))
   {
      delayValue.QuadPart = -(50 * 1000 * 1000); // 5 seconds

      KeClearEvent(&pDevExt->WriteStopAckEvent);
      KeSetEvent(&pDevExt->WriteStopEvent, IO_NO_INCREMENT, FALSE);

      nts = KeWaitForSingleObject
            (
               &pDevExt->WriteStopAckEvent,
               Executive,
               KernelMode,
               FALSE,
               &delayValue
            );
      if (nts == STATUS_TIMEOUT)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> WTSuspend: WTO\n", pDevExt->PortName)
         );
         KeClearEvent(&pDevExt->WriteStopEvent);
      }

      KeClearEvent(&pDevExt->WriteStopAckEvent);
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> WTSuspend: 0x%x\n", pDevExt->PortName, nts)
   );

   return nts;
}  // QCWT_Suspend

NTSTATUS QCWT_Resume(PDEVICE_EXTENSION pDevExt)
{
   if ((pDevExt->hWriteThreadHandle != NULL) || (pDevExt->pWriteThread != NULL))
   {
      KeSetEvent(&pDevExt->WriteResumeEvent, IO_NO_INCREMENT, FALSE);
   }

   return STATUS_SUCCESS;

}  // QCWT_Resume

