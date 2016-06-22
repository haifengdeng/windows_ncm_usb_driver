// DriverInstall.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "devcon.h"
#include "operation.h"
#include <stdio.h>
#include <tchar.h>
using namespace MSXML2;
FILE *g_stream;
TCHAR g_ModulePath[MAX_PATH+1];
TCHAR g_OSVersion[10][MAX_PATH]={_T("Win2000"),_T("WinXp"),_T("Win2003"),_T("WinVista"),_T("Win7"),_T("Win8"),_T("Win9x"),_T("WinUnknown")};
TCHAR g_SBArray[10][MAX_PATH]={_T("x86"),_T("x64")};
// Macro that calls a COM method returning HRESULT value.
#define CHK_HR(stmt)        do { hr=(stmt); if (FAILED(hr)) goto CleanUp; } while(0)

// Macro to verify memory allcation.
#define CHK_ALLOC(p)        do { if (!(p)) { hr = E_OUTOFMEMORY; goto CleanUp; } } while(0)

// Macro that releases a COM object if not NULL.
#define SAFE_RELEASE(p)     do { if ((p)) { (p)->Release(); (p) = NULL; } } while(0)\

// Helper function to create a VT_BSTR variant from a null terminated string. 
HRESULT VariantFromString(LPCTSTR wszValue, VARIANT &Variant)
{
	HRESULT hr = S_OK;
	BSTR bstr = SysAllocString(wszValue);
	CHK_ALLOC(bstr);

	V_VT(&Variant)   = VT_BSTR;
	V_BSTR(&Variant) = bstr;

CleanUp:
	return hr;
}

// Helper function to create a DOM instance. 
HRESULT CreateAndInitDOM(MSXML2::IXMLDOMDocument **ppDoc)
{
	HRESULT hr = CoCreateInstance(__uuidof(DOMDocument60), NULL, CLSCTX_INPROC_SERVER, __uuidof(*ppDoc), reinterpret_cast<void**>(ppDoc));
	if (SUCCEEDED(hr))
	{
		// these methods should not fail so don't inspect result
		(*ppDoc)->put_async(VARIANT_FALSE);  
		(*ppDoc)->put_validateOnParse(VARIANT_FALSE);
		(*ppDoc)->put_resolveExternals(VARIANT_FALSE);
		(*ppDoc)->put_preserveWhiteSpace(VARIANT_TRUE);
	}
	return hr;
}
// Helper function to display parse error.
// It returns error code of the parse error.
HRESULT ReportParseError(MSXML2::IXMLDOMDocument *pDoc, TCHAR *szDesc)
{
	HRESULT hr = S_OK;
	HRESULT hrRet = E_FAIL; // Default error code if failed to get from parse error.
	MSXML2::IXMLDOMParseError *pXMLErr = NULL;
	BSTR bstrReason = NULL;

	CHK_HR(pDoc->get_parseError(&pXMLErr));
	CHK_HR(pXMLErr->get_errorCode(&hrRet));
	CHK_HR(pXMLErr->get_reason(&bstrReason));
	_ftprintf(g_stream,_T("%s\n%S\n"), szDesc, bstrReason);

CleanUp:
	SAFE_RELEASE(pXMLErr);
	SysFreeString(bstrReason);
	return hrRet;
}

// 获取exe所在文件夹路径   
// remark:   
// lpPath is the folder paths   
// dwBufferSize is the lpPath size equal sizeof(lpPath)   
void GetAppPath(TCHAR* lpPath, DWORD dwBufferSize)   
{   
	ZeroMemory(lpPath, dwBufferSize);   
	TCHAR exePath[MAX_PATH];   
	DWORD dwFile;   
	// exe全文件路径与GetCurrentDirectory不一样，   
	// 后者是当前目录而不是exe的目录   
	dwFile = GetModuleFileName(NULL, exePath, MAX_PATH);   
	if ( ERROR_SUCCESS== dwFile)   
	{      
		TCHAR errBuffer[20];   
		wsprintf(errBuffer, _T("%s%d"),TEXT("获取exe文件路径出错,错误码："), GetLastError() );   

		_ftprintf(g_stream,_T("%s"),errBuffer);   
	}   
	//   Returns a pointer to the last occurrence of c in     string, or NULL if c is not found.   
	TCHAR *pLastSlash = _tcsrchr(exePath,_T( '/')); 
	if(pLastSlash==NULL)
		pLastSlash=_tcsrchr(exePath,_T( '\\'));
	_tcsncpy(lpPath, exePath, (pLastSlash - exePath));   
}   

