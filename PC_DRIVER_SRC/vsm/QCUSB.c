/*===========================================================================
FILE: QCUSB.c

DESCRIPTION:
   This file contains USB-related implementations.

INITIALIZATION AND SEQUENCING REQUIREMENTS:

Copyright (c) 2003-2007 QUALCOMM Inc. All Rights Reserved. QUALCOMM Proprietary
Export of this technology or software is regulated by the U.S. Government. 
Diversion contrary to U.S. law prohibited.
===========================================================================*/
#include "QCMAIN.h"
#include "QCPWR.h"
#include "QCUTILS.h"

/*********************************************************************
 *
 * function:   QCUSB_PassThrough
 *
 * purpose:    retrieve the max packet size
 *
 * arguments:  DeviceObject = adr(device object)
 *             ioBuffer     = adr(buffer for return data)
 *
 * returns:    NT status
 *
 * comments:
 *
 * Format of the USB control request header --
 *
 * This is taken from section 9.3 of the USB spec. It is an 8 byte packet of the
 * following format:
 *
 * UCHAR bmRequestType;   //bit map giving direction, type, and recipient of request
 * UCHAR bRequest;        //value to be passed through  to device
 * USHORT   wValue;       //depends on request
 * USHORT wIndex;         //depends on request
 * USHORT wLength;        //number of bytes to transfer if there is a data phase.
 * UCHAR bData[1];        //data to send to device if there is a data phase and 
 *                        //the transfer direction is "Host to device"
 * The encoding of the bitmap is defined by the following constants, whose definitions
 * are taken from the USB spec:
 *
 *
 *
 */

