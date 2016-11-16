/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#ifndef CIPSTER_ENDIANCONV_H_
#define CIPSTER_ENDIANCONV_H_

#include "typedefs.h"

/** @file endianconv.h
 * @brief Responsible for Endianess conversion
 */

enum OpenerEndianess
{
    kOpenerEndianessUnknown = -1,
    kCIPsterEndianessLittle = 0,
    kCIPsterEndianessBig = 1
};


/** @ingroup ENCAP
 * Function GetIntFromMessage
 * returns a 16Bit integer from the network buffer, and moves pointer beyond the 16 bit value
 * @param buffer Pointer to the network buffer array. This pointer will be incremented by 2!
 * @return Extracted 16 bit integer value
 */
inline EipUint16 GetIntFromMessage( const EipByte** buffer )
{
    const EipByte*  p = *buffer;

    EipUint16   data = p[0] | p[1] << 8;

    *buffer += 2;
    return data;
}


/** @ingroup ENCAP
 * Function GetDintFromMessage
 * returns a 32Bit integer from the network buffer.
 * @param buffer pointer to the network buffer array. This pointer will be incremented by 4!
 * @return Extracted 32 bit integer value
 */
inline EipUint32 GetDintFromMessage( const EipByte** buffer )
{
    const EipByte* p = *buffer;

    EipUint32 data = p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;

    *buffer += 4;
    return data;
}


inline EipUint16 GetIntFromMessageBE( const EipByte** buffer )
{
    const EipByte*   p = *buffer;

    EipUint16   data = p[1] | p[0] << 8;

    *buffer += 2;
    return data;
}


inline EipUint32 GetDintFromMessageBE( const EipByte** buffer )
{
    const EipByte* p = *buffer;

    EipUint32 data = p[3] | p[2] << 8 | p[1] << 16 | p[0] << 24;

    *buffer += 4;
    return data;
}


/** @ingroup ENCAP
 * Function AddSintToMessage
 * converts UINT8 data from host to little endian and writes it to buffer and advances buffer.
 * @param data value to be written
 * @param buffer pointer where data should be written.
 */
int AddSintToMessage( EipUint8 data, EipByte** buffer );

/** @ingroup ENCAP
 * Function AddIntToMessage
 * writes a 16Bit integer to buffer and advances buffer.
 * @param data value to write
 * @param buffer pointer to pointer to array.
 *
 * @return Length in bytes of the encoded message
 */
int AddIntToMessage( EipUint16 data, EipByte** buffer );

int AddIntToMessageBE( EipUint16 data, EipByte** buffer );


/** @ingroup ENCAP
 * Function AddDintToMessage
 * writes a 32Bit integer to the buffer and advances buffer.
 * @param data value to write
 * @param buffer pointer to the network buffer array. This pointer will be incremented by 4!
 *
 * @return Length in bytes of the encoded message
 */
int AddDintToMessage( EipUint32 data, EipByte** buffer );

int AddDintToMessageBE( EipUint32 data, EipByte** buffer );

#ifdef CIPSTER_SUPPORT_64BIT_DATATYPES

/**
 * Function GetLintFromMessage
 * reads EipUint64 from buffer and advances buffer.
 * @param buffer pointer to pointer to bytes
 * @return EipUint64
 */
EipUint64 GetLintFromMessage( EipByte** buffer );

/** @ingroup ENCAP
 * Functgoin AddLintToMessage
 * writes a 64Bit integer to buffer and advances buffer.
 * @param data value to write
 * @param buffer pointer to pointer byte array. This pointer will be incremented by 8.
 *
 * @return int - 8
 */
int AddLintToMessage( EipUint64 pa_unData, EipByte** buffer );

#endif

/** @brief Encapsulate the sockaddr information as necessary for the Common Packet Format data items
 *
 * Converts and adds the provided port and IP address into an common packet format message
 *
 * @param port Port of the socket, has to be provided in big-endian
 * @param address IP address of the socket, has to be provided in big-endian
 * @param communcation_buffer The message buffer for sending the message
 */
int EncapsulateIpAddress( EipUint16 port, EipUint32 address,
        EipByte** communication_buffer );

/**
 * Function DetermineEndianess
 * detects Endianess of the platform and sets global
 * g_nCIPsterPlatformEndianess variable accordingly:
 * 0 equals little endian and 1 equals big endian
 */
void DetermineEndianess();

/** @brief Return the endianess identified on system startup
 * @return
 *    - -1 endianess has not been identified up to now
 *    - 0  little endian system
 *    - 1  big endian system
 */
int GetEndianess();

void MoveMessageNOctets( CipOctet** message_runner, int n );

int FillNextNMessageOctetsWithValueAndMoveToNextPosition( EipByte value,
        int count, CipOctet** message_runner );


#endif    // CIPSTER_ENDIANCONV_H_
