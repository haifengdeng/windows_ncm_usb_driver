/*===========================================================================
FILE: QCINT.c

DESCRIPTION:
   This file contains implementations for reading data from USB device's
   interrupt endpoint.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003-2007 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#include "QCINT.h"
#include "QCUTILS.h"
#include "QCMGR.h"
#include "QCPWR.h"
#include "QCRD.h"

extern NTKERNELAPI VOID IoReuseIrp(IN OUT PIRP Irp, IN NTSTATUS Iostatus);

NTSTATUS QCINT_InitInterruptPipe(IN PDEVICE_OBJECT pDevObj)
{
   NTSTATUS ntStatus;
   USHORT usLength, i;
   PDEVICE_EXTENSION pDevExt;
   PIRP pIrp;
   OBJECT_ATTRIBUTES objAttr;

   pDevExt = pDevObj->DeviceExtension;

// *************** TEST CODE ******************
// if (pDevExt->InterruptPipe == (UCHAR)-1)
// {
//    return STATUS_SUCCESS;
// }
// *************** END OF TEST CODE ******************

   // Make sure the int thread is created at PASSIVE_LEVEL
   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> INT: wrong IRQL::%d\n", pDevExt->PortName, KeGetCurrentIrql())
      );
      return STATUS_UNSUCCESSFUL;
   }

   if ((pDevExt->pInterruptThread != NULL) || (pDevExt->hInterruptThreadHandle != NULL))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> INT up/resumed\n", pDevExt->PortName)
      );
      ResumeInterruptService(pDevExt, 0);
      return STATUS_SUCCESS;
   }
   pDevExt->pInterruptBuffer = NULL;

   // is there an interrupt pipe?
   if (pDevExt -> InterruptPipe != (UCHAR)-1)
   {
      // allocate buffer for interrupt data
      usLength = pDevExt->Interface[pDevExt->usCommClassInterface]
                 ->Pipes[pDevExt->InterruptPipe].MaximumPacketSize;
      pDevExt->pInterruptBuffer = ExAllocatePool
                                  (
                                     NonPagedPool, 
                                     usLength
                                  );
      if(!pDevExt->pInterruptBuffer)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> INT: NO_MEM BUF\n", pDevExt->PortName)
         );
         return STATUS_NO_MEMORY;
      }

      RtlZeroMemory(pDevExt->pInterruptBuffer, usLength);
   }

   // kick off interrupt service thread

   KeClearEvent(&pDevExt->IntThreadStartedEvent);
   InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
   ucHdlCnt++;
   pDevExt->bIntThreadInCreation = TRUE;
   ntStatus = PsCreateSystemThread
              (
                 OUT &pDevExt->hInterruptThreadHandle,
                 IN THREAD_ALL_ACCESS,
                 IN &objAttr,         // POBJECT_ATTRIBUTES
                 IN NULL,             // HANDLE  ProcessHandle
                 OUT NULL,            // PCLIENT_ID  ClientId
                 IN (PKSTART_ROUTINE)ReadInterruptPipe,
                 IN (PVOID) pDevExt
              );			
   if ((!NT_SUCCESS(ntStatus)) || (pDevExt->hInterruptThreadHandle == NULL))
   {
      pDevExt->hInterruptThreadHandle = NULL;
      pDevExt->pInterruptThread = NULL;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> INT th failure\n", pDevExt->PortName)
      );
      return ntStatus;
   }

   ntStatus = KeWaitForSingleObject
              (
                 &pDevExt->IntThreadStartedEvent,
                 Executive,
                 KernelMode,
                 FALSE,
                 NULL
              );
   if (pDevExt->bIntThreadInCreation == FALSE)
   {
      pDevExt->hInterruptThreadHandle = NULL;
      pDevExt->pInterruptThread = NULL;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> INT th failure 02\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   ntStatus = ObReferenceObjectByHandle
              (
                 pDevExt->hInterruptThreadHandle,
                 THREAD_ALL_ACCESS,
                 NULL,
                 KernelMode,
                 (PVOID*)&pDevExt->pInterruptThread,
                 NULL
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> INT: ObReferenceObjectByHandle failed 0x%x\n", pDevExt->PortName, ntStatus)
      );
      pDevExt->pInterruptThread = NULL;
   }
   else
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> INT handle=0x%p thOb=0x%p\n", pDevExt->PortName,
          pDevExt->hInterruptThreadHandle, pDevExt->pInterruptThread)
      );
   
      _closeHandle(pDevExt->hInterruptThreadHandle, "I-7");
   }

   pDevExt->bIntThreadInCreation = FALSE;

   return ntStatus;
}

VOID ReadInterruptPipe(IN PVOID pContext)
{
   USHORT siz;
   PURB pUrb = NULL;
   USBD_STATUS urb_status;
   PIRP pIrp = NULL;
   PIO_STACK_LOCATION nextstack;
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pContext;
   NTSTATUS ntStatus, ntStatus2;
   PUSB_NOTIFICATION_STATUS pNotificationStatus;
   KIRQL IrqLevel;
   BOOLEAN bKeepRunning = TRUE, bIntReadActive = FALSE;
   BOOLEAN bCancelled = FALSE, bStopped = FALSE, bStopRegAccess = FALSE;
   PKWAIT_BLOCK pwbArray = NULL;
   LARGE_INTEGER checkRegInterval, currentTime, lastTime = {0};
   UNICODE_STRING ucValueName;
   ULONG debugMask, oldMask, driverResident;
   ULONG selectiveSuspendIdleTime = 0;
   OBJECT_ATTRIBUTES oa;
   HANDLE hRegKey = NULL;
   KEVENT dummyEvent;
   BOOLEAN bFirstTime = TRUE;
   short errCnt, errCnt0;
   ULONG rmErr = 0;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> INT2: wrong IRQL::%d\n", pDevExt->PortName, KeGetCurrentIrql())
      );
      if (pDevExt->pInterruptBuffer != NULL)
      {
         ExFreePool(pDevExt->pInterruptBuffer);
         pDevExt->pInterruptBuffer = NULL;
      }
      _closeHandle(pDevExt->hInterruptThreadHandle, "I-1");
      pDevExt->bIntThreadInCreation = FALSE;
      KeSetEvent(&pDevExt->IntThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
   }

   if (pDevExt -> InterruptPipe != (UCHAR)-1)
   {
      siz = sizeof( struct _URB_BULK_OR_INTERRUPT_TRANSFER );
      pUrb = ExAllocatePool(NonPagedPool, siz);
      if (!pUrb) 
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> INT: NO_MEM Urb\n", pDevExt->PortName)
         );
         // fail and exit thread
         if (pDevExt->pInterruptBuffer != NULL)
         {
            ExFreePool(pDevExt->pInterruptBuffer);
            pDevExt->pInterruptBuffer = NULL;
         }
         _closeHandle(pDevExt->hInterruptThreadHandle, "I-2");
         pDevExt->bIntThreadInCreation = FALSE;
         KeSetEvent(&pDevExt->IntThreadStartedEvent,IO_NO_INCREMENT,FALSE);
         PsTerminateSystemThread(STATUS_NO_MEMORY);
      }

      // allocate an irp for the iocontrol call to the lower driver
      pIrp = IoAllocateIrp((CCHAR)(pDevExt->MyDeviceObject->StackSize), FALSE );

      if(!pIrp)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> INT: NO_MEM Irp\n", pDevExt->PortName)
         );
         ExFreePool(pUrb);
         if (pDevExt->pInterruptBuffer != NULL)
         {
            ExFreePool(pDevExt->pInterruptBuffer);
            pDevExt->pInterruptBuffer = NULL;
         }
         _closeHandle(pDevExt->hInterruptThreadHandle, "I-3");
         pDevExt->bIntThreadInCreation = FALSE;
         KeSetEvent(&pDevExt->IntThreadStartedEvent,IO_NO_INCREMENT,FALSE);
         PsTerminateSystemThread(STATUS_NO_MEMORY);
      }
   }

   // allocate a wait block array for the multiple wait
   pwbArray = _ExAllocatePool
              (
                 NonPagedPool,
                 (INT_PIPE_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK),
                 "ReadInterruptPipe.pwbArray"
              );
   if (!pwbArray)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> INT: NO_MEM 3\n", pDevExt->PortName)
      );
      if (pUrb != NULL)
      {
         ExFreePool(pUrb);
      }
      if (pIrp != NULL)
      {
         IoReuseIrp(pIrp, STATUS_SUCCESS);
         IoFreeIrp(pIrp);
      }
      if (pDevExt->pInterruptBuffer != NULL)
      {
         ExFreePool(pDevExt->pInterruptBuffer);
         pDevExt->pInterruptBuffer = NULL;
      }
      _closeHandle(pDevExt->hInterruptThreadHandle, "I-4");
      pDevExt->bIntThreadInCreation = FALSE;
      KeSetEvent(&pDevExt->IntThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   if (pDevExt->InterruptPipe != (UCHAR)-1)
   {
      KeSetPriorityThread(KeGetCurrentThread(), QCSER_INT_PRIORITY);
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> INT: prio %d\n", pDevExt->PortName,
          KeQueryPriorityThread(KeGetCurrentThread()) )
      );
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_FORCE,
      ("<%s> I: ON-0x%u\n", pDevExt->PortName, pDevExt->InterruptPipe)
   );
   bIntReadActive  = FALSE;

   pDevExt->pInterruptPipeEvents[QC_DUMMY_EVENT_INDEX] = &dummyEvent;
   KeInitializeEvent(&dummyEvent, NotificationEvent, FALSE);

   KeClearEvent(&dummyEvent);
   KeClearEvent(&pDevExt->eInterruptCompletion);
   KeClearEvent(&pDevExt->CancelInterruptPipeEvent);
   KeClearEvent(&pDevExt->InterruptStopServiceEvent);
   KeClearEvent(&pDevExt->InterruptResumeServiceEvent);

   bStopped = (pDevExt->PowerSuspended == TRUE || pDevExt->bInService == FALSE);
   errCnt = errCnt0 = 0;

   // Register WMI power configuration
   QCPWR_RegisterWmiPowerGuids(pDevExt);

   InitializeObjectAttributes(&oa, &gServicePath, 0, NULL, NULL);
   while(bKeepRunning)
   {
      if (bIntReadActive == TRUE)
      {
         goto wait_for_completion;
      }

      if ((pDevExt->InterruptPipe != (UCHAR)-1) && (bStopped == FALSE) &&
          (!inDevState(DEVICE_STATE_DEVICE_REMOVED0)) &&
          (!inDevState(DEVICE_STATE_SURPRISE_REMOVED)))
      {
         //goto wait_for_completion;
         
         // setup the urb
        //if((pDevExt->usCommClassInterface >=0 && pDevExt->usCommClassInterface <MAX_INTERFACE)
			  //&&(pDevExt->Interface[pDevExt->usCommClassInterface] != NULL)
			  //&&(pDevExt->InterruptPipe < pDevExt->Interface[pDevExt->usCommClassInterface]->NumberOfPipes ))
         if(!pUrb) goto wait_for_completion;
         UsbBuildInterruptOrBulkTransferRequest
         (
            pUrb, 
            siz,
            pDevExt->Interface[pDevExt->usCommClassInterface]
               ->Pipes[pDevExt->InterruptPipe].PipeHandle,
            (PVOID) pDevExt -> pInterruptBuffer,
            NULL,
            pDevExt ->Interface[pDevExt->usCommClassInterface]
                    ->Pipes[pDevExt->InterruptPipe].MaximumPacketSize,
            USBD_SHORT_TRANSFER_OK,
            NULL
         );

         // and init the irp stack for the lower level driver
         IoReuseIrp(pIrp, STATUS_SUCCESS);

         nextstack = IoGetNextIrpStackLocation( pIrp );
         nextstack->Parameters.Others.Argument1 = pUrb;
         nextstack->Parameters.DeviceIoControl.IoControlCode =
            IOCTL_INTERNAL_USB_SUBMIT_URB;
         nextstack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
         IoSetCompletionRoutine
         (
            pIrp, 
            (PIO_COMPLETION_ROUTINE) InterruptPipeCompletion, 
            (PVOID)pDevExt, 
            TRUE,
            TRUE,
            TRUE
         );
         // IoSetCancelRoutine(pIrp, NULL); // DV?

         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> Ith: sending IRP (%d)", pDevExt->PortName, bStopped)
         );
         bIntReadActive = TRUE;
         ntStatus = IoCallDriver(pDevExt->StackDeviceObject, pIrp );
         if(!NT_SUCCESS(ntStatus))
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_CRITICAL,
               ("<%s> Ith: IoCallDriver rtn 0x%x - %d", pDevExt->PortName, ntStatus, errCnt0)
            );
            ++errCnt0;
            if (errCnt0 >= pDevExt->NumOfRetriesOnError)
            {
               errCnt0 = 0;
               bStopped = TRUE;
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_CRITICAL,
                  ("<%s> Ith: IoCallDriver failed, stopped", pDevExt->PortName)
               );
            }
            if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
            {
               NTSTATUS nts;

               nts = QCUSB_ResetInt(pDevExt->MyDeviceObject, QCUSB_RESET_HOST_PIPE);
               if (nts == STATUS_NO_SUCH_DEVICE)
               {
                  bStopped = TRUE;
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_CRITICAL,
                     ("<%s> Ith: NO_SUCH_DEVICE, stopped", pDevExt->PortName)
                  );
               }
               else
               {
                  QCSER_Wait(pDevExt, -(500 * 1000L)); // 50ms
               }
            }
         }
         else
         {
            errCnt0 = 0;
            if (ntStatus != STATUS_PENDING)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> Ith: IoCallDriver not pending 0x%x", pDevExt->PortName, ntStatus)
               );
            }
         }
      }  // if (pDevExt -> InterruptPipe != (UCHAR)-1)

     // No matter what IoCallDriver returns, we always wait on the kernel event
     // we created earlier. Our completion routine will gain control when the IRP
     // completes to signal this event. -- Walter Oney's WDM book page 228
wait_for_completion:

      if (bFirstTime == TRUE)
      {
         bFirstTime = FALSE;
         KeSetEvent(&pDevExt->IntThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      }

      QCSER_DbgPrint2
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> Ith: Wait...\n", pDevExt->PortName)
      );
      checkRegInterval.QuadPart = -(50 * 1000 * 1000);   // 5.0 sec
      ntStatus = KeWaitForMultipleObjects
                 (
                    INT_PIPE_EVENT_COUNT,
                    (VOID **)&pDevExt->pInterruptPipeEvents,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE,             // non-alertable
                    &checkRegInterval,
                    pwbArray
                 );
      KeQuerySystemTime(&currentTime);
      if (ntStatus != STATUS_TIMEOUT)
      {
         if ((currentTime.QuadPart - lastTime.QuadPart) >= 50000000L)
         {
            lastTime.QuadPart = currentTime.QuadPart;
            KeSetEvent(&dummyEvent, IO_NO_INCREMENT, FALSE);
         }
      }

      switch (ntStatus)
      {
         case INT_COMPLETION_EVENT_INDEX:
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Ith: INT_COMPLETION (stop %u)\n", pDevExt->PortName, bStopped)
            );

            KeClearEvent(&pDevExt->eInterruptCompletion);
            bIntReadActive = FALSE;  // set in completion

            if (bCancelled == TRUE)
            {
               bKeepRunning = FALSE;
               break;  // just exit
            }
            if (pDevExt->InterruptPipe == (UCHAR)-1)
            {
               break;
            }
            else if (bStopped == TRUE)
            {
               pDevExt->InterruptPipe = (UCHAR)(-1);

               // signal StopInterruptService(...)
               KeSetEvent
               (
                  &pDevExt->InterruptStopServiceRspEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );
               break;
            }

            // check status of completed urb
            if (USBD_ERROR(pUrb->UrbHeader.Status)) 
            {
               ++errCnt;
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> INT: URB error 0x%x/0x%x (%d)\n", pDevExt->PortName,
                   pUrb->UrbHeader.Status, pIrp->IoStatus.Status, errCnt)
               );
               if ((errCnt >= pDevExt->NumOfRetriesOnError) || (pIrp->IoStatus.Status == STATUS_DEVICE_NOT_CONNECTED))
               {
                  errCnt = 0;
                  bStopped = TRUE;
               }
               else if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
               {
                  NTSTATUS nts;

                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> INT: Waiting pipe INT\n", pDevExt->PortName)
                  );
                  nts = QCUSB_ResetInt(pDevExt->MyDeviceObject, QCUSB_RESET_HOST_PIPE);
                  if (nts == STATUS_NO_SUCH_DEVICE)
                  {
                     bStopped = TRUE;
                     break;
                  }
               }
               QCSER_Wait(pDevExt, -(500 * 1000L)); // 50ms
               break; // bail on error
            }
            else
            {
               errCnt = 0;
            }

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Ith: INT_COMPLETION (%uB)\n", pDevExt->PortName,
                 pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength)
            );
            if (pUrb->UrbBulkOrInterruptTransfer.TransferBufferLength == 0)
            {
               break;
            }

            pNotificationStatus = (PUSB_NOTIFICATION_STATUS)pDevExt->pInterruptBuffer ;

            switch (pNotificationStatus->bNotification)
            {
               case CDC_NOTIFICATION_SERIAL_STATE:
                  QCINT_HandleSerialStateNotification(pDevExt);
                  break;
               case CDC_NOTIFICATION_RESPONSE_AVAILABLE:
                  QCINT_HandleResponseAvailableNotification(pDevExt);
                  break;
               case CDC_NOTIFICATION_NETWORK_CONNECTION:
                  QCINT_HandleNetworkConnectionNotification(pDevExt);
                  break;
               case CDC_NOTIFICATION_CONNECTION_SPD_CHG:
                  QCINT_HandleConnectionSpeedChangeNotification(pDevExt);
                  break;
               default:
                  break;
            }

            break;
         } // end of INT_COMPLETION_EVENT_INDEX
			
         case INT_STOP_SERVICE_EVENT:
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Ith: STOP(%d/%d)\n", pDevExt->PortName, bIntReadActive, bStopped)
            );

            KeClearEvent(&pDevExt->InterruptStopServiceEvent);
            bStopped = TRUE;

            if (bIntReadActive == TRUE)
            {
               // we need to cancel the outstanding read
               IoCancelIrp(pIrp);
               goto wait_for_completion;
            }
            else
            {
               pDevExt->InterruptPipe = (UCHAR)(-1);

               KeSetEvent
               (
                  &pDevExt->InterruptStopServiceRspEvent,
                  IO_NO_INCREMENT,
                  FALSE
               );
            }

            break;
         }
         case INT_RESUME_SERVICE_EVENT:
         {
            KeClearEvent(&pDevExt->InterruptResumeServiceEvent);
            bStopped = (pDevExt->PowerSuspended == TRUE || pDevExt->bInService == FALSE);
            if (TRUE == bStopped)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> Ith: RESUME in suspend mode, no act\n", pDevExt->PortName)
               );
               break;
            }

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Ith: RESUME\n", pDevExt->PortName)
            );
            pDevExt->InterruptPipe = pDevExt->InterruptPipeIdx;
            errCnt = errCnt0 = 0;
            break;
         }

         case INT_EMPTY_RD_QUEUE_EVENT_INDEX:
         {
            QCSER_DbgPrint2
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Ith: EMPTY_RD_QUEUE\n", pDevExt->PortName)
            );
            KeClearEvent(&pDevExt->InterruptEmptyRdQueueEvent);
            QcEmptyCompletionQueue
            (
               pDevExt,
               &pDevExt->RdCompletionQueue,
               &pDevExt->ReadSpinLock,
               QCUSB_IRP_TYPE_RIRP
            );
            goto wait_for_completion;;
         }
         case INT_EMPTY_WT_QUEUE_EVENT_INDEX:
         {
            QCSER_DbgPrint2
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Ith: EMPTY_WT_QUEUE\n", pDevExt->PortName)
            );
            KeClearEvent(&pDevExt->InterruptEmptyWtQueueEvent);
            QcEmptyCompletionQueue
            (
               pDevExt,
               &pDevExt->WtCompletionQueue,
               &pDevExt->WriteSpinLock,
               QCUSB_IRP_TYPE_WIRP
            );
            goto wait_for_completion;;
         }
         case INT_EMPTY_CTL_QUEUE_EVENT_INDEX:
         {
            QCSER_DbgPrint2
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Ith: EMPTY_CTL_QUEUE\n", pDevExt->PortName)
            );
            KeClearEvent(&pDevExt->InterruptEmptyCtlQueueEvent);
            QcEmptyCompletionQueue
            (
               pDevExt,
               &pDevExt->CtlCompletionQueue,
               &pDevExt->ControlSpinLock,
               QCUSB_IRP_TYPE_CIRP
            );
            goto wait_for_completion;;
         }
         case INT_EMPTY_SGL_QUEUE_EVENT_INDEX:
         {
            QCSER_DbgPrint2
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Ith: EMPTY_SGL_QUEUE\n", pDevExt->PortName)
            );
            KeClearEvent(&pDevExt->InterruptEmptySglQueueEvent);
            QcEmptyCompletionQueue
            (
               pDevExt,
               &pDevExt->SglCompletionQueue,
               &pDevExt->SingleIrpSpinLock,
               QCUSB_IRP_TYPE_CIRP
            );
            goto wait_for_completion;;
         }

         case CANCEL_EVENT_INDEX:
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Ith: CANCEL\n", pDevExt->PortName)
            );

            KeClearEvent(&pDevExt->CancelInterruptPipeEvent);
            bCancelled = TRUE;

            if (bIntReadActive == TRUE)
            {
               // we need to cancel the outstanding read
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> Ith: IoCancelIrp\n", pDevExt->PortName)
               );
               IoCancelIrp(pIrp);
               goto wait_for_completion;
            }
            bKeepRunning = FALSE;
            break;
         }

         case INT_REG_IDLE_NOTIF_EVENT_INDEX:
         {
            // Register Idle Notification Request
            // KeClearEvent(&pDevExt->InterruptRegIdleEvent);

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_PIRP,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Ith: REG IDLE\n", pDevExt->PortName)
            );
            QCPWR_RegisterIdleNotification(pDevExt);

            break;
         }

         case INT_STOP_REG_ACCESS_EVENT_INDEX:
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Ith: STOP_REG_ACC\n", pDevExt->PortName)
            );
            KeClearEvent(&pDevExt->InterruptStopRegAccessEvent);
            bStopRegAccess = TRUE;
            KeSetEvent
            (
               &pDevExt->InterruptStopRegAccessAckEvent,
               IO_NO_INCREMENT, FALSE
            );

            break;
         }

         case QC_DUMMY_EVENT_INDEX:
         case STATUS_TIMEOUT:
         default:
         {
            // unconditionally clears the dummyEvent
            KeClearEvent(&dummyEvent);
            lastTime.QuadPart = currentTime.QuadPart;
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_STATE,
               QCSER_DBG_LEVEL_CRITICAL,
               ("<%s> STA: svc %u/%u Rstp %u/%u Ron %u/%u Won %u Srm %u RM %u ST 0x%x Susp %u Lk %ld\n",
                 pDevExt->PortName,
                 pDevExt->bInService,     pDevExt->bStackOpen,
                 pDevExt->bL1Stopped,     pDevExt->bL2Stopped,
                 pDevExt->bL1ReadActive,  pDevExt->bL2ReadActive,
                 pDevExt->bWriteActive,   pDevExt->bDeviceSurpriseRemoved,
                 pDevExt->bDeviceRemoved, pDevExt->bmDevState,
                 pDevExt->PowerSuspended, LockCnt
               )
            );
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_STATE,
               QCSER_DBG_LEVEL_CRITICAL,
               ("<%s> STA: RML %ld %ld %ld %ld %ld CRW %ld/%ld %ld/%ld %ld/%ld PWR %u/%u SS %us/%u P/W %u/%u\n",
                 pDevExt->PortName,
                 pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
                 pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3], pDevExt->Sts.lRmlCount[4],
                 pDevExt->Sts.lAllocatedCtls, pDevExt->Sts.lAllocatedDSPs,
                 pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedRdMem,
                 pDevExt->Sts.lAllocatedWrites, pDevExt->Sts.lAllocatedWtMem,
                 pDevExt->DevicePower, pDevExt->SystemPower,
                 pDevExt->SelectiveSuspendIdleTime, pDevExt->InServiceSelectiveSuspension,
                 pDevExt->PowerManagementEnabled, pDevExt->WaitWakeEnabled)
            );

            if (bStopRegAccess == TRUE)
            {
               // We stop accessing registry since the PDO will be deleted
               goto wait_for_completion;
            }

            // Get Debug Level
            ntStatus2 = IoOpenDeviceRegistryKey
                        (
                           pDevExt->PhysicalDeviceObject,
                           PLUGPLAY_REGKEY_DRIVER,
                           KEY_ALL_ACCESS,
                           &hRegKey
                        );
            if (!NT_SUCCESS(ntStatus2))
            {
               _closeRegKey(hRegKey, "Ir-0");
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> INT: reg ERR\n", pDevExt->PortName)
               );
               break; // ignore failure
            }
            ucHdlCnt++;

            RtlInitUnicodeString(&ucValueName, VEN_DBG_MASK);
            ntStatus2 = getRegDwValueEntryData
                        (
                           hRegKey,
                           ucValueName.Buffer,
                           &debugMask
                        );

            if (ntStatus2 == STATUS_SUCCESS)
            {
               oldMask = pDevExt->DebugMask;

               #ifdef DEBUG_MSGS
               gVendorConfig.DebugMask = debugMask = 0xFFFFFFFF;
               #else
               gVendorConfig.DebugMask = debugMask;
               #endif

               gVendorConfig.DebugLevel = (UCHAR)(debugMask & 0x0F);
               pDevExt->DebugMask = debugMask;
               pDevExt->DebugLevel = (UCHAR)(debugMask & 0x0F);

               if (debugMask != oldMask)
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> INT: DebugMask 0x%x\n", pDevExt->PortName, debugMask)
                  );
               }
            }

            _closeRegKey(hRegKey, "Ir-1");

            // Get driver resident state
            ntStatus2 = ZwOpenKey
                        (
                           &hRegKey,
                           KEY_READ,
                           &oa
                        );
            if (!NT_SUCCESS(ntStatus2))
            {
               _closeRegKey(hRegKey, "Ir-0a");
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> INT: reg srv ERR\n", pDevExt->PortName)
               );
               break; // ignore failure
            }
            ucHdlCnt++;

            RtlInitUnicodeString(&ucValueName, VEN_DRV_RESIDENT);
            ntStatus2 = getRegDwValueEntryData
                        (
                           hRegKey,
                           ucValueName.Buffer,
                           &driverResident
                        );

            if (ntStatus2 == STATUS_SUCCESS)
            {
               if (driverResident != gVendorConfig.DriverResident)
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> INT: DriverResident 0x%x\n", pDevExt->PortName, driverResident)
                  );
                  gVendorConfig.DriverResident = driverResident;
               }
            }

            // Get selective suspend idle time
            RtlInitUnicodeString(&ucValueName, VEN_DRV_SS_IDLE_T);
            ntStatus2 = getRegDwValueEntryData
                        (
                           hRegKey,
                           ucValueName.Buffer,
                           &selectiveSuspendIdleTime
                        );

            if (ntStatus2 == STATUS_SUCCESS)
            {
               if (QCUTIL_IsHighSpeedDevice(pDevExt) == TRUE)
               {
                  pDevExt->InServiceSelectiveSuspension = TRUE;
               }
               else
               {
                  pDevExt->InServiceSelectiveSuspension = ((selectiveSuspendIdleTime >> 31) != 0);
               }
               selectiveSuspendIdleTime &= 0x00FFFFFF;

               if ((selectiveSuspendIdleTime < QCUSB_SS_IDLE_MIN) &&
                   (selectiveSuspendIdleTime != 0))
               {
                  selectiveSuspendIdleTime = QCUSB_SS_IDLE_MIN;
               }
               else if (selectiveSuspendIdleTime > QCUSB_SS_IDLE_MAX)
               {
                  selectiveSuspendIdleTime = QCUSB_SS_IDLE_MAX;
               }

               if (selectiveSuspendIdleTime != pDevExt->SelectiveSuspendIdleTime)
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> INT: new selective suspend idle time=%us(%u)\n",
                      pDevExt->PortName, selectiveSuspendIdleTime,
                      pDevExt->InServiceSelectiveSuspension)
                  );
                  pDevExt->SelectiveSuspendIdleTime = selectiveSuspendIdleTime;
                  QCPWR_SyncUpWaitWake(pDevExt);
                  QCPWR_SetIdleTimer(pDevExt, 0, FALSE, 8);
               }
            }
            /***** disable SS by default, so exclude the code below *****
            else
            {
               selectiveSuspendIdleTime = QCUSB_SS_IDLE_DEFAULT;  // use default
               pDevExt->InServiceSelectiveSuspension = QCUTIL_IsHighSpeedDevice(pDevExt);

               if (pDevExt->SelectiveSuspendIdleTime != selectiveSuspendIdleTime)
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> INT: selective suspend changed to default\n", pDevExt->PortName)
                  );
                  pDevExt->SelectiveSuspendIdleTime = selectiveSuspendIdleTime;
                  QCPWR_SetIdleTimer(pDevExt, 0, FALSE, 10);
               }
            }
            **************************************************************/
            _closeRegKey(hRegKey, "Ir-1a");

            // if ntStatus is unexpected after a cancel event, we still
            // need to go back to wait for the final completion
            if ((ntStatus != STATUS_TIMEOUT) && (ntStatus != QC_DUMMY_EVENT_INDEX))
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> INT: unexpected evt-0x%x\n", pDevExt->PortName, ntStatus)
               );
               goto wait_for_completion;
            }

            // best-effort to recover from bus failure
            if ( (pDevExt->bDeviceRemoved == TRUE)          &&
                 (pDevExt->bDeviceSurpriseRemoved == FALSE) &&
                 (!inDevState(DEVICE_STATE_DEVICE_REMOVED0)) )
            {
               ++rmErr;
               /*************
               if (rmErr > 2)
               {
                  KeSetEvent(&(DeviceInfo[pDevExt->MgrId].DspDeviceRetryEvent), IO_NO_INCREMENT, FALSE);
               }
               **************/
            }
            else
            {
               rmErr = 0;
            }

            break;
         }
      } // end switch
   } // end while keep running

   if (pUrb != NULL)
   {
      ExFreePool( pUrb );
      pUrb = NULL;
   }
   if (pIrp != NULL)
   {
      IoReuseIrp(pIrp, STATUS_SUCCESS);
      IoFreeIrp(pIrp); // free irp 
   }
   if(pwbArray != NULL)
   {
      _ExFreePool(pwbArray);
   }

   if(pDevExt->pInterruptBuffer != NULL)
   {
      ExFreePool(pDevExt->pInterruptBuffer);
      pDevExt->pInterruptBuffer = NULL;
   }
   if(pDevExt->bInService == TRUE)
   {
      // usBitsMask==0 forces any pending wait irp to complete
      ProcessNewUartState(pDevExt, 0, 0, FALSE);
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_FORCE,
      ("<%s> Ith: OUT (0x%x)-0x%x\n", pDevExt->PortName, ntStatus, pDevExt->InterruptPipe)
   );

   KeSetEvent(&pDevExt->InterruptPipeClosedEvent, IO_NO_INCREMENT, FALSE );

   _closeHandle(pDevExt->hInterruptThreadHandle, "I-8");
   PsTerminateSystemThread(STATUS_SUCCESS);  // end this thread
}

