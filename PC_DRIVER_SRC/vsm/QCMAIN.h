/*===========================================================================
FILE: QCMAIN.h

DESCRIPTION:


INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003-2005 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#ifndef QCMAIN_H

#define QCMAIN_H

// --- Important compilation flags: defined in SOURCES.FRE and SOURCES.CHK ---
// CHECKED_SHIPPABLE
// ENABLE_LOGGING
// QCSER_ENABLE_LOG_REC
// QCSER_DBGPRINT
// QCSER_DBGPRINT2
// QCSER_TARGET_XP

#include <initguid.h>
#include <ntddk.h>
#include <usbdi.h>
#include <usbdlib.h>
#include <ntddser.h>
#include <wdmguid.h>
#include <wmilib.h>
#include <wmistr.h>
#include "QCUSB.h"

#define QCUSB_STACK_IO_ON
#define QCUSB_MULTI_READS
#define QCUSB_MULTI_WRITES
#define QCUSB_FC

// Definitions for USB CDC
#define USB_CDC_CS_INTERFACE 0x24
#define USB_CDC_FD_MDLM      0x12 // Mobile Direct Line Model Functional Descriptor
#define USB_CDC_FD_MDLMD     0x13 // MDLM Detail Functional Descriptor
#define USB_CDC_ACM_FD       0x02 // ACM Functional Descriptor

#define USB_CDC_ACM_SET_COMM_FEATURE_BIT_MASK 0x01

#define QC_PNP_IDLE     0
#define QC_PNP_ADDING   1
#define QC_PNP_REMOVING 2

// Added this to support UNDP version numbers
#if !defined(QCUSB_DRIVER_VERSION_L) || !defined(QCUSB_DRIVER_VERSION)

// Constant definition -- hguo, Qualcomm Inc.
#if defined(QCSER_VERSION_W2K_FRE)
   #define QCUSB_DRIVER_VERSION_L  L"1.1.0.0"
   #define QCUSB_DRIVER_VERSION     "1.1.0.0"
#elif defined(QCSER_VERSION_W2K_CHK)
   #define QCUSB_DRIVER_VERSION_L  L"2.0.5.3d"
   #define QCUSB_DRIVER_VERSION     "2.0.5.3d"
#elif defined(QCSER_VERSION_WXP_FRE)
   #define QCUSB_DRIVER_VERSION_L  L"1.1.0.0"
   #define QCUSB_DRIVER_VERSION     "1.1.0.0"
#elif defined(QCSER_VERSION_WXP_CHK)
   #define QCUSB_DRIVER_VERSION_L  L"2.0.5.3d"
   #define QCUSB_DRIVER_VERSION     "2.0.5.3d"
#else
   #define QCUSB_DRIVER_VERSION_L  L"1.1.0.0"
   #define QCUSB_DRIVER_VERSION     "1.1.0.0"
#endif

#endif

#ifndef GUID_DEVINTERFACE_MODEM 
   DEFINE_GUID(GUID_DEVINTERFACE_MODEM,0x2c7089aa, 0x2e0e,0x11d1,0xb1, 0x14, 0x00, 0xc0, 0x4f, 0xc2, 0xaa, 0xe4);
#endif
#ifndef GUID_DEVINTERFACE_PORTS 
DEFINE_GUID(GUID_DEVINTERFACE_PORTS, 0x86e0d1e0L, 0x8089, 0x11d0, 0x9c, 0xe4, 0x08, 0x00, 0x3e, 0x30, 0x1f, 0x73);
#endif

 
#define QCUSB_MAX_MRW_BUF_COUNT   16 // do not use larger value!
#define QCUSB_MAX_PIPE_XFER_SIZE 4096
#define QCSER_RD_COMPLETION_THROTTLE_MIN 3
#define QCSER_RD_COMP_THROTTLE_START     50
#define QCSER_RECEIVE_BUFFER_SIZE    4096L
#define USB_WRITE_UNIT_SIZE          256L
#define QCSER_NUM_OF_LEVEL2_BUF      6
#define QCSER_BYTE_STUFFING_BUF_SIZE 4096 // initial buffer size default to 4K
#define QCSER_STUFFING_BYTE          0xFF
#define QCUSB_DRIVER_GUID_DATA_STR "{87E5A6EA-D48B-4883-8440-81D8A22508D7}:DATA"
#define QCUSB_DRIVER_GUID_DIAG_STR "{87E5A6EA-D48B-4883-8440-81D8A22508D7}:DIAG"
#define QCUSB_DRIVER_GUID_UNKN_STR "{87E5A6EA-D48B-4883-8440-81D8A22508D7}:UNKNOWN"

// define an internal read buffer
#define USB_INTERNAL_READ_BUFFER_SIZE      20*4096 
#define USB_INTERNAL_128K_READ_BUFFER_SIZE 32*4096 
#define USB_INTERNAL_256K_READ_BUFFER_SIZE 64*4096 

// thread priority
#define QCSER_INT_PRIORITY 27
#define QCSER_L2_PRIORITY  26
#define QCSER_L1_PRIORITY  25
#define QCSER_WT_PRIORITY  25
#define QCSER_DSP_PRIORITY 24

#define REMOTE_WAKEUP_MASK 0x20  // bit 5

// define number of best effort retries on error
#define BEST_RETRIES 500
#define BEST_RETRIES_MIN 6
#define BEST_RETRIES_MAX 1000

#define QCSER_CREATE_TX_LOG   0
#define QCSER_CREATE_RX_LOG   1

#define QCSER_LOG_TYPE_READ               0x0
#define QCSER_LOG_TYPE_WRITE              0x1
#define QCSER_LOG_TYPE_RESEND             0x2
#define QCSER_LOG_TYPE_RESPONSE_RD        0x3
#define QCSER_LOG_TYPE_RESPONSE_WT        0x4
#define QCSER_LOG_TYPE_OUT_OF_SEQUENCE_RD 0x5  // read
#define QCSER_LOG_TYPE_OUT_OF_SEQUENCE_WT 0x6  // write
#define QCSER_LOG_TYPE_OUT_OF_SEQUENCE_RS 0x7  // resent
#define QCSER_LOG_TYPE_ADD_READ_REQ       0x8  // add read IRP
#define QCSER_LOG_TYPE_THREAD_END         0x9
#define QCSER_LOG_TYPE_CANCEL_THREAD      0xA

#define QCUSB_IRP_TYPE_CIRP 0x0
#define QCUSB_IRP_TYPE_RIRP 0x1
#define QCUSB_IRP_TYPE_WIRP 0x2

// define config bits
#define QCSER_CONTINUE_ON_OVERFLOW  0x00000001L
#define QCSER_CONTINUE_ON_DATA_ERR  0x00000002L
#define QCSER_USE_128_BYTE_IN_PKT   0x00000004L
#define QCSER_USE_256_BYTE_IN_PKT   0x00000008L
#define QCSER_USE_512_BYTE_IN_PKT   0x00000010L
#define QCSER_USE_1024_BYTE_IN_PKT  0x00000020L
#define QCSER_USE_2048_BYTE_IN_PKT  0x00000040L
#define QCSER_USE_1K_BYTE_OUT_PKT   0x00000080L
#define QCSER_USE_2K_BYTE_OUT_PKT   0x00000100L
#define QCSER_USE_4K_BYTE_OUT_PKT   0x00000200L
#define QCSER_USE_128K_READ_BUFFER  0x00000400L
#define QCSER_USE_256K_READ_BUFFER  0x00000800L
#define QCSER_NO_TIMEOUT_ON_CTL_REQ 0x00001000L
#define QCSER_RETRY_ON_TX_ERROR     0x00002000L
#define QCSER_USE_READ_ARRAY        0x00004000L
#define QCSER_USE_MULTI_WRITES      0x00008000L
#define QCSER_LOGGING_WRITE_THROUGH 0x10000000L
#define QCSER_LOG_LATEST_PKTS       0x20000000L
#define QCSER_ENABLE_LOGGING        0x80000000L

#define QCSER_SYMLINK_REMOVE   0x01
#define QCSER_SYMLINK_REBUILD  0x02
#define QCSER_SYMLINK_PURGEMEM 0x04

typedef struct QCSER_VENDOR_CONFIG
{
   BOOLEAN ContinueOnOverflow;
   BOOLEAN ContinueOnDataError;
   BOOLEAN Use128ByteInPkt;
   BOOLEAN Use256ByteInPkt;
   BOOLEAN Use512ByteInPkt;
   BOOLEAN Use1024ByteInPkt;
   BOOLEAN Use2048ByteInPkt;
   BOOLEAN Use1kByteOutPkt;
   BOOLEAN Use2kByteOutPkt;
   BOOLEAN Use4kByteOutPkt;
   BOOLEAN Use128KReadBuffer;
   BOOLEAN Use256KReadBuffer;
   BOOLEAN NoTimeoutOnCtlReq;
   BOOLEAN RetryOnTxError;
   BOOLEAN UseReadArray;
   BOOLEAN UseMultiWrites;
   BOOLEAN LoggingWriteThrough;
   BOOLEAN EnableLogging;
   BOOLEAN LogLatestPkts;

   USHORT  MinInPktSize;
   USHORT  WriteUnitSize;
   ULONG   InternalReadBufSize;
   ULONG   NumOfRetriesOnError;
   ULONG   MaxPipeXferSize;
   ULONG   NumberOfL2Buffers;
   ULONG   DebugMask;
   UCHAR   DebugLevel;
   ULONG   DriverResident;
   char    PortName[255];
} QCSER_VENDOR_CONFIG;
extern QCSER_VENDOR_CONFIG gVendorConfig;

typedef struct _QC_STATS
{
   LONG lAllocatedDSPs;
   LONG lAllocatedCtls;
   LONG lAllocatedReads;
   LONG lAllocatedRdMem;
   LONG lAllocatedWrites;
   LONG lAllocatedWtMem;
   LONG lRmlCount[8];
} QC_STATS;

#ifdef DBG
   #ifdef CHECKED_SHIPPABLE

      #undef DBG
      #undef DEBUG_MSGS
      #undef ASSERT
      #define ASSERT

   #endif // CHECKED_SHIPPABLE
#endif // DBG

#define QC_MEM_TAG ((ULONG)'MDCQ')
#undef ExAllocatePool
#define ExAllocatePool(a,b) ExAllocatePoolWithTag(a,b,QC_MEM_TAG)

#include "QCUSB.h"
#include "QCDBG.h"

/*
 * Definitions not in the OSR2.1 USB supplement, but are in Memphis.
 */

