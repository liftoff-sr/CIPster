/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#include <string.h>

#include "cpf.h"

#include "cipster_api.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "endianconv.h"
#include "ciperror.h"
#include "cipconnectionmanager.h"
#include "trace.h"

CipCommonPacketFormatData g_cpf;

SocketAddressInfoItem::SocketAddressInfoItem( CipItemId aType, CipUdint aIP, int aPort ) :
    type_id( aType ),
    length( 16 ),
    sin_family( AF_INET ),
    sin_port( aPort ),
    sin_addr( aIP )
{
    memset( nasin_zero, 0, sizeof nasin_zero );
}


int NotifyCommonPacketFormat( EncapsulationData* receive_data,
        EipByte* reply_buffer )
{
    int return_value = g_cpf.Init(
          receive_data->buf_pos,
          receive_data->data_length
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
        if( g_cpf.address_item.type_id == kCipItemIdNullAddress )
        {
            // found null address item
            if( g_cpf.data_item.type_id == kCipItemIdUnconnectedDataItem )
            {
                // unconnected data item received

                return_value = NotifyMR( g_cpf.data_item.data, g_cpf.data_item.length );

                if( return_value != kEipStatusError )
                {
                    return_value = g_cpf.AssembleLinearMessage( &g_response, reply_buffer );
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
        EipByte* reply_buffer )
{
    int return_value = g_cpf.Init(
            received_data->buf_pos,
            received_data->data_length );

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
        if( g_cpf.address_item.type_id == kCipItemIdConnectionAddress )
        {
            // ConnectedAddressItem item
            CipConn* conn = GetConnectionByConsumingId( g_cpf.address_item.data.connection_identifier );

            if( conn )
            {
                // reset the watchdog timer
                conn->inactivity_watchdog_timer_usecs =
                    conn->o_to_t_RPI_usecs << ( 2 + conn->connection_timeout_multiplier );

                // TODO check connection id  and sequence count
                if( g_cpf.data_item.type_id == kCipItemIdConnectedDataItem )
                {
                    // connected data item received

                    EipByte* pnBuf = g_cpf.data_item.data;

                    g_cpf.address_item.data.sequence_number =
                        (EipUint32) GetIntFromMessage( &pnBuf );

                    return_value = NotifyMR( pnBuf, g_cpf.data_item.length - 2 );

                    if( return_value != kEipStatusError )
                    {
                        g_cpf.address_item.data.connection_identifier =
                            conn->producing_connection_id;

                        return_value = g_cpf.AssembleLinearMessage( &g_response, reply_buffer );
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
 * @param aSrc data which needs to be structured.
 * @param data_length	Length of data in pa_Data.
 * @param common_packet_format_data	Pointer to structure of CPF data item.
 *
 *   @return kEipStatusOk .. success
 *         kEipStatusError .. error
 */
EipStatus CipCommonPacketFormatData::Init( EipByte* aSrc, int data_length )
{
    EipByte*    data = aSrc;

    Clear();

    item_count = GetIntFromMessage( &data );

    if( item_count >= 1 )
    {
        address_item.type_id = GetIntFromMessage( &data );
        address_item.length  = GetIntFromMessage( &data );

        if( address_item.length >= 4 )
        {
            address_item.data.connection_identifier = GetDintFromMessage( &data );
        }

        if( address_item.length == 8 )
        {
            address_item.data.sequence_number = GetDintFromMessage( &data );
        }
    }

    if( item_count >= 2 )
    {
        data_item.type_id = GetIntFromMessage( &data );
        data_item.length  = GetIntFromMessage( &data );
        data_item.data    = data;

        data += data_item.length;
    }

    for( int j = 0; j + 2 < item_count && j < 2;  ++j )
    {
        int type_id = GetIntFromMessage( &data );

        if( type_id == kCipItemIdSocketAddressInfoOriginatorToTarget ||
            type_id == kCipItemIdSocketAddressInfoTargetToOriginator )
        {
            SocketAddressInfoItem saii;

            saii.type_id    = type_id;
            saii.length     = GetIntFromMessage( &data );
            saii.sin_family = GetIntFromMessageBE( &data );
            saii.sin_port   = GetIntFromMessageBE( &data );
            saii.sin_addr   = GetDintFromMessageBE( &data );

            for( int i = 0; i < 8; i++ )
            {
                saii.nasin_zero[i] = *data++;
            }

            AppendRx( saii );
        }
        else    // no sockaddr item, skip unknown item
        {
            data += GetIntFromMessage( &data );
        }
    }

    int byte_count = data - aSrc;

    if( byte_count == data_length )
    {
        return kEipStatusOk;
    }
    else
    {
        CIPSTER_TRACE_WARN( "%s: unknown CPF item(s)\n", __func__ );

        if( item_count > 2 )
        {
            // there is an optional CPF item in packet which is not sockaddr item
            return kEipStatusOk;
        }
        else // something with the length was wrong
        {
            return kEipStatusError;
        }
    }
}


static int encodeConnectedDataItemLength( CipMessageRouterResponse* response, EipByte** message )
{
    return AddIntToMessage( response->data_length + 4 + 2 + (2 * response->size_of_additional_status),  message );
}


static int encodeExtendedStatus( CipMessageRouterResponse* response, EipByte** message )
{
    EipByte* p = *message;

    *p++ = response->size_of_additional_status;

    for( int i = 0; i < response->size_of_additional_status;  ++i )
        AddIntToMessage( response->additional_status[i], &p );

    int byte_count = p - *message;

    *message = p;

    return byte_count;
}


static int encodeUnconnectedDataItemLength( CipMessageRouterResponse* response, EipByte** message )
{
    // Unconnected Item
    return AddIntToMessage( response->data_length + 4 + (2 * response->size_of_additional_status), message );
}


int CipCommonPacketFormatData::AssembleLinearMessage( CipMessageRouterResponse* response, EipByte* aDst )
{
    EipByte* p = aDst;

    if( response )
    {
        // add Interface Handle and Timeout = 0 -> only for SendRRData and SendUnitData necessary
        AddDintToMessage( 0, &p );
        AddIntToMessage( 0, &p );
    }

    AddIntToMessage( item_count + TxSocketAddressInfoItemCount(), &p );

    // process Address Item
    switch( address_item.type_id )
    {
    case kCipItemIdNullAddress:
        AddIntToMessage( kCipItemIdNullAddress, &p );
        AddIntToMessage( 0, &p );
        break;

    case kCipItemIdConnectionAddress:
        // connected data item -> address length set to 4 and copy ConnectionIdentifier
        AddIntToMessage( kCipItemIdConnectionAddress, &p );
        AddIntToMessage( 4, &p );
        AddDintToMessage( address_item.data.connection_identifier, &p );
        break;

    case kCipItemIdSequencedAddressItem:
        // sequenced address item -> address length set to 8 and copy ConnectionIdentifier and SequenceNumber
        AddIntToMessage( kCipItemIdSequencedAddressItem, &p );
        AddIntToMessage( 8, &p );
        AddDintToMessage( address_item.data.connection_identifier, &p );
        AddDintToMessage( address_item.data.sequence_number, &p );
        break;
    }

    // process Data Item
    if( data_item.type_id == kCipItemIdUnconnectedDataItem ||
        data_item.type_id == kCipItemIdConnectedDataItem )
    {
        if( response )
        {
            AddIntToMessage( data_item.type_id, &p );

            if( data_item.type_id == kCipItemIdConnectedDataItem ) // Connected Item
            {
                encodeConnectedDataItemLength( response, &p );

                // sequence number
                AddIntToMessage( address_item.data.sequence_number, &p );
            }
            else // Unconnected Item
            {
                encodeUnconnectedDataItemLength( response, &p );
            }

            // write message router response into linear memory

            AddSintToMessage( response->reply_service, &p );

            *p++ = response->reserved;

            *p++ = response->general_status;

            encodeExtendedStatus( response, &p );

            for( int i = 0; i < response->data_length; ++i )
            {
                *p++ = response->data[i];
            }
        }
        else // connected IO Message to send
        {
            AddIntToMessage( data_item.type_id, &p );

            AddIntToMessage( data_item.length, &p );

            for( int i = 0; i < data_item.length;  ++i )
            {
                *p++ = data_item.data[i];
            }
        }
    }

    // process SockAddr Info Items
    /* make sure first the O->T and then T->O appears on the wire.
     * EtherNet/IP specification doesn't demand it, but there are EIP
     * devices which depend on CPF items to appear in the order of their
     * ID number */
    for( int type = kCipItemIdSocketAddressInfoOriginatorToTarget;
         type <= kCipItemIdSocketAddressInfoTargetToOriginator;  ++type )
    {
        SocketAddressInfoItem* saii = SearchTx( CipItemId(type) );

        if( saii )
        {
            AddIntToMessage( saii->type_id, &p );

            AddIntToMessage( saii->length, &p );

            AddIntToMessageBE( saii->sin_family, &p);

            AddIntToMessageBE( saii->sin_port, &p );

            AddDintToMessageBE( saii->sin_addr, &p );

            memcpy( p, saii->nasin_zero, 8 );  p += 8;
        }
    }

    return p - aDst;
}


int CipCommonPacketFormatData::AssembleIOMessage( EipByte* message )
{
    return AssembleLinearMessage( NULL, g_message_data_reply_buffer );
}

