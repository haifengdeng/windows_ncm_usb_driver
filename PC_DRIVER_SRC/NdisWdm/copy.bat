copy /y C:\Users\Administrator\Desktop\PC-end\NdisWdm\objchk_wxp_x86\i386\ndiswdm.pdb E:\Windows\Symbols\NdisSymbols\
copy /y C:\Users\Administrator\Desktop\PC-end\NdisWdm\objchk_wxp_x86\i386\ndiswdm.sys C:\Users\Administrator\Desktop\PC-end\sysdriver\x86
"C:\Program Files\TortoiseSVN\bin\TortoiseProc.exe" /command:commit /logmsg:"fix bug" /path:C:\Users\Administrator\Desktop\PC-end\sysdriver\x86\ndiswdm.sys /closeonend:1
