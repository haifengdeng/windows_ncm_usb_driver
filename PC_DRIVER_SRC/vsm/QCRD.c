/*===========================================================================
FILE: QCRD.c

DESCRIPTION:
   This file contains implementation for reading data over USB.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003-2007 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#include "QCPTDO.h"
#include "QCRD.h"
#include "QCUTILS.h"
#include "QCDSP.h"
#include "QCMGR.h"
#include "QCPWR.h"

extern USHORT   ucHdlCnt;

#undef QCOM_TRACE_IN

// The following protypes are implemented in ntoskrnl.lib
extern NTKERNELAPI VOID IoReuseIrp(IN OUT PIRP Irp, IN NTSTATUS Iostatus);

NTSTATUS CancelReadThread(PDEVICE_EXTENSION pDevExt, UCHAR cookie)
{
   NTSTATUS ntStatus;
   LARGE_INTEGER delayValue;
   PVOID readThread;
   
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> CancelReadThread %d\n", pDevExt->PortName, cookie)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> CancelReadThread: wrong IRQL::%d - %d\n", pDevExt->PortName, KeGetCurrentIrql(), cookie)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if (pDevExt->bRdCancelStarted == TRUE)
   {
      while ((pDevExt->hL1ReadThreadHandle != NULL) || (pDevExt->pL1ReadThread != NULL))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> Rth cxl in pro\n", pDevExt->PortName)
         );
         QCSER_Wait(pDevExt, -(3 * 1000 * 1000L));  // 0.3 second
      }
      return STATUS_SUCCESS;
   }
   pDevExt->bRdCancelStarted = TRUE;

   if ((pDevExt->hL1ReadThreadHandle != NULL) || (pDevExt->pL1ReadThread != NULL))
   {
      KeClearEvent(&pDevExt->L1ReadThreadClosedEvent);
      KeSetEvent(&pDevExt->L1CancelReadEvent,IO_NO_INCREMENT,FALSE);
   
      if (pDevExt->pL1ReadThread != NULL)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> CancelReadThread: Wait for L1 (%d)\n", pDevExt->PortName, cookie)
         );
         ntStatus = KeWaitForSingleObject
                    (
                       pDevExt->pL1ReadThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pDevExt->pL1ReadThread);
         KeClearEvent(&pDevExt->L1ReadThreadClosedEvent);
         _closeHandle(pDevExt->hL1ReadThreadHandle, "R-0");
         pDevExt->pL1ReadThread = NULL;
      }
      else  // best effort
      {
         ntStatus = KeWaitForSingleObject
                    (
                       &pDevExt->L1ReadThreadClosedEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         KeClearEvent(&pDevExt->L1ReadThreadClosedEvent);
         _closeHandle(pDevExt->hL1ReadThreadHandle, "R-1");
      }

      vResetReadBuffer(pDevExt, 4);
   }

   pDevExt->bRdCancelStarted = FALSE;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> CancelReadThread: L1 OUT (%d)\n", pDevExt->PortName, cookie)
   );

   return STATUS_SUCCESS;
} // CancelReadThread

NTSTATUS StartTheReadGoing( PDEVICE_EXTENSION pDevExt, PIRP pIrp, UCHAR cookie)
{
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock;
   NTSTATUS ntStatus;

   QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);
   if (((pIrp == NULL) && (pDevExt->pReadHead != NULL)) ||
       (pDevExt->BulkPipeInput  == (UCHAR)-1)) // DEVICETYPE_CTRL
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> RIOB: not necessary\n", pDevExt->PortName)
      );
      QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
      return STATUS_SUCCESS;
   }
   QcExAllocateReadIOB(pIOBlock, FALSE);
   QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);

   if (!pIOBlock)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> RIOB: STATUS_NO_MEMORY\n", pDevExt->PortName)
      );
      ntStatus = STATUS_NO_MEMORY;
      goto Exit;
   }

   RtlZeroMemory
   (
      pIOBlock,
      sizeof(VXD_WDM_IO_CONTROL_BLOCK)
   );

   pIOBlock->pSerialDeviceObject = pDevExt->MyDeviceObject;

   pIOBlock->ulBFDBytes = pDevExt->MaxPipeXferSize;
   pIOBlock->pCompletionRoutine = (STE_COMPLETIONROUTINE)ReadIrpCompletion;
   pIOBlock->bPurged = FALSE;
   pIOBlock->bReturnOnChars = FALSE;
   pIOBlock->TimerExpired   = FALSE;

   // According to Windows DDK built on July 23, 2004, KeInitializeDpc
   // can be running at any IRQL.
   KeInitializeDpc(&pIOBlock->TimeoutDpc, ReadTimeoutDpc, pIOBlock);

   if (pIrp)
   {
      pIOBlock->pCallingIrp = pIrp;

      // immediate "timeout" due to special parameters
      if (!StartReadTimeout(pIOBlock))
      {
         QCSER_DbgPrint2
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> free IOB 02 0x%p\n", pDevExt->PortName, pIOBlock)
         );
         QcExFreeReadIOB(pIOBlock, TRUE);
         ntStatus = STATUS_SUCCESS;
         goto Exit;
      }
   }

   ntStatus = STESerial_Read(pIOBlock);
   if (ntStatus != STATUS_PENDING)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> RIOB: Enqueue failure 0x%p(0x%x)\n", pDevExt->PortName, pIOBlock, ntStatus)
      );
      QcExFreeReadIOB(pIOBlock, TRUE);
   }
   else
   {
      // If the IRP is cancelled, then the IOB would be freed at this point
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> RIOB: Enqueue<%d> 0x%p/0x%p\n", pDevExt->PortName, cookie, 
           pIrp, pIOBlock)
      );
   }
   
Exit:
   return ntStatus;
} // StartTheReadGoing


NTSTATUS STESerial_Read(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock)
{
   NTSTATUS ntStatus;
   PDEVICE_OBJECT pDO;
   PDEVICE_EXTENSION pDevExt;
   PVXD_WDM_IO_CONTROL_BLOCK pReadQueEntry;
   PVXD_WDM_IO_CONTROL_BLOCK pNextQueEntry;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   // get device extension
   pDO = pIOBlock->pSerialDeviceObject;
   pDevExt = pDO->DeviceExtension;

   if (!pIOBlock)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> NULL RIOB: STATUS_INVALID_PARAMETER\n", pDevExt->PortName)
      );
      return STATUS_INVALID_PARAMETER;
   }

   if (! pDO || ! pDevExt)  // device objects initialized ?
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> STATUS_SERIAL_NO_DEVICE_INITED\n", pDevExt->PortName)
      );
      return STATUS_SERIAL_NO_DEVICE_INITED; // no
   }

   L1_Enqueue:

   if (pIOBlock->ulBFDBytes == 0)
   {
      pIOBlock->ulBFDBytes = 0;
      pIOBlock->ntStatus = STATUS_SUCCESS;  // succeeded in reading 0 bytes

      return STATUS_SUCCESS;
   }

   ntStatus = pIOBlock->ntStatus = STATUS_PENDING;
   InterlockedExchange(&(pIOBlock->lPurgeForbidden), pDevExt->lPurgeBegin);

   // setup to que and/or kick read thread
   QcAcquireSpinLockWithLevel(&pDevExt->ReadSpinLock, &levelOrHandle, irql);

   // if the IRP was cancelled between SetTimer (where the cancel routine is set)
   // and now, then we need special processing here. Note: the cancel routine
   // will complete the IRP anyway if it's running.
   if (pIOBlock->pCallingIrp != NULL)
   {
      if (pIOBlock->pCallingIrp->Cancel)
      {
         AbortReadTimeout(pIOBlock);
         if (IoSetCancelRoutine(pIOBlock->pCallingIrp, NULL) != NULL)
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_RIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> RIRP 0x%p pre-cxl-0\n", pDevExt->PortName, pIOBlock->pCallingIrp)
            );
            // The IRP will be cancelled once this status is returned.
            ntStatus = pIOBlock->ntStatus = STATUS_CANCELLED;
         }
         else
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_RIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> RIRP 0x%p pre-cxl-1\n", pDevExt->PortName, pIOBlock->pCallingIrp)
            );
            // The cancel routine is running, do not touch the IRP anymore,
            // let the party who nullified the cancel routine complete the IRP.
            ntStatus = pIOBlock->ntStatus = STATUS_PENDING;
            QcExFreeReadIOB(pIOBlock, FALSE);
         }
         QcReleaseSpinLockWithLevel(&pDevExt->ReadSpinLock, levelOrHandle, irql);
         return ntStatus;
      }
      else
      {
         // if the read thread has been terminated by IRP_MJ_CLEANUP, 
         // we need to re-start it.
         if ((pDevExt->hL1ReadThreadHandle == NULL) &&
             (pDevExt->pL1ReadThread == NULL))
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

   pReadQueEntry = pDevExt->pReadHead;
   if (pReadQueEntry)
   {
      pNextQueEntry = pReadQueEntry->pNextEntry;

      // an outstanding read, just link this one on
      while(pNextQueEntry)
      {
         pReadQueEntry = pNextQueEntry;
         pNextQueEntry = pReadQueEntry->pNextEntry;
      }

      pReadQueEntry->pNextEntry = pIOBlock;

      // Before enqueue, check the read buffer again to see
      // whether any bytes get in between now and the first fill.
      // This is to fix the bug uncovered by EFS Explorer
      if ((pIOBlock->pCallingIrp != NULL) && CountL1ReadQueue(pDevExt))
      {
         KeSetEvent
         (
            &pDevExt->L1ReadAvailableEvent,
            IO_NO_INCREMENT,
            FALSE
         );
      }
      /***
      else if (pDevExt->pReadCurrent == NULL)
      {
         KeSetEvent
         (
            &pDevExt->L1KickReadEvent,
            IO_NO_INCREMENT,
            FALSE
         ); // kick the read thread
      }
      ***/
   }
   else
   {
      pDevExt->pReadHead = pIOBlock;
      if ((pIOBlock->pCallingIrp != NULL) && CountL1ReadQueue(pDevExt))
      {
         KeSetEvent
         (
            &pDevExt->L1ReadAvailableEvent,
            IO_NO_INCREMENT,
            FALSE
         );
      }
      else if (pDevExt->pReadCurrent == NULL)
      {
         if ((pDevExt->bInService == TRUE) &&
             inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
         {
            KeSetEvent
            (
               &pDevExt->L1KickReadEvent,
               IO_NO_INCREMENT,
               FALSE
            ); // kick the read thread
         }
         else
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_RIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> RIRP simQed 0x%p/0x%p\n", pDevExt->PortName,
                 pIOBlock->pCallingIrp, pIOBlock)
            );
         }
      }
   }

   if (pIOBlock->TimerExpired == TRUE)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_RIRP,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> ENQ RIRP 0x%p/0x%p => DN\n", pDevExt->PortName,
           pIOBlock->pCallingIrp, pIOBlock)
      );
      KeSetEvent
      (
         &pDevExt->L1ReadPreTimeoutEvent,
         IO_NO_INCREMENT,
         FALSE
      );
   }

   QcReleaseSpinLockWithLevel(&pDevExt->ReadSpinLock, levelOrHandle, irql);

   return ntStatus; // accepted the read
}  // STESerial_Read

// if pIrp is NULL, find the next read IRP in queue
PVXD_WDM_IO_CONTROL_BLOCK FindReadIrp
(
   PDEVICE_EXTENSION pDevExt,
   PIRP pIrp
)
{
   PVXD_WDM_IO_CONTROL_BLOCK pCurrIOBlock = pDevExt->pReadHead;

   if (pDevExt->pReadCurrent && pDevExt->pReadCurrent->pCallingIrp)
   {
      if (!pIrp || pIrp == pDevExt->pReadCurrent->pCallingIrp)
      {
         if (pDevExt->pReadCurrent->bPurged == FALSE)
         {
            return pDevExt->pReadCurrent;
         }
      }
   }
   while (pCurrIOBlock)
   {
      // if (!pIrp && pCurrIOBlock->pCallingIrp)
      if (!pIrp && pCurrIOBlock->pCallingIrp && pCurrIOBlock->bPurged==FALSE)
      {
         break;
      }
      // if ((pIrp) && pCurrIOBlock->pCallingIrp == pIrp)
      if ((pIrp) && pCurrIOBlock->pCallingIrp == pIrp && (pCurrIOBlock->bPurged==FALSE))
      {
         break;
      }
      pCurrIOBlock = pCurrIOBlock->pNextEntry;
   }

   if (pCurrIOBlock != NULL)
   {
      if (pCurrIOBlock->bPurged == TRUE)
      {
         QCSER_DbgPrint2
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> FindReadIrp: PURGED 2\n", pDevExt->PortName)
         );
      }
   }
   else
   {
      QCSER_DbgPrint2
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> FindReadIrp: return pIOB EMPTY \n", pDevExt->PortName)
      );
   }

   return pCurrIOBlock;
}

NTSTATUS ReadCompletionRoutine
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pContext;

   #ifdef QCOM_TRACE_IN
   KdPrint(("L2 OFF: PDO 0x%p Irp 0x%p\n", pDO, pIrp));
   #endif

   KeSetEvent
   (
      pDevExt->pL2ReadEvents[L2_READ_COMPLETION_EVENT_INDEX],
      IO_NO_INCREMENT,
      FALSE
   );

   QCPWR_SetIdleTimer(pDevExt, 0, FALSE, 3); // RD completion

   return STATUS_MORE_PROCESSING_REQUIRED;
}

// This function need to be called within SpinLock
NTSTATUS QCSER_CancelActiveIOB
(
   PDEVICE_EXTENSION pDevExt,
   PVXD_WDM_IO_CONTROL_BLOCK pCurrIOBlock,
   NTSTATUS status
)
{
   if (pCurrIOBlock == NULL)
   {
      return STATUS_SUCCESS;
   }

   pCurrIOBlock->ntStatus = status;
   pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, FALSE, 0);

   pDevExt->pReadCurrent = NULL;  // need this before freeing IOB

   QCSER_DbgPrint2
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> free IOB 03 0x%p\n", pDevExt->PortName, pCurrIOBlock)
   );
   QcExFreeReadIOB(pCurrIOBlock, FALSE);

   return STATUS_SUCCESS;
}


