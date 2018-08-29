/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corporation.
 *
 ******************************************************************************/
#ifndef CIPSTER_CIPETHERNETLINK_H_
#define CIPSTER_CIPETHERNETLINK_H_

#include "typedefs.h"
#include "ciptypes.h"
#include "cipclass.h"


/**
 * Class CipEthernetLinkInstance
 */
class CipEthernetLinkInstance : public CipInstance
{
    friend class CipEthernetLinkClass;

public:
    CipEthernetLinkInstance( int aInstanceId ) :
        CipInstance( aInstanceId ),
        interface_speed( 100 ),

        // successful speed and duplex neg, full duplex active link,
        // TODO in future it should be checked if link is active
        interface_flags( 0xf ),
        physical_address()
    {}


protected:
    uint32_t   interface_speed;
    uint32_t   interface_flags;
    uint8_t    physical_address[6];
};


/**
 * Class CipEthernetLinkClass
 */
class CipEthernetLinkClass : public CipClass
{
public:

    /**
     * Function Init
     * initializes the Ethernet Link Objects
     */
    static EipStatus Init();

    static void ConfigureMacAddress( int aInstanceId, const uint8_t* mac_address );

    CipEthernetLinkClass();

    CipEthernetLinkInstance* CreateInstance();
};


#endif // CIPSTER_CIPETHERNETLINK_H_
