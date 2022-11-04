/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#include "networkhandler.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#if defined(__linux__)
 #include <unistd.h>
 #include <sys/time.h>
 #include <time.h>
 #include <sys/select.h>
#endif

#if defined(__APPLE__)
 #include <unistd.h>
 #include <sys/cdefs.h>
 #include <arpa/inet.h>
 #include <sys/socket.h>
 #include <sys/sockio.h>
 #include <net/if.h>
 #include <sys/ioctl.h>
 #include <sys/uio.h>
 #include <netinet/in.h>
#endif

#include <cipster_api.h>
#include <trace.h>
#include "encap.h"
#include "cip/cipconnectionmanager.h"
#include "cip/ciptcpipinterface.h"


/**
 * The number of bytes used for the Ethernet message buffer on
 * the PC port. For different platforms it may make sense to
 * have more than one buffer.
 *
 *  This buffer size will be used for any received message.
 *  The same buffer is used for the replied explicit message.
 */
static uint8_t s_buf[CIPSTER_ETHERNET_BUFFER_SIZE];
#define S_BUFZ                      sizeof(s_buf)

#define MAX_NO_OF_TCP_SOCKETS       10

static fd_set master_set;
static fd_set read_set;

// temporary file descriptor for select()
static int highest_socket_handle;

uint64_t    g_current_usecs;
unsigned    s_last_usecs;       // only 32 needed bits here.


struct NetworkStatus
{
    int         tcp_listener;
    int         udp_unicast_listener;
    int         udp_local_broadcast_listener;
    int         udp_global_broadcast_listener;
    unsigned    elapsed_time_usecs;
    unsigned    tcp_inactivity_usecs;
};


static NetworkStatus s_sockets;


/// Return a monotonically increasing usecs time that wraps around
/// after overflow.  Just use 32 bits here, 64 bits are kept in
/// g_current_usecs which is global.  This a cross platform concern which
/// will make implementing this function easier under other OS's.
static unsigned usecs_now()
{
#if defined(__linux__) || defined(__APPLE__)
    struct timespec	now;

    clock_gettime( CLOCK_MONOTONIC, &now );

    unsigned usecs = unsigned( (unsigned long) now.tv_nsec/1000u + now.tv_sec * 1000000 );

#elif defined(_WIN32)

    static struct QPC_Init
    {
        // Call QueryPerformanceFrequency only once during
        // program loading, i.e. static construction.
        QPC_Init()
        {
            LARGE_INTEGER lfrequency;

            QueryPerformanceFrequency( &lfrequency );

            frequency = lfrequency.QuadPart;
        }

        uint64_t frequency;

    } clock;

    LARGE_INTEGER performance_counter;

    QueryPerformanceCounter( &performance_counter );

    unsigned usecs = unsigned( performance_counter.QuadPart * 1000000 / clock.frequency );
#endif

    return usecs;
}


static void master_set_add( const char* aType, int aSocket )
{
    CIPSTER_TRACE_INFO( "%s[%d]: %s socket\n", __func__, aSocket, aType );

    (void) aType;

    FD_SET( aSocket, &master_set );

    if( aSocket > highest_socket_handle )
    {
        highest_socket_handle = aSocket;
    }
}


static void master_set_rem( int aSocket )
{
    CIPSTER_ASSERT( aSocket >= 0 );
    CIPSTER_TRACE_INFO( "%s[%d]\n", __func__, aSocket );

    FD_CLR( aSocket, &master_set );

    if( aSocket == highest_socket_handle && aSocket > 0 )
    {
        --highest_socket_handle;
    }
}


bool SocketAsync( int aSocket, bool isAsync )
{
#if defined(__linux__) || defined(__APPLE__)
    int flags   = isAsync ? (O_RDWR | O_NONBLOCK) : O_RDWR;
    int ret     = fcntl( aSocket, F_SETFL, flags );
#elif defined(_WIN32)
    unsigned long mode = isAsync;
    int ret = ioctlsocket( aSocket, FIONBIO, &mode );
#endif

    if( ret )
    {
        CIPSTER_TRACE_ERR( "%s[%d]: errno:'%s'\n",
            __func__, aSocket, strerrno().c_str() );
    }

    return !ret;
}


void CloseSocket( int aSocket )
{
    if( aSocket >= 0 )
    {
        CIPSTER_TRACE_INFO( "%s[%d]\n", __func__, aSocket );

        master_set_rem( aSocket );

#if defined(__linux__) || defined(__APPLE__)
        shutdown( aSocket, SHUT_RDWR );
        close( aSocket );
#elif defined(_WIN32)
        closesocket( aSocket );
#endif
    }
}


/**
 * Function checkSocketSet
 * checks if the given socket is set in 'read_set' and 'master_set'.
 */
