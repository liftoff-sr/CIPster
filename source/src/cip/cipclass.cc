/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#include <unordered_map>

#include <cipclass.h>
#include <cipcommon.h>
#include <cipster_api.h>
#include <cipmessagerouter.h>


static EipUint16 Zero = 0;



/**
 * Class CipClassRegistry
 * is a container for the defined CipClass()es, which in turn hold all
 * the CipInstance()s.  This container takes ownership of the CipClasses.
 * (Ownership means having the obligation to delete upon destruction.)
 */
class CipClassRegistry
{
    // hashtable from C++ std library.
    typedef std::unordered_map< int, CipClass* >    ClassHash;

public:
    CipClass*   FindClass( int aClassId )
    {
        ClassHash::iterator it = container.find( aClassId );

        if( it != container.end() )
            return it->second;

        return NULL;
    }

    /** @brief Register a Class in the CIP class registry for the message router
     *  @param aClass a class object to be registered, and to take ownership over.
     *  @return bool - true.. success
     *                 false.. class with conflicting class_id is already registered
     */
    bool RegisterClass( CipClass* aClass )
    {
        ClassHash::value_type e( aClass->ClassId(), aClass );

        std::pair< ClassHash::iterator, bool > r = container.insert( e );

        return r.second;
    }

    void DeleteAll()
    {
        while( container.size() )
        {
            delete container.begin()->second;       // Delete the first of remaining classes
            container.erase( container.begin() );   // Erase first class's ClassEntry
        }
    }

    ~CipClassRegistry()
    {
        DeleteAll();
    }

private:

    ClassHash   container;
};


static CipClassRegistry registry;



CipClass::CipClass(
        int         aClassId,
        const char* aClassName,
        int         aClassAttributesMask,
        int         aRevision
        ) :
    CipInstance( 0 ),       // instance_id of public class is always 0
    class_id( aClassId ),
    class_name( aClassName ),
    revision( aRevision )
{
    // The public class holds services for the instances, and attributes for itself.

    // class of "this" public class is meta_class, call meta-class special constructor.
    CipClass* meta_class = new CipClass(
                aClassId,
                aClassName
                );

    // The meta class has no attributes, but holds services for the public class.

    // The meta class has only one instance and it is the public class and it
    // is not owned by the meta-class (will not delete it during destruction).
    // But in fact the public class owns the meta-class.
    meta_class->InstanceInsert( this );     // sets this->cip_class also.

    // Create the standard class attributes as requested.
    // See Vol 1 Table 4-4.2

    if( aClassAttributesMask & (1<<1) )
        AttributeInsert( 1, kCipUint, &revision );

    // largest instance id
    if( aClassAttributesMask & (1<<2) )
        AttributeInsert( 2, getLargestInstanceId );

    // number of instances currently existing
    if( aClassAttributesMask & (1<<3) )
        AttributeInsert( 3, getInstanceCount );

    // optional attribute list - default = 0
    if( aClassAttributesMask & (1<<4) )
        AttributeInsert( 4, kCipUint, &Zero );

    // optional service list - default = 0
    if( aClassAttributesMask & (1<<5) )
        AttributeInsert( 5, kCipUint, &Zero );

    // max class attribute number
    if( aClassAttributesMask & (1<<6) )
        AttributeInsert( 6, getLargestClassAttributeId );

    // max instance attribute number
    if( aClassAttributesMask & (1<<7) )
        AttributeInsert( 7, getLargestInstanceAttributeId );

    // create the standard instance services
    ServiceInsert( kGetAttributeSingle, GetAttributeSingle, "GetAttributeSingle" );
    ServiceInsert( kSetAttributeSingle, SetAttributeSingle, "SetAttributeSingle" );

    if( getable_all_mask )
        ServiceInsert( kGetAttributeAll, GetAttributeAll, "GetAttributeAll" );
}


