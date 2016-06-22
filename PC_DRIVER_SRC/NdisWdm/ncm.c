#include "ndiswdm.h"
#include "usbpnp.h"
//static const struct ndp_parser_opts ndp16_opts = INIT_NDP16_OPTS;
//static const struct ndp_parser_opts ndp32_opts = INIT_NDP32_OPTS;

NTSTATUS 
cdc_ncm_reset(PMP_ADAPTER Adapter )
{
	PMP_USBPIPE usbpipe;
	STRUCT_USB_CDC_NCM_NTB_PARAMETERS ncm_parm;
	NTSTATUS status;
	USHORT ntb_fmt_supported;
	ULONG  Retsize;
	
	usbpipe=Adapter->UsbPipeForNIC;
	status= UsbclassTypeRequestCall(Adapter,USB_TYPE_CLASS | USB_DIR_IN|USB_RECIP_INTERFACE,
		USB_CDC_GET_NTB_PARAMETERS,0,usbpipe->InterfaceComm->InterfaceNumber,sizeof(STRUCT_USB_CDC_NCM_NTB_PARAMETERS),&ncm_parm,
		&Retsize);
	if(!NT_SUCCESS(status))
	{
		goto func_end;

	}	
	if(Retsize!=sizeof(STRUCT_USB_CDC_NCM_NTB_PARAMETERS))
	{
		status=STATUS_FAIL_CHECK;
		goto func_end;
	}
	ntb_fmt_supported = le16_to_cpu(ncm_parm.bmNtbFormatsSupported);
	/* set NTB format, if both formats are supported */
	if (ntb_fmt_supported & USB_CDC_NCM_NTH32_SIGN) {
		status= UsbclassTypeRequestCall(Adapter,USB_TYPE_CLASS | USB_DIR_OUT|USB_RECIP_INTERFACE,
			USB_CDC_SET_NTB_FORMAT,USB_CDC_NCM_NTB16_FORMAT,usbpipe->InterfaceComm->InterfaceNumber,0,NULL,
			NULL);
		if(!NT_SUCCESS(status))
		{
			DEBUGP(MP_ERROR,("Setting NTB format to 16-bit failed\n"));
			goto func_end;
		}	
	}
func_end:
	return status;

}
NTSTATUS
cdc_ncm_Init(
			 PMP_ADAPTER Adapter
			 )
			 /*
			 this function is called when MPInitialize function is called after SetInterfaces function.
			 */
{
	PMP_USBPIPE usbpipe;
	STRUCT_USB_CDC_NCM_NTB_PARAMETERS ncm_parm;
	NTSTATUS status;
	USHORT ntb_fmt_supported;
	USHORT max_datagram_size;
	ULONG  Retsize;
	UCHAR flags;
	NCMDWORD val;
	int eth_headerlen;

	usbpipe=Adapter->UsbPipeForNIC;
	status= UsbclassTypeRequestCall(Adapter,USB_TYPE_CLASS | USB_DIR_IN|USB_RECIP_INTERFACE,
		USB_CDC_GET_NTB_PARAMETERS,0,usbpipe->InterfaceComm->InterfaceNumber,sizeof(STRUCT_USB_CDC_NCM_NTB_PARAMETERS),&ncm_parm,
		&Retsize);
	if(!NT_SUCCESS(status))
	{
		goto func_end;

	}	
	if(Retsize!=sizeof(STRUCT_USB_CDC_NCM_NTB_PARAMETERS))
	{
		status=STATUS_FAIL_CHECK;
		goto func_end;
	}
	/* read correct set of parameters according to device mode */
	usbpipe->rx_max =le32_to_cpu(ncm_parm.dwNtbInMaxSize);
	usbpipe->tx_max =le32_to_cpu(ncm_parm.dwNtbOutMaxSize);
	usbpipe->tx_remainder =le16_to_cpu(ncm_parm.wNdpOutPayloadRemainder);
	usbpipe->tx_modulus = le16_to_cpu(ncm_parm.wNdpOutDivisor);
	usbpipe->tx_ndp_modulus =le16_to_cpu(ncm_parm.wNdpOutAlignment);
	/* devices prior to NCM Errata shall set this field to zero */
	usbpipe->tx_max_datagrams =64;
	usbpipe->interrupt_max=usbpipe->InterfaceComm->Pipes[usbpipe->InterruptPipe].MaximumPacketSize;
	ntb_fmt_supported = le16_to_cpu(ncm_parm.bmNtbFormatsSupported);

	eth_headerlen=ETH_HEADER_SIZE;
	flags = usbpipe->ncm_desc->bmNetworkCapabilities;
	usbpipe->max_datagram_size = le16_to_cpu(usbpipe->ether_desc->wMaxSegmentSize);
	if (usbpipe->max_datagram_size < CDC_NCM_MIN_DATAGRAM_SIZE)
		usbpipe->max_datagram_size = CDC_NCM_MIN_DATAGRAM_SIZE;

	/* common absolute max for NCM and MBIM */
	if (usbpipe->max_datagram_size > CDC_NCM_MAX_DATAGRAM_SIZE)
		usbpipe->max_datagram_size = CDC_NCM_MAX_DATAGRAM_SIZE;

	/* max count of tx datagrams */
	if ((usbpipe->tx_max_datagrams == 0) ||
		(usbpipe->tx_max_datagrams > CDC_NCM_DPT_DATAGRAMS_MAX))
		usbpipe->tx_max_datagrams = CDC_NCM_DPT_DATAGRAMS_MAX;

	/* verify maximum size of received NTB in bytes */
	if (usbpipe->rx_max < USB_CDC_NCM_NTB_MIN_IN_SIZE) {
		usbpipe->rx_max = USB_CDC_NCM_NTB_MIN_IN_SIZE;
	}

	if (usbpipe->rx_max > CDC_NCM_NTB_MAX_SIZE_RX) {
		usbpipe->rx_max = CDC_NCM_NTB_MAX_SIZE_RX;
	}

	/* inform device about NTB input size changes */
	if (usbpipe->rx_max != le32_to_cpu(ncm_parm.dwNtbInMaxSize)) {
		NCMDWORD dwNtbInMaxSize =cpu_to_le32(usbpipe->rx_max);
		status= UsbclassTypeRequestCall(Adapter,USB_TYPE_CLASS | USB_DIR_OUT|USB_RECIP_INTERFACE,
			USB_CDC_SET_NTB_INPUT_SIZE,0,usbpipe->InterfaceComm->InterfaceNumber,sizeof(NCMDWORD),&dwNtbInMaxSize,
			&Retsize);
		if(!NT_SUCCESS(status))
		{
			goto func_end;

		}
	}

	/* verify maximum size of transmitted NTB in bytes */
	if (usbpipe->tx_max > CDC_NCM_NTB_MAX_SIZE_TX) {
		usbpipe->tx_max = CDC_NCM_NTB_MAX_SIZE_TX;

		/* Adding a pad byte here simplifies the handling in
		* cdc_ncm_fill_tx_frame, by making tx_max always
		* represent the real skb max size.
		*/
		if (usbpipe->tx_max % usbpipe->InterfaceComm->Pipes[usbpipe->BulkPipeOutput].MaximumPacketSize == 0)
			usbpipe->tx_max++;

	}

	/*
	* verify that the structure alignment is:
	* - power of two
	* - not greater than the maximum transmit length
	* - not less than four bytes
	*/
	val = usbpipe->tx_ndp_modulus;

	if ((val < USB_CDC_NCM_NDP_ALIGN_MIN_SIZE) ||
		(0 == val%2)|| (val >= usbpipe->tx_max)) {
			DEBUGP(MP_INFO,("Using default alignment: 4 bytes\n"));
			usbpipe->tx_ndp_modulus = USB_CDC_NCM_NDP_ALIGN_MIN_SIZE;
	}

	/*
	* verify that the payload alignment is:
	* - power of two
	* - not greater than the maximum transmit length
	* - not less than four bytes
	*/
	val = usbpipe->tx_modulus;

	if ((val < USB_CDC_NCM_NDP_ALIGN_MIN_SIZE) ||
		(0 == val%2) || (val >= usbpipe->tx_max)) {
			DEBUGP( MP_INFO,("Using default transmit modulus: 4 bytes\n"));
			usbpipe->tx_modulus = USB_CDC_NCM_NDP_ALIGN_MIN_SIZE;
	}

	/* verify the payload remainder */
	if (usbpipe->tx_remainder >= usbpipe->tx_modulus) {
		DEBUGP(MP_INFO,("Using default transmit remainder: 0 bytes\n"));
		usbpipe->tx_remainder = 0;
	}

	/* adjust TX-remainder according to NCM specification. */
	usbpipe->tx_remainder = ((usbpipe->tx_remainder - eth_headerlen) &
		(usbpipe->tx_modulus - 1));

	/* additional configuration */

	/* set CRC Mode */
	if (flags & USB_CDC_NCM_NCAP_CRC_MODE) {
		status= UsbclassTypeRequestCall(Adapter,USB_TYPE_CLASS | USB_DIR_OUT|USB_RECIP_INTERFACE,
			USB_CDC_SET_CRC_MODE,USB_CDC_NCM_CRC_NOT_APPENDED,usbpipe->InterfaceComm->InterfaceNumber,0,NULL,
			NULL);
		if(!NT_SUCCESS(status))
		{
			DEBUGP(MP_ERROR,("Setting CRC mode off failed\n"));
			goto func_end;

		}		
	}

	/* set NTB format, if both formats are supported */
	//if (ntb_fmt_supported & USB_CDC_NCM_NTH32_SIGN) {
	//	status= UsbclassTypeRequestCall(Adapter,USB_TYPE_CLASS | USB_DIR_OUT|USB_RECIP_INTERFACE,
	//		USB_CDC_SET_NTB_FORMAT,USB_CDC_NCM_NTB16_FORMAT,usbpipe->InterfaceComm->InterfaceNumber,0,NULL,
	//		NULL);
	//	if(!NT_SUCCESS(status))
	//	{
	//		status= UsbclassTypeRequestCall(Adapter,USB_TYPE_CLASS | USB_DIR_OUT|USB_RECIP_INTERFACE,
	//			USB_CDC_SET_NTB_FORMAT,USB_CDC_NCM_NTB32_FORMAT,usbpipe->InterfaceComm->InterfaceNumber,0,NULL,
	//			NULL);
	//		DEBUGP(MP_ERROR,("Setting NTB format to 16-bit failed\n"));
	//		goto func_end;
	//	}	
	//}

	/* inform the device about the selected Max Datagram Size */
	if (!(flags & USB_CDC_NCM_NCAP_MAX_DATAGRAM_SIZE))
		goto func_end;

	/* read current mtu value from device */

	status= UsbclassTypeRequestCall(Adapter,USB_TYPE_CLASS | USB_DIR_IN|USB_RECIP_INTERFACE,
		USB_CDC_GET_MAX_DATAGRAM_SIZE,0,usbpipe->InterfaceComm->InterfaceNumber,2, &max_datagram_size,
		&Retsize);
	if(!NT_SUCCESS(status))
	{
		DEBUGP(MP_ERROR,("GET_MAX_DATAGRAM_SIZE failed\n"));
		goto func_end;
	}	

	if (le16_to_cpu(max_datagram_size) == usbpipe->max_datagram_size)
		goto func_end;

	max_datagram_size = cpu_to_le16(usbpipe->max_datagram_size);
	status= UsbclassTypeRequestCall(Adapter,USB_TYPE_CLASS | USB_DIR_OUT|USB_RECIP_INTERFACE,
		USB_CDC_SET_MAX_DATAGRAM_SIZE,0,usbpipe->InterfaceComm->InterfaceNumber,2, &max_datagram_size,
		&Retsize);
	if(!NT_SUCCESS(status))
	{
		DEBUGP(MP_ERROR,("SET_MAX_DATAGRAM_SIZE failed\n"));
		goto func_end;
	}	
func_end:
	return status;
}


