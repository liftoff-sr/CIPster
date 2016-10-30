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
#include "endianconv.h"
#include "cipster_api.h"
#include "encap.h"
#include "cipidentity.h"
#include "trace.h"
#include "cipconnection.h"
#include "cipassembly.h"
#include "cpf.h"
#include "appcontype.h"
#include "encap.h"

// values needed from the CIP identity object
extern EipUint16    vendor_id_;
extern EipUint16    device_type_;
extern EipUint16    product_code_;
extern CipRevision  revision_;


bool table_init = true;

/// Length in bytes of the forward open command specific data until the start
/// of the connection path
const int g_kForwardOpenHeaderLength = 36;


/// Test for a logical segment type
#define EQLOGICALSEGMENT( x, y )        ( ( (x) & 0xfc ) == (y) )


/// List holding all currently active connections
/*@null@*/ CipConn* g_active_connection_list = NULL;


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


/**
 * Function checkElectronicKey
 * compares the electronic key received with a forward open request with the device's data.
 *
 * @param key_data the electronic key data received in the forward open request
 * @param extended_status the extended error code in case an error happened
 *
 * @return bool - true on success, false on error
 *    - On an error the general status code to be put into the response
 */
static bool checkElectronicKeyData( CipElectronicKeySegment* key_data, ConnectionManagerStatusCode* extended_status )
{
    bool compatiblity_mode = key_data->major_revision & 0x80;

    // Remove compatibility bit
    key_data->major_revision &= 0x7F;       // bad, modifying caller's data

    // Default return value
    *extended_status = kConnectionManagerStatusCodeSuccess;

    // Check VendorID and ProductCode, must match, or 0
    if( ( (key_data->vendor_id != vendor_id_) && (key_data->vendor_id != 0) )
        || ( (key_data->product_code != product_code_)
             && (key_data->product_code != 0) ) )
    {
        *extended_status = kConnectionManagerStatusCodeErrorVendorIdOrProductcodeError;
        return false;
    }
    else
    {
        // VendorID and ProductCode are correct

        // Check DeviceType, must match or 0
        if( (key_data->device_type != device_type_) && (key_data->device_type != 0) )
        {
            *extended_status = kConnectionManagerStatusCodeErrorDeviceTypeError;
            return false;
        }
        else
        {
            // VendorID, ProductCode and DeviceType are correct

            if( !compatiblity_mode )
            {
                // Major = 0 is valid
                if( 0 == key_data->major_revision )
                {
                    return true;
                }

                // Check Major / Minor Revision, Major must match, Minor match or 0
                if( (key_data->major_revision != revision_.major_revision)
                    || ( (key_data->minor_revision != revision_.minor_revision)
                         && (key_data->minor_revision != 0) ) )
                {
                    *extended_status = kConnectionManagerStatusCodeErrorRevisionMismatch;
                    return false;
                }
            }
            else
            {
                // Compatibility mode is set

                // Major must match, Minor != 0 and <= MinorRevision
                if( (key_data->major_revision == revision_.major_revision)
                    && (key_data->minor_revision > 0)
                    && (key_data->minor_revision <= revision_.minor_revision) )
                {
                    return true;
                }
                else
                {
                    *extended_status = kConnectionManagerStatusCodeErrorRevisionMismatch;
                    return false;
                }
            } // end if CompatiblityMode handling
        }
    }

    //return *extended_status == kConnectionManagerStatusCodeSuccess ? true : false;
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


#if 0
    else
    {
        *extended_error = kConnectionManagerStatusCodeInvalidConsumingApllicationPath;

        CIPSTER_TRACE_INFO(
            "%s: client asked for non-existent assembly class instance_id:%d\n",
            __func__, instance_id
            );

        return kCipErrorConnectionFailure;
    }

        }
        else
        {
            *extended_error = kConnectionManagerStatusCodeInvalidProducingApplicationPath;

            CIPSTER_TRACE_INFO( "%s: 2 noinstance\n", __func__ );

            return kCipErrorConnectionFailure;
        }
#endif