// meta-class constructor
CipClass::CipClass(
        int aClassId,
        const char* aClassName
        ) :
    CipInstance( -1 ),              // instance_id and NULL class
    class_id( aClassId ),
    class_name( std::string( "meta-" ) + aClassName ),
    revision( 0 )
{
    /*
        A metaClass is a class that holds the class services.
        CIP can talk to an instance, therefore an instance has a pointer to
        its class. CIP can talk to a class, therefore a class struct is a
        subclass of the instance struct, and contains a pointer to a
        metaclass. CIP never explicitly addresses a metaclass.
    */

    ServiceInsert( kGetAttributeSingle, GetAttributeSingle, "GetAttributeSingle" );
    ServiceInsert( kGetAttributeAll,    GetAttributeAll,    "GetAttributeAll" );
    ServiceInsert( kReset,              Reset,              "Reset" );
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
            delete instances.back();
            instances.pop_back();
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

        delete services.back();
        services.pop_back();
    }
}


EipStatus CipClass::Register( CipClass* cip_class )
{
    if( registry.RegisterClass( cip_class ) )
        return kEipStatusOk;
    else
        return kEipStatusError;
}


CipClass* CipClass::Get( int aClassId )
{
    return registry.FindClass( aClassId );
}


void CipClass::DeleteAll()
{
    registry.DeleteAll();
}


CipError CipClass::OpenConnection( ConnectionData* aParams,
        Cpf* cpfd, ConnMgrStatus* extended_error )
{
    CIPSTER_TRACE_INFO( "%s: NOT implemented for class '%s'\n", __func__, ClassName().c_str() );
    *extended_error = kConnMgrStatusInconsistentApplicationPathCombo;
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
    if( aInstance->Class() )
    {
        CIPSTER_TRACE_ERR( "%s: aInstance id:%d is already owned\n",
            __func__, aInstance->Id() );

        return false;
    }

    CipInstances::iterator it;

    // Keep instances sorted by Id()
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

    instances.insert( it, aInstance );

    // it's official, instance is a member of this class as of now.
    aInstance->setClass( this );

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
                "%s: removing service '%s' from class '%s'.\n",
                __func__, (*it)->ServiceName().c_str(),
                ClassName().c_str()
                );

            ret = *it;              // pass ownership to ret
            services.erase( it );   // close gap
            break;
        }
    }

    return ret;
}

//----<AttrubuteFuncs>-------------------------------------------------------

EipStatus CipClass::getInstanceCount( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    CipClass* clazz = dynamic_cast<CipClass*>( attr->Instance() );

    // This func must be invoked only on a class attribute,
    // because on an instance attribute clazz will be NULL since
    // Instance() is not a CipClass and dynamic_cast<> returns NULL.
    if( clazz )
    {
        EipUint16 instance_count = clazz->InstanceCount();

        BufWriter out = response->Writer();

        out.put16( instance_count );
        response->SetWrittenSize( 2 );

        return kEipStatusOkSend;
    }
    return kEipStatusError;
}


EipStatus CipClass::getLargestInstanceId( CipAttribute* attr,
    CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    CipClass* clazz = dynamic_cast<CipClass*>( attr->Instance() );

    // This func must be invoked only on a class attribute,
    // because on an instance attribute clazz will be NULL since
    // Instance() is not a CipClass and dynamic_cast<> returns NULL.
    if( clazz )
    {
        EipUint16 largest_id = 0;

        if( clazz->InstanceCount() )
        {
            // instances are sorted by Id(), so last one is highest.
            largest_id = clazz->Instances().back()->Id();
        }

        BufWriter out = response->Writer();

        out.put16( largest_id );
        response->SetWrittenSize( 2 );

        return kEipStatusOkSend;
    }
    return kEipStatusError;
}


EipStatus CipClass::getLargestInstanceAttributeId( CipAttribute* attr,
    CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    CipInstance* inst = attr->Instance();

    if( inst )
    {
        EipUint16 largest_id = 0;

        if( inst->Attributes().size() )
        {
            // attributes are sorted by Id(), so last one is highest.
            largest_id = inst->Attributes().back()->Id();
        }

        BufWriter out = response->Writer();

        out.put16( largest_id );
        response->SetWrittenSize( 2 );

        return kEipStatusOkSend;
    }
    return kEipStatusError;
}


