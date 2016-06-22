/*===========================================================================
FILE: QCDSP.c

DESCRIPTION:
   This file contains dispatch functions.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003-2007 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#include "QCMAIN.h"
#include "QCDSP.h"
#include "QCSER.h"
#include "QCPTDO.h"
#include "QCWT.h"
#include "QCRD.h"
#include "QCINT.h"
#include "QCUTILS.h"
#include "QCPNP.h"
#include "QCMGR.h"
#include "QCPWR.h"
#include "QCDEV.h"

extern NTKERNELAPI VOID IoReuseIrp(IN OUT PIRP Irp, IN NTSTATUS Iostatus);

NTSTATUS QCDSP_DirectDispatch(IN PDEVICE_OBJECT CalledDO, IN PIRP Irp)
{
   PIO_STACK_LOCATION     irpStack;
   NTSTATUS               ntStatus;
   PDEVICE_EXTENSION      pDevExt;
   PFDO_DEVICE_EXTENSION  pFdoExt;
   PDEVICE_OBJECT         DeviceObject;
   BOOLEAN                bDevRemoved = FALSE;
   BOOLEAN                bForXwdm;
   KIRQL irql = KeGetCurrentIrql();
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCSER_DbgPrintG2
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("DDSP: ------------- Dispatch 0x%p ---------\n", Irp)
   );

   irpStack = IoGetCurrentIrpStackLocation(Irp);

   DeviceObject = QCPTDO_FindPortDOByFDO(CalledDO, irql);
   if (DeviceObject == NULL)
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CIRP,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> CIRP: No PTDO - 0x%p\n", gDeviceName, Irp)
      );
      QCSER_DispatchDebugOutput(Irp, irpStack, CalledDO, DeviceObject, irql);
      return QcCompleteRequest(Irp, STATUS_DELETE_PENDING, 0);
   }
   else
   {
      pDevExt = DeviceObject->DeviceExtension;
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CIRP,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> CIRP: 0x%p => t 0x%p[%d/%d:%d]\n", pDevExt->PortName, Irp,
        KeGetCurrentThread(), Irp->CurrentLocation, Irp->StackCount, irql)
   );

   if (pDevExt->StackDeviceObject == NULL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CIRP,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> CIRP: No PDO - 0x%p\n", pDevExt->PortName, Irp)
      );
      return QcCompleteRequest(Irp, STATUS_DELETE_PENDING, 0);
   }

   #ifdef QCUSB_STACK_IO_ON
   if ((CalledDO == DeviceObject) && inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      if ((pDevExt->bStackOpen == TRUE) ||
          (irpStack->MajorFunction == IRP_MJ_CREATE))
      {
         return QCDSP_SendIrpToStack(DeviceObject, Irp, "CIRP0");
      }
   }
   #endif 

   if (pDevExt->DebugMask > 0)
   {
      QCSER_DispatchDebugOutput(Irp, irpStack, CalledDO, DeviceObject, irql);
   }

   // Acquire remove lock except when getting IRP_MJ_CLOSE
   if (irpStack->MajorFunction != IRP_MJ_CLOSE)
   {
      if (irpStack->MajorFunction == IRP_MJ_CREATE)
      {
         // Because we will loss the create IRP when removing remove lock
         // for IRP_MJ_CLOSE, we do not supply tag here.
         ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, NULL);
      }
      else
      {
         ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, Irp);
      }
      if (!NT_SUCCESS(ntStatus))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CIRP,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> CIRP: (Ce 0x%x) 0x%p RMLerr-D\n", pDevExt->PortName, ntStatus, Irp)
         );
         return QcCompleteRequest(Irp, ntStatus, 0);
      }
      if (irpStack->MajorFunction == IRP_MJ_CREATE)
      {
         QcInterlockedIncrement(4, Irp, 0);
      }
      else
      {
         QcInterlockedIncrement(0, Irp, 0);
      }
   }

   QCSER_DbgPrint2
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> DSP-RML-0 <%ld, %ld, %ld, %ld> CRW <%ld/%ld, %ld/%ld, %ld/%ld>\n",
        pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
        pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3],
        pDevExt->Sts.lAllocatedCtls, pDevExt->Sts.lAllocatedDSPs,
        pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedRdMem,
        pDevExt->Sts.lAllocatedWrites, pDevExt->Sts.lAllocatedWtMem)
   );

   // Note: QCDEV_IsXwdmRequest() must be called at PASSIVE_LEVEL
   //       so the following call is questionable and driver should
   //       avoid using QCDSP_DirectDispatch()
   bForXwdm = QCDEV_IsXwdmRequest(pDevExt, Irp);
   return QCDSP_Dispatch(DeviceObject, CalledDO, Irp, &bDevRemoved, bForXwdm);

}  // QCDSP_DirectDispatch

NTSTATUS QCDSP_QueuedDispatch(IN PDEVICE_OBJECT CalledDO, IN PIRP Irp)
{
   ULONG                  ioControlCode;
   PIO_STACK_LOCATION     irpStack;
   NTSTATUS               ntStatus;
   PDEVICE_EXTENSION      pDevExt;
   PFDO_DEVICE_EXTENSION  pFdoExt;
   PDEVICE_OBJECT         DeviceObject;
   BOOLEAN                bDevRemoved = FALSE;
   ULONG                  ioMethod;
   BOOLEAN                is64BitIoctl;
   KIRQL irql = KeGetCurrentIrql();

   QCSER_DbgPrintG2
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("QDSP: ------------- Dispatch 0x%p ---------\n", Irp)
   );

   irpStack = IoGetCurrentIrpStackLocation(Irp);

   DeviceObject = QCPTDO_FindPortDOByFDO(CalledDO, irql);
   if (DeviceObject == NULL)
   {
      QCSER_DbgPrintG
      (
         (QCSER_DBG_MASK_CIRP | QCSER_DBG_MASK_CONTROL),
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> CIRP: No PTDO - 0x%p\n", gDeviceName, Irp)
      );
      QCSER_DispatchDebugOutput(Irp, irpStack, CalledDO, DeviceObject, irql);
      return QcCompleteRequest(Irp, STATUS_DELETE_PENDING, 0);
   }
   else
   {
      pDevExt = DeviceObject->DeviceExtension;
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CIRP,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> CIRP: 0x%p => t 0x%p[%d/%d:%d] FDO %p [0x%x,0x%x]\n", pDevExt->PortName, Irp,
        KeGetCurrentThread(), Irp->CurrentLocation, Irp->StackCount, irql, pDevExt->FDO,
        irpStack->MajorFunction, irpStack->MinorFunction)
   );

   ioControlCode = irpStack->Parameters.DeviceIoControl.IoControlCode;
   ioMethod = ioControlCode & 0x00000003;
   is64BitIoctl = ((ioControlCode & 0x400) != 0);

   #ifdef QCUSB_STACK_IO_ON
   if ((CalledDO == DeviceObject) && inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      if ((pDevExt->bStackOpen == TRUE) ||
          (irpStack->MajorFunction == IRP_MJ_CREATE))
      {
         return QCDSP_SendIrpToStack(DeviceObject, Irp, "CIRP");
      }
   }
   #endif

   if (pDevExt->DebugMask > 0)
   {
      QCSER_DispatchDebugOutput(Irp, irpStack, CalledDO, DeviceObject, irql);
   }

   if (irpStack->MajorFunction == IRP_MJ_DEVICE_CONTROL)
   {
      if (ioMethod >= METHOD_NEITHER)
      {
         // DbgPrint("<%s> CIRP: (Ce 0x%x) 0x%p bad ioMethod %u\n", pDevExt->PortName, STATUS_UNSUCCESSFUL, Irp, ioMethod);
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CIRP,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> CIRP: (Ce 0x%x) 0x%p bad ioMethod %u\n", pDevExt->PortName, STATUS_UNSUCCESSFUL, Irp, ioMethod)
         );
         return QcCompleteRequest(Irp, STATUS_UNSUCCESSFUL, 0);
      }
      if (is64BitIoctl == TRUE)
      {
         // DbgPrint("<%s> CIRP: (Ce 0x%x) 0x%p 64-bit ioctl, no support\n", pDevExt->PortName, STATUS_UNSUCCESSFUL, Irp);
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CIRP,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> CIRP: (Ce 0x%x) 0x%p 64-bit ioctl, no support\n", pDevExt->PortName, STATUS_UNSUCCESSFUL, Irp)
         );
         return QcCompleteRequest(Irp, STATUS_UNSUCCESSFUL, 0);
      }
   }

   if (irpStack->MajorFunction == IRP_MJ_POWER)
   {
      if (QCPWR_Prelude(Irp, pDevExt, &ntStatus) == TRUE)
      {
         return ntStatus;
      }
   }

   // Acquire remove lock except when getting IRP_MJ_CLOSE
   if (irpStack->MajorFunction != IRP_MJ_CLOSE)  // condition always TRUE
   {
      PVOID rmlTag = NULL;

      if (irpStack->MajorFunction != IRP_MJ_CREATE)
      {
         // Because we will loss the CREATE-IRP when removing remove lock
         // for IRP_MJ_CLOSE, we do not supply tag for IRP_MJ_CREATE
         rmlTag = (PVOID)Irp;
      }
      ntStatus = IoAcquireRemoveLock(pDevExt->pRemoveLock, rmlTag);
      if (!NT_SUCCESS(ntStatus))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CIRP,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> CIRP: (Ce 0x%x) 0x%p RMLerr-Q\n", pDevExt->PortName, ntStatus, Irp)
         );
         return QcCompleteRequest(Irp, ntStatus, 0);
      }
      if (irpStack->MajorFunction == IRP_MJ_CREATE)
      {
         QcInterlockedIncrement(4, rmlTag, 1);
      }
      else
      {
         QcInterlockedIncrement(0, rmlTag, 1);
      }
   }

   if (irpStack->MajorFunction == IRP_MJ_DEVICE_CONTROL)
   {
      if ((!inDevState(DEVICE_STATE_PRESENT_AND_STARTED)) &&  // device detached
          (pDevExt->bInService == FALSE)                  &&  // port not in use
          (gVendorConfig.DriverResident == 0))                // driver not resident
      {
         // QCSER_DbgPrint
         // (
         //    QCSER_DBG_MASK_CIRP,
         //    QCSER_DBG_LEVEL_ERROR,
         //    ("<%s> CIRP: (Ce 0x%x) 0x%p no DEV\n", pDevExt->PortName, STATUS_DELETE_PENDING, Irp)
         // );
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
         return QcCompleteRequest(Irp, STATUS_DELETE_PENDING, 0);
      }
   }

   QCSER_DbgPrint2
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> DSP-RML-0 <%ld, %ld, %ld, %ld> RW <%ld, %ld, %ld>\n",
        pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
        pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3],
        pDevExt->Sts.lAllocatedCtls,
        pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedWrites)
   );

   if (irpStack->MajorFunction == IRP_MJ_DEVICE_CONTROL)
   {
      switch (ioControlCode)
      {
         case IOCTL_SERIAL_PURGE:
         {
            QCSER_DbgPrint2
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> QDSP: PURGE_BEGIN!\n", pDevExt->PortName)
            );
            InterlockedExchange(&(pDevExt->lPurgeBegin), 1);
            break;
         }

         case IOCTL_SERIAL_SET_WAIT_MASK:
         case IOCTL_SERIAL_WAIT_ON_MASK:
            // Special treatment providing workaround to the problem in
            // upper layer which might not have sync mechanism between
            // read and this IOCTL. Debugging showed the upper layer
            // would issue this IOCTL and read request in a same thread
            // and it's expected that the execution of this IOCTL and the
            // read request were serialized, which means the execution
            // of this IOCTL should block the issuance of the next read
            // request.
            //
            // It was observed that the async implmentation of this IOCTL
            // and the read request(s) would cause the upper layer
            // to stop issuing further read requests, resulting in
            // communication failure. We believe at this driver level
            // async execution is common practice, so the bug should be
            // either within modem.sys or the user space.
            // 
            // This is a workaround to the aforesaid problem, this
            // should not cause anything bad but it breaks the async
            // design of the driver.
         case IOCTL_QCUSB_SYSTEM_POWER:
         case IOCTL_QCUSB_DEVICE_POWER:
         {
            return QCDSP_Dispatch(DeviceObject, CalledDO, Irp, &bDevRemoved, FALSE);
         }
      }
   }

   return QCDSP_Enqueue(DeviceObject, CalledDO, Irp, irql);

}  // QCDSP_QueuedDispatch

NTSTATUS QCDSP_Enqueue
(
   PDEVICE_OBJECT DeviceObject,
   PDEVICE_OBJECT CalledDO,
   PIRP           Irp,
   KIRQL          Irql
)
{
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   NTSTATUS ntStatus = STATUS_SUCCESS; 
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   PQCDSP_IOBlockType dspIoBlock = NULL;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);
   PLIST_ENTRY headOfList;

   ntStatus = STATUS_PENDING;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> -->QCDSP_Enqueue IRP 0x%p\n", pDevExt->PortName, Irp)
   );
   QcAcquireSpinLockWithLevel(&pDevExt->ControlSpinLock, &levelOrHandle, Irql);
   if ((Irp->Cancel) || (pDevExt->MgrId < 0))
   {
      PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
      KdPrint(("QCDSP_Enqueue: CANCELLED\n"));
      ntStatus = Irp->IoStatus.Status = STATUS_CANCELLED;
      QcReleaseSpinLockWithLevel(&pDevExt->ControlSpinLock, levelOrHandle, Irql);
      goto disp_enq_exit;
   }

   if (!IsListEmpty(&pDevExt->DispatchFreeQueue))
   {
      headOfList = RemoveHeadList(&pDevExt->DispatchFreeQueue);
      dspIoBlock = CONTAINING_RECORD
                   (
                      headOfList,
                      QCDSP_IOBlockType,
                      List
                   );
   }
   else
   {
      // Allocate QCDSP_IOBlock
      dspIoBlock = ExAllocatePool(NonPagedPool, sizeof(QCDSP_IOBlockType));
      if (dspIoBlock != NULL)
      {
         InterlockedIncrement(&(pDevExt->Sts.lAllocatedDSPs));
      }
   }
   QcReleaseSpinLockWithLevel(&pDevExt->ControlSpinLock, levelOrHandle, Irql);

   if (dspIoBlock == NULL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> DSP: iob NO_MEMORY\n", pDevExt->PortName)
      );
      ntStatus = Irp->IoStatus.Status = STATUS_NO_MEMORY;
      goto disp_enq_exit;
   }
   InterlockedIncrement(&(pDevExt->Sts.lAllocatedCtls));
   dspIoBlock->Irp      = Irp;
   dspIoBlock->CalledDO = CalledDO;
   dspIoBlock->Hold     = FALSE;
   dspIoBlock->IsXwdmReq = QCDEV_IsXwdmRequest(pDevExt, Irp);

   _IoMarkIrpPending(Irp);
   IoSetCancelRoutine(Irp, DispatchCancelQueued);

   QcAcquireSpinLockWithLevel(&pDevExt->ControlSpinLock, &levelOrHandle, Irql);

   if (irpStack->MajorFunction == IRP_MJ_POWER)
   {
      // Give power IRP priority to avoid being blocked by other IRP's
      // InsertHeadList(&pDevExt->DispatchQueue, &dspIoBlock->List);
      QCPWR_Enqueue(pDevExt, dspIoBlock);
   }
   else
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_TRACE,
         ("<%s> QCDSP_Enqueue; non-PWR IRP 0x%p/0x%p to tail\n", pDevExt->PortName, Irp, dspIoBlock)
      );
      InsertTailList(&pDevExt->DispatchQueue, &dspIoBlock->List);
   }

   QcReleaseSpinLockWithLevel(&pDevExt->ControlSpinLock, levelOrHandle, Irql);

   KeSetEvent
   (
      // pDevExt->pDispatchEvents[DSP_START_EVENT_INDEX],
      &(DeviceInfo[pDevExt->MgrId].DspStartEvent),
      IO_NO_INCREMENT,
      FALSE
   );

disp_enq_exit:

   if (ntStatus != STATUS_PENDING)
   {
      IoSetCancelRoutine(Irp, NULL);
      Irp->IoStatus.Status = ntStatus;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CIRP,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> CIRP (C 0x%x) 0x%p\n", pDevExt->PortName, ntStatus, Irp)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return ntStatus;
   }

   /* The following debug statement could cause memory access fault
    * because the device object (including extention) could be removed
    * at this time -- we do not hold remove lock anymore 
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> <--QCDSP_Enqueue IRP 0x%p ST 0x%x\n", pDevExt->PortName, Irp, ntStatus)
   );
   */

   return ntStatus;
}  // QCDSP_Enqueue

