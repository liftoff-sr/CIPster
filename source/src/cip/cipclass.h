/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#ifndef CIPCLASS_H_
#define CIPCLASS_H_

#include "cipinstance.h"
#include "cipservice.h"

#ifndef CIPSTER_DEPRECATED
 #if defined(__GNUC__) || defined(__clang__)
  #define CIPSTER_DEPRECATED(msg) __attribute__((deprecated(msg)))
 #else
  #define CIPSTER_DEPRECATED(msg)
 #endif
#endif

class ConnectionData;


/**
 * Function memberOffset
 * returns the byte offset of a data member within its (single-inheritance)
 * CipInstance-derived class, computed from a pointer-to-member.  This is the
 * type-safe replacement for the memb_offs() macro, used by the typed
 * AttributeInsert<Type>() inserters.  It carries the same assumption memb_offs()
 * always relied on: CipInstance is the first, non-virtual base subobject.
 */
template<typename I, typename M>
inline uint16_t memberOffset( M I::* aMember )
{
    return uint16_t(
        reinterpret_cast<uintptr_t>( &(reinterpret_cast<I*>(1)->*aMember) ) - 1 );
}


/**
 * Class CipClass
 * implements the CIP class spec.
 */
class CipClass : public CipInstance
{
public:

    /**
     * Constructor CipClass
     * is a base CIP Class and contains services and class (not instance) attributes.
     * The class attributes are held in the CipInstance from which this C++ class
     * is derived.
     *
     * @param aClassId the published class ID
     * @param aClassName name of class
     *
     * @param aClassAttributesMask is a bit map of desired common class attributes
     *  in this class.
     *
     * @param aRevision class revision
     */
    CipClass(
        int         aClassId,
        const char* aClassName,
        int         aClassAttributesMask,
        int         aRevision = 1
        );

    virtual ~CipClass();


    //-----<Class Registry Support>---------------------------------------------

    /**
     * Delete all instances and classes in the CIP stack
     */
    static void DeleteAll();

    /**
     * Function Register
     * registers  @a aClass in the stack.
     * @param aClass CIP class to be registered
     * @return kEipStatusOk on success
     */
    static EipStatus Register( CipClass* aClass );

    static CipClass* Get( int aClassId );

    //-----</Class Registry Support>--------------------------------------------


    //-----<CipServiceFunctions>------------------------------------------------

    /**
     * Function GetAttributeSingle
     * is a CipService that provides an attribute fetch service.
     *
     * @param instance which instance to act on.
     * @param request
     * @param response
     *
     * @return status  >0 .. success
     *          -1 .. requested attribute not available
     */
    static EipStatus GetAttributeSingle( CipInstance* instance,
            CipMessageRouterRequest* request,
            CipMessageRouterResponse* response );

    static EipStatus SetAttributeSingle( CipInstance* instance,
            CipMessageRouterRequest* request,
            CipMessageRouterResponse* response );

    /**
     * Function GetAttributeAll
     * is a CipService function that gets several attributes at once.
     *
     * Copy all attributes from Object into the global message buffer.
     * @param instance pointer to object instance with data.
     * @param request pointer to MR request.
     * @param response pointer for MR response.
     * @return status >0 .. success
     *                -1 .. no reply to send
     */
    static EipStatus GetAttributeAll( CipInstance* instance,
            CipMessageRouterRequest* request,
            CipMessageRouterResponse* response );

    /**
     * Function Reset (ServiceId = kReset )
     * is a common service which is a dummy place holder so the proper
     * error code of kCipErrorInvalidParameter can be returned rather
     * than kCipErrorServiceNotSupported. Individual classes are expected
     * to override this base class behaviour.
     */
    static EipStatus Reset( CipInstance* instance,
            CipMessageRouterRequest* request,
            CipMessageRouterResponse* response );

    //-----</CipServiceFunctions>-----------------------------------------------

    /**
     * Function ServiceInsert
     * inserts an instance service and returns true if succes, else false.
     *
     * @param aService is to be inserted and must not already be part of a CipClass.
     *  It must by dynamically allocated, not compiled in, because this container
     *  takes ownership of aService.
     *
     * @return bool - true on success, else false.  Since services may be overridden,
     *  currently this will not fail if the service_id already exists.  It will merely
     *  delete the existing one with the same service_id.
     */
    bool ServiceInsert( _CI aCI, CipService* aService );

