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


//-----<ConnectionPath>---------------------------------------------------------

int ConnectionPath::Deserialize( BufReader aInput, int aCtl )
{
    BufReader       in = aInput;
    const char*     working_on = "nothing yet";     // for catch blocks.

    // clear all CipAppPaths and later assign those seen below
    Clear();

    try
    {
        if( in.size() )
        {
            working_on = "PortSegmentGroup: ";
            in += port_segs.DeserializePortSegmentGroup( in, aCtl );
        }

        /*
           There can be 1-3 application_paths in a connection_path. Depending on
           the O->T_connection_parameters and T->O_connection_parameters fields
           and the presence of a data segment, one or more encoded application
           paths shall be specified. In general, the application paths are in
           the order of Configuration path, Consumption path, and Production
           path. However, a single encoded path can be used when configuration,
           consumption, and/or production use the same path. See Vol1 table
           3-5.13.
        */

        if( in.size() )
        {
            working_on = "app_path1: ";
            in += app_path1.DeserializeAppPath( in, NULL, aCtl );
        }

        if( in.size() )
        {
            working_on = "app_path2: ";
            in += app_path2.DeserializeAppPath( in, &app_path1, aCtl );
        }

        if( in.size() )
        {
            working_on = "app_path3: ";
            in += app_path3.DeserializeAppPath( in, &app_path2, aCtl );
        }

        if( in.size() )     // There could be a data segment
        {
            working_on = "data_segment: ";
            in += data_seg.DeserializeDataSegment( in, aCtl );
        }
    }
    catch( const std::overflow_error& ov )
    {
        std::string m = __func__;
        m += ": ERROR deserializing ";
        m += working_on;
        m += ov.what();
        throw std::overflow_error( m );
    }
    catch( const std::runtime_error& ex )
    {
        std::string m = __func__;
        m += ": ERROR deserializing ";
        m += working_on;
        m += ex.what();
        throw std::runtime_error( m );
    }

    if( in.size() )   // should have consumed all of it by now
    {
        // Generally a connection_path will always be an even number of bytes
        // because it is specified as a number of words in normal use. So if
        // there is another single byte and we were given an even number to
        // parse, assume it is a pad byte.
        if( in.size() == 1 && !(aInput.size() & 1) )
        {
            ++in;   // padding
        }
        else
        {
            std::string m = __func__;
            m += ": unknown extra segments in connection path";
            throw std::runtime_error( m );
        }
    }

    return in.data() - aInput.data();
}


int ConnectionPath::Serialize( BufWriter aOutput, int aCtl ) const
{
    BufWriter out = aOutput;

    if( port_segs.HasAny() )
        out += port_segs.Serialize( out, aCtl );

    if( !(aCtl & CTL_UNCOMPRESSED_EPATH) )
    {
        const CipAppPath* prev = NULL;

        for( int i=0; i < DIM(app_path);  ++i )
        {
            if( !app_path[i].HasAny() )
                continue;

            const CipAppPath& cur = app_path[i];

            int ctl = aCtl;

            if( prev )
            {
                if( prev->GetClass() == cur.GetClass() )
                    ctl |= CTL_OMIT_CLASS;

                if( ( cur.HasConnPt() || cur.HasAttribute() ) && prev->GetInstance() == cur.GetInstance() )
                    ctl |= CTL_OMIT_INSTANCE;

                if( cur.HasAttribute() && prev->GetConnPt() == cur.GetConnPt() )
                    ctl |= CTL_OMIT_CONN_PT;
            }

            out += cur.Serialize( out, ctl );

            prev = &cur;
        }
    }

    else
    {
        if( app_path1.HasAny() )
            out += app_path1.Serialize( out, aCtl );

        if( app_path2.HasAny() )
            out += app_path2.Serialize( out, aCtl );

        if( app_path3.HasAny() )
            out += app_path3.Serialize( out, aCtl );
    }

    if( data_seg.HasAny() )
        out += data_seg.Serialize( out, aCtl );

    return out.data() - aOutput.data();
}


int ConnectionPath::SerializedCount( int aCtl ) const
{
    uint8_t     stack_buf[128];
    BufWriter   out( stack_buf, sizeof stack_buf );

    // Hose it to a junk buffer and measure it.  This strategy:
    // 1) simplifies code maintenance
    // 2) ensures agreement between the 2 functions
    // 3) is compact.
    return Serialize( out, aCtl );
}


std::string ConnectionPath::Format() const
{
    std::string dest;

    if( ConfigPath().HasAny() )
    {
        dest += "(config ";
        dest += ConfigPath().Format();
        dest += ')';
    }

    if( ConsumingPath().HasAny() )
    {
        dest += "(consuming ";
        dest += ConsumingPath().Format();
        dest += ')';
    }

    if( ProducingPath().HasAny() )
    {
        dest += "(producing ";
        dest += ProducingPath().Format();
        dest += ')';
    }

    return dest;
}


// Something to point to when one of the app_paths is empty.
CipAppPath ConnectionPath::HasAny_No;


//-----<ConnectionData>---------------------------------------------------------

CipUint ConnectionData::serial_number_allocator;

ConnectionData::ConnectionData(
        uint8_t aPriorityTimeTick,
        uint8_t aTimeoutTicks,
        CipUdint aConsumingConnectionId,
        CipUdint aProducingConnectionId,
        CipUint aConnectionSerialNumber,
        CipUint aOriginatorVendorId,
        CipUdint aOriginatorSerialNumber,
        ConnTimeoutMultiplier aConnectionTimeoutMultiplier,
        CipUdint aConsumingRPI_usecs,
        CipUdint aProcudingRPI_usecs
        ) :
    priority_timetick( aPriorityTimeTick ),
    timeout_ticks( aTimeoutTicks ),
    consuming_connection_id( aConsumingConnectionId ),
    producing_connection_id( aProducingConnectionId ),
    connection_serial_number( aConnectionSerialNumber ),
    originator_vendor_id( aOriginatorVendorId ),
    originator_serial_number( aOriginatorSerialNumber ),
    consuming_RPI_usecs( aConsumingRPI_usecs ),
    producing_RPI_usecs( aProcudingRPI_usecs ),
    corrected_consuming_size( 0 ),
    corrected_producing_size( 0 ),
    consuming_instance( 0 ),
    producing_instance( 0 ),
    config_instance( 0 ),
    connection_timeout_multiplier_value( 0 ),
    mgmnt_class( 0 ),

    // kRealTimeFmtHeartbeat is set later when applicable.
    producing_fmt( kCIPsterProducedDataHasRunIdleHeader ?
        kRealTimeFmt32BitHeader : kRealTimeFmtModeless ),

    consuming_fmt( kCIPsterConsumedDataHasRunIdleHeader ?
        kRealTimeFmt32BitHeader : kRealTimeFmtModeless )
{
}


