echo off

REM need to define QCUSB_DDK_DIR,    example: set QCUSB_DDK_DIR=C:\NTDDK
REM need to define QCUSB_SOURCE_DIR, example: set QCUSB_SOURCE_DIR=C:\usb\src
REM need to define QCUSB_TARGET_DIR, example: set QCUSB_TARGET_DIR=C:\usb\cs\target

echo.
echo  -------------------------------------------------------
echo  ^|       Qualcomm USB Host Driver Building Process      ^|
echo  ^| Copyright (c) 2006 QUALCOMM Inc. All Rights Reserved ^|
echo  --------------------------------------------------------
echo.

if "%1"=="/?" goto usage
if "%1"=="-?" goto usage
if "%1"=="\?" goto usage
if "%1"==""   goto usage

if "%1"=="/xpddk" goto build_on_xp
if "%1"=="/2kddk" goto build_on_2k
if "%1"=="/svsp1" goto build_on_sv
if "%1"=="/wdk6k" goto build_on_lh
goto usage

REM -------- Build with XP DDK ---------
:build_on_xp
echo Compliling Windows XP checked/debug build with Windows XP DDK...
start /I "XP Debug Build" /MAX /WAIT CMD.EXE /C qcpl.bat -x xp -d

echo Compliling Windows XP free/non-debug build with Windows XP DDK...
start /I "XP Free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -x xp

echo Compliling Windows 2000 checked/debug build with Windows XP DDK...
REM start /I "2K Debug Build " /MAX /WAIT CMD.EXE /C qcpl.bat -x 2k -d

echo Compliling Windows 2000 free/non-debug build with Windows XP DDK...
REM start /I "2K Free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -x 2k

REM echo Compliling Windows IA64 checked/debug build with Windows XP DDK...
REM start /I "64-bit Debug Build " /MAX /WAIT CMD.EXE /C qcpl.bat -x ia64 -d

REM echo Compliling Windows IA64 free/non-debug build with Windows XP DDK...
REM start /I "64-bit Free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -x ia64

goto EndOfBuild

REM -------- Build with 2K DDK ---------
:build_on_2k
echo Compliling Windows 2000 checked/debug build with Windows 2000 DDK...
start /I "2K Debug Build " /MAX /WAIT CMD.EXE /C qcpl.bat -k -d

echo Compliling Windows 2000 free/non-debug build with Windows 2000 DDK...
start /I "2K Free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -k

goto EndOfBuild

REM -------- Build with Server 2003 SP1 DDK ---------
:build_on_sv
echo Compliling Windows XP checked/debug build with Windows Server 2003 SP1 DDK...
start /I "XP Debug Build" /MAX /WAIT CMD.EXE /C qcpl.bat -s xp -d

echo Compliling Windows XP free/non-debug build with Windows Server 2003 SP1 DDK...
start /I "XP Free Build" /MAX /WAIT CMD.EXE /C qcpl.bat -s xp

echo Compliling Windows 2000 checked/debug build with Windows Server 2003 SP1 DDK...
start /I "2K Debug Build " /MAX /WAIT CMD.EXE /C qcpl.bat -s 2k -d

echo Compliling Windows 2000 free/non-debug build with Windows Server 2003 SP1 DDK...
start /I "2K Free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -s 2k

echo Compliling Windows Server 2003 x86 checked build with Windows Server 2003 SP1 DDK...
start /I "Server 2003 x86 Debug Build " /MAX /WAIT CMD.EXE /C qcpl.bat -s x86 -d

echo Compliling Windows Server 2003 x86 free build with Windows Server 2003 SP1 DDK...
start /I "Server 2003 x86 Free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -s x86

echo Compliling Windows IA64 checked/debug build with Windows Server 2003 SP1 DDK...
start /I "IA64 Debug Build " /MAX /WAIT CMD.EXE /C qcpl.bat -s ia64 -d

