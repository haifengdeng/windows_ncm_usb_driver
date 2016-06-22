/*===========================================================================
FILE: QCDEV.c

DESCRIPTION:
   This file contains implementations for standalone USB WDM device.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2007 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#include "QCMAIN.h"
#include "QCPTDO.h"
#include "QCUTILS.h"
#include "QCSER.h"
#include "QCPWR.h"
#include "QCDEV.h"

static UNICODE_STRING QcDevName, QcDevPath;
static SYSTEM_POWER_STATE QCDEV_SysPowerState = PowerSystemWorking;
static DEVICE_POWER_STATE QCDEV_DevPowerState = PowerDeviceD0;
static BOOLEAN QCDEV_Registered = FALSE;
static LONG RefCount = 0;
static PVOID QCDEV_IfContext = NULL;
static UCHAR QCDEV_PrepareToPowerDown = 0;

VOID QCDEV_RegisterDeviceInterface(PDEVICE_EXTENSION pDevExt)
{
   NTSTATUS ntStatus;
   static BOOLEAN initInProgress = FALSE;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   if (pDevExt->WdmVersion < WinXpOrHigher)
   {
      // no Win2K support
      return;
   }

   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> -->QCDEV_RegisterDeviceInterface\n", pDevExt->PortName)
   );

   QcAcquireSpinLock(&gPnpSpinLock, &levelOrHandle);
   if (QCDEV_Registered == TRUE)
   {
      // already registered
      QcReleaseSpinLock(&gPnpSpinLock, levelOrHandle);
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> QCDEV_RegisterDeviceInterface: already done\n", pDevExt->PortName)
      );
      return;
   }
   else if (initInProgress == TRUE)
   {
      QcReleaseSpinLock(&gPnpSpinLock, levelOrHandle);
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> QCDEV_RegisterDeviceInterface: init in progress\n", pDevExt->PortName)
      );
      return;
   }

   initInProgress = TRUE;

   QcReleaseSpinLock(&gPnpSpinLock, levelOrHandle);

   RtlInitUnicodeString(&QcDevName, QCDEV_NAME);
   RtlInitUnicodeString(&QcDevPath, QCDEV_PATH);

   ntStatus = IoRegisterDeviceInterface
              (
                 pDevExt->PhysicalDeviceObject,
                 &GUID_DEVINTERFACE_QCUSB_SERIAL,
                 &QcDevName,
                 &pDevExt->QCDEV_IfaceSymbolicLinkName
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> QCDEV_RegisterDeviceInterface: failure 0x%x\n", pDevExt->PortName, ntStatus)
      );
   }
   else
   {
      ntStatus = IoSetDeviceInterfaceState
                 (
                    &pDevExt->QCDEV_IfaceSymbolicLinkName, TRUE
                 );
      if (!NT_SUCCESS(ntStatus))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> QCDEV_RegisterDeviceInterface: failure(set) 0x%x\n", pDevExt->PortName, ntStatus)
         );
         _freeUcBuf(pDevExt->QCDEV_IfaceSymbolicLinkName);
      }
      else
      {
         QCDEV_Registered = TRUE;
         QCDEV_IfContext  = (PVOID)pDevExt;
      }
   }   
   initInProgress = FALSE;

}  // QCDEV_RegisterDeviceInterface

VOID QCDEV_DeregisterDeviceInterface(PDEVICE_EXTENSION pDevExt)
{
   static BOOLEAN removalInProgress = FALSE;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif


   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> -->QCDEV_DeregisterDeviceInterface\n", pDevExt->PortName)
   );

   QcAcquireSpinLock(&gPnpSpinLock, &levelOrHandle);
   if (QCDEV_Registered == FALSE)
   {
      // already deregistered
      QcReleaseSpinLock(&gPnpSpinLock, levelOrHandle);
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> <--QCDEV_DeregisterDeviceInterface: already deregistered\n", pDevExt->PortName)
      );
      return;
   }
   else if (removalInProgress == TRUE)
   {
      QcReleaseSpinLock(&gPnpSpinLock, levelOrHandle);
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> QCDEV_DeregisterDeviceInterface: in progress\n", pDevExt->PortName)
      );
      return;
   }

   if (pDevExt != QCDEV_IfContext)
   {
      // wrong caller
      QcReleaseSpinLock(&gPnpSpinLock, levelOrHandle);
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> <--QCDEV_DeregisterDeviceInterface: wrong caller\n", pDevExt->PortName)
      );
      return;
   }

   QCDEV_IfContext  = NULL;
   QCDEV_Registered = FALSE;
   removalInProgress = TRUE;
   QcReleaseSpinLock(&gPnpSpinLock, levelOrHandle);

   if (pDevExt->QCDEV_IfaceSymbolicLinkName.Length > 0)
   {
      IoSetDeviceInterfaceState
      (
         &pDevExt->QCDEV_IfaceSymbolicLinkName, FALSE
      );
      _freeUcBuf(pDevExt->QCDEV_IfaceSymbolicLinkName);
   }


   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> <--QCDEV_DeregisterDeviceInterface\n", pDevExt->PortName)
   );

   removalInProgress = FALSE;
}  // QCDEV_DeregisterDeviceInterface

BOOLEAN QCDEV_CompareFileName(PUNICODE_STRING pus1, PUNICODE_STRING pus2)
{
   char file1[256], file2[256];
   int i;
   BOOLEAN result = TRUE;

   if (pus1->Length != pus2->Length)
   {
      return FALSE;
   }

   for (i = 0; i < pus1->Length/2; i++)
   {
      if (i > 250)
      {
         result = FALSE;
         break;
      }
      file1[i] = (char)pus1->Buffer[i];
      file2[i] = (char)pus2->Buffer[i];
      // if (toupper(file1[i] != toupper(file2[i])))
      if (file1[i] != file2[i])
      {
         result = FALSE;
         break;
      }
   }

   return result;
}  // QCDEV_CompareFileName

BOOLEAN QCDEV_IsXwdmRequest
(
   PDEVICE_EXTENSION pDevExt,
   PIRP              Irp
)
{
   BOOLEAN isXwdm = FALSE;
   PIO_STACK_LOCATION irpStack;
   PFILE_OBJECT fileObj;

   if (Irp == NULL)
   {
      return isXwdm;
   }

   irpStack = IoGetCurrentIrpStackLocation(Irp);

   fileObj = irpStack->FileObject;

   if (fileObj == NULL)
   {
      return isXwdm;
   }

   switch (irpStack->MajorFunction)
   {
      case IRP_MJ_CREATE:
      {
         
         if (fileObj->FileName.Length != 0)
         {
            BOOLEAN res = QCDEV_CompareFileName
                          (
                             &QcDevPath,
                             &(fileObj->FileName)
                          );

            QCSER_DbgPrintG
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> QCDEV_CREATE: FN<%ws> (%d) - %d [%d]\n", gDeviceName, 
                fileObj->FileName.Buffer,
                fileObj->FileName.Length/sizeof(WCHAR), res, pDevExt->QcDevOpenCount)
            );
            if (res == TRUE)
            {
               InterlockedIncrement(&(pDevExt->QcDevOpenCount));
               fileObj->FsContext = (PVOID)&QcDevName;
               isXwdm = TRUE;
            }
         }
         else
         {
            QCSER_DbgPrintG
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> OTHER_CREATE: FsContext 0x%p, set to NUL\n", gDeviceName, 
                irpStack->FileObject->FsContext)
            );
            fileObj->FsContext = NULL;
         }
         break;
      }

      case IRP_MJ_CLEANUP:
      {
         if (fileObj->FsContext == (PVOID)&QcDevName)
         {
            isXwdm = TRUE;
            QCSER_DbgPrintG
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> QCDEV_CLEANUP: CTXT 0x%p\n", gDeviceName, fileObj->FsContext)
            );
            QCDEV_CancelNotificationIrp(pDevExt->MyDeviceObject);
         }
         break;
      }
      case IRP_MJ_CLOSE:
      {
         if (fileObj->FsContext == (PVOID)&QcDevName)
         {
            QCDEV_CancelNotificationIrp(pDevExt->MyDeviceObject);
            isXwdm = TRUE;
            InterlockedDecrement(&(pDevExt->QcDevOpenCount));
            QCSER_DbgPrintG
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> QCDEV_CLOSE: CTXT 0x%p [%d]\n", gDeviceName, fileObj->FsContext,
                 pDevExt->QcDevOpenCount)
            );
            fileObj->FsContext = NULL;
         }
         break;
      }
      default:
      {
         break;
      }
   }  // switch

   return isXwdm;
}  // QCDEV_IsXwdmRequest

VOID QCDEV_SystemToPowerDown(BOOLEAN PrepareToPowerDown)
{
   if (TRUE == PrepareToPowerDown)
   {
      QCDEV_PrepareToPowerDown = 1;
   }
   else
   {
      QCDEV_PrepareToPowerDown = 0;
   }
}  // QCDEV_SystemToPowerDown

VOID QCDEV_SetSystemPowerState(SYSTEM_POWER_STATE State)
{
   QCDEV_SysPowerState = State;
}  // QCDEV_SetSystemPowerState

VOID QCDEV_SetDevicePowerState(DEVICE_POWER_STATE State)
{
   QCDEV_DevPowerState = State;
}  // QCDEV_SetDevicePowerState

VOID QCDEV_GetSystemPowerState(PVOID Buffer)
{
   PQCUSB_POWER_REQ pwrReq;

   pwrReq = (PQCUSB_POWER_REQ)Buffer;

   switch(pwrReq->Type)
   {
      case QCUSB_SYS_POWER_CURRENT:
      {
         pwrReq->State = QCDEV_SysPowerState;
         break;
      }
      case QCUSB_PREPARE_TO_STANDBY:
      {
         pwrReq->State = QCDEV_PrepareToPowerDown;
         break;
      }
      default:
      {
         // error
         pwrReq->State = 0xFF;
         break;
      }
   }
   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_ERROR,
      ("<%s> QCDEV_GetSystemPowerState[%d]=%d\n", gDeviceName,
        pwrReq->Type, pwrReq->State)
   );
}  // QCDEV_GetSystemPowerState

VOID QCDEV_GetDevicePowerState(PVOID Buffer)
{
   RtlCopyMemory
   (
      Buffer,
      &QCDEV_DevPowerState,
      sizeof(DEVICE_POWER_STATE)
   );
   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_ERROR,
      ("<%s> QCDEV_GetDevicePowerState %d\n", gDeviceName, QCDEV_DevPowerState)
   );
}  // QCDEV_GetDevicePowerState

NTSTATUS QCDEV_CacheNotificationIrp
(
   PDEVICE_OBJECT DeviceObject,
   PVOID          ioBuffer,
   PIRP           pIrp
)
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus;
   KIRQL IrqLevel,IrqLevelCancelSpinlock;
   PIO_STACK_LOCATION irpStack;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();
   
   pDevExt = DeviceObject->DeviceExtension;
   pIrp -> IoStatus.Information = 0; // default is error for now
   ntStatus = STATUS_INVALID_PARAMETER; // default is error for now

   if (!ioBuffer)
   {
      return ntStatus;
   }

   irpStack = IoGetCurrentIrpStackLocation( pIrp );
   if (irpStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
   {
      ntStatus = STATUS_BUFFER_TOO_SMALL;
      return ntStatus;
   }

   if (pDevExt->bDeviceSurpriseRemoved == TRUE)
   {
      return STATUS_UNSUCCESSFUL;
   }

   if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      return STATUS_UNSUCCESSFUL;
   }

   QcAcquireSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, &levelOrHandle, irql);
   // Got duplicated requests, deny it.
   if (pDevExt->pQcDevNotificationIrp != NULL)
   {
      *(ULONG *)pIrp->AssociatedIrp.SystemBuffer = QCOMSER_DUPLICATED_NOTIFICATION_REQ;
      pIrp->IoStatus.Information = sizeof( ULONG );
      pIrp->IoStatus.Status = STATUS_UNSUCCESSFUL;
      ntStatus = STATUS_UNSUCCESSFUL;
   }
   else
   {
      // always pend the IRP, never complete it immediately
      ntStatus = STATUS_PENDING;
      _IoMarkIrpPending(pIrp); // it should already be pending from the dispatch queue!
      pDevExt->pQcDevNotificationIrp = pIrp;
      IoSetCancelRoutine(pIrp, QCDEV_CancelNotificationRoutine);

      if (pIrp->Cancel)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> NotIrp: Cxled\n", pDevExt->PortName)
         );
         if (IoSetCancelRoutine(pIrp, NULL) != NULL)
         {
            pDevExt->pQcDevNotificationIrp= NULL;
            ntStatus = STATUS_CANCELLED;
         }
         else
         {
            // do nothing
         }
      } // if
   }
   QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);

   // the dispatch routine will complete the IRP is ntStatus is not pending

   return ntStatus;
}  // QCDEV_CacheNotificationIrp

void QCDEV_PostNotification(PDEVICE_EXTENSION pDevExt)
{
   PIRP tmpIrp;
   NTSTATUS nts = STATUS_SUCCESS;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
   tmpIrp = pDevExt->pQcDevNotificationIrp;
   if (tmpIrp != NULL)
   {
      nts = SerialNotifyClient(tmpIrp, QCOMSER_REMOVAL_NOTIFICATION);
      if (nts == STATUS_SUCCESS)
      {
         pDevExt->pQcDevNotificationIrp = NULL;
         InsertTailList(&pDevExt->SglCompletionQueue, &tmpIrp->Tail.Overlay.ListEntry);
         KeSetEvent(&pDevExt->InterruptEmptySglQueueEvent, IO_NO_INCREMENT, FALSE);
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> RmNoti: cannot post!!!\n", pDevExt->PortName)
         );
      }
   }
   QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
}  // QCDEV_PostNotification

NTSTATUS QCDEV_CancelNotificationIrp(PDEVICE_OBJECT pDevObj)
{
   PDEVICE_EXTENSION   pDevExt;
   NTSTATUS            ntStatus = STATUS_SUCCESS;
   PIRP                pIrp;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   pDevExt = pDevObj->DeviceExtension;

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
   pIrp = pDevExt->pQcDevNotificationIrp;
   if (pIrp != NULL)
   {

      if (IoSetCancelRoutine(pIrp, NULL) != NULL)
      {
         pDevExt->pQcDevNotificationIrp = NULL;
         pIrp->IoStatus.Status = STATUS_CANCELLED;
         pIrp->IoStatus.Information = 0;

         InsertTailList(&pDevExt->SglCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
         KeSetEvent(&pDevExt->InterruptEmptySglQueueEvent, IO_NO_INCREMENT, FALSE);
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> QCDEV_CancelNotificationIrp: already cancelled 0x%p\n", pDevExt->PortName, pIrp)
         );
      }
   }
   QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

   return ntStatus;
}  // QCDEV_CancelNotificationIrp

VOID QCDEV_CancelNotificationRoutine( PDEVICE_OBJECT CalledDO, PIRP pIrp )
{
   KIRQL irql = KeGetCurrentIrql();
   PDEVICE_OBJECT pDevObj = QCPTDO_FindPortDOByFDO(CalledDO, irql);
   PDEVICE_EXTENSION pDevExt = pDevObj->DeviceExtension;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   IoReleaseCancelSpinLock(pIrp->CancelIrql);

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
   if (pDevExt->pQcDevNotificationIrp == NULL)
   {
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
      return;
   }
   IoSetCancelRoutine(pIrp, NULL);  // not necessary
   ASSERT(pDevExt->pQcDevNotificationIrp==pIrp);
   pDevExt->pQcDevNotificationIrp = NULL;

   pIrp->IoStatus.Status = STATUS_CANCELLED;
   pIrp->IoStatus.Information = 0;

   InsertTailList(&pDevExt->SglCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
   KeSetEvent(&pDevExt->InterruptEmptySglQueueEvent, IO_NO_INCREMENT, FALSE);

   QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

} //  QCDEV_CancelNotificationRoutine
