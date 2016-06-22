@echo off

REM ============== About Vista Test Certificate =================
REM Ignoe the following steps if the driver is built with a DDK other
REM than the Vista WDK.
REM
REM In order to test sign the driver for Windows Vista,
REM follow the steps below:
REM
REM 1) On the driver build PC (Vista), open any WDK build environment
REM    and make a directory, for example
REM    C:>md MyCert
REM
REM 2) In the WDK build environment, create a test certificate in the
REM    certificate store trustedpublisher. For example,
REM    C:\MyCert>makecert -r -pe -ss trustedpublisher -n CN=USBHostDriver(Test002) qcusbtest.cer
REM    This generates a certificate file qcusbtest.cer in C:\MyCert
REM 
REM 3) Add the certificate (qcusbtest.cer) to the root store. For example,
REM    C:\MyCert>Certmgr.exe -add qcusbtest.cer -s -r localMachine root
REM
REM 4) If a user uses a different certificate name other than USBHostDriver(Test002),
REM    make sure the QCUSB_CERT_NAME in this script is set to the new name.
REM
REM 5) Make sure the build PC is connected to the internet.
REM
REM 6) Build the driver and the driver binaries will be test signed. For example,
REM    C:\Driver>blddrv /wdk6k
REM
REM If the driver is to be installed on another Vista PC, install the certificate first
REM on that PC. For example,
REM    C:\DriverCert>Certmgr.exe -add qcusbtest.cer -s -r localMachine root
REM    C:\DriverCert>Certmgr.exe -add qcusbtest.cer -s -r localMachine trustedpublisher
REM 
REM ==============================================================

set QCUSB_CERT_NAME=USBHostDriver(Test002)

set QCUSB_DDK_DIR=C:\WinDDK\7600.16385.1

if "%QCUSB_DDK_DRIVE%"=="" set QCUSB_DDK_DRIVE=C:

if "%QCUSB_SOURCE_DIR%"=="" set QCUSB_SOURCE_DIR=%CD%
if not exist %QCUSB_SOURCE_DIR% goto error_path_src

if "%QCUSB_TARGET_DIR%"==""     set QCUSB_TARGET_DIR=%QCUSB_SOURCE_DIR%
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\target
set QCUSB_VISTA_BUILD=no
set QCUSB_CERT_FILE=%QCUSB_SOURCE_DIR%\private\qcusbtest.pfx
set QCUSB_TEST_CERT=%QCUSB_SOURCE_DIR%\private\stuff
if not exist %QCUSB_TARGET_DIR%\TestCertificate md %QCUSB_TARGET_DIR%\TestCertificate
if not exist %QCUSB_TARGET_DIR%\TestCertificate\qcusbtest.cer goto copy_test_cert
goto build_start

:copy_test_cert
copy %QCUSB_TEST_CERT%\qcusbtest.cer %QCUSB_TARGET_DIR%\TestCertificate
copy %QCUSB_TEST_CERT%\README.txt    %QCUSB_TARGET_DIR%\TestCertificate
if exist  %QCUSB_SOURCE_DIR%\certmgr.exe copy %QCUSB_SOURCE_DIR%\certmgr.exe %QCUSB_TARGET_DIR%\TestCertificate
goto build_start

:error_path_ddk
echo ERROR: Invalid DDK path: %QCUSB_DDK_DIR%
goto end_all

:error_path_src
echo ERROR: Invalid source path: %QCUSB_SOURCE_DIR%
goto end_all

:build_start
echo QCUSB_DDK_DIR=%QCUSB_DDK_DIR%
echo QCUSB_SOURCE_DIR=%QCUSB_SOURCE_DIR%
echo QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%

if "%1"=="/?" goto usage
if "%1"=="-?" goto usage
if "%1"=="\?" goto usage
if "%1"=="" goto usage

if "%1"=="-x" goto build_from_xp
if "%1"=="-k" goto build_from_2k
if "%1"=="-s" goto build_from_sv
if "%1"=="-l" goto build_from_lh
goto usage

