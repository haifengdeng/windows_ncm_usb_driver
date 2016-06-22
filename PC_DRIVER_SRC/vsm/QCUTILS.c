/*===========================================================================
FILE: QCUTILS.c

DESCRIPTION:
   This file contains utility functions.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

/*
 * title:      serutils.c
 *
 * purpose:    WDM driver utility functions for USB support of serial devices
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
 * $History: Sysfserutils.c $
 * 
 * *****************  Version 1  *****************
 * User: Edk          Date: 3/27/98    Time: 10:24a
 * Created in $/Host/modem/USB WDM Driver/Generic
 * 
 ****************************************************************************/

#include <stdio.h>
#include "QCMAIN.h"
#include "QCRD.h"
#include "QCSER.h"
#include "QCUTILS.h"
#include "QCPWR.h"
#include "QCWT.h"

// The following protypes are implemented in ntoskrnl.lib
extern NTKERNELAPI VOID ExSystemTimeToLocalTime
(
   IN PLARGE_INTEGER SystemTime,
   OUT PLARGE_INTEGER LocalTime
);

// if bHoldWaitIrp is TRUE, returns pending event;
// otherwise completes wait IRP if any
ULONG ProcessNewUartState
(
   PDEVICE_EXTENSION pDevExt,
   USHORT usNewUartState, 
   USHORT usBitsMask,
   BOOLEAN bHoldWaitIrp
)
{
   USHORT usBitsNew, usBitsOld, usBitsEvent;
   ULONG ulNewEvents;
   PIRP pWOMIrp;
   VOID * pvFormerCancelRoutine;
   UCHAR ucModemStatusOld, ucModemStatusNew;
   BOOLEAN bForceIrpCompletion = (usBitsMask == 0);
   PSERIAL_HANDFLOW pSh = pDevExt->pSerialHandflow;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   KIRQL irql = KeGetCurrentIrql();

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> -->ProcessNewUartState: 0x%x, 0x%x, %u\n", pDevExt->PortName,
        usNewUartState, usBitsMask, bHoldWaitIrp)
   );

   usBitsNew = usBitsOld = pDevExt->usCurrUartState; // bugbug protect with mutex

   // put new state into current state
   usBitsNew &= ~usBitsMask;
   usBitsNew |= (usNewUartState & usBitsMask);
   
   // bCTSfollowsDSR == TRUE
   if (usBitsNew & SERIAL_EV_DSR)
   {
      usBitsNew |= SERIAL_EV_CTS;
   }
   else
   {
      usBitsNew &= ~SERIAL_EV_CTS;
   }

   // check for modem change event bits
   usBitsEvent = (usBitsNew ^ usBitsOld) & US_BITS_MODEM; // changed modem bits

   if (usBitsEvent)
   {
      ucModemStatusOld = pDevExt->ModemStatusReg;
      ucModemStatusNew = 0;

      if (usBitsEvent & SERIAL_EV_CTS)
         ucModemStatusNew |= SERIAL_MSR_DCTS;
      if (usBitsNew & SERIAL_EV_CTS)
         ucModemStatusNew |= SERIAL_MSR_CTS;

      if (usBitsEvent & SERIAL_EV_DSR)
         ucModemStatusNew |= SERIAL_MSR_DDSR;
      if (usBitsNew & SERIAL_EV_DSR)
         ucModemStatusNew |= SERIAL_MSR_DSR;

      if (usBitsEvent & SERIAL_EV_RING)
         ucModemStatusNew |= SERIAL_MSR_TERI;
      if (usBitsNew & SERIAL_EV_RING)
         ucModemStatusNew |= SERIAL_MSR_RI;

      if (usBitsEvent & SERIAL_EV_RLSD)
         ucModemStatusNew |= SERIAL_MSR_DDCD;
      if (usBitsNew & SERIAL_EV_RLSD)
         ucModemStatusNew |= SERIAL_MSR_DCD;

      ASSERT(ucModemStatusOld==pDevExt->ModemStatusReg);
      pDevExt->ModemStatusReg = ucModemStatusNew;
   }

   // check for existing event bits
   usBitsMask = usBitsNew;                // existing events
   usBitsMask &= ~US_BITS_MODEM;          // not including modem events

   usBitsEvent |= usBitsMask;             // new non-modem bits, changed modem bits

   // update current state
   pDevExt->usCurrUartState = usBitsNew;

   // check for flowin control processing
   if (usBitsEvent & SERIAL_EV_PERR) // input buffer full
   {
      pDevExt->usCurrUartState &= ~SERIAL_EV_PERR; // don't actually report it...
      usBitsEvent &= ~SERIAL_EV_PERR;
   }
   
   // return the pending events?
   ulNewEvents = (ULONG) usBitsEvent;
   if (bHoldWaitIrp)
   {
      return ulNewEvents;
   }

   // complete the pending Irp?
   if (!bForceIrpCompletion)
   {
      bForceIrpCompletion = (ulNewEvents & pDevExt->ulWaitMask)?TRUE:FALSE;
   }

   QcAcquireSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, &levelOrHandle, irql);
   pWOMIrp = pDevExt->pWaitOnMaskIrp;

   if (pWOMIrp && bForceIrpCompletion)
   {
      pDevExt->usCurrUartState &= US_BITS_MODEM;
      pvFormerCancelRoutine = IoSetCancelRoutine(pWOMIrp, NULL);
      if (pvFormerCancelRoutine == NULL)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> ERR: WOM null cxlRtn\n", pDevExt->PortName)
         );
      }
      else
      {
         pDevExt->pWaitOnMaskIrp = NULL;
         *(ULONG *)(pWOMIrp -> AssociatedIrp.SystemBuffer) = (ulNewEvents & pDevExt->ulWaitMask);
         pWOMIrp -> IoStatus.Information = sizeof(ULONG); //caller's expecting a long
         pWOMIrp->IoStatus.Status = STATUS_SUCCESS;

         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_DETAIL,
            ("<%s> ProcessNewUartState: complete WOM 0x%p: 0x%x(0x%x, 0x%x)\n",
              pDevExt->PortName, pWOMIrp, (ulNewEvents & pDevExt->ulWaitMask),
              ulNewEvents, pDevExt->ulWaitMask
            )
         );

         /*** Test Code ***
         if ((ulNewEvents & pDevExt->ulWaitMask) != 0)
         {
            KdPrint((" pWaitOnMaskIrp: 0x%x\n", (ulNewEvents & pDevExt->ulWaitMask)));
         }
          *** End of Test Code ***/

         // put WOM Irp onto the completion queue
         InsertTailList(&pDevExt->SglCompletionQueue, &pWOMIrp->Tail.Overlay.ListEntry);
         KeSetEvent(&pDevExt->InterruptEmptySglQueueEvent, IO_NO_INCREMENT, FALSE);
      }
      QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);
      
   }
   else
   {
      QcReleaseSpinLockWithLevel(&pDevExt->SingleIrpSpinLock, levelOrHandle, irql);
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> <--ProcessNewUartState: 0x%x, 0x%x, %u, IrpComp %u\n", pDevExt->PortName,
        usNewUartState, usBitsMask, bHoldWaitIrp, bForceIrpCompletion)
   );

   return 0;
}


