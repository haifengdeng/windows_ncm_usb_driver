#ifndef _USB_STRUCTURE_H_
#define _USB_STRUCTURE_H_

#include "usbdi.h"
#include "usbdlib.h"
#include "ntstrsafe.h"

// Definitions for USB CDC
#define USB_CDC_SUBCLASS_ETHERNET       0x06
#define USB_CDC_SUBCLASS_NCM            0x0D

#define USB_CDC_CS_INTERFACE 0x24

#define USB_CDC_ETHERNET_TYPE       0x0f    /** ether_desc */
#define USB_CDC_NCM_TYPE        0x1A

typedef unsigned int NCMDWORD;


#pragma pack(push) //保存对齐状态
#pragma pack(1)//设定为1字节对齐
typedef struct _USB_CDC_NCM_DESC {
	UCHAR        bFunctionLength;      // Length of this descriptor
	UCHAR        bDescriptorType;      // ncm function type,should be USB_CDC_CS_INTERFACE
	UCHAR        bDescriptorSubtype;   // NCM Functional Descriptor subtype,should be USB_CDC_NCM_TYPE
	USHORT       bcdNcmVersion;        // Release number of this specification in BCD(0x0100)
	                                   //
	UCHAR        bmNetworkCapabilities;//Specifies the capabilities of this function. A bit value of 
	                                   //zero indicates that the capability is not supported.
	                                   //D7..D5:  Reserved (zero)
                                       //D4:   Function can process SetCrcMode and 
                                       //	   GetCrcMode requests
                                       //D3:   Function can process SetMaxDatagramSize and GetMaxDatagramSize requests.
                                       //D2:   Function can process SendEncapsulatedCommand and GetEncapsulatedResponse
                                       //	   requests.
                                       //D1:   Function can process GetNetAddress and 
                                       //	   SetNetAddress requests. 
                                       //D0:   Function can process SetEthenetPacketFilter requests, as defined in [USBECM12]. 
                                       //	   If not set, broadcast, directed and multicast 
                                       //	   packets are always passed to the host.    
} USB_CDC_NCM_DESC, *PUSB_CDC_NCM_DESC;
#pragma pack(pop)//恢复对齐状态

#pragma pack(push) //保存对齐状态
#pragma pack(1)//设定为1字节对齐
typedef struct _USB_CDC_ETHER_DESC
{
	UCHAR        bFunctionLength;      // Length of this descriptor
	UCHAR        bDescriptorType;      // ETH_networking function type,should be USB_CDC_CS_INTERFACE
	UCHAR        bDescriptorSubtype;   // ETH_networking Functional Descriptor subtype,should be USB_CDC_ETHERNET_TYPE
	                                   //
	UCHAR        iMACAddress;           // Index of string descriptor. The string 
										// descriptor holds the 48bit Ethernet MAC 
										// address. The Unicode representation of the 
										// MAC address is as follows: the first Unicode 
										// character represents the high order nibble of 
										// the first byte of the MAC address in network 
										// byte order. The next character represents the 
										// next 4 bits and so on. The Unicode character 
										// is chosen from the set of values 30h through 
										// 39h and 41h through 46h (0-9 and A-F). 
										// iMACAddress can not be zero and the 
										// Unicode representation must be 12 characters 
										// long. For example, the MAC Address 
										// 0123456789ABh is represented as the 
										// Unicode string "0123456789AB".
	NCMDWORD       bmEthernetStatistics;   //Indicates which Ethernet statistics functions 
										//the device collects. If a bit is set to 0, the host 
										//network driver is expected to keep count for 
										//the corresponding statistic (if able).
	USHORT      wMaxSegmentSize;/*The maximum segment size that the Ethernet 
		                        device is capable of supporting. This is 
		                        typically 1514 bytes, but could be extended */
	USHORT      wNumberMCFilters;/*Contains the number of multicast filters that 
											can be configured by the host. 
									D15:  0 - The device performs perfect 
										  multicast address filtering (no 
										  hashing). 
										  1- The device uses imperfect 
										  multicast address filtering 
										  (hashing). Here, the host software 
										  driver must perform further 
										  qualification itself to achieve 
										  perfect filtering. 
									D14..0:  Indicates the number of multicast 
										  address filters supported by the 
										  device (0 to 32767). If the host 
										  finds the number of filters 
										  supported by the device to be 
										  inadequate, it may choose to set 
										  the device's Ethernet Packet Filter 
										  to forward all multicast frames to 
										  the host, performing all multicast 
										  filtering in software instead. 
										  If this value is 0, the device does 
										  not support the 
										  SetEthernetMulticastFilters 
										  request. */
	UCHAR       bNumberPowerFilters;/*Contains the number of pattern filters that are 
		                            available for causing wake-up of the host.*/ 

}USB_CDC_ETHER_DESC ,*PUSB_CDC_ETHER_DESC;
#pragma pack(pop)//恢复对齐状态
/**-------------------------------------------------------------------------*/

