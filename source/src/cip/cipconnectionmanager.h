/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#ifndef CIPSTER_CIPCONNECTIONMANAGER_H_
#define CIPSTER_CIPCONNECTIONMANAGER_H_

#include "opener_user_conf.h"
#include "opener_api.h"
#include "typedefs.h"
#include "ciptypes.h"
#include "cipmessagerouter.h"
#include "cipconnection.h"



/** @brief macros for comparing sequence numbers according to CIP spec vol
 * 2 3-4.2 for int type variables
 * @define SEQ_LEQ32(a, b) Checks if sequence number a is less or equal than b
 * @define SEQ_GEQ32(a, b) Checks if sequence number a is greater or equal than
 *  b
 *  @define SEQ_GT32(a, b) Checks if sequence number a is greater than b
 */
#define SEQ_LEQ32( a, b )   ( (int) ( (a) - (b) ) <= 0 )
#define SEQ_GEQ32( a, b )   ( (int) ( (a) - (b) ) >= 0 )
#define SEQ_GT32( a, b )    ( (int) ( (a) - (b) ) > 0 )

/** @brief similar macros for comparing 16 bit sequence numbers
 * @define SEQ_LEQ16(a, b) Checks if sequence number a is less or equal than b
 * @define SEQ_GEQ16(a, b) Checks if sequence number a is greater or equal than
 *  b
 */
#define SEQ_LEQ16( a, b )   ( (short) ( (a) - (b) ) <= 0 )
#define SEQ_GEQ16( a, b )   ( (short) ( (a) - (b) ) >= 0 )


struct CipUnconnectedSendParameter
{
    EipByte         priority;
    EipUint8        timeout_ticks;
    EipUint16       message_request_size;

    CipMessageRouterRequest     message_request;
    CipMessageRouterResponse*   message_response;

    EipUint8        reserved;
    // CipRoutePath    route_path;      CipPortSegment?
    void*           data;
};


// public functions

/** @brief Initialize the data of the connection manager object
 */
EipStatus ConnectionManagerInit();

/** @brief Get a connected object dependent on requested ConnectionID.
 *
 *   @param connection_id  requested @var connection_id of opened connection
 *   @return pointer to connected Object
 *           0 .. connection not present in device
 */
CipConn* GetConnectedObject( EipUint32 connection_id );

/**  Get a connection object for a given output assembly.
 *
 *   @param output_assembly_id requested output assembly of requested
 * connection
 *   @return pointer to connected Object
 *           0 .. connection not present in device
 */
CipConn* GetConnectedOutputAssembly( EipUint32 output_assembly_id );


/** @brief Close the given connection
 *
 * This function will take the data form the connection and correctly closes the
 * connection (e.g., open sockets)
 * @param cip_conn pointer to the connection object structure to be
 * closed
 */
void CloseConnection( CipConn* cip_conn );

bool IsConnectedInputAssembly( EipUint32 aInstanceId );

// TODO: Missing documentation
bool IsConnectedOutputAssembly( EipUint32 aInstanceId );

/** @brief Insert the given connection object to the list of currently active
 *  and managed connections.
 *
 * By adding a connection to the active connection list the connection manager
 * will perform the supervision and handle the timing (e.g., timeout,
 * production inhibit, etc).
 *
 * @param cip_conn pointer to the connection object to be added.
 */
void AddNewActiveConnection( CipConn* cip_conn );

// TODO: Missing documentation
void RemoveFromActiveConnections( CipConn* cip_conn );

#endif // CIPSTER_CIPCONNECTIONMANAGER_H_