static bool checkSocketSet( int aSocket )
{
    if( FD_ISSET( aSocket, &read_set ) )
    {
        // remove it from the read set so that later checks will not find it
        FD_CLR( aSocket, &read_set );

        if( FD_ISSET( aSocket, &master_set ) )
        {
            //CIPSTER_TRACE_INFO( "%s[%d]: true\n", __func__, aSocket );
            return true;
        }

        CIPSTER_TRACE_INFO( "%s[%d]: closed with pending message\n",
            __func__, aSocket );
    }

    return false;
}


void CheckAndHandleUdpUnicastSocket()
{
    SockAddr    from_addr;
    socklen_t   from_addr_length;

    // see if this is an unsolicited inbound UDP message
    if( checkSocketSet( s_sockets.udp_unicast_listener ) )
    {
        from_addr_length = sizeof(from_addr);

        CIPSTER_TRACE_STATE(
                "%s[%d]: unsolicited UDP message on EIP unicast socket\n",
                __func__, s_sockets.udp_unicast_listener );

        // Handle UDP broadcast messages
        int received_size = recvfrom( s_sockets.udp_unicast_listener,
                (char*) s_buf, S_BUFZ,
                0, from_addr,  &from_addr_length );

        if( received_size <= 0 ) // got error
        {
            CIPSTER_TRACE_ERR(
                    "%s[%d]: error on recvfrom UDP unicast socket: '%s'\n",
                    __func__,
                    s_sockets.udp_unicast_listener,
                    strerrno().c_str() );
            return;
        }

        int reply_length = Encapsulation::HandleReceivedExplicitUdpData(
                s_sockets.udp_unicast_listener, from_addr,
                BufReader( s_buf, received_size ),
                BufWriter( s_buf, S_BUFZ ), true );

        if( reply_length > 0 )
        {
            // if the active socket matches a registered UDP callback, handle a UDP packet
            int sent_count = sendto( s_sockets.udp_unicast_listener,
                        (char*) s_buf, reply_length, 0,
                        from_addr, sizeof(from_addr) );

            CIPSTER_TRACE_INFO( "%s[%d]: sent %d reply bytes\n",
                __func__, s_sockets.udp_unicast_listener,  sent_count );

            if( sent_count != reply_length )
            {
                CIPSTER_TRACE_INFO(
                        "%s[%d]: UDP unicast response was not fully sent\n",
                        __func__, s_sockets.udp_unicast_listener );
            }
        }
    }
}


/**
 * Function CheckAndHandlTcpListernetSocket
 * handles any connection request coming in the TCP server socket.
 */
void CheckAndHandleTcpListenerSocket()
{
    int new_socket;

    // see if this is a connection request to the dedicated "tcp_listener"
    if( checkSocketSet( s_sockets.tcp_listener ) )
    {
        new_socket = accept( s_sockets.tcp_listener, NULL, NULL );

        CIPSTER_TRACE_INFO( "%s[%d]: new TCP connection\n", __func__, new_socket );

        if( new_socket == kSocketInvalid )
        {
            CIPSTER_TRACE_ERR( "%s[%d]: error on accept: %s\n",
                    __func__, s_sockets.tcp_listener, strerrno().c_str() );
            return;
        }

        EncapError result = SessionMgr::RegisterTcpConnection( new_socket );

        if( result != kEncapErrorSuccess )
        {
            CIPSTER_TRACE_ERR(
                "%s[%d]: rejecting incoming TCP connection since count exceeds\n"
                " CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS (= %d)\n",
                __func__, new_socket,
                CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS
                );
            return;
        }

        master_set_add( "TCP", new_socket );
    }
}


/*

    Vol2 2-2:
    Whenever UDP is used to send an encapsulated message, the entire
    message shall be sent in a single UDP packet. Only one encapsulated
    message shall be present in a single UDP packet destined to UDP port
    0xAF12.

*/


/**
 * Function CheckAndHandleUdpLocalBroadcastSocket
 * checks if data has been received on the UDP broadcast socket and if
 * yes handles it correctly.
 */
void CheckAndHandleUdpLocalBroadcastSocket()
{
    SockAddr    from_addr;
    socklen_t   from_addr_length;

    // see if this is an unsolicited inbound UDP message
    if( checkSocketSet( s_sockets.udp_local_broadcast_listener ) )
    {
        from_addr_length = sizeof(from_addr);

        CIPSTER_TRACE_STATE(
            "%s[%d]: unsolicited UDP on local broadcast socket\n",
            __func__,
            s_sockets.udp_local_broadcast_listener
            );

        // Handle UDP broadcast messages
        int received_size = recvfrom( s_sockets.udp_local_broadcast_listener,
                (char*) s_buf,  S_BUFZ, 0,
                from_addr, &from_addr_length );

        if( received_size <= 0 ) // got error
        {
            CIPSTER_TRACE_ERR(
                    "%s[%d]: error on recvfrom UDP local broadcast socket: '%s'\n",
                    __func__,
                    s_sockets.udp_local_broadcast_listener,
                    strerrno().c_str() );
            return;
        }

        int reply_length = Encapsulation::HandleReceivedExplicitUdpData(
                s_sockets.udp_local_broadcast_listener, from_addr,
                BufReader( s_buf, received_size ),
                BufWriter( s_buf, S_BUFZ ), false );

        if( reply_length > 0 )
        {
            // if the active socket matches a registered UDP callback, handle a UDP packet
            int sent_count = sendto( s_sockets.udp_local_broadcast_listener,
                        (char*) s_buf, reply_length, 0,
                        from_addr, sizeof(from_addr) );

            CIPSTER_TRACE_INFO( "%s[%d]: sent %d reply bytes\n",
                __func__, s_sockets.udp_local_broadcast_listener, sent_count );

            if( sent_count != reply_length )
            {
                CIPSTER_TRACE_INFO(
                    "%s[%d]: UDP response was not fully sent\n",
                    __func__, s_sockets.udp_local_broadcast_listener );
            }
        }
    }
}


