/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#include <string.h>

#include "cpf.h"

#include "opener_api.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "endianconv.h"
#include "ciperror.h"
#include "cipconnectionmanager.h"
#include "trace.h"

CipCommonPacketFormatData g_common_packet_format_data_item;    //*< CPF global data items

int NotifyCommonPacketFormat( EncapsulationData* receive_data,
        EipUint8* reply_buffer )
{
    int return_value = CreateCommonPacketFormatStructure(
          receive_data->buf_pos,
          receive_data->data_length,
          &g_common_packet_format_data_item
          );

    if( return_value == kEipStatusError )
    {
        CIPSTER_TRACE_ERR( "notifyCPF: error from createCPFstructure\n" );
    }
    else
    {
        return_value = kEipStatusOk; // In cases of errors we normally need to send an error response

        // Check if NullAddressItem received, otherwise it is no unconnected
        // message and should not be here
        if( g_common_packet_format_data_item.address_item.type_id
            == kCipItemIdNullAddress )
        {
            // found null address item
            if( g_common_packet_format_data_item.data_item.type_id
                == kCipItemIdUnconnectedDataItem ) // unconnected data item received
            {
                return_value = NotifyMR(
                        g_common_packet_format_data_item.data_item.data,
                        g_common_packet_format_data_item.data_item.length );

                if( return_value != kEipStatusError )
                {
                    return_value = AssembleLinearMessage(
                            &g_response, &g_common_packet_format_data_item,
                            reply_buffer );
                }
            }
            else
            {
                // wrong data item detected
                CIPSTER_TRACE_ERR(
                        "notifyCPF: got something besides the expected CIP_ITEM_ID_UNCONNECTEDMESSAGE\n" );
                receive_data->status = kEncapsulationProtocolIncorrectData;
            }
        }
        else
        {
            CIPSTER_TRACE_ERR(
                    "notifyCPF: got something besides the expected CIP_ITEM_ID_NULL\n" );
            receive_data->status = kEncapsulationProtocolIncorrectData;
        }
    }

    return return_value;
}


int NotifyConnectedCommonPacketFormat( EncapsulationData* received_data,
        EipUint8* reply_buffer )
{
    int return_value = CreateCommonPacketFormatStructure(
            received_data->buf_pos,
            received_data->data_length, &g_common_packet_format_data_item );

    if( kEipStatusError == return_value )
    {
        CIPSTER_TRACE_ERR( "notifyConnectedCPF: error from createCPFstructure\n" );
    }
    else
    {
        return_value = kEipStatusError; // For connected explicit messages status always has to be 0

        /*  check if ConnectedAddressItem received, otherwise it is no connected
            message and should not be here
        */
        if( g_common_packet_format_data_item.address_item.type_id
            == kCipItemIdConnectionAddress )
        {
            // ConnectedAddressItem item
            CipConn* conn = GetConnectedObject(
                    g_common_packet_format_data_item.address_item.data
                    .connection_identifier );

            if( conn )
            {
                // reset the watchdog timer
                conn->inactivity_watchdog_timer = (conn->o_to_t_requested_packet_interval /1000) <<
                                                        ( 2 + conn->connection_timeout_multiplier );

                // TODO check connection id  and sequence count
                if( g_common_packet_format_data_item.data_item.type_id
                    == kCipItemIdConnectedDataItem ) // connected data item received
                {
                    EipUint8* pnBuf = g_common_packet_format_data_item.data_item.data;

                    g_common_packet_format_data_item.address_item.data.sequence_number =
                        (EipUint32) GetIntFromMessage( &pnBuf );

                    return_value = NotifyMR(
                            pnBuf, g_common_packet_format_data_item.data_item.length - 2 );

                    if( return_value != kEipStatusError )
                    {
                        g_common_packet_format_data_item.address_item.data
                        .connection_identifier = conn
                                                 ->produced_connection_id;
                        return_value = AssembleLinearMessage(
                                &g_response, &g_common_packet_format_data_item,
                                reply_buffer );
                    }
                }
                else
                {
                    // wrong data item detected
                    CIPSTER_TRACE_ERR(
                            "notifyConnectedCPF: got something besides the expected CIP_ITEM_ID_UNCONNECTEDMESSAGE\n" );
                }
            }
            else
            {
                CIPSTER_TRACE_ERR(
                        "notifyConnectedCPF: connection with given ID could not be found\n" );
            }
        }
        else
        {
            CIPSTER_TRACE_ERR(
                    "notifyConnectedCPF: got something besides the expected CIP_ITEM_ID_NULL\n" );
        }
    }

    return return_value;
}


