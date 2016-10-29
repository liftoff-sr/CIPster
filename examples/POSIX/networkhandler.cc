/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#include "networkhandler.h"

#include "opener_api.h"
#include "enet_encap/encap.h"
#include "cip/cipconnectionmanager.h"
#include "enet_encap/endianconv.h"
#include "trace.h"
#include "cip/ciptcpipinterface.h"

// values needed from the connection manager
extern CipConn* g_active_connection_list;

/** @brief The number of bytes used for the Ethernet message buffer on
 * the PC port. For different platforms it may make sense to
 * have more than one buffer.
 *
 *  This buffer size will be used for any received message.
 *  The same buffer is used for the replied explicit message.
 */
static EipByte s_packet[1200];


#define MAX_NO_OF_TCP_SOCKETS 10

typedef unsigned  MicroSeconds;

static fd_set master_set;
static fd_set read_set;

// temporary file descriptor for select()
static int highest_socket_handle;

/** @brief This variable holds the TCP socket the received to last explicit message.
 * It is needed for opening point to point connection to determine the peer's
 * address.
 */
static int g_current_active_tcp_socket;

static MicroSeconds g_actual_time_usecs;
static MicroSeconds g_last_time_usecs;

void CheckAndHandleUdpUnicastSocket();

void CheckAndHandleUdpGlobalBroadcastSocket();


/** @brief handle any connection request coming in the TCP server socket.
 *
 */
void CheckAndHandleTcpListenerSocket();

/** @brief check if data has been received on the UDP broadcast socket and if yes handle it correctly
 *
 */
void CheckAndHandleUdpLocalBroadcastSocket();

/** @brief check if on one of the UDP consuming sockets data has been received and if yes handle it correctly
 *
 */
void CheckAndHandleConsumingUdpSockets();

/** @brief check if the given socket is set in the read set
 * @param socket The socket to check
 * @return
 */
bool CheckSocketSet( int socket );

/** @brief
 *
 */
EipStatus HandleDataOnTcpSocket( int socket );

int GetMaxSocket( int socket1, int socket2, int socket3, int socket4 );

const std::string strerrno()
{
    char    buf[256];

    return strerror_r( errno, buf, sizeof buf );
}


MicroSeconds GetMicroSeconds()
{
    struct timespec	now;

    clock_gettime( CLOCK_MONOTONIC, &now );

    MicroSeconds usecs = ((MicroSeconds)now.tv_nsec)/1000 + (MicroSeconds)now.tv_sec * 1000 * 1000;

    return usecs;
}


struct NetworkStatus
{
    int tcp_listener;
    int udp_unicast_listener;
    int udp_local_broadcast_listener;
    int udp_global_broadcast_listener;
    MicroSeconds elapsed_time_usecs;
};


static NetworkStatus g_sockets;