// bugbug redefine mutex functions/objects
// #define USE_FAST_MUTEX
#ifdef USE_FAST_MUTEX

#define _InitializeMutex(_a_) ExInitializeFastMutex(_a_)
#define _AcquireMutex(_a_)    ExAcquireFastMutex(_a_)
#define _ReleaseMutex(_a_)    ExReleaseFastMutex(_a_)
#define _MUTEX                FAST_MUTEX
#define _PMUTEX               PFAST_MUTEX

#else  // !USE_FAST_MUTEX

#define _InitializeMutex(_a_) KeInitializeMutex(_a_,1)
#define _AcquireMutex(_a_)    KeWaitForMutexObject(_a_,Executive,KernelMode,FALSE,NULL)
#define _ReleaseMutex(_a_)    KeReleaseMutex(_a_,FALSE)
#define _MUTEX                KMUTEX
#define _PMUTEX               PKMUTEX

#endif // !USE_FAST_MUTEX

#ifdef QCSER_TARGET_XP
   #define QcAcquireSpinLock(_lock,_levelOrHandle) KeAcquireInStackQueuedSpinLock(_lock,_levelOrHandle)
   #define QcAcquireSpinLockAtDpcLevel(_lock,_levelOrHandle) KeAcquireInStackQueuedSpinLockAtDpcLevel(_lock,_levelOrHandle)
   #define QcReleaseSpinLock(_lock,_levelOrHandle) KeReleaseInStackQueuedSpinLock(&(_levelOrHandle))
   #define QcReleaseSpinLockFromDpcLevel(_lock,_levelOrHandle) KeReleaseInStackQueuedSpinLockFromDpcLevel(&(_levelOrHandle))
#else
   #define QcAcquireSpinLock(_lock,_levelOrHandle) {KeAcquireSpinLock(_lock,_levelOrHandle);InterlockedIncrement(&LockCnt);}
   #define QcAcquireSpinLockAtDpcLevel(_lock,_levelOrHandle) {KeAcquireSpinLockAtDpcLevel(_lock);InterlockedIncrement(&LockCnt);}
   #define QcReleaseSpinLock(_lock,_levelOrHandle) {KeReleaseSpinLock(_lock,_levelOrHandle);InterlockedDecrement(&LockCnt);}
   #define QcReleaseSpinLockFromDpcLevel(_lock,_levelOrHandle) {KeReleaseSpinLockFromDpcLevel(_lock);InterlockedDecrement(&LockCnt);}

   #define QcAcquireSpinLockDbg(_lock,_levelOrHandle, info) \
           { \
              KdPrint(("AC%d: %s (0x%p)\n", KeGetCurrentIrql(), info, *_lock)); \
              KeAcquireSpinLock(_lock,_levelOrHandle); \
           }
   #define QcAcquireSpinLockAtDpcLevelDbg(_lock,_levelOrHandle, info) \
           { \
              KdPrint(("AC2: %s (0x%p)\n", info, *_lock)); \
              KeAcquireSpinLockAtDpcLevel(_lock); \
           }
   #define QcAcquireSpinLockWithLevelDbg(_lock,_levelOrHandle, _level, info) \
           { \
              if (_level < DISPATCH_LEVEL) \
              { \
                 QcAcquireSpinLock(_lock,_levelOrHandle); \
              } \
              else \
              { \
                 QcAcquireSpinLockAtDpcLevel(_lock,_levelOrHandle); \
              } \
           }