NTSTATUS QCUSB_PassThrough
( 
   PDEVICE_OBJECT pDevObj, 
   PVOID ioBuffer, 
   ULONG outputBufferLength, 
   ULONG *pRetSize
)
{

   #ifdef DEBUG_MSGS
   BOOLEAN bDbgoutSaved;
   #endif
   NTSTATUS nts;
   UCHAR bRequest;
   USHORT wRecipient, wType;
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   PURB pUrb = NULL;
   PDEVICE_EXTENSION pDevExt;
   ULONG size, DescSize, Rsize;
   USHORT UrbFunction;
   UCHAR UrbDescriptorType, *pData;
   PUSB_DEVICE_DESCRIPTOR deviceDesc = NULL;
   PUSB_ENDPOINT_DESCRIPTOR pEndPtDesc;
   PUSB_INTERFACE_DESCRIPTOR pIfDesc;
   USHORT FlagsWord;
   UCHAR bmRequestType;
 
#ifdef DEBUG_MSGS
   bDbgoutSaved = bDbgout;
   bDbgout = TRUE; // FALSE;
#endif

   pDevExt = pDevObj -> DeviceExtension;

   if (!inDevState(DEVICE_STATE_PRESENT))
   {
      return STATUS_DELETE_PENDING;
   }

   *pRetSize = 0;
   pRequest = (PUSB_DEFAULT_PIPE_REQUEST)ioBuffer;
   FlagsWord = USBD_SHORT_TRANSFER_OK;
   bmRequestType = pRequest -> bmRequestType;

   if ( bmRequestType & BM_REQUEST_DIRECTION_BIT )
   {
      FlagsWord |= USBD_TRANSFER_DIRECTION_IN;
   }

   wType = (bmRequestType >> bmRequestTypeBits)&3;
   wRecipient = bmRequestType&bmRequestRecipientMask;
   /*
   * We now have all the information we need to select and build the correct URBs.
   */
   pData = (PUCHAR)&pRequest -> Data[0];
   bRequest = pRequest -> bRequest;

   switch ( wType )
   {
      case USB_REQUEST_TYPE_STANDARD:  
      {

         switch (bRequest) 
         {
            case USB_REQUEST_GET_DESCRIPTOR: //bRequest
            {
               switch( wRecipient ) 
               {
                  case RECIPIENT_DEVICE:
                  {
                     UrbFunction = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
                     UrbDescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
                     DescSize = sizeof(USB_DEVICE_DESCRIPTOR);
                     break;
                  }
                  case RECIPIENT_ENDPOINT:
                  {             
                      UrbFunction = URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT;
                      UrbDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE;
                      DescSize = sizeof(USB_ENDPOINT_DESCRIPTOR);
                      break;
                  }
                  case RECIPIENT_INTERFACE:
                  {
#ifndef OSR21_COMPAT
                      UrbFunction = URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE;
                      UrbDescriptorType = USB_INTERFACE_DESCRIPTOR_TYPE;
                      DescSize = sizeof(USB_INTERFACE_DESCRIPTOR);
#endif //OSR21_COMPAT
                      break;
                  }
                  default:
                  {
                      nts = STATUS_INVALID_PARAMETER;
                      goto func_return;
                  }
               }

               size = sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST);

               pUrb = ExAllocatePool(
                   NonPagedPool, 
                   size);

               if ( !pUrb ) 
               {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
               }

               if (DescSize > outputBufferLength)
               {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
               }

               deviceDesc = ExAllocatePool(
                   NonPagedPool, 
                   DescSize);

               if ( !deviceDesc )
               {
                  nts = STATUS_NO_MEMORY;
                  goto func_return;
               }

               pUrb -> UrbHeader.Function =  UrbFunction;
               pUrb -> UrbHeader.Length = (USHORT) sizeof (struct _URB_CONTROL_DESCRIPTOR_REQUEST);
               pUrb -> UrbControlDescriptorRequest.TransferBufferLength = size;
               pUrb -> UrbControlDescriptorRequest.TransferBufferMDL = NULL;
               pUrb -> UrbControlDescriptorRequest.TransferBuffer = deviceDesc;
               pUrb -> UrbControlDescriptorRequest.DescriptorType = UrbDescriptorType;
               pUrb -> UrbControlDescriptorRequest.Index = 0;
               pUrb -> UrbControlDescriptorRequest.LanguageId = 0;
               pUrb -> UrbControlDescriptorRequest.UrbLink = NULL;

               nts = QCUSB_CallUSBD( pDevObj, pUrb );

               if( !NT_SUCCESS( nts ) )
               {
                   goto func_return;
               }
                // copy the device descriptor to the output buffer

               RtlCopyMemory( ioBuffer, deviceDesc, deviceDesc -> bLength );
               *pRetSize = deviceDesc -> bLength;
       
            
               break;
            } //USB_REQUEST_GET_DESCRIPTOR: //bRequest       
            case USB_REQUEST_SET_DESCRIPTOR: // bRequest
            {
                switch( wRecipient ) 
                {
                  case RECIPIENT_DEVICE:
                  {
                      UrbFunction = URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE;
                      UrbDescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
                      DescSize = sizeof(USB_DEVICE_DESCRIPTOR);
                      break;
                  }
                  case RECIPIENT_INTERFACE:
                  {
#ifndef OSR21_COMPAT
                      UrbFunction = URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE;
                      UrbDescriptorType = USB_INTERFACE_DESCRIPTOR_TYPE;
                      DescSize = sizeof(USB_INTERFACE_DESCRIPTOR);
#endif //OSR21_COMPAT        
                       break;
                  }
                  case RECIPIENT_ENDPOINT:
                  {
                      UrbFunction = URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT;
                      UrbDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE;
                      DescSize = sizeof(USB_ENDPOINT_DESCRIPTOR);
                      break;
                  }
                  default:   
                  {
                      nts = STATUS_INVALID_PARAMETER;
                      goto func_return;
                  }
               }

               size = sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST);

               pUrb = ExAllocatePool(
                   NonPagedPool, 
                   size);

               if (!pUrb) 
               {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
               }

               if (DescSize > outputBufferLength)
               {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
               }

               deviceDesc = ExAllocatePool(
                   NonPagedPool, 
                   DescSize);

               if ( !deviceDesc )
               {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
               }

               RtlCopyMemory( deviceDesc, pData, size );

               pUrb -> UrbHeader.Function =  URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE;
               pUrb -> UrbHeader.Length = (USHORT) sizeof (struct _URB_CONTROL_DESCRIPTOR_REQUEST);
               pUrb -> UrbControlDescriptorRequest.TransferBufferLength = size;
               pUrb -> UrbControlDescriptorRequest.TransferBufferMDL = NULL;
               pUrb -> UrbControlDescriptorRequest.TransferBuffer = deviceDesc;
               pUrb -> UrbControlDescriptorRequest.DescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
               pUrb -> UrbControlDescriptorRequest.Index = 0;
               pUrb -> UrbControlDescriptorRequest.LanguageId = 0;
               pUrb -> UrbControlDescriptorRequest.UrbLink = NULL;
               nts = QCUSB_CallUSBD( pDevObj, pUrb );
               break;
            } //USB_REQUEST_SET_DESCRIPTOR //bRequest
          
            case USB_REQUEST_SET_FEATURE: //bRequest
            {
                /*
                  USB defined Feature selectors
                  USB_FEATURE_ENDPOINT_STALL
                  USB_FEATURE_REMOTE_WAKEUP
                  USB_FEATURE_POWER_D0
                  USB_FEATURE_POWER_D1
                  USB_FEATURE_POWER_D2
                  USB_FEATURE_POWER_D3
                */

                switch( wRecipient ) 
                {
                  case RECIPIENT_DEVICE:
                  {
                      UrbFunction = URB_FUNCTION_SET_FEATURE_TO_DEVICE;
                      break;
                  }
                  case RECIPIENT_INTERFACE:
                  {
                      UrbFunction = URB_FUNCTION_SET_FEATURE_TO_INTERFACE;
                      break;
                  }
                  case RECIPIENT_ENDPOINT:
                  {
                      UrbFunction = URB_FUNCTION_SET_FEATURE_TO_ENDPOINT;
                      break;
                  }
                  default:
                  {
                      nts = STATUS_INVALID_PARAMETER;
                      goto func_return;
                  }
                }
               
                size = sizeof( struct _URB_CONTROL_FEATURE_REQUEST );

                pUrb = ExAllocatePool( 
                   NonPagedPool, 
                   size );

                if (!pUrb)
                {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
                }

                UsbBuildFeatureRequest(
                   pUrb,
                   UrbFunction,
                   pRequest -> wValue,   //feature selector, as passed in by caller
                   pRequest -> wIndex,
                   (struct _URB *)NULL);

                nts = QCUSB_CallUSBD( pDevObj, pUrb );     

                break;
            }
            case USB_REQUEST_CLEAR_FEATURE: //bRequest
            {
                switch( wRecipient ) 
                {
                  case RECIPIENT_DEVICE:
                  {
                      UrbFunction = URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE;
                      break;
                  }
                  case RECIPIENT_INTERFACE:
                  {
                      UrbFunction = URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE;
                      break;
                  }
                  case RECIPIENT_ENDPOINT:
                  {
                      UrbFunction = URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT;
                      break;
                  }
                  default:
                  {
                      nts = STATUS_INVALID_PARAMETER;
                      goto func_return;
                  }
                }

                size = sizeof( struct _URB_CONTROL_FEATURE_REQUEST );

                pUrb = ExAllocatePool( 
                   NonPagedPool, 
                   size );
             
                if (!pUrb)
                {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
                }

                UsbBuildFeatureRequest(
                      pUrb,
                      UrbFunction,
                      pRequest -> wValue,      //feature selector, as passed in by caller
                      pRequest -> wIndex,
                      (struct _URB *)NULL);
             
                nts = QCUSB_CallUSBD( pDevObj, pUrb );

                break;
            }

            // Configuration request
            case USB_REQUEST_GET_CONFIGURATION: //bRequest
            {
               switch( wRecipient ) 
               {
                  case RECIPIENT_DEVICE:
                  {
                     UrbFunction = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
                     UrbDescriptorType = USB_CONFIGURATION_DESCRIPTOR_TYPE;
                     DescSize = sizeof(USB_CONFIGURATION_DESCRIPTOR);
                     break;
                  }
                  default:
                  {
                      nts = STATUS_INVALID_PARAMETER;
                      goto func_return;
                  }
               }

               size = sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST);

               pUrb = ExAllocatePool(
                   NonPagedPool, 
                   size);

               if ( !pUrb ) 
               {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
               }

               if (DescSize > outputBufferLength)
               {
                   nts = STATUS_NO_MEMORY;
                   goto func_return;
               }

               deviceDesc = ExAllocatePool(
                   NonPagedPool, 
                   DescSize);

               if ( !deviceDesc )
               {
                  nts = STATUS_NO_MEMORY;
                  goto func_return;
               }

               pUrb -> UrbHeader.Function =  UrbFunction;
               pUrb -> UrbHeader.Length = (USHORT) sizeof (struct _URB_CONTROL_DESCRIPTOR_REQUEST);
               pUrb -> UrbControlDescriptorRequest.TransferBufferLength = size;
               pUrb -> UrbControlDescriptorRequest.TransferBufferMDL = NULL;
               pUrb -> UrbControlDescriptorRequest.TransferBuffer = deviceDesc;
               pUrb -> UrbControlDescriptorRequest.DescriptorType = UrbDescriptorType;
               pUrb -> UrbControlDescriptorRequest.Index = 0;
               pUrb -> UrbControlDescriptorRequest.LanguageId = 0;
               pUrb -> UrbControlDescriptorRequest.UrbLink = NULL;

               nts = QCUSB_CallUSBD( pDevObj, pUrb );

               if( !NT_SUCCESS( nts ) )
               {
                   goto func_return;
               }
                // copy the device descriptor to the output buffer

               RtlCopyMemory( ioBuffer, deviceDesc, deviceDesc -> bLength );
               *pRetSize = deviceDesc -> bLength;
       
            
               break;
            } // USB_REQUEST_GET_CONFIGURATION: //bRequest       

            case USB_REQUEST_SET_CONFIGURATION: //bRequest
            {
                nts = STATUS_INVALID_PARAMETER;
                goto func_return;
            }

            default: //bRequest
            {
                nts = STATUS_INVALID_PARAMETER;
                goto func_return;
            } 
         } //switch (bRequest) 


         break;
      } //wType = USB_REQUEST_TYPE_STANDARD
      case USB_REQUEST_TYPE_VENDOR: //wType
      {
         /* URB Function Code
      * Flags Word   Indicates transfer direction and whether short transfer is OK.
      * Request   This is the request code that is interpreted by the device (e.g., GET_PORT_STATUS).
      * Value   This field is defined by the vendor.
      * Index   This field is defined by the vendor.
      * BufSize   Length of data to be sent (if any).
      * Data[]   Data
      */
         switch( wRecipient ) 
         {
            case RECIPIENT_DEVICE:
            {
               UrbFunction = URB_FUNCTION_VENDOR_DEVICE;
               break;
            }
            case RECIPIENT_INTERFACE:
            {
               UrbFunction = URB_FUNCTION_VENDOR_INTERFACE;
               break;
            }
            case RECIPIENT_ENDPOINT:
            {
               UrbFunction = URB_FUNCTION_VENDOR_ENDPOINT;
               break;
            }
            default:
            {
               nts = STATUS_INVALID_PARAMETER;
               goto func_return;
            }
         }

         size = sizeof( struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST );

         pUrb = ExAllocatePool( 
               NonPagedPool, 
               size );
            
         if ( !pUrb )
         {
            nts =  STATUS_NO_MEMORY;
            goto func_return;
         }

         RtlZeroMemory( pUrb, size );

         UsbBuildVendorRequest (
               pUrb,
               UrbFunction,
               (USHORT)size,         //size of this URB
               FlagsWord,
               0,            //reserved bits
               pRequest -> bRequest,      //request
               pRequest -> wValue,      //value
               pRequest -> wIndex,      //index (zero interface?)
               pData,            //transfer buffer adr
               NULL,            //mdl adr
               pRequest -> wLength,      //size of transfer buffer
               NULL            //URB link adr
              );

         nts = QCUSB_CallUSBD( pDevObj, pUrb );

         if (NT_SUCCESS( nts ))
         {
            *pRetSize = pUrb -> UrbControlVendorClassRequest.TransferBufferLength;
         } else
         {
            *pRetSize = 0;
         }
 
         break;
      }
      
      case USB_REQUEST_TYPE_CLASS: //wType
      {
         switch( wRecipient ) 
         {
            case RECIPIENT_DEVICE:
            {
               UrbFunction = URB_FUNCTION_CLASS_DEVICE;
               break;
            }
            case RECIPIENT_INTERFACE:
            {
               UrbFunction = URB_FUNCTION_CLASS_INTERFACE;
               break;
            }
            case RECIPIENT_ENDPOINT:
            {
               UrbFunction = URB_FUNCTION_CLASS_ENDPOINT;
               break;
            }
            default:        
            {
               nts = STATUS_INVALID_PARAMETER;
               goto func_return;
            }
         }

         size = sizeof( struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST );

         pUrb = ExAllocatePool( NonPagedPool, size );
            
         if ( !pUrb )
         {
            nts =  STATUS_NO_MEMORY;
            goto func_return;
         }

         RtlZeroMemory( pUrb, size );      

         UsbBuildVendorRequest (
               pUrb,
               UrbFunction,
               (USHORT)size,         //size of this URB
               FlagsWord,
               0,            //reserved bits
               pRequest -> bRequest,      //request
               (USHORT)pRequest -> wValue,   //value
               (USHORT)pRequest -> wIndex,   //index (zero interface?)
               pData,            //transfer buffer adr
               NULL,            //mdl adr
               pRequest -> wLength,      //size of transfer buffer
               (struct _URB *)NULL      //URB link adr
              );

         nts = QCUSB_CallUSBD( pDevObj, pUrb );

         if (NT_SUCCESS( nts ))
         {
            *pRetSize = pUrb -> UrbControlVendorClassRequest.TransferBufferLength;
         } else
         {
            *pRetSize = 0;
         }

         break;
      }
      default:
      {
         nts = STATUS_INVALID_PARAMETER;
         goto func_return;
      }
   }

