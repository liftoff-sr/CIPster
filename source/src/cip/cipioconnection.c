/*******************************************************************************
 * Copyright (c) 2011, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/

#include <string.h>

#include "cipioconnection.h"

#include "cipconnectionmanager.h"
#include "cipassembly.h"
#include "ciptcpipinterface.h"
#include "cipcommon.h"
#include "appcontype.h"
#include "cpf.h"
#include "trace.h"
#include "endianconv.h"

//The port to be used per default for I/O messages on UDP.
const int kOpenerEipIoUdpPort = 2222;   // = 0x08AE;

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

EipUint16 HandleConfigData( CipClass* assembly_class,
        CipConn* conn );

/* Regularly close the IO connection. If it is an exclusive owner or input only
 * connection and in charge of the connection a new owner will be searched
 */
void CloseIoConnection( CipConn* conn );

void HandleIoConnectionTimeOut( CipConn* conn );

/** @brief  Send the data from the produced CIP Object of the connection via the socket of the connection object
 *   on UDP.
 *      @param cip_conn  pointer to the connection object
 *      @return status  EIP_OK .. success
 *                     EIP_ERROR .. error
 */
EipStatus SendConnectedData( CipConn* conn );

EipStatus HandleReceivedIoConnectionData( CipConn* conn,
        EipUint8* data, EipUint16 data_length );

//*** Global variables ***
EipUint8* g_config_data_buffer = NULL;    //*< buffers for the config data coming with a forward open request.
unsigned g_config_data_length = 0;

EipUint32 g_run_idle_state;    //*< buffer for holding the run idle information.

