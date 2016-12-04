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


/// Length in bytes of the forward open command specific data until the start
/// of the connection path
const int g_kForwardOpenHeaderLength = 36;


/// List holding all currently active connections
CipConn* g_active_connection_list;


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
static CipConn* findExistingMatchingConnection( CipConn* aConn )
{
    CipConn* active = g_active_connection_list;

    while( active )
    {
        if( active->state == kConnectionStateEstablished )
        {
            if( aConn->connection_serial_number == active->connection_serial_number
             && aConn->originator_vendor_id     == active->originator_vendor_id
             && aConn->originator_serial_number == active->originator_serial_number )
            {
                return active;
            }
        }

        active = active->next;
    }

    return NULL;
}


static CipInstance* check_path( const CipAppPath& aPath, ConnectionManagerStatusCode* extended_error, const char* aCaller )
{
    if( !aPath.IsSufficient() )
    {
        if( extended_error )
            *extended_error = kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;
        return NULL;
    }

    int class_id = aPath.GetClass();

    CipClass* clazz = GetCipClass( class_id );
    if( !clazz )
    {
        CIPSTER_TRACE_ERR( "%s: classid %d not found for %s\n", __func__, class_id, aCaller );

        if( class_id >= 0xc8 )   // reserved range of class ids
        {
            if( extended_error )
                *extended_error =  kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;
        }
        else
        {
            if( extended_error )
                *extended_error = kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
        }

        return NULL;
    }

    int instance_id = aPath.GetInstanceOrConnPt();

    CipInstance* inst = clazz->Instance( instance_id );
    if( !inst )
    {
        CIPSTER_TRACE_ERR( "%s: instance id %d not found for %s\n",
            __func__, instance_id, aCaller );

        // according to the test tool we should respond with this extended error code
        if( extended_error )
            *extended_error = kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;

        return NULL;
    }

    return inst;
}


/**
 * Function parseConnectionPath
 * parses the connection path of a forward open request.
 *
 * @param aPath just _past_ the word count of connection_path
 * @param extended_status where to put the extended error code in case of error
 *
 * @return CipError - indicating success of the parsing
 *    - kCipErrorSuccess on success
 *    - On an error the general status code to be put into the response
 */