NTSTATUS InterruptPipeCompletion
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PIRP           pIrp,
   IN PVOID          pContext
)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION)pContext;

   KeSetEvent( &pDevExt->eInterruptCompletion, IO_NO_INCREMENT, FALSE );

   QCPWR_SetIdleTimer(pDevExt, 0, FALSE, 2); // INT completion

   return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS CancelInterruptThread(PDEVICE_EXTENSION pDevExt, UCHAR cookie)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   LARGE_INTEGER delayValue;
   PVOID intThread;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> INT Cxl - %d\n", pDevExt->PortName, cookie)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> INT Cxl: wrong IRQL::%d - %d\n", pDevExt->PortName, KeGetCurrentIrql(), cookie)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if (pDevExt->bItCancelStarted == TRUE)
   {
      while ((pDevExt->hInterruptThreadHandle != NULL) || (pDevExt->pInterruptThread != NULL))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> Ith cxl in pro\n", pDevExt->PortName)
         );
         QCSER_Wait(pDevExt, -(3 * 1000 * 1000));
      }
      return STATUS_SUCCESS;
   }
   pDevExt->bItCancelStarted = TRUE;

   if ((pDevExt->hInterruptThreadHandle != NULL) || (pDevExt->pInterruptThread != NULL))
   {
      KeClearEvent(&pDevExt->InterruptPipeClosedEvent);
      KeSetEvent
      (
         &pDevExt->CancelInterruptPipeEvent,
         IO_NO_INCREMENT,
         FALSE
      );

      if (pDevExt->pInterruptThread != NULL)
      {
         ntStatus = KeWaitForSingleObject
                    (
                       pDevExt->pInterruptThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pDevExt->pInterruptThread);
         KeClearEvent(&pDevExt->InterruptPipeClosedEvent);
         _closeHandle(pDevExt->hInterruptThreadHandle, "I-5");
         pDevExt->pInterruptThread = NULL;
      }
      else  // best effort
      {
         ntStatus = KeWaitForSingleObject
                    (
                       &pDevExt->InterruptPipeClosedEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         KeClearEvent(&pDevExt->InterruptPipeClosedEvent);
         _closeHandle(pDevExt->hInterruptThreadHandle, "I-6");
      }
   }
   pDevExt->bItCancelStarted = FALSE;

   return ntStatus;
} // CancelInterruptThread

