/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corporation.
 *
 ******************************************************************************/
#include <string.h>

#include <byte_bufs.h>
#include <trace.h>
#include <cipster_api.h>
#include <cipster_user_conf.h>

#include "cipconnectionmanager.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "ciperror.h"
#include "cipconnection.h"
#include "cipassembly.h"
#include "appcontype.h"
#include "../enet_encap/encap.h"
//#include "../enet_encap/networkhandler.h"   // IpAddrStr()
#include "../enet_encap/encap.h"
#include "../enet_encap/cpf.h"


/// List holding all currently active connections
CipConnBox g_active_conns;


CipConn* CipConnMgrClass::FindExistingMatchingConnection( const ConnectionData& params )
{
    for( CipConnBox::iterator active = g_active_conns.begin();
            active != g_active_conns.end();  ++active )
    {
        if( active->State() == kConnStateEstablished )
        {
            if( params.TriadEquals( *active ) )
            {
                return active;
            }
        }
    }

    return NULL;
}


EipStatus CipConnMgrClass::RecvConnectedData( UdpSocket* aSocket,
        const SockAddr& aFromAddress, BufReader aCommand )
{
    Cpf cpfd( aFromAddress, 0 );
    int result;

    try
    {
        result = cpfd.DeserializeCpf( aCommand );
    }
    catch( const std::exception& e )
    {
        // This can happen if the item_count in DeserializeCpf() is erroneous.
        CIPSTER_TRACE_ERR( "%s[%d]: exception: %s in DeserializeCpf()\n",
            __func__, aSocket->h(), e.what()
            );
        return kEipStatusError;
    }

    if( result <= 0 )
    {
        CIPSTER_TRACE_ERR( "%s[%d]: unable to DeserializeCpf()\n", __func__, aSocket->h() );
        return kEipStatusError;
    }

    // Check if connected address item or sequenced address item  received,
    // otherwise it is no connected message and should not be here.
    if( cpfd.AddrType() == kCpfIdConnectedAddress
     || cpfd.AddrType() == kCpfIdSequencedAddress )
    {
        // found connected address item or found sequenced address item
        // -> for now the sequence number will be ignored

        if( cpfd.DataType() == kCpfIdConnectedDataItem )
        {
            CIPSTER_TRACE_INFO(
                "%s[%d]@%u CID:0x%08x len:%-3zd src:%s:%d seq:%d\n",
                __func__, aSocket->h(), CurrentUSecs32(),
                cpfd.AddrConnId(),
                aCommand.size(),
                aFromAddress.AddrStr().c_str(),
                aFromAddress.Port(),
                cpfd.AddrEncapSeqNum()
                );

            CipConn* conn = GetConnectionByConsumingId( cpfd.AddrConnId() );

            if( !conn )
            {
                CIPSTER_TRACE_INFO( "%s[%d]: no existing connection for CID:0x%x\n",
                    __func__, aSocket->h(), cpfd.AddrConnId()
                    );
                return kEipStatusError;
            }

            /*
            CIPSTER_TRACE_INFO( "%s: got consuming connection for conn_id 0x%x\n",
                __func__, cpfd.AddrConnId()
                );

            CIPSTER_TRACE_INFO( "%s: recv_address:%s:%d  aFromAddress:%s:%d\n",
                __func__,
                IpAddrStr( conn->recv_address.sin_addr ).c_str(),
                ntohs( conn->recv_address.sin_port ),
                IpAddrStr( aFromAddress->sin_addr ).c_str(),
                ntohs( aFromAddress->sin_port )
                );
            */

            // Only handle the data if it is coming from the peer.  Note that
            // we do not test the port here, only the IP address.
            if( aFromAddress.Addr() != conn->recv_address.Addr() )
            {
                CIPSTER_TRACE_WARN(
                        "%s[%d]: I/O data received with wrong originator address.\n"
                        " from:%s  originator:%s for matching CID\n",
                        __func__,
                        aSocket->h(),
                        aFromAddress.AddrStr().c_str(),
                        conn->recv_address.AddrStr().c_str()
                        );

                return kEipStatusError;
            }

            /*
            CIPSTER_TRACE_INFO( "%s[%d]: CID:0x%08x  cpf.seq=0x%08x  encap.seq=0x%08x\n",
                __func__,
                aSocket->h(),
                conn->ConsumingConnectionId(),
                cpfd.AddrEncapSeqNum(),
                conn->eip_level_sequence_count_consuming
                );
            */

            // if this is the first received frame
            if( conn->eip_level_sequence_count_consuming_first )
            {
                // put our tracking count within a half cycle of the leader.  Without this
                // there are many scenarios where the SEQ_GT32 below won't evaluate as true.
                conn->eip_level_sequence_count_consuming = cpfd.AddrEncapSeqNum() - 1;
                conn->eip_level_sequence_count_consuming_first = false;
            }

            // Vol2 3-4.1:
            // inform assembly object iff the sequence counter is greater or equal
            if( SEQ_GT32( cpfd.AddrEncapSeqNum(),
                          conn->eip_level_sequence_count_consuming ) )
            {
                // reset the watchdog timer
                conn->SetInactivityWatchDogTimerUSecs( conn->RxTimeoutUSecs() );

                conn->eip_level_sequence_count_consuming = cpfd.AddrEncapSeqNum();

                return conn->HandleReceivedIoConnectionData( BufReader( cpfd.DataRange() ) );
            }
            else
            {
                CIPSTER_TRACE_INFO(
                    "%s[%d]: received encap_sequence number was not greater, ignoring frame\n"
                    " received:%08x   connection seqn:%08x\n",
                    __func__,
                    aSocket->h(),
                    cpfd.AddrEncapSeqNum(),
                    conn->eip_level_sequence_count_consuming
                    );
            }
        }
    }

    (void) aSocket;

    return kEipStatusOk;
}


