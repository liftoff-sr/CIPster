/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#include <stdexcept>

#include <ciptcpipinterface.h>

#include <trace.h>
#include <cipcommon.h>
#include <cipmessagerouter.h>
#include <ciperror.h>
#include <byte_bufs.h>
#include <cipethernetlink.h>
#include <cipster_api.h>

#undef  INSTANCE_CLASS
#define INSTANCE_CLASS  CipTCPIPInterfaceInstance


// A static pointer to the only class object, this avoids Registry lookup,
// thus improving speed in the API Functions.
static CipTCPIPInterfaceClass* s_tcp;

CipUint CipTCPIPInterfaceInstance::inactivity_timeout_secs = 120;  // spec default

std::string CipTCPIPInterfaceInstance::hostname;


CipTCPIPInterfaceInstance::CipTCPIPInterfaceInstance( int aInstanceId ) :
    CipInstance( aInstanceId ),
    status( 1 ),        // attribute_id 1

    configuration_capability(
        0
                |   (1<<0)  // BootP client
                |   (1<<1)  // DNS capable
                |   (1<<2)  // Bit 2  => "DHCP Client"
                |   (1<<5)  // Bit 5  => "Hardware Configurable"
        ),

    configuration_control( 0 ),
    time_to_live( 1 )
{
}


//-----<AttrubuteFuncs>-----------------------------------------------------

EipStatus CipTCPIPInterfaceInstance::get_attr_4( CipInstance* aInstance,
        CipAttribute* attribute,
        CipMessageRouterRequest* aRequest,
        CipMessageRouterResponse* aResponse )
{
    EipStatus   status = kEipStatusOkSend;
    BufWriter   out = aResponse->Writer();

    CipAppPath app_path;

    app_path.SetClass( kCipEthernetLinkClass );
    app_path.SetInstance( aInstance->Id() );

    int result = app_path.Serialize( out + 2 );

    out.put16( result/2 );      // word count as 16 bits

    out += result;

    aResponse->SetWrittenSize( out.data() - aResponse->Writer().data() );

    return status;
}


EipStatus CipTCPIPInterfaceInstance::get_attr_5( CipInstance* aInstance,
        CipAttribute* attribute,
        CipMessageRouterRequest* aRequest,
        CipMessageRouterResponse* aResponse )
{
    BufWriter   out = aResponse->Writer();

    CipTCPIPInterfaceInstance* inst = static_cast<CipTCPIPInterfaceInstance*>( aInstance );

    const CipTcpIpInterfaceConfiguration& c = inst->interface_configuration;

    out.put32( ntohl( c.ip_address ) );
    out.put32( ntohl( c.network_mask ) );
    out.put32( ntohl( c.gateway ) );
    out.put32( ntohl( c.name_server ) );
    out.put32( ntohl( c.name_server_2 ) );
    out.put_STRING( c.domain_name, true /* yes pad to even */ );

    aResponse->SetWrittenSize( out.data() - aResponse->Writer().data() );

    return kEipStatusOkSend;
}


// Attribute 9
EipStatus CipTCPIPInterfaceInstance::get_multicast_config( CipInstance* aInstance,
        CipAttribute* attribute,
        CipMessageRouterRequest* aRequest,
        CipMessageRouterResponse* aResponse )
{
    EipStatus   status = kEipStatusOkSend;
    BufWriter   out = aResponse->Writer();

    CipTCPIPInterfaceInstance* i = static_cast<CipTCPIPInterfaceInstance*>( aInstance );

    out.put8( i->multicast_configuration.alloc_control );
    out.put8( 0 );
    out.put16( i->multicast_configuration.number_of_allocated_multicast_addresses );

    uint32_t ma = ntohl( i->multicast_configuration.starting_multicast_address );
    out.put32( ma );

    aResponse->SetWrittenSize( out.data() - aResponse->Writer().data() );
    return status;
}


EipStatus CipTCPIPInterfaceInstance::set_multicast_config( CipInstance* aInstance,
        CipAttribute* attribute,
        CipMessageRouterRequest* aRequest,
        CipMessageRouterResponse* aResponse )
{
    BufReader   in = aRequest->Data();
    CipTCPIPInterfaceInstance* i = static_cast<CipTCPIPInterfaceInstance*>( aInstance );
    MulticastAddressConfiguration* mc = &i->multicast_configuration;

    mc->alloc_control = in.get8();
    mc->reserved_zero = in.get8();
    mc->number_of_allocated_multicast_addresses = in.get16();
    mc->starting_multicast_address = htonl( in.get32() );

    return kEipStatusOkSend;
}


EipStatus CipTCPIPInterfaceInstance::get_attr_7( CipInstance* aInstance,
        CipAttribute* attribute,
        CipMessageRouterRequest* aRequest,
        CipMessageRouterResponse* aResponse )
{
    BufWriter out = aResponse->Writer();

    // insert 6 zeros for the required empty safety network number
    // according to Vol2 Table 5-4.15
    out.fill( 6 );

    aResponse->SetWrittenSize( 6 );

    return kEipStatusOkSend;
}