void CheckAndHandleUdpGlobalBroadcastSocket()
{
    SockAddr    from_addr;
    socklen_t   from_addr_length;

    // see if this is an unsolicited inbound UDP message
    if( checkSocketSet( s_sockets.udp_global_broadcast_listener ) )
    {
        from_addr_length = sizeof(from_addr);

        CIPSTER_TRACE_STATE(
            "%s[%d]: unsolicited UDP on global broadcast socket\n",
            __func__, s_sockets.udp_global_broadcast_listener );

        // Handle UDP broadcast messages
        int received_size = recvfrom( s_sockets.udp_global_broadcast_listener,
                (char*) s_buf, S_BUFZ, 0,
                from_addr, &from_addr_length );

        if( received_size <= 0 ) // got error
        {
            CIPSTER_TRACE_ERR(
                "%s[%d]: error on recvfrom UDP global broadcast socket: '%s'\n",
                __func__,
                s_sockets.udp_global_broadcast_listener,
                strerrno().c_str() );

            return;
        }

        CIPSTER_TRACE_INFO( "%s[%d]: %d bytes received on global broadcast UDP\n",
            __func__, s_sockets.udp_global_broadcast_listener, received_size );

        int reply_length = Encapsulation::HandleReceivedExplicitUdpData(
                s_sockets.udp_global_broadcast_listener, from_addr,
                BufReader( s_buf, received_size ),
                BufWriter( s_buf, S_BUFZ ), false );

        if( reply_length > 0 )
        {
            // if the active socket matches a registered UDP callback, handle a UDP packet
            int sent_count = sendto( s_sockets.udp_global_broadcast_listener,
                        (char*) s_buf, reply_length, 0,
                        from_addr, SADDRZ );

            CIPSTER_TRACE_INFO( "%s[%d]: sent %d reply bytes\n",
                __func__, s_sockets.udp_global_broadcast_listener, sent_count );

            if( sent_count != reply_length )
            {
                CIPSTER_TRACE_INFO(
                        "%s[%d]: UDP response was not fully sent\n",
                        __func__, s_sockets.udp_global_broadcast_listener );
            }
        }
    }
}


/**
 * Function checkAndHandleUdpSockets
 * checks all open UDP sockets for inbound data, and passes any packets
 * up to RecvConnectedData() for filtering.
 */
static void checkAndHandleUdpSockets()
{
    /*
        We can get garbage in on any open UDP socket and it must be dealt with
        by draining it and perhaps tossing it. Merely accepting data from
        sockets associated with consuming connections is not correct, this will
        screw up the caller's assumption that anything still in read_set is a
        TCP connection.

        Also, unless we drain what we've received, TCP stack buffers can get
        exhausted.
    */

    UdpSocketMgr::sockets& all = UdpSocketMgr::GetAllSockets(); // UDP only

    SockAddr    from_addr;

    for( UdpSocketMgr::sock_iter it = all.begin();  it != all.end();  ++it )
    {
        UdpSocket*  s = *it;

        s->Show();

        if( checkSocketSet( s->h() ) )
        {
            CIPSTER_TRACE_INFO( "GOT ONE %s[%d]\n", __func__, s->h() );

            // Since it is non-blocking, call Recv() until
            // byte_count is <= 0.  Keep socket open for every case.

            // Drain each UDP socket up to some limit you can choose.
            // This strategy contemplates that somebody might be bombing us,
            // maybe even maliciously.  Anything we don't fetch out now
            // will likely still be there on the next call to
            // NetworkHandlerProcessOnce() and we should eventually catch up.
            int limit = 64 * s->RefCount();

            int attempt;
            for( attempt = 0;  attempt < limit;  ++attempt )
            {
                int byte_count = s->Recv( &from_addr, BufWriter( s_buf, S_BUFZ ) );

                if( byte_count <= 0 )
                {
#if defined(_WIN32)
                    if (WSAGetLastError () != WSAEWOULDBLOCK)
#else
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
#endif
                    {
                        CIPSTER_TRACE_ERR( "%s[%d]: errno: '%s'\n",
                            __func__, s->h(), strerrno().c_str() );
                    }

                    break;
                }

                CipConnMgrClass::RecvConnectedData(
                    s, from_addr, BufReader( s_buf, byte_count ) );
            }

            if( attempt && attempt == limit )
            {
                CIPSTER_TRACE_ERR( "%s[%d]: too much inbound UDP traffic\n",
                    __func__, s->h() );
            }
        }
    }
}