EipStatus CipClass::getLargestClassAttributeId( CipAttribute* attr,
    CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    CipClass* clazz = dynamic_cast<CipClass*>( attr->Instance() );

    // This func must be invoked only on a class attribute,
    // because on an instance attribute clazz will be NULL since
    // Instance() is not a CipClass and dynamic_cast<> returns NULL.
    if( clazz )
    {
        EipUint16 largest_id = 0;

        if( clazz->Attributes().size() )
        {
            // attributes are sorted by Id(), so last one is highest.
            largest_id = clazz->Attributes().back()->Id();
        }

        BufWriter out = response->Writer();

        out.put16( largest_id );
        response->SetWrittenSize( 2 );

        return kEipStatusOkSend;
    }
    return kEipStatusError;
}


//----</AttrubuteFuncs>------------------------------------------------------


//-----<CipServiceFunctions>----------------------------------------------------

EipStatus CipClass::GetAttributeSingle( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    int     attribute_id = request->Path().GetAttribute();

#if 0
    if( instance->Id() == 0 && instance->Class()->ClassId() == 1 && attribute_id == 3 )
    {
        int break_here = 1;
    }
#endif

    CipAttribute* attribute = instance->Attribute( attribute_id );
    if( !attribute )
    {
        response->SetGenStatus( kCipErrorAttributeNotSupported );
        return kEipStatusOkSend;
    }

    return attribute->Get( request, response );
}


EipStatus CipClass::GetAttributeAll( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    BufWriter start = response->Writer();

    // Implement GetAttributeAll() by calling GetAttributeSingle() in a loop.
    CipService* service = instance->Class()->Service( kGetAttributeSingle );

    if( !service )
    {
        // Return kEipStatusOk if cannot find kGetAttributeSingle service
        return kEipStatusOk;
    }

#if 0
    if( instance->Id() == 0 && instance->Class()->ClassId() == 6 )
    {
        int break_here = 1;
    }
#endif

    const CipInstance::CipAttributes& attributes = instance->Attributes();

    if( !attributes.size() )
    {
        // there are no attributes to be sent back
        response->SetGenStatus( kCipErrorServiceNotSupported );
    }
    else
    {
        int get_mask = instance->getable_all_mask;

        for( CipInstance::CipAttributes::const_iterator it = attributes.begin();
                it != attributes.end(); ++it )
        {
            int attribute_id = (*it)->Id();

            // only do attributes that are flagged as being part of GetAttributeAll
            if( attribute_id < 32 && (get_mask & (1 << attribute_id)) )
            {
                // change the attribute id in the request path
                request->SetPathAttribute( attribute_id );

                EipStatus result = service->service_function( instance, request, response );

                if( result != kEipStatusOkSend )
                {
                    response->SetWriter( start );
                    return kEipStatusError;
                }

                response->WriterAdvance( response->WrittenSize() );
                response->SetWrittenSize( 0 );

                // clear non-readable from GetAttributeSingle()
                response->SetGenStatus( kCipErrorSuccess );
            }
        }

        response->SetWrittenSize( response->Writer().data() - start.data() );

        CIPSTER_TRACE_INFO( "%s: response->WrittenSize():%d\n", __func__, response->WrittenSize() );

        response->SetWriter( start );
    }

    return kEipStatusOkSend;
}


EipStatus CipClass::SetAttributeSingle( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    CipAttribute* attribute = instance->Attribute( request->Path().GetAttribute() );

    if( !attribute )
    {
        response->SetGenStatus( kCipErrorAttributeNotSupported );
        return kEipStatusOkSend;
    }

    return attribute->Set( request, response );
}


EipStatus CipClass::Reset( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    // Each class must override this per Vol1 appendix A.

    if( request->Data().size() )
    {
        // Conformance tool is complaining about any parameters in default Reset
        response->SetGenStatus( kCipErrorInvalidParameter );
    }

    return kEipStatusOkSend;
}

//-----</CipServiceFuncs>-------------------------------------------------------
