#ifndef _USBWDM_H
#define _USBWDM_H
#include <usbdi.h>
#include <usbdlib.h>

NTSTATUS
FreeUsbPipes(
			 IN PMP_USBPIPE usbpipe
			 );
NTSTATUS
HandleStartDevice(
				  IN PMP_ADAPTER Adapter,BOOLEAN bIsReset
				  );
NTSTATUS
ReadandSelectDescriptors(
						 IN PMP_ADAPTER Adapter,BOOLEAN bIsReset
						 );
NTSTATUS
ConfigureDevice(
				IN PMP_ADAPTER Adapter,BOOLEAN bIsReset
				);
NTSTATUS
SelectInterfaces(
				 IN PMP_ADAPTER                Adapter,
				 IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,BOOLEAN bIsReset
				 );
NTSTATUS
DeconfigureDevice(
				  IN PMP_ADAPTER Adapter
				  );
NTSTATUS
CallUSBD(
		 IN PMP_ADAPTER Adapter,
		 IN PURB           Urb
		 );

NTSTATUS usbpnp_QueryCapabilities
(
 IN PMP_ADAPTER     Adapter,
 IN PDEVICE_CAPABILITIES DeviceCapabilities
 );
NTSTATUS 
ParseAssociatedDescriptors(PMP_ADAPTER Adapter,UCHAR *pDescriptor,int len);

#endif