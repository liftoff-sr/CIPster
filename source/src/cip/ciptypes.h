/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corporation.
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


/**
 * Enum ClassIds
 * is a set of common classs ids.
 */
enum ClassIds
{
    kCipIdentityClass           = 0x01,
    kCipMessageRouterClass      = 0x02,
    kCipAssemblyClass           = 0x04,
    kCipConnectionClass         = 0x05,
    kCipConnectionManagerClass  = 0x06,
    kCipRegisterClass           = 0x07,
    kCipTcpIpInterfaceClass     = 0xF5,
    kCipEthernetLinkClass       = 0xF6,
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
    kCip6Usint = 0xA2,                          ///< Struct for MAC Address (six USINTs)
    kCipMemberList  = 0xA3,                     ///<
    kCipByteArray   = 0xA4,                     ///<

    // non standard, could assign any value here
    kCipByteArrayLength = 0xA5,
};

/**
 * Enum UdpCommunicationDirection
 * is a set of two directions: inbound or outbound data in the parlance of
 * CIP which terms them as consuming or producing.
 */
enum UdpDirection
{
    kUdpConsuming  = 0,    ///< Consuming direction; receiver
    kUdpProducing  = 1,    ///< Producing direction; sender
};


struct CipRevision
{
    CipRevision( uint8_t aMajor = 0, uint8_t aMinor = 0 ) :
        major_revision( aMajor ),
        minor_revision( aMinor )
    {}

    uint8_t    major_revision;
    uint8_t    minor_revision;
};


class CipInstance;
class CipAttribute;
class CipClass;
class CipMessageRouterRequest;
class CipMessageRouterResponse;
class CipConn;
class Cpf;


// Macros to create integer with bitfields
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

#endif  // CIPSTER_CIPTYPES_H_
