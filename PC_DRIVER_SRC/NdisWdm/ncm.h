#ifndef _NCM_H
#define _NCM_H
#include "cdc.h"

#define CDC_NCM_DATA_ALTSETTING_NCM		1

/* CDC NCM subclass 3.2.1 */
#define USB_CDC_NCM_NDP16_LENGTH_MIN		0x10

/* Maximum NTB length */
#define	CDC_NCM_NTB_MAX_SIZE_TX			32768	/* bytes */
#define	CDC_NCM_NTB_MAX_SIZE_RX			32768	/* bytes */

/* Minimum value for MaxDatagramSize, ch. 6.2.9 */
#define	CDC_NCM_MIN_DATAGRAM_SIZE		1514	/* bytes */

/* Minimum value for MaxDatagramSize, ch. 8.1.3 */
#define CDC_MBIM_MIN_DATAGRAM_SIZE		2048	/* bytes */

#define	CDC_NCM_MIN_TX_PKT			512	/* bytes */

/* Default value for MaxDatagramSize */
#define	CDC_NCM_MAX_DATAGRAM_SIZE		8192	/* bytes */

/*
 * Maximum amount of datagrams in NCM Datagram Pointer Table, not counting
 * the last NULL entry.
 */
#define	CDC_NCM_DPT_DATAGRAMS_MAX		40

/* The following macro defines the minimum header space */
#define	CDC_NCM_MIN_HDR_SIZE \
	(sizeof(USB_CDC_NCM_NTH16) + sizeof( USB_CDC_NCM_NDP16) + \
	(CDC_NCM_DPT_DATAGRAMS_MAX ) * sizeof( USB_CDC_NCM_DPE16))

#define CDC_NCM_NDP_SIZE \
	(sizeof( USB_CDC_NCM_NDP16) +				\
	(CDC_NCM_DPT_DATAGRAMS_MAX) * sizeof( USB_CDC_NCM_DPE16))

#define CDC_NCM_COMM_INTF_IS_ECM(desc)  (desc->bInterfaceSubClass == USB_CDC_SUBCLASS_ETHERNET)

/* to trigger crc/non-crc ndp signature */

#define NCM_NDP_HDR_CRC_MASK	0x01000000
#define NCM_NDP_HDR_CRC		0x01000000
#define NCM_NDP_HDR_NOCRC	0x00000000

enum ncm_notify_state {
	NCM_NOTIFY_NONE,		/* don't notify */
	NCM_NOTIFY_CONNECT,		/* issue CONNECT next */
	NCM_NOTIFY_SPEED,		/* issue SPEED_CHANGE next */
};
/*-------------------------------------------------------------------------*/

/*
 * We cannot group frames so use just the minimal size which ok to put
 * one max-size ethernet frame.
 * If the host can group frames, allow it to do that, 16K is selected,
 * because it's used by default by the current linux host driver
 */
#define NTB_DEFAULT_IN_SIZE	USB_CDC_NCM_NTB_MIN_IN_SIZE
#define NTB_OUT_SIZE		16384

/*
 * skbs of size less than that will not be aligned
 * to NCM's dwNtbInMaxSize to save bus bandwidth
 */

#define	MAX_TX_NONFIXED		(512 * 3)

#define FORMATS_SUPPORTED	(USB_CDC_NCM_NTB16_SUPPORTED |	\
				 USB_CDC_NCM_NTB32_SUPPORTED)

#define ALIGN(x,a)    (((x)+(a)-1)&~(a-1))


#define USB_NCM_PACKET_TYPE_PROMISCUOUS     0x0001
#define USB_NCM_PACKET_TYPE_ALL_MULTICAST     0x0002
#define USB_NCM_PACKET_TYPE_DIRECTED             0x0004
#define USB_NCM_PACKET_TYPE_BROADCAST         0x0008
#define USB_NCM_PACKET_TYPE_MULTICAST           0x0010

USHORT   NdisPacketfilter2UsbPacketFilter(ULONG packetFilter);

#endif _NCM_H