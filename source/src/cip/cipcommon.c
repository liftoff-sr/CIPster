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

/// Binary search function template, with templated compare function
template< typename T, typename IterT, typename compare >
IterT vec_search( IterT begin, IterT end, T target, compare comp )
{
    IterT initial_end = end;

    while( begin < end )
    {
        IterT middle = begin + (end - begin - 1)/2;
        int r = comp( target, *middle );
        if( r < 0 )
            end = middle;
        else if( r > 0 )
            begin = middle + 1;
        else
            return middle;
    }
    return initial_end;
}

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

    /*
    // clean the data needed for the assembly object's attribute 3
    ShutdownAssemblies();
    */

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

        const CipClass::CipServices& services = cip_class->Services();
        for( unsigned i = 0;  i < services.size();  ++i )
        {
            CipService* service = services[i];

            if( request->service == service->service_id )    // if match is found
            {
                OPENER_TRACE_INFO( "notify: calling '%s' service\n", service->service_name.c_str() );
                OPENER_ASSERT( service->service_function );

                // call the service, and return what it returns
                return service->service_function( instance, request, response );
            }
        }

        OPENER_TRACE_WARN( "notify: service 0x%x not supported\n",
                request->service );

        // if no services or service not found, return an error reply
        response->general_status = kCipErrorServiceNotSupported;
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


CipInstance::~CipInstance()
{
    if( cip_class )         // if not nested in a meta-class
    {
        if( instance_id )   // or nested in a public class
        {
            OPENER_TRACE_INFO( "deleting instance %d of class '%s'\n",
                instance_id, cip_class->class_name.c_str() );
        }
    }

    for( unsigned i = 0; i < attributes.size(); ++i )
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
}


CipClass::~CipClass()
{
    // cip_class is NULL for a meta-class, which BTW does not own its one
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

    OPENER_TRACE_INFO( "deleting class '%s'\n", class_name.c_str() );
}


static int serv_comp( EipUint8 id, CipService* service )
{
    return id - service->service_id;
}


CipService* CipClass::Service( EipUint8 service_id ) const
{
    CipClass::CipServices::const_iterator  it;

    // binary search thru vector of pointers looking for attribute_id
    it = vec_search( services.begin(), services.end(), service_id, serv_comp );

    if( it != services.end() )
        return *it;

    OPENER_TRACE_WARN( "service %d not defined\n", service_id );

    return NULL;
}


CipInstance* CipClass::InstanceInsert( EipUint32 instance_id )
{
    CipClass::CipInstances::iterator it;

    // Keep sorted by id
    for( it = instances.begin(); it != instances.end();  ++it )
    {
        if( instance_id < (*it)->instance_id )
            break;

        else if( instance_id == (*it)->instance_id )
        {
            OPENER_TRACE_ERR( "class '%s' already has instance %d\n",
                class_name.c_str(), instance_id
                );

            return NULL;
        }
    }

    CipInstance* instance = new CipInstance( instance_id, this );

    instances.insert( it, instance );

    return instance;
}


static int inst_comp( EipUint32 instance_id, CipInstance* instance )
{
    return instance_id - instance->instance_id;
}


CipInstance* CipClass::Instance( EipUint32 instance_id ) const
{
    CipInstances::const_iterator  it;

    // a binary search thru the vector of pointers looking for attribute_id
    it = vec_search( instances.begin(), instances.end(), instance_id, inst_comp );

    if( it != instances.end() )
        return *it;

    OPENER_TRACE_WARN( "instance %d not in class '%s'\n",
        instance_id, class_name.c_str() );

    return NULL;
}


bool AddCipInstances( CipClass* cip_class, int number_of_instances )
{
    OPENER_TRACE_INFO( "adding %d instances to class '%s'\n", number_of_instances,
            cip_class->class_name.c_str() );

    const CipClass::CipInstances& instances = cip_class->Instances();

    // Assume no instances have been inserted which have a higher instance_id
    // than their respective index into the collection.  (Not always true, can fail.)

    int instance_number = instances.size()
                            + 1;  // the first instance is number 1

    // create the new instances
    for( int i = 0; i < number_of_instances;  ++i, ++instance_number )
    {
        if( !cip_class->InstanceInsert( instance_number ) )
        {
            OPENER_TRACE_ERR( "class '%s' collision on instance_id: %d\n",
                cip_class->class_name.c_str(), instance_number );

            return false;
        }
    }
    return true;
}