#endif  // QCSER_TARGET_XP

#define QcAcquireSpinLockWithLevel(_lock,_levelOrHandle, _level) \
        { \
           if (_level < DISPATCH_LEVEL) \
           { \
              QcAcquireSpinLock(_lock,_levelOrHandle); \
           } \
           else \
           { \
              QcAcquireSpinLockAtDpcLevel(_lock,_levelOrHandle); \
           } \
        }
#define QcReleaseSpinLockWithLevel(_lock,_levelOrHandle, _level) \
        { \
           if (_level < DISPATCH_LEVEL) \
           { \
              QcReleaseSpinLock(_lock,_levelOrHandle); \
           } \
           else \
           { \
              QcReleaseSpinLockFromDpcLevel(_lock,_levelOrHandle); \
           } \
        }

#define QcAcquireDspPass(event) \
        { \
           KeWaitForSingleObject(event, Executive, KernelMode, FALSE, NULL); \
           KeClearEvent(event); \
        }
#define QcReleaseDspPass(event) \
        { \
           KeSetEvent(event, IO_NO_INCREMENT, FALSE); \
        }

#define QcAcquireEntryPass(event, tag) \
        { \
           QCSER_DbgPrintG2 \
           ( \
              QCSER_DBG_MASK_CONTROL, \
              QCSER_DBG_LEVEL_FORCE, \
              ("<%s> entry pass 0\n", tag) \
           ); \
           QcAcquireDspPass(event); \
        }
#define QcReleaseEntryPass(event, tag, tag2) \
        { \
           QcReleaseDspPass(event); \
           QCSER_DbgPrintG2 \
           ( \
              QCSER_DBG_MASK_CONTROL, \
              QCSER_DBG_LEVEL_FORCE, \
              ("<%s> entry pass 1 - %s\n", tag, tag2) \
           ); \
        }

#define QcDebugSpinLock(_info, _lock) \
        { \
           QCSER_DbgPrintG2 \
           ( \
              QCSER_DBG_MASK_CONTROL, \
              QCSER_DBG_LEVEL_FORCE, \
              ("<%s-%u> %s: 0x%p (%u/%u)\n", pDevExt->PortName, \
                KeGetCurrentIrql(), _info, &(_lock), _lock, LockCnt) \
           ); \
        }

#define arraysize(p) (sizeof(p)/sizeof((p)[0]))

#define initUnicodeString(out,in) { \
    (out).Buffer = (in); \
	(out).Length = sizeof( (in) ) - sizeof( WCHAR ); \
    (out).MaximumLength = sizeof( (in) ); }

#define initAnsiString(out,in) { \
    (out).Buffer = (in); \
	(out).Length = strlen( (in) ); \
    (out).MaximumLength = strlen( (in) ) + sizeof( CHAR ); }

#define initDevState(bits) {pDevExt->bmDevState = (bits);}
#define initDevStateX(ext, bits) {(ext)->bmDevState = (bits);}

#define setDevState(bits) {pDevExt->bmDevState |= (bits);}
#define setDevStateX(ext, bits) {(ext)->bmDevState |= (bits);}

#define clearDevState(bits) {pDevExt->bmDevState &= ~(bits);}
#define clearDevStateX(ext, bits) {(ext)->bmDevState &= ~(bits);}

#define inDevState(bits) ((pDevExt->bmDevState & (bits)) == (bits))
#define inDevStateX(ext, bits) (((ext)->bmDevState & (bits)) == (bits))

#define if_DevState(bit) \
	if ( pDevExt -> bmDevState & (bit) )

#define unless_DevState(bit) \
	if ( !(pDevExt -> bmDevState & (bit)) )

#ifdef DEBUG_MSGS
#define QcIoReleaseRemoveLock(p0, p1, p2) { \
           IoReleaseRemoveLock(p0,p1); \
           InterlockedDecrement(&(pDevExt->Sts.lRmlCount[p2])); \
           KdPrint(("RL:RML_COUNTS=%ld,%ld,%ld,%ld\n\n", \
                     pDevExt->Sts.lRmlCount[0], \
                     pDevExt->Sts.lRmlCount[1], \
                     pDevExt->Sts.lRmlCount[2], \
                     pDevExt->Sts.lRmlCount[3]));}
#define QcInterlockedIncrement(p0, p1, p2) { \
           InterlockedIncrement(&(pDevExt->Sts.lRmlCount[p0])); \
           DbgPrint("<%s> A RML-%u 0x%p\n", pDevExt->PortName, p2, p1); }
#else
#define QcIoReleaseRemoveLock(p0, p1, p2) { \
           IoReleaseRemoveLock(p0,p1); \
           InterlockedDecrement(&(pDevExt->Sts.lRmlCount[p2])); }
#define QcInterlockedIncrement(p0, p1, p2) { \
           InterlockedIncrement(&(pDevExt->Sts.lRmlCount[p0])); }
#endif


// define modem configuration types
#define DEVICETYPE_NONE     0x00
#define DEVICETYPE_CDC      0x01  // first dev with int & bulk pipes
#define DEVICETYPE_CDC_LIKE 0x02
#define DEVICETYPE_SERIAL   0x03  // first dev without int pipes
#define DEVICETYPE_CTRL     0x04  // first dev without bulk pipes
#define DEVICETYPE_INVALID  0xFF

//define some class commands

//#define GET_DEVICE_ID	0
#define GET_PORT_STATUS	1
#define SOFT_RESET	2

#define MAX_INTERFACE   16
#define MAX_IO_QUEUES	8	//number of elements in the PSR array

#pragma pack(push, 1)
typedef struct _MODEM_INFO {
   ULONG ulDteRate;
   UCHAR ucStopBit;
   UCHAR ucParityType;
   UCHAR ucDataBits;
} MODEM_INFO, *PMODEM_INFO;
#pragma pack(pop)

// IRP_MJ_PNP subtype that's defined in ntddk.h but not wdm.h
#define IRP_MN_QUERY_LEGACY_BUS_INFORMATION 0x18

//define the UART state bits returned on the interrupt pipe

#define bDsr			0x10	//current state of DSR
#define bCarrierDetect	0x20	//state of carrier detect
#define bBreakRx		0x40	//state of break detection
#define bRingSignal		0x100	//ring detect

/*
 These three event bits from VCOMM.H overlap bits from NTDDSER.H ???bugbug???
 Are they ever sent unless their wait mask is set?
*/
#define	EV_CTSS		0x00000400	 /* CTS state  OVERLAPS SERIAL_EV_RX80FULL */
#define	EV_DSRS		0x00000800	 /* DSR state  OVERLAPS SERIAL_EV_EVENT1 */
#define	EV_RLSDS	0x00001000	 /* RLSD state OVERLAPS SERIAL_EV_EVENT2 */

