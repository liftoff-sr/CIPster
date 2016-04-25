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

/** @brief Implementation of the SetAttributeSingle CIP service for Assembly
 *          Objects.
 *  Currently only supports Attribute 3 (CIP_BYTE_ARRAY) of an Assembly
 */
EipStatus SetAssemblyAttributeSingle( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response );


EipStatus CipAssemblyInitialize()
{
    // may only register once.
    if( !GetCipClass( kCipAssemblyClassCode ) )
    {
        CipClass* clazz = new CipClass( kCipAssemblyClassCode,
                "assembly",  // aClassName
                0,           // assembly object should not have a get_attribute_all service
                0,           // assembly object should not have a get_attribute_all service
                2            // aRevision, according to the CIP spec currently this has to be 2
                );

        RegisterCipClass( clazz );

        clazz->ServiceInsert( kSetAttributeSingle, &SetAssemblyAttributeSingle, "SetAssemblyAttributeSingle" );
    }

    return kEipStatusOk;
}


CipInstance* CreateAssemblyObject( EipUint32 instance_id, EipByte* data,
        EipUint16 data_length )
{
    CipClass* clazz = GetCipClass( kCipAssemblyClassCode );

    OPENER_ASSERT( clazz );     // Stack startup should call CipAssemblyInitialize()

    OPENER_TRACE_INFO( "%s: creating assembly instance_id %d\n", __func__, instance_id );

    CipByteArray* byte_array = (CipByteArray*) CipCalloc( 1, sizeof(CipByteArray) );
    if( !byte_array )
    {
        return NULL;    // TODO remove assembly instance in case of error
    }

    byte_array->length = data_length;
    byte_array->data   = data;

    CipInstance* instance = new CipInstance( instance_id );

    // add true so ~CipAttribute() deletes the byte_array.
    instance->AttributeInsert( 3, kCipByteArray, byte_array, kSetAndGetAble, true );

    // Attribute 4 Number of bytes in Attribute 3
    instance->AttributeInsert( 4, kCipUint, &byte_array->length, kGetableSingle );

    // This is a public function, we don't expect caller to insert instance
    // into the class, do it here.
    clazz->InstanceInsert( instance );

    return instance;
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
        OPENER_TRACE_ERR( "wrong amount of data arrived for assembly object\n" );
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


EipStatus SetAssemblyAttributeSingle( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    OPENER_TRACE_INFO( "%s: setAttribute %d on assembly instance %d\n",
            __func__,
            request->request_path.attribute_number,
            instance->instance_id
            );

    EipUint8* router_request_data = request->data;

    response->data_length = 0;
    response->reply_service = 0x80 | request->service;
    response->general_status = kCipErrorAttributeNotSupported;
    response->size_of_additional_status = 0;

    CipAttribute*  attribute = instance->Attribute( request->request_path.attribute_number );

    if( attribute  &&  3 == request->request_path.attribute_number )
    {
        OPENER_TRACE_INFO( "%s: attr3\n" );

        if( attribute->data )
        {
            CipByteArray* byte_array = (CipByteArray*) attribute->data;

            // TODO: check for ATTRIBUTE_SET/GETABLE MASK
            if( IsConnectedOutputAssembly( instance->instance_id ) )
            {
                OPENER_TRACE_WARN(
                        "Assembly AssemblyAttributeSingle: received data for connected output assembly\n\r" );
                response->general_status = kCipErrorAttributeNotSetable;
            }
            else
            {
                if( request->data_length < byte_array->length )
                {
                    OPENER_TRACE_INFO(
                            "Assembly setAssemblyAttributeSingle: not enough data received.\r\n" );
                    response->general_status = kCipErrorNotEnoughData;
                }
                else
                {
                    if( request->data_length > byte_array->length )
                    {
                        OPENER_TRACE_INFO(
                                "Assembly setAssemblyAttributeSingle: too much data received.\r\n" );
                        response->general_status = kCipErrorTooMuchData;
                    }
                    else
                    {
                        memcpy( byte_array->data, router_request_data, byte_array->length );

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
            }
        }
        else
        {
            // the attribute was zero we are a heartbeat assembly
            response->general_status = kCipErrorTooMuchData;
        }
    }

    else if( attribute && 4 == request->request_path.attribute_number )
    {
        response->general_status = kCipErrorAttributeNotSetable;
    }

    return kEipStatusOkSend;
}
