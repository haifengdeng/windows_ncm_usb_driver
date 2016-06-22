/*===========================================================================
FILE: QCSER.c

DESCRIPTION:
   This file implementations of ioctls for USB support of serial devices

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003-2007 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

/*
 * title:      QCSER.c
 *
 * purpose:    ioctls for WDM driver for USB support of serial devices
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
 *
 * $History: Sysfserioctl.c $
 * 
 * *****************  Version 1  *****************
 * User: Edk          Date: 3/27/98    Time: 10:20a
 * Created in $/Host/modem/USB WDM Driver/Generic
 *
 ************************************************************************/                            



#define MODEM_CONTEXT_BUFSIZE 16

#include "QCMAIN.h"
#include "QCWT.h"
#include "QCRD.h"
#include "QCUTILS.h"
#include "QCSER.h"
#include "QCINT.h"
#include "QCPWR.h"
#include "QCDEV.h"

NTSTATUS QCSER_Open(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock)
{
   LONG lResult, i;
   NTSTATUS ntStatus;
   PDEVICE_OBJECT pDeviceObject;
   PDEVICE_EXTENSION pDevExt;

   pDeviceObject = pIOBlock->pSerialDeviceObject;
   pDevExt = pDeviceObject->DeviceExtension;

   if (inDevState(DEVICE_STATE_DEVICE_QUERY_REMOVE))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> _Open: dev being removed\n", pDevExt->PortName)
      );
      return STATUS_DELETE_PENDING;
   }

   if (pDevExt->bInService == TRUE)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> _Open: STATUS_DEVICE_BUSY\n", pDevExt->PortName)
      );
      // QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock->pCallingIrp, 0);
      return STATUS_DEVICE_BUSY;
   }

   if (pDevExt->bFdoReused == FALSE)
   {
      InterlockedExchange(&(pDevExt->lPurgeBegin), 0);
      setDevState((DEVICE_STATE_CLIENT_PRESENT | DEVICE_STATE_WOM_FIRST_TIME));
   }
   else
   {
      setDevState(DEVICE_STATE_CLIENT_PRESENT);
   }

   if (pDevExt->PowerSuspended == FALSE)
   {
      ResumeInterruptService(pDevExt, 3);
      ntStatus = QCUSB_ResetInput(pDeviceObject, QCUSB_RESET_PIPE_AND_ENDPOINT);
      if (ntStatus == STATUS_SUCCESS)
      {
         ntStatus = QCUSB_ResetOutput(pDeviceObject, QCUSB_RESET_PIPE_AND_ENDPOINT);
      }

      if (ntStatus != STATUS_SUCCESS)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> _Open: ResetInput err 0x%x\n", pDevExt->PortName, ntStatus)
         );
         clearDevState(DEVICE_STATE_DEVICE_STARTED);
         // QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIOBlock->pCallingIrp, 0);
         // return STATUS_UNSUCCESSFUL;
      }
   }

   pDevExt->bInService = TRUE;
   pDevExt->bWOMHeldForRead = FALSE;

   if (pDevExt->InServiceSelectiveSuspension == FALSE)
   {
      QCPWR_CancelIdleTimer(pDevExt, 0, TRUE, 2);
   }

   // Start the read/write threads
   QCSER_StartDataThreads(pDevExt);

   return STATUS_SUCCESS;
} // QCSER_Open

NTSTATUS QCSER_Close(PVXD_WDM_IO_CONTROL_BLOCK pIOBlock)
{
   PDEVICE_OBJECT pDO;
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   BOOLEAN  bClosed = FALSE;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   pDO = pIOBlock->pSerialDeviceObject;
   pDevExt = pDO->DeviceExtension;

   if (pDevExt->Sts.lRmlCount[4] == 0)
   {
      bClosed = TRUE;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> _Close: ERROR 0\n", pDevExt->PortName)
      );
   }

   if (pDevExt->bInService == FALSE) // device not CREATED
   {
      bClosed = TRUE;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> _Close: Already out of service\n", pDevExt->PortName)
      );
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> _Close: Clear DTR/RTS at port close\n", pDevExt->PortName)
   );
   ntStatus = SerialClrDtrRts(pDevExt->MyDeviceObject, FALSE);
   QCUSB_CDC_SetInterfaceIdle
   (
      pDevExt->MyDeviceObject,
      pDevExt->DataInterface,
      TRUE,
      2
   );

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> _Close: CancelReadThread\n", pDevExt->PortName)
   );
   ntStatus = CancelReadThread(pDevExt, 0);
   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> _Close: CancelWriteThread\n", pDevExt->PortName)
   );
   ntStatus = CancelWriteThread(pDevExt, 2);

   StopInterruptService(pDevExt, FALSE, 4);

   // then check if this close has come after a device removal

   clearDevState( DEVICE_STATE_CLIENT_PRESENT );

   pDevExt->bInService = FALSE; // show device closed

   ntStatus = CancelWOMIrp(pDO);
   ntStatus = CancelNotificationIrp(pDO);
   QCUTILS_CleanupReadWriteQueues(pDevExt);

   pDevExt->RXHolding = 0;
   pDevExt->TXHolding = 0;

   if ((inDevState(DEVICE_STATE_DEVICE_REMOVED0 | DEVICE_STATE_SURPRISE_REMOVED)) ||
       (pDevExt->bDeviceRemoved == TRUE))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> _CLose: remove port from reg.\n", pDevExt->PortName)
      );
      if (pDevExt->ucsDeviceMapEntry.Buffer != NULL)
      {
         RtlDeleteRegistryValue
         (
            RTL_REGISTRY_DEVICEMAP,
            L"SERIALCOMM",
            pDevExt->ucsDeviceMapEntry.Buffer
         );
         // if ((pDevExt->ucsUnprotectedLink).Buffer != NULL)
         // {
         //    IoDeleteSymbolicLink(&pDevExt->ucsUnprotectedLink);
         // }
      }
   }

   #ifdef QCSER_ENABLE_LOG_REC
   if (pDevExt->LogLatestPkts == TRUE)
   {
      QCSER_OutputLatestLogRecords(pDevExt);
   }
   #endif // QCSER_ENABLE_LOG_REC

   // de-ref the stack-top-DO
   QcAcquireSpinLock(&pDevExt->ControlSpinLock, &levelOrHandle);
   if (pDevExt->StackTopDO != NULL)
   {
      ObDereferenceObject(pDevExt->StackTopDO);
      pDevExt->StackTopDO = NULL;
   }
   QcReleaseSpinLock(&pDevExt->ControlSpinLock, levelOrHandle);

   if (pDevExt->NumIrpsToComplete != 0)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> _Close: NumIrpsToComplete err %d\n", pDevExt->PortName,
           pDevExt->NumIrpsToComplete)
      );
      pDevExt->NumIrpsToComplete = 0;
   }

   // Finally we register idle notification
   if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      QCPWR_SetIdleTimer(pDevExt, 0, FALSE, 7);
   }

   return STATUS_SUCCESS;
}  // QCSER_Close

void RemovePurgedReads(PDEVICE_OBJECT DeviceObject)
{
   PDEVICE_EXTENSION pDevExt;
   PVXD_WDM_IO_CONTROL_BLOCK pReadQueEntry, pNextBlock;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   pDevExt = DeviceObject -> DeviceExtension;

   QcAcquireSpinLockWithLevel(&pDevExt->ReadSpinLock, &levelOrHandle, irql);

   pReadQueEntry = pDevExt->pReadHead;
   while (pReadQueEntry)
   {
      if ((pReadQueEntry == pDevExt->pReadHead) &&
          (pReadQueEntry->bPurged == TRUE))
      {
         pDevExt->pReadHead = pReadQueEntry->pNextEntry;
         pNextBlock = pReadQueEntry->pNextEntry;
         pReadQueEntry->ntStatus = STATUS_CANCELLED;

         pReadQueEntry->pCompletionRoutine(pReadQueEntry, FALSE, 6);
         QCSER_DbgPrint2
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> SER free IOB 11 0x%p\n", pDevExt->PortName, pReadQueEntry)
         );
         QcExFreeReadIOB(pReadQueEntry, FALSE);

         pReadQueEntry = pNextBlock;
      }
      else
      {
         break;
      }
   }

   while (pReadQueEntry)
   {
      pNextBlock = pReadQueEntry->pNextEntry;
      if (pNextBlock)
      {
         if (pNextBlock->bPurged == TRUE)
         {
            pReadQueEntry->pNextEntry = pNextBlock->pNextEntry;
            pNextBlock->ntStatus = STATUS_CANCELLED;
            pNextBlock->pCompletionRoutine(pNextBlock, FALSE, 14);
            QCSER_DbgPrint2
            (
               QCSER_DBG_MASK_READ,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> SER free IOB 12 0x%p\n", pDevExt->PortName, pNextBlock)
            );
            QcExFreeReadIOB(pNextBlock, FALSE);
         }
         else
         {
            pReadQueEntry = pNextBlock;
         }
      }
      else
      {
         break;
      }
   }
   QcReleaseSpinLockWithLevel(&pDevExt->ReadSpinLock, levelOrHandle, irql);
}


NTSTATUS SerialGetStats( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )
{
   NTSTATUS nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt;
   PIO_STACK_LOCATION irpStack;

   /*   -- Reference --
   typedef struct _SERIALPERF_STATS {
      ULONG ReceivedCount;
      ULONG TransmittedCount;
      ULONG FrameErrorCount;
      ULONG SerialOverrunErrorCount;
      ULONG BufferOverrunErrorCount;
      ULONG ParityErrorCount;
   } SERIALPERF_STATS, *PSERIALPERF_STATS;
        -- End --    */
 
   pDevExt = DeviceObject -> DeviceExtension;
   irpStack = IoGetCurrentIrpStackLocation( pIrp );
   if (irpStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(SERIALPERF_STATS))
   {
      pIrp -> IoStatus.Information = 0;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> GET_STATS: STATUS_BUFFER_TOO_SMALL\n", pDevExt->PortName)
      );
      return STATUS_BUFFER_TOO_SMALL;
   }
 
   if(!ioBuffer)
   {
      pIrp -> IoStatus.Information = 0;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> GET_STATS: STATUS_INVALID_PARAMETER\n", pDevExt->PortName)
      );
      return STATUS_INVALID_PARAMETER;
   }
   RtlCopyMemory( ioBuffer, pDevExt -> pPerfstats, sizeof( SERIALPERF_STATS ) );
   pIrp -> IoStatus.Information = sizeof( SERIALPERF_STATS );

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> Stats: RvCnt(%ld) XmitCnt(%ld) FrErCnt(%ld) SerOvRunErr(%ld) BufOvrunErr(%ld) ParityErr(%ld)\n",
         pDevExt->PortName,
         pDevExt->pPerfstats->ReceivedCount,
         pDevExt->pPerfstats->TransmittedCount,
         pDevExt->pPerfstats->FrameErrorCount,
         pDevExt->pPerfstats->SerialOverrunErrorCount,
         pDevExt->pPerfstats->BufferOverrunErrorCount,
         pDevExt->pPerfstats->ParityErrorCount
      )
   );
   return nts;
}

NTSTATUS SerialClearStats( PDEVICE_OBJECT DeviceObject )

{
   NTSTATUS nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt;
 
   pDevExt = DeviceObject->DeviceExtension;
   RtlZeroMemory(pDevExt->pPerfstats, sizeof(SERIALPERF_STATS));
   return nts;
}

