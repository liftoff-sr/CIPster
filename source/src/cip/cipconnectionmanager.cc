/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/
#include <string.h>

#include "cipconnectionmanager.h"

#include "cipster_user_conf.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "ciperror.h"
#include "byte_bufs.h"
#include "cipster_api.h"
#include "encap.h"
#include "trace.h"
#include "cipconnection.h"
#include "cipassembly.h"
#include "cpf.h"
#include "appcontype.h"
#include "encap.h"


/// List holding all currently active connections
CipConnBox g_active_conns;


/**
 * Function findExsitingMatchingConnection
 * finds an existing matching established connection.
 *
 * The comparison is done according to the definitions in the CIP specification Section 3-5.5.2:
 * The following elements have to be equal: Vendor ID, Connection Serial Number, Originator Serial Number
 *
 * @param aConn connection instance containing the comparison elements from the forward open request
 *
 * @return CipConn*
 *    - NULL if no equal established connection exists
 *    - pointer to the equal connection object
 */
static CipConn* findExistingMatchingConnection( const ConnectionData& params )
{
    CipConnBox::iterator active = g_active_conns.begin();

    for(  ; active != g_active_conns.end();  ++active )
    {
        if( active->State() == kConnectionStateEstablished )
        {
            if( params.connection_serial_number == active->connection_serial_number
             && params.originator_vendor_id     == active->originator_vendor_id
             && params.originator_serial_number == active->originator_serial_number )
            {
                return active;
            }
        }
    }

    return NULL;
}