void ConnectionData::SetOriginatorTimeoutMSecs( unsigned aTimeoutMSecs )
{
    // use the smallest possible tick time
    unsigned tick_time;
    for( tick_time = 0; tick_time <= 15;  ++tick_time )
    {
        if( RequestMSecs( tick_time, 255 ) >= aTimeoutMSecs )
            break;
    }

    CIPSTER_ASSERT( tick_time <= 15 );  // aTimeoutMSecs could be illegal (>8355840)

    unsigned time_per_tick = 1 << tick_time;
    unsigned tick_counts   = (aTimeoutMSecs + time_per_tick - 1) / time_per_tick;

    CIPSTER_TRACE_INFO(
        "%s: tick_time:%u  tick_counts:%u  RequestMSecs:%u  aTimeoutMSecs:%u\n",
        __func__,
        tick_time,
        tick_counts,
        RequestMSecs( tick_time, tick_counts ),
        aTimeoutMSecs
        );

    CIPSTER_ASSERT( RequestMSecs( tick_time, tick_counts ) >= aTimeoutMSecs );

    SetTickTime( tick_time );
    SetTimeoutTicks( tick_counts );
}


ConnectionData& ConnectionData::SetTimeoutMultiplier( ConnTimeoutMultiplier aMultiplier )
{
    uint8_t  value = 0;
    unsigned m = aMultiplier >> 3;

    while( m )
    {
        ++value;
        m >>= 1;
    }

    //CIPSTER_TRACE_INFO( "%s: value=%d\n", __func__, value );

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
            *aExtError = kConnMgrStatusInvalidSegmentTypeInPath;
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
                *aExtError =  kConnMgrStatusInvalidSegmentTypeInPath;
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
            *aExtError = kConnMgrStatusInvalidSegmentTypeInPath;

        return NULL;
    }

    return inst;
}


