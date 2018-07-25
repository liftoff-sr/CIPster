/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016-2018, SoftPLC Corportion.
 *
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#if defined(__linux__)
 #include <unistd.h>
 #include <sys/time.h>
 #include <time.h>
#elif defined(_WIN32)
 #include <winsock2.h>
 #include <windows.h>
 #include <ws2tcpip.h>
#endif

#include "networkhandler.h"

#include "cipster_api.h"
#include "enet_encap/encap.h"
#include "cip/cipconnectionmanager.h"
#include "trace.h"
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

typedef uint32_t    USECS;

static fd_set master_set;
static fd_set read_set;

// temporary file descriptor for select()
static int highest_socket_handle;

/** @brief This variable holds the TCP socket the received to last explicit message.
 * It is needed for opening point to point connection to determine the peer's
 * address.
 */
static int g_current_active_tcp_socket;

static USECS g_last_time_usecs;


struct NetworkStatus
{
    int     tcp_listener;
    int     udp_unicast_listener;
    int     udp_local_broadcast_listener;
    int     udp_global_broadcast_listener;
    USECS   elapsed_time_usecs;
};


static NetworkStatus s_sockets;


const std::string strerrno()
{
#if defined(__linux__)
    char    buf[256];

    // mingw seems not to have this in its libraries, although it is in headers.
    return strerror_r( errno, buf, sizeof buf );

#elif defined(_WIN32)
    return strerror( errno );
#endif
}