void STESerial_ReadThread(PVOID pContext)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pContext;
   PVXD_WDM_IO_CONTROL_BLOCK pCurrIOBlock = NULL;
   BOOLEAN bCancelled = FALSE;
   NTSTATUS  ntStatus;
   BOOLEAN bFirstTime = TRUE;
   BOOLEAN bNotDone = TRUE;
   PKWAIT_BLOCK pwbArray;
   LONG lBytesInQueue;
   OBJECT_ATTRIBUTES objAttr;
   KEVENT dummyEvent;
   int i, j, oldFillIdx, nullTest = 0;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   // delayValue.QuadPart = -(10 * 1000 * 1000); // 1 second

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> _ReadThread: wrong IRQL::%d\n", pDevExt->PortName,KeGetCurrentIrql())
      );
      #ifdef DEBUG_MSGS
      _asm int 3;
      #endif
   }

   // need to simulate serial state for the serial device
   if (pDevExt->ucDeviceType >= DEVICETYPE_SERIAL)
   {
      if_DevState(DEVICE_STATE_WOM_FIRST_TIME)
      {
         pDevExt->usCurrUartState &= ~US_BITS_MODEM_RAW;
         clearDevState(DEVICE_STATE_WOM_FIRST_TIME);
      }
      ProcessNewUartState
      (
         pDevExt,
         SERIAL_EV_DSR | SERIAL_EV_RLSD,
         US_BITS_MODEM_RAW,
         TRUE    // no WOM Irp to complete at this time
      );
   }

   // allocate a wait block array for the multiple wait
   pwbArray = _ExAllocatePool
              (
                 NonPagedPool,
                 (L1_READ_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK),
                 "STESerial_ReadThread.pwbArray"
              );
   if (!pwbArray)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> L1: STATUS_NO_MEMORY 01\n", pDevExt->PortName)
      );
      _closeHandle(pDevExt->hL1ReadThreadHandle, "L1R-0");
      pDevExt->bRdThreadInCreation = FALSE;
      KeSetEvent(&pDevExt->ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   // Set L1 thread priority
   pDevExt->L1Priority = KeQueryPriorityThread(KeGetCurrentThread());
   KeSetPriorityThread(KeGetCurrentThread(), QCSER_L1_PRIORITY);

   // Initialize L2 buffer
   pDevExt->L2IrpIdx = pDevExt->L2FillIdx = 0;
   for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
   {
      pDevExt->pL2ReadBuffer[i].bFilled = FALSE;
      pDevExt->pL2ReadBuffer[i].Length  = 0;
      pDevExt->pL2ReadBuffer[i].Status  = STATUS_PENDING;
   }

   pDevExt->bL1ReadActive = FALSE;
   pDevExt->pReadCurrent = NULL;

   pDevExt->pL1ReadEvents[QC_DUMMY_EVENT_INDEX] = &dummyEvent;
   KeInitializeEvent(&dummyEvent, NotificationEvent, FALSE);
   KeClearEvent(&dummyEvent);

   while (bNotDone)
   {
      QCSER_DbgPrint2
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> L1 checking rd Q\n", pDevExt->PortName)
      );

      QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);
      if ((pDevExt->pReadHead) && (pDevExt->bL1ReadActive == FALSE))
      {
         if (pCurrIOBlock != NULL)
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_CRITICAL,
               ("<%s> L1R: ERR-0\n", pDevExt->PortName)
            );
         }
         // yes we have a read to deque
         pDevExt->pReadCurrent = pCurrIOBlock = pDevExt->pReadHead;
         pDevExt->pReadHead = pCurrIOBlock->pNextEntry;

         // check any status' which would ask us to drain que
         if (pCurrIOBlock->TimerExpired == TRUE)
         {
            QCSER_DbgPrint2
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> free IOB 04 0x%p\n", pDevExt->PortName, pCurrIOBlock)
            );
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_FORCE,
               ("<%s> Rth: pre-to 0x%p(%lu/%lu)\n", pDevExt->PortName,
                 pCurrIOBlock, pCurrIOBlock->ulActiveBytes, pCurrIOBlock->ulBFDBytes)
            );
            pCurrIOBlock->ntStatus = STATUS_TIMEOUT;
            pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, FALSE, 8);
            pDevExt->pReadCurrent = NULL;  // need this before freeing IOB
            QcExFreeReadIOB(pCurrIOBlock, FALSE);
            QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
            continue;
         }
         else if ((pDevExt->bL1Stopped == TRUE) || (pCurrIOBlock->bPurged == TRUE))
         {
            QCSER_DbgPrint2
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> free IOB 05 0x%p\n", pDevExt->PortName, pCurrIOBlock)
            );
            pCurrIOBlock->ntStatus = STATUS_CANCELLED;
            pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, FALSE, 2);
            pDevExt->pReadCurrent = NULL;  // need this before freeing IOB
            QcExFreeReadIOB(pCurrIOBlock, FALSE);
            QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
            continue;
         }
      } // end if ReadHead
      QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);

      if (pDevExt->bL1Stopped == TRUE)
      {
         if (bCancelled == TRUE)
         {
            bNotDone = FALSE;
            continue;
         }
         goto wait_for_completion2;
      }

      if ((pDevExt->pReadCurrent == NULL) && (pDevExt->pReadHead == NULL))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_INFO,
            ("<%s> CASE_X: L1 to add read req.\n", pDevExt->PortName)
         );

         if ((CountReadQueue(pDevExt) < pDevExt->lReadBufferHigh) ||
             (pDevExt->ContinueOnOverflow == TRUE))
         {
            ntStatus = StartTheReadGoing(pDevExt, NULL, 2);
            // error occurred, such as NO_MEMORY
            if (!NT_SUCCESS(ntStatus))
            {
               pDevExt->bL1Stopped = TRUE; // bCancelled = TRUE;
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> L1 ERR: start read\n", pDevExt->PortName)
               );
               continue;
            }
         }
         else
         {
            LARGE_INTEGER delayValue;

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> L1 wait for ring3\n", pDevExt->PortName)
            );

            // wait for 10ms to share CPU power. The next read operation
            // will be triggered by a request from the ring3 application.
            QCSER_Wait(pDevExt, -(100 * 1000L));  // 10ms
         }
      }

wait_for_completion:

      if (bFirstTime == TRUE)
      {
         // To ensure when L2 runs, the L1 has already been waiting for data
         if ((pDevExt->hL2ReadThreadHandle == NULL) &&
             (pDevExt->pL2ReadThread == NULL))
         {
            // Start the level2 read thread
            KeClearEvent(&pDevExt->L2ReadThreadStartedEvent);
            InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
            ucHdlCnt++;
            pDevExt->bL2ThreadInCreation = TRUE;
            ntStatus = PsCreateSystemThread
                       (
                          OUT &pDevExt->hL2ReadThreadHandle,
                          IN THREAD_ALL_ACCESS,
                          IN &objAttr, // POBJECT_ATTRIBUTES
                          IN NULL,     // HANDLE  ProcessHandle
                          OUT NULL,    // PCLIENT_ID  ClientId
                          IN (PKSTART_ROUTINE)QCSER_ReadThread,
                          IN (PVOID) pDevExt
                       );
            if ((!NT_SUCCESS(ntStatus)) || (pDevExt->hL2ReadThreadHandle == NULL))
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_CRITICAL,
                  ("<%s> L1: L2 th failure\n", pDevExt->PortName)
               );
               ExFreePool(pwbArray);
               _closeHandle(pDevExt->hL1ReadThreadHandle, "R-4");
               _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-A");
               pDevExt->pL2ReadThread = NULL;
               pDevExt->bL2ThreadInCreation = FALSE;
               pDevExt->bRdThreadInCreation = FALSE;
               KeSetEvent(&pDevExt->ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
               PsTerminateSystemThread(STATUS_NO_MEMORY);
            }
            else
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_CRITICAL,
                  ("<%s> L1: L2 hdl 0x%x\n", pDevExt->PortName, pDevExt->hL2ReadThreadHandle)
               );
            }
            ntStatus = KeWaitForSingleObject
                       (
                          &pDevExt->L2ReadThreadStartedEvent,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL
                       );

            if (pDevExt->bL2ThreadInCreation == FALSE)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_CRITICAL,
                  ("<%s> L1: L2 th failure 02\n", pDevExt->PortName)
               );
               ExFreePool(pwbArray);
               _closeHandle(pDevExt->hL1ReadThreadHandle, "R-4a");
               _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-A1");
               pDevExt->pL2ReadThread = NULL;
               pDevExt->bRdThreadInCreation = FALSE;
               KeSetEvent(&pDevExt->ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
               PsTerminateSystemThread(STATUS_NO_MEMORY);
            }

            ntStatus = ObReferenceObjectByHandle
            (
               pDevExt->hL2ReadThreadHandle,
               THREAD_ALL_ACCESS,
               NULL,
               KernelMode,
               (PVOID*)&pDevExt->pL2ReadThread,
               NULL
            );
            if (!NT_SUCCESS(ntStatus))
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_CRITICAL,
                  ("<%s> L2: ObReferenceObjectByHandle failed 0x%x\n", pDevExt->PortName, ntStatus)
               );
               pDevExt->pL2ReadThread = NULL;
            }
            else
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> L2 handle=0x%p thOb=0x%p\n", pDevExt->PortName,
                   pDevExt->hL2ReadThreadHandle, pDevExt->pL2ReadThread)
               );

               _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-9");
            }
            pDevExt->bL2ThreadInCreation = FALSE;
         }  // end of creating L2 thread

         bFirstTime = FALSE;

         // inform read function that we've started
         KeSetEvent(&pDevExt->ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      }  // if (bFirstTime == TRUE)

      if (KeGetCurrentIrql() > PASSIVE_LEVEL)
      {
         KdPrint(("\n\n =========== L1 SUISIDE ==============\n\n"));
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> L1 ERR: wrong IRQL::%d\n", pDevExt->PortName, KeGetCurrentIrql())
         );
         #ifdef DEBUG_MSGS
         _asm int 3;
         #endif
      }

      if (pCurrIOBlock != NULL)
      {
         pDevExt->bL1ReadActive = TRUE;
         if (nullTest == 1)
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> L1 IOB NOT NULL\n", pDevExt->PortName)
            );
            nullTest = 0;
         }
      }
      else if (CountReadQueue(pDevExt) >= pDevExt->lReadBufferHigh)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> L1 full with NUL IOB, wait...\n", pDevExt->PortName)
         );
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> L1 NULL IOB, cont...\n", pDevExt->PortName)
         );
         nullTest = 1;
         continue;
      }

