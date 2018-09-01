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


static uint16_t Zero = 0;



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
    revision( aRevision ),
    clss_getable_all_mask( 0 ),
    inst_getable_all_mask( 0 )
{
    owning_class = this;

    ServiceInsert( _C, kGetAttributeSingle, GetAttributeSingle, "GetAttributeSingle" );
    ServiceInsert( _C, kGetAttributeAll,    GetAttributeAll,    "GetAttributeAll" );
    ServiceInsert( _C, kReset,              Reset,              "Reset" );

    // Create the standard class attributes as requested.
    // See Vol 1 Table 4-4.2

    if( aClassAttributesMask & (1<<1) )
        AttributeInsert( _C, 1, kCipUint, &revision );

    // largest instance id
    if( aClassAttributesMask & (1<<2) )
        AttributeInsert( _C, 2, getLargestInstanceId );

    // number of instances currently existing
    if( aClassAttributesMask & (1<<3) )
        AttributeInsert( _C, 3, getInstanceCount );

    // optional attribute list - default = 0
    if( aClassAttributesMask & (1<<4) )
        AttributeInsert( _C, 4, kCipUint, &Zero );

    // optional service list - default = 0
    if( aClassAttributesMask & (1<<5) )
        AttributeInsert( _C, 5, kCipUint, &Zero );

    // max class attribute number
    if( aClassAttributesMask & (1<<6) )
        AttributeInsert( _C, 6, getLargestClassAttributeId );

    // max instance attribute number
    if( aClassAttributesMask & (1<<7) )
        AttributeInsert( _C, 7, getLargestInstanceAttributeId );

    // create the standard instance services
    ServiceInsert( _I, kGetAttributeSingle, GetAttributeSingle, "GetAttributeSingle" );
    ServiceInsert( _I, kSetAttributeSingle, SetAttributeSingle, "SetAttributeSingle" );

    if( inst_getable_all_mask )
        ServiceInsert( _I, kGetAttributeAll, GetAttributeAll, "GetAttributeAll" );
}