/**
 * @brief Creates Common Packet Format structure out of data.
 * @param data Pointer to data which need to be structured.
 * @param data_length	Length of data in pa_Data.
 * @param common_packet_format_data	Pointer to structure of CPF data item.
 *
 *   @return kEipStatusOk .. success
 *         kEipStatusError .. error
 */
EipStatus CreateCommonPacketFormatStructure( EipUint8* data, int data_length,
        CipCommonPacketFormatData* cpfd )
{
    cpfd->address_info_item[0].type_id = 0;
    cpfd->address_info_item[1].type_id = 0;

    int length_count = 0;

    cpfd->item_count = GetIntFromMessage( &data );
    length_count += 2;

    if( cpfd->item_count >= 1 )
    {
        cpfd->address_item.type_id = GetIntFromMessage( &data );
        cpfd->address_item.length  = GetIntFromMessage( &data );
        length_count += 4;

        if( cpfd->address_item.length >= 4 )
        {
            cpfd->address_item.data.connection_identifier = GetDintFromMessage( &data );
            length_count += 4;
        }

        if( cpfd->address_item.length == 8 )
        {
            cpfd->address_item.data.sequence_number = GetDintFromMessage( &data );
            length_count += 4;
        }
    }

    if( cpfd->item_count >= 2 )
    {
        cpfd->data_item.type_id    = GetIntFromMessage( &data );
        cpfd->data_item.length     = GetIntFromMessage( &data );
        cpfd->data_item.data = data;
        data += cpfd->data_item.length;
        length_count += 4 + cpfd->data_item.length;
    }

    for( int j = 0; j < (cpfd->item_count - 2); j++ ) // TODO there needs to be a limit check here???
    {
        cpfd->address_info_item[j].type_id = GetIntFromMessage( &data );
        length_count += 2;

        if( cpfd->address_info_item[j].type_id == kCipItemIdSocketAddressInfoOriginatorToTarget ||
            cpfd->address_info_item[j].type_id == kCipItemIdSocketAddressInfoTargetToOriginator )
        {
            cpfd->address_info_item[j].length = GetIntFromMessage( &data );

            cpfd->address_info_item[j].sin_family = GetIntFromMessage( &data );
            cpfd->address_info_item[j].sin_port = GetIntFromMessage( &data );
            cpfd->address_info_item[j].sin_addr = GetDintFromMessage( &data );

            for( int i = 0; i < 8; i++ )
            {
                cpfd->address_info_item[j].nasin_zero[i] = *data++;
            }

            length_count += 18;
        }
        else // no sockaddr item found
        {
            cpfd->address_info_item[j].type_id = 0; // mark as not set
            data -= 2;
        }
    }

    // set the addressInfoItems to not set if they were not received
    if( cpfd->item_count < 4 )
    {
        cpfd->address_info_item[1].type_id = 0;

        if( cpfd->item_count < 3 )
        {
            cpfd->address_info_item[0].type_id = 0;
        }
    }

    if( length_count == data_length ) // length of data is equal to length of Addr and length of Data
    {
        return kEipStatusOk;
    }
    else
    {
        CIPSTER_TRACE_WARN(
                "something is wrong with the length in Message Router @ CreateCommonPacketFormatStructure\n" );

        if( cpfd->item_count > 2 )
        {
            // there is an optional packet in data stream which is not sockaddr item
            return kEipStatusOk;
        }
        else // something with the length was wrong
        {
            return kEipStatusError;
        }
    }
}


// null address item -> address length set to 0
/**
 * Encodes a Null Address Item into the message frame
 * @param message The message frame
 * @param size The actual size of the message frame
 *
 * @return The new size of the message frame after encoding
 */
int EncodeNullAddressItem( EipUint8** message, int size )
{
    // null address item -> address length set to 0
    size  += AddIntToMessage( kCipItemIdNullAddress, message );
    size  += AddIntToMessage( 0, message );
    return size;
}


// connected data item -> address length set to 4 and copy ConnectionIdentifier
/**
 * Encodes a Connected Address Item into the message frame
 * @param message The message frame
 * @param cpfd The Common Packet Format data structure from which the message is constructed
 * @param size The actual size of the message frame
 *
 * @return The new size of the message frame after encoding
 */
int EncodeConnectedAddressItem( EipUint8** message,
        CipCommonPacketFormatData* cpfd, int size )
{
    // connected data item -> address length set to 4 and copy ConnectionIdentifier
    size    += AddIntToMessage( kCipItemIdConnectionAddress, message );
    size    += AddIntToMessage( 4, message );
    size    += AddDintToMessage( cpfd->address_item.data.connection_identifier, message );
    return size;
}


