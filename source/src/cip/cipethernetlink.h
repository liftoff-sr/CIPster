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
    EipUint32   interface_speed;
    EipUint32   interface_flags;
    EipUint8    physical_address[6];
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

    static void ConfigureMacAddress( int aInstanceId, const EipUint8* mac_address );

    CipEthernetLinkClass();

    CipEthernetLinkInstance* CreateInstance();
};


#endif // CIPSTER_CIPETHERNETLINK_H_
