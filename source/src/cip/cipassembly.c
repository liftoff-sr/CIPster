/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/

#include <string.h>    //needed for memcpy

#include "cipassembly.h"

#include "cipcommon.h"
//#include "opener_api.h"
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

    return kEipStatusOkSend;
}


static EipStatus setAttrAssemblyData( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    if( attr->data )
    {
        CipByteArray*   byte_array = (CipByteArray*) attr->data;
        CipInstance*    instance = attr->Instance();

        if( IsConnectedInputAssembly( instance->instance_id ) )
        {
            CIPSTER_TRACE_WARN( "%s: received data for connected input assembly\n", __func__ );
            response->general_status = kCipErrorAttributeNotSetable;
        }
        else if( request->data_length < byte_array->length )
        {
            CIPSTER_TRACE_INFO( "%s: not enough data received.\n", __func__ );
            response->general_status = kCipErrorNotEnoughData;
        }
        else if( request->data_length > byte_array->length )
        {
            CIPSTER_TRACE_INFO( "%s: too much data received.\n", __func__ );
            response->general_status = kCipErrorTooMuchData;
        }
        else
        {
            CIPSTER_TRACE_INFO(
                "%s: writing %d bytes to assembly_id: %d.\n",
                __func__,
                request->data_length,
                instance->Id()
                );

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


/**
 * Class AssemblyInstance
 * is extended from CipInstance with an extra CipByteArray at the end.
 * That byte array has no ownership of the low level array, which for an
 * assembly is owned by the application program and passed into
 * CreateAssemblyInstance().
 */
class AssemblyInstance : public CipInstance
{
public:
    AssemblyInstance( EipUint16 aInstanceId ) :
        CipInstance( aInstanceId )
    {
    }

//protected:
    CipByteArray    byte_array;
};


CipInstance* CreateAssemblyInstance( int instance_id, EipByte* data, int data_length )
{
    CipClass* clazz = GetCipClass( kCipAssemblyClassCode );

    CIPSTER_ASSERT( clazz ); // Stack startup should have called CipAssemblyInitialize()

    if( clazz->Instance( instance_id ) )
    {
        CIPSTER_TRACE_ERR( "%s: cannot create another instance_id = %d.\n", __func__, instance_id );
        return NULL;
    }

    CIPSTER_TRACE_INFO( "%s: creating assembly instance_id %d\n", __func__, instance_id );

    AssemblyInstance* i = new AssemblyInstance( instance_id );

    i->byte_array.length = data_length;
    i->byte_array.data   = data;

    // Attribute 3 is the byte array transfer of the assembly data itself
    i->AttributeInsert( 3, kCipByteArray, kSetAndGetAble, getAttrAssemblyData, setAttrAssemblyData, &i->byte_array );

    // Attribute 4 Number of bytes in Attribute 3
    i->AttributeInsert( 4, kCipUint, kGetableSingle, &i->byte_array.length );

    clazz->InstanceInsert( i );

    return i;
}


class CipAssemblyClass : public CipClass
{
public:
    CipAssemblyClass() :
        CipClass( kCipAssemblyClassCode,
            "Assembly",     // aClassName
            (1<<7)|(1<<6)|(1<<5)|(1<<4)|(1<<3)|(1<<2)|(1<<1),
            0,              // assembly class has no get_attribute_all service
            0,              // assembly instance has no get_attribute_all service
            2               // aRevision, according to the CIP spec currently this has to be 2
            )
    {
    }

    CipError OpenConnection( CipConn* aConn, ConnectionManagerStatusCode* extended_error ); // override
};


CipError CipAssemblyClass::OpenConnection( CipConn* aConn, ConnectionManagerStatusCode* extended_error )
{
    return CipConnectionClass::OpenIO( aConn, extended_error );
}


EipStatus CipAssemblyInitialize()
{
    if( !GetCipClass( kCipAssemblyClassCode ) )
    {
        CipClass* clazz = new CipAssemblyClass();

        RegisterCipClass( clazz );
    }

    return kEipStatusOk;
}


EipStatus NotifyAssemblyConnectedDataReceived( CipInstance* instance,
        EipUint8* data,
        EipUint16 data_length )
{
    CIPSTER_ASSERT( instance->owning_class->ClassId() == kCipAssemblyClassCode );

    // empty path (path size = 0) need to be checked and taken care of in future

    // copy received data to Attribute 3
    CipAttribute* attr3 = instance->Attribute( 3 );
    CIPSTER_ASSERT( attr3 );

    CipByteArray* byte_array = (CipByteArray*) attr3->data;
    CIPSTER_ASSERT( byte_array );

    if( byte_array->length != data_length )
    {
        CIPSTER_TRACE_ERR( "%s: wrong amount of data arrived for assembly object\n", __func__ );
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