/**
 * Function HandleDataOnTcpSocket
 */
EipStatus HandleDataOnTcpSocket( int aSocket )
{
    int num_read = Encapsulation::ReceiveTcpMsg( aSocket,
                        BufWriter( s_buf, sizeof s_buf ) );

    //CIPSTER_TRACE_INFO( "%s[%d]: num_read:%d\n", __func__, aSocket, num_read );

    if( num_read < ENCAPSULATION_HEADER_LENGTH )
    {
        return kEipStatusError;
    }

    int replyz = Encapsulation::HandleReceivedExplicitTcpData( aSocket,
                        BufReader( s_buf, num_read ),
                        BufWriter( s_buf, sizeof s_buf ) );

    if( replyz > 0 )
    {
#if defined(DEBUG) && 0
        byte_dump( "sTCP", s_buf, replyz );
#endif
        int sent_count = send( aSocket, (char*) s_buf, replyz, 0 );

        CIPSTER_TRACE_INFO( "%s[%d]: replied with %d bytes\n",
                __func__, aSocket, sent_count );

        if( sent_count != replyz )
        {
            CIPSTER_TRACE_WARN( "%s[%d]: TCP response was not fully sent\n",
                    __func__, aSocket );
        }

        return kEipStatusOk;
    }

    else if( replyz == 0 )
    {
        CIPSTER_TRACE_INFO(
            "%s[%d]: 0 length reply from HandleReceivedExplicitTcpData()\n",
            __func__, aSocket );

        return kEipStatusOk;
    }

    else
    {
        CIPSTER_TRACE_INFO(
            "%s[%d]: < 0 length reply from HandleReceivedExplicitTcpData()\n",
            __func__, aSocket );

        return kEipStatusError;
    }
}