EipStatus NetworkHandlerInitialize()
{
    static const int one = 1;

    // clear the master an temp sets
    FD_ZERO( &master_set );
    FD_ZERO( &read_set );

    g_sockets.tcp_listener = -1;
    g_sockets.udp_unicast_listener = -1;
    g_sockets.udp_local_broadcast_listener = -1;
    g_sockets.udp_global_broadcast_listener = -1;

    struct sockaddr_in address;

    //-----<tcp_listener>-------------------------------------------

    // create a new TCP socket
    g_sockets.tcp_listener = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

    CIPSTER_TRACE_INFO( "g_sockets.tcp_listener == %d\n", g_sockets.tcp_listener );

    if( g_sockets.tcp_listener == -1 )
    {
        CIPSTER_TRACE_ERR( "error allocating socket stream listener, %d\n", errno );
        goto error;
    }

    // Activates address reuse
    if( setsockopt( g_sockets.tcp_listener, SOL_SOCKET, SO_REUSEADDR,
                &one, sizeof(one) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
                "error setting socket option SO_REUSEADDR on tcp_listener\n" );
        goto error;
    }

    memset( &address, 0, sizeof( address ) );
    address.sin_family = AF_INET;
    address.sin_port = htons( kOpenerEthernetPort );
    address.sin_addr.s_addr = interface_configuration_.ip_address;

    // bind the new socket to port 0xAF12 (CIP)
    if( bind( g_sockets.tcp_listener, (sockaddr*) &address, sizeof( address ) ) == -1 )
    {
        CIPSTER_TRACE_ERR( "error with tcp_listener bind: %s\n", strerrno().c_str() );
        goto error;
    }

    //-----<udp_global_broadcast_listner>--------------------------------------
    // create a new UDP socket
    g_sockets.udp_global_broadcast_listener = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if( g_sockets.udp_global_broadcast_listener == -1 )
    {
        CIPSTER_TRACE_ERR( "error allocating UDP broadcast listener socket, %d\n",
                errno );
        goto error;
    }

    // Activates address reuse
    if( setsockopt( g_sockets.udp_global_broadcast_listener, SOL_SOCKET, SO_REUSEADDR,
            &one, sizeof(one) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
                "error setting socket option SO_REUSEADDR on udp_broadcast_listener\n" );
        goto error;
    }

    // enable the UDP socket to receive broadcast messages
    if( setsockopt( g_sockets.udp_global_broadcast_listener,
            SOL_SOCKET, SO_BROADCAST, &one, sizeof(one) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
                "error with setting broadcast receive for UDP socket: %s\n",
                strerrno().c_str() );
        goto error;
    }

    memset( &address, 0, sizeof address );
    address.sin_family = AF_INET;
    address.sin_port = htons( kOpenerEthernetPort );
    address.sin_addr.s_addr = htonl( INADDR_BROADCAST );

    if( ( bind( g_sockets.udp_global_broadcast_listener,
                  (sockaddr*) &address, sizeof(address) ) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
                "error with global broadcast UDP bind: %s\n",
                strerrno().c_str() );
        goto error;
    }


    //-----<udp_local_broadcast_listener>---------------------------------------
    // create a new UDP socket
    g_sockets.udp_local_broadcast_listener = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if( g_sockets.udp_local_broadcast_listener == -1 )
    {
        CIPSTER_TRACE_ERR( "error allocating UDP broadcast listener socket, %d\n",
                errno );

        goto error;
    }

    // Activates address reuse
    if( setsockopt( g_sockets.udp_local_broadcast_listener,
            SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
                "error setting socket option SO_REUSEADDR on udp_broadcast_listener\n" );
        goto error;
    }


    memset( &address, 0, sizeof( address ) );
    address.sin_family = AF_INET;
    address.sin_port   = htons( kOpenerEthernetPort );
    address.sin_addr.s_addr = interface_configuration_.ip_address
                            | ~interface_configuration_.network_mask;

    if( ( bind( g_sockets.udp_local_broadcast_listener,
                  (sockaddr*) &address, sizeof(address) ) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
                "error with udp_local_broadcast_listener bind: %s\n",
                strerrno().c_str() );
        goto error;
    }


    //-----<udp_unicast_listener>----------------------------------------------
    // create a new UDP socket
    g_sockets.udp_unicast_listener = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if( g_sockets.udp_unicast_listener == -1 )
    {
        CIPSTER_TRACE_ERR( "error allocating UDP unicast listener socket, %d\n",
                errno );
        goto error;
    }

    // Activates address reuse
    if( setsockopt( g_sockets.udp_unicast_listener, SOL_SOCKET, SO_REUSEADDR,
            &one, sizeof(one) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
                "error setting socket option SO_REUSEADDR on udp_unicast_listener\n" );
        goto error;
    }

    memset( &address, 0, sizeof( address ) );
    address.sin_family = AF_INET;
    address.sin_port = htons( kOpenerEthernetPort );
    address.sin_addr.s_addr = interface_configuration_.ip_address;

    if( ( bind( g_sockets.udp_unicast_listener,
                  (sockaddr*) &address, sizeof(address) ) ) == -1 )
    {
        CIPSTER_TRACE_ERR(
            "error with udp_unicast_listener bind: %s\n",
            strerrno().c_str() );
        goto error;
    }


    //-----</udp_unicast_listener>---------------------------------------------

    // switch socket in listen mode
    if( ( listen( g_sockets.tcp_listener, MAX_NO_OF_TCP_SOCKETS ) ) == -1 )
    {
        CIPSTER_TRACE_ERR( "%s: error with listen: %s\n",
                __func__, strerrno().c_str() );
        goto error;
    }

    // add the listener socket to the master set
    FD_SET( g_sockets.tcp_listener, &master_set );
    FD_SET( g_sockets.udp_unicast_listener, &master_set );
    FD_SET( g_sockets.udp_local_broadcast_listener, &master_set );
    FD_SET( g_sockets.udp_global_broadcast_listener, &master_set );

    // keep track of the biggest file descriptor
    highest_socket_handle = GetMaxSocket(
            g_sockets.tcp_listener,
            g_sockets.udp_global_broadcast_listener,
            g_sockets.udp_local_broadcast_listener,
            g_sockets.udp_unicast_listener );

    CIPSTER_TRACE_INFO( "%s:\n"
        " tcp_listener                 :%d\n"
        " udp_unicast_listener         :%d\n"
        " udp_local_broadcast_listener :%d\n"
        " udp_global_broadcast_listener:%d\n",
        __func__,
        g_sockets.tcp_listener,
        g_sockets.udp_unicast_listener,
        g_sockets.udp_local_broadcast_listener,
        g_sockets.udp_global_broadcast_listener
        );

    g_last_time_usecs = GetMicroSeconds();    // initialize time keeping
    g_sockets.elapsed_time_usecs = 0;

    return kEipStatusOk;

error:
    NetworkHandlerFinish();
    return kEipStatusError;
}


