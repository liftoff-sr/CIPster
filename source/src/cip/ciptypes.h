/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 *
 * Conversion to C++ is Copyright (C) 2016, SoftPLC Corportion.
 *
 * All rights reserved.
 *
 ******************************************************************************/
#ifndef CIPSTER_CIPTYPES_H_
#define CIPSTER_CIPTYPES_H_


#include <string>
#include <vector>

#include "typedefs.h"
#include "trace.h"
#include "opener_user_conf.h"
#include "ciperror.h"


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


//* @brief Enum containing values which kind of logical segment is encoded
enum LogicalSegmentType
{
    kLogicalSegmentTypeClassId         = 0x00 + kSegmentTypeLogical,    ///< Class ID
    kLogicalSegmentTypeInstanceId      = 0x04 + kSegmentTypeLogical,    ///< Instance ID
    kLogicalSegmentTypeMemberId        = 0x08 + kSegmentTypeLogical,    ///< Member ID
    kLogicalSegmentTypeConnectionPoint = 0x0C + kSegmentTypeLogical,    ///< Connection Point
    kLogicalSegmentTypeAttributeId     = 0x10 + kSegmentTypeLogical,    ///< Attribute ID
    kLogicalSegmentTypeSpecial         = 0x14 + kSegmentTypeLogical,    ///< Special
    kLogicalSegmentTypeService         = 0x18 + kSegmentTypeLogical,    ///< Service ID
    kLogicalSegmentTypeExtendedLogical = 0x1C + kSegmentTypeLogical,    ///< Extended Logical
};


enum NetworkSegmentSubType
{
    kProductionTimeInhibitTimeNetworkSegment = 0x43 ///< production inhibit time network segment
};


enum DataSegmentType
{
    kDataSegmentTypeSimpleDataMessage = kSegmentTypeData + 0x00,
    kDataSegmentTypeAnsiExtendedSymbolMessage = kSegmentTypeData + 0x11
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
    kCipUint = 0xC7,        ///< 16-bit unsignedeger
    kCipUdint = 0xC8,       ///< 32-bit unsignedeger
    kCipUlint = 0xC9,       ///< 64-bit unsignedeger
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
    kCipEpath = 0xDC,       ///< CIP path segments
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
    kInternalUint6  = 0xF0                      /**< bogus hack, for port class attribute 9, TODO
                                                 *  figure out the right way to handle it */
};


/**
 * Enum CIPServiceCode
 * is the set of all CIP service codes.
 * Common services codes range from 0x01 to 0x1C.
 */
enum CIPServiceCode
{
    // Start CIP common services
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
    // End CIP common services

    // Start CIP object-specific services
    kForwardOpen = 0x54,
    kLargeForwardOpen = 0x5b,
    kForwardClose = 0x4E,
    kUnconnectedSend = 0x52,
    kGetConnectionOwner = 0x5A
    // End CIP object-specific services
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


#if 0
typedef std::basic_string<EipByte>    ByteArrayBase;

/** @brief CIP Byte Array
 *
 */
class CipByteArray : public ByteArrayBase
{
    CipByteArray( EipUint16 aLength = 0, EipByte* aData = 0 ) :
        ByteArrayBase( aData, aLength )
    {
    }

    EipUint16       Length() const  { return size(); }
    const EipByte*  Data() const    { return data(); }

    // inherit all member funcs of ByteArrayBase.
};

#else

struct CipByteArray
{
    EipUint16   length;     ///< Length of the Byte Array
    EipByte*    data;       ///< Pointer to the data
};

#endif


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


/**
 * Struct CipEPath
 */
struct CipEpath
{
    EipUint8    path_size;          ///< count of 16-bit words in serialized path
    EipUint32   class_id;           ///< Class ID of the linked object
    EipUint32   instance_number;    ///< Requested Instance Number of the linked object
    EipUint32   attribute_number;   ///< Requested Attribute Number of the linked object
    EipUint32   connection_point;

    void Clear()
    {
        path_size = 0;
        class_id = 0;
        instance_number = 0;
        attribute_number = 0;
        connection_point = 0;
    }
};


struct CipPortSegment
{
    int port;                       ///< if == -1, means not used
    std::vector<EipUint8>   link_address;

