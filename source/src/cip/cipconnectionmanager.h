/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corporation.
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

    CipInstance* CreateInstance( int aInstanceId );

    static EipStatus ManageConnections();

    /**
     * Function CloseClass3Connections
     * closes all class 3 connections having @a aSessionHandle.
     * Since multiple CIP Class 3 connections can be instantiated all on the
     * same TCP connection, we could be deleting more than one here.
     *
     * @param aSessionHandle is a session handle to match against.
     */
    static void CloseClass3Connections( CipUdint aSessionHandle );

    static void CheckForTimedOutConnectionsAndCloseTCPConnections( CipUdint aSessionHandle );

    /**
     * Function FindExsitingMatchingConnection
     * finds an existing matching established connection.
     *
     * The comparison is done according to the definitions in the CIP
     * specification Section 3-5.5.2: The following elements have to be equal:
     * Vendor ID, Connection Serial Number, Originator Serial Number
     *
     * @param aConn connection instance containing the comparison elements from
     *   the forward open request
     *
     * @return CipConn*
     *    - NULL if no equal established connection exists
     *    - pointer to the equal connection object
     */
    static CipConn* FindExistingMatchingConnection( const ConnectionData& params );

    /**
     * Function RecvConnectedData
     * notifies the connection manager that data for a connection has been
     * received.
     *
     * This function is called by the networkhandler.cc code when a consumable
     * UDP frame has arrived on a socket known to be in use for an i/o connection.
     *
     * @param aSocket is the socket that the connected data arrived on.
     * @param aFromAddress address from which the data has been received. Only
     *           data from the connections originator may be accepted. Avoids
     *           connection hijacking
     * @param aCommand received data buffer pointing just past the
     *   encapsulation header and a byte count remaining in frame.
     * @param aReply where to put the reply and tells its maximum length.
     * @return EipStatus
     */
    static EipStatus RecvConnectedData( UdpSocket* aSocket,
        const SockAddr& aFromAddress, BufReader aCommand );

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
     * Function forward_open
     * is a client of both forward_open_service() and large_forward_open_service()
     * and checks if resources for new connection are available, and
     * generates a forward_open reply message.
     *
     * @param aInstance CIP object instance
     * @param aRequest CipMessageRouterRequest.
     * @param aResponse CipMessageRouterResponse.
     * @param isLarge is true when called from largeForwardOpen(), false when called from forwardOpen()
     *  and the distinction is whether to expect 32 or 16 bits of "network connection parameters".
     *
     * @return EipStatus
     *     -  >0 .. success, 0 .. no reply to send back
     *     -  -1 .. error
     */
    static EipStatus forward_open( CipInstance* aInstance,
            CipMessageRouterRequest* aRequest,
            CipMessageRouterResponse* aResponse, bool isLarge );

    /**
     * Function assembleForwardOpenResponse
     * serializes a response to a forward_open
    static void assembleForwardOpenResponse( ConnectionData* aParams,
            CipMessageRouterResponse* response, CipError general_status,
            ConnMgrStatus extended_status );
     */
};


/**
 * Macros for comparing 32 bit sequence numbers according to CIP spec Vol2 3-4.2.
 * @def SEQ_LEQ32(a, b) Checks if sequence number a is less or equal than b
 * @def SEQ_GEQ32(a, b) Checks if sequence number a is greater or equal than
 * @def SEQ_GT32(a, b) Checks if sequence number a is greater than b
 */
#define SEQ_LEQ32( a, b )   ( (int) ( (a) - (b) ) <= 0 )
#define SEQ_GEQ32( a, b )   ( (int) ( (a) - (b) ) >= 0 )
#define SEQ_GT32( a, b )    ( (int) ( (a) - (b) ) > 0 )


/**
 * Macros for comparing 16 bit sequence numbers.
 * @def SEQ_LEQ16(a, b) Checks if sequence number a is less or equal than b
 * @def SEQ_GEQ16(a, b) Checks if sequence number a is greater or equal than
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
 * is a containter for CipConns (likely to be replace with std::vector some day).
 * Used to hold an active list of CipConns, using CipConn->prev and ->next.
 */
class CipConnBox
{
public:

    CipConnBox() :
        head( NULL )
    {}

    /// Class CipConnBox::iterator walks the linked list and mimics a pointer
    /// when the dereferencing operators and cast are used.
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
        CipConn& operator*()  const     { return *p; }
        operator CipConn* ()  const     { return p; }

    private:
        CipConn*    p;
    };

    /**
     * Function Insert
     * inserts the given connection object into this container.
     *
     * By adding a connection to the active connection list the connection manager
     * will perform the supervision and handle the timing (e.g., timeout,
     * production inhibit, etc).
     *
     * @param aConn the connection to be added at the beginning.
     * @return bool - true if it was successfully inserted, else false because
     *  aConn was already on the list.
     */
    bool Insert( CipConn* aConn );

    /**
     * Function Remove
     * @return bool - true if it was successfully removed, else false because
     *  aConn was not previously on the list.
     */
    bool Remove( CipConn* aConn );

    iterator end()      const   { return iterator( NULL ); }
    iterator begin()    const   { return iterator( head ); }

protected:
    CipConn* head;
};

extern CipConnBox g_active_conns;

#endif // CIPSTER_CIPCONNECTIONMANAGER_H_