void UnInstallInfFile()
{
	DWORD dwType=REG_DWORD;
	DWORD OEMSize=0;
	DWORD cbData=sizeof(DWORD);
	DWORD ResetOemSize=0;
	TCHAR   SUBKEY[MAX_PATH];
	TCHAR   oemFile[MAX_PATH+1];
    SHGetValue(HKEY_LOCAL_MACHINE,_T("SOFTWARE\\Sh technologies\\CurrentOEM\\"),_T("OEMSize"),&dwType,&OEMSize,&cbData);
	for(int i=0;i<OEMSize;i++)
	{
		dwType=REG_SZ;
		cbData=MAX_PATH;
		wsprintf(SUBKEY,_T("%d"),i);
		SHGetValue(HKEY_LOCAL_MACHINE,_T("SOFTWARE\\Sh technologies\\CurrentOEM\\"),SUBKEY,&dwType,oemFile,&cbData);
		if(cbData>0)
		{
			DPDelete(oemFile);
		}
		SHDeleteValue(HKEY_LOCAL_MACHINE,_T("SOFTWARE\\Sh technologies\\CurrentOEM\\"),SUBKEY);
	}
	SHSetValue(HKEY_LOCAL_MACHINE,_T("SOFTWARE\\Sh technologies\\CurrentOEM\\"),_T("OEMSize"),REG_DWORD,&ResetOemSize,sizeof(DWORD));
}
void InstallInfFile(MSXML2::IXMLDOMNode *pFirstNode,TCHAR *pInfDirPath)
{
	MSXML2::IXMLDOMNode *pNode = NULL;
    MSXML2::IXMLDOMNode *pNextMode=NULL;
    MSXML2::IXMLDOMNode *pChildNode=NULL;
	TCHAR   FullInfPath[MAX_PATH];
	TCHAR   OEMName[50];
	TCHAR   SUBKEY[MAX_PATH];
	int error;
	HRESULT hr = S_OK;
	BSTR bstrNodeValue = NULL;
    DWORD  addsuccess=0;

	pNode=pFirstNode;
	while(pNode)
	{
		CHK_HR(pNode->selectSingleNode(_T("name"),&pChildNode));
		if(pChildNode)
		   CHK_HR(pChildNode->get_text(&bstrNodeValue));
		else
			goto hello;

        wsprintf(FullInfPath,_T("%s\\%s"),pInfDirPath,bstrNodeValue);

		 error=cmdDPAdd(FullInfPath,OEMName);
		if(error!=EXIT_FAIL)
		{
			wsprintf(SUBKEY,_T("%d"),addsuccess);
            SHSetValue(HKEY_LOCAL_MACHINE,_T("SOFTWARE\\Sh technologies\\CurrentOEM\\"),SUBKEY,REG_SZ,OEMName,_tcslen(OEMName)*sizeof(TCHAR));
			addsuccess++;
		}

		SysFreeString(bstrNodeValue);
		bstrNodeValue=NULL;
		SAFE_RELEASE(pChildNode);

 hello:
		CHK_HR(pNode->get_nextSibling(&pNextMode));

		if(pNode!=pFirstNode)
			SAFE_RELEASE(pNode);

		pNode=pNextMode;
		pNextMode=NULL;
	}
CleanUp:
	SHSetValue(HKEY_LOCAL_MACHINE,_T("SOFTWARE\\Sh technologies\\CurrentOEM\\"),_T("OEMSize"),REG_DWORD,&addsuccess,sizeof(DWORD));
	if(pNode!=pFirstNode)
	   SAFE_RELEASE(pNode);

	SAFE_RELEASE(pNextMode);
	SAFE_RELEASE(pChildNode);
	SysFreeString(bstrNodeValue);
}

