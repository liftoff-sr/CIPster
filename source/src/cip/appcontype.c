/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#include "appcontype.h"
#include "cipconnectionmanager.h"
#include "opener_api.h"
#include <string.h>

/// @brief External globals needed from connectionmanager.c
extern CipConn* g_active_connection_list;

struct ExclusiveOwnerConnection
{
    unsigned output_assembly;           ///< the O-to-T point for the connection
    unsigned input_assembly;            ///< the T-to-O point for the connection
    unsigned config_assembly;           ///< the config point for the connection
    CipConn connection_data;   ///< the connection data, only one connection is allowed per O-to-T point
};

struct InputOnlyConnection
{
    unsigned output_assembly;           ///< the O-to-T point for the connection
    unsigned input_assembly;            ///< the T-to-O point for the connection
    unsigned config_assembly;           ///< the config point for the connection
    CipConn connection_data[CIPSTER_CIP_NUM_INPUT_ONLY_CONNS_PER_CON_PATH]; ///< the connection data
};

struct ListenOnlyConnection
{
    unsigned output_assembly;           ///< the O-to-T point for the connection
    unsigned input_assembly;            ///< the T-to-O point for the connection
    unsigned config_assembly;           ///< the config point for the connection
    CipConn connection_data[CIPSTER_CIP_NUM_LISTEN_ONLY_CONNS_PER_CON_PATH];    ///< the connection data
};

ExclusiveOwnerConnection g_exlusive_owner_connections[CIPSTER_CIP_NUM_EXLUSIVE_OWNER_CONNS];

InputOnlyConnection g_input_only_connections[CIPSTER_CIP_NUM_INPUT_ONLY_CONNS];

ListenOnlyConnection g_listen_only_connections[CIPSTER_CIP_NUM_LISTEN_ONLY_CONNS];

CipConn* GetExclusiveOwnerConnection( CipConn* cip_conn,
        EipUint16* extended_error );

CipConn* GetInputOnlyConnection( CipConn* cip_conn,
        EipUint16* extended_error );

CipConn* GetListenOnlyConnection( CipConn* cip_conn,
        EipUint16* extended_error );

void ConfigureExclusiveOwnerConnectionPoint( unsigned connection_number,
        unsigned output_assembly,
        unsigned input_assembly,
        unsigned config_assembly )
{
    if( CIPSTER_CIP_NUM_EXLUSIVE_OWNER_CONNS > connection_number )
    {
        g_exlusive_owner_connections[connection_number].output_assembly =
            output_assembly;
        g_exlusive_owner_connections[connection_number].input_assembly =
            input_assembly;
        g_exlusive_owner_connections[connection_number].config_assembly =
            config_assembly;
    }
}


void ConfigureInputOnlyConnectionPoint( unsigned connection_number,
        unsigned output_assembly,
        unsigned input_assembly,
        unsigned config_assembly )
{
    if( CIPSTER_CIP_NUM_INPUT_ONLY_CONNS > connection_number )
    {
        g_input_only_connections[connection_number].output_assembly =
            output_assembly;
        g_input_only_connections[connection_number].input_assembly  = input_assembly;
        g_input_only_connections[connection_number].config_assembly =
            config_assembly;
    }
}


void ConfigureListenOnlyConnectionPoint( unsigned connection_number,
        unsigned output_assembly,
        unsigned input_assembly,
        unsigned config_assembly )
{
    if( CIPSTER_CIP_NUM_LISTEN_ONLY_CONNS > connection_number )
    {
        g_listen_only_connections[connection_number].output_assembly =
            output_assembly;
        g_listen_only_connections[connection_number].input_assembly =
            input_assembly;
        g_listen_only_connections[connection_number].config_assembly =
            config_assembly;
    }
}


CipConn* GetIoConnectionForConnectionData( CipConn* cip_conn,
        EipUint16* extended_error )
{
    CipConn* io_connection = NULL;

    *extended_error = 0;

    io_connection = GetExclusiveOwnerConnection( cip_conn,
            extended_error );

    if( NULL == io_connection )
    {
        if( 0 == *extended_error )
        {
            // we found no connection and don't have an error so try input only next
            io_connection = GetInputOnlyConnection( cip_conn, extended_error );

            if( NULL == io_connection )
            {
                if( 0 == *extended_error )
                {
                    // we found no connection and don't have an error so try listen only next
                    io_connection = GetListenOnlyConnection( cip_conn,
                            extended_error );

                    if( (NULL == io_connection) && (0 == *extended_error) )
                    {
                        // no application connection type was found that suits the given data
                        // TODO check error code VS
                        *extended_error =
                            kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
                    }
                    else
                    {
                        cip_conn->instance_type = kConnectionTypeIoListenOnly;
                    }
                }
            }
            else
            {
                cip_conn->instance_type = kConnectionTypeIoInputOnly;
            }
        }
    }
    else
    {
        cip_conn->instance_type = kConnectionTypeIoExclusiveOwner;
    }

    if( io_connection )
    {
        CopyConnectionData( io_connection, cip_conn );
    }

    return io_connection;
}