#define OUR_DEVICE_OBJECT_FLAGS (DO_BUFFERED_IO | DO_EXCLUSIVE | DO_POWER_PAGABLE)

struct _VXD_WDM_IO_CONTROL_BLOCK; // for forward reference

typedef NTSTATUS (_stdcall *STE_COMPLETIONROUTINE)(struct _VXD_WDM_IO_CONTROL_BLOCK *, BOOLEAN, UCHAR);	

typedef struct _POWER_COMPLETION_CONTEXT
{
   PVOID DeviceExtension;
   PIRP  Irp;
} POWER_COMPLETION_CONTEXT, *PPOWER_COMPLETION_CONTEXT;

typedef struct _QCDSP_IOBlockType
{
   LIST_ENTRY     List;
   PIRP           Irp;       // IRP
   PDEVICE_OBJECT CalledDO;  // either FDO or PortDO
   BOOLEAN        Hold;
   BOOLEAN        IsXwdmReq;
} QCDSP_IOBlockType, *PQCDSP_IOBlockType;

typedef struct _VXD_WDM_IO_CONTROL_BLOCK
{
        LIST_ENTRY              List;
	PDEVICE_OBJECT	        pSerialDeviceObject;
	LONG			lPurgeForbidden;
	PVOID			pBufferFromDevice;
	ULONG			ulBFDBytes;
	PVOID			pBufferToDevice;
	ULONG			ulBTDBytes;
	PVOID			pActiveBuffer;
	ULONG			ulActiveBytes;
	PVOID			pContext;
	NTSTATUS		ntStatus;
	STE_COMPLETIONROUTINE	pCompletionRoutine;
	PIRP			pCallingIrp;
	KTIMER			TimeoutTimer; // Read Timer
	KDPC			TimeoutDpc;   // Read Timer
	BOOLEAN			TimerExpired; // Read Timer
	ULONG			ulTimeout;
	ULONG			ulReadIntervalTimeout;
        BOOLEAN                 bTimerInitiated;
        BOOLEAN                 bRItimerInitiated;
        BOOLEAN                 bPurged;
        BOOLEAN                 bReturnOnChars;
	struct _VXD_WDM_IO_CONTROL_BLOCK * pNextEntry;  // link for que'ing
} VXD_WDM_IO_CONTROL_BLOCK, *PVXD_WDM_IO_CONTROL_BLOCK;

#define MAXIOBUFFSIZE 68

// Commonly used event indexes
#define QC_DUMMY_EVENT_INDEX             0
#define CANCEL_EVENT_INDEX               1

// L2 read event indexes
#define L2_KICK_READ_EVENT_INDEX         2
#define L2_READ_COMPLETION_EVENT_INDEX   3 // not used for MULTI_READS
#define L2_READ_PURGE_EVENT_INDEX        4
#define L2_READ_STOP_EVENT_INDEX         5
#define L2_READ_RESUME_EVENT_INDEX       6
#define L2_READ_EVENT_COUNT              7

// L1 read event indexes
#define L1_CANCEL_EVENT_INDEX            1
#define L1_KICK_READ_EVENT_INDEX         2
#define L1_READ_COMPLETION_EVENT_INDEX   3
#define L1_READ_PURGE_EVENT_INDEX        4
#define L1_READ_AVAILABLE_EVENT_INDEX    5
#define L1_READ_PRE_TIMEOUT_EVENT_INDEX  6
#define L1_CANCEL_CURRENT_EVENT_INDEX    7
#define L1_READ_EVENT_COUNT              8

// Write event indexes
#define KICK_WRITE_EVENT_INDEX           2
#define WRITE_COMPLETION_EVENT_INDEX     3
#define WRITE_PURGE_EVENT_INDEX          4
#define WRITE_CANCEL_CURRENT_EVENT_INDEX 5
#define WRITE_PRE_TIMEOUT_EVENT_INDEX    6
#define WRITE_STOP_EVENT_INDEX           7
#define WRITE_RESUME_EVENT_INDEX         8
#define WRITE_FLOW_ON_EVENT_INDEX        9
#define WRITE_FLOW_OFF_EVENT_INDEX       10
#define WRITE_TIMEOUT_COMPLETION_EVENT_INDEX 11
#define WRITE_EVENT_COUNT                12

// Interrupt event indexes
#define INT_COMPLETION_EVENT_INDEX       2
#define INT_STOP_SERVICE_EVENT           3
#define INT_RESUME_SERVICE_EVENT         4
#define INT_EMPTY_RD_QUEUE_EVENT_INDEX   5
#define INT_EMPTY_WT_QUEUE_EVENT_INDEX   6
#define INT_EMPTY_CTL_QUEUE_EVENT_INDEX  7
#define INT_EMPTY_SGL_QUEUE_EVENT_INDEX  8
#define INT_REG_IDLE_NOTIF_EVENT_INDEX   9
#define INT_STOP_REG_ACCESS_EVENT_INDEX  10
#define INT_PIPE_EVENT_COUNT             11

// Dispatch event indexes
#define DSP_CANCEL_EVENT_INDEX           1
#define DSP_START_EVENT_INDEX            2
#define DSP_DEV_RETRY_EVENT_INDEX        3
#define DSP_DEV_RESET_IN_EVENT_INDEX     4
#define DSP_DEV_RESET_OUT_EVENT_INDEX    5
#define DSP_START_DATA_THREADS_INDEX     6
#define DSP_RESUME_DATA_THREADS_INDEX    7
#define DSP_PRE_WAKEUP_EVENT_INDEX       8
#define DSP_EVENT_COUNT                  9

// End   of WDM_VxD.c support structures (service threads)

// device extension for driver instance, used to store needed data

#ifdef QCUSB_MULTI_READS
typedef enum _L2BUF_STATE
{
   L2BUF_STATE_READY     = 0,
   L2BUF_STATE_PENDING   = 1,
   L2BUF_STATE_COMPLETED = 2
} L2BUF_STATE;

typedef enum _L2_STATE
{
   L2_STATE_WORKING    = 0,
   L2_STATE_STOPPING   = 1,
   L2_STATE_PURGING    = 2,
   L2_STATE_CANCELLING = 3
} L2_STATE;
#endif // QCUSB_MULTI_READS

typedef struct _QCRD_L2BUFFER
{
   LIST_ENTRY List;
   NTSTATUS   Status;
   PVOID      Buffer;
   ULONG      Length;
   BOOLEAN    bFilled;
   BOOLEAN    bReturnToUser;

   #ifdef QCUSB_MULTI_READS
   PVOID       DeviceExtension;
   UCHAR       Index;  // for debugging purpose
   PIRP        Irp;
   URB         Urb;
   L2BUF_STATE State;
   KEVENT      CompletionEvt;
   #endif // QCUSB_MULTI_READS

} QCRD_L2BUFFER, *PQCRD_L2BUFFER;