/**
 * Class-Specific Control Requests (6.2)
 *
 * section 3.6.2.1 table 4 has the ACM profile, for modems.
 * section 3.8.2 table 10 has the ethernet profile.
 *
 * Microsoft's RNDIS stack for Ethernet is a vendor-specific CDC ACM variant,
 * heavily dependent on the encapsulated (proprietary) command mechanism.
 */

#define USB_CDC_SEND_ENCAPSULATED_COMMAND	0x00
#define USB_CDC_GET_ENCAPSULATED_RESPONSE	0x01
#define USB_CDC_REQ_SET_LINE_CODING		0x20
#define USB_CDC_REQ_GET_LINE_CODING		0x21
#define USB_CDC_REQ_SET_CONTROL_LINE_STATE	0x22
#define USB_CDC_REQ_SEND_BREAK			0x23
#define USB_CDC_SET_ETHERNET_MULTICAST_FILTERS	0x40
#define USB_CDC_SET_ETHERNET_PM_PATTERN_FILTER	0x41
#define USB_CDC_GET_ETHERNET_PM_PATTERN_FILTER	0x42
#define USB_CDC_SET_ETHERNET_PACKET_FILTER	0x43
#define USB_CDC_GET_ETHERNET_STATISTIC		0x44
#define USB_CDC_GET_NTB_PARAMETERS		0x80
#define USB_CDC_GET_NET_ADDRESS			0x81
#define USB_CDC_SET_NET_ADDRESS			0x82
#define USB_CDC_GET_NTB_FORMAT			0x83
#define USB_CDC_SET_NTB_FORMAT			0x84
#define USB_CDC_GET_NTB_INPUT_SIZE		0x85
#define USB_CDC_SET_NTB_INPUT_SIZE		0x86
#define USB_CDC_GET_MAX_DATAGRAM_SIZE		0x87
#define USB_CDC_SET_MAX_DATAGRAM_SIZE		0x88
#define USB_CDC_GET_CRC_MODE			0x89
#define USB_CDC_SET_CRC_MODE			0x8a

#pragma pack(push) //保存对齐状态
#pragma pack(1)//设定为1字节对齐
/** Line Coding Structure from CDC spec 6.2.13 */
struct usb_cdc_line_coding {
	NCMDWORD	dwDTERate;
	UCHAR	bCharFormat;
#define USB_CDC_1_STOP_BITS			0
#define USB_CDC_1_5_STOP_BITS			1
#define USB_CDC_2_STOP_BITS			2

	UCHAR	bParityType;
#define USB_CDC_NO_PARITY			0
#define USB_CDC_ODD_PARITY			1
#define USB_CDC_EVEN_PARITY			2
#define USB_CDC_MARK_PARITY			3
#define USB_CDC_SPACE_PARITY			4

	UCHAR	bDataBits;
}USB_CDC_LINE_CODING;
#pragma  pack(pop)


/** table 62; bits in multicast filter */
#define	USB_CDC_PACKET_TYPE_PROMISCUOUS		(1 << 0)
#define	USB_CDC_PACKET_TYPE_ALL_MULTICAST	(1 << 1) /** no filter */
#define	USB_CDC_PACKET_TYPE_DIRECTED		(1 << 2)
#define	USB_CDC_PACKET_TYPE_BROADCAST		(1 << 3)
#define	USB_CDC_PACKET_TYPE_MULTICAST		(1 << 4) /** filtered */

/**-------------------------------------------------------------------------*/

/**
 * Class-Specific Notifications (6.3) sent by interrupt transfers
 *
 * section 3.8.2 table 11 of the CDC spec lists Ethernet notifications
 * section 3.6.2.1 table 5 specifies ACM notifications, accepted by RNDIS
 * RNDIS also defines its own bit-incompatible notifications
 */

#define USB_CDC_NOTIFY_NETWORK_CONNECTION	0x00
#define USB_CDC_NOTIFY_RESPONSE_AVAILABLE	0x01
#define USB_CDC_NOTIFY_SERIAL_STATE		0x20
#define USB_CDC_NOTIFY_SPEED_CHANGE		0x2a