NTSTATUS StopInterruptService(PDEVICE_EXTENSION pDevExt, BOOLEAN CancelWaitWake, UCHAR cookie)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> INT stop - %d\n", pDevExt->PortName, cookie)
   );

   if ((pDevExt->pInterruptThread == NULL) && (pDevExt->hInterruptThreadHandle == NULL))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> INT not started - %d\n", pDevExt->PortName, cookie)
      );
      return ntStatus;
   }

   KeClearEvent(&pDevExt->InterruptStopServiceRspEvent);
   KeSetEvent
   (
      &pDevExt->InterruptStopServiceEvent,
      IO_NO_INCREMENT,
      FALSE
   );
   KeWaitForSingleObject
   (
      &pDevExt->InterruptStopServiceRspEvent,
      Executive,
      KernelMode,
      FALSE,
      NULL
   );
   KeClearEvent(&pDevExt->InterruptStopServiceRspEvent);

   // for STOP/SURPRISE_REMOVAL/REMOVE_DEVICE
   // CancelWaitWake is FALSE when called for power management
   QCPWR_CancelIdleTimer(pDevExt, 0, CancelWaitWake, 0);
   if (CancelWaitWake == TRUE)
   {
      QCPWR_CancelWaitWakeIrp(pDevExt, 3);   // for STOP/SURPRISE_REMOVAL/REMOVE_DEVICE
   }

   return ntStatus;
} // StopInterruptService

