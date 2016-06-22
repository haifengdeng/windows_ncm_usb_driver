/*===========================================================================
FILE: QCPTDO.c

DESCRIPTION:
   This file contains virtual COM port management functions.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003-2007 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#include <stdio.h>
#include "QCPTDO.h"
#include "QCRD.h"
#include "QCINT.h"
#include "QCPNP.h"
#include "QCUTILS.h"
#include "QCDSP.h"
#include "QCSER.h"
#include "QCMGR.h"
#include "QCPWR.h"

PQCSER_PortDeviceListType gPortDOList = NULL;
static USHORT gPortDOCount = 0;
static ULONG  gDeviceIndex = 0;
static KSPIN_LOCK gPortSpinLock; // lock to guard global port queue

PDEVICE_OBJECT QCPTDO_FindPortDOByDO(PDEVICE_OBJECT PortDO, KIRQL Irql)
{
   PQCSER_PortDeviceListType queuedPortDO;
   PDEVICE_OBJECT foundDO = NULL;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QcAcquireSpinLockWithLevel(&gPortSpinLock, &levelOrHandle, Irql);

   queuedPortDO = gPortDOList;
   while (queuedPortDO != NULL)
   {
      if (queuedPortDO->PortDO == PortDO)
      {
         foundDO = queuedPortDO->PortDO;
         break;
      }
      queuedPortDO = queuedPortDO->Next;
   }

   QcReleaseSpinLockWithLevel(&gPortSpinLock, levelOrHandle, Irql);
   return foundDO;

} //QCPTDO_FindPortDOByDO

PDEVICE_OBJECT QCPTDO_FindPortDOByFDO(PDEVICE_OBJECT FDO, KIRQL Irql)
{
   PDRIVER_OBJECT myDriverObject;
   PDEVICE_OBJECT pDevObj;
   PDEVICE_OBJECT foundDO = NULL;
   PQCSER_PortDeviceListType queuedPortDO;
   PDEVICE_EXTENSION pDevExt;
   PFDO_DEVICE_EXTENSION pFdoExt;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QcAcquireSpinLockWithLevel(&gPortSpinLock, &levelOrHandle, Irql);

   queuedPortDO = gPortDOList;

   while (queuedPortDO != NULL)
   {
      pDevExt = queuedPortDO->PortDO->DeviceExtension;
      if ((pDevExt->FDO == FDO) || (queuedPortDO->PortDO == FDO))
      {
         foundDO = queuedPortDO->PortDO;
         break;
      }
      queuedPortDO = queuedPortDO->Next;
   }

   // must be a to-be-removed FDO, so search the device object chain
   if (foundDO == NULL)
   {
      myDriverObject = FDO->DriverObject;
      pDevObj = myDriverObject->DeviceObject;
      while (pDevObj)
      {
         if (pDevObj == FDO)
         {
            pFdoExt = pDevObj->DeviceExtension;
            foundDO = pFdoExt->PortDevice;
            break;
         }
         pDevObj = pDevObj->NextDevice;
      }
   }

   QcReleaseSpinLockWithLevel(&gPortSpinLock, levelOrHandle, Irql);
   return foundDO;

} //QCPTDO_FindPortDOByFDO

// This routine must be running at IRQL PASSIVE_LEVEL
PDEVICE_OBJECT QCPTDO_FindPortDOByPort(PUNICODE_STRING Port)
{
   PDEVICE_EXTENSION pDevExt;
   PQCSER_PortDeviceListType queuedPortDO;
   PDEVICE_OBJECT foundDO = NULL;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QcAcquireSpinLock(&gPortSpinLock, &levelOrHandle);

   queuedPortDO = gPortDOList;
   while (queuedPortDO != NULL)
   {
      pDevExt = (PDEVICE_EXTENSION)queuedPortDO->PortDO->DeviceExtension;

      // RtlCompareUnicodeString() must be running at PASSIVE level, and
      // RtlCompareMemory() would fail (no interest to investigate),
      // so we designed our own function to compare unicode port names
      if (QCPTDO_ComparePortName(&(pDevExt->ucsPortName), Port) == TRUE)
      {
         foundDO = queuedPortDO->PortDO;
         break;
      }
      queuedPortDO = queuedPortDO->Next;
   }

   QcReleaseSpinLock(&gPortSpinLock, levelOrHandle);
   return foundDO;

} // QCPTDO_FindPortDOByPort

void QCPTDO_DisplayListInfo(void)
{
   PDEVICE_EXTENSION pDevExt;
   PQCSER_PortDeviceListType queuedPortDO;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QcAcquireSpinLock(&gPortSpinLock, &levelOrHandle);

   queuedPortDO = gPortDOList;

   while (queuedPortDO != NULL)
   {
      pDevExt = (PDEVICE_EXTENSION)queuedPortDO->PortDO->DeviceExtension;

      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> PortDO List: (DO, FDO)=(0x%p[%s], 0x%p)\n",
           gDeviceName, queuedPortDO->PortDO, pDevExt->PortName, pDevExt->FDO)
      );
      queuedPortDO = queuedPortDO->Next;
   }

   QcReleaseSpinLock(&gPortSpinLock, levelOrHandle);

} // QCPTDO_DisplayListInfo

// Create new PTDO
PDEVICE_OBJECT QCPTDO_CreateNewPTDO
(
   PDRIVER_OBJECT  pDriverObject,
   PDEVICE_OBJECT  PhysicalDeviceObject,
   PUNICODE_STRING pucPortName,
   char*           myPortName
)
{
   NTSTATUS            ntStatus               = STATUS_SUCCESS;
   PDEVICE_OBJECT      deviceObject           = NULL;
   PDEVICE_EXTENSION   pDevExt                = NULL;
   PUCHAR              pucNewReadBuffer       = NULL;
   PUCHAR              pucByteStuffingBuffer  = NULL;
   QCRD_L2BUFFER       *pL2ReadBuffer0        = NULL;
   char                myDeviceName[32];

   UNICODE_STRING ucUnprotectedLink; // "\$$\COMn(n)"
   UNICODE_STRING ucDeviceMapEntry;  // "\Device\QCOMSERn(nn)"
   UNICODE_STRING ucPortName;        // "COMn(n)"
   UNICODE_STRING ucDeviceNumber;    // "n(nn)"
   UNICODE_STRING ucDeviceNameBase;  // "QCOMSER" from registry
   UNICODE_STRING ucDeviceName;      // "QCOMSERn(nn)"
   UNICODE_STRING ucValueName;
   UNICODE_STRING tmpUnicodeString;
   ANSI_STRING    tmpAnsiString;

   _zeroUnicode(ucUnprotectedLink);
   _zeroUnicode(ucDeviceMapEntry);
   _zeroUnicode(ucPortName);
   _zeroUnicode(ucDeviceNumber);
   _zeroUnicode(ucDeviceNameBase);
   _zeroUnicode(ucDeviceName);
   _zeroUnicode(tmpUnicodeString);

   ucPortName.Buffer        = pucPortName->Buffer;
   ucPortName.Length        = pucPortName->Length;
   ucPortName.MaximumLength = pucPortName->MaximumLength;
   pucPortName->Buffer      = NULL;
   pucPortName->Length      = 0;

   ntStatus = AllocateUnicodeString
              (
                 &ucUnprotectedLink,
                 MAX_NAME_LEN,
                 "PnPAddDevice, ucUnprotectedLink.Buffer"
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_FORCE,
         ("<%s> QCPTDO: NO MEM - 3\n", myPortName)
      );
      goto QCPTDO_CreateNewPTDO_Return;
   }
   ntStatus = AllocateUnicodeString
              (
                 &ucDeviceMapEntry,
                 MAX_NAME_LEN,
                 "PnPAddDevice, ucDeviceMapEntry.Buffer"
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_FORCE,
         ("<%s> QCPTDO: NO MEM - 4\n", myPortName)
      );
      goto QCPTDO_CreateNewPTDO_Return;
   }
   ntStatus = AllocateUnicodeString
              (
                 &ucDeviceNumber,
                 MAX_NAME_LEN,
                 "PnPAddDevice, ucDeviceNumber.Buffer"
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_FORCE,
         ("<%s> QCPTDO: NO MEM - 5\n", myPortName)
      );
      goto QCPTDO_CreateNewPTDO_Return;
   }
   ntStatus = AllocateUnicodeString
              (
                 &ucDeviceName,
                 MAX_NAME_LEN,
                 "PnPAddDevice, ucDeviceName.Buffer"
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_FORCE,
         ("<%s> QCPTDO: NO MEM - 6\n", myPortName)
      );
      goto QCPTDO_CreateNewPTDO_Return;
   }

   ntStatus = AllocateUnicodeString
              (
                 &ucDeviceNameBase,
                 MAX_NAME_LEN,
                 "PnPAddDevice, ucDeviceNameBase.Buffer"
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_FORCE,
         ("<%s> QCPTDO: NO MEM - 7\n", myPortName)
      );
      goto QCPTDO_CreateNewPTDO_Return;
   }

   // Construct ucDeviceNameBase
   sprintf(myDeviceName, "QCUSB_%s_", myPortName);
   RtlInitAnsiString(&tmpAnsiString, myDeviceName);
   RtlAnsiStringToUnicodeString(&tmpUnicodeString, &tmpAnsiString, TRUE);
   RtlCopyUnicodeString(OUT &ucDeviceNameBase,IN &tmpUnicodeString);
   RtlFreeUnicodeString(&tmpUnicodeString);

   ntStatus = STATUS_OBJECT_NAME_COLLISION;

   while (ntStatus == STATUS_OBJECT_NAME_COLLISION)
   {
      // Generate new devie number
      InterlockedIncrement(&gDeviceIndex);
      ntStatus = RtlIntegerToUnicodeString
                 (
                    gDeviceIndex,
                    10,
                    &ucDeviceNumber
                 );                  // "n(nn)"

      // Construct device name
      RtlCopyUnicodeString
      (
         &ucDeviceName,
         &ucDeviceNameBase
      );                             // "QCOMSER"
      RtlAppendUnicodeStringToString
      (
         &ucDeviceName,
         &ucDeviceNumber
      );                             // "QCOMSERn(nn)"

      RtlInitUnicodeString(&ucValueName, DEVICE_NAME_PATH);   // "\Device\"
      RtlCopyUnicodeString(&ucDeviceMapEntry, &ucValueName);  // "\Device\"
      RtlAppendUnicodeStringToString
      (
         &ucDeviceMapEntry,
         &ucDeviceName
      );                              // "\Device\QCOMSERn(nn)"

      _dbgPrintUnicodeString(&ucDeviceMapEntry,"IoCreateDevice");

      // Create device object -- PTDO
      ntStatus = IoCreateDevice
                 (
                    pDriverObject,
                    sizeof(DEVICE_EXTENSION),
                    &ucDeviceMapEntry,  // "\Device\QCOMSERn(nn)"
                    FILE_DEVICE_SERIAL_PORT, // FILE_DEVICE_UNKNOWN, FILE_DEVICE_MODEM
                    0, // FILE_DEVICE_SECURE_OPEN, // DeviceCharacteristics
                    FALSE,
                    &deviceObject
                 );

      if (NT_SUCCESS(ntStatus))
      {
         deviceObject -> Flags |= DO_DIRECT_IO;
         deviceObject -> Flags &= ~DO_EXCLUSIVE;
         QCSER_DbgPrintG
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> QCPTDO_CreateNewPTDO: new PTDO 0x%p (flg 0x%x)\n",
              myPortName, deviceObject, deviceObject->Flags)
         );
      }
      else if (ntStatus != STATUS_OBJECT_NAME_COLLISION)
      {
         QCSER_DbgPrintG
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> QCPTDO_CreateNewPTDO: new PTDO failure 0x%x\n", myPortName, ntStatus)
         );
         deviceObject = NULL;
         break;
      }
      else
      {
         QCSER_DbgPrintG
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> QCPTDO_CreateNewPTDO: PTDO NAME_COLLISION, retry\n", myPortName)
         );
      }
   } // while

   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> QCPTDO_CreateNewPTDO: new PTDO-2 0x%p\n", myPortName, deviceObject)
   );

   if (deviceObject == NULL)
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_FORCE,
         ("<%s> QCPTDO: NO DEV - 0\n", myPortName)
      );
      goto QCPTDO_CreateNewPTDO_Return;
   }

   // Create symbolic links -- Unprotected
   RtlInitUnicodeString(&ucValueName, DEVICE_LINK_NAME_PATH); //"\??\"
   RtlCopyUnicodeString(&ucUnprotectedLink, &ucValueName);    //"\??\"
   RtlAppendUnicodeStringToString
   (
      &ucUnprotectedLink,
      &ucPortName
   );                             //"\??\COMn(n)"
   _dbgPrintUnicodeString
   (
      &ucUnprotectedLink,
      "IoCreateUnprotectedSymbolicLink"
   );

   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> QCPTDO_CreateNewPTDO: 3 0x%p\n", myPortName, deviceObject)
   );
   // Initialize device extension
   pDevExt = deviceObject->DeviceExtension;
   RtlZeroMemory(pDevExt, sizeof(DEVICE_EXTENSION));

   // StackDeviceObject will be assigned by FDO
   pDevExt->PhysicalDeviceObject = PhysicalDeviceObject;

   strcpy(pDevExt->PortName, myPortName);
   pDevExt->DebugMask            = gVendorConfig.DebugMask;
   pDevExt->DebugLevel           = gVendorConfig.DebugLevel;
   pDevExt->bFdoReused           = FALSE;
   pDevExt->bEnableByteStuffing  = FALSE;
   pDevExt->bVendorFeature       = FALSE;
   pDevExt->bByteStuffingFeature = FALSE;
   pDevExt->MyDeviceObject       = deviceObject;
   pDevExt->bSymLinksValid       = TRUE;
   pDevExt->pByteStuffingBuffer  = NULL;
   pDevExt->bBytePaddingFeature  = FALSE;
   pDevExt->bEnableBytePadding   = FALSE;

   // device config parameters
   pDevExt->ContinueOnOverflow  = gVendorConfig.ContinueOnOverflow;
   pDevExt->ContinueOnDataError = gVendorConfig.ContinueOnDataError;
   pDevExt->RetryOnTxError      = gVendorConfig.RetryOnTxError;
   pDevExt->UseReadArray        = gVendorConfig.UseReadArray;
   pDevExt->UseMultiWrites      = gVendorConfig.UseMultiWrites;
   pDevExt->EnableLogging       = gVendorConfig.EnableLogging;
   pDevExt->LogLatestPkts       = gVendorConfig.LogLatestPkts;
   pDevExt->NoTimeoutOnCtlReq   = gVendorConfig.NoTimeoutOnCtlReq;
   pDevExt->MinInPktSize        = gVendorConfig.MinInPktSize;
   pDevExt->WriteUnitSize       = gVendorConfig.WriteUnitSize;
   pDevExt->lReadBufferSize     = gVendorConfig.InternalReadBufSize;
   pDevExt->MaxPipeXferSize     = gVendorConfig.MaxPipeXferSize;
   pDevExt->NumberOfL2Buffers   = gVendorConfig.NumberOfL2Buffers;
   pDevExt->LoggingWriteThrough = gVendorConfig.LoggingWriteThrough;

   if ((pDevExt->UseReadArray == TRUE) || (pDevExt->UseMultiWrites == TRUE))
   {
      ULONG retries = QCUSB_MULTI_WRITE_BUFFERS + pDevExt->NumberOfL2Buffers;

      if (gVendorConfig.NumOfRetriesOnError < retries)
      {
         pDevExt->NumOfRetriesOnError = retries;
      }
      else
      {
         pDevExt->NumOfRetriesOnError = gVendorConfig.NumOfRetriesOnError;
      }
   }
   else
   {
      pDevExt->NumOfRetriesOnError = gVendorConfig.NumOfRetriesOnError;
   }

   // Create byte-stuffing buffer and init with 0xFF
   pDevExt->ulByteStuffingBufLen = QCSER_BYTE_STUFFING_BUF_SIZE;

   pucByteStuffingBuffer  = _ExAllocatePool
                            (
                               NonPagedPool,
                               pDevExt->ulByteStuffingBufLen,
                               "ByteStuffingBuffer"
                            );
   if (pucByteStuffingBuffer == NULL)
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> QCPTDO_CreateNewPTDO: ERR NO_MEM btst - 0x%x\n", myPortName, ntStatus)
      );
      ntStatus = STATUS_NO_MEMORY;
      pDevExt->ulByteStuffingBufLen = 0;
      IoDeleteDevice( deviceObject );
      deviceObject= NULL;
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_FORCE,
         ("<%s> QCPTDO: NO MEM - 8\n", myPortName)
      );
      goto QCPTDO_CreateNewPTDO_Return;
   }
   pDevExt->pByteStuffingBuffer = pucByteStuffingBuffer;
   pucByteStuffingBuffer = NULL;   // hand over the buffer
   RtlFillMemory
   (
      pDevExt->pByteStuffingBuffer,
      pDevExt->ulByteStuffingBufLen,
      0xFF
   );


   // allocate the read buffer here, before we decide to create the PTDO
   pucNewReadBuffer =  _ExAllocatePool
                       (
                          NonPagedPool,
                          2 + pDevExt->lReadBufferSize,
                          "pucNewReadBuffer"
                       );
   if (!pucNewReadBuffer)
   {
      pDevExt->lReadBufferSize /= 2;  // reduce the size and try again
      pucNewReadBuffer =  _ExAllocatePool
                          (
                             NonPagedPool,
                             2 + pDevExt->lReadBufferSize,
                             "pucNewReadBuffer"
                          );
      if (!pucNewReadBuffer)
      {
         QCSER_DbgPrintG
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_FORCE,
            ("<%s> QCPTDO: NO MEM - 0\n", myPortName)
         );
         ntStatus = STATUS_NO_MEMORY;
         IoDeleteDevice( deviceObject );
         deviceObject= NULL;
         goto QCPTDO_CreateNewPTDO_Return;
      }
   }

   pL2ReadBuffer0 = _ExAllocatePool
                    (
                       NonPagedPool,
                       sizeof(QCRD_L2BUFFER) * pDevExt->NumberOfL2Buffers,
                       "pL2ReadBuffer0"
                    );
   if (!pL2ReadBuffer0)
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_FORCE,
         ("<%s> QCPTDO: NO MEM - 2\n", myPortName)
      );
      ntStatus = STATUS_NO_MEMORY;
      IoDeleteDevice( deviceObject );
      deviceObject= NULL;
      goto QCPTDO_CreateNewPTDO_Return;
   }
   else
   {
      RtlZeroMemory
      (
         pL2ReadBuffer0,
         sizeof(QCRD_L2BUFFER) * pDevExt->NumberOfL2Buffers
      );
   }

   pDevExt->pL2ReadBuffer = pL2ReadBuffer0;
   pL2ReadBuffer0         = NULL;

   ntStatus = InitCommStructurePointers(deviceObject);
   if (!NT_SUCCESS(ntStatus))
   {
     IoDeleteDevice( deviceObject );
     deviceObject= NULL;
     QCSER_DbgPrintG
     (
        QCSER_DBG_MASK_CONTROL,
        QCSER_DBG_LEVEL_FORCE,
        ("<%s> QCPTDO: NO MEM - 7b\n", myPortName)
     );
     goto QCPTDO_CreateNewPTDO_Return;
   }

   pDevExt->bInService     = FALSE;  //open the gate
   pDevExt->bStackOpen     = FALSE;
   pDevExt->bL1ReadActive  = FALSE;
   pDevExt->bWriteActive   = FALSE;

   // Initialization for read timeout
   pDevExt->ReadTimeout.ucTimeoutType     = QCSER_READ_TIMEOUT_UNDEF;
   pDevExt->ReadTimeout.bUseReadInterval  = FALSE;
   pDevExt->ReadTimeout.bReturnOnAnyChars = FALSE;

   pDevExt->bReadBufferReset = FALSE;

   // initialize our unicode strings for object deletion
   pDevExt->ucsUnprotectedLink.Buffer = ucUnprotectedLink.Buffer;
   pDevExt->ucsUnprotectedLink.Length = ucUnprotectedLink.Length;
   pDevExt->ucsUnprotectedLink.MaximumLength =
      ucUnprotectedLink.MaximumLength;
   ucUnprotectedLink.Buffer = NULL; // we've handed off that buffer

   pDevExt->ucsDeviceMapEntry.Buffer = ucDeviceMapEntry.Buffer;
   pDevExt->ucsDeviceMapEntry.Length = ucDeviceMapEntry.Length;
   pDevExt->ucsDeviceMapEntry.MaximumLength =
      ucDeviceMapEntry.MaximumLength;
   ucDeviceMapEntry.Buffer = NULL; // we've handed off that buffer

   pDevExt->ucsPortName.Buffer = ucPortName.Buffer;
   pDevExt->ucsPortName.Length = ucPortName.Length;
   pDevExt->ucsPortName.MaximumLength = ucPortName.MaximumLength;
   ucPortName.Buffer = NULL;

   pDevExt->pucReadBufferStart = pucNewReadBuffer;
   pucNewReadBuffer            = NULL; // so we won't free it on exit from this sbr
   vResetReadBuffer(pDevExt, 2);

   // setup flow limits
   pDevExt->lReadBufferLow   = pDevExt->lReadBufferSize / 10;       /* 10% */
   pDevExt->lReadBufferHigh  = (pDevExt->lReadBufferSize * 9) / 10; /* 90% */
   pDevExt->lReadBuffer80pct = (pDevExt->lReadBufferSize * 4) / 5;  /* 80% */
   pDevExt->lReadBuffer50pct = pDevExt->lReadBufferSize / 2;        /* 50% */
   pDevExt->lReadBuffer20pct = pDevExt->lReadBufferSize / 5;        /* 20% */

   pDevExt->usCurrUartState    = 0;
   pDevExt->ulWaitMask         = 0xffff;
   pDevExt->pWaitOnMaskIrp     = NULL;
   pDevExt->pNotificationIrp   = NULL;
   pDevExt->pQcDevNotificationIrp = NULL;
   // pDevExt->pCancelledIrpStack = NULL;

   // Init locks, events, and mutex
   KeInitializeSpinLock(&pDevExt->ControlSpinLock);
   KeInitializeSpinLock(&pDevExt->ReadSpinLock);
   KeInitializeSpinLock(&pDevExt->WriteSpinLock);
   KeInitializeSpinLock(&pDevExt->SingleIrpSpinLock);

   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> SpinLocks: C:(0x%p, 0x%p) R:(0x%p, 0x%p) W:(0x%p, 0x%p) S:(0x%p, 0x%p) P:(0x%p, 0x%p)\n", myPortName,
        pDevExt->ControlSpinLock, &pDevExt->ControlSpinLock,
        pDevExt->ReadSpinLock,    &pDevExt->ReadSpinLock,
        pDevExt->WriteSpinLock,   &pDevExt->WriteSpinLock,
        pDevExt->SingleIrpSpinLock,   &pDevExt->SingleIrpSpinLock,
        gPortSpinLock, &gPortSpinLock)
   );

   KeInitializeEvent
   (
      &pDevExt->ForTimeoutEvent,
      SynchronizationEvent,
      TRUE
   );
   KeInitializeEvent
   (
      &pDevExt->InterruptPipeClosedEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->ReadThreadStartedEvent,
      SynchronizationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->L2ReadThreadStartedEvent,
      SynchronizationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->WriteThreadStartedEvent,
      SynchronizationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->DspThreadStartedEvent,
      SynchronizationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->IntThreadStartedEvent,
      SynchronizationEvent,
      FALSE
   );

   // interrupt pipe thread waits on these
   pDevExt->pInterruptPipeEvents[INT_COMPLETION_EVENT_INDEX] =
      &pDevExt->eInterruptCompletion;
   KeInitializeEvent(&pDevExt->eInterruptCompletion,NotificationEvent,FALSE);
   pDevExt->pInterruptPipeEvents[CANCEL_EVENT_INDEX] =
      &pDevExt->CancelInterruptPipeEvent;
   KeInitializeEvent
   (
      &pDevExt->CancelInterruptPipeEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pInterruptPipeEvents[INT_STOP_SERVICE_EVENT] =
      &pDevExt->InterruptStopServiceEvent;
   KeInitializeEvent
   (
      &pDevExt->InterruptStopServiceEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->InterruptStopServiceRspEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pInterruptPipeEvents[INT_RESUME_SERVICE_EVENT] =
      &pDevExt->InterruptResumeServiceEvent;
   KeInitializeEvent
   (
      &pDevExt->InterruptResumeServiceEvent,
      NotificationEvent,
      FALSE
   );

   pDevExt->pInterruptPipeEvents[INT_STOP_REG_ACCESS_EVENT_INDEX] =
      &pDevExt->InterruptStopRegAccessEvent;
   KeInitializeEvent
   (
      &pDevExt->InterruptStopRegAccessEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->InterruptStopRegAccessAckEvent,
      NotificationEvent,
      FALSE
   );

   pDevExt->pInterruptPipeEvents[INT_EMPTY_RD_QUEUE_EVENT_INDEX] =
      &pDevExt->InterruptEmptyRdQueueEvent;
   pDevExt->pInterruptPipeEvents[INT_EMPTY_WT_QUEUE_EVENT_INDEX] =
      &pDevExt->InterruptEmptyWtQueueEvent;
   pDevExt->pInterruptPipeEvents[INT_EMPTY_CTL_QUEUE_EVENT_INDEX] =
      &pDevExt->InterruptEmptyCtlQueueEvent;
   pDevExt->pInterruptPipeEvents[INT_EMPTY_SGL_QUEUE_EVENT_INDEX] =
      &pDevExt->InterruptEmptySglQueueEvent;
   pDevExt->pInterruptPipeEvents[INT_REG_IDLE_NOTIF_EVENT_INDEX] =
      &pDevExt->InterruptRegIdleEvent;

   KeInitializeEvent
   (
      &pDevExt->InterruptEmptyRdQueueEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->InterruptEmptyWtQueueEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->InterruptEmptyCtlQueueEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->InterruptEmptySglQueueEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->InterruptRegIdleEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->RegIdleAckEvent,
      NotificationEvent,
      FALSE
   );

   pDevExt->PMWmiRegistered   = FALSE;
   pDevExt->PowerSuspended    = FALSE;
   pDevExt->PrepareToPowerDown= FALSE;
   pDevExt->IdleTimerLaunched = FALSE;
   pDevExt->IoBusyMask        = 0;
   pDevExt->SelectiveSuspendIdleTime = 0;
   pDevExt->InServiceSelectiveSuspension = FALSE;
   QCPWR_GetWdmVersion(pDevExt);

   // write thread waits on these
   pDevExt->pWriteEvents[CANCEL_EVENT_INDEX] = &pDevExt->CancelWriteEvent;
   KeInitializeEvent(&pDevExt->CancelWriteEvent,NotificationEvent,FALSE);
   pDevExt->pWriteEvents[WRITE_COMPLETION_EVENT_INDEX] = &pDevExt->WriteCompletionEvent;
   KeInitializeEvent
   (
      &pDevExt->WriteCompletionEvent,
      NotificationEvent,
      FALSE
   );
   pDevExt->pWriteEvents[WRITE_CANCEL_CURRENT_EVENT_INDEX] = &pDevExt->CancelCurrentWriteEvent;
   KeInitializeEvent
   (
      &pDevExt->CancelCurrentWriteEvent,
      NotificationEvent,
      FALSE
   );

   pDevExt->pWriteEvents[KICK_WRITE_EVENT_INDEX] = &pDevExt->KickWriteEvent;
   KeInitializeEvent(&pDevExt->KickWriteEvent, NotificationEvent, FALSE);
   pDevExt->pWriteEvents[WRITE_PRE_TIMEOUT_EVENT_INDEX] = &pDevExt->WritePreTimeoutEvent;
   KeInitializeEvent(&pDevExt->WritePreTimeoutEvent, NotificationEvent, FALSE);
   pDevExt->pWriteEvents[WRITE_PURGE_EVENT_INDEX] = &pDevExt->WritePurgeEvent;
   KeInitializeEvent(&pDevExt->WritePurgeEvent, NotificationEvent, FALSE);

   pDevExt->pWriteEvents[WRITE_STOP_EVENT_INDEX] = &pDevExt->WriteStopEvent;
   KeInitializeEvent(&pDevExt->WriteStopEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->WriteStopAckEvent, NotificationEvent, FALSE);
   pDevExt->pWriteEvents[WRITE_RESUME_EVENT_INDEX] = &pDevExt->WriteResumeEvent;
   KeInitializeEvent(&pDevExt->WriteResumeEvent, NotificationEvent, FALSE);

   #ifdef QCUSB_FC
   pDevExt->pWriteEvents[WRITE_FLOW_ON_EVENT_INDEX] = &pDevExt->WriteFlowOnEvent;
   KeInitializeEvent(&pDevExt->WriteFlowOnEvent, NotificationEvent, FALSE);

   pDevExt->pWriteEvents[WRITE_TIMEOUT_COMPLETION_EVENT_INDEX] = &pDevExt->WriteTimeoutCompletionEvent;
   KeInitializeEvent(&pDevExt->WriteTimeoutCompletionEvent, NotificationEvent, FALSE);

   pDevExt->pWriteEvents[WRITE_FLOW_OFF_EVENT_INDEX] = &pDevExt->WriteFlowOffEvent;
   KeInitializeEvent(&pDevExt->WriteFlowOffEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->WriteFlowOffAckEvent, NotificationEvent, FALSE);
   #endif // QCUSB_FC

   // read thread waits on these
   pDevExt->pL2ReadEvents[CANCEL_EVENT_INDEX] = &pDevExt->CancelReadEvent;
   KeInitializeEvent(&pDevExt->CancelReadEvent,NotificationEvent,FALSE);

   pDevExt->pL1ReadEvents[L1_CANCEL_EVENT_INDEX] = &pDevExt->L1CancelReadEvent;
   KeInitializeEvent(&pDevExt->L1CancelReadEvent,NotificationEvent,FALSE);

   pDevExt->pL2ReadEvents[L2_READ_COMPLETION_EVENT_INDEX] = &pDevExt->L2ReadCompletionEvent;
   pDevExt->pL2ReadEvents[L2_READ_PURGE_EVENT_INDEX] = &pDevExt->L2ReadPurgeEvent;
   pDevExt->pL2ReadEvents[L2_READ_STOP_EVENT_INDEX] = &pDevExt->L2ReadStopEvent;
   pDevExt->pL2ReadEvents[L2_READ_RESUME_EVENT_INDEX] = &pDevExt->L2ReadResumeEvent;

   pDevExt->pL1ReadEvents[L1_READ_COMPLETION_EVENT_INDEX] = &pDevExt->L1ReadCompletionEvent;
   pDevExt->pL1ReadEvents[L1_READ_AVAILABLE_EVENT_INDEX] = &pDevExt->L1ReadAvailableEvent;
   pDevExt->pL1ReadEvents[L1_READ_PRE_TIMEOUT_EVENT_INDEX] = &pDevExt->L1ReadPreTimeoutEvent;
   pDevExt->pL1ReadEvents[L1_READ_PURGE_EVENT_INDEX] = &pDevExt->L1ReadPurgeEvent;

   KeInitializeEvent(&pDevExt->L2ReadCompletionEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L2ReadPurgeEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L2ReadStopEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L2ReadStopAckEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L2ReadResumeEvent, NotificationEvent, FALSE);

   KeInitializeEvent(&pDevExt->L1ReadCompletionEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L1ReadAvailableEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L1ReadPreTimeoutEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L1ReadPurgeEvent, NotificationEvent, FALSE);
   KeInitializeEvent(&pDevExt->L1ReadPurgeAckEvent, NotificationEvent, FALSE);

   pDevExt->pL2ReadEvents[L2_KICK_READ_EVENT_INDEX] = &pDevExt->L2KickReadEvent;
   KeInitializeEvent(&pDevExt->L2KickReadEvent, NotificationEvent, FALSE);

   pDevExt->pL1ReadEvents[L1_KICK_READ_EVENT_INDEX] = &pDevExt->L1KickReadEvent;
   KeInitializeEvent(&pDevExt->L1KickReadEvent, NotificationEvent, FALSE);

   pDevExt->pL1ReadEvents[L1_CANCEL_CURRENT_EVENT_INDEX] = &pDevExt->CancelCurrentReadEvent;
   KeInitializeEvent
   (
      &pDevExt->CancelCurrentReadEvent,
      NotificationEvent,
      FALSE
   );

   KeInitializeEvent
   (
      &pDevExt->L1ReadThreadClosedEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->L2ReadThreadClosedEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->ReadIrpPurgedEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->WriteThreadClosedEvent,
      NotificationEvent,
      FALSE
   );
   KeInitializeEvent
   (
      &pDevExt->ReadCompletionThrottleEvent,
      NotificationEvent,
      FALSE
   );

   // Initialize device extension
   InitializeListHead(&pDevExt->RdCompletionQueue);
   InitializeListHead(&pDevExt->WtCompletionQueue);
   InitializeListHead(&pDevExt->CtlCompletionQueue);
   InitializeListHead(&pDevExt->SglCompletionQueue);
   InitializeListHead(&pDevExt->DispatchQueue);
   InitializeListHead(&pDevExt->DispatchFreeQueue);
   InitializeListHead(&pDevExt->ReadFreeQueue);
   InitializeListHead(&pDevExt->WriteFreeQueue);

   #ifdef QCUSB_FC
   InitializeListHead(&pDevExt->MWTSentIrpQueue);
   InitializeListHead(&pDevExt->MWTSentIrpRecordPool);
   #endif // QCUSB_FC

   pDevExt->hInterruptThreadHandle = NULL;
   pDevExt->hL1ReadThreadHandle    = NULL;
   pDevExt->hL2ReadThreadHandle    = NULL;
   pDevExt->hWriteThreadHandle     = NULL;
   pDevExt->hTxLogFile             = NULL;
   pDevExt->hRxLogFile             = NULL;
   pDevExt->bL1InCancellation      = FALSE;

   pDevExt->bDspCancelStarted   = FALSE;
   pDevExt->bItCancelStarted    = FALSE;
   pDevExt->bRdCancelStarted    = FALSE;
   pDevExt->bWtCancelStarted    = FALSE;
   pDevExt->bRdThreadInCreation = FALSE;
   pDevExt->bL2ThreadInCreation = FALSE;
   pDevExt->bWtThreadInCreation = FALSE;
   pDevExt->bIntThreadInCreation = FALSE;
   pDevExt->bDspThreadInCreation = FALSE;

   KeClearEvent(&pDevExt->L1ReadThreadClosedEvent);
   KeClearEvent(&pDevExt->L2ReadThreadClosedEvent);
   KeClearEvent(&pDevExt->ReadIrpPurgedEvent);
   KeClearEvent(&pDevExt->L1ReadPurgeAckEvent);
   KeClearEvent(&pDevExt->WriteThreadClosedEvent);
   KeClearEvent(&pDevExt->InterruptPipeClosedEvent);
   KeClearEvent(&pDevExt->InterruptStopServiceEvent);
   KeClearEvent(&pDevExt->InterruptStopServiceRspEvent);
   KeClearEvent(&pDevExt->InterruptResumeServiceEvent);
   KeClearEvent(&pDevExt->CancelReadEvent);
   KeClearEvent(&pDevExt->L1CancelReadEvent);
   KeClearEvent(&pDevExt->CancelWriteEvent);
   // KeClearEvent(&pDevExt->CancelInterruptPipeEvent);

   initDevState(DEVICE_STATE_ZERO);
   pDevExt->bDeviceRemoved = FALSE;
   pDevExt->bDeviceSurpriseRemoved = FALSE;
   pDevExt->pRemoveLock = &pDevExt->RemoveLock;
   IoInitializeRemoveLock(pDevExt->pRemoveLock, 0, 0, 0);

   pDevExt->CompletionThrottle = QCSER_RD_COMPLETION_THROTTLE_MIN;

   deviceObject->Flags |= DO_POWER_PAGABLE;
   deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

   KeInitializeTimer(&pDevExt->IdleTimer);
   KeInitializeDpc(&pDevExt->IdleDpc, QCPWR_IdleDpcRoutine, pDevExt);

   pDevExt->SetCommFeatureSupported = FALSE;

QCPTDO_CreateNewPTDO_Return:
   if (pucNewReadBuffer)
   {
      ExFreePool(pucNewReadBuffer);
      pucNewReadBuffer = NULL;
   }
   if (pL2ReadBuffer0)
   {
      ExFreePool(pL2ReadBuffer0);
      pL2ReadBuffer0 = NULL;
   }

   _freeString(ucUnprotectedLink);
   _freeString(ucDeviceMapEntry);
   _freeString(ucPortName);
   _freeString(ucDeviceNumber);
   _freeString(ucDeviceNameBase);
   _freeString(ucDeviceName);

   if (!NT_SUCCESS(ntStatus))
   {
      if (pucByteStuffingBuffer)
      {
         ExFreePool(pucByteStuffingBuffer);
      }
   }
   else
   {
      ntStatus = QCSER_PostVendorRegistryProcess
                 (
                    pDriverObject,
                    PhysicalDeviceObject,
                    deviceObject
                 );
   }

   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> QCPTDO_CreateNewPTDO: exit 0x%x\n", myPortName, ntStatus)
   );
   return deviceObject;
}  // QCPTDO_CreateNewPTDO

