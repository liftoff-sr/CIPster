/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#ifndef CIPSTER_CIPETHERNETLINK_H_
#define CIPSTER_CIPETHERNETLINK_H_

#include "typedefs.h"
#include "ciptypes.h"

#define CIP_ETHERNETLINK_CLASS_CODE 0xF6

// public functions
/** @brief Initialize the Ethernet Link Objects data
 */
EipStatus CipEthernetLinkInit(void);

#endif // CIPSTER_CIPETHERNETLINK_H_