EipStatus NetworkHandlerInitialize()
{
#if defined(_WIN32)
    WORD    wVersionRequested;
    WSADATA wsaData;

    wVersionRequested = MAKEWORD(2, 2);

    WSAStartup( wVersionRequested, &wsaData );
#endif

    static const int one = 1;

    const CipTcpIpInterfaceConfiguration& c = CipTCPIPInterfaceClass::InterfaceConf(1);

    // clear the master and temp sets
    FD_ZERO( &master_set );
    FD_ZERO( &read_set );

    s_sockets.tcp_listener = -1;
    s_sockets.udp_unicast_listener = -1;
    s_sockets.udp_local_broadcast_listener = -1;
    s_sockets.udp_global_broadcast_listener = -1;

    //-----<tcp_listener>-------------------------------------------

    // create a new TCP socket
    s_sockets.tcp_listener = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

    CIPSTER_TRACE_INFO( "s_sockets.tcp_listener == %d\n", s_sockets.tcp_listener );

    if( s_sockets.tcp_listener == -1 )
    {
        CIPSTER_TRACE_ERR( "error allocating socket stream listener, %d\n", errno );
        goto error;
    }

    // Activates address reuse
    if( setsockopt( s_sockets.tcp_listener, SOL_SOCKET, SO_REUSEADDR,
                (char*) &one, sizeof(one) ) )
    {
        CIPSTER_TRACE_ERR(
                "error setting socket option SO_REUSEADDR on tcp_listener\n" );
        goto error;
    }

    {
        SockAddr address( kEIP_Reserved_Port, ntohl( c.ip_address ) );

        // bind the new socket to port 0xAF12 (CIP)
        if( bind( s_sockets.tcp_listener, address, SADDRZ ) )
        {
            CIPSTER_TRACE_ERR( "%s: bind(%s) error for tcp_listener: %s\n",
                __func__, address.AddrStr().c_str(), strerrno().c_str() );
            goto error;
        }
    }

    //-----<udp_global_broadcast_listner>--------------------------------------
    // create a new UDP socket
    s_sockets.udp_global_broadcast_listener = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if( s_sockets.udp_global_broadcast_listener == -1 )
    {
        CIPSTER_TRACE_ERR( "%s: error allocating UDP broadcast listener socket, %d\n",
                __func__, errno );
        goto error;
    }

    // Activates address reuse
    if( setsockopt( s_sockets.udp_global_broadcast_listener, SOL_SOCKET, SO_REUSEADDR,
            (char*) &one, sizeof(one) ) )
    {
        CIPSTER_TRACE_ERR(
                "error setting socket option SO_REUSEADDR on udp_broadcast_listener\n" );
        goto error;
    }

    // enable the UDP socket to receive broadcast messages
    if( setsockopt( s_sockets.udp_global_broadcast_listener,
            SOL_SOCKET, SO_BROADCAST, (char*) &one, sizeof(one) ) )
    {
        CIPSTER_TRACE_ERR(
                "error with setting broadcast receive for UDP socket: %s\n",
                strerrno().c_str() );
        goto error;
    }

    {
        SockAddr address( kEIP_Reserved_Port, INADDR_BROADCAST );

        if( bind( s_sockets.udp_global_broadcast_listener, address, SADDRZ ) )
        {
            CIPSTER_TRACE_ERR(
                    "error with global broadcast UDP bind: %s\n",
                    strerrno().c_str() );
            goto error;
        }
    }

    //-----<udp_local_broadcast_listener>---------------------------------------
    // create a new UDP socket
    s_sockets.udp_local_broadcast_listener = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if( s_sockets.udp_local_broadcast_listener == -1 )
    {
        CIPSTER_TRACE_ERR( "error allocating UDP broadcast listener socket, %d\n",
                errno );

        goto error;
    }

    // Activates address reuse
    if( setsockopt( s_sockets.udp_local_broadcast_listener,
            SOL_SOCKET, SO_REUSEADDR, (char*) &one, sizeof(one) ) )
    {
        CIPSTER_TRACE_ERR(
                "error setting socket option SO_REUSEADDR on udp_broadcast_listener\n" );
        goto error;
    }

    {
        SockAddr address(   kEIP_Reserved_Port,
                            ntohl( c.ip_address | ~c.network_mask ) );

        if( bind( s_sockets.udp_local_broadcast_listener, address, SADDRZ ) )
        {
            CIPSTER_TRACE_ERR(
                    "error with udp_local_broadcast_listener bind: %s\n",
                    strerrno().c_str() );
            goto error;
        }
    }

    //-----<udp_unicast_listener>----------------------------------------------
    // create a new UDP socket
    s_sockets.udp_unicast_listener = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if( s_sockets.udp_unicast_listener == -1 )
    {
        CIPSTER_TRACE_ERR( "error allocating UDP unicast listener socket, %d\n",
                errno );
        goto error;
    }

    // Activates address reuse
    if( setsockopt( s_sockets.udp_unicast_listener, SOL_SOCKET, SO_REUSEADDR,
            (char*) &one, sizeof(one) ) )
    {
        CIPSTER_TRACE_ERR(
                "error setting socket option SO_REUSEADDR on udp_unicast_listener\n" );
        goto error;
    }

    {
        SockAddr address( kEIP_Reserved_Port, ntohl( c.ip_address ) );

        if( bind( s_sockets.udp_unicast_listener, address, SADDRZ ) )
        {
            CIPSTER_TRACE_ERR(
                "error with udp_unicast_listener bind: %s\n",
                strerrno().c_str() );
            goto error;
        }
    }

    //-----</udp_unicast_listener>---------------------------------------------

    // switch socket in listen mode
    if( listen( s_sockets.tcp_listener, MAX_NO_OF_TCP_SOCKETS ) )
    {
        CIPSTER_TRACE_ERR( "%s: error with listen: %s\n",
                __func__, strerrno().c_str() );
        goto error;
    }

    // add the listener socket to the master set
    master_set_add( "TCP", s_sockets.tcp_listener );
    master_set_add( "UDP", s_sockets.udp_unicast_listener );
    master_set_add( "UDP", s_sockets.udp_local_broadcast_listener );
    master_set_add( "UDP", s_sockets.udp_global_broadcast_listener );

    CIPSTER_TRACE_INFO( "%s:\n"
        " tcp_listener                 :%d\n"
        " udp_unicast_listener         :%d\n"
        " udp_local_broadcast_listener :%d\n"
        " udp_global_broadcast_listener:%d\n"
        " added to master_set\n",
        __func__,
        s_sockets.tcp_listener,
        s_sockets.udp_unicast_listener,
        s_sockets.udp_local_broadcast_listener,
        s_sockets.udp_global_broadcast_listener
        );

    s_last_usecs = usecs_now();         // initialize time keeping
    s_sockets.elapsed_time_usecs = 0;
    s_sockets.tcp_inactivity_usecs = 0;

    return kEipStatusOk;

error:
    NetworkHandlerFinish();
    return kEipStatusError;
}


