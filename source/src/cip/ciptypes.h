/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/
#ifndef CIPSTER_CIPTYPES_H_
#define CIPSTER_CIPTYPES_H_


#include <string>
#include <vector>
#include <string.h>

#include "typedefs.h"
#include "trace.h"
#include "cipster_user_conf.h"
#include "ciperror.h"
#include "byte_bufs.h"

//* @brief Assembly Class Code
enum ClassIds
{
    kIdentityClassCode = 0x01,
    kCipMessageRouterClassCode = 0x02,
    kCipAssemblyClassCode = 0x04,
    kConnectionClassId = 0x05,
    kCipConnectionManagerClassCode = 0x06,
    kCipTcpIpInterfaceClassCode = 0xF5,
    kCipEthernetLinkClassCode = 0xF6,
};


/**
 * Enum SegmentType
 * is the set of bits 7-5 in the Segment Type/Format byte
 */
enum SegmentType
{
    // Segments
    kSegmentTypePort = 0x00,                    ///< Port segment
    kSegmentTypeLogical  = 0x20,                ///< Logical segment
    kSegmentTypeNetwork  = 0x40,                ///< Network segment
    kSegmentTypeSymbolic = 0x60,                ///< Symbolic segment
    kSegmentTypeData = 0x80,                    ///< Data segment
    kSegmentTypeDataTypeConstructed = 0xA0,     ///< Data type constructed
    kSegmentTypeDataTypeElementary  = 0xC0,     ///< Data type elementary
    kSegmentTypeSegmentTypeReserved = 0xE0
};

//* @brief Connection Manager Error codes
enum ConnectionManagerStatusCode
{
    kConnectionManagerStatusCodeSuccess = 0x00,
    kConnectionManagerStatusCodeErrorConnectionInUse = 0x0100,
    kConnectionManagerStatusCodeErrorTransportTriggerNotSupported = 0x0103,
    kConnectionManagerStatusCodeErrorOwnershipConflict = 0x0106,
    kConnectionManagerStatusCodeErrorConnectionNotFoundAtTargetApplication = 0x0107,
    kConnectionManagerStatusCodeErrorInvalidOToTConnectionType  = 0x123,
    kConnectionManagerStatusCodeErrorInvalidTToOConnectionType  = 0x124,
    kConnectionManagerStatusCodeErrorInvalidOToTConnectionSize  = 0x127,
    kConnectionManagerStatusCodeErrorInvalidTToOConnectionSize  = 0x128,
    kConnectionManagerStatusCodeErrorNoMoreConnectionsAvailable = 0x0113,
    kConnectionManagerStatusCodeErrorVendorIdOrProductcodeError = 0x0114,
    kConnectionManagerStatusCodeErrorDeviceTypeError    = 0x0115,
    kConnectionManagerStatusCodeErrorRevisionMismatch   = 0x0116,
    kConnectionManagerStatusCodeErrorPITGreaterThanRPI  = 0x011b,
    kConnectionManagerStatusCodeInvalidConfigurationApplicationPath = 0x0129,
    kConnectionManagerStatusCodeInvalidConsumingApllicationPath     = 0x012A,
    kConnectionManagerStatusCodeInvalidProducingApplicationPath     = 0x012B,
    kConnectionManagerStatusCodeInconsistentApplicationPathCombo    = 0x012F,
    kConnectionManagerStatusCodeNonListenOnlyConnectionNotOpened    = 0x0119,
    kConnectionManagerStatusCodeErrorParameterErrorInUnconnectedSendService = 0x0205,
    kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath   = 0x0315,
    kConnectionManagerStatusCodeTargetObjectOutOfConnections    = 0x011A
};


/**
 * Enum CipDataType
 * is the set of encoded CIP data types for CIP Messages
 */
