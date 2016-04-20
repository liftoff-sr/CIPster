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

typedef struct
{
    EipUint32   interface_speed;
    EipUint32   interface_flags;
    EipUint8    physical_address[6];
} CipEthernetLinkObject;

// global private variables
CipEthernetLinkObject g_ethernet_link;

void ConfigureMacAddress( const EipUint8* mac_address )
{
    memcpy( &g_ethernet_link.physical_address, mac_address,
            sizeof(g_ethernet_link.physical_address) );
}


EipStatus CipEthernetLinkInit()
{
    // set attributes to initial values
    g_ethernet_link.interface_speed = 100;

    // successful speed and duplex neg, full duplex active link,
    // TODO in future it should be checked if link is active
    g_ethernet_link.interface_flags = 0xF;

    CipClass* clazz = CreateCipClass( CIP_ETHERNETLINK_CLASS_CODE,
          0xffffffff,               // class getAttributeAll mask
          0xffffffff,               // instance getAttributeAll mask
          1,                        // # instances
          "Ethernet Link",
          1                         // version
          );

    if( clazz )
    {
        CipInstance* i = clazz->Instance( 1 );

        i->AttributeInsert( 1, kCipUdint,  &g_ethernet_link.interface_speed, kGetableSingleAndAll );
        i->AttributeInsert( 2, kCipDword,  &g_ethernet_link.interface_flags, kGetableSingleAndAll );
        i->AttributeInsert( 3, kCip6Usint, &g_ethernet_link.physical_address, kGetableSingleAndAll );
    }
    else
    {
        return kEipStatusError;
    }

    return kEipStatusOk;
}
