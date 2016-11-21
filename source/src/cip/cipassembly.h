/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/
#ifndef CIPSTER_CIPASSEMBLY_H_
#define CIPSTER_CIPASSEMBLY_H_

#include "typedefs.h"
#include "ciptypes.h"


// public functions

/** @brief Setup the Assembly object
 *
 * Creates the Assembly Class with zero instances and sets up all services.
 *
 * @return Returns kEipStatusOk if assembly object was successfully created, otherwise kEipStatusError
 */
EipStatus CipAssemblyInitialize();


/** @brief notify an Assembly object that data has been received for it.
 *
 *  The data will be copied into the assembly instance's attribute 3 and
 *  the application will be informed with the IApp_after_assembly_data_received function.
 *
 *  @param aInstance the assembly instance for which the data was received
 *  @param aInput the byte data received and its length
 *  @return
 *     - EIP_OK the received data was okay
 *     - EIP_ERROR the received data was wrong
 */
EipStatus NotifyAssemblyConnectedDataReceived( CipInstance* aInstance, BufReader aInput );

#endif // CIPSTER_CIPASSEMBLY_H_
