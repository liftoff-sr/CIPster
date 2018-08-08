/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#include <vector>
#include <string.h>

#include "appcontype.h"
#include <cipster_api.h>            // CheckIoConnectionEvent()

/**
 * Class ExclusiveOwner
 * is contains a static allocation pool for exclusive owner connections.  Each
 * instance is a registration of an expectation.
 * Each ExclusiveOnwer in the collection
 * is first created by calling ExclusiveOwner::AddExpectation()
 * [via ConfigureExclusiveOwnerConnectionPoint()].
 * Then later a call may be made to GetConnection() to match an inbound
 * connection request with the expectation.  If a match is found, then
 * this expectation matching connnection is returned.
 */
class ExclusiveOwner
{
public:
    ExclusiveOwner( int aOutputAssembly=0, int aInputAssembly=0, int aConfigAssembly=0 ) :
        output_assembly( aOutputAssembly ),
        input_assembly( aInputAssembly ),
        config_assembly( aConfigAssembly )
    {}

    /**
     * Function GetConnection
     * matches an inbound connection request with the set of previously
     * AddExpection() registered expectations.  If a match is found, then
     * this expectation matching connnection is returned.
     */
    static CipConn* GetConnection( ConnectionData* aConn, ConnMgrStatus* extended_error );

    /**
     * Function AddExpectation
     * registers an expectation of an incoming forward_open connection request of
     * the exclusive owner type.
     *
     * @param config_assembly is the assembly instance to later match.
     *  If -1 then the connection request may omit the config path.
     */
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
    int     output_assembly;    ///< the O-to-T point for the connection
    int     input_assembly;     ///< the T-to-O point for the connection
    int     config_assembly;    ///< the config point for the connection
    CipConn connection;         ///< the connection data, only one connection is allowed per O-to-T point

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
            if( kConnStateNonExistent == connection[j].State() )
            {
                connection[j].SetState( kConnStateConfiguring );
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

    static CipConn* GetConnection( ConnectionData* aConn, ConnMgrStatus* extended_error );

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
            if( kConnStateNonExistent == connection[j].State() )
            {
                connection[j].SetState( kConnStateConfiguring );
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

    static CipConn* GetConnection( ConnectionData* aConn, ConnMgrStatus* extended_error );

    typedef std::vector<ListenOnlyConnSet>::iterator  iterator;

private:
    int     output_assembly;        ///< the O-to-T point for the connection
    int     input_assembly;         ///< the T-to-O point for the connection
    int     config_assembly;        ///< the config point for the connection

    CipConn connection[CIPSTER_CIP_NUM_LISTEN_ONLY_CONNS_PER_CON_PATH];    ///< the connection data

    static std::vector<ListenOnlyConnSet>       s_listen_only;
};


std::vector<ExclusiveOwner>         ExclusiveOwner::s_exclusive_owner;
std::vector<InputOnlyConnSet>       InputOnlyConnSet::s_input_only;
std::vector<ListenOnlyConnSet>      ListenOnlyConnSet::s_listen_only;


CipConn* ExclusiveOwner::GetConnection( ConnectionData* aConn, ConnMgrStatus* extended_error )
{
    for( ExclusiveOwner::iterator it = s_exclusive_owner.begin();  it != s_exclusive_owner.end();  ++it )
    {
        if( it->output_assembly == aConn->conn_path.consuming_path.GetInstanceOrConnPt()
         && it->input_assembly  == aConn->conn_path.producing_path.GetInstanceOrConnPt()
         && ( it->config_assembly == aConn->conn_path.config_path.GetInstanceOrConnPt() ||
            ( it->config_assembly == -1 && !aConn->conn_path.config_path.HasAny() ) )
          )
        {
            // check if another connection point with the same output assembly is currently connected
            if( GetConnectedOutputAssembly( aConn->conn_path.consuming_path.GetInstanceOrConnPt() ) )
            {
                *extended_error = kConnMgrStatusErrorOwnershipConflict;
                break;
            }

            return &it->connection;
        }
    }

    return NULL;
}


CipConn* InputOnlyConnSet::GetConnection( ConnectionData* aConn, ConnMgrStatus* extended_error )
{
    for( InputOnlyConnSet::iterator it = s_input_only.begin();  it != s_input_only.end();  ++it )
    {
        // we have the same output assembly?
        if( it->output_assembly == aConn->conn_path.consuming_path.GetInstanceOrConnPt() )
        {
            if( it->input_assembly != aConn->conn_path.producing_path.GetInstanceOrConnPt() )
            {
                *extended_error = kConnMgrStatusInvalidProducingApplicationPath;
                break;
            }

            if( it->config_assembly != aConn->conn_path.config_path.GetInstanceOrConnPt() )
            {
                *extended_error = kConnMgrStatusInconsistentApplicationPathCombo;
                break;
            }

            CipConn* in = it->Alloc();

            if( in )
                return in;

            *extended_error = kConnMgrStatusTargetObjectOutOfConnections;
            break;
        }
    }

    return NULL;
}


CipConn* ListenOnlyConnSet::GetConnection( ConnectionData* aConn, ConnMgrStatus* extended_error )
{
    if( aConn->t_to_o_ncp.ConnectionType() != kIOConnTypeMulticast )
    {
        // a listen only connection has to be a multicast connection.
        *extended_error = kConnMgrStatusNonListenOnlyConnectionNotOpened;

        return NULL;
    }

    for( ListenOnlyConnSet::iterator it = s_listen_only.begin();  it != s_listen_only.end(); ++it )
    {
                // we have the same output assembly?
        if( it->output_assembly == aConn->conn_path.consuming_path.GetInstanceOrConnPt() )
        {
            if( it->input_assembly != aConn->conn_path.producing_path.GetInstanceOrConnPt() )
            {
                *extended_error = kConnMgrStatusInvalidProducingApplicationPath;
                break;
            }

            if( it->config_assembly != aConn->conn_path.config_path.GetInstanceOrConnPt() )
            {
                *extended_error = kConnMgrStatusInconsistentApplicationPathCombo;
                break;
            }

            if( NULL == GetExistingProducerMulticastConnection( aConn->conn_path.producing_path.GetInstanceOrConnPt() ) )
            {
                *extended_error = kConnMgrStatusNonListenOnlyConnectionNotOpened;
                break;
            }

            CipConn* listener = it->Alloc();

            if( listener )
                return listener;

            *extended_error = kConnMgrStatusTargetObjectOutOfConnections;
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


CipConn* GetIoConnectionForConnectionData( ConnectionData* aConn,  ConnMgrStatus* extended_error )
{
    ConnInstanceType    conn_type;

    *extended_error = kConnMgrStatusSuccess;

    CipConn* io_connection = ExclusiveOwner::GetConnection( aConn, extended_error );

    if( !io_connection )
    {
        if( kConnMgrStatusSuccess == *extended_error )
        {
            // we found no connection and don't have an error so try input only next
            io_connection = InputOnlyConnSet::GetConnection( aConn, extended_error );

            if( !io_connection )
            {
                if( kConnMgrStatusSuccess == *extended_error )
                {
                    // we found no connection and don't have an error so try listen only next
                    io_connection = ListenOnlyConnSet::GetConnection( aConn, extended_error );

                    if( !io_connection )
                    {
                        if( kConnMgrStatusSuccess == *extended_error )
                        {
                            // no application connection type was found that suits the given data
                            // TODO check error code VS
                            *extended_error = kConnMgrStatusInconsistentApplicationPathCombo;
                        }
                    }
                    else
                    {
                        conn_type = kConnInstanceTypeIoListenOnly;
                    }
                }
            }
            else
            {
                conn_type = kConnInstanceTypeIoInputOnly;
            }
        }
    }
    else
    {
        conn_type = kConnInstanceTypeIoExclusiveOwner;
    }

    if( io_connection )
    {
        // was CopyConnectionData( io_connection, aConn );
        *io_connection = *aConn;

        io_connection->SetInstanceType( conn_type );
    }

    return io_connection;
}


CipConn* GetExistingProducerMulticastConnection( int input_point )
{
    CipConnBox::iterator producer_multicast_connection = g_active_conns.begin();

    while( producer_multicast_connection != g_active_conns.end() )
    {
        if( producer_multicast_connection->InstanceType() == kConnInstanceTypeIoExclusiveOwner
         || producer_multicast_connection->InstanceType() == kConnInstanceTypeIoInputOnly )
        {
            if( input_point == producer_multicast_connection->conn_path.producing_path.GetInstanceOrConnPt()
                && producer_multicast_connection->t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast
                && kSocketInvalid != producer_multicast_connection->ProducingSocket() )
            {
                // we have a connection that produces the same input assembly,
                // is a multicast producer and manages the connection.
                break;
            }
        }

        ++producer_multicast_connection;
    }

    return producer_multicast_connection;
}


CipConn* GetNextNonControlMasterConnection( int input_point )
{
    CipConnBox::iterator c = g_active_conns.begin();

    for( ;  c != g_active_conns.end();  ++c )
    {
        if( c->InstanceType() == kConnInstanceTypeIoExclusiveOwner
         || c->InstanceType() == kConnInstanceTypeIoInputOnly )
        {
            if( input_point == c->conn_path.producing_path.GetInstanceOrConnPt()
             && c->t_to_o_ncp.ConnectionType() == kIOConnTypeMulticast
             && c->ProducingSocket() == kSocketInvalid )
            {
                // we have a connection that produces the same input assembly,
                // is a multicast producer and does not manages the connection.
                return c;
            }
        }
    }

    return NULL;
}


void CloseAllConnectionsForInputWithSameType(
        int input_point,  ConnInstanceType instance_type )
{
    CipConnBox::iterator c = g_active_conns.begin();

    while( c != g_active_conns.end() )
    {
        if( instance_type == c->InstanceType() &&
            input_point   == c->conn_path.producing_path.GetInstanceOrConnPt() )
        {
            CheckIoConnectionEvent(
                    c->conn_path.consuming_path.GetInstanceOrConnPt(),
                    c->conn_path.producing_path.GetInstanceOrConnPt(),
                    kIoConnectionEventClosed );

            CipConn* to_close = c;

            ++c;

            to_close->Close();
        }
        else
        {
            ++c;
        }
    }
}


void CloseAllConnections()
{
    while( g_active_conns.begin() != g_active_conns.end() )
    {
        // Close() removes the connection from the list.
        g_active_conns.begin()->Close();
    }
}


bool ConnectionWithSameConfigPointExists( int config_point )
{
    CipConnBox::iterator c = g_active_conns.begin();

    for( ; c != g_active_conns.end(); ++c )
    {
        if( config_point == c->conn_path.config_path.GetInstanceOrConnPt() )
        {
            break;
        }
    }

    return c;
}
