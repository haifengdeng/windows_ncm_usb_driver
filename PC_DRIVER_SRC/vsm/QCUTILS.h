/*===========================================================================
FILE: QCUTILS.h

DESCRIPTION:


INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#ifndef QCUTILS_H
#define QCUTILS_H

#include "QCMAIN.h"

ULONG ProcessNewUartState
(
   PDEVICE_EXTENSION pDevExt,
   USHORT usNewUartState,
   USHORT usBitsMask,
   BOOLEAN bHoldWaitIrp
);
NTSTATUS GetValueEntry
(
   HANDLE hKey,
   PWSTR FieldName,
   PKEY_VALUE_FULL_INFORMATION  *pKeyValInfo
);
VOID GetDataField_1( PKEY_VALUE_FULL_INFORMATION pKvi,  PUNICODE_STRING pUs );
VOID DestroyStrings( IN UNICODE_BUFF_DESC Ubd[] );
BOOLEAN InitStrings( UNICODE_BUFF_DESC Ubd[] );
VOID DebugPrintKeyValues(PKEY_VALUE_FULL_INFORMATION pKeyValueInfo);
NTSTATUS getRegValueEntryData
(
   IN HANDLE OpenRegKey,
   IN PWSTR ValueEntryName,
   OUT PUNICODE_STRING pValueEntryData
);
NTSTATUS getRegDwValueEntryData
(
   IN HANDLE OpenRegKey,
   IN PWSTR ValueEntryName,
   OUT PULONG pValueEntryData
);
NTSTATUS dbgPrintUnicodeString(IN PUNICODE_STRING ToPrint, PCHAR label);
ULONG GetDwordVal(HANDLE hKey, WCHAR *Label, ULONG Len);
ULONG GetDwordField( PKEY_VALUE_FULL_INFORMATION pKvi );
USHORT GetSubUnicodeIndex(PUNICODE_STRING SourceString, PUNICODE_STRING SubString);
NTSTATUS AllocateUnicodeString(PUNICODE_STRING pusString, ULONG ulSize, PUCHAR pucTag);

NTSTATUS QCSER_LogData
(
   PDEVICE_EXTENSION pDevExt,
   HANDLE hFile,
   PVOID buffer,
   ULONG length,
   UCHAR type
);
NTSTATUS QCSER_CreateLogs(PDEVICE_EXTENSION pDevExt, int which);
VOID QCSER_GetSystemTimeString(char *ts);
#ifdef QCSER_ENABLE_LOG_REC
VOID QCSER_OutputLatestLogRecords(PDEVICE_EXTENSION pDevExt);
#endif
VOID QCSER_Wait(PDEVICE_EXTENSION pDevExt, LONGLONG WaitTime);
BOOLEAN DeQueueIOBlock
(
   PVXD_WDM_IO_CONTROL_BLOCK pIOBlock,
   PVXD_WDM_IO_CONTROL_BLOCK* pQueueHead
);

VOID QcEmptyCompletionQueue
(
   PDEVICE_EXTENSION pDevExt,
   PLIST_ENTRY QueueToProcess,
   PKSPIN_LOCK pSpinLock,
   int IrpType
);
VOID QCUTILS_CleanupReadWriteQueues(IN PDEVICE_EXTENSION pDevExt);
VOID QCUTILS_FreeReadWriteQueues(IN PDEVICE_EXTENSION pDevExt);

VOID QCUTILS_PMGetRegEntryValues
(
   PDEVICE_EXTENSION pDevExt
);

NTSTATUS QCUTILS_PMSetRegEntry
(
   PDEVICE_EXTENSION pDevExt,
   UCHAR             Index,
   BOOLEAN           Enabled
);

VOID QCUTIL_PrintBytes
(
   PVOID Buf,
   ULONG len,
   ULONG PktLen,
   char *info,
   PDEVICE_EXTENSION x,
   ULONG DbgMask,
   ULONG DbgLevel
);

BOOLEAN QCUTIL_IsHighSpeedDevice(PDEVICE_EXTENSION pDevExt);

#endif // QCUTILS_H