static void master_set_add( const char* aType, int aSocket )
{
    CIPSTER_TRACE_INFO( "%s: %s socket %d\n", __func__, aType, aSocket );
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
    CIPSTER_TRACE_INFO( "%s: socket %d\n", __func__, aSocket );

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
        CIPSTER_TRACE_INFO( "%s: %d\n", __func__, aSocket );

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
    struct sockaddr_in from_address;

    socklen_t from_address_length;

    // see if this is an unsolicited inbound UDP message
    if( CheckSocketSet( s_sockets.udp_unicast_listener ) )
    {
        from_address_length = sizeof(from_address);

        CIPSTER_TRACE_STATE(
                "%s: unsolicited UDP message on EIP unicast socket\n", __func__ );

        // Handle UDP broadcast messages
        int received_size = recvfrom( s_sockets.udp_unicast_listener,
                (char*) s_packet,
                sizeof(s_packet),
                0, (struct sockaddr*) &from_address,
                &from_address_length );

        if( received_size <= 0 ) // got error
        {
            CIPSTER_TRACE_ERR(
                    "%s: error on recvfrom UDP unicast port: %s\n",
                    __func__,
                    strerrno().c_str() );
            return;
        }

        CIPSTER_TRACE_INFO( "%s: data received on UDP unicast:\n", __func__ );

        int reply_length = Encapsulation::HandleReceivedExplictUdpData(
                s_sockets.udp_unicast_listener, &from_address,
                BufReader( s_packet, received_size ),
                BufWriter( s_packet, sizeof s_packet ), true );

        if( reply_length > 0 )
        {
            // if the active socket matches a registered UDP callback, handle a UDP packet
            int sent_count = sendto( s_sockets.udp_unicast_listener,
                        (char*) s_packet, reply_length, 0,
                        (struct sockaddr*) &from_address, sizeof(from_address) );

            CIPSTER_TRACE_INFO( "%s: sent %d reply bytes.  line:%d\n",
                __func__, sent_count, __LINE__ );

            if( sent_count != reply_length )
            {
                CIPSTER_TRACE_INFO(
                        "%s: UDP unicast response was not fully sent\n", __func__ );
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

    // see if this is a connection request to the TCP listener
    if( CheckSocketSet( s_sockets.tcp_listener ) )
    {
        CIPSTER_TRACE_INFO( "%s: new TCP connection\n", __func__ );

        new_socket = accept( s_sockets.tcp_listener, NULL, NULL );

        if( new_socket == -1 )
        {
            CIPSTER_TRACE_ERR( "%s: error on accept: %s\n",
                    __func__, strerrno().c_str() );
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
    sockaddr_in from_address;
    socklen_t   from_address_length;

    // see if this is an unsolicited inbound UDP message
    if( CheckSocketSet( s_sockets.udp_local_broadcast_listener ) )
    {
        from_address_length = sizeof(from_address);

        CIPSTER_TRACE_STATE(
                "%s: unsolicited UDP message on EIP local broadcast socket\n",
                __func__ );

        // Handle UDP broadcast messages
        int received_size = recvfrom( s_sockets.udp_local_broadcast_listener,
                (char*) s_packet,
                sizeof(s_packet),
                0, (sockaddr*) &from_address,
                &from_address_length );

        if( received_size <= 0 ) // got error
        {
            CIPSTER_TRACE_ERR(
                    "%s: error on recvfrom UDP local broadcast port: %s\n",
                    __func__, strerrno().c_str() );
            return;
        }

        CIPSTER_TRACE_INFO( "Data received on UDP:\n" );

        int reply_length = Encapsulation::HandleReceivedExplictUdpData(
                s_sockets.udp_local_broadcast_listener, &from_address,
                BufReader( s_packet, received_size ),
                BufWriter( s_packet, sizeof s_packet ), false );

        if( reply_length > 0 )
        {
            // if the active socket matches a registered UDP callback, handle a UDP packet
            int sent_count = sendto( s_sockets.udp_local_broadcast_listener,
                        (char*) s_packet, reply_length, 0,
                        (sockaddr*) &from_address, sizeof(from_address) );

            CIPSTER_TRACE_INFO( "%s: sent %d reply bytes. line %d\n",
                __func__, sent_count, __LINE__ );

            if( sent_count != reply_length )
            {
                CIPSTER_TRACE_INFO(
                        "%s: UDP response was not fully sent\n", __func__ );
            }
        }
    }
}


void CheckAndHandleUdpGlobalBroadcastSocket()
{
    sockaddr_in from_address;
    socklen_t   from_address_length;

    // see if this is an unsolicited inbound UDP message
    if( CheckSocketSet( s_sockets.udp_global_broadcast_listener ) )
    {
        from_address_length = sizeof(from_address);

        CIPSTER_TRACE_STATE(
                "%s: unsolicited UDP message on EIP global broadcast socket\n",
                __func__ );

        // Handle UDP broadcast messages
        int received_size = recvfrom( s_sockets.udp_global_broadcast_listener,
                (char*) s_packet,
                sizeof(s_packet),
                0, (struct sockaddr*) &from_address,
                &from_address_length );

        if( received_size <= 0 ) // got error
        {
            CIPSTER_TRACE_ERR(
                    "%s: error on recvfrom UDP global broadcast port: %s\n",
                    __func__, strerrno().c_str() );
            return;
        }

        CIPSTER_TRACE_INFO( "%s: data received on global broadcast UDP:\n", __func__ );

        int reply_length = Encapsulation::HandleReceivedExplictUdpData(
                s_sockets.udp_global_broadcast_listener, &from_address,
                BufReader( s_packet, received_size ),
                BufWriter( s_packet, sizeof s_packet ), false );

        if( reply_length > 0 )
        {
            // if the active socket matches a registered UDP callback, handle a UDP packet
            int sent_count = sendto( s_sockets.udp_global_broadcast_listener,
                        (char*) s_packet, reply_length, 0,
                        (struct sockaddr*) &from_address, sizeof(from_address) );

            CIPSTER_TRACE_INFO( "%s: sent %d reply bytes. line %d:\n",
                __func__, sent_count, __LINE__ );

            if( sent_count != reply_length )
            {
                CIPSTER_TRACE_INFO(
                        "%s: UDP response was not fully sent\n",
                        __func__ );
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
    sockaddr_in from_address;
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
                    (struct sockaddr*) &from_address, &from_address_length );

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

            CIPSTER_TRACE_INFO( "%s[%d]: got %d UDP bytes for consuming connection %d\n",
                    __func__, conn->ConsumingSocket(),
                    received_size, conn->consuming_connection_id );

            CipConnMgrClass::HandleReceivedConnectedData( &from_address,
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

    if( num_read < ENCAPSULATION_HEADER_LENGTH )
    {
        return kEipStatusError;
    }

    g_current_active_tcp_socket = aSocket;

    int replyz = Encapsulation::HandleReceivedExplictTcpData( aSocket,
                        BufReader( s_packet, num_read ),
                        BufWriter( s_packet, sizeof s_packet ) );

    g_current_active_tcp_socket = -1;

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
    }

    return kEipStatusOk;
}

/// Return a monotonically increasing usecs time that wraps around after overflow.
static USECS usecs_now()
{
#if defined(__linux__)
    struct timespec	now;

    clock_gettime( CLOCK_MONOTONIC, &now );

    USECS usecs = USECS( now.tv_nsec/1000 + now.tv_sec * 1000000 );

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

    USECS usecs = USECS( performance_counter.QuadPart * 1000000LL / clock.frequency );
#endif

    return usecs;
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

    struct sockaddr_in address;

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

    memset( &address, 0, sizeof( address ) );
    address.sin_family = AF_INET;
    address.sin_port = htons( kEthernet_IP_Port );
    address.sin_addr.s_addr = c.ip_address;

    // bind the new socket to port 0xAF12 (CIP)
    if( bind( s_sockets.tcp_listener, (sockaddr*) &address, sizeof( address ) ) == -1 )
    {
        CIPSTER_TRACE_ERR( "%s: bind(%s) error for tcp_listener: %s\n",
            __func__, inet_ntoa( address.sin_addr ), strerrno().c_str() );
        goto error;
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

    memset( &address, 0, sizeof address );
    address.sin_family = AF_INET;
    address.sin_port = htons( kEthernet_IP_Port );
    address.sin_addr.s_addr = htonl( INADDR_BROADCAST );

    if( ( bind( s_sockets.udp_global_broadcast_listener,
                  (sockaddr*) &address, sizeof(address) ) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
                "error with global broadcast UDP bind: %s\n",
                strerrno().c_str() );
        goto error;
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

    memset( &address, 0, sizeof( address ) );
    address.sin_family = AF_INET;
    address.sin_port   = htons( kEthernet_IP_Port );

    address.sin_addr.s_addr = c.ip_address | ~c.network_mask;

    if( ( bind( s_sockets.udp_local_broadcast_listener,
                  (sockaddr*) &address, sizeof(address) ) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
                "error with udp_local_broadcast_listener bind: %s\n",
                strerrno().c_str() );
        goto error;
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

    memset( &address, 0, sizeof( address ) );
    address.sin_family = AF_INET;
    address.sin_port = htons( kEthernet_IP_Port );
    address.sin_addr.s_addr = c.ip_address;

    if( ( bind( s_sockets.udp_unicast_listener,
                  (sockaddr*) &address, sizeof(address) ) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
            "error with udp_unicast_listener bind: %s\n",
            strerrno().c_str() );
        goto error;
    }

    //-----</udp_unicast_listener>---------------------------------------------

    // switch socket in listen mode
    if( ( listen( s_sockets.tcp_listener, MAX_NO_OF_TCP_SOCKETS ) ) == -1 )
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

    g_last_time_usecs = usecs_now();    // initialize time keeping
    s_sockets.elapsed_time_usecs = 0;

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
        if( EINTR == errno )
        {
            /*  we have somehow been interrupted. The default behavior is to
                go back into the select loop.
            */
            return kEipStatusOk;
        }
        else
        {
            CIPSTER_TRACE_ERR( "%s: error with select: %s\n",
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
                    if( !CloseSession( socket ) )
                    {
                        // CloseSession could not find the socket.
                        CloseSocket( socket );
                    }
                }
            }
        }
    }

    USECS now = usecs_now();
    s_sockets.elapsed_time_usecs += now - g_last_time_usecs;
    g_last_time_usecs = now;

    /*  check if we had been not able to update the connection manager for
        several CIPSTER_TIMER_TICK.
        This should compensate the jitter of the windows timer
    */
    while( s_sockets.elapsed_time_usecs >= kOpenerTimerTickInMicroSeconds )
    {
        ManageConnections();
        s_sockets.elapsed_time_usecs -= kOpenerTimerTickInMicroSeconds;
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


EipStatus SendUdpData( struct sockaddr_in* address, int socket, BufReader aOutput )
{
    int sent_count = sendto( socket, (char*) aOutput.data(), aOutput.size(), 0,
            (struct sockaddr*) address, sizeof(*address) );

    CIPSTER_TRACE_INFO( "%s: socket:%d sending %d bytes\n", __func__, socket, (int) aOutput.size() );

    if( sent_count < 0 )
    {
        CIPSTER_TRACE_ERR( "%s: error with sendto in sendUDPData: %s\n",
                __func__, strerrno().c_str() );

        return kEipStatusError;
    }

    if( sent_count != aOutput.size() )
    {
        CIPSTER_TRACE_WARN(
                "%s: data_length != sent_count mismatch, sent %d of %d\n",
                __func__, sent_count, (int) aOutput.size() );

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


int CreateUdpSocket( UdpCommuncationDirection communication_direction,
        struct sockaddr_in* socket_data )
{
   int option_value = 1;

   int new_socket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

   if( new_socket == -1 )
   {
        CIPSTER_TRACE_ERR( "%s: cannot create UDP socket: %s\n",
                __func__, strerrno().c_str() );
        return kEipInvalidSocket;
    }

    CIPSTER_TRACE_INFO( "%s: UDP socket %d\n", __func__, new_socket );

    // check if it is sending or receiving
    if( communication_direction == kUdpConsuming )
    {
        if( setsockopt( new_socket, SOL_SOCKET, SO_REUSEADDR, (char*) &option_value,
                    sizeof(option_value) ) == -1 )
        {
            CIPSTER_TRACE_ERR(
                    "%s: error setting socket option SO_REUSEADDR on consuming udp socket\n",
                    __func__ );
            return kEipStatusError;
        }

        if( bind( new_socket, (sockaddr*) socket_data, sizeof *socket_data ) == -1 )
        {
            CIPSTER_TRACE_ERR( "%s: error on bind consuming udp: %s\n",
                    __func__, strerrno().c_str() );
            return kEipInvalidSocket;
        }

        CIPSTER_TRACE_INFO( "%s: bind UDP consuming socket %d\n", __func__, new_socket );
    }

    else    // we have a producing udp socket
    {
#if 1
        if( setsockopt( new_socket, SOL_SOCKET, SO_REUSEADDR, (char*) &option_value,
                    sizeof(option_value) ) == -1 )
        {
            CIPSTER_TRACE_ERR(
                    "%s: error setting socket option SO_REUSEADDR on producing udp socket\n",
                    __func__ );
            return kEipStatusError;
        }

        sockaddr_in     me;

        memset( &me, 0, sizeof me );

        me.sin_family = AF_INET;
        me.sin_addr.s_addr = htonl(INADDR_ANY); // CipTCPIPInterfaceClass::IpAddress( 1 );
        me.sin_port = socket_data->sin_port;

        if( bind( new_socket, (sockaddr*) &me, sizeof me ) == -1 )
        {
            CIPSTER_TRACE_ERR( "%s: error on bind producing udp: %s\n",
                    __func__, strerrno().c_str() );
            return kEipInvalidSocket;
        }

        CIPSTER_TRACE_INFO( "%s: bind UDP producing socket %d\n", __func__, new_socket );

#endif
        // but is it multicast?
        if( socket_data->sin_addr.s_addr ==
                CipTCPIPInterfaceClass::MultiCast( 1 ).starting_multicast_address )
        {
            uint8_t ttl = CipTCPIPInterfaceClass::TTL(1);
            if( 1 != ttl )
            {
                // we need to set a TTL value for the socket using a byte value
                if( setsockopt( new_socket, IPPROTO_IP, IP_MULTICAST_TTL, (char*) &ttl, 1 ) < 0 )
                {
                    CIPSTER_TRACE_ERR(
                            "%s: could not set the TTL to: %d, error: %s\n",
                            __func__, ttl, strerrno().c_str() );
                    return kEipInvalidSocket;
                }
            }
        }
    }

    if( communication_direction == kUdpConsuming || 0 == socket_data->sin_addr.s_addr )
    {
        sockaddr_in peer_address;
        socklen_t   peer_address_length = sizeof peer_address;

        // we have a peer to peer producer or a consuming connection
        if( getpeername( g_current_active_tcp_socket,
                    (sockaddr*) &peer_address, &peer_address_length ) < 0 )
        {
            CIPSTER_TRACE_ERR( "%s: could not get peername: %s\n",
                    __func__, strerrno().c_str() );
            return kEipInvalidSocket;
        }

        // store the originators address
        socket_data->sin_addr.s_addr = peer_address.sin_addr.s_addr;
    }

    if( communication_direction == kUdpConsuming )
        master_set_add( "UDP", new_socket );

    return new_socket;
}