wait_for_completion2:

      // Manipulations of NumIrpsToComplete are all within L1
      if (pDevExt->NumIrpsToComplete >= pDevExt->CompletionThrottle)
      {
         LARGE_INTEGER delayValue;
         NTSTATUS nts;

         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_INFO,
            ("<%s> L1 completion throttling(%d/%d)\n",
             pDevExt->PortName, pDevExt->NumIrpsToComplete,pDevExt->CompletionThrottle)
         );
         delayValue.QuadPart = -(5 * 1000 * 1000); // 0.5 second
         nts = KeWaitForSingleObject
               (
                  &pDevExt->ReadCompletionThrottleEvent,
                  Executive,
                  KernelMode,
                  FALSE,
                  &delayValue
               );
         if (nts == STATUS_SUCCESS)
         {
            KeClearEvent(&pDevExt->ReadCompletionThrottleEvent);
         }
      }

      if ((pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].bFilled == TRUE) &&
          (pDevExt->bL1Stopped == FALSE))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_INFO,
            ("<%s> L1 direct completion (0x%x)\n",
              pDevExt->PortName, pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status)
         );
         ntStatus = L1_READ_COMPLETION_EVENT_INDEX;
      }
      else
      {
         QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);
         // If we have data in the buffer 
         if ((pCurrIOBlock != NULL)          &&
             (CountL1ReadQueue(pDevExt) > 0) &&
             (FindReadIrp(pDevExt, NULL) != NULL))
         {
            QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> L1 rd avail sig\n", pDevExt->PortName)
            );
            ntStatus = L1_READ_AVAILABLE_EVENT_INDEX;
         }
         else
         {
            QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
            ntStatus = KeWaitForMultipleObjects
                       (
                          L1_READ_EVENT_COUNT,
                          (VOID **)&pDevExt->pL1ReadEvents,
                          WaitAny,
                          Executive,
                          KernelMode,
                          FALSE, // non-alertable // TRUE,
                          NULL,
                          pwbArray
                       );
            QCSER_DbgPrint2
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> L1 waited completion <0x%x>(0x%x)\n", pDevExt->PortName, ntStatus, pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status)
            );
         }
      }

      switch(ntStatus)
      {
         case QC_DUMMY_EVENT_INDEX:
         {
            KeClearEvent(&dummyEvent);
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> L1: DUMMY_EVENT\n", pDevExt->PortName)
            );
            goto wait_for_completion;
         }
         case L1_READ_COMPLETION_EVENT_INDEX:
         {
            // clear read completion event

            KeClearEvent(&pDevExt->L1ReadCompletionEvent);

            // In case of buffer reset
            if (pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].bFilled != TRUE)
            {
               if (pDevExt->bL1ReadActive == TRUE)
               {
                  goto wait_for_completion;
               }
               else
               {
                  continue;
               }
            }
            pDevExt->bL1ReadActive = FALSE;  // ????????????????? L1-specific

            // Try to reset pipe if halt, which runs at PASSIVE_LEVEL
            ntStatus = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status;
            if ((ntStatus == STATUS_DEVICE_DATA_ERROR) ||
                (ntStatus == STATUS_DEVICE_NOT_READY)  ||
                (ntStatus == STATUS_UNSUCCESSFUL))
            {
               if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> L1: Resetting pipe IN/INT (0x%x)\n", pDevExt->PortName, ntStatus)
                  );
                  QCUSB_ResetInput(pDevExt->MyDeviceObject, QCUSB_RESET_HOST_PIPE);
                  QCSER_Wait(pDevExt, -(500 * 1000L)); // 50ms
               }
            }

            if (pCurrIOBlock == NULL) // if the current read is cancelled
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> L1: COMP but NUL RD\n", pDevExt->PortName)
               );
               continue;
            }

            // check completion status
            QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);

            if (pCurrIOBlock->bPurged == TRUE)
            {
               QCSER_CancelActiveIOB(pDevExt, pCurrIOBlock, STATUS_CANCELLED);
               pCurrIOBlock = NULL;

               QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);

               KeSetEvent
               (
                  &pDevExt->ReadIrpPurgedEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );
               continue;
            }

            pCurrIOBlock->ntStatus = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status;
            pCurrIOBlock->ulActiveBytes =
               pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Length;
            pCurrIOBlock->pBufferFromDevice = pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Buffer;

            if ((pCurrIOBlock->ntStatus == STATUS_SUCCESS) &&
                (pCurrIOBlock->ulActiveBytes > 0))
            {
               pDevExt->bPacketsRead = TRUE;
            }

            // service client and client callback
            // called within mutex
            // The completion routine does the following:
            // 1. Put the read bytes to read buffer
            // 2. Find read IRP(IOBlock) from the read queue
            // 3. Fill the IRP from the read buffer
            ntStatus = pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, FALSE, 4);

            // reset the buffer record
            pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status  = STATUS_PENDING;
            pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Length  = 0;
            pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].bFilled = FALSE;
            oldFillIdx = pDevExt->L2FillIdx;
            if (++(pDevExt->L2FillIdx) == pDevExt->NumberOfL2Buffers)
            {
               pDevExt->L2FillIdx = 0;
            }

            // Signal L2 if all L2 buffers are exhausted
            if (oldFillIdx == pDevExt->L2IrpIdx)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> L1: L2 buf available, kick L2\n", pDevExt->PortName)
               );
               // kick L2 thread
               KeSetEvent
               (
                  &pDevExt->L2KickReadEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );
            }

            // if ((ntStatus == STATUS_CANCELLED) || (pDevExt->bDeviceRemoved == TRUE))
            if (pDevExt->bDeviceRemoved == TRUE)
            {
               pDevExt->bL1Stopped = TRUE; // bCancelled = TRUE;
            }

            // used by FindNextReadIrp() in pCompletionRoutine()
            // pDevExt->pReadCurrent = NULL; // moved into ReadIrpCompletion()

            // check if cancelled
            if ((ntStatus == STATUS_CANCELLED) || (pDevExt->bL1Stopped == TRUE))
            {
               QCSER_DbgPrint2
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> RCOMP to OUT lock (nts 0x%x, stp %d) -1\n", pDevExt->PortName,
                    ntStatus, pDevExt->bL1Stopped)
               );
               // singnal to go back up and drain the queue

               if (pCurrIOBlock != NULL)
               {
                  if (pCurrIOBlock->pCallingIrp != NULL)
                  {
                     PIRP irp = pCurrIOBlock->pCallingIrp;

                     if (IoSetCancelRoutine(irp, NULL) != NULL)
                     {
                        if (pCurrIOBlock->TimerExpired == FALSE)
                        {
                           AbortReadTimeout(pCurrIOBlock);
                        }
                        irp->IoStatus.Status = ntStatus;
                        irp->IoStatus.Information = 0;
                        pCurrIOBlock->pCallingIrp = NULL;
                        QCSER_DbgPrint
                        (
                           QCSER_DBG_MASK_RIRP,
                           QCSER_DBG_LEVEL_DETAIL,
                           ("<%s> RIRP (Ccx 0x%x/%ldB) 0x%p\n", pDevExt->PortName, ntStatus,
                            irp->IoStatus.Information, irp)
                        );
                        QcIoReleaseRemoveLock(pDevExt->pRemoveLock, irp, 1);
                        QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
                        _IoCompleteRequest(irp, IO_NO_INCREMENT);
                     }
                     else
                     {
                        QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);  // WARNING!!!
                        QCSER_DbgPrint
                        (
                           QCSER_DBG_MASK_RIRP,
                           QCSER_DBG_LEVEL_ERROR,
                           ("<%s> RCOMP: IRP 0x%p NUL CxnRtn\n", pDevExt->PortName, irp)
                        );
                     }
                  }
                  else
                  {
                     QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);  // WARNING!!!
                  }
                  QCSER_DbgPrint2
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> free IOB 06 0x%p\n", pDevExt->PortName, pCurrIOBlock)
                  );
                  pDevExt->pReadCurrent = NULL;
                  QcExFreeReadIOB(pCurrIOBlock, TRUE);
               }
               else
               {
                  QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);  // WARNING!!!
               }
               continue;
            }
            // If the calling read irp is still there unsatisfied, re-que
            // the read
            else if ((pCurrIOBlock != NULL ) && (pCurrIOBlock->pCallingIrp != NULL))
            {
               pCurrIOBlock->ntStatus = STATUS_PENDING;
               pCurrIOBlock->pNextEntry = pDevExt->pReadHead;
               pCurrIOBlock->bPurged = FALSE;
               InterlockedExchange(&(pCurrIOBlock->lPurgeForbidden), pDevExt->lPurgeBegin);
               pDevExt->pReadHead = pCurrIOBlock;
               QCSER_DbgPrint2
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_FORCE,
                  ("<%s> L1: reQ: 0x%p/0x%p\n", pDevExt->PortName,
                    pCurrIOBlock->pCallingIrp, pCurrIOBlock)
               );
               pDevExt->pReadCurrent = pCurrIOBlock = NULL;
               QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
               continue; // go around again
            }

            // If the que is empty and there's room in the buffer, re-que
            // the read
            else if (pDevExt->pReadHead == NULL)
            {
               if ((CountReadQueue(pDevExt) < pDevExt->lReadBufferHigh) ||
                   (pDevExt->ContinueOnOverflow == TRUE))
               {
                  QCSER_DbgPrint2
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> L1 re-queue IOB\n", pDevExt->PortName)
                  );
                  pCurrIOBlock->bPurged = FALSE;
                  InterlockedExchange(&(pCurrIOBlock->lPurgeForbidden), pDevExt->lPurgeBegin);
                  pCurrIOBlock->ntStatus = STATUS_PENDING;
                  pCurrIOBlock->pNextEntry = pDevExt->pReadHead;
                  pDevExt->pReadHead = pCurrIOBlock;
                  pDevExt->pReadCurrent = pCurrIOBlock = NULL;
                  QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
                  continue; // go de-queue it immediately
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> ERR: L1 full, no req to dev.\n", pDevExt->PortName)
                  );
               }
            } // filling the read buffer

            QCSER_DbgPrint2
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> RCOMP to OUT lock (free IOB 0x%p) -2\n", pDevExt->PortName, pCurrIOBlock)
            );
            QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
            // otherwise, release pCurrIOBlock
            pDevExt->pReadCurrent = NULL;
            QcExFreeReadIOB(pCurrIOBlock, TRUE);
            break;
         }

         case L1_READ_AVAILABLE_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L1ReadAvailableEvent);

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> L1_READ_AVAILABLE\n", pDevExt->PortName)
            );

            pDevExt->bL1ReadActive = FALSE;

            if (pCurrIOBlock == NULL)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> L1: COMP but NUL RD-2\n", pDevExt->PortName)
               );
               continue;
            }

            // check completion status
            QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);

            if (pCurrIOBlock->bPurged == TRUE)
            {
               QCSER_CancelActiveIOB(pDevExt, pCurrIOBlock, STATUS_CANCELLED);
               pCurrIOBlock = NULL;
               QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);

               KeSetEvent
               (
                  &pDevExt->ReadIrpPurgedEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );
               continue;
            }

            pCurrIOBlock->ntStatus = STATUS_SUCCESS;
            pCurrIOBlock->ulActiveBytes = 0; // not filling buffer
            // pCurrIOBlock->pBufferFromDevice = NULL;
            pDevExt->bPacketsRead = TRUE;
            ntStatus = pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, FALSE, 10);

            if (pDevExt->bDeviceRemoved == TRUE)
            {
               pDevExt->bL1Stopped = TRUE; // bCancelled = TRUE;
            }

            if ((ntStatus == STATUS_CANCELLED) || (pDevExt->bL1Stopped == TRUE))
            {
               QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
               if (pCurrIOBlock != NULL)
               {
                  QCSER_DbgPrint2
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> free IOB 07 0x%p\n", pDevExt->PortName, pCurrIOBlock)
                  );
                  pDevExt->pReadCurrent = NULL;  // need this before freeing IOB
                  QcExFreeReadIOB(pCurrIOBlock, TRUE);
               }
               continue;
            }
            // If the calling read irp is still there unsatisfied, re-que
            // the read
            else if (pCurrIOBlock->pCallingIrp)
            {
               pCurrIOBlock->ntStatus = STATUS_PENDING;
               pCurrIOBlock->pNextEntry = pDevExt->pReadHead;
               pCurrIOBlock->bPurged = FALSE;
               InterlockedExchange(&(pCurrIOBlock->lPurgeForbidden), pDevExt->lPurgeBegin);
               pDevExt->pReadHead = pCurrIOBlock;
               pDevExt->pReadCurrent = pCurrIOBlock = NULL;
               QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
               continue; // go around again
            }

            // If the que is empty and there's room in the buffer, re-que
            // the read
            else if (pDevExt->pReadHead == NULL)
            {
               if ((CountReadQueue(pDevExt) < pDevExt->lReadBufferHigh) ||
                   (pDevExt->ContinueOnOverflow == TRUE))
               {
                  // QCSER_DbgPrint
                  // (
                  //    QCSER_DBG_MASK_READ,
                  //    QCSER_DBG_LEVEL_DETAIL,
                  //    ("<%s> L1 re-queue IOB - 2\n", pDevExt->PortName)
                  // );
                  pCurrIOBlock->bPurged = FALSE;
                  InterlockedExchange(&(pCurrIOBlock->lPurgeForbidden), pDevExt->lPurgeBegin);
                  pCurrIOBlock->ntStatus = STATUS_PENDING;
                  pCurrIOBlock->pNextEntry = pDevExt->pReadHead;
                  pDevExt->pReadHead = pCurrIOBlock;
                  pDevExt->pReadCurrent = pCurrIOBlock = NULL;
                  QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
                  continue; // go de-queue it immediately
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> ERR: L1 full, no req to dev -2.\n", pDevExt->PortName)
                  );
               }
            } // filling the read buffer

/*****
            if ((CountL1ReadQueue(pDevExt) > 0) && (FindReadIrp(pDevExt, NULL) != NULL))
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> L1 rd avail sig-2.\n", pDevExt->PortName)
               );
               KeSetEvent
               (
                  &pDevExt->L1ReadAvailableEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );
            }
*****/

            QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
            pDevExt->pReadCurrent = NULL; // need this before freeing IOB
            QCSER_DbgPrint2
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> free IOB 08 0x%p\n", pDevExt->PortName, pCurrIOBlock)
            );
            QcExFreeReadIOB(pCurrIOBlock, TRUE);
            break;
         }

         case L1_READ_PRE_TIMEOUT_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L1ReadPreTimeoutEvent);

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> L1_READ_PRE_TIMEOUT\n", pDevExt->PortName)
            );

            QCRD_ProcessPreTimeoutIOB(pDevExt);
            break;
         }

         case L1_KICK_READ_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L1KickReadEvent);

            // Use error level as output priority
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> L1 R kicked, active=%d\n", pDevExt->PortName, pDevExt->bL1ReadActive)
            );

            // kick L2 thread
            KeSetEvent
            (
               &pDevExt->L2KickReadEvent,
               IO_NO_INCREMENT,
               FALSE
            ); // kick the read thread

            if (pDevExt->bL1ReadActive == TRUE)
            {
               // if a read is active, we can ignore activations, but must
               // continue to wait for the completion
               goto wait_for_completion;
            }
            else if (pCurrIOBlock != NULL)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> L1R kicked: 0x%p (%d)\n", pDevExt->PortName, pCurrIOBlock, pDevExt->bL1ReadActive)
               );
               goto wait_for_completion;
            }
            // else we just go around the loop again, picking up the next
            // read entry

            continue;
         }

         case L1_CANCEL_CURRENT_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->CancelCurrentReadEvent);
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> L1_CANCEL_CURRENT-0", pDevExt->PortName)
            );

            QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);
            if ((pDevExt->bL1ReadActive == TRUE) && (pCurrIOBlock != NULL))
            {
               // make sure nothing changed between signal trigger point and now
               if (pCurrIOBlock->TimerExpired == TRUE)
               {
                  // QCSER_DbgPrint
                  // (
                  //    QCSER_DBG_MASK_READ,
                  //    QCSER_DBG_LEVEL_FORCE,
                  //    ("<%s> Rth: Cxl curr\n", pDevExt->PortName)
                  // );
                  QCSER_CancelActiveIOB(pDevExt, pCurrIOBlock, STATUS_TIMEOUT);
                  pCurrIOBlock = NULL;
                  pDevExt->bL1ReadActive = FALSE;
               }
            }
            QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> L1_CANCEL_CURRENT-1", pDevExt->PortName)
            );
            break;
         }

         case L1_CANCEL_EVENT_INDEX:
         {
            pDevExt->bL1InCancellation = TRUE;
            pDevExt->bL1Stopped = TRUE;

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> L1_CANCEL_EVENT_INDEX\n", pDevExt->PortName)
            );
            
            // reset read cancel event so we dont reactive
            KeClearEvent(&pDevExt->L1CancelReadEvent); // never set

            // Alert L2 thread
            if ((pDevExt->hL2ReadThreadHandle != NULL) || (pDevExt->pL2ReadThread != NULL))
            {
               KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
               KeSetEvent
               (
                  &pDevExt->CancelReadEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );

               if (pDevExt->pL2ReadThread != NULL)
               {
                  KeWaitForSingleObject
                  (
                     pDevExt->pL2ReadThread,
                     Executive,
                     KernelMode,
                     FALSE,
                     NULL
                  );
                  ObDereferenceObject(pDevExt->pL2ReadThread);
                  KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
                  _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-7");
                  pDevExt->pL2ReadThread = NULL;
               }
               else  // best effort
               {
                  KeWaitForSingleObject
                  (
                     &pDevExt->L2ReadThreadClosedEvent,
                     Executive,
                     KernelMode,
                     FALSE,
                     NULL
                  );
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> L1R cxl: L2 OUT\n", pDevExt->PortName)
                  );
                  KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
                  _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-8");
               }
            }

            if (pDevExt->bL1ReadActive == TRUE)
            {
               QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);
               QCSER_CancelActiveIOB(pDevExt, pCurrIOBlock, STATUS_CANCELLED);
               pCurrIOBlock = NULL;
               pDevExt->bL1ReadActive = FALSE;
               QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
            }
            else if (pCurrIOBlock != NULL)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> L1R cxl: 0x%p (%d)\n", pDevExt->PortName, pCurrIOBlock, pDevExt->bL1ReadActive)
               );
            }
            bCancelled = TRUE; // singnal the loop that a cancel has occurred
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> L1_CANCEL_EVENT_INDEX END\n", pDevExt->PortName)
            );
            break;
         }

         case L1_READ_PURGE_EVENT_INDEX:
         {
            // reset read purge event so we dont reactive
            KeClearEvent(&pDevExt->L1ReadPurgeEvent);

            // Notify L2 thread
            KeClearEvent(&pDevExt->L1ReadPurgeAckEvent);
            KeSetEvent
            (
               &pDevExt->L2ReadPurgeEvent,
               IO_NO_INCREMENT,
               FALSE
            );
            KeWaitForSingleObject
            (
               &pDevExt->L1ReadPurgeAckEvent,
               Executive,
               KernelMode,
               FALSE,
               NULL    // &delayValue
            );
            KeClearEvent(&pDevExt->L1ReadPurgeAckEvent);

            if (pDevExt->bL1ReadActive == TRUE)
            {
               QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);
               QCSER_CancelActiveIOB(pDevExt, pCurrIOBlock, STATUS_CANCELLED);
               pCurrIOBlock = NULL;
               pDevExt->bL1ReadActive = FALSE;
               QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
               KeSetEvent
               (
                  &pDevExt->ReadIrpPurgedEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );
               // goto wait_for_completion;
            }
            else if (pCurrIOBlock != NULL)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> L1R prg: 0x%p (%d)\n", pDevExt->PortName, pCurrIOBlock, pDevExt->bL1ReadActive)
               );
            }
            break;  // go around and process another IOB
         }
         
         default:
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> L1 unsupported st 0x%x\n", pDevExt->PortName, ntStatus)
            );

            // Ignore it for now
            break;
         }  // default

      }  // switch (ntStatus)

   }  // while (bNoptDone)

   // exiting thread, release allocated resources
   if(pwbArray)
   {
      _ExFreePool(pwbArray);
      pwbArray = NULL;
   }

   if ((pDevExt->hL2ReadThreadHandle != NULL) || (pDevExt->pL2ReadThread != NULL))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> L1 thread exiting, cancelling L2\n", pDevExt->PortName)
      );

      KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
      KeSetEvent
      (
         &pDevExt->CancelReadEvent,
         IO_NO_INCREMENT,
         FALSE
      );

      if (pDevExt->pL2ReadThread != NULL)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> L1 thread exiting, cancelling L2 - 2 \n", pDevExt->PortName)
         );
         KeWaitForSingleObject
         (
            pDevExt->pL2ReadThread,
            Executive,
            KernelMode,
            FALSE,
            NULL
         );
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> L1 thread exiting, cancelling L2 - 3 \n", pDevExt->PortName)
         );
         ObDereferenceObject(pDevExt->pL2ReadThread);
         KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
         _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-5");
         pDevExt->pL2ReadThread = NULL;
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> L1 thread exiting, cancelling L2 - 4 \n", pDevExt->PortName)
         );
      }
      else  // best effort
      {
         KeWaitForSingleObject
         (
            &pDevExt->L2ReadThreadClosedEvent,
            Executive,
            KernelMode,
            FALSE,
            NULL       // &delayValue
         );
         KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
         _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-6");
      }
   }

   // Reset L2 Buffers
   for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
   {
      pDevExt->pL2ReadBuffer[i].bFilled = FALSE;
      pDevExt->pL2ReadBuffer[i].Length  = 0;
      pDevExt->pL2ReadBuffer[i].Status  = STATUS_PENDING;
   }

   KeSetEvent(&pDevExt->L1ReadThreadClosedEvent,IO_NO_INCREMENT,FALSE);
   pDevExt->bL1InCancellation = FALSE;
   pDevExt->bL1Stopped = FALSE;

   _closeHandle(pDevExt->hL1ReadThreadHandle, "R-3");
   PsTerminateSystemThread(STATUS_SUCCESS); // terminate this thread
}  // STESerial_ReadThread