//*** Implementation ***
int EstablishIoConnction( CipConn* conn, EipUint16* extended_error )
{
    int originator_to_target_connection_type;
    int target_to_originator_connection_type;
    int eip_status = kEipStatusOk;

    CipAttribute* attribute;

    // currently we allow I/O connections only to assembly objects

    CipClass* assembly_class = GetCipClass( kCipAssemblyClassCode );

    CipInstance* instance = NULL;

    CipConn* io_conn = GetIoConnectionForConnectionData( conn, extended_error );

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
                *extended_error = 0x111;    //*< RPI not supported. Extended Error code deprecated
                return kCipErrorConnectionFailure;
            }
        }
    }

    // set the connection call backs
    io_conn->connection_close_function        = CloseIoConnection;
    io_conn->connection_timeout_function      = HandleIoConnectionTimeOut;
    io_conn->connection_send_data_function    = SendConnectedData;
    io_conn->connection_receive_data_function = HandleReceivedIoConnectionData;

    GeneralConnectionConfiguration( io_conn );

    originator_to_target_connection_type = io_conn->o_to_t_ncp.ConnectionType();
    target_to_originator_connection_type = io_conn->t_to_o_ncp.ConnectionType();

    if( originator_to_target_connection_type == kIOConnTypeNull
     && target_to_originator_connection_type == kIOConnTypeNull )
    {
        // this indicates a re-configuration of the connection; currently not
        // supported and we should not come here as this is trapped in the
        // forwardopen() function
    }
    else
    {
        int producing_index = 0;
        int data_size;
        int diff_size;
        int is_heartbeat;

        if( (originator_to_target_connection_type != 0)
            && (target_to_originator_connection_type != 0) )
        {
            // we have a producing and consuming connection
            producing_index = 1;
        }

        io_conn->consuming_instance = 0;
        io_conn->consumed_connection_path_length = 0;
        io_conn->producing_instance = 0;
        io_conn->produced_connection_path_length = 0;

        if( originator_to_target_connection_type != 0 ) // setup consumer side
        {
            int instance_id = io_conn->conn_path.connection_point[0];

            instance = assembly_class->Instance( instance_id );

            // consuming Connection Point is present
            if( instance )
            {
                io_conn->consuming_instance = instance;

                io_conn->consumed_connection_path_length = 6;
                io_conn->consumed_connection_path.path_size = 6;

                io_conn->consumed_connection_path.class_id = io_conn->conn_path.class_id;

                io_conn->consumed_connection_path.instance_number = instance_id;

                io_conn->consumed_connection_path.attribute_number = 3;

                attribute = instance->Attribute( 3 );

                // an assembly object should always have an attribute 3
                CIPSTER_ASSERT( attribute );

                data_size   = io_conn->consumed_connection_size;
                diff_size   = 0;
                is_heartbeat = ( ( (CipByteArray*) attribute->data )->length == 0 );

                if( io_conn->transport_trigger.Class() == kConnectionTransportClass1 )
                {
                    // class 1 connection
                    data_size   -= 2; // remove 16-bit sequence count length
                    diff_size   += 2;
                }

                if( (kOpenerConsumedDataHasRunIdleHeader) && data_size > 0
                    && !is_heartbeat )    // we only have an run idle header if it is not an heartbeat connection
                {
                    data_size   -= 4;       // remove the 4 bytes needed for run/idle header
                    diff_size   += 4;
                }

                if( ( (CipByteArray*) attribute->data )->length != data_size )
                {
                    // wrong connection size
                    conn->correct_originator_to_target_size =
                        ( (CipByteArray*) attribute->data )->length + diff_size;

                    *extended_error =
                        kConnectionManagerStatusCodeErrorInvalidOToTConnectionSize;

                    CIPSTER_TRACE_INFO( "%s: byte_array length != data_size\n", __func__ );
                    return kCipErrorConnectionFailure;
                }
            }
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

        if( target_to_originator_connection_type != 0 )     // setup producer side
        {
            instance = assembly_class->Instance( io_conn->conn_path.connection_point[producing_index] );

            if( instance )
            {
                io_conn->producing_instance = instance;

                io_conn->produced_connection_path_length = 6;
                io_conn->produced_connection_path.path_size = 6;
                io_conn->produced_connection_path.class_id  = io_conn->conn_path.class_id;

                io_conn->produced_connection_path.instance_number =
                    io_conn->conn_path.connection_point[producing_index];

                io_conn->produced_connection_path.attribute_number = 3;

                attribute = instance->Attribute( 3 );

                // an assembly object should always have an attribute 3
                CIPSTER_ASSERT( attribute );

                data_size   = io_conn->produced_connection_size;
                diff_size   = 0;
                is_heartbeat = ( ( (CipByteArray*) attribute->data )->length == 0 );

                if( io_conn->transport_trigger.Class() == kConnectionTransportClass1 )
                {
                    // class 1 connection
                    data_size   -= 2; // remove 16-bit sequence count length
                    diff_size   += 2;
                }

                if( kOpenerProducedDataHasRunIdleHeader && data_size > 0
                    && !is_heartbeat )  // we only have a run idle header if it is not a heartbeat connection
                {
                    data_size   -= 4;   // remove the 4 bytes needed for run/idle header
                    diff_size   += 4;
                }

                if( ( (CipByteArray*) attribute->data )->length != data_size )
                {
                    //wrong connection size
                    conn->correct_target_to_originator_size =
                        ( (CipByteArray*) attribute->data )->length + diff_size;

                    *extended_error = kConnectionManagerStatusCodeErrorInvalidTToOConnectionSize;

                    CIPSTER_TRACE_INFO( "%s: bytearray length != data_size\n", __func__ );
                    return kCipErrorConnectionFailure;
                }
            }
            else
            {
                *extended_error = kConnectionManagerStatusCodeInvalidProducingApplicationPath;

                CIPSTER_TRACE_INFO( "%s: 2 noinstance\n", __func__ );

                return kCipErrorConnectionFailure;
            }
        }

        // If config data has been sent with this forward open request
        if( g_config_data_buffer )
        {
            *extended_error = HandleConfigData( assembly_class, io_conn );

            if( 0 != *extended_error )
            {
                CIPSTER_TRACE_INFO( "%s: extended_error != 0\n", __func__ );
                return kCipErrorConnectionFailure;
            }
        }

        eip_status = OpenCommunicationChannels( io_conn );

        if( kEipStatusOk != eip_status )
        {
            *extended_error = 0;    // TODO find out the correct extended error code

            CIPSTER_TRACE_ERR( "%s: OpenCommunicationChannels failed\n", __func__ );
            return eip_status;
        }
    }

    AddNewActiveConnection( io_conn );

    CheckIoConnectionEvent(
            io_conn->conn_path.connection_point[0],
            io_conn->conn_path.connection_point[1],
            kIoConnectionEventOpened );

    return eip_status;
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
    socket = CreateUdpSocket( kUdpCommuncationDirectionConsuming, &addr );

    if( socket == kEipInvalidSocket )
    {
        CIPSTER_TRACE_ERR(
                "cannot create UDP socket in OpenPointToPointConnection\n" );
        return kEipStatusError;
    }

    conn->originator_address = addr;   // store the address of the originator for packet scanning
    addr.sin_addr.s_addr = INADDR_ANY;              // restore the address
    conn->socket[kUdpCommuncationDirectionConsuming] = socket;

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


CipError OpenProducingPointToPointConnection( CipConn* conn,
        CipCommonPacketFormatData* cpfd )
{
    int socket;

    // The default port to be used if no port information is
    // part of the forward open request.
    in_port_t port = htons( kOpenerEipIoUdpPort );

    if( kCipItemIdSocketAddressInfoTargetToOriginator
        == cpfd->address_info_item[0].type_id )
    {
        port = cpfd->address_info_item[0].sin_port;
    }
    else
    {
        if( kCipItemIdSocketAddressInfoTargetToOriginator
            == cpfd->address_info_item[1].type_id )
        {
            port = cpfd->address_info_item[1].sin_port;
        }
    }

    conn->remote_address.sin_family = AF_INET;

    // we don't know the address of the originate will be set in the
    // IApp_CreateUDPSocket
    conn->remote_address.sin_addr.s_addr = 0;

    conn->remote_address.sin_port = port;

    socket = CreateUdpSocket( kUdpCommuncationDirectionProducing,
            &conn->remote_address );               // the address is only needed for bind used if consuming

    if( socket == kEipInvalidSocket )
    {
        CIPSTER_TRACE_ERR(
                "cannot create UDP socket in OpenPointToPointConnection\n" );
        // *pa_pnExtendedError = 0x0315; miscellaneous
        return kCipErrorConnectionFailure;
    }

    conn->socket[kUdpCommuncationDirectionProducing] = socket;

    return kCipErrorSuccess;
}


EipStatus OpenProducingMulticastConnection( CipConn* conn, CipCommonPacketFormatData* cpfd )
{
    CipConn* existing_conn =
        GetExistingProducerMulticastConnection( conn->conn_path.connection_point[1] );

    int j;

    // If we are the first connection producing for the given Input Assembly
    if( NULL == existing_conn )
    {
        return OpenMulticastConnection( kUdpCommuncationDirectionProducing,
                conn, cpfd );
    }
    else
    {
        // we need to inform our originator on the correct connection id
        conn->produced_connection_id = existing_conn->produced_connection_id;
    }

    // we have a connection reuse the data and the socket

    j = 0;   // allocate an unused sockaddr struct to use

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
        conn->socket[kUdpCommuncationDirectionProducing] =
            existing_conn->socket[kUdpCommuncationDirectionProducing];

        existing_conn->socket[kUdpCommuncationDirectionProducing] =  kEipInvalidSocket;
    }
    else    // this connection will not produce the data
    {
        conn->socket[kUdpCommuncationDirectionProducing] =
            kEipInvalidSocket;
    }

    cpfd->address_info_item[j].length  = 16;
    cpfd->address_info_item[j].type_id =
        kCipItemIdSocketAddressInfoTargetToOriginator;
    conn->remote_address.sin_family = AF_INET;
    conn->remote_address.sin_port = cpfd->address_info_item[j].sin_port = htons( kOpenerEipIoUdpPort );

    conn->remote_address.sin_addr.s_addr = cpfd->address_info_item[j].sin_addr =
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
        if( (kUdpCommuncationDirectionConsuming == direction)
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
                && ( !( (kUdpCommuncationDirectionConsuming == direction)
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
        cpfd->address_info_item[j].sin_family = htons(
                AF_INET );
        cpfd->address_info_item[j].sin_port = htons(
                kOpenerEipIoUdpPort );
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

    conn->socket[direction] = socket;

    if( direction == kUdpCommuncationDirectionConsuming )
    {
        cpfd->address_info_item[j].type_id = kCipItemIdSocketAddressInfoOriginatorToTarget;

        conn->originator_address = socket_address;
    }
    else
    {
        cpfd->address_info_item[j].type_id = kCipItemIdSocketAddressInfoTargetToOriginator;

        conn->remote_address = socket_address;
    }

    return kEipStatusOk;
}


EipUint16 HandleConfigData( CipClass* assembly_class, CipConn* conn )
{
    EipUint16 connection_manager_status = 0;

    CipInstance* instance = assembly_class->Instance( conn->conn_path.connection_point[2] );

    if( g_config_data_length )
    {
        if( ConnectionWithSameConfigPointExists( conn->conn_path.connection_point[2] ) )
        {
            // there is a connected connection with the same config point
            // we have to have the same data as already present in the config point

            CipByteArray* p = (CipByteArray*) instance->Attribute( 3 )->data;

            if( p->length != g_config_data_length )
            {
                connection_manager_status = kConnectionManagerStatusCodeErrorOwnershipConflict;
            }
            else
            {
                //FIXME check if this is correct
                if( memcmp( p->data, g_config_data_buffer, g_config_data_length ) )
                {
                    connection_manager_status =
                        kConnectionManagerStatusCodeErrorOwnershipConflict;
                }
            }
        }
        else
        {
            // put the data into the configuration assembly object
            if( kEipStatusOk != NotifyAssemblyConnectedDataReceived( instance,
                       g_config_data_buffer,
                        g_config_data_length ) )
            {
                CIPSTER_TRACE_WARN( "Configuration data was invalid\n" );
                connection_manager_status =
                    kConnectionManagerStatusCodeInvalidConfigurationApplicationPath;
            }
        }
    }

    return connection_manager_status;
}


void CloseIoConnection( CipConn* conn )
{
    CheckIoConnectionEvent( conn->conn_path.connection_point[0],
            conn->conn_path.connection_point[1],
            kIoConnectionEventClosed );

    if( conn->instance_type == kConnInstanceTypeIoExclusiveOwner
     || conn->instance_type == kConnInstanceTypeIoInputOnly )
    {
        if( conn->t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast
         && conn->socket[kUdpCommuncationDirectionProducing] != kEipInvalidSocket )
        {
            CipConn* next_non_control_master_connection =
                GetNextNonControlMasterConnection( conn->conn_path.connection_point[1] );

            if( NULL != next_non_control_master_connection )
            {
                next_non_control_master_connection->socket[kUdpCommuncationDirectionProducing] =
                    conn->socket[kUdpCommuncationDirectionProducing];

                memcpy( &(next_non_control_master_connection->remote_address),
                        &(conn->remote_address),
                        sizeof(next_non_control_master_connection->remote_address) );

                next_non_control_master_connection->eip_level_sequence_count_producing =
                    conn->eip_level_sequence_count_producing;

                next_non_control_master_connection->sequence_count_producing =
                    conn->sequence_count_producing;

                conn->socket[kUdpCommuncationDirectionProducing] =
                    kEipInvalidSocket;

                next_non_control_master_connection->transmission_trigger_timer =
                    conn->transmission_trigger_timer;
            }
            else // this was the last master connection close all listen only connections listening on the port
            {
                CloseAllConnectionsForInputWithSameType(
                        conn->conn_path.connection_point[1],
                        kConnInstanceTypeIoListenOnly );
            }
        }
    }

    CloseCommunicationChannelsAndRemoveFromActiveConnectionsList( conn );
}


void HandleIoConnectionTimeOut( CipConn* conn )
{
    CipConn* next_non_control_master_connection;

    CheckIoConnectionEvent( conn->conn_path.connection_point[0],
            conn->conn_path.connection_point[1],
            kIoConnectionEventTimedOut );

    if( conn->t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast )
    {
        switch( conn->instance_type )
        {
        case kConnInstanceTypeIoExclusiveOwner:
            CloseAllConnectionsForInputWithSameType(
                    conn->conn_path.connection_point[1],
                    kConnInstanceTypeIoInputOnly );
            CloseAllConnectionsForInputWithSameType(
                    conn->conn_path.connection_point[1],
                    kConnInstanceTypeIoListenOnly );
            break;

        case kConnInstanceTypeIoInputOnly:
            if( kEipInvalidSocket
                != conn->socket[kUdpCommuncationDirectionProducing] ) // we are the controlling input only connection find a new controller
            {
                next_non_control_master_connection =
                    GetNextNonControlMasterConnection( conn->conn_path.connection_point[1] );

                if( NULL != next_non_control_master_connection )
                {
                    next_non_control_master_connection->socket[kUdpCommuncationDirectionProducing] =
                        conn->socket[kUdpCommuncationDirectionProducing];
                    conn->socket[kUdpCommuncationDirectionProducing] =
                        kEipInvalidSocket;
                    next_non_control_master_connection->transmission_trigger_timer =
                        conn->transmission_trigger_timer;
                }

                // this was the last master connection close all listen only
                // connections listening on the port
                else
                {
                    CloseAllConnectionsForInputWithSameType(
                            conn->conn_path.connection_point[1],
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


EipStatus SendConnectedData( CipConn* conn )
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
        conn->produced_connection_id;

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
    cpfd->data_item.length =
        producing_instance_attributes->length;

    if( kOpenerProducedDataHasRunIdleHeader )
    {
        cpfd->data_item.length += 4;
    }

    if( conn->transport_trigger.Class() == kConnectionTransportClass1 )
    {
        cpfd->data_item.length += 2;

        AddIntToMessage( cpfd->data_item.length,
                &message_data_reply_buffer );

        AddIntToMessage( conn->sequence_count_producing,
                &message_data_reply_buffer );
    }
    else
    {
        AddIntToMessage( cpfd->data_item.length,
                &message_data_reply_buffer );
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
            conn->socket[kUdpCommuncationDirectionProducing],
            &g_message_data_reply_buffer[0], reply_length );

    return result;
}


EipStatus HandleReceivedIoConnectionData( CipConn* conn,
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


CipError OpenCommunicationChannels( CipConn* conn )
{
    CipError eip_status = kCipErrorSuccess;

    // get pointer to the CPF data, currently we have just one global instance
    // of the struct. This may change in the future
    CipCommonPacketFormatData* cpfd = &g_cpf;

    int originator_to_target_connection_type = conn->o_to_t_ncp.ConnectionType();
    int target_to_originator_connection_type = conn->t_to_o_ncp.ConnectionType();

    // open a connection "point to point" or "multicast" based on the ConnectionParameter
    if( originator_to_target_connection_type == kIOConnTypeMulticast ) //TODO: Fix magic number; Multicast consuming
    {
        if( OpenMulticastConnection( kUdpCommuncationDirectionConsuming,
                    conn, cpfd ) == kEipStatusError )
        {
            CIPSTER_TRACE_ERR( "error in OpenMulticast Connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    // TODO: Fix magic number; Point to Point consuming
    else if( originator_to_target_connection_type == kIOConnTypePointToPoint )
    {
        if( OpenConsumingPointToPointConnection( conn, cpfd )
            == kEipStatusError )
        {
            CIPSTER_TRACE_ERR( "error in PointToPoint consuming connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    // TODO: Fix magic number; Multicast producing
    if( target_to_originator_connection_type == kIOConnTypeMulticast )
    {
        if( OpenProducingMulticastConnection( conn, cpfd )
            == kEipStatusError )
        {
            CIPSTER_TRACE_ERR( "error in OpenMulticast Connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    // TODO: Fix magic number; Point to Point producing
    else if( target_to_originator_connection_type == kIOConnTypePointToPoint )
    {
        if( OpenProducingPointToPointConnection( conn, cpfd )
            != kCipErrorSuccess )
        {
            CIPSTER_TRACE_ERR( "error in PointToPoint producing connection\n" );
            return kCipErrorConnectionFailure;
        }
    }

    return eip_status;
}


void CloseCommunicationChannelsAndRemoveFromActiveConnectionsList(
        CipConn* conn )
{
    IApp_CloseSocket_udp( conn->socket[kUdpCommuncationDirectionConsuming] );

    conn->socket[kUdpCommuncationDirectionConsuming] = kEipInvalidSocket;

    IApp_CloseSocket_udp( conn->socket[kUdpCommuncationDirectionProducing] );

    conn->socket[kUdpCommuncationDirectionProducing] = kEipInvalidSocket;

    RemoveFromActiveConnections( conn );
}


#if 0
static int find_unique_free_id( const CipClass* aClass )
{
    CipInstances& instances = aClass->Instances();

    int last_id = 0;
    for( CipInstance::const_iterator it: instances )
    {
        // Is there a gap here?
        if( (*it)->Id() > last_id + 1 )
            break;

        last_id = (*it)->Id();
    }

    return last_id + 1;
}


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