NTSTATUS SerialGetProperties(PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp)
{
   NTSTATUS nts = STATUS_SUCCESS;
   PIO_STACK_LOCATION irpStack;
   PDEVICE_EXTENSION pDevExt = DeviceObject -> DeviceExtension;
   PSERIAL_COMMPROP pCP = pDevExt->pCommProp;

   irpStack = IoGetCurrentIrpStackLocation( pIrp ); 
   if (irpStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof( SERIAL_COMMPROP ))
   {
       pIrp -> IoStatus.Information = 0;
       return STATUS_BUFFER_TOO_SMALL;
   }
   if (!ioBuffer)
   {
      pIrp -> IoStatus.Information = 0;
      return STATUS_INVALID_PARAMETER;
   }

   pCP->PacketLength = sizeof(SERIAL_COMMPROP);
   pCP->PacketVersion = 2;
   pCP->MaxRxQueue = pDevExt->lReadBufferSize;

   pCP->ServiceMask = SERIAL_SP_SERIALCOMM;
   pCP->MaxBaud = SERIAL_BAUD_USER;
   if (pDevExt->ucDeviceType < DEVICETYPE_INVALID)
   {
      pCP->ProvSubType = SERIAL_SP_RS232; // SERIAL_SP_SERIALCOMM;
   }
   else
   {
      pCP->ProvSubType = SERIAL_SP_UNSPECIFIED;
   }

   pCP->ProvCapabilities = SERIAL_PCF_DTRDSR       |
                           SERIAL_PCF_RTSCTS       |
                           SERIAL_PCF_CD           |
                           SERIAL_PCF_PARITY_CHECK |
                       //  SERIAL_PCF_SETXCHAR     |
                           SERIAL_PCF_INTTIMEOUTS  |
                           SERIAL_PCF_TOTALTIMEOUTS;

   pCP->SettableParams = SERIAL_SP_BAUD         |
                         SERIAL_SP_PARITY       |
                         SERIAL_SP_DATABITS     |
                         SERIAL_SP_STOPBITS     |
                         SERIAL_SP_PARITY_CHECK |
                         SERIAL_SP_HANDSHAKING  |
                         SERIAL_SP_CARRIER_DETECT;

   pCP->SettableBaud = SERIAL_BAUD_USER + (SERIAL_BAUD_57600 - 1); // i.e., all of them
   pCP->SettableData = SERIAL_DATABITS_5 |
                       SERIAL_DATABITS_6 |
                       SERIAL_DATABITS_7 |
                       SERIAL_DATABITS_8;

   pCP->SettableStopParity = SERIAL_STOPBITS_10 |
                             SERIAL_STOPBITS_15 |
                             SERIAL_STOPBITS_20 |
                             SERIAL_PARITY_NONE |
                             SERIAL_PARITY_ODD  |
                             SERIAL_PARITY_EVEN |
                             SERIAL_PARITY_MARK |
                             SERIAL_PARITY_SPACE;

   pCP->CurrentTxQueue = 0;
   pCP->CurrentRxQueue = pDevExt->lReadBufferSize;

   RtlCopyMemory( ioBuffer, pCP, sizeof( SERIAL_COMMPROP ) );
   pIrp -> IoStatus.Information = sizeof( SERIAL_COMMPROP );
   return nts;
}

ULONG InitUartStateFromModem(PDEVICE_OBJECT DeviceObject, BOOLEAN bHoldWOMIrp)
{
   NTSTATUS nts = STATUS_SUCCESS;
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   char cBuf[sizeof(*pRequest) + sizeof(ULONG) + 1];
   USHORT usNewUartState, usOldUartState;
   ULONG ulRetsize;
   PDEVICE_EXTENSION  pDevExt = DeviceObject -> DeviceExtension;

   pRequest = (PUSB_DEFAULT_PIPE_REQUEST)cBuf;
   pRequest -> wValue = 0;
   pRequest -> wIndex = 0;
   pRequest -> wLength = UART_STATE_SIZE;

   if (pDevExt->ucDeviceType < DEVICETYPE_INVALID)
   {  // LIMIT:  CDC simulates GET_MODEMSTATUS from stored notification data

      USHORT usDummy = pDevExt->usCurrUartState & US_BITS_MODEM_RAW;
      // USHORT usDummy = pDevExt->usCurrUartState & 0xF0;
      // LIMIT:  as with legacy code, GET_MODEMSTATUS does not pick up DSR bit
      RtlCopyMemory
      (
         &pRequest->Data[0],
         &usDummy,
         UART_STATE_SIZE
      );
      nts = STATUS_SUCCESS;
   }
   else
   {
       nts = STATUS_UNSUCCESSFUL;
   }

   if (NT_SUCCESS( nts ))
   {
      // usNewUartState = modem bits from UART
      usNewUartState = US_BITS_MODEM_RAW & (*(PUSHORT)(&(pRequest->Data[0])));
      // usOldUartState = non-UART bits already set
      usOldUartState = (~US_BITS_MODEM) & (pDevExt->usCurrUartState);
      // usNewUartState = all bits now set
      usNewUartState |= usOldUartState;
      // usCurrUartState (before change) cleared, so all active bits generate events
      pDevExt->usCurrUartState = 0;

      clearDevState(DEVICE_STATE_WOM_FIRST_TIME);
      // ProcessNewUartState(,,,TRUE) won't complete any pending IRP
      ulRetsize = ProcessNewUartState(pDevExt, usNewUartState, 0xffff, bHoldWOMIrp);
   }
   else
   {
      ulRetsize = 0;
   }
   return ulRetsize;
}

NTSTATUS SerialGetModemStatus( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )

{
   NTSTATUS nts = STATUS_SUCCESS;
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   ULONG ulRetsize, ulBufsize;
   PIO_STACK_LOCATION irpStack;
   USHORT usModemStatus;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   char cBuf[16];            // need do someting here

   irpStack = IoGetCurrentIrpStackLocation( pIrp ); 
   ulBufsize = irpStack -> Parameters.DeviceIoControl.OutputBufferLength;
   if (ulBufsize < sizeof( long ))
   {
      pIrp -> IoStatus.Information = 0;
      return STATUS_INVALID_PARAMETER;
   }
   if(!ioBuffer) 
   {
      pIrp -> IoStatus.Information = 0;
      return STATUS_INVALID_PARAMETER;
   }

   if_DevState( DEVICE_STATE_WOM_FIRST_TIME )
   {
      // doesn't hold any pending IRP
      InitUartStateFromModem(DeviceObject, FALSE); // clears WOM_STATE_FIRST_TIME
   }

   pIrp -> IoStatus.Information = sizeof(ULONG);

   if (inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      pDevExt->ModemStatusReg &= ~(SERIAL_MSR_DCTS|SERIAL_MSR_DDSR|SERIAL_MSR_TERI|SERIAL_MSR_DDCD);
      ulRetsize = (ULONG) pDevExt->ModemStatusReg; // bugbug CDC doesn't ever report delta bits
   }
   else
   {
      ulRetsize = 0;
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> MdmStatus 0x%x\n", pDevExt->PortName, ulRetsize)
   );
   RtlCopyMemory( ioBuffer, &ulRetsize, sizeof(ULONG) );

   return nts;
}

NTSTATUS SerialGetCommStatus( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )
{
   NTSTATUS nts = STATUS_SUCCESS;
   PIO_STACK_LOCATION irpStack;
   PDEVICE_EXTENSION pDevExt;
   PSERIAL_STATUS pSs;
   SERIAL_STATUS psudoStatus;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();
 
   irpStack = IoGetCurrentIrpStackLocation( pIrp ); 
   if (irpStack -> Parameters.DeviceIoControl.OutputBufferLength < sizeof( SERIAL_STATUS ))
   {
      pIrp -> IoStatus.Information = 0;
      return STATUS_BUFFER_TOO_SMALL;
   }
   if(!ioBuffer)
   {
      pIrp -> IoStatus.Information = 0;
      return STATUS_INVALID_PARAMETER;
   }
   pDevExt = DeviceObject -> DeviceExtension;

   if (!inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
   {
      pSs = &psudoStatus;
      pSs->Errors = SERIAL_ERROR_BREAK        |
                    SERIAL_ERROR_FRAMING      |
                    SERIAL_ERROR_OVERRUN      |
                    SERIAL_ERROR_QUEUEOVERRUN |
                    SERIAL_ERROR_PARITY;
      pSs->EofReceived = TRUE;
   }
   else
   {
      pSs = pDevExt->pSerialStatus;
   }

   // here we update any non-static fields...
   QcAcquireSpinLockWithLevel(&pDevExt->ReadSpinLock, &levelOrHandle, irql);
   pSs->AmountInInQueue = CountReadQueue(pDevExt);
   QcReleaseSpinLockWithLevel(&pDevExt->ReadSpinLock, levelOrHandle, irql);
   pSs->AmountInOutQueue = CountWriteQueue(pDevExt);

   pSs->HoldReasons = 0;

   if (pDevExt->TXHolding)
   {
      if (pDevExt->TXHolding & SERIAL_TX_CTS)
      {
         pSs->HoldReasons |= SERIAL_TX_WAITING_FOR_CTS;
      }

      if (pDevExt->TXHolding & SERIAL_TX_DSR)
      {
         pSs->HoldReasons |= SERIAL_TX_WAITING_FOR_DSR;
      }

      if (pDevExt->TXHolding & SERIAL_TX_DCD)
      {
         pSs->HoldReasons |= SERIAL_TX_WAITING_FOR_DCD;
      }

      if (pDevExt->TXHolding & SERIAL_TX_XOFF)
      {
         pSs->HoldReasons |= SERIAL_TX_WAITING_FOR_XON;
      }

      if (pDevExt->TXHolding & SERIAL_TX_BREAK)
      {
         pSs->HoldReasons |= SERIAL_TX_WAITING_ON_BREAK;
      }
   }

   if (pDevExt->RXHolding & SERIAL_RX_DSR)
   {
      pSs->HoldReasons |= SERIAL_RX_WAITING_FOR_DSR;
   }

   if (pDevExt->RXHolding & SERIAL_RX_XOFF)
   {
      pSs->HoldReasons |= SERIAL_TX_WAITING_XOFF_SENT;
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> Stat: Err(%lu)Hold(%lu)inq(%lu)outq(%lu)EofRcvd(%d)WaitForImm(%d)\n",
         pDevExt->PortName, pSs->Errors, pSs->HoldReasons,
         pSs->AmountInInQueue,pSs->AmountInOutQueue,
         pSs->EofReceived, pSs->WaitForImmediate
      )
   );

   RtlCopyMemory(ioBuffer, pSs, sizeof(SERIAL_STATUS));
   pSs->Errors = 0; // clear error field
   pSs->EofReceived = FALSE; // supported in binary mode
   pIrp->IoStatus.Information = sizeof(SERIAL_STATUS);

   return nts;
}

NTSTATUS SerialResetDevice( PDEVICE_OBJECT DeviceObject )
{
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   NTSTATUS nts = STATUS_UNSUCCESSFUL;

   pDevExt->bModemInfoValid = FALSE;

   if (pDevExt->ucDeviceType < DEVICETYPE_INVALID)
   {  // LIMIT: CDC has no MODEM_RESET command
      nts = STATUS_SUCCESS;
   }

   return nts;
}

// This function gets the number of non-purged IOB's
ULONG GetInputQueueStates(PDEVICE_OBJECT DeviceObject)
{
   PDEVICE_EXTENSION pDevExt;
   PVXD_WDM_IO_CONTROL_BLOCK pReadQueEntry, pNextBlock;
   ULONG readCount = 0, nonPurged = 0;
   PIRP pIrp;
   USHORT result = 0;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   pDevExt = DeviceObject -> DeviceExtension;

   QcAcquireSpinLockWithLevel(&pDevExt->ReadSpinLock, &levelOrHandle, irql);

   pReadQueEntry = pDevExt->pReadHead;

   while (pReadQueEntry)
   {
      readCount++;
      if (pReadQueEntry->bPurged == FALSE)
      {
         nonPurged++;
      }
      pReadQueEntry = pReadQueEntry->pNextEntry;
   }

   QcReleaseSpinLockWithLevel(&pDevExt->ReadSpinLock, levelOrHandle, irql);

   return nonPurged;
}

/*------------------------------------------------
 * Description:
 *
 * Return Value: bit mask
 *               bit 0: indicate an active IOB to
 *                      be purged or not
 *               bit 1: indicate any queued IOB to
 *                      be purged
 *               bit 2: indicate any remaining IOB
 *                      not to be purged
 *               bit 3: indicate an active IOB not
 *                      allowed to be purged
 *-------------------------------------------------*/
