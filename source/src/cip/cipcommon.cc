/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/
#include <string.h>

#include "cipcommon.h"

#include "trace.h"
#include "cipster_api.h"
#include "cipidentity.h"
#include "ciptcpipinterface.h"
#include "cipethernetlink.h"
#include "cipconnectionmanager.h"
#include "cipconnection.h"
#include "byte_bufs.h"
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


/// Binary search function template, same as vec_search but returns equal or next greater
template< typename T, typename IterT >
IterT vec_search_gte( IterT begin, IterT end, T target )
{
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
    return end;
}


// global public variables
EipByte g_message_data_reply_buffer[CIPSTER_MESSAGE_DATA_REPLY_BUFFER];

const EipUint16 kCipUintZero = 0;

// private functions

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

    eip_status = ConnectionManagerInit();
    CIPSTER_ASSERT( kEipStatusOk == eip_status );

    eip_status = ConnectionClassInit( unique_connection_id );
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

    DestroyIoConnectionData();
}


//-----<CipAttrube>-------------------------------------------------------

CipAttribute::CipAttribute(
        int         aAttributeId,
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
    BufWriter out = response->data; // always copy response->data so it is not advanced.

    response->data_length = EncodeData( attr->type, attr->data, out );

    return kEipStatusOkSend;
}


EipStatus SetAttrData( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    BufReader in = request->data;

    int out_count = DecodeData( attr->type, attr->data, in );

    if( out_count >= 0 )
        return kEipStatusOkSend;
    else
        return kEipStatusError;
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

        BufWriter out = response->data;

        response->data_length = EncodeData( attr->type, &instance_count, out );

        return kEipStatusOkSend;
    }
    return kEipStatusError;
}


//-----<CipInstance>------------------------------------------------------

CipInstance::CipInstance( int aInstanceId ) :
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
            CIPSTER_TRACE_ERR( "class '%s' instance %d already has attribute %d, overriding\n",
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
        int             attribute_id,
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
        int             attribute_id,
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


CipAttribute* CipInstance::Attribute( int aAttributeId ) const
{
    CipAttributes::const_iterator  it;

    // a binary search thru the vector of pointers looking for aAttributeId
    it = vec_search( attributes.begin(), attributes.end(), aAttributeId );

    if( it != attributes.end() )
        return *it;

    CIPSTER_TRACE_WARN( "attribute %d not defined\n", aAttributeId );

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

    // class of "this" public class is meta_class, call meta-class special constructor.
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
        A metaClass is a class that holds the class services.
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
        // be NULL for the meta-class, and IsMetaClass() returns true.
        delete owning_class;
    }

    while( services.size() )
    {
#if 0
        if( class_name == "TCP/IP Interface" )
        {
            ShowServices();
        }
#endif

        delete *services.begin();

        services.erase( services.begin() );
    }
}


CipError CipClass::OpenConnection( CipConn* aConn, CipCommonPacketFormatData* cpfd, ConnectionManagerStatusCode* extended_error )
{
    CIPSTER_TRACE_INFO( "%s: NOT implemented for class '%s'\n", __func__, ClassName().c_str() );
    *extended_error = kConnectionManagerStatusCodeInconsistentApplicationPathCombo;
    return kCipErrorConnectionFailure;
}


int CipClass::FindUniqueFreeId() const
{
    int last_id = 0;

    for( CipInstances::const_iterator it = instances.begin(); it != instances.end(); ++it )
    {
        // Is there a gap here?
        if( (*it)->Id() > last_id + 1 )
            break;

        last_id = (*it)->Id();
    }

    return last_id + 1;
}


CipService* CipClass::Service( int aServiceId ) const
{
    CipServices::const_iterator  it;

    // binary search thru vector of pointers looking for attribute_id
    it = vec_search( services.begin(), services.end(), aServiceId );

    if( it != services.end() )
        return *it;

    CIPSTER_TRACE_WARN( "service %d not defined\n", aServiceId );

    return NULL;
}


