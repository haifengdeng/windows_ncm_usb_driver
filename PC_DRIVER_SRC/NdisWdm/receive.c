/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

        RECV.C
        
Abstract:

    This module contains miniport functions for handling Send & Receive
    packets and other helper routines called by these miniport functions.
    
Revision History:

Notes:

--*/
#pragma warning(disable:4201)  //standard extension used : nameless struct/union

#include "ndiswdm.h"

VOID 
MPReturnPacket(
    IN NDIS_HANDLE  MiniportAdapterContext,
    IN PNDIS_PACKET Packet)
/*++

Routine Description:

    NDIS Miniport entry point called whenever protocols are done with
    a packet that we had indicated up and they had queued up for returning
    later.

Arguments:

    MiniportAdapterContext    - pointer to MP_ADAPTER structure
    Packet    - packet being returned.

Return Value:

    None.

--*/
{
    PRCB pRCB = NULL;
    PMP_ADAPTER Adapter;
	BOOLEAN     datagramsover=FALSE;

    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    
    DEBUGP(MP_TRACE, ("---> MPReturnPacket\n"));

    pRCB = *(PRCB *)Packet->MiniportReserved;

    Adapter = pRCB->Adapter;

    ASSERT(Adapter);
    
    Adapter->nPacketsReturned++;
    
ncmdivi:
	if(NdisInterlockedDecrement(&pRCB->Ref) == 0)
	{	
		if(FALSE==datagramsover) {
			pRCB->Ref=1;
			datagramsover=cdc_ncm_rx_fixup(Adapter,pRCB);
			goto ncmdivi;
		}
		NICFreeRCB(pRCB);
	}

    
    DEBUGP(MP_TRACE, ("<--- MPReturnPacket\n"));
}

VOID
NICPostReadsWorkItemCallBack(
    PNDIS_WORK_ITEM WorkItem, 
    PVOID Context
    )
/*++

Routine Description:

   Workitem to post all the free read requests to the target
   driver. This workitme is scheduled from the MiniportInitialize
   worker routine during start and also from the NICFreeRCB whenever
   the outstanding read requests to the target driver goes below
   the NIC_SEND_LOW_WATERMARK.
      
Arguments:

    WorkItem - Pointer to workitem
    
    Dummy - Unused

Return Value:

    None.

--*/
{
    PMP_ADAPTER     Adapter = (PMP_ADAPTER)Context;
    NTSTATUS        ntStatus;
    PRCB            pRCB=NULL;

    UNREFERENCED_PARAMETER(WorkItem);
           
    DEBUGP(MP_TRACE, ("--->NICPostReadsWorkItemCallBack\n"));

    NdisAcquireSpinLock(&Adapter->RecvLock);

    while(MP_IS_READY(Adapter) && !IsListEmpty(&Adapter->RecvFreeList))
    {
        pRCB = (PRCB) RemoveHeadList(&Adapter->RecvFreeList);

        ASSERT(pRCB);// cannot be NULL
        
        //
        // Insert the RCB in the recv busy queue
        //     
        NdisInterlockedIncrement(&Adapter->nBusyRecv);          
        ASSERT(Adapter->nBusyRecv <= NIC_MAX_BUSY_RECVS);
        
        InsertTailList(&Adapter->RecvBusyList, &pRCB->List);

        NdisReleaseSpinLock(&Adapter->RecvLock);

        Adapter->nReadsPosted++;
        ntStatus = NICPostReadRequest(Adapter, pRCB);
        
        if (!NT_SUCCESS ( ntStatus ) ) {
            
            DEBUGP (MP_ERROR, ( "NICPostReadRequest failed %x\n", ntStatus ));
            break;            
        }
        
        NdisAcquireSpinLock(&Adapter->RecvLock);
        
    }

    NdisReleaseSpinLock(&Adapter->RecvLock);

    //
    // Clear the flag to let the WorkItem structure be reused for
    // scheduling another one.
    //
    InterlockedExchange(&Adapter->IsReadWorkItemQueued, FALSE);

    
    MP_DEC_REF(Adapter);     
    
    DEBUGP(MP_TRACE, ("<---NICPostReadsWorkItemCallBack\n"));        
}

NTSTATUS
NICPostReadRequest(
    PMP_ADAPTER Adapter,
    PRCB    pRCB
    )