void QCSER_ReadThread(PDEVICE_EXTENSION pDevExt)
{
   PIRP pIrp, pIrp2;
   PIO_STACK_LOCATION pNextStack;
   PURB     pUrb = NULL, pUrb2 = NULL;
   UCHAR    ucPipeIndex;
   BOOLEAN  bCancelled = FALSE;
   BOOLEAN  bL2ReadActive = FALSE;
   NTSTATUS ntStatus;
   BOOLEAN  bFirstTime = TRUE;
   PKWAIT_BLOCK pwbArray;
   LONG     lBytesInQueue;
   ULONG    ulReadSize;
   KEVENT   dummyEvent;
   int      i, devBusyCnt = 0, devErrCnt = 0;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> L2 ERR: wrong IRQL::%d\n", pDevExt->PortName, KeGetCurrentIrql())
      );
      #ifdef DEBUG_MSGS
      _asm int 3;
      #endif
   }

   // allocate an urb for read operations
   pUrb = ExAllocatePool
          (
             NonPagedPool,
             sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER)
          );
   if (pUrb == NULL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> L2 - STATUS_NO_MEMORY 0\n", pDevExt->PortName)
      );
      _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-0");
      pDevExt->bL2ThreadInCreation = FALSE;
      KeSetEvent(&pDevExt->L2ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   #ifdef QCSER_2ND_IRP
   pUrb2 = ExAllocatePool
          (
             NonPagedPool,
             sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER)
          );
   if (pUrb2 == NULL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> L2 - STATUS_NO_MEMORY 0.1\n", pDevExt->PortName)
      );
      ExFreePool(pUrb);
      _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-1");
      pDevExt->bL2ThreadInCreation = FALSE;
      KeSetEvent(&pDevExt->L2ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }
   #endif //QCSER_2ND_IRP

   // allocate a wait block array for the multiple wait
   pwbArray = _ExAllocatePool
              (
                 NonPagedPool,
                 (L2_READ_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK),
                 "Level2ReadThread.pwbArray"
              );
   if (!pwbArray)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> L2 - STATUS_NO_MEMORY 1\n", pDevExt->PortName)
      );
      ExFreePool(pUrb);
      #ifdef QCSER_2ND_IRP
      ExFreePool(pUrb2);
      #endif
      _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-2");
      pDevExt->bL2ThreadInCreation = FALSE;
      KeSetEvent(&pDevExt->L2ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   // allocate irp to use for read operations
   pIrp = IoAllocateIrp((CCHAR)(pDevExt->MyDeviceObject->StackSize), FALSE);
   if (!pIrp )
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> L2 - STATUS_NO_MEMORY 2\n", pDevExt->PortName)
      );
      ExFreePool(pUrb);
      #ifdef QCSER_2ND_IRP
      ExFreePool(pUrb2);
      #endif
      _ExFreePool(pwbArray);
      _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-3");
      pDevExt->bL2ThreadInCreation = FALSE;
      KeSetEvent(&pDevExt->L2ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   #ifdef QCSER_2ND_IRP
   pIrp2 = IoAllocateIrp((pDevExt->MyDeviceObject->StackSize), FALSE);
   if (!pIrp2 )
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> L2 - STATUS_NO_MEMORY 2.1\n", pDevExt->PortName)
      );
      ExFreePool(pUrb);
      ExFreePool(pUrb2);
      _ExFreePool(pwbArray);
      IoReuseIrp(pIrp, STATUS_SUCCESS);
      IoFreeIrp(pIrp);
      _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-4");
      pDevExt->bL2ThreadInCreation = FALSE;
      KeSetEvent(&pDevExt->L2ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }
   #endif // QCSER_2ND_IRP

   #ifdef ENABLE_LOGGING
   // Create logs
   if (pDevExt->EnableLogging == TRUE)
   {
      QCSER_CreateLogs(pDevExt, QCSER_CREATE_RX_LOG);
   }
   #ifdef QCSER_ENABLE_LOG_REC
   if (pDevExt->LogLatestPkts == TRUE)
   {
      RtlZeroMemory(pDevExt->RxLogRec, sizeof(LogRecType)*NUM_LATEST_PKTS);
      pDevExt->RxLogRecIndex = 0;
   }
   #endif  // QCSER_ENABLE_LOG_REC
   #endif  // ENABLE_LOGGING

   // Set L2 thread priority
   KeSetPriorityThread(KeGetCurrentThread(), QCSER_L2_PRIORITY);
   // KeSetPriorityThread(KeGetCurrentThread(), (pDevExt->L1Priority+2));
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> L2: pri=%d, L2Buf=%d\n",
        pDevExt->PortName,
        KeQueryPriorityThread(KeGetCurrentThread()),
        pDevExt->NumberOfL2Buffers
      )
   );

   pDevExt->bL2ReadActive = bL2ReadActive = FALSE;

   pDevExt->pL2ReadEvents[QC_DUMMY_EVENT_INDEX] = &dummyEvent;
   KeInitializeEvent(&dummyEvent,NotificationEvent,FALSE);
   KeClearEvent(&dummyEvent);

   pDevExt->bL2Stopped = pDevExt->PowerSuspended;

   while (bCancelled == FALSE)
   {
      if ((pDevExt->pL2ReadBuffer[pDevExt->L2IrpIdx].bFilled == FALSE) &&
          inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
      {
         if ((bL2ReadActive == FALSE) && (pDevExt->bL2Stopped == FALSE))
         {
            IoReuseIrp(pIrp, STATUS_SUCCESS);

            // initialize the static parameters in the read irp
            pNextStack = IoGetNextIrpStackLocation( pIrp );
            pNextStack->Parameters.Others.Argument1 = pUrb;
            pNextStack->Parameters.DeviceIoControl.IoControlCode =
               IOCTL_INTERNAL_USB_SUBMIT_URB;
            pNextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;

            IoSetCompletionRoutine
            (
               pIrp,
               ReadCompletionRoutine,
               pDevExt,
               TRUE,
               TRUE,
               TRUE
            );

            lBytesInQueue = CountReadQueue(pDevExt);  // should be updated in real time
            if (lBytesInQueue >= pDevExt->lReadBuffer80pct)
            {
               ulReadSize = pDevExt->MinInPktSize; // 64;
            }
/***
            else if (lBytesInQueue >= pDevExt->lReadBuffer50pct)
            {
               ulReadSize = pDevExt->MinInPktSize;
            }
            else if (lBytesInQueue >= pDevExt->lReadBuffer20pct)
            {
               if (pDevExt->MinInPktSize > 256)
               {
                  ulReadSize = pDevExt->MinInPktSize;
               }
               else
               {
                  ulReadSize = 256;
               }
            }
***/
            else
            {
               ulReadSize = pDevExt->MaxPipeXferSize;
            }

            // setup the URB  for this request
            UsbBuildInterruptOrBulkTransferRequest
            (
               pUrb,
               sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
               pDevExt->Interface[pDevExt->DataInterface]->Pipes[pDevExt->BulkPipeInput].PipeHandle,
               pDevExt->pL2ReadBuffer[pDevExt->L2IrpIdx].Buffer,
               NULL,
               ulReadSize,
               USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN,
               NULL
            )

            pDevExt->bL2ReadActive = bL2ReadActive = TRUE;
            ntStatus = IoCallDriver(pDevExt->StackDeviceObject,pIrp);
            if (ntStatus != STATUS_PENDING)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_CRITICAL,
                  ("<%s> Rth: IoCallDriver rtn 0x%x", pDevExt->PortName, ntStatus)
               );
               if ((ntStatus == STATUS_DEVICE_NOT_READY) || (ntStatus == STATUS_DEVICE_DATA_ERROR))
               {
                  devErrCnt++;
                  if (devErrCnt > 3)
                  {
                     pDevExt->bL2Stopped = TRUE;
                     QCSER_DbgPrint
                     (
                        QCSER_DBG_MASK_READ,
                        QCSER_DBG_LEVEL_ERROR,
                        ("<%s> L2 err, stopped\n", pDevExt->PortName)
                     );
                  }
               }
               else
               {
                  devErrCnt = 0;
               }
            }
            else
            {
               devErrCnt = 0;
            }
            #ifdef QCOM_TRACE_IN
            KdPrint(("L2 ON %ld/%lu\n", ulReadSize, CountReadQueue(pDevExt)));
            #endif
         } // if L2 is inactive and not stopped
         else
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> L2R: ERR-ACTorSTOP\n", pDevExt->PortName)
            );
         }
      } // end if -- if L2 buffer is available for filling
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> L2 buf exhausted or no dev, wait...\n", pDevExt->PortName)
         );
      }

      // No matter what IoCallDriver returns, we always wait on the kernel event
      // we created earlier. Our completion routine will gain control when the IRP
      // completes to signal this event. -- Walter Oney's WDM book page 228
wait_for_completion:

      if(bFirstTime == TRUE)
      {
         bFirstTime = FALSE;

         // inform read function that we've started
         KeSetEvent(&pDevExt->L2ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      }

      if (KeGetCurrentIrql() > PASSIVE_LEVEL)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> L2 ERR: wrong IRQL::%d -2\n", pDevExt->PortName, KeGetCurrentIrql())
         );
         #ifdef DEBUG_MSGS
         _asm int 3;
         #endif
      }

      ntStatus = KeWaitForMultipleObjects
                 (
                    L2_READ_EVENT_COUNT,
                    (VOID **)&pDevExt->pL2ReadEvents,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE,    // non-alertable
                    NULL,     // no timeou for bulk read
                    pwbArray
                 );

      switch (ntStatus)
      {
         case QC_DUMMY_EVENT_INDEX:
         {
            KeClearEvent(&dummyEvent);
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> L2: DUMMY_EVENT\n", pDevExt->PortName)
            );
            goto wait_for_completion;
         }
         case STATUS_ALERTED:
         {
            // this case should not happen
            goto wait_for_completion;
            break;
         }

         case L2_READ_COMPLETION_EVENT_INDEX:
         {
            // clear read completion event
            KeClearEvent(&pDevExt->L2ReadCompletionEvent);
            pDevExt->bL2ReadActive = bL2ReadActive = FALSE;

            ntStatus = pIrp->IoStatus.Status;

            #ifdef ENABLE_LOGGING
            if (ntStatus == STATUS_SUCCESS)
            {
               #ifdef QCOM_TRACE_IN
               KdPrint(("   L2R %ld\n", pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength));
               #endif
               // log to file
               if ((pDevExt->EnableLogging == TRUE) && (pDevExt->hRxLogFile != NULL))
               {
                  QCSER_LogData
                  (
                     pDevExt,
                     pDevExt->hRxLogFile,
                     (PVOID)(pUrb->UrbBulkOrInterruptTransfer.TransferBuffer),
                     pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength,
                     QCSER_LOG_TYPE_READ
                  );
               }

               #ifdef QCSER_ENABLE_LOG_REC
               // log to memory
               if (pDevExt->LogLatestPkts == TRUE)
               {
                  QCSER_GetSystemTimeString(pDevExt->RxLogRec[pDevExt->RxLogRecIndex].TimeStamp);
                  pDevExt->RxLogRec[pDevExt->RxLogRecIndex].PktLength =
                     pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;
                  RtlCopyBytes
                  (
                     (PVOID)pDevExt->RxLogRec[pDevExt->RxLogRecIndex].Data,
                     (PVOID)(pUrb->UrbBulkOrInterruptTransfer.TransferBuffer),
                     pDevExt->RxLogRec[pDevExt->RxLogRecIndex].PktLength > 64? 64:
                        pDevExt->RxLogRec[pDevExt->RxLogRecIndex].PktLength
                  );
                  if (++(pDevExt->RxLogRecIndex) >= NUM_LATEST_PKTS)
                  {
                     pDevExt->RxLogRecIndex = 0;
                  }
               }
               #endif // QCSER_ENABLE_LOG_REC
            }
            else
            {
               if ((pDevExt->EnableLogging == TRUE) && (pDevExt->hRxLogFile != NULL))
               {
                  // log to file
                  QCSER_LogData
                  (
                     pDevExt,
                     pDevExt->hRxLogFile,
                     (PVOID)&ntStatus,
                     sizeof(NTSTATUS),
                     QCSER_LOG_TYPE_RESPONSE_RD
                  );
               }

               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_CRITICAL,
                  ("<%s> L2 RX failed - 0x%x\n", pDevExt->PortName, pIrp->IoStatus.Status)
               );
            }
            #endif  // ENABLE_LOGGING

            if ((pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength > 0)
                || (ntStatus != STATUS_SUCCESS))
            {
               pDevExt->pL2ReadBuffer[pDevExt->L2IrpIdx].Status = ntStatus;
               pDevExt->pL2ReadBuffer[pDevExt->L2IrpIdx].Length =
                  pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;
               pDevExt->pL2ReadBuffer[pDevExt->L2IrpIdx].bFilled = TRUE;
               if (++(pDevExt->L2IrpIdx) == pDevExt->NumberOfL2Buffers)
               {
                  pDevExt->L2IrpIdx = 0;
               }

               if (!NT_SUCCESS(ntStatus))
               {
                  // STATUS_DEVICE_NOT_READY      0xC00000A3
                  // STATUS_DEVICE_DATA_ERROR     0xC000009C
                  // STATUS_DEVICE_NOT_CONNECTED  0xC000009D
                  // STATUS_UNSUCCESSFUL          0xC0000001
                  if (ntStatus != STATUS_CANCELLED)
                  {
                     if ((++devBusyCnt >= (pDevExt->NumOfRetriesOnError+2)) && (bCancelled == FALSE))
                     {
                        pDevExt->bL2Stopped = TRUE;
                        QCSER_DbgPrint
                        (
                           QCSER_DBG_MASK_READ,
                           QCSER_DBG_LEVEL_CRITICAL,
                           ("<%s> L2 cont failure, stopped...\n", pDevExt->PortName)
                        );
                        devBusyCnt = 0;
                        KeSetEvent
                        (
                           &pDevExt->L1ReadCompletionEvent,
                           IO_NO_INCREMENT,
                           FALSE
                        );
                        goto wait_for_completion;
                     }
                     if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
                     {
                        QCSER_DbgPrint
                        (
                           QCSER_DBG_MASK_READ,
                           QCSER_DBG_LEVEL_CRITICAL,
                           ("<%s> L2 RX failed, yield\n", pDevExt->PortName)
                        );
                        QCSER_Wait(pDevExt, -(1000 * 1000L));  // 100ms
                     }
                  }
                  else
                  {
                     devBusyCnt = 0;
                  }
               }
               else
               {
                  devBusyCnt = 0;
               }

               // Notify L1 thread
               KeSetEvent
               (
                  &pDevExt->L1ReadCompletionEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );

               continue;
            }

            if (pDevExt->bDeviceRemoved == TRUE)
            {
               // Clear all received bytes
               pDevExt->L2IrpIdx = pDevExt->L2FillIdx = 0;
               for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
               {
                  pDevExt->pL2ReadBuffer[i].bFilled = FALSE;
                  pDevExt->pL2ReadBuffer[i].Length  = 0;
                  pDevExt->pL2ReadBuffer[i].Status  = STATUS_PENDING;
               }
               pDevExt->bL2Stopped = TRUE; // bCancelled = TRUE;
            }

            break;
         }

         case L2_KICK_READ_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L2KickReadEvent);

            // Use error level as output priority
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> L2 R kicked, act=%d\n", pDevExt->PortName, bL2ReadActive)
            );

            if (bL2ReadActive == TRUE)
            {
               // if a read is active, we can ignore activations, but must
               // continue to wait for the completion
               goto wait_for_completion;
            }
            // else we just go around the loop again, picking up the next
            // read entry
            continue;
         }

         case CANCEL_EVENT_INDEX:
         {
            // reset read cancel event so we dont reactive
            KeClearEvent(&pDevExt->CancelReadEvent); // never set

            // Reset all L2 buffers on cancel event
            pDevExt->L2IrpIdx = pDevExt->L2FillIdx = 0;
            for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
            {
               pDevExt->pL2ReadBuffer[i].bFilled = FALSE;
               pDevExt->pL2ReadBuffer[i].Length  = 0;
               pDevExt->pL2ReadBuffer[i].Status  = STATUS_PENDING;
            }

            #ifdef ENABLE_LOGGING
            QCSER_LogData
            (
               pDevExt,
               pDevExt->hRxLogFile,
               (PVOID)NULL,
               0,
               QCSER_LOG_TYPE_CANCEL_THREAD
            );
            #endif // ENABLE_LOGGING

            bCancelled = pDevExt->bL2Stopped = TRUE; // singnal the loop that a cancel has occurred
            if (bL2ReadActive == TRUE)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_INFO,
                  ("<%s> L2 CANCEL - IRP\n", pDevExt->PortName)
               );
               IoCancelIrp(pIrp); // we don't care the result
               goto wait_for_completion;
            }
            break;
         }

         case L2_READ_PURGE_EVENT_INDEX:  // 4
         {
            KeClearEvent(&pDevExt->L2ReadPurgeEvent);

            // Reset all L2 buffers -- should I put the following within SpinLock?????
            pDevExt->L2IrpIdx = pDevExt->L2FillIdx = 0;
            for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
            {
               pDevExt->pL2ReadBuffer[i].bFilled = FALSE;
               pDevExt->pL2ReadBuffer[i].Length  = 0;
               pDevExt->pL2ReadBuffer[i].Status  = STATUS_PENDING;
            }

            if (bL2ReadActive == TRUE)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_INFO,
                  ("<%s>    L2 READ_PURGE_EVENT: Cancel(pIrp)\n", pDevExt->PortName)
               );
               // cancel any outstanding read so we can get out
               IoCancelIrp(pIrp); // we don't care the result

               KeSetEvent
               (
                  &pDevExt->L1ReadPurgeAckEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );
               // if a read is active, continue to wait for the read
               // completion
               goto wait_for_completion;
            }

            KeSetEvent
            (
               &pDevExt->L1ReadPurgeAckEvent,
               IO_NO_INCREMENT,
               FALSE
            );

            break;
         }

         case L2_READ_STOP_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L2ReadStopEvent);

            pDevExt->bL2Stopped = TRUE;

            if (bL2ReadActive == TRUE)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_INFO,
                  ("<%s>    L2_READ_STOP: Cancel(pIrp)\n", pDevExt->PortName)
               );
               // cancel any outstanding read so we can get out
               IoCancelIrp(pIrp); // we don't care the result

               KeSetEvent(&pDevExt->L2ReadStopAckEvent, IO_NO_INCREMENT, FALSE);

               // if a read is active, continue to wait for the read
               // completion
               goto wait_for_completion;
            }

            KeSetEvent(&pDevExt->L2ReadStopAckEvent, IO_NO_INCREMENT, FALSE);

            break;
         }  // L2_READ_STOP_EVENT_INDEX

         case L2_READ_RESUME_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L2ReadResumeEvent);

            if (TRUE == (pDevExt->bL2Stopped = pDevExt->PowerSuspended))
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> L2 Resume: in suspend mode, no act\n", pDevExt->PortName)
               );
               goto wait_for_completion;
            }

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> L2 Resume: act=%d\n", pDevExt->PortName, bL2ReadActive)
            );

            // Kick the L2
            KeSetEvent
            (
               &pDevExt->L2KickReadEvent,
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
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> L2 unsupported st 0x%x\n", pDevExt->PortName, ntStatus)
            );

            // Ignore for now
            break;

         }  // default

      }  // switch
   }  // end while forever 

   // exiting thread, release allocated resources
   if(pwbArray)
   {
      _ExFreePool(pwbArray);
      pwbArray = NULL;
   }
   if(pUrb) 
   {
      ExFreePool(pUrb);
      pUrb = NULL;
   }
   if(pIrp)
   {
      IoReuseIrp(pIrp, STATUS_SUCCESS);
      IoFreeIrp(pIrp);
   }
   #ifdef QCSER_2ND_IRP
   if(pUrb2) 
   {
      ExFreePool(pUrb2);
      pUrb2 = NULL;
   }
   if(pIrp2)
   {
      IoReuseIrp(pIrp2, STATUS_SUCCESS);
      IoFreeIrp(pIrp2);
   }
   #endif // QCSER_2ND_IRP

   pDevExt->bL2Stopped = FALSE;

   if (pDevExt->hRxLogFile != NULL)
   {
      #ifdef ENABLE_LOGGING
      QCSER_LogData
      (
         pDevExt,
         pDevExt->hRxLogFile,
         (PVOID)NULL,
         0,
         QCSER_LOG_TYPE_THREAD_END
      );
      #endif // ENABLE_LOGGING
      ZwClose(pDevExt->hRxLogFile);
      pDevExt->hRxLogFile = NULL;
   }

   KeSetEvent(&pDevExt->L2ReadThreadClosedEvent,IO_NO_INCREMENT,FALSE);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> L2: OUT\n", pDevExt->PortName, ntStatus)
   );

   _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-B");
   PsTerminateSystemThread(STATUS_SUCCESS); // terminate this thread
}  // QCSER_ReadThread