    CipService* ServiceInsert( _CI aCI, int aServiceId,
        CipServiceFunction aServiceFunction, const char* aServiceName );

    /**
     * Function ServiceRemove
     * removes an instance service given by @a aServiceId and returns ownership to caller
     * if it exists, else NULL.  Caller may delete it, and typically should.
     */
    CipService* ServiceRemove( _CI aCI, int aServiceId );

    /// Get an existing CipService or return NULL if not found.
    CipService* ServiceI( int aServiceId ) const
    {
        return Service( _I, aServiceId );
    }

    /// Get an existing CipService or return NULL if not found.
    CipService* ServiceC( int aServiceId ) const
    {
        return Service( _C, aServiceId );
    }

    CipService* Service( _CI aCI, int aServiceId ) const;

    /// Return a read only collection of instance services
    const CipServices& ServicesI() const    { return services[_I]; }

    /// Return a read only collection of class services
    const CipServices& ServicesC() const    { return services[_C]; }


    /**
     * Function InstanceInsert
     * inserts an instance and returns true if succes, else false.
     *
     * @param aInstance is a new instance, and since the normal CipInstance
     *  constructor marks any new instance as not yet belonging to a class, this
     *  instance may not belong to any class at the time this call is made.  After
     *  this call this container owns aInstance (=> will delete it upon destruction)
     *  so therefore aInstance must be dynamically (heap) allocated, not compiled in.
     *
     * @return bool - true on succes, else false.  Failure happens when the instance
     *  was marked as already being in another class, or if the instance id was
     *  not unique.  On success, ownership is passed to this class as a container.
     *  On failure, ownership remains with the caller.
     */
    bool InstanceInsert( CipInstance* aInstance );

    /**
     * Function InstanceRemove
     * removes an instance and returns it if success, else NULL.
     *
     * @param aInstanceId is the instance to remove.
     *
     * @return CipInstance* - removed instance, caller normally should delete it,
     *  NULL if not found.
     */
    CipInstance* InstanceRemove( int aInstanceId );

    CipInstance* Instance( int aInstanceId ) const;

    unsigned InstanceCount() const  { return instances.size(); }

    /// Return an iterator for aInstanceId it if exists, else for the next greater
    /// instance id, else Instances().end();
    CipInstances::const_iterator InstanceNext( int aInstanceId ) const;

    /// Return a read only collection of CipInstances.
    const CipInstances& Instances() const   { return instances; }

    const std::string& ClassName() const    { return class_name; }

    int ClassId() const                     { return class_id; }

    CipAttribute* AttributeI( int aAttributeId ) const
    {
        return Attribute( _I, aAttributeId );
    }
    CipAttribute* AttributeC( int aAttributeId ) const
    {
        return Attribute( _C, aAttributeId );
    }
    CipAttribute* Attribute( _CI aCI, int aAttributeId ) const;


    const CipAttributes& AttributesI() const    { return attributes[_I]; }
    const CipAttributes& AttributesC() const    { return attributes[_C]; }


    /**
     * Functions AttributeInsert
     * insert either an instance or class attribute and returns a pointer to
     * it if succes, else NULL.
     *
     * @param aCI is either _I or _C indicating "instance" or "class" respectively.
     *
     * @param aCookie is saved in the data member of the Attribute and will
     *  later be passed to either AttributeFunc provided.  It can point to anything
     *  convenient.
     * @param isCookieAnInstanceOffset should be set to true if aCookie is a
     *   data member of a CipInstance derivative

     * @return CipAttribute* - dynamically allocated by this function,
     * or NULL if failure. Currently attributes may be overrridden, so any
     * existing CipAttribute in the target container with the same attribute id
     * will be deleted in favour of this one.
     */

    CipAttribute* AttributeInsert( _CI aCI,
        int             aAttributeId,
        AttributeFunc   aGetter,
        bool            isGetableAll = true,
        AttributeFunc   aSetter = NULL,
        uintptr_t       aCookie = 0,
        bool            isCookieAnInstanceOffset = true,
        CipDataType     aCipType = kCipAny
        );


