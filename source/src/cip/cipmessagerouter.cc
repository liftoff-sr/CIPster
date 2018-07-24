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



//-----<CipMessageRounterRequest>-----------------------------------------------

int CipMessageRouterRequest::Serialize( BufWriter aOutput, int aCtl ) const
{
    BufWriter out = aOutput;

    out.put8( service );

    // out + 1 skips over unset word count byte.
    int rplen = path.Serialize( out + 1, aCtl );

    out.put8( rplen / 2 );     // set word count = byte_count / 2

    out += rplen;   // skip over path

    out.append( Data().data(), Data().size() );

    return out.data() - aOutput.data();
}


int CipMessageRouterRequest::SerializedCount( int aCtl ) const
{
    return 2 + path.SerializedCount( aCtl ) + Data().size();
}


int CipMessageRouterRequest::DeserializeMRReq( BufReader aRequest )
{
    BufReader in = aRequest;

    service = (CIPServiceCode) in.get8();

    unsigned byte_count = in.get8() * 2;     // word count x 2

    if( byte_count > in.size() )
    {
        return -1;
    }

    // limit the length of the request input so it pertains only to request path
    BufReader rpath( in.data(), byte_count );

    // Vol1 2-4.1.1
    CipElectronicKeySegment key;

    int result = key.DeserializeElectronicKey( rpath );

    if( result < 0 )
        return result;

    rpath += result;

    result = path.DeserializeAppPath( rpath );

    if( result < 0 )
        return result;

    int bytes_consumed = rpath.data() - aRequest.data() + result;

    // set this->data for service functions, it consists of the remaining
    // part of the message, the part identified as "Request_Data" in Vol1 2-4.1
    data = aRequest + bytes_consumed;

    return bytes_consumed;
}



//-----<CipMessageRounterResponse>----------------------------------------------

std::vector<EipByte> CipMessageRouterResponse::mmr_temp( CIPSTER_MESSAGE_DATA_REPLY_BUFFER );

CipMessageRouterResponse::CipMessageRouterResponse( Cpf* aCpf ) :
    data ( mmr_temp.data(), mmr_temp.size() ),
    cpf( aCpf )
{
    Clear();
}


void CipMessageRouterResponse::Clear()
{
    reply_service = CIPServiceCode( 0 );
    general_status = kCipErrorSuccess,
    size_of_additional_status = 0;
    written_size = 0;

    memset( additional_status, 0, sizeof additional_status );
}


int CipMessageRouterResponse::DeserializeMRRes( BufReader aReply )
{
    BufReader in = aReply;

    reply_service = CIPServiceCode( in.get8() & 0x7f );

    in.get8();      // gobble "reserved"

    SetGenStatus( (CipError) in.get8() );

    size_of_additional_status = in.get8();

    for( int i = 0; i < size_of_additional_status;  ++i )
    {
        if( i == DIM(additional_status) )
            throw std::overflow_error(
                "CipMessageRouterRespoinse::DeserializeMRRes(): too many additional status words" );

        additional_status[i] = in.get16();
    }

    // all of the remaining bytes are considered response_data
    SetWriter( BufWriter( (CipByte*) in.data(), in.size() ) );
    SetWrittenSize( in.size() );

    return aReply.size();
}


int CipMessageRouterResponse::SerializedCount( int aCtl ) const
{
    return      + 4                                 // at least 4 bytes of status
                + (2 * size_of_additional_status)   // optional additional status
                + WrittenSize();                    // actual reply data which is in "data"
}


int CipMessageRouterResponse::Serialize( BufWriter aOutput, int aCtl ) const
{
    BufWriter out = aOutput;

    out.put8( reply_service | 0x80 );
    out.put8( 0 );                      // reserved
    out.put8( general_status );
    out.put8( size_of_additional_status );

    for( int i = 0;  i < size_of_additional_status;  ++i )
        out.put16( additional_status[i] );

    out.append( Reader() );

    return out.data() - aOutput.data();
}


CipMessageRouterClass::CipMessageRouterClass() :
    CipClass( kCipMessageRouterClass,
        "Message Router",
        MASK7(1,2,3,4,5,6,7),   // common class attributes mask
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
    for( int i = 0; i < DIM( g_explicit_connections );  ++i )
    {
        if( g_explicit_connections[i].State() == kConnectionStateNonExistent )
            return &g_explicit_connections[i];
    }

    return NULL;
}


CipError CipMessageRouterClass::OpenConnection( ConnectionData* aConn,
            Cpf* cpfd, ConnectionManagerStatusCode* extended_error )
{
    CipError ret = kCipErrorSuccess;

    // TODO add check for transport type trigger
    // if (0x03 == (g_stDummyCipConn.TransportTypeClassTrigger & 0x03))

    CipConn* ex_conn = getFreeExplicitConnection();

    if( !ex_conn )
    {
        ret = kCipErrorConnectionFailure;

        *extended_error = kConnectionManagerStatusCodeErrorNoMoreConnectionsAvailable;
    }

    else
    {
        CopyConnectionData( ex_conn, aConn );

        EipUint32 saved = ex_conn->producing_connection_id;

        ex_conn->GeneralConnectionConfiguration();

        ex_conn->producing_connection_id = saved;

        ex_conn->SetInstanceType( kConnInstanceTypeExplicit );

#if 1
        CIPSTER_ASSERT( ex_conn->ConsumingSocket() == kEipInvalidSocket );
        CIPSTER_ASSERT( ex_conn->ProducingSocket() == kEipInvalidSocket );
#else
        ex_conn->SetConsumingSocket( kEipInvalidSocket );
        ex_conn->SetProducingSocket( kEipInvalidSocket );
#endif

        g_active_conns.Insert( ex_conn );
    }

    return ret;
}