EipStatus CipConnMgrClass::ManageConnections()
{
    EipStatus eip_status;

    // Check for application message triggers
    HandleApplication();

    ManageEncapsulationMessages();

    for( CipConnBox::iterator active = g_active_conns.begin();
            active != g_active_conns.end();  ++active )
    {
        if( active->State() == kConnStateEstablished )
        {
            // maybe check inactivity watchdog timer.
            if( active->HasInactivityWatchDogTimer() )
            {
                if( active->InactivityWatchDogTimerUSecs() <= 0 )
                {
                    // we have a timed out connection while performing watchdog check

                    if( active->trigger.Class() == kConnTransportClass3 )
                    {
                        CIPSTER_TRACE_INFO(
                            "%s<%d>: >>> c-class:%d timeOut@%u on session id:%d\n",
                            __func__,
                            active->instance_id,
                            active->trigger.Class(),
                            CurrentUSecs32(),
                            active->SessionHandle()
                            );
                    }
                    else
                    {
                        // If this shows -1 as socket values, its because the other end
                        // closed the transport and we closed it in response already.
                        CIPSTER_TRACE_INFO(
                            "%s<%d>: >>> c-class:%d timeOut@%u\n",
                            __func__,
                            active->instance_id,
                            active->trigger.Class(),
                            CurrentUSecs32()
                            );
                    }

                    active->timeOut();
                }
            }

            // only if the connection has not timed out check if data is to be sent
            if( active->State() == kConnStateEstablished )
            {
                // client connection, not server
                if( !active->trigger.IsServer()

                    && active->ExpectedPacketRateUSecs() != 0

                    // only produce for the master connection
                    && active->ProducingUdp() )
                {
                    if( active->TransmissionTriggerTimerUSecs() <= 0 ) // need to send packet
                    {
                        eip_status = active->SendConnectedData();

                        if( eip_status == kEipStatusError )
                        {
                            CIPSTER_TRACE_ERR( "%s<%d>: ERROR sending UDP\n",
                                __func__, active->instance_id );
                        }

                        active->BumpTransmissionTriggerTimerUSecs( active->ProducingRPI() );

                        if( active->trigger.Trigger() != kConnTriggerTypeCyclic )
                        {
                            // non cyclic connections have to reload the production inhibit timer
                            active->SetProductionInhibitTimerUSecs( active->GetPIT_USecs() );
                        }
                    }
                }
            }
        }
    }

    return kEipStatusOk;
}


