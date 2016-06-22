/*===========================================================================
FILE: QCMRD.c

DESCRIPTION:
   This file contains implementation for reading data over USB.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003-2005 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#include "QCPTDO.h"
#include "QCRD.h"
#include "QCUTILS.h"
#include "QCDSP.h"
#include "QCMGR.h"
#include "QCPWR.h"

#ifdef QCUSB_MULTI_READS

extern USHORT   ucHdlCnt;

#undef QCOM_TRACE_IN

// The following protypes are implemented in ntoskrnl.lib
extern NTKERNELAPI VOID IoReuseIrp(IN OUT PIRP Irp, IN NTSTATUS Iostatus);

// Note: Always set L2 buffer's State as the last step
//       to avoid sync-up issues from potential race condition
//       between L1 and L2.

VOID QCMRD_L1MultiReadThread(PVOID pContext)
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

   // Reset L2 buffer
   QCMRD_ResetL2Buffers(pDevExt);
   pDevExt->bL1PropagateCancellation = TRUE;

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
            QCSER_Wait(pDevExt, -(10 * 1000L));  // 1ms
            pDevExt->bWOMHeldForRead = FALSE;
            QCRD_ScanForWaitMask(pDevExt, TRUE, 5);
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
                          IN (PKSTART_ROUTINE)QCMRD_L2MultiReadThread,
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
               _closeHandle(pDevExt->hL1ReadThreadHandle, "R-4");
               _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-A");
               pDevExt->pL2ReadThread = NULL;
               pDevExt->bL2ThreadInCreation = FALSE;
               pDevExt->bRdThreadInCreation = FALSE;
               ExFreePool(pwbArray);
               KeSetEvent(&pDevExt->ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
               PsTerminateSystemThread(STATUS_NO_MEMORY);
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
               _closeHandle(pDevExt->hL1ReadThreadHandle, "R-4");
               _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-A");
               pDevExt->pL2ReadThread = NULL;
               pDevExt->bRdThreadInCreation = FALSE;
               ExFreePool(pwbArray);
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

      if ((pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].State == L2BUF_STATE_COMPLETED) &&
          (pDevExt->bL1Stopped == FALSE))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_INFO,
            ("<%s> L1 direct completion [%u](0x%x)\n", pDevExt->PortName,
             pDevExt->L2FillIdx, pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status)
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
               ("<%s> L1 waited completion <0x%x> [%u](0x%x)\n", pDevExt->PortName,
                 ntStatus, pDevExt->L2FillIdx, pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status)
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

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_TRACE,
               ("<%s> L1: R_COMPL[%u]\n", pDevExt->PortName, pDevExt->L2FillIdx)
            );

            if (pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].bReturnToUser == FALSE)
            {
               // reset the buffer record
               pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Status  = STATUS_PENDING;
               pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].Length  = 0;
               pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].bFilled = FALSE;
               pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].State   = L2BUF_STATE_READY;
               pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].bReturnToUser = TRUE;

               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> Recycled L2: L1[%u]=NO_FILL_READY\n", pDevExt->PortName, pDevExt->L2FillIdx)
               );

               oldFillIdx = pDevExt->L2FillIdx;
               if (++(pDevExt->L2FillIdx) == pDevExt->NumberOfL2Buffers)
               {
                  pDevExt->L2FillIdx = 0;
               }
               goto wait_for_completion;
            }

            // The following should not happen because the L2 buffer will
            // not be reset once it's initialized, and it won't go to another
            // state once it's in STATE_COMPLETED.
            if (pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].State != L2BUF_STATE_COMPLETED)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_FORCE,
                  ("<%s> L1 err: L2 buf[%u] state changed-%u\n", pDevExt->PortName,
                    pDevExt->L2FillIdx,
                    pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].State)
               );
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
                     ("<%s> L1: Waiting pipe IN (0x%x)\n", pDevExt->PortName, ntStatus)
                  );
                  KeSetEvent
                  (
                     &(DeviceInfo[pDevExt->MgrId].DspDeviceResetINEvent),
                     IO_NO_INCREMENT,
                     FALSE
                  );
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
            pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].bReturnToUser = TRUE;

            pDevExt->pL2ReadBuffer[pDevExt->L2FillIdx].State   = L2BUF_STATE_READY;

            QCSER_DbgPrint2
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Set L2 State: L1 - [%u] = READY\n", pDevExt->PortName, pDevExt->L2FillIdx)
            );

            oldFillIdx = pDevExt->L2FillIdx;
            if (++(pDevExt->L2FillIdx) == pDevExt->NumberOfL2Buffers)
            {
               pDevExt->L2FillIdx = 0;
            }

            // Signal L2 if all L2 buffers are exhausted
            if (oldFillIdx == pDevExt->L2IrpEndIdx)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_DETAIL,
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
            // otherwise, release pCurrIOBlock
            pDevExt->pReadCurrent = NULL;
            QcExFreeReadIOB(pCurrIOBlock, FALSE);
            QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
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
               if (pCurrIOBlock != NULL)
               {
                  QCSER_DbgPrint2
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> free IOB 07 0x%p\n", pDevExt->PortName, pCurrIOBlock)
                  );
                  pDevExt->pReadCurrent = NULL;  // need this before freeing IOB
                  QcExFreeReadIOB(pCurrIOBlock, FALSE);
               }
               QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
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

            pDevExt->pReadCurrent = NULL; // need this before freeing IOB
            QCSER_DbgPrint2
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> free IOB 08 0x%p\n", pDevExt->PortName, pCurrIOBlock)
            );
            QcExFreeReadIOB(pCurrIOBlock, FALSE);
            QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);
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
               ("<%s> ML1 CANCEL\n", pDevExt->PortName)
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

   KeSetEvent(&pDevExt->L1ReadThreadClosedEvent,IO_NO_INCREMENT,FALSE);
   pDevExt->bL1InCancellation = FALSE;
   pDevExt->bL1Stopped = FALSE;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_ERROR,
      ("<%s> MRD: term <%d, %d/%d>\n", pDevExt->PortName, pDevExt->Sts.lRmlCount[1],
       pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedRdMem)
   );
   _closeHandle(pDevExt->hL1ReadThreadHandle, "R-3");
   PsTerminateSystemThread(STATUS_SUCCESS); // terminate this thread
}  // QCMRD_L1MultiReadThread

NTSTATUS MultiReadCompletionRoutine
(
   PDEVICE_OBJECT DO,
   PIRP           Irp,
   PVOID          Context
)
{
   PQCRD_L2BUFFER    pL2Ctx  = (PQCRD_L2BUFFER)Context;
   PDEVICE_EXTENSION pDevExt = pL2Ctx->DeviceExtension;
   KIRQL             Irql    = KeGetCurrentIrql();
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QcAcquireSpinLockWithLevel(&pDevExt->L2Lock, &levelOrHandle, Irql);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> L2 COMPL: Irp[%u]=0x%p(0x%x)\n", pDevExt->PortName,
        pL2Ctx->Index, Irp, Irp->IoStatus.Status)
   );

   InsertTailList(&pDevExt->L2CompletionQueue, &pL2Ctx->List);
   KeSetEvent(&pL2Ctx->CompletionEvt, IO_NO_INCREMENT, FALSE);

   QcReleaseSpinLockWithLevel(&pDevExt->L2Lock, levelOrHandle, Irql);

   QCPWR_SetIdleTimer(pDevExt, 0, FALSE, 3); // RD completion

   return STATUS_MORE_PROCESSING_REQUIRED;
}  // MultiReadCompletionRoutine

VOID QCMRD_L2MultiReadThread(PDEVICE_EXTENSION pDevExt)
{
   PIO_STACK_LOCATION pNextStack;
   BOOLEAN            bCancelled = FALSE;
   BOOLEAN            bFirstTime = TRUE;
   NTSTATUS           ntStatus;
   PKWAIT_BLOCK       pwbArray;
   LONG               lBytesInQueue;
   ULONG              ulReadSize;
   KEVENT             dummyEvent;
   int                i, devBusyCnt = 0, devErrCnt = 0;
   ULONG              waitCount = L2_READ_EVENT_COUNT+pDevExt->NumberOfL2Buffers; // must < 64
   UCHAR              reqSent;  // for debugging purpose
   L2_STATE           l2State = L2_STATE_WORKING;
   PQCRD_L2BUFFER     pActiveL2Buf = NULL;
   PLIST_ENTRY        headOfList;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

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

   // allocate a wait block array for the multiple wait
   pwbArray = _ExAllocatePool
              (
                 NonPagedPool,
                 waitCount*sizeof(KWAIT_BLOCK),
                 "Level2ReadThread.pwbArray"
              );
   if (!pwbArray)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> L2: NO_MEMORY for %u wait blocks\n", pDevExt->PortName, waitCount)
      );
      _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-2");
      pDevExt->bL2ThreadInCreation = FALSE;
      KeSetEvent(&pDevExt->L2ReadThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   #ifdef ENABLE_LOGGING
   // Create logs
   if (pDevExt->EnableLogging == TRUE)
   {
      QCSER_CreateLogs(pDevExt, QCSER_CREATE_RX_LOG);
   }
   #endif  // ENABLE_LOGGING

   // Set L2 thread priority
   KeSetPriorityThread(KeGetCurrentThread(), QCSER_L2_PRIORITY);
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

   pDevExt->bL2ReadActive = FALSE;

   pDevExt->pL2ReadEvents[QC_DUMMY_EVENT_INDEX] = &dummyEvent;
   KeInitializeEvent(&dummyEvent,NotificationEvent,FALSE);
   KeClearEvent(&dummyEvent);

   // Assign read completion events
   for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
   {
      pDevExt->pL2ReadEvents[L2_READ_EVENT_COUNT+i] = 
         &(pDevExt->pL2ReadBuffer[i].CompletionEvt);
   }
   pDevExt->L2IrpStartIdx = pDevExt->L2IrpEndIdx = 0;

   pDevExt->bL2Stopped = pDevExt->PowerSuspended;

   while (bCancelled == FALSE)
   {
      reqSent = 0;

      while ((pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].State == L2BUF_STATE_READY) &&
              inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
      {
         PIRP pIrp = pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].Irp;
         PURB pUrb = &(pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].Urb);

         if (pDevExt->bL2Stopped == FALSE)
         {
            IoReuseIrp(pIrp, STATUS_SUCCESS);

            // initialize the static parameters in the read irp
            pNextStack = IoGetNextIrpStackLocation(pIrp);
            pNextStack->Parameters.Others.Argument1 = pUrb;
            pNextStack->Parameters.DeviceIoControl.IoControlCode =
               IOCTL_INTERNAL_USB_SUBMIT_URB;
            pNextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;

            IoSetCompletionRoutine
            (
               pIrp,
               MultiReadCompletionRoutine,
               (PVOID)&(pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx]),
               TRUE,
               TRUE,
               TRUE
            );

            lBytesInQueue = CountReadQueue(pDevExt);
            if (lBytesInQueue >= pDevExt->lReadBuffer80pct)
            {
               ulReadSize = pDevExt->MinInPktSize;

               // We may have been going too fast and may have blocked other threads.
               // One scenario here is that too much incoming data could very well
               // block the queuing of wait-on-mask IRP, and subsequently make the
               // application fail to issue another read request. Details below:
               //  1) IOCTL_SERIAL_WAIT_ON_MASK request reached our dispatch routine
               //  2) Huge amount of data was pending in the bus waiting for the
               //     driver to fetch.
               //  3) The driver receive buffer was almost full, so it switched to
               //     smaller receive size (MinInPktSize)
               //  4) The driver fell in a busy loop between itself and the bud driver
               //     detching data in chunks of MinInPktSize bytes.
               //  5) The driver's internal receive buffer was full.
               //  6) The read thread waited for "ring 3".
               //  7) The IOCTL_SERIAL_WAIT_ON_MASK request finally got a chance and
               //     euqueued.
               //  8) The IOCTL_SERIAL_WAIT_ON_MASK IRP may not get another chance to
               //     get completed until much later.
               //
               // Solution:
               //    In order to avoid the above scenario, we need to slow down and
               //    try to process any queued IOCTL_SERIAL_WAIT_ON_MASK when we gets here.

               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> ML2: rng3 too slow, rd size to be %uB\n",
                    pDevExt->PortName, ulReadSize)
               );
               QCSER_Wait(pDevExt, -(10 * 1000L));  // 1ms
               pDevExt->bWOMHeldForRead = FALSE;
               QCRD_ScanForWaitMask(pDevExt, TRUE, 4);
            }
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
               pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].Buffer,
               NULL,
               ulReadSize,
               USBD_SHORT_TRANSFER_OK | USBD_TRANSFER_DIRECTION_IN,
               NULL  // UrbLink -- must be NULL
            )

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_TRACE,
               ("<%s> ML2: IoCallDriver-IRP[%u]=0x%p\n", pDevExt->PortName,
                pDevExt->L2IrpEndIdx, pIrp)
            );
            pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].State = L2BUF_STATE_PENDING;
            QCSER_DbgPrint2
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Set L2 State: L2 - [%u] = PENDING\n", pDevExt->PortName, pDevExt->L2IrpEndIdx)
            );
            pDevExt->bL2ReadActive = TRUE;
            ntStatus = IoCallDriver(pDevExt->StackDeviceObject, pIrp);
            if (ntStatus != STATUS_PENDING)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_CRITICAL,
                  ("<%s> Rth: IoCallDriver rtn 0x%x", pDevExt->PortName, ntStatus)
               );
               pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].Status = ntStatus;
               pDevExt->pL2ReadBuffer[pDevExt->L2IrpEndIdx].State = L2BUF_STATE_COMPLETED;
               QCSER_DbgPrint2
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> Set L2 State: L2 - [%u] = COMPLETED\n", pDevExt->PortName, pDevExt->L2IrpEndIdx)
               );
               // if ((ntStatus == STATUS_DEVICE_NOT_READY) || (ntStatus == STATUS_DEVICE_DATA_ERROR))
               if (!NT_SUCCESS(ntStatus))
               {
                  devErrCnt++;
                  if (devErrCnt > 3)
                  {
                     pDevExt->bL2Stopped = TRUE;
                     l2State = L2_STATE_STOPPING;
                     QCSER_DbgPrint
                     (
                        QCSER_DBG_MASK_READ,
                        QCSER_DBG_LEVEL_ERROR,
                        ("<%s> L2 err, to stop\n", pDevExt->PortName)
                     );
                     KeSetEvent
                     (
                        &pDevExt->L2ReadStopEvent,
                        IO_NO_INCREMENT,
                        FALSE
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

            if (++(pDevExt->L2IrpEndIdx) == pDevExt->NumberOfL2Buffers)
            {
               pDevExt->L2IrpEndIdx = 0;
            }
            ++reqSent;

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
               ("<%s> L2R: ERR-ACTorSTOP(L2Stop=%u)\n", pDevExt->PortName, pDevExt->bL2Stopped)
            );
            break;
         }
      } // end while -- if L2 buffer is available to receive

      #ifdef QCSER_DBGPRINT
      if ((reqSent == 0) && (QCMRD_L2ActiveBuffers(pDevExt) == 0))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> L2 buf exhausted or no dev, wait...\n", pDevExt->PortName)
         );
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> %u L2 REQ's sent\n", pDevExt->PortName, reqSent)
         );
      }
      #endif // QCSER_DBGPRINT

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

      ntStatus = KeWaitForMultipleObjects
                 (
                    waitCount,
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

         case L2_READ_COMPLETION_EVENT_INDEX:
         {
            // this is not used, just for compatibility.
            KeClearEvent(&pDevExt->L2ReadCompletionEvent);
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_CRITICAL,
               ("<%s> ML2 ERR: COMPLETION_EVENT\n", pDevExt->PortName)
            );
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
               ("<%s> L2 R kicked\n", pDevExt->PortName)
            );

            // go around the loop, send next available request
            break;
         }

         case CANCEL_EVENT_INDEX:
         {
            // reset read cancel event so we dont reactive
            KeClearEvent(&pDevExt->CancelReadEvent); // never set

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> L2_CANCEL\n", pDevExt->PortName)
            );

            pActiveL2Buf = QCMRD_L2NextActive(pDevExt);

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

            pDevExt->bL2Stopped = TRUE;
            if (pActiveL2Buf != NULL)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_INFO,
                  ("<%s> L2 CANCEL - IRP[%u]\n", pDevExt->PortName, pActiveL2Buf->Index)
               );
               l2State = L2_STATE_CANCELLING;

               // direct cancellation to completion section
               IoCancelIrp(pActiveL2Buf->Irp);

               goto wait_for_completion;
            }

            bCancelled = TRUE;

            break;
         }  // CANCEL_EVENT_INDEX

         case L2_READ_PURGE_EVENT_INDEX:  // 4
         {
            KeClearEvent(&pDevExt->L2ReadPurgeEvent);

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> L2_PURGE\n", pDevExt->PortName)
            );

            pActiveL2Buf = QCMRD_L2NextActive(pDevExt);

            if (pActiveL2Buf != NULL)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_INFO,
                  ("<%s> L2 PURGE: Cancel Irp[%u]\n", pDevExt->PortName, pActiveL2Buf->Index)
               );
               l2State = L2_STATE_PURGING;

               // direct PURGE to completion section
               IoCancelIrp(pActiveL2Buf->Irp);

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

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> L2_STOP\n", pDevExt->PortName)
            );

            if (l2State == L2_STATE_CANCELLING)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_INFO,
                  ("<%s> L2_STOP: superceded by CANCELLATION\n", pDevExt->PortName)
               );
               goto wait_for_completion;
            }

            pActiveL2Buf = QCMRD_L2NextActive(pDevExt);

            pDevExt->bL2Stopped = TRUE;

            if (pActiveL2Buf != NULL)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_INFO,
                  ("<%s> L2 STOP: Cancel Irp[%u]\n", pDevExt->PortName, pActiveL2Buf->Index)
               );
               l2State = L2_STATE_STOPPING;

               // direct STOP to completion section
               IoCancelIrp(pActiveL2Buf->Irp);

               goto wait_for_completion;
            }

            KeSetEvent(&pDevExt->L2ReadStopAckEvent, IO_NO_INCREMENT, FALSE);

            break;
         }  // L2_READ_STOP_EVENT_INDEX

         case L2_READ_RESUME_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->L2ReadResumeEvent);

            if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> L2 Resume: dev removed, no act\n", pDevExt->PortName)
               );
               goto wait_for_completion;
            }

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
               ("<%s> L2 Resume - 0x%x\n", pDevExt->PortName, pDevExt->bL2Stopped)
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
            int            l2BufIdx;
            PIRP           pIrp;
            PURB           pUrb;

            if ((ntStatus < L2_READ_EVENT_COUNT) || (ntStatus >= waitCount))
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> L2 unsupported st 0x%x\n", pDevExt->PortName, ntStatus)
               );

               // Ignore for now
               break;
            }

            // L2_READ_COMPLETION_EVENT

            // De-queue L2 buffer item
            QcAcquireSpinLock(&pDevExt->L2Lock, &levelOrHandle);

            pActiveL2Buf = NULL;
            if (!IsListEmpty(&pDevExt->L2CompletionQueue))
            {
               headOfList = RemoveHeadList(&pDevExt->L2CompletionQueue);
               pActiveL2Buf = CONTAINING_RECORD
                           (
                              headOfList,
                              QCRD_L2BUFFER,
                              List
                           );
            }
            QcReleaseSpinLock(&pDevExt->L2Lock, levelOrHandle);

            // l2BufIdx = ntStatus - L2_READ_EVENT_COUNT;
            if (pActiveL2Buf == NULL)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> ML2 err: comp Q empty - %u\n", pDevExt->PortName, ntStatus)
               );
               QCSER_Wait(pDevExt, -(1000 * 1000L));  // 100ms
               break;
            }
            l2BufIdx = pActiveL2Buf->Index;

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> L2 completion [%u]=wait rtn[%u]\n", pDevExt->PortName,
                 l2BufIdx, (ntStatus-L2_READ_EVENT_COUNT))
            );

            // clear read completion event
            KeClearEvent(&(pDevExt->pL2ReadBuffer[l2BufIdx].CompletionEvt));

            pIrp = pDevExt->pL2ReadBuffer[l2BufIdx].Irp;
            pUrb = &(pDevExt->pL2ReadBuffer[l2BufIdx].Urb);

            ntStatus = pIrp->IoStatus.Status;

            #ifdef ENABLE_LOGGING
            if (ntStatus == STATUS_SUCCESS)
            {
               #ifdef QCOM_TRACE_IN
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> L2R[%d]=%ldB\n", pDevExt->PortName, l2BufIdx,
                    pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength)
               );
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
               // to debug output
               QCUTIL_PrintBytes
               (
                  (PVOID)(pUrb->UrbBulkOrInterruptTransfer.TransferBuffer),
                  128,
                  pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength,
                  "RxData",
                  pDevExt,
                  QCSER_DBG_MASK_RDATA,
                  QCSER_DBG_LEVEL_DETAIL
               );
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
                  ("<%s> L2 RX failed[%u] - 0x%x ToUser %d\n", pDevExt->PortName, l2BufIdx,
                    pIrp->IoStatus.Status, pDevExt->bL1PropagateCancellation)
               );
            }
            #endif  // ENABLE_LOGGING

            if ((pIrp->IoStatus.Status == STATUS_CANCELLED) &&
                (pDevExt->bL1PropagateCancellation == FALSE))
            {
               pDevExt->pL2ReadBuffer[l2BufIdx].bReturnToUser = FALSE;
            }
            else
            {
               pDevExt->pL2ReadBuffer[l2BufIdx].bReturnToUser = TRUE;
            }

            // if ((pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength > 0)
            //     || (ntStatus != STATUS_SUCCESS))
            {
               pDevExt->pL2ReadBuffer[l2BufIdx].Status = ntStatus;
               pDevExt->pL2ReadBuffer[l2BufIdx].Length =
                  pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;

               if ((pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength > 0)
                   || (ntStatus != STATUS_SUCCESS))
               {
                  pDevExt->pL2ReadBuffer[l2BufIdx].bFilled = TRUE;
               }
               pDevExt->pL2ReadBuffer[l2BufIdx].State = L2BUF_STATE_COMPLETED;
               QCSER_DbgPrint2
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> Set L2 State: L2 - [%u] = COMPLETED (WAIT)\n", pDevExt->PortName, l2BufIdx)
               );

               if (++l2BufIdx == pDevExt->NumberOfL2Buffers)
               {
                  l2BufIdx = 0;
               }
               pDevExt->L2IrpStartIdx = l2BufIdx;

               if (!NT_SUCCESS(ntStatus))
               {
                  // STATUS_DEVICE_NOT_READY      0xC00000A3
                  // STATUS_DEVICE_DATA_ERROR     0xC000009C
                  // STATUS_DEVICE_NOT_CONNECTED  0xC000009D
                  // STATUS_UNSUCCESSFUL          0xC0000001
                  if (ntStatus != STATUS_CANCELLED)
                  {
                     if ((++devBusyCnt >= (pDevExt->NumOfRetriesOnError+2)) &&
                         (bCancelled == FALSE) &&
                         (l2State == L2_STATE_WORKING))
                     {
                        pDevExt->bL2Stopped = TRUE;
                        QCSER_DbgPrint
                        (
                           QCSER_DBG_MASK_READ,
                           QCSER_DBG_LEVEL_CRITICAL,
                           ("<%s> L2 cont failure, stopped...\n", pDevExt->PortName)
                        );
                        devBusyCnt = 0;

                        // Set STOP event
                        KeSetEvent
                        (
                           &pDevExt->L2ReadStopEvent,
                           IO_NO_INCREMENT,
                           FALSE
                        );

                        // Notify L1
                        KeSetEvent
                        (
                           &pDevExt->L1ReadCompletionEvent,
                           IO_NO_INCREMENT,
                           FALSE
                        );
                        goto wait_for_completion;
                     }
                  }
               }

               // Notify L1 thread
               KeSetEvent
               (
                  &pDevExt->L1ReadCompletionEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );
            }

            pActiveL2Buf = QCMRD_L2NextActive(pDevExt);
            pDevExt->bL2ReadActive = (pActiveL2Buf != NULL);

            if (l2State != L2_STATE_WORKING)
            {
               if (pActiveL2Buf != NULL)
               {
                  IoCancelIrp(pActiveL2Buf->Irp);
                  goto wait_for_completion;
               }
               else
               {
                  if (l2State == L2_STATE_STOPPING)
                  {
                     KeSetEvent(&pDevExt->L2ReadStopAckEvent, IO_NO_INCREMENT, FALSE);
                  }
                  else if (l2State == L2_STATE_PURGING)
                  {
                     KeSetEvent
                     (
                        &pDevExt->L1ReadPurgeAckEvent,
                        IO_NO_INCREMENT,
                        FALSE
                     );
                  }
                  else if (l2State == L2_STATE_CANCELLING)
                  {
                     // all active IRP's have been cancelled, time to exit
                     bCancelled = TRUE;
                  }

                  l2State = L2_STATE_WORKING;
               }
            }

            if (pDevExt->bDeviceRemoved == TRUE)
            {
               // Set STOP event
               KeSetEvent
               (
                  &pDevExt->L2ReadStopEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );

               // Do we need to clear L2 buffers???
               pDevExt->bL2Stopped = TRUE;
            }

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

   // Empty the L2 completion queue
   QcAcquireSpinLock(&pDevExt->L2Lock, &levelOrHandle);
   while (!IsListEmpty(&pDevExt->L2CompletionQueue))
   {
      headOfList = RemoveHeadList(&pDevExt->L2CompletionQueue);
   }
   QcReleaseSpinLock(&pDevExt->L2Lock, levelOrHandle);

   KeSetEvent(&pDevExt->L2ReadThreadClosedEvent,IO_NO_INCREMENT,FALSE);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> L2: OUT\n", pDevExt->PortName, ntStatus)
   );

   _closeHandle(pDevExt->hL2ReadThreadHandle, "L2R-B");
   PsTerminateSystemThread(STATUS_SUCCESS); // terminate this thread
}  // QCMRD_L2MultiReadThread

VOID QCMRD_ResetL2Buffers(PDEVICE_EXTENSION pDevExt)
{
   int i;

   pDevExt->L2IrpStartIdx = pDevExt->L2IrpEndIdx = pDevExt->L2FillIdx = 0;

   for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
   {
      pDevExt->pL2ReadBuffer[i].Status  = STATUS_PENDING;
      pDevExt->pL2ReadBuffer[i].Length  = 0;
      pDevExt->pL2ReadBuffer[i].bFilled = FALSE;
      pDevExt->pL2ReadBuffer[i].bReturnToUser = TRUE;

      pDevExt->pL2ReadBuffer[i].DeviceExtension = pDevExt;
      IoReuseIrp(pDevExt->pL2ReadBuffer[i].Irp, STATUS_SUCCESS);
      pDevExt->pL2ReadBuffer[i].State           = L2BUF_STATE_READY;
      QCSER_DbgPrint2
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> Set L2 State: Rst - [%u] = READY (WAIT)\n", pDevExt->PortName, i)
      );
   }

   // Reset some of L1 events in cause they were set by L2 during
   // last thread termination
   KeClearEvent(&pDevExt->L1ReadCompletionEvent);

}  // QCMRD_ResetL2Buffers

PQCRD_L2BUFFER QCMRD_L2NextActive(PDEVICE_EXTENSION pDevExt)
{
   int startIdx = pDevExt->L2IrpStartIdx;
   int endIdx   = pDevExt->L2IrpEndIdx;

   // Note: must be done sequencially, starting with the IRP start index

   if (pDevExt->pL2ReadBuffer[startIdx].State == L2BUF_STATE_PENDING)
   {
      return &(pDevExt->pL2ReadBuffer[startIdx]);
   }

   // Note: endIdx is actually not pending, it's the next req idx to bus
   while (startIdx != endIdx)
   {
      if (++startIdx == pDevExt->NumberOfL2Buffers)
      {
         startIdx = 0;
      }

      if (startIdx != endIdx)
      {
         if (pDevExt->pL2ReadBuffer[startIdx].State == L2BUF_STATE_PENDING)
         {
            return &(pDevExt->pL2ReadBuffer[startIdx]);
         }
      }
   }

   return NULL;
}  // QCMRD_L2NextActive

int QCMRD_L2ActiveBuffers(PDEVICE_EXTENSION pDevExt)
{
   int i, num = 0;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QcAcquireSpinLock(&pDevExt->L2Lock, &levelOrHandle);

   for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
   {
      if (pDevExt->pL2ReadBuffer[i].State == L2BUF_STATE_PENDING)
      {
         ++num;
      }
   }

   QcReleaseSpinLock(&pDevExt->L2Lock, levelOrHandle);

   return num;
}  // QCMRD_L2ActiveBuffers

#endif // QCUSB_MULTI_READS