func_return:

   _freeBuf(pUrb);
   _freeBuf(deviceDesc);

#ifdef DEBUG_MSGS
   bDbgout = bDbgoutSaved;
#endif

   return nts;
}  // QCUSB_PassThrough

/*********************************************************************
 *
 * function:   QCUSB_ResetInput
 *
 * purpose:    Reset the current input pipe
 *
 * arguments:  pDevObj = adr(device object)
 *
 * returns:    NT status
 *
 */
/******************************************************************************

Description:
   This function is invoked by a calling app via IO_CONTROL. A URB pipe request
   URB is built and sent synchronously to the device.
      
******************************************************************************/      
NTSTATUS QCUSB_ResetInput(IN PDEVICE_OBJECT pDevObj, IN QCUSB_RESET_SCOPE Scope)
{
    NTSTATUS status;
    static USHORT mdmCnt = 0, serCnt = 0;
    USHORT errCnt;
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;
    PURB urb;
    ULONG size = sizeof( struct _URB_PIPE_REQUEST );
    int i;
    #define QC_RESET_RETRIES 3

   if (pDevExt->ucDeviceType >= DEVICETYPE_CTRL)
   {
      return STATUS_SUCCESS;
   }

   if (!inDevState(DEVICE_STATE_PRESENT_AND_INIT))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> USB: not init-ResetIn\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> -->ResetInput: scope %u\n", pDevExt->PortName, Scope)
   );

   urb = ExAllocatePool( NonPagedPool, size );
   if (urb == NULL)
   {
      return STATUS_NO_MEMORY;
   }
   urb->UrbHeader.Length = (USHORT) size;

   #if defined(QCSER_VERSION_WXP_FRE) || defined(QCSER_VERSION_WXP_CHK)
   if (Scope == QCUSB_RESET_HOST_PIPE)
   {
      urb->UrbHeader.Function = URB_FUNCTION_SYNC_RESET_PIPE;
   }
   else if (Scope == QCUSB_RESET_ENDPOINT)
   {
      urb->UrbHeader.Function = URB_FUNCTION_SYNC_CLEAR_STALL;
   }
   else
   #endif
   {
      urb->UrbHeader.Function = URB_FUNCTION_RESET_PIPE;
   }

   for (i = 0; i < QC_RESET_RETRIES; i++)
   {
      urb->UrbPipeRequest.PipeHandle = pDevExt->Interface[pDevExt->DataInterface] 
             ->Pipes[pDevExt->BulkPipeInput].PipeHandle;

      status = QCUSB_CallUSBD( pDevObj, urb ); 
      if (!NT_SUCCESS(status))
      {
         if (pDevExt->ucDeviceType == DEVICETYPE_CDC)
         {
            errCnt = ++mdmCnt;
         }
         else if (pDevExt->ucDeviceType == DEVICETYPE_SERIAL)
         {
            errCnt = ++serCnt;
         }
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> ResetIN(%d) - 0x%x IF %u pipe %u Hdl 0x%p\n", pDevExt->PortName, errCnt, status,
              pDevExt->DataInterface, pDevExt->BulkPipeInput, urb->UrbPipeRequest.PipeHandle)
         );
         QCSER_Wait(pDevExt, -(50 * 1000L));  // 5ms
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_READ,
            QCSER_DBG_LEVEL_TRACE,
            ("<%s> ResetIN - IF %u pipe %u Hdl 0x%p\n", pDevExt->PortName,
              pDevExt->DataInterface, pDevExt->BulkPipeInput, urb->UrbPipeRequest.PipeHandle)
         );
         break;
      }
   }
   ExFreePool( urb );

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> <--ResetInput: scope %u ST 0x%x\n", pDevExt->PortName, Scope, status)
   );

   return status;
}

