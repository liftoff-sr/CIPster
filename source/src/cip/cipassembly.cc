/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corporation.
 *
 ******************************************************************************/

//#include <string.h>    //needed for memcpy

#include "cipassembly.h"

#include <cipster_api.h>
#include "cipconnectionmanager.h"


// getter and setter of type AssemblyFunc, specific to this CIP class called "Assembly"

static EipStatus getAttrAssemblyData( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    if( attr->Data() )
    {
        BeforeAssemblyDataSend( attr->Instance() );

        return CipAttribute::GetAttrData( attr, request, response );
    }

    return kEipStatusOkSend;
}


static EipStatus setAttrAssemblyData( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    if( attr->Data() )
    {
        ByteBuf*        byte_array = (ByteBuf*) attr->Data();
        CipInstance*    instance = attr->Instance();

        if( IsConnectedInputAssembly( instance->Id() ) )
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
                instance->Id()
                );

            memcpy( byte_array->data(), request->Data().data(), byte_array->size() );

            if( AfterAssemblyDataReceived( instance ) != kEipStatusOk )
            {
                /* punt early without updating the status... though I don't know
                 * how much this helps us here, as the attribute's data has already
                 * been overwritten.
                 *
                 * however this is the task of the application side which will
                 * take the data. In addition we have to inform the sender that the
                 * data was not ok.
                 */
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
    // Attribute 3 is the byte array transfer of the assembly data itself
    AttributeInsert( 3, getAttrAssemblyData, false, setAttrAssemblyData, &byte_array );

    // Attribute 4 Number of bytes in Attribute 3
    AttributeInsert( 4, kCipByteArrayLength, &byte_array, true, false );
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

    ByteBuf* byte_array = (ByteBuf*) attr3->Data();
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