// TODO: Add doxygen documentation
// sequenced address item -> address length set to 8 and copy ConnectionIdentifier and SequenceNumber
// sequence number?????
int EncodeSequencedAddressItem( EipUint8** message,
        CipCommonPacketFormatData* cpfd, int size )
{
    // sequenced address item -> address length set to 8 and copy ConnectionIdentifier and SequenceNumber
    size += AddIntToMessage( kCipItemIdSequencedAddressItem, message );
    size += AddIntToMessage( 8, message );
    size += AddDintToMessage( cpfd->address_item.data.connection_identifier, message );
    size += AddDintToMessage( cpfd->address_item.data.sequence_number, message );
    return size;
}


/**
 * Adds the item count to the message frame
 *
 * @param cpfd The Common Packet Format data structure from which the message is constructed
 * @param message The message frame
 * @param size The actual size of the message frame
 *
 * @return The new size of the message frame after encoding
 */
int EncodeItemCount( CipCommonPacketFormatData* cpfd,
        EipUint8** message, int size )
{
    size += AddIntToMessage( cpfd->item_count, message ); // item count
    return size;
}


/**
 * Adds the data item type to the message frame
 *
 * @param cpfd The Common Packet Format data structure from which the message is constructed
 * @param message The message frame
 * @param size The actual size of the message frame
 *
 * @return The new size of the message frame after encoding
 */
int EncodeDataItemType( CipCommonPacketFormatData* cpfd,
        EipUint8** message, int size )
{
    size += AddIntToMessage( cpfd->data_item.type_id, message );
    return size;
}


/**
 * Adds the data item section length to the message frame
 *
 * @param cpfd The Common Packet Format data structure from which the message is constructed
 * @param message The message frame
 * @param size The actual size of the message frame
 *
 * @return The new size of the message frame after encoding
 */
int EncodeDataItemLength( CipCommonPacketFormatData* cpfd,
        EipUint8** message, int size )
{
    size += AddIntToMessage( cpfd->data_item.length, message );
    return size;
}


/**
 * Adds the data items to the message frame
 *
 * @param cpfd The Common Packet Format data structure from which the message is constructed
 * @param message The message frame
 * @param size The actual size of the message frame
 *
 * @return The new size of the message frame after encoding
 */
int EncodeDataItemData( CipCommonPacketFormatData* cpfd,
        EipUint8** message, int size )
{
    for( int i = 0; i < cpfd->data_item.length; i++ )
    {
        size += AddSintToMessage( *(cpfd->data_item.data + i), message );
    }

    return size;
}


int EncodeConnectedDataItemLength( CipMessageRouterResponse* response,
        EipUint8** message,
        int size )
{
    size += AddIntToMessage(
            (EipUint16) ( response->data_length + 4 + 2
                          + (2 * response->size_of_additional_status) ),
            message );
    return size;
}


int EncodeSequenceNumber( int size, const CipCommonPacketFormatData* cpfd,
        EipUint8** message )
{
    // 2 bytes
    size += AddIntToMessage( (EipUint16) cpfd->address_item.data.sequence_number,
                message );

    return size;
}


int EncodeReplyService( int size, EipUint8** message,
        CipMessageRouterResponse* response )
{
    size += AddSintToMessage( response->reply_service, message );
    return size;
}


int EnocdeReservedFieldOfLengthByte( int size, EipUint8** message,
        CipMessageRouterResponse* response )
{
    size += AddSintToMessage( response->reserved, message );
    return size;
}


int EncodeGeneralStatus( int size, EipUint8** message,
        CipMessageRouterResponse* response )
{
    size += AddSintToMessage( response->general_status, message );
    return size;
}


int EncodeExtendedStatusLength( int size, EipUint8** message,
        CipMessageRouterResponse* response )
{
    size += AddSintToMessage( response->size_of_additional_status,
            message );
    return size;
}


int EncodeExtendedStatusDataItems( int size, CipMessageRouterResponse* response,
        EipUint8** message )
{
    for( int i = 0; i < response->size_of_additional_status; i++ )
        size += AddIntToMessage( response->additional_status[i],
                message );

    return size;
}


int EncodeExtendedStatus( int size, EipUint8** message,
        CipMessageRouterResponse* response )
{
    size    = EncodeExtendedStatusLength( size, message, response );
    size    = EncodeExtendedStatusDataItems( size, response, message );

    return size;
}


int EncodeUnconnectedDataItemLength( int size, CipMessageRouterResponse* response,
        EipUint8** message )
{
    // Unconnected Item
    size += AddIntToMessage( (EipUint16) ( response->data_length + 4
                                + (2 * response->size_of_additional_status) ),
                message );

    return size;
}


int EncodeMessageRouterResponseData( int size, CipMessageRouterResponse* response,
        EipUint8** message )
{
    for( int i = 0; i < response->data_length; i++ )
    {
        size += AddSintToMessage( (response->data)[i], &*message );
    }

    return size;
}


