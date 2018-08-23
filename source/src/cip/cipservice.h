
/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/
#ifndef CIPSERVICE_H_
#define CIPSERVICE_H_

#include <string>
#include <typedefs.h>


/**
 * Enum CIPServiceCode
 * is the set of CIP service codes.
 * Common service codes range from 0x01 to 0x1c.  Beyond that there can
 * be class or instance specific service codes and some may overlap.
 */
enum CIPServiceCode
{
    kGetAttributeAll = 0x01,
    kSetAttributeAll = 0x02,
    kGetAttributeList = 0x03,
    kSetAttributeList = 0x04,
    kReset  = 0x05,
    kStart  = 0x06,
    kStop   = 0x07,
    kCreate = 0x08,
    kDelete = 0x09,
    kMultipleServicePacket = 0x0A,
    kApplyAttributes = 0x0D,
    kGetAttributeSingle = 0x0E,
    kSetAttributeSingle = 0x10,
    kFindNextObjectInstance = 0x11,
    kRestore = 0x15,
    kSave = 0x16,
    kNoOperation    = 0x17,
    kGetMember  = 0x18,
    kSetMember  = 0x19,
    kInsertMember = 0x1A,
    kRemoveMember = 0x1B,
    kGroupSync = 0x1C,

    // Start CIP class or instance specific services, probably should go in class specific area
    kForwardClose = 0x4E,
    kUnconnectedSend = 0x52,
    kForwardOpen = 0x54,
    kLargeForwardOpen = 0x5b,
    kGetConnectionOwner = 0x5A
    // End CIP class or instance specific services
};


/**
 * Typedef std::function<EipStatus (CipInstance*, CipMessageRouterRequest*, CipMessageRouterResponse*)> CipServiceFunction
 * is the function type for the implementation of CIP services.
 *
 * CIP services have to follow this signature in order to be handled correctly
 * by the stack.
 *
 * @param aInstance which was referenced in the service request
 * @param aRequest holds "data" coming from client, and it includes a length.
 * @param aResponse where to put the response, do it into member "data" which is length
 *  defined.  Upon completions update data_length to how many bytes were filled in.
 *
 * @return EipStatus - EipOKSend if service could be executed successfully
 *    and a response should be sent.
 */
typedef std::function<EipStatus (CipInstance*, CipMessageRouterRequest*, CipMessageRouterResponse*)> CipServiceFunction;


/**
 * Class CipService
 * holds info for a CIP service to be contained within a CipClass.
 */
class CipService
{
public:
    CipService( const char* aServiceName, int aServiceId,
            CipServiceFunction aServiceFunction ) :
        service_name( aServiceName ),
        service_id( aServiceId ),
        service_function( aServiceFunction )
    {
    }

    virtual ~CipService() {}

    int  Id() const                         { return service_id; }

    const std::string& ServiceName() const  { return service_name; }

    CipServiceFunction  service_function;

protected:
    std::string         service_name;
    int                 service_id;
};

#endif  // CIPSERVICE_H_