NTSTATUS GetValueEntry( HANDLE hKey, PWSTR FieldName, PKEY_VALUE_FULL_INFORMATION  *pKeyValInfo )
{
   PKEY_VALUE_FULL_INFORMATION  pKeyValueInfo = NULL;
   UNICODE_STRING Keyname;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   ULONG ulReturnLength = 0;
   ULONG ulBuffLength = 0; 

   _zeroString( Keyname );

   RtlInitUnicodeString(&Keyname,   FieldName);

   _dbgPrintUnicodeString(&Keyname, "Keyname");
   
   ulBuffLength = sizeof( KEY_VALUE_FULL_INFORMATION );

   while (1)
   {
      pKeyValueInfo = (PKEY_VALUE_FULL_INFORMATION) _ExAllocatePool( 
         NonPagedPool, 
         ulBuffLength,
         "GetValueEntry - 2");     
 
      if ( pKeyValueInfo == NULL )
      {
         *pKeyValInfo = NULL;
         ntStatus = STATUS_NO_MEMORY;
         goto GetValueEntry_Return; 
      }

      ntStatus = ZwQueryValueKey( 
         hKey,
         &Keyname, 
         KeyValueFullInformation, 
         pKeyValueInfo,
         ulBuffLength,
         &ulReturnLength );

      if ( NT_SUCCESS(ntStatus) )
      {
         *pKeyValInfo = pKeyValueInfo;
         break;
      }
      else
      {
         if ( ntStatus == STATUS_BUFFER_OVERFLOW )   
         {
            _freeBuf( pKeyValueInfo );
            ulBuffLength = ulReturnLength;
            continue;
         }
         else
         {
            _freeBuf( pKeyValueInfo );
            *pKeyValInfo = NULL;
            break;
         }
   
      }
   }// while

GetValueEntry_Return:

  return ntStatus;
   
}


VOID GetDataField_1( PKEY_VALUE_FULL_INFORMATION pKvi,  PUNICODE_STRING pUs )

{
 PCHAR  strPtr;
 USHORT len;

 pUs -> Length = 0;
 // strPtr = (char *)((ULONG)pKvi + (ULONG)(pKvi -> DataOffset));
 strPtr = (PCHAR)((PCHAR)pKvi + pKvi->DataOffset);
 len = (USHORT)pKvi -> DataLength;
 RtlCopyMemory( pUs -> Buffer, strPtr, len );
 pUs -> Length = (len - sizeof( WCHAR ));
}


VOID DestroyStrings( IN UNICODE_BUFF_DESC Ubd[] )
{
   short i;
 
   i = 0;
   do
   {
      if (Ubd[i].pUs -> Buffer != NULL)
      {
         _ExFreePool( Ubd[i].pUs -> Buffer );
      }
      Ubd[i].pUs -> Buffer = NULL; //in case this table is passed in twice
      i++;
    } while (Ubd[i].pUs != NULL);
}

BOOLEAN InitStrings( UNICODE_BUFF_DESC Ubd[] )
{
 PUNICODE_BUFF_DESC pUnbufdesc;
 PUNICODE_STRING pUs;
 PVOID pBuf;
 short i;
 
 i = 0;
 do {
     Ubd[i].pUs -> Buffer = NULL;
     i++;
    } while (Ubd[i].pUs != NULL);
 i = 0;
 do {
     pBuf = _ExAllocatePool( NonPagedPool, Ubd[i].usBufsize << 1,"InitStrings()" ); //wchar to bytes
     if (pBuf == NULL)
       {
        DestroyStrings( Ubd );
       return FALSE;
      }
     Ubd[i].pUs -> Buffer = pBuf;
     Ubd[i].pUs -> MaximumLength = Ubd[i].usBufsize << 1;
     Ubd[i].pUs -> Length = 0;
     i++;
    }while (Ubd[i].pUs != NULL);
 return TRUE;
}
     

VOID DebugPrintKeyValues( PKEY_VALUE_FULL_INFORMATION pKeyValueInfo )
{
 PCHAR strPtr;
 ULONG len, i, j;
 char  cBuf[264];
 
 strPtr = (char *)pKeyValueInfo -> Name;    
 len = (pKeyValueInfo -> NameLength) >> 1;
 for (i = j = 0; i < len; i++, j++)
    {
     cBuf[i] = strPtr[j];
     j++;
    }
 cBuf[i] = '\0';    
 // strPtr = (char *)((ULONG)pKeyValueInfo + (ULONG)(pKeyValueInfo -> DataOffset));
 strPtr = (PCHAR)((PCHAR)pKeyValueInfo + pKeyValueInfo->DataOffset);
 len = (pKeyValueInfo -> DataLength) >> 1;
 for (i = j = 0; i < len; i++, j++)
    {
     cBuf[i] = strPtr[j];
     j++;
    }
 cBuf[i] = '\0';    
}

/******************************************************************************************
* Function Name: getRegValueEntryData
* Arguments:
* 
*  IN HANDLE OpenRegKey, 
*  IN PWSTR ValueEntryName,
*  OUT PUNICODE_STRING ValueEntryData
*
* notes:
*   Buffer for valueEntryData is allocated in this function.
*******************************************************************************************/
NTSTATUS getRegValueEntryData
(
   IN HANDLE OpenRegKey, 
   IN PWSTR ValueEntryName,
   OUT PUNICODE_STRING pValueEntryData
)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   PKEY_VALUE_FULL_INFORMATION pKeyValueInfo = NULL;
   UNICODE_STRING ucData;

   _zeroString( ucData );
   _zeroStringPtr( pValueEntryData );

   ntStatus = GetValueEntry( OpenRegKey, ValueEntryName, &pKeyValueInfo );
   
   if ( !NT_SUCCESS( ntStatus ))
   {
      goto getRegValueEntryData_Return;
   }

#ifdef DEBUG_MSGS_KEYINFO
   if (pKeyValueInfo)
      DebugPrintKeyValues( pKeyValueInfo );
#endif

   ucData.Buffer = (PWSTR) _ExAllocatePool( 
         NonPagedPool,
         MAX_ENTRY_DATA_LEN,
         "getRegValueEntryData, ucData.Buffer");    

   if ( ucData.Buffer == NULL )
   {
      ntStatus = STATUS_NO_MEMORY;
      goto getRegValueEntryData_Return;
   }

   ucData.MaximumLength = MAX_ENTRY_DATA_LEN;

   GetDataField_1( pKeyValueInfo, &ucData );

   pValueEntryData -> Buffer = (PWSTR) _ExAllocatePool( 
         NonPagedPool,
         MAX_ENTRY_DATA_LEN,
         "getRegValueEntryData, pValueEntryData -> Buffer");    

   if ( pValueEntryData -> Buffer == NULL )
   {
      ntStatus = STATUS_NO_MEMORY;
      goto getRegValueEntryData_Return;
   }

   pValueEntryData -> MaximumLength = MAX_ENTRY_DATA_LEN;

   _dbgPrintUnicodeString(&ucData, "ucData");
   // _dbgPrintUnicodeString(pValueEntryData, "(before) pValueEntryData");
   RtlCopyUnicodeString(OUT pValueEntryData,IN &ucData);
   _dbgPrintUnicodeString(pValueEntryData, "(after) pValueEntryData");