USHORT PurgeInputQueue(PDEVICE_OBJECT DeviceObject)
{
   PDEVICE_EXTENSION pDevExt;
   PVXD_WDM_IO_CONTROL_BLOCK pReadQueEntry, pNextBlock;
   int readCount = 0, rdCurr = 0, rdPurged = 0, nonPurged = 0;
   PIRP pIrp;
   USHORT result = 0;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   pDevExt = DeviceObject -> DeviceExtension;

   QCSER_DbgPrint2
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> PurgeInputQueue: enter\n", pDevExt->PortName)
   );

   QcAcquireSpinLockWithLevel(&pDevExt->ReadSpinLock, &levelOrHandle, irql);

   if (pDevExt->pReadCurrent != NULL)
   {
      rdCurr = 1;
      if (pDevExt->pReadCurrent->lPurgeForbidden == 0)
      {
         if (pDevExt->pReadCurrent->pCallingIrp != NULL)
         {
            pDevExt->pReadCurrent->bPurged = TRUE;
            result = 0x01;
         }

         rdCurr += 2;
      }
      else
      {
         result |= 0x08;
      }
   }

   pReadQueEntry = pDevExt->pReadHead;
/****************
   while (pReadQueEntry)
   {
      readCount++;
      if (pReadQueEntry->lPurgeForbidden == 0)
      {
         rdPurged++;
         pReadQueEntry->bPurged = TRUE;
      }
      else
      {
         nonPurged++;
      }
      pReadQueEntry = pReadQueEntry->pNextEntry;
   }
******************/

   // start of new implementation
   while (pReadQueEntry)
   {
      readCount++;
      if (pReadQueEntry->lPurgeForbidden == 0)
      {
         if (pReadQueEntry->pCallingIrp != NULL)
         {
            rdPurged++;
            pReadQueEntry->bPurged = TRUE;
         }
         else
         {
            nonPurged++;
         }
      }
      else
      {
         nonPurged++;
      }
      pReadQueEntry = pReadQueEntry->pNextEntry;
   }
   // end of new implementation

   if (rdPurged)  result |= 0x02;
   if (nonPurged) result |= 0x04;

   QCSER_DbgPrint2
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> PurgeInputQueue: result=0x%x rdCnt=%d, rdCurr=%d, rdPgd=%d nonPgd=%d\n",
        pDevExt->PortName, result, readCount, rdCurr, rdPurged, nonPurged)
   );
   QcReleaseSpinLockWithLevel(&pDevExt->ReadSpinLock, levelOrHandle, irql);
   QCSER_DbgPrint2
   (
      QCSER_DBG_MASK_READ,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> PurgeInputQueue: exit\n", pDevExt->PortName)
   );
   return result;
}

NTSTATUS SerialPurge( PDEVICE_OBJECT DeviceObject, PIRP pIrp )
{
   NTSTATUS nts = STATUS_SUCCESS, purgeNts;
   ULONG Mask;
   PIO_STACK_LOCATION irpStack;
   PDEVICE_EXTENSION pDevExt;
   KIRQL irqLevel = KeGetCurrentIrql();
   USHORT bInputPurged = 0;
   LARGE_INTEGER timeoutValue;
 
   timeoutValue.QuadPart = -(20 * 1000 * 1000);  // 2 seconds
   pDevExt = DeviceObject -> DeviceExtension;
   pIrp->IoStatus.Information = 0;
   irpStack = IoGetCurrentIrpStackLocation( pIrp ); 

   /* -------- References ----------------
   #define SERIAL_PURGE_TXABORT 0x00000001
   #define SERIAL_PURGE_RXABORT 0x00000002
   #define SERIAL_PURGE_TXCLEAR 0x00000004
   #define SERIAL_PURGE_RXCLEAR 0x00000008
   ---------------------------------------*/
   if (irpStack -> Parameters.DeviceIoControl.InputBufferLength < sizeof(ULONG)) 
   {
      InterlockedExchange(&(pDevExt->lPurgeBegin), 0);
      return STATUS_INVALID_PARAMETER;
   }

   Mask = *((ULONG *)(pIrp -> AssociatedIrp.SystemBuffer));

   if ((!Mask) || (Mask & (~(SERIAL_PURGE_TXABORT |
                             SERIAL_PURGE_RXABORT |
                             SERIAL_PURGE_TXCLEAR |
                             SERIAL_PURGE_RXCLEAR
                            )
                          )
   )) 
   {
      InterlockedExchange(&(pDevExt->lPurgeBegin), 0);
      return STATUS_INVALID_PARAMETER;
   }

   // TX
   // if (Mask & (SERIAL_PURGE_TXCLEAR | SERIAL_PURGE_TXABORT))
   if (Mask & SERIAL_PURGE_TXABORT) // Cancel the pending writes
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> SER:  SERIAL_PURGE_TXABORT\n", pDevExt->PortName)
      );
      // KeSetEvent(&pDevExt->WritePurgeEvent,IO_NO_INCREMENT,FALSE); // this doesn't work
      // PurgeOutputQueue(DeviceObject);
   }

   if ((Mask & SERIAL_PURGE_TXCLEAR) && (irqLevel == PASSIVE_LEVEL))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> SER:  SERIAL_PURGE_TXCLEAR\n", pDevExt->PortName)
      );
      // if (pDevExt->bWriteActive == TRUE)
      // {
      //    QCUSB_AbortOutput( DeviceObject );
      // }
      // QCUSB_ResetOutput( DeviceObject );
   }

   // RX
   // if (Mask & (SERIAL_PURGE_RXCLEAR | SERIAL_PURGE_RXABORT))
   if (Mask & SERIAL_PURGE_RXABORT) // Cancel the pending reads
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> SER:  SERIAL_PURGE_RXABORT\n", pDevExt->PortName)
      );
      if (KeGetCurrentIrql() > PASSIVE_LEVEL)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> SER:  Wrong IRQL::%d\n", pDevExt->PortName, KeGetCurrentIrql())
         );
         return STATUS_INVALID_PARAMETER;
      }
//    if (pDevExt->bPacketsRead)
      {
         bInputPurged = PurgeInputQueue(DeviceObject);
         if ((pDevExt->bL1ReadActive == TRUE) && (bInputPurged & 0x1))
         {
            KeClearEvent(&pDevExt->ReadIrpPurgedEvent);
            KeSetEvent(&pDevExt->L1ReadPurgeEvent,IO_NO_INCREMENT,FALSE);
            // Wait for the purge completion
            purgeNts = KeWaitForSingleObject
                       (
                          &pDevExt->ReadIrpPurgedEvent,
                          Executive,
                          KernelMode,
                          FALSE,
                          &timeoutValue // NULL
                       );
            if (purgeNts == STATUS_TIMEOUT)
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_READ,
                  QCSER_DBG_LEVEL_ERROR,
                  ("<%s> serialPurge:  WTO\n", pDevExt->PortName)
               );
            }
         }
         RemovePurgedReads(DeviceObject);
      }
   }
   if ((Mask & SERIAL_PURGE_RXCLEAR) && (irqLevel == PASSIVE_LEVEL))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> SER:  SERIAL_PURGE_RXCLEAR\n", pDevExt->PortName)
      );
      vResetReadBuffer(pDevExt, 1);          // diag ok
   }

   InterlockedExchange(&(pDevExt->lPurgeBegin), 0);

   // Start a possible read IOB only for RXABORT to avoid memory over allocation
   if (Mask & SERIAL_PURGE_RXABORT)
   {
      if (((bInputPurged & 0x04) == 0x0 || (pDevExt->pReadHead == NULL)) &&
          inDevState(DEVICE_STATE_PRESENT_AND_STARTED))
      {
         StartTheReadGoing(pDevExt, NULL, 4);
      }
   }

   /****
   if ((GetInputQueueStates(DeviceObject) < 2) || (pDevExt->pReadCurrent == NULL))
   {
         StartTheReadGoing(pDevExt, NULL);
   }
   *****/

   nts = STATUS_SUCCESS;

   return nts;
} // SerialPurge

NTSTATUS SerialLsrMstInsert( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )
{
   NTSTATUS nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt;
   UCHAR ucInsert;
 
   if(!ioBuffer)
   {
      pIrp -> IoStatus.Information = 0;
      return STATUS_INVALID_PARAMETER;
   }
   pDevExt = DeviceObject -> DeviceExtension;
   ucInsert = *(UCHAR *)ioBuffer;
   pDevExt -> bLsrMsrInsert = ucInsert; // bugbug implement this to modem?
   pIrp -> IoStatus.Information = sizeof(UCHAR);
   return nts;
}

NTSTATUS GetModemConfig( PDEVICE_OBJECT pDevObj, PUCHAR pBuf )
{
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   NTSTATUS nts = STATUS_SUCCESS;
   ULONG ulRetSize, ulBufLen = 0;
   PDEVICE_EXTENSION pDevExt = pDevObj->DeviceExtension;
   PMODEM_INFO pMi = (PMODEM_INFO)(&((PUSB_DEFAULT_PIPE_REQUEST)pBuf) -> Data[0]);

   if (pDevExt->ucDeviceType == DEVICETYPE_CDC)
   { // LIMIT: CDC has no firmware problem requiring cacheing of line config
      pRequest = (PUSB_DEFAULT_PIPE_REQUEST)pBuf;
      pRequest -> bmRequestType = CLASS_INTERFACE_TO_HOST;
      pRequest -> bRequest = CDC_GET_LINE_CODING;
      pRequest -> wValue = 0;
      pRequest -> wIndex = pDevExt->usCommClassInterface;
      pRequest -> wLength = UART_CONFIG_SIZE;
      nts = QCUSB_PassThrough( pDevObj, pBuf, ulBufLen, &ulRetSize );

      if (NT_SUCCESS(nts))
      {
         // LIMIT: CDC DataBits are raw (7,8,...) so encode them here
         if (pMi->ucDataBits == 7)
         {
            pMi->ucDataBits = 2;
         }
         else if (pMi->ucDataBits == 8)
         {
            pMi->ucDataBits = 3;
         }
         else
         {
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CONTROL,
               QCSER_DBG_LEVEL_ERROR,
               ("<%s>: GetCommConfig - unexpected data bits %d\n", pDevExt->PortName, pMi->ucDataBits)
            );
            pMi->ucDataBits = 3;
         }
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s>: GET_LINE_CODING failure, return default values\n", pDevExt->PortName)
         );
         pMi->ulDteRate    = 2*1024*1024; // 115200;
         pMi->ucParityType = 0;
         pMi->ucStopBit    = 0; //0 -> 1 stop bit
         pMi->ucDataBits   = 3;
         nts = STATUS_SUCCESS;
      }

      return nts;
   } // if DEVICETYPE_CDC

   // we simulate a serial interface without interrupt endpoint
   if (pDevExt->ucDeviceType >= DEVICETYPE_SERIAL)
   {
      if (pDevExt->bModemInfoValid == TRUE)
      {
         RtlCopyMemory( pMi, &pDevExt->ModemInfo, sizeof(MODEM_INFO) );
      }
      else
      {
         pMi->ulDteRate    = 115200;
         pMi->ucParityType = 0;
         pMi->ucStopBit    = 0; //0 -> 1 stop bit
         pMi->ucDataBits   = 3;
      }
      return nts;
   }
 
   return STATUS_UNSUCCESSFUL; // nts;
}

NTSTATUS SetModemConfig( PDEVICE_OBJECT pDevObj, PUCHAR pBuf )

