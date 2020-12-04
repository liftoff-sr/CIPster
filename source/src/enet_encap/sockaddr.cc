
/*******************************************************************************
 * Copyright (C) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#include "sockaddr.h"

#if defined(__linux__) || defined(__APPLE__)
 #include <netdb.h>
#endif

#if defined(__APPLE__)
 #include <arpa/inet.h>
#endif

std::string IpAddrStr( in_addr aIP )
{
    // inet_ntoa uses a static buffer, so capture that into a std::string
    // for safe keeping.
    return inet_ntoa( aIP );
}


SockAddr::SockAddr( unsigned aPort, unsigned aIP )
{
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons( aPort );
    sa.sin_addr.s_addr = htonl( aIP );

    memset( &sa.sin_zero, 0, sizeof sa.sin_zero );
}


SockAddr::SockAddr( const char* aNameOrIPAddr, unsigned aPort )
{
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons( aPort );

    sa.sin_addr.s_addr = inet_addr( aNameOrIPAddr );
    if( sa.sin_addr.s_addr == INADDR_NONE )
    {
        hostent* ent = gethostbyname( aNameOrIPAddr );
        if( !ent )
        {
            const char* errmsg = "gethostbyname";

            switch( h_errno )
            {
            case HOST_NOT_FOUND:    errmsg = "host is unknown";     break;
            case NO_DATA:           errmsg = "host has no IP";      break;
            case NO_RECOVERY:       errmsg = "name server error";   break;
            case TRY_AGAIN:         errmsg = "try again later";     break;
            }

            throw socket_error( errmsg, h_errno );
        }

        sa.sin_addr.s_addr = *(int*) ent->h_addr_list[0];
    }

    memset( &sa.sin_zero, 0, sizeof sa.sin_zero );
}