// Create Port DO and initialize device extension
// Part of the device extension is initialized after
// FDO is created later.
PDEVICE_OBJECT QCPTDO_Create
(
   IN PDRIVER_OBJECT pDriverObject,
   IN PDEVICE_OBJECT pdo
)
{
   int i, j, k;
   char myPortName[16];
   PDEVICE_OBJECT        myPortDO  = NULL, fdo = NULL;
   PDEVICE_EXTENSION     portDoExt = NULL;
   PFDO_DEVICE_EXTENSION pDevExt   = NULL;
   HANDLE                hRegKey   = NULL;
   UNICODE_STRING ucValueName; // buffer is never allocated for this string
   NTSTATUS ntStatus = STATUS_SUCCESS;
   UNICODE_STRING ucPortName;        // "COMn(n)"
   PQCSER_PortDeviceListType portDoList;
   KIRQL OldRdIrqLevel;
   static BOOLEAN bPortLockInitialied = FALSE;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   // we could move the init code to DriverEntry and save the
   // CancelSpinLock. Since this routine is not frequently
   // called, the use of CancelSpinLock here should be OK.
   IoAcquireCancelSpinLock(&OldRdIrqLevel);
   if (bPortLockInitialied == FALSE)
   {
      bPortLockInitialied = TRUE;
      KeInitializeSpinLock(&gPortSpinLock);
   }
   IoReleaseCancelSpinLock(OldRdIrqLevel);

   // Get Port Name from registry and see if the port Do is there
   ntStatus = IoOpenDeviceRegistryKey
              (
                 pdo,
                 PLUGPLAY_REGKEY_DEVICE,
                 KEY_READ,
                 &hRegKey
              );
   if (!NT_SUCCESS(ntStatus))
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> QCPTDO_Create: ERR - reg 0x%x\n", gDeviceName, ntStatus)
      );
      goto QCPTDO_Create_Return;
   }
   ucHdlCnt++;

   RtlInitUnicodeString(&ucValueName, DEVICE_PORTNAME_LABEL);  // "PortName"
   _zeroUnicode(ucPortName);
   ntStatus = getRegValueEntryData
   (
      hRegKey,
      ucValueName.Buffer,
      &ucPortName
   );  // COMn(n)
   _closeRegKeyG( gDeviceName, hRegKey, "PNP-0" );
   if (!NT_SUCCESS(ntStatus))
   {
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> QCPTDO_Create: ERR - no PORT 0x%x\n", gDeviceName, ntStatus)
      );
      goto QCPTDO_Create_Return;
   }

   // extract port name into an ANSI string
   for (i = 0; i < ucPortName.Length/2; i++)
   {
      myPortName[i] = (char)(ucPortName.Buffer[i]);
      if (i == 14)
      {
         i++;
         break;
      }
   }
   myPortName[i] = 0;
   QCPTDO_StorePortName(myPortName);  // for fun

   myPortDO = QCPTDO_FindPortDOByPort(&ucPortName);
   if (myPortDO != NULL)
   {
      portDoExt = (PDEVICE_EXTENSION)myPortDO->DeviceExtension;

      if ((inDevStateX(portDoExt, DEVICE_STATE_PRESENT)) ||
          (inDevStateX(portDoExt, DEVICE_STATE_PRESENT_AND_STARTED)))
      {
         QCSER_DbgPrintG
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_FORCE,
            ("<%s> QCPTDO_Create: critical port conflict for 0x%p\n", myPortName, myPortDO)
         );
         ntStatus = STATUS_UNSUCCESSFUL;
         myPortDO = NULL;
         goto QCPTDO_Create_Return;
      }

      // PTDO exists and available, no init necessary
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> QCPTDO_Create: to reuse PTDO 0x%p\n", myPortName, myPortDO)
      );
      portDoExt->bFdoReused = TRUE;
      portDoExt->PhysicalDeviceObject = pdo;

      // Re-initialize anything necessary here
 
      // These need to be re-initialized because the events might be
      // set but not cleared during the previous surprise removal
      KeClearEvent(&portDoExt->L2ReadStopEvent);
      KeClearEvent(&portDoExt->WriteStopEvent);
      KeClearEvent(&portDoExt->CancelReadEvent);
      KeClearEvent(&portDoExt->CancelWriteEvent);
      KeClearEvent(&portDoExt->L1CancelReadEvent);
      KeClearEvent(&portDoExt->CancelInterruptPipeEvent);
      KeClearEvent(&portDoExt->InterruptStopServiceEvent);
      KeClearEvent(&portDoExt->InterruptStopRegAccessEvent);

      ntStatus = STATUS_SUCCESS;
   }
   else
   {
      // create new PTDO and establish symbolic links
      myPortDO = QCPTDO_CreateNewPTDO(pDriverObject, pdo, &ucPortName, myPortName);
      QCSER_DbgPrintG
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> QCPTDO_Create: got PTDO 0x%p\n", myPortName, myPortDO)
      );
      if (myPortDO == NULL)
      {
         ntStatus = STATUS_UNSUCCESSFUL;
      }
      else
      {
         // link it to the Port DO list
         QcAcquireSpinLock(&gPortSpinLock, &levelOrHandle);
         portDoList = gPortDOList;
         if (gPortDOList == NULL)
         {
            gPortDOList = _ExAllocatePool
                          (
                             NonPagedPool,
                             sizeof(QCSER_PortDeviceListType),
                             ("QCSER_PortDeviceList")
                          );
            if (gPortDOList == NULL)
            {
               QCSER_DbgPrintG
               (
                  QCSER_DBG_MASK_CONTROL,
                  QCSER_DBG_LEVEL_CRITICAL,
                  ("<%s> NO_MEM for PTDO: 0x%p\n", myPortName)
               );
               QcReleaseSpinLock(&gPortSpinLock, levelOrHandle);
               RemoveSymbolicLinks(myPortDO);
               QCSER_CleanupDeviceExtensionBuffers(myPortDO);
               IoDeleteDevice(myPortDO);
               myPortDO = NULL;
               goto QCPTDO_Create_Return;
            }
            portDoList = gPortDOList;
         }
         else
         {
            while (portDoList->Next != NULL)
            {
               portDoList = portDoList->Next;
            }
            portDoList->Next = _ExAllocatePool
                               (
                                  NonPagedPool,
                                  sizeof(QCSER_PortDeviceListType),
                                  ("QCSER_PortDeviceList")
                               );
            if (portDoList->Next == NULL)
            {
               QCSER_DbgPrintG
               (
                  QCSER_DBG_MASK_CONTROL,
                  QCSER_DBG_LEVEL_CRITICAL,
                  ("<%s> NO_MEM for PTDO 1: 0x%p\n", myPortName)
               );
               QcReleaseSpinLock(&gPortSpinLock, levelOrHandle);
               RemoveSymbolicLinks(myPortDO);
               QCSER_CleanupDeviceExtensionBuffers(myPortDO);
               IoDeleteDevice(myPortDO);
               myPortDO = NULL;
               goto QCPTDO_Create_Return;
            }
            portDoList = portDoList->Next;
         }

         portDoList->PortDO = myPortDO;
         portDoList->Next = NULL;
         gPortDOCount++;
         QcReleaseSpinLock(&gPortSpinLock, levelOrHandle);

         // Get mgr id for port DO
         portDoExt = (PDEVICE_EXTENSION)myPortDO->DeviceExtension;
         portDoExt->MgrId = QCMGR_RequestManagerElement();
         if (portDoExt->MgrId < 0)
         {
            QCSER_DbgPrintG
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_CRITICAL,
               ("<%s> PTDO: no mgr id\n", myPortName)
            );
            RemoveSymbolicLinks(myPortDO);
            QCSER_CleanupDeviceExtensionBuffers(myPortDO);
            IoDeleteDevice(myPortDO);
            myPortDO = NULL;
            goto QCPTDO_Create_Return;
         }

      }
   }