bool CipClass::InstanceInsert( CipInstance* aInstance )
{
    if( aInstance->owning_class )
    {
        CIPSTER_TRACE_ERR( "%s: aInstance id:%d is already owned\n",
            __func__, aInstance->Id() );

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


CipInstance* CipClass::InstanceRemove( int aInstanceId )
{
    CipInstance* ret = NULL;

    for( CipInstances::iterator it = instances.begin();  it != instances.end();  ++it )
    {
        if( aInstanceId == (*it)->Id() )
        {
            CIPSTER_TRACE_INFO(
                "%s: removing instance '%d'.\n", __func__, aInstanceId );

            ret = *it;                  // pass ownership to ret
            instances.erase( it );      // close gap
            break;
        }
    }

    return ret;
}


CipInstance* CipClass::Instance( int aInstanceId ) const
{
    if( aInstanceId == 0 )
        return (CipInstance*)  this;        // cast away const-ness

    CipInstances::const_iterator  it;

    // binary search thru the vector of pointers looking for id
    it = vec_search( instances.begin(), instances.end(), aInstanceId );

    if( it != instances.end() )
        return *it;

    CIPSTER_TRACE_WARN( "instance %d not in class '%s'\n",
        aInstanceId, class_name.c_str() );

    return NULL;
}


CipClass::CipInstances::const_iterator CipClass::InstanceNext( int aInstanceId ) const
{
    CipInstances::const_iterator it = vec_search_gte( instances.begin(), instances.end(), aInstanceId );
    return it;
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


CipService* CipClass::ServiceInsert( int aServiceId,
        CipServiceFunction aServiceFunction, const char* aServiceName )
{
    CipService* service = new CipService( aServiceName, aServiceId, aServiceFunction );

    if( !ServiceInsert( service ) )
    {
        delete service;
        service = NULL;     // return NULL on failure
    }

    return service;
}


CipService* CipClass::ServiceRemove( int aServiceId )
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


int EncodeData( int aDataType, const void* data, BufWriter& aBuf )
{
    EipByte*    start = aBuf.data();

    switch( aDataType )
    // check the data type of attribute
    {
    case kCipBool:
    case kCipSint:
    case kCipUsint:
    case kCipByte:
        *aBuf++ = *(EipUint8*) data;
        break;

    case kCipInt:
    case kCipUint:
    case kCipWord:
        aBuf.put16( *(EipUint16*) data );
        break;

    case kCipDint:
    case kCipUdint:
    case kCipDword:
    case kCipReal:
        aBuf.put32( *(EipUint32*) data );
        break;

    case kCipLint:
    case kCipUlint:
    case kCipLword:
    case kCipLreal:
        aBuf.put64( *(EipUint64*) data );
        break;

    case kCipStime:
    case kCipDate:
    case kCipTimeOfDay:
    case kCipDateAndTime:
        break;

    case kCipString:
        {
            CipString* string = (CipString*) data;

            aBuf.put16( *(EipUint16*) &string->length );
            aBuf.append( string->string, string->length );

            if( (aBuf.data() - start) & 1 )
            {
                *aBuf++ = 0;       // pad to even byte count
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

            *aBuf++ = short_string->length;
            aBuf.append( short_string->string, short_string->length );
        }
        break;

    case kCipTime:
        break;

    case kCipEngUnit:
        break;

    case kCipUsintUsint:
        {
            CipRevision* revision = (CipRevision*) data;

            *aBuf++ = revision->major_revision;
            *aBuf++ = revision->minor_revision;
        }
        break;

    case kCipUdintUdintUdintUdintUdintString:
        {
            // TCP/IP attribute 5
            CipTcpIpNetworkInterfaceConfiguration* tcp_data =
                (CipTcpIpNetworkInterfaceConfiguration*) data;

            aBuf.put32( ntohl( tcp_data->ip_address ) );
            aBuf.put32( ntohl( tcp_data->network_mask ) );
            aBuf.put32( ntohl( tcp_data->gateway ) );
            aBuf.put32( ntohl( tcp_data->name_server ) );
            aBuf.put32( ntohl( tcp_data->name_server_2 ) );

            EncodeData( kCipString, &tcp_data->domain_name, aBuf );
        }
        break;

    case kCip6Usint:
        {
            aBuf.append( (const EipByte*) data, 6 );
        }
        break;

    case kCipMemberList:
        break;

    case kCipByteArray:
        {
            CIPSTER_TRACE_INFO( "%s: CipByteArray\n", __func__ );
            CipByteArray* cip_byte_array = (CipByteArray*) data;

            // the array length is not encoded for CipByteArray.
            aBuf.append( cip_byte_array->data, cip_byte_array->length );
        }
        break;

    default:
        break;
    }

    int byte_count = aBuf.data() - start;

    return byte_count;
}


int DecodeData( int aDataType, void* data, BufReader& aBuf )
{
    const EipByte* start = aBuf.data();

    // check the data type of attribute
    switch( aDataType )
    {
    case kCipBool:
    case kCipSint:
    case kCipUsint:
    case kCipByte:
        *(EipUint8*) data = *aBuf++;
        break;

    case kCipInt:
    case kCipUint:
    case kCipWord:
        *(EipUint16*) data = aBuf.get16();
        break;

    case kCipDint:
    case kCipUdint:
    case kCipDword:
        *(EipUint32*) data = aBuf.get32();
        break;

    case kCipLint:
    case kCipUlint:
    case kCipLword:
        *(EipUint64*) data = aBuf.get64();
        break;

    case kCipByteArray:
        // this code has no notion of buffer overrun protection or memory ownership, be careful.
        {
            CIPSTER_TRACE_INFO( "%s: kCipByteArray\n", __func__ );

            // The CipByteArray's length must be set by caller, i.e. known
            // in advance and set in advance by caller.  And the data field
            // must point to a buffer large enough for this.
            CipByteArray* byte_array = (CipByteArray*) data;
            memcpy( byte_array->data, aBuf.data(), byte_array->length );
            aBuf += byte_array->length;   // no length field
        }
        break;

    case kCipString:
        {
            CipString* string = (CipString*) data;
            string->length = *aBuf++;
            memcpy( string->string, aBuf.data(), string->length );
            aBuf += string->length;

            // serialized input was padded to even byte count
            if( (aBuf.data() - start) & 1 )
            {
                ++aBuf;
            }
        }
        break;

    case kCipShortString:
        {
            CipShortString* short_string = (CipShortString*) data;

            short_string->length = *aBuf++;

            memcpy( short_string->string, aBuf.data(), short_string->length );
            aBuf += short_string->length;
        }
        break;

    default:
        return -1;
    }

    int byte_count = aBuf.data() - start;

    return byte_count;
}


EipStatus GetAttributeSingle( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    // Mask for filtering get-ability
    EipByte get_mask;

    // set filter according to service: get_attribute_all or get_attribute_single
    if( kGetAttributeAll == request->service )
    {
        get_mask = kGetableAll;
    }
    else
    {
        get_mask = kGetableSingle;
    }

    CipAttribute* attribute = instance->Attribute( request->request_path.GetAttribute() );

    if( attribute )
    {
        CIPSTER_TRACE_INFO( "%s: attribute->attribute_flags:%02x\n",
            __func__, attribute->attribute_flags );

        if( attribute->attribute_flags & get_mask )
        {
            CIPSTER_TRACE_INFO(
                "%s: attribute:%d  class:'%s'  instance:%d\n",
                __func__,
                request->request_path.GetAttribute(),
                instance->Id() == 0 ? ((CipClass*)instance)->ClassName().c_str() :
                                        instance->owning_class->ClassName().c_str(),
                instance->Id()
                );

            // create a reply message containing the data
            attribute->Get( request, response );

            CIPSTER_TRACE_INFO( "%s: attribute_id:%d  len:%d\n",
                __func__, attribute->Id(), response->data_length );
        }
        else
            response->general_status = kCipErrorAttributeNotSupported;
    }
    else
        response->general_status = kCipErrorAttributeNotSupported;

    return kEipStatusOkSend;
}


EipStatus GetAttributeAll( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    BufWriter start = response->data;

    CipService* service = instance->owning_class->Service( kGetAttributeSingle );

    if( service )
    {
        const CipInstance::CipAttributes& attributes = instance->Attributes();

        if( !attributes.size() )
        {
            // there are no attributes to be sent back
            response->general_status = kCipErrorServiceNotSupported;
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
                    // change the attribute id in the request path
                    request->request_path.SetAttribute( attribute_id );

                    EipStatus result = service->service_function( instance, request, response );

                    if( result != kEipStatusOkSend )
                    {
                        response->data = start;
                        return kEipStatusError;
                    }

                    response->data += response->data_length;
                    response->data_length = 0;
                    response->general_status = kCipErrorSuccess;    // clear non-readable from GetAttributeSingle()
                }
            }

            response->data_length = response->data.data() - start.data();

            CIPSTER_TRACE_INFO( "%s: response->data_length:%d\n", __func__, response->data_length );

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


EipStatus SetAttributeSingle( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    CipAttribute* attribute = instance->Attribute( request->request_path.GetAttribute() );

    if( attribute )
    {
        if( attribute->attribute_flags & kSetable )
        {
            CIPSTER_TRACE_INFO(
                "%s: attribute:%d  class:'%s'  instance:%d\n",
                __func__,
                request->request_path.GetAttribute(),
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

    else
    {
        // we don't have this attribute
        response->general_status = kCipErrorAttributeNotSupported;
    }

    return kEipStatusOkSend;
}


