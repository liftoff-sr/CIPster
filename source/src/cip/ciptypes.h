/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 *
 * Conversion to C++ is Copyright (C) 2016, SoftPLC Corportion.
 *
 * All rights reserved.
 *
 ******************************************************************************/
#ifndef OPENER_CIPTYPES_H_
#define OPENER_CIPTYPES_H_


#include <string>
#include <vector>

#include "typedefs.h"
#include "trace.h"
#include "opener_user_conf.h"


/** @brief Segment type Enum
 *
 * Bits 7-5 in the Segment Type/Format byte
 *
 */
enum SegmentType
{
    // Segments
    kSegmentTypePortSegment = 0x00,         ///< Port segment
    kSegmentTypeLogicalSegment  = 0x20,     ///< Logical segment
    kSegmentTypeNetworkSegment  = 0x40,     ///< Network segment
    kSegmentTypeSymbolicSegment = 0x60,     ///< Symbolic segment
    kSegmentTypeDataSegment = 0x80,         ///< Data segment
    kSegmentTypeDataTypeConstructed = 0xA0, ///< Data type constructed
    kSegmentTypeDataTypeElementary  = 0xC0, ///< Data type elementary
    kSegmentTypeSegmentTypeReserved = 0xE0
};

//* @brief Port Segment flags
enum PortSegmentFlag
{
    kPortSegmentFlagExtendedLinkAddressSize = 0x10 ///< Extended Link Address Size flag, Port segment
};

//* @brief Enum containing values which kind of logical segment is encoded
enum LogicalSegmentLogicalType
{
    kLogicalSegmentLogicalTypeClassId = 0x00,           ///< Class ID
    kLogicalSegmentLogicalTypeInstanceId = 0x04,        ///< Instance ID
    kLogicalSegmentLogicalTypeMemberId = 0x08,          ///< Member ID
    kLogicalSegmentLogicalTypeConnectionPoint = 0x0C,   ///< Connection Point
    kLogicalSegmentLogicalTypeAttributeId = 0x10,       ///< Attribute ID
    kLogicalSegmentLogicalTypeSpecial = 0x14,           ///< Special
    kLogicalSegmentLogicalTypeService = 0x18,           ///< Service ID
    kLogicalSegmentLogicalTypeExtendedLogical = 0x1C    ///< Extended Logical
};

/** @brief Enum containing values how long the encoded value will be (8, 16, or
 * 32 bit) */
enum LogicalSegmentLogicalFormat
{
    kLogicalSegmentLogicalFormatEightBitValue = 0x00,
    kLogicalSegmentLogicalFormatSixteenBitValue = 0x01,
    kLogicalSegmentLogicalFormatThirtyTwoBitValue = 0x02
};

enum NetworkSegmentSubType
{
    kProductionTimeInhibitTimeNetworkSegment = 0x43 ///< identifier indicating a production inhibit time network segment
};

enum DataSegmentType
{
    kDataSegmentTypeSimpleDataMessage = kSegmentTypeDataSegment + 0x00,
    kDataSegmentTypeAnsiExtendedSymbolMessage = kSegmentTypeDataSegment + 0x11
};

/** @brief Enum containing the encoding values for CIP data types for CIP
 * Messages */
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

/** @brief Definition of CIP service codes
 *
 * An Enum with all CIP service codes. Common services codes range from 0x01 to
 * 0x1C
 *
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

/** @brief CIP Byte Array
 *
 */
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

/** @brief Struct for padded EPATHs
 *
 */
struct CipEpath
{
    EipUint8    path_size;          ///< Size of the Path in 16-bit words
    // TODO: Fix, should be UINT (EIP_UINT16)

    EipUint16   class_id;           ///< Class ID of the linked object
    EipUint16   instance_number;    ///< Requested Instance Number of the linked object
    EipUint16   attribute_number;   ///< Requested Attribute Number of the linked object
};

/** @brief CIP Connection Path
 *
 */
struct CipConnectionPath
{
    EipUint8    path_size;          ///< Size of the Path in 16-bit words
    // TODO: Fix, should be UINT (EIP_UINT16)

    EipUint32   class_id;           ///< Class ID of the linked object
    EipUint32   connection_point[3];// TODO:  Why array length 3?
    EipUint8    data_segment;
    EipUint8*   segment_data;
};

/** @brief Struct representing the key data format of the electronic key segment
 *
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

/** @brief CIP Electronic Key Segment struct
 *
 */
struct CipElectronicKey
{
    CipUsint    segment_type;  ///< Specifies the Segment Type
    CipUsint    key_format;    /**< Key Format 0-3 reserved, 4 = see Key Format Table,
                                *  5-255 = Reserved */
    CipKeyData  key_data;       /**< Depends on key format used, usually Key Format 4 as
                                 *  specified in CIP Specification, Volume 1*/
};

/** @brief CIP Message Router Request
 *
 */
struct CipMessageRouterRequest
{
    CipUsint    service;
    CipEpath    request_path;
    EipInt16    data_length;
    CipOctet*   data;
};

#define MAX_SIZE_OF_ADD_STATUS 2    // for now we support extended status codes up to 2 16bit values there is mostly only one 16bit value used

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


/**
 * Class CipAttribute
 * holds info for a CIP attribute which may be contained by a #CipInstance
 */
