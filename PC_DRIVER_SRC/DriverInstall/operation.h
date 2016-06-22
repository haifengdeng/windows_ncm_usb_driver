#pragma  once

int cmdDPEnumLegacy();
int DPDelete(LPCTSTR strInf);
int cmdDPAdd( LPCTSTR strInf,LPTSTR oemName);
int cmdClassFilter(__in LPCTSTR Machine, LPCTSTR className,LPCTSTR strtype,_TCHAR*  strArray[],int size);
int cmdRescan(__in LPCTSTR Machine);
int cmdRemove(__in LPCTSTR Machine,LPTSTR HardwareId);