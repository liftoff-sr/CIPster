/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#include <string.h>

#include "cipconnectionmanager.h"

#include "opener_user_conf.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "ciperror.h"
#include "endianconv.h"
#include "opener_api.h"
#include "encap.h"
#include "cipidentity.h"
#include "trace.h"
#include "cipclass3connection.h"
#include "cipioconnection.h"
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
#define EQLOGICALSEGMENT( x, y ) ( ( (x) & 0xfc )==(y) )


#define NUM_CONNECTABLE_OBJECTS     (2 + CIPSTER_CIP_NUM_APPLICATION_SPECIFIC_CONNECTABLE_OBJECTS)

struct ConnectionManagementHandling
{
    EipUint32               class_id;
    OpenConnectionFunction  open_connection_function;
};

/** List holding information on the object classes and open/close function
 * pointers to which connections may be established.
 */
static ConnectionManagementHandling g_astConnMgmList[NUM_CONNECTABLE_OBJECTS];


/// List holding all currently active connections
/*@null@*/ CipConn* g_active_connection_list = NULL;


/// buffer connection object needed for forward open
static CipConn s_dummy_conn;

/// @brief Holds the connection ID's "incarnation ID" in the upper 16 bits
EipUint32 g_incarnation_id;


/** @brief check if the data given in the connection object match with an already established connection
 *
 * The comparison is done according to the definitions in the CIP specification Section 3-5.5.2:
 * The following elements have to be equal: Vendor ID, Connection Serial Number, Originator Serial Number
 * @param conn connection object containing the comparison elements from the forward open request
 * @return
 *    - NULL if no equal established connection exists
 *    - pointer to the equal connection object
 */
CipConn* CheckForExistingConnection( CipConn* conn )
{
    CipConn* active = g_active_connection_list;

    while( active )
    {
        if( active->state == kConnectionStateEstablished )
        {
            if( (conn->connection_serial_number == active->connection_serial_number)
                && (conn->originator_vendor_id  == active->originator_vendor_id)
                && (conn->originator_serial_number == active->originator_serial_number) )
            {
                return active;
            }
        }

        active = active->next_cip_conn;
    }

    return NULL;
}


/** @brief Compare the electronic key received with a forward open request with the device's data.
 *
 * @param key_data pointer to the electronic key data received in the forward open request
 * @param extended_status the extended error code in case an error happened
 * @return general status on the establishment
 *    - EIP_OK ... on success
 *    - On an error the general status code to be put into the response
 */
static EipStatus checkElectronicKeyData( CipElectronicKeySegment* key_data, EipUint16* extended_status )
{
    EipByte compatiblity_mode = key_data->major_revision & 0x80;

    // Remove compatibility bit
    key_data->major_revision &= 0x7F;

    // Default return value
    *extended_status = kConnectionManagerStatusCodeSuccess;

    // Check VendorID and ProductCode, must match, or 0
    if( ( (key_data->vendor_id != vendor_id_) && (key_data->vendor_id != 0) )
        || ( (key_data->product_code != product_code_)
             && (key_data->product_code != 0) ) )
    {
        *extended_status = kConnectionManagerStatusCodeErrorVendorIdOrProductcodeError;
        return kEipStatusError;
    }
    else
    {
        // VendorID and ProductCode are correct

        // Check DeviceType, must match or 0
        if( (key_data->device_type != device_type_) && (key_data->device_type != 0) )
        {
            *extended_status = kConnectionManagerStatusCodeErrorDeviceTypeError;
            return kEipStatusError;
        }
        else
        {
            // VendorID, ProductCode and DeviceType are correct

            if( !compatiblity_mode )
            {
                // Major = 0 is valid
                if( 0 == key_data->major_revision )
                {
                    return kEipStatusOk;
                }

                // Check Major / Minor Revision, Major must match, Minor match or 0
                if( (key_data->major_revision != revision_.major_revision)
                    || ( (key_data->minor_revision != revision_.minor_revision)
                         && (key_data->minor_revision != 0) ) )
                {
                    *extended_status = kConnectionManagerStatusCodeErrorRevisionMismatch;
                    return kEipStatusError;
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
                    return kEipStatusOk;
                }
                else
                {
                    *extended_status = kConnectionManagerStatusCodeErrorRevisionMismatch;
                    return kEipStatusError;
                }
            } // end if CompatiblityMode handling
        }
    }

    return (*extended_status == kConnectionManagerStatusCodeSuccess) ?
           kEipStatusOk : kEipStatusError;
}


#if 0
/**
 * Function parsePaddedLogicalSegment
 * de-serializes a padded logical segment and returns only the value, not the type.
 */