NTSTATUS InitDispatchThread(IN PDEVICE_OBJECT pDevObj)
{
   NTSTATUS ntStatus;
   USHORT usLength, i;
   PDEVICE_EXTENSION pDevExt;
   PIRP pIrp;
   OBJECT_ATTRIBUTES objAttr;

   pDevExt = pDevObj->DeviceExtension;

   // Make sure the int thread is created at PASSIVE_LEVEL
   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> DSP: wrong IRQL::%d\n", pDevExt->PortName, KeGetCurrentIrql())
      );
      return STATUS_UNSUCCESSFUL;
   }

   if ((DeviceInfo[pDevExt->MgrId].pDispatchThread != NULL) || (DeviceInfo[pDevExt->MgrId].hDispatchThreadHandle != NULL))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> DSP up - 1\n", pDevExt->PortName)
      );
      return STATUS_SUCCESS;
   }

   KeClearEvent(&pDevExt->DspThreadStartedEvent);
   InitializeObjectAttributes(&objAttr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
   ucHdlCnt++;
   pDevExt->bDspThreadInCreation = TRUE;
   ntStatus = PsCreateSystemThread
              (
                 OUT &(DeviceInfo[pDevExt->MgrId].hDispatchThreadHandle),
                 IN THREAD_ALL_ACCESS,
                 IN &objAttr,         // POBJECT_ATTRIBUTES
                 IN NULL,             // HANDLE  ProcessHandle
                 OUT NULL,            // PCLIENT_ID  ClientId
                 IN (PKSTART_ROUTINE)DispatchThread,
                 IN (PVOID) pDevExt
              );			
   if ((!NT_SUCCESS(ntStatus)) || (DeviceInfo[pDevExt->MgrId].hDispatchThreadHandle == NULL))
   {
      DeviceInfo[pDevExt->MgrId].hDispatchThreadHandle = NULL;
      DeviceInfo[pDevExt->MgrId].pDispatchThread = NULL;
      pDevExt->bDspThreadInCreation = FALSE;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> DSP th failure\n", pDevExt->PortName)
      );
      return ntStatus;
   }

   ntStatus = KeWaitForSingleObject
              (
                 &pDevExt->DspThreadStartedEvent,
                 Executive,
                 KernelMode,
                 FALSE,
                 NULL
              );
   if (pDevExt->bDspThreadInCreation == FALSE)
   {
      DeviceInfo[pDevExt->MgrId].hDispatchThreadHandle = NULL;
      DeviceInfo[pDevExt->MgrId].pDispatchThread = NULL;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> DSP th failure 02\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   ntStatus = ObReferenceObjectByHandle
              (
                 DeviceInfo[pDevExt->MgrId].hDispatchThreadHandle,
                 THREAD_ALL_ACCESS,
                 NULL,
                 KernelMode,
                 (PVOID*)&(DeviceInfo[pDevExt->MgrId].pDispatchThread),
                 NULL
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> DSP: ObReferenceObjectByHandle failed 0x%x\n", pDevExt->PortName, ntStatus)
      );
      DeviceInfo[pDevExt->MgrId].pDispatchThread = NULL;
   }
   else
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> DSP handle=0x%p thOb=0x%p\n", pDevExt->PortName,
          DeviceInfo[pDevExt->MgrId].hDispatchThreadHandle, DeviceInfo[pDevExt->MgrId].pDispatchThread)
      );
   
      _closeHandle(DeviceInfo[pDevExt->MgrId].hDispatchThreadHandle, "D-2");
   }
   pDevExt->bDspThreadInCreation = FALSE;

   return ntStatus;

}  // InitDispatchThread