EipStatus NetworkHandlerProcessOnce()
{
    read_set = master_set;

    timeval tv;

    // On  Linux,  select()  modifies timeout to reflect the amount of time
    // not slept; most other implementations do not do this.
    // Consider timeout to be undefined after select() returns.
    tv.tv_sec  = 0;
    tv.tv_usec = 0;

    int ready_count = select( highest_socket_handle + 1, &read_set, 0, 0, &tv );

    if( ready_count == -1 )
    {
        if( errno == EINTR )
        {
            /*  we have somehow been interrupted. The default behavior is to
                go back into the select loop.
            */
            return kEipStatusOk;
        }
        else
        {
            CIPSTER_TRACE_ERR( "%s: error with select: '%s'\n",
                    __func__, strerrno().c_str() );
            return kEipStatusError;
        }
    }

    if( ready_count > 0 )
    {
        // CIPSTER_TRACE_INFO( "%s: highest_socket_handle:%d ready_count:%d\n",
        //   __func__, highest_socket_handle, ready_count );

        CheckAndHandleTcpListenerSocket();
        CheckAndHandleUdpUnicastSocket();
        CheckAndHandleUdpLocalBroadcastSocket();
        CheckAndHandleUdpGlobalBroadcastSocket();
        checkAndHandleUdpSockets();

        // if it is still checked it is a TCP receive
        for( int socket = 0; socket <= highest_socket_handle;  ++socket )
        {
            if( checkSocketSet( socket ) )
            {
                if( kEipStatusError == HandleDataOnTcpSocket( socket ) )
                {
                    CIPSTER_TRACE_INFO( "%s[%d]: calling CloseBySocket()\n",
                        __func__, socket );
                    SessionMgr::CloseBySocket( socket );
                }
            }
        }
    }

    unsigned now = usecs_now();
    unsigned elapsed_usecs = now - s_last_usecs;

    s_last_usecs = now;

    s_sockets.elapsed_time_usecs    += elapsed_usecs;
    s_sockets.tcp_inactivity_usecs  += elapsed_usecs;

    g_current_usecs += elapsed_usecs;   // accumulate into 64 bits.

    /*  Call ManageConnections() if the elapsed_time_usecs is greater than
        kCIPsterTimerTickInMicroSeconds.  If more than once cycle
        was missed, call it more than once so internal time management
        functions can expect each call to represent kCIPsterTimerTickInMicroSeconds.
        This will compensate for jitter in how frequently NetworkHandlerProcessOnce()
        is called.  But please try and call it at least slightly more frequently
        than every kCIPsterTimerTickInMicroSeconds.
    */
    while( s_sockets.elapsed_time_usecs >= kCIPsterTimerTickInMicroSeconds )
    {
        ManageConnections();

        // Since we qualified this in the while() test, this will never go
        // below zero.
        s_sockets.elapsed_time_usecs -= kCIPsterTimerTickInMicroSeconds;
    }

    // process AgeInactivity every 1/2 second.  This is fine because
    // CipTCPIPInterfaceInstance::inactivity_timeout_secs is in seconds so
    // respecting the timeout within 1/2 is sufficient.
    const unsigned INACTIVITY_CHECK_PERIOD_USECS = 500000;

    if( s_sockets.tcp_inactivity_usecs >= INACTIVITY_CHECK_PERIOD_USECS )
    {
        s_sockets.tcp_inactivity_usecs -= INACTIVITY_CHECK_PERIOD_USECS;

        SessionMgr::AgeInactivity();
    }

    return kEipStatusOk;
}


EipStatus NetworkHandlerFinish()
{
    CloseSocket( s_sockets.tcp_listener );
    CloseSocket( s_sockets.udp_unicast_listener );
    CloseSocket( s_sockets.udp_local_broadcast_listener );
    CloseSocket( s_sockets.udp_global_broadcast_listener );

    return kEipStatusOk;
}


void SendUdpData( const SockAddr& aSockAddr, int aSocket, BufReader aOutput )
{
    int sent_count = sendto( aSocket, (char*) aOutput.data(), aOutput.size(), 0,
                        aSockAddr, SADDRZ );

#if 0
    CIPSTER_TRACE_INFO( "%s[%d]: %d bytes to:%s:%d\n",
        __func__,
        aSocket,
        (int) aOutput.size(),
        aSockAddr.AddrStr().c_str(),
        aSockAddr.Port()
        );
#endif

    if( sent_count < 0 )
    {
        socket_error se;

        CIPSTER_TRACE_ERR( "%s[%d]: sendto(): '%s'\n",
                __func__, aSocket, se.what() );

        throw se;
    }

    // This is highly unlikely to occur after any use of aOutput is trimmed to
    // a supported UDP maximum size.  i.e. it will be a testing bug, should not
    // be a runtime bug.  More likely all errors go through sent_count < 0 above.
    // Could probably comment this whole block out.
    if( sent_count != aOutput.size() )
    {
        std::string msg = StrPrintf(
                "%s[%d]: data_length != sent_count mismatch, sent %d of %d\n",
                __func__, aSocket, sent_count, (int) aOutput.size() );

        // Since the OS probably has no "errno" for this situation, we use -1.
        socket_error se( msg, -1 );
        throw se;
    }
}

//-----<UdpSocketMgr<-----------------------------------------------------------