#ifdef QCUSB_MULTI_WRITES
#define QCUSB_MULTI_WRITE_BUFFERS 8 // must < QCUSB_MAX_MRW_BUF_COUNT !!!

typedef enum _MWT_STATE
{
   MWT_STATE_WORKING    = 0,
   MWT_STATE_STOPPING   = 1,
   MWT_STATE_PURGING    = 2,
   MWT_STATE_CANCELLING = 3,
   MWT_STATE_FLOW_OFF   = 4
} MWT_STATE;

typedef enum _MWT_BUF_STATE
{
   MWT_BUF_IDLE      = 0,
   MWT_BUF_PENDING   = 1,
   MWT_BUF_COMPLETED = 2
} MWT_BUF_STATE;

typedef struct _QCMWT_BUFFER
{
   LIST_ENTRY    List;
   PVOID         DeviceExtension;
   PVXD_WDM_IO_CONTROL_BLOCK IoBlock;
   PIRP          Irp;
   URB           Urb;
   KEVENT        WtCompletionEvt;
   ULONG         Length;
   int           Index;
   MWT_BUF_STATE State;
} QCMWT_BUFFER, *PQCMWT_BUFFER;

typedef struct _QCMWT_CXLREQ
{
   LIST_ENTRY    List;
   int           Index;
} QCMWT_CXLREQ, *PQCMWT_CXLREQ;

#endif // QCUSB_MULTI_WRITES

#define NUM_LATEST_PKTS 8

#ifdef QCSER_ENABLE_LOG_REC
#pragma pack(push, 1)
typedef struct
{
   char      TimeStamp[32];
   ULONG     PktLength;
   UCHAR     Data[64];
} LogRecType;
#pragma pack(pop)
#endif // QCSER_ENABLE_LOG_REC

typedef struct
{
   UCHAR ucTimeoutType;
   BOOLEAN bUseReadInterval;
   BOOLEAN bReturnOnAnyChars;
} ReadTimeoutType;

struct _QCSER_FdoCollectionType
{
   PDEVICE_OBJECT Fdo;
   struct _QCSER_FdoCollectionType *Next;
};
typedef struct _QCSER_FdoCollectionType QCSER_FdoCollectionType, *PQCSER_FdoCollectionType;

struct _QCSER_PortDeviceListType
{
   PDEVICE_OBJECT PortDO;
   struct _QCSER_PortDeviceListType *Next;
};
typedef struct _QCSER_PortDeviceListType QCSER_PortDeviceListType, *PQCSER_PortDeviceListType;

#define QCSER_READ_TIMEOUT_UNDEF   0x00
#define QCSER_READ_TIMEOUT_CASE_1  0x01
#define QCSER_READ_TIMEOUT_CASE_2  0x02
#define QCSER_READ_TIMEOUT_CASE_3  0x03
#define QCSER_READ_TIMEOUT_CASE_4  0x04
#define QCSER_READ_TIMEOUT_CASE_5  0x05
#define QCSER_READ_TIMEOUT_CASE_6  0x06
#define QCSER_READ_TIMEOUT_CASE_7  0x07
#define QCSER_READ_TIMEOUT_CASE_8  0x08
#define QCSER_READ_TIMEOUT_CASE_9  0x09
#define QCSER_READ_TIMEOUT_CASE_10 0x10
#define QCSER_READ_TIMEOUT_CASE_11 0x11

// HS-USB determination
#define QC_HSUSB_VERSION         0x0200
#define QC_HSUSB_BULK_MAX_PKT_SZ 512
#define QC_HSUSB_VERSION_OK      0x01
#define QC_HSUSB_ALT_SETTING_OK  0x02
#define QC_HSUSB_BULK_MAX_PKT_OK 0x04
#define QC_HS_USB_OK  (QC_HSUSB_VERSION_OK | QC_HSUSB_ALT_SETTING_OK)
#define QC_HS_USB_OK2 (QC_HSUSB_VERSION_OK | QC_HSUSB_BULK_MAX_PKT_OK)
#define QC_HS_USB_OK3 (QC_HSUSB_VERSION_OK |     \
                       QC_HSUSB_ALT_SETTING_OK | \
                       QC_HSUSB_BULK_MAX_PKT_OK)

typedef struct _FDO_DEVICE_EXTENSION
{
   PDEVICE_OBJECT PDO;
   PDEVICE_OBJECT StackDeviceObject; // stack device object
   PDEVICE_OBJECT MyDeviceObject;
   PDEVICE_OBJECT PortDevice;
   BOOLEAN        bPendingRemoval;
} FDO_DEVICE_EXTENSION, *PFDO_DEVICE_EXTENSION;