CipConn* GetExclusiveOwnerConnection( CipConn* cip_conn,
        EipUint16* extended_error )
{
    CipConn* exclusive_owner_connection = NULL;

    for( int i = 0; i < CIPSTER_CIP_NUM_EXLUSIVE_OWNER_CONNS; i++ )
    {
        if( (g_exlusive_owner_connections[i].output_assembly
             == cip_conn->conn_path.connection_point[0])
            && (g_exlusive_owner_connections[i].input_assembly
                == cip_conn->conn_path.connection_point[1])
            && (g_exlusive_owner_connections[i].config_assembly
                == cip_conn->conn_path.connection_point[2]) )
        {
            // check if on other connection point with the same output assembly is currently connected
            if( GetConnectedOutputAssembly(
                        cip_conn->conn_path.connection_point[0] ) )
            {
                *extended_error = kConnectionManagerStatusCodeErrorOwnershipConflict;
                break;
            }

            exclusive_owner_connection = &g_exlusive_owner_connections[i].connection_data;
            break;
        }
    }

    return exclusive_owner_connection;
}


CipConn* GetInputOnlyConnection( CipConn* cip_conn, EipUint16* extended_error )
{
    CipConn* input_only_connection = NULL;

    for( int i = 0; i < CIPSTER_CIP_NUM_INPUT_ONLY_CONNS; i++ )
    {
        if( g_input_only_connections[i].output_assembly
            == cip_conn->conn_path.connection_point[0] ) // we have the same output assembly
        {
            if( g_input_only_connections[i].input_assembly
                != cip_conn->conn_path.connection_point[1] )
            {
                *extended_error =
                    kConnectionManagerStatusCodeInvalidProducingApplicationPath;
                break;
            }

            if( g_input_only_connections[i].config_assembly
                != cip_conn->conn_path.connection_point[2] )
            {
                *extended_error =
                    kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
                break;
            }

            for( int j = 0; j < CIPSTER_CIP_NUM_INPUT_ONLY_CONNS_PER_CON_PATH; j++ )
            {
                if( kConnectionStateNonExistent
                    == g_input_only_connections[i].connection_data[j].state )
                {
                    return &(g_input_only_connections[i].connection_data[j]);
                }
            }

            *extended_error =
                kConnectionManagerStatusCodeTargetObjectOutOfConnections;
            break;
        }
    }

    return input_only_connection;
}


CipConn* GetListenOnlyConnection( CipConn* cip_conn, EipUint16* extended_error )
{
    CipConn* listen_only_connection = NULL;

    if( kRoutingTypeMulticastConnection
        != (cip_conn->t_to_o_network_connection_parameter
            & kRoutingTypeMulticastConnection) )
    {
        // a listen only connection has to be a multicast connection.
        *extended_error =
            kConnectionManagerStatusCodeNonListenOnlyConnectionNotOpened; // maybe not the best error message however there is no suitable definition in the cip spec
        return NULL;
    }

    for( int i = 0; i < CIPSTER_CIP_NUM_LISTEN_ONLY_CONNS; i++ )
    {
        if( g_listen_only_connections[i].output_assembly
            == cip_conn->conn_path.connection_point[0] ) // we have the same output assembly
        {
            if( g_listen_only_connections[i].input_assembly
                != cip_conn->conn_path.connection_point[1] )
            {
                *extended_error =
                    kConnectionManagerStatusCodeInvalidProducingApplicationPath;
                break;
            }

            if( g_listen_only_connections[i].config_assembly
                != cip_conn->conn_path.connection_point[2] )
            {
                *extended_error =
                    kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
                break;
            }

            if( NULL == GetExistingProducerMulticastConnection(
                        cip_conn->conn_path.connection_point[1] ) )
            {
                *extended_error =
                    kConnectionManagerStatusCodeNonListenOnlyConnectionNotOpened;
                break;
            }

            for( int j = 0; j < CIPSTER_CIP_NUM_LISTEN_ONLY_CONNS_PER_CON_PATH; j++ )
            {
                if( kConnectionStateNonExistent
                    == g_listen_only_connections[i].connection_data[j].state )
                {
                    return &(g_listen_only_connections[i].connection_data[j]);
                }
            }

            *extended_error =
                kConnectionManagerStatusCodeTargetObjectOutOfConnections;
            break;
        }
    }

    return listen_only_connection;
}


