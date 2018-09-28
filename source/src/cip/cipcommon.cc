/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corporation.
 *
 ******************************************************************************/
#include <string.h>

#include "cipcommon.h"

#include "trace.h"
#include "cipster_api.h"
#include "cipidentity.h"
#include "ciptcpipinterface.h"
#include "cipethernetlink.h"
#include "cipconnectionmanager.h"
#include "cipconnection.h"
#include "byte_bufs.h"
#include "encap.h"
#include "ciperror.h"
#include "cipassembly.h"
#include "cipmessagerouter.h"
#include "cpf.h"
#include "appcontype.h"


// global public variables
uint8_t g_message_data_reply_buffer[CIPSTER_MESSAGE_DATA_REPLY_BUFFER];

// private functions

void CipStackInit( uint16_t unique_connection_id )
{
    EipStatus eip_status;

    Encapsulation::Init();

    // The message router is the first CIP object be initialized!!!
    eip_status = CipMessageRouterClass::Init();
    CIPSTER_ASSERT( kEipStatusOk == eip_status );

    eip_status = CipIdentityInit();
    CIPSTER_ASSERT( kEipStatusOk == eip_status );

    eip_status = CipTCPIPInterfaceClass::Init();
    CIPSTER_ASSERT( kEipStatusOk == eip_status );

    eip_status = CipEthernetLinkClass::Init();
    CIPSTER_ASSERT( kEipStatusOk == eip_status );

    eip_status = ConnectionManagerInit();
    CIPSTER_ASSERT( kEipStatusOk == eip_status );

    eip_status = CipConn::Init( unique_connection_id );
    CIPSTER_ASSERT( kEipStatusOk == eip_status );

    eip_status = CipAssemblyClass::Init();
    CIPSTER_ASSERT( kEipStatusOk == eip_status );

#if 0    // do this in caller after return from this function.
    // the application has to be initialized last
    eip_status = ApplicationInitialization();
    CIPSTER_ASSERT( kEipStatusOk == eip_status );
#endif

    (void) eip_status;
}


void ShutdownCipStack()
{
    // First close all connections
    CloseAllConnections();

    // Than free the sockets of currently active encapsulation sessions
    Encapsulation::ShutDown();

    CipTCPIPInterfaceClass::Shutdown();

    // destroy all the instances and classes
    CipClass::DeleteAll();
}


int EncodeData( CipDataType aDataType, const void* input, BufWriter& aBuf )
{
    uint8_t*    start = aBuf.data();

    switch( aDataType )
    {
    case kCipBool:
    case kCipSint:
    case kCipUsint:
    case kCipByte:
        aBuf.put8( *(uint8_t*) input );
        break;

    case kCipInt:
    case kCipUint:
    case kCipWord:
        aBuf.put16( *(uint16_t*) input );
        break;

    case kCipDint:
    case kCipUdint:
    case kCipDword:
    case kCipReal:
        aBuf.put32( *(uint32_t*) input );
        break;

    case kCipLint:
    case kCipUlint:
    case kCipLword:
    case kCipLreal:
        aBuf.put64( *(uint64_t*) input );
        break;

    case kCipStime:
    case kCipDate:
    case kCipTimeOfDay:
    case kCipDateAndTime:
        break;

    case kCipString:
        aBuf.put_STRING( * static_cast<const std::string*>(input), false );
        break;


    case kCipShortString:
        aBuf.put_SHORT_STRING( * static_cast<const std::string*>(input), false );
        break;

    case kCipString2:
        aBuf.put_STRING2( * static_cast<const std::string*>(input) );
        break;

    case kCipStringN:
        break;

    case kCipFtime:
    case kCipLtime:
    case kCipItime:
    case kCipTime:
        break;

    case kCipEngUnit:
        break;

    case kCipUsintUsint:
        {
            CipRevision* revision = (CipRevision*) input;

            aBuf.put8( revision->major_revision );
            aBuf.put8( revision->minor_revision );
        }
        break;

    case kCip6Usint:
        aBuf.append( (const uint8_t*) input, 6 );
        break;

    case kCipMemberList:
        break;

    // The CipByteArray is implemented using a ByteBuf instance.
    case kCipByteArray:
        {
            BufReader rdr = *(ByteBuf*) input;

            aBuf.append( rdr );
        }
        break;

    case kCipByteArrayLength:
        aBuf.put16( ((ByteBuf*) input)->size() );
        break;

    default:
        break;
    }

    int byte_count = aBuf.data() - start;

    return byte_count;
}


int DecodeData( CipDataType aDataType, void* data, BufReader& aBuf )
{
    const uint8_t* start = aBuf.data();

    switch( aDataType )
    {
    case kCipBool:
    case kCipSint:
    case kCipUsint:
    case kCipByte:
        *(uint8_t*) data = aBuf.get8();
        break;

    case kCipInt:
    case kCipUint:
    case kCipWord:
        *(uint16_t*) data = aBuf.get16();
        break;

    case kCipDint:
    case kCipUdint:
    case kCipDword:
        *(uint32_t*) data = aBuf.get32();
        break;

    case kCipLint:
    case kCipUlint:
    case kCipLword:
        *(uint64_t*) data = aBuf.get64();
        break;

    // The CipByteArray is implemented using a ByteBuf instance.
    case kCipByteArray:
        {
            BufWriter w = *(ByteBuf*) data;

            w.append( aBuf );
            aBuf += ((ByteBuf*)data)->size();
        }
        break;

    case kCipByteArrayLength:
        {
            ByteBuf* bb = (ByteBuf*) data;

            *bb = ByteBuf( bb->data(), aBuf.get16() );
        }
        break;

    case kCipString:
        *(std::string*) data = aBuf.get_STRING( true );
        break;

    case kCipShortString:
        *(std::string*) data = aBuf.get_SHORT_STRING( true );
        break;

    case kCipString2:
        *(std::string*) data = aBuf.get_STRING2();
        break;

    default:
        return -1;
    }

    int byte_count = aBuf.data() - start;

    return byte_count;
}