typedef struct _DEVICE_EXTENSION
{
   PDEVICE_OBJECT PhysicalDeviceObject;	// physical device object
   PDEVICE_OBJECT StackDeviceObject;	// stack device object
   PDEVICE_OBJECT MyDeviceObject;
   PDEVICE_OBJECT FDO;
   PDEVICE_OBJECT StackTopDO;
   PQCSER_FdoCollectionType FdoChain;

   IO_REMOVE_LOCK RemoveLock;           // removal control
   PIO_REMOVE_LOCK pRemoveLock;         // removal control

   // descriptors for device instance
   PUSB_DEVICE_DESCRIPTOR pUsbDevDesc;	// ptr since there is only 1 dev desc
   PUSB_CONFIGURATION_DESCRIPTOR pUsbConfigDesc;
   PUSBD_INTERFACE_INFORMATION Interface[MAX_INTERFACE];
   USHORT usCommClassInterface;         // CDC
   // handle to configuration that was selected
   USBD_CONFIGURATION_HANDLE ConfigurationHandle;
   BOOLEAN bInService;                  //set in create, cleared in close
   BOOLEAN bStackOpen;                  // whether the _CREATE comes from the stack

   ReadTimeoutType ReadTimeout;

   LIST_ENTRY RdCompletionQueue;
   LIST_ENTRY WtCompletionQueue;
   LIST_ENTRY CtlCompletionQueue;
   LIST_ENTRY SglCompletionQueue;

   LIST_ENTRY ReadFreeQueue;
   LIST_ENTRY WriteFreeQueue;
   LIST_ENTRY DispatchFreeQueue;
   LIST_ENTRY DispatchQueue;
   PQCDSP_IOBlockType pCurrentDispatch;

   PCHAR pInterruptBuffer;
   USHORT wMaxPktSize;
   UCHAR BulkPipeOutput;
   UCHAR BulkPipeInput;
   UCHAR InterruptPipe;
   UCHAR InterruptPipeIdx;
   UCHAR ControlInterface; // contains the Interrupt Pipe
   UCHAR DataInterface; // contains the Bulk Pipes; == ControlInterface == 0 if not CDC
   UCHAR ModemControlReg;
   UCHAR ModemStatusReg;
   UCHAR bLsrMsrInsert;
   PSERIALPERF_STATS pPerfstats;
   PSERIAL_COMMPROP pCommProp;
   PSERIAL_STATUS pSerialStatus;
   PSERIAL_HANDFLOW pSerialHandflow;
   PSERIAL_TIMEOUTS pSerialTimeouts;
   PSERIAL_CHARS pSerialChars;
   ULONG ulWaitMask;
   PIRP pWaitOnMaskIrp;
   PIRP pNotificationIrp;
   KEVENT WaitOnMaskEvent;
   KEVENT ForTimeoutEvent;
   UCHAR  ucDeviceType;    // DEVICETYPE_CDC or DEVICETYPE_SERIAL
   MODEM_INFO ModemInfo;  // write-thru cache because modem goes offline on GET() !!!
   BOOLEAN bModemInfoValid;
   ULONG TXHolding;
   ULONG RXHolding;

   USHORT usCurrUartState;
   KSPIN_LOCK ControlSpinLock;
   KSPIN_LOCK ReadSpinLock;
   KSPIN_LOCK WriteSpinLock;
   KSPIN_LOCK SingleIrpSpinLock;
   USHORT bmDevState;
   // PIRP_LINK pCancelledIrpStack;
   UNICODE_STRING       ucsUnprotectedLink;
   UNICODE_STRING	ucsDeviceMapEntry;
   UNICODE_STRING	ucsPortName;
   UNICODE_STRING       ucsIfaceSymbolicLinkName;
   DEVICE_TYPE          FdoDeviceType;
   char                 PortName[16];
   BOOLEAN bSymLinksValid;
   USHORT idVendor;
   USHORT idProduct;
   LONG lWriteBufferUnit;
   LONG lReadBufferSize;
   LONG lReadBuffer20pct;
   LONG lReadBuffer50pct;
   LONG lReadBuffer80pct;
   LONG lReadBufferHigh;
   LONG lReadBufferLow;
   LONG lPurgeBegin;
   PUCHAR pucReadBufferStart;
   PUCHAR pucReadBufferGet;
   PUCHAR pucReadBufferPut;

   //Bus drivers set the appropriate values in this structure in response
   //to an IRP_MN_QUERY_CAPABILITIES IRP. Function and filter drivers might
   //alter the capabilities set by the bus driver.
   DEVICE_CAPABILITIES DeviceCapabilities;

   // default power state to power down to on self-suspend 
   BOOLEAN            PowerSuspended;
   SYSTEM_POWER_STATE SystemPower;
   DEVICE_POWER_STATE DevicePower;
   BOOLEAN            PMWmiRegistered;
   BOOLEAN            bRemoteWakeupEnabled;  // device side
   BOOLEAN            PowerManagementEnabled;
   BOOLEAN            WaitWakeEnabled;       // host side
   BOOLEAN            SetCommFeatureSupported;
   PIRP               WaitWakeIrp;
   ULONG              PowerDownLevel; 
   UCHAR              IoBusyMask;
   BOOLEAN            IdleTimerLaunched;
   KTIMER             IdleTimer;
   KDPC               IdleDpc;
   PIRP               IdleNotificationIrp;
   USHORT             WdmVersion;
   ULONG              SelectiveSuspendIdleTime;
   BOOLEAN            InServiceSelectiveSuspension;
   WMILIB_CONTEXT     WmiLibInfo;
   BOOLEAN            PrepareToPowerDown;
   POWER_COMPLETION_CONTEXT PwrCompContext;
   // long               PendingDxIrp[POWER_SYSTEM_MAXIMUM];

   /******** WDM.h *******
   typedef enum _DEVICE_POWER_STATE {
       PowerDeviceUnspecified = 0,
       PowerDeviceD0,
       PowerDeviceD1,
       PowerDeviceD2,
       PowerDeviceD3,
       PowerDeviceMaximum
   } DEVICE_POWER_STATE, *PDEVICE_POWER_STATE;
   ***********************/

   // Start of WDM_VxD.c support structures (service threads)
   PKEVENT pL2ReadEvents[L2_READ_EVENT_COUNT+QCUSB_MAX_MRW_BUF_COUNT];
   PKEVENT pL1ReadEvents[L1_READ_EVENT_COUNT];  // array of events which alert the L1 read thread
   PKEVENT pWriteEvents[WRITE_EVENT_COUNT+QCUSB_MAX_MRW_BUF_COUNT];
   PKEVENT pInterruptPipeEvents[INT_PIPE_EVENT_COUNT];
   // PKEVENT pDispatchEvents[DSP_EVENT_COUNT];
   KEVENT L1ReadThreadClosedEvent;
   KEVENT L2ReadThreadClosedEvent;
   KEVENT ReadIrpPurgedEvent;
   KEVENT WriteThreadClosedEvent;
   KEVENT InterruptPipeClosedEvent;
   KEVENT InterruptStopServiceEvent;
   KEVENT InterruptStopServiceRspEvent;
   KEVENT InterruptResumeServiceEvent;
   KEVENT InterruptEmptyRdQueueEvent;
   KEVENT InterruptEmptyWtQueueEvent;
   KEVENT InterruptEmptyCtlQueueEvent;
   KEVENT InterruptEmptySglQueueEvent;
   KEVENT InterruptStopRegAccessEvent;
   KEVENT InterruptStopRegAccessAckEvent;
   KEVENT RegIdleAckEvent;
   KEVENT InterruptRegIdleEvent;
   KEVENT CancelReadEvent;
   KEVENT L1CancelReadEvent;
   KEVENT CancelCurrentReadEvent;
   KEVENT CancelCurrentWriteEvent;
   KEVENT CancelWriteEvent;
   KEVENT CancelInterruptPipeEvent;
   KEVENT WriteCompletionEvent;
   KEVENT L2ReadCompletionEvent;
   KEVENT L1ReadCompletionEvent;
   KEVENT L1ReadAvailableEvent;
   KEVENT L1ReadPreTimeoutEvent;
   KEVENT L2ReadPurgeEvent;
   KEVENT L1ReadPurgeAckEvent;
   KEVENT L1ReadPurgeEvent;
   KEVENT L2ReadStopEvent;
   KEVENT L2ReadStopAckEvent;
   KEVENT L2ReadResumeEvent;
   KEVENT WriteStopEvent;
   KEVENT WriteStopAckEvent;
   KEVENT WriteResumeEvent;
   KEVENT WritePurgeEvent;
   KEVENT WritePreTimeoutEvent;
   KEVENT KickWriteEvent;
   KEVENT L2KickReadEvent;
   KEVENT L1KickReadEvent;
   KEVENT ReadThreadStartedEvent;
   KEVENT L2ReadThreadStartedEvent;
   KEVENT WriteThreadStartedEvent;
   KEVENT DspThreadStartedEvent;
   KEVENT IntThreadStartedEvent;
   KEVENT eInterruptCompletion;
   KEVENT ReadCompletionThrottleEvent;

   #ifdef QCUSB_FC
   KEVENT WriteFlowOnEvent;
   KEVENT WriteFlowOffEvent;
   KEVENT WriteFlowOffAckEvent;
   KEVENT WriteTimeoutCompletionEvent;
   LIST_ENTRY MWTSentIrpQueue;
   LIST_ENTRY MWTSentIrpRecordPool;
   LONG   MWTPendingCnt;
   #endif // QCUSB_FC

   BOOLEAN bDeviceRemoved;
   BOOLEAN bDeviceSurpriseRemoved;

   HANDLE hInterruptThreadHandle;
   HANDLE hL1ReadThreadHandle;
   HANDLE hL2ReadThreadHandle;
   HANDLE hWriteThreadHandle;
   PKTHREAD pInterruptThread;  // typedef struct _KTHREAD *PKTHREAD;
   PKTHREAD pL1ReadThread;
   PKTHREAD pL2ReadThread;
   PKTHREAD pWriteThread;

   HANDLE hTxLogFile;
   HANDLE hRxLogFile;
   UNICODE_STRING ucLoggingDir;
   BOOLEAN        bLoggingOk;
   ULONG          ulRxLogCount;
   ULONG          ulLastRxLogCount;
   ULONG          ulTxLogCount;
   ULONG          ulLastTxLogCount;

   PVXD_WDM_IO_CONTROL_BLOCK pReadHead; // top of read que
   PVXD_WDM_IO_CONTROL_BLOCK pWriteHead; // top of write que
   PVXD_WDM_IO_CONTROL_BLOCK pReadCurrent;
   PVXD_WDM_IO_CONTROL_BLOCK pWriteCurrent;

   BOOLEAN bDspCancelStarted;
   BOOLEAN bItCancelStarted;
   BOOLEAN bRdCancelStarted;
   BOOLEAN bWtCancelStarted;
   BOOLEAN bRdThreadInCreation;
   BOOLEAN bL2ThreadInCreation;
   BOOLEAN bWtThreadInCreation;
   BOOLEAN bIntThreadInCreation;
   BOOLEAN bDspThreadInCreation;

   BOOLEAN bPacketsRead; 
   BOOLEAN bL1ReadActive;
   BOOLEAN bL1PropagateCancellation;
   BOOLEAN bL2ReadActive;
   BOOLEAN bWriteActive;
   BOOLEAN bReadBufferReset;
   BOOLEAN bL1InCancellation;

   // End   of WDM_VxD.c support structures (service threads)

   QCRD_L2BUFFER  *pL2ReadBuffer;

   #ifdef QCUSB_MULTI_READS

   LIST_ENTRY L2CompletionQueue;
   KSPIN_LOCK L2Lock;
   int L2IrpStartIdx;
   int L2IrpEndIdx;

   #endif // QCUSB_MULTI_READS

   int L2FillIdx;
   int L2IrpIdx;

   #ifdef QCUSB_MULTI_WRITES
   PQCMWT_BUFFER pMwtBuffer[QCUSB_MAX_MRW_BUF_COUNT];
   QCMWT_CXLREQ  CxlRequest[QCUSB_MAX_MRW_BUF_COUNT];
   ULONG      NumberOfMultiWrites;
   LIST_ENTRY MWriteIdleQueue;
   LIST_ENTRY MWritePendingQueue;
   LIST_ENTRY MWriteCompletionQueue;
   LIST_ENTRY MWriteCancellingQueue;
   #endif // QCUSB_MULTI_WRITES

   #ifdef QCSER_ENABLE_LOG_REC
   LogRecType TxLogRec[NUM_LATEST_PKTS];
   LogRecType RxLogRec[NUM_LATEST_PKTS];
   int        RxLogRecIndex;
   int        TxLogRecIndex;
   #endif // QCSER_ENABLE_LOG_REC

   BOOLEAN bVendorFeature;
   BOOLEAN bByteStuffingFeature;
   BOOLEAN bEnableByteStuffing;
   PVOID pByteStuffingBuffer;
   ULONG ulByteStuffingBufLen;
   BOOLEAN bBytePaddingFeature;
   BOOLEAN bEnableBytePadding;
   KPRIORITY L1Priority;
   BOOLEAN bFdoReused;

   // device configuration parameters
   BOOLEAN ContinueOnOverflow;
   BOOLEAN ContinueOnDataError;
   BOOLEAN RetryOnTxError;
   BOOLEAN UseReadArray;
   BOOLEAN UseMultiWrites;
   BOOLEAN EnableLogging;
   BOOLEAN LogLatestPkts;
   BOOLEAN NoTimeoutOnCtlReq;
   BOOLEAN LoggingWriteThrough;
   USHORT  MinInPktSize;
   USHORT  WriteUnitSize;
   ULONG   NumOfRetriesOnError;
   ULONG   MaxPipeXferSize;
   ULONG   NumberOfL2Buffers;

   USHORT  DeviceAddress;

   ULONG   DebugMask;
   UCHAR   DebugLevel;

   LONG    NumIrpsToComplete;
   ULONG   CompletionThrottle;

   QC_STATS Sts;
   LONG     TopDoCount;

   UCHAR   RdErrorCount;
   UCHAR   WtErrorCount;

   BOOLEAN bL1Stopped;
   BOOLEAN bL2Stopped;
   BOOLEAN bWriteStopped;
   BOOLEAN bWOMHeldForRead;

   int     MgrId;
   BOOLEAN CleanupInProgress;

   UNICODE_STRING QCDEV_IfaceSymbolicLinkName;
   PIRP pQcDevNotificationIrp;
   LONG QcDevOpenCount;

   UCHAR HighSpeedUsbOk;

}  DEVICE_EXTENSION, *PDEVICE_EXTENSION;


