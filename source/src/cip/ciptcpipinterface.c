/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/
#include <string.h>

#include "ciptcpipinterface.h"

#include "trace.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "ciperror.h"
#include "byte_bufs.h"
#include "cipethernetlink.h"
#include "cipster_api.h"

static CipDword tcp_status_ = 0x1;                         //*< #1  TCP status with 1 we indicate that we got a valid configuration from DHCP or BOOTP

static CipDword configuration_capability_ = 0x04 | 0x20;   //*< #2  This is a default value meaning that it is a DHCP client see 5-3.2.2.2 EIP specification; 0x20 indicates "Hardware Configurable"

static CipDword configuration_control_ = 0;                //*< #3  This is a TCP/IP object attribute. For now it is always zero and is not used for anything.


CipTcpIpNetworkInterfaceConfiguration interface_configuration_ =    //*< #5 IP, network mask, gateway, name server 1 & 2, domain name
{
    0,                               // default IP address
    0,                               // NetworkMask
    0,                               // Gateway
    0,                               // NameServer
    0,                               // NameServer2
    {                                // DomainName
        0, NULL,
    }
};

static CipString hostname_ =    //*< #6 Hostname
{ 0, NULL };

/** @brief #8 the time to live value to be used for multi-cast connections
 *
 * Currently we implement it non set-able and with the default value of 1.
 */
EipUint8 g_time_to_live_value = 1;


/** @brief #9 The multicast configuration for this device
 *
 * Currently we implement it non set-able and use the default
 * allocation algorithm
 */
MulticastAddressConfiguration g_multicast_configuration =
{
    0,  // us the default allocation algorithm
    0,  // reserved
    1,  // we currently use only one multicast address
    0   // the multicast address will be allocated on ip address configuration
};

//************* Functions ***************************************

static EipStatus get_attr_4( CipAttribute* attribute,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    EipStatus   status = kEipStatusOkSend;
    BufWriter   out = response->data;

    response->data_length = 0;
    response->reply_service = 0x80 | request->service;

    response->general_status = kCipErrorSuccess;
    response->size_of_additional_status = 0;

    CipAppPath app_path;

    app_path.SetClass( kCipEthernetLinkClassCode );
    app_path.SetInstance( attribute->Instance()->Id() );

    int result = app_path.SerializeAppPath( out + 2 );

    out.put16( result/2 );      // word count as 16 bits

    out += result;

    response->data_length += out.data() - response->data.data();

    return status;
}


// Attribute 9 can not be easily handled with the default mechanism
// therefore we will do here.
static EipStatus get_multicast_config( CipAttribute* attribute,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    EipStatus   status = kEipStatusOkSend;
    BufWriter   out = response->data;

    response->reply_service = 0x80 | request->service;

    response->general_status = kCipErrorSuccess;
    response->size_of_additional_status = 0;

    response->data_length += EncodeData(
            kCipUsint, &g_multicast_configuration.alloc_control, out );

    response->data_length += EncodeData(
            kCipUsint, &g_multicast_configuration.reserved_shall_be_zero,
            out );

    response->data_length += EncodeData(
            kCipUint,
            &g_multicast_configuration.number_of_allocated_multicast_addresses,
            out );

    EipUint32 multicast_address = ntohl(
            g_multicast_configuration.starting_multicast_address );

    response->data_length += EncodeData( kCipUdint,
            &multicast_address,
            out );

    return status;
}


static EipStatus get_attr_7( CipAttribute* attribute,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    // insert 6 zeros for the required empty safety network number
    // according to Table 5-4.15
    memset( response->data.data(), 0, 6 );
    response->data_length += 6;

    response->general_status = kCipErrorSuccess;

    return kEipStatusOkSend;
}


static EipStatus set_attr_13( CipAttribute* attribute,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    response->general_status = kCipErrorSuccess;

    int instance_id = attribute->Instance()->Id();

    int inactivity_timeout = request->data.get16();

    //TODO put it in the instance

    response->general_status = kCipErrorSuccess;

    return kEipStatusOkSend;
}


