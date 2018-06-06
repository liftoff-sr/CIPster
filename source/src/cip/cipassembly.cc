/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/

#include <string.h>    //needed for memcpy

#include "cipassembly.h"

#include "cipcommon.h"
//#include "cipster_api.h"
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
        else if( request->data.size() < byte_array->length )
        {
            CIPSTER_TRACE_INFO( "%s: not enough data received.\n", __func__ );
            response->general_status = kCipErrorNotEnoughData;
        }
        else if( request->data.size() > byte_array->length )
        {
            CIPSTER_TRACE_INFO( "%s: too much data received.\n", __func__ );
            response->general_status = kCipErrorTooMuchData;
        }
        else
        {
            CIPSTER_TRACE_INFO(
                "%s: writing %d bytes to assembly_id: %d.\n",
                __func__,
                (int) request->data.size(),
                instance->Id()
                );

            memcpy( byte_array->data, request->data.data(), byte_array->length );

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


AssemblyInstance::AssemblyInstance( int aInstanceId, BufWriter aBuffer ) :
    CipInstance( aInstanceId )
{
    byte_array.length = aBuffer.size();
    byte_array.data   = aBuffer.data();

    // Attribute 3 is the byte array transfer of the assembly data itself
    AttributeInsert( 3, kCipByteArray, kSetAndGetAble, getAttrAssemblyData, setAttrAssemblyData, &byte_array );

    // Attribute 4 Number of bytes in Attribute 3
    AttributeInsert( 4, kCipUint, kGetableSingle, &byte_array.length );
}


CipInstance* CreateAssemblyInstance( int instance_id, BufWriter aBuffer )
{
    CipClass* clazz = GetCipClass( kCipAssemblyClass );

    CIPSTER_ASSERT( clazz ); // Stack startup should have called CipAssemblyInitialize()

    AssemblyInstance* i = new AssemblyInstance( instance_id, aBuffer );

    if( !clazz->InstanceInsert( i ) )
    {
        delete i;
        i = NULL;
    }
    else
    {
        CIPSTER_TRACE_INFO( "%s: created assembly instance_id %d\n", __func__, instance_id );
    }

    return i;
}


class CipAssemblyClass : public CipClass
{
public:
    CipAssemblyClass() :
        CipClass( kCipAssemblyClass,
            "Assembly",
            MASK7( 1,2,3,4,5,6,7 ), // common class attributes mask
            0,                      // assembly class has no get_attribute_all service
            0,                      // assembly instance has no get_attribute_all service
            2                       // aRevision, according to the CIP spec currently this has to be 2
            )
    {
    }

    CipError OpenConnection( CipConn* aConn, CipCommonPacketFormatData* cpfd, ConnectionManagerStatusCode* extended_error ); // override
};


CipError CipAssemblyClass::OpenConnection( CipConn* aConn,
    CipCommonPacketFormatData* cpfd, ConnectionManagerStatusCode* extended_error )
{
    return CipConnectionClass::OpenIO( aConn, cpfd, extended_error );
}


EipStatus CipAssemblyInitialize()
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
    CIPSTER_ASSERT( instance->owning_class->ClassId() == kCipAssemblyClass );

    // empty path (path size = 0) need to be checked and taken care of in future

    // copy received data to Attribute 3
    CipAttribute* attr3 = instance->Attribute( 3 );
    CIPSTER_ASSERT( attr3 );

    CipByteArray* byte_array = (CipByteArray*) attr3->data;
    CIPSTER_ASSERT( byte_array );

    if( byte_array->length != aBuffer.size() )
    {
        CIPSTER_TRACE_ERR( "%s: wrong amount of data arrived for assembly object\n", __func__ );
        return kEipStatusError;

        // TODO question should we notify the application that
        // wrong data has been recieved???
    }
    else
    {
        memcpy( byte_array->data, aBuffer.data(), aBuffer.size() );
    }

    // notify application that new data arrived
    return AfterAssemblyDataReceived( instance );
}

