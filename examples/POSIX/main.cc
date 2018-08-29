/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "cipster_api.h"

EipStatus ApplicationInitialization();  // my business.

// ****************************************************************************
/** @brief Signal handler function for ending stack execution
 *
 * @param signal the signal we received
 */
void LeaveStack( int signal );

// ***************************************************************************
/** @brief Flag indicating if the stack should end its execution
 */
volatile bool g_end_stack;



bool parse_mac( const char* mac_str, uint8_t mac_out[6] )
{
    int b[6];

    if( 6 == sscanf( mac_str, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5] ) ||
        6 == sscanf( mac_str, "%x-%x-%x-%x-%x-%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5] ) )
    {
        for( int i=0; i<6; ++i )
            mac_out[i] = b[i];

        return true;
    }

    return false;
}


int main( int argc, char* argv[] )
{
    int ret = 0;

    uint8_t    my_mac_address[6];
    uint16_t   unique_connection_id;

    if( argc != 7 )
    {
        printf( "Wrong number of command line parameters! %d instead of 12\n", argc );
        printf( "The correct command line parameters are:\n" );
        printf( "%s ipaddress subnetmask gateway domainname hostaddress macaddress\n", argv[0] );
        printf( "e.g.\n" );
        printf( "    %s 192.168.0.2 255.255.255.0 192.168.0.1 test.com testdevice 00:15:C5:BF:D0:87\n", argv[0] );
        ret = 1;
        goto exit;
    }

    // unique_connection_id should be sufficiently random or incremented
    // and stored in non-volatile memory each time the device boots.
    unique_connection_id = rand();

    // Setup the CIP stack early, before calling any stack Configuration functions.
    CipStackInit( unique_connection_id );

    // fetch Internet address info from the platform
    ConfigureNetworkInterface( argv[1], argv[2], argv[3] );
    ConfigureDomainName( argv[4] );
    ConfigureHostName( argv[5] );

    if( !parse_mac( argv[6], my_mac_address ) )
    {
        printf( "Bad macaddress format. It can use either : or - to separate:\n"
                " e.g. 00:15:C5:BF:D0:87 or 00-15-C5-BF-D0-87\n" );
        ret = 2;
        goto exit;
    }

    ConfigureMacAddress( my_mac_address );

    // for a real device the serial number should be unique per device
    SetDeviceSerialNumber( 123456789 );

    if( ApplicationInitialization() != kEipStatusOk )
    {
        fprintf( stderr, "Unable to initialize Assembly instances\n" );
        ret = 2;
        goto shutdown;
    }

    // Setup Network only after Configure*() calls above
    if( NetworkHandlerInitialize() != kEipStatusOk )
    {
        fprintf( stderr, "Unable to initialize NetworkHandlers\n" );
        ret = 3;
        goto shutdown;
    }

#ifndef _WIN32
    // register for closing signals so that we can trigger the stack to end
    signal( SIGHUP, LeaveStack );
    signal( SIGINT, LeaveStack );
#endif

    printf( "running...\n" );

    // The event loop. Put other processing you need done continually in here
    while( !g_end_stack )
    {
        if( kEipStatusOk != NetworkHandlerProcessOnce() )
        {
            break;
        }
    }

    printf( "\ncleaning up and ending...\n" );

    // clean up network state
    NetworkHandlerFinish();

shutdown:
    // close remaining sessions and connections, cleanup used data
    ShutdownCipStack();

exit:
    return ret;
}


void LeaveStack( int signal )
{
    (void) signal;      // kill unused parameter warning

    CIPSTER_TRACE_STATE( "got signal\n" );

    g_end_stack = true;
}
