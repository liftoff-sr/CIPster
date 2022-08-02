
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


std::string strerrno()
{
    char    buf[256];

    buf[0] = 0;

#if defined(__linux__)
    // There are two versions of sterror_r() depending on age of glibc, try
    // and handle both of them with this:
    uintptr_t result = (uintptr_t) strerror_r( errno, buf, sizeof buf );

    return buf[0] ? buf : (char*) result;

#elif defined(_WIN32)

    // I don't think this works well, don't know why, its Windows....false advertising?
    int len = FormatMessage(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,                       // lpsource
        WSAGetLastError(),          // message id
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buf,
        sizeof buf,
        NULL );

    if( !buf[0] )   // insurance policy against above not working.
        len = snprintf( buf, sizeof buf, "%d", WSAGetLastError() );

    return std::string( buf, len );
#endif
}


socket_error::socket_error() :
    std::runtime_error( strerrno() ),
#if defined(__linux__)
    error_code( errno )
#elif defined(_WIN32)
    error_code( WSAGetLastError() )
#endif
{}


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


std::string SockAddr::Format() const
{
    char    buf[32];
    int     len = snprintf( buf, sizeof buf, ":%d", Port() );

    std::string ret = AddrStr();

    ret.append( buf, len );

    return ret;
}