NTSTATUS ResumeInterruptService(PDEVICE_EXTENSION pDevExt, UCHAR cookie)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> INT resume - %d\n", pDevExt->PortName, cookie)
   );

   KeClearEvent(&pDevExt->InterruptResumeServiceEvent);
   KeSetEvent
   (
      &pDevExt->InterruptResumeServiceEvent,
      IO_NO_INCREMENT,
      FALSE
   );

   return ntStatus;
} // ResumeInterruptService

VOID QCINT_HandleSerialStateNotification(PDEVICE_EXTENSION pDevExt)
{
   PUSB_NOTIFICATION_STATUS pUartStatus;
   USHORT usStatusBits;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> QCINT_ProcessSerialState\n", pDevExt->PortName)
   );

   usStatusBits = 0;
   pUartStatus = (PUSB_NOTIFICATION_STATUS)pDevExt->pInterruptBuffer ;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> INTSerSt: Req 0x%x Noti 0x%x Val 0x%x Idx 0x%x Len %d usVal 0x%x\n",
        pDevExt->PortName, pUartStatus->bmRequestType, pUartStatus->bNotification,
        pUartStatus->wValue, pUartStatus->wIndex, pUartStatus->wLength,
        pUartStatus->usValue)
   );

   if (pUartStatus->usValue & USB_CDC_INT_RX_CARRIER)
   {
      usStatusBits |= SERIAL_EV_RLSD; // carrier-detection
   }
   if (pUartStatus->usValue & USB_CDC_INT_TX_CARRIER)
   {
      usStatusBits |= SERIAL_EV_DSR;  // data-set-ready
   }
   if (pUartStatus->usValue & USB_CDC_INT_BREAK)
   {
      usStatusBits |= SERIAL_EV_BREAK;  // break
   }
   if (pUartStatus->usValue & USB_CDC_INT_RING)
   {
      usStatusBits |= SERIAL_EV_RING;  // ring-detection
   }
   if (pUartStatus->usValue & USB_CDC_INT_FRAME_ERROR)
   {
      usStatusBits |= SERIAL_EV_ERR;  // line-error
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> Interrupt: USB frame err\n", pDevExt->PortName)
      );
   }
   if (pUartStatus->usValue & USB_CDC_INT_PARITY_ERROR)
   {
      usStatusBits |= SERIAL_EV_ERR;  // line-error
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> Interrupt: USB parity err\n", pDevExt->PortName)
      );
   }
   // usStatusBits = pUartStatus->usValue & US_BITS_MODEM_RAW;
   usStatusBits &= US_BITS_MODEM_RAW;
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> Interrupt: status 0x%x / 0x%x\n", pDevExt->PortName, usStatusBits, pUartStatus->usValue)
   );

   if_DevState(DEVICE_STATE_WOM_FIRST_TIME)
   {
      pDevExt->usCurrUartState &= ~US_BITS_MODEM;
      clearDevState(DEVICE_STATE_WOM_FIRST_TIME);
   }
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> Interrupt: UART 0x%x\n", pDevExt->PortName, pDevExt->usCurrUartState)
   );

   ProcessNewUartState
   (
      pDevExt,
      usStatusBits,
      US_BITS_MODEM_RAW,
      FALSE
   );
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> Interrupt: new UART 0x%x\n", pDevExt->PortName, pDevExt->usCurrUartState)
   );
}  // QCINT_HandleSerialStateNotification