EipStatus NetworkHandlerProcessOnce()
{
    read_set = master_set;

    struct timeval tv;

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    int ready_socket = select( highest_socket_handle + 1, &read_set, 0, 0, &tv );

    if( ready_socket == kEipInvalidSocket )
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

    if( ready_socket > 0 )
    {
        CheckAndHandleTcpListenerSocket();
        CheckAndHandleUdpUnicastSocket();
        CheckAndHandleUdpLocalBroadcastSocket();
        CheckAndHandleUdpGlobalBroadcastSocket();
        CheckAndHandleConsumingUdpSockets();

        for( int socket = 0; socket <= highest_socket_handle; socket++ )
        {
            if( CheckSocketSet( socket ) )
            {
                // if it is still checked it is a TCP receive
                if( kEipStatusError == HandleDataOnTcpSocket( socket ) ) // if error
                {
                    CloseSocket( socket );
                    CloseSession( socket ); // clean up session and close the socket
                }
            }
        }
    }

    g_actual_time_usecs = GetMicroSeconds();
    g_sockets.elapsed_time_usecs += g_actual_time_usecs - g_last_time_usecs;
    g_last_time_usecs = g_actual_time_usecs;

    /*  check if we had been not able to update the connection manager for
        several CIPSTER_TIMER_TICK.
        This should compensate the jitter of the windows timer
    */
    while( g_sockets.elapsed_time_usecs >= kOpenerTimerTickInMicroSeconds )
    {
        ManageConnections();
        g_sockets.elapsed_time_usecs -= kOpenerTimerTickInMicroSeconds;
    }

    return kEipStatusOk;
}


EipStatus NetworkHandlerFinish()
{
    CloseSocket( g_sockets.tcp_listener );
    CloseSocket( g_sockets.udp_unicast_listener );
    CloseSocket( g_sockets.udp_local_broadcast_listener );
    CloseSocket( g_sockets.udp_global_broadcast_listener );

    return kEipStatusOk;
}


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

        FD_CLR( socket, &read_set );
        // remove it from the read set so that later checks will not find it
    }

    return return_value;
}


EipStatus SendUdpData( struct sockaddr_in* address, int socket, EipByte* data,
        EipUint16 data_length )
{
    int sent_count = sendto( socket, (char*) data, data_length, 0,
            (struct sockaddr*) address, sizeof(*address) );

    CIPSTER_TRACE_INFO( "%s: socket:%d sending %d bytes\n", __func__, socket, data_length );

    if( sent_count < 0 )
    {
        CIPSTER_TRACE_ERR( "%s: error with sendto in sendUDPData: %s\n",
                __func__, strerrno().c_str() );

        return kEipStatusError;
    }

    if( sent_count != data_length )
    {
        CIPSTER_TRACE_WARN(
                "%s: data_length != sent_count mismatch, sent %d of %d\n",
                __func__, sent_count, data_length );

        return kEipStatusError;
    }

    return kEipStatusOk;
}