{
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   NTSTATUS nts = STATUS_SUCCESS;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt = pDevObj->DeviceExtension;
   PMODEM_INFO pMi = (PMODEM_INFO)(&((PUSB_DEFAULT_PIPE_REQUEST)pBuf)->Data[0]);

   if (pDevExt->ucDeviceType == DEVICETYPE_CDC)
   { // LIMIT: CDC has no firmware problem requiring cacheing of line config
      // LIMIT: CDC DataBits are raw (7,8,...) so decode them here
      if (pMi->ucDataBits == 2)
      {
         pMi->ucDataBits = 7;
      }
      else
      {
         pMi->ucDataBits = 8;
      }

      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_INFO,
         ("<%s>: SetLineControl: DteRate=%ld, Parity=%d, StopBits=%ld, DataBits=%ld\n",
           pDevExt->PortName, pMi->ulDteRate, pMi->ucParityType,
           pMi->ucStopBit, pMi->ucDataBits
         )
      );

      pRequest = (PUSB_DEFAULT_PIPE_REQUEST)pBuf;
      pRequest -> bmRequestType = CLASS_HOST_TO_INTERFACE;
      pRequest->bRequest = CDC_SET_LINE_CODING;
      pRequest->wIndex = pDevExt->usCommClassInterface;
      pRequest -> wValue = 0;
      pRequest -> wLength = UART_CONFIG_SIZE;
      nts = QCUSB_PassThrough
            (
               pDevObj, pBuf, MODEM_CONTEXT_BUFSIZE, &ulRetSize
            );
      return nts;
   } // if DEVICETYPE_CDC

   // no action for an interface without interrupt endpoint
   if (pDevExt->ucDeviceType >= DEVICETYPE_SERIAL)
   {
      pDevExt->bModemInfoValid = TRUE;
      RtlCopyMemory( &pDevExt->ModemInfo, pMi, sizeof(MODEM_INFO) );
      return nts;
   }

   return STATUS_UNSUCCESSFUL; // nts;
}
 
NTSTATUS SerialGetBaudRate( PDEVICE_OBJECT DeviceObject, PVOID pInBuf, PIRP pIrp )
{
   NTSTATUS nts = STATUS_SUCCESS;
   UCHAR Modem[sizeof( USB_DEFAULT_PIPE_REQUEST )+sizeof( MODEM_INFO )];
   ULONG ulRetsize;
   PMODEM_INFO pMi;
   //ying
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   PIO_STACK_LOCATION irpStack;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
 
   irpStack = IoGetCurrentIrpStackLocation( pIrp ); 
   if (irpStack -> Parameters.DeviceIoControl.OutputBufferLength < sizeof(SERIAL_BAUD_RATE ))
   {
      pIrp -> IoStatus.Information = 0;
      return STATUS_BUFFER_TOO_SMALL;
   }

   if (!pInBuf)
   {
      pIrp -> IoStatus.Information = 0;
      return STATUS_INVALID_PARAMETER;
   }
   nts = GetModemConfig( DeviceObject, Modem );
   if (NT_SUCCESS( nts ))
   // if (STATUS_SUCCESS == nts )
   {
      //ying
      pRequest = (PUSB_DEFAULT_PIPE_REQUEST) Modem;
      pMi = (PMODEM_INFO)(&pRequest -> Data[0]);
      //
      *(ULONG *)pInBuf = pMi -> ulDteRate;    
      pIrp -> IoStatus.Information = sizeof( ULONG );

      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s>: GET: %lu\n", pDevExt->PortName, pMi->ulDteRate)
      );
   }
   else
   {
      pIrp -> IoStatus.Information = 0;
   }

   return nts;
}

NTSTATUS SerialSetBaudRate( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )

{
   NTSTATUS nts = STATUS_SUCCESS;
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   UCHAR ucBufIn[sizeof( USB_DEFAULT_PIPE_REQUEST )+sizeof( UART_CONFIG_SIZE )];
   UCHAR ucBufOut[sizeof( USB_DEFAULT_PIPE_REQUEST )+sizeof( UART_CONFIG_SIZE )];
   ULONG ulRetsize;
   PMODEM_INFO pMi;
   PVOID pOutBuf = NULL; 
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   PIO_STACK_LOCATION irpStack;
 
   pOutBuf = ioBuffer;
   if (!pOutBuf)
   {
      pIrp -> IoStatus.Information = 0;
      return STATUS_INVALID_PARAMETER;
   }
   irpStack = IoGetCurrentIrpStackLocation( pIrp ); 
   if (irpStack -> Parameters.DeviceIoControl.InputBufferLength < sizeof(SERIAL_BAUD_RATE ))
   {
      pIrp -> IoStatus.Information = 0;
      return STATUS_BUFFER_TOO_SMALL;
   }
      /* Must get the current config. before modifying part of it.
      */
   nts = GetModemConfig( DeviceObject, ucBufIn );
   if (NT_SUCCESS( nts ))
   // if (STATUS_SUCCESS == nts )
   {
      pRequest = (PUSB_DEFAULT_PIPE_REQUEST) ucBufIn;

      pMi = (PMODEM_INFO)&pRequest -> Data[0];

      pMi -> ulDteRate = *(ULONG *)pOutBuf;
      pRequest = (PUSB_DEFAULT_PIPE_REQUEST)ucBufOut;

      RtlCopyMemory( &pRequest -> Data[0], pMi, sizeof(ULONG) );

      nts = SetModemConfig( DeviceObject, ucBufOut );
      if(NT_SUCCESS(nts))
      // if (STATUS_SUCCESS == nts )
      {
         pIrp -> IoStatus.Information = sizeof(SERIAL_BAUD_RATE);
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s>: SET: Baud %lu Stop %x Parity %x\n Bits %x\n",
                pDevExt->PortName, pMi->ulDteRate, pMi->ucParityType,pMi->ucStopBit,pMi->ucDataBits)
         );
      }
      else 
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s>: SetBaudRate - status failure\n", pDevExt->PortName)
         );
         pIrp -> IoStatus.Information = 0;
      }
   }
   return nts;
}

NTSTATUS SerialSetQueueSize( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )

{
   NTSTATUS nts = STATUS_SUCCESS;
   PSERIAL_QUEUE_SIZE pQs = ioBuffer;
   PDEVICE_EXTENSION pDevExt = DeviceObject -> DeviceExtension;

   {
      pIrp->IoStatus.Information = sizeof(PSERIAL_QUEUE_SIZE);
   }
   return nts;
}


NTSTATUS SerialGetHandflow( PDEVICE_OBJECT DeviceObject, PVOID pInBuf, PIRP pIrp )
{
   NTSTATUS nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt;
   PSERIAL_HANDFLOW pSh;
   PIO_STACK_LOCATION irpStack;
 
   /*   -- Reference --
   typedef struct _SERIAL_HANDFLOW {
      ULONG ControlHandShake;
      ULONG FlowReplace;
      LONG XonLimit;
      LONG XoffLimit;
    } SERIAL_HANDFLOW,*PSERIAL_HANDFLOW;
    #define SERIAL_DTR_MASK           ((ULONG)0x03)
    #define SERIAL_DTR_CONTROL        ((ULONG)0x01)
    #define SERIAL_DTR_HANDSHAKE      ((ULONG)0x02)
    #define SERIAL_CTS_HANDSHAKE      ((ULONG)0x08)
    #define SERIAL_DSR_HANDSHAKE      ((ULONG)0x10)
    #define SERIAL_DCD_HANDSHAKE      ((ULONG)0x20)
    #define SERIAL_OUT_HANDSHAKEMASK  ((ULONG)0x38)
    #define SERIAL_DSR_SENSITIVITY    ((ULONG)0x40)
    #define SERIAL_ERROR_ABORT        ((ULONG)0x80000000)
    #define SERIAL_CONTROL_INVALID    ((ULONG)0x7fffff84)
    #define SERIAL_AUTO_TRANSMIT      ((ULONG)0x01)
    #define SERIAL_AUTO_RECEIVE       ((ULONG)0x02)
    #define SERIAL_ERROR_CHAR         ((ULONG)0x04)
    #define SERIAL_NULL_STRIPPING     ((ULONG)0x08)
    #define SERIAL_BREAK_CHAR         ((ULONG)0x10)
    #define SERIAL_RTS_MASK           ((ULONG)0xc0)
    #define SERIAL_RTS_CONTROL        ((ULONG)0x40)
    #define SERIAL_RTS_HANDSHAKE      ((ULONG)0x80)
    #define SERIAL_TRANSMIT_TOGGLE    ((ULONG)0xc0)
    #define SERIAL_XOFF_CONTINUE      ((ULONG)0x80000000)
    #define SERIAL_FLOW_INVALID       ((ULONG)0x7fffff20)
        -- End --     */

   irpStack = IoGetCurrentIrpStackLocation( pIrp ); 
   if (irpStack -> Parameters.DeviceIoControl.OutputBufferLength < sizeof( SERIAL_HANDFLOW ))
   {
      pIrp -> IoStatus.Information = 0;
      return STATUS_BUFFER_TOO_SMALL;
   }
   pDevExt = DeviceObject -> DeviceExtension;
   if (pInBuf)
   {
      pSh = pDevExt -> pSerialHandflow;
      RtlCopyMemory( pInBuf, pSh, sizeof( SERIAL_HANDFLOW ) );
      pIrp -> IoStatus.Information = sizeof( SERIAL_HANDFLOW );

      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s>: GET: Shake(0x%x) Replace(0x%x) XonLimit(0x%x) XoffLimit(0x%x)\n",
           pDevExt->PortName, pSh->ControlHandShake, pSh->FlowReplace, pSh->XonLimit, pSh->XoffLimit)
      );
   }
   else
   { 
       pIrp -> IoStatus.Information = 0;
       nts = STATUS_INVALID_PARAMETER;
   }
   return nts;
}

NTSTATUS SerialSetHandflow( PDEVICE_OBJECT DeviceObject, PVOID pOutBuf, PIRP pIrp )
{
   NTSTATUS nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt = DeviceObject -> DeviceExtension;
   PSERIAL_HANDFLOW pSh = (PSERIAL_HANDFLOW)pOutBuf;
   PSERIAL_STATUS pSs = pDevExt->pSerialStatus;
   BOOLEAN bTxWasHolding = (pSs->HoldReasons);
   PIO_STACK_LOCATION irpStack;

   irpStack = IoGetCurrentIrpStackLocation( pIrp );
   if (irpStack -> Parameters.DeviceIoControl.InputBufferLength < sizeof( SERIAL_HANDFLOW ))
   {
      pIrp -> IoStatus.Information = 0;
      return STATUS_BUFFER_TOO_SMALL;
   }
   if (pOutBuf == NULL)
   {
       pIrp -> IoStatus.Information = 0;
       return STATUS_INVALID_PARAMETER;
   }

// RtlCopyMemory( pDevExt->pSerialHandflow, pSh, sizeof( SERIAL_HANDFLOW ) );
// pIrp -> IoStatus.Information = sizeof( SERIAL_HANDFLOW );
// SerialSetRts(DeviceObject);
// SerialSetDtr(DeviceObject);

// return nts;

  // ------------------------------------------------------------
   if (pSh && !(pSh->ControlHandShake & SERIAL_CONTROL_INVALID)
        && !(pSh->FlowReplace & SERIAL_FLOW_INVALID))
   {
      RtlCopyMemory( pDevExt->pSerialHandflow, pSh, sizeof( SERIAL_HANDFLOW ) );
      pIrp -> IoStatus.Information = sizeof( SERIAL_HANDFLOW );
      if (pSh->FlowReplace & SERIAL_RTS_HANDSHAKE)
      {
         SerialSetRts(DeviceObject);
      }
      if (pSh->ControlHandShake & SERIAL_DTR_HANDSHAKE)
      {
         SerialSetDtr(DeviceObject);

         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_INFO,
            ("<%s> Set DTR on IOCTL_SERIAL_SET_HANDFLOW\n", pDevExt->PortName)
         );
      }

//    if (bTxWasHolding && !pSs->HoldReasons)
//    {
//       KeSetEvent(pDevExt->pWriteEvents[KICK_WRITE_EVENT_INDEX],IO_NO_INCREMENT,FALSE); // kick the write thread
//    } // restarting transmission

      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s>: SET: Shake(0x%x) Replace(0x%x) XonLimit(0x%x) XoffLimit(0x%x)\n",
           pDevExt->PortName, pSh->ControlHandShake, pSh->FlowReplace, pSh->XonLimit, pSh->XoffLimit)
      );
   }
   else 
   {
      pIrp -> IoStatus.Information = 0;
      nts = STATUS_INVALID_PARAMETER;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s>: SetHandflow - STATUS_INVALID_PARAMETER\n", pDevExt->PortName)
      );
   }
   return nts;
}

