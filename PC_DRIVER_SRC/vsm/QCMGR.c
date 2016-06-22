/*===========================================================================
FILE: QCMGR.c

DESCRIPTION:
   This file contains implementations for USB device manager.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003, 2004 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#include "QCMGR.h"

PMGR_DEV_INFO   DeviceInfo       = NULL;
static HANDLE   hMgrThreadHandle = NULL;
static PKTHREAD pMgrThread       = NULL;
static BOOLEAN  bMgrThreadInCreation = FALSE;
static KEVENT   DspTerminationRequestEvent;
static KEVENT   MgrThreadStartedEvent;
static KEVENT   MgrThreadTerminationReqEvent;
static KEVENT   MgrThreadTerminationRspEvent;
static PKEVENT  pMgrTerminationEvents[MGR_MAX_SIG];
static KSPIN_LOCK MgrSpinLock;

VOID QCMGR_SetTerminationRequest(int MgrId)
{
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QcAcquireSpinLock(&MgrSpinLock, &levelOrHandle);
   DeviceInfo[MgrId].TerminationRequest = TRUE;
   KeSetEvent(&DspTerminationRequestEvent, IO_NO_INCREMENT, FALSE);
   QcReleaseSpinLock(&MgrSpinLock, levelOrHandle);
}

int QCMGR_RequestManagerElement(VOID)
{
   USHORT element = 0;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QcAcquireSpinLock(&MgrSpinLock, &levelOrHandle);
   if (DeviceInfo == NULL)
   {
      DbgPrint("<%s> MGR: fatal-no mgr id\n", gDeviceName);
      QcReleaseSpinLock(&MgrSpinLock, levelOrHandle);
      return -1;
   }

   for (element = 0; element < MGR_MAX_NUM_DEV; element++)
   {
      if (DeviceInfo[element].Alive == FALSE)
      {
         DeviceInfo[element].Alive = TRUE;
         QcReleaseSpinLock(&MgrSpinLock, levelOrHandle);
         return element;
      }
   }

   DbgPrint("<%s> MGR: fatal-no mgr id\n", gDeviceName);
   QcReleaseSpinLock(&MgrSpinLock, levelOrHandle);
   return -1;
}  // QCMGR_RequestManagerElement

NTSTATUS QCMGR_StartManagerThread(VOID)
{
   NTSTATUS ntStatus;
   OBJECT_ATTRIBUTES objAttr;
   USHORT element;

   if (DeviceInfo == NULL)
   {
      DeviceInfo = _ExAllocatePool
                   (
                      NonPagedPool,
                      (sizeof(MGR_DEV_INFO) * MGR_MAX_NUM_DEV),
                      ("QCMGR")
                   );
      if (DeviceInfo == NULL)
      {
         return STATUS_UNSUCCESSFUL;
      }
   }

   if ((hMgrThreadHandle != NULL) || (pMgrThread != NULL))
   {
      // thread has been started, unlikely to get here
      // This function should be called only once (from DriverEntry)
      return STATUS_SUCCESS;
   }

   KeInitializeSpinLock(&MgrSpinLock);

   // Initialize all elements
   for (element = 0; element < MGR_MAX_NUM_DEV; element++)
   {
      DeviceInfo[element].Alive = FALSE;
      DeviceInfo[element].TerminationRequest = FALSE;
      KeInitializeEvent
      (
         &(DeviceInfo[element].DspStartEvent),
         NotificationEvent,
         FALSE
      );
      KeInitializeEvent
      (
         &(DeviceInfo[element].DspStartDataThreadsEvent),
         NotificationEvent,
         FALSE
      );
      KeInitializeEvent
      (
         &(DeviceInfo[element].DspResumeDataThreadsEvent),
         NotificationEvent,
         FALSE
      );
      KeInitializeEvent
      (
         &(DeviceInfo[element].DspResumeDataThreadsAckEvent),
         NotificationEvent,
         FALSE
      );
      KeInitializeEvent
      (
         &(DeviceInfo[element].DspPreWakeUpEvent),
         NotificationEvent,
         FALSE
      );

      KeInitializeEvent
      (
         &(DeviceInfo[element].DspDeviceRetryEvent),
         NotificationEvent,
         FALSE
      );
      KeInitializeEvent
      (
         &(DeviceInfo[element].DspDeviceResetINEvent),
         NotificationEvent,
         FALSE
      );
      KeInitializeEvent
      (
         &(DeviceInfo[element].DspDeviceResetOUTEvent),
         NotificationEvent,
         FALSE
      );
      KeInitializeEvent
      (
         &(DeviceInfo[element].TerminationOrderEvent),
         NotificationEvent,
         FALSE
      );
      KeInitializeEvent
      (
         &(DeviceInfo[element].TerminationCompletionEvent),
         NotificationEvent,
         FALSE
      );
      DeviceInfo[element].hDispatchThreadHandle = NULL;
      DeviceInfo[element].pDispatchThread       = NULL;

   }
   pMgrTerminationEvents[MGR_TERMINATE_DSP_EVENT] = &DspTerminationRequestEvent;
   pMgrTerminationEvents[MGR_TERMINATE_MGR_EVENT] = &MgrThreadTerminationReqEvent;

   KeInitializeEvent
   (
      &DspTerminationRequestEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &MgrThreadStartedEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &MgrThreadTerminationReqEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &MgrThreadTerminationRspEvent,
      NotificationEvent,
      FALSE
   );

   // Start manager thread
   InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
   bMgrThreadInCreation = TRUE;
   ntStatus = PsCreateSystemThread
              (
                 OUT &hMgrThreadHandle,
                 IN THREAD_ALL_ACCESS,
                 IN &objAttr,         // POBJECT_ATTRIBUTES
                 IN NULL,             // HANDLE  ProcessHandle
                 OUT NULL,            // PCLIENT_ID  ClientId
                 IN (PKSTART_ROUTINE)ManagerThread,
                 IN (PVOID)NULL       // Context
              );			
   if ((!NT_SUCCESS(ntStatus)) || (hMgrThreadHandle == NULL))
   {
      hMgrThreadHandle = NULL;
      pMgrThread = NULL;
      ExFreePool(DeviceInfo);
      DeviceInfo = NULL;
      bMgrThreadInCreation = FALSE;
      DbgPrint("<%s> MGR: th failure 0x%x\n", gDeviceName, ntStatus);
      return STATUS_UNSUCCESSFUL;
   }

   ntStatus = KeWaitForSingleObject
              (
                 &MgrThreadStartedEvent,
                 Executive,
                 KernelMode,
                 FALSE,
                 NULL
              );
   if (bMgrThreadInCreation == FALSE)
   {
      hMgrThreadHandle = NULL;
      pMgrThread = NULL;
      ExFreePool(DeviceInfo);
      DeviceInfo = NULL;
      DbgPrint("<%s> MGR: th failure-3\n", gDeviceName);
      return STATUS_UNSUCCESSFUL;
   }

   ntStatus = ObReferenceObjectByHandle
              (
                 hMgrThreadHandle,
                 THREAD_ALL_ACCESS,
                 NULL,
                 KernelMode,
                 (PVOID*)&pMgrThread,
                 NULL
              );
   if (!NT_SUCCESS(ntStatus))
   {
      DbgPrint("<%s> MGR: ObReferenceObjectByHandle failed 0x%x\n", gDeviceName, ntStatus);
      pMgrThread = NULL;
   }
   else
   {
      // DbgPrint("<%s> MGR h=0x%p t=0x%p\n", gDeviceName, hMgrThreadHandle, pMgrThread);
      if (hMgrThreadHandle != NULL)
      {
         ZwClose(hMgrThreadHandle);
         hMgrThreadHandle = NULL;
      }
   }
   bMgrThreadInCreation = FALSE;

   return STATUS_SUCCESS;
}  // QCMGR_StartManagerThread

VOID ManagerThread(IN PVOID pContext)
{
   PKWAIT_BLOCK pwbArray = NULL;
   NTSTATUS ntStatus;
   BOOLEAN bKeepRunning = TRUE;

   // allocate a wait block array for the multiple wait
   pwbArray = _ExAllocatePool
              (
                 NonPagedPool,
                 (MGR_MAX_SIG+1)*sizeof(KWAIT_BLOCK),
                 "Mgr.pwbArray"
              );
   if (pwbArray == NULL)
   {
      DbgPrint("<%s> MGR: no MEM\n", gDeviceName);
      ZwClose(hMgrThreadHandle);
      hMgrThreadHandle = NULL;
      KeSetEvent(&MgrThreadStartedEvent, IO_NO_INCREMENT, FALSE);
      bMgrThreadInCreation = FALSE;
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   KeSetEvent(&MgrThreadStartedEvent, IO_NO_INCREMENT, FALSE);

   while (bKeepRunning == TRUE)
   {
      ntStatus = KeWaitForMultipleObjects
                 (
                    MGR_MAX_SIG,
                    (VOID **)&pMgrTerminationEvents,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE,             // non-alertable
                    NULL,
                    pwbArray
                 );

      switch (ntStatus)
      {
         case MGR_TERMINATE_DSP_EVENT:
         {
            USHORT i;

            // device dispatch termination request
            KeClearEvent(&DspTerminationRequestEvent);

            for (i = 0; i < MGR_MAX_NUM_DEV; i++)
            {
               if (DeviceInfo[i].TerminationRequest == TRUE)
               {
                  ntStatus = QCMGR_TerminateDispatchThread(i);
                  QCSER_DbgPrintG
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> Mth: terminated_d %u\n", gDeviceName, i)
                  );
               }
            }
            break;
         }
   
         case MGR_TERMINATE_MGR_EVENT:
         {
            int i;

            // Mgr termination request
            KeClearEvent(&MgrThreadTerminationReqEvent);

            // Best effort to terminate any alive DSP thread
            for (i = 0; i < MGR_MAX_NUM_DEV; i++)
            {
               if (DeviceInfo[i].TerminationRequest == TRUE)
               {
                  ntStatus = QCMGR_TerminateDispatchThread(i);
                  QCSER_DbgPrintG
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> Mth: terminated_m %u\n", gDeviceName, i)
                  );
               }
            }

            bKeepRunning = FALSE;
            break;
         }

         default:
         {
            // unknown event
            DbgPrint("<%s> Mth: unknown event %u\n", gDeviceName, ntStatus);
            break;
         }
      }  // switch
   } // end while keep running

   if(pwbArray != NULL)
   {
      _ExFreePool(pwbArray);
   }

   DbgPrint("<%s> Mth: OUT\n", gDeviceName);

   KeSetEvent(&MgrThreadTerminationRspEvent, IO_NO_INCREMENT, FALSE);

   if (hMgrThreadHandle != NULL)
   {
      ZwClose(hMgrThreadHandle);
      hMgrThreadHandle = NULL;
   }

   PsTerminateSystemThread(STATUS_SUCCESS);  // end this thread
}  // ManagerThread

NTSTATUS QCMGR_CancelManagerThread(void)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;

   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> MGR Cxl-0\n", gDeviceName)
   );

   if ((hMgrThreadHandle != NULL) || (pMgrThread != NULL))
   {
      KeClearEvent(&MgrThreadTerminationRspEvent);
      KeSetEvent
      (
         &MgrThreadTerminationReqEvent,
         IO_NO_INCREMENT,
         FALSE
      );

      if (pMgrThread != NULL)
      {
         ntStatus = KeWaitForSingleObject
                    (
                       pMgrThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(pMgrThread);
      }
      else  // best effort
      {
         ntStatus = KeWaitForSingleObject
                    (
                       &MgrThreadTerminationRspEvent,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
      }

      KeClearEvent(&MgrThreadTerminationRspEvent);
      if (hMgrThreadHandle != NULL)
      {
         ZwClose(hMgrThreadHandle);
         hMgrThreadHandle = NULL;
      }
      pMgrThread = NULL;
   }

   if (DeviceInfo != NULL)
   {
      ExFreePool(DeviceInfo);
      DeviceInfo = NULL;
   }

   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> MGR Cxl-end\n", gDeviceName)
   );

   return ntStatus;
} // QCMGR_CancelManagerThread

NTSTATUS QCMGR_TerminateDispatchThread(int MgrId)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif


   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> MGR DSP - %d\n", gDeviceName, MgrId)
   );

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> MGR DSP: wrong IRQL::%d - %d\n", gDeviceName, KeGetCurrentIrql(), MgrId)
      );
      return STATUS_UNSUCCESSFUL;
   }

   if ((DeviceInfo[MgrId].hDispatchThreadHandle != NULL) || (DeviceInfo[MgrId].pDispatchThread != NULL))
   {
      KeClearEvent(&(DeviceInfo[MgrId].TerminationCompletionEvent));
      KeSetEvent
      (
         &(DeviceInfo[MgrId].TerminationOrderEvent),
         IO_NO_INCREMENT,
         FALSE
      );

      if (DeviceInfo[MgrId].pDispatchThread != NULL)
      {
         QCSER_DbgPrintG
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> MGR: wait for closure %d\n", gDeviceName, MgrId)
         );
         ntStatus = KeWaitForSingleObject
                    (
                       DeviceInfo[MgrId].pDispatchThread,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
         ObDereferenceObject(DeviceInfo[MgrId].pDispatchThread);
         DeviceInfo[MgrId].pDispatchThread = NULL;
      }
      else  // best effort
      {
         QCSER_DbgPrintG
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> MGR: wait 2 for closure %d\n", gDeviceName, MgrId)
         );
         ntStatus = KeWaitForSingleObject
                    (
                       &(DeviceInfo[MgrId].TerminationCompletionEvent),
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
      }
      KeClearEvent(&(DeviceInfo[MgrId].TerminationCompletionEvent));
      KeClearEvent(&(DeviceInfo[MgrId].TerminationOrderEvent));
      _closeHandleG(gDeviceName, DeviceInfo[MgrId].hDispatchThreadHandle, "M-1");
   }

   QcAcquireSpinLock(&MgrSpinLock, &levelOrHandle);
   DeviceInfo[MgrId].Alive = FALSE;
   DeviceInfo[MgrId].TerminationRequest = FALSE;
   QcReleaseSpinLock(&MgrSpinLock, levelOrHandle);

   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> MGR: DSP %d OUT\n", gDeviceName, MgrId)
   );

   return ntStatus;
} // QCMGR_TerminateDispatchThread

