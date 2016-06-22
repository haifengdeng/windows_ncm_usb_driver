/*++

Copyright (c) 2000  Microsoft Corporation

Module Name:

    bulkpnp.c

Abstract:

	Bulk USB device driver for Intel 82930 USB test board
	Plug and Play module.
    This file contains routines to handle pnp requests.
    These routines are not USB specific but is required
    for every driver which conforms to the WDM model.

Environment:

    Kernel mode

Notes:

    Copyright (c) 2000 Microsoft Corporation.  
    All Rights Reserved.

--*/

#include "ndiswdm.h"
#include "usbpnp.h"

NTSTATUS
FreeUsbPipes(
    IN PMP_USBPIPE usbpipe
	)
{
	if(usbpipe->InterfaceComm==usbpipe->InterfaceData)
		usbpipe->InterfaceData=NULL;
	USB_EXFreePool(usbpipe->pUsbConfigDesc);
	USB_EXFreePool(usbpipe->InterfaceComm);
	USB_EXFreePool(usbpipe->InterfaceData);
	USB_EXFreePool(usbpipe->ether_desc);
	USB_EXFreePool(usbpipe->ncm_desc);

	USB_EXFreePool(usbpipe->ntb_parameters);
	usbpipe->InterruptPipe =
	usbpipe->BulkPipeOutput=
	usbpipe->BulkPipeInput =(UCHAR)-1;
	return NDIS_STATUS_SUCCESS;
}

NTSTATUS
HandleStartDevice(
    IN PMP_ADAPTER Adapter,BOOLEAN bIsReset
    )
/*++
 
Routine Description:

    This is the dispatch routine for IRP_MN_START_DEVICE

Arguments:

    Adapter - pointer to a adapter object.

    Irp - I/O request packet

Return Value:

    NT status value

--*/
{
    NTSTATUS          ntStatus;
    PMP_USBPIPE       usbpipe;

    DEBUGP(3, ("HandleStartDevice - begins\n"));

    //
    // initialize variables,if the pointers is not NULL,release the memory
	// the pointers point to.
    //

	usbpipe =Adapter->UsbPipeForNIC;

	if(usbpipe->InterfaceComm==usbpipe->InterfaceData)
		usbpipe->InterfaceData=NULL;
	USB_EXFreePool(usbpipe->ether_desc);
	USB_EXFreePool(usbpipe->ncm_desc);
	USB_EXFreePool(usbpipe->pUsbConfigDesc);
	USB_EXFreePool(usbpipe->InterfaceComm);
	USB_EXFreePool(usbpipe->InterfaceData);

    usbpipe->InterruptPipe =
	usbpipe->BulkPipeOutput=
	usbpipe->BulkPipeInput =(UCHAR)-1;

    //
    // Read the device descriptor, configuration descriptor 
    // and select the interface descriptors
    //

    ntStatus = ReadandSelectDescriptors(Adapter,bIsReset);

    if(!NT_SUCCESS(ntStatus)) {

        DEBUGP(1, ("ReadandSelectDescriptors failed\n"));
        return ntStatus;
    }

    DEBUGP(3, ("HandleStartDevice - ends\n"));

    return ntStatus;
}


NTSTATUS
ReadandSelectDescriptors(
    IN PMP_ADAPTER Adapter,BOOLEAN bIsReset
    )