VOID DispatchThread(PVOID pContext)
{
   PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pContext;
   PIO_STACK_LOCATION pNextStack;
   BOOLEAN bCancelled = FALSE, bKeepLoop = TRUE, bDeviceRemoved = FALSE;
   NTSTATUS  ntStatus;
   PKWAIT_BLOCK pwbArray;
   PQCDSP_IOBlockType pDispatchIrp = NULL;
   BOOLEAN bFirstTime = TRUE;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KEVENT dummyEvent;
   PLIST_ENTRY headOfList;
   ULONG rmErr = 0;
   int mgrId = pDevExt->MgrId;
   PKEVENT pDispatchEvents[DSP_EVENT_COUNT];
   char myPortName[16];
   BOOLEAN bForXwdm = FALSE;
   LARGE_INTEGER checkRegInterval;

   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> Dth: wrong IRQL::%d\n", pDevExt->PortName, KeGetCurrentIrql())
      );
      #ifdef DEBUG_MSGS
      _asm int 3;
      #endif
   }

   // allocate a wait block array for the multiple wait
   pwbArray = _ExAllocatePool
              (
                 NonPagedPool,
                 (DSP_EVENT_COUNT+1)*sizeof(KWAIT_BLOCK),
                 "DispatchThread.pwbArray"
              );
   if (!pwbArray)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> Dth: STATUS_NO_MEMORY 1\n", pDevExt->PortName)
      );
      _closeHandle(DeviceInfo[mgrId].hDispatchThreadHandle, "D-1");
      pDevExt->bDspThreadInCreation = FALSE;
      KeSetEvent(&pDevExt->DspThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      PsTerminateSystemThread(STATUS_NO_MEMORY);
   }

   KeSetPriorityThread(KeGetCurrentThread(), QCSER_DSP_PRIORITY);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> D pri=%d\n", pDevExt->PortName, KeQueryPriorityThread(KeGetCurrentThread()))
   );

   strcpy(myPortName, pDevExt->PortName);

   pDispatchEvents[QC_DUMMY_EVENT_INDEX]   = &dummyEvent;
   pDispatchEvents[DSP_CANCEL_EVENT_INDEX] = &(DeviceInfo[mgrId].TerminationOrderEvent);
   pDispatchEvents[DSP_START_EVENT_INDEX]  = &(DeviceInfo[mgrId].DspStartEvent);
   pDispatchEvents[DSP_DEV_RETRY_EVENT_INDEX] = &(DeviceInfo[mgrId].DspDeviceRetryEvent);
   pDispatchEvents[DSP_DEV_RESET_IN_EVENT_INDEX] = &(DeviceInfo[mgrId].DspDeviceResetINEvent);
   pDispatchEvents[DSP_DEV_RESET_OUT_EVENT_INDEX] = &(DeviceInfo[mgrId].DspDeviceResetOUTEvent);
   pDispatchEvents[DSP_START_DATA_THREADS_INDEX] = &(DeviceInfo[mgrId].DspStartDataThreadsEvent);
   pDispatchEvents[DSP_RESUME_DATA_THREADS_INDEX] = &(DeviceInfo[mgrId].DspResumeDataThreadsEvent);
   pDispatchEvents[DSP_PRE_WAKEUP_EVENT_INDEX] = &(DeviceInfo[mgrId].DspPreWakeUpEvent);

   KeInitializeEvent(&dummyEvent, NotificationEvent, FALSE);
   pDevExt->CleanupInProgress = FALSE;

   while (bKeepLoop == TRUE)
   {
      if (bDeviceRemoved == TRUE)
      {
         QCSER_DbgPrintG
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> Dth: dev removed, goto wait\n", myPortName)
         );
         goto dispatch_wait;
      }

      // De-queue
      QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);

      if ((!IsListEmpty(&pDevExt->DispatchQueue)) && (pDispatchIrp == NULL))
      {
         PLIST_ENTRY   peekEntry;

         // Peek at the head record but do not de-queue
         headOfList = &pDevExt->DispatchQueue;
         peekEntry = headOfList->Flink;
         pDispatchIrp = CONTAINING_RECORD
                        (
                           peekEntry,
                           QCDSP_IOBlockType,
                           List
                        );
         if (QCDSP_ToProcessIrp(pDevExt, pDispatchIrp->Irp) == FALSE)
         {
            // Place it at the tail to give other IRPs priority
            headOfList = RemoveHeadList(&pDevExt->DispatchQueue);
            InsertTailList(&pDevExt->DispatchQueue, &pDispatchIrp->List);
            pDispatchIrp = NULL;
            QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
            goto dispatch_wait;
         }

         // We need to check if the IRP has been cancelled right here
         if (IoSetCancelRoutine(pDispatchIrp->Irp, NULL) == NULL)
         {
            PIRP pIrp = pDispatchIrp->Irp;

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> Dth: IRP CxlRtn called: COMP 0x%p\n", myPortName, pDispatchIrp->Irp)
            );

            // de-queue
            headOfList = RemoveHeadList(&pDevExt->DispatchQueue);

            // complete the IRP
            pIrp->IoStatus.Status = STATUS_CANCELLED;
            pIrp->IoStatus.Information = 0;
            InsertTailList(&pDevExt->CtlCompletionQueue, &pIrp->Tail.Overlay.ListEntry);
            KeSetEvent(&pDevExt->InterruptEmptyCtlQueueEvent, IO_NO_INCREMENT, FALSE);

            // recycle io block
            InsertTailList(&pDevExt->DispatchFreeQueue, &pDispatchIrp->List);
            QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

            InterlockedDecrement(&(pDevExt->Sts.lAllocatedCtls));
            pDispatchIrp = pDevExt->pCurrentDispatch = NULL;
            QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_CTRL, TRUE, 11);

            continue;
         }

         if ((pDispatchIrp->Hold == TRUE) && (pDevExt->DevicePower > PowerDeviceD0))
         {
            // Need to restore the cancel routine. Since we already nullified
            // the cancel routine, it's safe to restore it here.
            IoSetCancelRoutine(pDispatchIrp->Irp, DispatchCancelQueued);
            pDispatchIrp = pDevExt->pCurrentDispatch = NULL;
 
            QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
            goto dispatch_wait;
         }
         bForXwdm = pDispatchIrp->IsXwdmReq;
         if ((bForXwdm == FALSE) &&
             (QCPWR_CheckToWakeup(pDevExt, pDispatchIrp->Irp, QCUSB_BUSY_CTRL, 2) == TRUE))
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> Dth: back to wait IRP 0x%p at D%u\n", pDevExt->PortName,
                 pDispatchIrp->Irp, pDevExt->DevicePower-1)
            );
            pDispatchIrp->Hold = TRUE;

            // Need to restore the cancel routine. Since we already nullified
            // the cancel routine, it's safe to restore it here.
            IoSetCancelRoutine(pDispatchIrp->Irp, DispatchCancelQueued);
            pDispatchIrp = pDevExt->pCurrentDispatch = NULL;

            QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
            // QCSER_Wait(pDevExt, -(5 * 1000 * 1000));
            goto dispatch_wait;
         }

         // Now we de-queue
         headOfList = RemoveHeadList(&pDevExt->DispatchQueue);
         pDevExt->pCurrentDispatch = pDispatchIrp = CONTAINING_RECORD
                                                    (
                                                       headOfList,
                                                       QCDSP_IOBlockType,
                                                       List
                                                    );
         QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

         if (pDispatchIrp != NULL)
         {
            if (bCancelled == TRUE)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_CIRP,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> CIRP: (Cx 0x%p)\n", myPortName, pDispatchIrp->Irp)
               );
               pDispatchIrp->Irp->IoStatus.Status = STATUS_CANCELLED;
               QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pDispatchIrp->Irp, 0);
               _IoCompleteRequest(pDispatchIrp->Irp, IO_NO_INCREMENT);
               QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
               InsertTailList(&pDevExt->DispatchFreeQueue, &pDispatchIrp->List);
               QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
               // ExFreePool(pDispatchIrp);
               InterlockedDecrement(&(pDevExt->Sts.lAllocatedCtls));
               pDispatchIrp = pDevExt->pCurrentDispatch = NULL;
               QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_CTRL, TRUE, 12);
               continue;
            }

            ntStatus = QCDSP_Dispatch
                       (
                          pDevExt->MyDeviceObject,
                          pDispatchIrp->CalledDO,
                          pDispatchIrp->Irp,
                          &bDeviceRemoved,
                          bForXwdm
                       );
            pDispatchIrp->Irp = NULL;
            if (bDeviceRemoved == FALSE)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_CONTROL,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> DSP: free dsp block 0x%p <0x%x>\n", myPortName, pDispatchIrp, ntStatus)
               );
               QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
               InsertTailList(&pDevExt->DispatchFreeQueue, &pDispatchIrp->List);
               QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
            }
            else
            {
               QCSER_DbgPrintG
               (
                  QCSER_DBG_MASK_CONTROL,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> DSPg: free dsp block 0x%p <0x%x>\n", myPortName, pDispatchIrp, ntStatus)
               );
               ExFreePool(pDispatchIrp);
               // InterlockedDecrement(&(pDevExt->Sts.lAllocatedDSPs));
            }
            pDispatchIrp = NULL;

            if (bDeviceRemoved == FALSE)
            {
               QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_CTRL, TRUE, 16);
               pDevExt->pCurrentDispatch = NULL;
               InterlockedDecrement(&(pDevExt->Sts.lAllocatedCtls));
               QCSER_DbgPrint2
               (
                  QCSER_DBG_MASK_CONTROL,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> DSP: END-RML <%ld,%ld,%ld,%ld> CRW <%ld/%ld,%ld/%ld,%ld/%ld>\n\n", myPortName,
                    pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
                    pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3],
                    pDevExt->Sts.lAllocatedCtls, pDevExt->Sts.lAllocatedDSPs,
                    pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedRdMem,
                    pDevExt->Sts.lAllocatedWrites, pDevExt->Sts.lAllocatedWtMem
                  )
               );
            }
            // we don't try to complete the IRP, the dispatch routine will.
         }
         else
         {
            QCSER_DbgPrintG
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> Dth: ERR - NULL IRP\n", myPortName)
            );
         }
         continue;
      } 
      else
      {
         QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
         if (bCancelled == TRUE)
         {
            bKeepLoop = FALSE;
            continue;
         }
      }  //if ((!IsListEmpty(&pDevExt->DispatchQueue)) && (pDispatchIrp == NULL))

