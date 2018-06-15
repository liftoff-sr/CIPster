
/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corportion.
 *
 ******************************************************************************/
#ifndef CIPSERVICE_H_
#define CIPSERVICE_H_

#include <string>
#include <typedefs.h>

/**
 * Typedef EipStatus (*CipServiceFunc)( CipInstance *,
 *    CipMessageRouterRequest*, CipMessageRouterResponse*)
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
typedef EipStatus (*CipServiceFunction)( CipInstance* aInstance,
        CipMessageRouterRequest* aRequest, CipMessageRouterResponse* aResponse );


/**
 * Class CipService
 * holds info for a CIP service and services may be contained within a CipClass.
 */
class CipService
{
public:
    CipService( const char* aServiceName = "", int aServiceId = 0,
            CipServiceFunction aServiceFunction = 0 ) :
        service_name( aServiceName ),
        service_id( aServiceId ),
        service_function( aServiceFunction )
    {
    }

    virtual ~CipService() {}

    int  Id() const                         { return service_id; }

    const std::string& ServiceName() const  { return service_name; }

    CipServiceFunction service_function;    ///< pointer to a function call

protected:
    std::string service_name;               ///< name of the service
    int         service_id;                 ///< service number
};

#endif  // CIPSERVICE_H_