enum CipDataType
{
    kCipAny = 0x00,         ///< data type that can not be directly encoded
    kCipBool = 0xC1,        ///< boolean data type
    kCipSint = 0xC2,        ///< 8-bit signed integer
    kCipInt = 0xC3,         ///< 16-bit signed integer
    kCipDint = 0xC4,        ///< 32-bit signed integer
    kCipLint = 0xC5,        ///< 64-bit signed integer
    kCipUsint = 0xC6,       ///< 8-bit unsignedeger
    kCipUint = 0xC7,        ///< 16-bit unsigned
    kCipUdint = 0xC8,       ///< 32-bit unsigned
    kCipUlint = 0xC9,       ///< 64-bit unsigned
    kCipReal = 0xCA,        ///< Single precision floating point
    kCipLreal = 0xCB,       ///< Double precision floating point
    kCipStime = 0xCC,       ///< Synchronous time information*, type of DINT
    kCipDate = 0xCD,        ///< Date only
    kCipTimeOfDay = 0xCE,   ///< Time of day
    kCipDateAndTime = 0xCF, ///< Date and time of day
    kCipString  = 0xD0,     ///< Character string, 1 byte per character
    kCipByte    = 0xD1,     ///< 8-bit bit string
    kCipWord    = 0xD2,     ///< 16-bit bit string
    kCipDword   = 0xD3,     ///< 32-bit bit string
    kCipLword   = 0xD4,     ///< 64-bit bit string
    kCipString2 = 0xD5,     ///< Character string, 2 byte per character
    kCipFtime   = 0xD6,     ///< Duration in micro-seconds, high resolution; range of DINT
    kCipLtime   = 0xD7,     ///< Duration in micro-seconds, high resolution, range of LINT
    kCipItime   = 0xD8,     ///< Duration in milli-seconds, short; range of INT
    kCipStringN = 0xD9,     ///< Character string, N byte per character
    kCipShortString = 0xDA, /**< Character string, 1 byte per character, 1 byte
                             *  length indicator */
    kCipTime = 0xDB,        ///< Duration in milli-seconds; range of DINT
//    kCipEpath = 0xDC,       ///< CIP path segments
    kCipEngUnit = 0xDD,     ///< Engineering Units
    kCipStringI = 0xDE,     ///< International Character String

    // definition of some CIP structs
    // need to be validated in IEC 61131-3 subclause 2.3.3
    // TODO: Check these codes
    kCipUsintUsint = 0xA0,                      ///< Used for CIP Identity attribute 4 Revision
    kCipUdintUdintUdintUdintUdintString = 0xA1, /**< TCP/IP attribute 5 - IP address, subnet mask, gateway, IP name
                                                 *  server 1, IP name server 2, domain name*/
    kCip6Usint = 0xA2,                          ///< Struct for MAC Address (six USINTs)
    kCipMemberList  = 0xA3,                     ///<
    kCipByteArray   = 0xA4,                     ///<
};


/**
 * Enum CIPServiceCode
 * is the set of CIP service codes.
 * Common services codes range from 0x01 to 0x1c.  Beyond that there can
 * be class or instance specific service codes and some may overlap.
 */
enum CIPServiceCode
{
    kGetAttributeAll = 0x01,
    kSetAttributeAll = 0x02,
    kGetAttributeList = 0x03,
    kSetAttributeList = 0x04,
    kReset  = 0x05,
    kStart  = 0x06,
    kStop   = 0x07,
    kCreate = 0x08,
    kDelete = 0x09,
    kMultipleServicePacket = 0x0A,
    kApplyAttributes = 0x0D,
    kGetAttributeSingle = 0x0E,
    kSetAttributeSingle = 0x10,
    kFindNextObjectInstance = 0x11,
    kRestore = 0x15,
    kSave = 0x16,
    kNoOperation    = 0x17,
    kGetMember  = 0x18,
    kSetMember  = 0x19,
    kInsertMember = 0x1A,
    kRemoveMember = 0x1B,
    kGroupSync = 0x1C,

    // Start CIP class or instance specific services, probably should go in class specific area
    kForwardClose = 0x4E,
    kUnconnectedSend = 0x52,
    kForwardOpen = 0x54,
    kLargeForwardOpen = 0x5b,
    kGetConnectionOwner = 0x5A
    // End CIP class or instance specific services
};