UdpSocket* UdpSocketMgr::GrabSocket( const SockAddr& aSockAddr, const SockAddr* aMulticast )
{
    UdpSocket* iface = find( aSockAddr, m_sockets );

    if( iface )
    {
        //CIPSTER_TRACE_INFO( "%s: found %s:%d\n", __func__, aSockAddr.AddrStr().c_str(), aSockAddr.Port() );

        ++iface->m_ref_count;
    }
    else
    {
        int sock = createSocket( aSockAddr );

        if( sock == kSocketInvalid )
        {
            CIPSTER_TRACE_ERR( "%s: returning NULL\n", __func__ );
            return NULL;
        }

        iface = alloc( aSockAddr, sock );
        m_sockets.push_back( iface );

        //CIPSTER_TRACE_INFO( "%s: alloc %s:%d\n", __func__, aSockAddr.AddrStr().c_str(), aSockAddr.Port() );
    }

    if( aMulticast )
    {
        UdpSocket* group = find( *aMulticast, m_multicast );

        if( group )
        {
            ++group->m_ref_count;
        }
        else
        {
            // Note that you can join several groups to the same socket, not just one.
            ip_mreq mreq;

            mreq.imr_multiaddr.s_addr = htonl( aMulticast->Addr() );
            mreq.imr_interface.s_addr = htonl( iface->m_sockaddr.Addr() );

            if( setsockopt( iface->m_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                    (char*) &mreq,  sizeof mreq ) )
            {
                // https://developerweb.net/viewtopic.php?id=5784
                // https://stackoverflow.com/questions/3187919/error-no-such-device-in-call-setsockopt

                socket_error    serror;

                if( serror.error_code == ENXIO )        // "No such device or address"
                {
#if 0
                    in_addr ip;
                    ip.s_addr = CipTCPIPInterfaceClass::IpAddress( 1 );
                    setsockopt( iface->m_socket, IPPROTO_IP, IP_MULTICAST_IF, (char*)&ip, sizeof ip );
#else
                    CIPSTER_TRACE_ERR(
                        "%s[%d]: unable to add group %s to interface %s. Please add:\n"
                        " 'route add -net 224.0.0.0 netmask 224.0.0.0 eth0'\n"
                        "OR a 'default route' to an init file.\n",
                        __func__,
                        iface->m_socket,
                        aMulticast->AddrStr().c_str(),
                        iface->m_sockaddr.AddrStr().c_str()
                        );
#endif
                }
                else
                {
                    CIPSTER_TRACE_ERR(
                        "%s[%d]: unable to add group %s to interface %s. Error:'%s'\n",
                        __func__,
                        iface->m_socket,
                        aMulticast->AddrStr().c_str(),
                        iface->m_sockaddr.AddrStr().c_str(),
                        serror.what()
                        );
                }

                // reverse m_ref_count increment above, and keep socket close policy in one place.
                ReleaseSocket( iface );
                return NULL;
            }

            CIPSTER_TRACE_ERR( "%s[%d]: added group %s membership to interface %s OK.\n",
                __func__,
                iface->m_socket,
                aMulticast->AddrStr().c_str(),
                iface->m_sockaddr.AddrStr().c_str()
                );

            char loop = 0;
            setsockopt( iface->m_socket, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, 1 );

            group = alloc( *aMulticast, iface->m_socket );
            group->m_underlying = iface;
            m_multicast.push_back( group );
        }

        return group;
    }

    return iface;
}


bool UdpSocketMgr::ReleaseSocket( UdpSocket* aUdpSocket )
{
    sock_iter   it;
    UdpSocket*  group = NULL;
    UdpSocket*  iface = NULL;

#if defined(DEBUG) && 0
    CIPSTER_TRACE_INFO( "%s: %s:%d\n",
            __func__,
            aUdpSocket->m_sockaddr.AddrStr().c_str(),
            aUdpSocket->m_sockaddr.Port()
            );
#endif

    if( aUdpSocket->m_sockaddr.IsMulticast() )
    {
        for( it = m_multicast.begin();  it != m_multicast.end();  ++it )
        {
            if( *it == aUdpSocket )
            {
                group = *it;
                break;
            }
        }
    }

    if( group )
    {
        iface = group->m_underlying;

        if( --group->m_ref_count <= 0 )
        {
            m_multicast.erase( it );
            UdpSocketMgr::free( group );

            ip_mreq mreq;

            mreq.imr_multiaddr.s_addr = htonl( group->m_sockaddr.Addr() );
            mreq.imr_interface.s_addr = htonl( iface->m_sockaddr.Addr() );

            if( setsockopt( iface->m_socket, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                    (char*) &mreq, sizeof(struct ip_mreq) ) )
            {
                CIPSTER_TRACE_WARN(
                    "%s: unable to drop membership of group %s from interface %s\n",
                    __func__,
                    group->m_sockaddr.AddrStr().c_str(),
                    iface->m_sockaddr.AddrStr().c_str() );
            }
            else
            {
                CIPSTER_TRACE_INFO(
                    "%s: dropped group %s:%d from interface %s:%d OK.\n",
                    __func__,
                    group->m_sockaddr.AddrStr().c_str(), group->m_sockaddr.Port(),
                    iface->m_sockaddr.AddrStr().c_str(), iface->m_sockaddr.Port()
                    );
            }
        }
    }
    else
    {
        for( it = m_sockets.begin();  it != m_sockets.end();  ++it )
        {
            if( *it == aUdpSocket )
            {
                iface = aUdpSocket;
                break;
            }
        }
    }

    if( !iface )
    {
        CIPSTER_TRACE_ERR( "%s[%d]: ERROR releasing %s:%d\n",
            __func__, aUdpSocket->m_socket,
            aUdpSocket->m_sockaddr.AddrStr().c_str(),
            aUdpSocket->m_sockaddr.Port()
            );
        return false;
    }

    if( --iface->m_ref_count <= 0 )
    {
#if 0   // should work with or without this:
        master_set_rem( iface->m_socket );
        CloseSocket( iface->m_socket );
        m_sockets.erase( it );
        UdpSocketMgr::free( iface );
#endif
    }

    return true;
}