/*++

Routine Description:

    This routine sends a read IRP to the target device to get
    the incoming network packet from the device.
        
Arguments:

    Adapter    - pointer to the MP_ADAPTER structure
    pRCB    -  Pointer to the RCB block that contains the IRP.


Return Value:

    NT status code

--*/
{
    PIRP            irp = pRCB->Irp;
    PIO_STACK_LOCATION  nextStack;
    PDEVICE_OBJECT   TargetDeviceObject = Adapter->NextDeviceObject;
	PMP_USBPIPE   usbpipe;
	USHORT            siz;
	PURB              urb=pRCB->Urb;

    DEBUGP(MP_TRACE, ("--> NICPostReadRequest\n"));

    // 
    // Obtain a pointer to the stack location of the first driver that will be
    // invoked.  This is where the function codes and the parameters are set.
    // 
	usbpipe=Adapter->UsbPipeForNIC;
	siz = sizeof( struct _URB_BULK_OR_INTERRUPT_TRANSFER );
	UsbBuildInterruptOrBulkTransferRequest
		(
		urb, 
		siz,
		usbpipe->InterfaceData->Pipes[usbpipe->BulkPipeInput].PipeHandle,
		(PVOID)pRCB->pDataForNTB,
		NULL,
		usbpipe->rx_max,
		USBD_SHORT_TRANSFER_OK,
		NULL
		);




	nextStack = IoGetNextIrpStackLocation( irp );
	nextStack->Parameters.Others.Argument1 = urb;
	nextStack->Parameters.DeviceIoControl.IoControlCode =
		IOCTL_INTERNAL_USB_SUBMIT_URB;
	nextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
        
	//MmInitializeMdl(pRCB->BufferArray[0],pRCB->pDataForNTB,pRCB->ulBufferSize);
 //   irp->MdlAddress = (PMDL) pRCB->BufferArray[0];
	irp->MdlAddress =NULL;

    pRCB->IrpLock = IRPLOCK_CANCELABLE;    
    pRCB->Ref = 1;

    IoSetCompletionRoutine(irp,
                   NICReadRequestCompletion,
                   pRCB,
                   TRUE,
                   TRUE,
                   TRUE);
    //
    // We are making an asynchronous request, so we don't really care
    // about the return status of IoCallDriver.
    //
    (void) IoCallDriver(TargetDeviceObject, irp);

    DEBUGP(MP_TRACE, ("<-- NICPostReadRequest\n"));

    return STATUS_SUCCESS;
}

NTSTATUS
NICReadRequestCompletion(
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp,
    IN PVOID            Context
    )
/*++

Routine Description:

    Completion routine for the read request. This routine
    indicates the received packet from the WDM driver to 
    NDIS. This routine also handles the case where another 
    thread has canceled the read request.
        
Arguments:

    DeviceObject    -  not used. Should be NULL
    Irp    -   Pointer to our read IRP
    Context - pointer to our adapter context structure

Return Value:

    STATUS_MORE_PROCESSING_REQUIRED - because this is an asynchronouse IRP
    and we want to reuse it.
    
--*/
{
    PRCB    pRCB = (PRCB)Context;
    PMP_ADAPTER Adapter = pRCB->Adapter;
    ULONG   bytesRead = 0;
    BOOLEAN bIndicateReceive = FALSE;
	BOOLEAN     datagramsover=TRUE;

    UNREFERENCED_PARAMETER(DeviceObject);
    
    DEBUGP(MP_TRACE, ("--> NICReadRequestCompletion\n"));

    
    if(!NT_SUCCESS(Irp->IoStatus.Status)) {       
        
        Adapter->nReadsCompletedWithError++;        
        DEBUGP (MP_LOUD, ("Read request failed %x\n", Irp->IoStatus.Status));        

        //
        // Clear the flag to prevent any more reads from being
        // posted to the target device.
        //
        MP_CLEAR_FLAG(Adapter, fMP_POST_READS);
        
    } else {
		bytesRead = (ULONG)pRCB->Urb->UrbBulkOrInterruptTransfer.TransferBufferLength;
        DEBUGP (MP_VERY_LOUD, ("Read %d bytes\n", bytesRead));
        Adapter->nBytesRead += bytesRead;
		pRCB->ulSize=bytesRead;
		pRCB->nextndeoffset=pRCB->nextndpoffset=0;
		pRCB->bIsOver=FALSE;
        bIndicateReceive = TRUE;

    }
    
    if (InterlockedExchange((PVOID)&pRCB->IrpLock, IRPLOCK_COMPLETED) 
                    == IRPLOCK_CANCEL_STARTED) {
        // 
        // NICFreeBusyRecvPackets has got the control of the IRP. It will
        // now take the responsibility of freeing  the IRP. 
        // Therefore...
        
        return STATUS_MORE_PROCESSING_REQUIRED;
    }
ncmdivi:
    if(bIndicateReceive) {
		datagramsover=cdc_ncm_rx_fixup(Adapter,pRCB);
    }
    
    if(NdisInterlockedDecrement(&pRCB->Ref) == 0)
    {
		if(FALSE==datagramsover) {
			pRCB->Ref=1;
			goto ncmdivi;
		}
        NICFreeRCB(pRCB);
    }
    
    return STATUS_MORE_PROCESSING_REQUIRED;
}