//* @brief Definition of Get and Set Flags for CIP Attributes
enum CIPAttributeFlag           // TODO: Rework
{
    kNotSetOrGetable = 0x00,    ///< Neither set-able nor get-able
    kGetableAll = 0x01,         ///< Get-able, also part of Get Attribute All service
    kGetableSingle = 0x02,      ///< Get-able via Get Attribute
    kSetable = 0x04,            ///< Set-able via Set Attribute
    // combined for convenience
    kSetAndGetAble = 0x07,      ///< both set and get-able
    kGetableSingleAndAll = 0x03 ///< both single and all
};


enum IoConnectionEvent
{
    kIoConnectionEventOpened,
    kIoConnectionEventTimedOut,
    kIoConnectionEventClosed
};


struct CipByteArray
{
    EipUint16   length;     ///< Length of the Byte Array
    EipByte*    data;       ///< Pointer to the data
};


/** @brief CIP Short String
 *
 */
struct CipShortString
{
    EipUint8    length;     ///< Length of the String (8 bit value)
    EipByte*    string;     ///< Pointer to the string data
};


/** @brief CIP String
 *
 */
struct CipString
{
    EipUint16   length;     ///< Length of the String (16 bit value)
    EipByte*    string;     ///< Pointer to the string data
};


struct CipRevision
{
    EipUint8    major_revision;
    EipUint8    minor_revision;
};


class CipInstance;
class CipAttribute;
class CipClass;
class CipMessageRouterRequest;
class CipMessageRouterResponse;
class CipConn;


/** @ingroup CIP_API
 * @typedef  EIP_STATUS (*AttributeFunc)( CipAttribute *,
 *    CipMessageRouterRequest*, CipMessageRouterResponse*)
 *
 * @brief Signature definition for the implementation of CIP services.
 *
 * @param aAttribute
 *
 * @param aRequest request data
 *
 * @param aResponse storage for the response data, including a buffer for
 *      extended data.  Do not advance aResponse->data BufWriter, but rather only set
 *      aRequest->data_length to the number of bytes written to aReponse->data.
 *
 * @return EIP_OK_SEND if service could be executed successfully and a response
 *  should be sent
 */
typedef EipStatus (*AttributeFunc)( CipAttribute* aAttribute,
            CipMessageRouterRequest* aRequest,
            CipMessageRouterResponse* aResponse );

// Standard attribute getter functions, and you may add your own elsewhere also:
EipStatus   GetAttrData( CipAttribute* attr, CipMessageRouterRequest* request,
                CipMessageRouterResponse* response );

// Standard attribute setter functions, and you may add your own elsewhere also:
EipStatus   SetAttrData( CipAttribute* attr, CipMessageRouterRequest* request,
                CipMessageRouterResponse* response );


/**
 * Class CipAttribute
 * holds info for a CIP attribute which may be contained by a #CipInstance
 */
class CipAttribute
{
    friend class CipInstance;

public:
    CipAttribute(
            int         aAttributeId,
            EipUint8    aType,
            EipUint8    aFlags,

            // assign your getter and setter per attribute, either may be NULL
            AttributeFunc aGetter,
            AttributeFunc aSetter,

            void*   aData
            );

    virtual ~CipAttribute();

    int     Id() const                  { return attribute_id; }

    int         attribute_id;
    EipUint8    type;
    EipUint8    attribute_flags;         /**<   0 => getable_all,
                                                1 => getable_single;
                                                2 => setable_single;
                                                3 => get and setable;
                                                all other values reserved
                                         */

    void*       data;                   // no ownership of data

    CipInstance* Instance() const       { return owning_instance; }

    /**
     * Function Get
     * is an abstract function that calls the getter that was passed in to the constructor
     * or a standard fallback behaviour.
     */
    EipStatus Get( CipMessageRouterRequest* request, CipMessageRouterResponse* response )
    {
        if( getter )    // getter could be null, otherwise was established in attribute constructor
            return getter( this, request, response );   // this is an override of standard GetAttrData()
        else
            return GetAttrData( this, request, response );
    }