size_t tcb_tailroom(PTCB pTcb)
{
	return (NIC_BUFFER_SIZE-pTcb->ulSize);
}

void * tcb_put(PTCB ptcb, size_t len)  
{  
	void *tmp =ptcb->pData+ptcb->ulSize;  
	///改变相应的域。  
	ptcb->Buffer->ByteCount+=(ULONG) len;
	ptcb->ulSize+=(ULONG)len;
	return tmp;  
}  
void cdc_ncm_align_tail(PTCB ptcb, size_t modulus, size_t remainder, size_t max)
{
	size_t align = ALIGN(ptcb->ulSize, modulus) - ptcb->ulSize + remainder;

	if (ptcb->ulSize + align > max)
		align = max - ptcb->ulSize;
	if (align && tcb_tailroom(ptcb) >= align)
		memset(tcb_put(ptcb, align), 0, align);
}

/* return a pointer to a valid struct usb_cdc_ncm_ndp16 of type sign, possibly
* allocating a new one within skb
*/
PUSB_CDC_NCM_NDP16 cdc_ncm_ndp(PMP_ADAPTER Adapter,PTCB ptcb, NCMDWORD sign, size_t reserve)
{
	PUSB_CDC_NCM_NDP16 ndp16 = NULL;
	PUSB_CDC_NCM_NTH16 nth16 = (void *)ptcb->pData;
	PMP_USBPIPE usbpipe=Adapter->UsbPipeForNIC;
	size_t ndpoffset = le16_to_cpu(nth16->wFpIndex);

	/* follow the chain of NDPs, looking for a match */
	while (ndpoffset) {
		ndp16 = (PUSB_CDC_NCM_NDP16)(ptcb->pData + ndpoffset);
		if (ndp16->dwSignature==sign)
			return ndp16;
		ndpoffset = le16_to_cpu(ndp16->wNextFpIndex);
	}

	/* align new NDP */
	cdc_ncm_align_tail(ptcb,usbpipe->tx_ndp_modulus, 0, usbpipe->tx_max);

	/* verify that there is room for the NDP and the datagram (reserve) */
	if ((usbpipe->tx_max - (ptcb->ulSize) - reserve) < CDC_NCM_NDP_SIZE)
		return NULL;

	/* link to it */
	if (ndp16)
		ndp16->wNextFpIndex = cpu_to_le16((USHORT)ptcb->ulSize);
	else
		nth16->wFpIndex = cpu_to_le16((USHORT)ptcb->ulSize);

	/* push a new empty NDP */
	ndp16 = (PUSB_CDC_NCM_NDP16)tcb_put(ptcb, CDC_NCM_NDP_SIZE);
	NdisZeroMemory(ndp16, CDC_NCM_NDP_SIZE);
	ndp16->dwSignature = sign;
	ndp16->wLength = cpu_to_le16(sizeof(USB_CDC_NCM_NDP16));
	return ndp16;
}

