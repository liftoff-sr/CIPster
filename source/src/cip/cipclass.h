/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#ifndef CIPCLASS_H_
#define CIPCLASS_H_

#include "cipinstance.h"
#include "cipservice.h"

class ConnectionData;


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


    CipAttribute* AttributeInsert( _CI aCI,
        int             aAttributeId,
        CipDataType     aCipType,
        void*           aCookie,
        bool            isGetableSingle = true,
        bool            isGetableAll = true,
        bool            isSetableSingle = false
        );
    CipAttribute* AttributeInsert( _CI aCI,
        int             aAttributeId,
        CipDataType     aCipType,
        // use non-pointer aCookie to indicate CipInstance offset:
        uint16_t        aCookie,
        bool            isGetableSingle = true,
        bool            isGetableAll = true,
        bool            isSetableSingle = false
        );

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

    int             revision;               ///< class revision
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
    CipClass( CipClass& );                      // private because not implemented
    CipClass& operator=( const CipClass& );
};

#endif // CIPCLASS_H_