getRegValueEntryData_Return:

    _freeBuf(pKeyValueInfo);
    _freeUcBuf(ucData);
    return ntStatus;   

}


/******************************************************************************************
* Function Name: getRegDwValueEntryData
* Arguments:
* 
*  IN HANDLE OpenRegKey, 
*  IN PWSTR ValueEntryName,
*  OUT PULONG pValueEntryData
*
*******************************************************************************************/
NTSTATUS getRegDwValueEntryData
(
   IN HANDLE OpenRegKey, 
   IN PWSTR ValueEntryName,
   OUT PULONG pValueEntryData
)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   PKEY_VALUE_FULL_INFORMATION pKeyValueInfo = NULL;

   ntStatus = GetValueEntry( OpenRegKey, ValueEntryName, &pKeyValueInfo );
   
   if (!NT_SUCCESS( ntStatus ))
   {
      goto getRegDwValueEntryData_Return;
   }
               
#ifdef DEBUG_MSGS_KEYINFO
   if (pKeyValueInfo)
      DebugPrintKeyValues( pKeyValueInfo );
#endif

   *pValueEntryData = GetDwordField( pKeyValueInfo );

getRegDwValueEntryData_Return:

    if (pKeyValueInfo)
    {
       _ExFreePool( pKeyValueInfo );
       pKeyValueInfo = NULL;
    }

   return ntStatus;   
}

NTSTATUS dbgPrintUnicodeString(IN PUNICODE_STRING ToPrint, PCHAR label)
{
   NTSTATUS ntStatus = STATUS_SUCCESS;
   ANSI_STRING asToPrint;

   // KdPrint(("Name: %s\n", label));
   // KdPrint(("Length: %u\n", ToPrint->Length));
   // KdPrint(("MaxLength: %u\n", ToPrint->MaximumLength));

   ntStatus = RtlUnicodeStringToAnsiString( 
      &asToPrint,                                   
      ToPrint, 
      TRUE);     

   if ( !NT_SUCCESS( ntStatus ) )
   {
      goto dbgPrintUnicodeString_Return;
   }

   // KdPrint(("<%s>\n", asToPrint.Buffer));

dbgPrintUnicodeString_Return:

   RtlFreeAnsiString(&asToPrint);

   return ntStatus;
}

/************************************************************************
Routine Description:
   Return a ULONG value from an open registry node
      
Arguments:
   HANDLE hKey -- node key
   WCHAR *Label -- name of value entry
      
Returns:
   ULONG value of the value entry
      
************************************************************************/      
ULONG GetDwordVal( HANDLE hKey, WCHAR *Label, ULONG Len )
{
   PKEY_VALUE_FULL_INFORMATION pKeyValueInfo = NULL;
 NTSTATUS ntStatus;
 ULONG dwVal = 0;

 ntStatus = GetValueEntry( hKey, Label, &pKeyValueInfo );
 if (NT_SUCCESS( ntStatus ))
    {
     dwVal = GetDwordField( pKeyValueInfo );
     _ExFreePool( pKeyValueInfo );
    }
 return dwVal;
}
 
ULONG GetDwordField( PKEY_VALUE_FULL_INFORMATION pKvi )
{
 ULONG dwVal, *pVal;
  
 // pVal = (ULONG *)((ULONG)pKvi + (ULONG)(pKvi -> DataOffset));
 pVal = (PULONG)((PCHAR)pKvi + pKvi->DataOffset);
 dwVal = *pVal;
 return dwVal;
}

/*********************************************************************
 *
 * function:   GetSubUnicodeIndex
 *
 * purpose:    search a unicode source string for a unicode sub string
 *              and reture the index into the source string of the sub string
 *             if the sub string is found.  Return zero if not found.  Return
 *             -1 if one or both of the arguments are invalid.
 *
 * arguments:  Source string
 *            Sub string
 *
 * returns:    char index
 *
 */

USHORT GetSubUnicodeIndex(PUNICODE_STRING SourceString, PUNICODE_STRING SubString) 
{
   PWCHAR pSource, pSub;
   USHORT SourceCharIndex, SourceCharLen, SubCharLen, SubIndex;

   SourceCharLen = SourceString->Length >> 1;
   SubCharLen = SubString->Length >> 1;

   if ( (SourceCharLen == 0) || (SubCharLen == 0) ) return 0xFFFF; // invalid args

   SourceCharIndex = 0;
   
   while ( SourceCharIndex + SubCharLen < SourceCharLen )
   {
      pSub = SubString->Buffer;
      pSource = SourceString->Buffer + SourceCharIndex; // init to place in source we
                                               // are testing.
      SubIndex = SubString->Length >> 1;

      while ( (*pSource++ == *pSub++) && SubIndex-- );
      
      if ( !SubIndex ) return SourceCharIndex; // found sub in source because
                                               // subindex went to zero.
      SourceCharIndex++;
   }
   return 0;
} // end of GetSubUnicodeIndex function

NTSTATUS AllocateUnicodeString(PUNICODE_STRING pusString, ULONG ulSize, PUCHAR pucTag)
{
   pusString->Buffer = (PWSTR) _ExAllocatePool( NonPagedPool, ulSize, pucTag );
   if (pusString->Buffer == NULL)
   {
     return STATUS_NO_MEMORY;
   }   
   pusString->MaximumLength = ulSize;
   return STATUS_SUCCESS;
}

