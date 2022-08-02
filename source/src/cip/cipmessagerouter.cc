/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corporation.
 *
 ******************************************************************************/

#include <string.h>

#include "cipster_api.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "cipconnectionmanager.h"
#include "cipconnection.h"
#include "byte_bufs.h"
#include "ciperror.h"
#include "trace.h"


/// Array of the available explicit connections
static CipConn g_explicit_connections[CIPSTER_CIP_NUM_EXPLICIT_CONNS];


//-----<CipMessageRounterRequest>-----------------------------------------------

int CipMessageRouterRequest::Serialize( BufWriter aOutput, int aCtl ) const
{
    BufWriter out = aOutput;

    out.put8( service );

    // out + 1 skips over unset word count byte.
    int rplen = path.Serialize( out + 1, aCtl );

    out.put8( rplen / 2 );     // set word count = byte_count / 2

    out += rplen;   // skip over path

    out.append( Data() );

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

std::vector<uint8_t> CipMessageRouterResponse::mmr_temp( CIPSTER_MESSAGE_DATA_REPLY_BUFFER );

CipMessageRouterResponse::CipMessageRouterResponse( Cpf* aCpf, BufWriter aOutput ) :
    data ( aOutput ),
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
    // See Vol1 Table 2-4.2:

    BufReader in = aReply;

    reply_service = CIPServiceCode( in.get8() & 0x7f );

    ++in;      // gobble "reserved"

    SetGenStatus( (CipError) in.get8() );

    size_of_additional_status = in.get8();

    for( int i = 0; i < size_of_additional_status;  ++i )
    {
        if( i == DIM(additional_status) )
            throw std::invalid_argument(
                "CipMessageRouterRespoinse::DeserializeMRRes(): too many additional status words" );

        additional_status[i] = in.get16();
    }

    // all of the remaining bytes are considered "non-status" response_data
    SetReader( in );

    return in.data() - aReply.data();
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
    delete ServiceRemove( _I, kSetAttributeSingle );
}


static CipConn* getFreeExplicitConnection()
{
    for( int i = 0; i < DIM( g_explicit_connections );  ++i )
    {
        if( g_explicit_connections[i].State() == kConnStateNonExistent )
            return &g_explicit_connections[i];
    }

    return NULL;
}


CipError CipMessageRouterClass::OpenConnection( ConnectionData* aConnData,
            Cpf* aCpf, ConnMgrStatus* aExtError )
{
    CipError ret = kCipErrorSuccess;
    CipConn* new_explicit = getFreeExplicitConnection();

    if( !new_explicit )
    {
        ret = kCipErrorConnectionFailure;
        *aExtError = kConnMgrStatusNoMoreConnectionsAvailable;
    }

    else
    {
        new_explicit->GeneralConfiguration( aConnData, kConnInstanceTypeExplicit );

        // Save TCP client's IP address in order to qualify a future forward_close.
        CIPSTER_ASSERT( aCpf->TcpPeerAddr() );
        new_explicit->openers_address = *aCpf->TcpPeerAddr();

        // Save TCP connection session_handle for TCP inactivity timeouts.
        new_explicit->SetSessionHandle( aCpf->SessionHandle() );

        g_active_conns.Insert( new_explicit );
        new_explicit->SetState( kConnStateEstablished );
    }

    return ret;
}


CipInstance* CipMessageRouterClass::CreateInstance( int aInstanceId )
{
    CipInstance* i = new CipInstance( aInstanceId );

    if( !InstanceInsert( i ) )
    {
        delete i;
        i = NULL;
    }

    return i;
}


EipStatus CipMessageRouterClass::Init()
{
    // may not already be registered.
    if( !GetCipClass( kCipMessageRouterClass ) )
    {
        CipMessageRouterClass* clazz = new CipMessageRouterClass();

        RegisterCipClass( clazz );

        clazz->CreateInstance( clazz->Instances().size() + 1 );
    }

    return kEipStatusOk;
}


EipStatus CipMessageRouterClass::NotifyMR(
        CipMessageRouterRequest* aRequest, CipMessageRouterResponse* aResponse )
{
    CIPSTER_TRACE_INFO( "%s: routing...\n", __func__ );

    aResponse->SetService( aRequest->Service() );

    CipClass* clazz = NULL;

    int instance_id;

    if( aRequest->Path().HasSymbol() )
    {
        // Per Rockwell Automation Publication 1756-PM020D-EN-P - June 2016:
        // Symbol Class Id is 0x6b.  Forward this request to that class.
        // This class is not implemented in CIPster stack, but can be added by
        // an application using simple RegisterCipClass( CipClass* aClass );
        // Instances of this class are tags.
        // I have such an implementation in my application.

        instance_id = 0;   // talk to class 0x6b instance 0

        clazz = GetCipClass( 0x6b );
    }
    else if( aRequest->Path().HasInstance() )
    {
        instance_id = aRequest->Path().GetInstance();
        clazz = GetCipClass( aRequest->Path().GetClass() );
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
            aRequest->Path().Format().c_str()
            );

        aResponse->SetGenStatus( kCipErrorPathDestinationUnknown );
        return kEipStatusOkSend;
    }

    // The conformance tool wants to know about kCipErrorServiceNotSupported errors
    // before wanting to know about kCipErrorPathDestinationUnknown errors, so
    // the order of these next two if() tests is very important.

    CipService* service = instance_id == 0 ?
            clazz->ServiceC( aRequest->Service() ) :
            clazz->ServiceI( aRequest->Service() );

    if( !service )
    {
#if 0
        if( aRequest->Service() == kGetAttributeSingle &&
            instance_id     > 7 &&
            clazz->ClassId() == kCipTcpIpInterfaceClass )
        {
            int break_here = 1;
            (void) break_here;
        }
#endif

        CIPSTER_TRACE_WARN( "%s: service 0x%02x not found\n",
                __func__,
                aRequest->Service() );

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
        "%s: targeting '%s' instance:%d service:'%s'\n",
        __func__,
        clazz->ClassName().c_str(),
        instance_id,
        service->ServiceName().c_str()
        );

    CIPSTER_ASSERT( service->service_function );

    EipStatus status = service->service_function( instance, aRequest, aResponse );

    CIPSTER_TRACE_ERR(
            "%s: service %s of class '%s' returned %d\n",
            __func__,
            service->ServiceName().c_str(),
            clazz->ClassName().c_str(),
            status
            );

    return status;
}