void CipConnMgrClass::CheckForTimedOutConnectionsAndCloseTCPConnections( CipUdint aSessionHandle )
{
    bool another_active_with_same_session_found = false;

    for( CipConnBox::iterator it = g_active_conns.begin();
                it != g_active_conns.end();  ++it )
    {
        // This test assumes that caller first CipConn::Close()d the CIP connection
        // that timed out, so we do not have to exclude it from comparison here
        // because it is no longer in g_active_conns.
        if( it->SessionHandle() == aSessionHandle )
        {
            another_active_with_same_session_found = true;
            break;
        }
    }

    if( !another_active_with_same_session_found )
    {
        CIPSTER_TRACE_INFO( "%s: killing session:%d\n", __func__, aSessionHandle );
        SessionMgr::CloseBySessionHandle( aSessionHandle );
    }
}


void CipConnMgrClass::CloseClass3Connections( CipUdint aSessionHandle )
{
    CipConnBox::iterator it = g_active_conns.begin();

    while( it != g_active_conns.end() )
    {
        if( it->trigger.Class() == kConnTransportClass3 &&
            it->SessionHandle() == aSessionHandle )
        {
            CIPSTER_TRACE_INFO( "%s: closing class 3 on session:%d\n",
                __func__, aSessionHandle );

            CipConnBox::iterator to_close = it;

            ++it;
            to_close->Close();
        }
        else
            ++it;
    }
}


CipConn* GetConnectionByConsumingId( int aConnectionId )
{
    CipConnBox::iterator c = g_active_conns.begin();

    while( c != g_active_conns.end() )
    {
        if( c->State() == kConnStateEstablished )
        {
            if( c->ConsumingConnectionId() == aConnectionId )
            {
                return c;
            }
        }

        ++c;
    }

    return NULL;
}


CipConn* GetConnectedOutputAssembly( int output_assembly_id )
{
    CipConnBox::iterator active = g_active_conns.begin();

    for(  ; active != g_active_conns.end(); ++active )
    {
        if( active->State() == kConnStateEstablished )
        {
            if( active->ConsumingPath().GetInstanceOrConnPt() == output_assembly_id )
                return active;
        }
    }

    return NULL;
}


bool CipConnBox::Insert( CipConn* aConn )
{
    if( aConn->on_list )
    {
        CIPSTER_TRACE_WARN( "%s<%d>: called with aConn already on list\n",
            __func__, aConn->instance_id );
        return false;
    }

    if( aConn->trigger.Class() == kConnTransportClass1 )
    {
        //CIPSTER_TRACE_INFO( "%s: consuming_connection_id:%d\n", __func__, aConn->ConsumingConnectionId() );
    }

    aConn->prev = NULL;
    aConn->next = head;

    if( head )
    {
        head->prev = aConn;
    }

    head = aConn;
    aConn->on_list = true;

    return true;
}


bool CipConnBox::Remove( CipConn* aConn )
{
    if( !aConn->on_list )
    {
        CIPSTER_TRACE_WARN( "%s<%d>: called with aConn not on list\n",
            __func__, aConn->instance_id );
        return false;
    }

    if( aConn->trigger.Class() == kConnTransportClass1 )
    {
        //CIPSTER_TRACE_INFO( "%s: consuming_connection_id:%d\n", __func__, aConn->ConsumingConnectionId() );
    }

    if( aConn->prev )
    {
        aConn->prev->next = aConn->next;
    }
    else
    {
        head = aConn->next;
    }

    if( aConn->next )
    {
        aConn->next->prev = aConn->prev;
    }

    aConn->prev  = NULL;
    aConn->next  = NULL;
    aConn->on_list = false;
    return true;
}


bool IsConnectedInputAssembly( int aInstanceId )
{
    CipConnBox::iterator c = g_active_conns.begin();

    for(  ; c != g_active_conns.end();  ++c )
    {
        if( aInstanceId == c->ProducingPath().GetInstanceOrConnPt() )
            return true;
    }

    return false;
}


