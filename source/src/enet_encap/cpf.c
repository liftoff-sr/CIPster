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


SocketAddressInfoItem::SocketAddressInfoItem( CipItemId aType, CipUdint aIP, int aPort ) :
    type_id( aType ),
    length( 16 ),
    sin_family( AF_INET ),
    sin_port( aPort ),
    sin_addr( aIP )
{
    memset( nasin_zero, 0, sizeof nasin_zero );
}


int NotifyCommonPacketFormat( CipBufNonMutable aCommand, CipBufMutable aReply )
{
    CipCommonPacketFormatData   cpfd;
    CipMessageRouterResponse    response;

    int result = cpfd.DeserializeCPFD( aCommand );

    if( result <= 0 )
        return -kEncapsulationProtocolIncorrectData;

    // Check if NullAddressItem received, otherwise it is no unconnected
    // message and should not be here
    if( cpfd.AddressItemType() == kCipItemIdNullAddress )
    {
        if( cpfd.DataItemType() == kCipItemIdUnconnectedDataItem )
        {
            // unconnected data item received
            result = NotifyMR( cpfd.DataItemPayload(), &response );

            if( result < 0 )
                return -kEncapsulationProtocolIncorrectData;

            result = cpfd.SerializeCPFD( &response, aReply );
        }
        else
        {
            // wrong data item detected
            CIPSTER_TRACE_ERR(
                "%s: got something besides the expected CIP_ITEM_ID_UNCONNECTEDMESSAGE\n",
                __func__
                );
            return -kEncapsulationProtocolIncorrectData;
        }
    }
    else
    {
        CIPSTER_TRACE_ERR(
            "%s: got something besides the expected CIP_ITEM_ID_NULL\n",
            __func__
            );
        return -kEncapsulationProtocolIncorrectData;
    }

    return result;
}