CipClass::~CipClass()
{
    // delete all the instances of this class
    while( instances.size() )
    {
        delete instances.back();
        instances.pop_back();
    }

    while( services[_I].size() )
    {
        delete services[_I].back();
        services[_I].pop_back();
    }
    while( services[_C].size() )
    {
        delete services[_C].back();
        services[_C].pop_back();
    }

    while( attributes[_I].size() )
    {
        delete attributes[_I].back();
        attributes[_I].pop_back();
    }
    while( attributes[_C].size() )
    {
        delete attributes[_C].back();
        attributes[_C].pop_back();
    }

    CIPSTER_TRACE_INFO( "deleting class '%s'\n", class_name.c_str() );
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


bool CipClass::InstanceInsert( CipInstance* aInstance )
{
    CIPSTER_ASSERT( aInstance->Id() > 0 && aInstance->Id() <= 65535 );

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


CipInstances::const_iterator CipClass::InstanceNext( int aInstanceId ) const
{
    CipInstances::const_iterator it = vec_search_gte( instances.begin(), instances.end(), aInstanceId );
    return it;
}


CipService* CipClass::Service( _CI aCI, int aServiceId ) const
{
    CipServices::const_iterator  it;

    const CipServices& slist = services[aCI];

    // binary search thru vector of pointers looking for attribute_id
    it = vec_search( slist.begin(), slist.end(), aServiceId );

    if( it != slist.end() )
        return *it;

    CIPSTER_TRACE_WARN( "service %d not defined\n", aServiceId );

    return NULL;
}


bool CipClass::ServiceInsert( _CI aCI, CipService* aService )
{
    CipServices::iterator it;

    CipServices& s = services[aCI];

    // Keep sorted by id
    for( it = s.begin();  it != s.end();  ++it )
    {
        if( aService->Id() < (*it)->Id() )
            break;

        else if( aService->Id() == (*it)->Id() )
        {
            CIPSTER_TRACE_ERR(
                "%s: class '%s' already has service %d, overriding.\n",
                __func__, class_name.c_str(), aService->Id() );

            // re-use this slot given by position 'it'.
            delete *it;             // delete existing CipService
            s.erase( it );   // will re-gap service st::vector with following insert()
            break;
        }
    }

    s.insert( it, aService );

    return true;
}


CipService* CipClass::ServiceInsert( _CI aCI, int aServiceId,
        CipServiceFunction aServiceFunction, const char* aServiceName )
{
    CipService* service = new CipService( aServiceName, aServiceId, aServiceFunction );

    if( !ServiceInsert( aCI, service ) )
    {
        delete service;
        service = NULL;     // return NULL on failure
    }

    return service;
}


CipService* CipClass::ServiceRemove( _CI aCI, int aServiceId )
{
    CipService* ret = NULL;

    CipServices& s = services[aCI];

    for( CipServices::iterator it = s.begin();  it != s.end();  ++it )
    {
        if( aServiceId == (*it)->Id() )
        {
            CIPSTER_TRACE_INFO(
                "%s: removing service '%s' from class '%s'.\n",
                __func__, (*it)->ServiceName().c_str(),
                ClassName().c_str()
                );

            ret = *it;              // pass ownership to ret
            s.erase( it );      // close gap
            break;
        }
    }

    return ret;
}


bool CipClass::AttributeInsert(  _CI aCI, CipAttribute* aAttribute )
{
    CipAttributes::iterator it;

    CipAttributes& a = attributes[aCI];

    CIPSTER_ASSERT( !aAttribute->owning_class );  // only un-owned attributes may be inserted

    bool is_class = (aCI == _C);

    // Keep sorted by id
    for( it = a.begin(); it != a.end();  ++it )
    {
        if( aAttribute->Id() < (*it)->Id() )
            break;

        else if( aAttribute->Id() == (*it)->Id() )
        {
            CIPSTER_TRACE_ERR(
                "%s: class '%s' already has %s attribute %d, overriding\n",
                __func__,
                owning_class->ClassName().c_str(),
                is_class ? "a class" : "an instance",
                aAttribute->Id()
                );

            // Re-use this slot given by position 'it'.
            delete *it;
            a.erase( it );    // will re-insert at this position below
            break;
        }
    }

    a.insert( it, aAttribute );

    aAttribute->owning_class = this; // until now there was no owner of this attribute.

    if( aAttribute->Id() < 32 )
    {
        if( aAttribute->IsGetableAll() )
        {
            if( is_class )
                clss_getable_all_mask |= 1 << aAttribute->Id();
            else
                inst_getable_all_mask |= 1 << aAttribute->Id();
        }
    }

    return true;
}


CipAttribute* CipClass::AttributeInsert( _CI aCI,
        int             aAttributeId,
        AttributeFunc   aGetter,
        bool            isGetableAll,
        AttributeFunc   aSetter,
        uintptr_t       aCookie,
        bool            isCookieAnInstanceOffset,
        CipDataType     aDataType
        )
{
    CipAttribute* attribute = new CipAttribute(
            aAttributeId,
            aDataType,
            aGetter,
            aSetter,
            (uintptr_t) aCookie,
            isGetableAll,
            isCookieAnInstanceOffset
            );

    if( !AttributeInsert( aCI, attribute ) )
    {
        delete attribute;
        attribute = NULL;   // return NULL on failure
    }

    return attribute;
}

#if 0
CipAttribute* CipClass::AttributeInsert( _CI aCI,
        int             aAttributeId,
        AttributeFunc   aGetter,
        bool            isGetableAll,
        AttributeFunc   aSetter,
        uint16_t        aCookie,
        bool            isCookieAnInstanceOffset,
        CipDataType     aDataType
        )
{
    CipAttribute* attribute = new CipAttribute(
            aAttributeId,
            aDataType,
            aGetter,
            aSetter,
            aCookie,
            isGetableAll,
            isCookieAnInstanceOffset
            );

    if( !AttributeInsert( aCI, attribute ) )
    {
        delete attribute;
        attribute = NULL;   // return NULL on failure
    }

    return attribute;
}
#endif


CipAttribute* CipClass::AttributeInsert( _CI aCI,
        int             aAttributeId,
        CipDataType     aCipType,
        void*           aCookie,
        bool            isGetableSingle,
        bool            isGetableAll,
        bool            isSetableSingle
        )
{
    CipAttribute* attribute = new CipAttribute(
            aAttributeId,
            aCipType,
            isGetableSingle ? CipAttribute::GetAttrData : NULL,
            isSetableSingle ? CipAttribute::SetAttrData : NULL,
            (uintptr_t) aCookie,
            isGetableAll,
            false                   // isDataAnInstanceOffset
            );

    if( !AttributeInsert( aCI, attribute ) )
    {
        delete attribute;
        attribute = NULL;   // return NULL on failure
    }

    return attribute;
}


CipAttribute* CipClass::AttributeInsert( _CI aCI,
        int             aAttributeId,
        CipDataType     aCipType,
        uint16_t        aCookie,
        bool            isGetableSingle,
        bool            isGetableAll,
        bool            isSetableSingle
        )
{
    CipAttribute* attribute = new CipAttribute(
            aAttributeId,
            aCipType,
            isGetableSingle ? CipAttribute::GetAttrData : NULL,
            isSetableSingle ? CipAttribute::SetAttrData : NULL,
            aCookie,
            isGetableAll,
            true                    // isDataAnInstanceOffset
            );

    if( !AttributeInsert( aCI, attribute ) )
    {
        delete attribute;
        attribute = NULL;   // return NULL on failure
    }

    return attribute;
}


CipAttribute* CipClass::Attribute( _CI aCI, int aAttributeId ) const
{
    CipAttributes::const_iterator  it;

    const CipAttributes& list = attributes[aCI];

    // a binary search thru the vector of pointers looking for aAttributeId
    it = vec_search( list.begin(), list.end(), aAttributeId );

    if( it != list.end() )
        return *it;

    CIPSTER_TRACE_WARN( "attribute %d not defined\n", aAttributeId );

    return NULL;
}


//----<AttrubuteFuncs>-------------------------------------------------------

EipStatus CipClass::getInstanceCount( CipInstance* aInstance, CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    CipClass* clazz = attr->owning_class;

    // This func must be invoked only on a class attribute,
    // because on an instance attribute clazz will be NULL since
    // Instance() is not a CipClass and dynamic_cast<> returns NULL.
    if( clazz )
    {
        uint16_t instance_count = clazz->InstanceCount();

        BufWriter out = response->Writer();

        out.put16( instance_count );
        response->SetWrittenSize( 2 );

        return kEipStatusOkSend;
    }
    return kEipStatusError;
}


EipStatus CipClass::getLargestInstanceId( CipInstance* aInstance, CipAttribute* attr,
    CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    CipClass* clazz = attr->owning_class;

    // This func must be invoked only on a class attribute,
    // because on an instance attribute clazz will be NULL since
    // Instance() is not a CipClass and dynamic_cast<> returns NULL.
    if( clazz )
    {
        uint16_t largest_id = 0;

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


EipStatus CipClass::getLargestInstanceAttributeId( CipInstance* aInstance, CipAttribute* attr,
    CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    CipClass* clazz = attr->owning_class;

    if( clazz )
    {
        uint16_t largest_id = 0;

        if( clazz->AttributesI().size() )
        {
            // attributes are sorted by Id(), so last one is highest.
            largest_id = clazz->AttributesI().back()->Id();
        }

        response->Writer().put16( largest_id );
        response->SetWrittenSize( 2 );

        return kEipStatusOkSend;
    }
    return kEipStatusError;
}


EipStatus CipClass::getLargestClassAttributeId( CipInstance* aInstance, CipAttribute* attr,
    CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    CipClass* clazz = attr->owning_class;

    if( clazz )
    {
        uint16_t largest_id = 0;

        if( clazz->AttributesC().size() )
        {
            // attributes are sorted by Id(), so last one is highest.
            largest_id = clazz->AttributesC().back()->Id();
        }

        response->Writer().put16( largest_id );
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

    return attribute->Get( instance, request, response );
}


EipStatus CipClass::GetAttributeAll( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response )
{
    BufWriter start = response->Writer();

    // Implement GetAttributeAll() by calling GetAttributeSingle() in a loop.
    CipService* service = instance->Service( kGetAttributeSingle );

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

    const CipAttributes& attributes = instance->Attributes();

    if( !attributes.size() )
    {
        // there are no attributes to be sent back
        response->SetGenStatus( kCipErrorServiceNotSupported );
    }
    else
    {
        int get_mask = instance->Id() ?
                        instance->owning_class->inst_getable_all_mask :
                        instance->owning_class->clss_getable_all_mask;

        for( CipAttributes::const_iterator it = attributes.begin();
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

    return attribute->Set( instance, request, response );
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