// TimeoutReadRoutine() called with CancelSpinlock held 
VOID TimeoutReadRoutine
(
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock
)
{
   PDEVICE_EXTENSION pDevExt;
   BOOLEAN bFreeIOBlock;
   PIRP pIrp;

   pDevExt = pIOBlock->pSerialDeviceObject->DeviceExtension;
   pIrp = pIOBlock->pCallingIrp;
   if (IoSetCancelRoutine(pIrp, NULL) == NULL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_RIRP,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> TORR: nul CxlRtn 0x%p\n", pDevExt->PortName, pIrp)
      );
      return;
   }

   // remove it from pIOBlock
   AbortReadTimeout(pIOBlock);

   bFreeIOBlock = DeQueueIOBlock(pIOBlock, &pDevExt->pReadHead);

   if ((bFreeIOBlock == FALSE) && (pDevExt->pReadCurrent == pIOBlock))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_RIRP,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> TORR: cxl curr R\n", pDevExt->PortName)
      );
      // KeSetEvent(&pDevExt->CancelCurrentReadEvent,IO_NO_INCREMENT,FALSE);
      // return;
   }
   else if ((bFreeIOBlock == FALSE) && (pDevExt->pReadCurrent != pIOBlock))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_RIRP,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> TORR: stray away iob 0x%p/0x%p\n", pDevExt->PortName, pIrp, pIOBlock)
      );
      // the IOB may still being queued but not in queue yet.
      // do nothing to the IOB at this time, read thread takes care
      // Since we decided not to complete the IRP, we re-install the cancel
      // routine to let other task to complete the IRP.
      IoSetCancelRoutine(pIrp, CancelReadRoutine);
      return;
   }

   if (pIrp != NULL)
   {
      pIrp->IoStatus.Status = STATUS_TIMEOUT;
      pIOBlock->pCallingIrp = NULL;

      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_RIRP,
         QCSER_DBG_LEVEL_INFO,
         ("<%s> TORR: QtoC 0x%p/0x%p\n", pDevExt->PortName, pIrp, pIOBlock)
      );
      InsertTailList(&pDevExt->RdCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
      InterlockedIncrement(&(pDevExt->NumIrpsToComplete));
      KeSetEvent(&pDevExt->InterruptEmptyRdQueueEvent, IO_NO_INCREMENT, FALSE);
   }

   if (bFreeIOBlock)
   {
      QCSER_DbgPrint2
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> free IOB 09 0x%p\n", pDevExt->PortName, pIOBlock)
      );
      QcExFreeReadIOB(pIOBlock, FALSE);
   }
}  //TimeoutReadRoutine

VOID CancelReadRoutine(PDEVICE_OBJECT CalledDO, PIRP pIrp)
{
   KIRQL irql = KeGetCurrentIrql();
   PDEVICE_OBJECT DeviceObject = QCPTDO_FindPortDOByFDO(CalledDO, irql);
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock;
   BOOLEAN bFreeIOBlock = FALSE;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_ERROR,
      ("<%s> CancelReadRoutine: RIRP: 0x%p\n", pDevExt->PortName, pIrp)
   );
   IoReleaseCancelSpinLock(pIrp->CancelIrql);   //set by the IO mgr.

   QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);
   IoSetCancelRoutine(pIrp, NULL);  // not necessary

   // remove it from pIOBlock
   pIOBlock = FindReadIrp(pDevExt, pIrp);
   if (!pIOBlock)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> CxlRd: no IOB for IRP 0x%p, simply complete\n", pDevExt->PortName, pIrp)
      );

      // SR392475
      pIrp->IoStatus.Status = STATUS_CANCELLED;
      pIrp->IoStatus.Information = 0;
      InsertTailList(&pDevExt->RdCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
      InterlockedIncrement(&(pDevExt->NumIrpsToComplete));
      KeSetEvent(&pDevExt->InterruptEmptyRdQueueEvent, IO_NO_INCREMENT, FALSE);
      // End of SR392475
   }
   else
   {
      bFreeIOBlock = DeQueueIOBlock(pIOBlock, &pDevExt->pReadHead);
      AbortReadTimeout(pIOBlock);
      ASSERT(pIOBlock->pCallingIrp==pIrp);
      pIOBlock->pCallingIrp = NULL;

      pIrp->IoStatus.Status = STATUS_CANCELLED;
      pIrp->IoStatus.Information = 0;

      // put pIrp onto the completion queue
      InsertTailList(&pDevExt->RdCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
      InterlockedIncrement(&(pDevExt->NumIrpsToComplete));
      KeSetEvent(&pDevExt->InterruptEmptyRdQueueEvent, IO_NO_INCREMENT, FALSE);
   }

   if (bFreeIOBlock)
   {
      QCSER_DbgPrint2
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> free IOB 10 0x%p\n", pDevExt->PortName, pIOBlock)
      );
      QcExFreeReadIOB(pIOBlock, FALSE);
   }
   QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
}  // CancelReadRoutine

NTSTATUS QCRD_Read( IN PDEVICE_OBJECT CalledDO, IN PIRP pIrp )
{
   PDEVICE_OBJECT DeviceObject;
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   BOOLEAN status;
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock;
   UCHAR Modem[sizeof( USB_DEFAULT_PIPE_REQUEST )+sizeof( MODEM_INFO )];
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   DeviceObject = QCPTDO_FindPortDOByFDO(CalledDO, irql);
   if (DeviceObject == NULL)
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> RIRP: 0x%p (No Port for 0x%p)\n", gDeviceName, pIrp, CalledDO)
      );
      return QcCompleteRequest(pIrp, STATUS_DELETE_PENDING, 0);
   }

   pDevExt = DeviceObject->DeviceExtension;

   if (pDevExt->bInService == FALSE)
   {
      ntStatus = STATUS_UNSUCCESSFUL;

      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_RIRP,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> RIRP (Cus 0x%x/%ldB) 0x%p - no service\n", pDevExt->PortName, ntStatus,
          pIrp->IoStatus.Information, pIrp)
      );
      return QcCompleteRequest(pIrp, ntStatus, 0);
   }

/***

   #ifdef QCUSB_STACK_IO_ON
   if ((CalledDO == DeviceObject) && inDevState(DEVICE_STATE_PRESENT_AND_STARTED) &&
       (pDevExt->bStackOpen == TRUE))
   {
      return QCDSP_SendIrpToStack(DeviceObject, pIrp, "RIRP");
   }
   #endif // QCUSB_STACK_IO_ON

***/

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_RIRP,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s 0x%p> RIRP 0x%p => t 0x%p[%d/%d:%d]\n", pDevExt->PortName, DeviceObject, pIrp,
        KeGetCurrentThread(), pIrp->CurrentLocation, pIrp->StackCount, irql)
   );

   // HG_DBG
   // DbgPrint ("<%s 0x%p> RIRP 0x%p => t 0x%p[%d/%d:%d]\n", pDevExt->PortName, DeviceObject, pIrp,
   //      KeGetCurrentThread(), pIrp->CurrentLocation, pIrp->StackCount, irql);

   ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, pIrp);
   if (!NT_SUCCESS(ntStatus))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_RIRP,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> RIRP (Crm 0x%x/%ldB) 0x%p\n", pDevExt->PortName, ntStatus,
          pIrp->IoStatus.Information, pIrp)
      );
      return QcCompleteRequest(pIrp, ntStatus, 0);
   }
   QcInterlockedIncrement(1, pIrp, 7);

   pIrp->IoStatus.Information = 0;

   if (pDevExt->ucDeviceType >= DEVICETYPE_CTRL)
   {
      ntStatus = STATUS_UNSUCCESSFUL;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_RIRP,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> RIRP (Cus 0x%x/%ldB) 0x%p unsupported\n", pDevExt->PortName,
          ntStatus, pIrp->IoStatus.Information, pIrp)
      );
      goto Exit;
   }

   if ((gVendorConfig.DriverResident == 0) && (pDevExt->bInService == FALSE))
   {
      if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
      {
         ntStatus = STATUS_DELETE_PENDING;
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_RIRP,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> RIRP (Cdp 0x%x/%ldB) 0x%p No DEV (0x%x)\n", pDevExt->PortName,
             ntStatus, pIrp->IoStatus.Information, pIrp, pDevExt->bmDevState)
         );
         goto Exit;
      }
      // if ((pDevExt->bInService == FALSE) || (pDevExt->bDeviceRemoved == TRUE))
      if (pDevExt->bDeviceRemoved == TRUE)
      {
         ntStatus = STATUS_DELETE_PENDING;
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_RIRP,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> RIRP (Cdp 0x%x/%ldB) 0x%p No DEV-2 (0x%x)\n", pDevExt->PortName,
              ntStatus, pIrp->IoStatus.Information, pIrp, pDevExt->bmDevState)
         );
         goto Exit;
      } 
   }  // if driver not resident

   if (pDevExt->Sts.lRmlCount[4] <= 0 && pDevExt->bInService == TRUE) // device not opened
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> _Read: AC:RML_COUNTS=<%ld,%ld,%ld,%ld,%ld>\n",
           pDevExt->PortName,pDevExt->Sts.lRmlCount[0],pDevExt->Sts.lRmlCount[1],
           pDevExt->Sts.lRmlCount[2],pDevExt->Sts.lRmlCount[3],pDevExt->Sts.lRmlCount[4]
         )
      );
      #ifdef DEBUG_MSGS
      _asm int 3;
      #endif

      ntStatus = STATUS_DELETE_PENDING;
      goto Exit;
   }

   if (pDevExt->bL1InCancellation == TRUE)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> RD in cancellation\n", pDevExt->PortName)
      );
      ntStatus = STATUS_CANCELLED;
      goto Exit;
   }

   pDevExt->bWOMHeldForRead = FALSE;

   QcAcquireSpinLockWithLevel(&pDevExt->ReadSpinLock, &levelOrHandle, irql);

   if (!FindReadIrp(pDevExt, NULL) && bFilledReadIrpFromBuffer(pDevExt, pIrp, NULL))
   { // if there's no read irp pending & you fill this one from buffer...
      QcReleaseSpinLockWithLevel(&pDevExt->ReadSpinLock, levelOrHandle, irql);
      ntStatus = STATUS_SUCCESS;

      // Check if we still have data left to satisfy the WOM IRP.
      // Completing the WOM IRP is a way to req another RIRP from upper layer
      QCRD_ScanForWaitMask(pDevExt, FALSE, 1);
   }
   else
   {
      QcReleaseSpinLockWithLevel(&pDevExt->ReadSpinLock, levelOrHandle, irql);

      // Enqueue the Irp with IOBlock
      ntStatus = StartTheReadGoing(pDevExt, pIrp, 1);
   }