:build_from_xp
if "%QCUSB_DDK_DIR%"==""    set QCUSB_DDK_DIR=%QCUSB_DDK_DRIVE%\WINDDK\2600.1106
if not exist %QCUSB_DDK_DIR%    goto error_path_ddk
if not exist %QCUSB_DDK_DIR%\bin\setenv.bat goto error_path_ddk

if "%2"=="xp" goto build_xp_driver
if "%2"=="2k" goto build_2k_driver
if "%2"=="ia64" goto build_ia64_driver
goto usage

:build_xp_driver
if "%3"=="-d" goto build_xp_driver_dbg
if "%3"==""   goto build_xp_driver_free
goto usage

:build_2k_driver
if "%3"=="-d" goto build_2k_driver_dbg
if "%3"==""   goto build_2k_driver_free
goto usage

:build_ia64_driver
if "%3"=="-d" goto build_ia64_driver_dbg
if "%3"==""   goto build_ia64_driver_free

:build_from_2k
if "%QCUSB_DDK_DIR%"==""    set QCUSB_DDK_DIR=%QCUSB_DDK_DRIVE%\NTDDK
if not exist %QCUSB_DDK_DIR%    goto error_path_ddk

if "%2"=="2k" goto build_2k_from_2k
if "%2"==""   goto build_2k_from_2k_free
if "%2"=="-d" goto build_2k_from_2k_dbg
goto usage

:build_2k_from_2k
if "%3"=="-d" goto build_2k_from_2k_dbg
if "%3"==""   goto build_2k_from_2k_free
goto usage

:build_from_sv
if "%QCUSB_DDK_DIR%"==""    set QCUSB_DDK_DIR=%QCUSB_DDK_DRIVE%\WINDDK\3790.1830
if not exist %QCUSB_DDK_DIR%    goto error_path_ddk

if "%2"=="xp" goto build_xp_driver_from_sv
if "%2"=="2k" goto build_2k_driver_from_sv
if "%2"=="x86" goto build_x86_driver_from_sv
if "%2"=="ia64" goto build_ia64_driver_from_sv
if "%2"=="amd64" goto build_amd64_driver_from_sv
goto usage

:build_xp_driver_from_sv
if "%3"=="-d" goto build_xp_driver_from_sv_dbg
if "%3"==""   goto build_xp_driver_from_sv_free
goto usage

:build_2k_driver_from_sv
if "%3"=="-d" goto build_2k_driver_from_sv_dbg
if "%3"==""   goto build_2k_driver_from_sv_free
goto usage

:build_x86_driver_from_sv
if "%3"=="-d" goto build_x86_driver_from_sv_dbg
if "%3"==""   goto build_x86_driver_from_sv_free
goto usage

:build_ia64_driver_from_sv
if "%3"=="-d" goto build_ia64_driver_from_sv_dbg
if "%3"==""   goto build_ia64_driver_from_sv_free
goto usage

:build_amd64_driver_from_sv
if "%3"=="-d" goto build_amd64_driver_from_sv_dbg
if "%3"==""   goto build_amd64_driver_from_sv_free
goto usage

:build_from_lh
if "%QCUSB_DDK_DIR%"==""    set QCUSB_DDK_DIR=%QCUSB_DDK_DRIVE%\WINDDK\6000
if not exist %QCUSB_DDK_DIR%    goto error_path_ddk

if "%2"=="2k" goto build_2k_driver_from_lh
if "%2"=="xp" goto build_xp_driver_from_lh
if "%2"=="x86" goto build_x86_driver_from_lh
if "%2"=="ia64" goto build_ia64_driver_from_lh
if "%2"=="amd64" goto build_amd64_driver_from_lh
if "%2"=="x86v" goto build_lh_x86_driver_from_lh
if "%2"=="ia64v" goto build_lh_ia64_driver_from_lh
if "%2"=="amd64v" goto build_lh_amd64_driver_from_lh
goto usage