CipInstance* AddCIPInstance( CipClass* clazz, EipUint32 instance_id )
{
    return clazz->InstanceInsert( instance_id );
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
        AddCipInstances( clazz, number_of_instances );
    }

    if( RegisterCipClass( clazz ) == kEipStatusError )
    {
        return 0;       // TODO handle return value and clean up if necessary
    }

    // create the standard class attributes

    InsertAttribute( clazz, 1, kCipUint, (void*) &clazz->revision,
            kGetableSingleAndAll );                                         // revision

    // largest instance number
    InsertAttribute( clazz, 2, kCipUint,
            // (void*) &clazz->number_of_instances,
            NULL,
            kGetableSingleAndAll
            );

    // number of instances currently existing
    InsertAttribute( clazz, 3, kCipUint,
            // (void*) &clazz->number_of_instances,
            NULL,
            kGetableSingleAndAll
            );

    // optional attribute list - default = 0
    InsertAttribute( clazz, 4, kCipUint, (void*) &kCipUintZero,
            kGetableAll );

    InsertAttribute( clazz, 5, kCipUint, (void*) &kCipUintZero,
            kGetableAll );               // optional service list - default = 0

    InsertAttribute( clazz, 6, kCipUint,
            (void*) &meta_class->highest_attr_id,
            kGetableSingleAndAll );      // max class attribute number

    InsertAttribute( clazz, 7, kCipUint,
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


CipAttribute::~CipAttribute()
{
    if( own_data && data )
    {
        CipFree( data );
        data = NULL;
    }
}


CipAttribute* CipInstance::AttributeInsert( EipUint16 attribute_id,
        EipUint8 cip_type, void* data, EipByte cip_flags, bool attr_owns_data )
{
    CipInstance::CipAttributes::iterator it;

    // Keep sorted by id
    for( it = attributes.begin(); it != attributes.end();  ++it )
    {
        if( attribute_id < (*it)->attribute_id )
            break;

        else if( attribute_id == (*it)->attribute_id )
        {
            OPENER_TRACE_ERR( "class '%s' instance %d already has attribute %d\n",
                cip_class ? cip_class->class_name.c_str() : "meta-something",
                instance_id,
                attribute_id
                );

            return NULL;
        }
    }

    // remember the max attribute number that was defined
    if( attribute_id > cip_class->highest_attr_id )
    {
        cip_class->highest_attr_id = attribute_id;
    }

    CipAttribute* attribute = new CipAttribute(
                    attribute_id,
                    cip_type,
                    cip_flags,
                    data,
                    attr_owns_data
                    );

    attributes.insert( it, attribute );

    return attribute;
}


bool InsertAttribute( CipInstance* instance, EipUint16 attribute_id,
        EipUint8 cip_type, void* data, EipByte cip_flags )
{
    return instance->AttributeInsert( attribute_id, cip_type, data, cip_flags );
}


bool CipClass::InsertService( EipUint8 service_id,
        CipServiceFunction service_function, const char* service_name )
{
    CipClass::CipServices::iterator it;

    // Keep sorted by id
    for( it = services.begin(); it != services.end();  ++it )
    {
        if( service_id < (*it)->service_id )
            break;

        else if( service_id == (*it)->service_id )
        {
            OPENER_TRACE_ERR( "class '%s' already has service %d\n",
                class_name.c_str(), service_id
                );

            return false;
        }
    }

    services.insert( it,
        new CipService( service_name, service_id, service_function )
        );

    return true;
}


bool InsertService( CipClass* clazz, EipUint8 service_id,
        CipServiceFunction service_function, const char* service_name )
{
    return clazz->InsertService( service_id, service_function, service_name );
}


static int attr_comp( EipUint16 id, CipAttribute* attr )
{
    return id - attr->attribute_id;
}


CipAttribute* CipInstance::Attribute( EipUint16 attribute_id ) const
{
    CipInstance::CipAttributes::const_iterator  it;

    // a binary search thru the vector of pointers looking for attribute_id
    it = vec_search( attributes.begin(), attributes.end(), attribute_id, attr_comp );

    if( it != attributes.end() )
        return *it;

    OPENER_TRACE_WARN( "attribute %d not defined\n", attribute_id );

    return NULL;
}


CipAttribute* GetCipAttribute( CipInstance* instance,
        EipUint16 attribute_id )
{
    return instance->Attribute( attribute_id );
}


// TODO this needs to check for buffer overflow
EipStatus GetAttributeSingle( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    // Mask for filtering get-ability
    EipByte get_mask;

    CipAttribute* attribute = instance->Attribute( request->request_path.attribute_number );

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
    EipUint8* reply = response->data;

    if( instance->instance_id == 2 )
    {
        OPENER_TRACE_INFO( "GetAttributeAll: instance number 2\n" );
    }

    CipService* service = instance->cip_class->Service( kGetAttributeSingle );

    if( service )
    {
        const CipInstance::CipAttributes& attributes = instance->Attributes();

        if( !attributes.size() )
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
    else
    {
        // Return kEipStatusOk if cannot find GET_ATTRIBUTE_SINGLE service
        return kEipStatusOk;
    }
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
