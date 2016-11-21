/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/

#include <unordered_map>
#include <string.h>


#include "cipster_api.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "cipconnectionmanager.h"
#include "cipconnection.h"
#include "byte_bufs.h"
#include "ciperror.h"
#include "trace.h"


/// @brief Array of the available explicit connections
static CipConn g_explicit_connections[CIPSTER_CIP_NUM_EXPLICIT_CONNS];

/**
 * Class CipClassRegistry
 * is a container for the defined CipClass()es, which in turn hold all
 * the CipInstance()s.  This container takes ownership of the CipClasses.
 * (Ownership means having the obligation to delete upon destruction.)
 */
class CipClassRegistry
{
    // hashtable from C++ std library.
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
        ClassHash::value_type e( aClass->ClassId(), aClass );

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


std::vector<EipByte> CipMessageRouterResponse::mmr_temp( CIPSTER_MESSAGE_DATA_REPLY_BUFFER );

CipMessageRouterResponse::CipMessageRouterResponse( CipCommonPacketFormatData* aCPFD ) :
    reply_service( 0 ),
    reserved( 0 ),
    general_status( 0 ),
    size_of_additional_status( 0 ),

    // shared resizeable response buffer.
    data( mmr_temp.data(), mmr_temp.size() ),
    data_length( 0 ),
    cpfd( aCPFD )
{
    memset( additional_status, 0, sizeof additional_status );
}


class CipMessageRouterClass : public CipClass
{
public:
    CipMessageRouterClass();

    CipError OpenConnection( CipConn* aConn,
        CipCommonPacketFormatData* cpfd,
        ConnectionManagerStatusCode* extended_error_code );    // override
};


CipMessageRouterClass::CipMessageRouterClass() :
    CipClass( kCipMessageRouterClassCode,
        "Message Router",
        MASK7(1,2,3,4,5,6,7),   // common class attributes mask
        0,                      // class getAttributeAll mask
        0,                      // instance getAttributeAll mask
        1                       // revision
        )
{
    // CIP_Vol_1 3.19 section 5A-3.3 shows that Message Router class has no
    // SetAttributeSingle.
    // Also, conformance test tool does not like SetAttributeSingle on this class,
    // delete the service which was established in CipClass constructor.
    delete ServiceRemove( kSetAttributeSingle );
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


CipError CipMessageRouterClass::OpenConnection( CipConn* aConn,
            CipCommonPacketFormatData* cpfd, ConnectionManagerStatusCode* extended_error )
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

        /* done by "static construction" of CipConn instances now:
        //TODO this is bad, use a CipConn constructor instead.
        memset( g_explicit_connections, 0, sizeof g_explicit_connections );
        */
    }

    return kEipStatusOk;
}


EipStatus NotifyMR( BufReader aCommand, CipMessageRouterResponse* aReply )
{
    EipStatus   eip_status = kEipStatusOkSend;

    CIPSTER_TRACE_INFO( "notifyMR: routing unconnected message\n" );

    CipMessageRouterRequest request;

    int result = request.DeserializeMRR( aCommand );

    if( result <= 0 )
    {
        CIPSTER_TRACE_ERR( "notifyMR: error from createMRRequeststructure\n" );
        aReply->general_status = kCipErrorPathSegmentError;
        aReply->size_of_additional_status = 0;
        aReply->reserved = 0;
        aReply->data_length = 0;
        aReply->reply_service = 0x80 | request.service;
    }

    else
    {
        // Forward request to appropriate Object if it is registered.
        CipClass*   clazz = NULL;

        if( request.request_path.HasLogical() )
            clazz = GetCipClass( request.request_path.GetClass() );
        else if( request.request_path.HasSymbol() )
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
                request.request_path.GetClass()
                );

            // According to the test tool this should be the correct error flag
            // instead of CIP_ERROR_OBJECT_DOES_NOT_EXIST;
            aReply->general_status = kCipErrorPathDestinationUnknown;

            aReply->size_of_additional_status = 0;
            aReply->reserved = 0;
            aReply->data_length = 0;
            aReply->reply_service = 0x80 | request.service;
        }
        else
        {
            // Call notify function from Object with ClassID (gMRRequest.RequestPath.ClassID)
            // object will or will not make an reply into gMRResponse.
            aReply->reserved = 0;

            CIPSTER_TRACE_INFO( "notifyMR: calling notify function of class '%s'\n",
                    clazz->ClassName().c_str() );

            eip_status = NotifyClass( clazz, &request, aReply );

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


int CipMessageRouterRequest::DeserializeMRR( BufReader aRequest )
{
    BufReader in = aRequest;

    service = *in++;

    int byte_count = *in++ * 2;     // word count x 2

    int result = request_path.DeserializeAppPath( in );

    if( result <= 0 )
    {
        return result;
    }

    int bytes_consumed = in.data() - aRequest.data() + result;

    data = aRequest + bytes_consumed;   // set this->data

    return bytes_consumed;   // how many bytes consumed
}