    CipPortSegment() :
        port(-1)
    {}
};


/**
 * Struct CipConnectionPath
 */
struct CipConnectionPath
{
    EipUint8        path_size;          ///< Size of the Path in 16-bit words
    // TODO: Fix, should be UINT (EIP_UINT16)

    EipUint32       class_id;           ///< Class ID of the linked object
    EipUint32       connection_point[3];// TODO:  Why array length 3?
    CipPortSegment  port_segment;
};


/**
 * Struct CipKeyData
 * represents the key data format of the electronic key segment
 */
struct CipKeyData
{
    CipUint     vendor_id;          ///< Vendor ID
    CipUint     device_type;        ///< Device Type
    CipUint     product_code;       ///< Product Code
    CipByte     major_revision;     /**< Major Revision and Compatibility (Bit 0-6 = Major
                                     *  Revision) Bit 7 = Compatibility */
    CipUsint    minor_revision;     ///< Minor Revision
};


struct CipRevision
{
    EipUint8    major_revision;
    EipUint8    minor_revision;
};


/**
 * Struct CipElectronicKey
 */
struct CipElectronicKey
{
    CipUsint    key_format;    /**< Key Format 0-3 reserved, 4 = see Key Format Table,
                                *  5-255 = Reserved */
    CipKeyData  key_data;       /**< Depends on key format used, usually Key Format 4 as
                                 *  specified in CIP Specification, Volume 1*/
};


/**
 * Struct CipMessageRouterRequest
 */
struct CipMessageRouterRequest
{
    CipUsint    service;
    CipEpath    request_path;
    EipInt16    data_length;
    CipOctet*   data;

    /**
     * Function InitRequest.
     * parses the UCMM header consisting of: service, IOI size, IOI, data into a request structure
     * @param aRequest the message data received
     * @param aCount number of bytes in the message
     * @param request pointer to structure of MRRequest data item.
     * @return status  0 .. success
     *                 -1 .. error
     */
    CipError InitRequest( EipUint8* aRequest, EipInt16 aCount );
};


#define MAX_SIZE_OF_ADD_STATUS      2    // for now we support extended status codes up to 2 16bit values there is mostly only one 16bit value used

/** @brief CIP Message Router Response
 *
 */
struct CipMessageRouterResponse
{
    CipUsint reply_service;                                 /**< Reply service code, the requested service code +
                                                             *  0x80 */
    CipOctet reserved;                                      ///< Reserved; Shall be zero
    CipUsint general_status;                                /**< One of the General Status codes listed in CIP
                                                             *  Specification Volume 1, Appendix B */
    CipUsint size_of_additional_status;                     /**< Number of additional 16 bit words in
                                                             *  Additional Status Array */
    EipUint16 additional_status[MAX_SIZE_OF_ADD_STATUS];    /**< Array of 16 bit words; Additional status;
                                                             *  If SizeOfAdditionalStatus is 0. there is no
                                                             *  Additional Status */
    EipInt16    data_length;                                   // TODO: Check if this is correct
    CipOctet*   data;                                         /**< Array of octet; Response data per object definition from
                                                             *  request */
};

class CipInstance;
class CipAttribute;
class CipClass;


/** @ingroup CIP_API
 * @typedef  EIP_STATUS (*AttributeFunc)( CipAttribute *,
 *    CipMessageRouterRequest*, CipMessageRouterResponse*)
 *
 * @brief Signature definition for the implementation of CIP services.
 *
 * @param aAttribute
 * @param aRequest request data
 * @param aResponse storage for the response data, including a buffer for
 *      extended data
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
            EipUint16   aAttributeId,
            EipUint8    aType,
            EipUint8    aFlags,

            // assign your getter and setter per attribute, either may be NULL
            AttributeFunc aGetter,
            AttributeFunc aSetter,

            void*   aData
            );

    virtual ~CipAttribute();

    EipUint16   Id() const              { return attribute_id; }

    EipUint16   attribute_id;
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

    CipInstance( EipUint32 aInstanceId );

    virtual ~CipInstance();

    EipUint32   Id() const { return instance_id; }

    /**
     * Function AttributeInsert
     * inserts an attribute and returns true if succes, else false.
     *
     * @param aAttribute is the one to insert, and may not already be a member
     *  of another instance.
     *
     * @return bool - true if success, else false if failed.  Currently attributes
     *  may be overrridden, so any existing CipAttribute in this instance with the
     *  same attribute_id will be deleted in favour of this one.
     */
    bool AttributeInsert( CipAttribute* aAttributes );