    // DEPRECATED: these two bind an arbitrary void*/offset cookie to an arbitrary
    // CipDataType with no type checking, which allowed the reported attribute-aliasing
    // memory-corruption defect.  Prefer a typed AttributeInsert<Type>() inserter (below).
    // They are retained (and still correct) for scalar types as a migration aid, but they
    // refuse kCipByteArray / kCipByteArrayLength outright, since the backing type is now
    // CipByteArray (a void* ByteBuf would compile-but-corrupt) -- use AttributeInsertByteArray.
    CipAttribute* AttributeInsert( _CI aCI,
        int             aAttributeId,
        CipDataType     aCipType,
        void*           aCookie,
        bool            isGetableSingle = true,
        bool            isGetableAll = true,
        bool            isSetableSingle = false
        ) CIPSTER_DEPRECATED( "use a typed AttributeInsert<Type>() inserter" );
    CipAttribute* AttributeInsert( _CI aCI,
        int             aAttributeId,
        CipDataType     aCipType,
        // use non-pointer aCookie to indicate CipInstance offset:
        uint16_t        aCookie,
        bool            isGetableSingle = true,
        bool            isGetableAll = true,
        bool            isSetableSingle = false
        ) CIPSTER_DEPRECATED( "use a typed AttributeInsert<Type>() inserter" );

    /**
     * Function AttributeInsert
     * inserts an attribute and returns true if succes, else false.
     *
     * @param aCI is either _I or _C indicating "instance" or "class" respectively.
     *
     * @param aAttribute is the one to insert, and may not already be inserted
     *  elsewhere. It must be dynamically allocated, not compiled in,
     *  because this container takes ownership of aAttribute.
     *
     * @return bool - true if success, else false if failed.  Currently attributes
     *  may be overrridden, so any existing CipAttribute in this instance with the
     *  same attribute_id will be deleted in favour of this one.
     */
    bool AttributeInsert( _CI aCI, CipAttribute* aAttribute );

    //-----<Typed attribute inserters>------------------------------------------
    // Bind the C++ storage type to the CIP wire type at compile time, so a mismatch
    // (e.g. registering a CipByteArray as a kCipUdint, the reported aliasing defect)
    // cannot be expressed.  The wire type lives in the function-name suffix, which also
    // disambiguates the families where one C++ type maps to several CIP types
    // (uint32_t -> Udint/Dword, std::string -> String/ShortString/String2, ...).
    // Each name has two overloads: a pointer to static/global storage, and a
    // pointer-to-member for instance data (offset computed via memberOffset()).

#define CIP_INSERTER( Suffix, CppType, CipEnum )                                        \
    CipAttribute* AttributeInsert##Suffix( _CI aCI, int aId, CppType* aStorage,         \
            bool aGetable = true, bool aGetableAll = true, bool aSetable = false )       \
    { return attrInsertPtr( aCI, aId, CipEnum, (void*) aStorage,                         \
                            aGetable, aGetableAll, aSetable ); }                         \
    template<typename I>                                                                \
    CipAttribute* AttributeInsert##Suffix( _CI aCI, int aId, CppType I::* aMember,       \
            bool aGetable = true, bool aGetableAll = true, bool aSetable = false )       \
    { return attrInsertOff( aCI, aId, CipEnum, memberOffset( aMember ),                  \
                            aGetable, aGetableAll, aSetable ); }

    CIP_INSERTER( Bool,  CipBool,  kCipBool  )
    CIP_INSERTER( Sint,  int8_t,   kCipSint  )
    CIP_INSERTER( Usint, CipUsint, kCipUsint )
    CIP_INSERTER( Byte,  uint8_t,  kCipByte  )
    CIP_INSERTER( Int,   CipInt,   kCipInt   )
    CIP_INSERTER( Uint,  CipUint,  kCipUint  )
    CIP_INSERTER( Word,  CipWord,  kCipWord  )
    CIP_INSERTER( Dint,  int32_t,  kCipDint  )
    CIP_INSERTER( Udint, CipUdint, kCipUdint )
    CIP_INSERTER( Dword, CipDword, kCipDword )
    CIP_INSERTER( Real,  float,    kCipReal  )
    CIP_INSERTER( Lint,  int64_t,  kCipLint  )
    CIP_INSERTER( Ulint, uint64_t, kCipUlint )
    CIP_INSERTER( Lword, uint64_t, kCipLword )
    CIP_INSERTER( Lreal, double,   kCipLreal )
    CIP_INSERTER( Revision,        CipRevision,  kCipUsintUsint  )
    CIP_INSERTER( ShortString,     std::string,  kCipShortString )
    CIP_INSERTER( String,          std::string,  kCipString      )
    CIP_INSERTER( String2,         std::string,  kCipString2     )
    CIP_INSERTER( ByteArray,       CipByteArray, kCipByteArray   )
    CIP_INSERTER( ByteArrayLength, CipByteArray, kCipByteArrayLength )

