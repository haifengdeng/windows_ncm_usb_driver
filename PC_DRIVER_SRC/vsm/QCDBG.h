/*===========================================================================
FILE: QCDBG.h

DESCRIPTION:
   This file contains definitions various debugging purposes.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

#ifndef QCDBG_H
#define QCDBG_H

// #define DEBUG_MSGS

// VEN = Value Entry Name
// VED = Value Entry Data

extern char gDeviceName[255];

#define ENUM_PATH           L"Enum\\"
//#define DRIVER_PATH       L"System\\CurrentControlSet\\Services\\Class\\"
#define VEN_DRIVER          L"Driver"
#define VEN_DEVICE          L"Device"
#define VEN_STATUS          L"Status"
#define VEN_USB_PATH        L"UsbPath"
#define VEN_DEV_SYM_NAME    L"DeviceSymbolicName"
#define VEN_DEV_OBJ_NAME    L"DeviceObjectNameBase"
#define VEN_DEVICE_KEY      L"DeviceKey"
#define VED_NEW_DEVICE_KEY  L"NewDeviceKey"
#define VEN_USB_ID          L"USB_ID"
#define SLASH               L"\\"
#define VEN_DEV_NUM         L"DeviceNumber"
#define VEN_DEV_NUM_N       "DeviceNumber"
#define VED_NONE            L"Not Currently Present"
#define VEN_DEV_VER         L"DriverVersion"
#define VEN_DEV_PORT        L"AssignedPortForQCDevice"
#define VEN_DEV_CONFIG      L"QCDriverConfig"
#define VEN_DEV_LOG_DIR     L"QCDriverLoggingDirectory"
#define VEN_DEV_RTY_NUM     L"QCDriverRetriesOnError"
#define VEN_DEV_MAX_XFR     L"QCDriverMaxPipeXferSize"
#define VEN_DEV_L2_BUFS     L"QCDriverL2Buffers"
#define VEN_DBG_MASK        L"QCDriverDebugMask"
#define VEN_DRV_RESIDENT    L"QCDriverResident"
#define VEN_DRV_WRITE_UNIT  L"QCDriverWriteUnit"
#define VEN_DRV_SS_IDLE_T   L"QCDriverSelectiveSuspendIdleTime"
#define HW_KEY_BASE         "Enum\\"

#define DEVICE_OBJECT_PRESENT         0x0001
#define DEVICE_SYMBOLIC_NAME_PRESENT  0x0002

//#define HW_MODEM_PATH               L"Enum\\Root\\UsbMODEM\\"
#define DEVICE_PATH_1                 L"System\\CurrentControlSet\\Controls\\Class"
#define DRIVER_FIELD                  L"Driver"

#define DEVICE_PORTNAME_LABEL         L"PortName"

#define DEVICE_NAME_PATH              L"\\Device\\"
#define DEVICE_LINK_NAME_PATH         L"\\??\\"

// The  lower 4 bits is reserved for debug level
#define QCSER_DBG_LEVEL_FORCE    0x0
#define QCSER_DBG_LEVEL_CRITICAL 0x1
#define QCSER_DBG_LEVEL_ERROR    0x2
#define QCSER_DBG_LEVEL_INFO     0x3
#define QCSER_DBG_LEVEL_DETAIL   0x4
#define QCSER_DBG_LEVEL_VERBOSE  0x5
#define QCSER_DBG_LEVEL_TRACE    0x6

#define QCSER_DBG_MASK_CONTROL  0x00000010
#define QCSER_DBG_MASK_READ     0x00000020
#define QCSER_DBG_MASK_WRITE    0x00000040
#define QCSER_DBG_MASK_ENC      0x00000080
#define QCSER_DBG_MASK_POWER    0x00000100
#define QCSER_DBG_MASK_STATE    0x00000200
#define QCSER_DBG_MASK_RDATA    0x00000400
#define QCSER_DBG_MASK_TDATA    0x00000800
#define QCSER_DBG_MASK_RIRP     0x10000000
#define QCSER_DBG_MASK_WIRP     0x20000000
#define QCSER_DBG_MASK_CIRP     0x40000000
#define QCSER_DBG_MASK_PIRP     0x80000000

// macros 
#ifdef DBG
   #ifdef VERBOSE_ALLOC
      typedef struct tagAllocationList
      {
         struct tagAllocationList *pPrev, *pNext;
         PVOID pBuffer;
         UCHAR *pWho;
         ULONG ulSize;
      }  ALLOCATION_LIST;

      void _ReportMemoryUsage();
      PVOID _ExAllocatePool
      (
         IN    POOL_TYPE PoolType,
         IN    ULONG NumberOfBytes,
         UCHAR *pWho
      );
      VOID _ExFreePool(IN PVOID P);

      #define REPORT_MEMORY_USAGE
   #endif
#endif

#ifndef REPORT_MEMORY_USAGE
   #define _ReportMemoryUsage();
   #define _ExAllocatePool(a,b,c) ExAllocatePool(a,b)

   #ifdef DEBUG_MSGS
      #define _ExFreePool(_a) { \
         QCPNP_KdPrint(("\t\t\tfreeing: 0x%p \n", _a)); \
         ExFreePool(_a);_a=NULL;}
   #else
      #define _ExFreePool(_a) { ExFreePool(_a); _a=NULL; }
   #endif //DEBUG_MSGS

#endif

#define _freeUcBuf(_a) { \
     QCPNP_KdPrint(("_freeUcBuf: 0x%p \n", (_a).Buffer)); \
     if ( (_a).Buffer ) \
     { \
		 _ExFreePool( (_a).Buffer ); \
		 (_a).Buffer = NULL; \
		 (_a).MaximumLength = (_a).Length = 0; \
     } \
}

#define _freeString(_a) _freeUcBuf(_a)

#define _freeBuf(_a) { \
     QCPNP_KdPrint(("_freeBuf: 0x%p \n", (_a))); \
     if ( (_a) ) \
     { \
		 _ExFreePool( (_a) ); \
		 (_a) = NULL; \
     } \
}

#define _zeroUnicode(_a) { \
  (_a).Buffer = NULL; \
  (_a).MaximumLength = 0; \
  (_a).Length = 0; \
}

#define _zeroString(_a) { _zeroUnicode(_a) }
#define _zeroAnsi(_a) { _zeroUnicode(_a) }

#define _zeroStringPtr(_a) { \
  (_a) -> Buffer = NULL; \
  (_a) -> MaximumLength = 0; \
  (_a) -> Length = 0; \
}

#define _closeHandle(in, clue) \
        { \
           if ( (in) ) \
           { \
              KPROCESSOR_MODE procMode = ExGetPreviousMode(); \
              QCSER_DbgPrint2 \
              ( \
                 (QCSER_DBG_MASK_CONTROL|QCSER_DBG_MASK_READ|QCSER_DBG_MASK_WRITE), \
                 QCSER_DBG_LEVEL_DETAIL, \
                 ("<%s> %s closing handle 0x%p(HdlCnt %d) in mode %d-%d/%d\n", pDevExt->PortName, \
                       clue, in, ucHdlCnt, procMode, KernelMode, UserMode) \
              ); \
              if (procMode == KernelMode) \
              { \
                 if (STATUS_SUCCESS != (ZwClose(in))) \
                 { \
                    QCSER_DbgPrint \
                    ( \
                       (QCSER_DBG_MASK_CONTROL|QCSER_DBG_MASK_READ|QCSER_DBG_MASK_WRITE), \
                       QCSER_DBG_LEVEL_FORCE, \
                       ("<%s> %s closing handle err: procMode=%d-%d/%d\n", pDevExt->PortName, \
                          clue, procMode, KernelMode, UserMode) \
                    ); \
                 } \
                 else \
                 { \
                    ucHdlCnt--; \
                 } \
                 in=NULL; \
              } \
              else \
              { \
                 QCSER_DbgPrint \
                 ( \
                    (QCSER_DBG_MASK_CONTROL|QCSER_DBG_MASK_READ|QCSER_DBG_MASK_WRITE), \
                    QCSER_DBG_LEVEL_ERROR, \
                    ("<%s> %s closing handle err: wrong proc mode %d\n", pDevExt->PortName, \
                       clue, procMode) \
                 ); \
              } \
           } \
        }

// The handle is closed regardless of processor mode
#define _closeHandleG(x, in, clue) \
        { \
           if ( (in) ) \
           { \
              KPROCESSOR_MODE procMode = ExGetPreviousMode(); \
              QCSER_DbgPrintG2 \
              ( \
                 (QCSER_DBG_MASK_CONTROL|QCSER_DBG_MASK_READ|QCSER_DBG_MASK_WRITE), \
                 QCSER_DBG_LEVEL_DETAIL, \
                 ("<%s> %s closing handle 0x%p(HdlCnt %d) in mode %d-%d/%d\n", x, \
                       clue, in, ucHdlCnt, procMode, KernelMode, UserMode) \
              ); \
              if (STATUS_SUCCESS != (ZwClose(in))) \
              { \
                 QCSER_DbgPrintG \
                 ( \
                    (QCSER_DBG_MASK_CONTROL|QCSER_DBG_MASK_READ|QCSER_DBG_MASK_WRITE), \
                    QCSER_DBG_LEVEL_ERROR, \
                    ("<%s> %s closing handle err: prevMode=%d-%d/%d\n", x, clue, procMode, KernelMode, UserMode) \
                 ); \
              } \
              else \
              { \
                 ucHdlCnt--; \
                 in=NULL; \
              } \
           } \
        }

#define _closeRegKey(in,clue)    _closeHandleG((pDevExt->PortName),in,clue)
#define _closeRegKeyG(x,in,clue) _closeHandleG(x,in,clue)

#ifdef QCSER_DBGPRINT
   #define QCSER_DbgPrintG(mask,level,_x_) \
           { \
              if ( ((gVendorConfig.DebugMask & mask) && \
                    (gVendorConfig.DebugLevel >= level)) || \
                   (level == QCSER_DBG_LEVEL_FORCE) ) \
              { \
                 DbgPrint _x_; \
              }    \
           }

   #define QCSER_DbgPrintX(x,mask,level,_x_) \
           { \
              if ( (((x)->DebugMask & mask) && \
                    ((x)->DebugLevel >= level)) || \
                   (level == QCSER_DBG_LEVEL_FORCE) ) \
              { \
                 DbgPrint _x_; \
              }    \
           }

   #define QCSER_DbgPrint(mask,level,_x_) \
           { \
              if ( ((pDevExt->DebugMask & mask) && \
                    (pDevExt->DebugLevel >= level)) || \
                   (level == QCSER_DBG_LEVEL_FORCE) ) \
              { \
                 DbgPrint _x_; \
              }    \
           }

   #ifdef QCSER_DBGPRINT2
      #define QCSER_DbgPrint2(mask,level,_x_) QCSER_DbgPrint(mask,level,_x_)
      #define QCSER_DbgPrintX2(x,mask,level,_x_) QCSER_DbgPrintX(x,mask,level,_x_)
      #define QCSER_DbgPrintG2(mask,level,_x_) QCSER_DbgPrintG(mask,level,_x_)
   #else
      #define QCSER_DbgPrint2(mask,level,_x_)
      #define QCSER_DbgPrintX2(mask,level,_x_)
      #define QCSER_DbgPrintG2(mask,level,_x_)
   #endif
#else
   #define QCSER_DbgPrint(mask,level,_x_)
   #define QCSER_DbgPrint2(mask,level,_x_)
   #define QCSER_DbgPrintX(x,mask,level,_x_)
   #define QCSER_DbgPrintX2(x,mask,level,_x_)
   #define QCSER_DbgPrintG(mask,level,_x_)
   #define QCSER_DbgPrintG2(mask,level,_x_)
#endif // QCSER_DBGPRINT

#ifdef DBG
   #ifndef DEBUG_MSGS
      #define DEBUG_MSGS
   #endif // !DEBUG_MSGS
   #ifndef DEBUG_IRPS
      #define DEBUG_IRPS
   #endif
#endif // DBG

#define QCPNP_StringForSysState( sysState )  szSystemPowerState[ sysState ] 
#define QCPNP_StringForDevState( devState )  szDevicePowerState[ devState ] 

#ifdef DEBUG_MSGS
// #define DEBUG_IRPS

   extern BOOLEAN bDbgout; // because Windbg does dbgout over serial line.
   extern ULONG ulIrpsCompleted;

   #ifdef DEBUG_IRPS
      #define _IoCompleteRequest(pIrp,_b_) {DbgPrint("\n\n@@@@@ COMPLETE IRP 0x%p\n",pIrp);_QcIoCompleteRequest(pIrp,_b_);}
   #else // !DEBUG_IRPS
      #define _IoCompleteRequest IoCompleteRequest
   #endif // !DEBUG_IRPS

   // #define _int3 { KIRQL ki3= KeGetCurrentIrql(); _asm int 3 }
   #define _int3 
   #define QCPNP_KdPrint(_x_) if (bDbgout) { DbgPrint("<%s>: ", gDeviceName); DbgPrint _x_ ; }

   #undef ASSERT // when Windbg fails ASSERT(bTrue) it breaks & prints "bTrue" but doesn't point to source code
   #define ASSERT(_x_) {if (!(_x_)) _int3;}

   #define _dbgPrintUnicodeString(a,b) if (bDbgout) { dbgPrintUnicodeString(a,b); }

#else //DEBUG_MSGS

   #ifndef _int3
      #define _int3
   #endif // _int3

   #define _IoCompleteRequest(_a_,_b_) IoCompleteRequest(_a_,_b_)
   #define QCPNP_KdPrint(_x_)
   #define _dbgPrintUnicodeString(a,b)

#endif //DEBUG_MSGS

#endif // QCDBG_H
