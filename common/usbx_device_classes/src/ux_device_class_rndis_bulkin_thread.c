/**************************************************************************/
/*                                                                        */
/*       Copyright (c) Microsoft Corporation. All rights reserved.        */
/*                                                                        */
/*       This software is licensed under the Microsoft Software License   */
/*       Terms for Microsoft Azure RTOS. Full text of the license can be  */
/*       found in the LICENSE file at https://aka.ms/AzureRTOS_EULA       */
/*       and in the root directory of this software.                      */
/*                                                                        */
/**************************************************************************/

/**************************************************************************/
/**************************************************************************/
/**                                                                       */ 
/** USBX Component                                                        */ 
/**                                                                       */
/**   Device RNDIS Class                                                  */
/**                                                                       */
/**************************************************************************/
/**************************************************************************/

#define UX_SOURCE_CODE


/* Include necessary system files.  */

#include "ux_api.h"
#include "ux_device_class_rndis.h"
#include "ux_device_stack.h"


#if !defined(UX_DEVICE_STANDALONE)
/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _ux_device_class_rndis_bulkin_thread                PORTABLE C      */ 
/*                                                           6.3.0        */
/*  AUTHOR                                                                */
/*                                                                        */
/*    Chaoqiong Xiao, Microsoft Corporation                               */
/*                                                                        */
/*  DESCRIPTION                                                           */
/*                                                                        */ 
/*    This function is the thread of the rndis bulkin endpoint. The bulk  */ 
/*    IN endpoint is used when the device wants to write data to be sent  */ 
/*    to the host.                                                        */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    rndis_class                             Address of rndis class      */ 
/*                                                container               */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _ux_device_stack_transfer_request     Request transfer              */ 
/*    _ux_utility_event_flags_get           Get event flags               */
/*    _ux_device_mutex_on                   Take mutex                    */
/*    _ux_device_mutex_off                  Release mutex                 */
/*    _ux_utility_long_put                  Put 32-bit value              */
/*    nx_packet_transmit_release            Release NetX packet           */
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    ThreadX                                                             */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  05-19-2020     Chaoqiong Xiao           Initial Version 6.0           */
/*  09-30-2020     Chaoqiong Xiao           Modified comment(s),          */
/*                                            verified memset and memcpy  */
/*                                            cases, used UX prefix to    */
/*                                            refer to TX symbols instead */
/*                                            of using them directly,     */
/*                                            resulting in version 6.1    */
/*  01-31-2022     Chaoqiong Xiao           Modified comment(s),          */
/*                                            refined macros names,       */
/*                                            resulting in version 6.1.10 */
/*  04-25-2022     Chaoqiong Xiao           Modified comment(s),          */
/*                                            fixed standalone compile,   */
/*                                            resulting in version 6.1.11 */
/*  10-31-2022     Chaoqiong Xiao           Modified comment(s),          */
/*                                            used NX API to copy data,   */
/*                                            resulting in version 6.2.0  */
/*  10-31-2023     Chaoqiong Xiao           Modified comment(s),          */
/*                                            added zero copy support,    */
/*                                            added a new mode to manage  */
/*                                            endpoint buffer in classes, */
/*                                            resulting in version 6.3.0  */
/*                                                                        */
/**************************************************************************/
VOID  _ux_device_class_rndis_bulkin_thread(ULONG rndis_class)
{

UX_SLAVE_CLASS                  *class_ptr;
UX_SLAVE_CLASS_RNDIS            *rndis;
UX_SLAVE_DEVICE                 *device;
UX_SLAVE_TRANSFER               *transfer_request;
UINT                            status;
ULONG                           actual_flags;
NX_PACKET                       *current_packet;
ULONG                           transfer_length;
ULONG                           copied;
#if (UX_DEVICE_ENDPOINT_BUFFER_OWNER == 1) && defined(UX_DEVICE_CLASS_RNDIS_ZERO_COPY) && !defined(NX_DISABLE_PACKET_CHAIN)
NX_PACKET                       *packet;
UINT                            do_copy;
#endif

    /* Cast properly the rndis instance.  */
    UX_THREAD_EXTENSION_PTR_GET(class_ptr, UX_SLAVE_CLASS, rndis_class)
    
    /* Get the rndis instance from this class container.  */
    rndis =  (UX_SLAVE_CLASS_RNDIS *) class_ptr -> ux_slave_class_instance;
    
    /* Get the pointer to the device.  */
    device =  &_ux_system_slave -> ux_system_slave_device;
    
    /* This thread runs forever but can be suspended or resumed.  */
    while(1)
    {

        /* Get the transfer request for the bulk IN pip.  */
        transfer_request =  &rndis -> ux_slave_class_rndis_bulkin_endpoint -> ux_slave_endpoint_transfer_request;

        /* As long as the device is in the CONFIGURED state.  */
        while (device -> ux_slave_device_state == UX_DEVICE_CONFIGURED)
        { 

            /* Wait until we have a event sent by the application. We do not treat yet the case where a timeout based
               on the interrupt pipe frequency or a change in the idle state forces us to send an empty report.  */
            status =  _ux_utility_event_flags_get(&rndis -> ux_slave_class_rndis_event_flags_group, (UX_DEVICE_CLASS_RNDIS_NEW_BULKIN_EVENT |
                                                                                            UX_DEVICE_CLASS_RNDIS_NEW_DEVICE_STATE_CHANGE_EVENT), 
                                                                                            UX_OR_CLEAR, &actual_flags, UX_WAIT_FOREVER);
                                                                                            
            /* Check the completion code and the actual flags returned. */
            if (status == UX_SUCCESS && (actual_flags & UX_DEVICE_CLASS_RNDIS_NEW_DEVICE_STATE_CHANGE_EVENT) == 0)
            {
                
                /* Parse all packets.  */
                while(rndis -> ux_slave_class_rndis_xmit_queue != UX_NULL)
                {

                    /* Protect this thread.  */
                    _ux_device_mutex_on(&rndis -> ux_slave_class_rndis_mutex);
                
                    /* Get the current packet in the list.  */
                    current_packet =  rndis -> ux_slave_class_rndis_xmit_queue;
                    
                    /* Set the next packet (or a NULL value) as the head of the xmit queue. */
                    rndis -> ux_slave_class_rndis_xmit_queue =  current_packet -> nx_packet_queue_next;
                    
                    /* Free Mutex resource.  */
                    _ux_device_mutex_off(&rndis -> ux_slave_class_rndis_mutex);
                        
                    /* If the link is down no need to rearm a packet. */
                    if (rndis -> ux_slave_class_rndis_link_state == UX_DEVICE_CLASS_RNDIS_LINK_STATE_UP)
                    {

                        /* Input packet mapping:
                         *                           | <----- nx_packet_length -----> |
                         *    .. NX_PHYSICAL_HEADER? | NX_ETHERNET_SIZE | Packet ...  |
                         *  start                  prepend                         append
                         */

                        /* Calculate the transfer length.  */
                        transfer_length =  current_packet -> nx_packet_length + UX_DEVICE_CLASS_RNDIS_PACKET_HEADER_LENGTH;

#if (UX_DEVICE_ENDPOINT_BUFFER_OWNER == 1) && defined(UX_DEVICE_CLASS_RNDIS_ZERO_COPY)

                        /* Default to success.  */
                        status = UX_SUCCESS;

                        /* Check if there was enough space for RNDIS header, if not data should be copied.  */
                        do_copy = (current_packet -> nx_packet_data_start + UX_DEVICE_CLASS_RNDIS_PACKET_HEADER_LENGTH > current_packet -> nx_packet_prepend_ptr);

#ifndef NX_DISABLE_PACKET_CHAIN

                        /* Check if packet is chained, chained packets must be joined (copied).  */
                        if (current_packet -> nx_packet_next)
                            do_copy = 1;
#endif

                        /* Check if data is being copied to new packet.  */
                        if (do_copy)
                        {

                            /* Check if packet pool is good for data collection.  */
                            if (transfer_length > current_packet -> nx_packet_pool_owner -> nx_packet_pool_payload_size)
                                status = UX_TRANSFER_BUFFER_OVERFLOW;

                            /* Allocate a new packet for data collection.  */
                            if (status == UX_SUCCESS)
                            {
                                status = nx_packet_allocate(current_packet -> nx_packet_pool_owner,
                                    &packet, UX_DEVICE_CLASS_RNDIS_PACKET_HEADER_LENGTH,
                                    UX_MS_TO_TICK(UX_DEVICE_CLASS_RNDIS_PACKET_POOL_WAIT));

                                /* Reserve space for RNDIS header.  */
                                packet -> nx_packet_append_ptr = packet -> nx_packet_prepend_ptr + UX_DEVICE_CLASS_RNDIS_PACKET_HEADER_LENGTH;

                            }

                            /* Copy the data to allocated.  */
                            if (status == UX_SUCCESS)
                            {

                                /* RNDIS packet header already reserved.  */

                                /* Copy the data to allocated.  */
                                status = nx_packet_data_extract_offset(current_packet, 0,
                                    packet -> nx_packet_append_ptr,
                                    current_packet -> nx_packet_length, &copied);
                                if (status == NX_SUCCESS)
                                {
                                    packet -> nx_packet_length = copied;

                                    /* Release the chained packet.  */
                                    current_packet -> nx_packet_length = current_packet -> nx_packet_length - UX_DEVICE_CLASS_RNDIS_ETHERNET_SIZE;
                                    nx_packet_transmit_release(current_packet);

                                    /* Use copied packet to transfer.  */
                                    current_packet = packet;
                                }
                            }

                            /* Can not copy/buffer issue.  */
                            if (status != UX_SUCCESS)
                                status = UX_TRANSFER_BUFFER_OVERFLOW;
                        }
                        else
                        {

                            /* There is enough space for RNDIS header, move prepend_ptr for it.  */
                            current_packet -> nx_packet_prepend_ptr -= UX_DEVICE_CLASS_RNDIS_PACKET_HEADER_LENGTH;
                        }

                        if (status == UX_SUCCESS)
                        {

                            /* Add the RNDIS header to this packet.  */

                            /* Reset the RNDIS header.  */
                            _ux_utility_memory_set(current_packet -> nx_packet_prepend_ptr, 0x00, UX_DEVICE_CLASS_RNDIS_PACKET_HEADER_LENGTH); /* Use case of memset is verified. */

                            /* Initialize fields in header.  */
                            _ux_utility_long_put(current_packet -> nx_packet_prepend_ptr + UX_DEVICE_CLASS_RNDIS_PACKET_MESSAGE_TYPE, UX_DEVICE_CLASS_RNDIS_PACKET_HEADER_MSG);
                            _ux_utility_long_put(current_packet -> nx_packet_prepend_ptr + UX_DEVICE_CLASS_RNDIS_PACKET_MESSAGE_LENGTH, transfer_length);
                            _ux_utility_long_put(current_packet -> nx_packet_prepend_ptr + UX_DEVICE_CLASS_RNDIS_PACKET_DATA_OFFSET, 
                                                    UX_DEVICE_CLASS_RNDIS_PACKET_HEADER_LENGTH - UX_DEVICE_CLASS_RNDIS_PACKET_DATA_OFFSET);
                            _ux_utility_long_put(current_packet -> nx_packet_prepend_ptr + UX_DEVICE_CLASS_RNDIS_PACKET_DATA_LENGTH, current_packet -> nx_packet_length);

                            /* Set the transfer request data pointer to the packet buffer.  */
                            transfer_request -> ux_slave_transfer_request_data_pointer = current_packet -> nx_packet_prepend_ptr;

                            /* If trace is enabled, insert this event into the trace buffer.  */
                            UX_TRACE_IN_LINE_INSERT(UX_TRACE_DEVICE_CLASS_RNDIS_PACKET_TRANSMIT, rndis, 0, 0, 0, UX_TRACE_DEVICE_CLASS_EVENTS, 0, 0)

                            /* Send the request to the device controller.  */
                            status =  _ux_device_stack_transfer_request(transfer_request, transfer_length, transfer_length + 1);
                        }

                        /* Check error code.  */
                        if (status != UX_SUCCESS)
                        {

                            /* Is this not a transfer abort? (it's fine to be aborted)  */
                            if (status != UX_TRANSFER_BUS_RESET)
                            {

                                /* Error trap. */
                                _ux_system_error_handler(UX_SYSTEM_LEVEL_THREAD, UX_SYSTEM_CONTEXT_CLASS, status);
                            }

                        }
#else

                        /* Is there enough space for this packet in the transfer buffer?  */
                        if (transfer_length <= UX_DEVICE_CLASS_RNDIS_BULKIN_BUFFER_SIZE)
                        {

                            /* Copy the packet in the transfer descriptor buffer.  */
                            status = nx_packet_data_extract_offset(current_packet, 0,
                                    transfer_request -> ux_slave_transfer_request_data_pointer +
                                                    UX_DEVICE_CLASS_RNDIS_PACKET_HEADER_LENGTH,
                                    current_packet -> nx_packet_length, &copied);
                            if (status == NX_SUCCESS)
                            {

                                /* Add the RNDIS header to this packet.  */
                                _ux_utility_long_put(transfer_request -> ux_slave_transfer_request_data_pointer + UX_DEVICE_CLASS_RNDIS_PACKET_MESSAGE_TYPE, UX_DEVICE_CLASS_RNDIS_PACKET_HEADER_MSG);
                                _ux_utility_long_put(transfer_request -> ux_slave_transfer_request_data_pointer + UX_DEVICE_CLASS_RNDIS_PACKET_MESSAGE_LENGTH, transfer_length);
                                _ux_utility_long_put(transfer_request -> ux_slave_transfer_request_data_pointer + UX_DEVICE_CLASS_RNDIS_PACKET_DATA_OFFSET, 
                                                        UX_DEVICE_CLASS_RNDIS_PACKET_HEADER_LENGTH - UX_DEVICE_CLASS_RNDIS_PACKET_DATA_OFFSET);
                                _ux_utility_long_put(transfer_request -> ux_slave_transfer_request_data_pointer + UX_DEVICE_CLASS_RNDIS_PACKET_DATA_LENGTH, current_packet -> nx_packet_length);
                                                        
                                /* If trace is enabled, insert this event into the trace buffer.  */
                                UX_TRACE_IN_LINE_INSERT(UX_TRACE_DEVICE_CLASS_RNDIS_PACKET_TRANSMIT, rndis, 0, 0, 0, UX_TRACE_DEVICE_CLASS_EVENTS, 0, 0)

                                /* Send the request to the device controller.  */
                                status =  _ux_device_stack_transfer_request(transfer_request, transfer_length, UX_DEVICE_CLASS_RNDIS_BULKIN_BUFFER_SIZE + 1);
                            }

                            /* Check for error. */
                            if (status != UX_SUCCESS)
                            
                                /* Error trap. */
                                _ux_system_error_handler(UX_SYSTEM_LEVEL_THREAD, UX_SYSTEM_CONTEXT_CLASS, status);
                        }
                        else
                        {

                            /* No, there is not enough space.  */

                            /* Report error to application. */
                            _ux_system_error_handler(UX_SYSTEM_LEVEL_THREAD, UX_SYSTEM_CONTEXT_CLASS, UX_MEMORY_INSUFFICIENT);
                        }
#endif
                    }        
               
                    /* Free the packet that was just sent.  First do some housekeeping.  */
                    current_packet -> nx_packet_prepend_ptr =  current_packet -> nx_packet_prepend_ptr + UX_DEVICE_CLASS_RNDIS_ETHERNET_SIZE; 
                    current_packet -> nx_packet_length =  current_packet -> nx_packet_length - UX_DEVICE_CLASS_RNDIS_ETHERNET_SIZE;
                
                    /* And ask Netx to release it.  */
                    nx_packet_transmit_release(current_packet); 
                }
            }
            else
            {

                /* We get here when the link is down or the last transmission failed. All packets pending must be freed.  */
                while(rndis -> ux_slave_class_rndis_xmit_queue != UX_NULL)
                {

                    /* Protect the chain of packets.  */
                    _ux_device_mutex_on(&rndis -> ux_slave_class_rndis_mutex);
                
                    /* Get the current packet in the list.  */
                    current_packet =  rndis -> ux_slave_class_rndis_xmit_queue;
                    
                    /* Set the next packet (or a NULL value) as the head of the xmit queue. */
                    rndis -> ux_slave_class_rndis_xmit_queue =  current_packet -> nx_packet_queue_next;
                    
                    /* Free Mutex resource.  */
                    _ux_device_mutex_off(&rndis -> ux_slave_class_rndis_mutex);
                    
                    /* Free the packet */
                    current_packet -> nx_packet_prepend_ptr =  current_packet -> nx_packet_prepend_ptr + UX_DEVICE_CLASS_RNDIS_ETHERNET_SIZE; 
                    current_packet -> nx_packet_length =  current_packet -> nx_packet_length - UX_DEVICE_CLASS_RNDIS_ETHERNET_SIZE;
                
                    /* And ask Netx to release it.  */
                    nx_packet_transmit_release(current_packet); 
                }
            }
        }
       
        /* We need to suspend ourselves. We will be resumed by the device enumeration module.  */
        _ux_device_thread_suspend(&rndis -> ux_slave_class_rndis_bulkin_thread);
    }
}
#endif