BOOLEAN  
cdc_ncm_fill_tx_frame(PMP_ADAPTER Adapter,PNDIS_PACKET Packet,PTCB ptcb, NCMDWORD sign)
{
	PMP_USBPIPE  usbpipe = Adapter->UsbPipeForNIC;
	PUSB_CDC_NCM_NTH16  nth16;
	PUSB_CDC_NCM_NDP16 ndp16;
	USHORT n = 0, index, ndplen;
	UINT           PacketLength;  
	PNDIS_BUFFER   CurrentBuffer = NULL;
	PVOID          VirtualAddress = NULL;
	UINT           CurrentLength;
	BOOLEAN        bResult = TRUE;
	int            maxpacketsize;
	int max_datagrams;
	maxpacketsize=usbpipe->InterfaceData->Pipes[usbpipe->BulkPipeOutput].MaximumPacketSize;

	if (Packet == NULL) {
		return TRUE;
	}

	 max_datagrams=usbpipe->tx_max_datagrams;

	if (0==ptcb->ulSize) {

		/* fill out the initial 16-bit NTB header */
		nth16 = (PUSB_CDC_NCM_NTH16 )memset(tcb_put(ptcb, sizeof(USB_CDC_NCM_NTH16)), 0, sizeof(USB_CDC_NCM_NTH16));
		nth16->dwSignature = cpu_to_le32(USB_CDC_NCM_NTH16_SIGN);
		nth16->wHeaderLength = cpu_to_le16(sizeof(USB_CDC_NCM_NTH16));
		nth16->wSequence = cpu_to_le16(usbpipe->tx_seq++);

		/* count total number of frames in this NTB */
		ptcb->NumofOrgSendPacket= 0;
	}

	NdisQueryPacket(Packet,
		NULL,
		NULL,
		&CurrentBuffer,
		&PacketLength);

    n = ptcb->NumofOrgSendPacket;

	if(n>=max_datagrams)
	{
		ptcb->bRead2Send=1;
	}
	while(n<max_datagrams) {

		/* get the appropriate NDP for this skb */
		ndp16 = cdc_ncm_ndp(Adapter, ptcb, sign, PacketLength + usbpipe->tx_modulus + usbpipe->tx_remainder);

		/* align beginning of next frame */
		cdc_ncm_align_tail(ptcb,  usbpipe->tx_modulus, usbpipe->tx_remainder, usbpipe->tx_max);

		/* check if we had enough room left for both NDP and frame */
		if (NULL==ndp16 || ptcb->ulSize + PacketLength > usbpipe->tx_max) {
			if (n == 0) {
				ptcb->bRead2Send=0;
			} else {
				ptcb->bRead2Send = 1;
			}
			bResult=FALSE;
			break;
		}

		/* calculate frame number withing this NDP */
		ndplen = le16_to_cpu(ndp16->wLength);
		index = (ndplen - sizeof(USB_CDC_NCM_NDP16)) / sizeof(USB_CDC_NCM_DPE16);

		/* OK, add this Packet */
		ndp16->dpe16[index].wDatagramLength = cpu_to_le16((USHORT)PacketLength);
		ndp16->dpe16[index].wDatagramIndex = cpu_to_le16((USHORT)ptcb->ulSize);
		ndp16->wLength = cpu_to_le16(ndplen + sizeof(USB_CDC_NCM_DPE16));

		while(CurrentBuffer)
		{
			NdisQueryBufferSafe(
				CurrentBuffer,
				&VirtualAddress,
				&CurrentLength,
				NormalPagePriority);

			ASSERT(NULL!=VirtualAddress);

			CurrentLength = min(CurrentLength, PacketLength);         

			if(CurrentLength)
			{
				// Copy the data.
				NdisMoveMemory(tcb_put(ptcb, CurrentLength), VirtualAddress, CurrentLength);
				PacketLength -= CurrentLength;            
			}
			NdisGetNextBuffer(
				CurrentBuffer,
				&CurrentBuffer);
		}
		if(PacketLength){
			NdisZeroMemory(tcb_put(ptcb, PacketLength), PacketLength);
		    PacketLength=0;
		}


		InsertTailList(
			&ptcb->ListOrgSendPacket, 
			(PLIST_ENTRY)&Packet->MiniportReserved[0]);

		ptcb->NumofOrgSendPacket++;

		/* send now if this NDP is full */
		if (index >= CDC_NCM_DPT_DATAGRAMS_MAX) {
			ptcb->bRead2Send = 1;
			break;
		}
		break;
	}

	/* If collected data size is less or equal CDC_NCM_MIN_TX_PKT
	* bytes, we send buffers as it is. If we get more data, it
	* would be more efficient for USB HS mobile device with DMA
	* engine to receive a full size NTB, than canceling DMA
	* transfer and receiving a short packet.
	*
	* This optimization support is pointless if we end up sending
	* a ZLP after full sized NTBs.
	*/
	if (ptcb->bRead2Send==1&&(ptcb->ulSize%maxpacketsize== 0))
		memset(tcb_put(ptcb, 1), 0, 1);/* force short packet */

	/* set final frame length */
	nth16 = (PUSB_CDC_NCM_NTH16 )ptcb->pData;
	nth16->wBlockLength = cpu_to_le16((USHORT)ptcb->ulSize);

	return bResult;
}
/* verify NTB header and return offset of first NDP, or negative error */
NTSTATUS cdc_ncm_rx_verify_nth16(PMP_ADAPTER Adapter, PRCB prcb,int *ndpoffset)
{
	PUSB_CDC_NCM_NTH16 nth16;
	PMP_USBPIPE usbpipe=Adapter->UsbPipeForNIC;
	NTSTATUS NtStatus=STATUS_SUCCESS;
	USHORT len;

	if (prcb->ulSize < (sizeof(USB_CDC_NCM_NTH16) +
		sizeof(USB_CDC_NCM_NDP16))) {
			DEBUGP(MP_ERROR,("recevied frame too short\n"));
			NtStatus=STATUS_FAIL_CHECK;
			goto Exit;
	}

	nth16 = (PUSB_CDC_NCM_NTH16 )prcb->pDataForNTB;

	if (nth16->dwSignature != cpu_to_le32(USB_CDC_NCM_NTH16_SIGN)) {
		DEBUGP(MP_ERROR,("invalid NTH16 signature <%#010x>\n",
			le32_to_cpu(nth16->dwSignature)));
		NtStatus=STATUS_FAIL_CHECK;
		goto Exit;
	}

	len = le16_to_cpu(nth16->wBlockLength);
	if (len > usbpipe->rx_max) {
		DEBUGP(MP_ERROR,("unsupported NTB block length %u/%u\n", len,
			usbpipe->rx_max));
		goto Exit;
	}

	if ((usbpipe->rx_seq + 1) != le16_to_cpu(nth16->wSequence) &&
		(usbpipe->rx_seq || le16_to_cpu(nth16->wSequence)) &&
		!((usbpipe->rx_seq == 0xffff) && !le16_to_cpu(nth16->wSequence))) {
			DEBUGP(MP_INFO,("sequence number glitch prev=%d curr=%d\n",
				usbpipe->rx_seq, le16_to_cpu(nth16->wSequence)));
	}
	usbpipe->rx_seq = le16_to_cpu(nth16->wSequence);

	*ndpoffset= le16_to_cpu(nth16->wFpIndex);
Exit:
	return NtStatus;
}