bool IsConnectedOutputAssembly( int aInstanceId )
{
    CipConnBox::iterator c = g_active_conns.begin();

    for( ; c != g_active_conns.end(); ++c )
    {
        if( aInstanceId == c->ConsumingPath().GetInstanceOrConnPt() )
            return true;
    }

    return false;
}


EipStatus TriggerConnections( int aOutputAssembly, int aInputAssembly )
{
    EipStatus ret = kEipStatusError;

    CipConnBox::iterator c = g_active_conns.begin();

    for(  ; c != g_active_conns.end(); ++c )
    {
        if( aOutputAssembly == c->ConsumingPath().GetInstanceOrConnPt()
         && aInputAssembly  == c->ProducingPath().GetInstanceOrConnPt() )
        {
            if( c->Transport().Trigger() == kConnTriggerTypeApplication )
            {
                // produce at the next allowed occurrence
                c->SetTransmissionTriggerTimerUSecs( c->ProductionInhibitTimerUSecs() );
                ret = kEipStatusOk;
            }

            break;
        }
    }

    return ret;
}


EipStatus CipConnMgrClass::forward_open( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response, bool isLarge )
{
    (void) instance;        // suppress compiler warning

    // general and extended status.
    CipError        gen_status  = kCipErrorConnectionFailure;
    ConnMgrStatus   ext_status  = kConnMgrStatusSuccess;

    uint32_t        consuming_API_usecs = 0;
    uint32_t        producing_API_usecs = 0;

    unsigned        conn_path_byte_count;
    ConnectionData  params;

    BufReader  in = request->Data();

    try
    {
        in += params.DeserializeForwardOpenRequest( in, isLarge );
        conn_path_byte_count = in.get8() * 2;
    }
    catch( const std::range_error& /*e*/ )
    {
        // do not even send a reply, the params where not all supplied in the request.
        return kEipStatusError;
    }
    catch( const std::runtime_error& /*e*/ )
    {
        // currently cannot happen except under Murphy's law.
        return kEipStatusError;
    }

    // first check if we have already a connection with the given params
    if( FindExistingMatchingConnection( params ) )
    {
        ext_status = kConnMgrStatusConnectionInUse;
        goto forward_open_response;
    }

    if( params.connection_timeout_multiplier_value > 7 )
    {
        // Vol1 3-5.4.1.4
        //CIPSTER_TRACE_INFO( "%s: invalid connection timeout multiplier: %u\n", __func__, params.connection_timeout_multiplier_value );

        ext_status = kConnMgrStatusConnectionTimeoutMultiplierNotAcceptable;
        goto forward_open_response;
    }

    CIPSTER_TRACE_INFO(
        "%s: ConnSerNo:%x VendorId:%x OriginatorSerNum:%x CID:0x%08x PID:0x%08x\n",
        __func__,
        params.connection_serial_number,
        params.originator_vendor_id,
        params.originator_serial_number,
        params.ConsumingConnectionId(),
        params.ProducingConnectionId()
        );

    if( params.consuming_ncp.ConnectionType() == kIOConnTypeInvalid )
    {
        ext_status = kConnMgrStatusInvalidOToTConnectionType;
        goto forward_open_response;
    }

    if( params.producing_ncp.ConnectionType() == kIOConnTypeInvalid )
    {
        ext_status = kConnMgrStatusInvalidTToOConnectionType;
        goto forward_open_response;
    }

    // check for undocumented trigger bits
    if( 0x4c & params.trigger.Bits() )
    {
        //CIPSTER_TRACE_INFO( "%s: trigger 0x%02x not supported\n", __func__, params.trigger.Bits() );

        ext_status = kConnMgrStatusTransportTriggerNotSupported;
        goto forward_open_response;
    }

    // Vol1 3-5.4.1.2  Requested and Actual Packet Intervals
    // The actual packet interval parameter needs to be a multiple of
    // kCIPsterTimerTickInMicroSeconds from the user's header file

    consuming_API_usecs = params.consuming_RPI_usecs;

    if( consuming_API_usecs % kCIPsterTimerTickInMicroSeconds )
    {
        // find next "faster" multiple
        consuming_API_usecs = (consuming_API_usecs / kCIPsterTimerTickInMicroSeconds)
            * kCIPsterTimerTickInMicroSeconds;

        if( consuming_API_usecs == 0 )
        {
            CIPSTER_TRACE_ERR(
                "%s: consuming_RPI of %d is less than minimum of %d usecs\n",
                __func__,  params.consuming_RPI_usecs, kCIPsterTimerTickInMicroSeconds );

            ext_status = kConnMgrStatusRPINotSupported;
            goto forward_open_response;
        }

        if( consuming_API_usecs != params.consuming_RPI_usecs )
        {
            CIPSTER_TRACE_INFO(
                "%s: consuming_RPI was adjusted downward to %d usecs\n",
                __func__, consuming_API_usecs );

            params.consuming_RPI_usecs = consuming_API_usecs;
        }
    }

    producing_API_usecs = params.producing_RPI_usecs;

    if( producing_API_usecs % kCIPsterTimerTickInMicroSeconds )
    {
        // find next "faster" multiple
        producing_API_usecs = (producing_API_usecs / kCIPsterTimerTickInMicroSeconds)
            * kCIPsterTimerTickInMicroSeconds;

        if( producing_API_usecs == 0 )
        {
            CIPSTER_TRACE_ERR(
                "%s: producing_RPI of %d is less than minimum of %d usecs\n",
                __func__,  params.producing_RPI_usecs, kCIPsterTimerTickInMicroSeconds );

            ext_status = kConnMgrStatusRPINotSupported;
            goto forward_open_response;
        }

        if( producing_API_usecs != params.producing_RPI_usecs )
        {
            CIPSTER_TRACE_INFO(
                "%s: producing_RPI was adjusted downward to %d usecs\n",
                __func__, producing_API_usecs );

            params.producing_RPI_usecs = producing_API_usecs;
        }
    }

    if( conn_path_byte_count < in.size() )
    {
        gen_status = kCipErrorTooMuchData;
        goto forward_open_response;
    }

    if( conn_path_byte_count > in.size() )
    {
        gen_status = kCipErrorNotEnoughData;
        goto forward_open_response;
    }

    // At this point "in" has the exact correct BufReader::size()
    // for the connection path in bytes.

    try
    {
        in += params.DeserializeConnectionPath( in );
    }
    catch( const std::exception& ex )
    {
        CIPSTER_TRACE_INFO( "%s: %s\n", __func__, ex.what() );
        goto forward_open_response;
    }

    // electronic key?
    if( params.conn_path.port_segs.HasKey() )
    {
        ext_status = params.conn_path.port_segs.Key().Check();

        if( ext_status != kConnMgrStatusSuccess )
        {
            CIPSTER_TRACE_ERR( "%s: checkElectronicKeyData failed\n", __func__ );
            goto forward_open_response;
        }
    }

    gen_status = params.ResolveInstances( &ext_status );
    if( gen_status != kCipErrorSuccess )
        goto forward_open_response;

    CIPSTER_TRACE_INFO( "%s: trigger_class:%d\n", __func__, params.trigger.Class() );

    CIPSTER_TRACE_INFO( "%s: o_t RPI_usecs:%u\n", __func__, params.consuming_RPI_usecs );
    CIPSTER_TRACE_INFO( "%s: o_t size:%d\n", __func__, params.consuming_ncp.ConnectionSize() );
    CIPSTER_TRACE_INFO( "%s: o_t priority:%d\n", __func__, params.consuming_ncp.Priority() );
    CIPSTER_TRACE_INFO( "%s: o_t type:%s\n", __func__, params.consuming_ncp.ShowConnectionType() );

    CIPSTER_TRACE_INFO( "%s: t_o RPI_usecs:%u\n", __func__, params.producing_RPI_usecs );
    CIPSTER_TRACE_INFO( "%s: t_o size:%d\n", __func__, params.producing_ncp.ConnectionSize() );
    CIPSTER_TRACE_INFO( "%s: t_o priority:%d\n", __func__, params.producing_ncp.Priority() );
    CIPSTER_TRACE_INFO( "%s: t_o type:%s\n", __func__, params.producing_ncp.ShowConnectionType() );

    CipClass* clazz;
    clazz = GetCipClass( params.mgmnt_class );

    gen_status = clazz->OpenConnection( &params, response->CPF(), &ext_status );

    if( gen_status != kCipErrorSuccess )
    {
        //CIPSTER_TRACE_INFO( "%s: OpenConnection() failed. ext_status:0x%x\n", __func__, ext_status );

        goto forward_open_response;
    }

    CIPSTER_TRACE_INFO( "%s: OpenConnection() succeeded\n", __func__ );

forward_open_response:

    BufWriter out = response->Writer();

    if( gen_status == kCipErrorSuccess )
    {
        CIPSTER_TRACE_INFO( "%s: sending success response\n", __func__ );

        out.put32( params.ConsumingConnectionId() );
        out.put32( params.ProducingConnectionId() );
    }
    else
    {
        CIPSTER_TRACE_INFO(
            "%s: sending error response\n"
            " gen_status:0x%x\n"
            " ext_status:0x%x (%s)\n",
            __func__,
            gen_status,
            ext_status,
            ExtStatusStr( ext_status )
            );

        response->SetGenStatus( gen_status );

        switch( gen_status )
        {
        case kCipErrorNotEnoughData:
        case kCipErrorTooMuchData:
            break;

        default:
            switch( ext_status )
            {
            case kConnMgrStatusInvalidOToTConnectionSize:
                response->AddAdditionalSts( ext_status );
                response->AddAdditionalSts( params.corrected_consuming_size );
                break;

            case kConnMgrStatusInvalidTToOConnectionSize:
                response->AddAdditionalSts( ext_status );
                response->AddAdditionalSts( params.corrected_producing_size );
                break;

            default:
                response->AddAdditionalSts( ext_status );
                break;
            }
            break;
        }
    }

    out.put16( params.connection_serial_number );
    out.put16( params.originator_vendor_id );
    out.put32( params.originator_serial_number );

    if( gen_status == kCipErrorSuccess )
    {
        out.put32( consuming_API_usecs );
        out.put32( producing_API_usecs );

        out.put8( 0 );   // Application Reply Size
    }
    else
    {
        // Vol1 Table 3-5.20 Unsuccessful Forward_Open Response

        // Remaining Path Size: "The number of words in the
        // Connection_Path parameter of the request as received
        // by the router that detects the error."

        // In the failure response, the remaining remaining_path_size shall be
        // the “pre-stripped” size. This shall be the size of the path when the
        // node first receives the request and has not yet started processing
        // it. A target node may return either the “pre-stripped” size or 0 for
        // the remaining remaining_path_size.

        out.put8( 0 );
    }

    out.put8( 0 );   // reserved
    response->SetWrittenSize( out.data() - response->Writer().data() );
    return kEipStatusOkSend;
}