static CipInstance* createCipMessageRouterInstance()
{
    CipClass* clazz = GetCipClass( kCipMessageRouterClass );

    CipInstance* i = new CipInstance( clazz->Instances().size() + 1 );

    clazz->InstanceInsert( i );

    return i;
}


EipStatus CipMessageRouterClass::Init()
{
    // may not already be registered.
    if( !GetCipClass( kCipMessageRouterClass ) )
    {
        CipClass* clazz = new CipMessageRouterClass();

        RegisterCipClass( clazz );

        createCipMessageRouterInstance();
    }

    return kEipStatusOk;
}


EipStatus CipMessageRouterClass::NotifyMR( BufReader aCommand, CipMessageRouterResponse* aResponse )
{
    CIPSTER_TRACE_INFO( "%s: routing unconnected message\n", __func__ );

    CipMessageRouterRequest request;

    int result = request.DeserializeMRReq( aCommand );

    aResponse->SetService( request.Service() );

    if( result <= 0 )
    {
        CIPSTER_TRACE_ERR( "notifyMR: error from createMRRequeststructure\n" );
        aResponse->SetGenStatus( kCipErrorPathSegmentError );
        return kEipStatusOkSend;
    }

    CipClass* clazz = NULL;

    int instance_id;

    if( request.Path().HasSymbol() )
    {
        instance_id = 0;   // talk to class 06b instance 0

        // Per Rockwell Automation Publication 1756-PM020D-EN-P - June 2016:
        // Symbol Class Id is 0x6b.  Forward this request to that class.
        // This class is not implemented in CIPster stack, but can be added by
        // an application using simple RegisterCipClass( CipClass* aClass );
        // Instances of this class are tags.
        // I have such an implementation in my application.
        clazz = GetCipClass( 0x6b );

#if 0
        // We cannot know the instance number without looking it up in the
        // the symbol class.  If that lookup is a service of the class, not
        // of each instance, then enable this.  Any service for a class is
        // always held in the meta-class, given by owning_class on the clazz.
        if( clazz )
            clazz = clazz->owning_class;
#endif

    }
    else if( request.Path().HasInstance() )
    {
        instance_id = request.Path().GetInstance();
        clazz = GetCipClass( request.Path().GetClass() );
    }
    else
    {
        CIPSTER_TRACE_WARN( "%s: no instance specified\n", __func__ );

        // instance_id was not in the request
        aResponse->SetGenStatus( kCipErrorPathDestinationUnknown );
        return kEipStatusOkSend;
    }

    if( !clazz )
    {
        CIPSTER_TRACE_ERR(
            "%s: un-registered class in request path:'%s'\n",
            __func__,
            request.Path().Format().c_str()
            );

        aResponse->SetGenStatus( kCipErrorPathDestinationUnknown );
        return kEipStatusOkSend;
    }

    // The conformance tool wants to know about kCipErrorServiceNotSupported errors
    // before wanting to know about kCipErrorPathDestinationUnknown errors, so
    // the order of these next two if() tests is very important.

    CipService* service = instance_id == 0 ?
            clazz->Class()->Service( request.Service() ) :  // meta-class
            clazz->Service( request.Service() );

    if( !service )
    {
#if 0
        if( request.Service() == kGetAttributeSingle && instance_id == 1 && clazz->ClassId() == 1 )
        {
            int break_here = 1;
        }
#endif

        CIPSTER_TRACE_WARN( "%s: service 0x%02x not found\n",
                __func__,
                request.Service() );

        aResponse->SetGenStatus( kCipErrorServiceNotSupported );
        return kEipStatusOkSend;
    }

    CipInstance* instance = clazz->Instance( instance_id );
    if( !instance )
    {
        CIPSTER_TRACE_WARN( "%s: instance %d does not exist\n", __func__, instance_id );

        aResponse->SetGenStatus( kCipErrorPathDestinationUnknown );
        return kEipStatusOkSend;
    }

    CIPSTER_TRACE_INFO(
        "%s: targeting instance %d of class %s with service %s\n",
        __func__,
        instance_id,
        clazz->ClassName().c_str(),
        service->ServiceName().c_str()
        );

    CIPSTER_ASSERT( service->service_function );

    EipStatus status = service->service_function( instance, &request, aResponse );

    CIPSTER_TRACE_ERR(
            "%s: service %s of class '%s' returned %d\n",
            __func__,
            service->ServiceName().c_str(),
            clazz->ClassName().c_str(),
            status
            );

    return status;
}