int EncodeSockaddrInfoItemTypeId( int size, int item_type,
        CipCommonPacketFormatData* cpfd,
        EipUint8** message )
{
    CIPSTER_ASSERT( item_type == 0 || item_type == 1 );

    size += AddIntToMessage( cpfd->address_info_item[item_type].type_id,
                message );

    return size;
}


int EncodeSockaddrInfoLength( int size,
        int j,
        CipCommonPacketFormatData* cpfd,
        EipUint8** message )
{
    size += AddIntToMessage( cpfd->address_info_item[j].length, message );
    return size;
}


/** @brief Copy data from response struct and cpfd into linear memory in
 * pa_msg for transmission over in encapsulation.
 *
 * @param response	pointer to message router response which has to be aligned into linear memory.
 * @param cpfd pointer to CPF structure which has to be aligned into linear memory.
 * @param message		pointer to linear memory.
 *  @return length of reply in message in bytes
 *          -1 .. error
 */
int AssembleLinearMessage( CipMessageRouterResponse* response,
        CipCommonPacketFormatData* cpfd,
        EipUint8* message )
{
    int message_size = 0;

    if( response )
    {
        // add Interface Handle and Timeout = 0 -> only for SendRRData and SendUnitData necessary
        AddDintToMessage( 0, &message );
        AddIntToMessage( 0, &message );
        message_size += 6;
    }

    message_size = EncodeItemCount( cpfd, &message, message_size );

    // process Address Item
    switch( cpfd->address_item.type_id )
    {
    case kCipItemIdNullAddress:
        {
            message_size = EncodeNullAddressItem( &message, message_size );
        }
        break;

    case kCipItemIdConnectionAddress:
        {
            message_size = EncodeConnectedAddressItem( &message,
                                cpfd, message_size );
        }
        break;

    case kCipItemIdSequencedAddressItem:
        {
            message_size = EncodeSequencedAddressItem( &message,
                                cpfd, message_size );
        }
        break;
    }

    // process Data Item
    if( cpfd->data_item.type_id == kCipItemIdUnconnectedDataItem ||
        cpfd->data_item.type_id == kCipItemIdConnectedDataItem )
    {
        if( response )
        {
            message_size = EncodeDataItemType( cpfd, &message, message_size );

            if( cpfd->data_item.type_id == kCipItemIdConnectedDataItem ) // Connected Item
            {
                message_size = EncodeConnectedDataItemLength( response,
                        &message, message_size );

                message_size = EncodeSequenceNumber( message_size,
                        &g_common_packet_format_data_item,
                        &message );
            }
            else // Unconnected Item
            {
                message_size = EncodeUnconnectedDataItemLength( message_size,
                        response,
                        &message );
            }

            // write message router response into linear memory
            message_size = EncodeReplyService( message_size, &message,
                    response );

            message_size = EnocdeReservedFieldOfLengthByte( message_size, &message,
                    response );

            message_size = EncodeGeneralStatus( message_size, &message,
                    response );

            message_size = EncodeExtendedStatus( message_size, &message,
                    response );

            message_size = EncodeMessageRouterResponseData( message_size,
                    response,
                    &message );
        }
        else // connected IO Message to send
        {
            message_size = EncodeDataItemType( cpfd, &message, message_size );

            message_size = EncodeDataItemLength( cpfd, &message, message_size );

            message_size = EncodeDataItemData( cpfd, &message, message_size );
        }
    }

    // process SockAddr Info Items
    /* make sure first the O->T and then T->O appears on the wire.
     * EtherNet/IP specification doesn't demand it, but there are EIP
     * devices which depend on CPF items to appear in the order of their
     * ID number */
    for( int type = kCipItemIdSocketAddressInfoOriginatorToTarget;
         type <= kCipItemIdSocketAddressInfoTargetToOriginator; type++ )
    {
        for( int j = 0; j < 2; j++ )
        {
            if( cpfd->address_info_item[j].type_id == type )
            {
                message_size = EncodeSockaddrInfoItemTypeId(
                        message_size, j, cpfd, &message );

                message_size = EncodeSockaddrInfoLength( message_size, j,
                        cpfd,
                        &message );

                message_size += EncapsulateIpAddress(
                        cpfd->address_info_item[j].sin_port,
                        cpfd->address_info_item[j].sin_addr,
                        &message );

                message_size += FillNextNMessageOctetsWithValueAndMoveToNextPosition(
                        0, 8, &message );
                break;
            }
        }
    }

    return message_size;
}


int AssembleIOMessage( CipCommonPacketFormatData* cpfd,  EipUint8* message )
{
    return AssembleLinearMessage( 0, cpfd, g_message_data_reply_buffer );
}
