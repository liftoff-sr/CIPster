/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/
//#include <string.h>

#include "cpf.h"

#include "cipster_api.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "byte_bufs.h"
#include "ciperror.h"
#include "cipconnectionmanager.h"
#include "trace.h"


SockAddrInfoItem::SockAddrInfoItem( CpfId aType, CipUdint aIP, int aPort ) :
    type_id( aType ),
    length( 16 ),
    sin_family( AF_INET ),
    sin_port( aPort ),
    sin_addr( aIP ),
    nasin_zero()
{
}


int Cpf::NotifyCommonPacketFormat( BufReader aCommand, BufWriter aReply )
{
    CipMessageRouterResponse    response( this );

    int result = DeserializeCpf( aCommand );

    if( result <= 0 )
        return -kEncapsulationProtocolIncorrectData;

    // Check if NullAddressItem received, otherwise it is not an unconnected
    // message and should not be here
    if( AddrType() == kCpfIdNullAddress )
    {
        if( DataType() == kCpfIdUnconnectedDataItem )
        {
            result = CipMessageRouterClass::NotifyMR( DataItemPayload(), &response );

            if( result < 0 )
                return -kEncapsulationProtocolIncorrectData;

            SetPayload( &response );
            result = Serialize( aReply );
        }
        else
        {
            CIPSTER_TRACE_ERR(
                "%s: got DataItemType():%d and not the expected kCpfIdUnconnectedDataItem\n",
                __func__, DataType()
                );
            return -kEncapsulationProtocolIncorrectData;
        }
    }
    else
    {
        CIPSTER_TRACE_ERR(
            "%s: got AddressItemType():%d and not the expected kCpfIdNullAddress\n",
            __func__, AddrType()
            );
        return -kEncapsulationProtocolIncorrectData;
    }

    return result;
}


int Cpf::NotifyConnectedCommonPacketFormat( BufReader aCommand, BufWriter aReply )
{
    int result = DeserializeCpf( aCommand );

    if( result <= 0 )
        return -kEncapsulationProtocolIncorrectData;

    // Check if ConnectedAddressItem received, otherwise it is no connected
    // message and should not be here
    if( AddrType() != kCpfIdConnectedAddress )
    {
        CIPSTER_TRACE_ERR(
                "notifyConnectedCPF: got something besides the expected CIP_ITEM_ID_NULL\n" );
        return -kEncapsulationProtocolIncorrectData;
    }

    // ConnectedAddressItem item
    CipConn* conn = GetConnectionByConsumingId( address_item.connection_identifier );

    if( conn )
    {
        // reset the watchdog timer
        conn->inactivity_watchdog_timer_usecs = conn->TimeoutMSecs_o_to_t();

        // TODO check connection id  and sequence count
        if( DataType() == kCpfIdConnectedDataItem )
        {
            // connected data item received

            BufReader command( data_item.data, data_item.length );

            address_item.encap_sequence_number = command.get16();

            CipMessageRouterResponse response( this );

            // command is advanced by 2 here because of the get16() above
            result = CipMessageRouterClass::NotifyMR( command, &response );

            if( result < 0 )
                return -kEncapsulationProtocolIncorrectData;

            address_item.connection_identifier = conn->producing_connection_id;

            SetPayload( &response );
            result = Serialize( aReply );
        }
        else
        {
            // wrong data item detected
            CIPSTER_TRACE_ERR(
                    "%s: got DataItemType()=%d instead of expected kCpfIdConnectedDataItem\n",
                    __func__, DataType() );
        }
    }
    else
    {
        CIPSTER_TRACE_ERR(
                "%s: connection with given ID:%d could not be found\n",
                __func__,
                address_item.connection_identifier );
    }

    return result;
}