int ConnectionData::DeserializeForwardOpenRequest( BufReader aInput, bool isLarge )
{
    BufReader in = aInput;

    // Vol1 Table 3-5.17 Forward_Open / Large_Forward_Open Request

    priority_timetick = in.get8();
    timeout_ticks     = in.get8();

    SetConsumingConnectionId( in.get32() );     // O->T
    SetProducingConnectionId( in.get32() );     // T->O

    //-----<ConnectionTriad>----------------------------------------------------
    connection_serial_number = in.get16();
    originator_vendor_id     = in.get16();
    originator_serial_number = in.get32();
    //-----</ConnectionTriad>---------------------------------------------------

    connection_timeout_multiplier_value = in.get8();

    in += 3;         // skip over 3 reserved bytes.

    consuming_RPI_usecs = in.get32();
    consuming_ncp.Set( isLarge ? in.get32() : in.get16(), isLarge );

    producing_RPI_usecs = in.get32();
    producing_ncp.Set( isLarge ? in.get32() : in.get16(), isLarge );

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


int ConnectionData::Serialize( BufWriter aOutput, int aCtl ) const
{
    BufWriter out = aOutput;

    /*
        When executing this function, the host is acting as an "originator".
        Originator has a reverse interpretation of "Consuming" and "Producing"
        than does a target with respect to O->T nomenclature.

            O->T => Consuming, and T->O => Producing.   Target's perspective
            O->T => Producing, and T->O => Consuming.   Originator's perspective

        So swap the order of these values accordingly as we send
        them so that the ConnectionData accessors are always correct, regardless
        of the machine that they are executing on. Producing always means
        producing and Consuming always means consuming.
    */
    if( aCtl & CTL_FORWARD_OPEN )
    {
        // Vol1 Table 3-5.17
        out.put8( priority_timetick )
        .put8( timeout_ticks )

        .put32( ProducingConnectionId() )           // O->T
        .put32( ConsumingConnectionId() )           // T->O

        // The Connection Triad
        .put16( connection_serial_number )
        .put16( originator_vendor_id )
        .put32( originator_serial_number )

        .put8( connection_timeout_multiplier_value )

        .fill( 3 )      // 3 reserved bytes.

        .put32( ProducingRPI() );

        ProducingNCP().Serialize( out );

        out.put32( ConsumingRPI() );
        ConsumingNCP().Serialize( out );

        trigger.Serialize( out );

        if( !(CTL_OMIT_CONN_PATH & aCtl) )
        {
            uint8_t* cpathz_loc = out.data();   // note Connection_Path_Size location

            out += 1;   // skip over Connection_Path_Size location

            int byte_count = conn_path.Serialize( out, aCtl );

            out += byte_count;

            *cpathz_loc = byte_count / 2;       // words, not bytes
        }
    }

    else if( aCtl & CTL_FORWARD_CLOSE )
    {
        // Vol1 Table 3-5.21 Forward_Close Service Request
        out.put8( priority_timetick )
        .put8( timeout_ticks )

        // The Connection Triad
        .put16( connection_serial_number )
        .put16( originator_vendor_id )
        .put32( originator_serial_number );

        if( !(CTL_OMIT_CONN_PATH & aCtl) )
        {
            uint8_t* cpathz_loc = out.data();   // note Connection_Path_Size location

            out += 1;   // skip over Connection_Path_Size location &

            out.put8( 0 );      // Reserved

            int byte_count = conn_path.Serialize( out, aCtl );

            out += byte_count;

            *cpathz_loc = byte_count / 2;       // words, not bytes
        }
    }

    return out.data() - aOutput.data();
}


int ConnectionData::SerializedCount( int aCtl ) const
{
    int count = 0;

    if( aCtl & CTL_FORWARD_OPEN )
    {
        count += 31 + consuming_ncp.SerializedCount()
                    + producing_ncp.SerializedCount()
                    + 1 // connection path size USINT
                    + conn_path.SerializedCount( aCtl );
    }

    if( aCtl & CTL_FORWARD_CLOSE )
    {
        count += 12 + conn_path.SerializedCount( aCtl );
    }

    return count;
}


int ConnectionData::DeserializeForwardCloseRequest( BufReader aInput )
{
    BufReader in = aInput;

    priority_timetick = in.get8();
    timeout_ticks     = in.get8();

    //-----<ConnectionTriad>----------------------------------------------------
    connection_serial_number = in.get16();
    originator_vendor_id     = in.get16();
    originator_serial_number = in.get32();
    //-----</ConnectionTriad>---------------------------------------------------

    return in.data() - aInput.data();
}


CipError ConnectionData::ResolveInstances( ConnMgrStatus* aExtError )
{
    CipInstance*    instance1;      // corresponds to app_path1
    CipInstance*    instance2;      // corresponds to app_path2
    CipInstance*    instance3;      // corresponds to app_path3

    config_instance    = 0;
    consuming_instance = 0;
    producing_instance = 0;

    instance1 = check_path( conn_path.app_path1, aExtError, "app_path1" );
    if( !instance1 )
    {
        goto L_exit_error;
    }

    mgmnt_class = conn_path.app_path1.GetClass();

    IOConnType o_t;
    IOConnType t_o;

    o_t = consuming_ncp.ConnectionType();
    t_o = producing_ncp.ConnectionType();

    int path_count;
    path_count = 1 + conn_path.app_path2.HasAny() + conn_path.app_path3.HasAny();

    int8_t config_path, consuming_path, producing_path;

    // Set all three to default to not used unless set otherwise below.
    config_path = consuming_path = producing_path = -1;

    // This 'if else if' block is coded to look like table
    // 3-5.13; which should reduce risk of error
    if( o_t == kIOConnTypeNull && t_o == kIOConnTypeNull )
    {
        if( conn_path.data_seg.HasAny() )
        {
            // conn_path.app_path1 is for configuration.
            config_path     = 0;    // conn_path.app_path1;
            config_instance = instance1;

            // In this context, it's Ok to ignore app_path2 and app_path3
            // if present, also reflected in path_count.
        }
        else
        {
            // app_path1 is for pinging via a "not matching" connection.
            if( path_count != 1 )
            {
                CIPSTER_TRACE_ERR(
                    "%s: doubly null connection types takes only 1 app_path\n",
                    __func__ );
                goto L_exit_invalid;
            }

            // app_path1 is for pinging, but connection needs to be non-matching and
            // app_path1 must be Identity instance 1.  Caller can check.  Save
            // app_path1 in consuming_path for ping handler elsewhere.
            consuming_path     = 0;     // conn_path.app_path1;
            consuming_instance = instance1;
        }
    }

    // Row 2
    else if( o_t != kIOConnTypeNull && t_o == kIOConnTypeNull )
    {
        if( conn_path.data_seg.HasAny() )
        {
            switch( path_count )
            {
            case 1:
                // app_path1 is for both configuration and consumption
                config_path    = 0;     // conn_path.app_path1;
                consuming_path = 0;     // conn_path.app_path1;

                config_instance    = instance1;
                consuming_instance = instance1;
                break;

            case 2:
                instance2 = check_path( conn_path.app_path2, NULL, "app_path2 O->T(non-null) T->O(null)" );
                if( !instance2 )
                {
                    *aExtError = kConnMgrStatusInvalidConsumingApllicationPath;
                    goto L_exit_error;
                }

                // app_path1 is for configuration, app_path2 is for consumption
                config_path    = 0;     // conn_path.app_path1;
                consuming_path = 1;     // conn_path.app_path2;

                config_instance    = instance1;
                consuming_instance = instance2;
                break;
            case 3:
                goto L_exit_invalid;
            }
        }
        else
        {
            switch( path_count )
            {
            case 1:
                // app_path1 is for consumption
                consuming_path     = 0;     // conn_path.app_path1;
                consuming_instance = instance1;
                break;
            case 2:
            case 3:
                goto L_exit_invalid;
            }
        }
    }

    // Row 3
    else if( o_t == kIOConnTypeNull && t_o != kIOConnTypeNull )
    {
        if( conn_path.data_seg.HasAny() )
        {
            switch( path_count )
            {
            case 1:
                // app_path1 is for both configuration and production
                config_path    = 0;     // conn_path.app_path1;
                producing_path = 0;     // conn_path.app_path1;

                config_instance    = instance1;
                producing_instance = instance1;
                break;

            case 2:
                instance2 = check_path( conn_path.app_path2, NULL, "app_path2 O->T(null) T->O(non-null)" );
                if( !instance2 )
                {
                    *aExtError = kConnMgrStatusInvalidProducingApplicationPath;
                    goto L_exit_error;
                }

                // app_path1 is for configuration, app_path2 is for production
                config_path    = 0;     // conn_path.app_path1;
                producing_path = 1;     // conn_path.app_path2;

                config_instance    = instance1;
                producing_instance = instance2;
                break;

            case 3:
                goto L_exit_invalid;
            }
        }
        else
        {
            switch( path_count )
            {
            case 1:
                // app_path1 is for production
                producing_path     = 0;     // conn_path.app_path1;
                producing_instance = instance1;
                break;

            case 2:
            case 3:
                goto L_exit_invalid;
            }
        }
    }

    // Row 4
    else if( o_t != kIOConnTypeNull && t_o != kIOConnTypeNull )
    {
        if( conn_path.data_seg.HasAny() )
        {
            switch( path_count )
            {
            case 1:
                // app_path1 is for configuration, consumption, and production
                config_path    = 0;     // conn_path.app_path1;
                consuming_path = 0;     // conn_path.app_path1;
                producing_path = 0;     // conn_path.app_path1;

                config_instance    = instance1;
                consuming_instance = instance1;
                producing_instance = instance1;
                break;

            case 2:
                instance2 = check_path( conn_path.app_path2, NULL, "app_path2 O->T(non-null) T->O(non-null)" );
                if( !instance2 )
                {
                    *aExtError = kConnMgrStatusInvalidConsumingApllicationPath;
                    goto L_exit_error;
                }

                // app_path1 is for configuration, app_path2 is for consumption & production
                config_path    = 0;     // conn_path.app_path1;
                consuming_path = 1;     // conn_path.app_path2;
                producing_path = 1;     // conn_path.app_path2;

                config_instance    = instance1;
                consuming_instance = instance2;
                producing_instance = instance2;
                break;

            case 3:
                instance2 = check_path( conn_path.app_path2, NULL, "app_path2 O->T(non-null) T->O(non-null)" );
                if( !instance2 )
                {
                    *aExtError = kConnMgrStatusInvalidConsumingApllicationPath;
                    goto L_exit_error;
                }

                instance3 = check_path( conn_path.app_path3, NULL, "app_path3 O->T(non-null) T->O(non-null)" );
                if( !instance3 )
                {
                    *aExtError = kConnMgrStatusInvalidProducingApplicationPath;
                    goto L_exit_error;
                }

                // app_path1 is for configuration, app_path2 is for consumption, app_path3 is for production
                config_path    = 0;     // conn_path.app_path1;
                consuming_path = 1;     // conn_path.app_path2;
                producing_path = 2;     // conn_path.app_path3;

                config_instance    = instance1;
                consuming_instance = instance2;
                producing_instance = instance3;
            }
        }
        else
        {
            switch( path_count )
            {
            case 1:
                // app_path1 is for consumption and production
                consuming_path = 0;     // conn_path.app_path1;
                producing_path = 0;     // conn_path.app_path1;

                consuming_instance = instance1;
                producing_instance = instance1;
                break;

            case 2:
                // app_path1 is for consumption, app_path2 is for production
                instance2 = check_path( conn_path.app_path2, NULL, "app_path2 O->T(non-null) T->O(non-null)" );
                if( !instance2 )
                {
                    *aExtError = kConnMgrStatusInvalidProducingApplicationPath;
                    goto L_exit_error;
                }

                consuming_path = 0;     // conn_path.app_path1;
                producing_path = 1;     // conn_path.app_path2;

                consuming_instance = instance1;
                producing_instance = instance2;
                break;

            case 3:
                // first path is ignored, app_path2 is for consumption, app_path3 is for production
                instance2 = check_path( conn_path.app_path2, NULL, "app_path2 O->T(non-null) T->O(non-null)" );
                if( !instance2 )
                {
                    *aExtError = kConnMgrStatusInvalidConsumingApllicationPath;
                    goto L_exit_error;
                }

                instance3 = check_path( conn_path.app_path3, NULL, "app_path3 O->T(non-null) T->O(non-null)" );
                if( !instance3 )
                {
                    *aExtError = kConnMgrStatusInvalidProducingApplicationPath;
                    goto L_exit_error;
                }

                // There is no data segment, so we ignore app_path1, per spec.
                CIPSTER_TRACE_INFO(
                    "%s: ignoring config_path because of missing data segment\n",
                    __func__ );

                consuming_path = 1;     // conn_path.app_path2;
                producing_path = 2;     // conn_path.app_path3;

                consuming_instance = instance2;
                producing_instance = instance3;

                // Since we ignored app_path1, don't assume that class of app_path2 is same:
                mgmnt_class = conn_path.app_path2.GetClass();
                break;
            }
        }
    }

    conn_path.AssignAppPaths( config_path, consuming_path, producing_path );

    switch( trigger.Class() )
    {
    case kConnTransportClass3:
        // Class3 connection end point has to be the message router instance 1
        if( ConsumingPath().GetClass() != kCipMessageRouterClass ||
            ConsumingPath().GetInstanceOrConnPt() != 1 )
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
        Format().c_str()
        );

    return kCipErrorSuccess;

L_exit_invalid:

    // Add CIPSTER_TRACE statements at specific goto sites above, not here.
    *aExtError = kConnMgrStatusInvalidSegmentTypeInPath;

L_exit_error:
    return kCipErrorConnectionFailure;
}


CipError ConnectionData::VerifyForwardOpenParams( ConnMgrStatus* aExtError )
{
    // The Production Inhibit Time Network Segments only apply to Change of
    // State or Application triggered connections, i.e. all but Cyclic.
    // Vol1 3-4.4.17

    if( trigger.Trigger() == kConnTriggerTypeChangeOfState ||
        trigger.Trigger() == kConnTriggerTypeApplication )
    {
        if( !conn_path.port_segs.HasPIT() )
        {
            // saw no PIT segment in the connection path, set PIT to one fourth of RPI
            conn_path.port_segs.SetPIT_USecs( producing_RPI_usecs / 4 );
        }

        // if production inhibit time has been provided it needs to be smaller than the RPI
        else if( conn_path.port_segs.GetPIT_USecs() > producing_RPI_usecs )
        {
            // see section C-1.4.3.3
            *aExtError = kConnMgrStatusPITGreaterThanRPI;
            return kCipErrorConnectionFailure;
        }
    }

    return kCipErrorSuccess;
}


CipError ConnectionData::CorrectSizes( ConnMgrStatus* aExtError )
{
    int     data_size;
    int     diff_size;
    bool    is_heartbeat;

    if( consuming_ncp.ConnectionType() != kIOConnTypeNull ) // setup consumer side
    {
        CIPSTER_ASSERT( consuming_instance );

        // Vol1 3-5.4.1.10.2 Assumed Assembly Object Attribute (== 3)
        ConsumingPath().SetAttribute( 3 );

        CipAttribute* attribute = consuming_instance->Attribute( 3 );
        // an assembly object should always have an attribute 3
        CIPSTER_ASSERT( attribute );

        ByteBuf* attr_data = (ByteBuf*) consuming_instance->Data( attribute );

        data_size    = consuming_ncp.ConnectionSize();
        diff_size    = 0;
        is_heartbeat = ( attr_data->size() == 0 );

        if( trigger.Class() == kConnTransportClass1 )
        {
            data_size -= 2;     // remove 16-bit sequence count length
            diff_size += 2;
        }

        if( consuming_fmt == kRealTimeFmt32BitHeader
            // only expect a run idle header if it is not a heartbeat connection
            // and is not kRealTimeFmtModeless
            && !is_heartbeat )
        {
            data_size -= 4;     // remove the 4 bytes needed for run/idle header
            diff_size += 4;
        }

        if( ( consuming_ncp.IsFixed() && data_size != attr_data->size() )
          ||  data_size >  attr_data->size() )
        {
            // wrong connection size
            corrected_consuming_size = attr_data->size() + diff_size;

            *aExtError = kConnMgrStatusInvalidOToTConnectionSize;

            CIPSTER_TRACE_INFO(
                "%s: assembly size(%d) != requested conn_size(%d) for consuming:'%s'\n"
                " corrected_o_t:%d\n",
                __func__,
                (int) attr_data->size(),
                data_size,
                ConsumingPath().Format().c_str(),
                corrected_consuming_size
                );

            return kCipErrorConnectionFailure;
        }

        CIPSTER_TRACE_INFO(
            "%s: requested conn_size(%d) is OK for consuming:'%s'\n",
            __func__,
            data_size + diff_size,
            ConsumingPath().Format().c_str()
            );
    }

    if( producing_ncp.ConnectionType() != kIOConnTypeNull ) // setup producer side
    {
        CIPSTER_ASSERT( producing_instance );

        // Vol1 3-5.4.1.10.2 Assumed Assembly Object Attribute (== 3)
        ProducingPath().SetAttribute( 3 );

        CipAttribute* attribute = producing_instance->Attribute( 3 );

        // an assembly object should always have an attribute 3
        CIPSTER_ASSERT( attribute );

        ByteBuf* attr_data = (ByteBuf*) producing_instance->Data( attribute );

        data_size    = producing_ncp.ConnectionSize();
        diff_size    = 0;

        // Note: spec never talks about a heartbeat t_o connection, so why this?
        is_heartbeat = ( attr_data->size() == 0 );

        if( trigger.Class() == kConnTransportClass1 )
        {
            data_size -= 2; // remove 16-bit sequence count length
            diff_size += 2;
        }

        if( producing_fmt == kRealTimeFmt32BitHeader
            // only have a run idle header if it is not a heartbeat connection
            // and is not kRealTimeFmtModeless
            && !is_heartbeat )
        {
            data_size -= 4;   // remove the 4 bytes needed for run/idle header
            diff_size += 4;
        }

        if( ( producing_ncp.IsFixed() && data_size != attr_data->size() )
          ||  data_size > attr_data->size() )
        {
            // wrong connection size
            corrected_producing_size = attr_data->size() + diff_size;

            *aExtError = kConnMgrStatusInvalidTToOConnectionSize;

            CIPSTER_TRACE_INFO(
                "%s: assembly size(%d) != requested conn_size(%d) for producing:'%s'\n"
                " corrected_t_o:%d\n",
                __func__,
                (int) attr_data->size(),
                data_size,
                ProducingPath().Format().c_str(),
                corrected_producing_size
                );
            return kCipErrorConnectionFailure;
        }

        CIPSTER_TRACE_INFO(
            "%s: requested conn_size(%d) is OK for producing:'%s'\n",
            __func__,
            data_size + diff_size,
            ProducingPath().Format().c_str()
            );
    }

    return kCipErrorSuccess;
}


//-----<CipConn>----------------------------------------------------------------

int CipConn::constructed_count;         // CipConn::instance_id is only for debugging

CipConn::CipConn() :
    instance_id( ++constructed_count ),
    next(NULL),
    prev(NULL),
    on_list(false)
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

    SetTransmissionTriggerTimerUSecs( 0 );
    SetInactivityWatchDogTimerUSecs( 0 );
    SetProductionInhibitTimerUSecs( 0 );

    send_address.Clear();
    recv_address.Clear();
    openers_address.Clear();

    SetConsumingUdp( NULL );
    SetProducingUdp( NULL );

    encap_session = 0;

    next = NULL;
    prev = NULL;
    on_list = false;

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


const char* CipConn::ShowInstanceType( ConnInstanceType aType )
{
    switch( aType )
    {
    case kConnInstanceTypeExplicit:             return "Explicit";
    case kConnInstanceTypeIoExclusiveOwner:     return "IoExclusiveOwner";
    case kConnInstanceTypeIoInputOnly:          return "IoInputOnly";
    case kConnInstanceTypeIoListenOnly:         return "IoListenOnly";
    default:
        return "???";
    }
}


ConnMgrStatus CipConn::handleConfigData()
{
    ConnMgrStatus result = kConnMgrStatusSuccess;

    AssemblyInstance* instance = static_cast<AssemblyInstance*>( config_instance );

    const Words& words = conn_path.data_seg.Data();

    if( ConnectionWithSameConfigPointExists( ConfigPath().GetInstanceOrConnPt() ) )
    {
        // There is a connected connection with the same config point ->
        // we have to have the same data as already present in the config point,
        // else it's an error.  And if same, no reason to write it.

        ByteBuf bytes = instance->Buffer();

        if( bytes.size() != words.size() * 2  ||
            memcmp( bytes.data(), words.data(), bytes.size() ) != 0 )
        {
            CIPSTER_TRACE_INFO( "%s: config data mismatch\n", __func__ );
            result = kConnMgrStatusOwnershipConflict;
        }
    }

    // Put the data into the configuration assembly object
    else if( kEipStatusOk != instance->RecvData(
                this,
                BufReader( (uint8_t*)  words.data(),  words.size() * 2 ) ) )
    {
        CIPSTER_TRACE_WARN( "Configuration data was invalid\n" );
        result = kConnMgrStatusInvalidConfigurationApplicationPath;
    }

    return result;
}


// Holds the connection ID's "incarnation ID" in upper 16 bits
static CipUdint s_incarnation_id;

uint32_t CipConn::NewConnectionId()
{
    static CipUint connection_id = 18;

    ++connection_id;

    return s_incarnation_id | connection_id;
}


void CipConn::GeneralConfiguration( ConnectionData* aConnData, ConnInstanceType aType )
{
    if( aConnData != static_cast<CipConn*>( this ) )
        *this = *aConnData;     // copy all the ConnectionData stuff to start with.

    // In general, the consuming device selects the Network Connection ID for a
    // point-to-point connection, and the producing device selects the Network
    // Connection ID for a multicast connection.
    // See Vol2 Table 3-3.2 Network Connection ID Selection

    if( consuming_ncp.ConnectionType() == kIOConnTypePointToPoint )
    {
        // if we have a point to point connection for O->T
        // the target shall choose the connection Id.
        SetConsumingConnectionId( CipConn::NewConnectionId() );

        CIPSTER_TRACE_INFO( "%s<%d>: new PointToPoint CID:0x%x\n",
            __func__, instance_id, ConsumingConnectionId() );

        // Report assigned connection Id for possible forward_open response
        aConnData->SetConsumingConnectionId( ConsumingConnectionId() );
    }

    if( producing_ncp.ConnectionType() == kIOConnTypeMulticast )
    {
        // if we have a multi-cast connection for T->O the
        // target shall choose the connection Id.
        SetProducingConnectionId( CipConn::NewConnectionId() );

        CIPSTER_TRACE_INFO( "%s<%d>: new Multicast PID:0x%x\n",
            __func__, instance_id, ProducingConnectionId() );

        // Report assigned connection Id for possible forward_open response
        aConnData->SetProducingConnectionId( ProducingConnectionId() );
    }

    eip_level_sequence_count_producing = 0;
    sequence_count_producing = 0;

    eip_level_sequence_count_consuming = 0;
    eip_level_sequence_count_consuming_first = true;

    sequence_count_consuming = 0;

    watchdog_timeout_action = kWatchdogTimeoutActionAutoDelete;

    if( !trigger.IsServer() )  // Client Type Connection requested
    {
        // As soon as we are ready we should produce on the connection.
        // With the 0 here we will produce with the next timer tick
        // which should be sufficiently soon.
        SetTransmissionTriggerTimerUSecs( 0 );
    }

    // Server Type Connection requested
    SetExpectedPacketRateUSecs( consuming_RPI_usecs );

    SetProductionInhibitTimerUSecs( 0 );

    SetPIT_USecs( 0 );

    // Vol1 3-4.5.2, says to set *initial* value to greater of 10 seconds or
    // "expected_packet_rate x connection_timeout_multiplier".  Initial value
    // is called a "pre-consumption" timeout value.
    if( RxTimeoutUSecs() )
        SetInactivityWatchDogTimerUSecs( std::max( RxTimeoutUSecs(), 10000000u ) );
    else
    {
        // this is not an erro
        CIPSTER_TRACE_INFO(
            "%s<%d>: no inactivity/Watchdog activated; epected_packet_rate is zero\n",
            __func__, instance_id );
    }

    SetInstanceType( aType );
}


void CipConn::Close()
{
    if( state == kConnStateNonExistent )
    {
        CIPSTER_TRACE_WARN( "%s<%d> NO! its an already closed connection\n",
            __func__, instance_id );
        return;
    }

    CIPSTER_TRACE_INFO( "%s<%d>\n", __func__, instance_id );

    if( IsIOConnection() )
    {
        NotifyIoConnectionEvent( this, kIoConnectionEventClosed );

        if( InstanceType() == kConnInstanceTypeIoExclusiveOwner
         || InstanceType() == kConnInstanceTypeIoInputOnly )
        {
            if( producing_ncp.ConnectionType() == kIOConnTypeMulticast && ProducingUdp() )
            {
                CipConn* next_non_control_master =
                    GetNextNonControlMasterConnection( ProducingPath().GetInstanceOrConnPt() );

                if( next_non_control_master )
                {
                    next_non_control_master->SetProducingUdp( ProducingUdp() );

                    next_non_control_master->send_address = send_address;

                    next_non_control_master->eip_level_sequence_count_producing =
                        eip_level_sequence_count_producing;

                    next_non_control_master->sequence_count_producing = sequence_count_producing;

                    SetProducingUdp( NULL );

                    next_non_control_master->setTransmissionTriggerTimerUSecs(
                        transmissionTriggerTimerUSecs() );
                }

                else
                {
                    // This was the last master connection, close all listen only
                    // connections listening on the port.
                    CloseAllConnectionsForInputWithSameType(
                            ProducingPath().GetInstanceOrConnPt(),
                            kConnInstanceTypeIoListenOnly );
                }
            }
        }
    }

    if( ConsumingUdp() )
    {
        UdpSocketMgr::ReleaseSocket( ConsumingUdp() );
        SetConsumingUdp( NULL );
    }

    if( ProducingUdp() )
    {
        UdpSocketMgr::ReleaseSocket( ProducingUdp() );
        SetProducingUdp( NULL );
    }

    encap_session = 0;

    g_active_conns.Remove( this );
    SetState( kConnStateNonExistent );
}


CipError CipConn::Activate( Cpf* aCpf, ConnMgrStatus* aExtError )
{
    // If config data is present in forward_open request
    if( conn_path.ConfigPath().HasAny() && conn_path.data_seg.HasAny() )
    {
        *aExtError = handleConfigData();

        if( kConnMgrStatusSuccess != *aExtError )
        {
            CIPSTER_TRACE_INFO( "%s: extended_error != 0\n", __func__ );
            return kCipErrorConnectionFailure;
        }
    }

    // Save TCP peer info for TCP inactivity timeouts, and for originator
    // definition, before calling openCommunicationsChannels().
    SetSessionHandle( aCpf->SessionHandle() );

    CipError result = openCommunicationChannels( aCpf, aExtError );

    if( result )
    {
        // CIPSTER_TRACE_ERR( "%s: openCommunicationChannels() failed.  \n", __func__ );
        return result;
    }

    g_active_conns.Insert( this );
    SetState( kConnStateEstablished );

    NotifyIoConnectionEvent( this, kIoConnectionEventOpened );

    return result;
}


EipStatus CipConn::SendConnectedData()
{
    /*
        TODO think of adding an own send buffer to each connection object in
        order to pre-setup the whole message on connection opening and just
        change the variable data items e.g., sequence number
    */

    AssemblyInstance* assembly = static_cast<AssemblyInstance*>( producing_instance );

    EipStatus result = kEipStatusOk;

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
    // this means the Assembly data has changed or should be reported as
    // having updated depending on transportation class.
    if( BeforeAssemblyDataSend( assembly ) )
    {
        // Notify consumer that the data has changed or has been updated as
        // the case may be according to this connection's transportation class.
        // Implementor of BeforeAssemblyDataSend() must know which of the
        // 2 strategies to employ based on connection class.
        ++sequence_count_producing;
    }

    //----<DataInfoItem>--------------------------------------------------------

    ByteBuf attr3 = assembly->Buffer();

    int length = cpfd.Serialize( out );

    // Advance over Cpf serialization, which ended after data_item.length, but
    // prepare to re-write that 16 bit field below, ergo -2
    out += (length - 2);

    int data_len = attr3.size();

    if( producing_fmt == kRealTimeFmt32BitHeader && data_len )
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

    if( producing_fmt == kRealTimeFmt32BitHeader && data_len )
    {
        out.put32( g_run_idle_state );
    }

    out.append( attr3 );

    length += data_len;

    CIPSTER_TRACE_INFO(
        "%s[%d]@%u PID:0x%08x len:%-3d dst:%s:%d\n",
        __func__,
        ProducingUdp()->h(),
        (uint32_t)g_current_usecs,
        producing_connection_id,
        length,
        send_address.AddrStr().c_str(),
        send_address.Port()
        );

    // send out onto UDP wire
    try
    {
        ProducingUdp()->Send( send_address,
                BufReader( g_message_data_reply_buffer, length )
                );
    }
    catch( const socket_error& se )
    {
        // Send() does logging on error, don't also do it here.

        // @todo switch more of the API over to exceptions, not error codes.
        result = kEipStatusError;
    }

    return result;
}


EipStatus CipConn::HandleReceivedIoConnectionData( BufReader aInput )
{
    if( trigger.Class() == kConnTransportClass1 )
    {
        // consume first 2 bytes for the sequence count
        uint16_t sequence = aInput.get16();

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
        if( consuming_fmt == kRealTimeFmt32BitHeader )
        {
            uint32_t new_run_idle = aInput.get32();

            //-----<new logic>---------------------------------------
            // saves mode in each connection.
            consuming_header.Set( new_run_idle );

            //-----<old logic>---------------------------------------
            // has no ability to track multiple scanner's modes.
            if( g_run_idle_state != new_run_idle )
            {
                RunIdleChanged( new_run_idle );
            }

            g_run_idle_state = new_run_idle;
            //-----</old logic>---------------------------------------
        }
        else
        {
            // It is kRealTimeFmtModeless
        }

        AssemblyInstance* assembly = static_cast<AssemblyInstance*>( consuming_instance );

        EipStatus status = assembly->RecvData( this, aInput );

        if( status != kEipStatusOk )
        {
            return kEipStatusError;
        }
    }
    else
    {
        // It is kRealTimeFmtHeartbeat
    }

    // kRealTimeFmtZeroLengthData is not currently supported but would be easy.

    return kEipStatusOk;
}


void CipConn::timeOut()
{
    if( IsIOConnection() )
    {
        NotifyIoConnectionEvent( this, kIoConnectionEventTimedOut );

        if( producing_ncp.ConnectionType() == kIOConnTypeMulticast )
        {
            switch( InstanceType() )
            {
            case kConnInstanceTypeIoExclusiveOwner:
                CloseAllConnectionsForInputWithSameType(
                        ProducingPath().GetInstanceOrConnPt(),
                        kConnInstanceTypeIoInputOnly );
                CloseAllConnectionsForInputWithSameType(
                        ProducingPath().GetInstanceOrConnPt(),
                        kConnInstanceTypeIoListenOnly );
                break;

            case kConnInstanceTypeIoInputOnly:
                if( ProducingUdp() )
                {
                    // we are the controlling input only connection find a new controller

                    CipConn* next_non_control_master =
                        GetNextNonControlMasterConnection( ProducingPath().GetInstanceOrConnPt() );

                    if( next_non_control_master )
                    {
                        next_non_control_master->SetProducingUdp( ProducingUdp() );
                        SetProducingUdp( NULL );

                        next_non_control_master->setTransmissionTriggerTimerUSecs(
                            transmissionTriggerTimerUSecs() );
                    }

                    // this was the last master connection close all listen only
                    // connections listening on the port
                    else
                    {
                        CloseAllConnectionsForInputWithSameType(
                                ProducingPath().GetInstanceOrConnPt(),
                                kConnInstanceTypeIoListenOnly );
                    }
                }

                break;

            default:
                break;
            }
        }
    }

    // grab session handle before Close() zeroes it out.
    CipUdint session_handle = SessionHandle();

    Close();

    // Vol2 2-5.5.2
    // In the condition where a target’s CIP connections from an originator all
    // time out, the target shall close the TCP connection from that originator
    // immediately. The purpose of this behavior is to help prevent half-open
    // CIP connections that can result from TCP retries at the originator due to
    // link-lost conditions.

    if( session_handle )    // If we are a scanner, this could be 0, skip TCP killer
    {
        // check all "CIP connections", not just I/O connections.
        CipConnMgrClass::CheckForTimedOutConnectionsAndCloseTCPConnections( session_handle );
    }
}


CipError CipConn::openConsumingPointToPointConnection( Cpf* aCpf, ConnMgrStatus* aExtError )
{
    // For point-point connections, the point-point consumer shall choose
    // a UDP port number on which it will receive the connected data.

    // User may adjust g_my_io_udp_port to other than kEIP_IoUdpPort for this.

    SockAddr peers_destination(     // a.k.a "me"
                g_my_io_udp_port,
                DEFAULT_BIND_IPADDR
                );

    UdpSocket* socket = UdpSocketMgr::GrabSocket( peers_destination );
    if( !socket )
    {
        CIPSTER_TRACE_ERR( "%s: no UDP socket bound to %s:%d\n",
            __func__,
            peers_destination.AddrStr().c_str(),
            peers_destination.Port() );

        *aExtError = kConnMgrStatusTargetObjectOutOfConnections;
        return kCipErrorConnectionFailure;
    }

    // Vol2 3-3.9.6 return O->T Saii in the forward_open reply if I am
    // using a port different than the 0x08AE.
    if( g_my_io_udp_port != kEIP_IoUdpPort )
    {
        // See Vol2 table 3-3.3.
        // Originator ignores the IP address portion of peers_destination
        aCpf->AddTx( kSockAddr_O_T, peers_destination );
    }

    SockAddr remotes_source(                // a.k.a "peer"
                g_my_io_udp_port,           // ignored remote src port
                aCpf->TcpPeerAddr()->Addr() // IP of TCP peer
                );

    SetConsumingUdp( socket );
    recv_address = remotes_source;

    return kCipErrorSuccess;
}


CipError CipConn::openProducingPointToPointConnection( Cpf* aCpf, ConnMgrStatus* aExtError )
{
    CIPSTER_ASSERT( aCpf->TcpPeerAddr() );

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
            aCpf->TcpPeerAddr()->Addr()  // TCP client originator
            );

    SockAddr source(
            g_my_io_udp_port,    // I chose my source port consistently for non-multicast producing
            DEFAULT_BIND_IPADDR
            );

    UdpSocket* socket = UdpSocketMgr::GrabSocket( source );

    if( !socket )
    {
        CIPSTER_TRACE_ERR( "%s: no UDP socket bound to %s:%d\n",
                __func__,
                source.AddrStr().c_str(),
                source.Port() );

        *aExtError = kConnMgrStatusTargetObjectOutOfConnections;
        return kCipErrorConnectionFailure;
    }

    SetProducingUdp( socket );
    send_address = destination;

    return kCipErrorSuccess;
}


