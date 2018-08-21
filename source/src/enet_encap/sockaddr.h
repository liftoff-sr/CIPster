/*******************************************************************************
 * Copyright (C) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#ifndef CIPSTER_SOCKADDR_H_
#define CIPSTER_SOCKADDR_H_

#include <string.h>
#include <string>

#if defined(__linux__)
 #include <arpa/inet.h>
#elif defined(_WIN32)
 #undef _WINSOCKAPI_    // suppress Mingw32's "Please include winsock2.h before windows.h"
 #include <winsock2.h>
 #include <ws2tcpip.h>
#endif


const int SADDRZ = sizeof(sockaddr);

std::string IpAddrStr( in_addr aIP );


/**
 * Struct SockAddr
 * is a wrapper for a sock_addr_in.  It provides host endian accessors so that
 * client code can forget about network endianess.  It also provides an operator
 * to convert itself directly into a (sockaddr_in*) for use in BSD sockets calls.
 *
 * @see #Cpf which knows how to serialize and deserialize this for its
 *   own needs and on the wire it is called a "SockAddr Info Item".
 */
class SockAddr
{
public:
    SockAddr(
            int aPort = 0,
            int aIP = INADDR_ANY       // INADDR_ANY is zero
            );

    SockAddr( const sockaddr_in& aSockAddr ) :
        sa( aSockAddr )
    {}

    /// assign from a sockaddr_in to this
    SockAddr& operator=( const sockaddr_in& rhs )
    {
        sa = rhs;
        return *this;
    }

    bool operator==( const SockAddr& other ) const
    {
        return sa.sin_addr.s_addr == other.sa.sin_addr.s_addr
           &&  sa.sin_port == other.sa.sin_port;
    }

    bool operator!=( const SockAddr& other ) const  { return !(*this == other); }

    operator const sockaddr_in& () const    { return sa; }
    operator const sockaddr*    () const    { return (sockaddr*) &sa; }
    operator       sockaddr*    () const    { return (sockaddr*) &sa; }

    // All accessors take and return host endian values.  Internally,
    // sin_port and sin_addr.s_addr are stored in network byte order (big endian).

    SockAddr&   SetFamily( int aFamily )    { sa.sin_family = aFamily;                  return *this; }
    SockAddr&   SetPort( int aPort )        { sa.sin_port = htons( aPort );             return *this; }
    SockAddr&   SetAddr( int aIPAddr )      { sa.sin_addr.s_addr = htonl( aIPAddr );    return *this; }

    int Family() const          { return sa.sin_family; }
    int Port() const            { return ntohs( sa.sin_port ); }
    int Addr() const            { return ntohl( sa.sin_addr.s_addr ); }

    std::string AddrStr() const { return IpAddrStr( sa.sin_addr ); }

    /**
     * Function IsValid
     * checks fields according to CIP Vol2 3-3.9.4
     * and returns true if valid, else false.
     */
    bool IsValid() const
    {
        return sa.sin_family == AF_INET
          &&   !memcmp( &sa.sin_zero[0], &sa.sin_zero[1], 7 )
          &&   !sa.sin_zero[0];
    }

    bool IsMulticast() const
    {
        // Vol2 3-5.3
        // https://www.iana.org/assignments/multicast-addresses/multicast-addresses.xhtml
        // "The multicast addresses are in the range 224.0.0.0 through 239.255.255.255."
        static const unsigned lo = ntohl( inet_addr( "224.0.0.0" ) );
        static const unsigned hi = ntohl( inet_addr( "239.255.255.255" ) );

        unsigned addr = ntohl( sa.sin_addr.s_addr );
        return lo <= addr && addr <= hi;
    }

protected:
     sockaddr_in    sa;
};

#endif  //  CIPSTER_SOCKADDR_H_
