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

AssemblyInstance::AssemblyInstance( int aInstanceId, ByteBuf aBuffer ) :
    CipInstance( aInstanceId ),
    byte_array( aBuffer )
{
}


EipStatus AssemblyInstance::RecvData( CipConn* aConn, BufReader aBuffer )
{
    if( ( aConn->ConsumingNCP().IsFixed() && SizeBytes() != aBuffer.size()) ||
        (!aConn->ConsumingNCP().IsFixed() && SizeBytes() < aBuffer.size()) )
    {
        CIPSTER_TRACE_ERR(
            "%s: wrong data amount: %zd bytes arrived for assembly id: %d\n",
            __func__, aBuffer.size(), Id() );
        return kEipStatusError;
    }

    memcpy( Buffer().data(), aBuffer.data(), aBuffer.size() );

    // notify application that new data arrived
    return AfterAssemblyDataReceived( this, aConn->Mode(), aBuffer.size() );
}


CipAssemblyClass::CipAssemblyClass() :
    CipClass( kCipAssemblyClass,
        "Assembly",
        MASK7( 1,2,3,4,5,6,7 ), // common class attributes mask
        2                       // class revision
        )
{
    // Attribute 3 is the byte array transfer of the assembly data itself
    AttributeInsert( _I, 3, get_assembly_data_attr, false, set_assembly_data_attr,
        memb_offs(byte_array),  true, kCipByteArray);

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


EipStatus CipAssemblyClass::get_assembly_data_attr( CipInstance* aInstance, CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    AssemblyInstance* assembly = static_cast<AssemblyInstance*>( aInstance );

    if( !assembly->byte_array.size() )
    {
        // assembly has no length, my be for a heartbeat connection, nothing to do.
    }
    else
    {
        BeforeAssemblyDataSend( assembly );

        return CipAttribute::GetAttrData( assembly, attr, request, response );
    }

    return kEipStatusOkSend;
}


EipStatus CipAssemblyClass::set_assembly_data_attr( CipInstance* aInstance, CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    AssemblyInstance* assembly = static_cast<AssemblyInstance*>( aInstance );

    if( IsConnectedInputAssembly( assembly->Id() ) )
    {
        CIPSTER_TRACE_WARN( "%s: received data for connected input assembly\n", __func__ );
        response->SetGenStatus( kCipErrorAttributeNotSetable );
    }
    else if( request->Data().size() < assembly->byte_array.size() )
    {
        CIPSTER_TRACE_INFO( "%s: not enough data received.\n", __func__ );
        response->SetGenStatus( kCipErrorNotEnoughData );
    }
    else if( request->Data().size() > assembly->byte_array.size() )
    {
        CIPSTER_TRACE_INFO( "%s: too much data received.\n", __func__ );
        response->SetGenStatus( kCipErrorTooMuchData );
    }
    else if( !assembly->byte_array.size() )
    {
        // assembly data has no length, may be for heartbeat connection, nothing to do.
    }
    else
    {
        CIPSTER_TRACE_INFO(
            "%s: writing %d bytes to assembly_id: %d.\n",
            __func__,
            (int) request->Data().size(),
            assembly->Id()
            );

        memcpy( assembly->byte_array.data(), request->Data().data(),
                assembly->byte_array.size() );

        if( AfterAssemblyDataReceived( assembly,
                kOpModeUnknown, request->Data().size() ) != kEipStatusOk )
        {
            // NOTE: the attribute's data has already been overwritten.
            // Application did not like it.
            response->SetGenStatus( kCipErrorInvalidAttributeValue );
        }
    }

    return kEipStatusOkSend;
}