:build_2k_driver_from_lh
if "%3"=="-d" goto build_2k_driver_from_lh_dbg
if "%3"==""   goto build_2k_driver_from_lh_free
goto usage

:build_xp_driver_from_lh
if "%3"=="-d" goto build_xp_driver_from_lh_dbg
if "%3"==""   goto build_xp_driver_from_lh_free
goto usage

:build_x86_driver_from_lh
if "%3"=="-d" goto build_x86_driver_from_lh_dbg
if "%3"==""   goto build_x86_driver_from_lh_free
goto usage

:build_ia64_driver_from_lh
if "%3"=="-d" goto build_ia64_driver_from_lh_dbg
if "%3"==""   goto build_ia64_driver_from_lh_free
goto usage

:build_amd64_driver_from_lh
if "%3"=="-d" goto build_amd64_driver_from_lh_dbg
if "%3"==""   goto build_amd64_driver_from_lh_free
goto usage

:build_lh_x86_driver_from_lh
set QCUSB_VISTA_BUILD=yes
if "%3"=="-d" goto build_lh_x86_driver_from_lh_dbg
if "%3"==""   goto build_lh_x86_driver_from_lh_free
goto usage

:build_lh_ia64_driver_from_lh
set QCUSB_VISTA_BUILD=yes
if "%3"=="-d" goto build_lh_ia64_driver_from_lh_dbg
if "%3"==""   goto build_lh_ia64_driver_from_lh_free
goto usage

:build_lh_amd64_driver_from_lh
set QCUSB_VISTA_BUILD=yes
if "%3"=="-d" goto build_lh_amd64_driver_from_lh_dbg
if "%3"==""   goto build_lh_amd64_driver_from_lh_free
goto usage


REM *********************************************
REM ************ Windows XP DDK SP1 *************
REM *********************************************
:build_xp_driver_dbg
echo ================ build_xp_driver_dbg_from_xp ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% chk
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.CHK SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\WinXP\checked

build -e -w -c
cd objchk_wxp_x86\i386
goto post_build

:build_xp_driver_free
echo ================ build_xp_driver_free_from_xp ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% fre
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.FRE SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\WinXP\free
build -e -w -c
cd objfre_wxp_x86\i386
goto post_build

:build_2k_driver_dbg
echo ================ build_2k_driver_dbg_from_xp ==================
call %QCUSB_DDK_DIR%\bin\w2k\set2k.bat %QCUSB_DDK_DIR% checked
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESK.CHK SOURCES
copy qcmdm2k.inf qcmdm.inf
copy qcser2k.inf qcser.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\Win2K\checked
build -e -w -c
cd objchk_w2k_x86\i386
goto post_build

:build_2k_driver_free
echo ================ build_2k_driver_free_from_xp ==================
call %QCUSB_DDK_DIR%\bin\w2k\set2k.bat %QCUSB_DDK_DIR% free
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESK.FRE SOURCES
copy qcmdm2k.inf qcmdm.inf
copy qcser2k.inf qcser.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\Win2K\free
build -e -w -c
cd objfre_w2k_x86\i386
goto post_build

:build_ia64_driver_dbg
echo ================ build_ia64_driver_dbg_from_xp ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% chk 64
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.CHK SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win64\IA64\checked
build -e -w -c
cd objchk_wxp_ia64\ia64
goto post_build

:build_ia64_driver_free
echo ================ build_ia64_driver_free_from_xp ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% fre 64
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.FRE SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win64\IA64\free
build -e -w -c
cd objfre_wxp_ia64\ia64
goto post_build

REM *********************************************
REM ************** Windows 2K DDK ***************
REM *********************************************
:build_2k_from_2k_dbg
echo ================ build_2k_from_2k_dbg ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% checked
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESK.CHK SOURCES
copy qcmdm2k.inf qcmdm.inf
copy qcser2k.inf qcser.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win2K\checked
build -e -w -c
cd objchk\i386
goto post_build