NTSTATUS SerialGetLineControl
(
   PDEVICE_OBJECT DeviceObject,
   PVOID pInBuf,
   PIRP pIrp
)
{
   NTSTATUS nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt;
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   UCHAR Modem[sizeof( USB_DEFAULT_PIPE_REQUEST )+sizeof( MODEM_INFO )];
   ULONG ulRetsize;
   PMODEM_INFO pMi;
   PSERIAL_LINE_CONTROL pSlc;
   PIO_STACK_LOCATION irpStack;
 
   irpStack = IoGetCurrentIrpStackLocation( pIrp ); 
   //ying
   // if (irpStack->Parameters.DeviceIoControl.InputBufferLength<UART_CONFIG_INFO)
   if (irpStack->Parameters.DeviceIoControl.OutputBufferLength<sizeof(SERIAL_LINE_CONTROL))
   {
      pIrp -> IoStatus.Information = 0;
      nts = STATUS_BUFFER_TOO_SMALL;
   }
   else
   {
      pDevExt = DeviceObject -> DeviceExtension;
      if (!pInBuf)
      {
         pIrp -> IoStatus.Information = 0;
         nts = STATUS_INVALID_PARAMETER;
      } 
      else
      {
         nts = GetModemConfig( DeviceObject, Modem );
         if (NT_SUCCESS( nts))
         // if (STATUS_SUCCESS == nts )
         {
            pMi  = (PMODEM_INFO)(&((PUSB_DEFAULT_PIPE_REQUEST)Modem) -> Data[0]);
            pSlc = (PSERIAL_LINE_CONTROL)pInBuf;
            pSlc->StopBits             = pMi->ucStopBit;
            pSlc->Parity               = pMi->ucParityType;
            pSlc->WordLength           = (pMi->ucDataBits == 2)? 7 : 8;        
            pIrp->IoStatus.Information = sizeof(SERIAL_LINE_CONTROL);
         }
         else 
         {
            pIrp -> IoStatus.Information = 0;
         }
      }
   }
   ASSERT(NT_SUCCESS(nts));
   return nts;
}

NTSTATUS SerialSetLineControl( PDEVICE_OBJECT DeviceObject, PVOID pOutBuf, PIRP pIrp )

{
   NTSTATUS nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt;
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   UCHAR ModemIn[sizeof( USB_DEFAULT_PIPE_REQUEST )+sizeof( MODEM_INFO )];
   UCHAR ModemOut[sizeof( USB_DEFAULT_PIPE_REQUEST )+sizeof( MODEM_INFO )];
   UCHAR ucWordLength;
   ULONG ulRetsize;
   PMODEM_INFO pMi;
   PSERIAL_LINE_CONTROL pSlc = NULL;
   PIO_STACK_LOCATION irpStack;

   if (pOutBuf == NULL)
   {
      pIrp -> IoStatus.Information = 0;
      return STATUS_INVALID_PARAMETER;
   }
   irpStack = IoGetCurrentIrpStackLocation( pIrp );
   if (irpStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(SERIAL_LINE_CONTROL))
   {
      pIrp -> IoStatus.Information = 0;
      return STATUS_BUFFER_TOO_SMALL;
   }

   pSlc = (PSERIAL_LINE_CONTROL) pOutBuf;
   //
   if (!pSlc)
   {
      pIrp -> IoStatus.Information = 0;
      nts = STATUS_INVALID_PARAMETER;
   }
   else
   {
      nts = GetModemConfig( DeviceObject, ModemIn );
      if (NT_SUCCESS( nts ))
      // if (STATUS_SUCCESS == nts )
      {
         // ying
         // pMi = (PMODEM_INFO)ModemIn;
         pMi = (PMODEM_INFO)(&((PUSB_DEFAULT_PIPE_REQUEST)ModemIn) -> Data[0]);
         
         pMi -> ucStopBit = pSlc -> StopBits;
         pMi -> ucParityType = pSlc -> Parity;

         // parity and stop bit format ok, but wordlength has to be changed
         // from (7,8) -> (2,3) and lengths less than 7 are illegal
         ucWordLength = pSlc->WordLength;
         if(ucWordLength < 7 || ucWordLength > 8)
         {
            nts = STATUS_INVALID_PARAMETER;
         }
         else
         {
            // fix up wordlength
            if (ucWordLength == 7)
            {
               ucWordLength = 2;
            } else
            {
               ucWordLength = 3;
            }

            pMi -> ucDataBits = ucWordLength;
      
            pRequest = (PUSB_DEFAULT_PIPE_REQUEST)ModemOut;
            RtlCopyMemory( &pRequest -> Data[0], pMi, sizeof( MODEM_INFO ) );
            nts = SetModemConfig( DeviceObject, ModemOut );
            // ying
            // pIrp -> IoStatus.Information = sizeof( MODEM_INFO );
         }
         if(NT_SUCCESS(nts))
         // if (STATUS_SUCCESS == nts )
         {
            pIrp -> IoStatus.Information = sizeof( SERIAL_LINE_CONTROL );
         }
         else 
         {
            pIrp -> IoStatus.Information = 0;
         }
      }
   }

   ASSERT(NT_SUCCESS(nts));
   return nts;
}

NTSTATUS SerialSetBreakOn( PDEVICE_OBJECT DeviceObject )

{
   NTSTATUS nts = STATUS_SUCCESS;
   USB_DEFAULT_PIPE_REQUEST Request;
   PUSB_DEFAULT_PIPE_REQUEST pRequest = &Request;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;

   if (pDevExt->ucDeviceType >= DEVICETYPE_SERIAL)
   {
      return nts;
   }
   if (pDevExt->ucDeviceType == DEVICETYPE_CDC)
   {
      Request.bmRequestType = CLASS_HOST_TO_INTERFACE;
      Request.bRequest = CDC_SEND_BREAK;
      Request.wValue = 0xFFFF; // length of break
      Request.wIndex = pDevExt->usCommClassInterface;
   }
   else
   {
      return STATUS_UNSUCCESSFUL;
   }
   Request.wLength = 0;

   nts = QCUSB_PassThrough( DeviceObject, pRequest, 0, &ulRetSize );
   return nts;
}

NTSTATUS SerialSetBreakOff( PDEVICE_OBJECT DeviceObject )

{
   NTSTATUS nts = STATUS_SUCCESS;
   USB_DEFAULT_PIPE_REQUEST Request;
   PUSB_DEFAULT_PIPE_REQUEST pRequest = &Request;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;

   if (pDevExt->ucDeviceType >= DEVICETYPE_SERIAL)
   {
      return nts;
   }
   if (pDevExt->ucDeviceType == DEVICETYPE_CDC)
   {
      Request.bmRequestType = CLASS_HOST_TO_INTERFACE;
      Request.bRequest = CDC_SEND_BREAK;
      Request.wValue = 0; // length of break
      Request.wIndex = pDevExt->usCommClassInterface;
   }
   else
   {
      return STATUS_UNSUCCESSFUL;
   }
   Request.wLength = 0;
   nts = QCUSB_PassThrough( DeviceObject, pRequest, 0, &ulRetSize );
   return nts;
}

NTSTATUS SerialGetTimeouts( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )

{
   NTSTATUS nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt;
   PVOID pInBuf = NULL;
   PSERIAL_TIMEOUTS pTo;
   PIO_STACK_LOCATION irpStack;
 
   /*  -- Reference --
    typedef struct _SERIAL_TIMEOUTS {
       ULONG ReadIntervalTimeout;
       ULONG ReadTotalTimeoutMultiplier;
       ULONG ReadTotalTimeoutConstant;
       ULONG WriteTotalTimeoutMultiplier;
       ULONG WriteTotalTimeoutConstant;
    } SERIAL_TIMEOUTS,*PSERIAL_TIMEOUTS;
       -- End --    */

   irpStack = IoGetCurrentIrpStackLocation( pIrp );
   pIrp -> IoStatus.Information = 0;
   if (irpStack -> Parameters.DeviceIoControl.OutputBufferLength < sizeof( SERIAL_TIMEOUTS ))
   {
      pIrp -> IoStatus.Information = 0;
      nts = STATUS_BUFFER_TOO_SMALL;
   }
   else
   {
      pDevExt = DeviceObject -> DeviceExtension;
      if (ioBuffer)
      {
         pTo = pDevExt->pSerialTimeouts;
         RtlCopyMemory( ioBuffer, pTo, sizeof( SERIAL_TIMEOUTS ) );
         pIrp -> IoStatus.Information = sizeof( SERIAL_TIMEOUTS );

         QCSER_DbgPrint
         (
            (QCSER_DBG_MASK_READ | QCSER_DBG_MASK_WRITE | QCSER_DBG_MASK_WIRP | QCSER_DBG_MASK_RIRP),
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> GET: RI=%u RM=%u RC=%u WM=%u WC=%u\n",
              pDevExt->PortName,
              pTo->ReadIntervalTimeout,
              pTo->ReadTotalTimeoutMultiplier,
              pTo->ReadTotalTimeoutConstant,
              pTo->WriteTotalTimeoutMultiplier,
              pTo->WriteTotalTimeoutConstant
            )
         );
      }
      else
      {
         pIrp -> IoStatus.Information = 0;
         nts = STATUS_INVALID_PARAMETER;
      }
   } 
   return nts;
}