VOID
NICIndicateReceivedPacket(
    IN PRCB             pRCB,
	IN ULONG            dataoffset,
    IN ULONG            BytesToIndicate,
	IN ULONG            PacketNum
    )
/*++

Routine Description:

    Initialize the packet to describe the received data and
    indicate to NDIS.
        
Arguments:

    pRCB - pointer to the RCB block    
    BytesToIndicate - number of bytes to indicate

Return value:

    VOID
--*/
{
    ULONG           PacketLength;
    PNDIS_BUFFER    CurrentBuffer = NULL;
    PETH_HEADER     pEthHeader = NULL;
    PMP_ADAPTER     Adapter = pRCB->Adapter;
    KIRQL           oldIrql;	
	PVOID  VirtualAddress=NULL;
	ASSERT(     PacketNum   <    RCB_BUFFERARRAY_SIZE);
	ASSERT((dataoffset+BytesToIndicate) <  pRCB->ulBufferSize);


    

	
    //NdisAdjustBufferLength(pRCB->Buffer, BytesToIndicate);
	//MmInitializeMdl(pRCB->BufferArray[PacketNum],pRCB->pDataForNTB+dataoffset,BytesToIndicate);
	VirtualAddress=MmGetMdlVirtualAddress(pRCB->BufferArray[PacketNum]);
	ASSERT(VirtualAddress!=NULL);
	NdisMoveMemory(VirtualAddress,pRCB->pDataForNTB+dataoffset,BytesToIndicate);
	NdisAdjustBufferLength(pRCB->BufferArray[PacketNum], BytesToIndicate);
	//NdisMoveMemory(pRCB->pDataForNet+NIC_BUFFER_SIZE*PacketNum,pRCB->pDataForNTB+dataoffset,BytesToIndicate);
    //
    // Prepare the recv packet
    //

    NdisReinitializePacket(pRCB->PacketArray[PacketNum]);

    *((PRCB *)pRCB->PacketArray[PacketNum]->MiniportReserved) = pRCB;

    //
    // Chain the TCB buffers to the packet
    //
    NdisChainBufferAtBack(pRCB->PacketArray[PacketNum], pRCB->BufferArray[PacketNum]);

    NdisQueryPacket(pRCB->PacketArray[PacketNum], NULL, NULL, &CurrentBuffer, (PUINT) &PacketLength);

    ASSERT(CurrentBuffer == pRCB->BufferArray[PacketNum]);

    pEthHeader = (PETH_HEADER)(pRCB->pDataForNTB+dataoffset);

    if(PacketLength >= sizeof(ETH_HEADER) && 
        Adapter->PacketFilter &&
        NICIsPacketAcceptable(Adapter, pEthHeader->DstAddr)){
            
        DEBUGP(MP_LOUD, ("Src Address = %02x-%02x-%02x-%02x-%02x-%02x", 
            pEthHeader->SrcAddr[0],
            pEthHeader->SrcAddr[1],
            pEthHeader->SrcAddr[2],
            pEthHeader->SrcAddr[3],
            pEthHeader->SrcAddr[4],
            pEthHeader->SrcAddr[5]));

        DEBUGP(MP_LOUD, ("  Dest Address = %02x-%02x-%02x-%02x-%02x-%02x\n", 
            pEthHeader->DstAddr[0],
            pEthHeader->DstAddr[1],
            pEthHeader->DstAddr[2],
            pEthHeader->DstAddr[3],
            pEthHeader->DstAddr[4],
            pEthHeader->DstAddr[5]));

        DEBUGP(MP_LOUD, ("Indicating packet = %p, Packet Length = %d\n", 
                            pRCB->PacketArray[PacketNum], PacketLength));

        NdisInterlockedIncrement(&pRCB->Ref);


        NDIS_SET_PACKET_STATUS(pRCB->PacketArray[PacketNum], NDIS_STATUS_SUCCESS);
        Adapter->nPacketsIndicated++;

        //
        // NDIS expects the indication to happen at DISPATCH_LEVEL if the
        // device is assinged any I/O resources in the IRP_MN_START_DEVICE_IRP.
        // Since this sample is flexible enough to be used as a standalone
        // virtual miniport talking to another device or part of a WDM stack for
        // devices consuming hw resources such as ISA, PCI, PCMCIA. I have to
        // do the following check. You should avoid raising the IRQL, if you
        // know for sure that your device wouldn't have any I/O resources. This
        // would be the case if your driver is talking to USB, 1394, etc.
        //
        if(Adapter->IsHardwareDevice){
            
            KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
            NdisMIndicateReceivePacket(Adapter->AdapterHandle,
                            &pRCB->PacketArray[PacketNum],
                            1);
            KeLowerIrql(oldIrql);            
            
        }else{
        
            NdisMIndicateReceivePacket(Adapter->AdapterHandle,
                            &pRCB->PacketArray[PacketNum],
                            1);
        }

    }else {
        DEBUGP(MP_VERY_LOUD, 
                ("Invalid packet or filter is not set packet = %p,Packet Length = %d\n",
                pRCB->PacketArray, PacketLength));        
    }            

}