NTSTATUS QCUSB_ResetInt(IN PDEVICE_OBJECT pDevObj, IN QCUSB_RESET_SCOPE Scope)
{
   ULONG size;
   PURB pUrb;
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS nts = STATUS_SUCCESS;
 
   pDevExt = pDevObj->DeviceExtension;
   if (pDevExt->InterruptPipe == (UCHAR)-1)
   {
      // if there's no interrupt pipe presented
      return nts;
   }

   if (!inDevState(DEVICE_STATE_PRESENT_AND_INIT))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> USB: not init-ResetInt\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> -->ResetInt: scope %u\n", pDevExt->PortName, Scope)
   );

   size = sizeof( struct _URB_PIPE_REQUEST );
   //edk size = sizeof( struct _URB_PIPE_REQUEST ) + 64;
   if ((pUrb = ExAllocatePool( NonPagedPool, size )) == NULL)
   {
      nts = STATUS_NO_MEMORY;
   }
   else
   {
      pUrb -> UrbPipeRequest.PipeHandle = pDevExt -> Interface[pDevExt->usCommClassInterface]
         -> Pipes[pDevExt -> InterruptPipe].PipeHandle;
      pUrb -> UrbPipeRequest.Hdr.Length = (USHORT)size;

      #if defined(QCSER_VERSION_WXP_FRE) || defined(QCSER_VERSION_WXP_CHK)
      if (Scope == QCUSB_RESET_HOST_PIPE)
      {
         pUrb->UrbPipeRequest.Hdr.Function = URB_FUNCTION_SYNC_RESET_PIPE;
      }
      else if (Scope == QCUSB_RESET_ENDPOINT)
      {
         pUrb->UrbPipeRequest.Hdr.Function = URB_FUNCTION_SYNC_CLEAR_STALL;
      }
      else
      #endif
      {
         pUrb->UrbPipeRequest.Hdr.Function = URB_FUNCTION_RESET_PIPE;
      }
      nts = QCUSB_CallUSBD( pDevObj, pUrb ); 
      ExFreePool( pUrb );
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> <--ResetInt: scope %u ST 0x%x\n", pDevExt->PortName, Scope, nts)
   );
   return nts;
}


/*********************************************************************
 *
 * function:   QCUSB_AbortInterrupt
 *
 * purpose:    abort operations on the current interrupt pipe
 *
 * arguments:  pDevObj = adr(device object)
 *
 * returns:    NT status
 *
 */
NTSTATUS QCUSB_AbortInterrupt( IN PDEVICE_OBJECT pDevObj)
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   KIRQL IrqLevel;

   pDevExt = pDevObj->DeviceExtension;
   if (pDevExt->InterruptPipe == (UCHAR)-1)
   {
      return ntStatus;
   }
   ntStatus = QCUSB_AbortPipe
              (
                 pDevObj,
                 pDevExt->InterruptPipe,
                 pDevExt->usCommClassInterface
              );
   return ntStatus; 
}
/*********************************************************************
 *
 * function:   QCUSB_AbortOutput
 *
 * purpose:    abort operations on the current output pipe
 *
 * arguments:  pDevObj = adr(device object)
 *
 * returns:    NT status
 *
 */
NTSTATUS QCUSB_AbortOutput( IN PDEVICE_OBJECT pDevObj)
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   KIRQL IrqLevel;

   pDevExt = pDevObj -> DeviceExtension;
   if (pDevExt->BulkPipeOutput == (UCHAR)-1)
   {
      return ntStatus;
   }
   ntStatus = QCUSB_AbortPipe
              (
                 pDevObj,
                 pDevExt->BulkPipeOutput,
                 pDevExt->DataInterface
              );
   return ntStatus; 
}
/*********************************************************************
 *
 * function:   QCUSB_AbortInput
 *
 * purpose:    abort operations on the current input pipe
 *
 * arguments:  pDevObj = adr(device object)
 *
 * returns:    NT status
 *
 */
NTSTATUS QCUSB_AbortInput( IN PDEVICE_OBJECT pDevObj )
{
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS ntStatus = STATUS_SUCCESS;
   KIRQL IrqLevel;

   pDevExt = pDevObj -> DeviceExtension;
   if (pDevExt->BulkPipeInput == (UCHAR)-1)
   {
      return ntStatus;
   }
   ntStatus = QCUSB_AbortPipe
              (
                 pDevObj,
                 pDevExt->BulkPipeInput,
                 pDevExt->DataInterface
              );
   return ntStatus; 
}

