#include "stdafx.h"
#include <tchar.h> // Make all functions UNICODE safe.
#include <newdev.h> // for the API UpdateDriverForPlugAndPlayDevices().
#include <setupapi.h> // for SetupDiXxx functions.
#include "install.h"
int DisplayError(TCHAR * ErrorName)
{
	DWORD Err = GetLastError();
	LPVOID lpMessageBuffer = NULL;
	if (FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL,
		Err,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &lpMessageBuffer,
		0,
		NULL ))
		NULL;  //_tprintf(TEXT("%s FAILURE: %s/n"),ErrorName,(TCHAR *)lpMessageBuffer);
	else
		NULL;  //_tprintf(TEXT("%s FAILURE: (0x%08x)/n"),ErrorName,Err);
	if (lpMessageBuffer) LocalFree( lpMessageBuffer ); // Free system buffer

	SetLastError(Err);
	return FALSE;
}
BOOL FindExistingDevice(IN LPTSTR HardwareId)
{
	HDEVINFO DeviceInfoSet;
	SP_DEVINFO_DATA DeviceInfoData;
	DWORD i,err;
	BOOL Found;
	//
	// Create a Device Information Set with all present devices.
	//
	DeviceInfoSet = SetupDiGetClassDevs(NULL, // All Classes
		0,
		0,
		DIGCF_ALLCLASSES | DIGCF_PRESENT ); // All devices present on system
	if (DeviceInfoSet == INVALID_HANDLE_VALUE)
	{
		return DisplayError(TEXT("GetClassDevs(All Present Devices)"));

	}
	//_tprintf(TEXT("Search for Device ID: [%s]/n"),HardwareId);
	//
	//  Enumerate through all Devices.
	//
	Found = FALSE;
	DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	for (i=0;SetupDiEnumDeviceInfo(DeviceInfoSet,i,&DeviceInfoData);i++)
	{
		DWORD DataT;
		LPTSTR p,buffer = NULL;
		DWORD buffersize = 0;
		//
		// We won't know the size of the HardwareID buffer until we call
		// this function. So call it with a null to begin with, and then
		// use the required buffer size to Alloc the nessicary space.
		// Keep calling we have success or an unknown failure.
		//
		while (!SetupDiGetDeviceRegistryProperty(
			DeviceInfoSet,
			&DeviceInfoData,
			SPDRP_HARDWAREID,
			&DataT,
			(PBYTE)buffer,
			buffersize,
			&buffersize))
		{
			if (GetLastError() == ERROR_INVALID_DATA)
			{
				//
				// May be a Legacy Device with no HardwareID. Continue.
				//
				break;
			}
			else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				//
				// We need to change the buffer size.
				//
				if (buffer)
					LocalFree(buffer);
				buffer = (char *)LocalAlloc(LPTR,buffersize);
			}
			else
			{
				//
				// Unknown Failure.
				//
				DisplayError(TEXT("GetDeviceRegistryProperty"));
				goto cleanup_DeviceInfo;
			}
		}
		if (GetLastError() == ERROR_INVALID_DATA)
			continue;
		//
		// Compare each entry in the buffer multi-sz list with our HardwareID.
		//
		for (p=buffer;*p&&(p<&buffer[buffersize]);p+=lstrlen(p)+sizeof(TCHAR))

		{
			//_tprintf(TEXT("Compare device ID: [%s]/n"),p);
			if (!_tcscmp(HardwareId,p))
			{
				//_tprintf(TEXT("Found! [%s]/n"),p);
				Found = TRUE;
				break;
			}
		}
		if (buffer) LocalFree(buffer);
		if (Found) break;
	}
	if (GetLastError() != NO_ERROR)
	{
		DisplayError(TEXT("EnumDeviceInfo"));
	}
	//
	//  Cleanup.
	//