static void dump( const char* aPrompt, EipByte* aBytes, int aCount )
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


static int ensuredRead(  int sock, EipByte* where, int count )
{
    int i;
    int numRead;

    for( i=0;  i < count;  i += numRead )
    {
        numRead = recv( sock, where+i, count - i, 0 );
        if( numRead == 0 )
            break;

        else if( numRead == -1 )
        {
            i = -1;
            break;
        }
    }

    return i;
}


EipStatus HandleDataOnTcpSocket( int socket )
{
    /* We will handle just one EIP packet here the rest is done by the select
     * method which will inform us if more data is available in the socket
     *  because of the current implementation of the main loop this may not be
     *  the fastest way and a loop here with a non blocking socket would better
     *  fit*/

    // Check how many data is here -- read the first four bytes from the connection
    int num_read = ensuredRead( socket, s_packet, 4 );

    // TODO we may have to set the socket to a non blocking socket

    if( num_read == 0 )
    {
        CIPSTER_TRACE_ERR( "networkhandler: connection closed by client: %s\n",
                strerrno().c_str() );
        return kEipStatusError;
    }

    if( num_read < 0 )
    {
        CIPSTER_TRACE_ERR( "networkhandler: error on recv: %s\n", strerrno().c_str() );
        return kEipStatusError;
    }

    EipByte* read_buffer = &s_packet[2];    // here is EIP's data length

    unsigned packetz = GetIntFromMessage( &read_buffer ) + ENCAPSULATION_HEADER_LENGTH - 4;
    // -4 is for the 4 bytes we have already read

    // is the packet bigger than our s_packet buffer?
    if( packetz > sizeof(s_packet) - 4 )
    {
        CIPSTER_TRACE_ERR(
                "%s: packet len=%d is too big, ignoring packet\n",
                __func__, packetz );

        // toss the whole packet in chunks.

        int readz = sizeof(s_packet);

        unsigned total_read = 0;

        while( total_read < packetz )
        {
            num_read = ensuredRead( socket, &s_packet[0], readz );

            if( num_read == 0 ) // got error or connection closed by client
            {
                CIPSTER_TRACE_ERR( "networkhandler: connection closed by client: %s\n",
                        strerrno().c_str() );
                return kEipStatusError;
            }

            if( num_read < 0 )
            {
                CIPSTER_TRACE_ERR( "networkhandler: error on recv: %s\n",
                        strerrno().c_str() );
                return kEipStatusError;
            }

            dump( "bigTCP", s_packet, num_read );

            total_read += num_read;

            if( packetz - total_read < sizeof(s_packet) )
            {
                readz = packetz - total_read;
            }
        }

        return kEipStatusOk;
    }

    num_read = ensuredRead( socket, &s_packet[4], packetz );

    if( num_read == 0 ) // got error or connection closed by client
    {
        CIPSTER_TRACE_ERR( "networkhandler: connection closed by client: %s\n",
                strerrno().c_str() );
        return kEipStatusError;
    }

    if( num_read < 0 )
    {
        CIPSTER_TRACE_ERR( "networkhandler: error on recv: %s\n", strerrno().c_str() );
        return kEipStatusError;
    }

    if( (unsigned) num_read == packetz )
    {
        // we got the right amount of data
        packetz += 4;

        dump( "rTCP", s_packet, num_read + 4 );

        // TODO handle partial packets
        CIPSTER_TRACE_INFO( "Data received on tcp:\n" );

        g_current_active_tcp_socket = socket;

        int remaining_bytes = 0;

        num_read = HandleReceivedExplictTcpData(
                socket, s_packet, packetz, &remaining_bytes );

        g_current_active_tcp_socket = -1;

        if( remaining_bytes != 0 )
        {
            CIPSTER_TRACE_WARN(
                    "%s: received packet was too long: %d Bytes left!\n",
                    __func__, remaining_bytes );
        }

        if( num_read > 0 )
        {
            int sent_count = send( socket, (char*) s_packet, num_read, 0 );

            CIPSTER_TRACE_INFO( "%s: sent %d reply bytes. line %d\n",
                __func__, sent_count, __LINE__ );

            if( sent_count != num_read )
            {
                CIPSTER_TRACE_WARN( "%s: TCP response was not fully sent\n", __func__ );
            }
        }

        return kEipStatusOk;
    }
    else
    {
        /* we got a fragmented packet currently we cannot handle this will
         * for this we would need a network buffer per TCP socket
         *
         * However with typical packet sizes of EIP this should't be a big issue.
         */
        //TODO handle fragmented packets

        CIPSTER_TRACE_ERR( "%s: TCP read problem\n", __func__ );
    }

    return kEipStatusError;
}


