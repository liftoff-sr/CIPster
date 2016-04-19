/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 *
 * Conversion to C++ is Copyright (C) 2016, SoftPLC Corportion.
 *
 * All rights reserved.
 *
 ******************************************************************************/
#include <string.h>

#include "cipcommon.h"

#include "trace.h"
#include "opener_api.h"
#include "cipidentity.h"
#include "ciptcpipinterface.h"
#include "cipethernetlink.h"
#include "cipconnectionmanager.h"
#include "endianconv.h"
#include "encap.h"
#include "ciperror.h"
#include "cipassembly.h"
#include "cipmessagerouter.h"
#include "cpf.h"
#include "appcontype.h"

// global public variables
EipUint8 g_message_data_reply_buffer[OPENER_MESSAGE_DATA_REPLY_BUFFER];

const EipUint16 kCipUintZero = 0;

// private functions
int EncodeEPath( CipEpath* epath, EipUint8** message );

void CipStackInit( EipUint16 unique_connection_id )
{
    EipStatus eip_status;

    EncapsulationInit();

    // The message router is the first CIP object be initialized!!!
    eip_status = CipMessageRouterInit();
    OPENER_ASSERT( kEipStatusOk == eip_status );

    eip_status = CipIdentityInit();
    OPENER_ASSERT( kEipStatusOk == eip_status );

    eip_status = CipTcpIpInterfaceInit();
    OPENER_ASSERT( kEipStatusOk == eip_status );

    eip_status = CipEthernetLinkInit();
    OPENER_ASSERT( kEipStatusOk == eip_status );

    eip_status = ConnectionManagerInit( unique_connection_id );
    OPENER_ASSERT( kEipStatusOk == eip_status );

    eip_status = CipAssemblyInitialize();
    OPENER_ASSERT( kEipStatusOk == eip_status );

#if 0    // do this in caller after return from this function.
    // the application has to be initialized at last
    eip_status = ApplicationInitialization();
    OPENER_ASSERT( kEipStatusOk == eip_status );
#endif

    (void) eip_status;
}


void ShutdownCipStack()
{
    // First close all connections
    CloseAllConnections();

    // Than free the sockets of currently active encapsulation sessions
    EncapsulationShutDown();

    // clean the data needed for the assembly object's attribute 3
    ShutdownAssemblies();

    ShutdownTcpIpInterface();

    // clear all the instances and classes
    DeleteAllClasses();
}


EipStatus NotifyClass( CipClass* cip_class,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    // find the instance: if instNr==0, the class is addressed, else find the instance
    unsigned instance_number = request->request_path.instance_number;

    // look up the instance (note that if inst==0 this will be the class itself)
    CipInstance* instance = GetCipInstance( cip_class, instance_number );

    if( instance )
    {
        OPENER_TRACE_INFO( "notify: found instance %d%s\n", instance_number,
                instance_number == 0 ? " (class object)" : "" );

        CipClass::CipServices& services = instance->cip_class->services;
        for( unsigned i = 0; i < services.size(); ++i )
        {
            CipService* service = services[i];

            if( request->service == service->service_id )    // if match is found
            {
                // call the service, and return what it returns
                OPENER_TRACE_INFO( "notify: calling %s service\n", service->service_name.c_str() );
                OPENER_ASSERT( NULL != service->service_function );
                return service->service_function( instance, request, response );
            }
        }

        OPENER_TRACE_WARN( "notify: service 0x%x not supported\n",
                request->service );
        response->general_status = kCipErrorServiceNotSupported; // if no services or service not found, return an error reply
    }
    else
    {
        OPENER_TRACE_WARN( "notify: instance number %d unknown\n", instance_number );

        // If instance not found, return an error reply.
        // According to the test tool this should be the correct error flag
        // instead of CIP_ERROR_OBJECT_DOES_NOT_EXIST;
        response->general_status = kCipErrorPathDestinationUnknown;
    }

    // handle error replies
    response->size_of_additional_status = 0; // fill in the rest of the reply with not much of anything
    response->data_length = 0;

    // but the reply code is an echo of the command + the reply flag
    response->reply_service = 0x80 | request->service;

    return kEipStatusOkSend;
}