VOID
NICFreeBusyInterruptNotifyPackets(
    PMP_ADAPTER Adapter
    )
/*++

Routine Description:

    This function tries to cancel all the outstanding read IRP if it is not 
    already completed and frees the RCB block. This routine is called
    only by the Halt handler.
    
Arguments:

    Adapter    - pointer to the MP_ADAPTER structure
    
Return value:

    VOID
--*/
{ 
    PLIST_ENTRY         pEntry = NULL;
    PNOTICB                pNotiCB = NULL;
    
    DEBUGP(MP_TRACE, ("--> NICFreeBusyInterruptNotifyPackets\n"));

    if(!MP_TEST_FLAG(Adapter, fMP_INTERRUPT_SIDE_RESOURCE_ALLOCATED)){
        return;
    }
    
    NdisAcquireSpinLock(&Adapter->InterruptNotifyLock);

    while(!IsListEmpty(&Adapter->InterruptBusyList))
    {
        
        pEntry = (PLIST_ENTRY)RemoveHeadList(&Adapter->InterruptBusyList);
        pNotiCB = CONTAINING_RECORD(pEntry, NOTICB, List);
        NdisInitializeListHead(&pNotiCB->List);
        
        NdisReleaseSpinLock(&Adapter->InterruptNotifyLock); 
        
        
        if (InterlockedExchange((PVOID)&pNotiCB->IrpLock, IRPLOCK_CANCEL_STARTED) 
                                == IRPLOCK_CANCELABLE) {

            // 
            // We got it to the IRP before it was completed. We can cancel
            // the IRP without fear of losing it, as the completion routine
            // will not let go of the IRP until we say so.
            // 
            IoCancelIrp(pNotiCB->Irp);
            // 
            // Release the completion routine. If it already got there,
            // then we need to free it ourself. Otherwise, we got
            // through IoCancelIrp before the IRP completed entirely.
            // 
            if (InterlockedExchange((PVOID)&pNotiCB->IrpLock, IRPLOCK_CANCEL_COMPLETE) 
                                    == IRPLOCK_COMPLETED) {
                if(NdisInterlockedDecrement(&pNotiCB->Ref) == 0) {
                    NICFreeNotify(pNotiCB);
                } else {
                    ASSERTMSG("Only we have the right to free RCB\n", FALSE);                
                }
            }

        }

        NdisAcquireSpinLock(&Adapter->InterruptNotifyLock);
        
    }

    NdisReleaseSpinLock(&Adapter->InterruptNotifyLock); 

    DEBUGP(MP_TRACE, ("<-- NICFreeBusyRecvPackets\n"));
     
    return ;
}


VOID 
NICFreeNotify(
    IN PNOTICB pNotiCB)