dispatch_wait:

      if (bFirstTime == TRUE)
      {
         bFirstTime = FALSE;
         KeSetEvent(&pDevExt->DspThreadStartedEvent,IO_NO_INCREMENT,FALSE);
      }

      if (bDeviceRemoved == FALSE)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_TRACE,
            ("<%s> D: WAIT...\n", myPortName)
         );
      }
      checkRegInterval.QuadPart = -(10 * 1000 * 1000); // 1.0 sec
      ntStatus = KeWaitForMultipleObjects
                 (
                    DSP_EVENT_COUNT,
                    (VOID **)&pDispatchEvents,
                    WaitAny,
                    Executive,
                    KernelMode,
                    FALSE,
                    &checkRegInterval,
                    pwbArray
                 );

      switch (ntStatus)
      {
         case QC_DUMMY_EVENT_INDEX:
         {
            KeClearEvent(&dummyEvent);
            QCSER_DbgPrintG
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> D: DUMMY_EVENT\n", myPortName)
            );
            goto dispatch_wait;
         }

         case DSP_START_EVENT_INDEX:
         {
            KeClearEvent(&(DeviceInfo[mgrId].DspStartEvent));
            if (bDeviceRemoved == FALSE)
            {
               QCSER_DbgPrintG2
               (
                  QCSER_DBG_MASK_CONTROL,
                  QCSER_DBG_LEVEL_VERBOSE,
                  ("<%s> D: START_DISPATCH_EVENT\n", myPortName)
               );
            }
            continue;
         }

         case DSP_CANCEL_EVENT_INDEX:
         {
            KeClearEvent(&(DeviceInfo[mgrId].TerminationOrderEvent));
            QCSER_DbgPrintG
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> D: CANCEL_DISPATCH_EVENT\n", myPortName)
            );
            bCancelled = TRUE;
            bKeepLoop = FALSE;

            // QCPTDO_RemovePort() must empty the dispatch queue after it
            // signals the thread termination
            continue;
         }

         case DSP_DEV_RETRY_EVENT_INDEX:
         {
            if (bDeviceRemoved == TRUE)
            {
               break;
            }

            KeClearEvent(&(DeviceInfo[mgrId].DspDeviceRetryEvent));

            if (pDevExt->ContinueOnDataError == FALSE)
            {
               break;
            }

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_FORCE,
               ("<%s> D: DEV_RETRY\n", myPortName)
            );

            if ( (pDevExt->bDeviceRemoved == TRUE)          &&
                 (pDevExt->bDeviceSurpriseRemoved == FALSE) &&
                 (!inDevState(DEVICE_STATE_DEVICE_REMOVED0)) )
            {
               // try to re-establish communication
               pDevExt->bDeviceRemoved = FALSE;
               setDevState(DEVICE_STATE_PRESENT);
               if (QCUSB_RetryDevice(pDevExt->MyDeviceObject, ++rmErr) == FALSE)
               {
                  pDevExt->bDeviceRemoved = TRUE;
                  clearDevState(DEVICE_STATE_PRESENT);
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> D: DEV_RETRY failed\n", myPortName)
                  );
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_FORCE,
                     ("<%s> D: DEV_RETRY succeeded\n", myPortName)
                  );
                  if ((pDevExt->ucsDeviceMapEntry.Buffer != NULL) &&
                      (pDevExt->ucsPortName.Buffer != NULL))
                  {
                     RtlWriteRegistryValue
                     (
                        RTL_REGISTRY_DEVICEMAP,
                        L"SERIALCOMM",
                        pDevExt->ucsDeviceMapEntry.Buffer,
                        REG_SZ,
                        pDevExt->ucsPortName.Buffer,
                        pDevExt->ucsPortName.Length + sizeof(WCHAR)
                     );
                  }
               }
            }
            else
            {
               rmErr = 0;
            }
            break;
         }

         case DSP_DEV_RESET_IN_EVENT_INDEX:
         {
            if (bDeviceRemoved == TRUE)
            {
               break;
            }

            KeClearEvent(&(DeviceInfo[mgrId].DspDeviceResetINEvent));

            if ( (pDevExt->bDeviceSurpriseRemoved == TRUE) ||
                 (inDevState(DEVICE_STATE_DEVICE_REMOVED0)) )
            {
               break;
            }

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> D: RESET_IN\n", myPortName)
            );

            QCRD_L2Suspend(pDevExt);
            QCUSB_ResetInput(pDevExt->MyDeviceObject, QCUSB_RESET_HOST_PIPE);
            QCRD_L2Resume(pDevExt);
            break;
         }

         case DSP_DEV_RESET_OUT_EVENT_INDEX:
         {
            if (bDeviceRemoved == TRUE)
            {
               break;
            }

            KeClearEvent(&(DeviceInfo[mgrId].DspDeviceResetOUTEvent));

            if ( (pDevExt->bDeviceSurpriseRemoved == TRUE) ||
                 (inDevState(DEVICE_STATE_DEVICE_REMOVED0)) )
            {
               break;
            }

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> D: RESET_OUT\n", myPortName)
            );

            QCWT_Suspend(pDevExt);
            QCUSB_ResetOutput(pDevExt->MyDeviceObject, QCUSB_RESET_HOST_PIPE);
            QCWT_Resume(pDevExt);
            break;
         }

         case DSP_START_DATA_THREADS_INDEX:
         {
            if (bDeviceRemoved == TRUE)
            {
               break;
            }

            KeClearEvent(&(DeviceInfo[mgrId].DspStartDataThreadsEvent));
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> D: START_DATA_THREADS\n", pDevExt->PortName)
            );

            if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED) && (pDevExt->bInService == TRUE))
            {
               QCSER_StartDataThreads(pDevExt);
            }

            break;
         }

         case DSP_RESUME_DATA_THREADS_INDEX:
         {
            if (bDeviceRemoved == TRUE)
            {
               KeSetEvent
               (
                  &(DeviceInfo[mgrId].DspResumeDataThreadsAckEvent),
                  IO_NO_INCREMENT, FALSE
               );
               break;
            }

            KeClearEvent(&(DeviceInfo[mgrId].DspResumeDataThreadsEvent));
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> D: RESUME_DATA_THREADS\n", pDevExt->PortName)
            );

            // if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED) && (pDevExt->bInService == TRUE))
            if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
            {
               if (pDevExt->bInService == FALSE)
               {
                  QCUSB_CDC_SetInterfaceIdle
                  (
                     pDevExt->MyDeviceObject,
                     pDevExt->DataInterface,
                     TRUE,
                     3
                  );
               }
               else
               {
                  QCUSB_CDC_SetInterfaceIdle
                  (
                     pDevExt->MyDeviceObject,
                     pDevExt->DataInterface,
                     FALSE,
                     5
                  );
               }
               ResumeInterruptService(pDevExt, 1);
               QCRD_L2Resume(pDevExt);
               QCWT_Resume(pDevExt);
            }

            KeSetEvent
            (
               &(DeviceInfo[mgrId].DspResumeDataThreadsAckEvent),
               IO_NO_INCREMENT, FALSE
            );
            break;
         }

         case DSP_PRE_WAKEUP_EVENT_INDEX:
         {
            if (bDeviceRemoved == TRUE)
            {
               break;
            }

            KeClearEvent(&(DeviceInfo[mgrId].DspPreWakeUpEvent));
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> D: -->PRE_WAKEUP\n", pDevExt->PortName)
            );

            if (pDevExt->PowerSuspended == TRUE)
            {
               // Wakeup the device
               QCPWR_CancelIdleNotificationIrp(pDevExt, 2);
            }
            QCSER_Wait(pDevExt, -(10 * 1000L));  // yield, 1ms
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> D: <--PRE_WAKEUP\n", pDevExt->PortName)
            );
            break;
         }

         default:
         {
            if (bDeviceRemoved == TRUE)
            {
               QCSER_DbgPrintG
               (
                  QCSER_DBG_MASK_CONTROL,
                  QCSER_DBG_LEVEL_TRACE,
                  ("<%s> D: default sig or TO-0\n", myPortName)
               );
            }
            else
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_CONTROL,
                  QCSER_DBG_LEVEL_TRACE,
                  ("<%s> D: default sig or TO-1\n", myPortName)
               );
            }
            break;
         }
      }
   } // while (TRUE)

   if(pwbArray)
   {
      ExFreePool(pwbArray);
   }

   KeSetEvent
   (
      &(DeviceInfo[mgrId].TerminationCompletionEvent),
      IO_NO_INCREMENT,
      FALSE
   );

   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_FORCE,
      ("<%s> Dth: OUT\n", myPortName)
   );

   _closeHandleG(myPortName, DeviceInfo[mgrId].hDispatchThreadHandle, "D-4");
   PsTerminateSystemThread(STATUS_SUCCESS); // end this thread
}  // DispatchThread

VOID DispatchCancelQueued(PDEVICE_OBJECT CalledDO, PIRP Irp)
{
   KIRQL irql = KeGetCurrentIrql();
   PDEVICE_OBJECT DeviceObject = QCPTDO_FindPortDOByFDO(CalledDO, irql);
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   PQCDSP_IOBlockType dspIoBlock = NULL;
   LIST_ENTRY tmpQueue;
   PLIST_ENTRY headOfList;
   BOOLEAN inQueue = FALSE;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> DSP CQed: 0x%p\n", pDevExt->PortName, Irp)
   );
   IoReleaseCancelSpinLock(Irp->CancelIrql);

   InitializeListHead(&tmpQueue);

   // De-queue
   QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);

   // search DSP IOB in the dispatch queue
   while (!IsListEmpty(&pDevExt->DispatchQueue))
   {
      headOfList = RemoveHeadList(&pDevExt->DispatchQueue);
      dspIoBlock =  CONTAINING_RECORD
                    (
                       headOfList,
                       QCDSP_IOBlockType,
                       List
                    );
      if (dspIoBlock->Irp == Irp)
      {
         inQueue = TRUE;
         // ExFreePool(dspIoBlock);
         InsertTailList(&pDevExt->DispatchFreeQueue, &dspIoBlock->List);
         InterlockedDecrement(&(pDevExt->Sts.lAllocatedCtls));
         continue;
      }

      InsertTailList(&tmpQueue, &dspIoBlock->List);
   }

   while (!IsListEmpty(&tmpQueue))
   {
      headOfList = RemoveTailList(&tmpQueue);
      dspIoBlock =  CONTAINING_RECORD
                    (
                       headOfList,
                       QCDSP_IOBlockType,
                       List
                    );
      InsertHeadList(&pDevExt->DispatchQueue, &dspIoBlock->List);
   }

   if (inQueue == TRUE)
   {
      // Now, finish the IRP
      IoSetCancelRoutine(Irp, NULL); // not necessary
      Irp->IoStatus.Status = STATUS_CANCELLED;
      Irp->IoStatus.Information = 0;
      InsertTailList(&pDevExt->CtlCompletionQueue, &Irp->Tail.Overlay.ListEntry);
      KeSetEvent(&pDevExt->InterruptEmptyCtlQueueEvent, IO_NO_INCREMENT, FALSE);
   }
   else
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> DSP CQed: IRP not in Q, leave to DSP: 0x%p\n", pDevExt->PortName, Irp)
      );
   }

   QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
}  // DispatchCancelQueued