#ifdef ENABLE_LOGGING
NTSTATUS QCSER_LogData
(
   PDEVICE_EXTENSION pDevExt,
   HANDLE hFile,
   PVOID buffer,
   ULONG length,
   UCHAR type
)
{
   NTSTATUS        ntStatus = STATUS_SUCCESS;
   LARGE_INTEGER   systemTime;
   IO_STATUS_BLOCK ioStatus;

   if ((pDevExt->EnableLogging == FALSE) || (hFile == NULL))
   {
      return ntStatus;
   }

   // This function must be running at IRQL PASSIVE_LEVEL
   if (KeGetCurrentIrql() > PASSIVE_LEVEL)
   {
      return STATUS_CANCELLED;
   }

   if (type == QCSER_LOG_TYPE_READ)
   {
      if (pDevExt->ulRxLogCount != pDevExt->ulLastRxLogCount)
      {
         type = QCSER_LOG_TYPE_OUT_OF_SEQUENCE_RD;
      }
      pDevExt->ulRxLogCount++;
   }
   else if (type == QCSER_LOG_TYPE_WRITE)
   {
      if (pDevExt->ulTxLogCount != pDevExt->ulLastTxLogCount)
      {
         type = QCSER_LOG_TYPE_OUT_OF_SEQUENCE_WT;
      }
      pDevExt->ulTxLogCount++;
   }
   else if (type == QCSER_LOG_TYPE_RESEND)
   {
      if (pDevExt->ulTxLogCount != pDevExt->ulLastTxLogCount)
      {
         type = QCSER_LOG_TYPE_OUT_OF_SEQUENCE_RS;
      }
      pDevExt->ulTxLogCount++;
   }
      
   KeQuerySystemTime(&systemTime);

   // Log system time
   ntStatus = ZwWriteFile
              (
                 hFile,
                 NULL,  // Event
                 NULL,  // ApcRoutine
                 NULL,  // ApcContext
                 &ioStatus,
                 (PVOID)&systemTime,
                 sizeof(LARGE_INTEGER),
                 NULL,  // ByteOffset
                 NULL
              );
   // log the logging type
   ntStatus = ZwWriteFile
              (
                 hFile,
                 NULL,  // Event
                 NULL,  // ApcRoutine
                 NULL,  // ApcContext
                 &ioStatus,
                 (PVOID)&type,
                 sizeof(UCHAR),
                 NULL,  // ByteOffset
                 NULL
              );
   // log data length
   ntStatus = ZwWriteFile
              (
                 hFile,
                 NULL,  // Event
                 NULL,  // ApcRoutine
                 NULL,  // ApcContext
                 &ioStatus,
                 (PVOID)&length,
                 sizeof(ULONG),
                 NULL,  // ByteOffset
                 NULL
              );

   // log data
   if (length != 0)
   {
      ntStatus = ZwWriteFile
                 (
                    hFile,
                    NULL,  // Event
                    NULL,  // ApcRoutine
                    NULL,  // ApcContext
                    &ioStatus,
                    buffer,
                    length,
                    NULL,  // ByteOffset
                    NULL
                 );
   }

   if (type == QCSER_LOG_TYPE_READ)
   {
      pDevExt->ulLastRxLogCount++;
   }
   else if ((type == QCSER_LOG_TYPE_WRITE) || (type == QCSER_LOG_TYPE_RESEND))
   {
      pDevExt->ulLastTxLogCount++;
   }

   return ntStatus;
}  // QCSER_LogData