/**
 * Function parseConnectionPath
 * parses the connection path of a forward open request.
 *
 * This function will take the connection object and the received data stream
 * and parse the connection path.
 *
 * @param conn the connection object for which the connection should be established
 * @param request the received request packet. The position of the data stream
 *  pointer has to be at the connection length entry
 * @param extended_status where to put the extended error code in case an error happened
 *
 * @return CipError - general status on the establishment
 *    - kEipStatusOk on success
 *    - On an error the general status code to be put into the response
 */
CipError CipConn::parseConnectionPath( CipMessageRouterRequest* request, ConnectionManagerStatusCode* extended_error )
{
    int         byte_count = request->data[0] * 2;
    EipUint8*   message = request->data + 1;
    EipUint8*   limit   = message + byte_count;
    int         result;

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

    if( g_kForwardOpenHeaderLength + byte_count < request->data_length )
    {
        // the received packet is larger than the data in the path
        return kCipErrorTooMuchData;
    }

    if( g_kForwardOpenHeaderLength + byte_count > request->data_length )
    {
        // there is not enough data in received packet
        return kCipErrorNotEnoughData;
    }

    if( message < limit )
    {
        result = conn_path.port_segs.DeserializePadded( message, limit );

        if( result < 0 )
        {
            goto L_exit_invalid;
        }

        message += result;
    }

    // ignore the port segments if any obtained above

    // electronic key?
    if( conn_path.port_segs.HasKey() )
    {
        if( !checkElectronicKeyData( &conn_path.port_segs.key, extended_error ) )
        {
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

    if( message < limit )
    {
        result = app_path1.DeserializePadded( message, limit );

        if( result < 0 )
        {
            goto L_exit_invalid;
        }

        message += result;
    }

    if( message < limit )
    {
        result = app_path2.DeserializePadded( message, limit, &app_path1 );

        if( result < 0 )
        {
            goto L_exit_invalid;
        }

        message += result;
    }

    if( message < limit )
    {
        result = app_path3.DeserializePadded( message, limit, &app_path2 );

        if( result < 0 )
        {
            goto L_exit_invalid;
        }

        message += result;
    }

    if( message < limit )
    {
        // There could be a data segment
        result = conn_path.data_seg.DeserializePadded( message, limit );
        message += result;
    }

    if( message < limit )   // should have consumed all of it by now, 3 app paths max
    {
        CIPSTER_TRACE_ERR( "%s: unknown extra segments in forward open connection path\n", __func__ );
        goto L_exit_invalid;
    }

  //  handle the PIT segments if any.

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
        if( conn_path.consuming_path.GetClass() != kCipMessageRouterClassCode
         || conn_path.consuming_path.GetInstanceOrConnPt() != 1 )
        {
            *extended_error = kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
            goto L_exit_error;
        }
    }

    CIPSTER_TRACE_INFO( "%s: forward_open conn_path: %s\n",
        __func__,
        conn_path.Format().c_str()
        );

    // Save back the current position in the stream allowing followers to parse
    // anything that's still there
    request->data = message;
    return kCipErrorSuccess;

L_exit_invalid:

    // Add CIPSTER_TRACE statements at specific goto sites above, not here.
    *extended_error = kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;

L_exit_error:
    return kCipErrorConnectionFailure;
}


/**
 * Adds a Null Address Item to the common data packet format data
 * @param cpfd The CPF data packet where the Null Address Item shall be added
 */
void AddNullAddressItem( CipCommonPacketFormatData* cpfd )
{
    // Precondition: Null Address Item only valid in unconnected messages
    assert( cpfd->data_item.type_id == kCipItemIdUnconnectedDataItem );

    cpfd->address_item.type_id    = kCipItemIdNullAddress;
    cpfd->address_item.length     = 0;
}


EipStatus HandleReceivedConnectedData( EipUint8* data, int data_length,
        struct sockaddr_in* from_address )
{
    CIPSTER_TRACE_INFO( "%s:\n", __func__ );

    if( g_cpf.Init( data, data_length ) == kEipStatusError )
    {
        return kEipStatusError;
    }
    else
    {
        // Check if connected address item or sequenced address item  received,
        // otherwise it is no connected message and should not be here.
        if( g_cpf.address_item.type_id == kCipItemIdConnectionAddress
         || g_cpf.address_item.type_id == kCipItemIdSequencedAddressItem )
        {
            // found connected address item or found sequenced address item
            // -> for now the sequence number will be ignored

            if( g_cpf.data_item.type_id == kCipItemIdConnectedDataItem ) // connected data item received
            {
                CipConn* conn = GetConnectionByConsumingId( g_cpf.address_item.data.connection_identifier );

                if( !conn )
                {
                    CIPSTER_TRACE_INFO( "%s: no consuming connection for conn_id %d\n",
                        __func__, g_cpf.address_item.data.connection_identifier
                        );
                    return kEipStatusError;
                }

                CIPSTER_TRACE_INFO( "%s: got consuming connection for conn_id %d\n",
                    __func__, g_cpf.address_item.data.connection_identifier
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
                        g_cpf.address_item.data.sequence_number,
                        conn->eip_level_sequence_count_consuming
                        );

                    // if this is the first received frame
                    if( conn->eip_level_sequence_count_consuming_first )
                    {
                        // put our tracking count within a half cycle of the leader.  Without this
                        // there are many scenarios where the SEQ_GT32 below won't evaluate as true.
                        conn->eip_level_sequence_count_consuming = g_cpf.address_item.data.sequence_number - 1;
                        conn->eip_level_sequence_count_consuming_first = false;
                    }

                    // only inform assembly object if the sequence counter is greater or equal, or
                    if( SEQ_GT32( g_cpf.address_item.data.sequence_number,
                                  conn->eip_level_sequence_count_consuming ) )
                    {
                        // reset the watchdog timer
                        conn->inactivity_watchdog_timer_usecs =
                            conn->o_to_t_RPI_usecs << (2 + conn->connection_timeout_multiplier);

                        CIPSTER_TRACE_INFO( "%s: reset inactivity_watchdog_timer_usecs:%u\n",
                            __func__,
                            conn->inactivity_watchdog_timer_usecs );

                        conn->eip_level_sequence_count_consuming = g_cpf.address_item.data.sequence_number;

                        if( conn->connection_receive_data_function )
                        {
                            return conn->connection_receive_data_function(
                                    conn, g_cpf.data_item.data, g_cpf.data_item.length );
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
            if( active->consuming_instance

                // All server connections have to maintain an inactivity watchdog timer
                || active->transport_trigger.IsServer() )
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
    CipCommonPacketFormatData* cpfd = &g_cpf;

    EipByte* message = response->data;

    cpfd->item_count = 2;
    cpfd->data_item.type_id = kCipItemIdUnconnectedDataItem;

    AddNullAddressItem( cpfd );

    response->reply_service  = 0x80 | kForwardOpen;
    response->general_status = general_status;

    if( general_status == kCipErrorSuccess )
    {
        CIPSTER_TRACE_INFO( "%s: sending success response\n", __func__ );

        response->data_length = 26; // if there is no application specific data
        response->size_of_additional_status = 0;

        AddDintToMessage( aConn->consuming_connection_id, &message );
        AddDintToMessage( aConn->producing_connection_id, &message );
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
        response->data_length = 10;

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

    AddIntToMessage( aConn->connection_serial_number, &message );
    AddIntToMessage( aConn->originator_vendor_id, &message );
    AddDintToMessage( aConn->originator_serial_number, &message );

    if( general_status == kCipErrorSuccess )
    {
        // set the actual packet rate to requested packet rate
        AddDintToMessage( aConn->o_to_t_RPI_usecs, &message );
        AddDintToMessage( aConn->t_to_o_RPI_usecs, &message );
    }

    *message++ = 0;   // remaining path size - for routing devices relevant
    *message++ = 0;   // reserved
}


/**
 * Function assembleFWDCloseResponse
 * creates FWDClose response dependent on status.
 *
 * @return EipStatus -
 *          0 .. no reply need to ne sent back
 *          1 .. need to send reply
 *         -1 .. error
 */
static EipStatus assembleForwardCloseResponse( EipUint16 connection_serial_number,
        EipUint16 originatior_vendor_id,
        EipUint32 originator_serial_number,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response,
        EipUint16 extended_error_code )
{
    // write reply information in CPF struct dependent of pa_status
    CipCommonPacketFormatData* cpfd = &g_cpf;
    EipByte* message = response->data;

    cpfd->item_count = 2;
    cpfd->data_item.type_id = kCipItemIdUnconnectedDataItem;

    AddNullAddressItem( cpfd );

    AddIntToMessage( connection_serial_number, &message );
    AddIntToMessage( originatior_vendor_id, &message );
    AddDintToMessage( originator_serial_number, &message );

    response->reply_service = 0x80 | request->service;

    response->data_length = 10; // if there is no application specific data

    if( kConnectionManagerStatusCodeSuccess == extended_error_code )
    {
        *message++ = 0; // no application data
        response->general_status = kCipErrorSuccess;
        response->size_of_additional_status = 0;
    }
    else
    {
        *message++ = *request->data; // remaining path size
        response->general_status = kCipErrorConnectionFailure;
        response->additional_status[0] = extended_error_code;
        response->size_of_additional_status = 1;
    }

    *message++ = 0;         // reserved

    return kEipStatusOkSend;
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


#if 0  // not yet needed
CipConn* GetConnectionByProducingId( int aConnectionId )
{
    CipConn* conn = g_active_connection_list;

    while( conn )
    {
        if( conn->state == kConnectionStateEstablished )
        {
            if( conn->producing_connection_id == aConnectionId )
            {
                return conn;
            }
        }

        conn = conn->next;
    }

    return NULL;
}
#endif


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
    conn->first = NULL;
    conn->next  = g_active_connection_list;

    if( NULL != g_active_connection_list )
    {
        g_active_connection_list->first = conn;
    }

    g_active_connection_list = conn;
    g_active_connection_list->state = kConnectionStateEstablished;
}


void RemoveFromActiveConnections( CipConn* conn )
{
    if( NULL != conn->first )
    {
        conn->first->next = conn->next;
    }
    else
    {
        g_active_connection_list = conn->next;
    }

    if( NULL != conn->next )
    {
        conn->next->first = conn->first;
    }

    conn->first = NULL;
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


EipStatus TriggerConnections( unsigned aOutputAssembly, unsigned aInputAssembly )
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
 * Function forwardOpenCommon
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
static EipStatus forwardOpenCommon( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response, bool isLarge )
{
    ConnectionManagerStatusCode connection_status = kConnectionManagerStatusCodeSuccess;

    (void) instance;        // suppress compiler warning

    static CipConn dummy;

    dummy.priority_timetick = *request->data++;
    dummy.timeout_ticks = *request->data++;

    // O_to_T
    dummy.consuming_connection_id   = GetDintFromMessage( &request->data );

    // T_to_O
    dummy.producing_connection_id   = GetDintFromMessage( &request->data );

    // The Connection Triad used in the Connection Manager specification relates
    // to the combination of Connection Serial Number, Originator Vendor ID and
    // Originator Serial Number parameters.
    dummy.connection_serial_number = GetIntFromMessage( &request->data );
    dummy.originator_vendor_id     = GetIntFromMessage( &request->data );
    dummy.originator_serial_number = GetDintFromMessage( &request->data );

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

    dummy.connection_timeout_multiplier = *request->data++;

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

    request->data += 3; // skip over 3 reserved bytes.

    CIPSTER_TRACE_INFO(
        "%s: ConConnID:0x%08x, ProdConnID:0x%08x, ConnSerNo:%u\n",
        __func__,
        dummy.consuming_connection_id,
        dummy.producing_connection_id,
        dummy.connection_serial_number
        );

    dummy.o_to_t_RPI_usecs = GetDintFromMessage( &request->data );

    if( isLarge )
        dummy.o_to_t_ncp.SetLarge( GetDintFromMessage( &request->data ) );
    else
        dummy.o_to_t_ncp.SetNotLarge( GetIntFromMessage( &request->data ) );

    CIPSTER_TRACE_INFO( "%s: o_to_t RPI_usecs:%u\n", __func__, dummy.o_to_t_RPI_usecs );
    CIPSTER_TRACE_INFO( "%s: o_to_t size:%d\n", __func__, dummy.o_to_t_ncp.ConnectionSize() );
    CIPSTER_TRACE_INFO( "%s: o_to_t priority:%d\n", __func__, dummy.o_to_t_ncp.Priority() );
    CIPSTER_TRACE_INFO( "%s: o_to_t type:%d\n", __func__, dummy.o_to_t_ncp.ConnectionType() );

    dummy.t_to_o_RPI_usecs = GetDintFromMessage( &request->data );

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
        dummy.t_to_o_ncp.SetLarge( GetDintFromMessage( &request->data ) );
    else
        dummy.t_to_o_ncp.SetNotLarge( GetIntFromMessage( &request->data ) );

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

    int trigger = *request->data++;

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

    CipError result = dummy.parseConnectionPath( request, &connection_status );

    if( result != kCipErrorSuccess )
    {
        CIPSTER_TRACE_INFO( "%s: unable to parse connection path\n", __func__ );

        assembleForwardOpenResponse( &dummy, response, result, connection_status );

        return kEipStatusOkSend;    // send reply
    }

    CipClass* clazz = GetCipClass( dummy.mgmnt_class );

    result = clazz->OpenConnection( &dummy, &connection_status );

    if( result != kCipErrorSuccess )
    {
        CIPSTER_TRACE_INFO( "%s: OpenConnection() failed. status:0x%x\n", __func__, connection_status );

        // in case of error the dummy objects holds all necessary information
        assembleForwardOpenResponse( &dummy, response, result, connection_status );

        return kEipStatusOkSend;    // send reply
    }
    else
    {
        CIPSTER_TRACE_INFO( "%s: OpenConnection() succeeded\n", __func__ );

        // in case of success, g_active_connection_list points to the new connection
        assembleForwardOpenResponse( g_active_connection_list,
                response, kCipErrorSuccess, kConnectionManagerStatusCodeSuccess );

        return kEipStatusOkSend;    // send reply
    }
}


static EipStatus forwardOpen( CipInstance* instance,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    return forwardOpenCommon( instance, request, response, false );
}


static EipStatus largeForwardOpen( CipInstance* instance,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    return forwardOpenCommon( instance, request, response, true );
}


static EipStatus forwardClose( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    // Suppress compiler warning
    (void) instance;

    // check connection_serial_number && originator_vendor_id && originator_serial_number if connection is established
    ConnectionManagerStatusCode connection_status =
        kConnectionManagerStatusCodeErrorConnectionNotFoundAtTargetApplication;

    CipConn* active = g_active_connection_list;

    // prevent assembleLinearMsg from serializing socket addresses
    g_cpf.ClearTx();

    request->data += 2; // ignore Priority/Time_tick and Time-out_ticks

    EipUint16 connection_serial_number = GetIntFromMessage( &request->data );
    EipUint16 originator_vendor_id     = GetIntFromMessage( &request->data );
    EipUint32 originator_serial_number = GetDintFromMessage( &request->data );

    CIPSTER_TRACE_INFO( "ForwardClose: ConnSerNo %d\n", connection_serial_number );

    while( active )
    {
        // This check should not be necessary as only established connections
        // should be in the active connection list
        if( active->state == kConnectionStateEstablished
         || active->state == kConnectionStateTimedOut )
        {
            if( (active->connection_serial_number == connection_serial_number)
             && (active->originator_vendor_id     == originator_vendor_id)
             && (active->originator_serial_number == originator_serial_number) )
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

    return assembleForwardCloseResponse( connection_serial_number,
            originator_vendor_id,
            originator_serial_number,
            request,
            response,
            connection_status );
}


/*
static EipStatus getConnectionOwner( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    // suppress compiler warnings
    (void) instance;
    (void) request;
    (void) response;

    return kEipStatusOk;
}
*/


class CipConnMgrClass : public CipClass
{
public:
    CipConnMgrClass();
};


CipConnMgrClass::CipConnMgrClass() :
    CipClass( kCipConnectionManagerClassCode,
                "Connection Manager",       // class name
                (1<<7)|(1<<6)|(1<<5)|(1<<4)|(1<<3)|(1<<2)|(1<<1),
                MASK4( 7, 6, 2, 1 ),        // class getAttributeAll mask
                0,                          // instance getAttributeAll mask
                1                           // revision
                )
{
    // There are no attributes in instance of this class yet, so nothing to set.
    delete ServiceRemove( kSetAttributeSingle );

    ServiceInsert( kForwardOpen, forwardOpen, "ForwardOpen" );
    ServiceInsert( kLargeForwardOpen, largeForwardOpen, "LargeForwardOpen" );

    ServiceInsert( kForwardClose, forwardClose, "ForwardClose" );

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