EipStatus CipConnMgrClass::HandleReceivedConnectedData(
        const sockaddr_in* from_address, BufReader aCommand )
{
    CIPSTER_TRACE_INFO( "%s:\n", __func__ );

    Cpf cpfd;

    if( cpfd.DeserializeCpf( aCommand ) <= 0 )
    {
        return kEipStatusError;
    }

    // Check if connected address item or sequenced address item  received,
    // otherwise it is no connected message and should not be here.
    if( cpfd.AddrType() == kCpfIdConnectedAddress
     || cpfd.AddrType() == kCpfIdSequencedAddress )
    {
        // found connected address item or found sequenced address item
        // -> for now the sequence number will be ignored

        if( cpfd.DataType() == kCpfIdConnectedDataItem ) // connected data item received
        {
            CipConn* conn = GetConnectionByConsumingId( cpfd.AddrConnId() );

            if( !conn )
            {
                CIPSTER_TRACE_INFO( "%s: no consuming connection for conn_id 0x%x\n",
                    __func__, cpfd.AddrConnId()
                    );
                return kEipStatusError;
            }

            CIPSTER_TRACE_INFO( "%s: got consuming connection for conn_id 0x%x\n",
                __func__, cpfd.AddrConnId()
                );

            CIPSTER_TRACE_INFO( "%s: c.s_addr=%08x  f.s_addr=%08x\n",
                __func__,
                (unsigned) conn->originator_address.sin_addr.s_addr,
                (unsigned) from_address->sin_addr.s_addr
                );

            // only handle the data if it is coming from the originator
            if( conn->originator_address.sin_addr.s_addr == from_address->sin_addr.s_addr )
            {
                CIPSTER_TRACE_INFO( "%s: g.sn=0x%08x  c.sn=0x%08x\n",
                    __func__,
                    cpfd.AddrEncapSeqNum(),
                    conn->eip_level_sequence_count_consuming
                    );

                // if this is the first received frame
                if( conn->eip_level_sequence_count_consuming_first )
                {
                    // put our tracking count within a half cycle of the leader.  Without this
                    // there are many scenarios where the SEQ_GT32 below won't evaluate as true.
                    conn->eip_level_sequence_count_consuming = cpfd.AddrEncapSeqNum() - 1;
                    conn->eip_level_sequence_count_consuming_first = false;
                }

                // only inform assembly object if the sequence counter is greater or equal, or
                if( SEQ_GT32( cpfd.AddrEncapSeqNum(),
                              conn->eip_level_sequence_count_consuming ) )
                {
                    // reset the watchdog timer
                    conn->SetInactivityWatchDogTimerUSecs( conn->TimeoutUSecs() );

                    conn->eip_level_sequence_count_consuming = cpfd.AddrEncapSeqNum();

                    return conn->HandleReceivedIoConnectionData( BufReader( cpfd.DataRange() ) );
                }
                else
                {
                    CIPSTER_TRACE_INFO( "%s: received sequence number was not greater, no watchdog reset\n"
                        " received:%08x   connection seqn:%08x\n",
                        __func__,
                        cpfd.AddrEncapSeqNum(),
                        conn->eip_level_sequence_count_consuming
                        );
                }
            }
            else
            {
                CIPSTER_TRACE_WARN(
                        "%s: connected data received with wrong originator address.\n"
                        " from:%08x   connection originator:%08x\n",
                        __func__,
                        (unsigned) from_address->sin_addr.s_addr,
                        (unsigned) conn->originator_address.sin_addr.s_addr
                        );
            }
        }
    }

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
        if( active->State() == kConnectionStateEstablished )
        {
            // maybe check inactivity watchdog timer.
            if( active->HasInactivityWatchDogTimer() )
            {
                active->AddToInactivityWatchDogTimerUSecs( -kOpenerTimerTickInMicroSeconds );

                if( active->InactivityWatchDogTimerUSecs() <= 0 )
                {
                    // we have a timed out connection while performing watchdog check
                    // If this shows -1 as socket values, its because the other end
                    // closed the transport and we closed it in response already.
                    CIPSTER_TRACE_INFO(
                        "%s: >>> Connection class: %d timed out consuming_socket:%d producing_socket:%d\n",
                        __func__,
                        active->trigger.Class(),
                        active->ConsumingSocket(),
                        active->ProducingSocket()
                        );

                    active->timeOut();
                }
            }

            // only if the connection has not timed out check if data is to be sent
            if( active->State() == kConnectionStateEstablished )
            {
                // client connection, not server
                if( !active->trigger.IsServer()

                    && active->ExpectedPacketRateUSecs() != 0

                    // only produce for the master connection
                    && active->ProducingSocket() != kEipInvalidSocket )
                {
                    if( active->trigger.Trigger() != kConnectionTriggerTypeCyclic )
                    {
                        // non cyclic connections have to decrement production inhibit timer
                        if( active->production_inhibit_timer_usecs >= 0 )
                        {
                            active->production_inhibit_timer_usecs -= kOpenerTimerTickInMicroSeconds;
                        }
                    }

                    active->transmission_trigger_timer_usecs -= kOpenerTimerTickInMicroSeconds;

                    if( active->transmission_trigger_timer_usecs <= 0 ) // need to send package
                    {
                        eip_status = active->SendConnectedData();

                        if( eip_status == kEipStatusError )
                        {
                            CIPSTER_TRACE_ERR( "sending of UDP data in manage Connection failed\n" );
                        }

                        // reload the timer value
                        active->transmission_trigger_timer_usecs = active->ExpectedPacketRateUSecs();

                        if( active->trigger.Trigger() != kConnectionTriggerTypeCyclic )
                        {
                            // non cyclic connections have to reload the production inhibit timer
                            active->production_inhibit_timer_usecs = active->GetPIT_USecs();
                        }
                    }
                }
            }
        }
    }

    return kEipStatusOk;
}


void CipConnMgrClass::assembleForwardOpenResponse( ConnectionData* aParams,
        CipMessageRouterResponse* response, CipError general_status,
        ConnectionManagerStatusCode extended_status )
{
    //Cpf cpfd( kCpfIdNullAddress, kCpfIdUnconnectedDataItem );

    BufWriter out = response->Writer();

    response->SetGenStatus( general_status );

    CIPSTER_ASSERT( response->AdditionalStsCount() == 0 );

    if( general_status == kCipErrorSuccess )
    {
        CIPSTER_TRACE_INFO( "%s: sending success response\n", __func__ );

        out.put32( aParams->consuming_connection_id );
        out.put32( aParams->producing_connection_id );
    }
    else
    {
        CIPSTER_TRACE_INFO(
            "%s: sending error response, general_status:0x%x extended_status:0x%x\n",
            __func__,
            general_status,
            extended_status
            );

        switch( general_status )
        {
        case kCipErrorNotEnoughData:
        case kCipErrorTooMuchData:
            break;

        default:
            switch( extended_status )
            {
            case kConnectionManagerStatusCodeErrorInvalidOToTConnectionSize:
                response->AddAdditionalSts( extended_status );
                response->AddAdditionalSts( aParams->corrected_o_to_t_size );
                break;

            case kConnectionManagerStatusCodeErrorInvalidTToOConnectionSize:
                response->AddAdditionalSts( extended_status );
                response->AddAdditionalSts( aParams->corrected_t_to_o_size );
                break;

            default:
                response->AddAdditionalSts( extended_status );
                break;
            }
            break;
        }
    }

    out.put16( aParams->connection_serial_number );
    out.put16( aParams->originator_vendor_id );
    out.put32( aParams->originator_serial_number );

    if( general_status == kCipErrorSuccess )
    {
        // Set the actual packet rates to caller's unadjusted rates.
        // Vol1 3-5.4.1.2 & Vol1 3-5.4.3 are not clear enough here.
        out.put32( aParams->o_to_t_RPI_usecs );
        out.put32( aParams->t_to_o_RPI_usecs );
    }

    out.put8( 0 );   // remaining path size - for routing devices relevant
    out.put8( 0 );   // reserved

    response->SetWrittenSize( out.data() - response->Writer().data() );
}