NTSTATUS SerialSetTimeouts(PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp)
{
   NTSTATUS nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt;
   PSERIAL_TIMEOUTS pTo;
   PIO_STACK_LOCATION irpStack;

   pDevExt = DeviceObject->DeviceExtension;

   irpStack = IoGetCurrentIrpStackLocation( pIrp );
   pIrp -> IoStatus.Information = 0;
   if (irpStack -> Parameters.DeviceIoControl.InputBufferLength != sizeof( SERIAL_TIMEOUTS ))
   {
      QCSER_DbgPrint
      (
         0xFFFFFFFF,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> SetTimeout: wrong buf size: %d\n", pDevExt->PortName,
           irpStack->Parameters.DeviceIoControl.InputBufferLength)
      );
      pIrp -> IoStatus.Information = 0;
      return STATUS_INVALID_PARAMETER;
   }

   if (ioBuffer)
   {
      pTo = pDevExt -> pSerialTimeouts;
      RtlCopyMemory( pTo, ioBuffer, sizeof( SERIAL_TIMEOUTS ) );
      pIrp -> IoStatus.Information = sizeof( SERIAL_TIMEOUTS );

      // Decide Read Timeout Type
      if (pTo->ReadIntervalTimeout == 0)
      {
         // case 1/2: no timeout -- read until chars are received
         if ((pTo->ReadTotalTimeoutMultiplier == 0) &&
              (pTo->ReadTotalTimeoutConstant == 0))
         {
            pDevExt->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_1;
         }
         else if ((pTo->ReadTotalTimeoutMultiplier == MAXULONG) ||
                  (pTo->ReadTotalTimeoutConstant == MAXULONG))
         {
            pDevExt->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_2; 
         }
         // case 3: timeout after ReadTotalTimeout
         else
         {
            pDevExt->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_3; 
         }
      }
      else if (pTo->ReadIntervalTimeout == MAXULONG)
      {
         // case 4: return immediately
         if ((pTo->ReadTotalTimeoutMultiplier == 0) &&
             (pTo->ReadTotalTimeoutConstant == 0))
         {
            pDevExt->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_4; 
         }
         else if (pTo->ReadTotalTimeoutMultiplier == MAXULONG)
         {
            // case 5: special handling
            if ((pTo->ReadTotalTimeoutConstant > 0) &&
                (pTo->ReadTotalTimeoutConstant < MAXULONG))
            {
               pDevExt->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_5;
               pDevExt->ReadTimeout.bReturnOnAnyChars = TRUE;
            }
            // case 6: RI=MAX RM=MAX RC=MAX/0 => return immediately
            else
            {
               pDevExt->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_6;
            }
         }
         // case 7: no timeout
         else if (pTo->ReadTotalTimeoutConstant == MAXULONG)
         {
            pDevExt->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_7;
         }
         // case 8: RI=MAX RM=[0..MAX) RC=[0..MAX), use read total timeout
         else
         {
            pDevExt->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_8;
         }
      }
      // case 9/10: RI==(0..MAXULONG), RI timeout
      else if ((pTo->ReadTotalTimeoutConstant==0) &&
               (pTo->ReadTotalTimeoutMultiplier==0))
      {
         pDevExt->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_9;
         pDevExt->ReadTimeout.bUseReadInterval = TRUE;
      }
      else if ((pTo->ReadTotalTimeoutConstant == MAXULONG) ||
               (pTo->ReadTotalTimeoutMultiplier == MAXULONG))
      {
         pDevExt->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_10;
         pDevExt->ReadTimeout.bUseReadInterval = TRUE;
      }
      // case 11: RI/RM/RC == (0..MAXULONG), choose a smaller timeout
      else
      {
         pDevExt->ReadTimeout.ucTimeoutType = QCSER_READ_TIMEOUT_CASE_11;
      }

      if ((pDevExt->ReadTimeout.ucTimeoutType != QCSER_READ_TIMEOUT_CASE_9) &&
          (pDevExt->ReadTimeout.ucTimeoutType != QCSER_READ_TIMEOUT_CASE_10))
      {
         pDevExt->ReadTimeout.bUseReadInterval = FALSE;
      }

      if (pDevExt->ReadTimeout.ucTimeoutType != QCSER_READ_TIMEOUT_CASE_5)
      {
         pDevExt->ReadTimeout.bReturnOnAnyChars = FALSE;
      }

      QCSER_DbgPrint
      (
         (QCSER_DBG_MASK_READ | QCSER_DBG_MASK_WRITE | QCSER_DBG_MASK_WIRP | QCSER_DBG_MASK_RIRP),
         QCSER_DBG_LEVEL_CRITICAL,
         ("<%s> SET: RI=%u RM=%u RC=%u WM=%u WC=%u\n",
           pDevExt->PortName,
           pTo->ReadIntervalTimeout,
           pTo->ReadTotalTimeoutMultiplier,
           pTo->ReadTotalTimeoutConstant,
           pTo->WriteTotalTimeoutMultiplier,
           pTo->WriteTotalTimeoutConstant
         )
      );
   }
   else
   {
      pIrp -> IoStatus.Information = 0;
      nts = STATUS_INVALID_PARAMETER;
   }

   return nts;
}

NTSTATUS SerialImmediateChar(PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp)
{
   NTSTATUS nts = STATUS_SUCCESS;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(pIrp);
 
   if ((irpStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(UCHAR)) ||
      (ioBuffer == NULL)) 
   {
      pIrp->IoStatus.Information = 0;
      nts = STATUS_INVALID_PARAMETER;
   }
   else 
   {
      PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
      PUCHAR puc = (PUCHAR)ioBuffer;

      if (pDevExt->ucDeviceType < DEVICETYPE_INVALID)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_INFO,
            ("<%s> ImmediateChar 0x%x\n", pDevExt->PortName, (UCHAR)*puc)
         );
         return QCWT_ImmediateChar(DeviceObject, ioBuffer, pIrp);
      }
      else
      {
         nts = STATUS_UNSUCCESSFUL;
      }

      if (!NT_SUCCESS(nts))
      {
         pIrp->IoStatus.Information = 0;
      }
      else
      {
         pIrp->IoStatus.Information = sizeof(UCHAR);
      }
   }
   return nts;
}

NTSTATUS SerialXoffCounter( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )

{
 NTSTATUS nts = STATUS_SUCCESS;
_int3; // bugbug temp
 pIrp -> IoStatus.Information = 0;
 return nts;
}

NTSTATUS SerialSetDtr( PDEVICE_OBJECT DeviceObject )
{
   NTSTATUS nts = STATUS_SUCCESS;
   USB_DEFAULT_PIPE_REQUEST Request;
   PUSB_DEFAULT_PIPE_REQUEST pRequest = &Request;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   UCHAR ucSetValue = CDC_CONTROL_LINE_DTR;

   if (pDevExt->ucDeviceType >= DEVICETYPE_SERIAL)
   {
      pDevExt->ModemControlReg |= SERIAL_DTR_STATE;
      return nts;
   }
   if (pDevExt->ModemControlReg & SERIAL_RTS_STATE)
   {
      ucSetValue |= CDC_CONTROL_LINE_RTS;
   }

   if ((pDevExt->ucDeviceType == DEVICETYPE_CDC) &&
       (inDevState(DEVICE_STATE_PRESENT_AND_STARTED)))
   {
      Request.bmRequestType = CLASS_HOST_TO_INTERFACE;
      Request.bRequest = CDC_SET_CONTROL_LINE_STATE;
      Request.wValue = ucSetValue;
      // KdPrint(("DBG-- SetDtr; value: 0x%x\n", ucSetValue));
      Request.wIndex = pDevExt->usCommClassInterface;
   }
   else
   {
      return STATUS_UNSUCCESSFUL;
   }
   Request.wLength = 0;
   nts = QCUSB_PassThrough( DeviceObject, pRequest, 0, &ulRetSize );
   if (NT_SUCCESS(nts))
   {
     pDevExt->ModemControlReg |= SERIAL_DTR_STATE;
   }
   // KdPrint(("DBG-- SetDtr; ModemControlReg: 0x%x\n", pDevExt->ModemControlReg));
   return nts;
}

NTSTATUS SerialClrDtr( PDEVICE_OBJECT DeviceObject )

{
   NTSTATUS nts = STATUS_SUCCESS;
   USB_DEFAULT_PIPE_REQUEST Request;
   PUSB_DEFAULT_PIPE_REQUEST pRequest = &Request;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   UCHAR ucSetValue = 0;

   if (pDevExt->ucDeviceType >= DEVICETYPE_SERIAL)
   {
      pDevExt->ModemControlReg &= ~SERIAL_DTR_STATE;
      return nts;
   }

   if (pDevExt->ModemControlReg & SERIAL_RTS_STATE)
   {
      ucSetValue = CDC_CONTROL_LINE_RTS;
   }
   if (pDevExt->ucDeviceType == DEVICETYPE_CDC)
   {
      Request.bmRequestType = CLASS_HOST_TO_INTERFACE;
      Request.bRequest = CDC_SET_CONTROL_LINE_STATE;
      Request.wValue = ucSetValue;
      // KdPrint(("DBG-- ClrDtr; value: 0x%x\n", ucSetValue));
      Request.wIndex = pDevExt->usCommClassInterface;
   }
   else
   {
      return STATUS_UNSUCCESSFUL;
   }
   Request.wLength = 0;
   nts = QCUSB_PassThrough( DeviceObject, pRequest, 0, &ulRetSize );
   if (NT_SUCCESS(nts))
   {
     pDevExt->ModemControlReg &= ~SERIAL_DTR_STATE;
   }
   // KdPrint(("DBG-- ClrDtr; ModemControlReg: 0x%x\n", pDevExt->ModemControlReg));
   return nts;
}

NTSTATUS SerialSetRts( PDEVICE_OBJECT DeviceObject )
{
   NTSTATUS nts = STATUS_SUCCESS;
   USB_DEFAULT_PIPE_REQUEST Request;
   PUSB_DEFAULT_PIPE_REQUEST pRequest = &Request;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   UCHAR ucSetValue = CDC_CONTROL_LINE_RTS;

   if (pDevExt->ucDeviceType >= DEVICETYPE_SERIAL)
   {
      pDevExt->ModemControlReg |= SERIAL_RTS_STATE;
      return nts;
   }

   if (pDevExt->ModemControlReg & SERIAL_DTR_STATE)
   {
      ucSetValue |= CDC_CONTROL_LINE_DTR;
   }
   if (pDevExt->ucDeviceType == DEVICETYPE_CDC)
   {
      Request.bmRequestType = CLASS_HOST_TO_INTERFACE;
      Request.bRequest = CDC_SET_CONTROL_LINE_STATE;
      Request.wValue = ucSetValue;
      // KdPrint(("DBG-- SetRts; value: 0x%x\n", ucSetValue));
      Request.wIndex = pDevExt->usCommClassInterface;
   }
   else
   {
      return STATUS_UNSUCCESSFUL;
   }
   Request.wLength = 0;
   nts = QCUSB_PassThrough( DeviceObject, pRequest, 0, &ulRetSize );
   if (NT_SUCCESS(nts))
   {
     pDevExt->ModemControlReg |= SERIAL_RTS_STATE;
   }
   // KdPrint(("DBG-- SetRts; ModemControlReg: 0x%x\n", pDevExt->ModemControlReg));
   return nts;
}

NTSTATUS SerialClrRts( PDEVICE_OBJECT DeviceObject )
{
   NTSTATUS nts = STATUS_SUCCESS;
   USB_DEFAULT_PIPE_REQUEST Request;
   PUSB_DEFAULT_PIPE_REQUEST pRequest = &Request;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   UCHAR ucSetValue = 0;

   if (pDevExt->ucDeviceType >= DEVICETYPE_SERIAL)
   {
      pDevExt->ModemControlReg &= ~SERIAL_RTS_STATE;
      return nts;
   }

   if (pDevExt->ModemControlReg & SERIAL_DTR_STATE)
   {
      ucSetValue = CDC_CONTROL_LINE_DTR;
   }
   if (pDevExt->ucDeviceType == DEVICETYPE_CDC)
   {
      Request.bmRequestType = CLASS_HOST_TO_INTERFACE;
      Request.bRequest = CDC_SET_CONTROL_LINE_STATE;
      Request.wValue = ucSetValue;
      // KdPrint(("DBG-- ClrRts; value: 0x%x\n", ucSetValue));
      Request.wIndex = pDevExt->usCommClassInterface;
   }
   else
   {
      return STATUS_UNSUCCESSFUL;
   }
   Request.wLength = 0;
   nts = QCUSB_PassThrough( DeviceObject, pRequest, 0, &ulRetSize );
   if (NT_SUCCESS(nts))
   {
     pDevExt->ModemControlReg &= ~SERIAL_RTS_STATE;
   }
   // KdPrint(("DBG-- ClrRts; ModemControlReg: 0x%x\n", pDevExt->ModemControlReg));
   return nts;
}

NTSTATUS SerialClrDtrRts( PDEVICE_OBJECT DeviceObject, BOOLEAN bLocalOnly )
{
   NTSTATUS nts = STATUS_SUCCESS;
   USB_DEFAULT_PIPE_REQUEST Request;
   PUSB_DEFAULT_PIPE_REQUEST pRequest = &Request;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;

   if (pDevExt->ucDeviceType >= DEVICETYPE_SERIAL)
   {
      pDevExt->ModemControlReg &= ~SERIAL_DTR_STATE;
      pDevExt->ModemControlReg &= ~SERIAL_RTS_STATE;
      return nts;
   }
   if (pDevExt->ucDeviceType == DEVICETYPE_CDC)
   {
      if (bLocalOnly == TRUE)
      {
         pDevExt->ModemControlReg &= ~SERIAL_DTR_STATE;
         pDevExt->ModemControlReg &= ~SERIAL_RTS_STATE;
         return nts;
      }
      
      Request.bmRequestType = CLASS_HOST_TO_INTERFACE;
      Request.bRequest = CDC_SET_CONTROL_LINE_STATE;
      Request.wValue = 0;
      Request.wIndex = pDevExt->usCommClassInterface;
   }
   else
   {
      return STATUS_INVALID_PARAMETER;
   }
   Request.wLength = 0;
   nts = QCUSB_PassThrough( DeviceObject, pRequest, 0, &ulRetSize );
   if (nts == STATUS_SUCCESS)
   {
     pDevExt->ModemControlReg &= ~SERIAL_DTR_STATE;
     pDevExt->ModemControlReg &= ~SERIAL_RTS_STATE;
   }
   return nts;
}