NTSTATUS QCUSB_AbortPipe
(
   IN PDEVICE_OBJECT pDevObj,
   UCHAR             PipeNum,
   UCHAR             InterfaceNum
)
{
   PDEVICE_EXTENSION pDevExt;
   ULONG size;
   PURB pUrb;
   NTSTATUS nts = STATUS_SUCCESS;
   KIRQL IrqLevel;
   UCHAR EndpointAddress;
 
   pDevExt = pDevObj->DeviceExtension;

   if (PipeNum == (UCHAR)-1)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> USB_AbortPp: unnecessary\n", pDevExt->PortName)
      );
      return nts;
   }

   if (!inDevState(DEVICE_STATE_PRESENT_AND_INIT))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> USB: not init-AbortPp\n", pDevExt->PortName)
      );
      nts = STATUS_UNSUCCESSFUL;
      return nts;
   }

   size = sizeof( struct _URB_PIPE_REQUEST );
   if ((pUrb = ExAllocatePool( NonPagedPool, size )) == NULL)
   {
      nts = STATUS_NO_MEMORY;
   }
   else
   {
      pUrb->UrbPipeRequest.PipeHandle = pDevExt->Interface[InterfaceNum]
         ->Pipes[PipeNum].PipeHandle;
      pUrb -> UrbPipeRequest.Hdr.Length = (USHORT)size;
      pUrb -> UrbHeader.Length = (USHORT)size;
      pUrb -> UrbHeader.Function = URB_FUNCTION_ABORT_PIPE; 
      nts = QCUSB_CallUSBD( pDevObj, pUrb ); 
      ExFreePool( pUrb );
   }
   return nts;
}

/*********************************************************************
 *
 * function:   QCUSB_ResetOutput
 *
 * purpose:    abort operations on the current output pipe
 *
 * arguments:  pDevObj = adr(device object)
 *
 * returns:    NT status
 *
 */
/******************************************************************************

Description:
   This function is invoked by a calling app via IO_CONTROL. A URB pipe request
   URB is built and sent synchronously to the device.
      
******************************************************************************/      

NTSTATUS QCUSB_ResetOutput(IN PDEVICE_OBJECT pDevObj, IN QCUSB_RESET_SCOPE Scope)
{
   ULONG size;
   PURB pUrb;
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS nts;
 
   pDevExt = pDevObj -> DeviceExtension;

   if (pDevExt->ucDeviceType >= DEVICETYPE_CTRL)
   {
      return STATUS_SUCCESS;
   }

   if (!inDevState(DEVICE_STATE_PRESENT_AND_INIT))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> USB: not init-ResetOut\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> -->ResetOutput: scope %u\n", pDevExt->PortName, Scope)
   );

   size = sizeof( struct _URB_PIPE_REQUEST );

   if ((pUrb = ExAllocatePool( NonPagedPool, size )) == NULL)
   {
      nts = STATUS_NO_MEMORY;
   }
   else
    {
      pUrb->UrbPipeRequest.PipeHandle = pDevExt->Interface[pDevExt->DataInterface]
          ->Pipes[pDevExt->BulkPipeOutput].PipeHandle;
      pUrb -> UrbPipeRequest.Hdr.Length = (USHORT)size;

      #if defined(QCSER_VERSION_WXP_FRE) || defined(QCSER_VERSION_WXP_CHK)
      if (Scope == QCUSB_RESET_HOST_PIPE)
      {
         pUrb->UrbPipeRequest.Hdr.Function = URB_FUNCTION_SYNC_RESET_PIPE;
      }
      else if (Scope == QCUSB_RESET_ENDPOINT)
      {
         pUrb->UrbPipeRequest.Hdr.Function = URB_FUNCTION_SYNC_CLEAR_STALL;
      }
      else
      #endif
      {
         pUrb->UrbPipeRequest.Hdr.Function = URB_FUNCTION_RESET_PIPE;
      }
      nts = QCUSB_CallUSBD( pDevObj, pUrb ); 
      ExFreePool( pUrb );
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_TRACE,
      ("<%s> <--ResetOutput: scope %u ST 0x%x\n", pDevExt->PortName, Scope, nts)
   );

   return nts;
}

/*****************************************************************************
 *
 * function:   QCUSB_GetStatus
 *
 * purpose:    get status from device
 *
 * arguments:  pDevObj   = adr(device object)
 *             ioBuffer = adr(data return buffer) 
 *
 * returns:    NT status
 *
 */
/******************************************************************************
Description:
   This function is invoked by a calling app via IO_CONTROL. A Get status
   URB is built and sent synchronously to the device, and the results are
   returned to the buffer adr passed in by the (ring 3) caller.
      
******************************************************************************/      

NTSTATUS QCUSB_GetStatus( IN PDEVICE_OBJECT pDevObj, PUCHAR ioBuff )

{
   ULONG size, *pUl;
   PURB pUrb;
   PDEVICE_EXTENSION pDevExt;
   NTSTATUS nts;
 
   pUl = (PULONG)ioBuff;
   *pUl = 0L;      //clear the buffer
   pDevExt = pDevObj -> DeviceExtension;
   size = sizeof( struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST );
   if ((pUrb = ExAllocatePool( NonPagedPool, size )) == NULL)
      nts = STATUS_NO_MEMORY;
   else
    {
      UsbBuildVendorRequest (
               pUrb,
               URB_FUNCTION_CLASS_INTERFACE,
               (USHORT)size,      //size of this URB
               USBD_TRANSFER_DIRECTION_IN|USBD_SHORT_TRANSFER_OK,
               0,            //reserved bits
               GET_PORT_STATUS,      //request
               0,            //value
               0,            //index (zero interface?)
               ioBuff,         //transfer buffer adr
               NULL,         //mdl adr
               sizeof( ULONG ),      //size of transfer buffer
               NULL         //URB link adr
               )
      nts = QCUSB_CallUSBD( pDevObj, pUrb ); 
      ExFreePool( pUrb );
   }

   return nts;
}

NTSTATUS QCUSB_CallUSBD_Completion
(
   PDEVICE_OBJECT dummy,
   PIRP pIrp,
   PVOID pEvent
)
{
   KeSetEvent((PKEVENT)pEvent, IO_NO_INCREMENT, FALSE);
   return STATUS_MORE_PROCESSING_REQUIRED;
}

/*****************************************************************************
 *
 * function:   QCUSB_CallUSBD
 *
 * purpose:    Passes a URB to the USBD class driver
 *
 * arguments:  DeviceObject = adr(device object)
 *             Urb       = adr(USB Request Block)
 *
 * returns:    NT status
 *
 */