QCPTDO_Create_Return:

   _freeString(ucPortName);

   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> QCPTDO_Create: Return DO 0x%p ST 0x%x\n", myPortName, myPortDO, ntStatus)
   );

   return myPortDO;
}  // QCPTDO_Create

// perform much of the function of DeleteDevice
BOOLEAN QCPTDO_RemovePort(PDEVICE_OBJECT PortDO, PIRP Irp)
{
   PQCSER_PortDeviceListType portDoList, portDoList0;
   PDEVICE_EXTENSION pDevExt = PortDO->DeviceExtension;
   char myPortName[16];
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   strcpy(myPortName, pDevExt->PortName);

   QcAcquireSpinLock(&gPortSpinLock, &levelOrHandle);
   portDoList = portDoList0 = gPortDOList;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> QCPTDO_RemovePort: start 0x%p::0x%p\n", myPortName, PortDO, portDoList)
   );

   if (portDoList != NULL)
   {
      if (portDoList->PortDO == PortDO)
      {
         gPortDOList = portDoList->Next;
      }
      else
      {
         portDoList = portDoList->Next;
         while (portDoList != NULL)
         {
            if (portDoList->PortDO == PortDO)
            {
               portDoList0->Next = portDoList->Next;
               break;
            }
            portDoList0 = portDoList;
            portDoList  = portDoList->Next;
         }
      }
      if (portDoList != NULL)
      {
         ExFreePool(portDoList);
         gPortDOCount--;
      }
   }
   else // wasn't found
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> QCPTDO_RemovePort: no PTDO on list 0x%p\n", myPortName, PortDO)
      );
   }
   QcReleaseSpinLock(&gPortSpinLock, levelOrHandle);

   // perform cleanup on portDoList->PortDO (including extension)
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> QCPTDO_RemovePort: cleaning up PTDO 0x%p\n", myPortName, PortDO)
   );

   // Signal manager thread to close the dispatch thread right after we return
   QCMGR_SetTerminationRequest(pDevExt->MgrId);

   /*********** Handle RemoveLock ***********/
   if ((pDevExt->bInService == FALSE) &&
       (pDevExt->FDO == NULL)         &&
       (pDevExt->FdoChain == NULL)    && // FDO removed from chain before this
       (gPortDOList == NULL))
   {
      QcAcquireSpinLock(&gPnpSpinLock, &levelOrHandle);
      if (gPnpState == QC_PNP_IDLE)
      {
         gPnpState = QC_PNP_REMOVING;
         QcReleaseSpinLock(&gPnpSpinLock, levelOrHandle);

         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> Bf IoReleaseRemoveLockAndWait (%ld,%ld,%ld,%ld,%ld) CRW <%ld/%ld, %ld/%ld, %ld/%ld>\n",
              pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
              pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3], pDevExt->Sts.lRmlCount[4],
              pDevExt->Sts.lAllocatedCtls, pDevExt->Sts.lAllocatedDSPs,
              pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedRdMem,
              pDevExt->Sts.lAllocatedWrites, pDevExt->Sts.lAllocatedWtMem)
         );
         IoReleaseRemoveLockAndWait(pDevExt->pRemoveLock, Irp);
         InterlockedDecrement(&(pDevExt->Sts.lRmlCount[0]));
      }
      else
      {
         QcReleaseSpinLock(&gPnpSpinLock, levelOrHandle);
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> Bf QcIoReleaseRemoveLock (%ld,%ld,%ld,%ld,%ld) CRW <%ld/%ld, %ld/%ld, %ld/%ld>\n",
              pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
              pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3], pDevExt->Sts.lRmlCount[4],
              pDevExt->Sts.lAllocatedCtls, pDevExt->Sts.lAllocatedDSPs,
              pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedRdMem,
              pDevExt->Sts.lAllocatedWrites, pDevExt->Sts.lAllocatedWtMem)
         );
         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
      }
   }
   else
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> Bf QcIoReleaseRemoveLock (%ld,%ld,%ld,%ld) CRW <%ld/%ld, %ld/%ld, %ld/%ld>\n",
           pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
           pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3],
           pDevExt->Sts.lAllocatedCtls, pDevExt->Sts.lAllocatedDSPs,
           pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedRdMem,
           pDevExt->Sts.lAllocatedWrites, pDevExt->Sts.lAllocatedWtMem)
      );
      QcIoReleaseRemoveLock(pDevExt->pRemoveLock, Irp, 0);
   }
   /*********** End of RemoveLock ***********/

   QcReleaseEntryPass(&gSyncEntryEvent, myPortName, "OUT");

   CancelInterruptThread(pDevExt, 9);
   QCDSP_PurgeDispatchQueue(pDevExt);
   QCSER_CleanupDeviceExtensionBuffers(PortDO);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_CRITICAL,
      ("<%s> Terminated (%ld,%ld,%ld,%ld) CRW <%ld/%ld, %ld/%ld, %ld/%ld>\n",
        pDevExt->PortName, pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lRmlCount[1],
        pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lRmlCount[3],
        pDevExt->Sts.lAllocatedCtls, pDevExt->Sts.lAllocatedDSPs,
        pDevExt->Sts.lAllocatedReads, pDevExt->Sts.lAllocatedRdMem,
        pDevExt->Sts.lAllocatedWrites, pDevExt->Sts.lAllocatedWtMem)
   );
   DbgPrint("   --------------------------------\n");
   DbgPrint("     Device(%d) Removed by Driver\n", pDevExt->ucDeviceType);
   DbgPrint("       Version: %-10s         \n", QCUSB_DRIVER_VERSION);
   DbgPrint("       Port:    %-50s\n", pDevExt->PortName);
   DbgPrint("   --------------------------------\n");

   IoDeleteDevice(PortDO);
   QCSER_DbgPrintG
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> QCPTDO_RemovePort: removed PTDO 0x%p\n", myPortName, PortDO)
   );

   return TRUE;
}  // QCPTDO_RemovePort

