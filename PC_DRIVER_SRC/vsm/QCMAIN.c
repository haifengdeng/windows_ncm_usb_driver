/*===========================================================================
FILE: QCMAIN.c

DESCRIPTION:
   This file contains entry functions for the USB driver.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003-2007 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

// CopyRight (c) Smart Technology Enablers, 1999 - 2000, All Rights Reserved.
/*
 * title:      sysfser.c
 *
 * purpose:    WDM driver for USB support of serial devices
 *
 * author:     rlc
 *             SystemSoft Corporation
 *             200 South 'A' Street, Suite 357
 *             Oxnard, CA  93030
 *             (805) 486-6686
 *
 * Copyright (C) 1997 by SystemSoft Corporation, as an unpublished
 * work.  All rights reserved.  Contains confidential information
 * and trade secrets proprietary to SystemSoft Corporation.
 * Disassembly or decompilation prohibited.
 */

#include <stdio.h>
#include "QCMAIN.h"
#include "QCSER.h"
#include "QCPTDO.h"
#include "QCWT.h"
#include "QCRD.h"
#include "QCINT.h"
#include "QCUTILS.h"
#include "QCPNP.h"
#include "QCDSP.h"
#include "QCMGR.h"

long LockCnt = 0;

// Global Data, allocated at init time

#ifdef DEBUG_MSGS
   BOOLEAN bDbgout = TRUE;  // switch to control Debug Messages
#endif // DEBUG_MSGS

UNICODE_STRING gServicePath;
KEVENT   gSyncEntryEvent;
char     gDeviceName[255];
USHORT   ucHdlCnt = 0;
QCSER_VENDOR_CONFIG gVendorConfig;
KSPIN_LOCK gPnpSpinLock;
int        gPnpState = QC_PNP_IDLE;

/****************************************************************************
 *
 * function: DriverEntry
 *
 * purpose:  Installable driver initialization entry point, called directly
 *           by the I/O system.
 *
 * arguments:DriverObject = pointer to the driver object
 *           RegistryPath = pointer to a unicode string representing the path
 *
 * returns:  NT status
 *
 */
NTSTATUS DriverEntry
(
   IN PDRIVER_OBJECT DriverObject,
   IN PUNICODE_STRING RegistryPath
)
{
   ULONG i;
   ANSI_STRING asDevName;
   char *p;
   NTSTATUS ntStatus;

   // Create dispatch points for device control, create, close.
   gDeviceName[0] = 0;
   KeInitializeSpinLock(&gPnpSpinLock);
   gPnpState = QC_PNP_IDLE;

   gVendorConfig.DriverResident = 0;

   gPortDOList = NULL;
   ntStatus = RtlUnicodeStringToAnsiString
              (
                 &asDevName,
                 RegistryPath,
                 TRUE
              );

   if ( !NT_SUCCESS( ntStatus ) )
   {
      KdPrint(("qcusb: Failure at DriverEntry.\n"));
      return STATUS_UNSUCCESSFUL;
   }

   KeInitializeEvent(&gSyncEntryEvent, SynchronizationEvent, TRUE);
   // KeSetEvent(&gSyncEntryEvent,IO_NO_INCREMENT,FALSE);

   p = asDevName.Buffer + asDevName.Length - 1;
   while ( p > asDevName.Buffer )
   {
      if (*p == '\\')
      {
         p++;
         break;
      }
      p--;
   }

   RtlZeroMemory(gDeviceName, 255);
   RtlZeroMemory((PVOID)(&gVendorConfig), sizeof(QCSER_VENDOR_CONFIG));
   if ((strlen(p) == 0) || (strlen(p) >= 255)) // invalid name
   {
      strcpy(gDeviceName, "qc_unknown");
   }
   strcpy(gDeviceName, p);

   gVendorConfig.DebugMask  = 0x70000074;
   gVendorConfig.DebugLevel = 4;
   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("\n<%s> DriverEntry (Build: %s/%s)\n", gDeviceName, __DATE__, __TIME__)
   );
   QCSER_DbgPrintG2
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("\n<%s> DriverEntry RegPath:\n   %s\n", gDeviceName, asDevName.Buffer)
   );
   RtlFreeAnsiString(&asDevName);

   // Store the service path
   ntStatus = AllocateUnicodeString
              (
                 &gServicePath,
                 RegistryPath->Length,
                 "gServicePath"
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("\n<%s> DriverEntry: gServicePath failure", gDeviceName)
      );
      _zeroUnicode(gServicePath);
   }
   else
   {
      RtlCopyUnicodeString(&gServicePath, RegistryPath);
   }

   // Assign dispatch routines
   // for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
   // {
   //    DriverObject->MajorFunction[i] = QCDSP_QueuedDispatch;
   // }
   DriverObject->MajorFunction[IRP_MJ_CREATE]         = QCDSP_QueuedDispatch;
   DriverObject->MajorFunction[IRP_MJ_CLOSE]          = QCDSP_QueuedDispatch;
   DriverObject->MajorFunction[IRP_MJ_CLEANUP]        = QCDSP_QueuedDispatch;

   DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS]  = QCWT_Write;
   DriverObject->MajorFunction[IRP_MJ_WRITE]          = QCWT_Write;
   DriverObject->MajorFunction[IRP_MJ_READ]           = QCRD_Read;
   DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = QCDSP_QueuedDispatch;
   DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = QCDSP_QueuedDispatch;

   DriverObject->MajorFunction[IRP_MJ_PNP]               = QCDSP_QueuedDispatch;
   DriverObject->MajorFunction[IRP_MJ_POWER]             = QCDSP_QueuedDispatch; 
   DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION] = QCDSP_QueuedDispatch;
   DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION]   = QCDSP_QueuedDispatch;
   DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL]    = QCDSP_QueuedDispatch;

   DriverObject->DriverUnload                       = QCMAIN_Unload;
   DriverObject->DriverExtension->AddDevice         = QCPNP_AddDevice;

   ntStatus = QCMGR_StartManagerThread();
   if ( !NT_SUCCESS( ntStatus ) )
   {
      KdPrint(("<%s> MgrThread Failure\n", gDeviceName));
      return STATUS_UNSUCCESSFUL;
   }

   return STATUS_SUCCESS;
}  //DriverEntry