Exit:
   if ((ntStatus != STATUS_PENDING) && (ntStatus != STATUS_TIMEOUT))
   {
      pIrp->IoStatus.Status = ntStatus;
      if (ntStatus == STATUS_SUCCESS)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_RIRP,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> RIRP (Cs %ldB) 0x%p <%ld/%ld/%ld>\n", pDevExt->PortName,
             pIrp->IoStatus.Information, pIrp,
             pDevExt->Sts.lRmlCount[1], pDevExt->Sts.lAllocatedReads,
             pDevExt->Sts.lAllocatedRdMem)
         );
      }
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, 1);
      IoCompleteRequest(pIrp, IO_NO_INCREMENT);
   }

   return ntStatus;
}  // QCRD_Read
 
// called by read thread within ReadQueMutex because it manipulates pCallingIrp
NTSTATUS ReadIrpCompletion
(
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock,
   BOOLEAN                   AllowSpinlock,
   UCHAR                     cookie
)
{
   PIRP pIrp;
   NTSTATUS ntStatus = pIOBlock->ntStatus;
   PDEVICE_EXTENSION pDevExt = pIOBlock->pSerialDeviceObject->DeviceExtension;
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlockIrp, pNextIOB, pFirstIOBIrp;
   BOOLEAN bCompleteCallingIrp = TRUE;

   if (ntStatus == STATUS_SUCCESS)
   {
      if (pIOBlock->ulActiveBytes > 0)
      {
         QCRD_AdjustPaddingBytes(pIOBlock);
         pDevExt->RdErrorCount = 0; // reset the count
         // Put the characters in the read buffer, if there's room
         vPutToReadBuffer(pDevExt, pIOBlock->pBufferFromDevice, pIOBlock->ulActiveBytes);
      }
   }
   else if ((ntStatus == STATUS_DEVICE_DATA_ERROR) ||
            (ntStatus == STATUS_DEVICE_NOT_READY)  ||
            (ntStatus == STATUS_UNSUCCESSFUL))
   {
      pDevExt->RdErrorCount++;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> RIC<%d>: STATUS_DEVICE_DATA_ERROR %d\n", pDevExt->PortName, cookie, pDevExt->RdErrorCount)
      );

      // after some magic number of times of failure,
      // we mark the device as 'removed'
      if ((pDevExt->RdErrorCount > pDevExt->NumOfRetriesOnError) && (pDevExt->ContinueOnDataError == FALSE))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> RIC<%d>: err %d - dev removed\n\n", pDevExt->PortName, cookie, pDevExt->RdErrorCount)
         );
         clearDevState(DEVICE_STATE_DEVICE_STARTED); 
         pDevExt->bDeviceRemoved = TRUE;
         pDevExt->RdErrorCount = pDevExt->NumOfRetriesOnError;
         QCSER_PostRemovalNotification(pDevExt);
      }
   }
   else
   {
      QCSER_DbgPrint2
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> RIC<%d>: err nts 0x%x\n\n", pDevExt->PortName, cookie, ntStatus)
      );
   }

   pFirstIOBIrp = FindReadIrp(pDevExt, NULL);
   if (pIOBlock->pCallingIrp == NULL)
   {
      // If there's a read pending, complete it if possible
      pIOBlockIrp = pFirstIOBIrp; // FindReadIrp(pDevExt, NULL);
   }
   else
   {
      pIOBlockIrp = pIOBlock;
   }
   pDevExt->pReadCurrent = NULL;  //  IMPORTANT after calling FindReadIrp so that
                                  //  pNextIOB can get correct result

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_RIRP,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> RIC<%d> RIRP=0x%p IOB=0x%p (%ldB) st 0x%x\n",
        pDevExt->PortName, cookie, ((pIOBlockIrp != NULL) ? pIOBlockIrp->pCallingIrp : 0),
        pIOBlock, pIOBlock->ulActiveBytes, ntStatus
      )
   );
   pIOBlock->ulActiveBytes = 0;

   if (pIOBlockIrp)
   {
      pIrp = pIOBlockIrp->pCallingIrp;
      if ((ntStatus == STATUS_SUCCESS) ||
          ((ntStatus == STATUS_TIMEOUT) && (pIOBlock == pFirstIOBIrp)))
      {
         bCompleteCallingIrp = bFilledReadIrpFromBuffer(pDevExt, pIrp, pIOBlockIrp);
         QCSER_DbgPrint2
         (
            QCSER_DBG_MASK_RIRP,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> RIC<%d> - Filled RIRP 0x%p/0x%p - %d (nts 0x%x)\n", pDevExt->PortName, cookie,
              pIrp, pIOBlockIrp, bCompleteCallingIrp, pIOBlockIrp->ntStatus)
         );
      }
      if (pIrp->Cancel)
      {
         bCompleteCallingIrp = TRUE;
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_RIRP,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> RIC<%d> RIRP 0x%p C: in callellation!!\n", pDevExt->PortName, cookie, pIrp)
         );
      }
      if (bCompleteCallingIrp)
      {
         if (IoSetCancelRoutine(pIrp, NULL) == NULL)
         {
            // the IRP is in Cancellation
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_RIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> RIC<%d> RIRP 0x%p C: 0 CxlRtn<%d>\n", pDevExt->PortName, cookie, pIrp, cookie)
            );
            // SR392475
            AbortReadTimeout(pIOBlockIrp);
            pIOBlockIrp->TimerExpired = TRUE;
            // End of SR392475

            // Check if we still have data left to satisfy the WOM IRP. Completing
            // the WOM IRP is a way to req another RIRP from upper layer
            if (pDevExt->pReadHead == NULL)
            {
               QCRD_ScanForWaitMask(pDevExt, FALSE, 2);
            }

            return ntStatus;
         }

         if (pIOBlockIrp->TimerExpired == FALSE)
         {
            AbortReadTimeout(pIOBlockIrp);
         }
         else
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_RIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> RIC<%d> - TO-x RIRP (0x%x) 0x%p/0x%p\n", pDevExt->PortName,
                 cookie, ntStatus, pIrp, pIOBlockIrp)
            );
         }
         pIOBlockIrp->pCallingIrp = NULL;
         pIrp->IoStatus.Status = ntStatus;

         if (ntStatus == STATUS_CANCELLED)
         {
            pIrp->IoStatus.Information = 0;
         }

         QCSER_DbgPrint2
         (
            QCSER_DBG_MASK_RIRP,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> RIC<%d> - QtoC RIRP (0x%x) 0x%p\n", pDevExt->PortName,
              cookie, ntStatus, pIrp)
         );
         InsertTailList(&pDevExt->RdCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
         InterlockedIncrement(&(pDevExt->NumIrpsToComplete));
         pNextIOB = FindReadIrp(pDevExt, NULL);

         if (pDevExt->Sts.lRmlCount[QCUSB_IRP_TYPE_RIRP] <= QCSER_RD_COMP_THROTTLE_START)
         {
            pDevExt->CompletionThrottle = QCSER_RD_COMPLETION_THROTTLE_MIN;
         }
         else if (pDevExt->Sts.lRmlCount[QCUSB_IRP_TYPE_RIRP] > QCSER_RD_COMP_THROTTLE_START*2)
         {
            pDevExt->CompletionThrottle = pDevExt->Sts.lRmlCount[QCUSB_IRP_TYPE_RIRP] /2;
         }
         else
         {
            pDevExt->CompletionThrottle = pDevExt->Sts.lRmlCount[QCUSB_IRP_TYPE_RIRP] -
                                          QCSER_RD_COMP_THROTTLE_START;
            if (pDevExt->CompletionThrottle < QCSER_RD_COMPLETION_THROTTLE_MIN)
            {
               pDevExt->CompletionThrottle = QCSER_RD_COMPLETION_THROTTLE_MIN;
            }
         }
         if ((pNextIOB == NULL) || (CountL1ReadQueue(pDevExt) == 0) ||
             (pDevExt->NumIrpsToComplete >= pDevExt->CompletionThrottle))
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_RIRP,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> RIC<%d> signal to completion Q (%d)\n", pDevExt->PortName, cookie, pDevExt->NumIrpsToComplete)
            );
 
            KeSetEvent(&pDevExt->InterruptEmptyRdQueueEvent, IO_NO_INCREMENT, FALSE);
         }

         // Check if we still have data left to satisfy the WOM IRP. Completing
         // the WOM IRP would trigger another RIRP sent to our driver.
         if (pDevExt->pReadHead == NULL)
         {
            // Check if we still have data left to satisfy the WOM IRP. Completing
            // the WOM IRP is a way to req another RIRP from upper layer
            QCRD_ScanForWaitMask(pDevExt, FALSE, 3);
         }
      }
   }

   // Let the read thread take care of freeing pIOBlock

   return ntStatus;
}  // ReadIrpCompletion

BOOLEAN StartReadIntervalTimeout(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock, char cookie)
{
   PDEVICE_EXTENSION pDevExt = pIOBlock->pSerialDeviceObject->DeviceExtension;
   PSERIAL_TIMEOUTS pSt = pDevExt->pSerialTimeouts;
   BOOLEAN inQueue;
   LARGE_INTEGER dueTime;
   PIRP pIrp = pIOBlock->pCallingIrp;
   NTSTATUS ntStatus;

   ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, pIOBlock);
   if (!NT_SUCCESS(ntStatus))
   {
      return FALSE;
   }
   QcInterlockedIncrement(3, pIOBlock, 8);

   if ((pIOBlock->ulReadIntervalTimeout == MAXULONG) ||
       (pIOBlock->ulReadIntervalTimeout == 0))
   {
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3);
      return FALSE;
   }

   // cancel existing timer if any
   // if (pIOBlock->TimeoutTimer.DueTime.LowPart)
   // {
   //    KeCancelTimer(&pIOBlock->TimeoutTimer);
   //    RtlZeroMemory(&pIOBlock->TimeoutTimer,sizeof(KTIMER));
   // }

   // we need to release remove lock for the existing timer
   // since the existing timer will be cancelled by
   // KeSetTimer()
   // Or, we still need to release the remove lock if the timer
   // already expired (which shouldn't happen here
   if (pIOBlock->bRItimerInitiated == TRUE)
   {
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3);
   }

   if (pIOBlock->TimerExpired == TRUE)  // shouldn't happen here
   {
      // this enable bFilledReadIrpFromBuffer() to return bFilledSome
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
          ("<%s> RI: error 11\n", pDevExt->PortName)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3);
      return FALSE;
   }

   pIOBlock->ulTimeout = pIOBlock->ulReadIntervalTimeout;
   IoSetCancelRoutine(pIrp, CancelReadRoutine);
   _IoMarkIrpPending(pIrp);

   if (pIOBlock->bRItimerInitiated == FALSE)
   {
      KeInitializeTimer(&pIOBlock->TimeoutTimer);
      pIOBlock->bRItimerInitiated = TRUE;
   }

   dueTime.QuadPart = (LONGLONG)(-10000) * (LONGLONG)pIOBlock->ulTimeout;

   // launch the timer
   inQueue = KeSetTimer(&pIOBlock->TimeoutTimer,dueTime,&pIOBlock->TimeoutDpc);

   ASSERT(inQueue == FALSE);  // assert timer not already in system queue

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_RIRP,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> StartRITimeout<%d>: 0x%p/0x%p %ldms %ldB\n", pDevExt->PortName, cookie,
       pIrp, pIOBlock, pIOBlock->ulReadIntervalTimeout, pIrp->IoStatus.Information)
   );

   return TRUE; // a valid timeout
} // StartReadIntervalTimeout


BOOLEAN StartReadTimeout(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock)
{
   PDEVICE_EXTENSION pDevExt = pIOBlock->pSerialDeviceObject->DeviceExtension;
   PSERIAL_TIMEOUTS pSt = pDevExt->pSerialTimeouts;
   BOOLEAN inQueue;
   LARGE_INTEGER dueTime;
   PIRP pIrp = pIOBlock->pCallingIrp;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation (pIrp);
   ULONG ulCharsNeeded = irpStack->Parameters.Read.Length - pIrp->IoStatus.Information;
   NTSTATUS ntStatus;

   ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, pIOBlock);
   if (!NT_SUCCESS(ntStatus))
   {
      return FALSE;
   }
   QcInterlockedIncrement(3, pIOBlock, 9);

   switch (pDevExt->ReadTimeout.ucTimeoutType)
   {
      case QCSER_READ_TIMEOUT_UNDEF:
      case QCSER_READ_TIMEOUT_CASE_1:
      case QCSER_READ_TIMEOUT_CASE_2:  // no timeout
      case QCSER_READ_TIMEOUT_CASE_7:  // no timeout
         IoSetCancelRoutine(pIrp, CancelReadRoutine);
         _IoMarkIrpPending(pIrp);
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3);
         return TRUE;
      case QCSER_READ_TIMEOUT_CASE_3:  // total timeout
      case QCSER_READ_TIMEOUT_CASE_8:  // total timeout
         pIOBlock->ulTimeout = pSt->ReadTotalTimeoutConstant +
            pSt->ReadTotalTimeoutMultiplier * ulCharsNeeded;
         break;
      case QCSER_READ_TIMEOUT_CASE_4:  // rtn immediately
      case QCSER_READ_TIMEOUT_CASE_6:  // rtn immediately
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3);
         return FALSE;
      case QCSER_READ_TIMEOUT_CASE_5:  // special handling
         // a) rtn immediately if there're any chars in the buffer
         if (pIOBlock->pCallingIrp->IoStatus.Information > 0)
         {
            QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3);
            return FALSE;
         }
         // b) wait until a char arrives and return immediately or
         //    timeout after ReadTotalTimeoutConstant time
          else
         {
            pIOBlock->bReturnOnChars = TRUE;
            pIOBlock->ulTimeout = pSt->ReadTotalTimeoutConstant;
         }
         break;
      case QCSER_READ_TIMEOUT_CASE_9:
      case QCSER_READ_TIMEOUT_CASE_10:  // RI timeout
         pIOBlock->ulReadIntervalTimeout = pSt->ReadIntervalTimeout;
         pIOBlock->bRItimerInitiated = FALSE;

         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3);
         if ((pIrp->IoStatus.Information > 0) && // filled some at the beginning
             (pIOBlock->ulReadIntervalTimeout > 0))
         {
            // Start timer if insufficient chars received
            return StartReadIntervalTimeout(pIOBlock, 1);
         }

         // set the cancel routine and mark pending
         IoSetCancelRoutine(pIrp, CancelReadRoutine);
         _IoMarkIrpPending(pIrp);

         // we don't fire up timer until any data is received
         return TRUE;
      case QCSER_READ_TIMEOUT_CASE_11:
         if (pIrp->IoStatus.Information > 0) // filled some at the beginning
         {
            pIOBlock->ulTimeout = pSt->ReadIntervalTimeout;
         }
         else
         {
            pIOBlock->ulTimeout = pSt->ReadTotalTimeoutConstant +
               pSt->ReadTotalTimeoutMultiplier * ulCharsNeeded;
         }
         break;
      default:  // no timeout
         IoSetCancelRoutine(pIrp, CancelReadRoutine);
         _IoMarkIrpPending(pIrp);
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3);
         return TRUE;
   }  // switch (pDevExt->ReadTimeout.ucTimeoutType)

