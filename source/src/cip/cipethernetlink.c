/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#include <string.h>

#include "cipethernetlink.h"

#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "ciperror.h"
#include "endianconv.h"
#include "opener_api.h"


struct CipEthernetLinkObject
{
    EipUint32   interface_speed;
    EipUint32   interface_flags;
    EipUint8    physical_address[6];
};

// global private variables
static CipEthernetLinkObject g_ethernet_link;

void ConfigureMacAddress( const EipUint8* mac_address )
{
    memcpy( &g_ethernet_link.physical_address, mac_address,
            sizeof(g_ethernet_link.physical_address) );
}


static CipInstance* createEthernetLinkInstance()
{
    CipClass*   clazz = GetCipClass( CIP_ETHERNETLINK_CLASS_CODE );

    CipInstance* i = new CipInstance( clazz->Instances().size() + 1 );

    i->AttributeInsert( 1, kCipUdint,  kGetableSingleAndAll, GetAttrData, NULL, &g_ethernet_link.interface_speed );
    i->AttributeInsert( 2, kCipDword,  kGetableSingleAndAll, GetAttrData, NULL, &g_ethernet_link.interface_flags );
    i->AttributeInsert( 3, kCip6Usint, kGetableSingleAndAll, GetAttrData, NULL, &g_ethernet_link.physical_address );

    return i;
}


EipStatus CipEthernetLinkInit()
{
    if( !GetCipClass( CIP_ETHERNETLINK_CLASS_CODE ) )
    {
        // set attributes to initial values
        g_ethernet_link.interface_speed = 100;

        // successful speed and duplex neg, full duplex active link,
        // TODO in future it should be checked if link is active
        g_ethernet_link.interface_flags = 0xF;

        CipClass* clazz = new CipClass( CIP_ETHERNETLINK_CLASS_CODE,
              "Ethernet Link",
              (1<<7)|(1<<6)|(1<<5)|(1<<4)|(1<<3)|(1<<2)|(1<<1),
              0xffffffff,               // class getAttributeAll mask
              0xffffffff,               // instance getAttributeAll mask
              1                         // version
              );

        RegisterCipClass( clazz );

        clazz->InstanceInsert( createEthernetLinkInstance() );
    }

    return kEipStatusOk;
}