EipStatus CipConnMgrClass::forward_open_service( CipInstance* instance,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    return forward_open( instance, request, response, false );
}


EipStatus CipConnMgrClass::large_forward_open_service( CipInstance* instance,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    return forward_open( instance, request, response, true );
}


EipStatus CipConnMgrClass::forward_close_service( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    // Suppress compiler warning
    (void) instance;

    // general and extended status.
    CipError        gen_status  = kCipErrorConnectionFailure;
    ConnMgrStatus   ext_status  = kConnMgrStatusSuccess;

    unsigned        conn_path_byte_count;
    ConnectionData  params;

    BufReader  in = request->Data();

    try
    {
        in += params.DeserializeForwardCloseRequest( in );
        conn_path_byte_count = in.get8() * 2;

        ++in;   // skip "reserved" byte.  Note: forward_open does not have this.
    }
    catch( const std::range_error& /*e*/ )
    {
        // do not even send a reply, the params where not all supplied in the request.
        return kEipStatusError;
    }
    catch( const std::runtime_error& /*e*/ )
    {
        // currently cannot happen except under Murphy's law.
        return kEipStatusError;
    }
    catch( const std::exception& /*e*/ )
    {
        return kEipStatusError;
    }

    if( conn_path_byte_count < in.size() )
    {
        gen_status = kCipErrorTooMuchData;
        goto forward_close_response;
    }

    if( conn_path_byte_count > in.size() )
    {
        gen_status = kCipErrorNotEnoughData;
        goto forward_close_response;
    }

#if 0   // spec says this is optional
    try
    {
        in += params.DeserializeConnectionPath( in );
    }
    catch( const std::runtime_error& ex )
    {
        CIPSTER_TRACE_INFO( "%s: unable to parse connection path\n", __func__ );
        goto forward_close_reponse;
    }
#endif

    CIPSTER_TRACE_INFO(
        "%s: ConnSerNo:%x VendorId:%x OriginatorSerNum:%x\n",
        __func__,
        params.connection_serial_number,
        params.originator_vendor_id,
        params.originator_serial_number
        );

    CipConn* match;
    match = FindExistingMatchingConnection( params );

    if( !match )
    {
        ext_status =  kConnMgrStatusConnectionNotFoundAtTargetApplication;

        CIPSTER_TRACE_INFO( "%s: no match\n", __func__ );
    }
    else
    {
        CIPSTER_ASSERT( response->CPF()->TcpPeerAddr() );

        if( response->CPF()->TcpPeerAddr()->Addr() != match->openers_address.Addr() )
        {
            // Vol2 3-3.10 Forward_Close
            gen_status = kCipErrorPrivilegeViolation;
            goto forward_close_response;
        }

        CIPSTER_TRACE_INFO( "%s: CID:0x%08x PID:0x%08x\n",
            __func__,
            match->ConsumingConnectionId(),
            match->ProducingConnectionId() );

        match->Close();
        gen_status = kCipErrorSuccess;
    }

forward_close_response:

    BufWriter out = response->Writer();

    out.put16( params.connection_serial_number );
    out.put16( params.originator_vendor_id );
    out.put32( params.originator_serial_number );

    if( gen_status == kCipErrorSuccess )
    {
        // response has general and extended status already set to no errors.

        // Vol1 Table 3-5.22
        out.put8( 0 );      // application data word count
        out.put8( 0 );      // reserved
    }
    else
    {
        // Vol1 Table 3-5.23
        response->SetGenStatus( gen_status );

        if( ext_status != kConnMgrStatusSuccess )
            response->AddAdditionalSts( ext_status );

        out.put8( 0 );      // out.put8( connection_path_size );
        out.put8( 0 );      // reserved
    }

    (void) conn_path_byte_count;

    response->SetWrittenSize( out.data() - response->Writer().data() );

    return kEipStatusOkSend;
}