#pragma pack(push) //保存对齐状态
#pragma pack(1)//设定为1字节对齐
typedef struct _usb_cdc_notification {
	UCHAR	bmRequestType;
	UCHAR	bNotificationType;
	USHORT	wValue;
	USHORT	wIndex;
	USHORT	wLength;
} USB_CDC_NOTIFICATION,*PUSB_CDC_NOTIFICATION;
#pragma  pack(pop)
/**-------------------------------------------------------------------------*/

/**
 * Class Specific structures and constants
 *
 * CDC NCM NTB parameters structure, CDC NCM subclass 6.2.1
 *
 */
#pragma pack(push) //保存对齐状态
#pragma pack(1)//设定为1字节对齐
typedef struct _usb_cdc_ncm_ntb_parameters {
	USHORT	wLength;
	USHORT	bmNtbFormatsSupported;
	NCMDWORD	dwNtbInMaxSize;
	USHORT	wNdpInDivisor;
	USHORT	wNdpInPayloadRemainder;
	USHORT	wNdpInAlignment;
	USHORT	wPadding1;
	NCMDWORD	dwNtbOutMaxSize;
	USHORT	wNdpOutDivisor;
	USHORT	wNdpOutPayloadRemainder;
	USHORT	wNdpOutAlignment;
	USHORT	wPadding2;
}STRUCT_USB_CDC_NCM_NTB_PARAMETERS,*PSTRUCTUSB_CDC_NCM_NTB_PARAMETERS;
#pragma  pack(pop)
/**
 * CDC NCM transfer headers, CDC NCM subclass 3.2
 */

#define USB_CDC_NCM_NTH16_SIGN		0x484D434E /** NCMH */
#define USB_CDC_NCM_NTH32_SIGN		0x686D636E /** ncmh */


#pragma pack(push) //保存对齐状态
#pragma pack(1)//设定为1字节对齐
typedef struct _usb_cdc_ncm_nth16 {
	NCMDWORD	dwSignature;
	USHORT	wHeaderLength;
	USHORT	wSequence;
	USHORT	wBlockLength;
	USHORT	wFpIndex;
}USB_CDC_NCM_NTH16,*PUSB_CDC_NCM_NTH16;
#pragma pack(pop)


#pragma pack(push) //保存对齐状态
#pragma pack(1)//设定为1字节对齐
typedef struct _usb_cdc_ncm_nth32 {
	NCMDWORD	dwSignature;
	USHORT	wHeaderLength;
	USHORT	wSequence;
	NCMDWORD	dwBlockLength;
	NCMDWORD	dwFpIndex;
}USB_CDC_NCM_NTH32,*PUSB_CDC_NCM_NTH32;
#pragma pack(pop)
/**
 * CDC NCM datagram pointers, CDC NCM subclass 3.3
 */

#define USB_CDC_NCM_NDP16_CRC_SIGN	0x314D434E /** NCM1 */
#define USB_CDC_NCM_NDP16_NOCRC_SIGN	0x304D434E /** NCM0 */
#define USB_CDC_NCM_NDP32_CRC_SIGN	0x316D636E /** ncm1 */
#define USB_CDC_NCM_NDP32_NOCRC_SIGN	0x306D636E /** ncm0 */

#pragma pack(push) //保存对齐状态
#pragma pack(1)//设定为1字节对齐
/** 16-bit NCM Datagram Pointer Entry */
typedef struct _usb_cdc_ncm_dpe16 {
	USHORT	wDatagramIndex;
	USHORT	wDatagramLength;
}USB_CDC_NCM_DPE16,*PUSB_CDC_NCM_DPE16;
#pragma  pack(pop)

#pragma pack(push) //保存对齐状态
#pragma pack(1)//设定为1字节对齐
/** 16-bit NCM Datagram Pointer Table */
typedef struct _usb_cdc_ncm_ndp16 {
	NCMDWORD	dwSignature;
	USHORT	wLength;
	USHORT	wNextFpIndex;
	USB_CDC_NCM_DPE16 dpe16[1];
}USB_CDC_NCM_NDP16,*PUSB_CDC_NCM_NDP16;
#pragma  pack(pop)

#pragma pack(push) //保存对齐状态
#pragma pack(1)//设定为1字节对齐
/** 32-bit NCM Datagram Pointer Entry */
typedef struct _usb_cdc_ncm_dpe32 {
	NCMDWORD	dwDatagramIndex;
	NCMDWORD	dwDatagramLength;
}USB_CDC_NCM_DPE32,*PUSB_CDC_NCM_DPE32;
#pragma  pack(pop)

