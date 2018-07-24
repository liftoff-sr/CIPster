/*******************************************************************************
 * Copyright (c) 2016-2018, SoftPLC Corportion.
 *
 ******************************************************************************/
#ifndef CIPTCPIPINTERFACE_H_
#define CIPTCPIPINTERFACE_H_


/** @file ciptcpipinterface.h
 * @brief Public interface of the TCP/IP Interface Object
 *
 */

#include <typedefs.h>
#include "ciptypes.h"
#include "cipclass.h"


/** @brief Multicast Configuration struct, called Mcast config
 *
 */
struct MulticastAddressConfiguration
{
    MulticastAddressConfiguration() :
        alloc_control( 0 ),
        reserved_zero( 0 ),
        number_of_allocated_multicast_addresses( 1 ),
        starting_multicast_address( 0 )
    {}

    /// 0 for default multicast address generation algorithm;
    /// 1 for multicast addresses according to Num MCast and MCast Start Addr
    CipUsint    alloc_control;

    CipUsint    reserved_zero;               ///< shall be zero

    /// Number of IP multicast addresses allocated
    CipUint     number_of_allocated_multicast_addresses;

    /// Starting multicast address from which Num Mcast addresses are allocated
    CipUdint    starting_multicast_address;
};


/**
 * Struct CipTcpIpNetworkInterfaceConfiguration
 * is for holding TCP/IP interface information
 */
struct CipTcpIpInterfaceConfiguration
{
    CipTcpIpInterfaceConfiguration() :
        ip_address( 0 ),
        network_mask( 0 ),
        gateway( 0 ),
        name_server( 0 ),
        name_server_2( 0 )
    {}

    CipUdint    ip_address;
    CipUdint    network_mask;
    CipUdint    gateway;
    CipUdint    name_server;
    CipUdint    name_server_2;
    std::string domain_name;
};


class CipTCPIPInterfaceInstance : public CipInstance
{
    friend class CipTCPIPInterfaceClass;

public:
    CipTCPIPInterfaceInstance( int aInstanceId );

protected:
    // Attributes of a TCP/IP Interface instance are numbered #

    /// #1  TCP status with 1 we indicate that we got a valid
    /// configuration from DHCP or BOOTP or non volatile storage
    CipDword tcp_status;

    /// #2  This is a default value meaning that it is a DHCP client
    /// see Vol2 5-4.3.2.2:
    CipDword configuration_capability;

    /// #3  This is a TCP/IP object attribute. For now it is always zero
    /// and is not used for anything.
    CipDword configuration_control;

    /// #5 IP, network mask, gateway, name server 1 & 2, domain name
    CipTcpIpInterfaceConfiguration interface_configuration;

    /// #6 Hostname, static so its shared betweeen instances of this class.
    static std::string hostname;

    /**
    * #8 the time to live value to be used for multi-cast connections
     *
     * Currently we implement it non set-able and with the default value of 1.
     */
    EipUint8 time_to_live;

    /**
     * #9 The multicast configuration for this device
     *
     * Currently we implement it non set-able and use the default
     * allocation algorithm
     */
    MulticastAddressConfiguration multicast_configuration;

    EipStatus configureNetworkInterface(
            const char* ip_address,
            const char* subnet_mask,
            const char* gateway );


    //-----<AttrubuteFuncs>-----------------------------------------------------

    static EipStatus get_attr_4( CipAttribute* aAttribute,
            CipMessageRouterRequest* aRequest,
            CipMessageRouterResponse* aResponse );

    static EipStatus get_attr_5( CipAttribute* aAttribute,
            CipMessageRouterRequest* aRequest,
            CipMessageRouterResponse* aResponse );

    static EipStatus get_multicast_config( CipAttribute* aAttribute,
            CipMessageRouterRequest* aRequest,
            CipMessageRouterResponse* aResponse );

    static EipStatus set_multicast_config( CipAttribute* aAttribute,
            CipMessageRouterRequest* aRequest,
            CipMessageRouterResponse* aResponse );

    static EipStatus get_attr_7( CipAttribute* aAttribute,
            CipMessageRouterRequest* aRequest,
            CipMessageRouterResponse* aResponse );

    static EipStatus set_attr_13( CipAttribute* aAttribute,
            CipMessageRouterRequest* aRequest,
            CipMessageRouterResponse* aResponse );

    static EipStatus set_TTL( CipAttribute* aAttribute,
            CipMessageRouterRequest* aRequest,
            CipMessageRouterResponse* aResponse );

    //-----</AttrubuteFuncs>----------------------------------------------------
};


class CipTCPIPInterfaceClass : public CipClass
{
public:
    CipTCPIPInterfaceClass();

    /// overload Instance() to return proper derived CipInstance type
    CipTCPIPInterfaceInstance* Instance( int aInstanceId );

    /**
     * Function Init
     * initializes the data structures of the TCP/IP interface objects.
     */
    static EipStatus Init();

    /**
     * Function ShutdownTcpIpInterface
     * cleans up the allocated data of the TCP/IP interface objects
     */
    static void Shutdown();

    //-----<CipServiceFunctions>------------------------------------------------

    // overload GetAttributeAll because we have to fill in gaps
    // for "not implemented" attributes.
    static EipStatus get_all( CipInstance* aInstance,
        CipMessageRouterRequest* aRequest, CipMessageRouterResponse* aResponse );

    //-----</CipServiceFunctions>-----------------------------------------------

    static const MulticastAddressConfiguration& MultiCast( int aInstanceId );

    static const CipTcpIpInterfaceConfiguration& InterfaceConf( int aInstanceId );

    static EipByte TTL( int aInstanceId );

    static CipUdint IpAddress( int aInstanceId );


    /** @ingroup CIP_API
     * @brief Configure the data of the network interface of the device
     *
     *  This function setup the data of the network interface needed by CIPster.
     *  The multicast address is automatically calculated from he given data.
     *
     * @param aInstanceId is the CipTCPIPInterfaceInstance instance id, starting at 1.
     *   These objects reside in class CipTCPIPInterfaceClass, and there normally only 1.
     *  @param ip_address    the current IP address of the device
     *  @param subnet_mask  the subnet mask to be used
     *  @param gateway_address     the gateway address
     *  @return kEipStatusOk if the configuring worked otherwise EIP_ERROR
     */
    static EipStatus ConfigureNetworkInterface( int aInstanceId, const char* ip_address,
            const char* subnet_mask, const char* gateway_address );

    /** @ingroup CIP_API
     * @brief Configure the domain name of the device
     * @param domain_name the domain name to be used
     */
    static void ConfigureDomainName( int aInstanceId, const char* aDomainName );

    /** @ingroup CIP_API
     * @brief Configure the host name of the device
     * @param aHostName the host name to be used
     */
    static void ConfigureHostName( int aInstanceId, const char* aHostName );
};

#endif    // CIPTCPIPINTERFACE_H_
