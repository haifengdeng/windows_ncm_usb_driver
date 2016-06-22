/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

Module Name:

    devcon.h

Abstract:

    Device Console header

--*/

#include <windows.h>
#include <tchar.h>
#include <stdlib.h>
#include <stdio.h>
#include <setupapi.h>
#include <regstr.h>
#include <cfgmgr32.h>
#include <string.h>
#include <malloc.h>
#include <WinBase.h>
#include <objbase.h>
#include <strsafe.h>
//#include <newdev.h>
#include <Objbase.h>
#include <Shlwapi.h>

#include "infstr.h"
#include "msg.h"
#include "rc_ids.h"

#ifndef ARRAYSIZE
#define ARRAYSIZE(a)                (sizeof(a)/sizeof(a[0]))
#endif

extern FILE *g_stream;
typedef int (*CallbackFunc)(__in HDEVINFO Devs, __in PSP_DEVINFO_DATA DevInfo, __in DWORD Index, __in LPVOID Context);

#ifndef ARRAYSIZE
#define ARRAYSIZE(x) (sizeof(x)/sizeof(x[0]))
#endif

#define INSTANCEID_PREFIX_CHAR TEXT('@') // character used to prefix instance ID's
#define CLASS_PREFIX_CHAR      TEXT('=') // character used to prefix class name
#define WILD_CHAR              TEXT('*') // wild character
#define QUOTE_PREFIX_CHAR      TEXT('\'') // prefix character to ignore wild characters
#define SPLIT_COMMAND_SEP      TEXT(":=") // whole word, indicates end of id's

//
// Devcon.exe command line flags
//
#define DEVCON_FLAG_FORCE       0x00000001

void FormatToStream(__in FILE * stream, __in DWORD fmt,...);
void Padding(__in int pad);
int    EnumerateDevices(__in LPCTSTR Machine, __in DWORD Flags, __in int argc, __in PZPWSTR argv, __in CallbackFunc Callback, __in LPVOID Context);
LPTSTR GetDeviceStringProperty(__in HDEVINFO Devs, __in PSP_DEVINFO_DATA DevInfo, __in DWORD Prop);
LPTSTR GetDeviceDescription(__in HDEVINFO Devs, __in PSP_DEVINFO_DATA DevInfo);
LPTSTR * GetDevMultiSz(__in HDEVINFO Devs, __in PSP_DEVINFO_DATA DevInfo, __in DWORD Prop);
LPTSTR * GetRegMultiSz(__in HKEY hKey, __in LPCTSTR Val);
 LPTSTR * GetMultiSzIndexArray( LPTSTR MultiSz);
void DelMultiSz( PZPWSTR Array);
LPTSTR * CopyMultiSz( PZPWSTR Array);

BOOL DumpArray(__in int pad, __in PZPWSTR Array);
BOOL DumpDevice(__in HDEVINFO Devs, __in PSP_DEVINFO_DATA DevInfo);
BOOL DumpDeviceClass(__in HDEVINFO Devs, __in PSP_DEVINFO_DATA DevInfo);
BOOL DumpDeviceDescr(__in HDEVINFO Devs, __in PSP_DEVINFO_DATA DevInfo);
BOOL DumpDeviceStatus(__in HDEVINFO Devs, __in PSP_DEVINFO_DATA DevInfo);
BOOL DumpDeviceResources(__in HDEVINFO Devs, __in PSP_DEVINFO_DATA DevInfo);
BOOL DumpDeviceDriverFiles(__in HDEVINFO Devs, __in PSP_DEVINFO_DATA DevInfo);
BOOL DumpDeviceDriverNodes(__in HDEVINFO Devs, __in PSP_DEVINFO_DATA DevInfo);
BOOL DumpDeviceHwIds(__in HDEVINFO Devs, __in PSP_DEVINFO_DATA DevInfo);
BOOL DumpDeviceWithInfo(__in HDEVINFO Devs, __in PSP_DEVINFO_DATA DevInfo, __in_opt LPCTSTR Info);
BOOL DumpDeviceStack(__in HDEVINFO Devs, __in PSP_DEVINFO_DATA DevInfo);
BOOL DumpDriverPackageData(__in LPCTSTR InfName);
BOOL Reboot();


//
// UpdateDriverForPlugAndPlayDevices
//
typedef BOOL (WINAPI *UpdateDriverForPlugAndPlayDevicesProto)(__in HWND hwndParent,
                                                              __in LPCTSTR HardwareId,
                                                              __in LPCTSTR FullInfPath,
                                                              __in DWORD InstallFlags,
                                                              __out_opt PBOOL bRebootRequired
                                                         );
typedef BOOL (WINAPI *SetupSetNonInteractiveModeProto)(__in BOOL NonInteractiveFlag
                                                      );
typedef BOOL (WINAPI *SetupUninstallOEMInfProto)(__in LPCTSTR InfFileName,
                                                 __in DWORD Flags,
                                                 __reserved PVOID Reserved
                                                 );

#if _SETUPAPI_VER >= _WIN32_WINNT_WINXP
typedef BOOL (WINAPI *SetupVerifyInfFileProto)(__in LPCTSTR InfName,
                                               __in_opt PSP_ALTPLATFORM_INFO_V2 AltPlatformInfo,
                                               __inout PSP_INF_SIGNER_INFO InfSignerInfo );
#endif

#ifdef _UNICODE
#define UPDATEDRIVERFORPLUGANDPLAYDEVICES "UpdateDriverForPlugAndPlayDevicesW"
#define SETUPUNINSTALLOEMINF "SetupUninstallOEMInfW"
#else
#define UPDATEDRIVERFORPLUGANDPLAYDEVICES "UpdateDriverForPlugAndPlayDevicesA"
#define SETUPUNINSTALLOEMINF "SetupUninstallOEMInfA"
#endif
#define SETUPSETNONINTERACTIVEMODE "SetupSetNonInteractiveMode"
#define SETUPVERIFYINFFILE "SetupVerifyInfFile"

//
// exit codes
//
#define EXIT_OK      (0)
#define EXIT_REBOOT  (1)
#define EXIT_FAIL    (2)
#define EXIT_USAGE   (3)
enum OSVERSION
{
	OS_2000,
	OS_XP,
	OS_2003,
	OS_VISTA,
	OS_7,
	OS_8,
	OS_9X,
	OS_UNKNOWN
};
enum SYSBIT
{
	SB_32,
	SB_64
};