#pragma pack(push) //保存对齐状态
#pragma pack(1)//设定为1字节对齐
/** 32-bit NCM Datagram Pointer Table */
typedef struct _usb_cdc_ncm_ndp32 {
	NCMDWORD	dwSignature;
	USHORT	wLength;
	USHORT	wReserved6;
	NCMDWORD	dwNextNdpIndex;
	NCMDWORD	dwReserved12;
	USB_CDC_NCM_DPE32 dpe32[1];
}USB_CDC_NCM_NDP32,*PUSB_CDC_NCM_NDP32;
#pragma  pack(pop)
/** CDC NCM subclass 3.2.1 and 3.2.2 */
#define USB_CDC_NCM_NDP16_INDEX_MIN			0x000C
#define USB_CDC_NCM_NDP32_INDEX_MIN			0x0010

/** CDC NCM subclass 3.3.3 Datagram Formatting */
#define USB_CDC_NCM_DATAGRAM_FORMAT_CRC			0x30
#define USB_CDC_NCM_DATAGRAM_FORMAT_NOCRC		0X31

/** CDC NCM subclass 4.2 NCM Communications Interface Protocol Code */
#define USB_CDC_NCM_PROTO_CODE_NO_ENCAP_COMMANDS	0x00
#define USB_CDC_NCM_PROTO_CODE_EXTERN_PROTO		0xFE

/** CDC NCM subclass 5.2.1 NCM Functional Descriptor, bmNetworkCapabilities */
#define USB_CDC_NCM_NCAP_ETH_FILTER			(1 << 0)
#define USB_CDC_NCM_NCAP_NET_ADDRESS			(1 << 1)
#define USB_CDC_NCM_NCAP_ENCAP_COMMAND			(1 << 2)
#define USB_CDC_NCM_NCAP_MAX_DATAGRAM_SIZE		(1 << 3)
#define USB_CDC_NCM_NCAP_CRC_MODE			(1 << 4)

/** CDC NCM subclass Table 6-3: NTB Parameter Structure */
#define USB_CDC_NCM_NTB16_SUPPORTED			(1 << 0)
#define USB_CDC_NCM_NTB32_SUPPORTED			(1 << 1)

/** CDC NCM subclass Table 6-3: NTB Parameter Structure */
#define USB_CDC_NCM_NDP_ALIGN_MIN_SIZE			0x04
#define USB_CDC_NCM_NTB_MAX_LENGTH			0x1C

/** CDC NCM subclass 6.2.5 SetNtbFormat */
#define USB_CDC_NCM_NTB16_FORMAT			0x00
#define USB_CDC_NCM_NTB32_FORMAT			0x01

/** CDC NCM subclass 6.2.7 SetNtbInputSize */
#define USB_CDC_NCM_NTB_MIN_IN_SIZE			2048
#define USB_CDC_NCM_NTB_MIN_OUT_SIZE			2048

/** CDC NCM subclass 6.2.11 SetCrcMode */
#define USB_CDC_NCM_CRC_NOT_APPENDED			0x00
#define USB_CDC_NCM_CRC_APPENDED			0x01

#define  USB_EXFreePool(pool) \
	if (pool)                 \
{                         \
	ExFreePoolWithTag(pool,NIC_TAG);       \
	pool= NULL;              \
}


/*
 * USB types
 */
#define USB_TYPE_MASK			(0x03 << 5)
#define USB_TYPE_STANDARD		(0x00 << 5)
#define USB_TYPE_CLASS			(0x01 << 5)
#define USB_TYPE_VENDOR			(0x02 << 5)
#define USB_TYPE_RESERVED		(0x03 << 5)

/*
 * USB recipients
 */
#define USB_RECIP_MASK			0x1f
#define USB_RECIP_DEVICE		0x00
#define USB_RECIP_INTERFACE		0x01
#define USB_RECIP_ENDPOINT		0x02
#define USB_RECIP_OTHER			0x03

/*
 * USB directions
 */
#define USB_DIR_OUT			0		/* to device */
#define USB_DIR_IN			0x80		/* to host */

#define cpu_to_le32(p)  p
#define le32_to_cpu(p)  p
#define le16_to_cpu(p)  p
#define cpu_to_le16(p)  p

#pragma pack(push) //保存对齐状态
#pragma pack(1)//设定为1字节对齐
typedef struct _usb_cdc_speed_change {
	         NCMDWORD  DLBitRRate;     /* contains the downlink bit rate (IN pipe) */
	         NCMDWORD  ULBitRate;      /* contains the uplink bit rate (OUT pipe) */
 }USB_CDC_SPEED_CHANGE,*PUSB_CDC_SPEED_CHANGE;
#pragma pack(pop)
#endif