/*===========================================================================
FILE: QCMGR.h

DESCRIPTION:
   This file contains definitions for USB device management.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2004 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#ifndef QCMGR_H
#define QCMGR_H

#include "QCMAIN.h"

#define MGR_MAX_NUM_DEV 2048
#define MGR_TERMINATE_DSP_EVENT 0
#define MGR_TERMINATE_MGR_EVENT 1
#define MGR_MAX_SIG             2

typedef struct _MGR_DEV_INFO
{
   BOOLEAN  Alive;
   KEVENT   DspStartEvent;
   KEVENT   DspDeviceRetryEvent;
   KEVENT   DspDeviceResetINEvent;
   KEVENT   DspDeviceResetOUTEvent;
   KEVENT   DspStartDataThreadsEvent;
   KEVENT   DspResumeDataThreadsEvent;
   KEVENT   DspResumeDataThreadsAckEvent;
   KEVENT   DspPreWakeUpEvent;

   BOOLEAN  TerminationRequest;
   KEVENT   TerminationOrderEvent;
   KEVENT   TerminationCompletionEvent;
   HANDLE   hDispatchThreadHandle;
   PKTHREAD pDispatchThread;
} MGR_DEV_INFO, *PMGR_DEV_INFO;

extern PMGR_DEV_INFO DeviceInfo;

NTSTATUS QCMGR_StartManagerThread(VOID);
int      QCMGR_RequestManagerElement(VOID);
VOID     QCMGR_SetTerminationRequest(int MgrId);
VOID     ManagerThread(PVOID pContext);
NTSTATUS QCMGR_CancelManagerThread(VOID);
NTSTATUS QCMGR_TerminateDispatchThread(int MgrId);

#endif // QCMGR_H
