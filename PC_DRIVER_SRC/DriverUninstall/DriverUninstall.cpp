// DriverUninstall.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "DriverUninstall.h"
#include "DriverUninstallDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CDriverUninstallApp

BEGIN_MESSAGE_MAP(CDriverUninstallApp, CWinApp)
	ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()


// CDriverUninstallApp construction

CDriverUninstallApp::CDriverUninstallApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}
enum SYSBIT
{
	SB_32,
	SB_64
};

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
	//   Returns a pointer to the last occurrence of c in     string, or NULL if c is not found.   
	TCHAR *pLastSlash = _tcsrchr(exePath,_T( '/')); 
	if(pLastSlash==NULL)
		pLastSlash=_tcsrchr(exePath,_T( '\\'));
	_tcsncpy(lpPath, exePath, (pLastSlash - exePath));   
}   

// The one and only CDriverUninstallApp object

CDriverUninstallApp theApp;


// CDriverUninstallApp initialization

BOOL CDriverUninstallApp::InitInstance()
{
	// InitCommonControlsEx() is required on Windows XP if an application
	// manifest specifies use of ComCtl32.dll version 6 or later to enable
	// visual styles.  Otherwise, any window creation will fail.
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// Set this to include all the common control classes you want to use
	// in your application.
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinApp::InitInstance();

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	// of your final executable, you should remove from the following
	// the specific initialization routines you do not need
	// Change the registry key under which our settings are stored
	// TODO: You should modify this string to be something appropriate
	// such as the name of your company or organization
	SetRegistryKey(_T("Local AppWizard-Generated Applications"));

	SYSBIT SysBits= GetSystemBits();
	TCHAR 	g_ModulePath[MAX_PATH+1];
	GetAppPath(g_ModulePath,MAX_PATH);
	CString ModulePath=g_ModulePath;
	if(SysBits==SB_32)
	{
		ShellExecute(NULL,_T("open"),ModulePath+_T("\\DriverInstall_x32.exe"),_T("/uninstall"),NULL,SW_HIDE);
	}
	else if(SysBits==SB_64)
	{
		ShellExecute(NULL,_T("open"),ModulePath+_T("\\DriverInstall_x64.exe"),_T("/uninstall"),NULL,SW_HIDE);
	}
	//CDriverUninstallDlg dlg;
	//m_pMainWnd = &dlg;
	//INT_PTR nResponse = dlg.DoModal();
	//if (nResponse == IDOK)
	//{
	//	// TODO: Place code here to handle when the dialog is
	//	//  dismissed with OK
	//}
	//else if (nResponse == IDCANCEL)
	//{
	//	// TODO: Place code here to handle when the dialog is
	//	//  dismissed with Cancel
	//}

	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return FALSE;
}
