
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
    kGetAttributeAll            = 0x01,
    kSetAttributeAll            = 0x02,
    kGetAttributeList           = 0x03,
    kSetAttributeList           = 0x04,
    kReset                      = 0x05,
    kStart                      = 0x06,
    kStop                       = 0x07,
    kCreate                     = 0x08,
    kDelete                     = 0x09,
    kMultipleServicePacket      = 0x0a,
    kApplyAttributes            = 0x0d,
    kGetAttributeSingle         = 0x0e,
    kSetAttributeSingle         = 0x10,
    kFindNextObjectInstance     = 0x11,
    kRestore                    = 0x15,
    kSave                       = 0x16,
    kNoOperation                = 0x17,
    kGetMember                  = 0x18,
    kSetMember                  = 0x19,
    kInsertMember               = 0x1a,
    kRemoveMember               = 0x1b,
    kGroupSync                  = 0x1c,

    // Start CIP class or instance specific services, probably should go in class specific area
    kForwardClose               = 0x4e,
    kUnconnectedSend            = 0x52,
    kForwardOpen                = 0x54,
    kGetConnectionOwner         = 0x5a,
    kLargeForwardOpen           = 0x5b,
    // End CIP class or instance specific services
};


/**
 * Typedef EipStatus (*CipServiceFunc)( CipInstance *,
 *    CipMessageRouterRequest*, CipMessageRouterResponse*)
 * is a CIP service function pointer.
 *
 * @param aInstance which was referenced in the service request
 * @param aRequest holds "data" coming from client, and it includes a length.
 * @param aResponse where to put the response, do it into member "data" which is length
 *  defined.  Upon completions update data_length to how many bytes were filled in.
 *
 * @return EipStatus - EipOKSend if service could be executed successfully
 *    and a response should be sent.
 */
typedef EipStatus (*CipServiceFunction)( CipInstance* aInstance,
        CipMessageRouterRequest* aRequest, CipMessageRouterResponse* aResponse );


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
        // replies often or in 0x80 to service code, so stay below
        CIPSTER_ASSERT( aServiceId > 0 && aServiceId < 0x80 );
    }

    virtual ~CipService() {}

    int  Id() const                         { return service_id; }

    const std::string& ServiceName() const  { return service_name; }

    CipServiceFunction  service_function;

protected:
    std::string         service_name;
    int                 service_id;
};

typedef std::vector<CipService*>       CipServices;


#endif  // CIPSERVICE_H_
