/*===========================================================================
FILE: QCUSB.h

DESCRIPTION:
   This file contains definitions for USB communication.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government.
Diversion contrary to U.S. law prohibited.
===========================================================================*/

/*
 * Format of the USB control request header:
 *
 * URB Function Code
 * Flags Word   Indicates transfer direction and whether short transfer is OK.
 * Request   This is the request code that is interpreted by the device
 *              (e.g., GET_PORT_STATUS).
 * Value   This field is defined by the vendor.
 * Index   This field is defined by the vendor.
 * Length   Length of data to be sent (if any).
 * Data[]   Data
 */
#ifndef QCUSB_H
#define QCUSB_H

#pragma pack(push, 1)
typedef struct {
   UCHAR bmRequestType;
   UCHAR bRequest;
   USHORT wValue;
   USHORT wIndex;
   USHORT wLength;
   char Data[1];
}  USB_DEFAULT_PIPE_REQUEST, *PUSB_DEFAULT_PIPE_REQUEST;
#pragma pack(pop)

// constants that must be found in descriptors in Comm Device Class
// Configuration

// Call Management Functional Descriptor (Section 5.2.1.2) has bit D1 of
// bmCapabilities set i.e. "AT" commands sent over Data Class Interface,
// not encapsulated on Comm Class Interface

// fields in Interface Descriptors
#define CDCC_COMMUNICATION_INTERFACE_CLASS 0x02
#define CDCC_ABSTRACT_MODEL_SUBCLASS       0x02
#define CDCC_V25TER_PROTOCOL               0x01 // "AT" command set
#define CDCC_DATA_INTERFACE_CLASS          0x0a 
#define CDCC_OBEX_INTERFACE_CLASS          0x0b 

// Class Specific Functional Descriptor
#pragma pack(push, 1)
typedef struct _CDCC_FUNCTIONAL_DESCRIPTOR {
    UCHAR bFunctionLength;
    UCHAR bDescriptorType;   // == CDCC_CS_INTERFACE
    UCHAR bDescriptorSubtype;// == CDCC_CSUB_*
    UCHAR bmCapabilities; // CSUB_CALL_MANAGEMENT, CSUB_ABSTRACT_MANAGEMENT
    UCHAR bDataInterface; // CSUB_CALL_MANAGEMENT
} CDCC_FUNCTIONAL_DESCRIPTOR, *PCDCC_FUNCTIONAL_DESCRIPTOR;
#pragma pack(pop)

#define CDCC_CS_INTERFACE          0x24
#define CDCC_CSUB_HEADER           0x00
#define CDCC_CSUB_CALL_MANAGEMENT  0x01
#define CDCC_CSUB_ABSTRACT_CONTROL 0x02

// bmCapabilities (see CDC spec 5.2.1.3)
#define CDCC_ABSTRACT_FLAG_NETWORK_CONNECT      0x08
#define CDCC_ABSTRACT_FLAG_SET_BREAK            0x04
#define CDCC_ABSTRACT_FLAG_SET_LINE             0x02
#define CDCC_ABSTRACT_FLAG_SET_FEATURE          0x01
#define CDCC_ABSTRACT_FLAGS_REQUIRED (CDCC_ABSTRACT_FLAG_SET_BREAK | CDCC_ABSTRACT_FLAG_SET_LINE)

// bmCapabilities (see CDC spec 5.2.1.2)  
#define CDCC_CALLMGMT_FLAG_ENABLED              0x01
#define CDCC_CALLMGMT_FLAG_DATA_INTERFACE       0x02 // V25TER sent thru data pipe
#define CDCC_CALLMGMT_FLAGS_REQUIRED (CDCC_CALLMGMT_FLAG_ENABLED | CDCC_CALLMGMT_FLAG_DATA_INTERFACE)

#define CLASS_HOST_TO_INTERFACE	0x21
#define CLASS_INTERFACE_TO_HOST	0XA1

#define STANDARD_HOST_TO_DEVICE 0x00
#define STANDARD_DEVICE_TO_HOST 0x80

/*
 * Define USR Proprietary Legacy Mode control codes and bits.
 */

#define VENDOR_HOST_TO_DEVICE 0x40
#define VENDOR_DEVICE_TO_HOST 0XC0

// CDC (Communications Device Class) Vendor / Class Specific messages

 /*
 * Notification
 */
#define CDC_SERIAL_STATE                0x20 // (CDC spec 6.3.5)

#define CDC_SERIAL_ERR_MASK             0x70 // LIMIT: we're not handling error notifications
#define CDC_SERIAL_ERR_OVERRUN          0x40
#define CDC_SERIAL_ERR_PARITY           0x20
#define CDC_SERIAL_ERR_FRAMING          0x10

#define CDC_SERIAL_SIG_MASK             0x0F
#define CDC_SERIAL_SIG_RING             0x08
#define CDC_SERIAL_SIG_BREAK            0x04
#define CDC_SERIAL_SIG_DSR              0x02
#define CDC_SERIAL_SIG_DCD              0x01

 /*
 * bRequest field function codes...
 */
#define CDC_SEND_ENCAPSULATED_CMD       0x00
#define CDC_GET_ENCAPSULATED_RSP        0x01
#define CDC_SET_LINE_CODING             0x20
#define CDC_GET_LINE_CODING             0x21
#define CDC_SET_CONTROL_LINE_STATE      0x22
#define CDC_SEND_BREAK                  0x23
#define CDC_SET_COMM_FEATURE            0x02

 /*
 * wValue field definitions.
 */