CipInstance::CipInstance( EipUint32 instance_id, CipClass* aClass ) :
    instance_id( instance_id ),
    cip_class( aClass )
{
    if( aClass )
    {
        for( int i = 0; i< aClass->instance_attr_count;  ++i )
            attributes.push_back( new CipAttribute() );
    }
}


CipInstance::~CipInstance()
{
    if( cip_class && instance_id )     // if not nested in a meta-class or a class
    {
        OPENER_TRACE_INFO( "deleting instance %d of class %s\n",
            instance_id, cip_class->class_name.c_str() );
    }

    for( unsigned i=0; i< attributes.size(); ++i )
        delete attributes[i];
    attributes.clear();
}


CipClass::CipClass(
        const char* aClassName,
        EipUint32   aClassId,
        EipUint16   aClassAttributeCount,
        EipUint16   aClassServiceCount,
        EipUint32   a_get_all_class_attributes_mask,
        EipUint16   aInstanceAttributeCount,
        EipUint16   aInstanceServiceCount,
        EipUint32   a_get_all_instance_attributes_mask,
        EipUint16   aRevision
        ) :
    CipInstance(
        0,                  // instance_id of public class is always 0
        new CipClass(       // class of public class is this meta-class
                aClassName,
                aClassAttributeCount + 7,
                aClassServiceCount + (a_get_all_class_attributes_mask ? 1 : 2),
                a_get_all_class_attributes_mask,
                this
            )
        ),
    class_id( aClassId ),
    class_name( aClassName ),
    revision( aRevision ),
    instance_attr_count( aInstanceAttributeCount ),
    highest_attr_id( 0 ),
    get_attribute_all_mask( a_get_all_instance_attributes_mask )
{
    // The public class holds services for the instances, and attributes for itself.

    for( EipUint16 i = 0; i < aInstanceServiceCount;  ++i )
        services.push_back( new CipService() );

    for( EipUint16 i = 0; i < aClassAttributeCount;  ++i )
        attributes.push_back( new CipAttribute() );
}


CipClass::~CipClass()
{
    // cip_class is NULL for a "meta-" class, which does not own its one
    // public class instance
    if( cip_class )
    {
        // delete all the instances of this class
        while( instances.size() )
        {
            delete *instances.begin();

            // There could be a faster way, but this is not time critical
            // because the program is terminating here.
            instances.erase( instances.begin() );
        }

        // delete the meta-class, which invokes a small bit of recursion
        // back into this function, but on the nested call cip_class will
        // be NULL for the meta-class.
        delete cip_class;
    }

    while( services.size() )
    {
        delete *services.begin();

        services.erase( services.begin() );
    }

    OPENER_TRACE_INFO( "deleting class %s\n", class_name.c_str() );
}


void AddCipInstances( CipClass* cip_class, int number_of_instances )
{
    OPENER_TRACE_INFO( "adding %d instances to class %s\n", number_of_instances,
            cip_class->class_name.c_str() );

    CipClass::CipInstances& instances = cip_class->instances;

    int instance_number = instances.size()
                            + 1;  // the first instance is number 1

    // create the new instances
    for( int i = 0; i < number_of_instances;  ++i, ++instance_number )
    {
        // each instance is now in a separate block of ram so each can be uniquely deleted
        CipInstance* instance = new CipInstance( instance_number, cip_class );

        instances.push_back( instance );
    }
}


CipInstance* AddCIPInstance( CipClass* clazz, EipUint32 instance_id )
{
    CipInstance* instance = GetCipInstance( clazz, instance_id );

    if( !instance )
    {
        // each instance is in a separate block of ram so each can be uniquely deleted
        instance = new CipInstance( instance_id, clazz );

        clazz->instances.push_back( instance );
    }

    return instance;
}