CipError CipConn::openProducingMulticastConnection( Cpf* aCpf, ConnMgrStatus* aExtError )
{
    // producing multicast connections have to consider the rules that apply for
    // application connection types.

    CipConn* existing = GetExistingProducerMulticastConnection(
                            ProducingPath().GetInstanceOrConnPt() );

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
        // exclusive owners take the socket and further manage the connection
        // especially in the case of time outs.
        SetProducingUdp( existing->ProducingUdp() );

        existing->SetProducingUdp( NULL );
    }
    else    // this connection will not produce the data
    {
        SetProducingUdp( NULL );
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
        const SockAddr* saii = aCpf->SaiiRx( kSockAddr_T_O );

        // For our consuming multicast connections the producer chooses the
        // multicast address to use, so it *must* be in the aCpf as retrieved
        // above, else error

        if( !saii )
        {
            CIPSTER_TRACE_ERR(
                "%s: missing required T->O SockAddr Info Item for consuming.\n",
                __func__ );

            *aExtError = kConnMgrStatusParameterErrorInUnconnectedSendService;
            return kCipErrorConnectionFailure;
        }

        SockAddr remotes_destination = *saii;

        if( remotes_destination.Family() != AF_INET )
        {
            CIPSTER_TRACE_ERR(
                "%s: originator's T->O SockAddr Info Item has invalid sin_family.\n",
                __func__ );

            *aExtError = kConnMgrStatusParameterErrorInUnconnectedSendService;
            return kCipErrorConnectionFailure;
        }

        if( !remotes_destination.IsMulticast() )
        {
            CIPSTER_TRACE_ERR(
                "%s: originator's T->O SockAddr Info Item has invalid multicast address.\n",
                __func__ );

            *aExtError = kConnMgrStatusParameterErrorInUnconnectedSendService;
            return kCipErrorConnectionFailure;
        }

        // Vol2 3-3.9.5: originator is allowed to send garbage here, set it
        // to the required value to ensure it is valid, and for multicast it
        // must be the registered port.
        remotes_destination.SetPort( kEIP_IoUdpPort );

        SockAddr base_multicast_socket(
            kEIP_IoUdpPort,
            DEFAULT_BIND_IPADDR
            );

        UdpSocket* socket = UdpSocketMgr::GrabSocket( base_multicast_socket, &remotes_destination );

        if( !socket )
        {
            CIPSTER_TRACE_ERR( "%s: no UDP socket bound to %s:%d\n",
                __func__,
                remotes_destination.AddrStr().c_str(),
                remotes_destination.Port() );

            *aExtError = kConnMgrStatusTargetObjectOutOfConnections;
            return kCipErrorConnectionFailure;
        }

        SockAddr remotes_source(                // a.k.a "peer"
                    kEIP_IoUdpPort,
                    aCpf->TcpPeerAddr()->Addr() // IP of TCP peer
                    );

        SetConsumingUdp( socket );
        recv_address = remotes_source;
    }

    else
    {
        SockAddr source(
                g_my_io_udp_port,
                DEFAULT_BIND_IPADDR
                );

        SockAddr destination(
                kEIP_IoUdpPort,         // multicast cannot use g_my_io_udp_port here
                ntohl( CipTCPIPInterfaceClass::MultiCast( 1 ).starting_multicast_address )
                );

        //UdpSocket* socket = UdpSocketMgr::GrabSocket( source, &destination );
        UdpSocket* socket = UdpSocketMgr::GrabSocket( source );

        if( !socket )
        {
            CIPSTER_TRACE_ERR( "%s: no UDP socket bound to %s:%d\n",
                __func__,
                source.AddrStr().c_str(),
                source.Port()
                );

            *aExtError = kConnMgrStatusTargetObjectOutOfConnections;
            return kCipErrorConnectionFailure;
        }

        aCpf->AddTx( kSockAddr_T_O, destination );

        SetProducingUdp( socket );
        send_address = destination;
    }

    CIPSTER_TRACE_INFO( "%s: opened OK\n", __func__ );

    return kCipErrorSuccess;
}