void RemoveOrCopySysFile2DriverDir(MSXML2::IXMLDOMNode *pFirstNode,TCHAR *pSysFileDirPath,BOOL remove)
{
	MSXML2::IXMLDOMNode *pNode = NULL;
	MSXML2::IXMLDOMNode *pNextMode=NULL;
	MSXML2::IXMLDOMNode *pChildNameNode=NULL;
	MSXML2::IXMLDOMNode *pChildPathNode=NULL;
	TCHAR   WinDir[MAX_PATH+1];
	TCHAR   FullSysFilePath[MAX_PATH+1];
	TCHAR   FullDriverFilePath[MAX_PATH+1];
	int error;
	HRESULT hr = S_OK;

	long length;
	BSTR bstrNodeValue = NULL;
	BSTR bstrPathNodeValue=NULL;
    GetEnvironmentVariable(_T("WinDir"),WinDir,MAX_PATH);


	pNode=pFirstNode;
	while(pNode)
	{
		CHK_HR(pNode->selectSingleNode(_T("name"),&pChildNameNode));
		CHK_HR(pNode->selectSingleNode(_T("path"),&pChildPathNode));

		if(pChildNameNode&&pChildPathNode){
		   CHK_HR(pChildNameNode->get_text(&bstrNodeValue));
		   CHK_HR(pChildPathNode->get_text(&bstrPathNodeValue));
		}
		else
			goto hello;


		

        wsprintf(FullSysFilePath,_T("%s\\%s"),pSysFileDirPath,bstrNodeValue);
		wsprintf(FullDriverFilePath,_T("%s\\%s\\%s"),WinDir,bstrPathNodeValue,bstrNodeValue);
		if(remove)
		{
			DeleteFile(FullDriverFilePath);
		}else
		{
			CopyFile(FullSysFilePath,FullDriverFilePath,FALSE);
		}
       DWORD err=GetLastError();
		SysFreeString(bstrNodeValue);
		SysFreeString(bstrPathNodeValue);
		bstrNodeValue=NULL;
        bstrPathNodeValue=NULL;
		SAFE_RELEASE(pChildNameNode);
		SAFE_RELEASE(pChildPathNode);
	
hello:
		CHK_HR(pNode->get_nextSibling(&pNextMode));
		if(pNode!=pFirstNode)
		   SAFE_RELEASE(pNode);
		
		pNode=pNextMode;
		pNextMode=NULL;
	}
CleanUp:
	if(pNode!=pFirstNode)
	   SAFE_RELEASE(pNode);
	SAFE_RELEASE(pNextMode);
	SAFE_RELEASE(pChildNameNode);
	SAFE_RELEASE(pChildPathNode);
	SysFreeString(bstrNodeValue);
	SysFreeString(bstrPathNodeValue);
}


OSVERSION GetOSVersion()
{
	OSVERSIONINFO osverinfo;
	
	osverinfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&osverinfo);
	
	switch(osverinfo.dwPlatformId)
	{
	case VER_PLATFORM_WIN32_NT:
		if(osverinfo.dwMajorVersion == 5 && osverinfo.dwMinorVersion == 0)
		{
			return OS_2000;
		}
		if(osverinfo.dwMajorVersion == 5 && osverinfo.dwMinorVersion == 1)
		{
			return OS_XP;
		}
		if(osverinfo.dwMajorVersion == 5 && osverinfo.dwMinorVersion == 2)
		{
			return OS_2000;
		}
		if(osverinfo.dwMajorVersion == 6 && osverinfo.dwMinorVersion == 0)
		{
			return OS_VISTA;
		}
		if(osverinfo.dwMajorVersion == 6 && osverinfo.dwMinorVersion ==1)
		{
			return OS_7;
		}
		if(osverinfo.dwMajorVersion == 6 && osverinfo.dwMinorVersion == 3)
		{
			return OS_8;
		}
		if(osverinfo.dwMajorVersion <= 4)
		{
			return OS_9X;
		}
	default:
		return OS_UNKNOWN;
	}
	return OS_UNKNOWN;
}

SYSBIT GetSystemBits()
{
	SYSTEM_INFO si;
	GetNativeSystemInfo(&si);

	if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||    
			si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64 )
	{
			return SB_64;
	}
	return SB_32;	
}

