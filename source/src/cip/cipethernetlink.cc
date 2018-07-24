/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/
#include <string.h>

#include <cipethernetlink.h>

#include <cipcommon.h>
#include <cipmessagerouter.h>
#include <ciperror.h>
#include <byte_bufs.h>
#include <cipster_api.h>
#include <cipclass.h>



CipEthernetLinkInstance* CipEthernetLinkClass::CreateInstance()
{
    CipEthernetLinkInstance* i = new CipEthernetLinkInstance( Instances().size() + 1 );

    i->AttributeInsert( 1, kCipUdint,  &i->interface_speed );
    i->AttributeInsert( 2, kCipDword,  &i->interface_flags );
    i->AttributeInsert( 3, kCip6Usint, &i->physical_address );

    InstanceInsert( i );

    return i;
}


CipEthernetLinkClass::CipEthernetLinkClass() :
    CipClass( kCipEthernetLinkClass,
              "Ethernet Link",
              MASK7(1,2,3,4,5,6,7), // common class attributes mask
              1                     // version
              )
{
}


EipStatus CipEthernetLinkClass::Init()
{
    if( !GetCipClass( kCipEthernetLinkClass ) )
    {
        CipEthernetLinkClass* clazz = new CipEthernetLinkClass();

        RegisterCipClass( clazz );

        // create instance 1
        clazz->CreateInstance();
    }

    return kEipStatusOk;
}


void CipEthernetLinkClass::ConfigureMacAddress( int aInstanceId, const EipUint8* mac_address )
{
    CipClass* clazz = GetCipClass( kCipEthernetLinkClass );

    if( clazz )
    {
        CipEthernetLinkInstance* i = static_cast<CipEthernetLinkInstance*>( clazz->Instance( aInstanceId ) );

        if( i )
        {
            memcpy( &i->physical_address, mac_address, sizeof i->physical_address );
        }
    }
}
