/*******************************************************************************
 * Copyright (c) 2011, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/

#include <string.h>
#include <algorithm>

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


/**
 * Function closeCommunicationChannelsAndRemoveFromActiveConnectionsList
 * closes the communication channels of the given connection and remove it
 * from the active connections list.
 *
 * @param aConn the connection to close
 */
static void closeCommunicationChannelsAndRemoveFromActiveConnectionsList( CipConn* aConn )
{
    IApp_CloseSocket_udp( aConn->consuming_socket );

    aConn->consuming_socket = kEipInvalidSocket;

    IApp_CloseSocket_udp( aConn->producing_socket );

    aConn->producing_socket = kEipInvalidSocket;

    RemoveFromActiveConnections( aConn );
}


static ConnectionManagerStatusCode handleConfigData( CipConn* aConn )
{
    ConnectionManagerStatusCode result = kConnectionManagerStatusCodeSuccess;

    CipInstance* instance = aConn->config_instance;

    Words& words = aConn->conn_path.data_seg.words;

    if( ConnectionWithSameConfigPointExists( aConn->conn_path.config_path.GetInstanceOrConnPt() ) )
    {
        // There is a connected connection with the same config point ->
        // we have to have the same data as already present in the config point,
        // else it's an error.  And if same, no reason to write it.

        CipByteArray* p = (CipByteArray*) instance->Attribute( 3 )->data;

        if( p->length != words.size() * 2  ||
            memcmp( p->data, words.data(), p->length ) != 0 )
        {
            result = kConnectionManagerStatusCodeErrorOwnershipConflict;
        }
    }

    // Put the data into the configuration assembly object
    else if( kEipStatusOk != NotifyAssemblyConnectedDataReceived( instance,
             CipBufNonMutable( (EipByte*)  words.data(),  words.size() * 2 ) ) )
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


CipConn::CipConn()
{
    Clear();
}


void CipConn::Clear()
{
    state = kConnectionStateNonExistent;
    instance_type = kConnInstanceTypeExplicit;

    producing_connection_size = 0;
    consuming_connection_size = 0;

    producing_connection_id = 0;
    consuming_connection_id = 0;

    consuming_socket = kEipInvalidSocket;
    producing_socket = kEipInvalidSocket;

    mgmnt_class = 0;

    watchdog_timeout_action = kWatchdogTimeoutActionTransitionToTimedOut;

    priority_timetick = 0;
    timeout_ticks = 0;
    connection_serial_number = 0;
    originator_vendor_id = 0;
    originator_serial_number = 0;
    connection_timeout_multiplier = 0;

    t_to_o_RPI_usecs = 0;
    o_to_t_RPI_usecs = 0;

    t_to_o_ncp.Clear();
    o_to_t_ncp.Clear();

    transport_trigger.Clear();

    consuming_instance = NULL;
    producing_instance = NULL;
    config_instance = NULL;

    conn_path.Clear();

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

    connection_close_function = NULL;
    connection_timeout_function = NULL;
    connection_send_data_function = NULL;
    connection_receive_data_function = NULL;

    next = NULL;
    first = NULL;

    correct_originator_to_target_size = 0;
    correct_target_to_originator_size = 0;

    expected_packet_rate_usecs = 0;
}


void GeneralConnectionConfiguration( CipConn* aConn )
{
    if( aConn->o_to_t_ncp.ConnectionType() == kIOConnTypePointToPoint )
    {
        // if we have a point to point connection for the O to T direction
        // the target shall choose the connection ID.
        aConn->consuming_connection_id = getConnectionId();
    }

    if( aConn->t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast )
    {
        // if we have a multi-cast connection for the T to O direction the
        // target shall choose the connection ID.

        aConn->producing_connection_id = getConnectionId();
        CIPSTER_TRACE_INFO( "%s: new producing multicast connection_id:0x%08x\n",
            __func__, aConn->producing_connection_id );
    }

    aConn->eip_level_sequence_count_producing = 0;
    aConn->sequence_count_producing = 0;

    aConn->eip_level_sequence_count_consuming = 0;
    aConn->eip_level_sequence_count_consuming_first = true;

    aConn->sequence_count_consuming = 0;

    aConn->watchdog_timeout_action = kWatchdogTimeoutActionAutoDelete;  // the default for all connections on EIP

    aConn->SetExpectedPacketRateUSecs( 0 );    // default value

    if( !aConn->transport_trigger.IsServer() )  // Client Type Connection requested
    {
        aConn->SetExpectedPacketRateUSecs( aConn->t_to_o_RPI_usecs );

        /* As soon as we are ready we should produce the connection. With the 0
         * here we will produce with the next timer tick
         * which should be sufficient.
         */
        aConn->transmission_trigger_timer_usecs = 0;
    }
    else
    {
        // Server Type Connection requested
        aConn->SetExpectedPacketRateUSecs( aConn->o_to_t_RPI_usecs );
    }

    aConn->production_inhibit_timer_usecs = 0;

    aConn->SetPIT_USecs( 0 );

    // setup the preconsuption timer: max(ConnectionTimeoutMultiplier * EpectetedPacketRate, 10s)
    aConn->inactivity_watchdog_timer_usecs = std::max(
            aConn->o_to_t_RPI_usecs << (2 + aConn->connection_timeout_multiplier), 10000000u );

    CIPSTER_TRACE_INFO( "%s: inactivity_watchdog_timer_usecs:%u\n", __func__,
            aConn->inactivity_watchdog_timer_usecs );

    aConn->consuming_connection_size = aConn->o_to_t_ncp.ConnectionSize();
    aConn->producing_connection_size = aConn->t_to_o_ncp.ConnectionSize();
}



/**
 * Function closeIoConnection
 * closes an IO connection. If it is an exclusive owner or input only
 * connection and in charge of the connection a new owner will be searched
 */
static void closeIoConnection( CipConn* aConn )
{
    CheckIoConnectionEvent( aConn->conn_path.consuming_path.GetInstanceOrConnPt(),
            aConn->conn_path.producing_path.GetInstanceOrConnPt(),
            kIoConnectionEventClosed );

    if( aConn->instance_type == kConnInstanceTypeIoExclusiveOwner
     || aConn->instance_type == kConnInstanceTypeIoInputOnly )
    {
        if( aConn->t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast
         && aConn->producing_socket != kEipInvalidSocket )
        {
            CipConn* next_non_control_master_connection =
                GetNextNonControlMasterConnection( aConn->conn_path.producing_path.GetInstanceOrConnPt() );

            if( next_non_control_master_connection )
            {
                next_non_control_master_connection->producing_socket =
                    aConn->producing_socket;

                next_non_control_master_connection->remote_address = aConn->remote_address;

                next_non_control_master_connection->eip_level_sequence_count_producing =
                    aConn->eip_level_sequence_count_producing;

                next_non_control_master_connection->sequence_count_producing =
                    aConn->sequence_count_producing;

                aConn->producing_socket = kEipInvalidSocket;

                next_non_control_master_connection->transmission_trigger_timer_usecs =
                    aConn->transmission_trigger_timer_usecs;
            }
            else // this was the last master connection close all listen only connections listening on the port
            {
                CloseAllConnectionsForInputWithSameType(
                        aConn->conn_path.producing_path.GetInstanceOrConnPt(),
                        kConnInstanceTypeIoListenOnly );
            }
        }
    }

    closeCommunicationChannelsAndRemoveFromActiveConnectionsList( aConn );
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
static EipStatus sendConnectedData( CipConn* aConn )
{
    /*
        TODO think of adding an own send buffer to each connection object in
        order to pre-setup the whole message on connection opening and just
        change the variable data items e.g., sequence number
    */
    CipCommonPacketFormatData cpfd;

    aConn->eip_level_sequence_count_producing++;

    // assembleCPFData
    cpfd.SetItemCount( 2 );

    // use Sequenced Address Items if not Connection Class 0
    if( aConn->transport_trigger.Class() != kConnectionTransportClass0 )
    {
        cpfd.address_item.type_id = kCipItemIdSequencedAddressItem;
        cpfd.address_item.length  = 8;
        cpfd.address_item.data.sequence_number = aConn->eip_level_sequence_count_producing;
    }
    else
    {
        cpfd.address_item.type_id = kCipItemIdConnectionAddress;
        cpfd.address_item.length  = 4;
    }

    // notify the application that data will be sent immediately after the call
    if( BeforeAssemblyDataSend( aConn->producing_instance ) )
    {
        // the data has changed, increase sequence counter
        aConn->sequence_count_producing++;
    }

    cpfd.address_item.data.connection_identifier = aConn->producing_connection_id;

    cpfd.data_item.type_id = kCipItemIdConnectedDataItem;

    CipAttribute* attr3 = aConn->producing_instance->Attribute( 3 );
    CIPSTER_ASSERT( attr3 );

    CipByteArray* attr3_byte_array = (CipByteArray*) attr3->data;
    CIPSTER_ASSERT( attr3_byte_array );

    cpfd.data_item.length = 0;

    // set AddressInfo Items to invalid Type
    cpfd.ClearTx();

    CipBufMutable output( g_message_data_reply_buffer, sizeof g_message_data_reply_buffer );

    int reply_length = cpfd.SerializeForIO( output );

    if( reply_length == -1 )
    {
    }

    EipByte*   message = &g_message_data_reply_buffer[reply_length - 2];

    cpfd.data_item.length = attr3_byte_array->length;

    if( kOpenerProducedDataHasRunIdleHeader )
    {
        cpfd.data_item.length += 4;
    }

    if( aConn->transport_trigger.Class() == kConnectionTransportClass1 )
    {
        cpfd.data_item.length += 2;

        AddIntToMessage( cpfd.data_item.length, &message );

        AddIntToMessage( aConn->sequence_count_producing, &message );
    }
    else
    {
        AddIntToMessage( cpfd.data_item.length, &message );
    }

    if( kOpenerProducedDataHasRunIdleHeader )
    {
        AddDintToMessage( g_run_idle_state, &message );
    }

    // @todo verify this is not a buffer overrun.

    memcpy( message, attr3_byte_array->data,
            attr3_byte_array->length );

    reply_length += cpfd.data_item.length;

    EipStatus result = SendUdpData(
            &aConn->remote_address,
            aConn->producing_socket,
            CipBufNonMutable( g_message_data_reply_buffer, reply_length )
            );

    return result;
}


static EipStatus handleReceivedIoConnectionData( CipConn* aConn, CipBufNonMutable aInput )
{
    // check class 1 sequence number
    if( aConn->transport_trigger.Class() == kConnectionTransportClass1 )
    {
        const EipByte* in = aInput.data();

        EipUint16 sequence_buffer = GetIntFromMessage( &in );

        if( SEQ_LEQ16( sequence_buffer, aConn->sequence_count_consuming ) )
        {
            return kEipStatusOk; // no new data for the assembly
        }

        aConn->sequence_count_consuming = sequence_buffer;

        aInput += 2;
    }

    if( aInput.size() )
    {
        // we have no heartbeat connection
        if( kOpenerConsumedDataHasRunIdleHeader )
        {
            const EipByte* in = aInput.data();

            EipUint32 nRunIdleBuf = GetDintFromMessage( &in );

            if( g_run_idle_state != nRunIdleBuf )
            {
                RunIdleChanged( nRunIdleBuf );
            }

            g_run_idle_state = nRunIdleBuf;

            aInput += 4;
        }

        if( NotifyAssemblyConnectedDataReceived( aConn->consuming_instance, aInput ) != 0 )
        {
            return kEipStatusError;
        }
    }

    return kEipStatusOk;
}


static void handleIoConnectionTimeOut( CipConn* aConn )
{
    CipConn* next_non_control_master_connection;

    CheckIoConnectionEvent( aConn->conn_path.consuming_path.GetInstanceOrConnPt(),
            aConn->conn_path.producing_path.GetInstanceOrConnPt(),
            kIoConnectionEventTimedOut );

    if( aConn->t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast )
    {
        switch( aConn->instance_type )
        {
        case kConnInstanceTypeIoExclusiveOwner:
            CloseAllConnectionsForInputWithSameType(
                    aConn->conn_path.producing_path.GetInstanceOrConnPt(),
                    kConnInstanceTypeIoInputOnly );
            CloseAllConnectionsForInputWithSameType(
                    aConn->conn_path.producing_path.GetInstanceOrConnPt(),
                    kConnInstanceTypeIoListenOnly );
            break;

        case kConnInstanceTypeIoInputOnly:
            if( kEipInvalidSocket
                != aConn->producing_socket ) // we are the controlling input only connection find a new controller
            {
                next_non_control_master_connection =
                    GetNextNonControlMasterConnection( aConn->conn_path.producing_path.GetInstanceOrConnPt() );

                if( NULL != next_non_control_master_connection )
                {
                    next_non_control_master_connection->producing_socket =
                        aConn->producing_socket;
                    aConn->producing_socket =
                        kEipInvalidSocket;
                    next_non_control_master_connection->transmission_trigger_timer_usecs =
                        aConn->transmission_trigger_timer_usecs;
                }

                // this was the last master connection close all listen only
                // connections listening on the port
                else
                {
                    CloseAllConnectionsForInputWithSameType(
                            aConn->conn_path.producing_path.GetInstanceOrConnPt(),
                            kConnInstanceTypeIoListenOnly );
                }
            }

            break;

        default:
            break;
        }
    }

    CIPSTER_ASSERT( NULL != aConn->connection_close_function );
    aConn->connection_close_function( aConn );
}


/*   @brief Open a Point2Point connection dependent on pa_direction.
 *   @param cip_conn Pointer to registered Object in ConnectionManager.
 *   @param cpfd Index of the connection object
 *   @return status
 *               0 .. success
 *              -1 .. error
 */
EipStatus OpenConsumingPointToPointConnection( CipConn* aConn, CipCommonPacketFormatData* cpfd )
{
    // TODO think on improving the udp port assigment for point to point
    // connections

    sockaddr_in addr;

    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl( INADDR_ANY );
    addr.sin_port        = htons( kOpenerEipIoUdpPort );

    // the address is only needed for bind used if consuming
    int socket = CreateUdpSocket( kUdpConsuming, &addr );

    if( socket == kEipInvalidSocket )
    {
        CIPSTER_TRACE_ERR( "%s: cannot create UDP socket\n", __func__ );
        return kEipStatusError;
    }

    // CreateUdpSocket can modify addr.

    aConn->originator_address = addr;    // store the address of the originator for packet scanning

    addr.sin_addr.s_addr = htonl( INADDR_ANY );  // restore the address

    aConn->consuming_socket = socket;

    SocketAddressInfoItem saii(
        kCipItemIdSocketAddressInfoOriginatorToTarget,
        INADDR_ANY,
        kOpenerEipIoUdpPort
        );

    cpfd->AppendTx( saii );

    return kEipStatusOk;
}


CipError OpenProducingPointToPointConnection( CipConn* aConn, CipCommonPacketFormatData* cpfd )
{
    // The default port to be used if no port information is
    // part of the forward_open request.
    in_port_t port = htons( kOpenerEipIoUdpPort );

    SocketAddressInfoItem* saii = cpfd->SearchRx( kCipItemIdSocketAddressInfoTargetToOriginator );
    if( saii )
    {
        int hport = saii->sin_port;
        port = htons( hport );
    }

    aConn->remote_address.sin_family = AF_INET;

    // we don't know the address of the originate will be set in the
    // IApp_CreateUDPSocket
    aConn->remote_address.sin_addr.s_addr = 0;

    aConn->remote_address.sin_port = port;

    int socket = CreateUdpSocket( kUdpProducing,
            &aConn->remote_address );    // the address is only needed for bind used if consuming

    if( socket == kEipInvalidSocket )
    {
        CIPSTER_TRACE_ERR(
                "cannot create UDP socket in OpenPointToPointConnection\n" );
        // *pa_pnExtendedError = 0x0315; miscellaneous
        return kCipErrorConnectionFailure;
    }

    aConn->producing_socket = socket;

    return kCipErrorSuccess;
}


EipStatus OpenProducingMulticastConnection( CipConn* aConn, CipCommonPacketFormatData* cpfd )
{
    CipConn* existing_conn =
        GetExistingProducerMulticastConnection( aConn->conn_path.producing_path.GetInstanceOrConnPt() );

    // If we are the first connection producing for the given Input Assembly
    if( !existing_conn )
    {
        return OpenMulticastConnection( kUdpProducing, aConn, cpfd );
    }
    else
    {
        // inform our originator about the correct connection id
        aConn->producing_connection_id = existing_conn->producing_connection_id;
    }

    // we have a connection reuse the data and the socket

    if( kConnInstanceTypeIoExclusiveOwner == aConn->instance_type )
    {
        /* exclusive owners take the socket and further manage the connection
         * especially in the case of time outs.
         */
        aConn->producing_socket = existing_conn->producing_socket;

        existing_conn->producing_socket =  kEipInvalidSocket;
    }
    else    // this connection will not produce the data
    {
        aConn->producing_socket = kEipInvalidSocket;
    }

    SocketAddressInfoItem saii( kCipItemIdSocketAddressInfoTargetToOriginator,
        ntohl( g_multicast_configuration.starting_multicast_address ),
        kOpenerEipIoUdpPort
        );

    cpfd->AppendTx( saii );

    aConn->remote_address = saii;

    return kEipStatusOk;
}


/**  @brief Open a Multicast connection dependent on @var direction.
 *
 *   @param direction Flag to indicate if consuming or producing.
 *   @param aConn registered CipConn in ConnectionManager.
 *   @param cpfd     received CPF Data Item.
 *   @return status
 *               0 .. success
 *              -1 .. error
 */
EipStatus OpenMulticastConnection( UdpCommuncationDirection direction,
        CipConn* aConn,
        CipCommonPacketFormatData* cpfd )
{
    SocketAddressInfoItem* saii = NULL;

    // see Vol2 3-3.9.4 Sockaddr Info Item Placement and Errors
    if( direction == kUdpConsuming )
    {
        saii = cpfd->SearchRx( kCipItemIdSocketAddressInfoOriginatorToTarget );

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

        aConn->originator_address = socket_address;
    }

    else
    {
        SocketAddressInfoItem   a( kCipItemIdSocketAddressInfoTargetToOriginator,
            ntohl( g_multicast_configuration.starting_multicast_address ),
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

        aConn->producing_socket = socket;
        aConn->remote_address   = socket_address;
    }

    CIPSTER_TRACE_INFO( "%s: opened OK\n", __func__ );

    return kEipStatusOk;
}


CipError OpenCommunicationChannels( CipConn* aConn )
{
    CipError eip_status = kCipErrorSuccess;

    CipCommonPacketFormatData cpfd;

    IOConnType o_to_t = aConn->o_to_t_ncp.ConnectionType();
    IOConnType t_to_o = aConn->t_to_o_ncp.ConnectionType();

    // open a connection "point to point" or "multicast" based on the ConnectionParameter
    if( o_to_t == kIOConnTypeMulticast )
    {
        if( OpenMulticastConnection( kUdpConsuming, aConn, &cpfd ) == kEipStatusError )
        {
            CIPSTER_TRACE_ERR( "error in OpenMulticast Connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    else if( o_to_t == kIOConnTypePointToPoint )
    {
        if( OpenConsumingPointToPointConnection( aConn, &cpfd ) == kEipStatusError )
        {
            CIPSTER_TRACE_ERR( "error in PointToPoint consuming connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    if( t_to_o == kIOConnTypeMulticast )
    {
        if( OpenProducingMulticastConnection( aConn, &cpfd ) == kEipStatusError )
        {
            CIPSTER_TRACE_ERR( "error in OpenMulticast Connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    else if( t_to_o == kIOConnTypePointToPoint )
    {
        if( OpenProducingPointToPointConnection( aConn, &cpfd ) != kCipErrorSuccess )
        {
            CIPSTER_TRACE_ERR( "error in PointToPoint producing connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    return eip_status;
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
    delete ServiceRemove( kSetAttributeSingle );
}


CipError CipConnectionClass::OpenIO( CipConn* aConn, ConnectionManagerStatusCode* extended_error )
{
    IOConnType o_to_t;
    IOConnType t_to_o;

    // currently we allow I/O connections only to assembly objects

    CipConn* io_conn = GetIoConnectionForConnectionData( aConn, extended_error );

    if( !io_conn )
    {
        CIPSTER_TRACE_ERR(
            "%s: no reserved IO connection was found for:\n"
            " %s.\n"
            " All anticipated IO connections must be reserved with Configure<*>ConnectionPoint()\n",
            __func__,
            aConn->conn_path.Format().c_str()
            );

        return kCipErrorConnectionFailure;
    }

    // Both Change of State and Cyclic triggers use the Transmission Trigger Timer
    // according to Vol1_3.19_3-4.4.3.7.

    if( io_conn->transport_trigger.Trigger() != kConnectionTriggerTypeCyclic )
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
            data_size -= 2;     // remove 16-bit sequence count length
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
            // wrong connection size
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


const std::string CipConnPath::Format() const
{
    std::string ret;

    if( config_path.HasAny() )
    {
        ret += "config_path=\"";
        ret += config_path.Format();
        ret += '"';
    }

    if( consuming_path.HasAny() )
    {
        if( ret.size() )
            ret += ' ';

        ret += "consuming_path=\"";
        ret += consuming_path.Format();
        ret += '"';
    }

    if( producing_path.HasAny() )
    {
        if( ret.size() )
            ret += ' ';

        ret += "producing_path=\"";
        ret += producing_path.Format();
        ret += '"';
    }

    return ret;
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