    /**
     * Function Set
     * is an abstract function that calls the setter that was passed into the constructor
     * or a standard fallback behaviour.
     */
    EipStatus Set( CipMessageRouterRequest* request, CipMessageRouterResponse* response )
    {
        if( setter )    // setter could be null, otherwise was established in attribute constructor
            return setter( this, request, response );   // an override of standard SetAttrData()
        else
            return SetAttrData( this, request, response );
    }

protected:

    CipInstance*    owning_instance;

    /**
     * Function Pointer getter
     * may be fixed during construction to a custom getter function.
     */
    const AttributeFunc   getter;

    /**
     * Function Pointer setter
     * may be fixed during construction to a custom setter function.
     */
    const AttributeFunc   setter;
};


/**
 * Class CipInstance
 * holds CIP intance info and instances may be contained within a #CipClass.
 */
class CipInstance
{
public:
    typedef std::vector<CipAttribute*>      CipAttributes;

    CipInstance( int aInstanceId );

    virtual ~CipInstance();

    int   Id() const            { return instance_id; }

    /**
     * Function AttributeInsert
     * inserts an attribute and returns true if succes, else false.
     *
     * @param aAttribute is the one to insert, and may not already be a member
     *  of another instance.  It must be dynamically allocated, not compiled in,
     *  because this container takes ownership of aAttribute.
     *
     * @return bool - true if success, else false if failed.  Currently attributes
     *  may be overrridden, so any existing CipAttribute in this instance with the
     *  same attribute_id will be deleted in favour of this one.
     */
    bool AttributeInsert( CipAttribute* aAttributes );

    CipAttribute* AttributeInsert( int aAttributeId,
        EipUint8        cip_type,
        EipByte         cip_flags,
        void*           data
        );

    CipAttribute* AttributeInsert( int aAttributeId,
        EipUint8        cip_type,
        EipByte         cip_flags,
        AttributeFunc   aGetter,
        AttributeFunc   aSetter,
        void*           data = NULL
        );

    /**
     * Function Attribute
     * returns a CipAttribute or NULL if not found.
     */
    CipAttribute* Attribute( int aAttributeId ) const;

    const CipAttributes& Attributes() const
    {
        return attributes;
    }

    int             instance_id;    ///< this instance's number (unique within the class)
    CipClass*       owning_class;   ///< class the instance belongs to or NULL if none.

    int             highest_inst_attr_id;    ///< highest attribute_id for this instance

protected:
    CipAttributes       attributes;     ///< sorted pointer array to CipAttribute, unique to this instance

    void ShowAttributes()
    {
        for( CipAttributes::const_iterator it = attributes.begin();
            it != attributes.end();  ++it )
        {
            CIPSTER_TRACE_INFO( "id:%d\n", (*it)->Id() );
        }
    }

private:
    CipInstance( CipInstance& );                    // private because not implemented
    CipInstance& operator=( const CipInstance& );   // private because not implemented
};


/**
 * Typedef EipStatus (*CipServiceFunc)( CipInstance *,
 *    CipMessageRouterRequest*, CipMessageRouterResponse*)
 * is the function type for the implementation of CIP services.
 *
 * CIP services have to follow this signature in order to be handled correctly
 * by the stack.
 *
 * @param aInstance which was referenced in the service request
 * @param aRequest holds "data" coming from client, and it includes a length.
 * @param aResponse where to put the response, do it into member "data" which is length
 *  defined.  Upon completions update data_length to how many bytes were filled in.
 *
 * @return EipStatus - EipOKSend if service could be executed successfully
 *    and a response should be sent.
 */
typedef EipStatus (*CipServiceFunction)( CipInstance* aInstance,
        CipMessageRouterRequest* aRequest, CipMessageRouterResponse* aResponse );


/**
 * Class CipService
 * holds info for a CIP service and services may be contained within a CipClass.
 */
