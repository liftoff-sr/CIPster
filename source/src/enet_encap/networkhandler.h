/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016-2018, SoftPLC Corportion.
 *
 ******************************************************************************/

#ifndef NETWORKHANDLER_H_
#define NETWORKHANDLER_H_


#include "typedefs.h"

/**
 * Function NetworkHandlerInitialize
 * starts a TCP/UDP listening socket to accept connections.
 */
EipStatus NetworkHandlerInitialize();

EipStatus NetworkHandlerProcessOnce();

EipStatus NetworkHandlerFinish();


#endif  // NETWORKHANDLER_H_
