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
     *  @param aClass a class object to be registered, and to take ownership over.
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


static CipClassRegistry    g_class_registry;


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



//-----------------------------------------------------------------------------

static CipInstance* createCipMessageRouterInstance()
{
    CipClass* clazz = GetCipClass( kCipMessageRouterClassCode );

    CipInstance* i = new CipInstance( clazz->Instances().size() + 1 );

    clazz->InstanceInsert( i );

    return i;
}


EipStatus CipMessageRouterInit()
{
    // may not already be registered.
    if( !GetCipClass( kCipMessageRouterClassCode ) )
    {
        CipClass* clazz = new CipClass( kCipMessageRouterClassCode,
                "Message Router",  // class name
                (1<<7)|(1<<6)|(1<<5)|(1<<4)|(1<<3)|(1<<2)|(1<<1),
                0,                  // class getAttributeAll mask
                0,                  // instance getAttributeAll mask
                1                   // revision
                );

        RegisterCipClass( clazz );

        // CIP_Vol_1 3.19 section 5A-3.3 shows that Message Router class has no
        // SetAttributeSingle.
        // Also, conformance test tool does not like SetAttributeSingle on this class,
        // delete the service which was established in CipClass constructor.
        delete clazz->ServiceRemove( kSetAttributeSingle );

        createCipMessageRouterInstance();

        // reserved for future use -> set to zero
        g_response.reserved = 0;

        // set reply buffer, using a fixed buffer (about 100 bytes)
        g_response.data = g_message_data_reply_buffer;
    }

    return kEipStatusOk;
}


EipStatus NotifyMR( EipUint8* data, int data_length )
{
    EipStatus   eip_status = kEipStatusOkSend;

    // set reply buffer, using a fixed buffer (about 100 bytes)
    g_response.data = g_message_data_reply_buffer;

    CIPSTER_TRACE_INFO( "notifyMR: routing unconnected message\n" );

    EipByte nStatus = g_request.InitRequest( data, data_length );

    if( nStatus != kCipErrorSuccess )
    {
        CIPSTER_TRACE_ERR( "notifyMR: error from createMRRequeststructure\n" );
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
            CIPSTER_TRACE_ERR(
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

            CIPSTER_TRACE_INFO( "notifyMR: calling notify function of class '%s'\n",
                    clazz->ClassName().c_str() );

            eip_status = NotifyClass( clazz, &g_request, &g_response );

#ifdef CIPSTER_TRACE_ENABLED
            if( eip_status == kEipStatusError )
            {
                CIPSTER_TRACE_ERR(
                        "notifyMR: notify function of class '%s' returned an error\n",
                        clazz->ClassName().c_str() );
            }
            else if( eip_status == kEipStatusOk )
            {
                CIPSTER_TRACE_INFO(
                        "notifyMR: notify function of class '%s' returned no reply\n",
                        clazz->ClassName().c_str() );
            }
            else
            {
                CIPSTER_TRACE_INFO(
                        "notifyMR: notify function of class '%s' returned a reply\n",
                        clazz->ClassName().c_str() );
            }
#endif
        }
    }

    return eip_status;
}


CipError CipMessageRouterRequest::InitRequest( EipUint8* aRequest, EipInt16 aCount )
{
    service = *aRequest++;

    --aCount;

    int number_of_decoded_bytes = DecodePaddedEPath( &request_path, &aRequest );

    if( number_of_decoded_bytes < 0 )
    {
        return kCipErrorPathSegmentError;
    }

    data = aRequest;
    data_length = aCount - number_of_decoded_bytes;

    if( data_length < 0 )
        return kCipErrorPathSizeInvalid;
    else
        return kCipErrorSuccess;
}