void QCSER_PostRemovalNotification(PDEVICE_EXTENSION pDevExt)
{
   PIRP tmpIrp;
   NTSTATUS nts = STATUS_SUCCESS;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
   tmpIrp = pDevExt->pNotificationIrp;
   if (tmpIrp != NULL)
   {
      pDevExt->pNotificationIrp = NULL;
      nts = SerialNotifyClient(tmpIrp, QCOMSER_REMOVAL_NOTIFICATION);
      if (nts == STATUS_SUCCESS)
      {
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
}

NTSTATUS QCMAIN_IrpCompletionSetEvent
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PIRP Irp,
   IN PVOID Context
)
{
   ULONG ulResult;
   PKEVENT pEvent = Context;


   if(Irp->PendingReturned)
   {
      _IoMarkIrpPending(Irp);
   }

   ulResult = KeSetEvent(pEvent, IO_NO_INCREMENT, FALSE);

   // let our dispatch routine complete it
   return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS _QcIoCompleteRequest(IN PIRP Irp, IN CCHAR  PriorityBoost)
{
   NTSTATUS nts = Irp->IoStatus.Status;

   if (nts == STATUS_PENDING)
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> ERROR: IRP STATUS_PENDING\n", gDeviceName)
      );
      #ifdef DEBUG_MSGS
      _asm int 3;
      #endif
   }

   IoCompleteRequest(Irp, PriorityBoost);
   return nts;
}

NTSTATUS QcCompleteRequest(IN PIRP Irp, IN NTSTATUS status, IN ULONG_PTR info)
{
   Irp->IoStatus.Status = status;
   Irp->IoStatus.Information = info;
   IoCompleteRequest(Irp, IO_NO_INCREMENT);
   return status;
} // QcCompleteRequest

/*********************************************************************
 *
 * function:   QCSER_CleanupDeviceExtensionBuffers
 *
 * purpose: 
 *
 * arguments:  DeviceObject = adr( device object )
 *
 * returns:    NT status
 *
 */