StartTimer:

   // set the cancel routine and mark pending
   IoSetCancelRoutine(pIrp, CancelReadRoutine);
   _IoMarkIrpPending(pIrp);

   if (pIOBlock->ulTimeout == 0)
   {
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3);
      return TRUE; // a "timeout" of forever
   }

   KeInitializeTimer(&pIOBlock->TimeoutTimer);

   dueTime.QuadPart = (LONGLONG)(-10000) * (LONGLONG)pIOBlock->ulTimeout;

   // OK, launch the timer
   inQueue = KeSetTimer(&pIOBlock->TimeoutTimer,dueTime,&pIOBlock->TimeoutDpc);

   if (pDevExt->ReadTimeout.ucTimeoutType == QCSER_READ_TIMEOUT_CASE_11)
   {
      // RI may be used only if it's smaller than RT
      // if (pSt->ReadIntervalTimeout <= pIOBlock->ulTimeout)
      {
         pIOBlock->bRItimerInitiated = TRUE;
         pIOBlock->ulReadIntervalTimeout = pSt->ReadIntervalTimeout;
      }
   }

   ASSERT(inQueue == FALSE);  // assert timer not already in system queue
   return TRUE; // a valid timeout
}  // StartReadTimeout

VOID AbortReadTimeout(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock)
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
}  // AbortReadTimeout

VOID ReadTimeoutDpc
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
      DbgPrint("<%s> ReadTimeoutDpc: Error - purged IOB 0x%p\n",
                gDeviceName, pIOBlock);
      #endif
      return;
   }
   pDevExt = pIOBlock->pSerialDeviceObject->DeviceExtension;

   QcAcquireSpinLockAtDpcLevel(&pDevExt->ReadSpinLock, &levelOrHandle);
   if (KeReadStateTimer(&pIOBlock->TimeoutTimer) == FALSE)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> TODPC-Timer reset, abort.\n", pDevExt->PortName)
      );
      QcReleaseSpinLockFromDpcLevel(&pDevExt->ReadSpinLock, levelOrHandle);
      return;
   }

   RtlZeroMemory(&pIOBlock->TimeoutTimer,sizeof(KTIMER)); // tell everyone timer's gone

   QCSER_DbgPrint2
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> TODPC - 0x%p/0x%p\n", pDevExt->PortName, pIOBlock->pCallingIrp,
        pIOBlock)
   );

   // we move this lock removal to AbortTimeout called by TimeoutReadRoutine
// QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3);
   if (pIOBlock->pCallingIrp)
   {
      pIOBlock->TimerExpired = TRUE;
      TimeoutReadRoutine(pIOBlock);
   }
   else
   {
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock, 3); // shouldn't happen
   }

   QcReleaseSpinLockFromDpcLevel(&pDevExt->ReadSpinLock, levelOrHandle);
}  // ReadTimeoutDpc

VOID vResetReadBuffer(PDEVICE_EXTENSION pDevExt, UCHAR cookie)
{
   if (pDevExt->pucReadBufferGet != pDevExt->pucReadBufferPut)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_INFO,
         ("<%s> RB RESET-%d\n", pDevExt->PortName, cookie)
      );
   }
   pDevExt->bReadBufferReset = TRUE;
   pDevExt->pucReadBufferGet = pDevExt->pucReadBufferStart;
   pDevExt->pucReadBufferPut = pDevExt->pucReadBufferStart;
}

ULONG CountL1ReadQueue(PDEVICE_EXTENSION pDevExt)
{
   ULONG i;
   LONG lCount= pDevExt->pucReadBufferPut - pDevExt->pucReadBufferGet;
   if (lCount < 0) // wrap
   {
      lCount += pDevExt->lReadBufferSize;
   }

   return (ULONG)lCount;
}

ULONG CountReadQueue(PDEVICE_EXTENSION pDevExt)
{
   ULONG i;
   LONG lCount= pDevExt->pucReadBufferPut - pDevExt->pucReadBufferGet;
   if (lCount < 0) // wrap
   {
      lCount += pDevExt->lReadBufferSize;
   }

   for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
   {
      if (pDevExt->pL2ReadBuffer[i].bFilled == TRUE)
      {
         lCount += pDevExt->pL2ReadBuffer[i].Length;
      }
   }

   return (ULONG)lCount;
}

BOOLEAN bFilledReadIrpFromBuffer
(
   PDEVICE_EXTENSION pDevExt,
   PIRP irp,
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock
)
{
   PUCHAR pucTo, pucEnd;
   LONG lCharsAvailable, lCharsRequested, lCharsNeeded;
   BOOLEAN bResult = FALSE;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation (irp);
   BOOLEAN bFilledSome = FALSE;
   
   if (irp->MdlAddress) // this IRP is direct I/O
   {
      lCharsRequested = MmGetMdlByteCount(irp->MdlAddress);
      pucTo = (PUCHAR)MmGetSystemAddressForMdlSafe(irp->MdlAddress, HighPagePriority);
      if (!pucTo)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> ERROR: Mdl translation 0x%p\n", pDevExt->PortName, irp->MdlAddress)
         );
        bResult = FALSE;
        goto BFILLED_READ_EXIT;
      }
   }
   else // this IRP is buffered I/O
   {
      lCharsRequested = irpStack->Parameters.Read.Length;
      pucTo = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
   }
   pucTo += irp->IoStatus.Information;

   lCharsAvailable = CountL1ReadQueue(pDevExt);
   lCharsNeeded = lCharsRequested - irp->IoStatus.Information;
   if ((lCharsRequested == 1) && isprint(pDevExt->pucReadBufferGet[0]))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_RIRP,
         QCSER_DBG_LEVEL_INFO,
         ("<%s> FILL- 0x%p Rst=0x%x Av=%ld Req=%ld ask=%ld To=0x%p(0x%x,%c)\n",
           pDevExt->PortName, irp, pDevExt->bReadBufferReset, lCharsAvailable,
           lCharsRequested, lCharsNeeded, pucTo, 
           pDevExt->pucReadBufferGet[0], pDevExt->pucReadBufferGet[0])
      );
   }
   else
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_RIRP,
         QCSER_DBG_LEVEL_INFO,
         ("<%s> FILL- 0x%p Rst=0x%x Av=%ld Req=%ld ask=%ld To=0x%p (0x%x)\n",
           pDevExt->PortName, irp, pDevExt->bReadBufferReset, lCharsAvailable,
           lCharsRequested, lCharsNeeded, pucTo, pDevExt->pucReadBufferGet[0])
      );
   }

   if (lCharsNeeded <= 0)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> RIRP req 0 - 0x%p\n", pDevExt->PortName, irp)
      );
      bFilledSome = bResult = TRUE;
      goto BFILLED_READ_EXIT;
   }
   if (lCharsAvailable <= 0)
   {
      bFilledSome = bResult = FALSE;
      goto BFILLED_READ_EXIT;
   }
   if (lCharsAvailable >= lCharsNeeded)
   {
      lCharsAvailable = lCharsNeeded;
      bResult = TRUE;
   }
   if (lCharsAvailable)
   {
      // copy data from buffer to read irp
      // first, is there a partial copy before a wrap?
      if (pDevExt->pucReadBufferStart + pDevExt->lReadBufferSize // is end of buffer...
         <= pDevExt->pucReadBufferGet + lCharsAvailable) // .. before end of read?
      {
         lCharsNeeded = pDevExt->pucReadBufferStart + pDevExt->lReadBufferSize
            - pDevExt->pucReadBufferGet;
         RtlCopyMemory(pucTo, pDevExt->pucReadBufferGet, lCharsNeeded);
         bFilledSome = TRUE;
         lCharsAvailable -= lCharsNeeded;
         pucTo += lCharsNeeded;
         pDevExt->pucReadBufferGet = pDevExt->pucReadBufferStart;
         irp->IoStatus.Information += lCharsNeeded;
      }
      if (lCharsAvailable)
      {         
         RtlCopyMemory(pucTo, pDevExt->pucReadBufferGet, lCharsAvailable);
         bFilledSome = TRUE;
         pDevExt->pucReadBufferGet += lCharsAvailable;
         irp->IoStatus.Information += lCharsAvailable;
      }

      // HGUO090501
      if (pDevExt->pucReadBufferGet == pDevExt->pucReadBufferPut)
      {
         vResetReadBuffer(pDevExt, 5);
      }
   } // if (lCharsAvailable)
BFILLED_READ_EXIT:

   if (pIOBlock != NULL)
   {
      if (pIOBlock->TimerExpired == TRUE)
      {
         QCSER_DbgPrint2
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_FORCE,
            ("<%s> bFil: TO - true\n", pDevExt->PortName)
         );
         return TRUE;
      }
      // may need to start/re-start the read-interval timer
      if ((irp->IoStatus.Information > 0) && (irp->IoStatus.Information < lCharsRequested))
      {
         if (pIOBlock->ulReadIntervalTimeout > 0)
         {
            if (StartReadIntervalTimeout(pIOBlock, 2) == FALSE)
            {
               return bFilledSome;
            }

            return bResult;
         }
      }
      return (pIOBlock->bReturnOnChars == TRUE) ? bFilledSome : bResult;
   }
   // The first fill happens only to the new read IRP and the valid assumption
   // is the IoStatus.Information is zero before the IRP is filled
   else if (irp->IoStatus.Information == lCharsAvailable)
   {
      // if it's the first fill and the read interval is in place
      if (pDevExt->ReadTimeout.bUseReadInterval == TRUE)
      {
         return bResult;
      }
   }

   return (pDevExt->ReadTimeout.bReturnOnAnyChars == TRUE) ? bFilledSome : bResult;
}  // bFilledReadIrpFromBuffer

VOID vPutToReadBuffer
(
   PDEVICE_EXTENSION pDevExt,
   PUCHAR pucFrom,
   ULONG ulCount
)
{
   UCHAR ucXoff = 0;
   UCHAR ucXon = 0;
   UCHAR ucFlag = 0;
   UCHAR ucChar;
   PUCHAR pucToDummy, pucTo = pDevExt->pucReadBufferPut;
   PUCHAR pucEnd = pDevExt->pucReadBufferStart + pDevExt->lReadBufferSize;
   CHAR cFlowState = 0;
   ULONG ulXferred = 0;
   USHORT usNewUartState = 0;
   BOOLEAN bBufferOverrun = FALSE;
   BOOLEAN bStripNulls = ((pDevExt->pSerialHandflow->FlowReplace & SERIAL_NULL_STRIPPING));
   LONG lCountInBuffer = CountL1ReadQueue(pDevExt);

   ULONG leg1, leg2;
   LONG  leg2Len;
   int   i;
   PCHAR pucSrcPtr;

   pucSrcPtr = pucFrom;

   /**** Case 1:
   ----------------------------------------
           Get              Put
   ----------------------------------------
   | leg2   |                |    leg1    |

    **** Case 2:
   ----------------------------------------
           Put              Get
   ----------------------------------------
            |    Leg1        |
                                          */

   // fresh start, no way to overrun in this case
   // if (ulCount == 0)  // we already checked, not necessary here
   // {
   //    return;
   // }

   // Buffer full, drop the data on the floor
   if (lCountInBuffer > pDevExt->lReadBufferHigh)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> RD buf full, discard data (%ld,%ld)-(0x%p,%ld,0x%p)-(0x%p,0x%p)\n",
               pDevExt->PortName, lCountInBuffer, pDevExt->lReadBufferHigh,
               pDevExt->pucReadBufferStart, pDevExt->lReadBufferSize, pucEnd,
               pDevExt->pucReadBufferPut, pDevExt->pucReadBufferGet
         )
      );
      pDevExt->pSerialStatus->Errors |= SERIAL_ERROR_QUEUEOVERRUN;
      return;
   }

   if ((pDevExt->bReadBufferReset == TRUE) && (ulCount > 0))
   {
      if (ulCount > pDevExt->lReadBufferSize)
      {
         bBufferOverrun = TRUE;
      }
      else
      {
         pDevExt->bReadBufferReset = FALSE;
         RtlCopyMemory(pDevExt->pucReadBufferPut, pucFrom, ulCount);
         pDevExt->pucReadBufferPut += ulCount;
         ulXferred += ulCount;
      }
   }
   // Case 1:
   else if (pDevExt->pucReadBufferPut > pDevExt->pucReadBufferGet)
   {
      leg1 = pucEnd - pDevExt->pucReadBufferPut;
      leg2 = pDevExt->pucReadBufferGet - pDevExt->pucReadBufferStart;
      if (leg1 >= ulCount)  // copy to buffer
      {
         RtlCopyMemory(pDevExt->pucReadBufferPut, pucFrom, ulCount);
         pDevExt->pucReadBufferPut += ulCount;
         if (pDevExt->pucReadBufferPut >= pucEnd)
         {
            // Wrap around
            pDevExt->pucReadBufferPut = pDevExt->pucReadBufferStart;
         }
         ulXferred += ulCount;
      }
      else
      {
         // Case 1.2
         leg2Len = ulCount - leg1;
         RtlCopyMemory(pDevExt->pucReadBufferPut, pucFrom, leg1);
         pucFrom += leg1;
         RtlCopyMemory(pDevExt->pucReadBufferStart, pucFrom, leg2Len);
         pDevExt->pucReadBufferPut = pDevExt->pucReadBufferStart + leg2Len;
         ulXferred += ulCount;
         if (pDevExt->pucReadBufferGet <= pDevExt->pucReadBufferPut)
         {
            bBufferOverrun = TRUE;
            pDevExt->pPerfstats->BufferOverrunErrorCount +=
               (pDevExt->pucReadBufferPut - pDevExt->pucReadBufferGet + 1);
         }
      }
   }
   else if (pDevExt->pucReadBufferPut < pDevExt->pucReadBufferGet)
   {
      // Case 2
      leg1 = pDevExt->pucReadBufferGet - pDevExt->pucReadBufferPut;
      if (leg1 >= ulCount)  // copy to buffer
      {
         RtlCopyMemory(pDevExt->pucReadBufferPut, pucFrom, ulCount);
         pDevExt->pucReadBufferPut += ulCount;
         ulXferred += ulCount;
      }
      else
      {
         bBufferOverrun = TRUE;
         pDevExt->pPerfstats->BufferOverrunErrorCount += (ulCount - leg1);
      }
   }
   else  // buffer full
   {
      bBufferOverrun = TRUE;
      // Case 3
      pDevExt->pPerfstats->BufferOverrunErrorCount += ulCount;
   }

   /***************** Post processing ***************/
   if (pDevExt->ulWaitMask & SERIAL_EV_RXFLAG)
   {
      if ((ucFlag = pDevExt->pSerialChars->EventChar))
      {
         for (i = 0; i < ulCount; i++)
         {
            if (ucFlag == pucSrcPtr[i])
            {
               usNewUartState |= SERIAL_EV_RXFLAG;
               break;
            }
         }
      }
   }

   if (bBufferOverrun)
   {
      pDevExt->pSerialStatus->Errors |= SERIAL_ERROR_QUEUEOVERRUN;
      if (pDevExt->pSerialHandflow->FlowReplace & SERIAL_ERROR_CHAR)
      {
         if (pDevExt->pucReadBufferPut == pDevExt->pucReadBufferStart)
         {
            pDevExt->pucReadBufferPut = pucEnd;
         }
         pDevExt->pucReadBufferPut--;
         *pDevExt->pucReadBufferPut = pDevExt->pSerialChars->ErrorChar;
      }
      if (pDevExt->pSerialHandflow->FlowReplace & SERIAL_ERROR_ABORT)
      {
         _int3; // error
      }
   } // bBufferOverrun

   pDevExt->pPerfstats->ReceivedCount += ulCount;

   // if (lCountInBuffer == 0 && ulXferred > 0)
   if (ulXferred > 0)
   {
      usNewUartState |= SERIAL_EV_RXCHAR;
   }

   if (lCountInBuffer + ulXferred > pDevExt->lReadBuffer80pct)
   {
      usNewUartState |= SERIAL_EV_RX80FULL;
   }
   if (lCountInBuffer + ulXferred > pDevExt->lReadBufferHigh)
   {
//    usNewUartState |= SERIAL_EV_PERR;
   }

   if (usNewUartState)
   {
      ProcessNewUartState(pDevExt, usNewUartState, usNewUartState, FALSE);
   }

}  // vPutToReadBuffer()