:build_2k_from_2k_free
echo ================ build_2k_from_2k_free ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% free
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESK.FRE SOURCES
copy qcmdm2k.inf qcmdm.inf
copy qcser2k.inf qcser.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win2K\free
build -e -w -c
cd objfre\i386
goto post_build

REM *********************************************
REM ********* Windows Server 2003 SP1 DDK *******
REM *********************************************
:build_xp_driver_from_sv_dbg
echo ================ build_xp_from_sv_dbg ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% chk WXP
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.CHK SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\WinXP\checked

build -e -w -c
cd objchk_wxp_x86\i386
goto post_build

:build_xp_driver_from_sv_free
echo ================ build_xp_from_sv_free ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% fre WXP
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.FRE SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\WinXP\free

build -e -w -c
cd objfre_wxp_x86\i386
goto post_build

:build_2k_driver_from_sv_dbg
echo ================ build_2k_from_sv_dbg ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% w2k c
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESK.CHK SOURCES
copy qcmdm2k.inf qcmdm.inf
copy qcser2k.inf qcser.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\Win2K\checked
build -e -w -c
cd objchk_w2k_x86\i386
goto post_build

:build_2k_driver_from_sv_free
echo ================ build_2k_from_sv_free ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% w2k f
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESK.FRE SOURCES
copy qcmdm2k.inf qcmdm.inf
copy qcser2k.inf qcser.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\Win2K\free
build -e -w -c
cd objfre_w2k_x86\i386
goto post_build

:build_x86_driver_from_sv_dbg
echo ================ build_x86_from_sv_dbg ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% chk WNET
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.CHK SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\WNETx86\checked
build -e -w -c
cd objchk_wnet_x86\i386
goto post_build

:build_x86_driver_from_sv_free
echo ================ build_x86_from_sv_free ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% fre WNET
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.FRE SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\WNETx86\free
build -e -w -c
cd objfre_wnet_x86\i386
goto post_build

:build_ia64_driver_from_sv_dbg
echo ================ build_IA64_from_sv_dbg ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% chk 64 WNET
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.CHK SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win64\IA64\checked
build -e -w -c
cd objchk_wnet_ia64\ia64
goto post_build

:build_ia64_driver_from_sv_free
echo ================ build_IA64_from_sv_free ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% fre 64 WNET
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.FRE SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win64\IA64\free
build -e -w -c
cd objfre_wnet_ia64\ia64
goto post_build

:build_amd64_driver_from_sv_dbg
echo ================ build_AMD64_from_sv_dbg ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% chk AMD64 WNET
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.CHK SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win64\AMD64\checked
build -e -w -c
cd objchk_wnet_amd64\amd64
goto post_build

:build_amd64_driver_from_sv_free
echo ================ build_AMD64_from_sv_free ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% fre AMD64 WNET
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.FRE SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win64\AMD64\free
build -e -w -c
cd objfre_wnet_amd64\amd64
goto post_build

REM *********************************************
REM ***************** WDK 6000 ******************
REM *********************************************
:build_2k_driver_from_lh_dbg
echo ================ build_2k_from_lh_dbg ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% w2k c
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESK.CHK SOURCES
copy qcmdm2k.inf qcmdm.inf
copy qcser2k.inf qcser.inf
copy qcserxp.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\Win2K\checked
build -e -w -c
cd objchk_w2k_x86\i386
goto post_build

:build_2k_driver_from_lh_free
echo ================ build_2k_from_lh_free ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% w2k f
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESK.FRE SOURCES
copy qcmdm2k.inf qcmdm.inf
copy qcser2k.inf qcser.inf
copy qcserxp.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\Win2K\free
build -e -w -c
cd objfre_w2k_x86\i386
goto post_build

