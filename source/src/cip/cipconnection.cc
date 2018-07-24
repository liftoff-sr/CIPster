/*******************************************************************************
 * Copyright (c) 2011, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/

#include <string.h>
#include <algorithm>

#include "cipconnection.h"

#include <cipster_api.h>
#include "cipconnectionmanager.h"
#include "appcontype.h"
#include "cpf.h"
#include "cipassembly.h"
#include "cipcommon.h"


// The port to be used per default for I/O messages on UDP.
const int kOpenerEipIoUdpPort = 2222;   // = 0x08AE;


EipUint32 g_run_idle_state;    //*< buffer for holding the run idle information.


int ConnectionPath::Serialize( BufWriter aOutput, int aCtl ) const
{
    BufWriter out = aOutput;

    if( port_segs.HasAny() )
        out += port_segs.Serialize( out, aCtl );

    if( config_path.HasAny() )
        out += config_path.Serialize( out, aCtl );

    if( consuming_path.HasAny() )
        out += consuming_path.Serialize( out, aCtl );

    if( producing_path.HasAny() )
        out += producing_path.Serialize( out, aCtl );

    if( data_seg.HasAny() )
        out += data_seg.Serialize( out, aCtl );

    return out.data() - aOutput.data();
}


int ConnectionPath::SerializedCount( int aCtl ) const
{
    int count = 0;

    if( port_segs.HasAny() )
        count += port_segs.SerializedCount( aCtl );

    if( config_path.HasAny() )
        count += config_path.SerializedCount( aCtl );

    if( consuming_path.HasAny() )
        count += consuming_path.SerializedCount( aCtl );

    if( producing_path.HasAny() )
        count += producing_path.SerializedCount( aCtl );

    if( data_seg.HasAny() )
        count += data_seg.SerializedCount( aCtl );

    return count;
}


ConnectionData::ConnectionData(
        CipByte aPriorityTimeTick,
        CipByte aTimeoutTicks,
        CipUdint aConsumingConnectionId,
        CipUdint aProducingConnectionId,
        CipUint aConnectionSerialNumber,
        CipUint aOriginatorVendorId,
        CipUdint aOriginatorSerialNumber,
        CipByte aConnectionTimeoutMultiplier,
        CipUdint a_O_to_T_RPI_usecs,
        CipUdint a_T_to_O_RPI_usecs
        ) :
    priority_timetick( aPriorityTimeTick ),
    timeout_ticks( aTimeoutTicks ),
    consuming_connection_id( aConsumingConnectionId ),
    producing_connection_id( aProducingConnectionId ),
    connection_serial_number( aConnectionSerialNumber ),
    originator_vendor_id( aOriginatorVendorId ),
    originator_serial_number( aOriginatorSerialNumber ),
    connection_timeout_multiplier( aConnectionTimeoutMultiplier ),
    o_to_t_RPI_usecs( a_O_to_T_RPI_usecs ),
    t_to_o_RPI_usecs( a_T_to_O_RPI_usecs ),
    corrected_o_to_t_size( 0 ),
    corrected_t_to_o_size( 0 ),
    consuming_instance( 0 ),
    producing_instance( 0 ),
    config_instance( 0 )
{
}


static CipInstance* check_path( const CipAppPath& aPath, ConnectionManagerStatusCode* extended_error, const char* aCaller )
{
    if( !aPath.IsSufficient() )
    {
        CIPSTER_TRACE_ERR( "%s: aPath is not sufficient %s\n", __func__, aCaller );

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


int ConnectionData::DeserializeConnectionData( BufReader aInput, bool isLarge )
{
    BufReader in = aInput;

    priority_timetick = in.get8();
    timeout_ticks     = in.get8();

    consuming_connection_id = in.get32();     // O_to_T
    producing_connection_id = in.get32();     // T_to_O

    // The Connection Triad
    connection_serial_number = in.get16();
    originator_vendor_id     = in.get16();
    originator_serial_number = in.get32();

    connection_timeout_multiplier = in.get8();

    in += 3;         // skip over 3 reserved bytes.

    o_to_t_RPI_usecs = in.get32();
    o_to_t_ncp.Set( isLarge ? in.get32() : in.get16(), isLarge );

    t_to_o_RPI_usecs = in.get32();
    t_to_o_ncp.Set( isLarge ? in.get32() : in.get16(), isLarge );

    trigger.Set( in.get8() );

    return in.data() - aInput.data();
}


CipError ConnectionData::DeserializeConnectionPath(
        BufReader aPath, ConnectionManagerStatusCode* extended_error )
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
        ConnectionManagerStatusCode sts = conn_path.port_segs.Key().Check();

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
       production use the same path.  See Vol1 table 3-5.13.
    */

    if( in.size() )
    {
        result = app_path1.DeserializeAppPath( in );

        if( result < 0 )
        {
            CIPSTER_TRACE_ERR( "%s: unable to deserialize app_path1\n", __func__ );
            goto L_exit_invalid;
        }

        in += result;
    }

    if( in.size() )
    {
        result = app_path2.DeserializeAppPath( in, &app_path1 );

        if( result < 0 )
        {
            CIPSTER_TRACE_ERR( "%s: unable to deserialize app_path2\n", __func__ );
            goto L_exit_invalid;
        }

        in += result;
    }

    if( in.size() )
    {
        result = app_path3.DeserializeAppPath( in, &app_path2 );

        if( result < 0 )
        {
            CIPSTER_TRACE_ERR( "%s: unable to deserialize app_path3\n", __func__ );
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

    actual_app_path_count = 1 + app_path2.HasAny() + app_path3.HasAny();

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
                CIPSTER_TRACE_ERR(
                    "%s: doubly null connection types takes only 1 app_path\n",
                    __func__ );
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

    if( trigger.Class() == kConnectionTransportClass3 )
    {
        // connection end point has to be the message router instance 1
        if( conn_path.consuming_path.GetClass() != kCipMessageRouterClass ||
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


int ConnectionData::Serialize( BufWriter aOutput, int aCtl ) const
{
    BufWriter out = aOutput;

    out.put8( priority_timetick )
    .put8( timeout_ticks )

    .put32( consuming_connection_id )
    .put32( producing_connection_id )

    // The Connection Triad
    .put16( connection_serial_number )
    .put16( originator_vendor_id )
    .put32( originator_serial_number )

    .put8( connection_timeout_multiplier )

    .fill( 3 )      // output 3 reserved bytes.

    .put32( o_to_t_RPI_usecs );

    o_to_t_ncp.Serialize( out );

    out.put32( t_to_o_RPI_usecs );
    t_to_o_ncp.Serialize( out );

    trigger.Serialize( out );

    if( aCtl & CTL_INCLUDE_CONN_PATH )
    {
        CipByte* cpathz_loc = out.data();

        out += 1;   // skip over Connection_Path_Size location

        int byte_count = conn_path.Serialize( out, aCtl );

        *cpathz_loc = byte_count / 2;

        out += byte_count;
    }

    return out.data() - aOutput.data();
}


int ConnectionData::SerializedCount( int aCtl ) const
{
    int count = 31 + o_to_t_ncp.SerializedCount()
                   + t_to_o_ncp.SerializedCount();

    if( aCtl & CTL_INCLUDE_CONN_PATH )
    {
        count +=    1   // connection path size USINT
                +   conn_path.SerializedCount( aCtl );
    }

    return count;
}


/* producing multicast connections have to consider the rules that apply for
 * application connection types.
 */


ConnectionManagerStatusCode CipConn::handleConfigData()
{
    ConnectionManagerStatusCode result = kConnectionManagerStatusCodeSuccess;

    CipInstance* instance = config_instance;

    Words& words = conn_path.data_seg.words;

    if( ConnectionWithSameConfigPointExists( conn_path.config_path.GetInstanceOrConnPt() ) )
    {
        // There is a connected connection with the same config point ->
        // we have to have the same data as already present in the config point,
        // else it's an error.  And if same, no reason to write it.

        ByteBuf* p = (ByteBuf*) instance->Attribute( 3 )->Data();

        if( p->size() != words.size() * 2  ||
            memcmp( p->data(), words.data(), p->size() ) != 0 )
        {
            result = kConnectionManagerStatusCodeErrorOwnershipConflict;
        }
    }

    // Put the data into the configuration assembly object
    else if( kEipStatusOk != NotifyAssemblyConnectedDataReceived( instance,
             BufReader( (EipByte*)  words.data(),  words.size() * 2 ) ) )
    {
        CIPSTER_TRACE_WARN( "Configuration data was invalid\n" );
        result = kConnectionManagerStatusCodeInvalidConfigurationApplicationPath;
    }

    return result;
}


/// @brief Holds the connection ID's "incarnation ID" in the upper 16 bits
static EipUint32 g_incarnation_id;


EipUint32 CipConn::NewConnectionId()
{
    static EipUint32 connection_id = 18;

    connection_id++;
    return g_incarnation_id | (connection_id & 0x0000FFFF);
}


CipConn::CipConn()
{
    Clear( false );
}


void CipConn::Clear( bool doConnectionDataToo )
{
    if( doConnectionDataToo )
        ConnectionData::Clear();

    SetState( kConnectionStateNonExistent );

    SetInstanceType( kConnInstanceTypeExplicit );

    watchdog_timeout_action = kWatchdogTimeoutActionTransitionToTimedOut;

    eip_level_sequence_count_producing = 0;
    eip_level_sequence_count_consuming = 0;
    eip_level_sequence_count_consuming_first = true;

    sequence_count_producing = 0;
    sequence_count_consuming = 0;

    transmission_trigger_timer_usecs = 0;
    inactivity_watchdog_timer_usecs = 0;

    production_inhibit_timer_usecs = 0;

    memset( &remote_address, 0, sizeof remote_address );

    memset( &originator_address, 0, sizeof originator_address );

    hook_close = NULL;
    hook_timeout = NULL;

    SetConsumingSocket( kEipInvalidSocket );
    SetProducingSocket( kEipInvalidSocket );

    next = NULL;
    prev = NULL;

    expected_packet_rate_usecs = 0;
}


void CipConn::GeneralConnectionConfiguration()
{
    if( o_to_t_ncp.ConnectionType() == kIOConnTypePointToPoint )
    {
        // if we have a point to point connection for the O to T direction
        // the target shall choose the connection ID.
        consuming_connection_id = CipConn::NewConnectionId();
    }

    if( t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast )
    {
        // if we have a multi-cast connection for the T to O direction the
        // target shall choose the connection ID.

        producing_connection_id = CipConn::NewConnectionId();
        CIPSTER_TRACE_INFO( "%s: new producing multicast connection_id:0x%08x\n",
            __func__, producing_connection_id );
    }

    eip_level_sequence_count_producing = 0;
    sequence_count_producing = 0;

    eip_level_sequence_count_consuming = 0;
    eip_level_sequence_count_consuming_first = true;

    sequence_count_consuming = 0;

    watchdog_timeout_action = kWatchdogTimeoutActionAutoDelete;  // the default for all connections on EIP

    SetExpectedPacketRateUSecs( 0 );    // default value

    if( !trigger.IsServer() )  // Client Type Connection requested
    {
        SetExpectedPacketRateUSecs( t_to_o_RPI_usecs );

        /* As soon as we are ready we should produce the connection. With the 0
         * here we will produce with the next timer tick
         * which should be sufficient.
         */
        transmission_trigger_timer_usecs = 0;
    }
    else
    {
        // Server Type Connection requested
        SetExpectedPacketRateUSecs( o_to_t_RPI_usecs );
    }

    production_inhibit_timer_usecs = 0;

    SetPIT_USecs( 0 );

    // setup the preconsuption timer: max(ConnectionTimeoutMultiplier * EpectetedPacketRate, 10s)
    inactivity_watchdog_timer_usecs = std::max( TimeoutMSecs_o_to_t(), 10000000u );

    CIPSTER_TRACE_INFO( "%s: inactivity_watchdog_timer_usecs:%u\n", __func__,
            inactivity_watchdog_timer_usecs );
}


void CipConn::Close()
{
    if( hook_close )
        hook_close( this );

    if( IsIOConnection() )
    {
        CheckIoConnectionEvent(
                conn_path.consuming_path.GetInstanceOrConnPt(),
                conn_path.producing_path.GetInstanceOrConnPt(),
                kIoConnectionEventClosed
                );

        if( InstanceType() == kConnInstanceTypeIoExclusiveOwner
         || InstanceType() == kConnInstanceTypeIoInputOnly )
        {
            if( t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast
             && ProducingSocket() != kEipInvalidSocket )
            {
                CipConn* next_non_control_master =
                    GetNextNonControlMasterConnection( conn_path.producing_path.GetInstanceOrConnPt() );

                if( next_non_control_master )
                {
                    next_non_control_master->SetProducingSocket( ProducingSocket() );

                    next_non_control_master->remote_address = remote_address;

                    next_non_control_master->eip_level_sequence_count_producing =
                        eip_level_sequence_count_producing;

                    next_non_control_master->sequence_count_producing = sequence_count_producing;

                    SetProducingSocket( kEipInvalidSocket );

                    next_non_control_master->transmission_trigger_timer_usecs =
                        transmission_trigger_timer_usecs;
                }

                else
                {
                    // This was the last master connection, close all listen only
                    // connections listening on the port.
                    CloseAllConnectionsForInputWithSameType(
                            conn_path.producing_path.GetInstanceOrConnPt(),
                            kConnInstanceTypeIoListenOnly );
                }
            }
        }
    }

    CloseSocket( consuming_socket );
    SetConsumingSocket( kEipInvalidSocket );

    CloseSocket( producing_socket );
    SetProducingSocket( kEipInvalidSocket );

    g_active_conns.Remove( this );
}


EipStatus CipConn::SendConnectedData()
{
    /*
        TODO think of adding an own send buffer to each connection object in
        order to pre-setup the whole message on connection opening and just
        change the variable data items e.g., sequence number
    */

    EipStatus result;

    BufWriter out( g_message_data_reply_buffer, sizeof g_message_data_reply_buffer );

    ++eip_level_sequence_count_producing;

    //----<AddressInfoItem>-----------------------------------------------------

    Cpf cpfd(
        AddressItem(
            // use Sequenced Address Item if not Connection Class 0
            trigger.Class() == kConnectionTransportClass0 ?
                kCpfIdConnectedAddress : kCpfIdSequencedAddress,
            producing_connection_id,
            eip_level_sequence_count_producing ),
        kCpfIdConnectedDataItem
        );

    // Notify the application that Assembly data pertinent to provided instance
    // will be sent immediately after the call.  If application returns true,
    // this means the Assembly data has changed or should be reported as having changed.
    if( BeforeAssemblyDataSend( producing_instance ) )
    {
        // the data has changed, increase sequence counter
        ++sequence_count_producing;
    }

    //----<DataInfoItem>--------------------------------------------------------

    CipAttribute* attr3 = producing_instance->Attribute( 3 );
    CIPSTER_ASSERT( attr3 );

    ByteBuf* attr3_byte_array = (ByteBuf*) attr3->Data();
    CIPSTER_ASSERT( attr3_byte_array );

    int length = cpfd.Serialize( out );

    // Advance over Cpf serialization, which ended after data_item.length, but
    // prepare to re-write that 16 bit field below, ergo -2
    out += (length - 2);

    int data_len = attr3_byte_array->size();

    if( kOpenerProducedDataHasRunIdleHeader )
    {
        data_len += 4;
    }

    if( trigger.Class() == kConnectionTransportClass1 )
    {
        data_len += 2;

        out.put16( data_len );
        out.put16( sequence_count_producing );
    }
    else
    {
        out.put16( data_len );
    }

    if( kOpenerProducedDataHasRunIdleHeader )
    {
        out.put32( g_run_idle_state );
    }

    out.append( attr3_byte_array->data(), attr3_byte_array->size() );

    length += data_len;

    // send out onto UDP wire
    result = SendUdpData(
                &remote_address,
                producing_socket,
                BufReader( g_message_data_reply_buffer, length )
                );

    return result;
}


EipStatus CipConn::HandleReceivedIoConnectionData( BufReader aInput )
{
    if( trigger.Class() == kConnectionTransportClass1 )
    {
        // consume first 2 bytes for the sequence count
        EipUint16 sequence = aInput.get16();

        if( SEQ_LEQ16( sequence, sequence_count_consuming ) )
        {
            // This is a duplication of earlier data.  No new data for the assembly.
            // Do not notify application of this, which would cost cycles.
            return kEipStatusOk;
        }

        sequence_count_consuming = sequence;
    }

    // we may have consumed 2 bytes above, what is left is without sequence count
    if( aInput.size() )
    {
        // We have no heartbeat connection, because a heartbeat payload
        // may not contain a run_idle header.
        if( kOpenerConsumedDataHasRunIdleHeader )
        {
            EipUint32 new_run_idle = aInput.get32();

            if( g_run_idle_state != new_run_idle )
            {
                RunIdleChanged( new_run_idle );
            }

            g_run_idle_state = new_run_idle;
        }

        if( NotifyAssemblyConnectedDataReceived( consuming_instance, aInput ) != 0 )
        {
            return kEipStatusError;
        }
    }

    return kEipStatusOk;
}


void CipConn::timeOut()
{
    if( hook_timeout )
        hook_timeout( this );

    if( IsIOConnection() )
    {
        CheckIoConnectionEvent(
            conn_path.consuming_path.GetInstanceOrConnPt(),
            conn_path.producing_path.GetInstanceOrConnPt(),
            kIoConnectionEventTimedOut
            );

        if( t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast )
        {
            switch( InstanceType() )
            {
            case kConnInstanceTypeIoExclusiveOwner:
                CloseAllConnectionsForInputWithSameType(
                        conn_path.producing_path.GetInstanceOrConnPt(),
                        kConnInstanceTypeIoInputOnly );
                CloseAllConnectionsForInputWithSameType(
                        conn_path.producing_path.GetInstanceOrConnPt(),
                        kConnInstanceTypeIoListenOnly );
                break;

            case kConnInstanceTypeIoInputOnly:
                if( kEipInvalidSocket != ProducingSocket() )
                {
                    // we are the controlling input only connection find a new controller

                    CipConn* next_non_control_master =
                        GetNextNonControlMasterConnection( conn_path.producing_path.GetInstanceOrConnPt() );

                    if( next_non_control_master )
                    {
                        next_non_control_master->SetProducingSocket( ProducingSocket() );
                        SetProducingSocket( kEipInvalidSocket );

                        next_non_control_master->transmission_trigger_timer_usecs =
                            transmission_trigger_timer_usecs;
                    }

                    // this was the last master connection close all listen only
                    // connections listening on the port
                    else
                    {
                        CloseAllConnectionsForInputWithSameType(
                                conn_path.producing_path.GetInstanceOrConnPt(),
                                kConnInstanceTypeIoListenOnly );
                    }
                }

                break;

            default:
                break;
            }
        }
    }

    Close();
}


EipStatus CipConn::OpenConsumingPointToPointConnection( Cpf* cpfd )
{
    // TODO think on improving the udp port assigment for point to point
    // connections

    sockaddr_in addr;

    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl( INADDR_ANY );
    addr.sin_port        = htons( kOpenerEipIoUdpPort );

    // "addr" is only needed for bind; used if consuming
    int socket = CreateUdpSocket( kUdpConsuming, &addr );

    if( socket == kEipInvalidSocket )
    {
        CIPSTER_TRACE_ERR( "%s: cannot create UDP socket\n", __func__ );
        return kEipStatusError;
    }

    // CreateUdpSocket can modify addr.

    originator_address = addr;    // store the address of the originator for packet scanning

    addr.sin_addr.s_addr = htonl( INADDR_ANY );  // restore the address

    SetConsumingSocket( socket );

    SockAddrInfoItem saii(
        kCpfIdSockAddrInfo_O_to_T,
        INADDR_ANY,
        kOpenerEipIoUdpPort
        );

    cpfd->AppendTx( saii );

    return kEipStatusOk;
}


CipError CipConn::OpenProducingPointToPointConnection( Cpf* cpfd )
{
    // The default port to be used if no port information is
    // part of the forward_open request.
    in_port_t port;

    const SockAddrInfoItem* saii = cpfd->SearchRx( kCpfIdSockAddrInfo_T_to_O );
    if( saii )
        port = saii->sin_port;
    else
        port = kOpenerEipIoUdpPort;

    remote_address.sin_family = AF_INET;

    // We don't know the address of the originator, but it will be set in
    // CreateUdpSocket()
    remote_address.sin_addr.s_addr = 0;

    remote_address.sin_port = htons( port );

    int socket = CreateUdpSocket( kUdpProducing, &remote_address );

    if( socket == kEipInvalidSocket )
    {
        CIPSTER_TRACE_ERR(
                "cannot create UDP socket in OpenPointToPointConnection\n" );
        // *pa_pnExtendedError = 0x0315; miscellaneous
        return kCipErrorConnectionFailure;
    }

    SetProducingSocket( socket );

    return kCipErrorSuccess;
}


EipStatus CipConn::OpenMulticastConnection( UdpCommuncationDirection direction, Cpf* cpfd )
{
    SockAddrInfoItem* saii = NULL;

    // see Vol2 3-3.9.4 Sockaddr Info Item Placement and Errors
    if( direction == kUdpConsuming )
    {
        saii = (SockAddrInfoItem*) cpfd->SearchRx( kCpfIdSockAddrInfo_O_to_T );

        if( !saii )
        {
            CIPSTER_TRACE_ERR( "%s: no suitable addr info item available\n", __func__ );
            return kEipStatusError;
        }

        // For consuming connections the originator can choose the
        // multicast address to use; we have a given address in saii so use it

        cpfd->AppendTx( *saii );

        // allocate an unused sockaddr struct to use
        sockaddr_in socket_address = *saii;

        // the address is only needed for bind used if consuming
        int socket = CreateUdpSocket( direction, &socket_address );

        if( socket == kEipInvalidSocket )
        {
            CIPSTER_TRACE_ERR( "cannot create UDP socket in OpenMulticastConnection\n" );
            return kEipStatusError;
        }

        originator_address = socket_address;
    }

    else
    {
        SockAddrInfoItem   a( kCpfIdSockAddrInfo_T_to_O,
            ntohl( CipTCPIPInterfaceClass::MultiCast( 1 ).starting_multicast_address ),
            kOpenerEipIoUdpPort
            );

        cpfd->AppendTx( a );

        // allocate an unused sockaddr struct to use
        sockaddr_in socket_address = a;

        // the address is only needed for bind used if consuming
        int socket = CreateUdpSocket( direction, &socket_address );

        if( socket == kEipInvalidSocket )
        {
            CIPSTER_TRACE_ERR( "cannot create UDP socket in OpenMulticastConnection\n" );
            return kEipStatusError;
        }

        SetProducingSocket( socket );
        remote_address   = socket_address;
    }

    CIPSTER_TRACE_INFO( "%s: opened OK\n", __func__ );

    return kEipStatusOk;
}


EipStatus CipConn::OpenProducingMulticastConnection( Cpf* cpfd )
{
    CipConn* existing_conn =
        GetExistingProducerMulticastConnection( conn_path.producing_path.GetInstanceOrConnPt() );

    // If we are the first connection producing for the given Input Assembly
    if( !existing_conn )
    {
        return OpenMulticastConnection( kUdpProducing, cpfd );
    }
    else
    {
        // inform our originator about the correct connection id
        producing_connection_id = existing_conn->producing_connection_id;
    }

    // we have a connection, reuse the data and the socket

    if( kConnInstanceTypeIoExclusiveOwner == InstanceType() )
    {
        /* exclusive owners take the socket and further manage the connection
         * especially in the case of time outs.
         */
        SetProducingSocket( existing_conn->ProducingSocket() );

        existing_conn->SetProducingSocket( kEipInvalidSocket );
    }
    else    // this connection will not produce the data
    {
        SetProducingSocket( kEipInvalidSocket );
    }

    SockAddrInfoItem saii( kCpfIdSockAddrInfo_T_to_O,
        ntohl( CipTCPIPInterfaceClass::MultiCast( 1 ).starting_multicast_address ),
        kOpenerEipIoUdpPort
        );

    cpfd->AppendTx( saii );

    remote_address = saii;

    return kEipStatusOk;
}


CipError CipConn::OpenCommunicationChannels( Cpf* cpfd )
{
    IOConnType o_to_t = o_to_t_ncp.ConnectionType();
    IOConnType t_to_o = t_to_o_ncp.ConnectionType();

    // open a connection "point to point" or "multicast" based on the ConnectionParameter
    if( o_to_t == kIOConnTypeMulticast )
    {
        if( OpenMulticastConnection( kUdpConsuming, cpfd ) == kEipStatusError )
        {
            CIPSTER_TRACE_ERR( "error in OpenMulticast Connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    else if( o_to_t == kIOConnTypePointToPoint )
    {
        if( OpenConsumingPointToPointConnection( cpfd ) == kEipStatusError )
        {
            CIPSTER_TRACE_ERR( "error in PointToPoint consuming connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    if( t_to_o == kIOConnTypeMulticast )
    {
        if( OpenProducingMulticastConnection( cpfd ) == kEipStatusError )
        {
            CIPSTER_TRACE_ERR( "error in OpenMulticast Connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    else if( t_to_o == kIOConnTypePointToPoint )
    {
        if( OpenProducingPointToPointConnection( cpfd ) != kCipErrorSuccess )
        {
            CIPSTER_TRACE_ERR( "error in PointToPoint producing connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    return kCipErrorSuccess;
}


CipConnectionClass::CipConnectionClass() :
    CipClass(
        kCipConnectionClass,
        "Connection",
        MASK2( 6, 1 ),              // class attributes mask
        1                           // revision
        )
{
    // There are no attributes in instance of this class yet.
    delete ServiceRemove( kSetAttributeSingle );
    delete ServiceRemove( kGetAttributeSingle );
}


CipError CipConnectionClass::OpenIO( ConnectionData* aParams,
        Cpf* cpfd, ConnectionManagerStatusCode* extended_error )
{
    IOConnType o_to_t;
    IOConnType t_to_o;

    // currently we allow I/O connections only to assembly objects

    CipConn* io_conn = GetIoConnectionForConnectionData( aParams, extended_error );

    if( !io_conn )
    {
        /*
        CIPSTER_TRACE_ERR(
            "%s: no reserved IO connection was found for:\n"
            " %s.\n"
            " All anticipated IO connections must be reserved with Configure<*>ConnectionPoint()\n",
            __func__,
            aConn->conn_path.Format().c_str()
            );
        */

        return kCipErrorConnectionFailure;
    }

    // Both Change of State and Cyclic triggers use the Transmission Trigger Timer
    // according to Vol1_3.19_3-4.4.3.7.

    if( io_conn->trigger.Trigger() != kConnectionTriggerTypeCyclic )
    {
        // trigger is not cyclic, it is Change of State here.

        if( !io_conn->conn_path.port_segs.HasPIT() )
        {
            // saw no PIT segment in the connection path, set PIT to one fourth of RPI
            io_conn->SetPIT_USecs( io_conn->t_to_o_RPI_usecs / 4 );
        }

        // if production inhibit time has been provided it needs to be smaller than the RPI
        else if( io_conn->GetPIT_USecs() > io_conn->t_to_o_RPI_usecs )
        {
            // see section C-1.4.3.3
            *extended_error = kConnectionManagerStatusCodeErrorPITGreaterThanRPI;
            return kCipErrorConnectionFailure;
        }
    }

    io_conn->GeneralConnectionConfiguration();

    o_to_t = io_conn->o_to_t_ncp.ConnectionType();
    t_to_o = io_conn->t_to_o_ncp.ConnectionType();

    int     data_size;
    int     diff_size;
    bool    is_heartbeat;

    if( o_to_t != kIOConnTypeNull )    // setup consumer side
    {
        CIPSTER_ASSERT( io_conn->consuming_instance );

        // Vol1 3-5.4.1.10.2 Assumed Assembly Object Attribute (== 3)
        io_conn->conn_path.consuming_path.SetAttribute( 3 );

        CipAttribute* attribute = io_conn->consuming_instance->Attribute( 3 );
        ByteBuf* attr_data = (ByteBuf*) attribute->Data() ;

        // an assembly object should always have an attribute 3
        CIPSTER_ASSERT( attribute );

        data_size    = io_conn->o_to_t_ncp.ConnectionSize();
        diff_size    = 0;
        is_heartbeat = ( attr_data->size() == 0 );

        if( io_conn->trigger.Class() == kConnectionTransportClass1 )
        {
            data_size -= 2;     // remove 16-bit sequence count length
            diff_size += 2;
        }

        if( kOpenerConsumedDataHasRunIdleHeader && data_size >= 4
            // only expect a run idle header if it is not a heartbeat connection
            && !is_heartbeat )
        {
            data_size -= 4;       // remove the 4 bytes needed for run/idle header
            diff_size += 4;
        }

        if( ( io_conn->o_to_t_ncp.IsFixed() && data_size != attr_data->size() )
          ||  data_size >  attr_data->size() )
        {
            // wrong connection size
            aParams->corrected_o_to_t_size = attr_data->size() + diff_size;

            *extended_error = kConnectionManagerStatusCodeErrorInvalidOToTConnectionSize;

            CIPSTER_TRACE_INFO( "%s: assembly size(%d) != conn_size(%d)\n",
                    __func__, (int) attr_data->size(), data_size );
            return kCipErrorConnectionFailure;
        }
    }

    if( t_to_o != kIOConnTypeNull )     // setup producer side
    {
        CIPSTER_ASSERT( io_conn->producing_instance );

        // Vol1 3-5.4.1.10.2 Assumed Assembly Object Attribute (== 3)
        io_conn->conn_path.producing_path.SetAttribute( 3 );

        CipAttribute* attribute = io_conn->producing_instance->Attribute( 3 );
        ByteBuf* attr_data = (ByteBuf*) attribute->Data() ;

        // an assembly object should always have an attribute 3
        CIPSTER_ASSERT( attribute );

        data_size    = io_conn->t_to_o_ncp.ConnectionSize();
        diff_size    = 0;

        // Note: spec never talks about a heartbeat t_to_o connection, so why this?
        is_heartbeat = ( attr_data->size() == 0 );

        if( io_conn->trigger.Class() == kConnectionTransportClass1 )
        {
            data_size -= 2; // remove 16-bit sequence count length
            diff_size += 2;
        }

        if( kOpenerProducedDataHasRunIdleHeader && data_size >= 4
            // only have a run idle header if it is not a heartbeat connection
            && !is_heartbeat )
        {
            data_size -= 4;   // remove the 4 bytes needed for run/idle header
            diff_size += 4;
        }

        if( ( io_conn->t_to_o_ncp.IsFixed() && data_size != attr_data->size() )
          ||  data_size >  attr_data->size() )
        {
            // wrong connection size
            aParams->corrected_t_to_o_size = attr_data->size() + diff_size;

            *extended_error = kConnectionManagerStatusCodeErrorInvalidTToOConnectionSize;

            CIPSTER_TRACE_INFO( "%s: bytearray length != data_size\n", __func__ );
            return kCipErrorConnectionFailure;
        }
    }

    // If config data is present in forward_open request
    if( io_conn->conn_path.data_seg.HasAny() )
    {
        *extended_error = io_conn->handleConfigData();

        if( kConnectionManagerStatusCodeSuccess != *extended_error )
        {
            CIPSTER_TRACE_INFO( "%s: extended_error != 0\n", __func__ );
            return kCipErrorConnectionFailure;
        }
    }

    CipError result = io_conn->OpenCommunicationChannels( cpfd );

    if( result != kCipErrorSuccess )
    {
        *extended_error = kConnectionManagerStatusCodeSuccess;

        CIPSTER_TRACE_ERR( "%s: openCommunicationChannels() failed.  \n", __func__ );
        return result;
    }

    g_active_conns.Insert( io_conn );

    CheckIoConnectionEvent(
        io_conn->conn_path.consuming_path.GetInstanceOrConnPt(),
        io_conn->conn_path.producing_path.GetInstanceOrConnPt(),
        kIoConnectionEventOpened
        );

    return result;
}


EipStatus CipConn::Init( EipUint16 unique_connection_id )
{
    if( !GetCipClass( kCipConnectionClass ) )
    {
        CipClass* clazz = new CipConnectionClass();

        RegisterCipClass( clazz );

        g_incarnation_id = ( (EipUint32) unique_connection_id ) << 16;
    }

    return kEipStatusOk;
}


std::string ConnectionPath::Format() const
{
    std::string dest;

    if( config_path.HasAny() )
    {
        dest += "config_path=\"";
        dest += config_path.Format();
        dest += '"';
    }

    if( consuming_path.HasAny() )
    {
        if( dest.size() )
            dest += ' ';

        dest += "consuming_path=\"";
        dest += consuming_path.Format();
        dest += '"';
    }

    if( producing_path.HasAny() )
    {
        if( dest.size() )
            dest += ' ';

        dest += "producing_path=\"";
        dest += producing_path.Format();
        dest += '"';
    }

    return dest;
}


#if 0
static EipStatus create( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    CipClass* clazz = GetCipClass( kCipConnectionManagerClass );

    int id = find_unique_free_id( clazz );

    CipConn* conn = new CipConn( id );

    clazz->InstanceInsert( conn );
}
#endif