CipClass* CreateCipClass( EipUint32 class_id, int number_of_class_attributes,
        EipUint32 get_all_class_attributes_mask,
        int number_of_class_services,
        int number_of_instance_attributes,
        EipUint32 get_all_instance_attributes_mask,
        int number_of_instance_services,
        int number_of_instances, const char* name,
        EipUint16 revision )
{
    OPENER_TRACE_INFO( "creating class '%s' with id: 0x%08x\n", name,
            class_id );

    OPENER_ASSERT( !GetCipClass( class_id ) );   // should never try to redefine a class

    CipClass* clazz = new CipClass(
            name,
            class_id,
            number_of_class_attributes,         // EipUint16   aClassAttributeCount
            number_of_class_services,           // EipUint16   aClassServiceCount,
            get_all_class_attributes_mask,      // EipUint32   a_get_all_class_attributes_mask,
            number_of_instance_attributes,      // EipUint16   aInstanceAttributeCount,
            number_of_instance_services,        // EipUint16   aInstanceServiceCount,
            get_all_instance_attributes_mask,   // EipUint32   a_get_all_instance_attributes_mask,
            revision
            );

    CipClass* meta_class = clazz->cip_class;

    if( number_of_instances > 0 )
    {
        AddCipInstances( clazz, number_of_instances ); //TODO handle return value and clean up if necessary
    }

    if( RegisterCipClass( clazz ) == kEipStatusError )  // no memory to register class in Message Router
    {
        return 0;       // TODO handle return value and clean up if necessary
    }

    // create the standard class attributes

    InsertAttribute( (CipInstance*) clazz, 1, kCipUint, (void*) &clazz->revision,
            kGetableSingleAndAll );                                         // revision

    // largest instance number
    InsertAttribute( (CipInstance*) clazz, 2, kCipUint,
            // (void*) &clazz->number_of_instances,
            NULL,
            kGetableSingleAndAll
            );

    // number of instances currently existing
    InsertAttribute( (CipInstance*) clazz, 3, kCipUint,
    //      (void*) &clazz->number_of_instances,
            NULL,
            kGetableSingleAndAll
            );

    // optional attribute list - default = 0
    InsertAttribute( (CipInstance*) clazz, 4, kCipUint, (void*) &kCipUintZero,
            kGetableAll );

    InsertAttribute( (CipInstance*) clazz, 5, kCipUint, (void*) &kCipUintZero,
            kGetableAll );               // optional service list - default = 0

    InsertAttribute( (CipInstance*) clazz, 6, kCipUint,
            (void*) &meta_class->highest_attr_id,
            kGetableSingleAndAll );      // max class attribute number

    InsertAttribute( (CipInstance*) clazz, 7, kCipUint,
            (void*) &clazz->highest_attr_id,
            kGetableSingleAndAll );      // max instance attribute number

    // create the standard class services
    if( get_all_class_attributes_mask )
    {
        InsertService( meta_class, kGetAttributeAll, &GetAttributeAll,
                "GetAttributeAll" );  // bind instance services to the metaclass
    }

    InsertService( meta_class, kGetAttributeSingle, &GetAttributeSingle,
            "GetAttributeSingle" );

    // create the standard instance services
    if( get_all_instance_attributes_mask )
    {
        // bind instance services to the class
        InsertService( clazz, kGetAttributeAll, &GetAttributeAll, "GetAttributeAll" );
    }

    InsertService( clazz, kGetAttributeSingle, &GetAttributeSingle,
            "GetAttributeSingle" );

    return clazz;
}