/*++
 
Routine Description:

    This routine configures the USB device.
    In this routines we get the device descriptor, 
    the configuration descriptor and select the
    configuration descriptor.

Arguments:

    Adapter - pointer to a adapter object

Return Value:

    NTSTATUS - NT status value.

--*/
{
    PURB                   urb;
    ULONG                  siz;
    NTSTATUS               ntStatus;
    PUSB_DEVICE_DESCRIPTOR deviceDescriptor;
    
    //
    // initialize variables
    //

    urb = NULL;
    deviceDescriptor = NULL;

    //
    // 1. Read the device descriptor
    //

    urb =(PURB) ExAllocatePoolWithTag(NonPagedPool, 
                         sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),NIC_TAG);

    if(urb) {

        siz = sizeof(USB_DEVICE_DESCRIPTOR);
        deviceDescriptor = (PUSB_DEVICE_DESCRIPTOR)ExAllocatePoolWithTag(NonPagedPool, siz,NIC_TAG);

        if(deviceDescriptor) {

            UsbBuildGetDescriptorRequest(
                    urb, 
                    (USHORT) sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                    USB_DEVICE_DESCRIPTOR_TYPE, 
                    0, 
                    0, 
                    deviceDescriptor, 
                    NULL, 
                    siz, 
                    NULL);

            ntStatus = CallUSBD(Adapter, urb);

            if(NT_SUCCESS(ntStatus)) {

                ASSERT(deviceDescriptor->bNumConfigurations);
                ntStatus = ConfigureDevice(Adapter,bIsReset);    
            }
                            
            ExFreePool(urb);                
            ExFreePool(deviceDescriptor);
        }
        else {

            DEBUGP(1, ("Failed to allocate memory for deviceDescriptor\n"));

            ExFreePool(urb);
            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    else {

        DEBUGP(1, ("Failed to allocate memory for urb\n"));

        ntStatus = STATUS_INSUFFICIENT_RESOURCES;
    }

    return ntStatus;
}

NTSTATUS
ConfigureDevice(
    IN PMP_ADAPTER Adapter,BOOLEAN bIsReset
    )
/*++

Routine Description:

    This helper routine reads the configuration descriptor
    for the device in couple of steps.

Arguments:

    Adapter - pointer to a adapter object

Return Value:

    NTSTATUS - NT status value

--*/
{
    PURB                          urb;
    ULONG                         siz;
    NTSTATUS                      ntStatus;
    PMP_USBPIPE                   usbpipe;
    PUSB_CONFIGURATION_DESCRIPTOR configurationDescriptor;

    //
    // initialize the variables
    //

    urb = NULL;
    configurationDescriptor = NULL;
    usbpipe = Adapter->UsbPipeForNIC;

    ASSERT(usbpipe->pUsbConfigDesc == NULL);

    //
    // Read the first configuration descriptor
    // This requires two steps:
    // 1. Read the fixed sized configuration descriptor (CD)
    // 2. Read the CD with all embedded interface and endpoint descriptors
    //

    urb = (PURB)ExAllocatePoolWithTag(NonPagedPool, 
                         sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),NIC_TAG);

    if(urb) {

        siz = sizeof(USB_CONFIGURATION_DESCRIPTOR);
        configurationDescriptor = (PUSB_CONFIGURATION_DESCRIPTOR)ExAllocatePoolWithTag(NonPagedPool, siz,NIC_TAG);

        if(configurationDescriptor) {

            UsbBuildGetDescriptorRequest(
                    urb, 
                    (USHORT) sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                    USB_CONFIGURATION_DESCRIPTOR_TYPE, 
                    0, 
                    0, 
                    configurationDescriptor,
                    NULL, 
                    sizeof(USB_CONFIGURATION_DESCRIPTOR), 
                    NULL);

            ntStatus = CallUSBD(Adapter, urb);

            if(!NT_SUCCESS(ntStatus)) {

                DEBUGP(1, ("UsbBuildGetDescriptorRequest failed\n"));
                goto ConfigureDevice_Exit;
            }
        }
        else {

            DEBUGP(1, ("Failed to allocate mem for config Descriptor\n"));

            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
            goto ConfigureDevice_Exit;
        }

        siz = configurationDescriptor->wTotalLength;

        ExFreePool(configurationDescriptor);

        configurationDescriptor = (PUSB_CONFIGURATION_DESCRIPTOR)ExAllocatePoolWithTag(NonPagedPool, siz,NIC_TAG);

        if(configurationDescriptor) {

            UsbBuildGetDescriptorRequest(
                    urb, 
                    (USHORT)sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                    USB_CONFIGURATION_DESCRIPTOR_TYPE,
                    0, 
                    0, 
                    configurationDescriptor, 
                    NULL, 
                    siz, 
                    NULL);

            ntStatus = CallUSBD(Adapter, urb);

            if(!NT_SUCCESS(ntStatus)) {

                DEBUGP(1,("Failed to read configuration descriptor\n"));
                goto ConfigureDevice_Exit;
            }
        }
        else {

            DEBUGP(1, ("Failed to alloc mem for config Descriptor\n"));
            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
            goto ConfigureDevice_Exit;
        }
    }
    else {

        DEBUGP(1, ("Failed to allocate memory for urb\n"));
        ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        goto ConfigureDevice_Exit;
    }

    if(configurationDescriptor) {

        //
        // save a copy of configurationDescriptor in deviceExtension
        // remember to free it later.
        //
        Adapter->UsbPipeForNIC->pUsbConfigDesc = configurationDescriptor;

        if(configurationDescriptor->bmAttributes & REMOTE_WAKEUP_MASK)
        {
            //
            // this configuration supports remote wakeup
            //
            Adapter->UsbPipeForNIC->WaitWakeEnable = 1;
        }
        else
        {
            Adapter->UsbPipeForNIC->WaitWakeEnable = 0;
        }

        ntStatus = SelectInterfaces(Adapter, configurationDescriptor,bIsReset);
    }

ConfigureDevice_Exit:

    if(urb) {

        ExFreePool(urb);
    }

    if(configurationDescriptor &&
       Adapter->UsbPipeForNIC->pUsbConfigDesc == NULL) {

        ExFreePool(configurationDescriptor);
    }

    return ntStatus;
}

