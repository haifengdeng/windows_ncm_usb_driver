/*===========================================================================
FILE: QCMWT.c

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

#ifdef QCUSB_MULTI_WRITES

NTSTATUS QCMWT_WriteCompletionRoutine
(
   PDEVICE_OBJECT pDO,
   PIRP           pIrp,
   PVOID          pContext
)
{
   PQCMWT_BUFFER     pWtBuf  = (PQCMWT_BUFFER)pContext;
   PDEVICE_EXTENSION pDevExt = pWtBuf->DeviceExtension;
   KIRQL             Irql    = KeGetCurrentIrql();
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> MWT_WriteCompletionRoutine[%d]=0x%p(0x%x)\n",
        pDevExt->PortName, pWtBuf->Index, pIrp, pIrp->IoStatus.Status)
   );
   QcAcquireSpinLockWithLevel(&pDevExt->WriteSpinLock, &levelOrHandle, Irql);

   // State change: pending => completed
   pWtBuf->State = MWT_BUF_COMPLETED;

   // Remove entry from MWritePendingList
   RemoveEntryList(&pWtBuf->List);
   pDevExt->MWTPendingCnt--;

   // Put the entry into MWriteCompletionList
   InsertTailList(&pDevExt->MWriteCompletionQueue, &pWtBuf->List);

   QcReleaseSpinLockWithLevel(&pDevExt->WriteSpinLock, levelOrHandle, Irql);

   KeSetEvent
   (
      &pWtBuf->WtCompletionEvt,
      IO_NO_INCREMENT,
      FALSE
   );

   QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_WT, FALSE, 4); // WT completion

   return STATUS_MORE_PROCESSING_REQUIRED;
}  // QCMWT_WriteCompletionRoutine

VOID QCMWT_WriteThread(PVOID pContext)
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
   ULONG i;
   PCHAR pActiveBuffer = NULL;
   BOOLEAN bFirstTime = TRUE;
   ULONG  waitCount = WRITE_EVENT_COUNT + pDevExt->NumberOfMultiWrites;
   PQCMWT_BUFFER pWtBuf;   
   PLIST_ENTRY headOfList;
   MWT_STATE   mwtState = MWT_STATE_WORKING;
   UCHAR       reqSent;  // for debugging purpose
   QCUSB_MWT_SENT_IRP sentIrp[QCUSB_MAX_MRW_BUF_COUNT];
   BOOLEAN      bFlowOn = TRUE;
   PLIST_ENTRY  irpSent;
   PQCUSB_MWT_SENT_IRP irpSentRec;
   BOOLEAN bSendZeroLength = FALSE;
   ULONG   bytesSent = 0;
   CHAR    tempBuf[2];
   LARGE_INTEGER delayValue;
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
         ("<%s> MWT: wrong IRQL::%d\n", pDevExt->PortName, KeGetCurrentIrql())
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
                 "QCMWT_WriteThread.pwbArray"
              );
   if (!pwbArray)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> MWT: STATUS_NO_MEMORY 1\n", pDevExt->PortName)
      );
      _closeHandle(pDevExt->hWriteThreadHandle, "W-1");
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   #ifdef ENABLE_LOGGING
   // Create logs
   if (pDevExt->EnableLogging == TRUE)
   {
      QCSER_CreateLogs(pDevExt, QCSER_CREATE_TX_LOG);
   }
   #endif // ENABLE_LOGGING

   // clear any outbound flow-control
   pDevExt->pSerialStatus->HoldReasons = 0;

   // Set WRITE thread priority
   KeSetPriorityThread(KeGetCurrentThread(), QCSER_WT_PRIORITY);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> MW pri=%d\n", pDevExt->PortName, KeQueryPriorityThread(KeGetCurrentThread()))
   );

   pDevExt->bWriteActive = FALSE;
   pDevExt->pWriteCurrent = NULL;

   pDevExt->pWriteEvents[QC_DUMMY_EVENT_INDEX] = &dummyEvent;
   KeInitializeEvent(&dummyEvent, NotificationEvent, FALSE);
   KeClearEvent(&dummyEvent);

   pDevExt->bWriteStopped = pDevExt->PowerSuspended;
   
   RtlZeroMemory(&sentIrp, sizeof(QCUSB_MWT_SENT_IRP)*QCUSB_MAX_MRW_BUF_COUNT);
   for (i = 0; i < QCUSB_MAX_MRW_BUF_COUNT; i++)
   {
      sentIrp[i].IrpReturned = TRUE;
      InsertTailList(&pDevExt->MWTSentIrpRecordPool, &(sentIrp[i].List));
   }

   while(TRUE)
   {
      if (pDevExt->pSerialStatus->HoldReasons && (bCancelled == FALSE))
      {
         goto wait_for_completion;
      }

      if (pDevExt->bWriteStopped == TRUE)
      {
         if (bFlowOn == FALSE)
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> MWT: flow_off, OUT stopped", pDevExt->PortName)
            );
         }
         else
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> MWT: flow_on, but W stopped", pDevExt->PortName)
            );
         }
         goto wait_for_completion;
      }
      else if (bFlowOn == FALSE)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> MWT: flow_off, but W not stopped", pDevExt->PortName)
         );
         goto wait_for_completion;
      }

      reqSent = 0;

      while (TRUE)
      {
         QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

         // When all current data is sent, we check if a 0-len pkt is needed
         if (IsListEmpty(&pDevExt->MWritePendingQueue)    && // no pending tx
             IsListEmpty(&pDevExt->MWriteCompletionQueue) && // processed all returned
             (pDevExt->pWriteHead == NULL))                  // no further writes
         {
            if ((bytesSent > 0) && (bytesSent % pDevExt->wMaxPktSize == 0))
            {
               bSendZeroLength = TRUE;
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> MWT: sent %uB, add 0-len", pDevExt->PortName, bytesSent)
               );
               bytesSent = 0;
            }
         }

         if (((pDevExt->pWriteHead) || (bSendZeroLength == TRUE)) &&
             (!IsListEmpty(&pDevExt->MWriteIdleQueue))            &&
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
            else
            {
               if (pDevExt->pWriteHead == NULL)
               {
                  QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
                  break;  // get out of the loop
               }
            }

            // De-queue write idle buffer
            headOfList = RemoveHeadList(&pDevExt->MWriteIdleQueue);
            pWtBuf = CONTAINING_RECORD(headOfList, QCMWT_BUFFER, List);
            // State change: idle => pending
            pWtBuf->State = MWT_BUF_PENDING;
            InsertTailList(&pDevExt->MWritePendingQueue, &pWtBuf->List);
            ++(pDevExt->MWTPendingCnt);

            if (bSendZeroLength == FALSE)
            {
               // Dequeue write IRP
               pCurrIOBlock = pDevExt->pWriteHead;
               pDevExt->pWriteHead = pCurrIOBlock->pNextEntry;
               pWtBuf->IoBlock = (PVOID)pCurrIOBlock;
            }
            else
            {
               pWtBuf->IoBlock = pCurrIOBlock = NULL;
               QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
               goto MWT_Irp_Setup;
            }

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
                     ("<%s> MWT: buffers flushed (IRP 0x%p)\n", pDevExt->PortName,
                       pCurrIOBlock->pCallingIrp)
                  );
                  pCurrIOBlock->ntStatus = STATUS_SUCCESS;
                  pCurrIOBlock->ulBTDBytes = pCurrIOBlock->ulActiveBytes = 0;
                  pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, TRUE, 15);
                  QcExFreeWriteIOB(pCurrIOBlock, TRUE);

                  // Revert the MWT buffer
                  QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
                  pWtBuf->State = MWT_BUF_IDLE;
                  pWtBuf->IoBlock = NULL;
                  RemoveEntryList(&pWtBuf->List); // off MWritePendingQueue
                  InsertHeadList(&pDevExt->MWriteIdleQueue, &pWtBuf->List);
                  --(pDevExt->MWTPendingCnt);
                  QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                  // Set timer for selective suspension
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
                  ("<%s> MWT: pre-to 0x%p (%lu/%lu)\n", pDevExt->PortName,
                    pCurrIOBlock, pCurrIOBlock->ulActiveBytes, pCurrIOBlock->ulBTDBytes)
               );
               pCurrIOBlock->ntStatus = STATUS_TIMEOUT;
               pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, TRUE, 9);
               QcExFreeWriteIOB(pCurrIOBlock, TRUE);

               // Revert the MWT buffer
               QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
               pWtBuf->State = MWT_BUF_IDLE;
               pWtBuf->IoBlock = NULL;
               RemoveEntryList(&pWtBuf->List); // off MWritePendingQueue
               InsertHeadList(&pDevExt->MWriteIdleQueue, &pWtBuf->List);
               --(pDevExt->MWTPendingCnt);
               QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

               QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_WT, TRUE, 13);
               continue;
            }
            else if ((bCancelled == TRUE) || pCurrIOBlock->bPurged == TRUE)
            {
               // send cancel completions for each que'ed i/o
               pCurrIOBlock->ntStatus = STATUS_CANCELLED;
               pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, TRUE, 1);
               QcExFreeWriteIOB(pCurrIOBlock, TRUE);

               // Revert the MWT buffer
               QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
               pWtBuf->State = MWT_BUF_IDLE;
               pWtBuf->IoBlock = NULL;
               RemoveEntryList(&pWtBuf->List); // off MWritePendingQueue
               InsertHeadList(&pDevExt->MWriteIdleQueue, &pWtBuf->List);
               --(pDevExt->MWTPendingCnt);
               QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

               QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_WT, TRUE, 14);
               continue;
            }

   MWT_Irp_Setup:

            // setup irp for i/o
            if ((bSendZeroLength == FALSE) && (pCurrIOBlock->pCallingIrp != NULL))
            {
               PIRP pIrp = pCurrIOBlock->pCallingIrp;

               pActiveBuffer = (PCHAR)pCurrIOBlock->pBufferToDevice + pIrp->IoStatus.Information;
               pCurrIOBlock->ulActiveBytes = pCurrIOBlock->ulBTDBytes - pIrp->IoStatus.Information;
               ulTransferBytes = pCurrIOBlock->ulActiveBytes;
            }
            else
            {
               pActiveBuffer = (PCHAR)tempBuf;
               ulTransferBytes = 0;
               bSendZeroLength = FALSE;
            }

            pIrp = pWtBuf->Irp;
            pUrb = &pWtBuf->Urb;
            pWtBuf->Length = ulTransferBytes;

            #ifdef QCUSB_FC

            // record the IRP to be sent
            irpSent = RemoveHeadList(&pDevExt->MWTSentIrpRecordPool);
            irpSentRec = CONTAINING_RECORD(irpSent, QCUSB_MWT_SENT_IRP, List);
            irpSentRec->IOB = pCurrIOBlock;
            if (pCurrIOBlock != NULL)
            {
               irpSentRec->SentIrp = pCurrIOBlock->pCallingIrp;  // could be NULL for 0-len
               if (pCurrIOBlock->pCallingIrp != NULL)
               {
                  irpSentRec->TotalLength = ulTransferBytes + pCurrIOBlock->pCallingIrp->IoStatus.Information;
               }
               else
               {
                  // 0-len pkt
                  irpSentRec->TotalLength = ulTransferBytes;
               }
            }
            else
            {
               irpSentRec->SentIrp = NULL;
               irpSentRec->TotalLength = ulTransferBytes;
            }
            irpSentRec->MWTBuf = pWtBuf;
            irpSentRec->IrpReturned = FALSE;
            InsertTailList(&pDevExt->MWTSentIrpQueue, &irpSentRec->List);

            #endif // QCUSB_FC

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
               pActiveBuffer, // pCurrIOBlock->pBufferToDevice,
               NULL,
               ulTransferBytes,
               USBD_TRANSFER_DIRECTION_OUT,
               NULL
            );

            IoReuseIrp(pIrp, STATUS_SUCCESS);
            pNextStack = IoGetNextIrpStackLocation( pIrp );
            pNextStack -> Parameters.DeviceIoControl.IoControlCode =
               IOCTL_INTERNAL_USB_SUBMIT_URB;
            pNextStack -> MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
            pNextStack->Parameters.Others.Argument1 = pUrb;

            IoSetCompletionRoutine
            (
               pIrp,
               QCMWT_WriteCompletionRoutine,
               pWtBuf,
               TRUE,
               TRUE,
               TRUE
            );

            reqSent++;
            pDevExt->bWriteActive = TRUE;
            if (ulTransferBytes > 0)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> MWT: Sending IRP 0x%p/0x%p[%u] %uB/%uB\n", pDevExt->PortName,
                    pWtBuf->IoBlock->pCallingIrp, pIrp, pWtBuf->Index, ulTransferBytes, irpSentRec->TotalLength)
               );
            }
            else
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> MWT: Sending IRP 0x%p[%u] %uB/%uB (0x%p/%u) -added 0-len\n", pDevExt->PortName,
                    pIrp, pWtBuf->Index, ulTransferBytes, irpSentRec->TotalLength,
                    (pWtBuf->IoBlock != NULL ? pWtBuf->IoBlock->pCallingIrp: 0), bSendZeroLength)
               );
            }
            ntStatus = IoCallDriver(pDevExt->StackDeviceObject,pIrp);
            if(ntStatus != STATUS_PENDING)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_CRITICAL,
                  ("<%s> MWT: IoCallDriver rtn 0x%x", pDevExt->PortName, ntStatus)
               );
            }
         } // end if WriteHead
         else
         {
            QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
            break;  // exit loop
         } // else Writehead
      } // While 

wait_for_completion:
      QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
      if (IsListEmpty(&pDevExt->MWritePendingQueue)    &&
          IsListEmpty(&pDevExt->MWriteCompletionQueue) &&
          (bCancelled == TRUE))
      {
         QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> MWT: no act Qs, exit", pDevExt->PortName)
         );
         // if nothings active and we're cancelled, bail
         break; // goto exit_WriteThread; 
      }
      else
      {
         i = IsListEmpty(&pDevExt->MWriteCompletionQueue);
         // if (!i)
         // {
            // QCSER_DbgPrint
            // (
            //    QCSER_DBG_MASK_WRITE,
            //    QCSER_DBG_LEVEL_ERROR,
            //    ("<%s> MWT: direct completion, fake evt", pDevExt->PortName)
            // );
            // ntStatus = WRITE_EVENT_COUNT + 1;
            // QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
            // goto process_event;
         // }
         QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> MWT: Qs act %d, wait", pDevExt->PortName, reqSent)
         );
      }

      // No matter what IoCallDriver returns, we always wait on the kernel event
      // we created earlier. Our completion routine will gain control when the IRP
      // completes to signal this event. -- Walter Oney's WDM book page 228
// wait_for_completion:
      // wait for action

      if (bFirstTime == TRUE)
      {
         bFirstTime = FALSE;
         KeSetEvent(&pDevExt->WriteThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      }

      // if nothing in the queue, we just wait for a KICK event
      delayValue.QuadPart = -(50 * 1000 * 1000); // 5.0 sec
      ntStatus = KeWaitForMultipleObjects
                 (
                    waitCount,
                    (VOID **) &pDevExt->pWriteEvents,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE, // non-alertable // TRUE,
                    &delayValue,
                    pwbArray
                 );

process_event:

      switch(ntStatus)
      {
         case STATUS_TIMEOUT:
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> MWT: _TIMEOUT: ST %u/%u CompQ %u\n",
                 pDevExt->PortName, mwtState, pDevExt->bWriteStopped, (!i))
            );
            break;
         }
         case QC_DUMMY_EVENT_INDEX:
         {
            KeClearEvent(&dummyEvent);
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> MW: DUMMY_EVENT\n", pDevExt->PortName)
            );
            goto wait_for_completion;
         }

         case WRITE_COMPLETION_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->WriteCompletionEvent);

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> MWT: error - unsupported COMPLETION\n", pDevExt->PortName)
            );
            break;
         }
         
         case WRITE_PRE_TIMEOUT_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->WritePreTimeoutEvent);
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> MWT WRITE_PRE_TIMEOUT\n", pDevExt->PortName)
            );

            QCWT_ProcessPreTimeoutIOB(pDevExt);
            break;
         }

         case KICK_WRITE_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->KickWriteEvent);

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> MWT KICK\n", pDevExt->PortName)
            );
            continue;
         }

         case WRITE_CANCEL_CURRENT_EVENT_INDEX:
         {
            PQCMWT_CXLREQ pCxlReq;
            PQCMWT_BUFFER pWtBuf;

            KeClearEvent(&pDevExt->CancelCurrentWriteEvent);
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> MW CANCEL_CURRENT-0", pDevExt->PortName)
            );

            QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
            while (!IsListEmpty(&pDevExt->MWriteCancellingQueue))
            {
               headOfList = RemoveHeadList(&pDevExt->MWriteCancellingQueue);
               pCxlReq = CONTAINING_RECORD
                         (
                            headOfList,
                            QCMWT_CXLREQ,
                            List
                         );
               pWtBuf = pDevExt->pMwtBuffer[pCxlReq->Index];
               if (pWtBuf->State == MWT_BUF_PENDING)
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_WRITE,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> MW CANCEL[%d]", pDevExt->PortName, pCxlReq->Index)
                  );
                  // reset the index field (to unused)
                  // pCxlReq->Index = -1;

                  QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                  IoCancelIrp(pWtBuf->Irp);

                  QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_WRITE,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> MW CANCEL[%d] - done elsewhere", pDevExt->PortName, pCxlReq->Index)
                  );
                  pCxlReq->Index = -1;
               }
            }
            QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> MW CANCEL_CURRENT-1", pDevExt->PortName)
            );
            break;

         }
         case CANCEL_EVENT_INDEX:
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> MWT CANCEL-0 mwt %u", pDevExt->PortName, mwtState)
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

            QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
            if (!IsListEmpty(&pDevExt->MWritePendingQueue))
            {
               PLIST_ENTRY   firstEntry;

               mwtState = MWT_STATE_CANCELLING;
               headOfList = &pDevExt->MWritePendingQueue;
               firstEntry = headOfList->Flink;
               pWtBuf = CONTAINING_RECORD(firstEntry, QCMWT_BUFFER, List); 

               // Examine the buffer state because it could be cancelled before
               // being sent to the bus driver.
               if (pWtBuf->State == MWT_BUF_PENDING)
               {
                  // cancel the first outstanding irp
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_WRITE,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> MWT: CANCEL - IRP 0x%p\n", pDevExt->PortName, pWtBuf->Irp)
                  );

                  QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                  IoCancelIrp(pWtBuf->Irp);
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_WRITE,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> MWT: CANCEL err - IRP[%d] 0x%p not pending\n",
                     pDevExt->PortName, pWtBuf->Index, pWtBuf->Irp)
                  );
                  QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
               }

               goto wait_for_completion;
            }
            else
            {
               QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
            }

            bCancelled = TRUE;

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> MWT CANCEL-1", pDevExt->PortName)
            );
            break; // goto exit_WriteThread; // nothing active exit
         }
         case WRITE_PURGE_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->WritePurgeEvent);

            // this should not happen
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> MWT: error - PURGE_EVENT", pDevExt->PortName)
            );

            break;
         }

         case WRITE_STOP_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->WriteStopEvent);
            pDevExt->bWriteStopped = TRUE;

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> MWT STOP mwt %u", pDevExt->PortName, mwtState)
            );
            QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
            if (!IsListEmpty(&pDevExt->MWritePendingQueue))
            {
               PLIST_ENTRY   firstEntry;

               mwtState = MWT_STATE_STOPPING;
               headOfList = &pDevExt->MWritePendingQueue;
               firstEntry = headOfList->Flink;
               pWtBuf = CONTAINING_RECORD(firstEntry, QCMWT_BUFFER, List);

               if (pWtBuf->State == MWT_BUF_PENDING)
               {
                  // cancel the first outstanding irp
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_WRITE,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> MWT: STOP - IRP[%d] 0x%p\n",
                      pDevExt->PortName, pWtBuf->Index, pWtBuf->Irp)
                  );
                  QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                  IoCancelIrp(pWtBuf->Irp);
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_WRITE,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> MWT: STOP err - IRP[%d] 0x%p not pending\n",
                      pDevExt->PortName, pWtBuf->Index, pWtBuf->Irp)
                  );
                  QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
               }

               goto wait_for_completion;
            }
            else
            {
               QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
            }

            KeSetEvent(&pDevExt->WriteStopAckEvent, IO_NO_INCREMENT, FALSE);

            break;
         }

         case WRITE_RESUME_EVENT_INDEX:
         {
            KeClearEvent(&pDevExt->WriteResumeEvent);
            if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> MWT: - Resume: dev removed , no act\n", pDevExt->PortName)
               );
            }
            if (TRUE == (pDevExt->bWriteStopped = pDevExt->PowerSuspended))
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> MWT: - Resume in suspend mode, no act\n", pDevExt->PortName)
               );
               goto wait_for_completion;
            }

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> MWT: - Resume act %d\n", pDevExt->PortName, pDevExt->bWriteActive)
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

         #ifdef QCUSB_FC

         case WRITE_FLOW_ON_EVENT_INDEX:
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> W FLOW_ON_EVENT rm %d", pDevExt->PortName, pDevExt->bDeviceRemoved)
            );
            KeClearEvent(&pDevExt->WriteFlowOnEvent);

            // pDevExt->bWriteStopped = pDevExt->PowerSuspended;

            if (bSendZeroLength == FALSE)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> W FLOW_ON: no 0-len, no act", pDevExt->PortName)
               );
            }
            else
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> W FLOW_ON: resend 0-len, not to reset OUT", pDevExt->PortName)
               );
            }
            bFlowOn = TRUE;

            break;
         }  // WRITE_FLOW_ON_EVENT_INDEX

         case WRITE_FLOW_OFF_EVENT_INDEX:
         {
            BOOLEAN bAbortPipe = FALSE;

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> W FLOW_OFF_EVENT: pending %d", pDevExt->PortName,
                 pDevExt->MWTPendingCnt)
            );
            KeClearEvent(&pDevExt->WriteFlowOffEvent);

            if (mwtState != MWT_STATE_WORKING)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> W FLOW_OFF: state not working %u, no act",
                    pDevExt->PortName, mwtState)
               );
               KeSetEvent(&pDevExt->WriteFlowOffAckEvent, IO_NO_INCREMENT, FALSE);
               break;
            }
            pDevExt->bWriteStopped = TRUE;
            bFlowOn = FALSE;
            mwtState = MWT_STATE_FLOW_OFF;

            QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
            if (!IsListEmpty(&pDevExt->MWritePendingQueue))
            {
               bAbortPipe = TRUE;
            }
            else
            {
               if (IsListEmpty(&pDevExt->MWTSentIrpQueue))
               {
                  mwtState = MWT_STATE_WORKING;
               }
               pDevExt->bWriteStopped = pDevExt->PowerSuspended;
               KeSetEvent(&pDevExt->WriteFlowOffAckEvent, IO_NO_INCREMENT, FALSE);
            }
            QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

            if (bAbortPipe == TRUE)
            {
               QCUSB_AbortOutput(pDevExt->MyDeviceObject);
            }
            else
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> W FLOW_OFF: WPendingQ empty, no cancellation", pDevExt->PortName)
               );
            }
            break;
         }  // WRITE_FLOW_OFF_EVENT_INDEX

         case WRITE_TIMEOUT_COMPLETION_EVENT_INDEX:
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> MW TIMEOUT_COMPLETION: pending %d", pDevExt->PortName,
                 pDevExt->MWTPendingCnt)
            );
            KeClearEvent(&pDevExt->WriteTimeoutCompletionEvent);

            // complete the timeout IRP in SentIrpQueue
            QCMWT_TimeoutCompletion(pDevExt);
            break;
         }

         #endif // QCUSB_FC

         default:
         {
            int           idx;
            PQCMWT_BUFFER pWtBuf;
            PLIST_ENTRY   peekEntry;

            if ((ntStatus < WRITE_EVENT_COUNT) || (ntStatus >= waitCount))
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> MWT: default sig 0x%x", pDevExt->PortName, ntStatus)
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
            }

            // Multi-write completion

            // reset write completion event

            QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

            // Sanity checking
            if (IsListEmpty(&pDevExt->MWriteCompletionQueue))
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> MWT: error-empty completion queue", pDevExt->PortName)
               );
               QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
               break;
            }
           
            // Peek the completed entry, do not de-queue here
            headOfList = &pDevExt->MWriteCompletionQueue;
            peekEntry = headOfList->Flink;
            pWtBuf = CONTAINING_RECORD(peekEntry, QCMWT_BUFFER, List);
            KeClearEvent(&pWtBuf->WtCompletionEvt);
            idx = pWtBuf->Index;
            pIrp = pWtBuf->Irp;
            pUrb = &pWtBuf->Urb;
            pDevExt->bWriteActive = (!IsListEmpty(&pDevExt->MWritePendingQueue));
            pDevExt->CxlRequest[pWtBuf->Index].Index = -1;  // reset
            QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

            if (pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength % pDevExt->wMaxPktSize != 0)
            {
               // if transferred short packet
               bytesSent = 0;
            }
            else
            {
               bytesSent += pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;
            }

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> MWT: COMP[%d/%d] 0x%p (0x%x) act %d snt0 %uB <%u/%u>", pDevExt->PortName, idx,
                 (ntStatus-WRITE_EVENT_COUNT),
                 (pWtBuf->IoBlock != NULL) ? pWtBuf->IoBlock->pCallingIrp : 0, 
                 pIrp->IoStatus.Status, pDevExt->bWriteActive, bytesSent,
                 pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lAllocatedWrites)
            );
            // check completion status

            ntStatus = pIrp->IoStatus.Status;

            // Reset pipe if halt, which runs at PASSIVE_LEVEL
            if ((ntStatus == STATUS_DEVICE_DATA_ERROR) ||
                (ntStatus == STATUS_DEVICE_NOT_READY)  ||
                (ntStatus == STATUS_UNSUCCESSFUL))
            {
               if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
               {
                  if (mwtState == MWT_STATE_WORKING)
                  {
                     QCSER_DbgPrint
                     (
                        QCSER_DBG_MASK_WRITE,
                        QCSER_DBG_LEVEL_ERROR,
                        ("<%s> MWT: Waiting pipe OUT 0x%x", pDevExt->PortName, ntStatus)
                     );
                     KeSetEvent
                     (
                        &(DeviceInfo[pDevExt->MgrId].DspDeviceResetOUTEvent),
                        IO_NO_INCREMENT,
                        FALSE
                     );
                     QCSER_Wait(pDevExt, -(500 * 1000L)); // 50ms
                  }
                  else
                  {
                     QCSER_DbgPrint
                     (
                        QCSER_DBG_MASK_WRITE,
                        QCSER_DBG_LEVEL_ERROR,
                        ("<%s> MWT: comp error in non-working state %u/0x%x",
                          pDevExt->PortName, mwtState, ntStatus)
                     );
                  }
               }
            }

            pCurrIOBlock = pWtBuf->IoBlock;

            if (pCurrIOBlock == NULL)
            {
               // need to handle here
               if (!NT_SUCCESS(ntStatus))
               {
                  // failure sending 0-len, flag it and resend
                  bSendZeroLength = TRUE;
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_WRITE,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> MWT: error[%d] 0-len send failure\n", pDevExt->PortName, idx)
                  );
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_WRITE,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> MWT: completed[%d] 0-len \n", pDevExt->PortName, idx)
                  );
               }
               goto mwt_completion_episode;
            }

            // subtract the bytes transfered from the requested bytes
            ulTransferBytes =
               pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;

            pCurrIOBlock->ulActiveBytes -= ulTransferBytes;
            pDevExt->pPerfstats->TransmittedCount += ulTransferBytes;

            if (ntStatus == STATUS_SUCCESS)
            {
               // have we written the full request yet?
               if (pCurrIOBlock->ulActiveBytes > 0)
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_WRITE,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> MWT: error[%d] - TX'ed %u/%uB\n", pDevExt->PortName, idx,
                       ulTransferBytes, pWtBuf->Length)
                  );
               }

               #ifdef ENABLE_LOGGING
               if ((pDevExt->EnableLogging == TRUE) && (pDevExt->hTxLogFile != NULL))
               {
                  QCSER_LogData
                  (
                     pDevExt,
                     pDevExt->hTxLogFile,
                     pCurrIOBlock->pBufferToDevice,
                     ulTransferBytes,
                     QCSER_LOG_TYPE_WRITE
                  );
               }
               // to debug output
               QCUTIL_PrintBytes
               (
                  pCurrIOBlock->pBufferToDevice,
                  128,
                  ulTransferBytes,
                  "TxData",
                  pDevExt,
                  QCSER_DBG_MASK_TDATA,
                  QCSER_DBG_LEVEL_DETAIL
               );
               #endif // ENABLE_LOGGING

            } // if STATUS_SUCCESS
            else // error???
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> MWT: TX failure[%u] 0x%x xferred %u/%u", pDevExt->PortName, idx,
                    ntStatus, pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength,
                    pWtBuf->Length)
               );

               #ifdef ENABLE_LOGGING
               if ((pDevExt->EnableLogging == TRUE) && (pDevExt->hTxLogFile != NULL))
               {
                  if (ulTransferBytes > 0)
                  {
                     PCHAR dataBuf = (PCHAR)pCurrIOBlock->pBufferToDevice;

                     if (pCurrIOBlock->pCallingIrp != NULL)
                     {
                        dataBuf += pCurrIOBlock->pCallingIrp->IoStatus.Information;
                     }
                     QCSER_LogData
                     (
                        pDevExt,
                        pDevExt->hTxLogFile,
                        dataBuf,
                        ulTransferBytes,
                        QCSER_LOG_TYPE_WRITE
                     );
                  }
                  QCSER_LogData
                  (
                     pDevExt,
                     pDevExt->hTxLogFile,
                     (PVOID)&ntStatus,
                     sizeof(NTSTATUS),
                     QCSER_LOG_TYPE_RESPONSE_WT
                  );
               }
               #endif // ENABLE_LOGGING
            }

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

            mwt_completion_episode:

            #ifdef QCUSB_FC

            QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

            if (mwtState == MWT_STATE_FLOW_OFF)
            {
               BOOLEAN bAllReturned;

               // Mark the returned IRP
               // If all returned, put the IRP(s) back to the data queue
               // for re-transmission

               bAllReturned = USBMWT_MarkAndCheckReturnedIrp(pDevExt, pWtBuf, FALSE, mwtState, ntStatus);
               if (bAllReturned == TRUE)
               {
                  PLIST_ENTRY irpReturned;
                  PQCUSB_MWT_SENT_IRP irpRec;
                  PLIST_ENTRY headOfList, peekEntry;

                  // First, examine the head to see if it's a 0-len packet
                  if (!IsListEmpty(&pDevExt->MWTSentIrpQueue))
                  {
                     headOfList = &pDevExt->MWTSentIrpQueue;
                     peekEntry  = headOfList->Flink;
                     irpRec = CONTAINING_RECORD(peekEntry, QCUSB_MWT_SENT_IRP, List);
                     if (irpRec->SentIrp == NULL)
                     {
                        if (IsListEmpty(&pDevExt->MWritePendingQueue) &&
                            (pDevExt->pWriteHead == NULL))
                        {
                           bSendZeroLength = TRUE;
                        }
                        else
                        {
                           bSendZeroLength = FALSE;
                        }

                        QCSER_DbgPrint
                        (
                           QCSER_DBG_MASK_WRITE,
                           QCSER_DBG_LEVEL_DETAIL,
                           ("<%s> MWT: flow off, 0-len detected[%u] 0-len:%u\n",
                             pDevExt->PortName, irpRec->MWTBuf->Index, bSendZeroLength)
                        );
                     }
                     else
                     {
                        if ((irpRec->SentIrp->IoStatus.Information > 0) &&
                            (irpRec->TotalLength > irpRec->SentIrp->IoStatus.Information))
                        {
                           // It's possible that bSendZeroLength has been assigned TRUE
                           // at this point because the last data IRP in the SentIrpQueue
                           // contains data of multiple of 64/MAX-USB-TRANS bytes. In such
                           // a case, we need to reset bSendZeroLength when the first IRP
                           // was partially transferred.
                           if (bSendZeroLength == TRUE)
                           {
                              bSendZeroLength = FALSE;
                              QCSER_DbgPrint
                              (
                                 QCSER_DBG_MASK_WRITE,
                                 QCSER_DBG_LEVEL_DETAIL,
                                 ("<%s> MWT: flow off, partial transfer, reset 0-len flag\n",
                                   pDevExt->PortName)
                              );
                           }
                        }
                     }
                  } // if (!IsListEmpty(&pDevExt->MWTSentIrpQueue))

                  while (!IsListEmpty(&pDevExt->MWTSentIrpQueue))
                  {
                     PIRP pIrp;
                     PVXD_WDM_IO_CONTROL_BLOCK pIoBlock;

                     irpReturned =  RemoveTailList(&pDevExt->MWTSentIrpQueue);
                     irpRec = CONTAINING_RECORD(irpReturned, QCUSB_MWT_SENT_IRP, List);
                     pIrp = irpRec->SentIrp;
                     pIoBlock = irpRec->IOB;
                     if (pIrp != NULL)
                     {
                        if (pIoBlock != NULL)
                        {
                           QCSER_DbgPrint
                           (
                              QCSER_DBG_MASK_WRITE,
                              QCSER_DBG_LEVEL_DETAIL,
                              ("<%s> MWT: flow_off, re-q IRP[%d] 0x%p/0x%p(%uB/%uB)\n",
                                pDevExt->PortName, irpRec->MWTBuf->Index, pIrp, pIoBlock,
                                pIoBlock->ulActiveBytes, irpRec->TotalLength)
                           );
                           pIoBlock->pNextEntry = pDevExt->pWriteHead;
                           pDevExt->pWriteHead = pIoBlock;
                           pIrp->IoStatus.Information = irpRec->TotalLength - pIoBlock->ulActiveBytes;
                        }
                        else
                        {
                           // This is a 0-len IOB
                           QCSER_DbgPrint
                           (
                              QCSER_DBG_MASK_WRITE,
                              QCSER_DBG_LEVEL_CRITICAL,
                              ("<%s> MWT: flow_off, no re-q 0-len, recycle sndRec[%d]\n",
                                pDevExt->PortName, irpRec->MWTBuf->Index)
                           );
                        }
                        irpRec->SentIrp = NULL;
                     }
                     else
                     {
                        QCSER_DbgPrint
                        (
                           QCSER_DBG_MASK_WRITE,
                           QCSER_DBG_LEVEL_DETAIL,
                           ("<%s> MWT: flow_off, NUL irp, recycle sndRec[%d]\n",
                             pDevExt->PortName, irpRec->MWTBuf->Index)
                        );
                     }
                     // recycle the SentIrpRecord
                     irpRec->IOB    = NULL;
                     irpRec->MWTBuf = NULL;
                     InsertTailList(&pDevExt->MWTSentIrpRecordPool, &irpRec->List);
                  }
               }
               QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
            } // if ((mwtState == MWT_STATE_FLOW_OFF) && (!NT_SUCCESS(ntStatus)))
            else
            {
               // mark and remove the returned sent-irp
               USBMWT_MarkAndCheckReturnedIrp(pDevExt, pWtBuf, TRUE, mwtState, ntStatus);
               QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

               if (pCurrIOBlock != NULL)
               {
                  // complete the IRP
                  pCurrIOBlock->ntStatus = ntStatus;
                  pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, TRUE, 7);
                  QcExFreeWriteIOB(pCurrIOBlock, TRUE);
               }
            }
            // pActiveBuffer   = NULL;
            ulTransferBytes = 0;

            pWtBuf->Length     = 0;
            pWtBuf->IoBlock    = NULL;

            #else // QCUSB_FC

            if (pCurrIOBlock != NULL)
            {
               pCurrIOBlock->ntStatus = ntStatus;
               pCurrIOBlock->pCompletionRoutine(pCurrIOBlock, TRUE, 7);
               QcExFreeWriteIOB(pCurrIOBlock, TRUE);
            }

            pWtBuf->Length     = 0;
            pWtBuf->IoBlock    = NULL;

            #endif // QCUSB_FC

            // De-queue, completion => idle
            QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
            RemoveEntryList(&pWtBuf->List);
            InsertTailList(&pDevExt->MWriteIdleQueue, &pWtBuf->List);

            // State change: completed => idle
            pWtBuf->State = MWT_BUF_IDLE;


            #ifdef QCUSB_FC
            if (mwtState == MWT_STATE_FLOW_OFF)
            {
               if (IsListEmpty(&pDevExt->MWTSentIrpQueue))
               {
                  mwtState = MWT_STATE_WORKING;
                  pDevExt->bWriteStopped = pDevExt->PowerSuspended;
                  KeSetEvent(&pDevExt->WriteFlowOffAckEvent, IO_NO_INCREMENT, FALSE);
               }
               QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

               // Since AbortPipe is called, we need to exit here
               break;
            }
            #endif // QCUSB_FC

            if (mwtState != MWT_STATE_WORKING)
            {
               if (!IsListEmpty(&pDevExt->MWritePendingQueue))
               {
                  PLIST_ENTRY   firstEntry;

                  headOfList = &pDevExt->MWritePendingQueue;
                  firstEntry = headOfList->Flink;
                  pWtBuf = CONTAINING_RECORD(firstEntry, QCMWT_BUFFER, List);

                  // cancel the first outstanding irp
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_WRITE,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> MWT_comp: CANCEL - IRP 0x%p\n", pDevExt->PortName, pWtBuf->Irp)
                  );
                  QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

                  IoCancelIrp(pWtBuf->Irp);

                  goto wait_for_completion;
               }
               else
               {
                  if (mwtState == MWT_STATE_STOPPING)
                  {
                     KeSetEvent(&pDevExt->WriteStopAckEvent, IO_NO_INCREMENT, FALSE);
                  }
                  else if (mwtState == MWT_STATE_CANCELLING)
                  {
                     bCancelled = TRUE;
                  }
                  mwtState = MWT_STATE_WORKING;
               }
            }
            QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

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

   if(pwbArray)
   {
      _ExFreePool(pwbArray);
   }

   // Empty queue so that items can be added next time
   QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
   while (!IsListEmpty(&pDevExt->MWTSentIrpRecordPool))
   {
      irpSent = RemoveHeadList(&pDevExt->MWTSentIrpRecordPool);
   }
   QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

   KeSetEvent
   (
      &pDevExt->WriteThreadClosedEvent,
      IO_NO_INCREMENT,
      FALSE
   ); // signal write thread closed

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_ERROR,
      ("<%s> MWT: term <%d, %d/%d>\n", pDevExt->PortName, pDevExt->Sts.lRmlCount[2],
       pDevExt->Sts.lAllocatedWrites, pDevExt->Sts.lAllocatedWtMem)
   );

   _closeHandle(pDevExt->hWriteThreadHandle, "W-6");
   PsTerminateSystemThread(STATUS_SUCCESS); // end this thread
}  // QCMWT_WriteThread

NTSTATUS QCMWT_InitializeMultiWriteElements(PDEVICE_EXTENSION pDevExt)
{
   int           i;
   PQCMWT_BUFFER pWtBuf;

   if (pDevExt->bFdoReused == TRUE)
   {
      return STATUS_SUCCESS;
   }

   InitializeListHead(&pDevExt->MWriteIdleQueue);
   InitializeListHead(&pDevExt->MWritePendingQueue);
   InitializeListHead(&pDevExt->MWriteCompletionQueue);
   InitializeListHead(&pDevExt->MWriteCancellingQueue);

   pDevExt->NumberOfMultiWrites = QCUSB_MULTI_WRITE_BUFFERS; //must < QCUSB_MAX_MRW_BUF_COUNT

   for (i = 0; i < pDevExt->NumberOfMultiWrites; i++)
   {
      pWtBuf = ExAllocatePool(NonPagedPool, sizeof(QCMWT_BUFFER));
      if (pWtBuf == NULL)
      {
         goto MWT_InitExit;
      }
      else
      {
         pWtBuf->IoBlock = NULL;
         pWtBuf->Irp = IoAllocateIrp
                       (
                          (CCHAR)(pDevExt->MyDeviceObject->StackSize),
                          FALSE
                       );
         if (pWtBuf->Irp == NULL)
         {
            ExFreePool(pWtBuf);
            goto MWT_InitExit;
         }

         pDevExt->pMwtBuffer[i]  = pWtBuf;
         pWtBuf->DeviceExtension = pDevExt;
         pWtBuf->Length          = 0;
         pWtBuf->Index           = i;
         pWtBuf->State           = MWT_BUF_IDLE;
         KeInitializeEvent
         (
            &pWtBuf->WtCompletionEvt,
            NotificationEvent, FALSE
         );

         InsertTailList(&pDevExt->MWriteIdleQueue, &pWtBuf->List);
         pDevExt->pWriteEvents[WRITE_EVENT_COUNT+i] = &(pWtBuf->WtCompletionEvt);
      }
   }

   // Initialize elements for MWT req to be cancelled
   for (i = 0; i < QCUSB_MAX_MRW_BUF_COUNT; i++)
   {
      pDevExt->CxlRequest[i].Index = -1;
   }

   return STATUS_SUCCESS;

MWT_InitExit:

   pDevExt->NumberOfMultiWrites = i;

   if (i == 0)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> MWT-init: ERR - NO_MEMORY\n", pDevExt->PortName)
      );
      return STATUS_NO_MEMORY;
   }
   else
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> MWT-init: WRN - degrade to %d\n", pDevExt->PortName, i)
      );
      return STATUS_SUCCESS;
   }

}  // QCMWT_InitializeMultiWriteElements

// This function must be called inside WriteSpinLock
PQCMWT_BUFFER QCMWT_IsIrpPending
(
   PDEVICE_EXTENSION pDevExt,
   PIRP              Irp,
   BOOLEAN           EnqueueToCancel
)
{
   PLIST_ENTRY     headOfList, peekEntry;
   PQCMWT_BUFFER   pWtBuf = NULL;
   BOOLEAN         enqueueResult = FALSE;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> -->QCMWT_IsIrpPending 0x%p - %d\n", pDevExt->PortName,
        Irp, EnqueueToCancel)
   );

   // Examine MWritePendingQueue
   if (!IsListEmpty(&pDevExt->MWritePendingQueue))
   {
      headOfList = &pDevExt->MWritePendingQueue;
      peekEntry = headOfList->Flink;

      while (peekEntry != headOfList)
      {
         pWtBuf = CONTAINING_RECORD
                  (
                     peekEntry,
                     QCMWT_BUFFER,
                     List
                  );
         if (pWtBuf->IoBlock != NULL)
         {
            if ((pWtBuf->IoBlock->pCallingIrp == Irp) && (pWtBuf->State == MWT_BUF_PENDING))
            {
               break;
            }
         }
         peekEntry = peekEntry->Flink;
         pWtBuf = NULL;  // bug fix, need to reset here
      }
   }

   if ((pWtBuf != NULL) && (EnqueueToCancel == TRUE))
   {
      if (pDevExt->CxlRequest[pWtBuf->Index].Index < 0)
      {
         pDevExt->CxlRequest[pWtBuf->Index].Index = pWtBuf->Index;
         InsertTailList
         (
            &pDevExt->MWriteCancellingQueue,
            &(pDevExt->CxlRequest[pWtBuf->Index].List)
         );
         KeSetEvent(&pDevExt->CancelCurrentWriteEvent,IO_NO_INCREMENT,FALSE);
         enqueueResult = TRUE;
      }

      if (enqueueResult == FALSE)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> MWT-IsIrpPending: failed to enQ %d\n", pDevExt->PortName, pWtBuf->Index)
         );
      }
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> <--QCMWT_IsIrpPending 0x%p - %d (0x%p)\n", pDevExt->PortName,
        Irp, EnqueueToCancel, pWtBuf)
   );

   return pWtBuf;
}  // QCMWT_IsIrpPending

VOID QCMWT_CancelWriteRoutine(PDEVICE_OBJECT CalledDO, PIRP pIrp)
{
   KIRQL irql = KeGetCurrentIrql();
   PDEVICE_OBJECT DeviceObject = QCPTDO_FindPortDOByFDO(CalledDO, irql);
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   BOOLEAN bFreeIOBlock = FALSE;
   PQCMWT_BUFFER pWtBuf = NULL;
   BOOLEAN bQueuedForCompletion = FALSE;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WIRP,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> -->MWT_CancelWriteRoutine IRP 0x%p\n", pDevExt->PortName, pIrp)
   );
   IoReleaseCancelSpinLock(pIrp->CancelIrql);

   QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);

   // try to remove it from IOB queue
   pIOBlock = FindWriteIrp(pDevExt, pIrp, TRUE); // search IOB queue only

   if (pIOBlock == NULL)
   {
      // search the MWT pending queue
      pWtBuf = QCMWT_IsIrpPending(pDevExt, pIrp, FALSE);

      if (pWtBuf == NULL)
      {
         PQCMWT_BUFFER pCompletedWtBuf;

         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WIRP,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> MWT_CxlW: IRP 0x%p not pending, chk completionQ\n", pDevExt->PortName, pIrp)
         );

         // Is IRP in the completion queue?
         pCompletedWtBuf = QCMWT_IsIrpBeingCompleted(pDevExt, pIrp);
         if (pCompletedWtBuf != NULL)
         {
            // This means it's in completion queue but before being de-queued, either
            // because WriteIrpCompletion has finished or it's to be completed.
            // So, need to notify WriteIrpCompletion or whoever that the IRP
            // is completed here. In this case, the IoBlock member cannot be NULL.
            pIOBlock = pCompletedWtBuf->IoBlock;
            pIOBlock->pCallingIrp = NULL;
            pIrp->IoStatus.Information = pIOBlock->ulBTDBytes - pIOBlock->ulActiveBytes;
            pIrp->IoStatus.Status = pIOBlock->ntStatus = pCompletedWtBuf->Irp->IoStatus.Status;
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> MWT_CxlW: IRP under completion 0x%p(%uB) 0x%x\n", pDevExt->PortName,
                 pIrp, pIrp->IoStatus.Information, pIrp->IoStatus.Status)
            );
         }
         else
         {
            pIrp->IoStatus.Information = 0;
            pIrp->IoStatus.Status = STATUS_CANCELLED;
         }

         // SR392475
         InsertTailList(&pDevExt->WtCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
         KeSetEvent(&pDevExt->InterruptEmptyWtQueueEvent, IO_NO_INCREMENT, FALSE);
         // End of SR392475

         bQueuedForCompletion = TRUE;
      }
      else
      {
         pIOBlock = pWtBuf->IoBlock;

 
         // Check if the IRP has been queued for cancellation - is it possible
         // that this cancel routine could be called more than once on a same IRP?
         // Could this be true after we restore the IRP cancel routine later in this
         // function? Anyway, if the IRP has already been sent to the cancel
         // queue, no action is needed. However, we have not seen this scenario
         // so far.
         if (pDevExt->CxlRequest[pWtBuf->Index].Index >= 0)
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WIRP,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> MWT_CxlW: IRP 0x%p[%d] already in cxl Q\n", pDevExt->PortName,
                pIrp, pDevExt->CxlRequest[pWtBuf->Index].Index)
            );

            // Restore the cancel routine and let write thread complete the IRP
            IoSetCancelRoutine(pIrp, QCMWT_CancelWriteRoutine); 
            QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WIRP,
               QCSER_DBG_LEVEL_TRACE,
               ("<%s> <--MWT_CancelWriteRoutine IRP 0x%p\n", pDevExt->PortName, pIrp)
            );
            return;
         }
      }
   }

   if (pIOBlock != NULL)
   {
      // If the IRP is not in the MWT pending queue
      if (pWtBuf == NULL)
      {
         // case 1: IRP in IOB queue
         bFreeIOBlock = DeQueueIOBlock(pIOBlock, &pDevExt->pWriteHead);

         // case 2: IRP just de-queued from IOB queue but will be completed
         //         before being sent down to the bus driver, so it'll not
         //         be on the MWT pending queue either when the write
         //         thread tries to cancel it after this call. We take care
         //         of this case in the write thread by checking the State
         //         member of the item out of MWriteCancellingQueue.
      }
      AbortWriteTimeout(pIOBlock);
      ASSERT(pIOBlock->pCallingIrp==pIrp);

      // If the IRP being cancelled is the pending in the bus driver
      if (pWtBuf != NULL)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WIRP,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> MWT_CxlW: cxl curr W[%d]=0x%p\n", pDevExt->PortName, pWtBuf->Index, pIrp)
         );
         pDevExt->CxlRequest[pWtBuf->Index].Index = pWtBuf->Index;
         InsertTailList
         (
            &pDevExt->MWriteCancellingQueue,
            &(pDevExt->CxlRequest[pWtBuf->Index].List)
         );

         pIOBlock->TimerExpired = TRUE; // pretend to time out
         IoSetCancelRoutine(pIrp, QCMWT_CancelWriteRoutine);  // restore the cxl routine
         KeSetEvent(&pDevExt->CancelCurrentWriteEvent,IO_NO_INCREMENT,FALSE);
         QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WIRP,
            QCSER_DBG_LEVEL_TRACE,
            ("<%s> <--MWT_CancelWriteRoutine IRP 0x%p\n", pDevExt->PortName, pIrp)
         );
         return;
      }

      pIOBlock->pCallingIrp = NULL;

      if (bQueuedForCompletion == FALSE)
      {
         pIrp->IoStatus.Status = STATUS_CANCELLED;
         pIrp->IoStatus.Information = 0;
         InsertTailList(&pDevExt->WtCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
         KeSetEvent(&pDevExt->InterruptEmptyWtQueueEvent, IO_NO_INCREMENT, FALSE);
      }
   }

   if (bFreeIOBlock)
   {
      QcExFreeWriteIOB(pIOBlock, FALSE);
   }
   QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WIRP,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> <--MWT_CancelWriteRoutine IRP 0x%p\n", pDevExt->PortName, pIrp)
   );
}  // QCMWT_CancelWriteRoutine

VOID QCMWT_TimeoutWriteRoutine
(
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock
)
{
   PDEVICE_EXTENSION pDevExt;
   BOOLEAN bFreeIOBlock;
   PIRP pIrp;
   PQCMWT_BUFFER pWtBuf = NULL;

   pDevExt = pIOBlock->pSerialDeviceObject->DeviceExtension;
   pIrp = pIOBlock->pCallingIrp;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WIRP,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> -->QCMWT_TimeoutWriteRoutine IRP 0x%p\n", pDevExt->PortName, pIrp)
   );

   // remove it from pIOBlock
   AbortWriteTimeout(pIOBlock);

   bFreeIOBlock = DeQueueIOBlock(pIOBlock, &pDevExt->pWriteHead);

   if (bFreeIOBlock == FALSE)
   {
      pWtBuf = QCMWT_IsIrpPending(pDevExt, pIrp, FALSE);

      if (pWtBuf != NULL)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WIRP,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> TOMWR: cxl curr W[%d]\n", pDevExt->PortName, pWtBuf->Index)
         );

         pDevExt->CxlRequest[pWtBuf->Index].Index = pWtBuf->Index;
         InsertTailList
         (
            &pDevExt->MWriteCancellingQueue,
            &(pDevExt->CxlRequest[pWtBuf->Index].List)
         );
         KeSetEvent(&pDevExt->CancelCurrentWriteEvent,IO_NO_INCREMENT,FALSE);
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WIRP,
            QCSER_DBG_LEVEL_TRACE,
            ("<%s> <--QCMWT_TimeoutWriteRoutine IRP 0x%p\n", pDevExt->PortName, pIrp)
         );
         return;
      }
   }

   // Two possibilities with the following condition:
   // 1. IOB expired before being queued
   // 2. IOB is in the late stage of completion - just before WriteIrpCompletion
   //    is called. If it's a partial-write completion, then it'll be completed
   //    after re-queued because of timer expiration.
   if ((bFreeIOBlock == FALSE) && (pWtBuf == NULL)) 
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WIRP,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> TOMWR: ERR - stray away iob 0x%p/0x%p\n", pDevExt->PortName, pIrp, pIOBlock)
      );
      // do nothing to the IOB and the IRP in this case
      // the write thread will take care of this type of IOB
      KeSetEvent(&pDevExt->WriteTimeoutCompletionEvent, IO_NO_INCREMENT, FALSE);
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
            ("<%s> WIRP (TO) 0x%p - MWT ERR: cxled already\n", pDevExt->PortName, pIrp)
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
         ("<%s> TOMWR: NUL IRP\n", pDevExt->PortName)
      );
   }

   if (bFreeIOBlock == TRUE)
   {
      QcExFreeWriteIOB(pIOBlock, FALSE);
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WIRP,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> -->QCMWT_TimeoutWriteRoutine IRP 0x%p\n", pDevExt->PortName, pIrp)
   );
}  // QCMWT_TimeoutWriteRoutine

BOOLEAN USBMWT_MarkAndCheckReturnedIrp
(
   PDEVICE_EXTENSION pDevExt,
   PQCMWT_BUFFER     MWTBuf,
   BOOLEAN           RemoveIrp,
   MWT_STATE         OperationState,
   NTSTATUS          Status
)
{
   PLIST_ENTRY         irpSent, headOfList;
   PQCUSB_MWT_SENT_IRP irpBlock, irpReturned = NULL;
   BOOLEAN             bAllReturned = TRUE;
   int                 count = 0;
   PVXD_WDM_IO_CONTROL_BLOCK iob;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> --> MarkAndCheckReturnedIrp[%u]-%d ST %u\n", pDevExt->PortName,
        MWTBuf->Index, RemoveIrp, OperationState)
   );
   // Peek the queue
   headOfList = &pDevExt->MWTSentIrpQueue;
   irpSent = headOfList->Flink;

   while (irpSent != headOfList)
   {
      ++count;
      irpBlock = CONTAINING_RECORD(irpSent, QCUSB_MWT_SENT_IRP, List);
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> MarkAndCheckReturnedIrp: chking [%u, %u] 0x%p/%uB\n", pDevExt->PortName,
           irpBlock->MWTBuf->Index, irpBlock->IrpReturned, irpBlock->SentIrp,
           irpBlock->TotalLength)
      );

      if (irpBlock->MWTBuf == MWTBuf)
      {
         // If it's current returned IRP
         if (irpBlock->IrpReturned == TRUE)
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_CRITICAL,
               ("<%s> MarkAndCheckReturnedIrp: critical - IRP already returned 0x%p, die!\n",
                 pDevExt->PortName, irpBlock->SentIrp)
            );
            // _asm int 3;
         }
         else
         {
            iob = irpBlock->IOB;
            if (iob != NULL)
            {
               iob->ntStatus = irpBlock->MWTBuf->Irp->IoStatus.Status;
            }
            irpBlock->IrpReturned = TRUE;
            irpReturned = irpBlock;
         }
      }
      else
      {
         // If it's not the current returned IRP

         if (irpBlock->IrpReturned != TRUE)
         {
            bAllReturned = FALSE;
         }
         else if (OperationState != MWT_STATE_FLOW_OFF)
         {
            // if already returned, complete the IRP to avoid stray-away IRP.
            irpSent = irpSent->Flink;

            // recycle IrpBlock
            iob = irpBlock->IOB;
            irpBlock->IOB     = NULL;
            irpBlock->SentIrp = NULL;
            irpBlock->MWTBuf  = NULL;
            RemoveEntryList(&irpBlock->List);
            InsertTailList(&pDevExt->MWTSentIrpRecordPool, &irpBlock->List);

            // complete the IOB
            if (iob != NULL)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> MarkAndCheck: ST %u IOB[%d] 0x%p IRP 0x%p (0x%x)\n", pDevExt->PortName,
                    OperationState, count, iob, iob->pCallingIrp, iob->ntStatus)
               );
               iob->pCompletionRoutine(iob, FALSE, 18);
               QcExFreeWriteIOB(iob, FALSE);
            }
            else
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WRITE,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> MarkAndCheck: ST %u IOB[%d] NULL, recycle\n", pDevExt->PortName,
                    OperationState, count)
               );
            }

            continue;
         }
      }

      irpSent = irpSent->Flink;
   }  // while

   if (irpReturned != NULL)
   {
      BOOLEAN completeIrp = FALSE;

      if ((RemoveIrp == TRUE) || (NT_SUCCESS(Status)))
      {
         if (RemoveIrp == FALSE)
         {
            completeIrp = TRUE;
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> MarkAndCheckReturnedIrp: comp under FLOW_OFF: st 0x%x\n",
                 pDevExt->PortName, Status)
            );
         }

         // recycle the element
         irpReturned->IOB     = NULL;
         irpReturned->SentIrp = NULL;
         irpReturned->MWTBuf  = NULL;
         RemoveEntryList(&irpReturned->List);
         InsertTailList(&pDevExt->MWTSentIrpRecordPool, &irpReturned->List);
      }

      if (completeIrp == TRUE)  // should only during FLOW_OFF
      {
         iob = MWTBuf->IoBlock;
         if (iob != NULL)
         {
            iob->ntStatus = Status;
            iob->pCompletionRoutine(iob, FALSE, 19);
            QcExFreeWriteIOB(iob, FALSE);
         }
      }

   }
   else
   {
      // error
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> MarkAndCheckReturnedIrp: error - no returned IRP\n", pDevExt->PortName)
      );
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> <-- MarkAndCheckReturnedIrp: %d\n", pDevExt->PortName, bAllReturned)
   );

   return bAllReturned;

}  // USBMWT_MarkAndCheckReturnedIrp

VOID QCMWT_TimeoutCompletion(PDEVICE_EXTENSION pDevExt)
{
   PLIST_ENTRY         irpSent, headOfList;
   PQCUSB_MWT_SENT_IRP irpBlock, irpReturned = NULL;
   BOOLEAN             bAllReturned = TRUE;
   PVXD_WDM_IO_CONTROL_BLOCK iob;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> --> TimeoutCompletion\n", pDevExt->PortName)
   );

   QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
   // Peek the queue
   headOfList = &pDevExt->MWTSentIrpQueue;
   irpSent = headOfList->Flink;

   while (irpSent != headOfList)
   {
      irpBlock = CONTAINING_RECORD(irpSent, QCUSB_MWT_SENT_IRP, List);
      iob = irpBlock->IOB;

      if (iob != NULL)
      {
         if ((iob->TimerExpired == TRUE) && (irpBlock->IrpReturned == TRUE))
         {
            irpSent = irpSent->Flink;

            // recycle irpBlock
            irpBlock->IOB     = NULL;
            irpBlock->SentIrp = NULL;
            irpBlock->MWTBuf  = NULL;
            RemoveEntryList(&irpBlock->List);
            InsertTailList(&pDevExt->MWTSentIrpRecordPool, &irpBlock->List);

            // complete the IOB
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_WRITE,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> MWT TimeoutCompletion: IOB/IRP 0x%p/0x%p(0x%x)\n", pDevExt->PortName,
                 iob, iob->pCallingIrp, iob->ntStatus)
            );
            iob->pCompletionRoutine(iob, FALSE, 16);
            QcExFreeWriteIOB(iob, FALSE);

            continue;
         }
      }
      irpSent = irpSent->Flink;
   }
   QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> <-- TimeoutCompletion\n", pDevExt->PortName)
   );
}  // QCMWT_TimeoutCompletion

// This function must be called inside WriteSpinLock
PQCMWT_BUFFER QCMWT_IsIrpBeingCompleted
(
   PDEVICE_EXTENSION pDevExt,
   PIRP              Irp
)
{
   PLIST_ENTRY     headOfList, peekEntry;
   PQCMWT_BUFFER   pWtBuf = NULL;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> -->QCMWT_IsIrpBeingCompleted 0x%p\n", pDevExt->PortName, Irp)
   );

   // Examine MWritePendingQueue
   if (!IsListEmpty(&pDevExt->MWriteCompletionQueue))
   {
      headOfList = &pDevExt->MWriteCompletionQueue;
      peekEntry = headOfList->Flink;

      while (peekEntry != headOfList)
      {
         pWtBuf = CONTAINING_RECORD
                  (
                     peekEntry,
                     QCMWT_BUFFER,
                     List
                  );
         if (pWtBuf->IoBlock != NULL)
         {
            if ((pWtBuf->IoBlock->pCallingIrp == Irp) && (pWtBuf->State == MWT_BUF_COMPLETED))
            {
               break;
            }
         }
         peekEntry = peekEntry->Flink;
         pWtBuf = NULL;
      }
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_WRITE,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> <--QCMWT_IsIrpBeingCompleted 0x%p (0x%p)\n", pDevExt->PortName, Irp, pWtBuf)
   );

   return pWtBuf;
}  // QCMWT_IsIrpBeingCompleted

#endif // QCUSB_MULTI_WRITES