CipError CipConn::parseConnectionPath( BufReader aPath, ConnectionManagerStatusCode* extended_error )
{
    int         result;
    BufReader   in = aPath;

    CipAppPath  app_path1;
    CipAppPath  app_path2;
    CipAppPath  app_path3;

    CipInstance* instance1;     // corresponds to app_path1
    CipInstance* instance2;     // corresponds to app_path2
    CipInstance* instance3;     // corresponds to app_path3

    // clear all CipAppPaths and later assign those seen below
    conn_path.Clear();

    config_instance    = 0;
    consuming_instance = 0;
    producing_instance = 0;

    if( in.size() )
    {
        result = conn_path.port_segs.DeserializePortSegmentGroup( in );

        if( result < 0 )
        {
            goto L_exit_invalid;
        }

        in += result;
    }

    // electronic key?
    if( conn_path.port_segs.HasKey() )
    {
        ConnectionManagerStatusCode sts = conn_path.port_segs.key.Check();

        if( sts != kConnectionManagerStatusCodeSuccess )
        {
            *extended_error = sts;
            CIPSTER_TRACE_ERR( "%s: checkElectronicKeyData failed\n", __func__ );
            goto L_exit_error;
        }
    }

    /*
       There can be 1-3 application_paths in a connection_path.  Depending on the
       O->T_connection_parameters and T->O_connection_parameters fields and the
       presence of a data segment, one or more encoded application paths shall
       be specified. In general, the application paths are in the order of
       Configuration path, Consumption path, and Production path. However, a
       single encoded path can be used when configuration, consumption, and/or
       production use the same path.  See table 3-5.13 of CIP Vol1.
    */

    if( in.size() )
    {
        result = app_path1.DeserializeAppPath( in );

        if( result < 0 )
        {
            goto L_exit_invalid;
        }

        in += result;
    }

    if( in.size() )
    {
        result = app_path2.DeserializeAppPath( in, &app_path1 );

        if( result < 0 )
        {
            goto L_exit_invalid;
        }

        in += result;
    }

    if( in.size() )
    {
        result = app_path3.DeserializeAppPath( in, &app_path2 );

        if( result < 0 )
        {
            goto L_exit_invalid;
        }

        in += result;
    }

    if( in.size() )
    {
        // There could be a data segment
        result = conn_path.data_seg.DeserializeDataSegment( in );
        in += result;
    }

    if( in.size() )   // should have consumed all of it by now, 3 app paths max
    {
        CIPSTER_TRACE_ERR( "%s: unknown extra segments in forward open connection path\n", __func__ );
        goto L_exit_invalid;
    }

    // We don't apply checking rules to the connection_path until done parsing it here.

    instance1 = check_path( app_path1, extended_error, "app_path1" );
    if( !instance1 )
    {
        goto L_exit_error;
    }

    mgmnt_class = app_path1.GetClass();

    IOConnType o_to_t;
    IOConnType t_to_o;

    o_to_t = o_to_t_ncp.ConnectionType();
    t_to_o = t_to_o_ncp.ConnectionType();

    int actual_app_path_count;

    actual_app_path_count = 1 + int( app_path2.HasAny() + app_path3.HasAny() );

    // This 'if else if' block is coded to look like table
    // 3-5.13; which should reduce risk of error
    if( o_to_t == kIOConnTypeNull && t_to_o == kIOConnTypeNull )
    {
        if( conn_path.data_seg.HasAny() )
        {
            // app_path1 is for configuration.
            conn_path.config_path = app_path1;
            config_instance = instance1;

            // In this context, it's Ok to ignore app_path2 and app_path3
            // if present, also reflected in actual_app_path_count.
        }
        else
        {
            // app_path1 is for pinging via a "not matching" connection.
            if( actual_app_path_count != 1 )
            {
                goto L_exit_invalid;
            }

            // app_path1 is for pinging, but connection needs to be non-matching and
            // app_path1 must be Identity instance 1.  Caller can check.  Save
            // app_path1 in consuming_path for ping handler elsewhere.
            conn_path.consuming_path = app_path1;
            consuming_instance = instance1;
        }
    }

    // Row 2
    else if( o_to_t != kIOConnTypeNull && t_to_o == kIOConnTypeNull )
    {
        if( conn_path.data_seg.HasAny() )
        {
            switch( actual_app_path_count )
            {
            case 1:
                // app_path1 is for both configuration and consumption
                conn_path.config_path    = app_path1;
                conn_path.consuming_path = app_path1;

                config_instance    = instance1;
                consuming_instance = instance1;
                break;

            case 2:
                instance2 = check_path( app_path2, NULL, "app_path2 O->T(non-null) T-O(null)" );
                if( !instance2 )
                {
                    *extended_error = kConnectionManagerStatusCodeInvalidConsumingApllicationPath;
                    goto L_exit_error;
                }

                // app_path1 is for configuration, app_path2 is for consumption
                conn_path.config_path    = app_path1;
                conn_path.consuming_path = app_path2;

                config_instance    = instance1;
                consuming_instance = instance2;
                break;
            case 3:
                goto L_exit_invalid;
            }
        }
        else
        {
            switch( actual_app_path_count )
            {
            case 1:
                // app_path1 is for consumption
                conn_path.consuming_path = app_path1;

                consuming_instance = instance1;
                break;
            case 2:
            case 3:
                goto L_exit_invalid;
            }
        }
    }

    // Row 3
    else if( o_to_t == kIOConnTypeNull && t_to_o != kIOConnTypeNull )
    {
        if( conn_path.data_seg.HasAny() )
        {
            switch( actual_app_path_count )
            {
            case 1:
                // app_path1 is for both configuration and production
                conn_path.config_path    = app_path1;
                conn_path.producing_path = app_path1;

                config_instance    = instance1;
                producing_instance = instance1;
                break;

            case 2:
                instance2 = check_path( app_path2, NULL, "app_path2 O->T(null) T-O(non-null)" );
                if( !instance2 )
                {
                    *extended_error = kConnectionManagerStatusCodeInvalidProducingApplicationPath;
                    goto L_exit_error;
                }

                // app_path1 is for configuration, app_path2 is for production
                conn_path.config_path    = app_path1;
                conn_path.producing_path = app_path2;

                config_instance    = instance1;
                producing_instance = instance2;
                break;

            case 3:
                goto L_exit_invalid;
            }
        }
        else
        {
            switch( actual_app_path_count )
            {
            case 1:
                // app_path1 is for production
                conn_path.producing_path = app_path1;
                producing_instance = instance1;
                break;

            case 2:
            case 3:
                goto L_exit_invalid;
            }
        }
    }

    // Row 4
    else if( o_to_t != kIOConnTypeNull && t_to_o != kIOConnTypeNull )
    {
        if( conn_path.data_seg.HasAny() )
        {
            switch( actual_app_path_count )
            {
            case 1:
                // app_path1 is for configuration, consumption, and production
                conn_path.config_path    = app_path1;
                conn_path.consuming_path = app_path1;
                conn_path.producing_path = app_path1;

                config_instance    = instance1;
                consuming_instance = instance1;
                producing_instance = instance1;
                break;

            case 2:
                instance2 = check_path( app_path2, NULL, "app_path2 O->T(non-null) T-O(non-null)" );
                if( !instance2 )
                {
                    *extended_error = kConnectionManagerStatusCodeInvalidConsumingApllicationPath;
                    goto L_exit_error;
                }

                // app_path1 is for configuration, app_path2 is for consumption & production
                conn_path.config_path    = app_path1;
                conn_path.consuming_path = app_path2;
                conn_path.producing_path = app_path2;

                config_instance    = instance1;
                consuming_instance = instance2;
                producing_instance = instance2;
                break;

            case 3:
                instance2 = check_path( app_path2, NULL, "app_path2 O->T(non-null) T-O(non-null)" );
                if( !instance2 )
                {
                    *extended_error = kConnectionManagerStatusCodeInvalidConsumingApllicationPath;
                    goto L_exit_error;
                }

                instance3 = check_path( app_path3, NULL, "app_path3 O->T(non-null) T-O(non-null)" );
                if( !instance3 )
                {
                    *extended_error = kConnectionManagerStatusCodeInvalidProducingApplicationPath;
                    goto L_exit_error;
                }

                // app_path1 is for configuration, app_path2 is for consumption, app_path3 is for production
                conn_path.config_path    = app_path1;
                conn_path.consuming_path = app_path2;
                conn_path.producing_path = app_path3;

                config_instance    = instance1;
                consuming_instance = instance2;
                producing_instance = instance3;
            }
        }
        else
        {
            switch( actual_app_path_count )
            {
            case 1:
                // app_path1 is for consumption and production
                conn_path.consuming_path = app_path1;
                conn_path.producing_path = app_path1;

                consuming_instance = instance1;
                producing_instance = instance1;
                break;

            case 2:
                // app_path1 is for consumption, app_path2 is for production
                instance2 = check_path( app_path2, NULL, "app_path2 O->T(non-null) T-O(non-null)" );
                if( !instance2 )
                {
                    *extended_error = kConnectionManagerStatusCodeInvalidProducingApplicationPath;
                    goto L_exit_error;
                }

                conn_path.consuming_path = app_path1;
                conn_path.producing_path = app_path2;

                consuming_instance = instance1;
                producing_instance = instance2;
                break;

            case 3:
                // first path is ignored, app_path2 is for consumption, app_path3 is for production
                instance2 = check_path( app_path2, NULL, "app_path2 O->T(non-null) T-O(non-null)" );
                if( !instance2 )
                {
                    *extended_error = kConnectionManagerStatusCodeInvalidConsumingApllicationPath;
                    goto L_exit_error;
                }

                instance3 = check_path( app_path3, NULL, "app_path3 O->T(non-null) T-O(non-null)" );
                if( !instance3 )
                {
                    *extended_error = kConnectionManagerStatusCodeInvalidProducingApplicationPath;
                    goto L_exit_error;
                }

                conn_path.consuming_path = app_path2;
                conn_path.producing_path = app_path3;

                consuming_instance = instance2;
                producing_instance = instance3;

                // Since we ignored app_path1, don't assume that class of app_path2 is same:
                mgmnt_class = app_path2.GetClass();
                break;
            }
        }
    }

    if( transport_trigger.Class() == kConnectionTransportClass3 )
    {
        // connection end point has to be the message router instance 1
        if( conn_path.consuming_path.GetClass() != kCipMessageRouterClassCode ||
            conn_path.consuming_path.GetInstanceOrConnPt() != 1 )
        {
            *extended_error = kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
            goto L_exit_error;
        }
    }

    CIPSTER_TRACE_INFO( "%s: forward_open conn_path: %s\n",
        __func__,
        conn_path.Format().c_str()
        );

    return kCipErrorSuccess;

L_exit_invalid:

    // Add CIPSTER_TRACE statements at specific goto sites above, not here.
    *extended_error = kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;

L_exit_error:
    return kCipErrorConnectionFailure;
}