/*++

Routine Description:

    pRCB      - pointer to RCB block
        
Arguments:

    This routine reinitializes the RCB block and puts it back
    into the RecvFreeList for reuse.
    

Return Value:

    VOID

--*/
{
    PMP_ADAPTER Adapter = pNotiCB->Adapter;
    
    DEBUGP(MP_TRACE, ("--> NICFreeNotify %p\n", pNotiCB));
    
    ASSERT(pNotiCB->Irp);    // shouldn't be NULL
    ASSERT(!pNotiCB->Ref); // should be 0
    ASSERT(pNotiCB->Adapter); // shouldn't be NULL

    IoReuseIrp(pNotiCB->Irp, STATUS_SUCCESS);    


	pNotiCB->ulSize=0;
    // 
    // Set the MDL field to NULL so that we don't end up double freeing the
    // MDL in our call to IoFreeIrp.
    // 
      
    pNotiCB->Irp->MdlAddress = NULL;
    
    //
    // Insert the RCB back in the Recv free list     
    //
    NdisAcquireSpinLock(&Adapter->InterruptNotifyLock);
    
    RemoveEntryList(&pNotiCB->List);
    
    InsertTailList(&Adapter->InterruptFreeList, &pNotiCB->List);

    NdisInterlockedDecrement(&Adapter->nBusyNotify);
    ASSERT(Adapter->nBusyNotify >= 0);
    
    NdisReleaseSpinLock(&Adapter->InterruptNotifyLock); 
    
    //
    // For performance, instead of scheduling a workitem at the end of
    // every read completion, we will do it only when the number of 
    // outstanding IRPs goes below NIC_SEND_LOW_WATERMARK.
    // We shouldn't queue a workitem if it's already scheduled and waiting in
    // the system workitem queue to be fired.
    //
    if((Adapter->nBusyNotify ==0)
            && MP_TEST_FLAG(Adapter, fMP_POST_INTERRUPT)) {

        MP_INC_REF(Adapter);                   
        NdisScheduleWorkItem(&Adapter->InterruptNotifyItem);   
    }
    DEBUGP(MP_TRACE, ("<-- NICFreeNotify %d\n", Adapter->nBusyNotify));
    
}

VOID
NICFreeBusyRecvPackets(
    PMP_ADAPTER Adapter
    )
/*++

Routine Description:

    This function tries to cancel all the outstanding read IRP if it is not 
    already completed and frees the RCB block. This routine is called
    only by the Halt handler.
    
Arguments:

    Adapter    - pointer to the MP_ADAPTER structure
    
Return value:

    VOID
--*/
{ 
    PLIST_ENTRY         pEntry = NULL;
    PRCB                pRCB = NULL;
    
    DEBUGP(MP_TRACE, ("--> NICFreeBusyRecvPackets\n"));

    if(!MP_TEST_FLAG(Adapter, fMP_RECV_SIDE_RESOURCE_ALLOCATED)){
        return;
    }
    
    NdisAcquireSpinLock(&Adapter->RecvLock);

    while(!IsListEmpty(&Adapter->RecvBusyList))
    {
        
        pEntry = (PLIST_ENTRY)RemoveHeadList(&Adapter->RecvBusyList);
        pRCB = CONTAINING_RECORD(pEntry, RCB, List);
        NdisInitializeListHead(&pRCB->List);
        
        NdisReleaseSpinLock(&Adapter->RecvLock); 
        
        
        if (InterlockedExchange((PVOID)&pRCB->IrpLock, IRPLOCK_CANCEL_STARTED) 
                                == IRPLOCK_CANCELABLE) {

            // 
            // We got it to the IRP before it was completed. We can cancel
            // the IRP without fear of losing it, as the completion routine
            // will not let go of the IRP until we say so.
            // 
            IoCancelIrp(pRCB->Irp);
            // 
            // Release the completion routine. If it already got there,
            // then we need to free it ourself. Otherwise, we got
            // through IoCancelIrp before the IRP completed entirely.
            // 
            if (InterlockedExchange((PVOID)&pRCB->IrpLock, IRPLOCK_CANCEL_COMPLETE) 
                                    == IRPLOCK_COMPLETED) {
                if(NdisInterlockedDecrement(&pRCB->Ref) == 0) {
                    NICFreeRCB(pRCB);
                } else {
                    ASSERTMSG("Only we have the right to free RCB\n", FALSE);                
                }
            }

        }

        NdisAcquireSpinLock(&Adapter->RecvLock);
        
    }

    NdisReleaseSpinLock(&Adapter->RecvLock); 

    DEBUGP(MP_TRACE, ("<-- NICFreeBusyRecvPackets\n"));
     
    return ;
}


VOID 
NICFreeRCB(
    IN PRCB pRCB)