NTSTATUS QCUSB_CallUSBD
(
   IN PDEVICE_OBJECT DeviceObject,
   IN PURB Urb
)
{
   NTSTATUS ntStatus, ntStatus2;
   PDEVICE_EXTENSION pDevExt;
   PIRP irp;
   KEVENT event;
   IO_STATUS_BLOCK ioStatus;
   PIO_STACK_LOCATION nextStack;
   LARGE_INTEGER delayValue;
   KIRQL IrqLevel;

   // the device should respond within 50ms over the control pipe
   // so it's good enough to use 1.5 second here

   IrqLevel = KeGetCurrentIrql();

   if (IrqLevel > PASSIVE_LEVEL)
   {
      Urb->UrbHeader.Status = USBD_STATUS_CANCELED;
      return STATUS_CANCELLED;
   }

   pDevExt = DeviceObject->DeviceExtension;

   if (!inDevState(DEVICE_STATE_PRESENT))
   {
      return STATUS_DELETE_PENDING;
   }

   if ((pDevExt->bDeviceSurpriseRemoved == TRUE) ||
       (pDevExt->bDeviceRemoved == TRUE))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> CallUSBD: STATUS_DELETE_PENDING\n", pDevExt->PortName)
      );
      return STATUS_DELETE_PENDING;
   }

   // issue a synchronous request to read the UTB 
   KeInitializeEvent(&event, NotificationEvent, FALSE);

   irp = IoBuildDeviceIoControlRequest
         (
            IOCTL_INTERNAL_USB_SUBMIT_URB,
            pDevExt -> StackDeviceObject,
            NULL,
            0,
            NULL,
            0,
            TRUE, /* INTERNAL */
            &event,
            &ioStatus
         );

   if (!irp)
   {
      return STATUS_INSUFFICIENT_RESOURCES;
   }

   // Call the class driver to perform the operation.  If the returned status
   // is PENDING, wait for the request to complete.

   nextStack = IoGetNextIrpStackLocation( irp );
   nextStack->Parameters.Others.Argument1 = Urb;

   if (pDevExt->NoTimeoutOnCtlReq == TRUE)
   {
      ntStatus = IoCallDriver( pDevExt -> StackDeviceObject, irp );

      if (ntStatus == STATUS_PENDING)
      {
         ntStatus = KeWaitForSingleObject
                    (
                       &event,
                       Executive,
                       KernelMode,
                       FALSE,
                       NULL
                    );
      }

      // USBD maps the error code for us
      // ntStatus = URB_STATUS(Urb);
      ioStatus.Status = ntStatus;
      goto ExitCallUSBD;
   }

   // set completion routine to regain control of the irp
   IoSetCompletionRoutine
   (
      irp, QCUSB_CallUSBD_Completion, (PVOID)&event,
      TRUE, TRUE, TRUE
   );

   ntStatus = IoCallDriver( pDevExt -> StackDeviceObject, irp );

   if (ntStatus == STATUS_PENDING) 
   {
      delayValue.QuadPart = -(20 * 1000 * 1000);   // 2.0 sec  
      ntStatus = KeWaitForSingleObject
                 (
                    &event,
                    Executive,
                    KernelMode,
                    FALSE,
                    &delayValue // HGUO
                 );


      if (ntStatus == STATUS_TIMEOUT) 
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_ERROR,
            ("<%s> timeout on control req - 1\n", pDevExt->PortName)
         );
         IoCancelIrp(irp);  // cancel the irp
         KeWaitForSingleObject
         (
            &event,
            Executive,
            KernelMode,
            FALSE,
            NULL
         );

         Urb->UrbHeader.Status = USBD_STATUS_REQUEST_FAILED;  // HGUO
         // to complete the irp???
         // I don't think I want the above timeout with the Irp remaining
         // in the lower level driver. It's too dangerous to timeout.
         // So this case shouldn't happen.
      }
   }
   else // success or failrue
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> CallUSBD - IoCallDriver/0x%p 0x%x IRP 0x%x\n",
           pDevExt->PortName, pDevExt->StackDeviceObject, ntStatus,
           irp->IoStatus.Status)
      );
      delayValue.QuadPart = -(10 * 1000 * 1000);   // 1.0 sec
      ntStatus2 = KeWaitForSingleObject
                 (
                    &event,
                    Executive,
                    KernelMode,
                    FALSE,
                    &delayValue
                 );
      if (ntStatus2 == STATUS_TIMEOUT)
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_CRITICAL,
            ("<%s> CallUSBD ERR: 2nd wait timeout\n", pDevExt->PortName)
         );
      }
   }

   IoCompleteRequest(irp, IO_NO_INCREMENT);

ExitCallUSBD:

   if (ntStatus == STATUS_TIMEOUT)
   {
      ntStatus = STATUS_UNSUCCESSFUL;
   }

   QCPWR_SetIdleTimer(pDevExt, QCUSB_BUSY_CTRL, FALSE, 5); // end of CallUSBD

   return ntStatus;

}  // QCUSB_CallUSBD