#define DATA0		0x0000
#define DATA1		0x0001

// device states

#define DEVICE_STATE_ZERO             0x0000
#define	DEVICE_STATE_PRESENT          0x0001 // refer to PDO
#define DEVICE_STATE_USB_INITIALIZED  0x0002
#define DEVICE_STATE_DEVICE_STARTED   0x0004
#define DEVICE_STATE_DEVICE_STOPPED   0x0008
#define DEVICE_STATE_SURPRISE_REMOVED 0x0010
#define	DEVICE_STATE_WOM_FIRST_TIME   0x0020
#define	DEVICE_STATE_CLIENT_PRESENT   0x0040 // refer to upper level
#define	DEVICE_STATE_DELETE_DEVICE    0x0080
#define DEVICE_STATE_DEVICE_REMOVED0  0x0100 // removal start
#define DEVICE_STATE_DEVICE_REMOVED1  0x0200 // removal end
#define DEVICE_STATE_DEVICE_QUERY_REMOVE 0x0400
#define DEVICE_STATE_PRESENT_AND_STARTED (DEVICE_STATE_PRESENT|DEVICE_STATE_DEVICE_STARTED)
#define DEVICE_STATE_PRESENT_AND_INIT    (DEVICE_STATE_PRESENT|DEVICE_STATE_USB_INITIALIZED)