#define CDC_CONTROL_LINE_RTS            0x02 // LIMIT: RTS always asserted
#define CDC_CONTROL_LINE_DTR            0x01
#define CDC_ABSTRACT_STATE              0x01
 /*
 * Index fields...
 */

/*
 * bRequest field function codes...
 */
#define QCSER_ENABLE_BYTE_STUFFING      0xFE
#define QCSER_DISABLE_BYTE_STUFFING     0xFF
#define QCSER_ENABLE_BYTE_PADDING       0x81

/*
 * Define the USB Request Types.
 */
#define BM_REQUEST_DIRECTION_BIT 0x80
#define bmRequestTypeBits        0x05  //D6..5
#define bmRequestRecipientMask   0x1F  //D4..0
    
#define TYPE_STANDARD             0
#define TYPE_CLASS                1
#define TYPE_VENDOR               2

#define RECIPIENT_DEVICE          0
#define RECIPIENT_INTERFACE       1
#define RECIPIENT_ENDPOINT        2
#define RECIPIENT_OTHER           3
#define USB_REQUEST_TYPE_STANDARD 0
#define USB_REQUEST_TYPE_CLASS     1
#define USB_REQUEST_TYPE_VENDOR     2

#define TRANSFER_DIRECTION_IN     1
#define TRANSFER_DIRECTION_OUT    2

/*
 * Define the default pipe IOCTL passthrough function codes.
 */

#define USB_REQUEST_VENDOR 13
#define USB_REQUEST_CLASS  14

typedef enum _QCUSB_RESET_SCOPE
{
   QCUSB_RESET_HOST_PIPE = 0,
   QCUSB_RESET_ENDPOINT,
   QCUSB_RESET_PIPE_AND_ENDPOINT
} QCUSB_RESET_SCOPE;

// USB 2.0 calls -- this needs fixing: usb.h
#ifndef URB_FUNCTION_SYNC_RESET_PIPE
#define URB_FUNCTION_SYNC_RESET_PIPE  0x0030
#endif
#ifndef URB_FUNCTION_SYNC_CLEAR_STALL
#define URB_FUNCTION_SYNC_CLEAR_STALL 0x0031
#endif

// Function Prototypes
NTSTATUS QCUSB_PassThrough
(
   PDEVICE_OBJECT pDevObj,
   PVOID ioBuffer,
   ULONG outputBufferLength,
   ULONG *pRetSize
);
NTSTATUS QCUSB_ResetInput(IN PDEVICE_OBJECT pDevObj, QCUSB_RESET_SCOPE Scope);
NTSTATUS QCUSB_ResetOutput(IN PDEVICE_OBJECT pDevObj, QCUSB_RESET_SCOPE Scope);
NTSTATUS QCUSB_ResetInt(IN PDEVICE_OBJECT pDevObj, QCUSB_RESET_SCOPE Scope);
NTSTATUS QCUSB_AbortInterrupt( IN PDEVICE_OBJECT pDevObj);
NTSTATUS QCUSB_AbortOutput( IN PDEVICE_OBJECT pDevObj);
NTSTATUS QCUSB_AbortInput( IN PDEVICE_OBJECT pDevObj );
NTSTATUS QCUSB_AbortPipe
(
   IN PDEVICE_OBJECT pDevObj,
   UCHAR             PipeNum,
   UCHAR             InterfaceNum
);
NTSTATUS QCUSB_GetStatus(IN PDEVICE_OBJECT pDevObj, PUCHAR ioBuff);
NTSTATUS QCUSB_CallUSBD_Completion
(
   PDEVICE_OBJECT dummy,
   PIRP pIrp,
   PVOID pEvent
);
NTSTATUS QCUSB_CallUSBD
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PURB Urb
);
NTSTATUS QCUSB_SetRemoteWakeup(IN PDEVICE_OBJECT pDevObj);
NTSTATUS QCUSB_ClearRemoteWakeup(IN PDEVICE_OBJECT pDevObj);
NTSTATUS QCUSB_ByteStuffing(PDEVICE_OBJECT pDevObj, BOOLEAN action);
NTSTATUS QCUSB_CDC_SendEncapsulatedCommand
(
   PDEVICE_OBJECT DeviceObject,
   USHORT Interface,
   PIRP   Irp
);
NTSTATUS QCUSB_CDC_GetEncapsulatedResponse
(
   PDEVICE_OBJECT DeviceObject,
   USHORT         Interface,
   PIRP           Irp
);

NTSTATUS QCUSB_ReadEncapsulatedDataToIrp
(
   PDEVICE_OBJECT DeviceObject,
   USHORT         Interface,
   PIRP           Irp
);

NTSTATUS QCUSB_ReadEncapsulatedDataToBuffer
(
   PDEVICE_OBJECT DeviceObject,
   USHORT         Interface,
   PVOID          Buffer,
   ULONG          Length
);

BOOLEAN QCUSB_RetryDevice(PDEVICE_OBJECT pDevObj, ULONG info);

NTSTATUS QCUSB_CDC_SetInterfaceIdle
(
   PDEVICE_OBJECT DeviceObject,
   USHORT         Interface,
   BOOLEAN        IdleState,
   UCHAR          Cookie
);

NTSTATUS QCUSB_EnableBytePadding(PDEVICE_OBJECT pDevObj);

#endif // QCUSB_H