cleanup_DeviceInfo:
	err = GetLastError();
	SetupDiDestroyDeviceInfoList(DeviceInfoSet);
	SetLastError(err);
	return err == NO_ERROR; //???
}
BOOL
InstallRootEnumeratedDriver(IN  LPTSTR HardwareId,
							IN  LPTSTR INFFile,
							OUT PBOOL  RebootRequired  OPTIONAL
							)
{
	HDEVINFO DeviceInfoSet = 0;
	SP_DEVINFO_DATA DeviceInfoData;
	GUID ClassGUID;
	TCHAR ClassName[MAX_CLASS_NAME_LEN];
	DWORD err;
	//
	// Use the INF File to extract the Class GUID.
	//
	if (!SetupDiGetINFClass(INFFile,&ClassGUID,ClassName,sizeof(ClassName),0))

	{
		return DisplayError(TEXT("GetINFClass"));
	}
	//
	// Create the container for the to-be-created Device Information Element.
	//
	DeviceInfoSet = SetupDiCreateDeviceInfoList(&ClassGUID,0);
	if(DeviceInfoSet == INVALID_HANDLE_VALUE)
	{
		return DisplayError(TEXT("CreateDeviceInfoList"));
	}
	//
	// Now create the element.
	// Use the Class GUID and Name from the INF file.
	//
	DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	if (!SetupDiCreateDeviceInfo(DeviceInfoSet,
		ClassName,
		&ClassGUID,
		NULL,
		0,
		DICD_GENERATE_ID,
		&DeviceInfoData))
	{
		DisplayError(TEXT("CreateDeviceInfo"));
		goto cleanup_DeviceInfo;
	}
	//
	// Add the HardwareID to the Device's HardwareID property.
	//
	if(!SetupDiSetDeviceRegistryProperty(DeviceInfoSet,
		&DeviceInfoData,
		SPDRP_HARDWAREID,
		(LPBYTE)HardwareId,
		(lstrlen(HardwareId)+1+1)*sizeof(TCHAR)))
	{
		DisplayError(TEXT("SetDeviceRegistryProperty"));
		goto cleanup_DeviceInfo;
	}
	//
	// Transform the registry element into an actual devnode
	// in the PnP HW tree.
	//
	if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE,
		DeviceInfoSet,
		&DeviceInfoData))
	{
		DisplayError(TEXT("CallClassInstaller(REGISTERDEVICE)"));
		goto cleanup_DeviceInfo;
	}
	//
	// The element is now registered. We must explicitly remove the
	// device using DIF_REMOVE, if we encounter any failure from now on.
	//
	//
	// Install the Driver.
	//
	if (!UpdateDriverForPlugAndPlayDevices(0,
		HardwareId,
		INFFile,
		INSTALLFLAG_FORCE,
		RebootRequired))
	{
		DWORD err = GetLastError();
		DisplayError(TEXT("UpdateDriverForPlugAndPlayDevices"));
		if (!SetupDiCallClassInstaller(
			DIF_REMOVE,
			DeviceInfoSet,
			&DeviceInfoData))
		{
			DisplayError(TEXT("CallClassInstaller(REMOVE)"));
		}
		SetLastError(err);
	}
	//
	//  Cleanup.
	//