void InstallOrUninstallDriverPacket(OSVERSION osVersion,SYSBIT Systembit,BOOL remove)
{
    HRESULT hr = S_OK;
    MSXML2::IXMLDOMDocument *pXMLDom = NULL;
    MSXML2::IXMLDOMNode *pInfNode = NULL;
	MSXML2::IXMLDOMNode *pSysNode = NULL;
    MSXML2::IXMLDOMNode *pFitableOsNode = NULL;

    BSTR bstrQueryOsVerison = NULL;
    BSTR bstrQueryInfFile = NULL;
    BSTR bstrQuerySysFile = NULL;

    VARIANT_BOOL varStatus;
    VARIANT varFileName;
    VariantInit(&varFileName);

	TCHAR FullxmlFilePath[MAX_PATH+1];
	TCHAR FullDriverPacketPath[MAX_PATH+1];
	TCHAR FullOsVersionDOMPath[MAX_PATH+1];
	wsprintf(FullxmlFilePath,_T("%s\\Driver\\%s"),g_ModulePath,_T("install.xml"));
	wsprintf(FullDriverPacketPath,_T("%s\\Driver\\%s"),g_ModulePath,g_SBArray[Systembit]);
	wsprintf(FullOsVersionDOMPath,_T("xml/install/system[@version='%s']"),g_OSVersion[osVersion]);

    CHK_HR(CreateAndInitDOM(&pXMLDom));

    CHK_HR(VariantFromString(FullxmlFilePath, varFileName));
    CHK_HR(pXMLDom->load(varFileName, &varStatus));
    if (varStatus != VARIANT_TRUE)
    {
        CHK_HR(ReportParseError(pXMLDom,_T( "Failed to load DOM from install.xml.")));
    }

    bstrQueryOsVerison = SysAllocString(FullOsVersionDOMPath);
    CHK_ALLOC(bstrQueryOsVerison);
    CHK_HR(pXMLDom->selectSingleNode(bstrQueryOsVerison, &pFitableOsNode));
    if (NULL==pFitableOsNode)
    {
        CHK_HR(ReportParseError(pXMLDom, _T("Error while Search fitable OS section in xml file.")));
		goto CleanUp;
    }
    bstrQueryInfFile = SysAllocString(_T("inf/file"));
    CHK_ALLOC(bstrQueryInfFile);
    CHK_HR(pFitableOsNode->selectSingleNode(bstrQueryInfFile, &pInfNode));
    if(NULL==pInfNode)
    {
        CHK_HR(ReportParseError(pXMLDom, _T("Error while search inf section in xml file.")));
		goto CleanUp;
    }
    bstrQuerySysFile = SysAllocString(_T("sys/file"));
    CHK_ALLOC(bstrQuerySysFile);
    CHK_HR(pFitableOsNode->selectSingleNode(bstrQuerySysFile, &pSysNode));
	if(NULL==pSysNode)
	{
		CHK_HR(ReportParseError(pXMLDom, _T("Error while search sys section in xml file.")));
		goto CleanUp;

	}

	UnInstallInfFile();
	if(FALSE==remove)
	   InstallInfFile(pInfNode,FullDriverPacketPath);
	RemoveOrCopySysFile2DriverDir(pSysNode,FullDriverPacketPath,remove);

CleanUp:
    SAFE_RELEASE(pXMLDom);
    SAFE_RELEASE(pInfNode);
	SAFE_RELEASE(pSysNode);
    SAFE_RELEASE(pFitableOsNode);
    SysFreeString(bstrQueryOsVerison);
    SysFreeString(bstrQueryInfFile);
    SysFreeString(bstrQuerySysFile);
    VariantClear(&varFileName);
}

int _tmain(int argc, _TCHAR* argv[])
{	
	OSVERSION osVersion;
	SYSBIT Systembit;
	BOOL   bunInstall=FALSE;
	g_stream=fopen("install.log","a");

	if(argc>=2&&0==_tcscmp(argv[1],_T("/uninstall")) )
	{
		bunInstall=TRUE;
	}
	GetAppPath(g_ModulePath,MAX_PATH);

    osVersion= GetOSVersion();
	Systembit= GetSystemBits();
    

	HRESULT hr = CoInitialize(NULL);
	if(SUCCEEDED(hr))
	{
		InstallOrUninstallDriverPacket(osVersion,Systembit,bunInstall);
		CoUninitialize();
	}
    cmdRemove(NULL,_T("usb\\vid_12d1*"));
	cmdRescan(NULL);
	if(g_stream)
	{
		fclose(g_stream);
		g_stream=NULL;
	}
	return 0;
}