NTSTATUS QCSER_CreateLogs(PDEVICE_EXTENSION pDevExt, int which)
{
   NTSTATUS          ntStatus = STATUS_SUCCESS;
   OBJECT_ATTRIBUTES objectAttr;
   IO_STATUS_BLOCK   ioStatus;
   LARGE_INTEGER     systemTime, localTime;
   TIME_FIELDS       timeFields;
   ANSI_STRING       tmpAnsiString, asStr2;
   char              txFileName[48];
   char              rxFileName[48];
   char              *p, cLogPrefix[8];
   UNICODE_STRING    ucTxFileName, ucRxFileName, ucTxTmp, ucRxTmp;

   /* ---------- References ------------
    typedef struct _TIME_FIELDS {
        CSHORT Year;        // range [1601...]
        CSHORT Month;       // range [1..12]
        CSHORT Day;         // range [1..31]
        CSHORT Hour;        // range [0..23]
        CSHORT Minute;      // range [0..59]
        CSHORT Second;      // range [0..59]
        CSHORT Milliseconds;// range [0..999]
        CSHORT Weekday;     // range [0..6] == [Sunday..Saturday]
    } TIME_FIELDS;
    -------------------------------------*/

   if (pDevExt->bLoggingOk == FALSE)
   {
      return STATUS_UNSUCCESSFUL;
   }

   sprintf(cLogPrefix, "\\%s", pDevExt->PortName);

   // If necessary to put a leading '\' to a log file name
   ntStatus = RtlUnicodeStringToAnsiString
              (
                 &asStr2,
                 &pDevExt->ucLoggingDir,
                 TRUE
              );

   if ( ntStatus == STATUS_SUCCESS )
   {
      p = asStr2.Buffer + asStr2.Length - 1;
      if (*p == '\\')
      {
         sprintf(cLogPrefix, "%s", pDevExt->PortName);
      }
   }

   KeQuerySystemTime(&systemTime);
   ExSystemTimeToLocalTime(&systemTime, &localTime);
   RtlTimeToTimeFields(&localTime, &timeFields);

   if ((pDevExt->ucDeviceType > DEVICETYPE_NONE) &&
       (pDevExt->ucDeviceType < DEVICETYPE_INVALID))
   {
      sprintf(
                txFileName,
                "%sTx%02u%02u%02u%02u%02u%03u.log",
                cLogPrefix,
                timeFields.Month,
                timeFields.Day,
                timeFields.Hour,
                timeFields.Minute,
                timeFields.Second,
                timeFields.Milliseconds
             );
      sprintf(
                rxFileName,
                "%sRx%02u%02u%02u%02u%02u%03u.log",
                cLogPrefix,
                timeFields.Month,
                timeFields.Day,
                timeFields.Hour,
                timeFields.Minute,
                timeFields.Second,
                timeFields.Milliseconds
             );
   }

   ntStatus = AllocateUnicodeString
              (
                 &ucTxFileName,
                 MAX_NAME_LEN,
                 "QCSER_CreateLogFile, ucTxFileName.Buffer"
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> NO_MEM - TxFileName\n", pDevExt->PortName)
      );
      RtlFreeAnsiString(&asStr2);
      return ntStatus;
   }
   ntStatus = AllocateUnicodeString
              (
                 &ucRxFileName,
                 MAX_NAME_LEN,
                 "QCSER_CreateLogFile, ucRxFileName.Buffer"
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> NO_MEM - RxFileName\n", pDevExt->PortName)
      );
      RtlFreeAnsiString(&asStr2);
      _freeUcBuf(ucTxFileName); // RtlFreeUnicodeString(&ucTxFileName);
      return ntStatus;
   }

   RtlCopyUnicodeString(OUT &ucTxFileName,IN &pDevExt->ucLoggingDir);
   RtlCopyUnicodeString(OUT &ucRxFileName,IN &pDevExt->ucLoggingDir);

   RtlInitAnsiString(&tmpAnsiString, txFileName);
   ntStatus = RtlAnsiStringToUnicodeString(&ucTxTmp, &tmpAnsiString, TRUE);
   if (ntStatus != STATUS_SUCCESS)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> Error - ucTxTmp\n", pDevExt->PortName)
      );
      RtlFreeAnsiString(&asStr2);
      _freeUcBuf(ucTxFileName); // RtlFreeUnicodeString(&ucTxFileName);
      _freeUcBuf(ucRxFileName); // RtlFreeUnicodeString(&ucRxFileName);
      return ntStatus;
   }

   ntStatus = RtlAppendUnicodeStringToString
              (
                 &ucTxFileName,
                 &ucTxTmp
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_WRITE,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> SMALL_BUF - TxFileName\n", pDevExt->PortName)
      );
      RtlFreeAnsiString(&asStr2);
      RtlFreeUnicodeString(&ucTxTmp);
      _freeUcBuf(ucTxFileName); // RtlFreeUnicodeString(&ucTxFileName);
      _freeUcBuf(ucRxFileName); // RtlFreeUnicodeString(&ucRxFileName);
      return ntStatus;
   }
   RtlFreeUnicodeString(&ucTxTmp);

   RtlInitAnsiString(&tmpAnsiString, rxFileName);
   ntStatus = RtlAnsiStringToUnicodeString(&ucRxTmp, &tmpAnsiString, TRUE);
   if (ntStatus != STATUS_SUCCESS)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> Error - ucRxTmp\n", pDevExt->PortName)
      );
      RtlFreeAnsiString(&asStr2);
      RtlFreeUnicodeString(&ucTxTmp);
      _freeUcBuf(ucTxFileName); // RtlFreeUnicodeString(&ucTxFileName);
      _freeUcBuf(ucRxFileName); // RtlFreeUnicodeString(&ucRxFileName);
      return ntStatus;
   }
   ntStatus = RtlAppendUnicodeStringToString
              (
                 &ucRxFileName,
                 &ucRxTmp
              );
   if (ntStatus != STATUS_SUCCESS)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> SMALL_BUF - RxFileName\n", pDevExt->PortName)
      );
      RtlFreeAnsiString(&asStr2);
      RtlFreeUnicodeString(&ucTxTmp);
      RtlFreeUnicodeString(&ucRxTmp);
      _freeUcBuf(ucTxFileName); // RtlFreeUnicodeString(&ucTxFileName);
      _freeUcBuf(ucRxFileName); // RtlFreeUnicodeString(&ucRxFileName);
      return ntStatus;
   }
   RtlFreeUnicodeString(&ucRxTmp);

   _dbgPrintUnicodeString(&ucTxFileName, "ucTxFileName");
   _dbgPrintUnicodeString(&ucRxFileName, "ucRxFileName");

   if ((which == QCSER_CREATE_TX_LOG) && (pDevExt->hTxLogFile == NULL))
   {
      pDevExt->ulLastTxLogCount = pDevExt->ulTxLogCount = 0;

      // Create disk log files
      InitializeObjectAttributes
      (
         &objectAttr,
         &ucTxFileName,
         OBJ_CASE_INSENSITIVE,
         NULL,
         NULL
      );

      if (pDevExt->LoggingWriteThrough == FALSE)
      {
         ntStatus = ZwCreateFile
                    (
                       &pDevExt->hTxLogFile,
                       FILE_GENERIC_WRITE,
                       &objectAttr,
                       &ioStatus,
                       0,                                  // AllocationSize
                       FILE_ATTRIBUTE_NORMAL,              // FileAttributes
                       FILE_SHARE_READ | FILE_SHARE_WRITE, // ShareAccess
                       FILE_OPEN_IF,                       // CreateDisposition
                       FILE_SYNCHRONOUS_IO_NONALERT,       // CreateOptions
                       NULL,
                       0
                    );
      }
      else
      {
         ntStatus = ZwCreateFile
                    (
                       &pDevExt->hTxLogFile,
                       (STANDARD_RIGHTS_WRITE | FILE_WRITE_DATA |
                          FILE_WRITE_ATTRIBUTES | SYNCHRONIZE),
                       &objectAttr,
                       &ioStatus,
                       0,                                  // AllocationSize
                       FILE_ATTRIBUTE_NORMAL,              // FileAttributes
                       FILE_SHARE_READ | FILE_SHARE_WRITE, // ShareAccess
                       FILE_OPEN_IF,                       // CreateDisposition
                       (FILE_SYNCHRONOUS_IO_NONALERT |
                          FILE_NO_INTERMEDIATE_BUFFERING), // CreateOptions
                       NULL,
                       0
                    );
      }

      if (ntStatus != STATUS_SUCCESS)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_WRITE,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> File creation failure: TxFileName\n", pDevExt->PortName)
         );
        pDevExt->hTxLogFile = NULL;
      }
   }

   if ((which == QCSER_CREATE_RX_LOG) && (pDevExt->hRxLogFile == NULL))
   {
      pDevExt->ulLastRxLogCount = pDevExt->ulRxLogCount = 0;

      InitializeObjectAttributes
      (
         &objectAttr,
         &ucRxFileName,
         OBJ_CASE_INSENSITIVE,
         NULL,
         NULL
      );

      if (pDevExt->LoggingWriteThrough == FALSE)
      {
         ntStatus = ZwCreateFile
                    (
                       &pDevExt->hRxLogFile,
                       FILE_GENERIC_WRITE,
                       &objectAttr,
                       &ioStatus,
                       0,                     // AllocationSize
                       FILE_ATTRIBUTE_NORMAL, // FileAttributes
                       FILE_SHARE_READ | FILE_SHARE_WRITE,  // ShareAccess
                       FILE_OPEN_IF,          // CreateDisposition
                       FILE_SYNCHRONOUS_IO_NONALERT, // CreateOptions
                       NULL,
                       0
                    );
      }
      else
      {
         ntStatus = ZwCreateFile
                    (
                       &pDevExt->hRxLogFile,
                       (STANDARD_RIGHTS_WRITE | FILE_WRITE_DATA |
                          FILE_WRITE_ATTRIBUTES | SYNCHRONIZE),
                       &objectAttr,
                       &ioStatus,
                       0,                                  // AllocationSize
                       FILE_ATTRIBUTE_NORMAL,              // FileAttributes
                       FILE_SHARE_READ | FILE_SHARE_WRITE, // ShareAccess
                       FILE_OPEN_IF,                       // CreateDisposition
                       (FILE_SYNCHRONOUS_IO_NONALERT |
                          FILE_NO_INTERMEDIATE_BUFFERING), // CreateOptions
                       NULL,
                       0
                    );
      }

      if (ntStatus != STATUS_SUCCESS)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> File creation failure: RxFileName\n", pDevExt->PortName)
         );
        pDevExt->hRxLogFile = NULL;
      }
   }

   // write header of QCUSB_DRIVER_VERSION and max transaction size

   RtlFreeAnsiString(&asStr2);
   _freeUcBuf(ucTxFileName); // RtlFreeUnicodeString(&ucTxFileName);
   _freeUcBuf(ucRxFileName); // RtlFreeUnicodeString(&ucRxFileName);

   return ntStatus;

}  // QCSER_CreateLogs

#endif // ENABLE_LOGGING

VOID QCSER_GetSystemTimeString(char *ts)
{
   LARGE_INTEGER systemTime, localTime;
   TIME_FIELDS   timeFields;

   KeQuerySystemTime(&systemTime);
   ExSystemTimeToLocalTime(&systemTime, &localTime);
   RtlTimeToTimeFields(&localTime, &timeFields);

   sprintf(
             ts,
             "%02u/%02u/%02u:%02u:%02u:%03u",
             timeFields.Month,
             timeFields.Day,
             timeFields.Hour,
             timeFields.Minute,
             timeFields.Second,
             timeFields.Milliseconds
          );
}

