#include "ndiswdm.h"
#include "usbpnp.h"
NTSTATUS UsbclassTypeRequestCall
(
 IN PMP_ADAPTER                Adapter,
 UCHAR                         bmRequestType,
 UCHAR                         bmRequest,
 USHORT                        wValue,
 USHORT                        wIndex,
 USHORT                        wLength,
 PVOID                         pData,
 ULONG*                        pRetSize
 )
{	 
	NTSTATUS ntStatus;
	USHORT FlagsWord;
	USHORT wRecipient, wType;
	USHORT UrbFunction;
	ULONG size;
	PURB  pUrb;
	//
	//init var
	//
	pUrb      =NULL;
	ntStatus       =STATUS_SUCCESS; 
	FlagsWord = USBD_SHORT_TRANSFER_OK;
	wType     = bmRequestType &USB_TYPE_MASK;
	wRecipient= bmRequestType&USB_RECIP_MASK;

	if(NULL!=pRetSize)        (*pRetSize)= 0;
	if (bmRequestType & USB_DIR_IN)
	{
		FlagsWord |= USBD_TRANSFER_DIRECTION_IN;
	}
	ASSERT(wType==USB_TYPE_CLASS);

	switch( wRecipient ) 
	{
	case USB_RECIP_DEVICE:
		{
			UrbFunction = URB_FUNCTION_CLASS_DEVICE;
			break;
		}
	case USB_RECIP_INTERFACE:
		{
			UrbFunction = URB_FUNCTION_CLASS_INTERFACE;
			break;
		}
	case USB_RECIP_ENDPOINT:
		{
			UrbFunction = URB_FUNCTION_CLASS_ENDPOINT;
			break;
		}
	default:        
		{
			ntStatus = STATUS_INVALID_PARAMETER;
			goto func_return;
		}
	}

	size = sizeof( struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST );
	pUrb = ExAllocatePoolWithTag( NonPagedPool, size ,NIC_TAG);
	if ( !pUrb )
	{
		ntStatus =  STATUS_NO_MEMORY;
		goto func_return;
	}
	RtlZeroMemory( pUrb, size );      

	UsbBuildVendorRequest (
		pUrb,
		UrbFunction,
		(USHORT)size,         //size of this URB
		FlagsWord,
		0,            //reserved bits
		bmRequest,      //request
		(USHORT)wValue,   //value
		(USHORT)wIndex,   //index (zero interface?)
		pData,            //transfer buffer
		NULL,          
		wLength,      //size of transfer buffer
		(PURB)NULL      //URB link
		);

	ntStatus = CallUSBD( Adapter, pUrb );

	if (NT_SUCCESS( ntStatus )&&NULL!=pRetSize)
	{
		*pRetSize = pUrb -> UrbControlVendorClassRequest.TransferBufferLength;
	}
func_return:
	USB_EXFreePool(pUrb);
	return ntStatus;
}