NTSTATUS QCSER_CleanupDeviceExtensionBuffers(IN PDEVICE_OBJECT DeviceObject)
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   USHORT wIndex, i;
   PVXD_WDM_IO_CONTROL_BLOCK pQueEntry;
   PVXD_WDM_IO_CONTROL_BLOCK pNextEntry;
   PQCSER_FdoCollectionType fdoPtr;
   #ifdef QCUSB_MULTI_WRITES
   PLIST_ENTRY headOfList;
   PQCMWT_BUFFER pWtBuf;
   #endif // QCUSB_MULTI_WRITES
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   pDevExt = DeviceObject->DeviceExtension;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> CleanupDeviceExtension: enter\n", pDevExt->PortName)
   );

   QCUTILS_CleanupReadWriteQueues(pDevExt);
   QCUTILS_FreeReadWriteQueues(pDevExt);

   QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
   fdoPtr = pDevExt->FdoChain;
   while (fdoPtr != NULL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> ERR: FDO Chain - 0x%p/0x%p\n", pDevExt->PortName,
           fdoPtr, DeviceObject)
      );
      pDevExt->FdoChain = pDevExt->FdoChain->Next;
      _ExFreePool(fdoPtr);
      fdoPtr = pDevExt->FdoChain;
   }
   QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

   // Free memory allocated for descriptors
   if (pDevExt -> pUsbDevDesc)
   {
      ExFreePool( pDevExt -> pUsbDevDesc );
      pDevExt -> pUsbDevDesc = NULL;
   }
   if (pDevExt -> pUsbConfigDesc)
   {
      ExFreePool( pDevExt -> pUsbConfigDesc );
      pDevExt -> pUsbConfigDesc = NULL;
   }
   
   if (pDevExt -> pPerfstats )
   {
      ExFreePool( pDevExt -> pPerfstats );
      pDevExt -> pPerfstats = NULL;
   }
   if ( pDevExt -> pCommProp )
   {
      ExFreePool( pDevExt -> pCommProp );
      pDevExt -> pCommProp = NULL;
   }
   if ( pDevExt -> pSerialStatus )
   {
      ExFreePool( pDevExt -> pSerialStatus );
      pDevExt -> pSerialStatus = NULL;
   }
   if ( pDevExt -> pSerialHandflow )
   {
      ExFreePool( pDevExt -> pSerialHandflow );
      pDevExt -> pSerialHandflow = NULL;
   }
   if( pDevExt -> pSerialTimeouts )
   {
      ExFreePool( pDevExt -> pSerialTimeouts );
      pDevExt -> pSerialTimeouts = NULL;
   }
   if( pDevExt -> pSerialChars )
   {
      ExFreePool( pDevExt -> pSerialChars );
      pDevExt -> pSerialChars = NULL;
   }
   if ( pDevExt->pByteStuffingBuffer )
   {
      ExFreePool( pDevExt->pByteStuffingBuffer );
      pDevExt->pByteStuffingBuffer = NULL;
   }

   // Free up any interface structures in the device extension.
   for (wIndex = 0; wIndex < MAX_INTERFACE; wIndex++)
   {
      if (pDevExt -> Interface[wIndex] != NULL)
      {
         ExFreePool( pDevExt -> Interface[wIndex] );
         pDevExt -> Interface[wIndex] = NULL;
      }
   }

   _freeUcBuf(pDevExt->ucsIfaceSymbolicLinkName);
   _freeUcBuf(pDevExt->ucLoggingDir);
   _zeroUnicode(pDevExt->ucLoggingDir);
   if (pDevExt->pucReadBufferStart)
   {
      ExFreePool(pDevExt->pucReadBufferStart);
      pDevExt->pucReadBufferStart = NULL;
   }
   if (pDevExt->pL2ReadBuffer != NULL)
   {
      for (i = 0; i < pDevExt->NumberOfL2Buffers; i++)
      {
         if (pDevExt->pL2ReadBuffer[i].Buffer != NULL)
         {
            ExFreePool(pDevExt->pL2ReadBuffer[i].Buffer);
            pDevExt->pL2ReadBuffer[i].Buffer = NULL;
            #ifdef QCUSB_MULTI_READS
            if (pDevExt->pL2ReadBuffer[i].Irp != NULL)
            {
               IoFreeIrp(pDevExt->pL2ReadBuffer[i].Irp);
               pDevExt->pL2ReadBuffer[i].Irp = NULL;
            }
            #endif // QCUSB_MULTI_READS
         }
      }
      ExFreePool(pDevExt->pL2ReadBuffer);
      pDevExt->pL2ReadBuffer = NULL;
   }

   #ifdef QCUSB_MULTI_WRITES
   QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
   while (!IsListEmpty(&pDevExt->MWriteIdleQueue))
   {
      headOfList = RemoveHeadList(&pDevExt->MWriteIdleQueue);
      pWtBuf = CONTAINING_RECORD(headOfList, QCMWT_BUFFER, List);
      if (pWtBuf->Irp != NULL)
      {
         IoFreeIrp(pWtBuf->Irp);
      }
      ExFreePool(pWtBuf);
   }
   QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);
   #endif // QCUSB_MULTI_WRITES

   QcEmptyCompletionQueue
   (
      pDevExt,
      &pDevExt->RdCompletionQueue,
      &pDevExt->ReadSpinLock,
      QCUSB_IRP_TYPE_RIRP
   );
   QcEmptyCompletionQueue
   (
      pDevExt,
      &pDevExt->WtCompletionQueue,
      &pDevExt->WriteSpinLock,
      QCUSB_IRP_TYPE_WIRP
   );
   QcEmptyCompletionQueue
   (
      pDevExt,
      &pDevExt->CtlCompletionQueue,
      &pDevExt->ControlSpinLock,
      QCUSB_IRP_TYPE_CIRP
   );
   QcEmptyCompletionQueue
   (
      pDevExt,
      &pDevExt->SglCompletionQueue,
      &pDevExt->SingleIrpSpinLock,
      QCUSB_IRP_TYPE_CIRP
   );

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> CleanupDeviceExtension: Exit\n", pDevExt->PortName)
   );

   return ntStatus;
} // QCSER_CleanupDeviceExtensionBuffers

NTSTATUS CancelNotificationIrp(PDEVICE_OBJECT pDevObj)
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
   pIrp = pDevExt->pNotificationIrp;
   if ( pIrp )
   {

      if (IoSetCancelRoutine(pIrp, NULL) != NULL)
      {
         pDevExt->pNotificationIrp = NULL;
         pIrp->IoStatus.Status = STATUS_CANCELLED;
         pIrp->IoStatus.Information = 0;

         InsertTailList(&pDevExt->SglCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
         KeSetEvent(&pDevExt->InterruptEmptySglQueueEvent, IO_NO_INCREMENT, FALSE);
      }
   }
   QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

   return ntStatus;
}