cleanup_DeviceInfo:
	err = GetLastError();
	SetupDiDestroyDeviceInfoList(DeviceInfoSet);
	SetLastError(err);
	return err == NO_ERROR;
}
int InstallDriver(_TCHAR *InfName, _TCHAR *HardwareID)
{
	WIN32_FIND_DATA FindFileData;
	BOOL RebootRequired = 0; // Must be cleared.
	_TCHAR *FName, *HWID;
	FName = InfName;
	HWID = HardwareID;
	if (FindFirstFile(FName,&FindFileData)==INVALID_HANDLE_VALUE)
	{
		//_tprintf(TEXT("  File not found./n"));
		//_tprintf(TEXT("usage: install <INF_File> <Hardware_ID>/n"));
		return 2; // Install Failure
	}
	//
	// Look to see if this device allready exists.
	//
	if (FindExistingDevice(HWID))
	{
		//
		// No Need to Create a Device Node, just call our API.
		//
		if (!UpdateDriverForPlugAndPlayDevices(0, // No Window Handle
			HWID, // Hardware ID
			FName, // FileName
			INSTALLFLAG_FORCE,
			&RebootRequired))
		{
			DisplayError(TEXT("UpdateDriverForPlugAndPlayDevices"));
			return 2; // Install Failure
		}
	}
	else
	{
		if (GetLastError()!= ERROR_NO_MORE_ITEMS)
		{
			//
			// An unknown failure from FindExistingDevice()
			//
			//_tprintf(TEXT("(IERROR_NO_MORE_ITEMS)/n"));
			//_tprintf(TEXT("(Install Failure! Code = 2)/n"));
			return 2; // Install Failure
		}
		//
		// Driver Does not exist, Create and call the API.
		// HardwareID must be a multi-sz string, which argv[2] is.
		//
		if (!InstallRootEnumeratedDriver(HWID, // HardwareID
			FName, // FileName
			&RebootRequired))
		{
			//_tprintf(TEXT("(InstallRootEnumeratedDriver Failure! Code = 2)/n"));
				return 2; // Install Failure
		}
	}
	//_tprintf(TEXT("Driver Installed successfully./n"));
	if (RebootRequired)
	{
		//_tprintf(TEXT("(Reboot Required)/n"));
		return 1; // Install Success, reboot required.
	}
	return 0; // Install Success, no reboot required.
}
int RemoveDriver(_TCHAR *HardwareID)
{
	HDEVINFO DeviceInfoSet;
	SP_DEVINFO_DATA DeviceInfoData;
	DWORD i,err;
	DeviceInfoSet = SetupDiGetClassDevs(NULL, // All Classes
		0,
		0,
		DIGCF_ALLCLASSES | DIGCF_PRESENT ); // All devices present on system
	if (DeviceInfoSet == INVALID_HANDLE_VALUE)
	{
		DisplayError(TEXT("GetClassDevs(All Present Devices)"));
		return 1;
	}
	//
	//  Enumerate through all Devices.
	//
	DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	for (i=0;SetupDiEnumDeviceInfo(DeviceInfoSet,i,&DeviceInfoData);i++)
	{
		DWORD DataT;
		LPTSTR p,buffer = NULL;
		DWORD buffersize = 0;
		//
		// We won't know the size of the HardwareID buffer until we call
		// this function. So call it with a null to begin with, and then
		// use the required buffer size to Alloc the nessicary space.
		// Keep calling we have success or an unknown failure.
		//
		while (!SetupDiGetDeviceRegistryProperty(
			DeviceInfoSet,
			&DeviceInfoData,
			SPDRP_HARDWAREID,
			&DataT,
			(PBYTE)buffer,
			buffersize,
			&buffersize))
		{
			if (GetLastError() == ERROR_INVALID_DATA)
			{
				//
				// May be a Legacy Device with no HardwareID. Continue.
				//
				break;
			}
			else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				//
				// We need to change the buffer size.
				//
				if (buffer)
					LocalFree(buffer);
				buffer = (char *)LocalAlloc(LPTR,buffersize);
			}
			else
			{
				//
				// Unknown Failure.
				//
				DisplayError(TEXT("GetDeviceRegistryProperty"));
				goto cleanup_DeviceInfo;
			}
		}
		if (GetLastError() == ERROR_INVALID_DATA)
			continue;
		//
		// Compare each entry in the buffer multi-sz list with our HardwareID.

		//
		for (p=buffer;*p&&(p<&buffer[buffersize]);p+=lstrlen(p)+sizeof(TCHAR))

		{
			//_tprintf(TEXT("Compare device ID: [%s]/n"),p);
			if (!_tcscmp(HardwareID,p))
			{
				//_tprintf(TEXT("Found! [%s]/n"),p);
				//
				// Worker function to remove device.
				//
				if (!SetupDiCallClassInstaller(DIF_REMOVE,
					DeviceInfoSet,
					&DeviceInfoData))
				{
					DisplayError(TEXT("CallClassInstaller(REMOVE)"));
				}
				break;
			}
		}
		if (buffer) LocalFree(buffer);
	}
	if ((GetLastError()!=NO_ERROR)&&(GetLastError()!=ERROR_NO_MORE_ITEMS))
	{
		DisplayError(TEXT("EnumDeviceInfo"));
	}
	//
	//  Cleanup.
	//
cleanup_DeviceInfo:
	err = GetLastError();
	SetupDiDestroyDeviceInfoList(DeviceInfoSet);
	return err;
}