void CipInstance::InsertAttribute( EipUint16 attribute_id,
        EipUint8 cip_type, void* data, EipByte cip_flags )
{
    // remember the max attribute number that was defined
    if( attribute_id > cip_class->highest_attr_id )
    {
        cip_class->highest_attr_id = attribute_id;
    }

    // first look for a blank slot, otherwise insert a new attribute below
    for( unsigned i = 0; i < attributes.size();  ++i )
    {
        CipAttribute* a = attributes[i];

        if( a->data == NULL && a->type == 0 ) // found non set attribute
        {
            a->attribute_id = attribute_id;
            a->type = cip_type;
            a->attribute_flags = cip_flags;
            a->data = data;
            return;
        }
    }

    attributes.push_back( new CipAttribute(
                    attribute_id,
                    cip_type,
                    cip_flags,
                    data ) );
}


void InsertAttribute( CipInstance* instance, EipUint16 attribute_id,
        EipUint8 cip_type, void* data, EipByte cip_flags )
{
    instance->InsertAttribute( attribute_id, cip_type, data, cip_flags );
}


void CipClass::InsertService( EipUint8 service_id,
        CipServiceFunction service_function, const char* service_name )
{
    // Iterate over all service slots attached to the class
    for( unsigned i = 0; i < services.size();  ++i )
    {
        CipService* s = services[i];

        if( s->service_id == service_id || !s->service_function )    // found undefined service slot
        {
            s->service_id = service_id;
            s->service_function = service_function;
            s->service_name = service_name;
            return;
        }
    }

    // Create a new one
    services.push_back( new CipService( service_name, service_id, service_function ) );
}


void InsertService( CipClass* clazz, EipUint8 service_id,
        CipServiceFunction service_function, const char* service_name )
{
    clazz->InsertService( service_id, service_function, service_name );
}


CipAttribute* GetCipAttribute( CipInstance* instance,
        EipUint16 attribute_id )
{
    CipInstance::CipAttributes& attributes = instance->attributes;
    for( unsigned i = 0; i < attributes.size();   ++i )
    {
        if( attribute_id == attributes[i]->attribute_id )
            return attributes[i];
    }

    OPENER_TRACE_WARN( "attribute %d not defined\n", attribute_id );

    return 0;
}


// TODO this needs to check for buffer overflow
EipStatus GetAttributeSingle( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    // Mask for filtering get-ability
    EipByte get_mask;

    CipAttribute* attribute = GetCipAttribute(
            instance, request->request_path.attribute_number );

    EipByte* message = response->data;

    response->data_length = 0;
    response->reply_service = 0x80 | request->service;

    response->general_status = kCipErrorAttributeNotSupported;
    response->size_of_additional_status = 0;

    // set filter according to service: get_attribute_all or get_attribute_single
    if( kGetAttributeAll == request->service )
    {
        get_mask = kGetableAll;
        response->general_status = kCipErrorSuccess;
    }
    else
    {
        get_mask = kGetableSingle;
    }

    if( attribute && attribute->data )
    {
        if( attribute->attribute_flags & get_mask )
        {
            OPENER_TRACE_INFO( "%s: getAttribute %d\n",
                    __func__, request->request_path.attribute_number );

            // create a reply message containing the data

            /* TODO think if it is better to put this code in an own
             * getAssemblyAttributeSingle functions which will call get attribute
             * single.
             */

            if( attribute->type == kCipByteArray
                && instance->cip_class->class_id == kCipAssemblyClassCode )
            {
                // we are getting a byte array of a assembly object, kick out to the app callback
                OPENER_TRACE_INFO( " -> getAttributeSingle CIP_BYTE_ARRAY\r\n" );
                BeforeAssemblyDataSend( instance );
            }

            response->data_length = EncodeData( attribute->type,
                    attribute->data,
                    &message );

            response->general_status = kCipErrorSuccess;
        }
    }

    return kEipStatusOkSend;
}


