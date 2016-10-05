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


int g_CIPSTER_TRACE_LEVEL = CIPSTER_TRACE_LEVEL;

/// Binary search function template, dedicated for classes with Id() member func
template< typename T, typename IterT >
IterT vec_search( IterT begin, IterT end, T target )
{
    IterT initial_end = end;

    while( begin < end )
    {
        IterT   middle = begin + (end - begin - 1)/2;
        int     r = target - (*middle)->Id();

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
EipUint8 g_message_data_reply_buffer[CIPSTER_MESSAGE_DATA_REPLY_BUFFER];

const EipUint16 kCipUintZero = 0;

// private functions
int EncodeEPath( CipEpath* epath, EipUint8** message );

void CipStackInit( EipUint16 unique_connection_id )
{
    EipStatus eip_status;

    EncapsulationInit();

    // The message router is the first CIP object be initialized!!!
    eip_status = CipMessageRouterInit();
    CIPSTER_ASSERT( kEipStatusOk == eip_status );

    eip_status = CipIdentityInit();
    CIPSTER_ASSERT( kEipStatusOk == eip_status );

    eip_status = CipTcpIpInterfaceInit();
    CIPSTER_ASSERT( kEipStatusOk == eip_status );

    eip_status = CipEthernetLinkInit();
    CIPSTER_ASSERT( kEipStatusOk == eip_status );

    eip_status = ConnectionManagerInit( unique_connection_id );
    CIPSTER_ASSERT( kEipStatusOk == eip_status );

    eip_status = CipAssemblyInitialize();
    CIPSTER_ASSERT( kEipStatusOk == eip_status );

#if 0    // do this in caller after return from this function.
    // the application has to be initialized last
    eip_status = ApplicationInitialization();
    CIPSTER_ASSERT( kEipStatusOk == eip_status );
#endif

    (void) eip_status;
}


void ShutdownCipStack()
{
    // First close all connections
    CloseAllConnections();

    // Than free the sockets of currently active encapsulation sessions
    EncapsulationShutDown();

    ShutdownTcpIpInterface();

    // clear all the instances and classes
    DeleteAllClasses();
}


EipStatus NotifyClass( CipClass* cip_class,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    unsigned instance_id = request->request_path.instance_number;

    CipInstance* instance = cip_class->Instance( instance_id );

    if( instance )
    {
        CIPSTER_TRACE_INFO(
            "%s: targeting instance %d of class %s\n",
            __func__,
            instance_id,
            instance_id ? instance->owning_class->ClassName().c_str() :
                          ((CipClass*)instance)->ClassName().c_str()
            );

        CipService* service = cip_class->Service( request->service );

        if( service )
        {
            CIPSTER_TRACE_INFO( "%s: calling service '%s'\n", service->ServiceName().c_str() );
            CIPSTER_ASSERT( service->service_function );

            // call the service, and return what it returns
            return service->service_function( instance, request, response );
        }

        CIPSTER_TRACE_WARN( "%s: service 0x%x not supported\n",
                __func__,
                request->service );

        // if no services or service not found, return an error reply
        response->general_status = kCipErrorServiceNotSupported;
    }
    else
    {
        CIPSTER_TRACE_WARN( "%s: instance %d does not exist\n", __func__, instance_id );

        // If instance not found, return an error reply.
        // According to the test tool this should be the correct error flag
        // instead of CIP_ERROR_OBJECT_DOES_NOT_EXIST;
        response->general_status = kCipErrorPathDestinationUnknown;
    }

    // handle error replies, general_status was set above.

    response->size_of_additional_status = 0;
    response->data_length = 0;
    response->reply_service = 0x80 | request->service;

    return kEipStatusOkSend;
}

//-----<CipAttrube>-------------------------------------------------------

CipAttribute::CipAttribute(
        EipUint16   aAttributeId,
        EipUint8    aType,
        EipUint8    aFlags,

        // assign your getter and setter per attribute, either may be NULL
        AttributeFunc aGetter,
        AttributeFunc aSetter,

        void*   aData
        ) :
    attribute_id( aAttributeId ),
    type( aType ),
    attribute_flags( aFlags ),
    data( aData ),
    owning_instance( 0 ),
    getter( aGetter ),
    setter( aSetter )
{
}


CipAttribute::~CipAttribute()
{
}


EipStatus GetAttrData( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    EipByte* message = response->data;

    response->data_length = EncodeData( attr->type, attr->data, &message );

    response->general_status = kCipErrorSuccess;

    return kEipStatusOkSend;
}


EipStatus GetInstanceCount( CipAttribute* attr, CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    CipClass* clazz = dynamic_cast<CipClass*>( attr->Instance() );

    // This func must be invoked only on a class attribute,
    // because on an instance attribute clazz will be NULL since
    // instance is not a CipClass and dynamic_cast<> returns NULL.
    if( clazz )
    {
        EipUint16 instance_count = clazz->Instances().size();

        EipByte* message = response->data;

        response->data_length = EncodeData( attr->type, &instance_count, &message );

        response->general_status = kCipErrorSuccess;

        return kEipStatusOkSend;
    }
    return kEipStatusError;
}


EipStatus SetAttrData( CipAttribute* attr, CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    response->size_of_additional_status = 0;
    response->reply_service = 0x80 | request->service;

#if 1
    EipByte* message = request->data;

    int out_count = DecodeData( attr->type, attr->data, &message );

    if( out_count >= 0 )
    {
        request->data_length -= out_count;
        request->data += out_count;

        response->general_status = kCipErrorSuccess;
        response->data_length = 0;

        return kEipStatusOkSend;
    }
    else
        return kEipStatusError;

#else

    response->general_status = kCipErrorAttributeNotSetable;

    response->data_length = 0;
    return kEipStatusOkSend;

#endif
}


//-----<CipInstance>------------------------------------------------------

CipInstance::CipInstance( EipUint32 aInstanceId ) :
    instance_id( aInstanceId ),
    owning_class( 0 ),           // NULL (not owned) until I am inserted into a CipClass
    highest_inst_attr_id( 0 )
{
}


CipInstance::~CipInstance()
{
    if( owning_class )      // if not nested in a meta-class
    {
        if( instance_id )   // and not nested in a public class, then I am an instance.
        {
            CIPSTER_TRACE_INFO( "deleting instance %d of class '%s'\n",
                instance_id, owning_class->ClassName().c_str() );
        }
    }

    for( unsigned i = 0; i < attributes.size(); ++i )
        delete attributes[i];
    attributes.clear();
}

bool CipInstance::AttributeInsert( CipAttribute* aAttribute )
{
    CipAttributes::iterator it;

    CIPSTER_ASSERT( !aAttribute->owning_instance );  // only un-owned attributes may be inserted

    // Keep sorted by id
    for( it = attributes.begin(); it != attributes.end();  ++it )
    {
        if( aAttribute->Id() < (*it)->Id() )
            break;

        else if( aAttribute->Id() == (*it)->Id() )
        {
            CIPSTER_TRACE_ERR( "class '%s' instance %d already has attribute %d, ovveriding\n",
                owning_class ? owning_class->ClassName().c_str() : "meta-something",
                instance_id,
                aAttribute->Id()
                );

            // Re-use this slot given by position 'it'.
            delete *it;
            attributes.erase( it );    // will re-insert at this position below
            break;
        }
    }

    attributes.insert( it, aAttribute );

    aAttribute->owning_instance = this; // until now there was no owner of this attribute.

    // remember the max attribute number that was inserted
    if( aAttribute->Id() > highest_inst_attr_id )
    {
        highest_inst_attr_id = aAttribute->Id();
    }

    if( owning_class )
    {
        if( highest_inst_attr_id > owning_class->highest_attr_id )
            owning_class->highest_attr_id = highest_inst_attr_id;
    }

    return true;
}


CipAttribute* CipInstance::AttributeInsert(
        EipUint16       attribute_id,
        EipUint8        cip_type,
        EipByte         cip_flags,
        AttributeFunc   aGetter,
        AttributeFunc   aSetter,
        void* data
        )
{
    CipAttribute* attribute = new CipAttribute(
                    attribute_id,
                    cip_type,
                    cip_flags,
                    aGetter,
                    aSetter,
                    data
                    );

    if( !AttributeInsert( attribute ) )
    {
        delete attribute;
        attribute = NULL;   // return NULL on failure
    }

    return attribute;
}


CipAttribute* CipInstance::AttributeInsert(
        EipUint16       attribute_id,
        EipUint8        cip_type,
        EipByte         cip_flags,
        void* data
        )
{
    CipAttribute* attribute = new CipAttribute(
                    attribute_id,
                    cip_type,
                    cip_flags,
                    NULL,
                    NULL,
                    data
                    );

    if( !AttributeInsert( attribute ) )
    {
        delete attribute;
        attribute = NULL;   // return NULL on failure
    }

    return attribute;
}


CipAttribute* CipInstance::Attribute( EipUint16 attribute_id ) const
{
    CipAttributes::const_iterator  it;

    // a binary search thru the vector of pointers looking for attribute_id
    it = vec_search( attributes.begin(), attributes.end(), attribute_id );

    if( it != attributes.end() )
        return *it;

    CIPSTER_TRACE_WARN( "attribute %d not defined\n", attribute_id );

    return NULL;
}


//-----<CipClass>--------------------------------------------------------------

CipClass::CipClass(
        EipUint32   aClassId,
        const char* aClassName,
        int         aClassAttributesMask,
        EipUint32   a_get_all_class_attributes_mask,
        EipUint32   a_get_all_instance_attributes_mask,
        EipUint16   aRevision
        ) :
    CipInstance( 0 ),       // instance_id of public class is always 0
    class_id( aClassId ),
    class_name( aClassName ),
    revision( aRevision ),
    highest_attr_id( 0 ),
    highest_inst_id( 0 ),
    get_attribute_all_mask( a_get_all_instance_attributes_mask )
{
    // The public class holds services for the instances, and attributes for itself.

    // class of "this" public class is meta_class.
    CipClass* meta_class = new CipClass(
                aClassName,
                a_get_all_class_attributes_mask
                );

    // The meta class has no attributes, but holds services for the public class.

    // The meta class has only one instance and it is the public class and it
    // is not owned by the meta-class (will not delete it during destruction).
    // But in fact the public class owns the meta-class.
    meta_class->InstanceInsert( this );     // sets this->cip_class also.

    // create the standard class attributes as requested

    if( aClassAttributesMask & (1<<1) )
        AttributeInsert( 1, kCipUint, kGetableSingleAndAll, (void*) &revision );

    // largest instance id
    if( aClassAttributesMask & (1<<2) )
        AttributeInsert( 2, kCipUint, kGetableSingleAndAll, (void*) &highest_inst_id );

    // number of instances currently existing, dynamically determined elsewhere
    if( aClassAttributesMask & (1<<3) )
        AttributeInsert( 3, kCipUint, kGetableSingleAndAll, GetInstanceCount, NULL );

    // optional attribute list - default = 0
    if( aClassAttributesMask & (1<<4) )
        AttributeInsert( 4, kCipUint, kGetableAll, (void*) &kCipUintZero );

    // optional service list - default = 0
    if( aClassAttributesMask & (1<<5) )
        AttributeInsert( 5, kCipUint, kGetableAll, (void*) &kCipUintZero );

    // max class attribute number
    if( aClassAttributesMask & (1<<6) )
        AttributeInsert( 6, kCipUint, kGetableSingleAndAll, (void*) &meta_class->highest_attr_id );

    // max instance attribute number
    if( aClassAttributesMask & (1<<7) )
        AttributeInsert( 7, kCipUint, kGetableSingleAndAll, (void*) &highest_attr_id );

    // create the standard instance services
    ServiceInsert( kGetAttributeSingle, GetAttributeSingle, "GetAttributeSingle" );
    ServiceInsert( kSetAttributeSingle, SetAttributeSingle, "SetAttributeSingle" );

    if( get_attribute_all_mask )
    {
        ServiceInsert( kGetAttributeAll, GetAttributeAll, "GetAttributeAll" );
    }
}


CipClass::CipClass(
        // meta-class constructor

        const char* aClassName,             ///< without "meta-" prefix
        EipUint32   a_get_all_class_attributes_mask
        ) :
    CipInstance( 0xffffffff ),        // instance_id and NULL class
    class_id( 0xffffffff ),
    class_name( std::string( "meta-" ) + aClassName ),
    revision( 0 ),
    highest_attr_id( 0 ),
    highest_inst_id( 0 ),
    get_attribute_all_mask( a_get_all_class_attributes_mask )
{
    /*
        A metaClass is a class that holds the class attributes and services.
        CIP can talk to an instance, therefore an instance has a pointer to
        its class. CIP can talk to a class, therefore a class struct is a
        subclass of the instance struct, and contains a pointer to a
        metaclass. CIP never explicitly addresses a metaclass.
    */

    ServiceInsert( kGetAttributeSingle, GetAttributeSingle, "GetAttributeSingle" );

    // create the standard class services
    if( get_attribute_all_mask )
    {
        ServiceInsert( kGetAttributeAll, GetAttributeAll, "GetAttributeAll" );
    }
}


CipClass::~CipClass()
{
    CIPSTER_TRACE_INFO( "deleting class '%s'\n", class_name.c_str() );

    // a meta-class does not own its one public class instance
    if( !IsMetaClass() )
    {
        // delete all the instances of this class
        while( instances.size() )
        {
            delete *instances.begin();

            // There could be a faster way, but this is not time critical
            // because the program is terminating here.
            instances.erase( instances.begin() );
        }

        // The public class owns the meta-class.
        // Delete the meta-class, which invokes a small bit of recursion
        // back into this function, but on the nested call cip_class will
        // be NULL for the meta-class.
        delete owning_class;
    }

    while( services.size() )
    {
#if 0
        if( class_name == "TCP/IP interface" )
        {
            ShowServices();
        }
#endif

        delete *services.begin();

        services.erase( services.begin() );
    }
}


CipService* CipClass::Service( EipUint8 service_id ) const
{
    CipServices::const_iterator  it;

    // binary search thru vector of pointers looking for attribute_id
    it = vec_search( services.begin(), services.end(), service_id );

    if( it != services.end() )
        return *it;

    CIPSTER_TRACE_WARN( "service %d not defined\n", service_id );

    return NULL;
}


bool CipClass::InstanceInsert( CipInstance* aInstance )
{
    if( aInstance->owning_class )
    {
        CIPSTER_TRACE_ERR( "%s: aInstance id:%d is already owned\n", __func__, aInstance->Id() );
        return false;
    }

    CipInstances::iterator it;

    // Keep sorted by id
    for( it = instances.begin();  it != instances.end();  ++it )
    {
        if( aInstance->Id() < (*it)->Id() )
            break;

        else if( aInstance->Id() == (*it)->Id() )
        {
            CIPSTER_TRACE_ERR( "class '%s' already has instance %d\n",
                class_name.c_str(), aInstance->Id()
                );

            return false;
        }
    }

    if( aInstance->Id() > highest_inst_id )
        highest_inst_id = aInstance->Id();

    instances.insert( it, aInstance );

    // it's official, instance is a member of this class as of now.
    aInstance->owning_class = this;

    if( aInstance->highest_inst_attr_id > highest_attr_id )
        owning_class->highest_attr_id = aInstance->highest_inst_attr_id;

    return true;
}


CipInstance* CipClass::InstanceInsert( EipUint32 instance_id )
{
    CipInstance* instance = new CipInstance( instance_id );

    if( !InstanceInsert( instance ) )
    {
        delete instance;
        instance = NULL;        // return NULL on failure
    }

    return instance;
}


CipInstance* CipClass::Instance( EipUint32 instance_id ) const
{
    if( instance_id == 0 )
        return (CipInstance*)  this;        // cast away const-ness

    CipInstances::const_iterator  it;

    // binary search thru the vector of pointers looking for id
    it = vec_search( instances.begin(), instances.end(), instance_id );

    if( it != instances.end() )
        return *it;

    CIPSTER_TRACE_WARN( "instance %d not in class '%s'\n",
        instance_id, class_name.c_str() );

    return NULL;
}


bool CipClass::ServiceInsert( CipService* aService )
{
    CipServices::iterator it;

    // Keep sorted by id
    for( it = services.begin();  it != services.end();  ++it )
    {
        if( aService->Id() < (*it)->Id() )
            break;

        else if( aService->Id() == (*it)->Id() )
        {
            CIPSTER_TRACE_ERR( "class '%s' already has service %d, overriding.\n",
                class_name.c_str(), aService->Id()
                );

            // re-use this slot given by position 'it'.
            delete *it;             // delete existing CipService
            services.erase( it );   // will re-gap service st::vector with following insert()
            break;
        }
    }

    services.insert( it, aService );

    return true;
}


CipService* CipClass::ServiceInsert( EipUint8 service_id,
        CipServiceFunction service_function, const char* service_name )
{
    CipService* service = new CipService( service_name, service_id, service_function );

    if( !ServiceInsert( service ) )
    {
        delete service;
        service = NULL;     // return NULL on failure
    }

    return service;
}


CipService* CipClass::ServiceRemove( EipUint8 aServiceId )
{
    CipService* ret = NULL;

    for( CipServices::iterator it = services.begin();  it != services.end();  ++it )
    {
        if( aServiceId == (*it)->Id() )
        {
            CIPSTER_TRACE_INFO(
                "%s: removing service '%s'.\n", __func__, (*it)->ServiceName().c_str() );

            ret = *it;              // pass ownership to ret
            services.erase( it );   // close gap
            break;
        }
    }

    return ret;
}


// TODO this needs to check for buffer overflow
EipStatus GetAttributeSingle( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    // Mask for filtering get-ability
    EipByte get_mask;

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

    CipAttribute* attribute = instance->Attribute( request->request_path.attribute_number );

    if( attribute )
    {
        CIPSTER_TRACE_INFO( "%s: attribute->attribute_flags:%02x\n",
            __func__, attribute->attribute_flags );

        if( attribute->attribute_flags & get_mask )
        {
            CIPSTER_TRACE_INFO(
                "%s: attribute:%d  class:'%s'  instance:%d\n",
                __func__,
                request->request_path.attribute_number,
                instance->Id() == 0 ? ((CipClass*)instance)->ClassName().c_str() :
                                        instance->owning_class->ClassName().c_str(),
                instance->Id()
                );

            // create a reply message containing the data
            attribute->Get( request, response );
        }
    }

#if 0
    else if( request->request_path.attribute_number == 0 )
    {
        // This abomination is wanted by the conformance test tool:
        response->general_status = kCipErrorServiceNotSupported;
    }
#endif

    return kEipStatusOkSend;
}


EipStatus SetAttributeSingle( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    response->size_of_additional_status = 0;
    response->data_length = 0;
    response->reply_service = 0x80 | request->service;

    CipAttribute* attribute = instance->Attribute( request->request_path.attribute_number );

    if( attribute )
    {
        if( attribute->attribute_flags & kSetable )
        {
            CIPSTER_TRACE_INFO(
                "%s: attribute:%d  class:'%s'  instance:%d\n",
                __func__,
                request->request_path.attribute_number,
                instance->Id() == 0 ? ((CipClass*)instance)->ClassName().c_str() :
                                        instance->owning_class->ClassName().c_str(),
                instance->Id()
                );

            // Set() is very "attribute specific" and is determined by which
            // AttributeFunc is installed into the attribute, if any.
            return attribute->Set( request, response );
        }

        // it is an attribute we have, however this attribute is not setable
        response->general_status = kCipErrorAttributeNotSetable;
    }

#if 0
    else if( request->request_path.attribute_number == 0 )
    {
        // This abomination is wanted by the conformance test tool:
        response->general_status = kCipErrorServiceNotSupported;
    }
#endif

    else
    {
        // we don't have this attribute
        response->general_status = kCipErrorAttributeNotSupported;
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
        **message = *(EipUint8*) data;
        ++(*message);
        counter = 1;
        break;

    case kCipInt:
    case kCipUint:
    case kCipWord:
        AddIntToMessage( *(EipUint16*) data, message );
        counter = 2;
        break;

    case kCipDint:
    case kCipUdint:
    case kCipDword:
    case kCipReal:
        AddDintToMessage( *(EipUint32*) data, message );
        counter = 4;
        break;

#ifdef CIPSTER_SUPPORT_64BIT_DATATYPES
    case kCipLint:
    case kCipUlint:
    case kCipLword:
    case kCipLreal:
        AddLintToMessage( *(EipUint64*) data, message );
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

            AddIntToMessage( *(EipUint16*) &string->length, message );
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
            CIPSTER_TRACE_INFO( "%s: CipByteArray\n", __func__ );
            CipByteArray* cip_byte_array = (CipByteArray*) data;

            // the array length is not encoded for CipByteArray.
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
    int decode_byte_count = -1;

    // check the data type of attribute
    switch( cip_type )
    {
    case kCipBool:
    case kCipSint:
    case kCipUsint:
    case kCipByte:
        *(EipUint8*) data = **message;
        ++(*message);
        decode_byte_count = 1;
        break;

    case kCipInt:
    case kCipUint:
    case kCipWord:
        *(EipUint16*) data = GetIntFromMessage( message );
        decode_byte_count = 2;
        break;

    case kCipDint:
    case kCipUdint:
    case kCipDword:
        *(EipUint32*) data = GetDintFromMessage( message );
        decode_byte_count = 4;
        break;

#ifdef CIPSTER_SUPPORT_64BIT_DATATYPES
    case kCipLint:
    case kCipUlint:
    case kCipLword:
        {
            *(EipUint64*) data = GetLintFromMessage( message );
            decode_byte_count = 8;
        }
        break;
#endif

    case kCipByteArray:
        // this code has no notion of buffer overrun protection or memory ownership, be careful.
        {
            CIPSTER_TRACE_INFO( "%s: kCipByteArray\n", __func__ );

            // The CipByteArray's length must be set by caller, i.e. known
            // in advance and set in advance by caller.  And the data field
            // must point to a buffer large enough for this.
            CipByteArray* byte_array = (CipByteArray*) data;
            memcpy( byte_array->data, *message, byte_array->length );
            decode_byte_count = byte_array->length;   // no length field
        }
        break;

    case kCipString:
        {
            CipString* string = (CipString*) data;
            string->length = GetIntFromMessage( message );
            memcpy( string->string, *message, string->length );
            *message += string->length;

            decode_byte_count = string->length + 2; // 2 byte length field

            if( decode_byte_count & 1 )
            {
                // we have an odd byte count
                ++(*message);
                decode_byte_count++;
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

            decode_byte_count = short_string->length + 1;
        }
        break;

    default:
        break;
    }

    return decode_byte_count;
}


EipStatus GetAttributeAll( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    EipUint8* start = response->data;

    /*
    if( instance->instance_id == 2 )
    {
        CIPSTER_TRACE_INFO( "GetAttributeAll: instance number 2\n" );
    }
    */

    CipService* service = instance->owning_class->Service( kGetAttributeSingle );

    if( service )
    {
        const CipInstance::CipAttributes& attributes = instance->Attributes();

        if( !attributes.size() )
        {
            // there are no attributes to be sent back
            response->data_length = 0;
            response->reply_service = 0x80 | request->service;

            response->general_status = kCipErrorServiceNotSupported;
            response->size_of_additional_status = 0;
        }
        else
        {
            EipUint32 get_mask = instance->owning_class->get_attribute_all_mask;

            for( CipInstance::CipAttributes::const_iterator it = attributes.begin();
                    it != attributes.end(); ++it )
            {
                int attribute_id = (*it)->Id();

                // only return attributes that are flagged as being part of GetAttributeAll
                if( attribute_id < 32 && (get_mask & (1 << attribute_id)) )
                {
                    request->request_path.attribute_number = attribute_id;

                    EipStatus result = service->service_function( instance, request, response );

                    if( result != kEipStatusOkSend )
                    {
                        response->data = start;
                        return kEipStatusError;
                    }

                    response->data += response->data_length;
                }
            }

            response->data_length = response->data - start;
            response->data = start;
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

    if( length > 0 )
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

        if( length > 0 )
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

    // path size is in 16 bit chunks according to the specification
    return 2 + epath->path_size * 2;
}


int DecodePaddedEPath( CipEpath* epath, EipUint8** message )
{
    unsigned    decoded_element_count;
    EipUint8*   p = *message;

    epath->path_size = *p++;

    // Copy path to structure, in version 0.1 only 8 bit for Class,Instance
    // and Attribute, need to be replaced with function
    epath->class_id = 0;
    epath->instance_number  = 0;
    epath->attribute_number = 0;

    for( decoded_element_count = 0;
         decoded_element_count < epath->path_size;
         decoded_element_count++ )
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
            decoded_element_count++;
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
            decoded_element_count++;
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
            decoded_element_count++;
            break;

        default:
            CIPSTER_TRACE_ERR( "wrong path requested\n" );
            return kEipStatusError;
        }
    }

    *message = p;
    return decoded_element_count * 2 + 1;  // times 2 since every encoding uses 2 bytes
}


int DecodePortSegment( CipPortSegment* aSegment, EipUint8** aMessage )
{
    EipUint8*   p = *aMessage;
    EipUint8    first = *p++;

    if( (first & 0xE0) != kSegmentTypePortSegment )
    {
        CIPSTER_TRACE_ERR( "wrong path requested\n" );
        return kEipStatusError;
    }

    int link_addrz = (first & kPortSegmentFlagExtendedLinkAddressSize) ? *p++ : 0;

    if( first & 0xf == 15 )
        aSegment->port = GetIntFromMessage( &p );
    else
        aSegment->port = first & 0xf;

    switch( link_addrz )
    {
    case 0:
        break;
    case 1:
        aSegment->address = *p++;
        break;
    case 2:
        aSegment->address = GetIntFromMessage( &p );
        break;
    case 4:
        aSegment->address = GetDintFromMessage( &p );
        break;
    default:
        CIPSTER_TRACE_ERR( "unsupported link address size\n" );
        return kEipStatusError;
    }

    if( (p - *aMessage) & 1 )
        ++p;

    int ret = p - *aMessage;

    *aMessage = p;

    return ret;
}