VOID QCINT_HandleNetworkConnectionNotification(PDEVICE_EXTENSION pDevExt)
{
   PUSB_NOTIFICATION_STATUS pNetCon;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> INTNetworkConnection\n", pDevExt->PortName)
   );

   pNetCon = (PUSB_NOTIFICATION_STATUS)pDevExt->pInterruptBuffer ;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> INTNetCon: Req 0x%x Noti 0x%x Val 0x%x Idx 0x%x\n",
        pDevExt->PortName, pNetCon->bmRequestType, pNetCon->bNotification,
        pNetCon->wValue, pNetCon->wIndex)
   );

   if (pNetCon->wValue == 0)
   {
      // Disconnected
   }
   else
   {
      // Connected
   }
}  // QCINT_HandleNetworkConnectionNotification

VOID QCINT_HandleResponseAvailableNotification(PDEVICE_EXTENSION pDevExt)
{
   PUSB_NOTIFICATION_STATUS pNotification;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> INTRespAvail\n", pDevExt->PortName)
   );

   pNotification = (PUSB_NOTIFICATION_STATUS)pDevExt->pInterruptBuffer;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> INTRespAvail: Req 0x%x Noti 0x%x Idx(IF) 0x%x\n",
        pDevExt->PortName, pNotification->bmRequestType,
        pNotification->bNotification, pNotification->wIndex)
   );

   // Need to notify dispatch thread to issue GetEncapsulatedResponse

}  // QCINT_HandleResponseAvailableNotification