CipConnMgrClass::CipConnMgrClass() :
    CipClass( kCipConnectionManagerClass,
        "Connection Manager",
        MASK5( 1,2,3,6,7 ),     // common class attributes
        1                       // revision
        )
{
    // There are no attributes in instance of this class yet, so nothing to set.
    delete ServiceRemove( _I, kSetAttributeSingle );

    ServiceInsert( _I, kForwardOpen,        forward_open_service,       "ForwardOpen" );
    ServiceInsert( _I, kLargeForwardOpen,   large_forward_open_service, "LargeForwardOpen" );
    ServiceInsert( _I, kForwardClose,       forward_close_service,      "ForwardClose" );

    // Vol1 Table 3-5.4 limits what GetAttributeAll returns, but I want to support
    // attribute 3 also, so remove 3 from the auto generated
    // (via CipInstance::AttributeInsert()) bitmap.
    inst_getable_all_mask = MASK4( 1,2,6,7 );
}


CipInstance* CipConnMgrClass::CreateInstance( int aInstanceId )
{
    CipInstance* i = new CipInstance( aInstanceId );

    if( !InstanceInsert( i ) )
    {
        delete i;
        i = NULL;
    }

    return i;
}


EipStatus ConnectionManagerInit()
{
    if( !GetCipClass( kCipConnectionManagerClass ) )
    {
        CipConnMgrClass* clazz = new CipConnMgrClass();

        RegisterCipClass( clazz );

        // add only one instance
        clazz->CreateInstance( clazz->Instances().size() + 1 );;
    }

    return kEipStatusOk;
}
