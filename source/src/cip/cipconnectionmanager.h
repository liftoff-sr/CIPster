/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corportion.
 *
 ******************************************************************************/
#ifndef CIPSTER_CIPCONNECTIONMANAGER_H_
#define CIPSTER_CIPCONNECTIONMANAGER_H_

#include <cipster_user_conf.h>
#include <typedefs.h>
#include "ciptypes.h"
#include "cipmessagerouter.h"
#include "cipconnection.h"


class CipConnMgrClass : public CipClass
{
public:
    CipConnMgrClass();

    static EipStatus ManageConnections();

    /**
     * Function HandleReceivedConnectedData
     * notifies the connection manager that data for a connection has been
     * received.
     *
     * This function should be invoked by the network layer.
     * @param from_address address from which the data has been received. Only
     *           data from the connections originator may be accepted. Avoids
     *           connection hijacking
     * @param aCommand received data buffer pointing just past the
     *   encapsulation header and a byte count remaining in frame.
     * @param aReply where to put the reply and tells its maximum length.
     * @return EipStatus
     */
    static EipStatus HandleReceivedConnectedData(
        const sockaddr_in* from_address, BufReader aCommand );

    //-----<CipServiceFunctions>------------------------------------------------
    static EipStatus forward_open_service( CipInstance* instance,
            CipMessageRouterRequest* request, CipMessageRouterResponse* response );

    static EipStatus large_forward_open_service( CipInstance* instance,
            CipMessageRouterRequest* request, CipMessageRouterResponse* response );

    static EipStatus forward_close_service( CipInstance* instance,
            CipMessageRouterRequest* request,
            CipMessageRouterResponse* response );

    //-----</CipServiceFunctions>-----------------------------------------------

protected:

    /**
     * Function forward_open_common
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
    static EipStatus forward_open_common( CipInstance* instance,
            CipMessageRouterRequest* request,
            CipMessageRouterResponse* response, bool isLarge );

    /**
     * Function assembleForwardOpenResponse
     * serializes a response to a forward_open
     */
    static void assembleForwardOpenResponse( ConnectionData* aParams,
            CipMessageRouterResponse* response, CipError general_status,
            ConnectionManagerStatusCode extended_status );
};


/**
 * Macros for comparing 32 bit sequence numbers according to CIP spec Vol2 3-4.2.
 * @define SEQ_LEQ32(a, b) Checks if sequence number a is less or equal than b
 * @define SEQ_GEQ32(a, b) Checks if sequence number a is greater or equal than
 * @define SEQ_GT32(a, b) Checks if sequence number a is greater than b
 */
#define SEQ_LEQ32( a, b )   ( (int) ( (a) - (b) ) <= 0 )
#define SEQ_GEQ32( a, b )   ( (int) ( (a) - (b) ) >= 0 )
#define SEQ_GT32( a, b )    ( (int) ( (a) - (b) ) > 0 )


/**
 * Macros for comparing 16 bit sequence numbers.
 * @define SEQ_LEQ16(a, b) Checks if sequence number a is less or equal than b
 * @define SEQ_GEQ16(a, b) Checks if sequence number a is greater or equal than
 */
#define SEQ_LEQ16( a, b )   ( (short) ( (a) - (b) ) <= 0 )
#define SEQ_GEQ16( a, b )   ( (short) ( (a) - (b) ) >= 0 )


/** @brief Initialize the data of the connection manager object
 */
EipStatus ConnectionManagerInit();

/**
 * Function GetConnectionByConsumingId
 * returns a connection that has a matching consuming_connection id.
 *
 *   @param aConnectionId is a consuming connection id to find.
 *   @return CipConn* - the opened connection instance.
 *           NULL .. open connection not present
 */
CipConn* GetConnectionByConsumingId( int aConnectionId );

/**
 * Function GetConnectionByProducingId
 * returns a connection that has a matching producing_connection id.
 *
 *   @param aConnectionId is a producing connection id to find.
 *   @return CipConn* - the opened connection instance.
 *           NULL .. open connection not present
 */
CipConn* GetConnectionByProducingId( int aConnectionId );

/**  Get a connection object for a given output assembly.
 *
 *   @param output_assembly_id requested output assembly of requested
 * connection
 *   @return pointer to connected Object
 *           0 .. connection not present in device
 */
CipConn* GetConnectedOutputAssembly( int output_assembly_id );


bool IsConnectedInputAssembly( int aInstanceId );

// TODO: Missing documentation
bool IsConnectedOutputAssembly( int aInstanceId );

/**
 * Class CipConnBox
 * is a containter for CipConns, likely to be replace with std::vector someday.
 * Used to hold an active list of CipConns, using CipConn->prev and ->next.
 */
class CipConnBox
{
public:

    CipConnBox() :
        head( NULL )
    {}

    class iterator
    {
    public:
        iterator( CipConn* aConn ) : p( aConn ) {}

        iterator& operator ++()
        {
            p = p->next;
            return *this;
        }

        iterator operator ++( int ) // post-increment and return initial position
        {
            iterator ret( p );
            p = p->next;
            return ret;
        }

        bool operator == ( const iterator& other ) const
        {
            return p == other.p;
        }

        bool operator != ( const iterator& other ) const
        {
            return p != other.p;
        }

        CipConn* operator->() const     { return p; }
        CipConn* operator*() const      { return p; }
        operator CipConn* () const      { return p; }

    private:
        CipConn*    p;
    };

    /**
     * Function Insert
     * inserts the given connection object to this list.
     *
     * By adding a connection to the active connection list the connection manager
     * will perform the supervision and handle the timing (e.g., timeout,
     * production inhibit, etc).
     *
     * @param aConn the connection to be added at the beginning.
     */
    void Insert( CipConn* aConn );

    void Remove( CipConn* aConn );

    iterator end()      const   { return iterator( NULL ); }
    iterator begin()    const   { return iterator( head ); }

protected:
    CipConn* head;
};

extern CipConnBox g_active_conns;

#endif // CIPSTER_CIPCONNECTIONMANAGER_H_