#ifdef QCSER_ENABLE_LOG_REC

VOID QCSER_OutputLatestLogRecords(PDEVICE_EXTENSION pDevExt)
{
   ULONG i, j, pktLen;
   char dataBuffer[255];
   USHORT cnt;

   KdPrint(("\t ----- LATETST %d TRANSFER Records -----\n", NUM_LATEST_PKTS));
   for (i = 0; i < NUM_LATEST_PKTS; i++)
   {
      KdPrint(("\t ----- Record %d -----\n", i));
      KdPrint(("\t TX_RECORD: [%s] (%u bytes)\n",
                pDevExt->TxLogRec[i].TimeStamp,
                pDevExt->TxLogRec[i].PktLength));
      pktLen = (pDevExt->TxLogRec[i].PktLength > 64? 64: pDevExt->TxLogRec[i].PktLength);
      dataBuffer[0] = 0;
      cnt = 0;
      for (j = 0; j < pktLen; j++)
      {
         sprintf(dataBuffer+strlen(dataBuffer), " %02X", (UCHAR)pDevExt->TxLogRec[i].Data[j]);
         if (++cnt == 16)
         {
            sprintf(dataBuffer+strlen(dataBuffer), "\n");
            cnt = 0;
         }
      }
      KdPrint(("%s\n", dataBuffer));

      KdPrint(("\t RX_RECORD: [%s] (%u bytes)\n",
                pDevExt->RxLogRec[i].TimeStamp,
                pDevExt->RxLogRec[i].PktLength));
      pktLen = (pDevExt->RxLogRec[i].PktLength > 64? 64: pDevExt->RxLogRec[i].PktLength);
      dataBuffer[0] = 0;
      cnt = 0;
      for (j = 0; j < pktLen; j++)
      {
         sprintf(dataBuffer+strlen(dataBuffer), " %02X", (UCHAR)pDevExt->RxLogRec[i].Data[j]);
         if (++cnt == 16)
         {
            sprintf(dataBuffer+strlen(dataBuffer), "\n");
            cnt = 0;
         }
      }
      KdPrint(("%s\n", dataBuffer));
   }
}

#endif // QCSER_ENABLE_LOG_REC

VOID QCSER_Wait(PDEVICE_EXTENSION pDevExt, LONGLONG WaitTime)
{
   LARGE_INTEGER delayValue;

   delayValue.QuadPart = WaitTime;
   KeClearEvent(&pDevExt->ForTimeoutEvent);
   KeWaitForSingleObject
   (
      &pDevExt->ForTimeoutEvent,
      Executive,
      KernelMode,
      FALSE,
      &delayValue
   );
   KeClearEvent(&pDevExt->ForTimeoutEvent);

   return;
}

BOOLEAN DeQueueIOBlock
(
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock,
   PVXD_WDM_IO_CONTROL_BLOCK* pQueueHead
)
{
   PVXD_WDM_IO_CONTROL_BLOCK pCurrIOBlock = *pQueueHead;

   if (pCurrIOBlock == pIOBlock)
   {
      *pQueueHead = pIOBlock->pNextEntry;
      return TRUE;
   }
   while (pCurrIOBlock)
   {
      if (pCurrIOBlock->pNextEntry == pIOBlock)
      {
         pCurrIOBlock->pNextEntry = pIOBlock->pNextEntry;
         return TRUE;
      }
      pCurrIOBlock = pCurrIOBlock->pNextEntry;
   }
   return FALSE;
}  // DeQueueIOBlock

VOID QcIoCompleteRequestB(PIRP Irp, CCHAR PriorityBoost)
{
   IoCompleteRequest(Irp, PriorityBoost);
} 

VOID QcEmptyCompletionQueue
(
   PDEVICE_EXTENSION pDevExt,
   PLIST_ENTRY QueueToProcess,
   PKSPIN_LOCK pSpinLock,
   int IrpType
)
{
   PIRP pIrp;
   PLIST_ENTRY headOfList;
   BOOLEAN bComListEmpty;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif
   ULONG debugLevel;

   while (TRUE)
   {
      QcAcquireSpinLock(pSpinLock, &levelOrHandle);
      if (!IsListEmpty(QueueToProcess))
      {
         headOfList = RemoveHeadList(QueueToProcess);
         pIrp = CONTAINING_RECORD
                (
                   headOfList,
                   IRP,
                   Tail.Overlay.ListEntry
                );
         bComListEmpty = IsListEmpty(QueueToProcess);
         QcReleaseSpinLock(pSpinLock, levelOrHandle);

         if (NT_SUCCESS(pIrp->IoStatus.Status))
         {
            debugLevel = QCSER_DBG_LEVEL_DETAIL;
         }
         else
         {
            debugLevel = QCSER_DBG_LEVEL_ERROR;
         }

         switch (IrpType)
         {
            case QCUSB_IRP_TYPE_CIRP:
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_CIRP,
                  debugLevel,
                  ("<%s> CIRP (Cq 0x%x) 0x%p <%ld/%ld/%ld>\n",
                    pDevExt->PortName, pIrp->IoStatus.Status, pIrp,
                    pDevExt->Sts.lRmlCount[0], pDevExt->Sts.lAllocatedCtls,
                    pDevExt->Sts.lAllocatedDSPs) 
               );
               break;
            }
            case QCUSB_IRP_TYPE_RIRP:
            {
               InterlockedDecrement(&(pDevExt->NumIrpsToComplete));
               if (pDevExt->NumIrpsToComplete < 0)
               {
                  QCSER_DbgPrint
                  (
                     QCSER_DBG_MASK_READ,
                     QCSER_DBG_LEVEL_ERROR,
                     ("<%s> UTL: err - negative NumIrpsToComplete %d\n",
                       pDevExt->PortName, pDevExt->NumIrpsToComplete) 
                  );
                  pDevExt->NumIrpsToComplete = 0;
               }
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_RIRP,
                  debugLevel,
                  ("<%s> RIRP (Cq 0x%x/%ldB) 0x%p (%ld) <%ld/%ld/%ld>\n",
                    pDevExt->PortName, pIrp->IoStatus.Status,
                    pIrp->IoStatus.Information, pIrp, pDevExt->NumIrpsToComplete,
                    pDevExt->Sts.lRmlCount[1], pDevExt->Sts.lAllocatedReads,
                    pDevExt->Sts.lAllocatedRdMem) 
               );
               break;
            }
            case QCUSB_IRP_TYPE_WIRP:
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_WIRP,
                  debugLevel,
                  ("<%s> WIRP (Cq 0x%x/%ldB) 0x%p <%ld/%ld/%ld>\n", 
                    pDevExt->PortName, pIrp->IoStatus.Status, pIrp->IoStatus.Information,
                    pIrp, pDevExt->Sts.lRmlCount[2], pDevExt->Sts.lAllocatedWrites,
                    pDevExt->Sts.lAllocatedWtMem) 
               );
               break;
            }
            default:
            {
               QCSER_DbgPrint
               (
                  QCSER_DBG_MASK_CONTROL,
                  debugLevel,
                  ("<%s> UIRP (Cq 0x%x) 0x%p\n", pDevExt->PortName, pIrp->IoStatus.Status, pIrp) 
               );
               break;
            }
         } // switch

         QcIoReleaseRemoveLock(pDevExt->pRemoveLock, pIrp, IrpType);

         if ((pDevExt->Sts.lRmlCount[IrpType] > 0) && 
             ((IrpType > QCUSB_IRP_TYPE_CIRP) || (!bComListEmpty)) )
         {
            QCSER_DbgPrint
            (
               (QCSER_DBG_MASK_CONTROL | QCSER_DBG_MASK_WIRP | QCSER_DBG_MASK_RIRP),
               QCSER_DBG_LEVEL_DETAIL,
               ("<%s> IRP 0x%p (type %d) boost [%d,%d]\n", pDevExt->PortName,
                 pIrp, IrpType, pDevExt->Sts.lRmlCount[IrpType], bComListEmpty)
            );
            QcIoCompleteRequestB(pIrp, IO_SERIAL_INCREMENT);
         }
         else
         {
            QcIoCompleteRequestB(pIrp, IO_NO_INCREMENT);
         }
      }
      else
      {
         QcReleaseSpinLock(pSpinLock, levelOrHandle);
         break;
      }
   } // while

   if (IrpType == QCUSB_IRP_TYPE_RIRP)
   {
      KeSetEvent
      (
         &pDevExt->ReadCompletionThrottleEvent,
         IO_NO_INCREMENT,
         FALSE
      );
   }

}  // QcEmptyCompletionQueue