echo Compliling Windows IA64 free/non-debug build with Windows Server 2003 SP1 DDK...
start /I "IA64 Free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -s ia64

echo Compliling Windows AMD64 checked/debug build with Windows Server 2003 SP1 DDK...
start /I "AMD64 Debug Build " /MAX /WAIT CMD.EXE /C qcpl.bat -s amd64 -d

echo Compliling Windows AMD64 free/non-debug build with Windows Server 2003 SP1 DDK...
start /I "AMD64 free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -s amd64

goto EndOfBuild

REM -------- Build with WDK 6000 ---------
:build_on_lh
echo Compliling Windows XP checked/debug build with WDK
start /I "XP Debug Build" /MAX /WAIT CMD.EXE /C qcpl.bat -l xp -d

echo Compliling Windows XP free/non-debug build with WDK
start /I "XP Free Build" /MAX /WAIT CMD.EXE /C qcpl.bat -l xp

echo Compliling Windows 2000 checked/debug build with WDK
start /I "2K Debug Build " /MAX /WAIT CMD.EXE /C qcpl.bat -l 2k -d

echo Compliling Windows 2000 free/non-debug build with WDK
start /I "2K Free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -l 2k

echo Compliling Windows Server 2003 x86 checked build with WDK
start /I "Server 2003 x86 Debug Build " /MAX /WAIT CMD.EXE /C qcpl.bat -l x86 -d

echo Compliling Windows Server 2003 x86 free build with WDK
start /I "Server 2003 x86 Free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -l x86

echo Compliling Windows Server 2003 IA64 checked/debug build with WDK
start /I "IA64 Debug Build " /MAX /WAIT CMD.EXE /C qcpl.bat -l ia64 -d

echo Compliling Windows Server 2003 IA64 free/non-debug build with WDK
start /I "IA64 Free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -l ia64

echo Compliling Windows Server 2003 AMD64 checked/debug build with WDK
start /I "AMD64 Debug Build " /MAX /WAIT CMD.EXE /C qcpl.bat -l amd64 -d

echo Compliling Windows Server 2003 AMD64 free/non-debug build with WDK
start /I "AMD64 free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -l amd64

echo Compliling Windows Vista/Server x86 checked build with WDK
start /I "Server 2003 x86 Debug Build " /MAX /WAIT CMD.EXE /C qcpl.bat -l x86v -d

echo Compliling Windows Vista/Server x86 free build with WDK
start /I "Server 2003 x86 Free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -l x86v

echo Compliling Windows Vista/Server IA64 checked/debug build with WDK
start /I "IA64 Debug Build " /MAX /WAIT CMD.EXE /C qcpl.bat -l ia64v -d

echo Compliling Windows Vista/Server IA64 free/non-debug build with WDK
start /I "IA64 Free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -l ia64v

echo Compliling Windows Vista/Server AMD64 checked/debug build with WDK
start /I "AMD64 Debug Build " /MAX /WAIT CMD.EXE /C qcpl.bat -l amd64v -d

echo Compliling Windows Vista/Server AMD64 free/non-debug build with WDK
start /I "AMD64 free Build " /MAX /WAIT CMD.EXE /C qcpl.bat -l amd64v

goto EndOfBuild


:EndOfBuild
echo     ............. End of Build Process .............
echo.
echo.
goto EndAll

:usage
echo.
echo usage: blddrv ^</xpddk^|/2kddk^|/svsp1^|/wdk6k^>
echo.
echo   Example:  blddrv /xpddk - build images with Windows XP DDK
echo   Example:  blddrv /2kddk - build images with Windows 2000 DDK
echo   Example:  blddrv /svsp1 - build images with Windows Server 2003 SP1 DDK
echo   Example:  blddrv /wdk6k - build images with WDK 6000
echo   Copyright (c) 2007 by Qualcomm Incorporated. All rights reserved.
echo.
echo.

:EndAll
