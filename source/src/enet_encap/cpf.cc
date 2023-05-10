/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016, SoftPLC Corporation.
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


Cpf::Cpf( const SockAddr& aTcpPeer, CipUdint aSessionHandle ) :
    payload( 0 ),
    tcp_peer( aTcpPeer ),
    session_handle( aSessionHandle )
{
    Clear();
}


Cpf::Cpf( CpfId aAddrType, CpfId aDataType, Serializeable* aPayload ) :
    address_item( aAddrType, aDataType ),
    data_item( aDataType ),
    payload( aPayload ),
    session_handle( 0 )
{
    ClearRx_O_T();
    ClearRx_T_O();
    ClearTx_O_T();
    ClearTx_T_O();
}


Cpf::Cpf( const AddressItem& aAddr, CpfId aDataType ) :
    address_item( aAddr ),
    data_item( aDataType ),
    payload( 0 ),
    session_handle( 0 )
{
    ClearRx_O_T();
    ClearRx_T_O();
    ClearTx_O_T();
    ClearTx_T_O();
}


/*
const SockAddr* Cpf::TcpPeerAddr() const
{
    if( const EncapSession* s = SessionMgr::GetSession( session_handle ) )
    {
        return &s->m_peeraddr;
    }

    CIPSTER_ASSERT( !"no session for TcpPeerAddr" );

    return NULL;
}
*/

int Cpf::NotifyCommonPacketFormat( BufReader aCommand, BufWriter aReply )
{
    CipMessageRouterResponse    response( this );

    int result = DeserializeCpf( aCommand );

    if( result <= 0 )
        return -kEncapErrorIncorrectData;

    // Check if NullAddressItem received, otherwise it is not an unconnected
    // message and should not be here
    if( AddrType() == kCpfIdNullAddress )
    {
        if( DataType() == kCpfIdUnconnectedDataItem )
        {
            CipMessageRouterRequest request;

            int consumed = request.DeserializeMRReq( DataItemPayload() );

            if( consumed <= 0 )
            {
                CIPSTER_TRACE_ERR( "%s: error from DeserializeMRReq()\n", __func__ );
                response.SetGenStatus( kCipErrorPathSegmentError );
            }
            else
            {
                EipStatus s = CipMessageRouterClass::NotifyMR( &request, &response );

                if( s == kEipStatusError )
                    return -kEncapErrorIncorrectData;
            }

            SetPayload( &response );
            result = Serialize( aReply );
        }
        else
        {
            CIPSTER_TRACE_ERR(
                "%s: got DataItemType():%d and not the expected kCpfIdUnconnectedDataItem\n",
                __func__, DataType()
                );
            return -kEncapErrorIncorrectData;
        }
    }
    else
    {
        CIPSTER_TRACE_ERR(
            "%s: got AddressItemType():%d and not the expected kCpfIdNullAddress\n",
            __func__, AddrType()
            );
        return -kEncapErrorIncorrectData;
    }

    return result;
}