NTSTATUS SerialGetDtrRts( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )

{
   NTSTATUS nts = STATUS_SUCCESS;
   PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation (pIrp);
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   ULONG ModemControl = (ULONG) (pDevExt->ModemControlReg);
            
   if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
   {
      nts = STATUS_BUFFER_TOO_SMALL;
      pIrp->IoStatus.Information = 0;
   }
   else
   {
      nts = STATUS_SUCCESS;
      pIrp->IoStatus.Status = STATUS_SUCCESS;
      ModemControl &= (SERIAL_DTR_STATE | SERIAL_RTS_STATE);
      *(PULONG)pIrp->AssociatedIrp.SystemBuffer = ModemControl;
   }
   // KdPrint(("DBG-- GetDtrRts; ModemControl: 0x%x\n", ModemControl));
   return nts;
}

NTSTATUS SerialSetXon( PDEVICE_OBJECT DeviceObject )
{
   // We don't support soft flow control
   return STATUS_INVALID_PARAMETER;
}

NTSTATUS SerialSetXoff( PDEVICE_OBJECT DeviceObject )
{
   // We don't support soft flow control
   return STATUS_INVALID_PARAMETER;
}

NTSTATUS SerialGetWaitMask( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )
{
 NTSTATUS ntStatus;
 PDEVICE_EXTENSION pDevExt = DeviceObject -> DeviceExtension;
 PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation( pIrp );

   if (irpStack -> Parameters.DeviceIoControl.OutputBufferLength < sizeof( ULONG ))
   {
     pIrp -> IoStatus.Information = 0;
     _int3; // error
      return STATUS_BUFFER_TOO_SMALL;
   }
   if (!ioBuffer)
   {
     pIrp -> IoStatus.Information = 0;
     _int3; // error
     return STATUS_INVALID_PARAMETER;
   }

   *(ULONG *) pIrp -> AssociatedIrp.SystemBuffer = pDevExt -> ulWaitMask;
   pIrp -> IoStatus.Information = sizeof( ULONG );

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> GetWaitMask: 0x%x\n", pDevExt->PortName, pDevExt->ulWaitMask)
   );

   ntStatus = STATUS_SUCCESS;
   return ntStatus;
}

NTSTATUS SerialSetWaitMask( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )
{
   PDEVICE_EXTENSION pDevExt = DeviceObject -> DeviceExtension;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation( pIrp );

   if (irpStack -> Parameters.DeviceIoControl.InputBufferLength < sizeof( ULONG ))
   {
      pIrp -> IoStatus.Information = 0;
      _int3; // error
      return STATUS_BUFFER_TOO_SMALL;
   }
   if (!ioBuffer)
   {
     pIrp -> IoStatus.Information = 0;
     _int3; // error
      return STATUS_INVALID_PARAMETER;
   }

   pDevExt->ulWaitMask = *(ULONG *)ioBuffer; // first set the new mask

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> SetWaitMask: 0x%x\n", pDevExt->PortName, pDevExt->ulWaitMask)
   );

   return STATUS_SUCCESS;
}

NTSTATUS SerialNotifyClient( PIRP pIrp, ULONG info )
{
   if (IoSetCancelRoutine(pIrp, NULL) == NULL)
   {
      return STATUS_UNSUCCESSFUL;
   }
   *(ULONG *)pIrp->AssociatedIrp.SystemBuffer = info;
   pIrp->IoStatus.Information = sizeof( ULONG );
   pIrp->IoStatus.Status = STATUS_SUCCESS;
   return STATUS_SUCCESS;
}

NTSTATUS SerialCacheNotificationIrp( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )
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
   
   pDevExt = DeviceObject -> DeviceExtension;
   pIrp -> IoStatus.Information = 0; // default is error for now
   ntStatus = STATUS_INVALID_PARAMETER; // default is error for now

   if(!ioBuffer)
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
   if (pDevExt->pNotificationIrp != NULL)
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
      pDevExt->pNotificationIrp = pIrp;
      IoSetCancelRoutine(pIrp, CancelNotificationRoutine);

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
            pDevExt->pNotificationIrp= NULL;
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
}  // SerialCacheNotificationIrp

NTSTATUS SerialWaitOnMask( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )
{
   ULONG ulInitialEvents;
   PIO_STACK_LOCATION irpStack;
   NTSTATUS ntStatus = STATUS_CANCELLED;
   PDEVICE_EXTENSION pDevExt;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   pDevExt = DeviceObject -> DeviceExtension;
   pIrp -> IoStatus.Information = 0; // default is error for now
   ntStatus = STATUS_INVALID_PARAMETER; // default is error for now

   if(!ioBuffer) 
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> WaitOnMask: 0x%p INVALID_PARAM\n", pDevExt->PortName, pIrp)
      );
      goto SWOM_return;
   }

   irpStack = IoGetCurrentIrpStackLocation( pIrp );
   if (irpStack -> Parameters.DeviceIoControl.OutputBufferLength < sizeof( ULONG ))
   {
      ntStatus = STATUS_BUFFER_TOO_SMALL;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> WaitOnMask: 0x%p BUF_SMALL\n", pDevExt->PortName, pIrp)
      );
      goto SWOM_return;
   }
 

   if ((!inDevState(DEVICE_STATE_PRESENT_AND_STARTED)) &&
       (pDevExt->bInService == FALSE) &&
       (gVendorConfig.DriverResident == 0))
   {
      ntStatus = STATUS_DELETE_PENDING;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> WaitOnMask: 0x%p DELETE_PENDING\n", pDevExt->PortName, pIrp)
      );
      goto SWOM_return;
   }

   if_DevState( DEVICE_STATE_WOM_FIRST_TIME )
   {
      ulInitialEvents = InitUartStateFromModem(DeviceObject, TRUE); // clears WOM_STATE_FIRST_TIME
   }

   QcAcquireSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, &levelOrHandle, irql);

   if ((pDevExt->ulWaitMask != 0) && (pDevExt->pWaitOnMaskIrp == NULL))
   {
      // always pend the IRP, never complete it immediately
      ntStatus = STATUS_PENDING;
      _IoMarkIrpPending(pIrp); // it should already be pending from the dispatch queue!
      pDevExt->pWaitOnMaskIrp = pIrp;
      IoSetCancelRoutine(pIrp, CancelWOMRoutine);

      if (pIrp->Cancel)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> WaitOnMask: WOM Cxled\n", pDevExt->PortName)
         );
         if (IoSetCancelRoutine(pIrp, NULL) != NULL)
         {
            pDevExt->pWaitOnMaskIrp= NULL;
            ntStatus = STATUS_CANCELLED;
         }
         else
         {
            // do nothing
         }
      } // if
   
      QCSER_DbgPrint2
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> WaitOnMask: Cached 0x%p\n", pDevExt->PortName, pIrp)
      );
   }

   QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);

   // the dispatch routine will complete the IRP if ntStatus is not pending

SWOM_return:

   return ntStatus;
}  // SerialWaitOnMask

NTSTATUS SerialGetChars( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )
{
   /* -- Reference --
   typedef struct _SERIAL_CHARS {
      UCHAR EofChar;
      UCHAR ErrorChar;
      UCHAR BreakChar;
      UCHAR EventChar;
      UCHAR XonChar;
      UCHAR XoffChar;
    } SERIAL_CHARS,*PSERIAL_CHARS;
      -- end --    */

   NTSTATUS nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt;
   PIO_STACK_LOCATION irpStack;
 
   pDevExt = DeviceObject -> DeviceExtension;
   irpStack = IoGetCurrentIrpStackLocation( pIrp );
   //ying
   if (irpStack->Parameters.DeviceIoControl.OutputBufferLength<sizeof(SERIAL_CHARS))
   {
      pIrp -> IoStatus.Information = 0;
      _int3; // error
      return STATUS_BUFFER_TOO_SMALL;
   }
   if(!ioBuffer) 
   {
      pIrp -> IoStatus.Information = 0;
      nts = STATUS_INVALID_PARAMETER;
      _int3; // error
   }
   else 
   {
      pDevExt = DeviceObject -> DeviceExtension;
      RtlCopyMemory(ioBuffer, pDevExt->pSerialChars, sizeof( SERIAL_CHARS ) );
      pIrp->IoStatus.Information = sizeof(SERIAL_CHARS);
   }
   return nts;
}

NTSTATUS SerialSetChars( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )
{
   NTSTATUS nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt;
   PSERIAL_CHARS pSc;
 
   pDevExt = DeviceObject -> DeviceExtension;
   if (ioBuffer)
   {
      pSc = pDevExt -> pSerialChars;
      RtlCopyMemory( pSc, ioBuffer, sizeof( SERIAL_CHARS ) );
      pIrp -> IoStatus.Information = sizeof( SERIAL_CHARS );
   }
   else
   {
      pIrp -> IoStatus.Information = 0; 
      nts = STATUS_INVALID_PARAMETER;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s>: SerialSetChars - STATUS_INVALID_PARAMETER\n", pDevExt->PortName)
      );
   }
   return nts;
}

NTSTATUS SerialGetDriverGUIDString( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )
{
   NTSTATUS           nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION  pDevExt;
   PIO_STACK_LOCATION irpStack;
   ULONG              inBufLen;
   ULONG              drvGuidLen;
   char               *drvIdStr;

   pDevExt  = DeviceObject->DeviceExtension;
   irpStack = IoGetCurrentIrpStackLocation(pIrp);

   if (pDevExt->ucDeviceType == DEVICETYPE_CDC)
   {
      drvGuidLen = strlen(QCUSB_DRIVER_GUID_DATA_STR);
      drvIdStr = QCUSB_DRIVER_GUID_DATA_STR;
   }
   else if (pDevExt->ucDeviceType >= DEVICETYPE_SERIAL)
   {
      drvGuidLen = strlen(QCUSB_DRIVER_GUID_DIAG_STR);
      drvIdStr = QCUSB_DRIVER_GUID_DIAG_STR;
   }
   else
   {
      drvGuidLen = strlen(QCUSB_DRIVER_GUID_UNKN_STR);
      drvIdStr = QCUSB_DRIVER_GUID_UNKN_STR;
   }

   inBufLen = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
   if (inBufLen < drvGuidLen)
   {
      pIrp->IoStatus.Information = 0;
      return STATUS_BUFFER_TOO_SMALL;
   }

   if(!ioBuffer)
   {
      pIrp->IoStatus.Information = 0;
      nts = STATUS_INVALID_PARAMETER;
   }
   else
   {
      pDevExt = DeviceObject->DeviceExtension;
      RtlCopyMemory(ioBuffer, drvIdStr, drvGuidLen);
      pIrp->IoStatus.Information = drvGuidLen;
   }
   return nts;
}  // SerialGetDriverGUIDString

NTSTATUS SerialGetServiceKey( PDEVICE_OBJECT DeviceObject, PVOID ioBuffer, PIRP pIrp )
{
   NTSTATUS           nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION  pDevExt;
   PIO_STACK_LOCATION irpStack;
   ULONG              inBufLen;
   ULONG              drvKeyLen;
   int                i;
   char               *p;

   pDevExt  = DeviceObject->DeviceExtension;
   irpStack = IoGetCurrentIrpStackLocation(pIrp);

   drvKeyLen = gServicePath.Length / 2;
   inBufLen = irpStack->Parameters.DeviceIoControl.OutputBufferLength;

   if (inBufLen < (drvKeyLen+1))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> ServiceKey: expect buf siz %dB\n", pDevExt->PortName, (drvKeyLen+1))
      );
      pIrp->IoStatus.Information = 0;
      return STATUS_BUFFER_TOO_SMALL;
   }

   if(!ioBuffer)
   {
      pIrp->IoStatus.Information = 0;
      nts = STATUS_INVALID_PARAMETER;
   }
   else
   {
      p = (char *)ioBuffer;
      for (i = 0; i < drvKeyLen; i++)
      {
         p[i] = (char)gServicePath.Buffer[i];
      }
      p [i] = 0;
      pIrp->IoStatus.Information = drvKeyLen;
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> ServiceKey: %s\n", pDevExt->PortName, p)
      );
   }
   return nts;
}  // SerialGetServiceKey