NTSTATUS CancelWOMIrp(PDEVICE_OBJECT pDevObj)
{   
   PDEVICE_EXTENSION   pDevExt;
   NTSTATUS            ntStatus = STATUS_SUCCESS;
   PIRP                pIrp;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   pDevExt = pDevObj -> DeviceExtension;

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);
   pIrp = pDevExt->pWaitOnMaskIrp;

   if (pIrp)
   {
      if (IoSetCancelRoutine(pIrp, NULL) != NULL)
      {
         pIrp->IoStatus.Status = STATUS_CANCELLED;
         pIrp->IoStatus.Information = 0;
         pDevExt->pWaitOnMaskIrp = NULL;
         InsertTailList(&pDevExt->SglCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
         KeSetEvent(&pDevExt->InterruptEmptySglQueueEvent, IO_NO_INCREMENT, FALSE);
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> CxlWOM: NULL IRP!\n", pDevExt->PortName)
         );
      }

      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
   }
   else
   {
      QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
   }

   return ntStatus;
}  // CancelWOMIrp

/*********************************************************************
 *
 * function:   cancelAllIrps
 *
 * purpose:    clean up device object removal
 *
 * arguments:  pDevObj = adr(device object)
 *
 * returns:    none
 *
 */
VOID cancelAllIrps( PDEVICE_OBJECT pDevObj )
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus;

   pDevExt = pDevObj -> DeviceExtension;

   if_DevState( DEVICE_STATE_CLIENT_PRESENT )
   {
      if (pDevExt->pWaitOnMaskIrp)
      {
         if (!(pDevExt->ulWaitMask & (ULONG) pDevExt->usCurrUartState & SERIAL_EV_DSR))
         {  // to force the WOM Irp to be completed
            _int3;
            pDevExt->usCurrUartState |= SERIAL_EV_DSR;
            pDevExt->ulWaitMask |= SERIAL_EV_DSR;
         }
         ProcessNewUartState(pDevExt, 0, SERIAL_EV_DSR, FALSE);
      }
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> cancelAllIrps - cancel R/W threads\n", pDevExt->PortName)
   );
   ntStatus = CancelReadThread(pDevExt, 2);
   ntStatus = CancelWriteThread(pDevExt, 1);

} //cancellAllIrps

VOID RemoveSymbolicLinks( PDEVICE_OBJECT pDevObj )
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus;
   ULONG dwStatus;
   UNICODE_STRING ucLinkName;
   UNICODE_STRING ucLinkNameNone;
   UNICODE_STRING ucValueEntryName;
   UNICODE_STRING usValueName;
   OBJECT_ATTRIBUTES ObjectAttributes;   

   pDevExt = pDevObj->DeviceExtension;
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("\n<%s> RemoveSymbolicLinks: 0\n", pDevExt->PortName)
   );


   // here we delete the symbolic links from the device extension
   _dbgPrintUnicodeString
   (
      &pDevExt->ucsUnprotectedLink,
      "IoDeleteSymbolicLink"
   );
   ntStatus = IoDeleteSymbolicLink(&pDevExt->ucsUnprotectedLink);
   _freeString(pDevExt->ucsUnprotectedLink);

   /****
   _dbgPrintUnicodeString
   (
      &pDevExt->ucsProtectedLink,
      "IoDeleteSymbolicLink"
   );
   ntStatus = IoDeleteSymbolicLink(&pDevExt->ucsProtectedLink);
   _freeString(pDevExt->ucsProtectedLink);
   *****/

   // here we delete the device map entry from the device extension
   _dbgPrintUnicodeString
   (
      &pDevExt->ucsDeviceMapEntry,
      "IoDeleteSymbolicLink"
   );
   ntStatus = RtlDeleteRegistryValue
              (
                 RTL_REGISTRY_DEVICEMAP,
                 L"SERIALCOMM",
                 pDevExt->ucsDeviceMapEntry.Buffer
              );
   _freeString(pDevExt->ucsDeviceMapEntry);
   _freeString(pDevExt->ucsPortName);
   pDevExt->bSymLinksValid = FALSE;
}  //RemoveSymbolicLinks()

/*********************************************************************
 *
 * function:   QCMAIN_Unload
 *
 * purpose:    Free all the allocated resources, and unload driver
 *
 * arguments:  DriverObject = adr(driver object)
 *
 * returns:    none
 *
 *
 *
 **********************************************************************/

VOID QCMAIN_Unload( IN PDRIVER_OBJECT DriverObject )
{
   QCMGR_CancelManagerThread();

   // Free any global resources
   _freeString(gServicePath);

   DbgPrint("   ================================\n");
   DbgPrint("     Driver Unloaded by System\n");
   DbgPrint("       Version: %-10s         \n", QCUSB_DRIVER_VERSION);
   DbgPrint("       Device:  %-10s         \n", gDeviceName);
   DbgPrint("       Port:    %-50s\n", gVendorConfig.PortName);
   DbgPrint("   ================================\n");
}  //Unload

VOID CancelNotificationRoutine( PDEVICE_OBJECT CalledDO, PIRP pIrp )
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
   IoSetCancelRoutine(pIrp, NULL);  // not necessary
   ASSERT(pDevExt->pNotificationIrp==pIrp);
   pDevExt->pNotificationIrp = NULL;

   pIrp->IoStatus.Status = STATUS_CANCELLED;
   pIrp->IoStatus.Information = 0;

   InsertTailList(&pDevExt->SglCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
   KeSetEvent(&pDevExt->InterruptEmptySglQueueEvent, IO_NO_INCREMENT, FALSE);

   QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);
} //  CancelNotificationRoutine