CipConn* GetConnectionByConsumingId( int aConnectionId )
{
    CipConnBox::iterator c = g_active_conns.begin();

    while( c != g_active_conns.end() )
    {
        if( c->State() == kConnectionStateEstablished )
        {
            if( c->consuming_connection_id == aConnectionId )
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
        if( active->State() == kConnectionStateEstablished )
        {
            if( active->conn_path.consuming_path.GetInstanceOrConnPt() == output_assembly_id )
                return active;
        }
    }

    return NULL;
}


void CipConnBox::Insert( CipConn* aConn )
{
    if( aConn->trigger.Class() == kConnectionTransportClass1 )
    {
        //CIPSTER_TRACE_INFO( "%s: conn->consuming_connection_id:%d\n", __func__, aConn->consuming_connection_id );
    }

    aConn->prev = NULL;
    aConn->next = head;

    if( head )
    {
        head->prev = aConn;
    }

    head = aConn;

    aConn->SetState( kConnectionStateEstablished );
}


void CipConnBox::Remove( CipConn* aConn )
{
    if( aConn->trigger.Class() == kConnectionTransportClass1 )
    {
        //CIPSTER_TRACE_INFO( "%s: consuming_connection_id:%d\n", __func__, consuming_connection_id );
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
    aConn->SetState( kConnectionStateNonExistent );
}


bool IsConnectedInputAssembly( int aInstanceId )
{
    CipConn* c = g_active_conns.begin();

    for(  ; c != g_active_conns.end();  ++c )
    {
        if( aInstanceId == c->conn_path.producing_path.GetInstanceOrConnPt() )
            return true;
    }

    return false;
}


bool IsConnectedOutputAssembly( int aInstanceId )
{
    CipConnBox::iterator c = g_active_conns.begin();

    for( ; c != g_active_conns.end(); ++c )
    {
        if( aInstanceId == c->conn_path.consuming_path.GetInstanceOrConnPt() )
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
        if( aOutputAssembly == c->conn_path.consuming_path.GetInstanceOrConnPt()
         && aInputAssembly  == c->conn_path.producing_path.GetInstanceOrConnPt() )
        {
            if( c->trigger.Trigger() == kConnectionTriggerTypeApplication )
            {
                // produce at the next allowed occurrence
                c->SetTransmissionTriggerTimerUSecs( c->production_inhibit_timer_usecs );
                ret = kEipStatusOk;
            }

            break;
        }
    }

    return ret;
}


EipStatus CipConnMgrClass::forward_open_common( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response, bool isLarge )
{
    ConnectionManagerStatusCode connection_status = kConnectionManagerStatusCodeSuccess;

    BufReader in = request->Data();

    (void) instance;        // suppress compiler warning

    ConnectionData    params;

    try
    {
        in += params.DeserializeConnectionData( in, isLarge );
    }
    catch( const std::range_error& e )
    {
        // do not even send a reply, the params where not all supplied in the request.
        return kEipStatusError;
    }
    catch( const std::exception& e )
    {
        // currently cannot happen.
        return kEipStatusError;
    }

    // first check if we have already a connection with the given params
    if( findExistingMatchingConnection( params ) )
    {
        // TODO this test is  incorrect, see CIP spec 3-5.5.2 re: duplicate forward open
        // it should probably be testing the connection type fields
        // TODO think on how a reconfiguration request could be handled correctly.
        if( !params.consuming_connection_id && !params.producing_connection_id )
        {
            // TODO implement reconfiguration of connection

            CIPSTER_TRACE_ERR(
                    "this looks like a duplicate forward open -- I can't handle this yet,\n"
                    "sending a CIP_CON_MGR_ERROR_CONNECTION_IN_USE response\n" );
        }

        assembleForwardOpenResponse(
                &params, response,
                kCipErrorConnectionFailure,
                kConnectionManagerStatusCodeErrorConnectionInUse );

        return kEipStatusOkSend;
    }

    if( params.connection_timeout_multiplier_value > 7 )
    {
        // 3-5.4.1.4
       CIPSTER_TRACE_INFO( "%s: invalid connection timeout multiplier: %u\n",
           __func__, params.connection_timeout_multiplier_value );

        assembleForwardOpenResponse(
                &params, response,
                kCipErrorConnectionFailure,
                kConnectionManagerStatusCodeErrorInvalidOToTConnectionType );

        return kEipStatusOkSend;
    }

    CIPSTER_TRACE_INFO(
        "ForwardOpen : ConnSerNo %d ConConnID:0x%08x, ProdConnID:0x%08x\n",
        params.connection_serial_number,
        params.consuming_connection_id,
        params.producing_connection_id
        );

    if( params.o_to_t_ncp.ConnectionType() == kIOConnTypeInvalid )
    {
        CIPSTER_TRACE_INFO( "%s: invalid O to T connection type\n", __func__ );

        assembleForwardOpenResponse(
                &params, response,
                kCipErrorConnectionFailure,
                kConnectionManagerStatusCodeErrorInvalidOToTConnectionType );

        return kEipStatusOkSend;
    }

    if( params.t_to_o_ncp.ConnectionType() == kIOConnTypeInvalid )
    {
        CIPSTER_TRACE_INFO( "%s: invalid T to O connection type\n", __func__ );

        assembleForwardOpenResponse(
                &params, response,
                kCipErrorConnectionFailure,
                kConnectionManagerStatusCodeErrorInvalidTToOConnectionType );

        return kEipStatusOkSend;
    }

    // check for undocumented trigger bits
    if( 0x4c & params.trigger.Bits() )
    {
        CIPSTER_TRACE_INFO( "%s: trigger 0x%02x not supported\n",
            __func__, params.trigger.Bits() );

        assembleForwardOpenResponse(
                &params, response,
                kCipErrorConnectionFailure,
                kConnectionManagerStatusCodeErrorTransportTriggerNotSupported );

        return kEipStatusOkSend;
    }

    unsigned conn_path_byte_count = in.get8() * 2;

    if( conn_path_byte_count < in.size() )
    {
        assembleForwardOpenResponse( &params, response, kCipErrorTooMuchData, connection_status );
        return kEipStatusOkSend;
    }

    if( conn_path_byte_count > in.size() )
    {
        assembleForwardOpenResponse( &params, response, kCipErrorNotEnoughData, connection_status );
        return kEipStatusOkSend;
    }

    // At this point "in" has the exact correct size() for the connection path in bytes.

    CipError result = params.DeserializeConnectionPath( in, &connection_status );

    if( result != kCipErrorSuccess )
    {
        CIPSTER_TRACE_INFO( "%s: unable to parse connection path\n", __func__ );

        assembleForwardOpenResponse( &params, response, result, connection_status );
        return kEipStatusOkSend;
    }

    CIPSTER_TRACE_INFO( "%s: trigger_class:%d\n", __func__, params.trigger.Class() );

    CIPSTER_TRACE_INFO( "%s: o_to_t RPI_usecs:%u\n", __func__, params.o_to_t_RPI_usecs );
    CIPSTER_TRACE_INFO( "%s: o_to_t size:%d\n", __func__, params.o_to_t_ncp.ConnectionSize() );
    CIPSTER_TRACE_INFO( "%s: o_to_t priority:%d\n", __func__, params.o_to_t_ncp.Priority() );
    CIPSTER_TRACE_INFO( "%s: o_to_t type:%s\n", __func__, params.o_to_t_ncp.ShowConnectionType() );

    CIPSTER_TRACE_INFO( "%s: t_to_o RPI_usecs:%u\n", __func__, params.t_to_o_RPI_usecs );
    CIPSTER_TRACE_INFO( "%s: t_to_o size:%d\n", __func__, params.t_to_o_ncp.ConnectionSize() );
    CIPSTER_TRACE_INFO( "%s: t_to_o priority:%d\n", __func__, params.t_to_o_ncp.Priority() );
    CIPSTER_TRACE_INFO( "%s: t_to_o type:%s\n", __func__, params.t_to_o_ncp.ShowConnectionType() );

    CipClass* clazz = GetCipClass( params.mgmnt_class );

    result = clazz->OpenConnection( &params, response->CPF(), &connection_status );

    if( result != kCipErrorSuccess )
    {
        CIPSTER_TRACE_INFO( "%s: OpenConnection() failed. status:0x%x\n", __func__, connection_status );

        // in case of error the dummy contains all necessary information
        assembleForwardOpenResponse( &params, response, result, connection_status );
        return kEipStatusOkSend;
    }
    else
    {
        CIPSTER_TRACE_INFO( "%s: OpenConnection() succeeded\n", __func__ );

        // in case of success, g_active_conns holds to the new connection at begin()
        assembleForwardOpenResponse( g_active_conns.begin(),
                response, kCipErrorSuccess, kConnectionManagerStatusCodeSuccess );
        return kEipStatusOkSend;
    }
}


EipStatus CipConnMgrClass::forward_open_service( CipInstance* instance,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    return forward_open_common( instance, request, response, false );
}


EipStatus CipConnMgrClass::large_forward_open_service( CipInstance* instance,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    return forward_open_common( instance, request, response, true );
}


EipStatus CipConnMgrClass::forward_close_service( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    // Suppress compiler warning
    (void) instance;

    // check connection_serial_number && originator_vendor_id && originator_serial_number if connection is established
    ConnectionManagerStatusCode connection_status =
        kConnectionManagerStatusCodeErrorConnectionNotFoundAtTargetApplication;

    BufReader in = request->Data();

    in += 2;        // ignore Priority/Time_tick and Time-out_ticks

    CipUint     connection_serial_number = in.get16();
    CipUint     originator_vendor_id     = in.get16();
    CipUdint    originator_serial_number = in.get32();
    CipByte     connection_path_size     = in.get8();

    CIPSTER_TRACE_INFO( "ForwardClose: ConnSerNo %d\n", connection_serial_number );

    CipConnBox::iterator active = g_active_conns.begin();

    for( ; active != g_active_conns.end();  ++active )
    {
        // This check should not be necessary as only established connections
        // should be in the active connection list
        if( active->State() == kConnectionStateEstablished ||
            active->State() == kConnectionStateTimedOut )
        {
            if( active->connection_serial_number == connection_serial_number
             && active->originator_vendor_id     == originator_vendor_id
             && active->originator_serial_number == originator_serial_number )
            {
                // found the corresponding connection -> close it
                active->Close();
                connection_status = kConnectionManagerStatusCodeSuccess;
                break;
            }
        }
    }

    BufWriter out = response->Writer();

    out.put16( connection_serial_number );
    out.put16( originator_vendor_id );
    out.put32( originator_serial_number );

    if( connection_status == kConnectionManagerStatusCodeSuccess )
    {
        // Vol1 Table 3-5.22
        out.put8( 0 );         // application data word count
        out.put8( 0 );         // reserved
    }
    else
    {
        // Vol1 Table 3-5.23
        response->SetGenStatus( kCipErrorConnectionFailure );
        response->AddAdditionalSts( connection_status );

        out.put8( 0 );  // out.put8( connection_path_size );
        out.put8( 0 );         // reserved
    }

    (void) connection_path_size;

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
    delete ServiceRemove( kSetAttributeSingle );

    ServiceInsert( kForwardOpen, forward_open_service, "ForwardOpen" );
    ServiceInsert( kLargeForwardOpen, large_forward_open_service, "LargeForwardOpen" );

    ServiceInsert( kForwardClose, forward_close_service, "ForwardClose" );

    // Vol1 Table 3-5.4 limits what GetAttributeAll returns, but I want to support
    // attribute 3 also, so remove 3 from the auto generated
    // (via CipInstance::AttributeInsert()) bitmap.
    getable_all_mask = MASK4( 1,2,6,7 );

    InitializeIoConnectionData();
}


static CipInstance* createConnectionManagerInstance()
{
    CipClass* clazz = GetCipClass( kCipConnectionManagerClass );

    CipInstance* i = new CipInstance( clazz->Instances().size() + 1 );

    clazz->InstanceInsert( i );

    return i;
}


EipStatus ConnectionManagerInit()
{
    if( !GetCipClass( kCipConnectionManagerClass ) )
    {
        CipClass* clazz = new CipConnMgrClass();

        RegisterCipClass( clazz );

        // add one instance
        createConnectionManagerInstance();
    }

    return kEipStatusOk;
}
