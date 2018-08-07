/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/
#ifndef CIPSTER_APPCONTYPE_H_
#define CIPSTER_APPCONTYPE_H_

#include "cipconnectionmanager.h"


/**
 * Function GetIoConnectionForConnectionData
 * checks if for the given connection data received in a forward_open request
 * whether a suitable connection is available, because it was registered previously or
 * opened previously.
 *
 *  If a suitable connection is found the connection data is transfered the
 *  application connection type is set (i.e., EConnType).
 *
 *  @param aParams holds the connection data describing the needed connection
 *  @param extended_error is where to put an error
 *
 *  @return CipConn* -
 *        - on success: A pointer to the connection object already containing the connection
 *          data given in @a aParams.
 *        - on error: NULL
 */
CipConn* GetIoConnectionForConnectionData( ConnectionData* aParams,
        ConnMgrStatus* extended_error );

/** @brief Check if there exists already an exclusive owner or listen only connection
 *         which produces the input assembly.
 *
 *  @param input_point the Input point to be produced
 *  @return if a connection could be found a pointer to this connection if not NULL
 */
CipConn* GetExistingProducerMulticastConnection( int input_point );

/** @brief check if there exists an producing multicast exclusive owner or
 * listen only connection that should produce the same input but is not in charge
 * of the connection.
 *
 * @param input_point the produced input
 * @return if a connection could be found the pointer to this connection
 *      otherwise NULL.
 */
CipConn* GetNextNonControlMasterConnection( int input_point );

/** @brief Close all connection producing the same input and have the same type
 * (i.e., listen only or input only).
 *
 * @param input_point  the input point
 * @param instance_type the connection application type
 */
void CloseAllConnectionsForInputWithSameType( int input_point,
        ConnInstanceType instance_type );

/**@ brief close all open connections.
 *
 * For I/O connections the sockets will be freed. The sockets for explicit
 * connections are handled by the encapsulation layer, and freed there.
 */
void CloseAllConnections();

/** @brief Check if there is an established connection that uses the same
 * config point.
 *
 * @param config_point The configuration point
 * @return true if connection was found, otherwise false
 */
bool ConnectionWithSameConfigPointExists( int config_point );

#endif    // CIPSTER_APPCONTYPE_H_