int EncodeData( EipUint8 cip_type, void* data, EipUint8** message )
{
    int counter = 0;

    switch( cip_type )
    // check the data type of attribute
    {
    case kCipBool:
    case kCipSint:
    case kCipUsint:
    case kCipByte:
        **message = *(EipUint8*) (data);
        ++(*message);
        counter = 1;
        break;

    case kCipInt:
    case kCipUint:
    case kCipWord:
        AddIntToMessage( *(EipUint16*) (data), message );
        counter = 2;
        break;

    case kCipDint:
    case kCipUdint:
    case kCipDword:
    case kCipReal:
        AddDintToMessage( *(EipUint32*) (data), message );
        counter = 4;
        break;

#ifdef OPENER_SUPPORT_64BIT_DATATYPES
    case kCipLint:
    case kCipUlint:
    case kCipLword:
    case kCipLreal:
        AddLintToMessage( *(EipUint64*) (data), message );
        counter = 8;
        break;
#endif

    case kCipStime:
    case kCipDate:
    case kCipTimeOfDay:
    case kCipDateAndTime:
        break;

    case kCipString:
        {
            CipString* string = (CipString*) data;

            AddIntToMessage( *(EipUint16*) &(string->length), message );
            memcpy( *message, string->string, string->length );
            *message += string->length;

            counter = string->length + 2; // we have a two byte length field

            if( counter & 1 )
            {
                // we have an odd byte count
                **message = 0;
                ++(*message);
                counter++;
            }
        }
        break;

    case kCipString2:
    case kCipFtime:
    case kCipLtime:
    case kCipItime:
    case kCipStringN:
        break;

    case kCipShortString:
        {
            CipShortString* short_string = (CipShortString*) data;

            **message = short_string->length;
            ++(*message);

            memcpy( *message, short_string->string, short_string->length );
            *message += short_string->length;

            counter = short_string->length + 1;
        }
        break;

    case kCipTime:
        break;

    case kCipEpath:
        counter = EncodeEPath( (CipEpath*) data, message );
        break;

    case kCipEngUnit:
        break;

    case kCipUsintUsint:
        {
            CipRevision* revision = (CipRevision*) data;

            **message = revision->major_revision;
            ++(*message);
            **message = revision->minor_revision;
            ++(*message);
            counter = 2;
        }
        break;

    case kCipUdintUdintUdintUdintUdintString:
        {
            // TCP/IP attribute 5
            CipTcpIpNetworkInterfaceConfiguration* tcp_ip_network_interface_configuration =
                (CipTcpIpNetworkInterfaceConfiguration*) data;

            AddDintToMessage( ntohl( tcp_ip_network_interface_configuration->ip_address ), message );

            AddDintToMessage( ntohl( tcp_ip_network_interface_configuration->network_mask ), message );

            AddDintToMessage( ntohl( tcp_ip_network_interface_configuration->gateway ), message );

            AddDintToMessage( ntohl( tcp_ip_network_interface_configuration->name_server ), message );

            AddDintToMessage( ntohl( tcp_ip_network_interface_configuration->name_server_2 ), message );

            counter = 20;

            counter += EncodeData( kCipString, &tcp_ip_network_interface_configuration->domain_name,
                                message );
        }
        break;

    case kCip6Usint:
        {
            EipUint8* p = (EipUint8*) data;
            memcpy( *message, p, 6 );
            counter = 6;
        }
        break;

    case kCipMemberList:
        break;

    case kCipByteArray:
        {
            CipByteArray* cip_byte_array;
            OPENER_TRACE_INFO( " -> get attribute byte array\r\n" );
            cip_byte_array = (CipByteArray*) data;
            memcpy( *message, cip_byte_array->data, cip_byte_array->length );
            *message += cip_byte_array->length;
            counter = cip_byte_array->length;
        }
        break;

    case kInternalUint6:    // TODO for port class attribute 9, hopefully we can find a better way to do this
        {
            EipUint16* internal_uint16_6 = (EipUint16*) data;

            AddIntToMessage( internal_uint16_6[0], message );
            AddIntToMessage( internal_uint16_6[1], message );
            AddIntToMessage( internal_uint16_6[2], message );
            AddIntToMessage( internal_uint16_6[3], message );
            AddIntToMessage( internal_uint16_6[4], message );
            AddIntToMessage( internal_uint16_6[5], message );
            counter = 12;
        }
        break;

    default:
        break;
    }

    return counter;
}