NTSTATUS QCDSP_Dispatch
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PDEVICE_OBJECT CalledDO,
   IN PIRP Irp,
   IN OUT BOOLEAN *Removed,
   IN BOOLEAN ForXwdm
)
{
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   LARGE_INTEGER delayValue;
   PIO_STACK_LOCATION irpStack, nextStack;
   PDEVICE_EXTENSION pDevExt;
   PFDO_DEVICE_EXTENSION pFdoExt;
   PVOID ioBuffer;
   ULONG inputBufferLength, outputBufferLength, ioControlCode;
   NTSTATUS ntStatus = STATUS_SUCCESS, ntCloseStatus = STATUS_SUCCESS, myNts;
   PDEVICE_OBJECT pUsbDevObject;
   USHORT usLength;
   BOOLEAN bRemoveRequest = FALSE;
   KEVENT ePNPEvent;
   VXD_WDM_IO_CONTROL_BLOCK IOBlockTemp;
   char myPortName[16];
   KIRQL irql = KeGetCurrentIrql();

   pDevExt = pDevExt = DeviceObject->DeviceExtension;
   strcpy(myPortName, pDevExt->PortName);

   QCSER_DbgPrint2
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> DSP: >>>>> IRQL %d IRP 0x%p <<<<<\n", myPortName, irql, Irp)
   );

   irpStack = IoGetCurrentIrpStackLocation (Irp);
   nextStack = IoGetNextIrpStackLocation (Irp);
   if (CalledDO != NULL)
   {
      pFdoExt = CalledDO->DeviceExtension;
      pUsbDevObject = pFdoExt->StackDeviceObject;
   }
   else
   {
      pUsbDevObject = pDevExt->StackDeviceObject;
   }

   // get the parameters from an IOCTL call
   ioBuffer           = Irp->AssociatedIrp.SystemBuffer;
   inputBufferLength  = irpStack->Parameters.DeviceIoControl.InputBufferLength;
   outputBufferLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
   ioControlCode      = irpStack->Parameters.DeviceIoControl.IoControlCode;

   if (irpStack->MajorFunction == IRP_MJ_DEVICE_CONTROL)
   {
      if ((gVendorConfig.DriverResident == 0) && (pDevExt->bInService == FALSE))
      {
         if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> DSP: dev disconnected-1\n", myPortName)
            );
            Irp->IoStatus.Status = STATUS_DELETE_PENDING;
            Irp->IoStatus.Information = 0;
            goto QCDSP_Dispatch_Done0;
         }
      }
   }
   else if (irpStack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL)
   {
      if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> DSP: dev disconnected-2\n", myPortName)
         );
         Irp->IoStatus.Status = STATUS_DELETE_PENDING;
         Irp->IoStatus.Information = 0;
         goto QCDSP_Dispatch_Done0;
      }
   }

   switch (irpStack->MajorFunction)
   {
      case IRP_MJ_CREATE:
      {
         Irp->IoStatus.Status = ntStatus = STATUS_SUCCESS;
         Irp->IoStatus.Information = 0;

         if (ForXwdm == TRUE)
         {
            break;
         }

         // make sure it not a late request
         if ((pDevExt->FDO == CalledDO) || (DeviceObject == CalledDO))
         {
            IOBlockTemp.pSerialDeviceObject = DeviceObject;
            IOBlockTemp.pCompletionRoutine  = NULL;
            IOBlockTemp.pCallingIrp         = Irp;
            Irp->IoStatus.Status = ntStatus = QCSER_Open(&IOBlockTemp);

            if ((pDevExt->FDO == CalledDO) && (pDevExt->bInService == TRUE) &&
                (ntStatus == STATUS_SUCCESS))
            {
               pDevExt->bStackOpen = TRUE;
            }

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_CRITICAL,
               ("<%s> OP-RML <%ld, %ld, %ld, %ld, %ld> CRW <%ld/%ld, %ld/%ld, %ld/%ld> - 0x%x\n",
                 pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
                 pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3], pDevExt->Sts.lRmlCount[4],
                 pDevExt->Sts.lAllocatedCtls, pDevExt->Sts.lAllocatedDSPs,
                 pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedRdMem,
                 pDevExt->Sts.lAllocatedWrites, pDevExt->Sts.lAllocatedWtMem,
                 ntStatus)
            );
         }
         else if (pDevExt->bInService == TRUE)
         {
            Irp->IoStatus.Status = ntStatus = STATUS_DEVICE_BUSY;
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> DSP: late _CREATE, keep port status\n", myPortName)
            );
         }
         else
         {
            Irp->IoStatus.Status = ntStatus = STATUS_UNSUCCESSFUL;
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> DSP: late _CREATE, STATUS_UNSUCCESSFUL\n", myPortName)
            );
         }
         break;
      }
      case IRP_MJ_CLOSE:
      {
         // Believe we should always close the port when receive this request
         ntStatus = Irp->IoStatus.Status = STATUS_SUCCESS;
         Irp->IoStatus.Information = 0;

         if (ForXwdm == TRUE)
         {
            break;
         }

         // make sure new FDO is not in the picture yet, otherwise a late request
         // if (((pDevExt->FDO == CalledDO) || (DeviceObject == CalledDO)) &&
         //     (pDevExt->bInService == TRUE))
         {
            // real close code goes on in IRP_MN_REMOVE_DEVICE
            IOBlockTemp.pSerialDeviceObject = DeviceObject;
            IOBlockTemp.pCompletionRoutine = NULL;
            IOBlockTemp.pCallingIrp = Irp;
            ntCloseStatus = QCSER_Close(&IOBlockTemp);
            pDevExt->bStackOpen = FALSE;

            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_CRITICAL,
               ("<%s> CL-RML <%ld, %ld, %ld, %ld, %ld> CRW <%ld/%ld, %ld/%ld, %ld/%ld>\n\n",
                 pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
                 pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3], pDevExt->Sts.lRmlCount[4],
                 pDevExt->Sts.lAllocatedCtls, pDevExt->Sts.lAllocatedDSPs,
                 pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedRdMem,
                 pDevExt->Sts.lAllocatedWrites, pDevExt->Sts.lAllocatedWtMem)
            );

            if (!IOBlockTemp.pCallingIrp) // consumed by QCSER_Close()
            {
               goto QCDSP_Dispatch_Done;
            }
         }

         break;
      }
      case IRP_MJ_CLEANUP:
      {
         Irp->IoStatus.Status = STATUS_SUCCESS;
         Irp->IoStatus.Information = 0;

         if (ForXwdm == TRUE)
         {
            break;
         }

         if ((pDevExt->FDO == NULL)     ||
             (pDevExt->FDO == CalledDO) ||
             (DeviceObject == CalledDO))
         {
            Irp->IoStatus.Status = QCDSP_CleanUp(DeviceObject, Irp);
         }
         else
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s> DSP: late _CLEANUP, no action(0x%p, 0x%p, 0x%p)\n",
                 myPortName, pDevExt->FDO, DeviceObject, CalledDO)
            );
         }
         break; // IRP_MJ_CLEANUP
      }
      case IRP_MJ_DEVICE_CONTROL:
      {
         Irp->IoStatus.Status = STATUS_SUCCESS;
         Irp->IoStatus.Information = 0;

         switch (ioControlCode)
         {
            case IOCTL_SERIAL_GET_STATS:
               Irp -> IoStatus.Status = SerialGetStats
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_CLEAR_STATS:
               Irp -> IoStatus.Status = SerialClearStats( DeviceObject );
               break;

            case IOCTL_SERIAL_GET_PROPERTIES:
               Irp -> IoStatus.Status = SerialGetProperties
                                        (
                                           DeviceObject,
                                           ioBuffer, 
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_GET_MODEMSTATUS:
               Irp -> IoStatus.Status = SerialGetModemStatus
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_GET_COMMSTATUS:
               Irp -> IoStatus.Status = SerialGetCommStatus
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_RESET_DEVICE:
               Irp -> IoStatus.Status = SerialResetDevice( DeviceObject );
               break;

            case IOCTL_SERIAL_PURGE:
               Irp -> IoStatus.Status = SerialPurge( DeviceObject, Irp );
               break;

            case IOCTL_SERIAL_LSRMST_INSERT:
               Irp -> IoStatus.Status = SerialLsrMstInsert
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_GET_BAUD_RATE:
               Irp -> IoStatus.Status = SerialGetBaudRate
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_SET_BAUD_RATE:
               Irp -> IoStatus.Status = SerialSetBaudRate
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_SET_QUEUE_SIZE:
               Irp -> IoStatus.Status = SerialSetQueueSize
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_GET_HANDFLOW:
               Irp -> IoStatus.Status = SerialGetHandflow
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_SET_HANDFLOW:
               Irp -> IoStatus.Status = SerialSetHandflow
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_GET_LINE_CONTROL:
               Irp -> IoStatus.Status = SerialGetLineControl
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_SET_LINE_CONTROL:
               Irp -> IoStatus.Status = SerialSetLineControl
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_SET_BREAK_ON:
               Irp -> IoStatus.Status = SerialSetBreakOn( DeviceObject );
               break;

            case IOCTL_SERIAL_SET_BREAK_OFF:
               Irp -> IoStatus.Status = SerialSetBreakOff( DeviceObject );
               break;

            case IOCTL_SERIAL_GET_TIMEOUTS:
               Irp -> IoStatus.Status = SerialGetTimeouts
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_SET_TIMEOUTS:
               Irp -> IoStatus.Status = SerialSetTimeouts
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_IMMEDIATE_CHAR:
               Irp -> IoStatus.Status = SerialImmediateChar
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               if (Irp->IoStatus.Status == STATUS_TIMEOUT)
               {
                  goto QCDSP_Dispatch_Done;
               }
               break;

            case IOCTL_SERIAL_XOFF_COUNTER:
               Irp -> IoStatus.Status = SerialXoffCounter
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_SET_DTR:
               Irp -> IoStatus.Status = SerialSetDtr( DeviceObject );
               break;
   
            case IOCTL_SERIAL_CLR_DTR:
               Irp -> IoStatus.Status = SerialClrDtr( DeviceObject );
               break;

            case IOCTL_SERIAL_SET_RTS:
               Irp -> IoStatus.Status = SerialSetRts( DeviceObject );
               break;

            case IOCTL_SERIAL_CLR_RTS:
               Irp -> IoStatus.Status = SerialClrRts( DeviceObject );
               break;

            case IOCTL_SERIAL_GET_DTRRTS:
               Irp -> IoStatus.Status = SerialGetDtrRts
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_SET_XON:
               Irp -> IoStatus.Status = SerialSetXon( DeviceObject );
               break;

            case IOCTL_SERIAL_SET_XOFF:
               Irp -> IoStatus.Status = SerialSetXoff( DeviceObject );
               break;

            case IOCTL_SERIAL_GET_WAIT_MASK:
               Irp -> IoStatus.Status = SerialGetWaitMask
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_SET_WAIT_MASK:
               Irp -> IoStatus.Status = SerialSetWaitMask
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );

               if (NT_SUCCESS(Irp->IoStatus.Status))
               {
                  // Complete this IRP
                  QCSER_CompleteWaitOnMaskIrp(pDevExt);

                  if (irql > PASSIVE_LEVEL)
                  {
                     QcAcquireSpinLockWithLevel
                     (
                        &pDevExt->SingleIrpSpinLock, &levelOrHandle, irql
                     );

                     InsertTailList
                     (
                        &pDevExt->SglCompletionQueue,
                        &Irp->Tail.Overlay.ListEntry
                     );
                     KeSetEvent
                     (
                        &pDevExt->InterruptEmptySglQueueEvent,
                        IO_NO_INCREMENT,
                        FALSE
                     );

                     QcReleaseSpinLockWithLevel
                     (
                        &pDevExt->SingleIrpSpinLock, levelOrHandle, irql
                     );

                     ntStatus = STATUS_PENDING;
                     goto QCDSP_Dispatch_Done;
                  }
               }

               break;

            case IOCTL_SERIAL_WAIT_ON_MASK:
               ntStatus = SerialWaitOnMask( DeviceObject, ioBuffer, Irp );
               if (ntStatus == STATUS_PENDING)
               {
                  QCRD_ScanForWaitMask(pDevExt, TRUE, 0);

                  goto QCDSP_Dispatch_Done; // don't touch the "pending" IRP!
               }
               Irp -> IoStatus.Status = ntStatus;
               break;

            case IOCTL_SERIAL_GET_CHARS:
               Irp -> IoStatus.Status = SerialGetChars
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;

            case IOCTL_SERIAL_SET_CHARS:
               Irp -> IoStatus.Status = SerialSetChars
                                        (
                                           DeviceObject,
                                           ioBuffer,
                                           Irp
                                        );
               break;
         
            case WIN2K_DUN_IOCTL:
               Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
               break;

            case IOCTL_QCOMSER_WAIT_NOTIFY:
               ntStatus = SerialCacheNotificationIrp
                          (
                             DeviceObject,
                             ioBuffer,
                             Irp
                          );
               if (ntStatus == STATUS_PENDING)
               {
                  goto QCDSP_Dispatch_Done; // don't touch the "pending" IRP!
               }
               Irp->IoStatus.Status = ntStatus;
               break;

            case IOCTL_QCUSB_QCDEV_NOTIFY:
               ntStatus = QCDEV_CacheNotificationIrp
                          (
                             DeviceObject,
                             ioBuffer,
                             Irp
                          );
               if (ntStatus == STATUS_PENDING)
               {
                  goto QCDSP_Dispatch_Done; // don't touch the "pending" IRP!
               }
               Irp->IoStatus.Status = ntStatus;
               break;

            case IOCTL_QCOMSER_DRIVER_ID:
               Irp->IoStatus.Status = SerialGetDriverGUIDString
                                      (
                                         DeviceObject,
                                         ioBuffer,
                                         Irp
                                      );
               break;
            case IOCTL_QCSER_GET_SERVICE_KEY:
               Irp->IoStatus.Status = SerialGetServiceKey
                                      (
                                         DeviceObject,
                                         ioBuffer,
                                         Irp
                                      );
               break;

            case IOCTL_QCUSB_SYSTEM_POWER:
            {
               if (outputBufferLength < sizeof(QCUSB_POWER_REQ))
               {
                  QCSER_DbgPrintG
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_FORCE,
                     ("<%s> IOCTL_QCUSB_SYSTEM_POWER err\n", gDeviceName)
                  );
                  Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
                  Irp->IoStatus.Information = 0;
               }
               else
               {
                  QCDEV_GetSystemPowerState(ioBuffer);
                  Irp->IoStatus.Information = sizeof(QCUSB_POWER_REQ);
                  Irp->IoStatus.Status = STATUS_SUCCESS;
               }
               break;
            }

            case IOCTL_QCUSB_DEVICE_POWER:
            {
               if (outputBufferLength < sizeof(DEVICE_POWER_STATE))
               {
                  QCSER_DbgPrintG
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_FORCE,
                     ("<%s> IOCTL_QCUSB_DEVICE_POWER err\n", gDeviceName)
                  );
                  Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
                  Irp->IoStatus.Information = 0;
               }
               else
               {
                  QCDEV_GetDevicePowerState(ioBuffer);
                  Irp->IoStatus.Information = sizeof(SYSTEM_POWER_STATE);
                  Irp->IoStatus.Status = STATUS_SUCCESS;
               }
               break;
            }
            default:
               Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
         } // switch ioControlCode

         ASSERT(Irp->IoStatus.Status != STATUS_PENDING);
         break;
      }  // IRP_MJ_DEVICE_CONTROL

      case IRP_MJ_INTERNAL_DEVICE_CONTROL:
      {
        // THESE ARE ADDRESSED TO THE SERIAL PORT IT THINKS WE ARE
         switch(ioControlCode)
         {
            case IOCTL_SERIAL_INTERNAL_DO_WAIT_WAKE:
               Irp->IoStatus.Information = 0;
               Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
               break;

            case IOCTL_SERIAL_INTERNAL_CANCEL_WAIT_WAKE:
               Irp->IoStatus.Information = 0;
               Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
               break;

            case IOCTL_SERIAL_INTERNAL_BASIC_SETTINGS:
               Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
               break;

            case IOCTL_SERIAL_INTERNAL_RESTORE_SETTINGS:
               Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
               break;

            default:
               Irp->IoStatus.Information = 0;
               Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
               break;
         } // switch(IoControlCode)

         break;
      }  // INTERNAL_DEVICE_CONTROL

      case IRP_MJ_POWER:
      {
         QCPWR_PowrerManagement(pDevExt, Irp, irpStack);

         goto QCDSP_Dispatch_Done;
      }  // IRP_MJ_POWER

      case IRP_MJ_PNP:
      {
         switch (irpStack->MinorFunction) 
         {
            case IRP_MN_QUERY_CAPABILITIES:  // PASSIVE_LEVEL
            {
               PDEVICE_CAPABILITIES pdc = irpStack->Parameters.DeviceCapabilities.Capabilities;

               if (pdc->Version < 1)
               {
                  // just pass this down the stack
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CIRP,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> CIRP: (C FWD) 0x%p\n", myPortName, Irp)
                  );
                  IoSkipCurrentIrpStackLocation(Irp);
                  ntStatus = IoCallDriver(pUsbDevObject, Irp);
                  QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);

                  goto QCDSP_Dispatch_Done;
               }
               else
               {
                  KEVENT event;
                  // we forward and wait the IRP
                  KeInitializeEvent(&event, SynchronizationEvent, FALSE);
                  IoCopyCurrentIrpStackLocationToNext(Irp); 
                  IoSetCompletionRoutine
                  (
                     Irp,
                     QCMAIN_IrpCompletionSetEvent,
                     &event,
                     TRUE,
                     TRUE,
                     TRUE
                  );
                  // IoSetCancelRoutine(Irp, NULL);  // DV?
                  IoCallDriver(pUsbDevObject, Irp);
                  KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
                  ntStatus = Irp->IoStatus.Status;
                  if (NT_SUCCESS(ntStatus))
                  {
                     irpStack = IoGetCurrentIrpStackLocation(Irp);
                     pdc = irpStack->Parameters.DeviceCapabilities.Capabilities;
                     pdc->SurpriseRemovalOK = TRUE;
                     pdc->Removable         = FALSE; // TRUE;

                     // Power Management Stuff
                     pdc->D1Latency  = 4000;  // 0.4 second
                     pdc->D2Latency  = 5000;  // 0.5 second

                     RtlCopyMemory
                     ( 
                        &(pDevExt->DeviceCapabilities), 
                        irpStack->Parameters.DeviceCapabilities.Capabilities, 
                        sizeof(DEVICE_CAPABILITIES)
                     );

                     // inspect capability values
                     QCPWR_VerifyDeviceCapabilities(pDevExt);
                  }
               }
               goto QCDSP_Dispatch_Done0;
            }  // IRP_MN_QUERY_CAPABILITIES

            case IRP_MN_START_DEVICE:  // PASSIVE_LEVEL
               IoCopyCurrentIrpStackLocationToNext(Irp);

               // after the copy set the completion routine
               
               IoSetCompletionRoutine
               (
                  Irp,
                  QCMAIN_IrpCompletionSetEvent,
                  &ePNPEvent,
                  TRUE,
                  TRUE,
                  TRUE
               );

               KeInitializeEvent(&ePNPEvent, SynchronizationEvent, FALSE);

               ntStatus = IoCallDriver
                          (
                             pDevExt->StackDeviceObject,
                             Irp
                          );
               if (!NT_SUCCESS(ntStatus))
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> IRP_MN_START_DEVICE lowerERR= 0x%x\n", pDevExt->PortName, ntStatus)
                  );

                  // CR137325
                  delayValue.QuadPart = -(10 * 1000 * 1000); // 1.0 sec
                  KeWaitForSingleObject
                  (
                     &ePNPEvent,
                     Executive,
                     KernelMode,
                     FALSE,
                     &delayValue
                  );
                  goto QCDSP_Dispatch_Done0;
               }

               ntStatus = KeWaitForSingleObject
                          (
                             &ePNPEvent,
                             Executive,
                             KernelMode,
                             FALSE,
                             NULL
                          );

               ntStatus = QCPNP_StartDevice( DeviceObject, 0 );
 
               ASSERT(NT_SUCCESS(ntStatus));

               Irp->IoStatus.Status = ntStatus;
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_CONTROL,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> PNP IRP_MN_START_DEVICE = 0x%x (mgr %d)\n", pDevExt->PortName,
                   ntStatus, pDevExt->MgrId)
               );
               QCPTDO_DisplayListInfo();

               if (NT_SUCCESS(ntStatus))
               {
                  pDevExt->bDeviceRemoved = FALSE;
                  pDevExt->bDeviceSurpriseRemoved = FALSE;
                  setDevState(DEVICE_STATE_PRESENT_AND_STARTED);

                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> START_DEVICE, stack size = %u\n", pDevExt->PortName,
                       pDevExt->MyDeviceObject->StackSize)
                  );
                  // kick the R/W threads in case the threads have been
                  // created and idling. The scenario happens when the
                  // port is opened with device disconnected or no device
                  // attached.
                  pDevExt->RdErrorCount = pDevExt->WtErrorCount = 0;
                  pDevExt->bL1Stopped = pDevExt->bL2Stopped = FALSE;
                  pDevExt->bWriteStopped    = FALSE;

                  if (pDevExt->bInService == TRUE)
                  {
                     QCSER_StartDataThreads(pDevExt);
                  }
                  else
                  {
                     QCUSB_CDC_SetInterfaceIdle
                     (
                        pDevExt->MyDeviceObject,
                        pDevExt->DataInterface,
                        TRUE,
                        4
                     );
                  }
                  KeSetEvent(&pDevExt->L1KickReadEvent, IO_NO_INCREMENT, FALSE);
                  KeSetEvent(&pDevExt->KickWriteEvent, IO_NO_INCREMENT, FALSE);

                  // Now, create the symbolic link and update registry
                  if (QCPNP_CreateSymbolicLink(DeviceObject) == FALSE)
                  {
                     ntStatus = STATUS_UNSUCCESSFUL;
                  }

                  if (pDevExt->bRemoteWakeupEnabled)
                  {
                      QCPWR_RegisterWaitWakeIrp(pDevExt, 3);
                  }
                  QCPWR_SetIdleTimer(pDevExt, 0, FALSE, 1); // start device
               }

               // our dispatch routine completes the IRP
               goto QCDSP_Dispatch_Done0;
               break;
                            
            case IRP_MN_QUERY_STOP_DEVICE:  // PASSIVE_LEVEL
               if ((pDevExt->FDO == CalledDO) || (DeviceObject == CalledDO))
               {
                  StopInterruptService(pDevExt, TRUE, 0);
                  cancelAllIrps( DeviceObject );
                  ntStatus = QCPNP_StopDevice( DeviceObject );
                  if (NT_SUCCESS(ntStatus))
                  {
                     clearDevState(DEVICE_STATE_DEVICE_STARTED);
                     setDevState(DEVICE_STATE_DEVICE_STOPPED);
                  }
                  Irp->IoStatus.Status = ntStatus;
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> QUERY_STOP_DEVICE: late cFDO 0x%p\n", pDevExt->PortName, pDevExt->FDO)
                  );
                  Irp->IoStatus.Status = STATUS_SUCCESS;
               }
               Irp->IoStatus.Information = 0;

               break;
               
            case IRP_MN_CANCEL_STOP_DEVICE:  // PASSIVE_LEVEL
            {
               IoCopyCurrentIrpStackLocationToNext(Irp);

               // after the copy set the completion routine

               IoSetCompletionRoutine
               (
                  Irp,
                  QCMAIN_IrpCompletionSetEvent,
                  &ePNPEvent,
                  TRUE,
                  TRUE,
                  TRUE
               );

               KeInitializeEvent(&ePNPEvent, SynchronizationEvent, FALSE);

               ntStatus = IoCallDriver
                          (
                             pDevExt->StackDeviceObject,
                             Irp
                          );
               if (!NT_SUCCESS(ntStatus))
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> IRP_MN_CANCEL_STOP lowerERR= 0x%x\n", pDevExt->PortName, ntStatus)
                  );

                  // CR137325
                  delayValue.QuadPart = -(10 * 1000 * 1000); // 1.0 sec
                  KeWaitForSingleObject
                  (
                     &ePNPEvent,
                     Executive,
                     KernelMode,
                     FALSE,
                     &delayValue
                  );
                  goto QCDSP_Dispatch_Done0;
               }

               ntStatus = KeWaitForSingleObject
                          (
                             &ePNPEvent,
                             Executive,
                             KernelMode,
                             FALSE,
                             NULL
                          );

               if (((pDevExt->FDO == CalledDO) || (DeviceObject == CalledDO)) &&
                   (inDevState(DEVICE_STATE_DEVICE_STOPPED)))
               {
                  QCDSP_RestartDeviceFromCancelStopRemove(DeviceObject, Irp); // restart device
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> CANCEL_STOP_DEVICE: late cFDO 0x%p\n", pDevExt->PortName, pDevExt->FDO)
                  );
                  Irp->IoStatus.Status = STATUS_SUCCESS;
               }
               Irp->IoStatus.Information = 0;
               break;
            }

            case IRP_MN_STOP_DEVICE:  // PASSIVE_LEVEL
               if (inDevState(DEVICE_STATE_DEVICE_STOPPED))
               {
                  Irp->IoStatus.Status = STATUS_SUCCESS;
                  Irp->IoStatus.Information = 0;
                  break;
               }

               if ((pDevExt->FDO == CalledDO) || (DeviceObject == CalledDO))
               {
                  StopInterruptService(pDevExt, TRUE, 1);
                  cancelAllIrps( DeviceObject );
                  ntStatus = QCPNP_StopDevice( DeviceObject );
                  if (NT_SUCCESS(ntStatus))
                  {
                     clearDevState(DEVICE_STATE_DEVICE_STARTED);
                     setDevState(DEVICE_STATE_DEVICE_STOPPED);
                  }
                  Irp->IoStatus.Status = ntStatus;
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> STOP_DEVICE: late cFDO 0x%p\n", pDevExt->PortName, pDevExt->FDO)
                  );
                  Irp->IoStatus.Status = STATUS_SUCCESS;
               }
               Irp->IoStatus.Information = 0;
               break;
                 
            case IRP_MN_QUERY_REMOVE_DEVICE:  // PASSIVE_LEVEL
               QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);

               if ((pDevExt->FDO == CalledDO) && (pDevExt->bInService == TRUE))
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> QUERY_REMOVAL: open handle\n", pDevExt->PortName)
                  );
                  ntStatus = STATUS_UNSUCCESSFUL;
               }
               else if ((pDevExt->FDO == NULL) || (pDevExt->FDO == CalledDO))
               {
                  // clearDevState(DEVICE_STATE_PRESENT_AND_STARTED);
                  // setDevState(DEVICE_STATE_SURPRISE_REMOVED);
                  // pDevExt->bDeviceSurpriseRemoved = TRUE;
                  setDevState(DEVICE_STATE_DEVICE_QUERY_REMOVE);
                  ntStatus = STATUS_SUCCESS;
                  // pDevExt->FDO = NULL;  // 07-19
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> QUERY_REMOVAL: stray-away FDO 0x%p/0x%p\n",
                       pDevExt->PortName, CalledDO, pDevExt->FDO)
                  );
                  ntStatus = STATUS_SUCCESS;
               }

               if (ntStatus != STATUS_SUCCESS)
               {
                  Irp->IoStatus.Status = ntStatus;
                  QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
                  goto QCDSP_Dispatch_Done0;
               }

               // else pass it down the stack
               QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

               if (ntStatus == STATUS_SUCCESS)
               {
                  QCPWR_CancelWaitWakeIrp(pDevExt, 5);
               }
               break;

            case IRP_MN_CANCEL_REMOVE_DEVICE:  // PASSIVE_LEVEL
            {
               IoCopyCurrentIrpStackLocationToNext(Irp);

               // after the copy set the completion routine

               IoSetCompletionRoutine
               (
                  Irp,
                  QCMAIN_IrpCompletionSetEvent,
                  &ePNPEvent,
                  TRUE,
                  TRUE,
                  TRUE
               );

               KeInitializeEvent(&ePNPEvent, SynchronizationEvent, FALSE);

               ntStatus = IoCallDriver
                          (
                             pDevExt->StackDeviceObject,
                             Irp
                          );
               if (!NT_SUCCESS(ntStatus))
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_DETAIL,
                     ("<%s> IRP_MN_CANCEL_REMOVE lowerERR= 0x%x\n", pDevExt->PortName, ntStatus)
                  );

                  // CR137325
                  delayValue.QuadPart = -(10 * 1000 * 1000); // 1.0 sec
                  KeWaitForSingleObject
                  (
                     &ePNPEvent,
                     Executive,
                     KernelMode,
                     FALSE,
                     &delayValue
                  );
                  goto QCDSP_Dispatch_Done0;
               }

               ntStatus = KeWaitForSingleObject
                          (
                             &ePNPEvent,
                             Executive,
                             KernelMode,
                             FALSE,
                             NULL
                          );

               if (((pDevExt->FDO == CalledDO) || (DeviceObject == CalledDO)) &&
                   (inDevState(DEVICE_STATE_DEVICE_QUERY_REMOVE)))
                   // (inDevState(DEVICE_STATE_SURPRISE_REMOVED)))
               {
                  // QCDSP_RestartDeviceFromCancelStopRemove(DeviceObject, Irp); // restart device
                  clearDevState(DEVICE_STATE_DEVICE_QUERY_REMOVE);
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> CANCEL_REMOVE_DEVICE: late cFDO 0x%p\n", pDevExt->PortName, pDevExt->FDO)
                  );
                  Irp->IoStatus.Status = STATUS_SUCCESS;
               }
               Irp->IoStatus.Information = 0;
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_CONTROL,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> IRP_MN_CANCEL_REMOVE done: 0x%x\n", pDevExt->PortName, ntStatus)
               );
               break;
            }
            case IRP_MN_SURPRISE_REMOVAL:  // PASSIVE_LEVEL
            {
               if (pDevExt->FDO != CalledDO)
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_FORCE,
                     ("<%s> SURPRISE_REMOVAL: wrong FDO! 0x%p/0x%p\n",
                       pDevExt->PortName, pDevExt->FDO, CalledDO)
                  );
                  Irp->IoStatus.Status = STATUS_SUCCESS;
                  break;
               }

               clearDevState(DEVICE_STATE_PRESENT_AND_STARTED);
               setDevState(DEVICE_STATE_SURPRISE_REMOVED);

               // Disable registered interfaces
               if (pDevExt->ucsIfaceSymbolicLinkName.Length > 0)
               {
                  IoSetDeviceInterfaceState
                  (
                     &pDevExt->ucsIfaceSymbolicLinkName, FALSE
                  );
               }

               QCSER_PostRemovalNotification(pDevExt);
               QCDEV_DeregisterDeviceInterface(pDevExt); // QCDEV_PostNotification(pDevExt);
               QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
               // SerialClrDtrRts(DeviceObject, TRUE);
               pDevExt->FDO = NULL;
               QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

               StopInterruptService(pDevExt, TRUE, 2);

               pDevExt->bDeviceRemoved = TRUE;
               pDevExt->bDeviceSurpriseRemoved = TRUE;
               cancelAllIrps( DeviceObject );

               Irp->IoStatus.Status = STATUS_SUCCESS;

               // just send on down to device
               break;
            }
            case IRP_MN_REMOVE_DEVICE:  // PASSIVE_LEVEL
               QCSER_PostRemovalNotification(pDevExt);
               QCDEV_DeregisterDeviceInterface(pDevExt); // QCDEV_PostNotification(pDevExt);
               if ((pDevExt->FDO == NULL) || (pDevExt->FDO == CalledDO))
               {
                  StopInterruptService(pDevExt, TRUE, 3);
                  QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
                  clearDevState(DEVICE_STATE_PRESENT_AND_STARTED);
                  clearDevState(DEVICE_STATE_USB_INITIALIZED);
                  setDevState(DEVICE_STATE_DEVICE_REMOVED0);
                  pDevExt->FDO = NULL;
                  QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
                  QCDSP_CleanUp(DeviceObject, Irp);  // once again
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_FORCE,
                     ("<%s> REMOVE: WARN - late FDO rm 0x%p/rm0x%p PTDOflag 0x%x\n",
                       myPortName, pDevExt->FDO, CalledDO, DeviceObject->Flags)
                  );
               }

               QcAcquireEntryPass(&gSyncEntryEvent, myPortName);

               QCPWR_DeregisterWmiPowerGuids(pDevExt);
               QCSER_RemoveFdoFromCollection(pDevExt, CalledDO);
               QCINT_StopRegistryAccess(pDevExt);
               QCPNP_HandleRemoveDevice(DeviceObject, CalledDO, Irp);

               // Nullify top stack DO
               QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
               if (pDevExt->StackTopDO != NULL)
               {
                  ObDereferenceObject(pDevExt->StackTopDO);
                  pDevExt->StackTopDO = NULL;
                  pDevExt->bStackOpen = FALSE;
               }
               QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

               // if we don't do this, the driver will be resident
               if ((pDevExt->bInService == FALSE) &&
                   (pDevExt->FDO == NULL)         &&
                   (pDevExt->FdoChain == NULL)    && // FDO removed from chain before this
                   (gVendorConfig.DriverResident == 0))
               {
                  bRemoveRequest = TRUE;
                  RemoveSymbolicLinks(DeviceObject);
                  QCPTDO_RemovePort(DeviceObject, Irp);
               }
               else
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_CONTROL,
                     QCSER_DBG_LEVEL_INFO,
                     ("<%s> Before QcIoReleaseRemoveLock (%ld,%ld,%ld,%ld)\n",
                       pDevExt->PortName,
                       pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
                       pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3])
                  );
                  QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);

                  QcReleaseEntryPass(&gSyncEntryEvent, myPortName, "RMDEV");
               }
               ntStatus = STATUS_SUCCESS;

               Irp->IoStatus.Status = ntStatus;

               // our dispatch routine completes the IRP
               goto QCDSP_Dispatch_Done0;

            case IRP_MN_QUERY_ID:  // PASSIVE_LEVEL
               QCSER_DbgPrint2
               (
                  QCSER_DBG_MASK_CONTROL,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> IRP_MN_QUERY_ID: 0x%x\n", myPortName,
                    irpStack->Parameters.QueryId.IdType)
               );
               break; // case IRP_MN_QUERY_ID

            case IRP_MN_QUERY_PNP_DEVICE_STATE:
               // Irp->IoStatus.Information = PNP_DEVICE_RESOURCE_REQUIREMENTS_CHANGED; 
               // Irp->IoStatus.Status = STATUS_SUCCESS;
               break; // case IRP_MN_QUERY_PNP_DEVICE_STATE

            case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
               break; // case IRP_MN_QUERY_RESOURCE_REQUIREMENTS

            case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
               break; // case IRP_MN_FILTER_RESOURCE_REQUIREMENTS

            case IRP_MN_QUERY_DEVICE_RELATIONS:
               break; // case IRP_MN_QUERY_DEVICE_RELATIONS

            case IRP_MN_QUERY_LEGACY_BUS_INFORMATION:
               // MSDN says "This IRP is reserved for system use"
               // and WDM.H doesn't define the IRP type
               // (although ntddk.h does).
               break;

            default:
            {
               QCSER_DbgPrint2
               (
                  QCSER_DBG_MASK_CONTROL,
                  QCSER_DBG_LEVEL_DETAIL,
                  ("<%s> PNP IRP MN 0x%x not handled\n", myPortName, irpStack->MinorFunction)
               );
            }
            
         } //switch pnp minorfunction

         // All PNP messages get passed to PhysicalDeviceObject.

         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CIRP,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> CIRP: (C FWD) 0x%p\n", myPortName, Irp)
         );
         IoSkipCurrentIrpStackLocation(Irp);
         // IoSetCancelRoutine(Irp, NULL);  // DV ?
         ntStatus = IoCallDriver( pUsbDevObject, Irp );

         /*
         * Bypass the IoCompleteRequest since the USBD stack took care of it.
         */
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
         goto QCDSP_Dispatch_Done;
         break;         // pnp power
         
      } // IRP_MJ_PNP

      case IRP_MJ_SYSTEM_CONTROL:
      {
         ntStatus = QCPWR_ProcessSystemControlIrps(pDevExt, Irp);
         if (!NT_SUCCESS(ntStatus))
         {
            // forward
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_PIRP,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> CIRP (Fwd-0 0x%x) 0x%p\n", myPortName, Irp->IoStatus.Status, Irp)
            );
            IoSkipCurrentIrpStackLocation(Irp);
            ntStatus = IoCallDriver( pUsbDevObject, Irp );
         }
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);

         goto QCDSP_Dispatch_Done; // the USBD stack took care of it
         break;         // power           
      } // IRP_MJ_SYSTEM_CONTROL i.e. WMI

      case IRP_MJ_QUERY_INFORMATION:
      {
         Irp->IoStatus.Status = SerialQueryInformation(DeviceObject, Irp);
         break;
      }

      case IRP_MJ_SET_INFORMATION:
      {
         Irp->IoStatus.Status = SerialSetInformation(DeviceObject, Irp);
         break;
      }

      default:
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CIRP,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> CIRP: (C FWD) 0x%p\n", myPortName, Irp)
         );
         IoSkipCurrentIrpStackLocation(Irp);
         ntStatus = IoCallDriver( pUsbDevObject, Irp );

         // Bypass the IoCompleteRequest since the USBD stack took care of it
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
         goto QCDSP_Dispatch_Done;
         break;  // power           
      }
   }// switch majorfunction