class CipService
{
public:
    CipService( const char* aServiceName = "", int aServiceId = 0,
            CipServiceFunction aServiceFunction = 0 ) :
        service_name( aServiceName ),
        service_id( aServiceId ),
        service_function( aServiceFunction )
    {
    }

    virtual ~CipService() {}

    int  Id() const                         { return service_id; }

    const std::string& ServiceName() const  { return service_name; }

    CipServiceFunction service_function;    ///< pointer to a function call

protected:
    std::string service_name;               ///< name of the service
    int         service_id;                 ///< service number
};

class CipCommonPacketFormatData;

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
     * @param aClassAttributesMask is a bit map of common class attributes desired
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
        EipUint32   aClassId,
        const char* aClassName,
        int         aClassAttributesMask,
        EipUint32   a_get_all_class_attributes_mask,
        EipUint32   a_get_all_instance_attributes_mask,
        EipUint16   aRevision = 1
        );

    virtual ~CipClass();

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

    /// Return an iterator for aInstanceId it if exists, else for the next greater
    /// instance id, else Instances().end();
    CipInstances::const_iterator InstanceNext( int aInstanceId ) const;

    /// Return a read only collection of CipInstances.
    const CipInstances& Instances() const   { return instances; }

    const std::string& ClassName() const    { return class_name; }

    int ClassId() const                     { return class_id; }

    int         revision;                   ///< class revision
    int         highest_attr_id;            /**< highest defined attribute number
                                             *  (attribute numbers are not necessarily
                                             *  consecutive)*/

    int         highest_inst_id;            ///< highest defined instance number, not necessarily consecutive

    EipUint32   get_attribute_all_mask;     /**< mask indicating which attributes are
                                              *  returned by getAttributeAll*/

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
     * @param aConn The connection object which is opening the connection
     *
     * @param extended_error_code The returned error code of the connection object
     * @return CIPError
     */
    virtual     CipError OpenConnection( CipConn* aConn, CipCommonPacketFormatData* cpfd, ConnectionManagerStatusCode* extended_error_code );

protected:

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
            const char* aClassName,         ///< without "meta-" prefix
            EipUint32   a_get_all_class_attributes_mask
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


/**
 * Struct CipTcpIpNetworkInterfaceConfiguration
 * is for holding TCP/IP interface information
 */
struct CipTcpIpNetworkInterfaceConfiguration
{
    CipUdint    ip_address;
    CipUdint    network_mask;
    CipUdint    gateway;
    CipUdint    name_server;
    CipUdint    name_server_2;
    CipString   domain_name;
};


// these are used for creating the getAttributeAll masks
// TODO there might be a way simplifying this using __VARARGS__ in #define
#define MASK1( a )          ( 1 << (a) )
#define MASK2( a, b )       ( 1 << (a) | 1 << (b) )
#define MASK3( a, b, c )    ( 1 << (a) | 1 << (b) | 1 << (c) )
#define MASK4( a, b, c, d ) ( 1 << (a) | 1 << (b) | 1 << (c) | 1 << (d) )
#define MASK5( a, b, c, d, e ) \
    ( 1 << (a) | 1 << (b) | 1 << (c) | 1 << (d) | 1 << (e) )

#define MASK6( a, b, c, d, e, f ) \
    ( 1 << (a) | 1 << (b) | 1 << (c) | 1 << (d) | 1 << (e) | 1 << (f) )

#define MASK7( a, b, c, d, e, f, g ) \
    ( 1 << (a) | 1 << (b) | 1 << (c) | 1 << (d) | 1 << (e) | 1 << (f) | 1 << (g) )

#define MASK8( a, b, c, d, e, f, g, h ) \
    ( 1 << (a) | 1 << (b) | 1 << (c) | 1 << (d) | 1 << (e) | 1 << (f) | \
      1 << (g) | 1 << (h) )

#define OR7( a, b, c, d, e, f, g ) \
    ( (a)<<6 | (b)<<5 | (c)<<4 | (d)<<3 | (e)<<2 || (f)<<1 || (g) )

#endif // CIPSTER_CIPTYPES_H_
