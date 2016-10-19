/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/

#include <unordered_map>
#include <string.h>

#include "opener_api.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "cipconnectionmanager.h"
#include "cipconnection.h"
#include "endianconv.h"
#include "ciperror.h"
#include "trace.h"

CipMessageRouterRequest     g_request;
CipMessageRouterResponse    g_response;

/// @brief Array of the available explicit connections
static CipConn g_explicit_connections[CIPSTER_CIP_NUM_EXPLICIT_CONNS];

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


CipClass* GetCipClass( int class_id )
{
    return g_class_registry.FindClass( class_id );
}



//-----------------------------------------------------------------------------

class CipMessageRouterClass : public CipClass
{
public:
    CipMessageRouterClass();
    CipError OpenConnection( CipConn* aConn, ConnectionManagerStatusCode* extended_error_code );    // override
};


CipMessageRouterClass::CipMessageRouterClass() :
    CipClass( kCipMessageRouterClassCode,
                "Message Router",  // class name
                (1<<7)|(1<<6)|(1<<5)|(1<<4)|(1<<3)|(1<<2)|(1<<1),
                0,                  // class getAttributeAll mask
                0,                  // instance getAttributeAll mask
                1                   // revision
                )
{
    // CIP_Vol_1 3.19 section 5A-3.3 shows that Message Router class has no
    // SetAttributeSingle.
    // Also, conformance test tool does not like SetAttributeSingle on this class,
    // delete the service which was established in CipClass constructor.
    delete ServiceRemove( kSetAttributeSingle );

    // reserved for future use -> set to zero
    g_response.reserved = 0;

    // set reply buffer, using a fixed buffer (about 100 bytes)
    g_response.data = g_message_data_reply_buffer;

    //TODO this is bad, use a CipConn constructor instead.
    memset( g_explicit_connections, 0, sizeof g_explicit_connections );
}


static CipConn* getFreeExplicitConnection()
{
    for( int i = 0; i < CIPSTER_CIP_NUM_EXPLICIT_CONNS; i++ )
    {
        if( g_explicit_connections[i].state == kConnectionStateNonExistent )
            return &g_explicit_connections[i];
    }

    return NULL;
}


CipError CipMessageRouterClass::OpenConnection( CipConn* aConn, ConnectionManagerStatusCode* extended_error )
{
    CipError ret = kCipErrorSuccess;

    EipUint32 producing_connection_id_buffer;

    // TODO add check for transport type trigger
    // if (0x03 == (g_stDummyCipConn.TransportTypeClassTrigger & 0x03))

    CipConn* explicit_connection = getFreeExplicitConnection();

    if( !explicit_connection )
    {
        ret = kCipErrorConnectionFailure;

        *extended_error = kConnectionManagerStatusCodeErrorNoMoreConnectionsAvailable;
    }
    else
    {
        CopyConnectionData( explicit_connection, aConn );

        producing_connection_id_buffer = explicit_connection->producing_connection_id;

        GeneralConnectionConfiguration( explicit_connection );

        explicit_connection->producing_connection_id = producing_connection_id_buffer;
        explicit_connection->instance_type = kConnInstanceTypeExplicit;

        explicit_connection->consuming_socket = kEipInvalidSocket;
        explicit_connection->producing_socket = kEipInvalidSocket;

        // set the connection call backs
        explicit_connection->connection_close_function = RemoveFromActiveConnections;

        // explicit connection have to be closed on time out
        explicit_connection->connection_timeout_function = RemoveFromActiveConnections;

        AddNewActiveConnection( explicit_connection );
    }

    return ret;
}


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
        CipClass* clazz = new CipMessageRouterClass();

        RegisterCipClass( clazz );

        createCipMessageRouterInstance();
    }

    return kEipStatusOk;
}


EipStatus NotifyMR( EipUint8* data, int data_length )
{
    EipStatus   eip_status = kEipStatusOkSend;

    // set reply buffer, using a fixed buffer (about 100 bytes)
    g_response.data = g_message_data_reply_buffer;

    CIPSTER_TRACE_INFO( "notifyMR: routing unconnected message\n" );

    CipError result = g_request.InitRequest( data, data_length );

    if( result != kCipErrorSuccess )
    {
        CIPSTER_TRACE_ERR( "notifyMR: error from createMRRequeststructure\n" );
        g_response.general_status = result;
        g_response.size_of_additional_status = 0;
        g_response.reserved = 0;
        g_response.data_length = 0;
        g_response.reply_service = 0x80 | g_request.service;
    }

    else
    {
        // Forward request to appropriate Object if it is registered.
        CipClass*   clazz = NULL;

        if( g_request.request_path.HasLogical() )
            clazz = GetCipClass( g_request.request_path.GetClass() );
        else if( g_request.request_path.HasSymbol() )
        {
            // per Rockwell Automation Publication 1756-PM020D-EN-P - June 2016:
            // Symbol Class Id is 0x6b.  Forward this request to that class.
            // This is not implemented in core stack, but can be added by
            // an application using this CIPster stack using simple
            // RegisterCipClass( CipClass* aClass );  In such a case I suppose instances
            // of this class might be tags.
            clazz = GetCipClass( 0x6b );
        }

        if( !clazz )
        {
            CIPSTER_TRACE_ERR(
                "%s: CIP_ERROR_OBJECT_DOES_NOT_EXIST reply, class:0x%02x not registered\n",
                __func__,
                g_request.request_path.GetClass()
                );

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
    EipUint8* start = aRequest;

    service = *aRequest++;

    int byte_count = *aRequest++ * 2;

    int result = request_path.DeserializePadded( aRequest, aRequest + byte_count );

    if( result <= 0 )
    {
        return kCipErrorPathSegmentError;
    }

    aRequest += result;
    data = aRequest;

    data_length = aCount - (aRequest - start);

    if( data_length < 0 )
        return kCipErrorPathSizeInvalid;
    else
        return kCipErrorSuccess;
}