NTSTATUS QCUSB_SetRemoteWakeup(IN PDEVICE_OBJECT pDevObj)
{
    NTSTATUS status;
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;
    PURB urb;
    ULONG size = sizeof( struct _URB_CONTROL_FEATURE_REQUEST );

    urb = ExAllocatePool( NonPagedPool, size );
    if (urb == NULL)
    {
       return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(urb, size);
    urb->UrbHeader.Length = (USHORT) size;
    urb->UrbHeader.Function = URB_FUNCTION_SET_FEATURE_TO_DEVICE;
    urb->UrbControlFeatureRequest.UrbLink = NULL;
    urb->UrbControlFeatureRequest.FeatureSelector = 1; // 1: remote wakeup; 0: endpoint halt; 2: test mode
    urb->UrbControlFeatureRequest.Index = 0;

    status = QCUSB_CallUSBD( pDevObj, urb );
    if (!NT_SUCCESS(status))
    {
       QCSER_DbgPrint
       (
          QCSER_DBG_MASK_CONTROL,
          QCSER_DBG_LEVEL_ERROR,
          ("<%s> SetRemoteWakeup - Err 0x%x\n", pDevExt->PortName, status)
       );
       status = STATUS_UNSUCCESSFUL;
    }
    else
    {
       pDevExt->bRemoteWakeupEnabled = TRUE;
    }
    ExFreePool( urb );

    return status;
}

NTSTATUS QCUSB_ClearRemoteWakeup(IN PDEVICE_OBJECT pDevObj)
{
    NTSTATUS status;
    PDEVICE_EXTENSION pDevExt = (PDEVICE_EXTENSION) pDevObj->DeviceExtension;
    PURB urb;
    ULONG size = sizeof( struct _URB_CONTROL_FEATURE_REQUEST );

    urb = ExAllocatePool( NonPagedPool, size );
    if (urb == NULL)
    {
       return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(urb, size);
    urb->UrbHeader.Length = (USHORT) size;
    urb->UrbHeader.Function = URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE;
    urb->UrbControlFeatureRequest.UrbLink = NULL;
    urb->UrbControlFeatureRequest.FeatureSelector = 1; // 1: remote wakeup; 0: endpoint halt; 2: test mode
    urb->UrbControlFeatureRequest.Index = 0;

    status = QCUSB_CallUSBD( pDevObj, urb );
    if (!NT_SUCCESS(status))
    {
       QCSER_DbgPrint
       (
          QCSER_DBG_MASK_CONTROL,
          QCSER_DBG_LEVEL_ERROR,
          ("<%s> ClearRemoteWakeup - Err 0x%x\n", pDevExt->PortName, status)
       );
    }
    ExFreePool( urb );
    return status;
}

// Vendor-specific request
NTSTATUS QCUSB_ByteStuffing(PDEVICE_OBJECT pDevObj, BOOLEAN action)
{
   UCHAR pBuf[sizeof(USB_DEFAULT_PIPE_REQUEST)+16];  // 1 byte request code
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   NTSTATUS nts = STATUS_SUCCESS;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt = pDevObj->DeviceExtension;

   if (pDevExt->bByteStuffingFeature == FALSE)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_INFO,
         ("<%s> ByteStuffing: feature not available in device.\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   RtlZeroMemory(pBuf, sizeof(USB_DEFAULT_PIPE_REQUEST)+16);

   pRequest = (PUSB_DEFAULT_PIPE_REQUEST)pBuf;

   // bmRequest:
   //    STANDARD_HOST_TO_DEVICE
   //    STANDARD_DEVICE_TO_HOST
   //    VENDOR_HOST_TO_DEVICE
   //    CLASS_HOST_TO_INTERFACE
   // bRequest:
   //    CDC_SEND_ENCAPSULATED_CMD
   //    USB_REQUEST_GET_CONFIGURATION
   //    USB_REQUEST_SET_CONFIGURATION
   // wValue:
   //    QCSER_ENABLE_BYTE_STUFFING  0x0E
   //    QCSER_DISABLE_BYTE_STUFFING 0x0F
   // wIndex:
   // wLength:
   // Data:

   pRequest->bmRequestType = VENDOR_DEVICE_TO_HOST;
   pRequest->bRequest      = QCSER_ENABLE_BYTE_STUFFING;
   pRequest->wValue        = 0;
   pRequest->wIndex        = 0;
   pRequest->wLength       = 4;

   if (action == FALSE)
   {
      pRequest->bRequest = QCSER_DISABLE_BYTE_STUFFING;
   }

   nts = QCUSB_PassThrough ( pDevObj, pBuf, 16, &ulRetSize );

   // The returned data should be in pRequest->Data[0] , [1], [2], ...

   if (nts == STATUS_SUCCESS)
   {
      if ((pRequest->Data[0] == 'B') &&
          (pRequest->Data[1] == 'T') &&
          (pRequest->Data[2] == 'S') &&
          (pRequest->Data[3] == 'T') &&
          (ulRetSize == 4))
      {
         pDevExt->bEnableByteStuffing = action;

         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_INFO,
            ("<%s> ByteStuffing is supported by the device.\n", pDevExt->PortName)
         );
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_INFO,
            ("<%s> ByteStuffing is NOT supported by the device.\n", pDevExt->PortName)
         );
      }
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> EnableByteStuffing status: 0x%x/0x%x\n", pDevExt->PortName,
        nts, pDevExt->bEnableByteStuffing)
   );

   return nts;
}  // QCUSB_ByteStuffing

NTSTATUS QCUSB_CDC_SendEncapsulatedCommand
(
   PDEVICE_OBJECT DeviceObject,
   USHORT Interface,
   PIRP   pIrp
)
{
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   NTSTATUS nts = STATUS_SUCCESS;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(pIrp);
   PVOID ioBuffer = pIrp->AssociatedIrp.SystemBuffer;
   ULONG Length = irpStack->Parameters.DeviceIoControl.InputBufferLength;
   PVOID pBuf;
   ULONG bufSize;

   bufSize = sizeof(USB_DEFAULT_PIPE_REQUEST)+Length;
   pBuf = ExAllocatePool(NonPagedPool, bufSize);
   if (pBuf == NULL)
   {
      return STATUS_NO_MEMORY;
   }
   RtlZeroMemory(pBuf, bufSize);

   pRequest = (PUSB_DEFAULT_PIPE_REQUEST)pBuf;
   pRequest->bmRequestType = CLASS_HOST_TO_INTERFACE;
   pRequest->bRequest = CDC_SEND_ENCAPSULATED_CMD;
   pRequest->wIndex = Interface;
   pRequest->wValue = 0;
   pRequest->wLength = Length;

   RtlCopyMemory(&pRequest->Data[0], ioBuffer, Length);

   nts = QCUSB_PassThrough
         (
            DeviceObject, pBuf, bufSize, &ulRetSize
         );

   ExFreePool(pBuf);

   return nts;
} // QCUSB_CDC_SendEncapsulatedCommand

NTSTATUS QCUSB_CDC_GetEncapsulatedResponse
(
   PDEVICE_OBJECT DeviceObject,
   USHORT         Interface,
   PIRP           pIrp
)
{
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   NTSTATUS nts = STATUS_SUCCESS;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(pIrp);
   PVOID ioBuffer = pIrp->AssociatedIrp.SystemBuffer;
   ULONG Length = irpStack->Parameters.DeviceIoControl.InputBufferLength;
   PVOID pBuf;
   ULONG bufSize;

   bufSize = sizeof(USB_DEFAULT_PIPE_REQUEST)+Length;
   pBuf = ExAllocatePool(NonPagedPool, bufSize);
   if (pBuf == NULL)
   {
      return STATUS_NO_MEMORY;
   }
   RtlZeroMemory(pBuf, bufSize);

   pRequest = (PUSB_DEFAULT_PIPE_REQUEST)pBuf;
   pRequest->bmRequestType = CLASS_INTERFACE_TO_HOST;
   pRequest->bRequest = CDC_GET_ENCAPSULATED_RSP;
   pRequest->wIndex = Interface;
   pRequest->wValue = 0;
   pRequest->wLength = Length;

   nts = QCUSB_PassThrough
         (
            DeviceObject, pBuf, bufSize, &ulRetSize
         );

   if (ulRetSize > Length)
   {
      // err
   }
   RtlCopyMemory(ioBuffer, &pRequest->Data[0], Length);
   ExFreePool(pBuf);

   return nts;

}  // QCUSB_CDC_GetEncapsulatedResponse

BOOLEAN QCUSB_RetryDevice(PDEVICE_OBJECT pDevObj, ULONG info)
{
   NTSTATUS status;
   PDEVICE_EXTENSION pDevExt = pDevObj->DeviceExtension;
   USB_DEFAULT_PIPE_REQUEST Request;
   PUSB_DEFAULT_PIPE_REQUEST pRequest = &Request;
   ULONG ulRetSize;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_FORCE,
      ("<%s> RetryDevice - %d\n", pDevExt->PortName, info)
   );