/* verify NDP header and return number of datagrams, or negative error */
NTSTATUS cdc_ncm_rx_verify_ndp16(PRCB prcb, int ndpoffset,int *datagrams)
{	
	PUSB_CDC_NCM_NDP16 ndp16;
	NTSTATUS           NtStatus  = STATUS_SUCCESS;


	if ((ndpoffset + sizeof(USB_CDC_NCM_NDP16)) > prcb->ulSize) {
		DEBUGP(MP_ERROR,("invalid NDP offset  <%u>\n",
			ndpoffset));
		NtStatus=STATUS_FAIL_CHECK;
		goto Exit;
	}
	ndp16 = (USB_CDC_NCM_NDP16 *)(prcb->pDataForNTB + ndpoffset);

	if (le16_to_cpu(ndp16->wLength) < USB_CDC_NCM_NDP16_LENGTH_MIN) {
		DEBUGP(MP_ERROR,("invalid DPT16 length <%u>\n",
			le16_to_cpu(ndp16->wLength)) );
		NtStatus=STATUS_FAIL_CHECK;
		goto Exit;
	}

	*datagrams = ((le16_to_cpu(ndp16->wLength) -sizeof(USB_CDC_NCM_NDP16)) /
		sizeof(USB_CDC_NCM_DPE16));
	//(*datagrams)--; /* we process NDP entries except for the last one */,already except for the last one

	if ((sizeof( USB_CDC_NCM_NDP16) +
		(*datagrams) * (sizeof( USB_CDC_NCM_DPE16))) > prcb->ulSize) {
			DEBUGP(MP_ERROR,("Invalid nframes = %d\n", (*datagrams)) );
			NtStatus =STATUS_FAIL_CHECK;
			goto Exit;
	}
Exit:
	return NtStatus;
}

