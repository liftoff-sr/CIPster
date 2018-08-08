/*******************************************************************************
 * Copyright (c) 2011, Rockwell Automation, Inc.
 * Copyright (c) 2016-2018, SoftPLC Corporation.
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
        ConnTimeoutMultiplier aConnectionTimeoutMultiplier,
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
    o_to_t_RPI_usecs( a_O_to_T_RPI_usecs ),
    t_to_o_RPI_usecs( a_T_to_O_RPI_usecs ),
    corrected_o_to_t_size( 0 ),
    corrected_t_to_o_size( 0 ),
    consuming_instance( 0 ),
    producing_instance( 0 ),
    config_instance( 0 )
{
    SetTimeoutMultiplier( aConnectionTimeoutMultiplier );
}


ConnectionData& ConnectionData::SetTimeoutMultiplier( ConnTimeoutMultiplier aMultiplier )
{
    EipByte  value = 0;
    unsigned m = aMultiplier >> 3;

    while( m )
    {
        ++value;
        m >>= 1;
    }

    // CIPSTER_TRACE_INFO( "%s: value=%d\n", __func__, value );

    connection_timeout_multiplier_value = value;
    return *this;
}


static CipInstance* check_path( const CipAppPath& aPath,
            ConnMgrStatus* aExtError, const char* aCaller )
{
    if( !aPath.IsSufficient() )
    {
        CIPSTER_TRACE_ERR( "%s: aPath is not sufficient %s\n", __func__, aCaller );

        if( aExtError )
            *aExtError = kConnMgrStatusErrorInvalidSegmentTypeInPath;
        return NULL;
    }

    int class_id = aPath.GetClass();

    CipClass* clazz = GetCipClass( class_id );
    if( !clazz )
    {
        CIPSTER_TRACE_ERR( "%s: classid %d not found for %s\n",
            __func__, class_id, aCaller );

        if( class_id >= 0xc8 )   // reserved range of class ids
        {
            if( aExtError )
                *aExtError =  kConnMgrStatusErrorInvalidSegmentTypeInPath;
        }
        else
        {
            if( aExtError )
                *aExtError = kConnMgrStatusInconsistentApplicationPathCombo;
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
        if( aExtError )
            *aExtError = kConnMgrStatusErrorInvalidSegmentTypeInPath;

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

    connection_timeout_multiplier_value = in.get8();

    in += 3;         // skip over 3 reserved bytes.

    o_to_t_RPI_usecs = in.get32();
    o_to_t_ncp.Set( isLarge ? in.get32() : in.get16(), isLarge );

    t_to_o_RPI_usecs = in.get32();
    t_to_o_ncp.Set( isLarge ? in.get32() : in.get16(), isLarge );

    /*
        For Forward_Open services that establish a class 0/1 bound connection
        pair the following applies to the target and routers:

        1. The transport class bits apply to both the O->T and T->O connections.

        2. The direction bit should be Client (0) but in either case shall be ignored.

        3. The trigger bits only apply to the T->O connection.
    */

    trigger.Set( in.get8() );

    return in.data() - aInput.data();
}