NTSTATUS SerialQueryInformation(PDEVICE_OBJECT DeviceObject, PIRP pIrp)
{
   /* -- Reference --
      typedef enum _FILE_INFORMATION_CLASS {
          FileBasicInformation = 4,
          FileStandardInformation = 5,
          FilePositionInformation = 14,
          FileEndOfFileInformation = 20,
      } FILE_INFORMATION_CLASS, *PFILE_INFORMATION_CLASS;

      typedef struct _FILE_STANDARD_INFORMATION {
          LARGE_INTEGER AllocationSize;
          LARGE_INTEGER EndOfFile;
          ULONG NumberOfLinks;
          BOOLEAN DeletePending;
          BOOLEAN Directory;
      } FILE_STANDARD_INFORMATION, *PFILE_STANDARD_INFORMATION;

      typedef struct _FILE_POSITION_INFORMATION {
          LARGE_INTEGER CurrentByteOffset;
      } FILE_POSITION_INFORMATION, *PFILE_POSITION_INFORMATION;
      -- end --    */

   NTSTATUS nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt;
   PIO_STACK_LOCATION irpStack;

   pDevExt = DeviceObject->DeviceExtension;
   irpStack = IoGetCurrentIrpStackLocation(pIrp);
   pIrp->IoStatus.Information = 0;

   switch (irpStack->Parameters.QueryFile.FileInformationClass)
   {
      case FileStandardInformation:
      {
         PFILE_STANDARD_INFORMATION fileInfo = pIrp->AssociatedIrp.SystemBuffer;

         if (irpStack->Parameters.DeviceIoControl.OutputBufferLength <
             sizeof(FILE_STANDARD_INFORMATION))
         {
            pIrp->IoStatus.Information = 0;
            return STATUS_BUFFER_TOO_SMALL;
         }
         fileInfo->AllocationSize.QuadPart = 0;
         fileInfo->EndOfFile = fileInfo->AllocationSize;
         fileInfo->NumberOfLinks = 0;
         fileInfo->DeletePending = FALSE;
         fileInfo->Directory = FALSE;
         pIrp->IoStatus.Information = sizeof(FILE_STANDARD_INFORMATION);
         break;
      }
      case FilePositionInformation:
      {
         PFILE_POSITION_INFORMATION fileInfo = pIrp->AssociatedIrp.SystemBuffer;

         if (irpStack->Parameters.DeviceIoControl.OutputBufferLength <
             sizeof(FILE_POSITION_INFORMATION))
         {
            pIrp->IoStatus.Information = 0;
            return STATUS_BUFFER_TOO_SMALL;
         }
         fileInfo->CurrentByteOffset.QuadPart = 0;
         pIrp->IoStatus.Information = sizeof(FILE_POSITION_INFORMATION);

         break;
      }
      default:
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> QueryInformation: unknown infoClass 0x%x\n", pDevExt->PortName, 
              irpStack->Parameters.QueryFile.FileInformationClass)
         );
         nts = STATUS_INVALID_PARAMETER;
         break;
      }
   }

   return nts;
}  // SerialQueryInformation

NTSTATUS SerialSetInformation(PDEVICE_OBJECT DeviceObject, PIRP pIrp)
{
   NTSTATUS nts = STATUS_SUCCESS;
   PDEVICE_EXTENSION pDevExt;
   PIO_STACK_LOCATION irpStack;

   pDevExt = DeviceObject->DeviceExtension;
   irpStack = IoGetCurrentIrpStackLocation(pIrp);

   switch (irpStack->Parameters.SetFile.FileInformationClass)
   {
      case FileEndOfFileInformation:
      case FileAllocationInformation:
      {
         break;
      }
      default:
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> SetInformation: unknown infoClass 0x%x\n", pDevExt->PortName, 
              irpStack->Parameters.SetFile.FileInformationClass)
         );
         nts = STATUS_INVALID_PARAMETER;
         break;
      }
   }

   pIrp->IoStatus.Information = 0;

   return nts;
}  // SerialSetInformation






NTSTATUS InitCommStructurePointers( PDEVICE_OBJECT DeviceObject )
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS nts;

   pDevExt = DeviceObject -> DeviceExtension;
   if ((pDevExt -> pPerfstats = ExAllocatePool
                                (
                                   NonPagedPool,
                                   sizeof( SERIALPERF_STATS )
                                ) ) == NULL)
   {
      nts = STATUS_NO_MEMORY;
   }
   else
   {
      RtlZeroMemory( pDevExt -> pPerfstats, sizeof(SERIALPERF_STATS));
      if ((pDevExt -> pCommProp = ExAllocatePool
                                  (
                                     NonPagedPool,
                                     sizeof( SERIAL_COMMPROP )
                                  )) == NULL)
      {
         _ExFreePool( pDevExt -> pPerfstats );
         nts = STATUS_NO_MEMORY;
      }
      else
      {
         RtlZeroMemory( pDevExt -> pCommProp, sizeof(SERIAL_COMMPROP));
         if ((pDevExt -> pSerialStatus = ExAllocatePool
                                         (
                                            NonPagedPool,
                                            sizeof(SERIAL_STATUS)
                                         )) == NULL)
         {
            _ExFreePool( pDevExt -> pPerfstats );
            _ExFreePool( pDevExt -> pCommProp );
            nts = STATUS_NO_MEMORY;
         }
         else
         {
            RtlZeroMemory( pDevExt -> pSerialStatus, sizeof(SERIAL_STATUS));
            if ((pDevExt -> pSerialHandflow = ExAllocatePool
                                              (
                                                 NonPagedPool,
                                                 sizeof(SERIAL_HANDFLOW)
                                              )) == NULL)
            {
               _ExFreePool( pDevExt -> pSerialStatus );
               _ExFreePool( pDevExt -> pPerfstats );
               _ExFreePool( pDevExt -> pCommProp );
               nts = STATUS_NO_MEMORY;
            }
            else
            {
               RtlZeroMemory
               (
                  pDevExt -> pSerialHandflow,
                  sizeof( SERIAL_HANDFLOW )
               );
               if ((pDevExt -> pSerialTimeouts = ExAllocatePool
                                                 (
                                                    NonPagedPool,
                                                    sizeof(SERIAL_TIMEOUTS)
                                                 )) == NULL)
               {
                  _ExFreePool( pDevExt -> pSerialHandflow );
                  _ExFreePool( pDevExt -> pSerialStatus );
                  _ExFreePool( pDevExt -> pPerfstats );
                  _ExFreePool( pDevExt -> pCommProp );
                  nts = STATUS_NO_MEMORY;
               }
               else
               {
                  RtlZeroMemory
                  (
                     pDevExt -> pSerialTimeouts,
                     sizeof( SERIAL_TIMEOUTS )
                  );
                  if ((pDevExt->pSerialChars = ExAllocatePool
                                               (
                                                  NonPagedPool,
                                                  sizeof(SERIAL_CHARS)
                                               )) == NULL)
                  {
                     _ExFreePool(pDevExt -> pSerialTimeouts);
                     _ExFreePool(pDevExt -> pSerialHandflow);
                     _ExFreePool(pDevExt -> pSerialStatus);
                     _ExFreePool(pDevExt -> pPerfstats);
                     _ExFreePool(pDevExt -> pCommProp);
                     nts = STATUS_NO_MEMORY;
                  }
                  else
                  {
                     RtlZeroMemory
                     (
                        pDevExt -> pSerialChars,
                        sizeof( SERIAL_CHARS )
                     );
                     nts = STATUS_SUCCESS;
                  }
               }
            }
         }
      }
   }
   return nts;
}  // InitCommStructurePointers

NTSTATUS QCSER_StartDataThreads(PDEVICE_EXTENSION pDevExt)
{
   NTSTATUS nts;

   if (pDevExt->ucDeviceType >= DEVICETYPE_CTRL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> (Start) Data threads not available for dev type %u\n", pDevExt->PortName, pDevExt->ucDeviceType)
      );
      return STATUS_UNSUCCESSFUL;
   }

   // resume the interrupt thread in case the thread was
   // stopped by power state change.
   QCINT_InitInterruptPipe(pDevExt->MyDeviceObject);

   // bring up the read/write threads
   nts = QCRD_StartReadThread(pDevExt);
   if (NT_SUCCESS(nts))
   {
      nts = QCWT_StartWriteThread(pDevExt);
   }

   // kick the R/W threads in case the threads have been
   // created and idling. The scenario happens when the
   // port is opened with device disconnected or no device
   // attached.
   KeSetEvent(&pDevExt->L1KickReadEvent, IO_NO_INCREMENT, FALSE);
   KeSetEvent(&pDevExt->KickWriteEvent, IO_NO_INCREMENT, FALSE);

   QCUSB_CDC_SetInterfaceIdle
   (
      pDevExt->MyDeviceObject,
      pDevExt->DataInterface,
      FALSE,
      0
   );

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> Data threads started 0x%x\n", pDevExt->PortName, nts)
   );

   return nts;

}  // QCSER_StartDataThreads

NTSTATUS QCSER_StopDataThreads(PDEVICE_EXTENSION pDevExt, BOOLEAN CancelWaitWake)
{
   if (pDevExt->ucDeviceType >= DEVICETYPE_CTRL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> (stop) Data threads not available for dev type %u\n", pDevExt->PortName, pDevExt->ucDeviceType)
      );
      return STATUS_UNSUCCESSFUL;
   }

   QCUSB_CDC_SetInterfaceIdle
   (
      pDevExt->MyDeviceObject,
      pDevExt->DataInterface,
      TRUE,
      1
   );

   QCRD_L2Suspend(pDevExt);
   QCWT_Suspend(pDevExt);
   StopInterruptService(pDevExt, CancelWaitWake, 6);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> Data threads stopped\n", pDevExt->PortName)
   );

   return STATUS_SUCCESS;

}  // QCSER_StopDataThreads

VOID QCSER_CompleteWaitOnMaskIrp(PDEVICE_EXTENSION pDevExt)
{
   PIRP pWOMIrp;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   QcAcquireSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, &levelOrHandle, irql);

   pWOMIrp = pDevExt->pWaitOnMaskIrp;

   if (pWOMIrp != NULL)
   {
      if (IoSetCancelRoutine(pWOMIrp, NULL) == NULL)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> ERR: WOM null cxlRtn\n", pDevExt->PortName)
         );
         QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);
      }
      else
      {
         pDevExt->pWaitOnMaskIrp = NULL;
         *(ULONG *)(pWOMIrp->AssociatedIrp.SystemBuffer) = 0;
         pWOMIrp->IoStatus.Information = sizeof(ULONG);
         pWOMIrp->IoStatus.Status = STATUS_SUCCESS;

         if (irql == PASSIVE_LEVEL)
         {
            QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);
            QCSER_DbgPrint
            (
               QCSER_DBG_MASK_CIRP,
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> CIRP (Cwom 0x0) 0x%p\n", pDevExt->PortName, pWOMIrp)
            );
            QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pWOMIrp, 0);
            IoCompleteRequest(pWOMIrp, IO_NO_INCREMENT);
         }
         else
         {
            // put WOM Irp onto the completion queue
            InsertTailList(&pDevExt->SglCompletionQueue, &pWOMIrp->Tail.Overlay.ListEntry);
            KeSetEvent(&pDevExt->InterruptEmptySglQueueEvent, IO_NO_INCREMENT, FALSE);
            QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);
         }
      }
   }
   else
   {
      QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);
   }

}  // QCSER_CompleteWaitOnMaskIrp