:build_xp_driver_from_lh_dbg
echo ================ build_xp_from_lh_dbg ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% chk WXP
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.CHK SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
copy qcserxp.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\WinXP\checked
build -e -w -c
cd objchk_wxp_x86\i386
goto post_build

:build_xp_driver_from_lh_free
echo ================ build_xp_from_lh_free ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% fre WXP
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.FRE SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
copy qcserxp.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\WinXP\free
build -e -w -c
cd objfre_wxp_x86\i386
goto post_build

:build_x86_driver_from_lh_dbg
echo ================ build_x86_from_lh_dbg ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% chk WNET
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.CHK SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy qcserxp.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\WNET\x86\checked
build -e -w -c
cd objchk_wnet_x86\i386
goto post_build

:build_x86_driver_from_lh_free
echo ================ build_x86_from_lh_free ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% fre WNET
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.FRE SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy qcserxp.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\WNET\x86\free
build -e -w -c
cd objfre_wnet_x86\i386
goto post_build

:build_ia64_driver_from_lh_dbg
echo ================ build_IA64_from_lh_dbg ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% chk 64 WNET
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.CHK SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
copy qcserxp.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win64\WNET\IA64\checked
build -e -w -c
cd objchk_wnet_ia64\ia64
goto post_build

:build_ia64_driver_from_lh_free
echo ================ build_IA64_from_lh_free ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% fre 64 WNET
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.FRE SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
copy qcserxp.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win64\WNET\IA64\free
build -e -w -c
cd objfre_wnet_ia64\ia64
goto post_build

:build_amd64_driver_from_lh_dbg
echo ================ build_AMD64_from_lh_dbg ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% chk AMD64 WNET
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.CHK SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
copy qcserxp.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win64\WNET\AMD64\checked
build -e -w -c
cd objchk_wnet_amd64\amd64
goto post_build

:build_amd64_driver_from_lh_free
echo ================ build_AMD64_from_lh_free ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% fre AMD64 WNET
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESX.FRE SOURCES
copy qcmdmxp.inf qcmdm.inf
copy qcserxp.inf qcser.inf
copy msmdmxp.inf msmdm.inf
copy qcserxp.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win64\WNET\AMD64\free
build -e -w -c
cd objfre_wnet_amd64\amd64
goto post_build

:build_lh_x86_driver_from_lh_dbg
echo ================ build_lh_x86_from_lh_dbg ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% chk WLH
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESV.CHK SOURCES
copy qcmdmwv.inf qcmdm.inf
copy qcserwv.inf qcser.inf
copy msmdmxp.inf msmdm.inf
copy qcserwv.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\VISTA\x86\checked
build -e -w -c
cd objchk_wlh_x86\i386
goto post_build

:build_lh_x86_driver_from_lh_free
echo ================ build_lh_x86_from_lh_free ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% fre WLH
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESV.FRE SOURCES
copy qcmdmwv.inf qcmdm.inf
copy qcserwv.inf qcser.inf
copy msmdmxp.inf msmdm.inf
copy qcserwv.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win32\VISTA\x86\free
build -e -w -c
cd objfre_wlh_x86\i386
goto post_build

:build_lh_ia64_driver_from_lh_dbg
echo ================ build_lh_IA64_from_lh_dbg ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% chk 64 WLH
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESV.CHK SOURCES
copy qcmdmwv.inf qcmdm.inf
copy qcserwv.inf qcser.inf
copy msmdmxp.inf msmdm.inf
copy qcserwv.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win64\VISTA\IA64\checked
build -e -w -c
cd objchk_wlh_ia64\ia64
goto post_build

:build_lh_ia64_driver_from_lh_free
echo ================ build_IA64_from_lh_free ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% fre 64 WLH
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESV.FRE SOURCES
copy qcmdmwv.inf qcmdm.inf
copy qcserwv.inf qcser.inf
copy msmdmxp.inf msmdm.inf
copy qcserwv.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win64\VISTA\IA64\free
build -e -w -c
cd objfre_wlh_ia64\ia64
goto post_build