CipError ConnectionData::DeserializeConnectionPath(
        BufReader aPath, ConnMgrStatus* aExtError )
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
        ConnMgrStatus sts = conn_path.port_segs.Key().Check();

        if( sts != kConnMgrStatusSuccess )
        {
            *aExtError = sts;
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

    instance1 = check_path( app_path1, aExtError, "app_path1" );
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
                    *aExtError = kConnMgrStatusInvalidConsumingApllicationPath;
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
                    *aExtError = kConnMgrStatusInvalidProducingApplicationPath;
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
                    *aExtError = kConnMgrStatusInvalidConsumingApllicationPath;
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
                    *aExtError = kConnMgrStatusInvalidConsumingApllicationPath;
                    goto L_exit_error;
                }

                instance3 = check_path( app_path3, NULL, "app_path3 O->T(non-null) T-O(non-null)" );
                if( !instance3 )
                {
                    *aExtError = kConnMgrStatusInvalidProducingApplicationPath;
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
                    *aExtError = kConnMgrStatusInvalidProducingApplicationPath;
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
                    *aExtError = kConnMgrStatusInvalidConsumingApllicationPath;
                    goto L_exit_error;
                }

                instance3 = check_path( app_path3, NULL, "app_path3 O->T(non-null) T-O(non-null)" );
                if( !instance3 )
                {
                    *aExtError = kConnMgrStatusInvalidProducingApplicationPath;
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

    switch( trigger.Class() )
    {
    case kConnTransportClass3:
        // Class3 connection end point has to be the message router instance 1
        if( conn_path.consuming_path.GetClass() != kCipMessageRouterClass ||
            conn_path.consuming_path.GetInstanceOrConnPt() != 1 )
        {
            *aExtError = kConnMgrStatusInconsistentApplicationPathCombo;
            goto L_exit_error;
        }
        break;

    case kConnTransportClass0:
    case kConnTransportClass1:
        // Vol1 3-5.4.1.12
        // "The direction bit should be Client (0) but in either case shall be ignored."
        trigger.SetServer( false );
        break;

    default:
        ;
    }

    CIPSTER_TRACE_INFO( "%s: forward_open conn_path: %s\n",
        __func__,
        conn_path.Format().c_str()
        );

    return kCipErrorSuccess;

L_exit_invalid:

    // Add CIPSTER_TRACE statements at specific goto sites above, not here.
    *aExtError = kConnMgrStatusErrorInvalidSegmentTypeInPath;

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

    .put8( connection_timeout_multiplier_value )

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


//-----<CipConn>----------------------------------------------------------------


CipConn::CipConn()
{
    Clear( false );
}


void CipConn::Clear( bool doConnectionDataToo )
{
    if( doConnectionDataToo )
        ConnectionData::Clear();

#if 0
    SetState( kConnStateNonExistent );
    SetInstanceType( kConnInstanceTypeExplicit );
#else
    state = kConnStateNonExistent;
    instance_type = kConnInstanceTypeExplicit;
#endif

    watchdog_timeout_action = kWatchdogTimeoutActionTransitionToTimedOut;

    eip_level_sequence_count_producing = 0;
    eip_level_sequence_count_consuming = 0;
    eip_level_sequence_count_consuming_first = true;

    sequence_count_producing = 0;
    sequence_count_consuming = 0;

    transmission_trigger_timer_usecs = 0;
    inactivity_watchdog_timer_usecs = 0;

    production_inhibit_timer_usecs = 0;

    memset( &send_address,    0, sizeof send_address );
    memset( &recv_address,    0, sizeof recv_address );
    memset( &openers_address, 0, sizeof openers_address );

    hook_close = NULL;
    hook_timeout = NULL;

    SetConsumingSocket( kSocketInvalid );
    SetProducingSocket( kSocketInvalid );

    encap_session = 0;

    next = NULL;
    prev = NULL;

    expected_packet_rate_usecs = 0;
}


const char* CipConn::ShowState( ConnState aState )
{
    static char unknown[16];

    switch( aState )
    {
    case kConnStateNonExistent:             return "NonExistent";
    case kConnStateConfiguring:             return "Configuring";
    case kConnStateWaitingForConnectionId:  return "WaitingForConnectionID";
    case kConnStateEstablished:             return "Established";
    case kConnStateTimedOut:                return "TimedOut";
    case kConnStateDeferredDelete:          return "DeferredDelete";
    case kConnStateClosing:                 return "Closing";
    default:
        snprintf( unknown, sizeof unknown, "?=0x%x", aState );
        return unknown;
    }
}

ConnMgrStatus CipConn::handleConfigData()
{
    ConnMgrStatus result = kConnMgrStatusSuccess;

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
            result = kConnMgrStatusErrorOwnershipConflict;
        }
    }

    // Put the data into the configuration assembly object
    else if( kEipStatusOk != NotifyAssemblyConnectedDataReceived( instance,
             BufReader( (EipByte*)  words.data(),  words.size() * 2 ) ) )
    {
        CIPSTER_TRACE_WARN( "Configuration data was invalid\n" );
        result = kConnMgrStatusInvalidConfigurationApplicationPath;
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


void CipConn::GeneralConnectionConfiguration()
{
    if( o_to_t_ncp.ConnectionType() == kIOConnTypePointToPoint )
    {
        // if we have a point to point connection for the O to T direction
        // the target shall choose the connection Id.
        consuming_connection_id = CipConn::NewConnectionId();

        CIPSTER_TRACE_INFO( "%s: new PointToPoint CID:0x%x\n",
            __func__, consuming_connection_id );
    }

    if( t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast )
    {
        // if we have a multi-cast connection for the T to O direction the
        // target shall choose the connection Id.
        producing_connection_id = CipConn::NewConnectionId();

        CIPSTER_TRACE_INFO( "%s: new Multicast PID:0x%x\n",
            __func__, producing_connection_id );
    }

    eip_level_sequence_count_producing = 0;
    sequence_count_producing = 0;

    eip_level_sequence_count_consuming = 0;
    eip_level_sequence_count_consuming_first = true;

    sequence_count_consuming = 0;

    watchdog_timeout_action = kWatchdogTimeoutActionAutoDelete;

    if( !trigger.IsServer() )  // Client Type Connection requested
    {
        SetExpectedPacketRateUSecs( t_to_o_RPI_usecs );

        // As soon as we are ready we should produce on the connection.
        // With the 0 here we will produce with the next timer tick
        // which should be sufficiently soon.
        SetTransmissionTriggerTimerUSecs( 0 );
    }
    else
    {
        // Server Type Connection requested
        SetExpectedPacketRateUSecs( o_to_t_RPI_usecs );
    }

    production_inhibit_timer_usecs = 0;

    SetPIT_USecs( 0 );

    // Vol1 3-4.5.2, says to set *initial* value to greater of 10 seconds or
    // "expected_packet_rate x connection_timeout_multiplier".  Initial value
    // is called a "pre-consumption" timeout value.
    if( TimeoutUSecs() )
        SetInactivityWatchDogTimerUSecs( std::max( TimeoutUSecs(), 10000000u ) );
    else
    {
        // this is not an erro
        CIPSTER_TRACE_INFO(
            "%s: no inactivity/Watchdog activated; epected_packet_rate is zero\n",
            __func__ );
    }
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
             && ProducingSocket() != kSocketInvalid )
            {
                CipConn* next_non_control_master =
                    GetNextNonControlMasterConnection( conn_path.producing_path.GetInstanceOrConnPt() );

                if( next_non_control_master )
                {
                    next_non_control_master->SetProducingSocket( ProducingSocket() );

                    next_non_control_master->send_address = send_address;

                    next_non_control_master->eip_level_sequence_count_producing =
                        eip_level_sequence_count_producing;

                    next_non_control_master->sequence_count_producing = sequence_count_producing;

                    SetProducingSocket( kSocketInvalid );

                    next_non_control_master->SetTransmissionTriggerTimerUSecs(
                        TransmissionTriggerTimerUSecs() );
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
    SetConsumingSocket( kSocketInvalid );

    CloseSocket( producing_socket );
    SetProducingSocket( kSocketInvalid );

    encap_session = 0;

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

    /*
        For class 0 and class 1 connections over EtherNet/IP, devices shall
        maintain an Encapsulation Sequence Number in the UDP payload defined in
        section 3-2.2.1. The Encapsulation Sequence Number shall be maintained
        per connection. Each time an EtherNet/IP device sends a CIP class 0 and
        class 1 packet, it shall increment the Encapsulation Sequence Number by
        1 for that connection. It shall increment even if the CIP Sequence Count
        (in the class 1 case) has not changed. If the receiving EtherNet/IP
        device receives a packet whose Encapsulation Sequence Number is less
        than or equal to the previously received packet, the packet with the
        smaller or the same Encapsulation Sequence Number shall be discarded.
    */
    ++eip_level_sequence_count_producing;

    //----<AddressInfoItem>-----------------------------------------------------

    Cpf cpfd(
        AddressItem(
            // use Sequenced Address Item if not Connection Class 0
            trigger.Class() == kConnTransportClass0 ?
                kCpfIdConnectedAddress : kCpfIdSequencedAddress,
            producing_connection_id,
            eip_level_sequence_count_producing ),
        kCpfIdConnectedDataItem
        );

    // Notify the application that Assembly data pertinent to provided instance
    // will be sent immediately after the call.  If application returns true,
    // this means the Assembly data has changed or should be reported as been
    // having updated depending on transportation class.
    if( BeforeAssemblyDataSend( producing_instance ) )
    {
        // Notify consumer that the data has changed or has been updated as
        // the case may according to this connection's transportation class.
        // Implementor of BeforeAssemblyDataSend() must know which of the
        // 2 strategies to employ based on connection class.
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

    if( trigger.Class() == kConnTransportClass1 )
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
                send_address,
                producing_socket,
                BufReader( g_message_data_reply_buffer, length )
                );

    return result;
}


EipStatus CipConn::HandleReceivedIoConnectionData( BufReader aInput )
{
    if( trigger.Class() == kConnTransportClass1 )
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
                if( kSocketInvalid != ProducingSocket() )
                {
                    // we are the controlling input only connection find a new controller

                    CipConn* next_non_control_master =
                        GetNextNonControlMasterConnection( conn_path.producing_path.GetInstanceOrConnPt() );

                    if( next_non_control_master )
                    {
                        next_non_control_master->SetProducingSocket( ProducingSocket() );
                        SetProducingSocket( kSocketInvalid );

                        next_non_control_master->SetTransmissionTriggerTimerUSecs(
                            TransmissionTriggerTimerUSecs() );
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

    CipConnMgrClass::CheckForTimedOutConnectionsAndCloseTCPConnections( this );

    Close();
}


CipError CipConn::openConsumingPointToPointConnection( Cpf* aCpf, ConnMgrStatus* aExtError )
{
    // For point-point connections, the point-point consumer shall choose
    // a UDP port number on which it will receive the connected data.

    // User may adjust g_my_io_udp_port to other than kEIP_IoUdpPort for this.

    SockAddr peers_destination(     // a.k.a "me"
                g_my_io_udp_port
                , ntohl( CipTCPIPInterfaceClass::IpAddress( 1 ) )
                );

    int socket = CreateUdpSocket( kUdpConsuming, peers_destination );

    if( socket == kSocketInvalid )
    {
        CIPSTER_TRACE_ERR( "%s: cannot create UDP socket on port:%d\n",
            __func__, g_my_io_udp_port );

        *aExtError = kConnMgrStatusTargetObjectOutOfConnections;
        return kCipErrorConnectionFailure;
    }

    // Vol2 3-3.9.6 return O->T Saii in the forward_open reply if I am
    // using a port different than the 0x08AE.
    if( g_my_io_udp_port != kEIP_IoUdpPort )
    {
        // See Vol1 table 3-3.3
        aCpf->AddTx( kSockAddr_O_T, peers_destination );
    }

    SockAddr remotes_source(                // a.k.a "peer"
                g_my_io_udp_port,           // ignored remote src port
                aCpf->ClientAddr()->Addr()  // IP of originator
                );

    SetConsumingSocket( socket );
    recv_address = remotes_source;

    return kCipErrorSuccess;
}


CipError CipConn::openProducingPointToPointConnection( Cpf* aCpf, ConnMgrStatus* aExtError )
{
    CIPSTER_ASSERT( aCpf->HasClient() );

    int destination_port;

    if( const SockAddr* saii = aCpf->SaiiRx( kSockAddr_T_O ) )
    {
        // If aCpf has a kSockAddr_T_O, use this originator provided port
        // as the peer for this Point to Point CIP connection on which the
        // originator is the consumer and has the right to choose port number.

        destination_port = saii->Port();

        if( destination_port != kEIP_IoUdpPort )
        {
            CIPSTER_TRACE_INFO( "%s: client provided non-standard port:%d\n",
                __func__, saii->Port() );
        }
    }
    else
        destination_port = kEIP_IoUdpPort;

    SockAddr destination(
            destination_port,
            aCpf->ClientAddr()->Addr()  // TCP client originator
            );

    SockAddr source(
            g_my_io_udp_port    // I chose my source port consistently for non-multicast producing
            ,ntohl( CipTCPIPInterfaceClass::IpAddress( 1 ) )
            );

    int socket = CreateUdpSocket( kUdpProducing, source );

    if( socket == kSocketInvalid )
    {
        CIPSTER_TRACE_ERR( "%s: cannot create UDP socket on port:%d\n",
                __func__, source.Port() );

        *aExtError = kConnMgrStatusTargetObjectOutOfConnections;
        return kCipErrorConnectionFailure;
    }

    SetProducingSocket( socket );
    send_address = destination;

    return kCipErrorSuccess;
}


CipError CipConn::openProducingMulticastConnection( Cpf* aCpf, ConnMgrStatus* aExtError )
{
    // producing multicast connections have to consider the rules that apply for
    // application connection types.

    CipConn* existing = GetExistingProducerMulticastConnection(
                            conn_path.producing_path.GetInstanceOrConnPt() );

    // If we are the first connection producing for the given Input Assembly
    if( !existing )
    {
        return openMulticastConnection( kUdpProducing, aCpf, aExtError );
    }
    else
    {
        // inform our originator about the correct connection id
        producing_connection_id = existing->producing_connection_id;
    }

    // we have a connection, reuse the data and the socket

    if( kConnInstanceTypeIoExclusiveOwner == InstanceType() )
    {
        /* exclusive owners take the socket and further manage the connection
         * especially in the case of time outs.
         */
        SetProducingSocket( existing->ProducingSocket() );

        existing->SetProducingSocket( kSocketInvalid );
    }
    else    // this connection will not produce the data
    {
        SetProducingSocket( kSocketInvalid );
    }

    SockAddr destination(
            kEIP_IoUdpPort,         // multicast: no use of g_my_io_udp_port here
            ntohl( CipTCPIPInterfaceClass::MultiCast( 1 ).starting_multicast_address )
            );

    aCpf->AddTx( kSockAddr_T_O, destination );

    send_address = destination;

    return kCipErrorSuccess;
}


CipError CipConn::openMulticastConnection( UdpDirection aDirection,
        Cpf* aCpf, ConnMgrStatus* aExtError )
{
    // see Vol2 3-3.9.4 Sockaddr Info Item Placement and Errors
    if( aDirection == kUdpConsuming )
    {
        const SockAddr* saii = aCpf->SaiiRx( kSockAddr_O_T );

        // For consuming multicast connections the originator chooses the
        // multicast address to use, so *must* supply the kSockAddr_O_T

        if( !saii )
        {
            CIPSTER_TRACE_ERR(
                "%s: missing required SockAddr Info Item for consuming.\n",
                __func__ );

            *aExtError = kConnMgrStatusErrorParameterErrorInUnconnectedSendService;
            return kCipErrorConnectionFailure;
        }

        if( saii->Family() != AF_INET )
        {
            CIPSTER_TRACE_ERR(
                "%s: originator's T->O SockAddr Info Item is invalid.\n",
                __func__ );

            *aExtError = kConnMgrStatusErrorParameterErrorInUnconnectedSendService;
            return kCipErrorConnectionFailure;
        }

        SockAddr remotes_destination = *saii;

        // Vol2 3-3.9.5: originator is allowed to send garbage here, set it
        // to the required value to ensure it is valid, and for multicast it
        // must be the registered port.
        remotes_destination.SetPort( kEIP_IoUdpPort );

        int socket = CreateUdpSocket( aDirection, remotes_destination );

        if( socket == kSocketInvalid )
        {
            CIPSTER_TRACE_ERR( "%s: cannot create consuming UDP socket\n", __func__ );
            *aExtError = kConnMgrStatusTargetObjectOutOfConnections;
            return kCipErrorConnectionFailure;
        }

        SetConsumingSocket( socket );
        recv_address = remotes_destination;
    }

    else
    {
        SockAddr source(
                g_my_io_udp_port,
                ntohl( CipTCPIPInterfaceClass::MultiCast( 1 ).starting_multicast_address )
                );

        SockAddr destination(
                kEIP_IoUdpPort,         // multicast cannot use g_my_io_udp_port here
                ntohl( CipTCPIPInterfaceClass::MultiCast( 1 ).starting_multicast_address )
                );

        int socket = CreateUdpSocket( aDirection, source );

        if( socket == kSocketInvalid )
        {
            CIPSTER_TRACE_ERR( "%s: cannot create producing UDP socket\n",
                __func__ );

            *aExtError = kConnMgrStatusTargetObjectOutOfConnections;
            return kCipErrorConnectionFailure;
        }

        aCpf->AddTx( kSockAddr_T_O, destination );

        SetProducingSocket( socket );
        send_address = destination;
    }

    CIPSTER_TRACE_INFO( "%s: opened OK\n", __func__ );

    return kCipErrorSuccess;
}


CipError CipConn::openCommunicationChannels( Cpf* aCpf, ConnMgrStatus* aExtError )
{
    IOConnType  o_to_t = o_to_t_ncp.ConnectionType();
    IOConnType  t_to_o = t_to_o_ncp.ConnectionType();

    // one, both, or no consuming/producing ends based on IOConnType for each

    //----<Consuming End>-------------------------------------------------------

    if( o_to_t == kIOConnTypeMulticast )
    {
        CipError result = openMulticastConnection( kUdpConsuming, aCpf, aExtError );

        if( result )
        {
            CIPSTER_TRACE_ERR( "%s: error in O->T Multicast connection\n", __func__ );
            return result;
        }
    }

    else if( o_to_t == kIOConnTypePointToPoint )
    {
        CipError result = openConsumingPointToPointConnection( aCpf, aExtError );

        if( result )
        {
            CIPSTER_TRACE_ERR( "%s: error in O->T PointToPoint connection\n", __func__ );
            return result;
        }
    }

    //----<Producing End>-------------------------------------------------------

    if( t_to_o == kIOConnTypeMulticast )
    {
        CipError result = openProducingMulticastConnection( aCpf, aExtError );

        if( result )
        {
            CIPSTER_TRACE_ERR( "%s: error in T->O Multicast connection\n", __func__ );
            return result;
        }
    }

    else if( t_to_o == kIOConnTypePointToPoint )
    {
        CipError result = openProducingPointToPointConnection( aCpf, aExtError );

        if( result )
        {
            CIPSTER_TRACE_ERR( "%s: error in T->O PointToPoint connection\n", __func__ );
            return result;
        }
    }

    // Save TCP client's IP address in order to qualify a future forward_close.
    if( t_to_o != kIOConnTypeNull || o_to_t != kIOConnTypeNull )
    {
        CIPSTER_ASSERT( aCpf->HasClient() );
        openers_address = *aCpf->ClientAddr();
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
        Cpf* aCpf, ConnMgrStatus* aExtError )
{
    // currently we allow I/O connections only to assembly objects

    CipConn* c = GetIoConnectionForConnectionData( aParams, aExtError );

    if( !c )
    {
        CIPSTER_TRACE_ERR(
            "%s: no reserved IO connection was found for:\n"
            " %s.\n"
            " All anticipated IO connections must be reserved with Configure<*>ConnectionPoint()\n",
            __func__,
            c->conn_path.Format().c_str()
            );

        return kCipErrorConnectionFailure;
    }

    return c->Activate( aCpf, aExtError,
                &c->corrected_o_to_t_size,
                &c->corrected_t_to_o_size
                );
}


CipError CipConn::Activate( Cpf* aCpf, ConnMgrStatus* aExtError,
        EipUint16* aCorrectedOTz , EipUint16* aCorrectedTOz )
{
    IOConnType o_to_t;
    IOConnType t_to_o;

    // Both Change of State and Cyclic triggers use the Transmission Trigger Timer
    // according to Vol1_3.19_3-4.4.3.7.

    if( trigger.Trigger() != kConnTriggerTypeCyclic )
    {
        // trigger is not cyclic, it is Change of State here.

        if( !conn_path.port_segs.HasPIT() )
        {
            // saw no PIT segment in the connection path, set PIT to one fourth of RPI
            SetPIT_USecs( t_to_o_RPI_usecs / 4 );
        }

        // if production inhibit time has been provided it needs to be smaller than the RPI
        else if( GetPIT_USecs() > t_to_o_RPI_usecs )
        {
            // see section C-1.4.3.3
            *aExtError = kConnMgrStatusErrorPITGreaterThanRPI;
            return kCipErrorConnectionFailure;
        }
    }

    GeneralConnectionConfiguration();

    o_to_t = o_to_t_ncp.ConnectionType();
    t_to_o = t_to_o_ncp.ConnectionType();

    int     data_size;
    int     diff_size;
    bool    is_heartbeat;

    if( o_to_t != kIOConnTypeNull )    // setup consumer side
    {
        CIPSTER_ASSERT( consuming_instance );

        // Vol1 3-5.4.1.10.2 Assumed Assembly Object Attribute (== 3)
        conn_path.consuming_path.SetAttribute( 3 );

        CipAttribute* attribute = consuming_instance->Attribute( 3 );
        ByteBuf* attr_data = (ByteBuf*) attribute->Data() ;

        // an assembly object should always have an attribute 3
        CIPSTER_ASSERT( attribute );

        data_size    = o_to_t_ncp.ConnectionSize();
        diff_size    = 0;
        is_heartbeat = ( attr_data->size() == 0 );

        if( trigger.Class() == kConnTransportClass1 )
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

        if( ( o_to_t_ncp.IsFixed() && data_size != attr_data->size() )
          ||  data_size >  attr_data->size() )
        {
            // wrong connection size
            *aCorrectedOTz = attr_data->size() + diff_size;

            *aExtError = kConnMgrStatusErrorInvalidOToTConnectionSize;

            CIPSTER_TRACE_INFO(
                "%s: assembly size(%d) != requested conn_size(%d) for consuming:'%s'\n"
                " corrected_o_to_t:%d\n",
                __func__,
                (int) attr_data->size(),
                data_size,
                conn_path.consuming_path.Format().c_str(),
                *aCorrectedOTz
                );

            return kCipErrorConnectionFailure;
        }

        CIPSTER_TRACE_INFO(
            "%s: requested conn_size(%d) is OK for consuming:'%s'\n",
            __func__,
            data_size + diff_size,
            conn_path.consuming_path.Format().c_str()
            );
    }

    if( t_to_o != kIOConnTypeNull )     // setup producer side
    {
        CIPSTER_ASSERT( producing_instance );

        // Vol1 3-5.4.1.10.2 Assumed Assembly Object Attribute (== 3)
        conn_path.producing_path.SetAttribute( 3 );

        CipAttribute* attribute = producing_instance->Attribute( 3 );
        ByteBuf* attr_data = (ByteBuf*) attribute->Data() ;

        // an assembly object should always have an attribute 3
        CIPSTER_ASSERT( attribute );

        data_size    = t_to_o_ncp.ConnectionSize();
        diff_size    = 0;

        // Note: spec never talks about a heartbeat t_to_o connection, so why this?
        is_heartbeat = ( attr_data->size() == 0 );

        if( trigger.Class() == kConnTransportClass1 )
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

        if( ( t_to_o_ncp.IsFixed() && data_size != attr_data->size() )
          ||  data_size >  attr_data->size() )
        {
            // wrong connection size
            *aCorrectedTOz = attr_data->size() + diff_size;

            *aExtError = kConnMgrStatusErrorInvalidTToOConnectionSize;

            CIPSTER_TRACE_INFO(
                "%s: assembly size(%d) != requested conn_size(%d) for producing:'%s'\n"
                " corrected_t_to_o:%d\n",
                __func__,
                (int) attr_data->size(),
                data_size,
                conn_path.producing_path.Format().c_str(),
                *aCorrectedTOz
                );
            return kCipErrorConnectionFailure;
        }

        CIPSTER_TRACE_INFO(
            "%s: requested conn_size(%d) is OK for producing:'%s'\n",
            __func__,
            data_size + diff_size,
            conn_path.producing_path.Format().c_str()
            );
    }

    // If config data is present in forward_open request
    if( conn_path.data_seg.HasAny() )
    {
        *aExtError = handleConfigData();

        if( kConnMgrStatusSuccess != *aExtError )
        {
            CIPSTER_TRACE_INFO( "%s: extended_error != 0\n", __func__ );
            return kCipErrorConnectionFailure;
        }
    }

    CipError result = openCommunicationChannels( aCpf, aExtError );

    if( result )
    {
        // CIPSTER_TRACE_ERR( "%s: openCommunicationChannels() failed.  \n", __func__ );
        return result;
    }

    g_active_conns.Insert( this );

    CheckIoConnectionEvent(
        conn_path.consuming_path.GetInstanceOrConnPt(),
        conn_path.producing_path.GetInstanceOrConnPt(),
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

