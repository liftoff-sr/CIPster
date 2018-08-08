/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#ifndef NETWORKHANDLER_H_
#define NETWORKHANDLER_H_

#include <string>

#include "sockaddr.h"
#include "../cip/ciptypes.h"

extern uint64_t g_current_usecs;


/**
 * Function NetworkHandlerInitialize
 * starts a TCP/UDP listening socket to accept connections.
 */
EipStatus NetworkHandlerInitialize();

EipStatus NetworkHandlerProcessOnce();

EipStatus NetworkHandlerFinish();

std::string strerrno();


/**
 * Function CreateUdpSocket
 * creates a producing or consuming UDP socket.
 *
 * @param aDirection may be kUdpConsuming or kUdpProducing
 * @param aSockAddr tells how to setup the socket.
 * @return int - socket on success or -1 on error
 */
int CreateUdpSocket( UdpDirection aDirection, const SockAddr& aSockAddr );

inline const char* ShowUdpDirection( UdpDirection aDirection )
{
    if( aDirection == kUdpConsuming )
        return "consuming";
    else
        return "producing";
}

/**
 * Function SendUdpData
 * sends the bytes provided in @a aOutput to the UDP node given by @a aSockAddr
 * using @a aSocket.
 *
 * @param aSockAddr is the "send to" address
 * @param aSocket is the socket descriptor to send on
 * @param aOutput is the data to send and its length
 * @return  EipStatus - EipStatusSuccess on success
 */
EipStatus SendUdpData( const SockAddr& aSockAddr, int aSocket, BufReader aOutput );

/**
 * Function CloseSocket
 * closes @a aSocket
 */
void CloseSocket( int aSocket );


#endif  // NETWORKHANDLER_H_