VOID QCUTILS_CleanupReadWriteQueues(IN PDEVICE_EXTENSION pDevExt)
{
   PVXD_WDM_IO_CONTROL_BLOCK pQueEntry;
   PVXD_WDM_IO_CONTROL_BLOCK pNextEntry;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCSER_DbgPrint2
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> _CleanupReadWriteQueues: enter\n", pDevExt->PortName)
   );

   // clean read/write queue
   QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);
   pQueEntry = pDevExt->pReadHead;
   while (pQueEntry != NULL)
   {
      pNextEntry = pQueEntry->pNextEntry;
      pDevExt->pReadHead = pNextEntry;
      pQueEntry->ntStatus = STATUS_DELETE_PENDING;
      pQueEntry->pCompletionRoutine(pQueEntry, FALSE, 9);
      QcExFreeReadIOB(pQueEntry, FALSE);
      pQueEntry = pNextEntry;
   }
   QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);

   QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
   pQueEntry = pDevExt->pWriteHead;
   while (pQueEntry != NULL)
   {
      pNextEntry = pQueEntry->pNextEntry;
      pDevExt->pWriteHead = pNextEntry;
      pQueEntry->ntStatus = STATUS_DELETE_PENDING;
      pQueEntry->pCompletionRoutine(pQueEntry, FALSE, 9);
      QcExFreeWriteIOB(pQueEntry, FALSE);
      pQueEntry = pNextEntry;
   }
   QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

   QCSER_DbgPrint2
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> _CleanupReadWriteQueues: exit\n", pDevExt->PortName)
   );
}  // QCUTILS_CleanupReadWriteQueues

VOID QCUTILS_FreeReadWriteQueues(IN PDEVICE_EXTENSION pDevExt)
{
   PVXD_WDM_IO_CONTROL_BLOCK pQueEntry;
   PLIST_ENTRY headOfList;
   #ifdef QCSER_TARGET_XP
   KLOCK_QUEUE_HANDLE levelOrHandle;
   #else
   KIRQL levelOrHandle;
   #endif

   QCSER_DbgPrint2
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> _FreeReadWriteQueues: enter\n", pDevExt->PortName)
   );

   // clean read/write queue
   QcAcquireSpinLock(&pDevExt->ReadSpinLock, &levelOrHandle);
   while (!IsListEmpty(&pDevExt->ReadFreeQueue))
   {
      headOfList = RemoveHeadList(&pDevExt->ReadFreeQueue);
      pQueEntry = CONTAINING_RECORD
                  (
                     headOfList,
                     VXD_WDM_IO_CONTROL_BLOCK,
                     List
                  );
      ExFreePool(pQueEntry);
      InterlockedDecrement(&(pDevExt->Sts.lAllocatedRdMem));
   }
   QcReleaseSpinLock(&pDevExt->ReadSpinLock, levelOrHandle);

   QcAcquireSpinLock(&pDevExt->WriteSpinLock, &levelOrHandle);
   while (!IsListEmpty(&pDevExt->WriteFreeQueue))
   {
      headOfList = RemoveHeadList(&pDevExt->WriteFreeQueue);
      pQueEntry = CONTAINING_RECORD
                  (
                     headOfList,
                     VXD_WDM_IO_CONTROL_BLOCK,
                     List
                  );
      ExFreePool(pQueEntry);
      InterlockedDecrement(&(pDevExt->Sts.lAllocatedWtMem));
   }
   QcReleaseSpinLock(&pDevExt->WriteSpinLock, levelOrHandle);

   QCSER_DbgPrint2
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> _FreeReadWriteQueues: exit\n", pDevExt->PortName)
   );
}  // QCUTILS_FreeReadWriteQueues

VOID QCUTILS_PMGetRegEntryValues
(
   PDEVICE_EXTENSION pDevExt
)
{
   OBJECT_ATTRIBUTES oa;
   HANDLE            hRegKey = NULL;
   ULONG             data;
   UNICODE_STRING    ucValueName;
   NTSTATUS          ntStatus;
   ULONG             selectiveSuspendIdleTime = 0;

   InitializeObjectAttributes(&oa, &gServicePath, 0, NULL, NULL);

   ntStatus = ZwOpenKey
              (
                 &hRegKey,
                 KEY_READ,
                 &oa
              );
   if (!NT_SUCCESS(ntStatus))
   {
      _closeRegKey(hRegKey, "Ur-0");
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> UPM: reg srv ERR-0\n", pDevExt->PortName)
      );
      pDevExt->PowerManagementEnabled = QCUTIL_IsHighSpeedDevice(pDevExt);
   }
   else
   {
      // PowerManagementEnabled
      RtlInitUnicodeString(&ucValueName, QCPWR_REG_NAME_PMENABLED);
      ntStatus = getRegDwValueEntryData
                 (
                    hRegKey,
                    ucValueName.Buffer,
                    &data
                 );
      if (ntStatus == STATUS_SUCCESS)
      {
         pDevExt->PowerManagementEnabled = (data != 0);
      }
      else
      {
         pDevExt->PowerManagementEnabled = QCUTIL_IsHighSpeedDevice(pDevExt);
      }
      _closeRegKey(hRegKey, "Ur-1");
   }

   // init selective suspension and no wait-wake
   // both variables are to be determined later in the INT thread
   pDevExt->WaitWakeEnabled = FALSE;
   pDevExt->SelectiveSuspendIdleTime = 0;

   /*****  WaitWakeEnabled now depends on PowerManagementEnabled
   // WaitWakeEnabled
   RtlInitUnicodeString(&ucValueName, QCPWR_REG_NAME_WWENABLED);
   ntStatus = getRegDwValueEntryData
              (
                 hRegKey,
                 ucValueName.Buffer,
                 &data
              );
   if (ntStatus == STATUS_SUCCESS)
   {
      pDevExt->WaitWakeEnabled = (data != 0);
   }
   else
   {
      pDevExt->WaitWakeEnabled = QCUTIL_IsHighSpeedDevice(pDevExt);
   }

   _closeRegKey(hRegKey, "Ur-1");
   *****/

}  // QCUTILS_PMGetRegEntryValues

