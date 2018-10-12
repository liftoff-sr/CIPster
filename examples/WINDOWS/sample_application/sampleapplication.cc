/*******************************************************************************
 * Copyright (c) 2012, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/

#include <cipster_api.h>
#include <string.h>
#include <stdlib.h>

#define DEMO_APP_INPUT_ASSEMBLY_NUM                 100 // 0x064
#define DEMO_APP_OUTPUT_ASSEMBLY_NUM                150 // 0x096
#define DEMO_APP_CONFIG_ASSEMBLY_NUM                151 // 0x097
#define DEMO_APP_HEARBEAT_INPUT_ONLY_ASSEMBLY_NUM   152 // 0x098
#define DEMO_APP_HEARBEAT_LISTEN_ONLY_ASSEMBLY_NUM  153 // 0x099
#define DEMO_APP_EXPLICT_ASSEMBLY_NUM               154 // 0x09A

// global variables for demo application (4 assembly data fields)  ***********

uint8_t    g_assembly_data064[128];    // Input
uint8_t    g_assembly_data096[128];    // Output
uint8_t    g_assembly_data097[64];     // Config
uint8_t    g_assembly_data09A[128];    // Explicit


EipStatus ApplicationInitialization()
{
    // create 3 assembly object instances
    // INPUT
    CreateAssemblyInstance( DEMO_APP_INPUT_ASSEMBLY_NUM,
        ByteBuf( g_assembly_data064, sizeof(g_assembly_data064) ) );

    // OUTPUT
    CreateAssemblyInstance( DEMO_APP_OUTPUT_ASSEMBLY_NUM,
        ByteBuf( g_assembly_data096, sizeof(g_assembly_data096) ) );

    // CONFIG
    CreateAssemblyInstance( DEMO_APP_CONFIG_ASSEMBLY_NUM,
        ByteBuf( g_assembly_data097, sizeof(g_assembly_data097) ) );

    // Heart-beat output assembly for Input only connections
    CreateAssemblyInstance( DEMO_APP_HEARBEAT_INPUT_ONLY_ASSEMBLY_NUM,
        ByteBuf( 0, 0 ) );

    // Heart-beat output assembly for Listen only connections
    CreateAssemblyInstance( DEMO_APP_HEARBEAT_LISTEN_ONLY_ASSEMBLY_NUM,
        ByteBuf( 0, 0 ) );

    // assembly for explicit messaging
    CreateAssemblyInstance( DEMO_APP_EXPLICT_ASSEMBLY_NUM,
        ByteBuf( g_assembly_data09A, sizeof(g_assembly_data09A) ) );

    ConfigureExclusiveOwnerConnectionPoint(
            DEMO_APP_OUTPUT_ASSEMBLY_NUM,
            DEMO_APP_INPUT_ASSEMBLY_NUM,
            DEMO_APP_CONFIG_ASSEMBLY_NUM );

    // Reserve a connection instance that can connect without a config_path
    ConfigureExclusiveOwnerConnectionPoint(
            DEMO_APP_OUTPUT_ASSEMBLY_NUM,
            DEMO_APP_INPUT_ASSEMBLY_NUM,
            -1 );                           // config path may be omitted

    ConfigureInputOnlyConnectionPoint(
            DEMO_APP_HEARBEAT_INPUT_ONLY_ASSEMBLY_NUM,
            DEMO_APP_INPUT_ASSEMBLY_NUM,
            DEMO_APP_CONFIG_ASSEMBLY_NUM );

    ConfigureListenOnlyConnectionPoint(
            DEMO_APP_HEARBEAT_LISTEN_ONLY_ASSEMBLY_NUM,
            DEMO_APP_INPUT_ASSEMBLY_NUM,
            DEMO_APP_CONFIG_ASSEMBLY_NUM );

    return kEipStatusOk;
}


void HandleApplication()
{
    // check if application needs to trigger a connection
}


void NotifyIoConnectionEvent( CipConn* aConn, IoConnectionEvent aEvent )
{
    // maintain a correct output state according to the connection state
    // maintain a correct output state according to the connection state
    int consuming_id = aConn->ConsumingPath().GetInstanceOrConnPt();
    int producing_id = aConn->ProducingPath().GetInstanceOrConnPt();
}


EipStatus AfterAssemblyDataReceived( AssemblyInstance* aInstance,
    OpMode aMode, int aBytesReceivedCount )
{
    EipStatus status = kEipStatusOk;

    // handle the data received e.g., update outputs of the device
    switch( aInstance->Id() )
    {
    case DEMO_APP_OUTPUT_ASSEMBLY_NUM:
        /* Data for the output assembly has been received.
         * Mirror it to the inputs */
        memcpy( &g_assembly_data064[0], &g_assembly_data096[0],
                sizeof(g_assembly_data064) );
        break;

    case DEMO_APP_EXPLICT_ASSEMBLY_NUM:
        /* do something interesting with the new data from
         * the explicit set-data-attribute message */
        break;

    case DEMO_APP_CONFIG_ASSEMBLY_NUM:
        /* Add here code to handle configuration data and check if it is ok
         * The demo application does not handle config data.
         * However in order to pass the test we accept any data given.
         * EIP_ERROR
         */
        status = kEipStatusOk;
        break;
    }

    return status;
}


bool BeforeAssemblyDataSend( AssemblyInstance* instance )
{
    // update data to be sent e.g., read inputs of the device
    /*In this sample app we mirror the data from out to inputs on data receive
     * therefore we need nothing to do here. Just return true to inform that
     * the data is new.
     */

    if( instance->Id() == DEMO_APP_EXPLICT_ASSEMBLY_NUM )
    {
        /* do something interesting with the existing data
         * for the explicit get-data-attribute message */
    }

    return true;
}


EipStatus ResetDevice()
{
    // add reset code here
    return kEipStatusOk;
}


EipStatus ResetDeviceToInitialConfiguration( bool also_reset_comm_params )
{
    // reset the parameters

    // then perform device reset

    return kEipStatusOk;
}


void RunIdleChanged( uint32_t run_idle_value )
{
    (void) run_idle_value;
}