CipError CipConn::openCommunicationChannels( Cpf* aCpf, ConnMgrStatus* aExtError )
{
    // one, both, or no consuming/producing ends based on IOConnType for each

    //----<Consuming End>-------------------------------------------------------

    if( consuming_ncp.ConnectionType() == kIOConnTypeMulticast )
    {
        CipError result = openMulticastConnection( kUdpConsuming, aCpf, aExtError );

        if( result )
        {
            CIPSTER_TRACE_ERR( "%s: error in consuming Multicast connection\n", __func__ );
            return result;
        }
    }

    else if( consuming_ncp.ConnectionType() == kIOConnTypePointToPoint )
    {
        CipError result = openConsumingPointToPointConnection( aCpf, aExtError );

        if( result )
        {
            CIPSTER_TRACE_ERR( "%s: error in consuming PointToPoint connection\n", __func__ );
            return result;
        }
    }

    //----<Producing End>-------------------------------------------------------

    if( producing_ncp.ConnectionType() == kIOConnTypeMulticast )
    {
        CipError result = openProducingMulticastConnection( aCpf, aExtError );

        if( result )
        {
            CIPSTER_TRACE_ERR( "%s: error in producing Multicast connection\n", __func__ );
            return result;
        }
    }

    else if( producing_ncp.ConnectionType() == kIOConnTypePointToPoint )
    {
        CipError result = openProducingPointToPointConnection( aCpf, aExtError );

        if( result )
        {
            CIPSTER_TRACE_ERR( "%s: error in producing PointToPoint connection\n", __func__ );
            return result;
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
    delete ServiceRemove( _I, kSetAttributeSingle );
    delete ServiceRemove( _I, kGetAttributeSingle );
}


CipError CipConnectionClass::OpenIO( ConnectionData* aConnData,
        Cpf* aCpf, ConnMgrStatus* aExtError )
{
    // currently we allow I/O connections only to assembly objects

    CipError gen_status;

    gen_status = aConnData->VerifyForwardOpenParams( aExtError );
    if( gen_status != kCipErrorSuccess )
        return gen_status;

    gen_status = aConnData->CorrectSizes( aExtError );
    if( gen_status != kCipErrorSuccess )
        return gen_status;

    CipConn* c = GetIoConnectionForConnectionData( aConnData, aExtError );

    if( !c )
    {
        if( *aExtError == kConnMgrStatusInconsistentApplicationPathCombo )
        {
            CIPSTER_TRACE_ERR(
                "%s: no reserved IO connection was found for:\n"
                " %s.\n"
                " All anticipated IO connections must be reserved with Configure<*>ConnectionPoint()\n",
                __func__,
                aConnData->Format().c_str()
                );
        }

        return kCipErrorConnectionFailure;
    }

    c->GeneralConfiguration( aConnData, c->InstanceType() );

    gen_status = c->Activate( aCpf, aExtError );

    if( gen_status == kCipErrorSuccess )
    {
        // Save TCP client's IP address in order to qualify a future forward_close.
        CIPSTER_ASSERT( aCpf->TcpPeerAddr() );
        c->openers_address = *aCpf->TcpPeerAddr();
    }

    return gen_status;
}


EipStatus CipConn::Init( uint16_t unique_connection_id )
{
    if( !GetCipClass( kCipConnectionClass ) )
    {
        CipClass* clazz = new CipConnectionClass();

        RegisterCipClass( clazz );

        if( !unique_connection_id )
            unique_connection_id = 0xc0de;

        s_incarnation_id = unique_connection_id << 16;
    }

    return kEipStatusOk;
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