int DecodeData( EipUint8 cip_type, void* data, EipUint8** message )
{
    int number_of_decoded_bytes = -1;

    // check the data type of attribute
    switch( cip_type )
    {
    case kCipBool:
    case kCipSint:
    case kCipUsint:
    case kCipByte:
        *(EipUint8*) data = **message;
        ++(*message);
        number_of_decoded_bytes = 1;
        break;

    case kCipInt:
    case kCipUint:
    case kCipWord:
        *(EipUint16*) data = GetIntFromMessage( message );
        number_of_decoded_bytes = 2;
        break;

    case kCipDint:
    case kCipUdint:
    case kCipDword:
        *(EipUint32*) data = GetDintFromMessage( message );
        number_of_decoded_bytes = 4;
        break;

#ifdef OPENER_SUPPORT_64BIT_DATATYPES
    case kCipLint:
    case kCipUlint:
    case kCipLword:
        {
            *(EipUint64*) data = GetLintFromMessage( message );
            number_of_decoded_bytes = 8;
        }
        break;
#endif

    case kCipString:
        {
            CipString* string = (CipString*) data;
            string->length = GetIntFromMessage( message );
            memcpy( string->string, *message, string->length );
            *message += string->length;

            number_of_decoded_bytes = string->length + 2; // we have a two byte length field

            if( number_of_decoded_bytes & 1 )
            {
                // we have an odd byte count
                ++(*message);
                number_of_decoded_bytes++;
            }
        }
        break;

    case kCipShortString:
        {
            CipShortString* short_string = (CipShortString*) data;

            short_string->length = **message;
            ++(*message);

            memcpy( short_string->string, *message, short_string->length );
            *message += short_string->length;

            number_of_decoded_bytes = short_string->length + 1;
        }
        break;

    default:
        break;
    }

    return number_of_decoded_bytes;
}


EipStatus GetAttributeAll( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    EipUint8*       reply = response->data;

    if( instance->instance_id == 2 )
    {
        OPENER_TRACE_INFO( "GetAttributeAll: instance number 2\n" );
    }

    CipInstance::CipAttributes& attributes = instance->attributes;

    CipClass::CipServices& services = instance->cip_class->services;
    for( unsigned i = 0; i < services.size();  ++i )  // hunt for the GET_ATTRIBUTE_SINGLE service
    {
        CipService* service = services[i];

        if( service->service_id == kGetAttributeSingle )   // found the service
        {
            if( 0 == attributes.size() )
            {
                response->data_length = 0;          // there are no attributes to be sent back
                response->reply_service = 0x80 | request->service;

                response->general_status = kCipErrorServiceNotSupported;
                response->size_of_additional_status = 0;
            }
            else
            {
                for( unsigned j = 0; j < attributes.size();  ++j ) // for each instance attribute of this class
                {
                    int attrNum = attributes[j]->attribute_id;

                    // only return attributes that are flagged as being part of GetAttributeAll
                    if( attrNum < 32 && (instance->cip_class->get_attribute_all_mask & (1 << attrNum) ) )
                    {
                        request->request_path.attribute_number = attrNum;

                        if( kEipStatusOkSend != service->service_function(
                                instance, request, response ) )
                        {
                            response->data = reply;
                            return kEipStatusError;
                        }

                        response->data += response->data_length;
                    }
                }

                response->data_length = response->data - reply;
                response->data = reply;
            }

            return kEipStatusOkSend;
        }
    }

    return kEipStatusOk; // Return kEipStatusOk if cannot find GET_ATTRIBUTE_SINGLE service
}