QCDSP_Dispatch_Done0:

   ntStatus = Irp->IoStatus.Status;

   if (ntStatus == STATUS_PENDING)
   {
      _IoMarkIrpPending(Irp);
      if (bRemoveRequest == TRUE)
      {
         QCSER_DbgPrintG
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> Dispatch: (P 0x%p)-0x%x\n", myPortName, Irp, ntStatus)
         );
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> Dispatch: (P 0x%p)-0x%x\n", myPortName, Irp, ntStatus)
         );
      }
   }
   else
   {
      if (bRemoveRequest == TRUE)
      {
         QCSER_DbgPrintG
         (
            QCSER_DBG_MASK_CIRP,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> CIRP: (C 0x%x) 0x%p\n", myPortName, ntStatus, Irp)
         );
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CIRP,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> CIRP: (C 0x%x) 0x%p\n", myPortName, ntStatus, Irp)
         );
      }
      ASSERT(ntStatus == STATUS_SUCCESS       ||
             ntStatus == STATUS_NOT_SUPPORTED ||
             ntStatus == STATUS_DEVICE_NOT_CONNECTED ||
             ntStatus == STATUS_CANCELLED ||
             ntStatus == STATUS_DELETE_PENDING
            );

      if ( (irpStack->MajorFunction == IRP_MJ_PNP &&
            irpStack->MinorFunction == IRP_MN_REMOVE_DEVICE) ||
           ((irpStack->MajorFunction == IRP_MJ_CREATE) &&
            (ntStatus == STATUS_SUCCESS)) )
      {
         _IoCompleteRequest(Irp, IO_NO_INCREMENT);
      }
      else
      {
         if (irpStack->MajorFunction == IRP_MJ_CLOSE &&
             ntCloseStatus == STATUS_UNSUCCESSFUL)
         {
            _IoCompleteRequest(Irp, IO_NO_INCREMENT);
         }
         else
         {
            if ((irpStack->MajorFunction == IRP_MJ_CLOSE) ||
                ((irpStack->MajorFunction == IRP_MJ_CREATE) &&
                 (ntStatus != STATUS_SUCCESS)) )
            {
               // releasing the RemoveLock acquired by IRP_MJ_CREATE
               QcIoReleaseRemoveLock(pDevExt->pRemoveLock, NULL, 4);
            }
            else
            {
               QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
            }
            _IoCompleteRequest(Irp, IO_NO_INCREMENT);
         }
      }
   }