EipStatus ConfigureNetworkInterface( const char* ip_address,
        const char* subnet_mask,
        const char* gateway )
{
    interface_configuration_.ip_address = inet_addr( ip_address );
    interface_configuration_.network_mask = inet_addr( subnet_mask );
    interface_configuration_.gateway = inet_addr( gateway );

    // Calculate the CIP multicast address. The multicast address is calculated, not input.
    // See CIP spec Vol2 3-5.3 for multicast address algorithm.
    EipUint32 host_id = ntohl( interface_configuration_.ip_address )
                        & ~ntohl( interface_configuration_.network_mask );
    host_id -= 1;
    host_id &= 0x3ff;

    g_multicast_configuration.starting_multicast_address = htonl(
            ntohl( inet_addr( "239.192.1.0" ) ) + (host_id << 5) );

    return kEipStatusOk;
}


void ConfigureDomainName( const char* domain_name )
{
    if( interface_configuration_.domain_name.string )
    {
        /* if the string is already set to a value we have to free the resources
         * before we can set the new value in order to avoid memory leaks.
         */
        CipFree( interface_configuration_.domain_name.string );
    }

    interface_configuration_.domain_name.length = strlen( domain_name );

    if( interface_configuration_.domain_name.length )
    {
        interface_configuration_.domain_name.string = (EipByte*) CipCalloc(
                interface_configuration_.domain_name.length + 1, sizeof(EipInt8) );

        strcpy( (char*) interface_configuration_.domain_name.string, domain_name );
    }
    else
    {
        interface_configuration_.domain_name.string = NULL;
    }
}


void ConfigureHostName( const char* hostname )
{
    if( hostname_.string )
    {
        /* if the string is already set to a value we have to free the resources
         * before we can set the new value in order to avoid memory leaks.
         */
        CipFree( hostname_.string );
    }

    hostname_.length = strlen( hostname );

    if( hostname_.length )
    {
        hostname_.string = (EipByte*) CipCalloc( hostname_.length + 1,
                sizeof(EipByte) );

        strcpy( (char*) hostname_.string, hostname );
    }
    else
    {
        hostname_.string = NULL;
    }
}


static CipInstance* createTcpIpInterfaceInstance()
{
    CipClass* clazz = GetCipClass( kCipTcpIpInterfaceClassCode );

    CipInstance* i = new CipInstance( clazz->Instances().size() + 1 );

    i->AttributeInsert( 1, kCipDword, kGetableSingleAndAll, &tcp_status_ );
    i->AttributeInsert( 2, kCipDword, kGetableSingleAndAll, &configuration_capability_ );

    i->AttributeInsert( 3, kCipDword, kGetableSingleAndAll, &configuration_control_ );

    i->AttributeInsert( 4, kCipAny, kGetableSingleAndAll, get_attr_4, NULL );

    i->AttributeInsert( 5, kCipUdintUdintUdintUdintUdintString, kGetableSingleAndAll, &interface_configuration_ );

    i->AttributeInsert( 6, kCipString, kGetableSingleAndAll, &hostname_ );

    i->AttributeInsert( 7, kCipAny, kGetableSingleAndAll, get_attr_7, NULL );

    // This is settable also, but volatile after setting.  To make it NV, supply
    // a setter which calls SetAttrData() and then writes it to NV memory.
    i->AttributeInsert( 8, kCipUsint, kSetAndGetAble, &g_time_to_live_value );

    i->AttributeInsert( 9, kCipAny, kGetableSingleAndAll, get_multicast_config, NULL, &g_multicast_configuration );

    i->AttributeInsert( 13, kCipAny, kSetable, NULL, set_attr_13 );

    clazz->InstanceInsert( i );

    return i;
}


EipStatus CipTcpIpInterfaceInit()
{
    if( !GetCipClass( kCipTcpIpInterfaceClassCode ) )
    {
        CipClass* clazz = new CipClass( kCipTcpIpInterfaceClassCode,
              "TCP/IP Interface",
              MASK7(1,2,3,4,5,6,7),     // common class attributes mask
              0xffffffff,               // class getAttributeAll mask
              0xffffffff,               // instance getAttributeAll mask
              4                         // version
              );

        RegisterCipClass( clazz );

        createTcpIpInterfaceInstance();
    }

    return kEipStatusOk;
}


void ShutdownTcpIpInterface()
{
    if( hostname_.string )
    {
        CipFree( hostname_.string );
        hostname_.string = NULL;
    }

    if( interface_configuration_.domain_name.string )
    {
        CipFree( interface_configuration_.domain_name.string );
        interface_configuration_.domain_name.string = NULL;
    }
}