EipStatus CipTCPIPInterfaceInstance::set_attr_13( CipInstance* aInstance,
        CipAttribute* attribute,
        CipMessageRouterRequest* aRequest,
        CipMessageRouterResponse* aResponse )
{
    // all instances are sharing a common value for this attribute so ignore instance
    inactivity_timeout_secs = BufReader( aRequest->Data() ).get16();

    // [write it to disk here?]

    return kEipStatusOkSend;
}


// This is here to protect against setting to zero
EipStatus CipTCPIPInterfaceInstance::set_TTL( CipInstance* aInstance,
        CipAttribute* attribute,
        CipMessageRouterRequest* aRequest,
        CipMessageRouterResponse* aResponse )
{
    uint8_t ttl  = BufReader( aRequest->Data() ).get8();

    if( ttl == 0 )
        aResponse->SetGenStatus( kCipErrorInvalidAttributeValue );
    else
        *(uint8_t*) aInstance->Data( attribute ) = ttl;

    // [write it to disk here?]

    return kEipStatusOkSend;
}


EipStatus CipTCPIPInterfaceInstance::configureNetworkInterface(
        const char* ip_address,
        const char* subnet_mask,
        const char* gateway )
{
    // Save all these values in network byte order

    interface_configuration.ip_address   = inet_addr( ip_address );
    interface_configuration.network_mask = inet_addr( subnet_mask );
    interface_configuration.gateway      = inet_addr( gateway );

    // Calculate the CIP multicast address. The multicast address is calculated, not input.
    // See CIP spec Vol2 3-5.3 for multicast address algorithm.
    uint32_t host_id = ntohl( interface_configuration.ip_address )
                        & ~ntohl( interface_configuration.network_mask );
    host_id -= 1;
    host_id &= 0x3ff;

    multicast_configuration.starting_multicast_address = htonl(
            ntohl( inet_addr( "239.192.1.0" ) ) + (host_id << 5) );

    return kEipStatusOk;
}

//-----</AttrubuteFuncs>--------------------------------------------------------


//-----<CipServiceFunctions>-----------------------------------------------------------

EipStatus CipTCPIPInterfaceInstance::get_all( CipInstance* aInstance,
        CipMessageRouterRequest* aRequest, CipMessageRouterResponse* aResponse )
{
    CipTCPIPInterfaceInstance* i = static_cast< CipTCPIPInterfaceInstance* >( aInstance );

    BufWriter out = aResponse->Writer();

    // output attributes 1, 2, & 3
    out.put32( i->status )
    .put32( i->configuration_capability )
    .put32( i->configuration_control );

    // attribute 4
    CipAppPath app_path;
    app_path.SetClass( kCipEthernetLinkClass );
    app_path.SetInstance( i->Id() );
    int path_len = app_path.Serialize( out + 2 );
    out.put16( path_len/2 );      // word count as 16 bits
    out += path_len;

    // attribute 5
    const CipTcpIpInterfaceConfiguration& c = i->interface_configuration;

    out.put32( ntohl( c.ip_address ) )
    .put32( ntohl( c.network_mask ) )
    .put32( ntohl( c.gateway ) )
    .put32( ntohl( c.name_server ) )
    .put32( ntohl( c.name_server_2 ) )
    .put_STRING( c.domain_name, true );

    // attribute 6
    out.put_STRING( i->hostname, true );

    // attribute 7, 6 zeros
    out.fill( 6 );

    // attribute 8
    out.put8( i->time_to_live );

    // attribute 9
    out.put8( i->multicast_configuration.alloc_control );
    out.put8( 0 );
    out.put16( i->multicast_configuration.number_of_allocated_multicast_addresses );
    uint32_t ma = ntohl( i->multicast_configuration.starting_multicast_address );
    out.put32( ma );

    // attribute 10
    out.put8( 0 );

    // attribute 11
    out.put8( 0 ).fill( 6+28 );

    // attribute 12
    out.put8( 0 );

    // attribute 13
    out.put16( i->inactivity_timeout_secs );

    aResponse->SetWrittenSize( out.data() - aResponse->Writer().data() );

    return kEipStatusOk;
}

//-----</CipServiceFunctions>---------------------------------------------------


//-----</CipTCPIPInterfaceInstance>---------------------------------------------


//-----<CipTCPIPInterfaceClass>-------------------------------------------------