:build_lh_amd64_driver_from_lh_dbg
echo ================ build_lh_AMD64_from_lh_dbg ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% chk AMD64 WLH
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESV.CHK SOURCES
copy qcmdmwv.inf qcmdm.inf
copy qcserwv.inf qcser.inf
copy msmdmxp.inf msmdm.inf
copy qcserwv.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win64\VISTA\AMD64\checked
build -e -w -c
cd objchk_wlh_amd64\amd64
goto post_build

:build_lh_amd64_driver_from_lh_free
echo ================ build_lh_AMD64_from_lh_free ==================
call %QCUSB_DDK_DIR%\bin\setenv.bat %QCUSB_DDK_DIR% fre AMD64 WLH
cd /D %QCUSB_SOURCE_DIR%
copy SOURCESV.FRE SOURCES
copy qcmdmwv.inf qcmdm.inf
copy qcserwv.inf qcser.inf
copy msmdmxp.inf msmdm.inf
copy qcserwv.cdf qcusbser.cdf
set QCUSB_TARGET_DIR=%QCUSB_TARGET_DIR%\Win64\VISTA\AMD64\free
build -e -w -c
cd objfre_wlh_amd64\amd64
goto post_build


REM *********************************************
REM ***************** Post Build ****************
REM *********************************************
:post_build
if not exist %QCUSB_TARGET_DIR% md %QCUSB_TARGET_DIR%
copy qcusbser.sys %QCUSB_TARGET_DIR%
copy qcusbser.pdb %QCUSB_TARGET_DIR%

dir qcusb*.sys

cd /D %QCUSB_SOURCE_DIR%
del *.sbr
copy logReader.exe %QCUSB_TARGET_DIR%
copy README.txt    %QCUSB_TARGET_DIR%
if "%QCUSB_VISTA_BUILD%"=="no" copy qcusb.inf %QCUSB_TARGET_DIR%
copy qcmdm.inf     %QCUSB_TARGET_DIR%
copy qcser.inf     %QCUSB_TARGET_DIR%
if exist msmdm.inf copy msmdm.inf %QCUSB_TARGET_DIR%

if "%1"=="-l" goto gen_cat_file
goto file_cleanup

REM create .cat file
:gen_cat_file
copy qcusbser.cdf %QCUSB_TARGET_DIR%
cd /D %QCUSB_TARGET_DIR%
%QCUSB_DDK_DIR%\bin\selfsign\makecat /v qcusbser.cdf
del qcusbser.cdf


REM Test-sign the driver if built within Qualcomm NA domain
if /i "%USERDNSDOMAIN%"=="na.qualcomm.com" goto test_sign_catalog
goto file_cleanup

:test_sign_catalog
if exist %QCUSB_CERT_FILE% goto sign_with_file
%QCUSB_DDK_DIR%\bin\selfsign\signtool.exe sign /v /s trustedpublisher /n %QCUSB_CERT_NAME% /t http://timestamp.verisign.com/scripts/timstamp.dll qcusbser.cat
goto file_cleanup

:sign_with_file
echo Test sign with Personal Information Exchange (PFX) file
%QCUSB_DDK_DIR%\bin\selfsign\signtool.exe sign /v /f %QCUSB_CERT_FILE% -p testcert /t http://timestamp.verisign.com/scripts/timstamp.dll qcusbser.cat

