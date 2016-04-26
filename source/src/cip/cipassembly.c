/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/

#include <string.h>    //needed for memcpy

#include "cipassembly.h"

#include "cipcommon.h"
#include "opener_api.h"
#include "trace.h"
#include "cipconnectionmanager.h"


// getter and setter of type AssemblyFunc, specific to this CIP class called "Assembly"

static EipStatus getAttrAssemblyData( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    if( attr->data )
    {
        BeforeAssemblyDataSend( attr->Instance() );

        return GetAttrData( attr, request, response );
    }
}


static EipStatus setAttrAssemblyData( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    if( attr->data )
    {
        CipByteArray*      byte_array = (CipByteArray*) attr->data;
        CipInstance*    instance = attr->Instance();

        if( IsConnectedOutputAssembly( instance->instance_id ) )
        {
            OPENER_TRACE_WARN( "%s: received data for connected output assembly\n", __func__ );
            response->general_status = kCipErrorAttributeNotSetable;
        }
        else if( request->data_length < byte_array->length )
        {
            OPENER_TRACE_INFO( "%s: not enough data received.\n", __func__ );
            response->general_status = kCipErrorNotEnoughData;
        }
        else if( request->data_length > byte_array->length )
        {
            OPENER_TRACE_INFO( "%s: too much data received.\n", __func__ );
            response->general_status = kCipErrorTooMuchData;
        }
        else
        {
            memcpy( byte_array->data, request->data, byte_array->length );

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
                response->general_status = kCipErrorInvalidAttributeValue;
            }
            else
            {
                response->general_status = kCipErrorSuccess;
            }
        }
    }
    else
    {
        // attr->data was zero; this is a heartbeat assembly
        response->general_status = kCipErrorTooMuchData;
    }

    return kEipStatusOkSend;
}


EipStatus CipAssemblyInitialize()
{
    if( !GetCipClass( kCipAssemblyClassCode ) )
    {
        CipClass* clazz = new CipClass( kCipAssemblyClassCode,
                "Assembly",  // aClassName
                0,           // assembly class should not have a get_attribute_all service
                0,           // assembly instance should not have a get_attribute_all service
                2            // aRevision, according to the CIP spec currently this has to be 2
                );

        RegisterCipClass( clazz );
    }

    return kEipStatusOk;
}


/**
 * Class AssemblyInstance
 * is extended from CipInstance with an extra CipByteArray at the end.
 * That byte array has no ownership of the low level array, which for an
 * assembly is owned by the application program and passed into
 * CreateAssemblyObject().
 */
class AssemblyInstance : public CipInstance
{
public:
    AssemblyInstance( EipUint16 aInstanceId ) :
        CipInstance( aInstanceId )
    {
    }

    CipByteArray    byte_array;
};


CipInstance* CreateAssemblyObject( EipUint32 instance_id, EipByte* data,
        EipUint16 data_length )
{
    CipClass* clazz = GetCipClass( kCipAssemblyClassCode );

    OPENER_ASSERT( clazz ); // Stack startup should have called CipAssemblyInitialize()

    OPENER_TRACE_INFO( "%s: creating assembly instance_id %d\n", __func__, instance_id );

    AssemblyInstance* i = new AssemblyInstance( instance_id );

    i->byte_array.length = data_length;
    i->byte_array.data   = data;

    // Attribute 3 is the byte array transfer of the assembly data itself
    i->AttributeInsert( 3, kCipByteArray, kSetAndGetAble, getAttrAssemblyData, setAttrAssemblyData, &i->byte_array );

    // Attribute 4 Number of bytes in Attribute 3
    i->AttributeInsert( 4, kCipUint, kGetableSingle, &i->byte_array.length );

    // This is a public function, we should not expect caller to insert this
    // instance its proper public CIP class "Assembly", so do it here.
    clazz->InstanceInsert( i );

    return i;
}


EipStatus NotifyAssemblyConnectedDataReceived( CipInstance* instance,
        EipUint8* data,
        EipUint16 data_length )
{
    OPENER_ASSERT( instance->owning_class->Id() == kCipAssemblyClassCode );

    // empty path (path size = 0) need to be checked and taken care of in future

    // copy received data to Attribute 3
    CipAttribute* attr3 = instance->Attribute( 3 );
    OPENER_ASSERT( attr3 );

    CipByteArray* byte_array = (CipByteArray*) attr3->data;
    OPENER_ASSERT( byte_array );

    if( byte_array->length != data_length )
    {
        OPENER_TRACE_ERR( "%s: wrong amount of data arrived for assembly object\n", __func__ );
        return kEipStatusError;

        // TODO question should we notify the application that
        // wrong data has been recieved???
    }
    else
    {
        memcpy( byte_array->data, data, data_length );
    }

    // notify application that new data arrived
    return AfterAssemblyDataReceived( instance );
}

