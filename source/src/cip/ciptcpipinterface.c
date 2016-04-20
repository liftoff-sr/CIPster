/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
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
    2,                                              //*< EIP_UINT16 (UINT) PathSize in 16 Bit chunks
    CIP_ETHERNETLINK_CLASS_CODE,                    //*< EIP_UINT16 ClassID
    1,                                              //*< EIP_UINT16 InstanceNr
    0                                               //*< EIP_UINT16 AttributNr (not used as this is the EPATH the EthernetLink object)
};

CipTcpIpNetworkInterfaceConfiguration interface_configuration_ =    //*< #5 IP, network mask, gateway, name server 1 & 2, domain name
{
    0,                                                              // default IP address
    0,                                                              // NetworkMask
    0,                                                              // Gateway
    0,                                                              // NameServer
    0,                                                              // NameServer2
    {                                                               // DomainName
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
EipStatus GetAttributeSingleTcpIpInterface( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response );

EipStatus GetAttributeAllTcpIpInterface( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response );

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
    if( NULL != interface_configuration_.domain_name.string )
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
    if( NULL != hostname_.string )
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


EipStatus SetAttributeSingleTcp( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    CipAttribute* attribute = GetCipAttribute(
            instance, request->request_path.attribute_number );

    (void) instance; //Suppress compiler warning

    if( 0 != attribute )
    {
        // it is an attribute we currently support, however no attribute is setable
        // TODO: if you like to have a device that can be configured via this CIP object add your code here
        // TODO: check for flags associated with attributes
        response->general_status = kCipErrorAttributeNotSetable;
    }
    else
    {
        // we don't have this attribute
        response->general_status = kCipErrorAttributeNotSupported;
    }

    response->size_of_additional_status = 0;
    response->data_length = 0;
    response->reply_service = (0x80
                                              | request->service);
    return kEipStatusOkSend;
}


EipStatus CipTcpIpInterfaceInit()
{
    CipClass* tcp_ip_class = NULL;

    if( ( tcp_ip_class = CreateCipClass( kCipTcpIpInterfaceClassCode, 0,    // # class attributes
                  0xffffffff,                                               // class getAttributeAll mask
                  0,                                                        // # class services
                  8,                                                        // # instance attributes
                  0xffffffff,                                               // instance getAttributeAll mask
                  1,                                                        // # instance services
                  1,                                                        // # instances
                  "TCP/IP interface", 3 ) ) == 0 )
    {
        return kEipStatusError;
    }

    CipInstance* instance = GetCipInstance( tcp_ip_class, 1 ); // bind attributes to the instance #1 that was created above

    InsertAttribute( instance, 1, kCipDword, (void*) &tcp_status_, kGetableSingleAndAll );
    InsertAttribute( instance, 2, kCipDword, (void*) &configuration_capability_, kGetableSingleAndAll );

    InsertAttribute( instance, 3, kCipDword, (void*) &configuration_control_, kGetableSingleAndAll );

    InsertAttribute( instance, 4, kCipEpath, &physical_link_object_, kGetableSingleAndAll );

    InsertAttribute( instance, 5, kCipUdintUdintUdintUdintUdintString,
            &interface_configuration_, kGetableSingleAndAll );

    InsertAttribute( instance, 6, kCipString, (void*) &hostname_,
            kGetableSingleAndAll );

    InsertAttribute( instance, 8, kCipUsint, (void*) &g_time_to_live_value,
            kGetableSingleAndAll );

    InsertAttribute( instance, 9, kCipAny, (void*) &g_multicast_configuration,
            kGetableSingleAndAll );

    InsertService( tcp_ip_class, kGetAttributeSingle,
            &GetAttributeSingleTcpIpInterface,
            "GetAttributeSingleTCPIPInterface" );

    InsertService( tcp_ip_class, kGetAttributeAll, &GetAttributeAllTcpIpInterface,
            "GetAttributeAllTCPIPInterface" );

    InsertService( tcp_ip_class, kSetAttributeSingle, &SetAttributeSingleTcp,
            "SetAttributeSingle" );

    return kEipStatusOk;
}


void ShutdownTcpIpInterface( void )
{
    //Only free the resources if they are initialized
    if( NULL != hostname_.string )
    {
        CipFree( hostname_.string );
        hostname_.string = NULL;
    }

    if( NULL != interface_configuration_.domain_name.string )
    {
        CipFree( interface_configuration_.domain_name.string );
        interface_configuration_.domain_name.string = NULL;
    }
}


EipStatus GetAttributeSingleTcpIpInterface( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    EipStatus status = kEipStatusOkSend;
    EipByte* message = response->data;

    if( 9 == request->request_path.attribute_number )
    {
        /*  attribute 9 can not be easily handled with the default mechanism
         *   therefore we will do it by hand
         */
        response->data_length = 0;
        response->reply_service = 0x80 | request->service;

        response->general_status = kCipErrorSuccess;
        response->size_of_additional_status = 0;

        response->data_length += EncodeData(
                kCipUsint, &(g_multicast_configuration.alloc_control), &message );

        response->data_length += EncodeData(
                kCipUsint, &(g_multicast_configuration.reserved_shall_be_zero),
                &message );

        response->data_length += EncodeData(
                kCipUint,
                &(g_multicast_configuration.number_of_allocated_multicast_addresses),
                &message );

        EipUint32 multicast_address = ntohl(
                g_multicast_configuration.starting_multicast_address );

        response->data_length += EncodeData( kCipUdint,
                &multicast_address,
                &message );
    }
    else
    {
        status = GetAttributeSingle( instance, request, response );
        OPENER_TRACE_INFO( "%s: status:%d\n", __func__, status );
    }

    return status;
}


EipStatus GetAttributeAllTcpIpInterface( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    EipUint8* start = response->data;       // snapshot initial

    const CipInstance::CipAttributes& attributes = instance->Attributes();
    for( unsigned j = 0; j < attributes.size();  ++j )
    {
        CipAttribute* attribute = attributes[j];

        int attribute_id = attribute->attribute_id;

        // only return attributes that are flagged as being part of GetAttributeAll
        if( attribute_id < 32
            && (instance->cip_class->get_attribute_all_mask & 1 << attribute_id) )
        {
            request->request_path.attribute_number = attribute_id;

            if( 8 == attribute_id ) // insert 6 zeros for the required empty safety network number according to Table 5-3.10
            {
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

        attribute++;
    }

    response->data_length = response->data - start;
    response->data = start;

    return kEipStatusOkSend;
}