static CipUdint parsePaddedLogicalSegment( EipUint8** aMessage )
{
    EipUint8* p  = *aMessage;

    int first = *p++;
    int format = 0x03 & first;

    CipUdint ret;

    if( format == 0 )
    {
        ret = *p++;
    }
    else if( format == 1 )
    {
        ++p;             // skip pad
        ret = GetIntFromMessage( &p );
    }
    else if( format == 2 )
    {
        ++p;
        ret = GetDintFromMessage( &p );
    }
    else
    {
        CIPSTER_TRACE_ERR( "%s: illegal logical path segment %02x\n", __func__, first );
        ret = 0;
    }

    *aMessage = p;

    return ret;
}
#endif


/**
 * Function parseConnectionPath
 * parses the connection path of a forward open request.
 *
 * This function will take the connection object and the received data stream and parse the connection path.
 * @param conn the connection object for which the connection should be established
 * @param request the received request packet. The position of the data stream pointer has to be at the connection length entry
 * @param extended_status where to put the extended error code in case an error happened
 * @return general status on the establishment
 *    - EIP_OK ... on success
 *    - On an error the general status code to be put into the response
 */
static EipUint8 parseConnectionPath( CipConn* conn,
        CipMessageRouterRequest* request,
        EipUint16* extended_error )
{
    int         byte_count = request->data[0] * 2;
    EipUint8*   message = request->data + 1;
    EipUint8*   limit   = message + byte_count;
    int         result;

    CipClass* clazz = NULL;

    IOConnType o_to_t_type;
    IOConnType t_to_o_type;

    // with 256 we mark that we haven't got a PIT segment
    conn->production_inhibit_time = 256;

    if( g_kForwardOpenHeaderLength + byte_count < request->data_length )
    {
        // the received packet is larger than the data in the path
        *extended_error = 0;
        return kCipErrorTooMuchData;
    }

    if( g_kForwardOpenHeaderLength + byte_count > request->data_length )
    {
        // there is not enough data in received packet
        *extended_error = 0;
        return kCipErrorNotEnoughData;
    }

    CipPortSegmentGroup port_segs;

    if( message < limit )
    {
        result = port_segs.DeserializePadded( message, limit );

        if( result < 0 )
        {
            *extended_error = kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;
            return kCipErrorConnectionFailure;
        }

        message += result;
    }

    // ignore the port segment if any obtained above

    // electronic key?
    if( port_segs.HasKey() )
    {
        if( kEipStatusOk != checkElectronicKeyData( &port_segs.key, extended_error ) )
        {
            return kCipErrorConnectionFailure;
        }
    }

    CipAppPath      app_path1;
    CipAppPath      app_path2( &app_path1 );
    CipAppPath      app_path3( &app_path2 );

    CipSimpleDataSegment    data_seg;

    // There can be 1-3 application_paths in a connection_path
    if( message < limit )
    {
        result = app_path1.DeserializePadded( message, limit );

        if( result < 0 )
        {
            *extended_error = kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;
            return kCipErrorConnectionFailure;
        }

        message += result;
    }

    if( message < limit )
    {
        result = app_path2.DeserializePadded( message, limit );

        if( result < 0 )
        {
            *extended_error = kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;
            return kCipErrorConnectionFailure;
        }

        message += result;
    }

    if( message < limit )
    {
        result = app_path3.DeserializePadded( message, limit );

        if( result < 0 )
        {
            *extended_error = kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;
            return kCipErrorConnectionFailure;
        }

        message += result;
    }

    if( message < limit )
    {
        result = data_seg.DeserializePadded( message, limit );
        message += result;
    }

    if( message < limit )   // should have consumed all of it by now, 3 app paths max
    {
        CIPSTER_TRACE_ERR( "%s: unknown extra segments in forward open connection path\n", __func__ );
        *extended_error = kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;
        return kCipErrorConnectionFailure;
    }

    // We don't examine the data until done parsing it here.

    if( !app_path1.HasClass() || (!app_path1.HasInstance() && !app_path1.HasConnPt()) )
    {
        *extended_error = kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;
        return kCipErrorConnectionFailure;
    }

    conn->conn_path.class_id = app_path1.GetClass();

    // store the configuration ID for later checking in the application connection types
    if( app_path1.GetClass() == kCipAssemblyClassCode && !app_path1.HasInstance() )
        conn->conn_path.connection_point[2] = app_path1.GetConnPt();
    else
        conn->conn_path.connection_point[2] = app_path1.GetInstance();

    clazz = GetCipClass( conn->conn_path.class_id );
    if( !clazz )
    {
        CIPSTER_TRACE_ERR( "%s: classid %d not found\n", __func__,
                (int) conn->conn_path.class_id );

        if( conn->conn_path.class_id >= 0xC8 ) // reserved range of class ids
        {
            *extended_error =
                kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;
        }
        else
        {
            *extended_error =
                kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
        }

        return kCipErrorConnectionFailure;
    }

    CIPSTER_TRACE_INFO( "%s: classid %d (%s)\n",
            __func__,
            (int) conn->conn_path.class_id,
            clazz->ClassName().c_str() );

    if( !clazz->Instance( conn->conn_path.connection_point[2] ) )
    {
        CIPSTER_TRACE_ERR( "%s: instance id %d not found\n", __func__,
                (int) conn->conn_path.connection_point[2] );

        // according to the test tool we should respond with this extended error code
        *extended_error = kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;
        return kCipErrorConnectionFailure;
    }

    CIPSTER_TRACE_INFO( "%s: configuration instance id %u\n",
            __func__, conn->conn_path.connection_point[2] );

    if( conn->transport_trigger.Class() == kConnectionTransportClass3 )
    {
        if( app_path2.HasAny() || data_seg.HasAny() )
        {
            CIPSTER_TRACE_WARN(
                "%s: multiple application_paths in connection_path for class 3 connection\n",
                __func__
                );

            *extended_error =
                kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;
            return kCipErrorConnectionFailure;
        }

        // connection end point has to be the message router instance 1
        if( conn->conn_path.class_id != kCipMessageRouterClassCode
         || conn->conn_path.connection_point[2] != 1 )
        {
            *extended_error = kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
            return kCipErrorConnectionFailure;
        }

        conn->conn_path.connection_point[0] = conn->conn_path.connection_point[2];
    }
    else
    {
        o_to_t_type = conn->o_to_t_ncp.ConnectionType();
        t_to_o_type = conn->t_to_o_ncp.ConnectionType();

        conn->conn_path.connection_point[1] = 0; // set not available path to Invalid

        int number_of_encoded_paths = 0;

        if( o_to_t_type == kIOConnTypeNull )
        {
            // configuration only connection
            if( t_to_o_type == kIOConnTypeNull )
            {
                number_of_encoded_paths = 0;
                CIPSTER_TRACE_WARN( "assembly: type invalid\n" );
            }
            else // 1 path -> path is for production
            {
                CIPSTER_TRACE_INFO( "assembly: type produce\n" );
                number_of_encoded_paths = 1;
            }
        }
        else
        {
            // 1 path -> path is for consumption
            if( t_to_o_type == kIOConnTypeNull )
            {
                CIPSTER_TRACE_INFO( "assembly: type consume\n" );
                number_of_encoded_paths = 1;
            }
            else // 2 paths -> 1st for production 2nd for consumption
            {
                CIPSTER_TRACE_INFO( "assembly: type bidirectional\n" );
                number_of_encoded_paths = 2;
            }
        }

        if( number_of_encoded_paths != int( app_path2.HasAny() + app_path3.HasAny() ) )
        {
            *extended_error = kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;
            return kCipErrorConnectionFailure;
        }

        if( app_path2.HasAny() )
        {
            if( !app_path2.HasInstance() && !app_path2.HasConnPt() )
            {
                *extended_error = kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;
                return kCipErrorConnectionFailure;
            }

            conn->conn_path.connection_point[0] = app_path2.HasInstance() ? app_path2.GetInstance() : app_path2.GetConnPt();

            CIPSTER_TRACE_INFO( "%s: app_path2 connection point: %u\n",
                __func__, (unsigned) conn->conn_path.connection_point[0] );

            if( !clazz->Instance( conn->conn_path.connection_point[0] ) )
            {
                *extended_error = kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
                return kCipErrorConnectionFailure;
            }
        }

        if( app_path3.HasAny() )
        {
            if( !app_path3.HasInstance() && !app_path3.HasConnPt() )
            {
                *extended_error = kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath;
                return kCipErrorConnectionFailure;
            }

            conn->conn_path.connection_point[1] = app_path3.HasInstance() ? app_path3.GetInstance() : app_path3.GetConnPt();

            CIPSTER_TRACE_INFO( "%s: app_path3 connection point: %u\n",
                __func__, (unsigned) conn->conn_path.connection_point[1] );

            if( !clazz->Instance( conn->conn_path.connection_point[1] ) )
            {
                *extended_error = kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
                return kCipErrorConnectionFailure;
            }
        }
    }

    // Save back the current position in the stream allowing followers to parse
    // anything that's still there
    request->data = message;
    return kEipStatusOk;
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


ConnectionManagementHandling* GetConnMgmEntry( EipUint32 class_id );


/** @brief Generate a new connection Id utilizing the Incarnation Id as
 * described in the EIP specs.
 *
 * A unique connectionID is formed from the boot-time-specified "incarnation ID"
 * and the per-new-connection-incremented connection number/counter.
 * @return new connection id
 */
EipUint32 GetConnectionId()
{
    static EipUint32 connection_id = 18;

    connection_id++;
    return g_incarnation_id | (connection_id & 0x0000FFFF);
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
        if( (g_cpf.address_item.type_id == kCipItemIdConnectionAddress)
         || (g_cpf.address_item.type_id == kCipItemIdSequencedAddressItem) )
        {
            // found connected address item or found sequenced address item
            // -> for now the sequence number will be ignored

            if( g_cpf.data_item.type_id == kCipItemIdConnectedDataItem ) // connected data item received
            {
                CipConn* conn = GetConnectedObject( g_cpf.address_item.data.connection_identifier );

                if( !conn )
                    return kEipStatusError;

                // only handle the data if it is coming from the originator
                if( conn->originator_address.sin_addr.s_addr == from_address->sin_addr.s_addr )
                {
                    if( SEQ_GT32(
                                g_cpf.address_item.data.sequence_number,
                                conn->eip_level_sequence_count_consuming ) )
                    {
                        // reset the watchdog timer
                        conn->inactivity_watchdog_timer =
                            (conn->o_to_t_requested_packet_interval / 1000) << (2 + conn->connection_timeout_multiplier);

                        CIPSTER_TRACE_INFO( "%s: reset inactivity_watchdog_timer:%u\n",
                            __func__,
                            conn->inactivity_watchdog_timer );

                        // only inform assembly object if the sequence counter is greater or equal
                        conn->eip_level_sequence_count_consuming =
                            g_cpf.address_item.data.sequence_number;

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



void GeneralConnectionConfiguration( CipConn* conn )
{
    if( conn->o_to_t_ncp.ConnectionType() == kIOConnTypePointToPoint )
    {
        // if we have a point to point connection for the O to T direction
        // the target shall choose the connection ID.
        conn->consumed_connection_id = GetConnectionId();
    }

    if( conn->t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast )
    {
        // if we have a multi-cast connection for the T to O direction the
        // target shall choose the connection ID.
        conn->produced_connection_id = GetConnectionId();
    }

    conn->eip_level_sequence_count_producing = 0;
    conn->sequence_count_producing = 0;
    conn->eip_level_sequence_count_consuming = 0;
    conn->sequence_count_consuming = 0;

    conn->watchdog_timeout_action = kWatchdogTimeoutActionAutoDelete;  // the default for all connections on EIP

    conn->expected_packet_rate = 0;                                    // default value

    if( !conn->transport_trigger.IsServer() )  // Client Type Connection requested
    {
        conn->expected_packet_rate = (EipUint16) ( (conn->t_to_o_requested_packet_interval) / 1000 );

        /* As soon as we are ready we should produce the connection. With the 0
         * here we will produce with the next timer tick
         * which should be sufficient.
         */
        conn->transmission_trigger_timer = 0;
    }
    else
    {
        // Server Type Connection requested
        conn->expected_packet_rate = (EipUint16) ( (conn->o_to_t_requested_packet_interval) / 1000 );
    }

    conn->production_inhibit_timer = conn->production_inhibit_time = 0;

    // setup the preconsuption timer: max(ConnectionTimeoutMultiplier * EpectetedPacketRate, 10s)
    conn->inactivity_watchdog_timer =
        ( ( ( (conn->o_to_t_requested_packet_interval) / 1000 )
            << (2 + conn->connection_timeout_multiplier) ) > 10000 ) ?
        ( ( (conn->o_to_t_requested_packet_interval) / 1000 )
          << (2 + conn->connection_timeout_multiplier) ) :
        10000;

    CIPSTER_TRACE_INFO( "%s: inactivity_watchdog_timer:%u\n", __func__,
            conn->inactivity_watchdog_timer );

    conn->consumed_connection_size = conn->o_to_t_ncp.ConnectionSize();
    conn->produced_connection_size = conn->t_to_o_ncp.ConnectionSize();
}


EipStatus ManageConnections()
{
    EipStatus eip_status;

    //Inform application that it can execute
    HandleApplication();
    ManageEncapsulationMessages();

    for( CipConn* active = g_active_connection_list;  active;
            active = active->next_cip_conn )
    {
        if( active->state == kConnectionStateEstablished )
        {
            // We have a consuming connection check inactivity watchdog timer.
            if( active->consuming_instance

                // All server connections have to maintain an inactivity watchdog timer
                || active->transport_trigger.IsServer() )
            {
                active->inactivity_watchdog_timer -= kOpenerTimerTickInMilliSeconds;

                if( active->inactivity_watchdog_timer <= 0 )
                {
                    // we have a timed out connection: perform watchdog check
                    CIPSTER_TRACE_INFO( "%s: >>>>>Connection timed out socket[0]:%d socket[1]:%d\n",
                        __func__,
                        active->socket[0], active->socket[1]
                        );

                    CIPSTER_ASSERT( active->connection_timeout_function );

                    active->connection_timeout_function( active );
                }
            }

            // only if the connection has not timed out check if data is to be sent
            if( active->state == kConnectionStateEstablished )
            {
                // client connection
                if( active->expected_packet_rate != 0 &&

                    // only produce for the master connection
                    active->socket[kUdpCommuncationDirectionProducing] != kEipInvalidSocket )
                {
                    if( active->transport_trigger.Trigger() != kConnectionTriggerTypeCyclic )
                    {
                        // non cyclic connections have to decrement production inhibit timer
                        if( 0 <= active->production_inhibit_timer )
                        {
                            active->production_inhibit_timer -= kOpenerTimerTickInMilliSeconds;
                        }
                    }

                    active->transmission_trigger_timer -= kOpenerTimerTickInMilliSeconds;

                    if( active->transmission_trigger_timer <= 0 ) // need to send package
                    {
                        CIPSTER_ASSERT( active->connection_send_data_function );

                        eip_status = active->connection_send_data_function( active );

                        if( eip_status == kEipStatusError )
                        {
                            CIPSTER_TRACE_ERR( "sending of UDP data in manage Connection failed\n" );
                        }

                        // reload the timer value
                        active->transmission_trigger_timer = active->expected_packet_rate;

                        if( active->transport_trigger.Trigger() != kConnectionTriggerTypeCyclic )
                        {
                            // non cyclic connections have to reload the production inhibit timer
                            active->production_inhibit_timer = active->production_inhibit_time;
                        }
                    }
                }
            }
        }
    }

    return kEipStatusOk;
}


/* TODO: Update Documentation  INT8 assembleFWDOpenResponse(S_CIP_CipConn *pa_pstConnObj, S_CIP_MR_Response * pa_MRResponse, EIP_UINT8 pa_nGeneralStatus, EIP_UINT16 pa_nExtendedStatus,
 * * deleteMeSomeday, EIP_UINT8 * pa_msg)
 *   create FWDOpen response dependent on status.
 *      pa_pstConnObj pointer to connection Object
 *      pa_MRResponse	pointer to message router response
 *      pa_nGeneralStatus the general status of the response
 *      pa_nExtendedStatus extended status in the case of an error otherwise 0
 *      pa_CPF_data	pointer to CPF Data Item
 *      pa_msg		pointer to memory where reply has to be stored
 *  return status
 *          0 .. no reply need to be sent back
 *          1 .. need to send reply
 *        -1 .. error
 */
static EipStatus assembleForwardOpenResponse( CipConn* co,
        CipMessageRouterResponse* response, EipUint8 general_status,
        EipUint16 extended_status )
{
    // write reply information in CPF struct dependent of pa_status
    CipCommonPacketFormatData* cpfd = &g_cpf;

    EipByte* message = response->data;

    cpfd->item_count = 2;
    cpfd->data_item.type_id = kCipItemIdUnconnectedDataItem;

    AddNullAddressItem( cpfd );

    response->reply_service  = 0x80 | kForwardOpen;
    response->general_status = general_status;

    if( kCipErrorSuccess == general_status )
    {
        CIPSTER_TRACE_INFO( "assembleFWDOpenResponse: sending success response\n" );
        response->data_length = 26; // if there is no application specific data
        response->size_of_additional_status = 0;

        if( cpfd->address_info_item[0].type_id != 0 )
        {
            cpfd->item_count = 3;

            if( cpfd->address_info_item[1].type_id != 0 )
            {
                cpfd->item_count = 4; // there are two sockaddrinfo items to add
            }
        }

        AddDintToMessage( co->consumed_connection_id, &message );
        AddDintToMessage( co->produced_connection_id, &message );
    }
    else
    {
        // we have an connection creation error
        CIPSTER_TRACE_INFO( "assembleFWDOpenResponse: sending error response\n" );
        co->state = kConnectionStateNonExistent;
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
                response->additional_status[1] = co->correct_originator_to_target_size;
                break;

            case kConnectionManagerStatusCodeErrorInvalidTToOConnectionSize:
                response->size_of_additional_status = 2;
                response->additional_status[0] = extended_status;
                response->additional_status[1] = co->correct_target_to_originator_size;
                break;

            default:
                response->size_of_additional_status = 1;
                response->additional_status[0] = extended_status;
                break;
            }
            break;
        }
    }

    AddIntToMessage( co->connection_serial_number, &message );
    AddIntToMessage( co->originator_vendor_id, &message );
    AddDintToMessage( co->originator_serial_number, &message );

    if( kCipErrorSuccess == general_status )
    {
        // set the actual packet rate to requested packet rate
        AddDintToMessage( co->o_to_t_requested_packet_interval, &message );
        AddDintToMessage( co->t_to_o_requested_packet_interval, &message );
    }

    *message++ = 0;   // remaining path size - for routing devices relevant
    *message++ = 0;   // reserved

    return kEipStatusOkSend; // send reply
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


CipConn* GetConnectedObject( EipUint32 connection_id )
{
    CipConn* conn = g_active_connection_list;

    while( conn )
    {
        if( conn->state == kConnectionStateEstablished )
        {
            if( conn->consumed_connection_id == connection_id )
                return conn;
        }

        conn = conn->next_cip_conn;
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
            if( active->conn_path.connection_point[0] == output_assembly_id )
                return active;
        }

        active = active->next_cip_conn;
    }

    return NULL;
}


void CloseConnection( CipConn* conn )
{
    conn->state = kConnectionStateNonExistent;

    if( conn->transport_trigger.Class() != kConnectionTransportClass3 )
    {
        // only close the UDP connection for not class 3 connections
        IApp_CloseSocket_udp( conn->socket[kUdpCommuncationDirectionConsuming] );
        conn->socket[kUdpCommuncationDirectionConsuming] = kEipInvalidSocket;

        IApp_CloseSocket_udp( conn->socket[kUdpCommuncationDirectionProducing] );
        conn->socket[kUdpCommuncationDirectionProducing] = kEipInvalidSocket;
    }

    RemoveFromActiveConnections( conn );
}


void AddNewActiveConnection( CipConn* conn )
{
    conn->first_cip_conn = NULL;
    conn->next_cip_conn  = g_active_connection_list;

    if( NULL != g_active_connection_list )
    {
        g_active_connection_list->first_cip_conn = conn;
    }

    g_active_connection_list = conn;
    g_active_connection_list->state = kConnectionStateEstablished;
}


void RemoveFromActiveConnections( CipConn* conn )
{
    if( NULL != conn->first_cip_conn )
    {
        conn->first_cip_conn->next_cip_conn =
            conn->next_cip_conn;
    }
    else
    {
        g_active_connection_list = conn->next_cip_conn;
    }

    if( NULL != conn->next_cip_conn )
    {
        conn->next_cip_conn->first_cip_conn =
            conn->first_cip_conn;
    }

    conn->first_cip_conn = NULL;
    conn->next_cip_conn  = NULL;
    conn->state = kConnectionStateNonExistent;
}



bool IsConnectedInputAssembly( EipUint32 aInstanceId )
{
    CipConn* conn = g_active_connection_list;

    while( conn )
    {
        if( aInstanceId == conn->conn_path.connection_point[1] )
            return true;

        conn = conn->next_cip_conn;
    }

    return false;
}


bool IsConnectedOutputAssembly( EipUint32 aInstanceId )
{
    CipConn* conn = g_active_connection_list;

    while( conn )
    {
        if( aInstanceId == conn->conn_path.connection_point[0] )
            return true;

        conn = conn->next_cip_conn;
    }

    return false;
}


EipStatus AddConnectableObject( EipUint32 aClassId, OpenConnectionFunction func )
{
    // parsing is now finished all data is available and check now establish the connection
    for( int i = 0; i < NUM_CONNECTABLE_OBJECTS; ++i )
    {
        if( !g_astConnMgmList[i].class_id || aClassId == g_astConnMgmList[i].class_id )
        {
            CIPSTER_TRACE_INFO(
                "%s: adding classId %d with function ptr %p at index %d\n",
                __func__, aClassId, func, i
                );

            g_astConnMgmList[i].class_id = aClassId;
            g_astConnMgmList[i].open_connection_function = func;

            table_init = true;

            return kEipStatusOk;
        }
    }

    CIPSTER_TRACE_INFO( "%s: unable to add aClassId:%d\n", __func__, aClassId );

    return kEipStatusError;
}


ConnectionManagementHandling* GetConnMgmEntry( EipUint32 class_id )
{
    for( int i = 0;  i < NUM_CONNECTABLE_OBJECTS;  ++i )
    {
        CIPSTER_TRACE_INFO( "%s: [%d]:class_id: %d\n",
            __func__, i, g_astConnMgmList[i].class_id );

        if( class_id == g_astConnMgmList[i].class_id )
        {
            CIPSTER_TRACE_ERR( "%s: found class %d at entry %d\n", __func__, class_id, i );
            return &g_astConnMgmList[i];
        }
    }

    CIPSTER_TRACE_ERR( "%s: could not find class %d\n", __func__, class_id );

    return NULL;
}


EipStatus TriggerConnections( unsigned aOutputAssembly, unsigned aInputAssembly )
{
    EipStatus nRetVal = kEipStatusError;

    CipConn* conn = g_active_connection_list;

    while( conn )
    {
        if( aOutputAssembly == conn->conn_path.connection_point[0]
         && aInputAssembly  == conn->conn_path.connection_point[1] )
        {
            if( conn->transport_trigger.Trigger() == kConnectionTriggerTypeApplication )
            {
                // produce at the next allowed occurrence
                conn->transmission_trigger_timer = conn->production_inhibit_timer;
                nRetVal = kEipStatusOk;
            }

            break;
        }
    }

    return nRetVal;
}


void InitializeConnectionManagerData()
{
    CIPSTER_TRACE_INFO( "%s: \n", __func__ );

    memset( g_astConnMgmList, 0, sizeof g_astConnMgmList );

    InitializeClass3ConnectionData();
    InitializeIoConnectionData();
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
 * @return EipStatus -
 *         >0 .. success, 0 .. no reply to send back
 *         -1 .. error
 */
static EipStatus forwardOpenCommon( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response, bool isLarge )
{
    EipUint16 connection_status = kConnectionManagerStatusCodeSuccess;
    ConnectionManagementHandling* connection_management_entry;

    (void) instance;        // suppress compiler warning

    // first check if we have already a connection with the given params
    s_dummy_conn.priority_timetick = *request->data++;
    s_dummy_conn.timeout_ticks = *request->data++;

    // O_to_T Conn ID
    s_dummy_conn.consumed_connection_id   = GetDintFromMessage( &request->data );

    // T_to_O Conn ID
    s_dummy_conn.produced_connection_id   = GetDintFromMessage( &request->data );

    // The Connection Triad used in the Connection Manager specification relates
    // to the combination of Connection Serial Number, Originator Vendor ID and
    // Originator Serial Number parameters.
    s_dummy_conn.connection_serial_number = GetIntFromMessage( &request->data );
    s_dummy_conn.originator_vendor_id     = GetIntFromMessage( &request->data );
    s_dummy_conn.originator_serial_number = GetDintFromMessage( &request->data );

    if( CheckForExistingConnection( &s_dummy_conn ) )
    {
        // TODO this test is  incorrect, see CIP spec 3-5.5.2 re: duplicate forward open
        // it should probably be testing the connection type fields
        // TODO think on how a reconfiguration request could be handled correctly.
        if( !s_dummy_conn.consumed_connection_id && !s_dummy_conn.produced_connection_id )
        {
            // TODO implement reconfiguration of connection

            CIPSTER_TRACE_ERR(
                    "this looks like a duplicate forward open -- I can't handle this yet,\n"
                    "sending a CIP_CON_MGR_ERROR_CONNECTION_IN_USE response\n" );
        }

        return assembleForwardOpenResponse(
                &s_dummy_conn, response,
                kCipErrorConnectionFailure,
                kConnectionManagerStatusCodeErrorConnectionInUse );
    }

    // keep it to non-existent until the setup is done, this eases error handling and
    // the state changes within the forward open request can not be detected from
    // the application or from outside (reason we are single threaded)
    s_dummy_conn.state = kConnectionStateNonExistent;

    s_dummy_conn.sequence_count_producing = 0; // set the sequence count to zero

    s_dummy_conn.connection_timeout_multiplier = *request->data++;
    request->data += 3; // skip over 3 reserved bytes.

    // the requested packet interval parameter needs to be a multiple of
    // TIMERTICK from the header file
    CIPSTER_TRACE_INFO(
        "%s: ConConnID %u, ProdConnID %u, ConnSerNo %u\n",
        __func__,
        s_dummy_conn.consumed_connection_id,
        s_dummy_conn.produced_connection_id,
        s_dummy_conn.connection_serial_number
        );

    s_dummy_conn.o_to_t_requested_packet_interval    = GetDintFromMessage( &request->data );

    if( isLarge )
        s_dummy_conn.o_to_t_ncp.SetLarge( GetDintFromMessage( &request->data ) );
    else
        s_dummy_conn.o_to_t_ncp.SetNotLarge( GetIntFromMessage( &request->data ) );

    s_dummy_conn.t_to_o_requested_packet_interval = GetDintFromMessage( &request->data );

    EipUint32 temp = s_dummy_conn.t_to_o_requested_packet_interval
                         % (kOpenerTimerTickInMilliSeconds * 1000);

    if( temp > 0 )
    {
        s_dummy_conn.t_to_o_requested_packet_interval =
            (EipUint32) ( s_dummy_conn.t_to_o_requested_packet_interval
                          / (kOpenerTimerTickInMilliSeconds * 1000) )
            * (kOpenerTimerTickInMilliSeconds * 1000)
            + (kOpenerTimerTickInMilliSeconds * 1000);
    }

    if( isLarge )
        s_dummy_conn.t_to_o_ncp.SetLarge( GetDintFromMessage( &request->data ) );
    else
        s_dummy_conn.t_to_o_ncp.SetNotLarge( GetIntFromMessage( &request->data ) );

    // check if Network connection parameters are ok
    if( s_dummy_conn.o_to_t_ncp.ConnectionType() == kIOConnTypeInvalid )
    {
        CIPSTER_TRACE_INFO( "%s: invalid O to T connection type\n", __func__ );

        return assembleForwardOpenResponse(
                &s_dummy_conn, response,
                kCipErrorConnectionFailure,
                kConnectionManagerStatusCodeErrorInvalidOToTConnectionType );
    }

    if( s_dummy_conn.t_to_o_ncp.ConnectionType() == kIOConnTypeInvalid )
    {
        CIPSTER_TRACE_INFO( "%s: invalid T to O connection type\n", __func__ );

        return assembleForwardOpenResponse(
                &s_dummy_conn, response,
                kCipErrorConnectionFailure,
                kConnectionManagerStatusCodeErrorInvalidTToOConnectionType );
    }

    int trigger = *request->data++;

    // check for undocumented trigger bits
    if( 0x4c & trigger )
    {
        CIPSTER_TRACE_INFO( "%s: 'server' trigger 0x%02x not supported\n",
            __func__, trigger );

        return assembleForwardOpenResponse(
                &s_dummy_conn, response,
                kCipErrorConnectionFailure,
                kConnectionManagerStatusCodeErrorTransportTriggerNotSupported );
    }

    s_dummy_conn.transport_trigger.Set( trigger );

    temp = parseConnectionPath( &s_dummy_conn, request, &connection_status );

    if( kEipStatusOk != temp )
    {
        CIPSTER_TRACE_INFO( "%s: unable to parse connection path\n", __func__ );

        return assembleForwardOpenResponse( &s_dummy_conn,
                response, temp,
                connection_status );
    }

    // parsing is now finished, all data is available, now establish the connection
    connection_management_entry = GetConnMgmEntry( s_dummy_conn.conn_path.class_id );
    if( connection_management_entry )
    {
        temp = connection_management_entry->open_connection_function(
                &s_dummy_conn, &connection_status );

        CIPSTER_TRACE_INFO( "%s: open_connection_function, temp = %d\n", __func__, temp );
    }
    else
    {
        CIPSTER_TRACE_INFO( "%s: GetConnMgmEntry returned NULL\n", __func__ );
        temp = kEipStatusError;
        connection_status = kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
    }

    if( kEipStatusOk != temp )
    {
        CIPSTER_TRACE_INFO( "%s: open_connection_function() failed. ret:%d\n", __func__, temp );

        // in case of error the dummy objects holds all necessary information
        return assembleForwardOpenResponse( &s_dummy_conn,
                response, temp,
                connection_status );
    }
    else
    {
        CIPSTER_TRACE_INFO( "%s: open_connection_function() succeeded\n", __func__ );

        // in case of success the g_pstActiveConnectionList points to the new connection
        return assembleForwardOpenResponse( g_active_connection_list,
                response,
                kCipErrorSuccess, 0 );
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

    // set AddressInfo Items to invalid TypeID to prevent assembleLinearMsg to read them
    g_cpf.address_info_item[0].type_id   = 0;
    g_cpf.address_info_item[1].type_id   = 0;

    request->data += 2; // ignore Priority/Time_tick and Time-out_ticks

    EipUint16 connection_serial_number = GetIntFromMessage( &request->data );
    EipUint16 originator_vendor_id = GetIntFromMessage( &request->data );
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
             && (active->originator_vendor_id == originator_vendor_id)
             && (active->originator_serial_number == originator_serial_number) )
            {
                // found the corresponding connection object -> close it
                CIPSTER_ASSERT( active->connection_close_function );
                active->connection_close_function( active );
                connection_status = kConnectionManagerStatusCodeSuccess;
                break;
            }
        }

        active = active->next_cip_conn;
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


static CipInstance* createConnectionManagerInstance()
{
    CipClass* clazz = GetCipClass( kCipConnectionManagerClassCode );

    CipInstance* i = new CipInstance( clazz->Instances().size() + 1 );

    clazz->InstanceInsert( i );

    return i;
}


EipStatus ConnectionManagerInit( EipUint16 unique_connection_id )
{
    if( !GetCipClass( kCipConnectionManagerClassCode ) )
    {
        CipClass* clazz = new CipClass( kCipConnectionManagerClassCode,
                "Connection Manager",       // class name
                (1<<7)|(1<<6)|(1<<5)|(1<<4)|(1<<3)|(1<<2)|(1<<1),
                MASK4( 7, 6, 2, 1 ),        // class getAttributeAll mask
                0,                          // instance getAttributeAll mask
                1                           // revision
                );

        RegisterCipClass( clazz );

        // There are no attributes in instance of this class yet, so nothing to set.
        delete clazz->ServiceRemove( kSetAttributeSingle );

        clazz->ServiceInsert( kForwardOpen, forwardOpen, "ForwardOpen" );
        clazz->ServiceInsert( kLargeForwardOpen, largeForwardOpen, "LargeForwardOpen" );

        clazz->ServiceInsert( kForwardClose, forwardClose, "ForwardClose" );

        //clazz->ServiceInsert( kGetConnectionOwner, getConnectionOwner, "GetConnectionOwner" );

        // add one instance
        createConnectionManagerInstance();

        g_incarnation_id = ( (EipUint32) unique_connection_id ) << 16;

        InitializeConnectionManagerData();

        AddConnectableObject( kCipMessageRouterClassCode, EstablishClass3Connection );
        AddConnectableObject( kCipAssemblyClassCode, EstablishIoConnction );
    }

    return kEipStatusOk;
}

