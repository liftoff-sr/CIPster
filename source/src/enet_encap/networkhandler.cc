/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#include "networkhandler.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#if defined(__linux__)
 #include <unistd.h>
 #include <sys/time.h>
 #include <time.h>
#endif


#include <cipster_api.h>
#include <trace.h>
#include "encap.h"
#include "cip/cipconnectionmanager.h"
#include "cip/ciptcpipinterface.h"


/** @brief The number of bytes used for the Ethernet message buffer on
 * the PC port. For different platforms it may make sense to
 * have more than one buffer.
 *
 *  This buffer size will be used for any received message.
 *  The same buffer is used for the replied explicit message.
 */
static EipByte s_packet[CIPSTER_ETHERNET_BUFFER_SIZE];


#define MAX_NO_OF_TCP_SOCKETS           10

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


std::string strerrno()
{
#if defined(__linux__)
    char    buf[256];

    // mingw seems not to have this in its libraries, although it is in headers.
    return strerror_r( errno, buf, sizeof buf );

#elif defined(_WIN32)
    return strerror( errno );
#endif
}


/// Return a monotonically increasing usecs time that wraps around
/// after overflow.  Just use 32 bits here, 64 bits are kept in
/// g_current_usecs which is global.  This a cross platform concern which
/// will make implementing this function easier under other OS's.
static unsigned usecs_now()
{
#if defined(__linux__)
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


void CloseSocket( int aSocket )
{
    if( aSocket >= 0 )
    {
        CIPSTER_TRACE_INFO( "%s[%d]\n", __func__, aSocket );

        master_set_rem( aSocket );

#if defined(__linux__)
        shutdown( aSocket, SHUT_RDWR );
        close( aSocket );
#elif defined(_WIN32)
        closesocket( aSocket );
#endif
    }
}


/** @brief check if the given socket is set in the read set
 * @param socket The socket to check
 * @return
 */
bool CheckSocketSet( int socket )
{
    bool return_value = false;

    if( FD_ISSET( socket, &read_set ) )
    {
        if( FD_ISSET( socket, &master_set ) )
        {
            return_value = true;
        }
        else
        {
            CIPSTER_TRACE_INFO( "socket: %d closed with pending message\n", socket );
        }

        // remove it from the read set so that later checks will not find it
        FD_CLR( socket, &read_set );
    }

    return return_value;
}


void CheckAndHandleUdpUnicastSocket()
{
    SockAddr    from_address;
    socklen_t   from_address_length;

    // see if this is an unsolicited inbound UDP message
    if( CheckSocketSet( s_sockets.udp_unicast_listener ) )
    {
        from_address_length = sizeof(from_address);

        CIPSTER_TRACE_STATE(
                "%s[%d]: unsolicited UDP message on EIP unicast socket\n",
                __func__, s_sockets.udp_unicast_listener );

        // Handle UDP broadcast messages
        int received_size = recvfrom( s_sockets.udp_unicast_listener,
                (char*) s_packet,
                sizeof(s_packet),
                0, from_address,  &from_address_length );

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
                s_sockets.udp_unicast_listener, from_address,
                BufReader( s_packet, received_size ),
                BufWriter( s_packet, sizeof s_packet ), true );

        if( reply_length > 0 )
        {
            // if the active socket matches a registered UDP callback, handle a UDP packet
            int sent_count = sendto( s_sockets.udp_unicast_listener,
                        (char*) s_packet, reply_length, 0,
                        from_address, sizeof(from_address) );

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
    if( CheckSocketSet( s_sockets.tcp_listener ) )
    {
        new_socket = accept( s_sockets.tcp_listener, NULL, NULL );

        CIPSTER_TRACE_INFO( "%s[%d]: new TCP connection\n", __func__, new_socket );

        if( new_socket == kSocketInvalid )
        {
            CIPSTER_TRACE_ERR( "%s: error on accept: %s\n",
                    __func__, strerrno().c_str() );
            return;
        }

        EncapError result = ServerSessionMgr::RegisterTcpConnection( new_socket );

        if( result != kEncapErrorSuccess )
        {
            CIPSTER_TRACE_ERR(
                "%s: rejecting incoming TCP connection since count exceeds\n"
                " CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS (= %d)\n",
                    __func__,
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
    SockAddr    from_address;
    socklen_t   from_address_length;

    // see if this is an unsolicited inbound UDP message
    if( CheckSocketSet( s_sockets.udp_local_broadcast_listener ) )
    {
        from_address_length = sizeof(from_address);

        CIPSTER_TRACE_STATE(
            "%s[%d]: unsolicited UDP on local broadcast socket\n",
            __func__,
            s_sockets.udp_local_broadcast_listener
            );

        // Handle UDP broadcast messages
        int received_size = recvfrom( s_sockets.udp_local_broadcast_listener,
                (char*) s_packet,
                sizeof(s_packet),
                0, from_address,
                &from_address_length );

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
                s_sockets.udp_local_broadcast_listener, from_address,
                BufReader( s_packet, received_size ),
                BufWriter( s_packet, sizeof s_packet ), false );

        if( reply_length > 0 )
        {
            // if the active socket matches a registered UDP callback, handle a UDP packet
            int sent_count = sendto( s_sockets.udp_local_broadcast_listener,
                        (char*) s_packet, reply_length, 0,
                        from_address, sizeof(from_address) );

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
    SockAddr    from_address;
    socklen_t   from_address_length;

    // see if this is an unsolicited inbound UDP message
    if( CheckSocketSet( s_sockets.udp_global_broadcast_listener ) )
    {
        from_address_length = sizeof(from_address);

        CIPSTER_TRACE_STATE(
            "%s[%d]: unsolicited UDP on global broadcast socket\n",
            __func__, s_sockets.udp_global_broadcast_listener );

        // Handle UDP broadcast messages
        int received_size = recvfrom( s_sockets.udp_global_broadcast_listener,
                (char*) s_packet,
                sizeof(s_packet),
                0, from_address,
                &from_address_length );

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
                s_sockets.udp_global_broadcast_listener, from_address,
                BufReader( s_packet, received_size ),
                BufWriter( s_packet, sizeof s_packet ), false );

        if( reply_length > 0 )
        {
            // if the active socket matches a registered UDP callback, handle a UDP packet
            int sent_count = sendto( s_sockets.udp_global_broadcast_listener,
                        (char*) s_packet, reply_length, 0,
                        from_address, SADDRZ );

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
 * Function CheckAndHandleConsumingUdpSockets
 * checks if on one of the UDP consuming sockets data has been received
 * and if yes handles it correctly.
 */
void CheckAndHandleConsumingUdpSockets()
{
    SockAddr    from_address;
    socklen_t   from_address_length;

    CipConnBox::iterator it = g_active_conns.begin();

    // see if a message on one of the registered UDP sockets has been received
    while(  it != g_active_conns.end() )
    {
        CipConn* conn = it;

        //CIPSTER_TRACE_STATE( "%s[%d]:\n", __func__, conn->consuming_socket );

        // do this at the beginning as the close function can make the it entry invalid
        ++it;

        if( conn->ConsumingSocket() != -1  &&  CheckSocketSet( conn->ConsumingSocket() ) )
        {
            from_address_length = sizeof(from_address);

            int received_size = recvfrom(
                    conn->ConsumingSocket(),
                    (char*) s_packet, sizeof(s_packet), 0,
                    from_address, &from_address_length );

            if( 0 == received_size )
            {
                CIPSTER_TRACE_STATE( "%s[%d]: connection closed by client\n",
                    __func__, conn->ConsumingSocket() );

                conn->Close();
                continue;
            }

            if( received_size < 0 )
            {
                CIPSTER_TRACE_ERR( "%s[%d]: error on recv: %s\n",
                        __func__, conn->ConsumingSocket(), strerrno().c_str() );

                conn->Close();
                continue;
            }

            CIPSTER_TRACE_INFO( "%s[%d]: %d bytes CID:0x%08x %s:%d\n",
                    __func__,
                    conn->ConsumingSocket(),
                    received_size,
                    conn->consuming_connection_id,
                    from_address.AddrStr().c_str(),
                    from_address.Port()
                    );

            CipConnMgrClass::HandleReceivedConnectedData( from_address,
                BufReader( s_packet, received_size ) );
        }
    }
}


/**
 * Function HandleDataOnTcpSocket
 */
EipStatus HandleDataOnTcpSocket( int aSocket )
{
    int num_read = Encapsulation::ReceiveTcpMsg( aSocket,
                        BufWriter( s_packet, sizeof s_packet ) );

    //CIPSTER_TRACE_INFO( "%s[%d]: num_read:%d\n", __func__, aSocket, num_read );

    if( num_read < ENCAPSULATION_HEADER_LENGTH )
    {
        return kEipStatusError;
    }

    int replyz = Encapsulation::HandleReceivedExplicitTcpData( aSocket,
                        BufReader( s_packet, num_read ),
                        BufWriter( s_packet, sizeof s_packet ) );

    if( replyz > 0 )
    {
#if defined(DEBUG)
        byte_dump( "sTCP", s_packet, replyz );
#endif
        int sent_count = send( aSocket, (char*) s_packet, replyz, 0 );

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
                (char*) &one, sizeof(one) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
                "error setting socket option SO_REUSEADDR on tcp_listener\n" );
        goto error;
    }

    {
        SockAddr address( kEIP_Reserved_Port, ntohl( c.ip_address ) );

        // bind the new socket to port 0xAF12 (CIP)
        if( bind( s_sockets.tcp_listener, address, SADDRZ ) == -1 )
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
            (char*) &one, sizeof(one) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
                "error setting socket option SO_REUSEADDR on udp_broadcast_listener\n" );
        goto error;
    }

    // enable the UDP socket to receive broadcast messages
    if( setsockopt( s_sockets.udp_global_broadcast_listener,
            SOL_SOCKET, SO_BROADCAST, (char*) &one, sizeof(one) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
                "error with setting broadcast receive for UDP socket: %s\n",
                strerrno().c_str() );
        goto error;
    }

    {
        SockAddr address( kEIP_Reserved_Port, INADDR_BROADCAST );

        if( ( bind( s_sockets.udp_global_broadcast_listener,
                      address, SADDRZ ) ) == -1 )
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
            SOL_SOCKET, SO_REUSEADDR, (char*) &one, sizeof(one) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
                "error setting socket option SO_REUSEADDR on udp_broadcast_listener\n" );
        goto error;
    }

    {
        SockAddr address(   kEIP_Reserved_Port,
                            ntohl( c.ip_address | ~c.network_mask ) );

        if( bind( s_sockets.udp_local_broadcast_listener,
                      address, SADDRZ ) == -1 )
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
            (char*) &one, sizeof(one) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
                "error setting socket option SO_REUSEADDR on udp_unicast_listener\n" );
        goto error;
    }

    {
        SockAddr address( kEIP_Reserved_Port, ntohl( c.ip_address ) );

        if( bind( s_sockets.udp_unicast_listener,
                      address, SADDRZ ) == -1 )
        {
            CIPSTER_TRACE_ERR(
                "error with udp_unicast_listener bind: %s\n",
                strerrno().c_str() );
            goto error;
        }
    }

    //-----</udp_unicast_listener>---------------------------------------------

    // switch socket in listen mode
    if( listen( s_sockets.tcp_listener, MAX_NO_OF_TCP_SOCKETS ) == -1 )
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

    static timeval tv;

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
        CheckAndHandleTcpListenerSocket();
        CheckAndHandleUdpUnicastSocket();
        CheckAndHandleUdpLocalBroadcastSocket();
        CheckAndHandleUdpGlobalBroadcastSocket();
        CheckAndHandleConsumingUdpSockets();

        for( int socket = 0; socket <= highest_socket_handle;  ++socket )
        {
            if( CheckSocketSet( socket ) )
            {
                // if it is still checked it is a TCP receive
                if( kEipStatusError == HandleDataOnTcpSocket( socket ) )
                {
                    CIPSTER_TRACE_INFO( "%s[%d]: calling CloseBySocket()\n",
                        __func__, socket );
                    ServerSessionMgr::CloseBySocket( socket );
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
        kOpenerTimerTickInMicroSeconds.  If more than once cycle
        was missed, call it more than once so internal time management
        functions can expect each call to represent kOpenerTimerTickInMicroSeconds.
        This will compensate for jitter in how frequently NetworkHandlerProcessOnce()
        is called.  But please try and call it at least slightly more frequently
        than every kOpenerTimerTickInMicroSeconds.
    */
    while( s_sockets.elapsed_time_usecs >= kOpenerTimerTickInMicroSeconds )
    {
        ManageConnections();

        // Since we qualified this in the while() test, this will never go
        // below zero.
        s_sockets.elapsed_time_usecs -= kOpenerTimerTickInMicroSeconds;
    }

    // process AgeInactivity every 1/2 second.  This is fine because
    // CipTCPIPInterfaceInstance::inactivity_timeout_secs is in seconds so
    // respecting the timeout within 1/2 is sufficient.
    const unsigned INACTIVITY_CHECK_PERIOD_USECS = 500000;

    if( s_sockets.tcp_inactivity_usecs >= INACTIVITY_CHECK_PERIOD_USECS )
    {
        s_sockets.tcp_inactivity_usecs -= INACTIVITY_CHECK_PERIOD_USECS;

        ServerSessionMgr::AgeInactivity();
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


EipStatus SendUdpData( const SockAddr& aSockAddr, int aSocket, BufReader aOutput )
{
    int sent_count = sendto( aSocket, (char*) aOutput.data(), aOutput.size(), 0,
                        aSockAddr, SADDRZ );

    CIPSTER_TRACE_INFO( "%s[%d]: %d bytes to:%s:%d\n",
        __func__,
        aSocket,
        (int) aOutput.size(),
        aSockAddr.AddrStr().c_str(),
        aSockAddr.Port()
        );

    if( sent_count < 0 )
    {
        CIPSTER_TRACE_ERR( "%s[%d]: error with sendto in sendUDPData: %s\n",
                __func__, aSocket, strerrno().c_str() );

        return kEipStatusError;
    }

    if( sent_count != aOutput.size() )
    {
        CIPSTER_TRACE_WARN(
                "%s[%d]: data_length != sent_count mismatch, sent %d of %d\n",
                __func__, aSocket, sent_count, (int) aOutput.size() );

        return kEipStatusError;
    }

    return kEipStatusOk;
}


#if defined(DEBUG)
void byte_dump( const char* aPrompt, EipByte* aBytes, int aCount )
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


int CreateUdpSocket( UdpDirection aDirection, const SockAddr& aSockAddr )
{
   const int one = 1;

   int udp_sock = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

   if( udp_sock == kSocketInvalid )
   {
        CIPSTER_TRACE_ERR( "%s: cannot create %s UDP socket: '%s'\n",
                __func__,
                ShowUdpDirection( aDirection ),
                strerrno().c_str()
                );
        goto exit;
    }

    if( aDirection == kUdpConsuming )
    {
        if( setsockopt( udp_sock, SOL_SOCKET, SO_REUSEADDR,
                        (char*) &one, sizeof(one) ) == -1 )
        {
            CIPSTER_TRACE_ERR(
                "%s[%d]: error with SO_REUSEADDR on consuming udp socket: '%s'\n",
                __func__,
                udp_sock,
                strerrno().c_str()
                );

            goto close_and_exit;
        }

        if( bind( udp_sock, aSockAddr, SADDRZ ) == -1 )
        {
            CIPSTER_TRACE_ERR( "%s[%d]: bind(ip=%s, port=0x%x) error on consuming udp: '%s'\n",
                    __func__,
                    udp_sock,
                    aSockAddr.AddrStr().c_str(),
                    aSockAddr.Port(),
                    strerrno().c_str()
                    );
            goto close_and_exit;
            return kSocketInvalid;
        }

        CIPSTER_TRACE_INFO( "%s[%d]: consuming on %s:%d\n",
            __func__,
            udp_sock,
            aSockAddr.AddrStr().c_str(),
            aSockAddr.Port()
            );
    }

    else    // kUdpProducing
    {
#if 1
        // set source port and addr (producing) to caller's aSockAddr port

        if( setsockopt( udp_sock, SOL_SOCKET, SO_REUSEADDR,
                        (char*) &one, sizeof(one) ) == -1 )
        {
            CIPSTER_TRACE_ERR(
                "%s[%d]: error with SO_REUSEADDR on producing udp: '%s'\n",
                __func__,
                udp_sock,
                strerrno().c_str()
                );

            goto close_and_exit;
        }

        if( bind( udp_sock, (sockaddr*) &aSockAddr, SADDRZ ) == -1 )
        {
            CIPSTER_TRACE_ERR( "%s[%d]: bind(ip:%s, port:%d) error on producing udp: '%s'\n",
                    __func__,
                    udp_sock,
                    aSockAddr.AddrStr().c_str(),
                    aSockAddr.Port(),
                    strerrno().c_str()
                    );

            goto close_and_exit;
        }
#endif

        // Is it multicast?
        if( htonl( aSockAddr.Addr() ) ==
                CipTCPIPInterfaceClass::MultiCast( 1 ).starting_multicast_address )
        {
            char ttl = CipTCPIPInterfaceClass::TTL(1);
            if( 1 != ttl )
            {
                // set TTL for socket using a byte sized value
                if( setsockopt( udp_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, 1 ) < 0 )
                {
                    CIPSTER_TRACE_ERR(
                            "%s[%d]: could not set the TTL to: %d, error: %s\n",
                            __func__, udp_sock, ttl, strerrno().c_str() );

                    goto close_and_exit;
                }
            }

            CIPSTER_TRACE_INFO( "%s[%d]: producing multicast on %s:%d\n",
                __func__,
                udp_sock,
                aSockAddr.AddrStr().c_str(),
                aSockAddr.Port()
                );
        }
        else
        {
            CIPSTER_TRACE_INFO( "%s[%d]: producing point-point on %s:%d\n",
                __func__,
                udp_sock,
                aSockAddr.AddrStr().c_str(),
                aSockAddr.Port()
                );
        }
    }

    if( aDirection == kUdpConsuming )
        master_set_add( "UDP", udp_sock );

exit:
    return udp_sock;

close_and_exit:

    CloseSocket( udp_sock );
    return kSocketInvalid;
}
