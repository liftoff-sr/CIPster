/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corportion.
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

    typedef std::vector<CipInstance*>      CipInstances;
    typedef std::vector<CipService*>       CipServices;

    /**
     * Constructor CipClass
     * is a base CIP Class and contains services and class (not instance) attributes.
     * The class attributes are held in the CipInstance from which this C++ class
     * is derived.
     *
     * @param aClassId ID of the class
     * @param aClassName name of class
     *
     * @param aClassAttributesMask is a bit map of desired common class attributes
     *  in this class.
     *
     * @param a_get_attribute_all_mask mask of which attribute Ids are included in the
     *  class getAttributeAll. If the mask is 0 the getAttributeAll service will not
     *  be added as class service.
     *
     * @param a_instance_attributes_get_attributes_all_mask  mask of which attributes
     *  are included in the instance getAttributeAll. If the mask is 0 the getAttributeAll
     *  service will not be added as class service.
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

    /// Return true if this is a meta-class, false if public.
    bool IsMetaClass() const    { return !owning_class; }

    /**
     * Function ServiceInsert
     * inserts a service and returns true if succes, else false.
     *
     * @param aService is to be inserted and must not already be part of a CipClass.
     *  It must by dynamically allocated, not compiled in, because this container
     *  takes ownership of aService.
     *
     * @return bool - true on success, else false.  Since services may be overridden,
     *  currently this will not fail if the service_id already exists.  It will merely
     *  delete the existing one with the same service_id.
     */
    bool ServiceInsert( CipService* aService );

    CipService* ServiceInsert( int aServiceId,
        CipServiceFunction aServiceFunction, const char* aServiceName );

    /**
     * Function ServiceRemove
     * removes a service given by @a aServiceId and returns ownership to caller
     * if it exists, else NULL.  Caller may delete it, and typically should.
     */
    CipService* ServiceRemove( int aServiceId );

    /// Get an existing CipService or return NULL if not found.
    CipService* Service( int aServiceId ) const;

    /// Return a read only collection of services
    const CipServices& Services() const
    {
        return services;
    }

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
        Cpf* cpfd, ConnectionManagerStatusCode* extended_error_code );

protected:

    //-----<AttributeFuncs>-----------------------------------------------------
    static EipStatus getInstanceCount( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response );

    static EipStatus getLargestInstanceId( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response );

    static EipStatus getLargestInstanceAttributeId( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response );

    static EipStatus getLargestClassAttributeId( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response );
    //-----</AttributeFuncs>----------------------------------------------------

    int         revision;                   ///< class revision
    int         class_id;                   ///< class ID
    std::string class_name;                 ///< class name

    /**

        Constructor for the meta-class, and only called by public constructor
        above. The constructor above constructs the "public" CIP class. This one
        constructs the meta-class. The meta-class is "owned" by the public class,
        i.e. ownership means "is responsible for deleting it".

        A metaClass is a class that holds the attributes and services of the
        single public class object. CIP can talk to an instance, therefore an
        instance has a pointer to its class. CIP can talk to a class, therefore
        CipClass is a subclass of CipInstance, and this base C++ class contains
        a pointer to a CipClass used as the meta-class. CIP never explicitly
        addresses a meta-class.

    */

    CipClass(
            int         aClassId,           ///< should be same as public class
            const char* aClassName          ///< without "meta-" prefix
            );

    CipInstances    instances;              ///< collection of instances
    CipServices     services;               ///< collection of services

    void ShowServices()
    {
        for( CipServices::const_iterator it = services.begin();
            it != services.end();  ++it )
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
    CipClass( CipClass& );                  // private because not implemented
};

#endif // CIPCLASS_H_