VOID QCINT_HandleConnectionSpeedChangeNotification(PDEVICE_EXTENSION pDevExt)
{
   PUSB_NOTIFICATION_STATUS pNotification;
   PUSB_NOTIFICATION_CONNECTION_SPEED pConSpd;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> INTConSpeedChange\n", pDevExt->PortName)
   );

   pNotification = (PUSB_NOTIFICATION_STATUS)pDevExt->pInterruptBuffer;
   pConSpd = (PUSB_NOTIFICATION_CONNECTION_SPEED)&pNotification->usValue;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> INTConSpdChange: Req 0x%x Noti 0x%x Idx(IF) 0x%x Len %d US %ul DS %ul\n",
        pDevExt->PortName, pNotification->bmRequestType,
        pNotification->bNotification, pNotification->wIndex, pNotification->wLength,
        pConSpd->ulUSBitRate, pConSpd->ulDSBitRate)
   );

   #ifdef QCUSB_FC
   if (pDevExt->bInService == FALSE)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> INTConSpeedChange: port not open, no act\n", pDevExt->PortName)
      );
      return;
   }

   if (pConSpd->ulUSBitRate == 0)
   {
      // read flow off
      QCRD_L2Suspend(pDevExt);
   }
   else
   {
      // read flow on
      QCRD_L2Resume(pDevExt);
   }

   if (pConSpd->ulDSBitRate == 0)
   {
      NTSTATUS ntStatus;
      LARGE_INTEGER delayValue;

      // write flow off
      KeClearEvent(&pDevExt->WriteFlowOffAckEvent);
      KeSetEvent(&pDevExt->WriteFlowOffEvent, IO_NO_INCREMENT, FALSE);
      delayValue.QuadPart = -(100 * 1000 * 1000); // 10.0 sec
      ntStatus = KeWaitForSingleObject
                 (
                    &pDevExt->WriteFlowOffAckEvent,
                    Executive,
                    KernelMode,
                    FALSE,
                    &delayValue
                 );
      if (ntStatus == STATUS_TIMEOUT)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> INT: timeout -- W flow off\n", pDevExt->PortName)
         );
         // clear the sig incase the WT thread is not running
         KeClearEvent(&pDevExt->WriteFlowOffEvent);
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> INT: W flow off\n", pDevExt->PortName)
         );
      }
   }
   else
   {
      // write flow on
      KeSetEvent(&pDevExt->WriteFlowOnEvent, IO_NO_INCREMENT, FALSE);
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> INT: W flow on\n", pDevExt->PortName)
      );
   }

   #endif // QCUSB_FC

}  // QCINT_HandleConnectionSpeedChangeNotification

VOID QCINT_StopRegistryAccess(PDEVICE_EXTENSION pDevExt)
{
   LARGE_INTEGER delayValue;
   NTSTATUS      ntStatus;

   if ((pDevExt->pInterruptThread == NULL) && (pDevExt->hInterruptThreadHandle == NULL))
   {
      // INT thread is not alive
      return;
   }

   KeClearEvent(&pDevExt->InterruptStopRegAccessAckEvent);
   KeSetEvent
   (
      &pDevExt->InterruptStopRegAccessEvent,
      IO_NO_INCREMENT, FALSE
   );

   delayValue.QuadPart = -(200 * 1000 * 1000);   // 20 sec
   ntStatus = KeWaitForSingleObject
              (
                 &pDevExt->InterruptStopRegAccessAckEvent,
                 Executive,
                 KernelMode,
                 FALSE,
                 &delayValue
              );
   KeClearEvent(&pDevExt->InterruptStopRegAccessAckEvent);

   if (ntStatus == STATUS_TIMEOUT)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> timeout on StopRegAcc\n", pDevExt->PortName)
      );
   }
}  // QCINT_StopRegistryAccess