/** @brief create a new UDP socket for the connection manager
 *
 * @param communciation_direction Consuming or producing port
 * @param socket_data Data for socket creation
 *
 * @return the socket handle if successful, else -1 */
int CreateUdpSocket( UdpCommuncationDirection communication_direction,
        struct sockaddr_in* socket_data )
{
    struct sockaddr_in peer_address;
    int new_socket;

    socklen_t peer_address_length;

    peer_address_length = sizeof(struct sockaddr_in);

    // create a new UDP socket
    if( ( new_socket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP ) ) == -1 )
    {
        CIPSTER_TRACE_ERR( "networkhandler: cannot create UDP socket: %s\n",
                strerrno().c_str() );
        return kEipInvalidSocket;
    }

    CIPSTER_TRACE_INFO( "networkhandler: UDP socket %d\n", new_socket );

    // check if it is sending or receiving
    if( communication_direction == kUdpConsuming )
    {
        int option_value = 1;

        if( setsockopt( new_socket, SOL_SOCKET, SO_REUSEADDR, (char*) &option_value,
                    sizeof(option_value) ) == -1 )
        {
            CIPSTER_TRACE_ERR(
                    "error setting socket option SO_REUSEADDR on consuming udp socket\n" );
            return kEipStatusError;
        }

        // bind is only for consuming necessary
        if( ( bind( new_socket, (struct sockaddr*) socket_data,
                      sizeof(struct sockaddr) ) ) == -1 )
        {
            CIPSTER_TRACE_ERR( "error on bind udp: %s\n", strerrno().c_str() );
            return kEipInvalidSocket;
        }

        CIPSTER_TRACE_INFO( "networkhandler: bind UDP socket %d\n", new_socket );
    }
    else    // we have a producing udp socket
    {
        if( socket_data->sin_addr.s_addr
            == g_multicast_configuration.starting_multicast_address )
        {
            if( 1 != g_time_to_live_value ) // we need to set a TTL value for the socket
            {
                if( setsockopt( new_socket, IPPROTO_IP, IP_MULTICAST_TTL,
                            &g_time_to_live_value,
                            sizeof(g_time_to_live_value) < 0 ) )
                {
                    CIPSTER_TRACE_ERR(
                            "networkhandler: could not set the TTL to: %d, error: %s\n",
                            g_time_to_live_value, strerrno().c_str() );
                    return kEipInvalidSocket;
                }
            }
        }
    }

    if( (communication_direction == kUdpConsuming)
        || (0 == socket_data->sin_addr.s_addr) )
    {
        // we have a peer to peer producer or a consuming connection
        if( getpeername( g_current_active_tcp_socket,
                    (struct sockaddr*) &peer_address, &peer_address_length )
            < 0 )
        {
            CIPSTER_TRACE_ERR( "networkhandler: could not get peername: %s\n",
                    strerrno().c_str() );
            return kEipInvalidSocket;
        }

        // store the originators address
        socket_data->sin_addr.s_addr = peer_address.sin_addr.s_addr;
    }

    // add new socket to the master list
    FD_SET( new_socket, &master_set );

    if( new_socket > highest_socket_handle )
    {
        highest_socket_handle = new_socket;
    }

    return new_socket;
}


void IApp_CloseSocket_udp( int socket_handle )
{
    CloseSocket( socket_handle );
}


void IApp_CloseSocket_tcp( int socket_handle )
{
    CloseSocket( socket_handle );
}


