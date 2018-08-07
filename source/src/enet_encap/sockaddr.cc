
/*******************************************************************************
 * Copyright (C) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#include "sockaddr.h"

std::string IpAddrStr( in_addr aIP )
{
    // inet_ntoa uses a static buffer, so capture that into a std::string
    // for safe keeping.
    return inet_ntoa( aIP );
}


SockAddr::SockAddr( int aPort, int aIP )
{
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons( aPort );
    sa.sin_addr.s_addr = htonl( aIP );

    memset( &sa.sin_zero, 0, sizeof sa.sin_zero );
}