VOID CancelWOMRoutine( PDEVICE_OBJECT CalledDO, PIRP pIrp )
{
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   KIRQL irql = KeGetCurrentIrql();
   PDEVICE_OBJECT pDevObj = QCPTDO_FindPortDOByFDO(CalledDO, irql);
   PDEVICE_EXTENSION pDevExt = pDevObj->DeviceExtension;
   
   IoReleaseCancelSpinLock(pIrp->CancelIrql);

   QcAcquireSpinLock(&pDevExt->SingleIrpSpinLock, &levelOrHandle);

   ASSERT(pDevExt->pWaitOnMaskIrp==pIrp);
   pDevExt->pWaitOnMaskIrp = NULL;
   IoSetCancelRoutine(pIrp, NULL);  // not necessary here 

   pIrp->IoStatus.Status = STATUS_CANCELLED;
   pIrp->IoStatus.Information = 0;

   InsertTailList(&pDevExt->SglCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
   KeSetEvent(&pDevExt->InterruptEmptySglQueueEvent, IO_NO_INCREMENT, FALSE);

   QcReleaseSpinLock(&pDevExt->SingleIrpSpinLock, levelOrHandle);

   return;
} //  CancelWOMRoutine

void QCSER_DispatchDebugOutput
(
   PIRP               Irp,
   PIO_STACK_LOCATION irpStack,
   PDEVICE_OBJECT     CalledDO,
   PDEVICE_OBJECT     DeviceObject,
   KIRQL              irql
)
{
   PDEVICE_EXTENSION pDevExt = NULL;
   char msgBuf[256], *msg;

   msg = msgBuf;
   if (DeviceObject == NULL)
   {
      if (gVendorConfig.DebugLevel < QCSER_DBG_LEVEL_DETAIL)
      {
         return;
      }
      sprintf(msg, "<%s 0X%p::0x%p:IRQL-%d:0x%p> ", gDeviceName, CalledDO, DeviceObject, irql, Irp);
   }
   else
   {
      pDevExt = DeviceObject->DeviceExtension;
      if (pDevExt->DebugLevel < QCSER_DBG_LEVEL_DETAIL)
      {
         // HG_DBG return;
      }
      sprintf(msg, "<%s 0X%p::0x%p:IRQL-%d:0x%p> ", pDevExt->PortName, CalledDO, DeviceObject, irql, Irp);
   }
   msg += strlen(msg);
   switch (irpStack->MajorFunction)
   {
      case IRP_MJ_CREATE:
         sprintf(msg, "IRP_MJ_CREATE");
         break;
      case IRP_MJ_CLOSE:
         sprintf(msg, "IRP_MJ_CLOSE");
         break;
      case IRP_MJ_CLEANUP:
         sprintf(msg, "IRP_MJ_CLEANUP");
         break;
      case IRP_MJ_DEVICE_CONTROL:
      {
         ULONG ioMethod = irpStack->Parameters.DeviceIoControl.IoControlCode & 0x00000003;

         switch (irpStack->Parameters.DeviceIoControl.IoControlCode)
         {
            case IOCTL_SERIAL_SET_BAUD_RATE:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_BAUD_RATE");
               break;
            case IOCTL_SERIAL_SET_QUEUE_SIZE:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_QUEUE_SIZE");
               break;
            case IOCTL_SERIAL_SET_LINE_CONTROL:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_LINE_CONTROL");
               break;
            case IOCTL_SERIAL_SET_BREAK_ON:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_BREAK_ON");
               break;
            case IOCTL_SERIAL_SET_BREAK_OFF:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_BREAK_OFF");
               break;
            case IOCTL_SERIAL_IMMEDIATE_CHAR:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_IMMEDIATE_CHAR");
               break;
            case IOCTL_SERIAL_SET_TIMEOUTS:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_TIMEOUTS");
               break;
            case IOCTL_SERIAL_GET_TIMEOUTS:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_TIMEOUTS");
               break;
            case IOCTL_SERIAL_SET_DTR:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_DTR");
               break;
            case IOCTL_SERIAL_CLR_DTR:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_CLR_DTR");
               break;
            case IOCTL_SERIAL_RESET_DEVICE:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_RESET_DEVICE");
               break;
            case IOCTL_SERIAL_SET_RTS:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_RTS");
               break;
            case IOCTL_SERIAL_CLR_RTS:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_CLR_RTS");
               break;
            case IOCTL_SERIAL_SET_XOFF:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_XOFF");
               break;
            case IOCTL_SERIAL_SET_XON:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_XON");
               break;
            case IOCTL_SERIAL_GET_WAIT_MASK:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_WAIT_MASK");
               break;
            case IOCTL_SERIAL_SET_WAIT_MASK:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_WAIT_MASK");
               break;
            case IOCTL_SERIAL_WAIT_ON_MASK:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_WAIT_ON_MASK");
               break;
            case IOCTL_SERIAL_PURGE:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_PURGE");
               break;
            case IOCTL_SERIAL_GET_BAUD_RATE:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_BAUD_RATE");
               break;
            case IOCTL_SERIAL_GET_LINE_CONTROL:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_LINE_CONTROL");
               break;
            case IOCTL_SERIAL_GET_CHARS:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_CHARS");
               break;
            case IOCTL_SERIAL_SET_CHARS:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_CHARS");
               break;
            case IOCTL_SERIAL_GET_HANDFLOW:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_HANDFLOW");
               break;
            case IOCTL_SERIAL_SET_HANDFLOW:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_HANDFLOW");
               break;
            case IOCTL_SERIAL_GET_MODEMSTATUS:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_MODEMSTATUS");
               break;
            case IOCTL_SERIAL_GET_COMMSTATUS:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_COMMSTATUS");
               break;
            case IOCTL_SERIAL_XOFF_COUNTER:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_XOFF_COUNTER");
               break;
            case IOCTL_SERIAL_GET_PROPERTIES:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_PROPERTIES");
               break;
            case IOCTL_SERIAL_GET_DTRRTS:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_DTRRTS");
               break;

            // begin_winioctl
            case IOCTL_SERIAL_LSRMST_INSERT:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_LSRMST_INSERT");
               break;
            case IOCTL_SERENUM_EXPOSE_HARDWARE:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERENUM_EXPOSE_HARDWARE");
               break;
            case IOCTL_SERENUM_REMOVE_HARDWARE:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERENUM_REMOVE_HARDWARE");
               break;
            case IOCTL_SERENUM_PORT_DESC:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERENUM_PORT_DESC");
               break;
            case IOCTL_SERENUM_GET_PORT_NAME:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERENUM_GET_PORT_NAME");
               break;
            // end_winioctl

            case IOCTL_SERIAL_CONFIG_SIZE:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_CONFIG_SIZE");
               break;
            case IOCTL_SERIAL_GET_COMMCONFIG:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_COMMCONFIG");
               break;
            case IOCTL_SERIAL_SET_COMMCONFIG:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_COMMCONFIG");
               break;

            case IOCTL_SERIAL_GET_STATS:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_STATS");
               break;
            case IOCTL_SERIAL_CLEAR_STATS:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_CLEAR_STATS");
               break;
            case IOCTL_SERIAL_GET_MODEM_CONTROL:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_GET_MODEM_CONTROL");
               break;
            case IOCTL_SERIAL_SET_MODEM_CONTROL:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_MODEM_CONTROL");
               break;
            case IOCTL_SERIAL_SET_FIFO_CONTROL:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_SERIAL_SET_FIFO_CONTROL");
               break;

            case WIN2K_DUN_IOCTL:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/WIN2K_DUN_IOCTL");
               break;
            case IOCTL_QCOMSER_WAIT_NOTIFY:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_QCOMSER_WAIT_NOTIFY");
               break;
            case IOCTL_QCOMSER_DRIVER_ID:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_QCOMSER_DRIVER_ID");
               break;
            case IOCTL_QCSER_GET_SERVICE_KEY:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_QCSER_GET_SERVICE_KEY");
               break;
            case IOCTL_QCUSB_SYSTEM_POWER:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_QCUSB_SYSTEM_POWER");
               break;
            case IOCTL_QCUSB_DEVICE_POWER:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_QCUSB_DEVICE_POWER");
               break;
            case IOCTL_QCUSB_QCDEV_NOTIFY:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/IOCTL_QCUSB_QCDEV_NOTIFY");
               break;
            default:
               sprintf(msg, "IRP_MJ_DEVICE_CONTROL/ControlCode_0x%llx",
                       irpStack->Parameters.DeviceIoControl.IoControlCode);
         }
         msg += strlen(msg);
         sprintf(msg, " m%u", ioMethod);
         break;
      }
      case IRP_MJ_INTERNAL_DEVICE_CONTROL:
      {
         switch (irpStack->Parameters.DeviceIoControl.IoControlCode)
         {
            case IOCTL_SERIAL_INTERNAL_DO_WAIT_WAKE:
               sprintf(msg, "IRP_MJ_INTERNAL_DEVICE_CONTROL/IOCTL_SERIAL_INTERNAL_DO_WAIT_WAKE");
               break;
            case IOCTL_SERIAL_INTERNAL_CANCEL_WAIT_WAKE:
               sprintf(msg, "IRP_MJ_INTERNAL_DEVICE_CONTROL/IOCTL_SERIAL_INTERNAL_CANCEL_WAIT_WAKE");
               break;
            case IOCTL_SERIAL_INTERNAL_BASIC_SETTINGS:
               sprintf(msg, "IRP_MJ_INTERNAL_DEVICE_CONTROL/IOCTL_SERIAL_INTERNAL_BASIC_SETTINGS");
               break;
            case IOCTL_SERIAL_INTERNAL_RESTORE_SETTINGS:
               sprintf(msg, "IRP_MJ_INTERNAL_DEVICE_CONTROL/IOCTL_SERIAL_INTERNAL_RESTORE_SETTINGS");
               break;
            default:
               sprintf(msg, "IRP_MJ_INTERNAL_DEVICE_CONTROL/ControlCode_0x%llx",
                       irpStack->Parameters.DeviceIoControl.IoControlCode);
         }
         break;
      }
      case IRP_MJ_POWER:
         switch (irpStack->MinorFunction)
         {
            case IRP_MN_WAIT_WAKE:
               sprintf(msg, "IRP_MJ_POWER/IRP_MN_WAIT_WAKE");
               break;
            case IRP_MN_POWER_SEQUENCE:
               sprintf(msg, "IRP_MJ_POWER/IRP_MN_POWER_SEQUENCE");
               break;
            case IRP_MN_SET_POWER:
               sprintf(msg, "IRP_MJ_POWER/IRP_MN_SET_POWER");
               break;
            case IRP_MN_QUERY_POWER:
               sprintf(msg, "IRP_MJ_POWER/IRP_MN_QUERY_POWER");
               break;
            default:
               sprintf(msg, "IRP_MJ_POWER/MinorFunc_0x%x", irpStack->MinorFunction);
         }
         break;
      case IRP_MJ_PNP:
      {
         switch (irpStack->MinorFunction)
         {
            case IRP_MN_QUERY_CAPABILITIES:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_QUERY_CAPABILITIES");
               break;
            case IRP_MN_START_DEVICE:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_START_DEVICE");
               break;
            case IRP_MN_QUERY_STOP_DEVICE:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_QUERY_STOP_DEVICE");
               break;
            case IRP_MN_CANCEL_STOP_DEVICE:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_CANCEL_STOP_DEVICE");
               break;
            case IRP_MN_STOP_DEVICE:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_STOP_DEVICE");
               break;
            case IRP_MN_QUERY_REMOVE_DEVICE:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_QUERY_REMOVE_DEVICE");
               break;
            case IRP_MN_CANCEL_REMOVE_DEVICE:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_CANCEL_REMOVE_DEVICE");
               break;
            case IRP_MN_SURPRISE_REMOVAL:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_SURPRISE_REMOVAL");
               break;
            case IRP_MN_REMOVE_DEVICE:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_REMOVE_DEVICE");
               break;
            case IRP_MN_QUERY_ID:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_QUERY_ID");
               break;
            case IRP_MN_QUERY_PNP_DEVICE_STATE:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_QUERY_PNP_DEVICE_STATE");
               break;
            case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_QUERY_RESOURCE_REQUIREMENTS");
               break;
            case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_FILTER_RESOURCE_REQUIREMENTS");
               break;
            case IRP_MN_QUERY_DEVICE_RELATIONS:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_QUERY_DEVICE_RELATIONS(0x%x)",
                       irpStack->Parameters.QueryDeviceRelations.Type);
               break;
            case IRP_MN_QUERY_LEGACY_BUS_INFORMATION:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_QUERY_LEGACY_BUS_INFORMATION");
               break;
            case IRP_MN_QUERY_INTERFACE:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_QUERY_INTERFACE");
               break;
            case IRP_MN_QUERY_DEVICE_TEXT:
               sprintf(msg, "IRP_MJ_PNP/IRP_MN_QUERY_DEVICE_TEXT");
               break;
            default:
               sprintf(msg, "IRP_MJ_PNP/MINOR_FUNCTION_0x%x",
                       irpStack->MinorFunction);
         }
         break;
      }
      case IRP_MJ_SYSTEM_CONTROL:
         switch (irpStack->MinorFunction)
         {
            case IRP_MN_CHANGE_SINGLE_INSTANCE:
               sprintf(msg, "IRP_MJ_SYSTEM_CONTROL/IRP_MN_CHANGE_SINGLE_INSTANCE");
               break;
            case IRP_MN_CHANGE_SINGLE_ITEM:
               sprintf(msg, "IRP_MJ_SYSTEM_CONTROL/IRP_MN_CHANGE_SINGLE_ITEM");
               break;
            case IRP_MN_DISABLE_COLLECTION:
               sprintf(msg, "IRP_MJ_SYSTEM_CONTROL/IRP_MN_DISABLE_COLLECTION");
               break;
            case IRP_MN_DISABLE_EVENTS:
               sprintf(msg, "IRP_MJ_SYSTEM_CONTROL/IRP_MN_DISABLE_EVENTS");
               break;
            case IRP_MN_ENABLE_COLLECTION:
               sprintf(msg, "IRP_MJ_SYSTEM_CONTROL/IRP_MN_ENABLE_COLLECTION");
               break;
            case IRP_MN_ENABLE_EVENTS:
               sprintf(msg, "IRP_MJ_SYSTEM_CONTROL/IRP_MN_ENABLE_EVENTS");
               break;
            case IRP_MN_EXECUTE_METHOD:
               sprintf(msg, "IRP_MJ_SYSTEM_CONTROL/IRP_MN_EXECUTE_METHOD");
               break;
            case IRP_MN_QUERY_ALL_DATA:
               sprintf(msg, "IRP_MJ_SYSTEM_CONTROL/IRP_MN_QUERY_ALL_DATA");
               break;
            case IRP_MN_QUERY_SINGLE_INSTANCE:
               sprintf(msg, "IRP_MJ_SYSTEM_CONTROL/IRP_MN_QUERY_SINGLE_INSTANCE");
               break;
            case IRP_MN_REGINFO:
               sprintf(msg, "IRP_MJ_SYSTEM_CONTROL/IRP_MN_REGINFO");
               break;
            #if defined(QCSER_VERSION_WXP_FRE) || defined(QCSER_VERSION_WXP_CHK)
            case IRP_MN_REGINFO_EX:
               sprintf(msg, "IRP_MJ_SYSTEM_CONTROL/IRP_MN_REGINFO_EX: 0x%p", irpStack->Parameters.WMI.ProviderId);
               break;
            #endif
            default:
               sprintf(msg, "IRP_MJ_SYSTEM_CONTROL/0x%x", irpStack->MinorFunction);
               break;
         }
         break;

      case IRP_MJ_QUERY_INFORMATION:
         sprintf(msg, "IRP_MJ_QUERY_INFORMATION");
         break;
      case IRP_MJ_SET_INFORMATION:
         sprintf(msg, "IRP_MJ_SET_INFORMATION");
         break;
      case IRP_MJ_FLUSH_BUFFERS:
         sprintf(msg, "IRP_MJ_FLUSH_BUFFERS");
         break;
      default:
         sprintf(msg, "MAJOR_FUNCTIN: 0x%x", irpStack->MajorFunction);
         break;
   }
   sprintf(msg+strlen(msg), "\n");

   if (pDevExt != NULL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         (msgBuf)
      );
   }
   else
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         (msgBuf)
      );
   }

   // HG_DBG
   // DbgPrint(msgBuf);
}

