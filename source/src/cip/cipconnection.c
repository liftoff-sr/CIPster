/*******************************************************************************
 * Copyright (c) 2011, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/

#include <string.h>

#include "cipconnection.h"

#include "cipconnectionmanager.h"
#include "cipassembly.h"
#include "ciptcpipinterface.h"
#include "cipcommon.h"
#include "appcontype.h"
#include "cpf.h"
#include "trace.h"
#include "endianconv.h"

// The port to be used per default for I/O messages on UDP.
const int kOpenerEipIoUdpPort = 2222;   // = 0x08AE;


EipUint32 g_run_idle_state;    //*< buffer for holding the run idle information.


/* producing multicast connections have to consider the rules that apply for
 * application connection types.
 */
EipStatus OpenProducingMulticastConnection( CipConn* conn,
        CipCommonPacketFormatData* cpfd );

EipStatus OpenMulticastConnection( UdpCommuncationDirection direction,
        CipConn* conn,
        CipCommonPacketFormatData* cpfd );

EipStatus OpenConsumingPointToPointConnection( CipConn* conn,
        CipCommonPacketFormatData* cpfd );

CipError OpenProducingPointToPointConnection( CipConn* conn,
        CipCommonPacketFormatData* cpfd );


static ConnectionManagerStatusCode handleConfigData( CipConn* conn )
{
    ConnectionManagerStatusCode result = kConnectionManagerStatusCodeSuccess;

    CipInstance* instance = conn->config_instance;

    Words& words = conn->conn_path.data_seg.words;

    if( ConnectionWithSameConfigPointExists( conn->conn_path.config_path.GetInstanceOrConnPt() ) )
    {
        // there is a connected connection with the same config point ->
        // we have to have the same data as already present in the config point,
        // else its an error.  And if same, no reason to write it.

        CipByteArray* p = (CipByteArray*) instance->Attribute( 3 )->data;

        if( p->length != words.size() * 2  ||
            memcmp( p->data, words.data(), p->length ) != 0 )
        {
            result = kConnectionManagerStatusCodeErrorOwnershipConflict;
        }
    }

    // put the data into the configuration assembly object
    else if( kEipStatusOk != NotifyAssemblyConnectedDataReceived( instance,
             (EipByte*)  words.data(),  words.size() * 2 ) )
    {
        CIPSTER_TRACE_WARN( "Configuration data was invalid\n" );
        result = kConnectionManagerStatusCodeInvalidConfigurationApplicationPath;
    }

    return result;
}


/// @brief Holds the connection ID's "incarnation ID" in the upper 16 bits
static EipUint32 g_incarnation_id;


/** @brief Generate a new connection Id utilizing the Incarnation Id as
 * described in the EIP specs.
 *
 * A unique connectionID is formed from the boot-time-specified "incarnation ID"
 * and the per-new-connection-incremented connection number/counter.
 * @return new connection id
 */
static EipUint32 getConnectionId()
{
    static EipUint32 connection_id = 18;

    connection_id++;
    return g_incarnation_id | (connection_id & 0x0000FFFF);
}