EipStatus HandleReceivedConnectedData( const sockaddr_in* from_address, BufReader aCommand )
{
    CIPSTER_TRACE_INFO( "%s:\n", __func__ );

    CipCommonPacketFormatData cpfd;

    if( cpfd.DeserializeCPFD( aCommand ) == kEipStatusError )
    {
        return kEipStatusError;
    }
    else
    {
        // Check if connected address item or sequenced address item  received,
        // otherwise it is no connected message and should not be here.
        if( cpfd.address_item.type_id == kCipItemIdConnectionAddress
         || cpfd.address_item.type_id == kCipItemIdSequencedAddressItem )
        {
            // found connected address item or found sequenced address item
            // -> for now the sequence number will be ignored

            if( cpfd.data_item.type_id == kCipItemIdConnectedDataItem ) // connected data item received
            {
                CipConn* conn = GetConnectionByConsumingId( cpfd.address_item.data.connection_identifier );

                if( !conn )
                {
                    CIPSTER_TRACE_INFO( "%s: no consuming connection for conn_id %d\n",
                        __func__, cpfd.address_item.data.connection_identifier
                        );
                    return kEipStatusError;
                }

                CIPSTER_TRACE_INFO( "%s: got consuming connection for conn_id %d\n",
                    __func__, cpfd.address_item.data.connection_identifier
                    );

                CIPSTER_TRACE_INFO( "%s: c.s_addr=%08x  f.s_addr=%08x\n",
                    __func__,
                    conn->originator_address.sin_addr.s_addr,
                    from_address->sin_addr.s_addr
                    );

                // only handle the data if it is coming from the originator
                if( conn->originator_address.sin_addr.s_addr == from_address->sin_addr.s_addr )
                {
                    CIPSTER_TRACE_INFO( "%s: g.sn=0x%08x  c.sn=0x%08x\n",
                        __func__,
                        cpfd.address_item.data.sequence_number,
                        conn->eip_level_sequence_count_consuming
                        );

                    // if this is the first received frame
                    if( conn->eip_level_sequence_count_consuming_first )
                    {
                        // put our tracking count within a half cycle of the leader.  Without this
                        // there are many scenarios where the SEQ_GT32 below won't evaluate as true.
                        conn->eip_level_sequence_count_consuming = cpfd.address_item.data.sequence_number - 1;
                        conn->eip_level_sequence_count_consuming_first = false;
                    }

                    // only inform assembly object if the sequence counter is greater or equal, or
                    if( SEQ_GT32( cpfd.address_item.data.sequence_number,
                                  conn->eip_level_sequence_count_consuming ) )
                    {
                        // reset the watchdog timer
                        conn->inactivity_watchdog_timer_usecs =
                            conn->o_to_t_RPI_usecs << (2 + conn->connection_timeout_multiplier);

                        CIPSTER_TRACE_INFO( "%s: reset inactivity_watchdog_timer_usecs:%u\n",
                            __func__,
                            conn->inactivity_watchdog_timer_usecs );

                        conn->eip_level_sequence_count_consuming = cpfd.address_item.data.sequence_number;

                        if( conn->connection_receive_data_function )
                        {
                            return conn->connection_receive_data_function( conn,
                                BufReader( cpfd.data_item.data, cpfd.data_item.length ) );
                        }
                    }
                }
                else
                {
                    CIPSTER_TRACE_WARN(
                            "Connected Message Data Received with wrong address information\n" );
                }
            }
        }
    }

    return kEipStatusOk;
}


