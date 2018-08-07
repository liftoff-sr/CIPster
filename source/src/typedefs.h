/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 *
 ******************************************************************************/
#ifndef CIPSTER_TYPEDEFS_H_
#define CIPSTER_TYPEDEFS_H_

#include <inttypes.h>
#include <stddef.h>

/** @file typedefs.h
 *  Do not use interface types for internal variables, such as "int i;", which is
 *  commonly used for loop counters or counting things.
 *
 *  Do not over-constrain data types. Prefer the use of the native "int" and
 *  "unsigned" types.
 *
 *  Use char for native character strings.
 *
 *  Do not use "char" for data buffers - use "unsigned char" instead. Using char
 *  for data buffers can occasionally blow up in your face rather nastily.
 */

/** @brief EIP Data type definitions
 */
typedef uint8_t     EipByte;    ///< unsigned byte
typedef int8_t      EipInt8;    ///< 8-bit signed number
typedef int16_t     EipInt16;   ///< 16-bit signed number
typedef int32_t     EipInt32;   ///< 32-bit signed number
typedef uint8_t     EipUint8;   ///< 8-bit unsigned number
typedef uint16_t    EipUint16;  ///< 16-bit unsigned number
typedef uint32_t    EipUint32;  ///< 32-bit unsigned number
typedef int64_t     EipInt64;   ///< 64-bit signed number
typedef uint64_t    EipUint64;  ///< 64-bit unsigned number
typedef float       EipFloat;   ///< IEEE 754 32-bit floating point number
typedef double      EipDfloat;  ///< IEEE 754 64-bit floating point number


/** @brief Data types as defined in the CIP Specification Vol 1 Appendix C
 */
typedef uint8_t     CipOctet;   ///< 8 bit value that indicates particular data type
typedef uint8_t     CipBool;    ///< Boolean data type
typedef uint8_t     CipByte;    ///< 8-bit bit string
typedef uint16_t    CipWord;    ///< 16-bit bit string
typedef uint32_t    CipDword;   ///< 32-bit bit string
typedef uint8_t     CipUsint;   ///< 8-bit unsigned
typedef uint16_t    CipUint;    ///< CipUint 16-bit unsigned
typedef uint32_t    CipUdint;   ///< CipUdint 32-bit unsigned
typedef int8_t      CipSint;    ///< 8-bit signed integer
typedef int16_t     CipInt;     ///< 16-bit signed integer
typedef int32_t     CipDint;    ///< 32-bit signed integer
typedef float       CipReal;    ///< 32-bit IEEE 754 floating point
typedef double      CipLreal;   ///< 64-bit IEEE 754 floating point

typedef int64_t     CipLint;    ///< 64-bit signed integer
typedef uint64_t    CipUlint;   ///< 64-bit unsignedeger
typedef uint64_t    CipLword;   ///< 64-bit bit string


typedef uint32_t    USECS_T;



/// Constant identifying an invalid socket
const int kEipInvalidSocket = -1;

/**
 *
 *  The following are generally true regarding return status:
 *  -1 ... an error occurred
 *  0 ... success
 *
 *  Occasionally there is a variation on this:
 *  -1 ... an error occurred
 *  0 ..  success and there is no reply to send
 *  1 ... success and there is a reply to send
 *
 *  For both of these cases EipStatus is the return type.
 *
 *  Other return type are:
 *  -- return pointer to thing, 0 if error (return type is "pointer to thing")
 *  -- return count of something, -1 if error, (return type is int)
 *
 */


/**
 * Enum EipStatus
 * is a set of status values which are returned by API functions.
 */
enum EipStatus
{
    kEipStatusOk      = 0,
    kEipStatusOkSend  = 1,
    kEipStatusError   = -1,
};


/// The count of elements in a single dimension array:
#define DIM(x)          int( sizeof(x)/sizeof(x[0]) )

#endif // CIPSTER_TYPEDEFS_H_