/*****
   // Set Configuration
   Request.bmRequestType = STANDARD_HOST_TO_DEVICE;
   Request.bRequest      = USB_REQUEST_SET_CONFIGURATION; // USB_SET_CONFIGURATION;
   Request.wValue        = 0x0001;
   Request.wIndex        = 0;
   Request.wLength       = 0;
   status = QCUSB_PassThrough( pDevObj, pRequest, 0, &ulRetSize );
   if (!NT_SUCCESS(status))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> RetryDevice[%d]: SetConfig err 0x%x\n", pDevExt->PortName, info, status)
      );
      return FALSE;
   }
*****/

   // Clear Feature to IN
   status = QCUSB_ResetInput(pDevObj, QCUSB_RESET_PIPE_AND_ENDPOINT);
   if (!NT_SUCCESS(status))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> RetryDevice[%d]: ResetInput err 0x%x\n", pDevExt->PortName, info, status)
      );
      return FALSE;
   }

   // Clear Feature to OUT
   status = QCUSB_ResetOutput(pDevObj, QCUSB_RESET_PIPE_AND_ENDPOINT);
   if (!NT_SUCCESS(status))
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> RetryDevice[%d]: ResetOutput err 0x%x\n", pDevExt->PortName, info, status)
      );
      return FALSE;
   }

   return TRUE;
}  // QCUSB_RetryDevice

NTSTATUS QCUSB_CDC_SetInterfaceIdle
(
   PDEVICE_OBJECT DeviceObject,
   USHORT         Interface,
   BOOLEAN        IdleState,
   UCHAR          Cookie
)
{
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   NTSTATUS nts = STATUS_SUCCESS;
   ULONG ulRetSize;
   PDEVICE_EXTENSION pDevExt = DeviceObject->DeviceExtension;
   ULONG Length = sizeof(USHORT);
   PVOID pBuf;
   ULONG bufSize;
   PUSHORT pCommStatus;

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> -->CDC_SetInterfaceIdle<%d>: IF %u Idle %d\n",
        pDevExt->PortName, Cookie, Interface, IdleState)
   );

   if (pDevExt->ucDeviceType != DEVICETYPE_CDC)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> <--CDC_SetInterfaceIdle<%d>: IF %u - not CDC interface\n",
           pDevExt->PortName, Cookie, Interface)
      );
      return nts;
   }

   if (pDevExt->SetCommFeatureSupported == FALSE)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_DETAIL,
         ("<%s> <--CDC_SetInterfaceIdle<%d>: IF %u - not supported\n",
           pDevExt->PortName, Cookie, Interface)
      );
      return nts;
   }

   bufSize = sizeof(USB_DEFAULT_PIPE_REQUEST)+Length;
   pBuf = ExAllocatePool(NonPagedPool, bufSize);
   if (pBuf == NULL)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_ERROR,
         ("<%s> <--CDC_SetInterfaceIdle: NO_MEM\n", pDevExt->PortName)
      );
      return STATUS_NO_MEMORY;
   }
   RtlZeroMemory(pBuf, bufSize);

   pRequest = (PUSB_DEFAULT_PIPE_REQUEST)pBuf;
   pRequest->bmRequestType = CLASS_HOST_TO_INTERFACE;
   pRequest->bRequest = CDC_SET_COMM_FEATURE;
   pRequest->wIndex = Interface;
   pRequest->wValue = CDC_ABSTRACT_STATE;
   pRequest->wLength = Length;
   pCommStatus = (PUSHORT)pRequest->Data;
   *pCommStatus = (IdleState == TRUE);

   nts = QCUSB_PassThrough
         (
            DeviceObject, pBuf, bufSize, &ulRetSize
         );

   ExFreePool(pBuf);

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_DETAIL,
      ("<%s> <--CDC_SetInterfaceIdle<%d>: IF %u Idle %d ST 0x%x\n",
        pDevExt->PortName, Cookie, Interface, IdleState, nts)
   );

   return nts;
}  // QCUSB_CDC_SetInterfaceIdle

NTSTATUS QCUSB_EnableBytePadding(PDEVICE_OBJECT pDevObj)
{
   UCHAR pBuf[sizeof(USB_DEFAULT_PIPE_REQUEST)+16];  // 1 byte request code
   PUSB_DEFAULT_PIPE_REQUEST pRequest;
   NTSTATUS                  nts = STATUS_SUCCESS;
   ULONG                     ulRetSize;
   PDEVICE_EXTENSION         pDevExt = pDevObj->DeviceExtension;

   if (pDevExt->bBytePaddingFeature == FALSE)
   {
      QCSER_DbgPrint
      (
         QCSER_DBG_MASK_CONTROL,
         QCSER_DBG_LEVEL_INFO,
         ("<%s> BytePaddin: feature not available in device.\n", pDevExt->PortName)
      );
      return STATUS_UNSUCCESSFUL;
   }

   RtlZeroMemory(pBuf, sizeof(USB_DEFAULT_PIPE_REQUEST)+16);

   pRequest = (PUSB_DEFAULT_PIPE_REQUEST)pBuf;

   pRequest->bmRequestType = VENDOR_DEVICE_TO_HOST;
   pRequest->bRequest      = QCSER_ENABLE_BYTE_PADDING;
   pRequest->wValue        = 0x0001;  // supported max version
   pRequest->wIndex        = pDevExt->DataInterface;
   pRequest->wLength       = 1;       // expected response data

   nts = QCUSB_PassThrough ( pDevObj, pBuf, 16, &ulRetSize );

   // The returned data should be in pRequest->Data[0] , [1], [2], ...

   if (nts == STATUS_SUCCESS)
   {
      if (pRequest->Data[0] == 0x01)   // version check
      {
         pDevExt->bEnableBytePadding = TRUE;

         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_INFO,
            ("<%s> BytePadding is supported by the device.\n", pDevExt->PortName)
         );
      }
      else
      {
         QCSER_DbgPrint
         (
            QCSER_DBG_MASK_CONTROL,
            QCSER_DBG_LEVEL_INFO,
            ("<%s> BytePadding is NOT supported by the device.\n", pDevExt->PortName)
         );
      }
   }

   QCSER_DbgPrint
   (
      QCSER_DBG_MASK_CONTROL,
      QCSER_DBG_LEVEL_INFO,
      ("<%s> EnableBytePadding status: 0x%x/0x%x\n", pDevExt->PortName,
        nts, pDevExt->bEnableBytePadding)
   );

   return nts;
}  // QCUSB_EnableBytePadding

