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


CipCommonPacketFormatData::CipCommonPacketFormatData( int aSocket ) :
    socket( aSocket )
{
    Clear();
}


int CipCommonPacketFormatData::NotifyCommonPacketFormat( BufReader aCommand, BufWriter aReply )
{
    CipMessageRouterResponse    response( this );

    int result = DeserializeCPFD( aCommand );

    if( result <= 0 )
        return -kEncapsulationProtocolIncorrectData;

    // Check if NullAddressItem received, otherwise it is no unconnected
    // message and should not be here
    if( AddressItemType() == kCipItemIdNullAddress )
    {
        if( DataItemType() == kCipItemIdUnconnectedDataItem )
        {
            // unconnected data item received
            result = CipMessageRouterClass::NotifyMR( DataItemPayload(), &response );

            if( result < 0 )
                return -kEncapsulationProtocolIncorrectData;

            result = SerializeCPFD( &response, aReply );
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


int CipCommonPacketFormatData::NotifyConnectedCommonPacketFormat( BufReader aCommand, BufWriter aReply )
{
    int result = DeserializeCPFD( aCommand );

    if( result <= 0 )
        return -kEncapsulationProtocolIncorrectData;

    // Check if ConnectedAddressItem received, otherwise it is no connected
    // message and should not be here
    if( AddressItemType() != kCipItemIdConnectionAddress )
    {
        CIPSTER_TRACE_ERR(
                "notifyConnectedCPF: got something besides the expected CIP_ITEM_ID_NULL\n" );
        return -kEncapsulationProtocolIncorrectData;
    }

    // ConnectedAddressItem item
    CipConn* conn = GetConnectionByConsumingId( address_item.data.connection_identifier );

    if( conn )
    {
        // reset the watchdog timer
        conn->inactivity_watchdog_timer_usecs =
            conn->o_to_t_RPI_usecs << ( 2 + conn->connection_timeout_multiplier );

        // TODO check connection id  and sequence count
        if( DataItemType() == kCipItemIdConnectedDataItem )
        {
            // connected data item received

            BufReader command( data_item.data, data_item.length );

            address_item.data.sequence_number = command.get16();

            CipMessageRouterResponse response( this );

            // command is advanced by 2 here
            EipStatus s = CipMessageRouterClass::NotifyMR( command, &response );

            if( s != kEipStatusError )
            {
            }

            address_item.data.connection_identifier = conn->producing_connection_id;

            result = SerializeCPFD( &response, aReply );
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

    int received_item_count = in.get16();

    for( int item=0; item < received_item_count;  ++item )
    {
        CipItemId type_id = (CipItemId) in.get16();
        int length = in.get16();

        switch( type_id )
        {
        //case kCipItemIdListIdentityResponse
        //case kCipItemIdListServiceResponse:
        case kCipItemIdNullAddress:
        case kCipItemIdConnectionAddress:
        case kCipItemIdSequencedAddressItem:
            address_item.type_id = type_id;
            address_item.length  = length;
            if( length >= 4 )
                address_item.data.connection_identifier = in.get32();
            if( length == 8 )
                address_item.data.sequence_number = in.get32();
            break;

        case kCipItemIdConnectedDataItem:
        case kCipItemIdUnconnectedDataItem:
            data_item.type_id = type_id;
            data_item.length  = length;
            data_item.data = in.data();
            in += length;               // might throw exception
            break;

        case kCipItemIdSocketAddressInfoOriginatorToTarget:
        case kCipItemIdSocketAddressInfoTargetToOriginator:
            {
                SocketAddressInfoItem saii;

                saii.type_id    = type_id;
                saii.length     = length;
                saii.sin_family = in.get16BE();
                saii.sin_port   = in.get16BE();
                saii.sin_addr   = in.get32BE();

                if( saii.length != 16 )
                    goto error;

                for( int i = 0; i < 8;  ++i )
                {
                    saii.nasin_zero[i] = in.get8();
                }

                AppendRx( saii );
            }
            break;

        default:
            // Vol 2 Table 2-6.10 says reply with 0x0003 in encap status.
            // Leave item_count at zero.
            goto error;
        }
    }
    item_count = received_item_count;

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

