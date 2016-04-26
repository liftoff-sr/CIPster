/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#ifndef OPENER_NETWORKHANDLER_H_
#define OPENER_NETWORKHANDLER_H_


#include "typedefs.h"

/** @brief Start a TCP/UDP listening socket, accept connections, receive data
*    in select loop, call manageConnections periodically.
 *  @return status
 *          EIP_ERROR .. error
 */
EipStatus NetworkHandlerInitialize();

EipStatus NetworkHandlerProcessOnce();

EipStatus NetworkHandlerFinish();

#endif // OPENER_NETWORKHANDLER_H_
