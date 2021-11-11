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


/**
 * Function NetworkHandlerInitialize
 * starts a TCP/UDP listening socket to accept connections.
 */
EipStatus NetworkHandlerInitialize();

EipStatus NetworkHandlerProcessOnce();

EipStatus NetworkHandlerFinish();

/**
 * Function CloseSocket
 * closes @a aSocket
 */
void CloseSocket( int aSocket );

bool SocketAsync( int aSocket, bool isAsync = true );


/**
 * Function SendUdpData
 * sends the bytes provided in @a aOutput to the UDP node given by @a aSockAddr
 * using @a aSocket.
 *
 * @param aSockAddr is the "send to" address and could be multicast or unicast or broadcast.
 * @param aSocket is the socket descriptor to send on
 * @param aOutput is the data to send and its length
 * @throw  socket_error - if @a aOutput contains more than what was sent.
 */
void SendUdpData( const SockAddr& aSockAddr, int aSocket, BufReader aOutput );


/**
 * Class UdpSocket
 * holds a UDP socket handle and also optional group membership with
 * reference counting for such membership.
 */
class UdpSocket
{
    friend class UdpSocketMgr;

public:
    UdpSocket( const SockAddr& aSockAddr, int aSocket ) :
        m_sockaddr( aSockAddr ),
        m_socket( aSocket ),
        m_ref_count( 1 ),
        m_underlying( 0 )
    {
    }

    ~UdpSocket()
    {
        if( m_socket != kSocketInvalid )
        {
            CloseSocket( m_socket );
            m_socket = kSocketInvalid;
        }
    }

    /**
     * Function Send
     * send a packet on UDP.
     * @throw socket_error if a problem sending
     */
    void Send( const SockAddr& aAddr, const BufReader& aReader )
    {
        ::SendUdpData( aAddr, m_socket, aReader );
    }

    int Recv( SockAddr* aAddr, const BufWriter& aWriter )
    {
        socklen_t   from_addr_length = SADDRZ;

        return recvfrom( m_socket, (char*) aWriter.data(), aWriter.capacity(), 0,
                    *aAddr, &from_addr_length );
    }

    const SockAddr& SocketAddress() const   { return m_sockaddr; }
    int h() const                           { return m_socket; }
    int RefCount() const                    { return m_ref_count; }

    void Show() const
    {
        CIPSTER_TRACE_INFO( "UdpSocket[%d] bound:%s  mcast:%s\n",
            m_socket,
            m_sockaddr.Format().c_str(),
            m_underlying ? m_underlying->m_sockaddr.Format().c_str() : "no" );
    }

private:
    SockAddr    m_sockaddr;     // what the m_socket is bound to with bind()
    int         m_socket;
    int         m_ref_count;
    UdpSocket*  m_underlying;   // used by Multicast only.
};


/**
 * Class UdpSocketMgr
 * manages UDP sockets in the form of UdpSocket instances.
 * Since a UDP socket can be used for multiple inbound
 * and outbound UDP frames, without regard to who the recipient or sender is,
 * we re-use UDP sockets accross multiple I/O connections.  This way we do
 * not create more than the minimum number of sockets, and we should not have
 * to use SO_REUSEADDR.
 */
class UdpSocketMgr
{
public:

    typedef std::vector<UdpSocket*>     sockets;
    typedef sockets::iterator           sock_iter;
    typedef sockets::const_iterator     sock_citer;

#define DEFAULT_BIND_IPADDR             INADDR_ANY
//#define DEFAULT_BIND_IPADDR           ntohl( CipTCPIPInterfaceClass::IpAddress( 1 ) )

    /**
     * Function GrabSocket
     * registers use of a UDP socket bound to aSockAddr, and creates it if
     * necessary. Additionally, if aMulticastAddr is not NULL, it will register
     * use of that group and join it to the socket associated with aSockAddr if it
     * is not already.  Since reference counting is used both for the base socket
     * and for the groups, you must balance the number of calls to GrabSocket with
     * those to ReleaseSocket.
     */
    static UdpSocket*       GrabSocket( const SockAddr& aSockAddr, const SockAddr* aMulticastAddr=NULL );

    /**
     * Function RelaseSocket
     * reduces the reference count associated with aUdpSocket which was obtained
     * earlier with GrabSocket.  aUdpSocket may be a multicast group address if
     * you want to drop a group membership, but that will only happen when the
     * group's reference count goes to zero.
     */
    static bool             ReleaseSocket( UdpSocket* aUdpSocket );
    static sockets&         GetAllSockets()  { return m_sockets; }

protected:

    /**
     * Function createSocket
     * creates a UDP socket and binds it to aSockAddr.
     *
     * @param aSockAddr tells how to setup the socket.
     * @return int - socket on success or kSocketInvalid on error
     */
    static int createSocket( const SockAddr& aSockAddr );

    // Allocate and initialize a new UdpSocket
    static UdpSocket*  alloc( const SockAddr& aSockAddr, int aSocket );

    static void free( UdpSocket* aUdpSocket );

    // find aSockAddr in aList, return it or NULL if not found.
    static UdpSocket* find( const SockAddr& aSockAddr, const sockets& aList );

    static sockets      m_sockets;
    static sockets      m_multicast;    // these piggyback on a m_socket entry
    static sockets      m_free;         // recycling bin
};


#endif  // NETWORKHANDLER_H_