:file_cleanup
cd /D %QCUSB_SOURCE_DIR%
REM === remove intermidiate files ===
del *.log
if exist %QCUSB_SOURCE_DIR%\objchk_wxp_x86 rd /S/Q %QCUSB_SOURCE_DIR%\objchk_wxp_x86
if exist %QCUSB_SOURCE_DIR%\objfre_wxp_x86 rd /S/Q %QCUSB_SOURCE_DIR%\objfre_wxp_x86
if exist %QCUSB_SOURCE_DIR%\objchk_w2k_x86 rd /S/Q %QCUSB_SOURCE_DIR%\objchk_w2k_x86
REM ===if exist %QCUSB_SOURCE_DIR%\objfre_w2k_x86 rd /S/Q %QCUSB_SOURCE_DIR%\objfre_w2k_x86
if exist %QCUSB_SOURCE_DIR%\objchk_wxp_ia64 rd /S/Q %QCUSB_SOURCE_DIR%\objchk_wxp_ia64
if exist %QCUSB_SOURCE_DIR%\objfre_wxp_ia64 rd /S/Q %QCUSB_SOURCE_DIR%\objfre_wxp_ia64

if exist %QCUSB_SOURCE_DIR%\objchk_wnet_amd64 rd /S/Q %QCUSB_SOURCE_DIR%\objchk_wnet_amd64
if exist %QCUSB_SOURCE_DIR%\objchk_wnet_ia64 rd /S/Q %QCUSB_SOURCE_DIR%\objchk_wnet_ia64
if exist %QCUSB_SOURCE_DIR%\objchk_wnet_x86 rd /S/Q %QCUSB_SOURCE_DIR%\objchk_wnet_x86
if exist %QCUSB_SOURCE_DIR%\objfre_wnet_amd64 rd /S/Q %QCUSB_SOURCE_DIR%\objfre_wnet_amd64
if exist %QCUSB_SOURCE_DIR%\objfre_wnet_ia64 rd /S/Q %QCUSB_SOURCE_DIR%\objfre_wnet_ia64
if exist %QCUSB_SOURCE_DIR%\objfre_wnet_x86 rd /S/Q %QCUSB_SOURCE_DIR%\objfre_wnet_x86

if exist %QCUSB_SOURCE_DIR%\objchk_wlh_amd64 rd /S/Q %QCUSB_SOURCE_DIR%\objchk_wlh_amd64
if exist %QCUSB_SOURCE_DIR%\objchk_wlh_ia64 rd /S/Q %QCUSB_SOURCE_DIR%\objchk_wlh_ia64
if exist %QCUSB_SOURCE_DIR%\objchk_wlh_x86 rd /S/Q %QCUSB_SOURCE_DIR%\objchk_wlh_x86
if exist %QCUSB_SOURCE_DIR%\objfre_wlh_amd64 rd /S/Q %QCUSB_SOURCE_DIR%\objfre_wlh_amd64
if exist %QCUSB_SOURCE_DIR%\objfre_wlh_ia64 rd /S/Q %QCUSB_SOURCE_DIR%\objfre_wlh_ia64
if exist %QCUSB_SOURCE_DIR%\objfre_wlh_x86 rd /S/Q %QCUSB_SOURCE_DIR%\objfre_wlh_x86

goto end

:usage

echo.
echo usage: cpl ^<-x^|-k^|-l^> [xp^|2k^|lh] [-d]
echo.
echo   Example:  cpl -x xp -d   build checked XP driver under XP DDK
echo   Example:  cpl -k 2k      build free Win2K driver under 2K DDK
echo   Example:  cpl -l lh      build free Vista driver under WDK
echo   Copyright (c) 2007 by Qualcomm Incorporated. All rights reserved.
echo.
echo.

:end
if exist %QCUSB_SOURCE_DIR%\SOURCES del %QCUSB_SOURCE_DIR%\SOURCES
if exist %QCUSB_SOURCE_DIR%\qcmdm.inf del %QCUSB_SOURCE_DIR%\qcmdm.inf
if exist %QCUSB_SOURCE_DIR%\qcser.inf del %QCUSB_SOURCE_DIR%\qcser.inf
if exist %QCUSB_SOURCE_DIR%\msmdm.inf del %QCUSB_SOURCE_DIR%\msmdm.inf
if exist %QCUSB_SOURCE_DIR%\qcusbser.cdf del %QCUSB_SOURCE_DIR%\qcusbser.cdf

:end_all
REM pause
