/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corporation.
 *
 ******************************************************************************/
#ifndef CIPSTER_CIPIDENTITY_H_
#define CIPSTER_CIPIDENTITY_H_

#include "typedefs.h"
#include "ciptypes.h"

/// Status of the CIP Identity object
enum CipIdentityStatus
{
    kOwned = 0x0001,                    ///< Indicates that the device has an owner
    kConfigured = 0x0004,               /**< Indicates that the device is configured to do
                                         *  something different, than the out-of-the-box default. */
    kMinorRecoverableFault  = 0x0100,   /**< Indicates that the device detected a
                                         *  fault with itself, which was thought to be recoverable. The device did not
                                         *  switch to a faulted state. */
    kMinorUncoverableFault  = 0x0200,   /**< Indicates that the device detected a
                                         *  fault with itself, which was thought to be recoverable. The device did not
                                         *  switch to a faulted state. */
    kMajorRecoveralbeFault  = 0x0400,   /**< Indicates that the device detected a
                                        *   fault with itself,which was thought to be recoverable. The device changed
                                        *   to the "Major Recoverable Fault" state */
    kMajorUnrecoverableFault = 0x0800   /**< Indicates that the device detected a
                                         *  fault with itself,which was thought to be recoverable. The device changed
                                         *  to the "Major Unrecoverable Fault" state */
};


enum CipIdentityExtendedStatus
{
    kSelftestingUnknown = 0x0000,
    FirmwareUpdateInProgress = 0x0010,
    kStatusAtLeastOneFaultedIoConnection = 0x0020,
    kNoIoConnectionsEstablished     = 0x0030,
    kNonVolatileConfigurationBad    = 0x0040,
    kMajorFault = 0x0050,
    kAtLeastOneIoConnectionInRuneMode = 0x0060,
    kAtLeastOneIoConnectionEstablishedAllInIdleMode = 0x0070
};


/**
 * Function CipIdentityInit
 * sets up the CIP Identity class and instance.
 *
 * @return EipStatus - EipError if failure, else EipOk
 */
EipStatus CipIdentityInit();


// Publics from the CIP identity object
extern uint16_t         vendor_id_;
extern uint16_t         device_type_;
extern uint16_t         product_code_;
extern CipRevision      revision_;
extern uint16_t         status_;
extern uint32_t         serial_number_;
extern std::string      product_name_;


#endif    // CIPSTER_CIPIDENTITY_H_