NTSTATUS QCUTILS_PMSetRegEntry
(
   PDEVICE_EXTENSION pDevExt,
   UCHAR             Index,
   BOOLEAN           IsEnabled
)
{
   OBJECT_ATTRIBUTES oa;
   HANDLE            hRegKey = NULL;
   ULONG             data;
   UNICODE_STRING    ucValueName;
   NTSTATUS          ntStatus;

   InitializeObjectAttributes(&oa, &gServicePath, 0, NULL, NULL);

   ntStatus = ZwOpenKey
              (
                 &hRegKey,
                 KEY_READ,
                 &oa
              );
   if (!NT_SUCCESS(ntStatus))
   {
      _closeRegKey(hRegKey, "U-0");
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_READ,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> UPM: reg srv ERR\n", pDevExt->PortName)
      );
      return ntStatus;
   }

   switch (Index)
   {
      case QCPWR_WMI_POWER_DEVICE_ENABLE:
      {
         RtlInitUnicodeString(&ucValueName, QCPWR_REG_NAME_PMENABLED);
         data = IsEnabled;
         break;
      }
      case QCPWR_WMI_POWER_DEVICE_WAKE_ENABLE:
      {
         RtlInitUnicodeString(&ucValueName, QCPWR_REG_NAME_WWENABLED);
         data = IsEnabled;
         break;
      }
      default:
      {
         ntStatus = STATUS_NOT_SUPPORTED;
         break;
      }
   }

   if (NT_SUCCESS(ntStatus))
   {
      ZwSetValueKey
      (
         hRegKey,
         &ucValueName,
         0,
         REG_DWORD,
         (PVOID)&data,
         sizeof(ULONG)
      );
   }

   _closeRegKey(hRegKey, "U-1");

   return ntStatus;

}  // QCUTILS_PMSetRegEntry


VOID QCUTIL_PrintBytes
(
   PVOID Buf,
   ULONG len,
   ULONG PktLen,
   char *info,
   PDEVICE_EXTENSION x,
   ULONG DbgMask,
   ULONG DbgLevel
)
{
   ULONG nWritten;
   char  *buf, *p, *cBuf, *cp;
   char *buffer;
   ULONG count = 0, lastCnt = 0, spaceNeeded;
   ULONG i, j, s;
   ULONG nts;
   PCHAR dbgOutputBuffer;
   ULONG myTextSize = 1280;

   #define SPLIT_CHAR '|'

   // re-calculate text buffer size
   if (myTextSize < (len * 5 +360))
   {
      myTextSize = len * 5 +360;
   }

   buffer = (char *)Buf;

   dbgOutputBuffer = ExAllocatePool(NonPagedPool, myTextSize);
   if (dbgOutputBuffer == NULL)
   {
      return;
   }

   RtlZeroMemory(dbgOutputBuffer, myTextSize);
   cBuf = dbgOutputBuffer;
   buf  = dbgOutputBuffer + 128;
   p    = buf;
   cp   = cBuf;

   if (PktLen < len)
   {
      len = PktLen;
   }

   sprintf(p, "\r\n\t   --- <%s> DATA %u/%u BYTES ---\r\n", info, len, PktLen);
   p += strlen(p);

   for (i = 1; i <= len; i++)
   {
      if (i % 16 == 1)
      {
         sprintf(p, "  %04u:  ", i-1);
         p += 9;
      }

      sprintf(p, "%02X ", (UCHAR)buffer[i-1]);
      if (isprint(buffer[i-1]) && (!isspace(buffer[i-1])))
      {
         sprintf(cp, "%c", buffer[i-1]);
      }
      else
      {
         sprintf(cp, ".");
      }

      p += 3;
      cp += 1;

      if ((i % 16) == 8)
      {
         sprintf(p, "  ");
         p += 2;
      }

      if (i % 16 == 0)
      {
         if (i % 64 == 0)
         {
            sprintf(p, " %c  %s\r\n\r\n", SPLIT_CHAR, cBuf);
         }
         else
         {
            sprintf(p, " %c  %s\r\n", SPLIT_CHAR, cBuf);
         }
         QCSER_DbgPrintX
         (
            x,
            DbgMask,
            DbgLevel,
            (buf)
         );
         RtlZeroMemory(dbgOutputBuffer, myTextSize);
         p = buf;
         cp = cBuf;
      }
   }

   lastCnt = i % 16;

   if (lastCnt == 0)
   {
      lastCnt = 16;
   }

   if (lastCnt != 1)
   {
      // 10 + 3*8 + 2 + 3*8 = 60 (full line bytes)
      spaceNeeded = (16 - lastCnt + 1) * 3;
      if (lastCnt <= 8)
      {
         spaceNeeded += 2;
      }
      for (s = 0; s < spaceNeeded; s++)
      {
         sprintf(p++, " ");
      }
      sprintf(p, " %c  %s\r\n\t   --- <%s> END OF DATA BYTES(%u/%uB) ---\n",
              SPLIT_CHAR, cBuf, info, len, PktLen);
      QCSER_DbgPrintX
      (
         x,
         DbgMask,
         DbgLevel,
         (buf)
      );
   }
   else
   {
      sprintf(buf, "\r\n\t   --- <%s> END OF DATA BYTES(%u/%uB) ---\n", info, len, PktLen);
      QCSER_DbgPrintX
      (
         x,
         DbgMask,
         DbgLevel,
         (buf)
      );
   }

   ExFreePool(dbgOutputBuffer);

}  //QCUTIL_PrintBytes

BOOLEAN QCUTIL_IsHighSpeedDevice(PDEVICE_EXTENSION pDevExt)
{
   return ((pDevExt->HighSpeedUsbOk == QC_HS_USB_OK)  ||
           (pDevExt->HighSpeedUsbOk == QC_HS_USB_OK2) ||
           (pDevExt->HighSpeedUsbOk == QC_HS_USB_OK3));
}  // QCUTIL_IsHighSpeedDevice