class CipAttribute
{
public:
    CipAttribute(
            EipUint16 aAttributeId = 0,
            EipUint8 aType = 0,
            EipUint8 aFlags = 0,
            void* aData = 0,
            bool IOwnData = false
            ) :
        attribute_id( aAttributeId ),
        type( aType ),
        attribute_flags( aFlags ),
        data( aData ),
        own_data( IOwnData )
    {}

    virtual ~CipAttribute();

    EipUint16   attribute_id;
    EipUint8    type;
    EipUint8    attribute_flags;         /**<   0 => getable_all,
                                                1 => getable_single;
                                                2 => setable_single;
                                                3 => get and setable;
                                                all other values reserved
                                         */
    void*       data;

    bool        own_data;               // Do I own data?
                                        // If so, I must CipFree() it in destructor.
} ;


class CipClass;

/**
 * Class CipInstance
 * holds CIP intance info and instances may be contained within a #CipClass.
 */
class CipInstance
{
public:
    typedef std::vector<CipAttribute*>      CipAttributes;

    CipInstance( EipUint32 aInstanceId, CipClass* aClass ) :
        instance_id( aInstanceId ),
        cip_class( aClass )
    {
    }

    virtual ~CipInstance();

    CipAttribute* AttributeInsert( EipUint16 attribute_id,
        EipUint8 cip_type,
        void* data,
        EipByte cip_flags,
        bool attr_owns_data = false
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

    EipUint32           instance_id;    ///< this instance's number (unique within the class)
    CipClass*           cip_class;      ///< class the instance belongs to

protected:
    CipAttributes       attributes;     ///< sorted pointer array to CipAttributes, unique to this instance

private:

    CipInstance( CipInstance& );        // private because not implemented
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

    std::string service_name;               ///< name of the service
    EipUint8    service_id;                 ///< service number

    CipServiceFunction service_function;    ///< pointer to a function call
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

    CipClass(
        const char* aClassName,
        EipUint32   aClassId,
        EipUint32   a_get_all_class_attributes_mask,
        EipUint32   a_get_all_instance_attributes_mask,
        EipUint16   aRevision = 1
        );

    ~CipClass();

    CipService* ServiceInsert( EipUint8 service_id,
        CipServiceFunction service_function, const char* service_name );

    CipService* Service( EipUint8 service_id ) const;

    const CipServices& Services() const
    {
        return services;
    }


    /**
     * Function InstanceInsert
     * inserts a new instance into this class if the @a instance_id is unique.
     * @return CipInstance* - the new instance or NULL if failure.
     */
    CipInstance* InstanceInsert( EipUint32 instance_id );

    CipInstance* Instance( EipUint32 instance_id ) const;

    const CipInstances& Instances() const
    {
        return instances;
    }

    // the rest of these are specific to the Class class only.
    EipUint32   class_id;                   ///< class ID
    std::string class_name;                 ///< class name
    EipUint16   revision;                   ///< class revision
    EipUint16   highest_attr_id;            /**< highest defined attribute number
                                             *  (attribute numbers are not necessarily
                                             *  consecutive)*/

    EipUint16   highest_inst_id;            ///< highest defined instance number, not necessarily consecutive

    EipUint32   get_attribute_all_mask;     /**< mask indicating which attributes are
                                              *  returned by getAttributeAll*/


protected:

    /**

        Constructor for the meta-class, and only called by public constructor
        above. The constructor above constructs the "public" CIP class. This one
        constructs the meta-class. The meta-class is owned by the public class (
        => is responsible for deleting it).

        A metaClass is a class that holds the class attributes and services.
        CIP can talk to an instance, therefore an instance has a pointer to
        its class. CIP can talk to a class, therefore a class struct is a
        subclass of the instance struct, and contains a pointer to a
        metaclass. CIP never explicitly addresses a metaclass.
    */

    CipClass(
            const char* aClassName,             ///< without "meta-" prefix
            EipUint32   a_get_all_class_attributes_mask,
            CipClass*   aPublicClass
            );

    CipInstances    instances;              ///< collection of instances
    CipServices     services;               ///< collection of services

private:
    CipClass( CipClass& );                  // private because not implemented
};


/**
 * @brief Struct for saving TCP/IP interface information
 */
struct CipTcpIpNetworkInterfaceConfiguration
{
    CipUdint ip_address;
    CipUdint network_mask;
    CipUdint gateway;
    CipUdint name_server;
    CipUdint name_server_2;
    CipString domain_name;
};

struct CipRoutePath
{
    EipUint8    path_size;
    EipUint32   port; // support up to 32 bit path
    EipUint32   address;
};

struct CipUnconnectedSendParameter
{
    EipByte         priority;
    EipUint8        timeout_ticks;
    EipUint16       message_request_size;

    CipMessageRouterRequest     message_request;
    CipMessageRouterResponse*   message_response;

    EipUint8        reserved;
    CipRoutePath    route_path;
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

#define MASK8( a, b, c, d, e, f, g, h )                                \
    ( 1 << (a) | 1 << (b) | 1 << (c) | 1 << (d) | 1 << (e) | 1 << (f) | \
        1 << (g) | 1 << (h) )

#endif // OPENER_CIPTYPES_H_