int EncodeEPath( CipEpath* epath, EipUint8** message )
{
    unsigned    length = epath->path_size;
    EipUint8*   p = *message;

    AddIntToMessage( epath->path_size, &p );

    if( epath->class_id < 256 )
    {
        *p++ = 0x20;   // 8 Bit Class Id
        *p++ = (EipUint8) epath->class_id;
        length -= 1;
    }
    else
    {
        *p++ = 0x21;   // 16 Bit Class Id
        *p++ = 0;      // pad byte
        AddIntToMessage( epath->class_id, &p );
        length -= 2;
    }

    if( 0 < length )
    {
        if( epath->instance_number < 256 )
        {
            *p++ = 0x24;   // 8 Bit Instance Id
            *p++ = (EipUint8) epath->instance_number;
            length -= 1;
        }
        else
        {
            *p++ = 0x25;   // 16 Bit Instance Id
            *p++ = 0;      // pad byte
            AddIntToMessage( epath->instance_number, &p );
            length -= 2;
        }

        if( 0 < length )
        {
            if( epath->attribute_number < 256 )
            {
                *p++ = 0x30;   // 8 Bit Attribute Id
                *p++ = (EipUint8) epath->attribute_number;
                length -= 1;
            }
            else
            {
                *p++ = 0x31;   // 16 Bit Attribute Id
                *p++ = 0;      // pad byte
                AddIntToMessage( epath->attribute_number, &p );
                length -= 2;
            }
        }
    }

    *message = p;

    return 2 + epath->path_size * 2; // path size is in 16 bit chunks according to the specification
}


int DecodePaddedEPath( CipEpath* epath, EipUint8** message )
{
    unsigned    number_of_decoded_elements;
    EipUint8*   p = *message;

    epath->path_size = *p++;

    // Copy path to structure, in version 0.1 only 8 bit for Class,Instance
    // and Attribute, need to be replaced with function
    epath->class_id = 0;
    epath->instance_number  = 0;
    epath->attribute_number = 0;

    for( number_of_decoded_elements = 0;
         number_of_decoded_elements < epath->path_size;
         number_of_decoded_elements++ )
    {
        if( kSegmentTypeSegmentTypeReserved == ( *p & kSegmentTypeSegmentTypeReserved ) )
        {
            // If invalid/reserved segment type, segment type greater than 0xE0
            return kEipStatusError;
        }

        switch( *p )
        {
        case kSegmentTypeLogicalSegment + kLogicalSegmentLogicalTypeClassId +
                kLogicalSegmentLogicalFormatEightBitValue:
            epath->class_id = p[1];
            p += 2;
            break;

        case kSegmentTypeLogicalSegment + kLogicalSegmentLogicalTypeClassId +
                kLogicalSegmentLogicalFormatSixteenBitValue:
            p += 2;
            epath->class_id = GetIntFromMessage( &p );
            number_of_decoded_elements++;
            break;

        case kSegmentTypeLogicalSegment + kLogicalSegmentLogicalTypeInstanceId +
                kLogicalSegmentLogicalFormatEightBitValue:
            epath->instance_number = p[1];
            p += 2;
            break;

        case kSegmentTypeLogicalSegment + kLogicalSegmentLogicalTypeInstanceId +
                kLogicalSegmentLogicalFormatSixteenBitValue:
            p += 2;
            epath->instance_number = GetIntFromMessage( &p );
            number_of_decoded_elements++;
            break;

        case kSegmentTypeLogicalSegment + kLogicalSegmentLogicalTypeAttributeId +
                kLogicalSegmentLogicalFormatEightBitValue:
            epath->attribute_number = p[1];
            p += 2;
            break;

        case kSegmentTypeLogicalSegment + kLogicalSegmentLogicalTypeAttributeId +
                kLogicalSegmentLogicalFormatSixteenBitValue:
            p += 2;
            epath->attribute_number = GetIntFromMessage( &p );
            number_of_decoded_elements++;
            break;

        default:
            OPENER_TRACE_ERR( "wrong path requested\n" );
            return kEipStatusError;
            break;
        }
    }

    *message = p;
    return number_of_decoded_elements * 2 + 1;  // times 2 since every encoding uses 2 bytes
}
