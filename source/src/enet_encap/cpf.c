/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/
#include <string.h>

#include "cpf.h"

#include "cipster_api.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "byte_bufs.h"
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


int NotifyCommonPacketFormat( BufReader aCommand, BufWriter aReply )
{
    CipCommonPacketFormatData   cpfd;
    CipMessageRouterResponse    response( &cpfd );

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


int NotifyConnectedCommonPacketFormat( BufReader aCommand, BufWriter aReply )
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

            BufReader command( cpfd.data_item.data, cpfd.data_item.length );

            cpfd.address_item.data.sequence_number = command.get16();

            CipMessageRouterResponse response( &cpfd );

            // command is advanced by 2 here
            EipStatus s = NotifyMR( command, &response );

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


int CipCommonPacketFormatData::DeserializeCPFD( BufReader aSrc )
{
    BufReader in = aSrc;

    Clear();

    item_count = in.get16();

    if( item_count >= 1 )
    {
        address_item.type_id = (CipItemId) in.get16();
        address_item.length  = in.get16();

        if( address_item.length >= 4 )
        {
            address_item.data.connection_identifier = in.get32();
        }

        if( address_item.length == 8 )
        {
            address_item.data.sequence_number = in.get32();
        }
    }

    if( item_count >= 2 )
    {
        data_item.type_id = (CipItemId) in.get16();
        data_item.length  = in.get16();

        data_item.data = in.data();
        in += data_item.length;     // might throw exception
    }

    for( int j = 0; j + 2 < item_count && j < 2;  ++j )
    {
        CipItemId type_id = (CipItemId) in.get16();

        if( type_id == kCipItemIdSocketAddressInfoOriginatorToTarget ||
            type_id == kCipItemIdSocketAddressInfoTargetToOriginator )
        {
            SocketAddressInfoItem saii;

            saii.type_id    = type_id;
            saii.length     = in.get16();
            saii.sin_family = in.get16BE();
            saii.sin_port   = in.get16BE();
            saii.sin_addr   = in.get32BE();

            if( saii.length != 16 )
                goto error;

            for( int i = 0; i < 8;  ++i )
            {
                saii.nasin_zero[i] = *in++;
            }

            AppendRx( saii );
        }
        else
        {
            int unknown_size = in.get16();

            // skip unknown item
            in += unknown_size;
        }
    }

    return in.data() - aSrc.data();

error:
    CIPSTER_TRACE_ERR( "%s: failed\n", __func__ );
    return aSrc.data() - in.data();     // negative offset of problem
}


static void encodeConnectedDataItemLength( CipMessageRouterResponse* response, BufWriter& out )
{
    out.put16( response->data_length + 4 + 2 + (2 * response->size_of_additional_status) );
}


static void encodeUnconnectedDataItemLength( CipMessageRouterResponse* response, BufWriter& out )
{
    // Unconnected Item
    out.put16( response->data_length + 4 + (2 * response->size_of_additional_status) );
}


int CipCommonPacketFormatData::SerializeCPFD( CipMessageRouterResponse* aResponse, BufWriter aDst )
{
    BufWriter   out = aDst;

    if( aResponse )
    {
        // add Interface Handle and Timeout = 0 -> only for SendRRData and SendUnitData
        out.put32( 0 );
        out.put16( 0 );
    }

    out.put16( item_count + TxSocketAddressInfoItemCount() - RxSocketAddressInfoItemCount() );

    // process Address Item
    switch( address_item.type_id )
    {
    case kCipItemIdNullAddress:
        out.put16( kCipItemIdNullAddress );
        out.put16( 0 );
        break;

    case kCipItemIdConnectionAddress:
        // connected data item -> address length set to 4 and copy ConnectionIdentifier
        out.put16( kCipItemIdConnectionAddress );
        out.put16( 4 );
        out.put32( address_item.data.connection_identifier );
        break;

    case kCipItemIdSequencedAddressItem:
        // sequenced address item -> address length set to 8 and copy ConnectionIdentifier and SequenceNumber
        out.put16( kCipItemIdSequencedAddressItem );
        out.put16( 8 );
        out.put32( address_item.data.connection_identifier );
        out.put32( address_item.data.sequence_number );
        break;
    }

    // process Data Item
    if( data_item.type_id == kCipItemIdUnconnectedDataItem ||
        data_item.type_id == kCipItemIdConnectedDataItem )
    {
        if( aResponse )
        {
            out.put16( data_item.type_id );

            if( data_item.type_id == kCipItemIdConnectedDataItem ) // Connected Item
            {
                encodeConnectedDataItemLength( aResponse, out );

                // sequence number
                out.put16( address_item.data.sequence_number );
            }
            else // Unconnected Item
            {
                encodeUnconnectedDataItemLength( aResponse, out );
            }

            // serialize message router response
            out += aResponse->SerializeMRResponse( out );

            out.append( aResponse->data.data(), aResponse->data_length );
        }
        else // connected IO Message to send
        {
            out.put16( data_item.type_id );
            out.put16( data_item.length );
            out.append( data_item.data, data_item.length );
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
            out.put16( saii->type_id );
            out.put16( saii->length );
            out.put16BE( saii->sin_family );
            out.put16BE( saii->sin_port );
            out.put32BE( saii->sin_addr );
            out.append( saii->nasin_zero, 8 );
        }
    }

    return out.data() - aDst.data();

error:
    return -1;      // would be buffer overrun prevented
}