NTSTATUS QCSER_AddToFdoCollection(PDEVICE_EXTENSION pDevExt, PDEVICE_OBJECT fdo)
{
   PQCSER_FdoCollectionType fdoPtr;
   int cnt = 1;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> AddToFdoCollection: 0x%p\n", pDevExt->PortName, fdo)
   );

   QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
   if (pDevExt->FdoChain == NULL)
   {
      pDevExt->FdoChain = _ExAllocatePool
                          (
                             NonPagedPool,
                             sizeof(QCSER_FdoCollectionType),
                             ("AddToFdoCollection")
                          );
      if (pDevExt->FdoChain == NULL)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> AddToFdoCollection: NO_MEM for fdo 0x%p\n", pDevExt->PortName, fdo)
         );
         QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
         return STATUS_NO_MEMORY;
      }
      pDevExt->FdoChain->Fdo = fdo;
      pDevExt->FdoChain->Next = NULL;
   }
   else
   {
      fdoPtr = pDevExt->FdoChain;
      while (fdoPtr->Next != NULL)
      {
         fdoPtr = fdoPtr->Next;
         ++cnt;
      }
      fdoPtr->Next = _ExAllocatePool
                     (
                        NonPagedPool,
                        sizeof(QCSER_FdoCollectionType),
                        ("QCSER_FdoCollectionType_1")
                     );
      if (fdoPtr->Next == NULL)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> AddToFdoCollection: NO_MEM for fdoPtr-0x%p cnt %d\n", pDevExt->PortName, fdo, cnt)
         );
         QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
         return STATUS_NO_MEMORY;
      }
      ++cnt;
      fdoPtr = fdoPtr->Next;
      fdoPtr->Fdo = fdo;
      fdoPtr->Next = NULL;
   }
   QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> AddToFdoCollection: 0x%p cnt %d\n", pDevExt->PortName, fdo, cnt)
   );

   return STATUS_SUCCESS;
}  // QCSER_AddToFdoCollection

