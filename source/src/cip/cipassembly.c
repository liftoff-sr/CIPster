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

CipClass* CreateAssemblyClass()
{
    CipClass* assembly_class;

    // create the CIP Assembly object with zero instances
    assembly_class = CreateCipClass( kCipAssemblyClassCode, 0,  // # class attributes
            0,                                                  // 0 as the assembly object should not have a get_attribute_all service
            0,                                                  // # class services
            2,                                                  // # instance attributes
            0,                                                  // 0 as the assembly object should not have a get_attribute_all service
            1,                                                  // # instance services
            0,                                                  // # instances
            "assembly",                                         // name
            2                                                   // Revision, according to the CIP spec currently this has to be 2
            );

    if( NULL != assembly_class )
    {
        InsertService( assembly_class, kSetAttributeSingle,
                &SetAssemblyAttributeSingle, "SetAssemblyAttributeSingle" );
    }

    return assembly_class;
}


EipStatus CipAssemblyInitialize()
{
    // create the CIP Assembly class
    return CreateAssemblyClass() ? kEipStatusOk : kEipStatusError;
}


CipInstance* CreateAssemblyObject( EipUint32 instance_id, EipByte* data,
        EipUint16 data_length )
{
    CipClass* assembly_class = GetCipClass( kCipAssemblyClassCode );

    if( !assembly_class )
    {
        assembly_class = CreateAssemblyClass();
        if( !assembly_class )
        {
            return NULL;
        }
    }

    CipInstance* instance = AddCIPInstance( assembly_class, instance_id );

    CipByteArray* byte_array = (CipByteArray*) CipCalloc( 1, sizeof(CipByteArray) );
    if( !byte_array )
    {
        return NULL;    // TODO remove assembly instance in case of error
    }

    byte_array->length = data_length;
    byte_array->data   = data;

    instance->AttributeInsert( 3, kCipByteArray, byte_array, kSetAndGetAble, true );

    // Attribute 4 Number of bytes in Attribute 3
    instance->AttributeInsert( 4, kCipUint, &byte_array->length, kGetableSingle );

    return instance;
}


EipStatus NotifyAssemblyConnectedDataReceived( CipInstance* instance,
        EipUint8* data,
        EipUint16 data_length )
{
    OPENER_ASSERT( instance->cip_class->class_id == kCipAssemblyClassCode );

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
    EipUint8* router_request_data;
    CipAttribute* attribute;

    OPENER_TRACE_INFO( " setAttribute %d\n",
            request->request_path.attribute_number );

    router_request_data = request->data;

    response->data_length = 0;
    response->reply_service = 0x80 | request->service;
    response->general_status = kCipErrorAttributeNotSupported;
    response->size_of_additional_status = 0;

    attribute = GetCipAttribute( instance, request->request_path.attribute_number );

    if( (attribute != NULL)
        && (3 == request->request_path.attribute_number) )
    {
        if( attribute->data != NULL )
        {
            CipByteArray* data = (CipByteArray*) attribute->data;

            // TODO: check for ATTRIBUTE_SET/GETABLE MASK
            if( IsConnectedOutputAssembly( instance->instance_id ) )
            {
                OPENER_TRACE_WARN(
                        "Assembly AssemblyAttributeSingle: received data for connected output assembly\n\r" );
                response->general_status = kCipErrorAttributeNotSetable;
            }
            else
            {
                if( request->data_length < data->length )
                {
                    OPENER_TRACE_INFO(
                            "Assembly setAssemblyAttributeSingle: not enough data received.\r\n" );
                    response->general_status = kCipErrorNotEnoughData;
                }
                else
                {
                    if( request->data_length > data->length )
                    {
                        OPENER_TRACE_INFO(
                                "Assembly setAssemblyAttributeSingle: too much data received.\r\n" );
                        response->general_status = kCipErrorTooMuchData;
                    }
                    else
                    {
                        memcpy( data->data, router_request_data, data->length );

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

    if( (attribute != NULL)
        && (4 == request->request_path.attribute_number) )
    {
        response->general_status = kCipErrorAttributeNotSetable;
    }

    return kEipStatusOkSend;
}