// client states

#define CLIENT_STATE_PRESENT     0x01

extern long LockCnt;

extern char    gDeviceName[255];
extern KEVENT  gSyncEntryEvent;
extern KSPIN_LOCK gPnpSpinLock;
extern int        gPnpState;
extern PQCSER_PortDeviceListType gPortDOList;
extern UNICODE_STRING gServicePath;
extern USHORT ucHdlCnt;

VOID QCMAIN_Unload (IN PDRIVER_OBJECT DriverObject);

// Removal-related procedures
VOID RemoveSymbolicLinks(PDEVICE_OBJECT pDevObj);
VOID cancelAllIrps( PDEVICE_OBJECT pDevObj );
NTSTATUS QCSER_CleanupDeviceExtensionBuffers(IN  PDEVICE_OBJECT DeviceObject);

NTSTATUS CancelWOMIrp(PDEVICE_OBJECT pDevObj);
NTSTATUS CancelNotificationIrp(PDEVICE_OBJECT pDevObj);
VOID CancelWOMRoutine(PDEVICE_OBJECT pDevObj, PIRP pIrp);
VOID CancelNotificationRoutine(PDEVICE_OBJECT pDevObj, PIRP pIrp);

// add new function prototypes above this line.

typedef struct {
   PUNICODE_STRING pUs;
   USHORT usBufsize;
} UNICODE_BUFF_DESC, *PUNICODE_BUFF_DESC;


#define USB_CDC_INT_RX_CARRIER   0x01   // bit 0
#define USB_CDC_INT_TX_CARRIER   0x02   // bit 1
#define USB_CDC_INT_BREAK        0x04   // bit 2
#define USB_CDC_INT_RING         0x08   // bit 3
#define USB_CDC_INT_FRAME_ERROR  0x10   // bit 4
#define USB_CDC_INT_PARITY_ERROR 0x20   // bit 5
#define USB_CDC_INT_OVERRUN      0x30   // bit 6

/**********************
#define SERIAL_DTR_STATE         ((ULONG)0x00000001)
#define SERIAL_RTS_STATE         ((ULONG)0x00000002)
#define SERIAL_CTS_STATE         ((ULONG)0x00000010)
#define SERIAL_DSR_STATE         ((ULONG)0x00000020)
#define SERIAL_RI_STATE          ((ULONG)0x00000040)
#define SERIAL_DCD_STATE         ((ULONG)0x00000080)

#define SERIAL_EV_RXCHAR           0x0001  // Any Character received
#define SERIAL_EV_RXFLAG           0x0002  // Received certain character
#define SERIAL_EV_TXEMPTY          0x0004  // Transmitt Queue Empty
#define SERIAL_EV_CTS              0x0008  // CTS changed state
#define SERIAL_EV_DSR              0x0010  // DSR changed state
#define SERIAL_EV_RLSD             0x0020  // RLSD changed state
#define SERIAL_EV_BREAK            0x0040  // BREAK received
#define SERIAL_EV_ERR              0x0080  // Line status error occurred
#define SERIAL_EV_RING             0x0100  // Ring signal detected
#define SERIAL_EV_PERR             0x0200  // Printer error occured
#define SERIAL_EV_RX80FULL         0x0400  // Receive buffer is 80 percent full
#define SERIAL_EV_EVENT1           0x0800  // Provider specific event 1
#define SERIAL_EV_EVENT2           0x1000  // Provider specific event 2
***********************/

/***
#define US_BITS_MODEM  (SERIAL_EV_RXCHAR| \
                        SERIAL_EV_RXFLAG| \
                        SERIAL_EV_TXEMPTY| \
                        SERIAL_EV_CTS| \
                        SERIAL_EV_DSR| \
                        SERIAL_EV_RLSD| \
                        SERIAL_EV_BREAK| \
                        SERIAL_EV_ERR| \
                        SERIAL_EV_RING| \
                        SERIAL_EV_RX80FULL)
***/

#define US_BITS_MODEM  (SERIAL_EV_CTS| \
                        SERIAL_EV_DSR| \
                        SERIAL_EV_RLSD| \
                        SERIAL_EV_BREAK| \
                        SERIAL_EV_ERR| \
                        SERIAL_EV_RING)
// CTS is not reported by HW; it follows either DSR or RTS
#define US_BITS_MODEM_RAW (SERIAL_EV_DSR| \
                           SERIAL_EV_RLSD| \
                           SERIAL_EV_BREAK| \
                           SERIAL_EV_RING)   

#define MAX_DEVICE_ID_LEN 1024 //128
#define MAX_ENTRY_NAME_LEN	1024 //64
#define MAX_ENTRY_DATA_LEN	2048 //256 11/14/98
#define MAX_ENTRY_LARGE_LEN	1024 //512
#define MAX_USB_KEY_LEN	2048
#define MAX_NAME_LEN	1024 //128
#define MAX_KEY_NAME_LEN	1024 //256
#define DEVICE_PATH_SIZE 1024 //255

/*
 ************************************************************************
 This section contains dispatcher queueing routines
 */
#define _IoMarkIrpPending(_pIrp_) {IoMarkIrpPending(_pIrp_);}

typedef NTSTATUS (*PSTART_ROUTINE) (IN PDEVICE_EXTENSION pDevExt);
NTSTATUS DispatchShell(IN PDEVICE_OBJECT DeviceObject,IN PIRP Irp);
NTSTATUS QCMAIN_IrpCompletionSetEvent
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PIRP Irp,
   IN PVOID Context
);

VOID RemovePurgedReads(PDEVICE_OBJECT DeviceObject);
NTSTATUS _QcIoCompleteRequest(IN PIRP Irp, IN CCHAR  PriorityBoost);
NTSTATUS QcCompleteRequest(IN PIRP Irp, IN NTSTATUS status, IN ULONG_PTR info);
VOID QCSER_PostRemovalNotification(PDEVICE_EXTENSION pDevExt);
VOID QCSER_DispatchDebugOutput
(
   PIRP               Irp,
   PIO_STACK_LOCATION irpStack,
   PDEVICE_OBJECT     CalledDO,
   PDEVICE_OBJECT     DeviceObject,
   KIRQL              irql
);
NTSTATUS QCSER_AddToFdoCollection(PDEVICE_EXTENSION pDevExt, PDEVICE_OBJECT fdo);
NTSTATUS QCSER_RemoveFdoFromCollection(PDEVICE_EXTENSION pDevExt, PDEVICE_OBJECT fdo);

#endif // QCMAIN_H
