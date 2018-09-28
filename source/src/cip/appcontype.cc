/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#include <vector>
#include <string.h>

#include "appcontype.h"
#include <cipster_api.h>            // NotifyIoConnectionEvent()

/**
 * Class ExclusiveOwner
 * contains a static allocation pool for exclusive owner connections.  Each
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
    static CipConn* GetConnection( ConnectionData* aConnData, ConnMgrStatus* aExtError );

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
        for( int j = 0; j < DIM( connection );  ++j )
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

    static CipConn* GetConnection( ConnectionData* aConnData, ConnMgrStatus* aExtError );

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
        for( int j = 0; j < DIM( connection );  ++j )
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

    static CipConn* GetConnection( ConnectionData* aConnData, ConnMgrStatus* aExtError );

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


CipConn* ExclusiveOwner::GetConnection( ConnectionData* aConnData, ConnMgrStatus* aExtError )
{
    for( ExclusiveOwner::iterator it = s_exclusive_owner.begin();  it != s_exclusive_owner.end();  ++it )
    {
        if( it->output_assembly == aConnData->ConsumingPath().GetInstanceOrConnPt()
         && it->input_assembly  == aConnData->ProducingPath().GetInstanceOrConnPt()
         && ( it->config_assembly == aConnData->ConfigPath().GetInstanceOrConnPt() ||
            ( it->config_assembly == -1 && !aConnData->ConfigPath().HasAny() ) )
          )
        {
            // check if another connection point with the same output assembly is currently connected
            if( GetConnectedOutputAssembly( aConnData->ConsumingPath().GetInstanceOrConnPt() ) )
            {
                CIPSTER_TRACE_INFO( "%s:ERROR. Matching consuming assembly id:%d\n",
                    __func__, aConnData->ConsumingPath().GetInstanceOrConnPt() );

                *aExtError = kConnMgrStatusOwnershipConflict;
                break;
            }

            return &it->connection;
        }
    }

    return NULL;
}


CipConn* InputOnlyConnSet::GetConnection( ConnectionData* aConnData, ConnMgrStatus* aExtError )
{
    for( InputOnlyConnSet::iterator it = s_input_only.begin();  it != s_input_only.end();  ++it )
    {
        // we have the same output assembly?
        if( it->output_assembly == aConnData->ConsumingPath().GetInstanceOrConnPt() )
        {
            if( it->input_assembly != aConnData->ProducingPath().GetInstanceOrConnPt() )
            {
                *aExtError = kConnMgrStatusInvalidProducingApplicationPath;
                break;
            }

            if( it->config_assembly != aConnData->ConfigPath().GetInstanceOrConnPt() )
            {
                *aExtError = kConnMgrStatusInconsistentApplicationPathCombo;
                break;
            }

            CipConn* in = it->Alloc();

            if( in )
                return in;

            *aExtError = kConnMgrStatusTargetObjectOutOfConnections;
            break;
        }
    }

    return NULL;
}


CipConn* ListenOnlyConnSet::GetConnection( ConnectionData* aConnData, ConnMgrStatus* aExtError )
{
    if( aConnData->ProducingNCP().ConnectionType() != kIOConnTypeMulticast )
    {
        // a listen only connection has to be a multicast connection.
        *aExtError = kConnMgrStatusNonListenOnlyConnectionNotOpened;

        return NULL;
    }

    for( ListenOnlyConnSet::iterator it = s_listen_only.begin();  it != s_listen_only.end(); ++it )
    {
                // we have the same output assembly?
        if( it->output_assembly == aConnData->ConsumingPath().GetInstanceOrConnPt() )
        {
            if( it->input_assembly != aConnData->ProducingPath().GetInstanceOrConnPt() )
            {
                *aExtError = kConnMgrStatusInvalidProducingApplicationPath;
                break;
            }

            if( it->config_assembly != aConnData->ConfigPath().GetInstanceOrConnPt() )
            {
                *aExtError = kConnMgrStatusInconsistentApplicationPathCombo;
                break;
            }

            if( NULL == GetExistingProducerMulticastConnection( aConnData->ProducingPath().GetInstanceOrConnPt() ) )
            {
                *aExtError = kConnMgrStatusNonListenOnlyConnectionNotOpened;
                break;
            }

            CipConn* listener = it->Alloc();

            if( listener )
                return listener;

            *aExtError = kConnMgrStatusTargetObjectOutOfConnections;
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


CipConn* GetIoConnectionForConnectionData( ConnectionData* aConnData, ConnMgrStatus* aExtError )
{
    ConnInstanceType    conn_type;

    *aExtError = kConnMgrStatusSuccess;

    CipConn* io_connection = ExclusiveOwner::GetConnection( aConnData, aExtError );

    if( !io_connection )
    {
        if( kConnMgrStatusSuccess == *aExtError )
        {
            // we found no connection and don't have an error so try input only next
            io_connection = InputOnlyConnSet::GetConnection( aConnData, aExtError );

            if( !io_connection )
            {
                if( kConnMgrStatusSuccess == *aExtError )
                {
                    // we found no connection and don't have an error so try listen only next
                    io_connection = ListenOnlyConnSet::GetConnection( aConnData, aExtError );

                    if( !io_connection )
                    {
                        if( kConnMgrStatusSuccess == *aExtError )
                        {
                            // no application connection type was found that suits the given data
                            *aExtError = kConnMgrStatusInconsistentApplicationPathCombo;
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
            if( input_point == producer_multicast_connection->ProducingPath().GetInstanceOrConnPt()
                && producer_multicast_connection->ProducingNCP().ConnectionType() == kIOConnTypeMulticast
                && producer_multicast_connection->ProducingUdp() )
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
            if( input_point == c->ProducingPath().GetInstanceOrConnPt()
             && c->ProducingNCP().ConnectionType() == kIOConnTypeMulticast
             && !c->ProducingUdp() )
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
            input_point   == c->ProducingPath().GetInstanceOrConnPt() )
        {
            NotifyIoConnectionEvent( c, kIoConnectionEventClosed );

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
        if( config_point == c->ConfigPath().GetInstanceOrConnPt() )
        {
            break;
        }
    }

    return c;
}
