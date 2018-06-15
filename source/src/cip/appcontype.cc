/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 *
 ******************************************************************************/

#include <vector>
#include <string.h>

#include "appcontype.h"
#include <cipster_api.h>            // CheckIoConnectionEvent()

/**
 * Class ExclusiveOwner
 * manages exclusive owner connections.  Each ExclusiveOnwer in the collection
 * is first created by calling ExclusiveOwner::AddExpectation()
 * [via ConfigureExclusiveOwnerConnectionPoint()].
 * Then later a call may be made to GetConnection() to match an inbound
 * connection request with the expectation.  If a match is found, then
 * this->connnection is marked as in use.
 */
class ExclusiveOwner
{
public:
    ExclusiveOwner( int aOutputAssembly=0, int aInputAssembly=0, int aConfigAssembly=0 ) :
        output_assembly( aOutputAssembly ),
        input_assembly( aInputAssembly ),
        config_assembly( aConfigAssembly )
    {}

    static CipConn* GetConnection( CipConn* aConn, ConnectionManagerStatusCode* extended_error );

    static bool AddExpectation( int output_assembly, int input_assembly, int config_assembly )
    {
        if( s_exclusive_owner.size() < CIPSTER_CIP_NUM_EXCLUSIVE_OWNER_CONNS )
        {
            s_exclusive_owner.push_back(
                ExclusiveOwner( output_assembly, input_assembly, config_assembly ) );
            return true;
        }

        return false;
    }

    static void Clear()    { s_exclusive_owner.clear(); }

    typedef std::vector<ExclusiveOwner>::iterator     iterator;

private:
    int output_assembly;        ///< the O-to-T point for the connection
    int input_assembly;         ///< the T-to-O point for the connection
    int config_assembly;        ///< the config point for the connection
    CipConn connection;    ///< the connection data, only one connection is allowed per O-to-T point

    static std::vector<ExclusiveOwner>  s_exclusive_owner;
};


class InputOnlyConnSet
{
public:
    InputOnlyConnSet( int aOutputAssembly = 0, int aInputAssembly=0, int aConfigAssembly=0 ) :
        output_assembly( aOutputAssembly ),
        input_assembly( aInputAssembly ),
        config_assembly( aConfigAssembly )
    {}

    CipConn* Alloc()
    {
        for( int j = 0; j < CIPSTER_CIP_NUM_LISTEN_ONLY_CONNS_PER_CON_PATH; j++ )
        {
            if( kConnectionStateNonExistent == connection[j].state )
            {
                connection[j].state = kConnectionStateConfiguring;
                return &connection[j];
            }
        }

        return NULL;
    }

    static bool AddExpectation( int output_assembly, int input_assembly, int config_assembly )
    {
        if( s_input_only.size() < CIPSTER_CIP_NUM_INPUT_ONLY_CONNS )
        {
            s_input_only.push_back(
                    InputOnlyConnSet( output_assembly, input_assembly, config_assembly ) );
            return true;
        }

        return false;
    }

    static CipConn* GetConnection( CipConn* aConn, ConnectionManagerStatusCode* extended_error );

    static void Clear()     { s_input_only.clear(); }

    typedef std::vector<InputOnlyConnSet>::iterator     iterator;

private:
    int output_assembly;        ///< the O-to-T point for the connection
    int input_assembly;         ///< the T-to-O point for the connection
    int config_assembly;        ///< the config point for the connection

    CipConn connection[CIPSTER_CIP_NUM_INPUT_ONLY_CONNS_PER_CON_PATH]; ///< the connection data

    static std::vector<InputOnlyConnSet>    s_input_only;
};


/**
 * Class ListenOnlyConnSet
 * manages listen only connections by starting with an expectation of an inbound
 * connection request via a forward_open request, and then attaches one or more
 * actual ListenOnly connections to the corresponding expectation.
 */
class ListenOnlyConnSet
{
public:
    ListenOnlyConnSet( int aOutputAssembly=0, int aInputAssembly=0, int aConfigAssembly=0 ) :
        output_assembly( aOutputAssembly ),
        input_assembly( aInputAssembly ),
        config_assembly( aConfigAssembly )
    {}

    CipConn* Alloc()
    {
        for( int j = 0; j < CIPSTER_CIP_NUM_LISTEN_ONLY_CONNS_PER_CON_PATH; j++ )
        {
            if( kConnectionStateNonExistent == connection[j].state )
            {
                connection[j].state = kConnectionStateConfiguring;
                return &connection[j];
            }
        }
        return NULL;
    }

    static bool AddExpectation( int output_assembly, int input_assembly, int config_assembly )
    {
        if( s_listen_only.size() < CIPSTER_CIP_NUM_LISTEN_ONLY_CONNS )
        {
            s_listen_only.push_back(
                ListenOnlyConnSet( output_assembly, input_assembly, config_assembly ) );
            return true;
        }

        return false;
    }

    static void Clear()     { s_listen_only.clear(); }