CipConn* GetExistingProducerMulticastConnection( EipUint32 input_point )
{
    CipConn* producer_multicast_connection = g_active_connection_list;

    while( producer_multicast_connection )
    {
        if( (kConnectionTypeIoExclusiveOwner == producer_multicast_connection->instance_type)
            || (kConnectionTypeIoInputOnly   == producer_multicast_connection->instance_type) )
        {
            if( (input_point
                 == producer_multicast_connection->conn_path.connection_point[1])
                && ( kRoutingTypeMulticastConnection
                     == (producer_multicast_connection
                         ->t_to_o_network_connection_parameter
                         & kRoutingTypeMulticastConnection) )
                && (kEipInvalidSocket
                    != producer_multicast_connection->socket[kUdpCommuncationDirectionProducing]) )
            {
                /* we have a connection that produces the same input assembly,
                 * is a multicast producer and manages the connection.
                 */
                break;
            }
        }

        producer_multicast_connection = producer_multicast_connection
                                        ->next_cip_conn;
    }

    return producer_multicast_connection;
}


CipConn* GetNextNonControlMasterConnection( EipUint32 input_point )
{
    CipConn* next_non_control_master_connection =
        g_active_connection_list;

    while( next_non_control_master_connection )
    {
        if( (kConnectionTypeIoExclusiveOwner == next_non_control_master_connection->instance_type)
            || (kConnectionTypeIoInputOnly   == next_non_control_master_connection->instance_type) )
        {
            if( (input_point == next_non_control_master_connection->conn_path.connection_point[1])
                && ( kRoutingTypeMulticastConnection ==
                    (next_non_control_master_connection->t_to_o_network_connection_parameter & kRoutingTypeMulticastConnection) )
                && (kEipInvalidSocket == next_non_control_master_connection->socket[kUdpCommuncationDirectionProducing]) )
            {
                /* we have a connection that produces the same input assembly,
                 * is a multicast producer and does not manages the connection.
                 */
                break;
            }
        }

        next_non_control_master_connection = next_non_control_master_connection
                                             ->next_cip_conn;
    }

    return next_non_control_master_connection;
}


void CloseAllConnectionsForInputWithSameType( EipUint32 input_point,
        ConnectionType instance_type )
{
    CipConn* connection = g_active_connection_list;
    CipConn* connection_to_delete;

    while( connection )
    {
        if( (instance_type == connection->instance_type)
            && (input_point == connection->conn_path.connection_point[1]) )
        {
            connection_to_delete = connection;
            connection = connection->next_cip_conn;

            CheckIoConnectionEvent(
                    connection_to_delete->conn_path.connection_point[0],
                    connection_to_delete->conn_path.connection_point[1],
                    kIoConnectionEventClosed );

            // FIXME check if this is ok
            connection_to_delete->connection_close_function( connection_to_delete );
            // closeConnection(pstToDelete); will remove the connection from the active connection list
        }
        else
        {
            connection = connection->next_cip_conn;
        }
    }
}


void CloseAllConnections()
{
    CipConn* connection = g_active_connection_list;

    while( connection )
    {
        // FIXME check if m_pfCloseFunc would be suitable
        CloseConnection( connection );

        /* Close connection will remove the connection from the list therefore we
         * need to get again the start until there is no connection left
         */
        connection = g_active_connection_list;
    }
}


bool ConnectionWithSameConfigPointExists( EipUint32 config_point )
{
    CipConn* connection = g_active_connection_list;

    while( connection )
    {
        if( config_point == connection->conn_path.connection_point[2] )
        {
            break;
        }

        connection = connection->next_cip_conn;
    }

    return NULL != connection;
}


void InitializeIoConnectionData()
{
    memset( g_exlusive_owner_connections, 0, sizeof g_exlusive_owner_connections );

    memset( g_input_only_connections, 0, sizeof g_input_only_connections );

    memset( g_listen_only_connections, 0, sizeof g_listen_only_connections );
}