QCDSP_Dispatch_Done:

   if (bRemoveRequest == TRUE)
   {
      QCSER_DbgPrintG2
      (
         QCSER_DBG_MASK_CIRP,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> DSP: Exit\n", myPortName)
      );
   }

   *Removed = bRemoveRequest;

   return ntStatus;
}  // QCDSP_Dispatch

NTSTATUS QCDSP_CleanUp(IN PDEVICE_OBJECT DeviceObject, PIRP pIrp)
{
   PDEVICE_EXTENSION pDevExt;
   KIRQL cancelIrql;
   NTSTATUS ntStatus;
   KIRQL IrqLevel;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   pDevExt = DeviceObject->DeviceExtension;

   vResetReadBuffer(pDevExt, 0);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> _CleanUp: - CancelReadThread\n", pDevExt->PortName)
   );

   QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
   if (pDevExt->CleanupInProgress == TRUE)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> _CleanUp: already in progress\n", pDevExt->PortName)
      );
      QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);
      return STATUS_SUCCESS;
   }
   pDevExt->CleanupInProgress = TRUE;
   QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

   // DbgPrint("<%s> HG-DBG: Cxl RD-0\n", pDevExt->PortName);
   ntStatus = CancelReadThread(pDevExt, 1);
   // DbgPrint("<%s> HG-DBG: Cxl RD-1\n", pDevExt->PortName);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> _CleanUp: - CancelWriteThread\n", pDevExt->PortName)
   );
   // DbgPrint("<%s> HG-DBG: Cxl WT-0\n", pDevExt->PortName);
   ntStatus = CancelWriteThread(pDevExt, 0);
   // DbgPrint("<%s> HG-DBG: Cxl WT-1\n", pDevExt->PortName);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> _CleanUp: - CancelSglIrp\n", pDevExt->PortName)
   );
   ntStatus = CancelWOMIrp( DeviceObject );
   ntStatus = CancelNotificationIrp( DeviceObject );

   QCUTILS_CleanupReadWriteQueues(pDevExt);

   // cancelIrps( DeviceObject );

   pIrp->IoStatus.Information = 0;

   pDevExt->CleanupInProgress = FALSE;

   return STATUS_SUCCESS;
}  // QCDSP_Cleanup