/*++

Routine Description:

    pRCB      - pointer to RCB block
        
Arguments:

    This routine reinitializes the RCB block and puts it back
    into the RecvFreeList for reuse.
    

Return Value:

    VOID

--*/
{
    PMP_ADAPTER Adapter = pRCB->Adapter;
    ULONG ulSendLowW= NIC_SEND_LOW_WATERMARK;
    
    DEBUGP(MP_TRACE, ("--> NICFreeRCB %p\n", pRCB));
    
    ASSERT(pRCB->Irp);    // shouldn't be NULL
    ASSERT(!pRCB->Ref); // should be 0
    ASSERT(pRCB->Adapter); // shouldn't be NULL

    IoReuseIrp(pRCB->Irp, STATUS_SUCCESS);    

	pRCB->nextndeoffset=0;    
	pRCB->nextndpoffset=0;
	pRCB->bIsOver=FALSE;
	pRCB->ulSize=0;
    // 
    // Set the MDL field to NULL so that we don't end up double freeing the
    // MDL in our call to IoFreeIrp.
    // 
      
    pRCB->Irp->MdlAddress = NULL;
    
    //
    // Re adjust the length to the originl size
    //
    //NdisAdjustBufferLength(pRCB->BufferArray[0], pRCB->ulBufferSize);

    //
    // Insert the RCB back in the Recv free list     
    //
    NdisAcquireSpinLock(&Adapter->RecvLock);
    
    RemoveEntryList(&pRCB->List);
    
    InsertTailList(&Adapter->RecvFreeList, &pRCB->List);

    NdisInterlockedDecrement(&Adapter->nBusyRecv);
    ASSERT(Adapter->nBusyRecv >= 0);
    
    NdisReleaseSpinLock(&Adapter->RecvLock); 
    
    //
    // For performance, instead of scheduling a workitem at the end of
    // every read completion, we will do it only when the number of 
    // outstanding IRPs goes below NIC_SEND_LOW_WATERMARK.
    // We shouldn't queue a workitem if it's already scheduled and waiting in
    // the system workitem queue to be fired.
    //
    if((!ulSendLowW || Adapter->nBusyRecv <= NIC_SEND_LOW_WATERMARK)
            && MP_TEST_FLAG(Adapter, fMP_POST_READS) && 
            (InterlockedExchange(&Adapter->IsReadWorkItemQueued, TRUE) == FALSE)) {

        Adapter->nReadWorkItemScheduled++;
        MP_INC_REF(Adapter);                   
        NdisScheduleWorkItem(&Adapter->ReadWorkItem);   
    }
    DEBUGP(MP_TRACE, ("<-- NICFreeRCB %d\n", Adapter->nBusyRecv));
    
}



BOOLEAN
NICIsPacketAcceptable(
    IN PMP_ADAPTER Adapter,
    IN PUCHAR   pDstMac
    )
/*++

Routine Description:

    Check if the destination address of a received packet
    matches the receive criteria of our adapter.
    
Arguments:

    Adapter    - pointer to the adapter structure
    pDstMac - Destination MAC address to compare


Return Value:

    True or False

--*/
{
    UINT            AddrCompareResult;
    ULONG           PacketFilter;
    BOOLEAN         bPacketMatch;
    BOOLEAN         bIsMulticast, bIsBroadcast;

    PacketFilter = Adapter->PacketFilter;

    bIsMulticast = ETH_IS_MULTICAST(pDstMac);
    bIsBroadcast = ETH_IS_BROADCAST(pDstMac);

    //
    // Handle the directed packet case first.
    //
    if (!bIsMulticast)
    {
        //
        // If the Adapter is not in promisc. mode, check if
        // the destination MAC address matches the local
        // address.
        //
        if ((PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS) == 0)
        {
            ETH_COMPARE_NETWORK_ADDRESSES_EQ(Adapter->CurrentAddress,
                                             pDstMac,
                                             &AddrCompareResult);

            bPacketMatch = ((AddrCompareResult == 0) &&
                           ((PacketFilter & NDIS_PACKET_TYPE_DIRECTED) != 0));
        }
        else
        {
            bPacketMatch = TRUE;
        }
     }
     else
     {
        //
        // Multicast or broadcast packet.
        //

        //
        // Indicate if the filter is set to promisc mode ...
        //
        if ((PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS)
                ||

            //
            // or if this is a broadcast packet and the filter
            // is set to receive all broadcast packets...
            //
            (bIsBroadcast &&
             (PacketFilter & NDIS_PACKET_TYPE_BROADCAST))
                ||

            //
            // or if this is a multicast packet, and the filter is
            // either set to receive all multicast packets, or
            // set to receive specific multicast packets. In the
            // latter case, indicate receive only if the destn
            // MAC address is present in the list of multicast
            // addresses set on the Adapter.
            //
            (!bIsBroadcast &&
             ((PacketFilter & NDIS_PACKET_TYPE_ALL_MULTICAST) ||
              ((PacketFilter & NDIS_PACKET_TYPE_MULTICAST) &&
               NICIsMulticastMatch(Adapter, pDstMac))))
           )
        {
            bPacketMatch = TRUE;
        }
        else
        {
            //
            // No protocols above are interested in this
            // multicast/broadcast packet.
            //
            bPacketMatch = FALSE;
        }
    }

    return (bPacketMatch);
}