NTSTATUS QCRD_InitializeL2Buffers(PDEVICE_EXTENSION pDevExt)
{
   int i;

   // Initialize L2 buffer
   pDevExt->L2IrpIdx = pDevExt->L2FillIdx = 0;

   if (pDevExt->bFdoReused == TRUE)
   {
      return STATUS_SUCCESS;
   }

   #ifdef QCUSB_MULTI_READS
   InitializeListHead(&pDevExt->L2CompletionQueue);
   KeInitializeSpinLock(&pDevExt->L2Lock);
   #endif // QCUSB_MULTI_READS

   for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
   {
      pDevExt->pL2ReadBuffer[i].bFilled = FALSE;
      pDevExt->pL2ReadBuffer[i].Length  = 0;
      pDevExt->pL2ReadBuffer[i].Status  = STATUS_PENDING;

      if (pDevExt->pL2ReadBuffer[i].Buffer == NULL)
      {
         pDevExt->pL2ReadBuffer[i].Buffer = _ExAllocatePool
                                            (
                                               NonPagedPool,
                                               pDevExt->MaxPipeXferSize,
                                               "pL2ReadBuffer"
                                            );
         if (pDevExt->pL2ReadBuffer[i].Buffer == NULL)
         {
            pDevExt->NumberOfL2Buffers = i;

            if (i == 0)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_CRITICAL,
                  ("<%s> InitL2: ERR - NO_MEMORY\n", pDevExt->PortName)
               );
               return STATUS_NO_MEMORY;
            }
            else
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_CRITICAL,
                  ("<%s> InitL2: WARNING - degraded to %d\n", pDevExt->PortName, i)
               );

               return STATUS_SUCCESS;
            }
         }
         #ifdef QCUSB_MULTI_READS
         else
         {
            pDevExt->pL2ReadBuffer[i].Irp = IoAllocateIrp((CCHAR)(pDevExt->MyDeviceObject->StackSize), FALSE);
            if (pDevExt->pL2ReadBuffer[i].Irp == NULL)
            {
               ExFreePool(pDevExt->pL2ReadBuffer[i].Buffer);
               pDevExt->pL2ReadBuffer[i].Buffer = NULL;
               pDevExt->NumberOfL2Buffers = i;

               if (i == 0)
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_CRITICAL,
                     ("<%s> InitL2: ERR2 - NO_MEMORY\n", pDevExt->PortName)
                  );
                  return STATUS_NO_MEMORY;
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_CRITICAL,
                     ("<%s> InitL2: WAR2 - degraded to %d\n", pDevExt->PortName, i)
                  );

                  return STATUS_SUCCESS;
               }
            }
            else
            {
               pDevExt->pL2ReadBuffer[i].Index = i;
               pDevExt->pL2ReadBuffer[i].State = L2BUF_STATE_READY;
               KeInitializeEvent
               (
                  &(pDevExt->pL2ReadBuffer[i].CompletionEvt),
                  NotificationEvent, FALSE
               );
            }
         }
         #endif // QCUSB_MULTI_READS

      }
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> InitL2: num buf %d\n", pDevExt->PortName, pDevExt->NumberOfL2Buffers)
   );
   return STATUS_SUCCESS;
}

VOID QCRD_ProcessPreTimeoutIOB(PDEVICE_EXTENSION pDevExt)
{
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock, preIOB;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);

   pIOBlock = preIOB = pDevExt->pReadHead;
   while (pIOBlock != NULL)
   {
      if (pIOBlock->TimerExpired == TRUE)
      {
         if (pIOBlock == pDevExt->pReadHead)
         {
            pDevExt->pReadHead = pIOBlock->pNextEntry;
            pIOBlock->ntStatus = STATUS_TIMEOUT;
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_RIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> RIRP 0x%p preQto-0\n", pDevExt->PortName, pIOBlock->pCallingIrp)
            );
            pIOBlock->pCompletionRoutine(pIOBlock, FALSE, 12);
            QcExFreeReadIOB(pIOBlock, FALSE);
            pIOBlock = preIOB = pDevExt->pReadHead;
         }
         else
         {
            preIOB->pNextEntry = pIOBlock->pNextEntry;
            pIOBlock->ntStatus = STATUS_TIMEOUT;
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_RIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> RIRP 0x%p preQto-1\n", pDevExt->PortName, pIOBlock->pCallingIrp)
            );
            pIOBlock->pCompletionRoutine(pIOBlock, FALSE, 16);
            QcExFreeReadIOB(pIOBlock, FALSE);
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

   QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
}  // QCRD_ProcessPreTimeoutIOB

NTSTATUS QCRD_StartReadThread(PDEVICE_EXTENSION pDevExt)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   OBJECT_ATTRIBUTES objAttr;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);

   if ((pDevExt->hL1ReadThreadHandle == NULL) &&
       (pDevExt->pL1ReadThread == NULL))
   {
      // Make sure the read thread is created when IRQL==PASSIVE_LEVEL
      if (irql > PASSIVE_LEVEL)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> _R: wrong IRQL::%d\n", pDevExt->PortName, KeGetCurrentIrql())
         );
         QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
         return STATUS_UNSUCCESSFUL;
      }
      KeClearEvent(&pDevExt->ReadThreadStartedEvent);

      if (pDevExt->bRdThreadInCreation == FALSE)
      {
         pDevExt->bRdThreadInCreation = TRUE;
         QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
      }
      else
      {
         QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
         return STATUS_SUCCESS;
      }

      InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
      ucHdlCnt++;

      #ifdef QCUSB_MULTI_READS
      if (pDevExt->UseReadArray == TRUE)
      {
         ntStatus = PsCreateSystemThread
                    (
                       OUT &pDevExt->hL1ReadThreadHandle,
                       IN THREAD_ALL_ACCESS,
                       IN &objAttr, // POBJECT_ATTRIBUTES
                       IN NULL,     // HANDLE  ProcessHandle
                       OUT NULL,    // PCLIENT_ID  ClientId
                       IN (PKSTART_ROUTINE)QCMRD_L1MultiReadThread,
                       IN (PVOID) pDevExt
                    );
      }
      else
      #endif // QCUSB_MULTI_READS
      {
         ntStatus = PsCreateSystemThread
                    (
                       OUT &pDevExt->hL1ReadThreadHandle,
                       IN THREAD_ALL_ACCESS,
                       IN &objAttr, // POBJECT_ATTRIBUTES
                       IN NULL,     // HANDLE  ProcessHandle
                       OUT NULL,    // PCLIENT_ID  ClientId
                       IN (PKSTART_ROUTINE)STESerial_ReadThread,
                       IN (PVOID) pDevExt
                    );
      }
  
      if ((ntStatus != STATUS_SUCCESS) || (pDevExt->hL1ReadThreadHandle == NULL))
      {
         pDevExt->pL1ReadThread = NULL;
         pDevExt->hL1ReadThreadHandle = NULL;
         pDevExt->bRdThreadInCreation = FALSE;
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> _Read: th failure 0x%x\n", pDevExt->PortName, ntStatus)
         );
         return STATUS_UNSUCCESSFUL;
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> L1 hdl 0x%x\n", pDevExt->PortName, pDevExt->hL1ReadThreadHandle)
         );
      }
      // wait for read thread to start up before que'ing and kicking
      ntStatus = KeWaitForSingleObject
                 (
                    &pDevExt->ReadThreadStartedEvent, 
                    Executive, 
                    KernelMode, 
                    FALSE, 
                    NULL
                 );

      if (pDevExt->bRdThreadInCreation == FALSE)
      {
         // thread failed to start
         pDevExt->pL1ReadThread = NULL;
         pDevExt->hL1ReadThreadHandle = NULL;
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> _Read: th failure 02\n", pDevExt->PortName)
         );
         return STATUS_UNSUCCESSFUL;
      }

      ntStatus = ObReferenceObjectByHandle
                 (
                    pDevExt->hL1ReadThreadHandle,
                    THREAD_ALL_ACCESS,
                    NULL,
                    KernelMode,
                    (PVOID*)&pDevExt->pL1ReadThread,
                    NULL
                 );
      if (!NT_SUCCESS(ntStatus))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> RD: ObReferenceObjectByHandle failed 0x%x\n", pDevExt->PortName, ntStatus)
         );
         pDevExt->pL1ReadThread = NULL;
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> L1 handle=0x%p thOb=0x%p\n", pDevExt->PortName,
              pDevExt->hL1ReadThreadHandle, pDevExt->pInterruptThread)
         );
         _closeHandle(pDevExt->hL1ReadThreadHandle, "R-2");
      }

      pDevExt->bRdThreadInCreation = FALSE;
   }
   else
   {
      QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> Rth alive-%d\n", pDevExt->PortName, KeGetCurrentIrql())
   );

   return ntStatus;
}  // QCRD_StartReadThread

NTSTATUS QCRD_L2Suspend(PDEVICE_EXTENSION pDevExt)
{
   LARGE_INTEGER delayValue;
   NTSTATUS nts = STATUS_UNSUCCESSFUL;

   if ((pDevExt->hL2ReadThreadHandle != NULL) ||
       (pDevExt->pL2ReadThread != NULL))
   {
      delayValue.QuadPart = -(50 * 1000 * 1000); // 5 seconds

      KeClearEvent(&pDevExt->L2ReadStopAckEvent);
      KeSetEvent(&pDevExt->L2ReadStopEvent, IO_NO_INCREMENT, FALSE);

      nts = KeWaitForSingleObject
            (
               &pDevExt->L2ReadStopAckEvent,
               Executive,
               KernelMode,
               FALSE,
               &delayValue
            );
      if (nts == STATUS_TIMEOUT)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> L2Suspend: err - WTO\n", pDevExt->PortName)
         );
         KeClearEvent(&pDevExt->L2ReadStopEvent);
      }
      KeClearEvent(&pDevExt->L2ReadStopAckEvent);
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> L2Suspend: 0x%x\n", pDevExt->PortName, nts)
   );

   return nts;
}  // QCRD_L2Suspend

NTSTATUS QCRD_L2Resume(PDEVICE_EXTENSION pDevExt)
{
   if ((pDevExt->hL2ReadThreadHandle != NULL) ||
       (pDevExt->pL2ReadThread != NULL))
   {
      KeSetEvent(&pDevExt->L2ReadResumeEvent, IO_NO_INCREMENT, FALSE);
   }

   return STATUS_SUCCESS;

}  // QCRD_L2Resume

VOID QCRD_ScanForWaitMask
(
   PDEVICE_EXTENSION pDevExt,
   BOOLEAN           SpinlockNeeded,
   UCHAR             Cookie
)
{
   PUCHAR bufEnd = pDevExt->pucReadBufferStart + pDevExt->lReadBufferSize;
   PUCHAR pucFrom;
   UCHAR  ucFlag = pDevExt->pSerialChars->EventChar;
   USHORT usNewUartState = 0, usCheckingState = (SERIAL_EV_RXCHAR | SERIAL_EV_RXFLAG);
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> -->RD_ScanForWaitMask: 0x%x/0x%x (%d)", pDevExt->PortName,
        pDevExt->ulWaitMask, ucFlag, Cookie)
   );

   if (SpinlockNeeded == TRUE)
   {
      QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);
   }

   pucFrom = pDevExt->pucReadBufferGet;

   if (pDevExt->bReadBufferReset == TRUE)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_TRACE,
         ("<%s> <-- RD_ScanForWaitMask - 1", pDevExt->PortName)
      );
      if (SpinlockNeeded == TRUE)
      {
         QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
      }
      return;
   }

   // If any char received
   if ((pDevExt->ulWaitMask & SERIAL_EV_RXCHAR) != 0)
   {
      usNewUartState |= SERIAL_EV_RXCHAR;
   }

   // Scan stream for event char
   if (((pDevExt->ulWaitMask & SERIAL_EV_RXFLAG) != 0) && (ucFlag != 0))
   {
      if (pDevExt->pucReadBufferPut > pDevExt->pucReadBufferGet)
      {
         while (pucFrom < pDevExt->pucReadBufferPut)
         {
            // scan the buffer
            if (*pucFrom++ == ucFlag)
            {
               usNewUartState |= SERIAL_EV_RXFLAG;
               break;
            }
         }
      }
      else // including buf full
      {
         while (pucFrom < bufEnd)
         {
            // scan first leg
            if (*pucFrom++ == ucFlag)
            {
               usNewUartState |= SERIAL_EV_RXFLAG;
               break;
            }
         }

         if ((usNewUartState & SERIAL_EV_RXFLAG) == 0)
         {
            pucFrom = pDevExt->pucReadBufferStart;
            while (pucFrom < pDevExt->pucReadBufferPut)
            {
               // scan second leg
               if (*pucFrom++ == ucFlag)
               {
                  usNewUartState |= SERIAL_EV_RXFLAG;
                  break;
               }
            }
         }
      }
   }
   else
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> RD_ScanForWaitMask: no event char to wait", pDevExt->PortName)
      );
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> RD_ScanForWaitMask: new UART 0x%x HeldForRD %u",
        pDevExt->PortName, usNewUartState, pDevExt->bWOMHeldForRead)
   );


   if (((usNewUartState & usCheckingState) != 0) &&
        (pDevExt->bWOMHeldForRead == FALSE))
   {
      ProcessNewUartState(pDevExt, usNewUartState, usNewUartState, FALSE);
      pDevExt->bWOMHeldForRead = TRUE;
   }

   if (SpinlockNeeded == TRUE)
   {
      QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> <-- RD_ScanForWaitMask", pDevExt->PortName)
   );
}  // QCRD_ScanForWaitMask

BOOLEAN QCRD_AdjustPaddingBytes(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock)
{
   PDEVICE_EXTENSION pDevExt = pIOBlock->pSerialDeviceObject->DeviceExtension;
   PUCHAR p;

   if ((pDevExt->bEnableBytePadding == FALSE) || (pIOBlock->ulActiveBytes < 5))
   {
      return FALSE;
   }

   // Adjust pIOBlock->pBufferFromDevice, pIOBlock->ulActiveBytes
   // if ((pIOBlock->ulActiveBytes % pDevExt->wMaxPktSize == 5) ||
   //     (pIOBlock->ulActiveBytes % pDevExt->wMaxPktSize == 6))
   if ((pIOBlock->ulActiveBytes % 64 == 5) ||
       (pIOBlock->ulActiveBytes % 64 == 6))
   {
      // examin the last 4 bytes
      p = (PUCHAR)pIOBlock->pBufferFromDevice;
      p = p + pIOBlock->ulActiveBytes - 4;
      if ((0xde == p[0]) && (0xad == p[1]) &&  // 0xdead
          (0xbe == p[2]) && (0xef == p[3]))   // 0xbeef
      {
         pIOBlock->ulActiveBytes -= 4;
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_TRACE,
            ("<%s> _AdjustPadding: found deadbeef, adjusting to %uB",
              pDevExt->PortName, pIOBlock->ulActiveBytes)
         );
         return TRUE;
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_TRACE,
            ("<%s> _AdjustPadding: not found deadbeef: %02x%02x%02x%02x\n",
              pDevExt->PortName, p[0], p[1], p[2], p[3])
         );
      }
   }

   return FALSE;
   
}  // QCRD_AdjustPaddingBytes
