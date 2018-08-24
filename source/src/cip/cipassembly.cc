/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corporation.
 *
 ******************************************************************************/

//#include <string.h>    //needed for memcpy

#include "cipassembly.h"

#include <cipster_api.h>
#include "cipconnectionmanager.h"

#undef  INSTANCE_CLASS
#define INSTANCE_CLASS  AssemblyInstance

// getter and setter of type AssemblyFunc, specific to this CIP class called "Assembly"

static EipStatus getAttrAssemblyData( CipInstance* aInstance, CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    if( aInstance->Data(attr) )
    {
        BeforeAssemblyDataSend( aInstance );

        return CipAttribute::GetAttrData( aInstance, attr, request, response );
    }

    return kEipStatusOkSend;
}


static EipStatus setAttrAssemblyData( CipInstance* aInstance, CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    if( ByteBuf* byte_array = (ByteBuf*) aInstance->Data( attr ) )
    {
        if( IsConnectedInputAssembly( aInstance->Id() ) )
        {
            CIPSTER_TRACE_WARN( "%s: received data for connected input assembly\n", __func__ );
            response->SetGenStatus( kCipErrorAttributeNotSetable );
        }
        else if( request->Data().size() < byte_array->size() )
        {
            CIPSTER_TRACE_INFO( "%s: not enough data received.\n", __func__ );
            response->SetGenStatus( kCipErrorNotEnoughData );
        }
        else if( request->Data().size() > byte_array->size() )
        {
            CIPSTER_TRACE_INFO( "%s: too much data received.\n", __func__ );
            response->SetGenStatus( kCipErrorTooMuchData );
        }
        else
        {
            CIPSTER_TRACE_INFO(
                "%s: writing %d bytes to assembly_id: %d.\n",
                __func__,
                (int) request->Data().size(),
                aInstance->Id()
                );

            memcpy( byte_array->data(), request->Data().data(), byte_array->size() );

            if( AfterAssemblyDataReceived( aInstance ) != kEipStatusOk )
            {
                // NOTE: the attribute's data has already been overwritten.
                // Application did not like it.  Probably need a better
                // interface which provides a Reader to the application
                // so it can conditionally do the memcpy not us here.
                response->SetGenStatus( kCipErrorInvalidAttributeValue );
            }
            else
            {
                response->SetGenStatus( kCipErrorSuccess );
            }
        }
    }
    else
    {
        // attr->data was zero; this is a heartbeat assembly
        response->SetGenStatus( kCipErrorTooMuchData );
    }

    return kEipStatusOkSend;
}


AssemblyInstance::AssemblyInstance( int aInstanceId, ByteBuf aBuffer ) :
    CipInstance( aInstanceId ),
    byte_array( aBuffer )
{
}


CipAssemblyClass::CipAssemblyClass() :
    CipClass( kCipAssemblyClass,
        "Assembly",
        MASK7( 1,2,3,4,5,6,7 ), // common class attributes mask
        2                       // class revision
        )
{
    // Attribute 3 is the byte array transfer of the assembly data itself
    AttributeInsert( _I, 3, getAttrAssemblyData, false, setAttrAssemblyData, memb_offs(byte_array) );

    // Attribute 4: is no. of bytes in Attribute 3
    AttributeInsert( _I, 4, kCipByteArrayLength, memb_offs(byte_array), true, false );
}


AssemblyInstance* CipAssemblyClass::CreateInstance( int aInstanceId, ByteBuf aBuffer )
{
    CipAssemblyClass* clazz = (CipAssemblyClass*) GetCipClass( kCipAssemblyClass );

    CIPSTER_ASSERT( clazz ); // Stack startup should have called CipAssemblyInitialize()

    AssemblyInstance* i = new AssemblyInstance( aInstanceId, aBuffer );

    if( !clazz->InstanceInsert( i ) )
    {
        delete i;
        i = NULL;
    }
    else
    {
        CIPSTER_TRACE_INFO( "%s: created assembly aInstanceId %d\n", __func__, aInstanceId );
    }

    return i;
}


CipError CipAssemblyClass::OpenConnection( ConnectionData* aConnData,
    Cpf* aCpf, ConnMgrStatus* aExtError )
{
    return CipConnectionClass::OpenIO( aConnData, aCpf, aExtError );
}


EipStatus CipAssemblyClass::Init()
{
    if( !GetCipClass( kCipAssemblyClass ) )
    {
        CipClass* clazz = new CipAssemblyClass();

        RegisterCipClass( clazz );
    }

    return kEipStatusOk;
}


EipStatus NotifyAssemblyConnectedDataReceived( CipInstance* instance, BufReader aBuffer )
{
    CIPSTER_ASSERT( instance->Class()->ClassId() == kCipAssemblyClass );

    // empty path (path size = 0) need to be checked and taken care of in future

    // copy received data to Attribute 3
    CipAttribute* attr3 = instance->Attribute( 3 );
    CIPSTER_ASSERT( attr3 );

    ByteBuf* byte_array = (ByteBuf*) instance->Data( attr3 );
    CIPSTER_ASSERT( byte_array );

    if( byte_array->size() != aBuffer.size() )
    {
        CIPSTER_TRACE_ERR( "%s: wrong amount of data arrived for assembly object\n", __func__ );
        return kEipStatusError;

        // TODO question should we notify the application that
        // wrong data has been recieved???
    }
    else
    {
        memcpy( byte_array->data(), aBuffer.data(), aBuffer.size() );
    }

    // notify application that new data arrived
    return AfterAssemblyDataReceived( instance );
}