int NotifyConnectedCommonPacketFormat( CipBufNonMutable aCommand, CipBufMutable aReply )
{
    CipCommonPacketFormatData cpfd;

    int result = cpfd.DeserializeCPFD( aCommand );

    if( result <= 0 )
        return -kEncapsulationProtocolIncorrectData;

    // Check if ConnectedAddressItem received, otherwise it is no connected
    // message and should not be here
    if( cpfd.AddressItemType() != kCipItemIdConnectionAddress )
    {
        CIPSTER_TRACE_ERR(
                "notifyConnectedCPF: got something besides the expected CIP_ITEM_ID_NULL\n" );
        return -kEncapsulationProtocolIncorrectData;
    }

    // ConnectedAddressItem item
    CipConn* conn = GetConnectionByConsumingId( cpfd.address_item.data.connection_identifier );

    if( conn )
    {
        // reset the watchdog timer
        conn->inactivity_watchdog_timer_usecs =
            conn->o_to_t_RPI_usecs << ( 2 + conn->connection_timeout_multiplier );

        // TODO check connection id  and sequence count
        if( cpfd.DataItemType() == kCipItemIdConnectedDataItem )
        {
            // connected data item received

            const EipByte* p = cpfd.data_item.data;

            cpfd.address_item.data.sequence_number = GetIntFromMessage( &p );

            CipMessageRouterResponse response;

            EipStatus s = NotifyMR(
                        CipBufNonMutable( p, cpfd.data_item.length - 2 ),
                        &response
                        );

            if( s != kEipStatusError )
            {
            }

            cpfd.address_item.data.connection_identifier = conn->producing_connection_id;

            result = cpfd.SerializeCPFD( &response, aReply );
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

    return result;
}


int CipCommonPacketFormatData::DeserializeCPFD( CipBufNonMutable aSrc )
{
    const EipByte*  p = aSrc.data();
    const EipByte*  limit = p + aSrc.size();

    Clear();

    if( p + 2 > limit )
        goto error;

    item_count = GetIntFromMessage( &p );

    if( item_count >= 1 )
    {
        if( p + 4 > limit )
            goto error;

        address_item.type_id = (CipItemId) GetIntFromMessage( &p );
        address_item.length  = GetIntFromMessage( &p );

        if( address_item.length >= 4 )
        {
            if( p + 4 > limit )
                goto error;

            address_item.data.connection_identifier = GetDintFromMessage( &p );
        }

        if( address_item.length == 8 )
        {
            if( p + 4 > limit )
                goto error;

            address_item.data.sequence_number = GetDintFromMessage( &p );
        }
    }

    if( item_count >= 2 )
    {
        if( p + 4 > limit )
            goto error;

        data_item.type_id = (CipItemId) GetIntFromMessage( &p );
        data_item.length  = GetIntFromMessage( &p );

        if( p + data_item.length > limit )
            goto error;

        data_item.data = p;
        p += data_item.length;
    }

    for( int j = 0; j + 2 < item_count && j < 2;  ++j )
    {
        if( p + 2 > limit )
            goto error;

        CipItemId type_id = (CipItemId) GetIntFromMessage( &p );

        if( type_id == kCipItemIdSocketAddressInfoOriginatorToTarget ||
            type_id == kCipItemIdSocketAddressInfoTargetToOriginator )
        {
            SocketAddressInfoItem saii;

            if( p + 18 > limit )
                goto error;

            saii.type_id    = type_id;
            saii.length     = GetIntFromMessage( &p );
            saii.sin_family = GetIntFromMessageBE( &p );
            saii.sin_port   = GetIntFromMessageBE( &p );
            saii.sin_addr   = GetDintFromMessageBE( &p );

            if( saii.length != 16 )
                goto error;

            for( int i = 0; i < 8;  ++i )
            {
                saii.nasin_zero[i] = *p++;
            }

            AppendRx( saii );
        }
        else
        {
            if( p + 2 > limit )
                goto error;

            int unknown_size = GetIntFromMessage( &p );

            if( p + unknown_size > limit )
            {
                CIPSTER_TRACE_ERR( "%s: large unknown field\n", __func__ );
                goto error;
            }

            // skip unknown item
            p += unknown_size;
        }
    }

    return p - aSrc.data();

error:
    CIPSTER_TRACE_ERR( "%s: failed\n", __func__ );
    return aSrc.data() - p;     // negative offset of problem
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


int CipCommonPacketFormatData::SerializeCPFD( CipMessageRouterResponse* aResponse, CipBufMutable aDst )
{
    EipByte*    p = aDst.data();
    EipByte*    limit = p + aDst.size();

    if( aResponse )
    {
        if( p + 6 > limit )
            goto error;

        // add Interface Handle and Timeout = 0 -> only for SendRRData and SendUnitData
        AddDintToMessage( 0, &p );
        AddIntToMessage( 0, &p );
    }

    if( p + 2 > limit )
        goto error;

    AddIntToMessage( item_count + TxSocketAddressInfoItemCount() - RxSocketAddressInfoItemCount(), &p );

    // process Address Item
    switch( address_item.type_id )
    {
    case kCipItemIdNullAddress:
        if( p + 4 > limit )
            goto error;

        AddIntToMessage( kCipItemIdNullAddress, &p );
        AddIntToMessage( 0, &p );
        break;

    case kCipItemIdConnectionAddress:
        // connected data item -> address length set to 4 and copy ConnectionIdentifier
        if( p + 8 > limit )
            goto error;

        AddIntToMessage( kCipItemIdConnectionAddress, &p );
        AddIntToMessage( 4, &p );
        AddDintToMessage( address_item.data.connection_identifier, &p );
        break;

    case kCipItemIdSequencedAddressItem:
        // sequenced address item -> address length set to 8 and copy ConnectionIdentifier and SequenceNumber
        if( p + 12 > limit )
            goto error;

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
        if( aResponse )
        {
            if( p + 2 > limit )
                goto error;

            AddIntToMessage( data_item.type_id, &p );

            if( data_item.type_id == kCipItemIdConnectedDataItem ) // Connected Item
            {
                if( p + 4 > limit )
                    goto error;

                encodeConnectedDataItemLength( aResponse, &p );

                // sequence number
                AddIntToMessage( address_item.data.sequence_number, &p );
            }
            else // Unconnected Item
            {
                if( p + 2 > limit )
                    goto error;

                encodeUnconnectedDataItemLength( aResponse, &p );
            }

            // serialize message router response
            if( p + 4 + 2 * aResponse->size_of_additional_status > limit )
                goto error;

            *p++ = aResponse->reply_service;
            *p++ = aResponse->reserved;
            *p++ = aResponse->general_status;

            encodeExtendedStatus( aResponse, &p );

            if( p + aResponse->data_length > limit )
                goto error;

            for( int i = 0; i < aResponse->data_length; ++i )
            {
                *p++ = aResponse->data.data()[i];
            }
        }
        else // connected IO Message to send
        {
            if( p + 4 + data_item.length > limit )
                goto error;

            AddIntToMessage( data_item.type_id, &p );

            AddIntToMessage( data_item.length, &p );

            for( int i = 0; i < data_item.length;  ++i )
            {
                *p++ = data_item.data[i];
            }
        }
    }

    /*

        Process SockAddr Info Items. Make sure first the O->T and then T->O
        appears on the wire. EtherNet/IP specification doesn't demand it, but
        there are EIP devices which depend on CPF items to appear in the order
        of their ID number

    */
    for( int type = kCipItemIdSocketAddressInfoOriginatorToTarget;
         type <= kCipItemIdSocketAddressInfoTargetToOriginator;  ++type )
    {
        SocketAddressInfoItem* saii = SearchTx( CipItemId(type) );

        if( saii )
        {
            if( p + 20 > limit )
                goto error;

            AddIntToMessage( saii->type_id, &p );

            AddIntToMessage( saii->length, &p );

            AddIntToMessageBE( saii->sin_family, &p);

            AddIntToMessageBE( saii->sin_port, &p );

            AddDintToMessageBE( saii->sin_addr, &p );

            memcpy( p, saii->nasin_zero, 8 );  p += 8;
        }
    }

    return p - aDst.data();

error:
    return -1;      // would be buffer overrun prevented
}