BOOLEAN cdc_ncm_rx_fixup(PMP_ADAPTER Adapter, PRCB prcb)
{
	NTSTATUS NtStatus=STATUS_SUCCESS;
	PMP_USBPIPE usbpipe=Adapter->UsbPipeForNIC;
	NCMDWORD len;
	int nframes;
	int x;
	int offset;
	PUSB_CDC_NCM_NDP16 ndp16;
	PUSB_CDC_NCM_DPE16 dpe16;
	int ndpoffset,ndeoffset;
	int currentFrame;
	int indexArray=0;
	int loopcount = 50; /* arbitrary max preventing infinite loop */

	if(prcb->bIsOver) return TRUE;

	ndpoffset=prcb->nextndpoffset;
	ndeoffset=prcb->nextndeoffset;

	if (0==ndpoffset)
	{
		NtStatus=cdc_ncm_rx_verify_nth16(Adapter, prcb,&ndpoffset);
		if (!NT_SUCCESS(NtStatus))
			goto error;
	}

next_ndp:
	NtStatus = cdc_ncm_rx_verify_ndp16(prcb, ndpoffset,&nframes);
	if (!NT_SUCCESS(NtStatus))
		goto error;

	ndp16 = (PUSB_CDC_NCM_NDP16)(prcb->pDataForNTB + ndpoffset);

	if (ndp16->dwSignature != cpu_to_le32(USB_CDC_NCM_NDP16_NOCRC_SIGN)) {
		DEBUGP(MP_ERROR,("invalid DPT16 signature <%#010x>\n",le32_to_cpu(ndp16->dwSignature)) );
		goto err_ndp;
	}
	if(0==ndeoffset)
	   dpe16 = ndp16->dpe16;
	else
	   dpe16=(PUSB_CDC_NCM_DPE16)((PUCHAR)ndp16+ndeoffset);

     currentFrame=(int)((dpe16-ndp16->dpe16)/sizeof(USB_CDC_NCM_DPE16));

	for (x = currentFrame; x < nframes; x++, dpe16++) {
		offset = le16_to_cpu(dpe16->wDatagramIndex);
		len = le16_to_cpu(dpe16->wDatagramLength);

		/*
		* CDC NCM ch. 3.7
		* All entries after first NULL entry are to be ignored
		*/
		if ((offset == 0) || (len == 0)) {
			if (!x)
				goto err_ndp; /* empty NTB */
			break;
		}

		/* sanity checking */
		if (((offset + len) > prcb->ulSize) ||
			(len > usbpipe->rx_max) || (len < ETH_HEADER_SIZE)) {
				DEBUGP(MP_ERROR,("invalid frame detected (ignored) offset[%u]=%u, length=%u, skb=%p\n",
					x, offset, len, prcb) );
				if (!x)
					goto err_ndp;
				break;

		} else { 
			if(indexArray>=RCB_BUFFERARRAY_SIZE)
			{
				prcb->nextndpoffset=ndpoffset;
				prcb->nextndeoffset=(NCMDWORD)((PUCHAR)dpe16-(PUCHAR)ndp16);
				return FALSE;
			}
			else{
			 NICIndicateReceivedPacket(prcb,offset,len,indexArray); 
			 Adapter->GoodReceives++;
			 indexArray++;
			}
		}
	}
err_ndp:
	/* are there more NDPs to process? */
	ndpoffset = le16_to_cpu(ndp16->wNextFpIndex);
	if (ndpoffset && loopcount--)
		goto next_ndp;
	else
	{
		prcb->bIsOver=TRUE;
	}
error:
	if (!NT_SUCCESS(NtStatus))
	{
		prcb->bIsOver=TRUE;
	}
	return prcb->bIsOver;
}

 void
