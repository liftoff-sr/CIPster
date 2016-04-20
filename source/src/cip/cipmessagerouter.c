/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/

#include <unordered_map>

#include "opener_api.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "endianconv.h"
#include "ciperror.h"
#include "trace.h"

CipMessageRouterRequest     g_request;
CipMessageRouterResponse    g_response;


/**
 * Class CipClassRegistry
 * is a container for the defined CipClass()es, which in turn hold all
 * the CipInstance()s.
 */
class CipClassRegistry
{
    typedef std::unordered_map< int, CipClass* >    ClassHash;

public:
    CipClass*   FindClass( int aClassId )
    {
        ClassHash::iterator it = container.find( aClassId );

        if( it != container.end() )
            return it->second;

        return NULL;
    }

    /** @brief Register a Class in the CIP class registry for the message router
     *  @param cip_class Pointer to a class object to be registered.
     *  @return bool - true.. success
     *                 false.. class with conflicting class_id is already registered
     */
    bool RegisterClass( CipClass* aClass )
    {
        ClassHash::value_type e( aClass->class_id, aClass );

        std::pair< ClassHash::iterator, bool > r = container.insert( e );

        return r.second;
    }

    void DeleteAll()
    {
        while( container.size() )
        {
            delete container.begin()->second;       // Delete the first of remaining classes
            container.erase( container.begin() );   // Erase first class's ClassEntry
        }
    }

    ~CipClassRegistry()
    {
        DeleteAll();
    }

private:

    ClassHash   container;
};


CipClassRegistry    g_class_registry;


void DeleteAllClasses()
{
    g_class_registry.DeleteAll();
}


EipStatus RegisterCipClass( CipClass* cip_class )
{
    if( g_class_registry.RegisterClass( cip_class ) )
        return kEipStatusOk;
    else
        return kEipStatusError;
}


CipClass* GetCipClass( EipUint32 class_id )
{
    return g_class_registry.FindClass( class_id );
}


/** @brief Create Message Router Request structure out of the received data.
 *
 * Parses the UCMM header consisting of: service, IOI size, IOI, data into a request structure
 * @param data pointer to the message data received
 * @param data_length number of bytes in the message
 * @param request pointer to structure of MRRequest data item.
 * @return status  0 .. success
 *                 -1 .. error
 */
CipError CreateMessageRouterRequestStructure( EipUint8* data, EipInt16 data_length,
        CipMessageRouterRequest* request );

EipStatus CipMessageRouterInit()
{
    CipClass* clazz = CreateCipClass( kCipMessageRouterClassCode,   // class ID
            0xffffffff,                                             // class getAttributeAll mask
            0xffffffff,                                             // instance getAttributeAll mask
            1,                                                      // # of instances
            "message router",                                       // class name
            1 );                                                    // revision

    if( !clazz )
        return kEipStatusError;

    // reserved for future use -> set to zero
    g_response.reserved = 0;

    // set reply buffer, using a fixed buffer (about 100 bytes)
    g_response.data = g_message_data_reply_buffer;

    return kEipStatusOk;
}


CipInstance* GetCipInstance( CipClass* cip_class, EipUint32 instance_id )
{
    // if the instance number is zero, return the class object itself
    return cip_class->Instance( instance_id );
}


EipStatus NotifyMR( EipUint8* data, int data_length )
{
    EipStatus   eip_status = kEipStatusOkSend;

    // set reply buffer, using a fixed buffer (about 100 bytes)
    g_response.data = g_message_data_reply_buffer;

    OPENER_TRACE_INFO( "notifyMR: routing unconnected message\n" );

    EipByte nStatus = CreateMessageRouterRequestStructure(
                        data, data_length, &g_request );

    if( nStatus != kCipErrorSuccess )
    {
        OPENER_TRACE_ERR( "notifyMR: error from createMRRequeststructure\n" );
        g_response.general_status = nStatus;
        g_response.size_of_additional_status = 0;
        g_response.reserved = 0;
        g_response.data_length = 0;
        g_response.reply_service = 0x80 | g_request.service;
    }
    else
    {
        // Forward request to appropriate Object if it is registered.
        CipClass*   clazz = GetCipClass( g_request.request_path.class_id );

        if( !clazz )
        {
            OPENER_TRACE_ERR(
                    "notifyMR: sending CIP_ERROR_OBJECT_DOES_NOT_EXIST reply, class id 0x%x is not registered\n",
                    (unsigned) g_request.request_path.class_id );

            // According to the test tool this should be the correct error flag
            // instead of CIP_ERROR_OBJECT_DOES_NOT_EXIST;
            g_response.general_status = kCipErrorPathDestinationUnknown;

            g_response.size_of_additional_status = 0;
            g_response.reserved = 0;
            g_response.data_length = 0;
            g_response.reply_service = 0x80 | g_request.service;
        }
        else
        {
            // Call notify function from Object with ClassID (gMRRequest.RequestPath.ClassID)
            // object will or will not make an reply into gMRResponse.
            g_response.reserved = 0;

            OPENER_TRACE_INFO( "notifyMR: calling notify function of class '%s'\n",
                    clazz->class_name.c_str() );

            eip_status = NotifyClass( clazz, &g_request, &g_response );

#ifdef OPENER_TRACE_ENABLED
            if( eip_status == kEipStatusError )
            {
                OPENER_TRACE_ERR(
                        "notifyMR: notify function of class '%s' returned an error\n",
                        clazz->class_name.c_str() );
            }
            else if( eip_status == kEipStatusOk )
            {
                OPENER_TRACE_INFO(
                        "notifyMR: notify function of class '%s' returned no reply\n",
                        clazz->class_name.c_str() );
            }
            else
            {
                OPENER_TRACE_INFO(
                        "notifyMR: notify function of class '%s' returned a reply\n",
                        clazz->class_name.c_str() );
            }
#endif
        }
    }

    return eip_status;
}


CipError CreateMessageRouterRequestStructure( EipUint8* data, EipInt16 data_length,
        CipMessageRouterRequest* request )
{
    int number_of_decoded_bytes;

    request->service = *data;
    data++; //TODO: Fix for 16 bit path lengths (+1
    data_length--;

    number_of_decoded_bytes = DecodePaddedEPath(
            &(request->request_path), &data );

    if( number_of_decoded_bytes < 0 )
    {
        return kCipErrorPathSegmentError;
    }

    request->data = data;
    request->data_length = data_length - number_of_decoded_bytes;

    if( request->data_length < 0 )
        return kCipErrorPathSizeInvalid;
    else
        return kCipErrorSuccess;
}