void GeneralConnectionConfiguration( CipConn* conn )
{
    if( conn->o_to_t_ncp.ConnectionType() == kIOConnTypePointToPoint )
    {
        // if we have a point to point connection for the O to T direction
        // the target shall choose the connection ID.
        conn->consuming_connection_id = getConnectionId();
    }

    if( conn->t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast )
    {
        // if we have a multi-cast connection for the T to O direction the
        // target shall choose the connection ID.
        conn->producing_connection_id = getConnectionId();
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

    conn->consuming_connection_size = conn->o_to_t_ncp.ConnectionSize();
    conn->producing_connection_size = conn->t_to_o_ncp.ConnectionSize();
}



/* Regularly close the IO connection. If it is an exclusive owner or input only
 * connection and in charge of the connection a new owner will be searched
 */
static void closeIoConnection( CipConn* conn )
{
    CheckIoConnectionEvent( conn->conn_path.consuming_path.GetInstanceOrConnPt(),
            conn->conn_path.producing_path.GetInstanceOrConnPt(),
            kIoConnectionEventClosed );

    if( conn->instance_type == kConnInstanceTypeIoExclusiveOwner
     || conn->instance_type == kConnInstanceTypeIoInputOnly )
    {
        if( conn->t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast
         && conn->producing_socket != kEipInvalidSocket )
        {
            CipConn* next_non_control_master_connection =
                GetNextNonControlMasterConnection( conn->conn_path.producing_path.GetInstanceOrConnPt() );

            if( next_non_control_master_connection )
            {
                next_non_control_master_connection->producing_socket =
                    conn->producing_socket;

                memcpy( &(next_non_control_master_connection->remote_address),
                        &(conn->remote_address),
                        sizeof(next_non_control_master_connection->remote_address) );

                next_non_control_master_connection->eip_level_sequence_count_producing =
                    conn->eip_level_sequence_count_producing;

                next_non_control_master_connection->sequence_count_producing =
                    conn->sequence_count_producing;

                conn->producing_socket =
                    kEipInvalidSocket;

                next_non_control_master_connection->transmission_trigger_timer =
                    conn->transmission_trigger_timer;
            }
            else // this was the last master connection close all listen only connections listening on the port
            {
                CloseAllConnectionsForInputWithSameType(
                        conn->conn_path.producing_path.GetInstanceOrConnPt(),
                        kConnInstanceTypeIoListenOnly );
            }
        }
    }

    CloseCommunicationChannelsAndRemoveFromActiveConnectionsList( conn );
}


/**
 * Function sendConnectedData
 * sends the data from the producing CIP Object of the connection via the socket
 * of the connection instance on UDP.
 *
 *      @param cip_conn  pointer to the connection object
 *      @return status  EIP_OK .. success
 *                     EIP_ERROR .. error
 */
static EipStatus sendConnectedData( CipConn* conn )
{
    CipCommonPacketFormatData* cpfd;
    EipUint16   reply_length;
    EipUint8*   message_data_reply_buffer;

    /*
        TODO think of adding an own send buffer to each connection object in
        order to preset up the whole message on connection opening and just
        change the variable data items e.g., sequence number
    */

    // TODO think on adding a CPF data item to the S_CIP_CipConn in order to
    // remove the code here or even better allocate memory in the connection object
    // for storing the message to send and just change the application data
    cpfd = &g_cpf;

    conn->eip_level_sequence_count_producing++;

    // assembleCPFData
    cpfd->item_count = 2;

    // use Sequenced Address Items if not Connection Class 0
    if( conn->transport_trigger.Class() != kConnectionTransportClass0 )
    {
        cpfd->address_item.type_id = kCipItemIdSequencedAddressItem;
        cpfd->address_item.length  = 8;
        cpfd->address_item.data.sequence_number =
            conn->eip_level_sequence_count_producing;
    }
    else
    {
        cpfd->address_item.type_id = kCipItemIdConnectionAddress;
        cpfd->address_item.length  = 4;
    }

    cpfd->address_item.data.connection_identifier =
        conn->producing_connection_id;

    cpfd->data_item.type_id = kCipItemIdConnectedDataItem;

    CipAttribute* attr3 = conn->producing_instance->Attribute( 3 );
    CIPSTER_ASSERT( attr3 );

    CipByteArray* producing_instance_attributes = (CipByteArray*) attr3->data;
    CIPSTER_ASSERT( producing_instance_attributes );

    cpfd->data_item.length = 0;

    // notify the application that data will be sent immediately after the call
    if( BeforeAssemblyDataSend( conn->producing_instance ) )
    {
        // the data has changed increase sequence counter
        conn->sequence_count_producing++;
    }

    // set AddressInfo Items to invalid Type
    cpfd->address_info_item[0].type_id = 0;
    cpfd->address_info_item[1].type_id = 0;

    reply_length = cpfd->AssembleIOMessage( &g_message_data_reply_buffer[0] );

    message_data_reply_buffer = &g_message_data_reply_buffer[reply_length - 2];
    cpfd->data_item.length = producing_instance_attributes->length;

    if( kOpenerProducedDataHasRunIdleHeader )
    {
        cpfd->data_item.length += 4;
    }

    if( conn->transport_trigger.Class() == kConnectionTransportClass1 )
    {
        cpfd->data_item.length += 2;

        AddIntToMessage( cpfd->data_item.length, &message_data_reply_buffer );

        AddIntToMessage( conn->sequence_count_producing, &message_data_reply_buffer );
    }
    else
    {
        AddIntToMessage( cpfd->data_item.length, &message_data_reply_buffer );
    }

    if( kOpenerProducedDataHasRunIdleHeader )
    {
        AddDintToMessage( g_run_idle_state, &message_data_reply_buffer );
    }

    // @todo verify this is not a buffer overrun.

    memcpy( message_data_reply_buffer, producing_instance_attributes->data,
            producing_instance_attributes->length );

    reply_length += cpfd->data_item.length;

    EipStatus result = SendUdpData(
            &conn->remote_address,
            conn->producing_socket,
            g_message_data_reply_buffer, reply_length );

    return result;
}


static EipStatus handleReceivedIoConnectionData( CipConn* conn,
        EipUint8* data, EipUint16 data_length )
{
    // check class 1 sequence number
    if( conn->transport_trigger.Class() == kConnectionTransportClass1 )
    {
        EipUint16 sequence_buffer = GetIntFromMessage( &(data) );

        if( SEQ_LEQ16( sequence_buffer, conn->sequence_count_consuming ) )
        {
            return kEipStatusOk; // no new data for the assembly
        }

        conn->sequence_count_consuming = sequence_buffer;
        data_length -= 2;
    }

    if( data_length > 0 )
    {
        // we have no heartbeat connection
        if( kOpenerConsumedDataHasRunIdleHeader )
        {
            EipUint32 nRunIdleBuf = GetDintFromMessage( &data );

            if( g_run_idle_state != nRunIdleBuf )
            {
                RunIdleChanged( nRunIdleBuf );
            }

            g_run_idle_state = nRunIdleBuf;
            data_length -= 4;
        }

        if( NotifyAssemblyConnectedDataReceived(
                    conn->consuming_instance, data, data_length ) != 0 )
        {
            return kEipStatusError;
        }
    }

    return kEipStatusOk;
}


static void handleIoConnectionTimeOut( CipConn* conn )
{
    CipConn* next_non_control_master_connection;

    CheckIoConnectionEvent( conn->conn_path.consuming_path.GetInstanceOrConnPt(),
            conn->conn_path.producing_path.GetInstanceOrConnPt(),
            kIoConnectionEventTimedOut );

    if( conn->t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast )
    {
        switch( conn->instance_type )
        {
        case kConnInstanceTypeIoExclusiveOwner:
            CloseAllConnectionsForInputWithSameType(
                    conn->conn_path.producing_path.GetInstanceOrConnPt(),
                    kConnInstanceTypeIoInputOnly );
            CloseAllConnectionsForInputWithSameType(
                    conn->conn_path.producing_path.GetInstanceOrConnPt(),
                    kConnInstanceTypeIoListenOnly );
            break;

        case kConnInstanceTypeIoInputOnly:
            if( kEipInvalidSocket
                != conn->producing_socket ) // we are the controlling input only connection find a new controller
            {
                next_non_control_master_connection =
                    GetNextNonControlMasterConnection( conn->conn_path.producing_path.GetInstanceOrConnPt() );

                if( NULL != next_non_control_master_connection )
                {
                    next_non_control_master_connection->producing_socket =
                        conn->producing_socket;
                    conn->producing_socket =
                        kEipInvalidSocket;
                    next_non_control_master_connection->transmission_trigger_timer =
                        conn->transmission_trigger_timer;
                }

                // this was the last master connection close all listen only
                // connections listening on the port
                else
                {
                    CloseAllConnectionsForInputWithSameType(
                            conn->conn_path.producing_path.GetInstanceOrConnPt(),
                            kConnInstanceTypeIoListenOnly );
                }
            }

            break;

        default:
            break;
        }
    }

    CIPSTER_ASSERT( NULL != conn->connection_close_function );
    conn->connection_close_function( conn );
}


/*   @brief Open a Point2Point connection dependent on pa_direction.
 *   @param cip_conn Pointer to registered Object in ConnectionManager.
 *   @param cpfd Index of the connection object
 *   @return status
 *               0 .. success
 *              -1 .. error
 */
EipStatus OpenConsumingPointToPointConnection( CipConn* conn,
        CipCommonPacketFormatData* cpfd )
{
    // static EIP_UINT16 nUDPPort = 2222;

    // TODO think on improving the udp port assigment for point to point
    // connections

    int j = 0;
    struct sockaddr_in addr;
    int socket;

    if( cpfd->address_info_item[0].type_id == 0 ) // it is not used yet
    {
        j = 0;
    }
    else if( cpfd->address_info_item[1].type_id == 0 )
    {
        j = 1;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;

    //addr.in_port = htons(nUDPPort++);
    addr.sin_port = htons( kOpenerEipIoUdpPort );

    // the address is only needed for bind used if consuming
    socket = CreateUdpSocket( kUdpConsuming, &addr );

    if( socket == kEipInvalidSocket )
    {
        CIPSTER_TRACE_ERR( "%s: cannot create UDP socket\n", __func__ );
        return kEipStatusError;
    }

    conn->originator_address = addr;   // store the address of the originator for packet scanning
    addr.sin_addr.s_addr = INADDR_ANY;              // restore the address
    conn->consuming_socket = socket;

    cpfd->address_info_item[j].length  = 16;
    cpfd->address_info_item[j].type_id =
        kCipItemIdSocketAddressInfoOriginatorToTarget;

    cpfd->address_info_item[j].sin_port = addr.sin_port;

    // TODO should we add our own address here?
    cpfd->address_info_item[j].sin_addr = addr.sin_addr.s_addr;

    memset( cpfd->address_info_item[j].nasin_zero, 0, 8 );

    cpfd->address_info_item[j].sin_family = htons( AF_INET );

    return kEipStatusOk;
}


CipError OpenProducingPointToPointConnection( CipConn* conn, CipCommonPacketFormatData* cpfd )
{
    // The default port to be used if no port information is
    // part of the forward_open request.
    in_port_t port = htons( kOpenerEipIoUdpPort );

    if( cpfd->address_info_item[0].type_id == kCipItemIdSocketAddressInfoTargetToOriginator )
    {
        port = cpfd->address_info_item[0].sin_port;
    }
    else if( cpfd->address_info_item[1].type_id == kCipItemIdSocketAddressInfoTargetToOriginator )
    {
        port = cpfd->address_info_item[1].sin_port;
    }

    conn->remote_address.sin_family = AF_INET;

    // we don't know the address of the originate will be set in the
    // IApp_CreateUDPSocket
    conn->remote_address.sin_addr.s_addr = 0;

    conn->remote_address.sin_port = port;

    int socket = CreateUdpSocket( kUdpProducing,
            &conn->remote_address );    // the address is only needed for bind used if consuming

    if( socket == kEipInvalidSocket )
    {
        CIPSTER_TRACE_ERR(
                "cannot create UDP socket in OpenPointToPointConnection\n" );
        // *pa_pnExtendedError = 0x0315; miscellaneous
        return kCipErrorConnectionFailure;
    }

    conn->producing_socket = socket;

    return kCipErrorSuccess;
}


EipStatus OpenProducingMulticastConnection( CipConn* conn, CipCommonPacketFormatData* cpfd )
{
    CipConn* existing_conn =
        GetExistingProducerMulticastConnection( conn->conn_path.producing_path.GetInstanceOrConnPt() );

    // If we are the first connection producing for the given Input Assembly
    if( !existing_conn )
    {
        return OpenMulticastConnection( kUdpProducing, conn, cpfd );
    }
    else
    {
        // inform our originator about the correct connection id
        conn->producing_connection_id = existing_conn->producing_connection_id;
    }

    // we have a connection reuse the data and the socket

    int j = 0;   // allocate an unused sockaddr struct to use

    if( g_cpf.address_info_item[0].type_id == 0 )    // it is not used yet
    {
        j = 0;
    }
    else if( g_cpf.address_info_item[1].type_id == 0 )
    {
        j = 1;
    }

    if( kConnInstanceTypeIoExclusiveOwner == conn->instance_type )
    {
        /* exclusive owners take the socket and further manage the connection
         * especially in the case of time outs.
         */
        conn->producing_socket = existing_conn->producing_socket;

        existing_conn->producing_socket =  kEipInvalidSocket;
    }
    else    // this connection will not produce the data
    {
        conn->producing_socket = kEipInvalidSocket;
    }

    cpfd->address_info_item[j].length  = 16;
    cpfd->address_info_item[j].type_id = kCipItemIdSocketAddressInfoTargetToOriginator;

    conn->remote_address.sin_family = AF_INET;
    conn->remote_address.sin_port = cpfd->address_info_item[j].sin_port = htons( kOpenerEipIoUdpPort );

    conn->remote_address.sin_addr.s_addr =
    cpfd->address_info_item[j].sin_addr =
              g_multicast_configuration.starting_multicast_address;

    memset( cpfd->address_info_item[j].nasin_zero, 0, 8 );

    cpfd->address_info_item[j].sin_family = htons( AF_INET );

    return kEipStatusOk;
}


/**  @brief Open a Multicast connection dependent on @var direction.
 *
 *   @param direction Flag to indicate if consuming or producing.
 *   @param cip_conn  pointer to registered Object in ConnectionManager.
 *   @param cpfd     received CPF Data Item.
 *   @return status
 *               0 .. success
 *              -1 .. error
 */
EipStatus OpenMulticastConnection( UdpCommuncationDirection direction,
        CipConn* conn,
        CipCommonPacketFormatData* cpfd )
{
    int j = 0;
    int socket;

    if( 0 != g_cpf.address_info_item[0].type_id )
    {
        if( (kUdpConsuming == direction)
            && (kCipItemIdSocketAddressInfoOriginatorToTarget
                == cpfd->address_info_item[0].type_id) )
        {
            // for consuming connection points the originator can choose the
            // multicast address to use we have a given address type so use it
        }
        else
        {
            j = 1;

            // if the type is not zero (not used) or if a given type it has to be the correct one
            if( (0 != g_cpf.address_info_item[1].type_id)
                && ( !( (kUdpConsuming == direction)
                        && (kCipItemIdSocketAddressInfoOriginatorToTarget
                            == cpfd->address_info_item[0].type_id) ) ) )
            {
                CIPSTER_TRACE_ERR( "no suitable addr info item available\n" );
                return kEipStatusError;
            }
        }
    }

    // If we are using an unused item initialize it with the default multicast address
    if( 0 == cpfd->address_info_item[j].type_id )
    {
        cpfd->address_info_item[j].sin_family = htons( AF_INET );
        cpfd->address_info_item[j].sin_port = htons( kOpenerEipIoUdpPort );

        cpfd->address_info_item[j].sin_addr =
            g_multicast_configuration.starting_multicast_address;

        memset( cpfd->address_info_item[j].nasin_zero, 0, 8 );
        cpfd->address_info_item[j].length = 16;
    }

    if( htons( AF_INET ) != cpfd->address_info_item[j].sin_family )
    {
        CIPSTER_TRACE_ERR(
                "Sockaddr Info Item with wrong sin family value recieved\n" );
        return kEipStatusError;
    }

    // allocate an unused sockaddr struct to use
    struct sockaddr_in socket_address;

    socket_address.sin_family = ntohs( cpfd->address_info_item[j].sin_family );

    socket_address.sin_addr.s_addr = cpfd->address_info_item[j].sin_addr;

    socket_address.sin_port = cpfd->address_info_item[j].sin_port;

    // the address is only needed for bind used if consuming
    socket = CreateUdpSocket( direction, &socket_address );

    if( socket == kEipInvalidSocket )
    {
        CIPSTER_TRACE_ERR( "cannot create UDP socket in OpenMulticastConnection\n" );
        return kEipStatusError;
    }

    if( direction == kUdpConsuming )
    {
        cpfd->address_info_item[j].type_id = kCipItemIdSocketAddressInfoOriginatorToTarget;

        conn->originator_address = socket_address;
    }
    else
    {
        conn->producing_socket = socket;

        cpfd->address_info_item[j].type_id = kCipItemIdSocketAddressInfoTargetToOriginator;

        conn->remote_address = socket_address;
    }

    return kEipStatusOk;
}


CipError OpenCommunicationChannels( CipConn* conn )
{
    CipError eip_status = kCipErrorSuccess;

    // get pointer to the CPF data, currently we have just one global instance
    // of the struct. This may change in the future
    CipCommonPacketFormatData* cpfd = &g_cpf;

    IOConnType o_to_t = conn->o_to_t_ncp.ConnectionType();
    IOConnType t_to_o = conn->t_to_o_ncp.ConnectionType();

    // open a connection "point to point" or "multicast" based on the ConnectionParameter
    if( o_to_t == kIOConnTypeMulticast )
    {
        if( OpenMulticastConnection( kUdpConsuming,
                    conn, cpfd ) == kEipStatusError )
        {
            CIPSTER_TRACE_ERR( "error in OpenMulticast Connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    else if( o_to_t == kIOConnTypePointToPoint )
    {
        if( OpenConsumingPointToPointConnection( conn, cpfd ) == kEipStatusError )
        {
            CIPSTER_TRACE_ERR( "error in PointToPoint consuming connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    if( t_to_o == kIOConnTypeMulticast )
    {
        if( OpenProducingMulticastConnection( conn, cpfd ) == kEipStatusError )
        {
            CIPSTER_TRACE_ERR( "error in OpenMulticast Connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    else if( t_to_o == kIOConnTypePointToPoint )
    {
        if( OpenProducingPointToPointConnection( conn, cpfd ) != kCipErrorSuccess )
        {
            CIPSTER_TRACE_ERR( "error in PointToPoint producing connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    return eip_status;
}


void CloseCommunicationChannelsAndRemoveFromActiveConnectionsList( CipConn* conn )
{
    IApp_CloseSocket_udp( conn->consuming_socket );

    conn->consuming_socket = kEipInvalidSocket;

    IApp_CloseSocket_udp( conn->producing_socket );

    conn->producing_socket = kEipInvalidSocket;

    RemoveFromActiveConnections( conn );
}


CipConnectionClass::CipConnectionClass() :
    CipClass(
        kConnectionClassId,
        "Connection",
        (1<<7)|(1<<6) /* |(1<<5)|(1<<4)|(1<<3)|(1<<2) */ | (1<<1),
        MASK3( 7, 6, 1 ),           // class getAttributeAll mask
        0,                          // instance getAttributeAll mask
        1                           // revision
        )
{
    // There are no attributes in instance of this class yet, so nothing to set.
    ServiceRemove( kSetAttributeSingle );
}


CipError CipConnectionClass::OpenConnection( CipConn* aConn, ConnectionManagerStatusCode* extended_error )
{
    IOConnType o_to_t;
    IOConnType t_to_o;

    // currently we allow I/O connections only to assembly objects

    CipConn* io_conn = GetIoConnectionForConnectionData( aConn, extended_error );

    if( !io_conn )
    {
        CIPSTER_TRACE_ERR( "%s: GetIoConnectionForConnectionData() returned NULL\n", __func__ );
        return kCipErrorConnectionFailure;
    }

    // Both Change of State and Cyclic triggers use the Transmission Trigger Timer
    // according to Vol1_3.19_3-4.4.3.7.

    if( io_conn->transport_trigger.Trigger() != kConnectionTriggerTypeCyclic )
    {
        // trigger is not cyclic, it is Change of State here.

        if( 256 == io_conn->production_inhibit_time )
        {
            // there was no PIT segment in the connection path, set PIT to one fourth of RPI
            io_conn->production_inhibit_time =
                ( (EipUint16) (io_conn->t_to_o_requested_packet_interval)
                  / 4000 );
        }
        else
        {
            // if production inhibit time has been provided it needs to be smaller than the RPI
            if( io_conn->production_inhibit_time
                > ( (EipUint16) ( (io_conn->t_to_o_requested_packet_interval) / 1000 ) ) )
            {
                // see section C-1.4.3.3
                *extended_error = kConnectionManagerStatusCodeErrorPITGreaterThanRPI;
                return kCipErrorConnectionFailure;
            }
        }
    }

    // set the connection call backs
    io_conn->connection_close_function        = closeIoConnection;
    io_conn->connection_timeout_function      = handleIoConnectionTimeOut;
    io_conn->connection_send_data_function    = sendConnectedData;
    io_conn->connection_receive_data_function = handleReceivedIoConnectionData;

    GeneralConnectionConfiguration( io_conn );

    o_to_t = io_conn->o_to_t_ncp.ConnectionType();
    t_to_o = io_conn->t_to_o_ncp.ConnectionType();

    int     data_size;
    int     diff_size;
    bool    is_heartbeat;

    if( o_to_t != kIOConnTypeNull )    // setup consumer side
    {
        CIPSTER_ASSERT( io_conn->consuming_instance );

        io_conn->conn_path.consuming_path.SetAttribute( 3 );

        CipAttribute* attribute = io_conn->consuming_instance->Attribute( 3 );

        // an assembly object should always have an attribute 3
        CIPSTER_ASSERT( attribute );

        data_size    = io_conn->consuming_connection_size;
        diff_size    = 0;
        is_heartbeat = ( ( (CipByteArray*) attribute->data )->length == 0 );

        if( io_conn->transport_trigger.Class() == kConnectionTransportClass1 )
        {
            // class 1 connection
            data_size -= 2; // remove 16-bit sequence count length
            diff_size += 2;
        }

        if( kOpenerConsumedDataHasRunIdleHeader && data_size > 0
            && !is_heartbeat )    // only have a run idle header if it is not a heartbeat connection
        {
            data_size -= 4;       // remove the 4 bytes needed for run/idle header
            diff_size += 4;
        }

        if( ( (CipByteArray*) attribute->data )->length != data_size )
        {
            // wrong connection size
            aConn->correct_originator_to_target_size =
                ( (CipByteArray*) attribute->data )->length + diff_size;

            *extended_error = kConnectionManagerStatusCodeErrorInvalidOToTConnectionSize;

            CIPSTER_TRACE_INFO( "%s: byte_array length != data_size\n", __func__ );
            return kCipErrorConnectionFailure;
        }
    }

    if( t_to_o != kIOConnTypeNull )     // setup producer side
    {
        CIPSTER_ASSERT( io_conn->producing_instance );

        io_conn->conn_path.producing_path.SetAttribute( 3 );

        CipAttribute* attribute = io_conn->producing_instance->Attribute( 3 );

        // an assembly object should always have an attribute 3
        CIPSTER_ASSERT( attribute );

        data_size    = io_conn->producing_connection_size;
        diff_size    = 0;
        is_heartbeat = ( ( (CipByteArray*) attribute->data )->length == 0 );

        if( io_conn->transport_trigger.Class() == kConnectionTransportClass1 )
        {
            // class 1 connection
            data_size -= 2; // remove 16-bit sequence count length
            diff_size += 2;
        }

        if( kOpenerProducedDataHasRunIdleHeader && data_size > 0
            && !is_heartbeat )  // only have a run idle header if it is not a heartbeat connection
        {
            data_size -= 4;   // remove the 4 bytes needed for run/idle header
            diff_size += 4;
        }

        if( ( (CipByteArray*) attribute->data )->length != data_size )
        {
            //wrong connection size
            aConn->correct_target_to_originator_size =
                ( (CipByteArray*) attribute->data )->length + diff_size;

            *extended_error = kConnectionManagerStatusCodeErrorInvalidTToOConnectionSize;

            CIPSTER_TRACE_INFO( "%s: bytearray length != data_size\n", __func__ );
            return kCipErrorConnectionFailure;
        }
    }

    // If config data is present in forward_open request
    if( io_conn->conn_path.data_seg.HasAny() )
    {
        *extended_error = handleConfigData( io_conn );

        if( kConnectionManagerStatusCodeSuccess != *extended_error )
        {
            CIPSTER_TRACE_INFO( "%s: extended_error != 0\n", __func__ );
            return kCipErrorConnectionFailure;
        }
    }

    CipError result = OpenCommunicationChannels( io_conn );

    if( result != kCipErrorSuccess )
    {
        *extended_error = kConnectionManagerStatusCodeSuccess;

        CIPSTER_TRACE_ERR( "%s: OpenCommunicationChannels failed\n", __func__ );
        return result;
    }

    AddNewActiveConnection( io_conn );

    CheckIoConnectionEvent(
            io_conn->conn_path.consuming_path.GetInstanceOrConnPt(),
            io_conn->conn_path.producing_path.GetInstanceOrConnPt(),
            kIoConnectionEventOpened );

    return result;
}


EipStatus ConnectionClassInit( EipUint16 unique_connection_id )
{
    if( !GetCipClass( kCipConnectionManagerClassCode ) )
    {
        CipClass* clazz = new CipConnectionClass();

        RegisterCipClass( clazz );

        g_incarnation_id = ( (EipUint32) unique_connection_id ) << 16;
    }

    return kEipStatusOk;
}


#if 0
static EipStatus create( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    CipClass* clazz = GetCipClass( kCipConnectionManagerClassCode );

    int id = find_unique_free_id( clazz );

    CipConn* conn = new CipConn( id );

    clazz->InstanceInsert( conn );
}
#endif