void CloseSocket( int socket_handle )
{
    CIPSTER_TRACE_INFO( "networkhandler: closing socket %d\n", socket_handle );

    if( socket_handle >= 0 )
    {
        FD_CLR( socket_handle, &master_set );
        shutdown( socket_handle, SHUT_RDWR );
        close( socket_handle );
    }
}


void CheckAndHandleTcpListenerSocket()
{
    int new_socket;

    // see if this is a connection request to the TCP listener
    if( true == CheckSocketSet( g_sockets.tcp_listener ) )
    {
        CIPSTER_TRACE_INFO( "networkhandler: new TCP connection\n" );

        new_socket = accept( g_sockets.tcp_listener, NULL, NULL );

        if( new_socket == -1 )
        {
            CIPSTER_TRACE_ERR( "networkhandler: error on accept: %s\n",
                    strerrno().c_str() );
            return;
        }

        FD_SET( new_socket, &master_set );

        // add newfd to master set
        if( new_socket > highest_socket_handle )
        {
            highest_socket_handle = new_socket;
        }

        CIPSTER_TRACE_INFO( "%s: adding TCP socket %d to master_set\n",
            __func__, new_socket );
    }
}


void CheckAndHandleUdpLocalBroadcastSocket()
{
    struct sockaddr_in from_address;

    socklen_t from_address_length;

    // see if this is an unsolicited inbound UDP message
    if( CheckSocketSet( g_sockets.udp_local_broadcast_listener ) )
    {
        from_address_length = sizeof(from_address);

        CIPSTER_TRACE_STATE(
                "networkhandler: unsolicited UDP message on EIP broadcast socket\n" );

        // Handle UDP broadcast messages
        int received_size = recvfrom( g_sockets.udp_local_broadcast_listener,
                s_packet,
                sizeof(s_packet),
                0, (struct sockaddr*) &from_address,
                &from_address_length );

        if( received_size <= 0 ) // got error
        {
            CIPSTER_TRACE_ERR(
                    "networkhandler: error on recvfrom UDP broadcast port: %s\n",
                    strerrno().c_str() );
            return;
        }

        CIPSTER_TRACE_INFO( "Data received on UDP:\n" );

        EipByte* receive_buffer = &s_packet[0];
        int remaining_bytes = 0;

        do {
            int reply_length = HandleReceivedExplictUdpData(
                    g_sockets.udp_local_broadcast_listener, &from_address,
                    receive_buffer, received_size, &remaining_bytes, false );

            receive_buffer  += received_size - remaining_bytes;
            received_size   = remaining_bytes;

            if( reply_length > 0 )
            {
                // if the active socket matches a registered UDP callback, handle a UDP packet
                int sent_count = sendto( g_sockets.udp_local_broadcast_listener,
                            (char*) s_packet, reply_length, 0,
                            (struct sockaddr*) &from_address, sizeof(from_address) );

                CIPSTER_TRACE_INFO( "%s: sent %d reply bytes. line %d\n",
                    __func__, sent_count, __LINE__ );

                if( sent_count != reply_length )
                {
                    CIPSTER_TRACE_INFO(
                            "networkhandler: UDP response was not fully sent\n" );
                }
            }
        } while( remaining_bytes > 0 );
    }
}


void CheckAndHandleUdpGlobalBroadcastSocket()
{
    struct sockaddr_in from_address;

    socklen_t from_address_length;

    // see if this is an unsolicited inbound UDP message
    if( true == CheckSocketSet( g_sockets.udp_global_broadcast_listener ) )
    {
        from_address_length = sizeof(from_address);

        CIPSTER_TRACE_STATE(
                "networkhandler: unsolicited UDP message on EIP global broadcast socket\n" );

        // Handle UDP broadcast messages
        int received_size = recvfrom( g_sockets.udp_global_broadcast_listener,
                s_packet,
                sizeof(s_packet),
                0, (struct sockaddr*) &from_address,
                &from_address_length );

        if( received_size <= 0 ) // got error
        {
            CIPSTER_TRACE_ERR(
                    "networkhandler: error on recvfrom UDP global broadcast port: %s\n",
                    strerrno().c_str() );
            return;
        }

        CIPSTER_TRACE_INFO( "Data received on global broadcast UDP:\n" );

        EipByte* receive_buffer = &s_packet[0];
        int remaining_bytes = 0;

        do {
            int reply_length = HandleReceivedExplictUdpData(
                    g_sockets.udp_global_broadcast_listener, &from_address,
                    receive_buffer, received_size, &remaining_bytes, false );

            receive_buffer  += received_size - remaining_bytes;
            received_size   = remaining_bytes;

            if( reply_length > 0 )
            {
                // if the active socket matches a registered UDP callback, handle a UDP packet
                int sent_count = sendto( g_sockets.udp_global_broadcast_listener,
                            (char*) s_packet, reply_length, 0,
                            (struct sockaddr*) &from_address, sizeof(from_address) );

                CIPSTER_TRACE_INFO( "%s: sent %d reply bytes. line %d:\n",
                    __func__, sent_count, __LINE__ );

                if( sent_count != reply_length )
                {
                    CIPSTER_TRACE_INFO(
                            "networkhandler: UDP response was not fully sent\n" );
                }
            }
        } while( remaining_bytes > 0 );
    }
}