NTSTATUS
SelectInterfaces(
    IN PMP_ADAPTER                Adapter,
    IN PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,BOOLEAN bIsReset
    )
/*++
 
Routine Description:

    This helper routine selects the configuration

Arguments:

    Adapter - pointer to Adapter object
    ConfigurationDescriptor - pointer to the configuration
    descriptor for the device

Return Value:

    NT status value

--*/
{
    LONG                        numberOfInterfaces, 
                                interfaceindex;
    ULONG                       i;
    PURB                        urb;
    NTSTATUS                    ntStatus;
    PMP_USBPIPE                 usbpipe;
    PUSB_INTERFACE_DESCRIPTOR   interfaceDescriptor;
    PUSBD_INTERFACE_LIST_ENTRY  interfaceList, 
                                tmp;
    PUSBD_INTERFACE_INFORMATION Interface;
	PVOID                       pStartPosition;
	PUSBD_PIPE_INFORMATION      pipeInformation;

    //
    // initialize the variables
    //

    urb = NULL;
    Interface = NULL;
    interfaceDescriptor = NULL;
    usbpipe =Adapter->UsbPipeForNIC;
    numberOfInterfaces = ConfigurationDescriptor->bNumInterfaces;
    interfaceindex  = 0;
	pStartPosition=NULL;
	pipeInformation=NULL;

    //
    // Parse the configuration descriptor for the interface;
    //

    tmp = interfaceList =
       (PUSBD_INTERFACE_LIST_ENTRY) ExAllocatePoolWithTag(
               NonPagedPool, 
               sizeof(USBD_INTERFACE_LIST_ENTRY) * (numberOfInterfaces + 1),NIC_TAG);

    if(!tmp) {

        DEBUGP(1, ("Failed to allocate mem for interfaceList\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

	pStartPosition = (PVOID)((PCHAR)ConfigurationDescriptor + ConfigurationDescriptor->bLength);
	ParseAssociatedDescriptors(Adapter,(UCHAR *)ConfigurationDescriptor,ConfigurationDescriptor->wTotalLength);

	interfaceDescriptor = (PUSB_INTERFACE_DESCRIPTOR)pStartPosition;
    while(interfaceDescriptor != NULL) {

        interfaceDescriptor = USBD_ParseConfigurationDescriptorEx(
                                            ConfigurationDescriptor, 
                                            pStartPosition,
                                            -1,
                                            -1, -1, -1, -1);

        if(interfaceDescriptor) {
			if(interfaceindex>0&&interfaceDescriptor->bAlternateSetting!=0){
				if(FALSE==bIsReset)
                   interfaceindex--;
				else
				{
					pStartPosition = (PVOID)((PCHAR)interfaceDescriptor + interfaceDescriptor->bLength);
					continue;
				}
			}

            interfaceList[interfaceindex].InterfaceDescriptor = interfaceDescriptor;
            interfaceList[interfaceindex].Interface = NULL;
        }
        
		if(interfaceDescriptor){ 
			interfaceindex++;  
		    pStartPosition = (PVOID)((PCHAR)interfaceDescriptor + interfaceDescriptor->bLength);
		}
    }

	if (interfaceindex<=numberOfInterfaces)
	{
		interfaceList[interfaceindex].InterfaceDescriptor = NULL;
		interfaceList[interfaceindex].Interface = NULL;
	}

    urb = USBD_CreateConfigurationRequestEx(ConfigurationDescriptor, tmp);

    if(urb) {

        Interface = &urb->UrbSelectConfiguration.Interface;

        for(i=0; i<Interface->NumberOfPipes; i++) {

            //
            // perform pipe initialization here
            // set the transfer size and any pipe flags we use
            // USBD sets the rest of the Interface struct members
            //

            Interface->Pipes[i].MaximumTransferSize = 
                                USBD_DEFAULT_MAXIMUM_TRANSFER_SIZE;
        }

        ntStatus = CallUSBD(Adapter, urb);

        if(NT_SUCCESS(ntStatus)) {

			//usbpipe->ConfigurationHandle=urb->UrbSelectConfiguration.ConfigurationHandle;
            //
            // save a copy of interface information in the device extension.
            //
            usbpipe->InterfaceComm = (PUSBD_INTERFACE_INFORMATION)ExAllocatePoolWithTag(NonPagedPool,
                                                           Interface->Length,NIC_TAG);

            if(usbpipe->InterfaceComm) {
                
                RtlCopyMemory(usbpipe->InterfaceComm,
                              Interface,
                              Interface->Length);
				usbpipe->InterfaceData=usbpipe->InterfaceComm;
            }
            else {

                ntStatus = STATUS_INSUFFICIENT_RESOURCES;
                DEBUGP(1, ("memory alloc for UsbInterface failed\n"));
            }

            //
            // Dump the interface to the debugger
            //

            Interface = &urb->UrbSelectConfiguration.Interface;

            DEBUGP(3, ("---------\n"));
            DEBUGP(3, ("NumberOfPipes 0x%x\n", 
                                 Interface->NumberOfPipes));
            DEBUGP(3, ("Length 0x%x\n", 
                                 Interface->Length));
            DEBUGP(3, ("Alt Setting 0x%x\n", 
                                 Interface->AlternateSetting));
            DEBUGP(3, ("Interface Number 0x%x\n", 
                                 Interface->InterfaceNumber));
            DEBUGP(3, ("Class, subclass, protocol 0x%x 0x%x 0x%x\n",
                                 Interface->Class,
                                 Interface->SubClass,
                                 Interface->Protocol));

            for(i=0; i<Interface->NumberOfPipes; i++) {

                DEBUGP(3, ("---------\n"));
                DEBUGP(3, ("PipeType 0x%x\n", 
                                     Interface->Pipes[i].PipeType));
                DEBUGP(3, ("EndpointAddress 0x%x\n", 
                                     Interface->Pipes[i].EndpointAddress));
                DEBUGP(3, ("MaxPacketSize 0x%x\n", 
                                    Interface->Pipes[i].MaximumPacketSize));
                DEBUGP(3, ("Interval 0x%x\n", 
                                     Interface->Pipes[i].Interval));
                DEBUGP(3, ("Handle 0x%x\n", 
                                     Interface->Pipes[i].PipeHandle));
                DEBUGP(3, ("MaximumTransferSize 0x%x\n", 
                                    Interface->Pipes[i].MaximumTransferSize));


				pipeInformation = &Interface->Pipes[i];
				if (pipeInformation->PipeType == UsbdPipeTypeBulk){
					if (((pipeInformation->EndpointAddress)&0x80) == 0){
						usbpipe->BulkPipeOutput =(UCHAR)i;
					}
					else{
						usbpipe -> BulkPipeInput =(UCHAR) i;
					}
				}
				else if (pipeInformation->PipeType == UsbdPipeTypeInterrupt){
					usbpipe->InterruptPipe = (UCHAR)i;
				} 
            }

            DEBUGP(3, ("---------\n"));
        }
        else {

            DEBUGP(1, ("Failed to select an interface\n"));
        }
    }
    else {
        
        DEBUGP(1, ("USBD_CreateConfigurationRequestEx failed\n"));
        ntStatus = STATUS_INSUFFICIENT_RESOURCES;
    }

	if(urb) {

		ExFreePool(urb);
	}

	USB_EXFreePool(tmp);
    return ntStatus;
}


NTSTATUS
DeconfigureDevice(
    IN PMP_ADAPTER Adapter
    )
/*++
 
Routine Description:

    This routine is invoked when the device is removed or stopped.
    This routine de-configures the usb device.

Arguments:

    Adapter - pointer to adapter object

Return Value:

    NT status value

--*/
{
    PURB     urb;
    ULONG    siz;
    NTSTATUS ntStatus;
    
    //
    // initialize variables
    //

    siz = sizeof(struct _URB_SELECT_CONFIGURATION);
    urb = (PURB)ExAllocatePoolWithTag(NonPagedPool, siz,NIC_TAG);

    if(urb) {

        UsbBuildSelectConfigurationRequest(urb, (USHORT)siz, NULL);

        ntStatus = CallUSBD(Adapter, urb);

        if(!NT_SUCCESS(ntStatus)) {

            DEBUGP(3, ("Failed to deconfigure device\n"));
        }

        ExFreePool(urb);
    }
    else {

        DEBUGP(1, ("Failed to allocate urb\n"));
        ntStatus = STATUS_INSUFFICIENT_RESOURCES;
    }

    return ntStatus;
}

NTSTATUS
CallUSBD(
    IN PMP_ADAPTER Adapter,
    IN PURB           Urb
    )
/*++
 
Routine Description:

    This routine synchronously submits an urb down the stack.

Arguments:

    Adapter - pointer to Adapter object
    Urb - USB request block

Return Value:

--*/
{
    PIRP               irp;
    KEVENT             event;
    NTSTATUS           ntStatus;
    IO_STATUS_BLOCK    ioStatus;
    PIO_STACK_LOCATION nextStack;
    PMP_USBPIPE        usbpipe;

    //
    // initialize the variables
    //

    irp = NULL;
    usbpipe =Adapter->UsbPipeForNIC;
    
    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB, 
                                        Adapter->NextDeviceObject,
                                        NULL, 
                                        0, 
                                        NULL, 
                                        0, 
                                        TRUE, 
                                        &event, 
                                        &ioStatus);

    if(!irp) {

        DEBUGP(1, ("IoBuildDeviceIoControlRequest failed\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    nextStack = IoGetNextIrpStackLocation(irp);
    ASSERT(nextStack != NULL);
    nextStack->Parameters.Others.Argument1 = Urb;

    DEBUGP(3, ("----->CallUSBD::\n"));
    //BulkUsb_IoIncrement(deviceExtension);

    ntStatus = IoCallDriver(Adapter->NextDeviceObject, irp);

    if(ntStatus == STATUS_PENDING) {

        KeWaitForSingleObject(&event, 
                              Executive, 
                              KernelMode, 
                              FALSE, 
                              NULL);

        ntStatus = ioStatus.Status;
    }
    
    DEBUGP(3, ("<-----CallUSBD::\n"));
    //BulkUsb_IoDecrement(deviceExtension);
    return ntStatus;
}

NTSTATUS
IrpCompletionRoutine(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP           Irp,
    IN PVOID          Context
    )
/*++
 
Routine Description:

    This routine is a completion routine.
    In this routine we set an event.

    Since the completion routine returns 
    STATUS_MORE_PROCESSING_REQUIRED, the Irps,
    which set this routine as the completion routine,
    should be marked pending.

Arguments:

    DeviceObject - pointer to device object
    Irp - I/O request packet
    Context - 

Return Value:

    NT status value

--*/
{

    PKEVENT event = (PKEVENT)Context;
	UNREFERENCED_PARAMETER(Irp);
	UNREFERENCED_PARAMETER(DeviceObject);
    KeSetEvent(event, 0, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}


/*---------------------------------------------------------------------------
Routine Description:

    This routine generates an internal IRP from this driver to the PDO
    to obtain information on the Physical Device Object's capabilities.
    We are most interested in learning which system power states
    are to be mapped to which device power states for honoring
    IRP_MJ_SET_POWER Irps.

    This is a blocking call which waits for the IRP completion routine
    to set an event on finishing.

Arguments:

    DeviceObject        - Physical DeviceObject for this USB controller.

Return Value:

    NTSTATUS value from the IoCallDriver() call.

----------------------------------------------------------------------------*/
NTSTATUS usbpnp_QueryCapabilities
(
   IN PMP_ADAPTER     Adapter,
   IN PDEVICE_CAPABILITIES DeviceCapabilities
)
{
   PIO_STACK_LOCATION nextStack;
   PIRP irp;
   NTSTATUS ntStatus;
   KEVENT event;


   // This is a DDK-defined DBG-only macro that ASSERTS we are not running
   // pageable code at higher than APC_LEVEL.
   PAGED_CODE();


   // Build an IRP for us to generate an internal query request to the PDO
   irp = IoAllocateIrp(Adapter->NextDeviceObject->StackSize, FALSE);

   if (!irp)
   {
      return STATUS_INSUFFICIENT_RESOURCES;
   }

   // IoGetNextIrpStackLocation gives a higher level driver access to the
   // next-lower driver's I/O stack location in an IRP so the caller can set
   // it up for the lower driver.
   IoReuseIrp(irp, STATUS_NOT_SUPPORTED);
   nextStack = IoGetNextIrpStackLocation(irp);
   ASSERT(nextStack != NULL);
   nextStack->MajorFunction= IRP_MJ_PNP;
   nextStack->MinorFunction= IRP_MN_QUERY_CAPABILITIES;

   // init an event to tell us when the completion routine's been called
   KeInitializeEvent(&event, NotificationEvent, FALSE);

   // Set a completion routine so it can signal our event when
   //  the next lower driver is done with the Irp
   IoSetCompletionRoutine
   (
      irp,
      IrpCompletionRoutine,
      &event,  // pass the event as Context to completion routine
      TRUE,    // invoke on success
      TRUE,    // invoke on error
      TRUE     // invoke on cancellation of the Irp
   );
   // IoSetCancelRoutine(irp, NULL); // DV ?

   // set our pointer to the DEVICE_CAPABILITIES struct
   nextStack->Parameters.DeviceCapabilities.Capabilities = DeviceCapabilities;

   ntStatus = IoCallDriver
              (
                 Adapter->NextDeviceObject,
                 irp
              );

   if (ntStatus == STATUS_PENDING)
   {
      // wait for irp to complete

      ntStatus = KeWaitForSingleObject
      (
         &event,
         Suspended,
         KernelMode,
         FALSE,
         NULL
      );
   }

   ASSERT(ntStatus==STATUS_SUCCESS);

   IoReuseIrp(irp, STATUS_SUCCESS);
   IoFreeIrp(irp);
   return ntStatus;
}

UCHAR HexChar2Hex(UCHAR c)
{
	UCHAR distance=0;
	if(c>='0'&&c<='9')
	{
		distance=c-'0';
	}else if(c>='a'&&c<='f')
	{
		distance=10+c-'a';
	}else if(c>='A'&&c<='F')
	{
		distance=10+c-'A';
	}
	return distance;
}
NTSTATUS ParseAssociatedDescriptors(PMP_ADAPTER Adapter,UCHAR *pDescriptor,int len)
{
	PMP_USBPIPE usbpipe=Adapter->UsbPipeForNIC;
    UCHAR *buf=pDescriptor;
	PURB  pURB=NULL;
	PUCHAR pAddress=NULL;
	USB_STRING_DESCRIPTOR USD;
	PUSB_STRING_DESCRIPTOR pFullUSD=NULL; 
	USHORT langID=0;
	int temp;
	NTSTATUS ntStatus=STATUS_SUCCESS;
	int index=0;
	int descriptorLen=sizeof(USB_CDC_ETHER_DESC);
	usbpipe->bNcmAddressValid=FALSE;

	/* parse through descriptors associated with control interface */
	while ((len > 0) && (buf[0] > 2) && (buf[0] <= len)) {
		descriptorLen=buf[0];

		if (buf[1] != USB_CDC_CS_INTERFACE)
			goto advance;

		switch (buf[2]) {

		case USB_CDC_ETHERNET_TYPE:
			if (descriptorLen < sizeof(USB_CDC_ETHER_DESC))
				break;

			usbpipe->ether_desc =(PUSB_CDC_ETHER_DESC)ExAllocatePoolWithTag(NonPagedPool, sizeof(USB_CDC_ETHER_DESC),NIC_TAG);
            RtlMoveMemory(usbpipe->ether_desc,buf,sizeof(USB_CDC_ETHER_DESC));
			break;

		case USB_CDC_NCM_TYPE:

			if (descriptorLen < sizeof(USB_CDC_NCM_DESC))
				break;

			usbpipe->ncm_desc =(PUSB_CDC_NCM_DESC)ExAllocatePoolWithTag(NonPagedPool, sizeof(USB_CDC_NCM_DESC),NIC_TAG);
			RtlMoveMemory(usbpipe->ncm_desc,buf,sizeof(USB_CDC_NCM_DESC));
			break;

		default:
			break;
		}
advance:
		/* advance to next descriptor */
		temp = descriptorLen;
		buf += temp;
		len -= temp;
	}

	if(usbpipe->ether_desc)
	{
		pURB=ExAllocatePoolWithTag(NonPagedPool, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),NIC_TAG);
		if(NULL==pURB)
		{
			ntStatus=STATUS_NO_MEMORY;
			goto End;
		}

		UsbBuildGetDescriptorRequest(
			pURB, // points to the URB to be filled in
			sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
			USB_STRING_DESCRIPTOR_TYPE,
			usbpipe->ether_desc->iMACAddress, // index of string descriptor
			langID, // language ID of string.
			&USD, // points to a USB_STRING_DESCRIPTOR.
			NULL,
			sizeof(USB_STRING_DESCRIPTOR),
			NULL
			);
		ntStatus=CallUSBD(Adapter,pURB);
		if(!NT_SUCCESS(ntStatus))
		   goto End;
		if(USD.bLength<sizeof(USB_STRING_DESCRIPTOR)+5*sizeof(WCHAR)){
			goto End;
		}

		pFullUSD = ExAllocatePoolWithTag(NonPagedPool, USD.bLength,NIC_TAG);
		UsbBuildGetDescriptorRequest(
			pURB, // points to the URB to be filled in
			sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
			USB_STRING_DESCRIPTOR_TYPE,
			usbpipe->ether_desc->iMACAddress, // index of string descriptor
			langID, // language ID of string
			pFullUSD,
			NULL,
			USD.bLength,
			NULL
			);
		ntStatus=CallUSBD(Adapter,pURB);
		if(!NT_SUCCESS(ntStatus))
			goto End;
		if(pFullUSD->bLength<(2*ETH_LENGTH_OF_ADDRESS*sizeof(WCHAR))){
			goto End;
		}
		 pAddress=(UCHAR *)pFullUSD->bString;
        for(index=0;index<ETH_LENGTH_OF_ADDRESS;index++)
		{

			usbpipe->ncmPhyNicMacAddress[index]=HexChar2Hex(pAddress[0])*16+HexChar2Hex(pAddress[2]);
			pAddress+=4;
		}
		usbpipe->bNcmAddressValid=TRUE;
	}
End:
	USB_EXFreePool(pFullUSD);
	USB_EXFreePool(pURB);
	return ntStatus;
}