NTSTATUS QCSER_RemoveFdoFromCollection(PDEVICE_EXTENSION pDevExt, PDEVICE_OBJECT fdo)
{
   PQCSER_FdoCollectionType fdoPtr, preFdo;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> RemoveFdoFromCollection: 0x%p\n", pDevExt->PortName, fdo)
   );
   QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
   preFdo = fdoPtr = pDevExt->FdoChain;
   if (fdoPtr != NULL)
   {
      if (fdoPtr->Fdo == fdo)
      {
         pDevExt->FdoChain = fdoPtr->Next;
         QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
         ExFreePool(fdoPtr);
         return STATUS_SUCCESS;
      }
      else
      {
         fdoPtr = fdoPtr->Next;
      }
   }

   while (fdoPtr != NULL)
   {
      if (fdoPtr->Fdo == fdo)
      {
         preFdo->Next = fdoPtr->Next;
         break;
      }
      else
      {
         preFdo = fdoPtr;
         fdoPtr = fdoPtr->Next;
      }
   }
   QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

   if (fdoPtr != NULL)
   {
      ExFreePool(fdoPtr);
   }
   else
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> RemoveFdoFromCollection failure: 0x%p\n", pDevExt->PortName, fdo)
      );
   }

   return STATUS_SUCCESS;

}  // QCSER_RemoveFdoFromCollection