void CheckAndHandleUdpUnicastSocket()
{
    struct sockaddr_in from_address;

    socklen_t from_address_length;

    // see if this is an unsolicited inbound UDP message
    if( true == CheckSocketSet( g_sockets.udp_unicast_listener ) )
    {
        from_address_length = sizeof(from_address);

        CIPSTER_TRACE_STATE(
                "networkhandler: unsolicited UDP message on EIP unicast socket\n" );

        // Handle UDP broadcast messages
        int received_size = recvfrom( g_sockets.udp_unicast_listener,
                s_packet,
                sizeof(s_packet),
                0, (struct sockaddr*) &from_address,
                &from_address_length );

        if( received_size <= 0 ) // got error
        {
            CIPSTER_TRACE_ERR(
                    "networkhandler: error on recvfrom UDP unicast port: %s\n",
                    strerrno().c_str() );
            return;
        }

        CIPSTER_TRACE_INFO( "Data received on UDP unicast:\n" );

        EipByte* receive_buffer = &s_packet[0];
        int remaining_bytes = 0;

        do {
            int reply_length = HandleReceivedExplictUdpData(
                    g_sockets.udp_unicast_listener, &from_address, receive_buffer,
                    received_size, &remaining_bytes, true );

            receive_buffer  += received_size - remaining_bytes;
            received_size   = remaining_bytes;

            if( reply_length > 0 )
            {
                // if the active socket matches a registered UDP callback, handle a UDP packet
                int sent_count = sendto( g_sockets.udp_unicast_listener,
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
        } while( remaining_bytes > 0 );
    }
}


void CheckAndHandleConsumingUdpSockets()
{
    struct sockaddr_in from_address;

    socklen_t from_address_length;

    CipConn* iter = g_active_connection_list;

    // see a message on one of the registered UDP sockets has been received
    while( iter )
    {
        CipConn* conn = iter;

        // do this at the beginning as the close function can make the entry invalid
        iter = iter->next;

        if( conn->consuming_socket != -1  &&  CheckSocketSet( conn->consuming_socket ) )
        {
            from_address_length = sizeof(from_address);

            int received_size = recvfrom(
                    conn->consuming_socket,
                    s_packet, sizeof(s_packet), 0,
                    (struct sockaddr*) &from_address, &from_address_length );

            if( 0 == received_size )
            {
                CIPSTER_TRACE_STATE( "connection closed by client\n" );
                conn->connection_close_function(
                        conn );
                continue;
            }

            if( 0 > received_size )
            {
                CIPSTER_TRACE_ERR( "%s: error on recv: %s\n",
                        __func__, strerrno().c_str() );

                conn->connection_close_function( conn );
                continue;
            }

            HandleReceivedConnectedData( s_packet,
                    received_size, &from_address );
        }
    }
}


int GetMaxSocket( int socket1, int socket2, int socket3, int socket4 )
{
    if( (socket1 > socket2) && (socket1 > socket3) && (socket1 > socket4) )
        return socket1;

    if( (socket2 > socket1) && (socket2 > socket3) && (socket2 > socket4) )
        return socket2;

    if( (socket3 > socket1) && (socket3 > socket2) && (socket3 > socket4) )
        return socket3;

    return socket4;
}