UdpSocket* UdpSocketMgr::find( const SockAddr& aSockAddr, const sockets& aList )
{
    for( sock_citer it = aList.begin();  it != aList.end();  ++it )
        if( (*it)->m_sockaddr == aSockAddr )
            return *it;

    return NULL;
}


// @see: https://stackoverflow.com/questions/6140734/cannot-bind-to-multicast-address-windows
int UdpSocketMgr::createSocket( const SockAddr& aSockAddr )
{
   int udp_sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

   if( udp_sock == kSocketInvalid )
   {
        CIPSTER_TRACE_ERR( "%s: errno creating UDP socket: '%s'\n",
            __func__, strerrno().c_str() );
        goto exit;
    }

    SocketAsync( udp_sock );

    /*  I know of no place where we have to assign an ip address or port more
        than once in the UDP IO connection realm.  The point of UdpSocketMgr
        class is to share sockets when they can be shared.
    const int one = 1;

    if( setsockopt( udp_sock, SOL_SOCKET, SO_REUSEADDR, (char*) &one, sizeof(one) ) )
    {
        CIPSTER_TRACE_ERR(
            "%s[%d]: errorno with SO_REUSEADDR: '%s'\n",
            __func__, udp_sock, strerrno().c_str() );

        goto close_and_exit;
    }
    */

    if( bind( udp_sock, aSockAddr, SADDRZ ) )
    {
        CIPSTER_TRACE_ERR( "%s[%d]: bind(%s:%d) errno: '%s'\n",
            __func__,
            udp_sock,
            aSockAddr.AddrStr().c_str(),
            aSockAddr.Port(),
            strerrno().c_str()
            );
        goto close_and_exit;
    }

    CIPSTER_TRACE_INFO( "%s[%d]: bound on %s\n", __func__, udp_sock, aSockAddr.Format().c_str() );

    {
        char ttl = CipTCPIPInterfaceClass::TTL(1);
        if( 1 != ttl )
        {
            // set TTL for socket using a byte sized value
            if( setsockopt( udp_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, 1 ) )
            {
                CIPSTER_TRACE_ERR(
                    "%s[%d]: could not set TTL to: %d, errno: '%s'\n",
                    __func__, udp_sock, ttl, strerrno().c_str() );

                goto close_and_exit;
            }
        }
    }

    master_set_add( "UDP", udp_sock );

exit:
    return udp_sock;

close_and_exit:

    CloseSocket( udp_sock );
    return kSocketInvalid;
}


UdpSocket* UdpSocketMgr::alloc( const SockAddr& aSockAddr, int aSocket )
{
    UdpSocket* result;

    if( m_free.size() )
    {
        result = m_free.back();

        m_free.pop_back();

        new(result) UdpSocket( aSockAddr, aSocket );    // in place construction
    }
    else
    {
        result = new UdpSocket( aSockAddr, aSocket );
    }

    return result;
}


void UdpSocketMgr::free( UdpSocket* aUdpSocket )
{
    m_free.push_back( aUdpSocket );
}


UdpSocketMgr::sockets UdpSocketMgr::m_sockets;
UdpSocketMgr::sockets UdpSocketMgr::m_multicast;
UdpSocketMgr::sockets UdpSocketMgr::m_free;

//-----</UdpSocketMgr>---------------------------------------------------------


#if defined(DEBUG)
void byte_dump( const char* aPrompt, uint8_t* aBytes, int aCount )
{
    int len = printf( "%s:", aPrompt );

    for( int i = 0; i < aCount;  ++i )
    {
        if( i && !( i % 16 ) )
            printf( "\n%*s", len, "" );

        printf( " %02x", aBytes[i] );
    }

    printf( "\n" );
}
#endif