BOOLEAN
NICIsMulticastMatch(
    IN PMP_ADAPTER  Adapter,
    IN PUCHAR       pDstMac
    )
/*++

Routine Description:

    Check if the given multicast destination MAC address matches
    any of the multicast address entries set on the Adapter.

    NOTE: the caller is assumed to hold a READ/WRITE lock
    to the parent ADAPT structure. This is so that the multicast
    list on the Adapter is invariant for the duration of this call.

Arguments:

    Adapter  - Adapter to look in
    pDstMac - Destination MAC address to compare

Return Value:

    TRUE iff the address matches an entry in the Adapter

--*/
{
    ULONG           i;
    UINT            AddrCompareResult;

    for (i = 0; i < Adapter->ulMCListSize; i++)
    {
        ETH_COMPARE_NETWORK_ADDRESSES_EQ(Adapter->MCList[i],
                                         pDstMac,
                                         &AddrCompareResult);
        
        if (AddrCompareResult == 0)
        {
            break;
        }
    }

    return (i != Adapter->ulMCListSize);
}
NTSTATUS
NICPostInterruptRequest(
    PMP_ADAPTER Adapter,
    PNOTICB    pNotiCB
    )
/*++

Routine Description:

    This routine sends a read IRP to the target device to get
    the incoming network packet from the device.
        
Arguments:

    Adapter    - pointer to the MP_ADAPTER structure
    pRCB    -  Pointer to the RCB block that contains the IRP.


Return Value:

    NT status code

--*/
{
    PIRP            irp = pNotiCB->Irp;
    PIO_STACK_LOCATION  nextStack;
    PDEVICE_OBJECT   TargetDeviceObject = Adapter->NextDeviceObject;
	PMP_USBPIPE   usbpipe;
	USHORT            siz;
	PURB              urb=pNotiCB->Urb;

    DEBUGP(MP_TRACE, ("--> NICPostInterruptRequest\n"));

    // 
    // Obtain a pointer to the stack location of the first driver that will be
    // invoked.  This is where the function codes and the parameters are set.
    // 
	usbpipe=Adapter->UsbPipeForNIC;
	siz = sizeof( struct _URB_BULK_OR_INTERRUPT_TRANSFER );
	UsbBuildInterruptOrBulkTransferRequest
		(
		urb, 
		siz,
		usbpipe->InterfaceData->Pipes[usbpipe->InterruptPipe].PipeHandle,
		(PVOID)pNotiCB->pData,
		NULL,
		usbpipe->interrupt_max,
		USBD_SHORT_TRANSFER_OK,
		NULL
		);




	nextStack = IoGetNextIrpStackLocation( irp );
	nextStack->Parameters.Others.Argument1 = urb;
	nextStack->Parameters.DeviceIoControl.IoControlCode =
		IOCTL_INTERNAL_USB_SUBMIT_URB;
	nextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
        

    pNotiCB->IrpLock = IRPLOCK_CANCELABLE;    
    pNotiCB->Ref = 1;

    IoSetCompletionRoutine(irp,
                   InterruptPipeCompletion,
                   pNotiCB,
                   TRUE,
                   TRUE,
                   TRUE);
    //
    // We are making an asynchronous request, so we don't really care
    // about the return status of IoCallDriver.
    //
    (void) IoCallDriver(TargetDeviceObject, irp);

    DEBUGP(MP_TRACE, ("<-- NICPostInterruptRequest\n"));

    return STATUS_SUCCESS;
}


VOID
NICPostInterruptNotifyItemCallBack(
    PNDIS_WORK_ITEM WorkItem, 
    PVOID Context
    )