EipStatus ManageConnections()
{
    EipStatus eip_status;

    //Inform application that it can execute
    HandleApplication();
    ManageEncapsulationMessages();

    for( CipConn* active = g_active_connection_list;  active;  active = active->next )
    {
        if( active->state == kConnectionStateEstablished )
        {
            // We have a consuming connection check inactivity watchdog timer.
            if( active->consuming_instance ||

                // All server connections have to maintain an inactivity watchdog timer
                active->transport_trigger.IsServer() )
            {
                active->inactivity_watchdog_timer_usecs -= kOpenerTimerTickInMicroSeconds;

                if( active->inactivity_watchdog_timer_usecs <= 0 )
                {
                    // we have a timed out connection: perform watchdog check
                    CIPSTER_TRACE_INFO(
                        "%s: >>>>>Connection timed out consuming_socket:%d producing_socket:%d\n",
                        __func__,
                        active->consuming_socket,
                        active->producing_socket
                        );

                    CIPSTER_ASSERT( active->connection_timeout_function );

                    active->connection_timeout_function( active );
                }
            }

            // only if the connection has not timed out check if data is to be sent
            if( active->state == kConnectionStateEstablished )
            {
                // client connection
                if( active->GetExpectedPacketRateUSecs() != 0 &&

                    // only produce for the master connection
                    active->producing_socket != kEipInvalidSocket )
                {
                    if( active->transport_trigger.Trigger() != kConnectionTriggerTypeCyclic )
                    {
                        // non cyclic connections have to decrement production inhibit timer
                        if( 0 <= active->production_inhibit_timer_usecs )
                        {
                            active->production_inhibit_timer_usecs -= kOpenerTimerTickInMicroSeconds;
                        }
                    }

                    active->transmission_trigger_timer_usecs -= kOpenerTimerTickInMicroSeconds;

                    if( active->transmission_trigger_timer_usecs <= 0 ) // need to send package
                    {
                        CIPSTER_ASSERT( active->connection_send_data_function );

                        eip_status = active->connection_send_data_function( active );

                        if( eip_status == kEipStatusError )
                        {
                            CIPSTER_TRACE_ERR( "sending of UDP data in manage Connection failed\n" );
                        }

                        // reload the timer value
                        active->transmission_trigger_timer_usecs = active->GetExpectedPacketRateUSecs();

                        if( active->transport_trigger.Trigger() != kConnectionTriggerTypeCyclic )
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


/**
 * Function assembleForwardOpenResponse
 * serializes a response to a forward_open
 */
static void assembleForwardOpenResponse( CipConn* aConn,
        CipMessageRouterResponse* response, CipError general_status,
        ConnectionManagerStatusCode extended_status )
{
    CipCommonPacketFormatData cpfd;

    BufWriter out = response->data;

    cpfd.SetItemCount( 2 );
    cpfd.data_item.type_id = kCipItemIdUnconnectedDataItem;

    cpfd.AddNullAddressItem();

    response->general_status = general_status;

    if( general_status == kCipErrorSuccess )
    {
        CIPSTER_TRACE_INFO( "%s: sending success response\n", __func__ );

        response->size_of_additional_status = 0;

        out.put32( aConn->consuming_connection_id );
        out.put32( aConn->producing_connection_id );
    }
    else
    {
        CIPSTER_TRACE_INFO(
            "%s: sending error response, general_status:0x%x extended_status:0x%x\n",
            __func__,
            general_status,
            extended_status
            );

        aConn->state = kConnectionStateNonExistent;

        switch( general_status )
        {
        case kCipErrorNotEnoughData:
        case kCipErrorTooMuchData:
            response->size_of_additional_status = 0;
            break;

        default:
            switch( extended_status )
            {
            case kConnectionManagerStatusCodeErrorInvalidOToTConnectionSize:
                response->size_of_additional_status = 2;
                response->additional_status[0] = extended_status;
                response->additional_status[1] = aConn->correct_originator_to_target_size;
                break;

            case kConnectionManagerStatusCodeErrorInvalidTToOConnectionSize:
                response->size_of_additional_status = 2;
                response->additional_status[0] = extended_status;
                response->additional_status[1] = aConn->correct_target_to_originator_size;
                break;

            default:
                response->size_of_additional_status = 1;
                response->additional_status[0] = extended_status;
                break;
            }
            break;
        }
    }

    out.put16( aConn->connection_serial_number );
    out.put16( aConn->originator_vendor_id );
    out.put32( aConn->originator_serial_number );

    if( general_status == kCipErrorSuccess )
    {
        // set the actual packet rate to requested packet rate
        out.put32( aConn->o_to_t_RPI_usecs );
        out.put32( aConn->t_to_o_RPI_usecs );
    }

    *out++ = 0;   // remaining path size - for routing devices relevant
    *out++ = 0;   // reserved

    response->data_length += out.data() - response->data.data();
}


CipConn* GetConnectionByConsumingId( int aConnectionId )
{
    CipConn* conn = g_active_connection_list;

    while( conn )
    {
        if( conn->state == kConnectionStateEstablished )
        {
            if( conn->consuming_connection_id == aConnectionId )
            {
                return conn;
            }
        }

        conn = conn->next;
    }

    return NULL;
}


CipConn* GetConnectedOutputAssembly( EipUint32 output_assembly_id )
{
    CipConn* active = g_active_connection_list;

    while( active )
    {
        if( active->state == kConnectionStateEstablished )
        {
            if( active->conn_path.consuming_path.GetInstanceOrConnPt() == output_assembly_id )
                return active;
        }

        active = active->next;
    }

    return NULL;
}


void CloseConnection( CipConn* conn )
{
    conn->state = kConnectionStateNonExistent;

    if( conn->transport_trigger.Class() != kConnectionTransportClass3 )
    {
        // only close the UDP connection for not class 3 connections
        IApp_CloseSocket_udp( conn->consuming_socket );
        conn->consuming_socket = kEipInvalidSocket;

        IApp_CloseSocket_udp( conn->producing_socket );
        conn->producing_socket = kEipInvalidSocket;
    }

    RemoveFromActiveConnections( conn );
}


void AddNewActiveConnection( CipConn* conn )
{
    conn->prev = NULL;
    conn->next = g_active_connection_list;

    if( g_active_connection_list )
    {
        g_active_connection_list->prev = conn;
    }

    g_active_connection_list = conn;
    g_active_connection_list->state = kConnectionStateEstablished;
}


void RemoveFromActiveConnections( CipConn* conn )
{
    if( conn->prev )
    {
        conn->prev->next = conn->next;
    }
    else
    {
        g_active_connection_list = conn->next;
    }

    if( conn->next )
    {
        conn->next->prev = conn->prev;
    }

    conn->prev  = NULL;
    conn->next  = NULL;
    conn->state = kConnectionStateNonExistent;
}



bool IsConnectedInputAssembly( EipUint32 aInstanceId )
{
    CipConn* conn = g_active_connection_list;

    while( conn )
    {
        if( aInstanceId == conn->conn_path.producing_path.GetInstanceOrConnPt() )
            return true;

        conn = conn->next;
    }

    return false;
}


bool IsConnectedOutputAssembly( EipUint32 aInstanceId )
{
    CipConn* conn = g_active_connection_list;

    while( conn )
    {
        if( aInstanceId == conn->conn_path.consuming_path.GetInstanceOrConnPt() )
            return true;

        conn = conn->next;
    }

    return false;
}


EipStatus TriggerConnections( int aOutputAssembly, int aInputAssembly )
{
    EipStatus nRetVal = kEipStatusError;

    CipConn* conn = g_active_connection_list;

    while( conn )
    {
        if( aOutputAssembly == conn->conn_path.consuming_path.GetInstanceOrConnPt()
         && aInputAssembly  == conn->conn_path.producing_path.GetInstanceOrConnPt() )
        {
            if( conn->transport_trigger.Trigger() == kConnectionTriggerTypeApplication )
            {
                // produce at the next allowed occurrence
                conn->transmission_trigger_timer_usecs = conn->production_inhibit_timer_usecs;
                nRetVal = kEipStatusOk;
            }

            break;
        }
    }

    return nRetVal;
}


/**
 * Function forward_open_common
 * checks if resources for new connection are available, and
 * generates a ForwardOpen Reply message.
 *
 * @param instance CIP object instance
 * @param request CipMessageRouterRequest.
 * @param response CipMessageRouterResponse.
 * @param isLarge is true when called from largeForwardOpen(), false when called from forwardOpen()
 *  and the distinction is whether to expect 32 or 16 bits of "network connection parameters".
 *
 * @return EipStatus
 *     -  >0 .. success, 0 .. no reply to send back
 *     -  -1 .. error
 */
static EipStatus forward_open_common( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response, bool isLarge )
{
    ConnectionManagerStatusCode connection_status = kConnectionManagerStatusCodeSuccess;

    (void) instance;        // suppress compiler warning

    static CipConn dummy;

    BufReader in = request->data;

    dummy.priority_timetick = *in++;
    dummy.timeout_ticks = *in++;

    dummy.consuming_connection_id = in.get32();     // O_to_T
    dummy.producing_connection_id = in.get32();     // T_to_O

    // The Connection Triad used in the Connection Manager specification relates
    // to the combination of Connection Serial Number, Originator Vendor ID and
    // Originator Serial Number parameters.
    dummy.connection_serial_number = in.get16();
    dummy.originator_vendor_id     = in.get16();
    dummy.originator_serial_number = in.get32();

    // first check if we have already a connection with the given params
    if( findExistingMatchingConnection( &dummy ) )
    {
        // TODO this test is  incorrect, see CIP spec 3-5.5.2 re: duplicate forward open
        // it should probably be testing the connection type fields
        // TODO think on how a reconfiguration request could be handled correctly.
        if( !dummy.consuming_connection_id && !dummy.producing_connection_id )
        {
            // TODO implement reconfiguration of connection

            CIPSTER_TRACE_ERR(
                    "this looks like a duplicate forward open -- I can't handle this yet,\n"
                    "sending a CIP_CON_MGR_ERROR_CONNECTION_IN_USE response\n" );
        }

        assembleForwardOpenResponse(
                &dummy, response,
                kCipErrorConnectionFailure,
                kConnectionManagerStatusCodeErrorConnectionInUse );

        return kEipStatusOkSend; // send reply
    }

    // keep it to non-existent until the setup is done, this eases error handling and
    // the state changes within the forward open request can not be detected from
    // the application or from outside (reason we are single threaded)
    dummy.state = kConnectionStateNonExistent;

    dummy.sequence_count_producing = 0; // set the sequence count to zero

    dummy.connection_timeout_multiplier = *in++;

    if( dummy.connection_timeout_multiplier > 7 )
    {
        // 3-5.4.1.4
       CIPSTER_TRACE_INFO( "%s: invalid connection timeout multiplier: %u\n",
           __func__, dummy.connection_timeout_multiplier );

        assembleForwardOpenResponse(
                &dummy, response,
                kCipErrorConnectionFailure,
                kConnectionManagerStatusCodeErrorInvalidOToTConnectionType );

        return kEipStatusOkSend;    // send reply
    }

    in += 3;         // skip over 3 reserved bytes.

    CIPSTER_TRACE_INFO(
        "%s: ConConnID:0x%08x, ProdConnID:0x%08x, ConnSerNo:%u\n",
        __func__,
        dummy.consuming_connection_id,
        dummy.producing_connection_id,
        dummy.connection_serial_number
        );

    dummy.o_to_t_RPI_usecs = in.get32();

    if( isLarge )
        dummy.o_to_t_ncp.SetLarge( in.get32() );
    else
        dummy.o_to_t_ncp.SetNotLarge( in.get16() );

    CIPSTER_TRACE_INFO( "%s: o_to_t RPI_usecs:%u\n", __func__, dummy.o_to_t_RPI_usecs );
    CIPSTER_TRACE_INFO( "%s: o_to_t size:%d\n", __func__, dummy.o_to_t_ncp.ConnectionSize() );
    CIPSTER_TRACE_INFO( "%s: o_to_t priority:%d\n", __func__, dummy.o_to_t_ncp.Priority() );
    CIPSTER_TRACE_INFO( "%s: o_to_t type:%d\n", __func__, dummy.o_to_t_ncp.ConnectionType() );

    dummy.t_to_o_RPI_usecs = in.get32();

    // The requested packet interval parameter needs to be a multiple of
    // kOpenerTimerTickInMicroSeconds from the header file
    EipUint32 temp = dummy.t_to_o_RPI_usecs % kOpenerTimerTickInMicroSeconds;

    // round up to slower nearest integer multiple of our timer.
    if( temp )
    {
        dummy.t_to_o_RPI_usecs = (EipUint32) ( dummy.t_to_o_RPI_usecs / kOpenerTimerTickInMicroSeconds )
            * kOpenerTimerTickInMicroSeconds + kOpenerTimerTickInMicroSeconds;
    }

    if( isLarge )
        dummy.t_to_o_ncp.SetLarge( in.get32() );
    else
        dummy.t_to_o_ncp.SetNotLarge( in.get16() );

    // check if Network connection parameters are ok
    if( dummy.o_to_t_ncp.ConnectionType() == kIOConnTypeInvalid )
    {
        CIPSTER_TRACE_INFO( "%s: invalid O to T connection type\n", __func__ );

        assembleForwardOpenResponse(
                &dummy, response,
                kCipErrorConnectionFailure,
                kConnectionManagerStatusCodeErrorInvalidOToTConnectionType );

        return kEipStatusOkSend;    // send reply
    }

    if( dummy.t_to_o_ncp.ConnectionType() == kIOConnTypeInvalid )
    {
        CIPSTER_TRACE_INFO( "%s: invalid T to O connection type\n", __func__ );

        assembleForwardOpenResponse(
                &dummy, response,
                kCipErrorConnectionFailure,
                kConnectionManagerStatusCodeErrorInvalidTToOConnectionType );

        return kEipStatusOkSend;    // send reply
    }

    int trigger = *in++;

    // check for undocumented trigger bits
    if( 0x4c & trigger )
    {
        CIPSTER_TRACE_INFO( "%s: trigger 0x%02x not supported\n",
            __func__, trigger );

        assembleForwardOpenResponse(
                &dummy, response,
                kCipErrorConnectionFailure,
                kConnectionManagerStatusCodeErrorTransportTriggerNotSupported );

        return kEipStatusOkSend;    // send reply
    }

    dummy.transport_trigger.Set( trigger );

    unsigned conn_path_byte_count = *in++ * 2;

    if( g_kForwardOpenHeaderLength + conn_path_byte_count < request->data.size() )
    {
        assembleForwardOpenResponse( &dummy, response, kCipErrorTooMuchData, connection_status );
        return kEipStatusOkSend;    // send reply
    }

    if( g_kForwardOpenHeaderLength + conn_path_byte_count > request->data.size() )
    {
        assembleForwardOpenResponse( &dummy, response, kCipErrorNotEnoughData, connection_status );
        return kEipStatusOkSend;
    }

    CipError result = dummy.parseConnectionPath( in, &connection_status );

    if( result != kCipErrorSuccess )
    {
        CIPSTER_TRACE_INFO( "%s: unable to parse connection path\n", __func__ );

        assembleForwardOpenResponse( &dummy, response, result, connection_status );
        return kEipStatusOkSend;
    }

    CipClass* clazz = GetCipClass( dummy.mgmnt_class );

    result = clazz->OpenConnection( &dummy, response->CPFD(), &connection_status );

    if( result != kCipErrorSuccess )
    {
        CIPSTER_TRACE_INFO( "%s: OpenConnection() failed. status:0x%x\n", __func__, connection_status );

        // in case of error the dummy contains all necessary information
        assembleForwardOpenResponse( &dummy, response, result, connection_status );
        return kEipStatusOkSend;
    }
    else
    {
        CIPSTER_TRACE_INFO( "%s: OpenConnection() succeeded\n", __func__ );

        // in case of success, g_active_connection_list points to the new connection
        assembleForwardOpenResponse( g_active_connection_list,
                response, kCipErrorSuccess, kConnectionManagerStatusCodeSuccess );
        return kEipStatusOkSend;
    }
}


static EipStatus forward_open_service( CipInstance* instance,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    return forward_open_common( instance, request, response, false );
}


static EipStatus large_forward_open_service( CipInstance* instance,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    return forward_open_common( instance, request, response, true );
}


static EipStatus forward_close_service( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    // Suppress compiler warning
    (void) instance;

    // check connection_serial_number && originator_vendor_id && originator_serial_number if connection is established
    ConnectionManagerStatusCode connection_status =
        kConnectionManagerStatusCodeErrorConnectionNotFoundAtTargetApplication;

    BufReader in = request->data;

    in += 2;        // ignore Priority/Time_tick and Time-out_ticks

    EipUint16 connection_serial_number = in.get16();
    EipUint16 originator_vendor_id     = in.get16();
    EipUint32 originator_serial_number = in.get32();
    EipUint8  connection_path_size     = *in++;

    CIPSTER_TRACE_INFO( "ForwardClose: ConnSerNo %d\n", connection_serial_number );

    CipConn* active = g_active_connection_list;

    while( active )
    {
        // This check should not be necessary as only established connections
        // should be in the active connection list
        if( active->state == kConnectionStateEstablished ||
            active->state == kConnectionStateTimedOut )
        {
            if( active->connection_serial_number == connection_serial_number
             && active->originator_vendor_id     == originator_vendor_id
             && active->originator_serial_number == originator_serial_number )
            {
                // found the corresponding connection object -> close it
                CIPSTER_ASSERT( active->connection_close_function );
                active->connection_close_function( active );
                connection_status = kConnectionManagerStatusCodeSuccess;
                break;
            }
        }

        active = active->next;
    }

    BufWriter out = response->data;

    out.put16( connection_serial_number );
    out.put16( originator_vendor_id );
    out.put32( originator_serial_number );

    if( connection_status == kConnectionManagerStatusCodeSuccess )
    {
        // Vol1 Table 3-5.22
        *out++ = 0;         // application data word count
        *out++ = 0;         // reserved
    }
    else
    {
        // Vol1 Table 3-5.23
        *out++ = connection_path_size;
        response->general_status = kCipErrorConnectionFailure;
        response->additional_status[0] = connection_status;
        response->size_of_additional_status = 1;

        *out++ = 0;         // reserved
    }

    response->data_length = out.data() - response->data.data();

    return kEipStatusOkSend;
}


class CipConnMgrClass : public CipClass
{
public:
    CipConnMgrClass();
};


CipConnMgrClass::CipConnMgrClass() :
    CipClass( kCipConnectionManagerClassCode,
        "Connection Manager",
        MASK5( 1,2,3,6,7 ),     // common class attributes
        MASK5( 1,2,3,6,7 ),     // class getAttributeAll mask
        0,                      // instance getAttributeAll mask
        1                       // revision
        )
{
    // There are no attributes in instance of this class yet, so nothing to set.
    delete ServiceRemove( kSetAttributeSingle );

    ServiceInsert( kForwardOpen, forward_open_service, "ForwardOpen" );
    ServiceInsert( kLargeForwardOpen, large_forward_open_service, "LargeForwardOpen" );

    ServiceInsert( kForwardClose, forward_close_service, "ForwardClose" );

    InitializeIoConnectionData();
}


static CipInstance* createConnectionManagerInstance()
{
    CipClass* clazz = GetCipClass( kCipConnectionManagerClassCode );

    CipInstance* i = new CipInstance( clazz->Instances().size() + 1 );

    clazz->InstanceInsert( i );

    return i;
}


EipStatus ConnectionManagerInit()
{
    if( !GetCipClass( kCipConnectionManagerClassCode ) )
    {
        CipClass* clazz = new CipConnMgrClass();

        RegisterCipClass( clazz );

        // add one instance
        createConnectionManagerInstance();
    }

    return kEipStatusOk;
}