int Cpf::DeserializeCpf( BufReader aSrc )
{
    BufReader in = aSrc;

    Clear();

    int received_item_count = in.get16();

    for( int item=0; item < received_item_count;  ++item )
    {
        CpfId   type_id = (CpfId) in.get16();
        int     length  = in.get16();

        switch( type_id )
        {
        //case kCpfIdListIdentityResponse
        //case kCpfIdListServiceResponse:
        case kCpfIdNullAddress:
        case kCpfIdConnectedAddress:
        case kCpfIdSequencedAddress:
            address_item.type_id = type_id;
            address_item.length  = length;
            if( length >= 4 )
                address_item.connection_identifier = in.get32();
            if( length == 8 )
                address_item.encap_sequence_number = in.get32();
            break;

        case kCpfIdConnectedDataItem:
        case kCpfIdUnconnectedDataItem:
            SetDataType( type_id );
            SetDataRange( ByteBuf( (CipByte*) in.data(), length ) );
            in += length;               // might throw exception
            break;

        case kCpfIdSockAddrInfo_O_to_T:
        case kCpfIdSockAddrInfo_T_to_O:
            {
                SockAddrInfoItem saii;

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


int Cpf::SerializedCount( int aCtl ) const
{
    int count = 2;      // item_count fills 2 bytes

    switch( address_item.type_id )
    {
    case kCpfIdNullAddress:
        count += 4;
        break;

    case kCpfIdConnectedAddress:
        count += 8;
        break;

    case kCpfIdSequencedAddress:
        count += 12;
        break;

    default:
        ;   // maybe no address?
    }

    // process Data Item
    if( data_item.type_id == kCpfIdUnconnectedDataItem ||
        data_item.type_id == kCpfIdConnectedDataItem )
    {
        count += 4;     // size of data_item.type_id + data_item.length

        if( payload )
        {
            if( data_item.type_id == kCpfIdConnectedDataItem ) // Connected Item
            {
                // +2 for the sequence number
                count += payload->SerializedCount() + 2;
            }
            else // Unconnected Item
            {
                count += payload->SerializedCount();
            }
        }
        else // connected IO Message to send
        {
            count += 4 + data_item.length;
        }
    }

    for( int type = kCpfIdSockAddrInfo_O_to_T;
         type <= kCpfIdSockAddrInfo_T_to_O;  ++type )
    {
        const SockAddrInfoItem* saii = SearchTx( CpfId(type) );

        if( saii )
        {
            count += 20;
        }
    }

    return count;
}


int Cpf::Serialize( BufWriter aDst, int aCtl ) const
{
    BufWriter   out = aDst;

    out.put16( item_count + TxSockAddrInfoItemCount() - RxSockAddrInfoItemCount() );

    // process Address Item
    switch( address_item.type_id )
    {
    case kCpfIdNullAddress:
        out.put16( address_item.type_id ).put16( 0 );
        break;

    case kCpfIdConnectedAddress:
        // connected data item -> address length set to 4 and copy
        // ConnectionIdentifier
        out.put16( address_item.type_id ).put16( 4 )

        .put32( address_item.connection_identifier );
        break;

    case kCpfIdSequencedAddress:
        // sequenced address item -> address length set to 8 and copy
        // ConnectionIdentifier and SequenceNumber
        out.put16( address_item.type_id ).put16( 8 )

        .put32( address_item.connection_identifier )
        .put32( address_item.encap_sequence_number );
        break;

    default:
        ;   // maybe no address?
    }

    // process Data Item
    if( data_item.type_id == kCpfIdUnconnectedDataItem ||
        data_item.type_id == kCpfIdConnectedDataItem )
    {
        if( payload )
        {
            out.put16( data_item.type_id );

            if( data_item.type_id == kCpfIdConnectedDataItem ) // Connected Item
            {
                // +2 for the sequence number, supplied just below here.
                out.put16( payload->SerializedCount() + 2 );

                // sequence number
                out.put16( address_item.encap_sequence_number );
            }
            else // Unconnected Item
            {
                out.put16( payload->SerializedCount() );
            }

            // serialize payload, either message router response or reply
            out += payload->Serialize( out );
        }
        else // connected IO Message to send
        {
            out.put16( data_item.type_id ).put16( data_item.length )

            .append( data_item.data, data_item.length );
        }
    }

    /*
        Process SockAddr Info Items. Make sure first the O->T and then T->O
        appears on the wire. EtherNet/IP specification doesn't demand it, but
        there are EIP devices which depend on CPF items to appear in the order
        of their ID number
    */
    for( int type = kCpfIdSockAddrInfo_O_to_T;
         type <= kCpfIdSockAddrInfo_T_to_O;  ++type )
    {
        const SockAddrInfoItem* saii = SearchTx( CpfId(type) );

        if( saii )
        {
            out.put16( saii->type_id ).put16( saii->length )

            .put16BE( saii->sin_family )
            .put16BE( saii->sin_port )
            .put32BE( saii->sin_addr )
            .append( saii->nasin_zero, 8 );
        }
    }

    return out.data() - aDst.data();
}