CipTCPIPInterfaceClass::CipTCPIPInterfaceClass() :
    CipClass( kCipTcpIpInterfaceClass,
        "TCP/IP Interface",
#if 0
        MASK7(1,2,3,4,5,6,7),   // common class attributes mask
#else
        // The Vol2 spec for this class says 4-7 are optional,
        // but conformance test software may whine about 4 & 5 so omit them.
        MASK5(1,2,3,6,7),       // common class attributes mask
#endif
        4                         // version
        )
{
    // overload an instance service
    ServiceInsert( _I, kGetAttributeAll, CipTCPIPInterfaceInstance::get_all, "GetAttributeAll" );

    AttributeInsert( _I, 1, kCipDword, memb_offs(status) );
    AttributeInsert( _I, 2, kCipDword, memb_offs(configuration_capability) );
    AttributeInsert( _I, 3, kCipDword, memb_offs(configuration_control) );
    AttributeInsert( _I, 4, CipTCPIPInterfaceInstance::get_attr_4 );
    AttributeInsert( _I, 5, CipTCPIPInterfaceInstance::get_attr_5 );
    AttributeInsert( _I, 6, kCipString, &CipTCPIPInterfaceInstance::hostname );

    //AttributeInsert( _I, 7, get_attr_7 );

    // Use a standard method to Get the attribute, but a custom one to Set it.
    AttributeInsert( _I, 8, CipAttribute::GetAttrData, true, CipTCPIPInterfaceInstance::set_TTL, memb_offs(time_to_live), true, kCipUsint );

    AttributeInsert( _I, 9, CipTCPIPInterfaceInstance::get_multicast_config, true, CipTCPIPInterfaceInstance::set_multicast_config );

    // Use a standard method to Get the attribute, but a custom one to Set it.
    // This would also be a good place to read it from disk or non volatile storage.
    AttributeInsert( _I, 13, CipAttribute::GetAttrData, true, CipTCPIPInterfaceInstance::set_attr_13, (uintptr_t) &CipTCPIPInterfaceInstance::inactivity_timeout_secs, false, kCipUint );
}


CipTCPIPInterfaceInstance* CipTCPIPInterfaceClass::Instance( int aInstanceId )
{
#if 1
    // will throw if you have a bad aInstanceId or used non contiguous ids
    // during construction. But this is the fastest strategy.
    CipTCPIPInterfaceInstance* inst = static_cast<CipTCPIPInterfaceInstance*>( instances[aInstanceId-1] );
#else
    CipTCPIPInterfaceInstance* inst = static_cast<CipTCPIPInterfaceInstance*>( CipClass::Instance( aInstanceId ) );
    if( !inst )
        throw std::range_error( "bad tcpip instance no." );
#endif

    return inst;
}


//----<API Funcs>---------------------------------------------------------------
const MulticastAddressConfiguration& CipTCPIPInterfaceClass::MultiCast( int aInstanceId )
{
    CipTCPIPInterfaceInstance* inst = s_tcp->Instance( aInstanceId );

    return inst->multicast_configuration;
}


const CipTcpIpInterfaceConfiguration& CipTCPIPInterfaceClass::InterfaceConf( int aInstanceId )
{
    CipTCPIPInterfaceInstance* inst = s_tcp->Instance( aInstanceId );

    return inst->interface_configuration;
}


uint8_t CipTCPIPInterfaceClass::TTL( int aInstanceId )
{
    CipTCPIPInterfaceInstance* inst = s_tcp->Instance( aInstanceId );
    return inst->time_to_live;
}


CipUdint CipTCPIPInterfaceClass::IpAddress( int aInstanceId )
{
    CipTCPIPInterfaceInstance* inst = s_tcp->Instance( aInstanceId );
    return inst->interface_configuration.ip_address;
}


EipStatus CipTCPIPInterfaceClass::ConfigureNetworkInterface( int aInstanceId,
        const char* ip_address,
        const char* subnet_mask,
        const char* gateway )
{
    CipTCPIPInterfaceInstance* inst = s_tcp->Instance( aInstanceId );

    return inst->configureNetworkInterface( ip_address, subnet_mask, gateway );
}


void CipTCPIPInterfaceClass::ConfigureDomainName( int aInstanceId, const char* aDomainName )
{
    CipTCPIPInterfaceInstance* inst = s_tcp->Instance( aInstanceId );

    inst->interface_configuration.domain_name = aDomainName;
}


void CipTCPIPInterfaceClass::ConfigureHostName( int aInstanceId, const char* aHostName )
{
    // hostname is actually static here, but code it as an instance variable.
    // Compiler figures it out either way.
    CipTCPIPInterfaceInstance* inst = s_tcp->Instance( aInstanceId );

    inst->hostname = aHostName;
}


EipStatus CipTCPIPInterfaceClass::Init()
{
    if( !GetCipClass( kCipTcpIpInterfaceClass ) )
    {
        CipTCPIPInterfaceClass* clazz = new CipTCPIPInterfaceClass();

        s_tcp = clazz;

        RegisterCipClass( clazz );

        // Add one of these for each TCP/IP interface, each with a unique
        // instance id starting at 1.  Must be contiguously numbered.
        // Do not use gaps in the id sequence because of the array indexing
        // done in CipTCPIPInterfaceClass::Instance()
        clazz->InstanceInsert( new CipTCPIPInterfaceInstance( 1 ) );
    }

    return kEipStatusOk;
}

void CipTCPIPInterfaceClass::Shutdown()
{
}