/*++

Routine Description:

   Workitem to post all the free read requests to the target
   driver. This workitme is scheduled from the MiniportInitialize
   worker routine during start and also from the NICFreeRCB whenever
   the outstanding read requests to the target driver goes below
   the NIC_SEND_LOW_WATERMARK.
      
Arguments:

    WorkItem - Pointer to workitem
    
    Dummy - Unused

Return Value:

    None.

--*/
{


	PMP_ADAPTER     Adapter = (PMP_ADAPTER)Context;
	NTSTATUS        ntStatus;
	PNOTICB            pNotiCB=NULL;

	UNREFERENCED_PARAMETER(WorkItem);

	DEBUGP(MP_TRACE, ("--->NICPostInterruptNotifyItemCallBack\n"));

	NdisAcquireSpinLock(&Adapter->InterruptNotifyLock);

	while(MP_IS_READY(Adapter) && !IsListEmpty(&Adapter->InterruptFreeList))
	{
		pNotiCB = (PNOTICB) RemoveHeadList(&Adapter->InterruptFreeList);

		ASSERT(pNotiCB);// cannot be NULL

		//
		// Insert the RCB in the recv busy queue
		//     
		NdisInterlockedIncrement(&Adapter->nBusyNotify);          
		ASSERT(Adapter->nBusyNotify <= NOTICB_BUFFER_COUNT);

		InsertTailList(&Adapter->InterruptBusyList, &pNotiCB->List);

		NdisReleaseSpinLock(&Adapter->InterruptNotifyLock);

		ntStatus = NICPostInterruptRequest(Adapter, pNotiCB);

		if (!NT_SUCCESS ( ntStatus ) ) {

			DEBUGP (MP_ERROR, ( "NICPostInterruptRequest failed %x\n", ntStatus ));
			break;            
		}

		NdisAcquireSpinLock(&Adapter->InterruptNotifyLock);

	}

	NdisReleaseSpinLock(&Adapter->InterruptNotifyLock);

	MP_DEC_REF(Adapter);     

	DEBUGP(MP_TRACE, ("<---NICPostInterruptNotifyItemCallBack\n"));         
}


NTSTATUS
InterruptPipeCompletion(
    IN PDEVICE_OBJECT   DeviceObject,
    IN PIRP             Irp,
    IN PVOID            Context
    )
/*++

Routine Description:

    Completion routine for the read request. This routine
    indicates the received packet from the WDM driver to 
    NDIS. This routine also handles the case where another 
    thread has canceled the read request.
        
Arguments:

    DeviceObject    -  not used. Should be NULL
    Irp    -   Pointer to our read IRP
    Context - pointer to our adapter context structure

Return Value:

    STATUS_MORE_PROCESSING_REQUIRED - because this is an asynchronouse IRP
    and we want to reuse it.
    
--*/
{


	PNOTICB    pNotiCB = (PNOTICB)Context;
	PMP_ADAPTER Adapter = pNotiCB->Adapter;
	ULONG   bytesRead = 0;

	UNREFERENCED_PARAMETER(DeviceObject);

	DEBUGP(MP_TRACE, ("--> InterruptPipeCompletion\n"));


	if(!NT_SUCCESS(Irp->IoStatus.Status)) {       

		Adapter->nReadsCompletedWithError++;        
		DEBUGP (MP_LOUD, ("Read request failed %x\n", Irp->IoStatus.Status));        

		//
		// Clear the flag to prevent any more reads from being
		// posted to the target device.
		//
		MP_CLEAR_FLAG(Adapter, fMP_POST_INTERRUPT);

	} else {
		bytesRead = (ULONG)pNotiCB->Urb->UrbBulkOrInterruptTransfer.TransferBufferLength;
		DEBUGP (MP_VERY_LOUD, ("Read %d bytes\n", bytesRead));
		pNotiCB->ulSize=bytesRead;
	}

	if (InterlockedExchange((PVOID)&pNotiCB->IrpLock, IRPLOCK_COMPLETED) 
		== IRPLOCK_CANCEL_STARTED) {
			// 
			// NICFreeBusyRecvPackets has got the control of the IRP. It will
			// now take the responsibility of freeing  the IRP. 
			// Therefore...

			return STATUS_MORE_PROCESSING_REQUIRED;
	}
	if (NT_SUCCESS(Irp->IoStatus.Status))
	{
		cdc_ncm_status(Adapter,pNotiCB->pData,pNotiCB->ulSize);
	}

	if(NdisInterlockedDecrement(&pNotiCB->Ref) == 0)
	{
		NICFreeNotify(pNotiCB);
	}

	DEBUGP(MP_TRACE, ("<-- InterruptPipeCompletion\n"));
	return STATUS_MORE_PROCESSING_REQUIRED;
}