NTSTATUS QCDSP_SendIrpToStack(IN PDEVICE_OBJECT PortDO, IN PIRP Irp, char *info)
{
   BOOLEAN topDoAssigned = FALSE;
   NTSTATUS ntStatus;
   PDEVICE_OBJECT currStackTopDO;
   PDEVICE_EXTENSION pDevExt;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   pDevExt = PortDO->DeviceExtension;

   QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);

   if (pDevExt->FDO == NULL)
   {
      QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CIRP,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> %s: NIL FDO - 0x%p\n", pDevExt->PortName, info, Irp)
      );

      return QcCompleteRequest(Irp, STATUS_DELETE_PENDING, 0);
   }

   currStackTopDO = IoGetAttachedDeviceReference(pDevExt->FDO);
   if (currStackTopDO != NULL)
   {
      InterlockedIncrement(&pDevExt->TopDoCount);
      if (pDevExt->StackTopDO == NULL)
      {
         pDevExt->StackTopDO = currStackTopDO;
         topDoAssigned = TRUE;
      }
   }
   else
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> %s: stack top DO gone!\n", pDevExt->PortName)
      );

      return QcCompleteRequest(Irp, STATUS_DELETE_PENDING, 0);
   }

   QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

/***
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> %s: to stack-0 0x%p=>0x%p 0x%p[%d/%d:%d]\n", pDevExt->PortName,
        info, Irp, pDevExt->StackTopDO, pDevExt->FDO, Irp->CurrentLocation,
        Irp->StackCount, KeGetCurrentIrql())
   );
***/
   if (Irp->CurrentLocation == Irp->StackCount)
   {
      // we are on the top of the device stack, in this case we do not call
      // IoSkipCurrentIrpStackLocation which will make the current IRP
      // stack location out of bound. Though it may not be a problem but
      // we cannot guaranttee that no other party such as IoCallDriver
      // would try to do something to the current IRP stack location.
      IoCopyCurrentIrpStackLocationToNext(Irp);
   }
   else
   {
      IoSkipCurrentIrpStackLocation(Irp);
   }

   if (currStackTopDO == pDevExt->StackTopDO)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> %s: to stack-1 0x%p=>0x%p 0x%p[%d/%d:%d] RefC %d\n", pDevExt->PortName,
           info, Irp, pDevExt->StackTopDO, pDevExt->FDO, Irp->CurrentLocation,
           Irp->StackCount, KeGetCurrentIrql(), pDevExt->TopDoCount)
      );
      ntStatus = IoCallDriver(pDevExt->StackTopDO, Irp);
   }
   else
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> %s: to stack fdo 0x%p=>0x%p[%d/%d:%d] RefC %d\n", pDevExt->PortName, info,
           Irp, pDevExt->FDO, Irp->CurrentLocation, Irp->StackCount, KeGetCurrentIrql(), pDevExt->TopDoCount)
      );

      ntStatus = IoCallDriver(pDevExt->FDO, Irp);
   }
   if (topDoAssigned == FALSE)
   {
      ObDereferenceObject(currStackTopDO);
      InterlockedDecrement(&pDevExt->TopDoCount);
   }

   return ntStatus;

}  // QCDSP_SendIrpToStack

VOID QCDSP_PurgeDispatchQueue(PDEVICE_EXTENSION pDevExt)
{
   PLIST_ENTRY headOfList;
   PQCDSP_IOBlockType pDispatchIrp;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> PurgeDspQ\n", pDevExt->PortName)
   );

   QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);

   while (!IsListEmpty(&pDevExt->DispatchFreeQueue))
   {
      headOfList = RemoveHeadList(&pDevExt->DispatchFreeQueue);
      pDispatchIrp = CONTAINING_RECORD
                     (
                        headOfList,
                        QCDSP_IOBlockType,
                        List
                     );
      ExFreePool(pDispatchIrp);
      InterlockedDecrement(&(pDevExt->Sts.lAllocatedDSPs));
   }

   while (!IsListEmpty(&pDevExt->DispatchQueue))
   {
      headOfList = RemoveHeadList(&pDevExt->DispatchQueue);
      pDispatchIrp = CONTAINING_RECORD
                     (
                        headOfList,
                        QCDSP_IOBlockType,
                        List
                     );
      if (pDispatchIrp != NULL)
      {
         if (IoSetCancelRoutine(pDispatchIrp->Irp, NULL) == NULL)
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_CRITICAL,
               ("<%s> DSP: Cxled IRP 0x%p\n", pDevExt->PortName, pDispatchIrp->Irp)
            );
            pDispatchIrp = NULL;
         }
         else
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CIRP,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> CIRP: (Cx_dsp 0x%p)\n", pDevExt->PortName, pDispatchIrp->Irp)
            );
            QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

            pDispatchIrp->Irp->IoStatus.Status = STATUS_CANCELLED;
            QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pDispatchIrp->Irp, 0);
            _IoCompleteRequest(pDispatchIrp->Irp, IO_NO_INCREMENT);
            ExFreePool(pDispatchIrp);
            InterlockedDecrement(&(pDevExt->Sts.lAllocatedCtls));
            InterlockedDecrement(&(pDevExt->Sts.lAllocatedDSPs));
            pDispatchIrp = NULL;

            QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
         }
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> Dth: ERR - NULL IRP\n", pDevExt->PortName)
         );
      }
   } // while

   QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_CRITICAL,
      ("<%s> PG-RML <%ld, %ld, %ld, %ld, %ld> RW <%ld/%ld, %ld/%ld, %ld/%ld>\n\n",
        pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
        pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3], pDevExt->Sts.lRmlCount[4],
        pDevExt->Sts.lAllocatedCtls, pDevExt->Sts.lAllocatedDSPs,
        pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedRdMem,
        pDevExt->Sts.lAllocatedWrites, pDevExt->Sts.lAllocatedWtMem)
   );
}  // QCDSP_PurgeDispatchQueue

NTSTATUS QCDSP_RestartDeviceFromCancelStopRemove
(
   PDEVICE_OBJECT DeviceObject,
   PIRP Irp
)
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus;

   pDevExt = DeviceObject->DeviceExtension;

   ResumeInterruptService(pDevExt, 2);
   ntStatus = QCPNP_StartDevice(DeviceObject, 1);
   if (NT_SUCCESS(ntStatus))
   {
      setDevState(DEVICE_STATE_DEVICE_STARTED);
      clearDevState(DEVICE_STATE_DEVICE_STOPPED);
   }
   Irp->IoStatus.Status = ntStatus;

   pDevExt->bDeviceRemoved = FALSE;
   pDevExt->bDeviceSurpriseRemoved = FALSE;
   pDevExt->RdErrorCount = pDevExt->WtErrorCount = 0;
   pDevExt->bL1Stopped = pDevExt->bL2Stopped = FALSE;
   pDevExt->bWriteStopped  = FALSE;

   if (pDevExt->bInService == TRUE)
   {
      QCSER_StartDataThreads(pDevExt);
   }
   else
   {
      QCUSB_CDC_SetInterfaceIdle
      (
         pDevExt->MyDeviceObject,
         pDevExt->DataInterface,
         TRUE,
         6
      );
   }
   KeSetEvent(&pDevExt->L1KickReadEvent, IO_NO_INCREMENT, FALSE);
   KeSetEvent(&pDevExt->KickWriteEvent, IO_NO_INCREMENT, FALSE);

   // Now, create the symbolic link and update registry
   if (QCPNP_CreateSymbolicLink(DeviceObject) == FALSE)
   {
      ntStatus = STATUS_UNSUCCESSFUL;
   }

   if (pDevExt->bRemoteWakeupEnabled)
   {
       QCPWR_RegisterWaitWakeIrp(pDevExt, 8);
   }
   QCPWR_SetIdleTimer(pDevExt, 0, FALSE, 17); // start device

   return ntStatus;

}  // QCDSP_RestartDeviceFromCancelStopRemove

BOOLEAN QCDSP_ToProcessIrp
(
   PDEVICE_EXTENSION pDevExt,
   PIRP              Irp
)
{
   PIO_STACK_LOCATION irpStack;

   irpStack = IoGetCurrentIrpStackLocation(Irp);

   if ((irpStack->MajorFunction == IRP_MJ_PNP) &&
       (irpStack->MinorFunction == IRP_MN_REMOVE_DEVICE))
   {
      if (pDevExt->Sts.lRmlCount[0] > 1)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> _ToProcessIrp: outstanding IRPs %d\n", pDevExt->PortName,
              pDevExt->Sts.lRmlCount[0])
         );

         if (pDevExt->PowerSuspended == FALSE)
         {
            // Device is in D0
            QCPWR_CancelIdleTimer(pDevExt, QCUSB_BUSY_CTRL, TRUE, 6);
         }
         else
         {
            KeSetEvent
            (
               &(DeviceInfo[pDevExt->MgrId].DspPreWakeUpEvent),
               IO_NO_INCREMENT,
               FALSE
            );
         }
         return FALSE;
      }
   }

   return TRUE;
}  // QCDSP_ToProcessRemoveIrp