    static CipConn* GetConnection( CipConn* aConn, ConnectionManagerStatusCode* extended_error );

    typedef std::vector<ListenOnlyConnSet>::iterator  iterator;

private:
    int output_assembly;        ///< the O-to-T point for the connection
    int input_assembly;         ///< the T-to-O point for the connection
    int config_assembly;        ///< the config point for the connection
    CipConn connection[CIPSTER_CIP_NUM_LISTEN_ONLY_CONNS_PER_CON_PATH];    ///< the connection data

    static std::vector<ListenOnlyConnSet>       s_listen_only;
};


std::vector<ExclusiveOwner>         ExclusiveOwner::s_exclusive_owner;
std::vector<InputOnlyConnSet>       InputOnlyConnSet::s_input_only;
std::vector<ListenOnlyConnSet>      ListenOnlyConnSet::s_listen_only;


CipConn* ExclusiveOwner::GetConnection( CipConn* aConn, ConnectionManagerStatusCode* extended_error )
{
    for( ExclusiveOwner::iterator it = s_exclusive_owner.begin();  it != s_exclusive_owner.end();  ++it )
    {
        if( it->output_assembly == aConn->conn_path.consuming_path.GetInstanceOrConnPt()
         && it->input_assembly  == aConn->conn_path.producing_path.GetInstanceOrConnPt()
         && ( it->config_assembly == aConn->conn_path.config_path.GetInstanceOrConnPt() ||
            ( it->config_assembly == -1 && !aConn->conn_path.config_path.HasAny() ) )
          )
        {
            // check if on other connection point with the same output assembly is currently connected
            if( GetConnectedOutputAssembly( aConn->conn_path.consuming_path.GetInstanceOrConnPt() ) )
            {
                *extended_error = kConnectionManagerStatusCodeErrorOwnershipConflict;
                break;
            }

            return &it->connection;
        }
    }

    return NULL;
}


CipConn* InputOnlyConnSet::GetConnection( CipConn* aConn, ConnectionManagerStatusCode* extended_error )
{
    for( InputOnlyConnSet::iterator it = s_input_only.begin();  it != s_input_only.end();  ++it )
    {
        // we have the same output assembly?
        if( it->output_assembly == aConn->conn_path.consuming_path.GetInstanceOrConnPt() )
        {
            if( it->input_assembly != aConn->conn_path.producing_path.GetInstanceOrConnPt() )
            {
                *extended_error = kConnectionManagerStatusCodeInvalidProducingApplicationPath;
                break;
            }

            if( it->config_assembly != aConn->conn_path.config_path.GetInstanceOrConnPt() )
            {
                *extended_error = kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
                break;
            }

            CipConn* in = it->Alloc();

            if( in )
                return in;

            *extended_error = kConnectionManagerStatusCodeTargetObjectOutOfConnections;
            break;
        }
    }

    return NULL;
}


CipConn* ListenOnlyConnSet::GetConnection( CipConn* aConn, ConnectionManagerStatusCode* extended_error )
{
    if( aConn->t_to_o_ncp.ConnectionType() != kIOConnTypeMulticast )
    {
        // a listen only connection has to be a multicast connection.
        *extended_error = kConnectionManagerStatusCodeNonListenOnlyConnectionNotOpened;

        // maybe not the best error message however there is no suitable definition in the cip spec
        return NULL;
    }

    for( ListenOnlyConnSet::iterator it = s_listen_only.begin();  it != s_listen_only.end(); ++it )
    {
                // we have the same output assembly?
        if( it->output_assembly == aConn->conn_path.consuming_path.GetInstanceOrConnPt() )
        {
            if( it->input_assembly != aConn->conn_path.producing_path.GetInstanceOrConnPt() )
            {
                *extended_error = kConnectionManagerStatusCodeInvalidProducingApplicationPath;
                break;
            }

            if( it->config_assembly != aConn->conn_path.config_path.GetInstanceOrConnPt() )
            {
                *extended_error = kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
                break;
            }

            if( NULL == GetExistingProducerMulticastConnection( aConn->conn_path.producing_path.GetInstanceOrConnPt() ) )
            {
                *extended_error = kConnectionManagerStatusCodeNonListenOnlyConnectionNotOpened;
                break;
            }

            CipConn* listener = it->Alloc();

            if( listener )
                return listener;

            *extended_error = kConnectionManagerStatusCodeTargetObjectOutOfConnections;
            break;
        }
    }

    return NULL;
}


bool ConfigureExclusiveOwnerConnectionPoint(
        int output_assembly,
        int input_assembly,
        int config_assembly )
{
    return ExclusiveOwner::AddExpectation( output_assembly, input_assembly, config_assembly );
}


bool ConfigureInputOnlyConnectionPoint(
        int output_assembly,
        int input_assembly,
        int config_assembly )
{
    return InputOnlyConnSet::AddExpectation( output_assembly, input_assembly, config_assembly );
}


