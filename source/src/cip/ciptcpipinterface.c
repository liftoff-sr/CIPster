/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Conversion to C++ is Copyright (C) 2016, SoftPLC Corportion.
 * All rights reserved.
 *
 ******************************************************************************/
#include <string.h>

#include "ciptcpipinterface.h"

#include "trace.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "ciperror.h"
#include "endianconv.h"
#include "cipethernetlink.h"
#include "opener_api.h"

CipDword tcp_status_ = 0x1;                         //*< #1  TCP status with 1 we indicate that we got a valid configuration from DHCP or BOOTP

CipDword configuration_capability_ = 0x04 | 0x20;   //*< #2  This is a default value meaning that it is a DHCP client see 5-3.2.2.2 EIP specification; 0x20 indicates "Hardware Configurable"

CipDword configuration_control_ = 0;                //*< #3  This is a TCP/IP object attribute. For now it is always zero and is not used for anything.

CipEpath physical_link_object_ =                    //*< #4
{
    2,                               //*< EIP_UINT16 (UINT) PathSize in 16 Bit chunks
    CIP_ETHERNETLINK_CLASS_CODE,     //*< EIP_UINT16 ClassID
    1,                               //*< EIP_UINT16 InstanceNr
    0                                //*< EIP_UINT16 AttributNr (not used as this is the EPATH the EthernetLink object)
};

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

CipString hostname_ =    //*< #6 Hostname
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


// Attribute 9 can not be easily handled with the default mechanism
// therefore we will do here.
static EipStatus get_multicast_config( CipAttribute* attribute,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    EipStatus status = kEipStatusOkSend;
    EipByte* message = response->data;

    response->data_length = 0;
    response->reply_service = 0x80 | request->service;

    response->general_status = kCipErrorSuccess;
    response->size_of_additional_status = 0;

    response->data_length += EncodeData(
            kCipUsint, &g_multicast_configuration.alloc_control, &message );

    response->data_length += EncodeData(
            kCipUsint, &g_multicast_configuration.reserved_shall_be_zero,
            &message );

    response->data_length += EncodeData(
            kCipUint,
            &g_multicast_configuration.number_of_allocated_multicast_addresses,
            &message );

    EipUint32 multicast_address = ntohl(
            g_multicast_configuration.starting_multicast_address );

    response->data_length += EncodeData( kCipUdint,
            &multicast_address,
            &message );

    return status;
}


static EipStatus get_attr_7( CipAttribute* attribute,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    // insert 6 zeros for the required empty safety network number
    // according to Table 5-4.15
    memset( response->data, 0, 6 );
    response->data_length += 6;

    response->general_status = kCipErrorSuccess;

    return kEipStatusOkSend;
}


#if 0
EipStatus GetAttributeAllTcpIpInterface( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    EipUint8*   start = response->data;       // snapshot initial
    EipUint32   get_mask = instance->owning_class->get_attribute_all_mask;

    const CipInstance::CipAttributes& attributes = instance->Attributes();

    for( CipInstance::CipAttributes::const_iterator it = attributes.begin();
            it != attributes.end(); ++it )
    {
        int attribute_id = (*it)->Id();

        // only return attributes that are flagged as being part of GetAttributeAll
        if( attribute_id < 32 && (get_mask & (1 << attribute_id)) )
        {
            request->request_path.attribute_number = attribute_id;

            if( 8 == attribute_id )
            {
                // insert 6 zeros for the required empty safety network number
                // according to Table 5-3.10
                memset( response->data, 0, 6 );
                response->data += 6;
            }

            if( kEipStatusOkSend != GetAttributeSingleTcpIpInterface(
                    instance, request, response ) )
            {
                response->data = start;
                return kEipStatusError;
            }

            response->data += response->data_length;
        }
    }

    response->data_length = response->data - start;
    response->data = start;

    return kEipStatusOkSend;
}
#endif


EipStatus ConfigureNetworkInterface( const char* ip_address,
        const char* subnet_mask,
        const char* gateway )
{
    interface_configuration_.ip_address = inet_addr( ip_address );
    interface_configuration_.network_mask = inet_addr( subnet_mask );
    interface_configuration_.gateway = inet_addr( gateway );

    // calculate the CIP multicast address. The multicast address is calculated, not input
    EipUint32 host_id = ntohl( interface_configuration_.ip_address )
                        & ~ntohl( interface_configuration_.network_mask ); // see CIP spec 3-5.3 for multicast address algorithm
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

    i->AttributeInsert( 4, kCipEpath, kGetableSingleAndAll, &physical_link_object_ );

    i->AttributeInsert( 5, kCipUdintUdintUdintUdintUdintString, kGetableSingleAndAll, &interface_configuration_ );

    i->AttributeInsert( 6, kCipString, kGetableSingleAndAll, &hostname_ );

    i->AttributeInsert( 7, kCipAny, kGetableSingleAndAll, get_attr_7, NULL );

    // This is settable also, but volatile after setting.  To make it NV, supply
    // a setter which calls SetAttrData() and then writes it to NV memory.
    i->AttributeInsert( 8, kCipUsint, kSetAndGetAble, &g_time_to_live_value );

    i->AttributeInsert( 9, kCipAny, kGetableSingleAndAll, get_multicast_config, NULL, &g_multicast_configuration );

    return i;
}


EipStatus CipTcpIpInterfaceInit()
{
    if( !GetCipClass( kCipTcpIpInterfaceClassCode ) )
    {
        CipClass* clazz = new CipClass( kCipTcpIpInterfaceClassCode,
              "TCP/IP interface",
              0xffffffff,               // class getAttributeAll mask
              0xffffffff,               // instance getAttributeAll mask
              4                         // version
              );

        RegisterCipClass( clazz );

        clazz->InstanceInsert( createTcpIpInterfaceInstance() );
    }

    return kEipStatusOk;
}


void ShutdownTcpIpInterface()
{
    //Only free the resources if they are initialized
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
