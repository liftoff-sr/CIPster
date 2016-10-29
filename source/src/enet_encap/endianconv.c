/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "endianconv.h"

OpenerEndianess g_opener_platform_endianess = kOpenerEndianessUnknown;

// THESE ROUTINES MODIFY THE BUFFER POINTER

int AddSintToMessage( EipUint8 data, EipUint8** buffer )
{
    unsigned char* p = (unsigned char*) *buffer;

    p[0] = (unsigned char) data;
    *buffer += 1;
    return 1;
}


int AddIntToMessage( EipUint16 data, EipUint8** buffer )
{
    unsigned char* p = (unsigned char*) *buffer;

    p[0] = (unsigned char) data;
    p[1] = (unsigned char) (data >> 8);
    *buffer += 2;
    return 2;
}


int AddIntToMessageBE( EipUint16 data, EipUint8** buffer )
{
    unsigned char* p = (unsigned char*) *buffer;

    p[1] = (unsigned char) data;
    p[0] = (unsigned char) (data >> 8);
    *buffer += 2;
    return 2;
}


int AddDintToMessage( EipUint32 data, EipUint8** buffer )
{
    unsigned char* p = (unsigned char*) *buffer;

    p[0] = (unsigned char) data;
    p[1] = (unsigned char) (data >> 8);
    p[2] = (unsigned char) (data >> 16);
    p[3] = (unsigned char) (data >> 24);
    *buffer += 4;

    return 4;
}


int AddDintToMessageBE( EipUint32 data, EipUint8** buffer )
{
    unsigned char* p = (unsigned char*) *buffer;

    p[3] = (unsigned char) data;
    p[2] = (unsigned char) (data >> 8);
    p[1] = (unsigned char) (data >> 16);
    p[0] = (unsigned char) (data >> 24);

    *buffer += 4;

    return 4;
}


#ifdef CIPSTER_SUPPORT_64BIT_DATATYPES

EipUint64 GetLintFromMessage( EipUint8** buffer )
{
    EipUint8* p = *buffer;
    EipUint64 data = (((EipUint64) p[0] ) << 56 ) |
                     (((EipUint64) p[1] ) << 48 ) |
                     (((EipUint64) p[2] ) << 40 ) |
                     (((EipUint64) p[3] ) << 32 ) |
                     (((EipUint64) p[4] ) << 24 ) |
                     (((EipUint64) p[5] ) << 16 ) |
                     (((EipUint64) p[6] ) <<  8 ) |
                     (((EipUint64) p[7] ) <<  0;

    (*buffer) += 8;
    return data;
}


int AddLintToMessage( EipUint64 data, EipUint8** buffer )
{
    EipUint8* p = *buffer;

    p[0] = (EipUint8) (data >> 56);
    p[1] = (EipUint8) (data >> 48);
    p[2] = (EipUint8) (data >> 40);
    p[3] = (EipUint8) (data >> 32);
    p[4] = (EipUint8) (data >> 24);
    p[5] = (EipUint8) (data >> 16);
    p[6] = (EipUint8) (data >> 8);
    p[7] = (EipUint8) data;

    (*buffer) += 8;
    return 8;
}
#endif


int EncapsulateIpAddress( EipUint16 port, EipUint32 address, EipByte** buffer )
{
    int size = 0;

    if( kCIPsterEndianessLittle == g_opener_platform_endianess )
    {
        size += AddIntToMessage( htons( AF_INET ), buffer );
        size += AddIntToMessage( port, buffer );
        size += AddDintToMessage( address, buffer );
    }
    else
    {
        if( kCIPsterEndianessBig == g_opener_platform_endianess )
        {
            EipByte* p = *buffer;

            *p++ = (EipByte) (AF_INET >> 8);
            *p++ = (EipByte) AF_INET;

            *p++ = (EipByte) (port >> 8);
            *p++ = (EipByte) port;

            *p++ = (EipByte) (address >> 24);
            *p++ = (EipByte) (address >> 16);
            *p++ = (EipByte) (address >> 8);
            *p++ = (EipByte) address;

            *buffer = p;
            size += 8;
        }
        else
        {
            fprintf( stderr,
                    "No endianess detected! Probably the DetermineEndianess function was not executed!" );
            exit( EXIT_FAILURE );
        }
    }

    return size;
}


void DetermineEndianess()
{
    int i = 1;
    char* p = (char*) &i;

    if( p[0] == 1 )
    {
        g_opener_platform_endianess = kCIPsterEndianessLittle;
    }
    else
    {
        g_opener_platform_endianess = kCIPsterEndianessBig;
    }
}


int GetEndianess()
{
    return g_opener_platform_endianess;
}


void MoveMessageNOctets( CipOctet** message_runner, int n )
{
    *message_runner += n;
}


int FillNextNMessageOctetsWithValueAndMoveToNextPosition( EipByte value,
        int count, CipOctet** message_runner )
{
    memset( *message_runner, value, count );
    *message_runner += count;
    return count;
}