// The longest port name is COMnn
// This function is intentionally written awkwardly for debugging purpose
BOOLEAN QCPTDO_ComparePortName(PUNICODE_STRING pus1, PUNICODE_STRING pus2)
{
   char port1[8], port2[8];
   int i;
   BOOLEAN result = TRUE;

   if (pus1->Length != pus2->Length)
   {
      return FALSE;
   }

   for (i = 0; i < pus1->Length/2; i++)
   {
      if (i > 7)
      {
         result = FALSE;
         break;
      }
      port1[i] = (char)pus1->Buffer[i];
      port2[i] = (char)pus2->Buffer[i];
      // if (toupper(port1[i] != toupper(port2[i])))
      if (port1[i] != port2[i])
      {
         result = FALSE;
         break;
      }
   }

   return result;
}  // QCPTDO_ComparePortName

VOID QCPTDO_StorePortName(char *myPortName)
{
   char portNumber[8], *p;

   int i, len;

   if (strlen(myPortName) < 4)
   {
      return;
   }
   else
   {
      for (i = 3; i < strlen(myPortName); i++)
      {
         portNumber[i-3] = myPortName[i];
         if (i == 5)
         {
            i++;
            break;
         }
      }
      portNumber[i-3] = 0;
   }

   len = strlen(gVendorConfig.PortName);

   if (strstr(gVendorConfig.PortName, portNumber) != NULL)
   {
      return;
   }
   else if (len == 0)
   {
      sprintf(gVendorConfig.PortName, "COM%s", portNumber);
   }
   else if (len < 220)
   {
      sprintf(gVendorConfig.PortName+len, ",%s", portNumber);
   }

}  // QCPTDO_StorePortName