#undef CIP_INSERTER

    /// kCip6Usint (e.g. a MAC address) backed by a 6-byte instance array member.
    template<typename I>
    CipAttribute* AttributeInsert6Usint( _CI aCI, int aId, uint8_t (I::*aMember)[6],
            bool aGetable = true, bool aGetableAll = true, bool aSetable = false )
    { return attrInsertOff( aCI, aId, kCip6Usint, memberOffset( aMember ),
                            aGetable, aGetableAll, aSetable ); }
    //-----</Typed attribute inserters>-----------------------------------------

    /**
     * Function FindUniqueFreeId
     * returns the first unused instance Id.
     */
    int FindUniqueFreeId() const;

    /**
     * Function OpenConnection
     * should be overridden in derived classes which DO handle open connections.
     * These include "Message Router" and "Assembly" classes at this time, but
     * user defined CipClasses can also override this function.
     *
     * @param aParams Holds connection service parameters that identify how to
     *   construction the connection.
     *
     * @param extended_error_code The returned error code of the connection object
     * @return CIPError
     */
    virtual     CipError OpenConnection( ConnectionData* aConn,
        Cpf* cpfd, ConnMgrStatus* extended_error_code );

protected:

    //-----<AttributeFuncs>-----------------------------------------------------
    static EipStatus getInstanceCount( CipInstance* aInstance, CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response );

    static EipStatus getLargestInstanceId( CipInstance* aInstance, CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response );

    static EipStatus getLargestInstanceAttributeId( CipInstance* aInstance, CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response );

    static EipStatus getLargestClassAttributeId( CipInstance* aInstance, CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response );
    //-----</AttributeFuncs>----------------------------------------------------

    uint16_t        revision;               ///< class revision (CIP UINT)
    int             class_id;               ///< class ID
    std::string     class_name;             ///< class name

    CipServices     services[2];            ///< collection of services

    CipAttributes   attributes[2];          ///< sorted pointer array to CipAttribute

    int             inst_getable_all_mask;
    int             clss_getable_all_mask;

    CipInstances    instances;              ///< collection of instances

    void ShowServicesI()
    {
        for( CipServices::const_iterator it = services[_I].begin();
            it != services[_I].end();  ++it )
        {
            CIPSTER_TRACE_INFO( "id:%d %s\n",
                (*it)->Id(),
                (*it)->ServiceName().c_str() );
        }
    }

    void ShowInstances()
    {
        for( CipInstances::const_iterator it = instances.begin();
            it != instances.end();  ++it )
        {
            CIPSTER_TRACE_INFO( "id:%d\n", (*it)->Id() );
        }
    }

private:
    // Non-deprecated internal workers shared by the typed inserters and the deprecated
    // generic overloads.  attrInsertPtr binds an absolute pointer (pointer mode);
    // attrInsertOff binds an instance-relative offset (offset mode).
    CipAttribute* attrInsertPtr( _CI aCI, int aId, CipDataType aType, void* aPtr,
        bool aGetable, bool aGetableAll, bool aSetable );
    CipAttribute* attrInsertOff( _CI aCI, int aId, CipDataType aType, uint16_t aOffset,
        bool aGetable, bool aGetableAll, bool aSetable );

    CipClass( CipClass& );                      // private because not implemented
    CipClass& operator=( const CipClass& );
};

#endif // CIPCLASS_H_