int Cpf::NotifyConnectedCommonPacketFormat( BufReader aCommand, BufWriter aReply )
{
    int result = DeserializeCpf( aCommand );

    if( result <= 0 )
        return -kEncapErrorIncorrectData;

    // Check if ConnectedAddressItem received, otherwise it is no connected
    // message and should not be here
    if( AddrType() != kCpfIdConnectedAddress )
    {
        CIPSTER_TRACE_ERR(
                "notifyConnectedCPF: got something besides the expected CIP_ITEM_ID_NULL\n" );
        return -kEncapErrorIncorrectData;
    }

    // ConnectedAddressItem item
    CipConn* conn = GetConnectionByConsumingId( address_item.connection_identifier );

    if( conn )
    {
        // reset the watchdog timer
        conn->SetInactivityWatchDogTimerUSecs( conn->RxTimeoutUSecs() );

        // TODO check connection id  and sequence count
        if( DataType() == kCpfIdConnectedDataItem )
        {
            // connected data item received
            BufReader   command( DataItemPayload() );

            address_item.encap_sequence_number = command.get16();

            CipMessageRouterResponse response( this );  // give Cpf to response
            CipMessageRouterRequest  request;

            // command is advanced by 2 here because of above get16().
            int consumed = request.DeserializeMRReq( command );

            if( consumed <= 0 )
            {
                CIPSTER_TRACE_ERR( "%s: error from DeserializeMRReq()\n", __func__ );
                response.SetGenStatus( kCipErrorPathSegmentError );
            }
            else
            {
                EipStatus s = CipMessageRouterClass::NotifyMR( &request, &response );

                if( s == kEipStatusError )
                    return -kEncapErrorIncorrectData;

                address_item.connection_identifier = conn->ProducingConnectionId();
            }

            SetPayload( &response );

            result = Serialize( aReply );  // this Cpf
        }
        else
        {
            // wrong data item detected
            CIPSTER_TRACE_ERR(
                    "%s: got DataItemType()=%d instead of expected kCpfIdConnectedDataItem\n",
                    __func__, DataType() );

            return -kEncapErrorIncorrectData;
        }
    }
    else
    {
        CIPSTER_TRACE_ERR(
                "%s: CID:0x%08x could not be found\n",
                __func__,
                address_item.connection_identifier );

        return -kEncapErrorIncorrectData;
    }

    return result;
}


int Cpf::DeserializeCpf( BufReader aSrc )
{
    BufReader in = aSrc;

    Clear();
    try
    {
        int received_item_count = (uint16_t) in.get16();

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
                SetDataRange( ByteBuf( (uint8_t*) in.data(), length ) );
                in += length;               // might throw exception
                break;

            case kCpfIdSockAddrInfo_O_T:
            case kCpfIdSockAddrInfo_T_O:
                {
                    if( length == 16 )
                    {
                        SockAddr saii;

                        in += deserialize_sockaddr( &saii, in );
                        AddRx( SockAddrId( type_id ), saii );
                    }
                    else
                        goto error;
                }
                break;

            default:
                // Vol 2 Table 2-6.10 says reply with 0x0003 in encap status.
                // Leave item_count at zero.
                goto error;
            }
        }
    }
    catch( const std::runtime_error& )
    {
        CIPSTER_TRACE_ERR( "%s: bad CPF format\n", __func__ );
        return -1;
    }
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
        ;   // maybe no address
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

    for( int type = kSockAddr_O_T;  type <= kSockAddr_T_O;  ++type )
    {
        if( SaiiTx( SockAddrId( type ) ) )
        {
            count += 20;
        }
    }

    return count;
}


int Cpf::Serialize( BufWriter aDst, int aCtl ) const
{
    BufWriter   out = aDst;
    int         item_count = HasAddr() + HasData() + HasTx_O_T() + HasTx_T_O();

    out.put16( item_count );

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
        ;   // maybe no address
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

    // Do O_T before T_O

    if( const SockAddr* saii = SaiiTx( kSockAddr_O_T ) )
    {
        out.put16( kSockAddr_O_T ).put16( 16 );
        out += serialize_sockaddr( *saii, out );
    }

    if( const SockAddr* saii = SaiiTx( kSockAddr_T_O ) )
    {
        out.put16( kSockAddr_T_O ).put16( 16 );
        out += serialize_sockaddr( *saii, out );
    }

    return out.data() - aDst.data();
}

int Cpf::serialize_sockaddr( const SockAddr& aSockAddr, BufWriter aOutput )
{
    BufWriter out = aOutput;

    out.put16BE( aSockAddr.Family() )
    .put16BE( aSockAddr.Port() )
    .put32BE( aSockAddr.Addr() )

    .fill( 8 );     // ignore sin_zero

    return out.data() - aOutput.data();
}

int Cpf::deserialize_sockaddr( SockAddr* aSockAddr, BufReader aInput )
{
    BufReader in = aInput;

    aSockAddr->SetFamily( in.get16BE() );
    aSockAddr->SetPort( in.get16BE() );
    aSockAddr->SetAddr( in.get32BE() );

    in += 8;        // ignore 8 bytes of sin_zero

    return in.data() - aInput.data();
}