bool ConfigureListenOnlyConnectionPoint(
        int output_assembly,
        int input_assembly,
        int config_assembly )
{
    return ListenOnlyConnSet::AddExpectation( output_assembly, input_assembly, config_assembly );
}


CipConn* GetIoConnectionForConnectionData( CipConn* aConn,  ConnectionManagerStatusCode* extended_error )
{
    *extended_error = kConnectionManagerStatusCodeSuccess;

    CipConn* io_connection = ExclusiveOwner::GetConnection( aConn, extended_error );

    if( !io_connection )
    {
        if( kConnectionManagerStatusCodeSuccess == *extended_error )
        {
            // we found no connection and don't have an error so try input only next
            io_connection = InputOnlyConnSet::GetConnection( aConn, extended_error );

            if( !io_connection )
            {
                if( 0 == *extended_error )
                {
                    // we found no connection and don't have an error so try listen only next
                    io_connection = ListenOnlyConnSet::GetConnection( aConn, extended_error );

                    if( !io_connection &&  0 == *extended_error )
                    {
                        // no application connection type was found that suits the given data
                        // TODO check error code VS
                        *extended_error = kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
                    }
                    else
                    {
                        aConn->instance_type = kConnInstanceTypeIoListenOnly;
                    }
                }
            }
            else
            {
                aConn->instance_type = kConnInstanceTypeIoInputOnly;
            }
        }
    }
    else
    {
        aConn->instance_type = kConnInstanceTypeIoExclusiveOwner;
    }

    if( io_connection )
    {
        CopyConnectionData( io_connection, aConn );
    }

    return io_connection;
}


CipConn* GetExistingProducerMulticastConnection( EipUint32 input_point )
{
    CipConn* producer_multicast_connection = g_active_connection_list;

    while( producer_multicast_connection )
    {
        if( producer_multicast_connection->instance_type == kConnInstanceTypeIoExclusiveOwner
         || producer_multicast_connection->instance_type == kConnInstanceTypeIoInputOnly )
        {
            if( input_point == producer_multicast_connection->conn_path.producing_path.GetInstanceOrConnPt()
                && producer_multicast_connection->t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast
                && kEipInvalidSocket != producer_multicast_connection->producing_socket )
            {
                // we have a connection that produces the same input assembly,
                // is a multicast producer and manages the connection.
                break;
            }
        }

        producer_multicast_connection = producer_multicast_connection->next;
    }

    return producer_multicast_connection;
}


CipConn* GetNextNonControlMasterConnection( EipUint32 input_point )
{
    CipConn* next_non_control_master_connection = g_active_connection_list;

    while( next_non_control_master_connection )
    {
        if( next_non_control_master_connection->instance_type == kConnInstanceTypeIoExclusiveOwner
         || next_non_control_master_connection->instance_type == kConnInstanceTypeIoInputOnly )
        {
            if( input_point == next_non_control_master_connection->conn_path.producing_path.GetInstanceOrConnPt()
             && next_non_control_master_connection->t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast
             && next_non_control_master_connection->producing_socket == kEipInvalidSocket )
            {
                // we have a connection that produces the same input assembly,
                // is a multicast producer and does not manages the connection.
                break;
            }
        }

        next_non_control_master_connection = next_non_control_master_connection
                                             ->next;
    }

    return next_non_control_master_connection;
}


void CloseAllConnectionsForInputWithSameType( EipUint32 input_point,  ConnInstanceType instance_type )
{
    CipConn* connection = g_active_connection_list;
    CipConn* connection_to_delete;

    while( connection )
    {
        if( instance_type == connection->instance_type &&
            input_point   == connection->conn_path.producing_path.GetInstanceOrConnPt() )
        {
            connection_to_delete = connection;
            connection = connection->next;

            CheckIoConnectionEvent(
                    connection_to_delete->conn_path.consuming_path.GetInstanceOrConnPt(),
                    connection_to_delete->conn_path.producing_path.GetInstanceOrConnPt(),
                    kIoConnectionEventClosed );

            // FIXME check if this is ok
            connection_to_delete->connection_close_function( connection_to_delete );
            // closeConnection(pstToDelete); will remove the connection from the active connection list
        }
        else
        {
            connection = connection->next;
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

        // Close connection will remove the connection from the list therefore we
        // need to get again the start until there is no connection left
        connection = g_active_connection_list;
    }
}


bool ConnectionWithSameConfigPointExists( EipUint32 config_point )
{
    CipConn* connection = g_active_connection_list;

    while( connection )
    {
        if( config_point == connection->conn_path.config_path.GetInstanceOrConnPt() )
        {
            break;
        }

        connection = connection->next;
    }

    return NULL != connection;
}


void InitializeIoConnectionData()
{
    // now done by "static C++ construction"
}


void DestroyIoConnectionData()
{
    ExclusiveOwner::Clear();

    InputOnlyConnSet::Clear();

    ListenOnlyConnSet::Clear();
}