cdc_ncm_speed_change(PMP_ADAPTER Adapter,PUSB_CDC_SPEED_CHANGE speedchange)
{
	NCMDWORD rx_speed = le32_to_cpu(speedchange->DLBitRRate);
	NCMDWORD tx_speed = le32_to_cpu(speedchange->ULBitRate);

	Adapter->UsbPipeForNIC->rx_speed=rx_speed;
    Adapter->UsbPipeForNIC->tx_speed=tx_speed;
	if ((tx_speed > 1000000) && (rx_speed > 1000000)) {
		DEBUGP(MP_INFO,("%u mbit/s downlink %u mbit/s uplink\n",
			(unsigned int)(rx_speed / 1000000U),
			(unsigned int)(tx_speed / 1000000U)) );
	} else {
		DEBUGP(MP_INFO,("%u kbit/s downlink %u kbit/s uplink\n",
			(unsigned int)(rx_speed / 1000U),
			(unsigned int)(tx_speed / 1000U)) );
	}

}
 void cdc_ncm_status(PMP_ADAPTER Adapter, PVOID buffer,NCMDWORD length)
 {
	 PUSB_CDC_NOTIFICATION  event;
	 PMP_USBPIPE usbpipe=Adapter->UsbPipeForNIC;

	 if (length< sizeof(USB_CDC_NOTIFICATION))   return;


	 event =(PUSB_CDC_NOTIFICATION)buffer;

	 switch (event->bNotificationType)
	 {
	 case USB_CDC_NOTIFY_NETWORK_CONNECTION:
		 /*
		 * According to the CDC NCM specification ch.7.1
		 * USB_CDC_NOTIFY_NETWORK_CONNECTION notification shall be
		 * sent by device after USB_CDC_NOTIFY_SPEED_CHANGE.
		 */
		 usbpipe->connected = le16_to_cpu(event->wValue);
		 DEBUGP(MP_INFO,("network connection: %sconnected\n",usbpipe->connected ? "" : "dis"));
		 if(usbpipe->connected)
		 {
			 MP_CLEAR_FLAG(Adapter,fMP_DISCONNECTED);
		 }else
		 {
			 MP_SET_FLAG(Adapter,fMP_DISCONNECTED);
		 }
		 break;

	 case USB_CDC_NOTIFY_SPEED_CHANGE:
		 if (length>=(sizeof(USB_CDC_NOTIFICATION) +sizeof(USB_CDC_SPEED_CHANGE)))
		 {
			 cdc_ncm_speed_change(Adapter,
				 ( PUSB_CDC_SPEED_CHANGE)(event+sizeof(USB_CDC_SPEED_CHANGE)));
		 }else
		 {
			 DEBUGP(MP_INFO,("USB_CDC_NOTIFY_SPEED_CHANGE notify length is short:%d\n",length));
		 }
		 break;

	 default:
		 DEBUGP(MP_ERROR,("NCM: unexpected notification 0x%02x!\n",
			 event->bNotificationType) );
		 break;
	 }
 }


 USHORT   NdisPacketfilter2UsbPacketFilter(ULONG packetFilter)
 {
	 USHORT usbPacketFilter=0;
	 if(packetFilter&NDIS_PACKET_TYPE_PROMISCUOUS)
	 {
		 usbPacketFilter|=USB_NCM_PACKET_TYPE_PROMISCUOUS;
	 }
	 if(packetFilter&NDIS_PACKET_TYPE_ALL_MULTICAST)
	 {
		 usbPacketFilter|=USB_NCM_PACKET_TYPE_ALL_MULTICAST;
	 }
	 if(packetFilter&NDIS_PACKET_TYPE_DIRECTED)
	 {
		 usbPacketFilter|=USB_NCM_PACKET_TYPE_DIRECTED;
	 }
	 if(packetFilter&NDIS_PACKET_TYPE_BROADCAST)
	 {
		 usbPacketFilter|=USB_NCM_PACKET_TYPE_BROADCAST;
	 }
	 if(packetFilter&NDIS_PACKET_TYPE_MULTICAST)
	 {
		 usbPacketFilter|=USB_NCM_PACKET_TYPE_MULTICAST;
	 }
	 return usbPacketFilter;
 }