    CipAttribute* AttributeInsert( EipUint16 attribute_id,
        EipUint8        cip_type,
        EipByte         cip_flags,
        void*           data
        );

    CipAttribute* AttributeInsert( EipUint16 attribute_id,
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
    CipAttribute* Attribute( EipUint16 attribute_id ) const;

    const CipAttributes& Attributes() const
    {
        return attributes;
    }

    EipUint32       instance_id;    ///< this instance's number (unique within the class)
    CipClass*       owning_class;   ///< class the instance belongs to or NULL if none.

    EipUint16       highest_inst_attr_id;    ///< highest attribute_id for this instance

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


/** @ingroup CIP_API
 *  @typedef  EIP_STATUS (*TCIPServiceFunc)( CipInstance *,
 *    CipMessageRouterRequest*, CipMessageRouterResponse*)
 *  @brief Signature definition for the implementation of CIP services.
 *
 *  CIP services have to follow this signature in order to be handled correctly
 * by the stack.
 *  @param instance which was referenced in the service request
 *  @param request request data
 *  @param response storage for the response data, including a buffer for
 *      extended data
 *  @return EIP_OK_SEND if service could be executed successfully and a response
 * should be sent
 */
typedef EipStatus (* CipServiceFunction)( CipInstance* instance,
        CipMessageRouterRequest* request,
        CipMessageRouterResponse* response );


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

    EipUint8  Id() const                    { return service_id; }

    const std::string& ServiceName() const  { return service_name; }

    CipServiceFunction service_function;    ///< pointer to a function call

protected:
    std::string service_name;               ///< name of the service
    EipUint8    service_id;                 ///< service number
};


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
     *
     * @return bool - true on success, else false.  Since services may be overridden,
     *  currently this will not fail if the service_id already exists.  It will merely
     *  delete the existing one with the same service_id.
     */
    bool ServiceInsert( CipService* aService );

    CipService* ServiceInsert( EipUint8 service_id,
        CipServiceFunction service_function, const char* service_name );

    /**
     * Function ServiceRemove
     * removes @a a service given by aServiceId and returns ownership to caller
     * if it exists, else NULL.  Caller may delete it, and typically should.
     */
    CipService* ServiceRemove( EipUint8 aServiceId );

    /// Get an existing CipService or return NULL if not found.
    CipService* Service( EipUint8 service_id ) const;

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
     *  instance may not belong to any class at the time this call is made.
     *
     * @return bool - true on succes, else false.  Failure happens when the instance
     *  was marked as already being in another class, or if the instance_id was
     *  not unique.  On success, ownership is passed to this class as a container.
     *  On failure, ownership remains with the caller.
     */
    bool InstanceInsert( CipInstance* aInstances );

    /**
     * Function InstanceInsert
     * inserts a new instance into this class if the @a instance_id is unique.
     * @return CipInstance* - the new instance or NULL if failure.
     */
    CipInstance* InstanceInsert( EipUint32 instance_id );

    CipInstance* Instance( EipUint32 instance_id ) const;

    /// Return a read only collection of CipInstances.
    const CipInstances& Instances() const
    {
        return instances;
    }

    const std::string& ClassName() const    { return class_name; }

    EipUint32   class_id;                   ///< class ID
    EipUint16   revision;                   ///< class revision
    EipUint16   highest_attr_id;            /**< highest defined attribute number
                                             *  (attribute numbers are not necessarily
                                             *  consecutive)*/

    EipUint16   highest_inst_id;            ///< highest defined instance number, not necessarily consecutive

    EipUint32   get_attribute_all_mask;     /**< mask indicating which attributes are
                                              *  returned by getAttributeAll*/


protected:

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
 * if for holding TCP/IP interface information
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


struct CipUnconnectedSendParameter
{
    EipByte         priority;
    EipUint8        timeout_ticks;
    EipUint16       message_request_size;

    CipMessageRouterRequest     message_request;
    CipMessageRouterResponse*   message_response;

    EipUint8        reserved;
    // CipRoutePath    route_path;      CipPortSegment?
    void*           data;
};

/* these are used for creating the getAttributeAll masks
 *  TODO there might be a way simplifying this using __VARARGS__ in #define */
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
