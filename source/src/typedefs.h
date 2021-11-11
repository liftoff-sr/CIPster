/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 *
 ******************************************************************************/
#ifndef CIPSTER_TYPEDEFS_H_
#define CIPSTER_TYPEDEFS_H_

#include <inttypes.h>
#include <stddef.h>


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



/// Constant identifying an invalid socket
const int kSocketInvalid = -1;

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
#define DIM(x)          int(      sizeof(x)/sizeof((x)[0]) )
#define UDIM(x)         unsigned( sizeof(x)/sizeof((x)[0]) )

#endif // CIPSTER_TYPEDEFS_H_
