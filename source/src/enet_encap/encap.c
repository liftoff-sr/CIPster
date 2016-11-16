/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#include <string.h>
#include <stdlib.h>
#include "cipster_api.h"
#include "cpf.h"
#include "encap.h"
#include "endianconv.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "cipconnectionmanager.h"
#include "cipidentity.h"

// Identity data from cipidentity.c
extern EipUint16 vendor_id_;
extern EipUint16 device_type_;
extern EipUint16 product_code_;
extern CipRevision revision_;
extern EipUint16 status_;
extern EipUint32 serial_number_;
extern CipShortString product_name_;

// ip address data taken from TCPIPInterfaceObject
extern CipTcpIpNetworkInterfaceConfiguration interface_configuration_;

const int kSupportedProtocolVersion = 1;                    //*< Supported Encapsulation protocol version

/// Mask of which options are supported; as of the current CIP specs no other
/// option value than 0 should be supported.
const int kEncapsulationHeaderOptionsFlag = 0x00;

const int kEncapsulationHeaderSessionHandlePosition = 4;    //*< the position of the session handle within the encapsulation header

enum SessionStatus
{
    kSessionStatusInvalid = -1,
    kSessionStatusValid = 0
};

const int kSenderContextSize = 8;    //*< size of sender context in encapsulation header

/// @brief definition of known encapsulation commands
enum EncapsulationCommand
{
    kEncapsulationCommandNoOperation = 0x0000,          //*< only allowed for TCP
    kEncapsulationCommandListServices = 0x0004,         //*< allowed for both UDP and TCP
    kEncapsulationCommandListIdentity = 0x0063,         //*< allowed for both UDP and TCP
    kEncapsulationCommandListInterfaces = 0x0064,       //*< optional, allowed for both UDP and TCP
    kEncapsulationCommandRegisterSession = 0x0065,      //*< only allowed for TCP
    kEncapsulationCommandUnregisterSession = 0x0066,    //*< only allowed for TCP
    kEncapsulationCommandSendRequestReplyData = 0x006F, //*< only allowed for TCP
    kEncapsulationCommandSendUnitData = 0x0070          //*< only allowed for TCP
};

/// @brief definition of capability flags
enum CapabilityFlags
{
    kCapabilityFlagsCipTcp = 0x0020,
    kCapabilityFlagsCipUdpClass0or1 = 0x0100
};


/// According to EIP spec at least 2 delayed message requests should be supported
#define ENCAP_NUMBER_OF_SUPPORTED_DELAYED_ENCAP_MESSAGES    2

// currently we only have the size of an encapsulation message
#define ENCAP_MAX_DELAYED_ENCAP_MESSAGE_SIZE   \
    ( ENCAPSULATION_HEADER_LENGTH + 39 + sizeof(CIPSTER_DEVICE_NAME) )


struct DelayedMsg
{
    EipInt32    time_out_usecs;     // must be signed 32 bits
    int         socket;
    sockaddr_in receiver;
    EipByte     message[ENCAP_MAX_DELAYED_ENCAP_MESSAGE_SIZE];
    unsigned    message_size;
};


/**
 * Class EncapsulationData
 * is not visible to CIP code, accomplished by keeping this out of a header
 */
class EncapsulationData
{
public:
    int         command_code;
    int         data_length;
    CipUdint    session_handle;
    CipUdint    status;
    CipOctet    sender_context[8];      ///< length of 8, according to the specification
    CipUdint    options;

public:
    /**
     * Function DeserializeEncap
     * grabs fields from serialized encapsulation header into this struct.
     *
     * @param aSrc the received packet
     *
     * @return int - no. bytes consumed, or -1 if error
     */
    int DeserializeEncap( CipBufNonMutable aSrc );

    int SerializeEncap( CipBufMutable aDst );
};


ListServices g_list_services(
    kCipItemIdListServiceResponse,
    1,
    kCapabilityFlagsCipTcp | kCapabilityFlagsCipUdpClass0or1,
    "Communications"
    );


static int g_registered_sessions[CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS];

static DelayedMsg g_delayed_messages[ENCAP_NUMBER_OF_SUPPORTED_DELAYED_ENCAP_MESSAGES];


//   @brief Initializes session list and interface information.
void EncapsulationInit()
{
    DetermineEndianess();

    // initialize random numbers for random delayed response message generation
    // we use the ip address as seed as suggested in the spec
    srand( interface_configuration_.ip_address );

    // initialize Sessions to invalid == free session
    for( unsigned i = 0; i < CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS; i++ )
    {
        g_registered_sessions[i] = kEipInvalidSocket;
    }

    for( unsigned i = 0; i < ENCAP_NUMBER_OF_SUPPORTED_DELAYED_ENCAP_MESSAGES; i++ )
    {
        g_delayed_messages[i].socket = -1;
    }
}


const int kListIdentityDefaultDelayTime = 2000;
const int kListIdentityMinimumDelayTime = 500;

static unsigned determineDelayTimeMSecs( unsigned aMaxMSecs )
{
    if( aMaxMSecs == 0 )
    {
        aMaxMSecs = kListIdentityDefaultDelayTime;
    }
    else if( aMaxMSecs < kListIdentityMinimumDelayTime )
    {
        // if seed time is between 1 and 500ms set it to 500ms
        aMaxMSecs = kListIdentityMinimumDelayTime;
    }

    // Set delay time between 0 and aMaxMSecs
    unsigned ret = ( aMaxMSecs * rand() ) / RAND_MAX;

    return ret;
}


static EncapsulationProtocolErrorCode registerSocket( int aSocket, CipUdint* aIndex )
{
    for( int i = 0; i < CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS; ++i )
    {
        if( g_registered_sessions[i] == aSocket )
        {
            // The socket has already registered a session. This is not allowed.
            // Return the already assigned session back, the cip spec is
            // not clear about this needs to be tested.
            *aIndex = i + 1;
            return  kEncapsulationProtocolInvalidOrUnsupportedCommand;
        }
    }

    for( int i = 0;  i < CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS;  ++i )
    {
        if( kEipInvalidSocket == g_registered_sessions[i] )
        {
            g_registered_sessions[i] = aSocket;
            *aIndex = i + 1;
            return kEncapsulationProtocolSuccess;
        }
    }

    return kEncapsulationProtocolInsufficientMemory;
}


/**
 * Function registerSession
 * checks supported protocol, generates a session handle, and serializes a reply.
 * @param socket which socket this request is associated with.
 * @param aCommand
 * @param aReply where to put reply
 * @return int - num bytes serialized into aReply
 */
static int registerSession( int socket, CipBufNonMutable aCommand, CipBufMutable aReply,
        EncapsulationProtocolErrorCode* aEncapError, CipUdint* aSessionHandle )
{
    if( aCommand.size() < 4 )
        return -1;

    if( aReply.size() < 4 )
        return -1;

    int result = -1;

    const EipByte* p = aCommand.data();

    int version = GetIntFromMessage( &p );
    int options = GetIntFromMessage( &p );

    // check if requested protocol version is supported and the register session option flag is zero
    if( version && version <= kSupportedProtocolVersion && !options )
    {
        *aEncapError = registerSocket( socket, aSessionHandle );
    }
    else    // protocol not supported
    {
        *aEncapError = kEncapsulationProtocolUnsupportedProtocol;
    }

    if( version > kSupportedProtocolVersion )
        version = kSupportedProtocolVersion;

    EipByte* r = aReply.data();

    AddIntToMessage( version, &r );
    AddIntToMessage( options, &r );

    return r - aReply.data();
}


/**
 * Function checkRegisteredSessions
 * checks if received package belongs to registered session.
 *
 * @param receive_data Received data.
 * @return 0 .. Session registered
 *          kInvalidSession .. invalid session -> return unsupported command received
 */
static SessionStatus checkRegisteredSessions( CipUdint session_handle )
{
    if( session_handle > 0 && session_handle <= CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS )
    {
        if( kEipInvalidSocket != g_registered_sessions[session_handle - 1] )
        {
            return kSessionStatusValid;
        }
    }

    return kSessionStatusInvalid;
}


/**
 * Function unregisterSession
 * closes all corresponding TCP connections and deletes session handle.
 */
static EncapsulationProtocolErrorCode unregisterSession( CipUdint session_handle )
{
    if( session_handle && session_handle <= CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS )
    {
        int ndx = session_handle - 1;

        if( kEipInvalidSocket != g_registered_sessions[ndx] )
        {
            IApp_CloseSocket_tcp( g_registered_sessions[ndx] );
            g_registered_sessions[ndx] = kEipInvalidSocket;
            return kEncapsulationProtocolSuccess;
        }
    }

    // no such session registered
    return kEncapsulationProtocolInvalidSessionHandle;
}


static int encapsulateListIdentyResponseMessage( CipBufMutable aReply )
{
    if( aReply.size() < 40 )
        return -1;

    EipByte* p = aReply.data();

    AddIntToMessage( 1, &p );       // Item count: one item
    AddIntToMessage( kCipItemIdListIdentityResponse, &p );

    // at this place the real length will be inserted below
    EipByte* id_length_buffer = p;

    p += 2;

    AddIntToMessage( kSupportedProtocolVersion, &p );

    EncapsulateIpAddress( htons( kOpenerEthernetPort ),
            interface_configuration_.ip_address,
            &p );

    memset( p, 0, 8 );
    p += 8;

    AddIntToMessage( vendor_id_, &p );
    AddIntToMessage( device_type_, &p );
    AddIntToMessage( product_code_, &p );

    *p++ = revision_.major_revision;
    *p++ = revision_.minor_revision;

    AddIntToMessage( status_, &p );
    AddDintToMessage( serial_number_, &p );

    *p++ = (EipByte) product_name_.length;

    memcpy( p, product_name_.string, product_name_.length );
    p += product_name_.length;

    *p++ = 0xFF;

    AddIntToMessage( p - id_length_buffer - 2,
            &id_length_buffer );      // the -2 is for not counting the length field

    return p - aReply.data();
}


static int handleReceivedListIdentityCommandImmediate( CipBufMutable aReply )
{
    return encapsulateListIdentyResponseMessage( aReply );
}

static int handleReceivedListIdentityCommandDelayed( int socket, const sockaddr_in* from_address,
        unsigned aMSecDelay, CipBufNonMutable aCommand )
{
    DelayedMsg* delayed = NULL;

    for( unsigned i = 0; i < ENCAP_NUMBER_OF_SUPPORTED_DELAYED_ENCAP_MESSAGES; i++ )
    {
        if( kEipInvalidSocket == g_delayed_messages[i].socket )
        {
            delayed = &g_delayed_messages[i];
            break;
        }
    }

    if( delayed )
    {
        delayed->socket   = socket;
        delayed->receiver = *from_address;

        delayed->time_out_usecs = aMSecDelay * 1000;

        memcpy( delayed->message, aCommand.data(), ENCAPSULATION_HEADER_LENGTH );

        delayed->message_size = encapsulateListIdentyResponseMessage(
            CipBufMutable( delayed->message + ENCAPSULATION_HEADER_LENGTH,
                sizeof( delayed->message ) - ENCAPSULATION_HEADER_LENGTH ) );

        EipByte* buf = delayed->message + 2;

        AddIntToMessage( delayed->message_size, &buf );

        delayed->message_size += ENCAPSULATION_HEADER_LENGTH;
    }

    return 0;
}


static int handleReceivedListInterfacesCommand( CipBufMutable aReply )
{
    EipByte* p = aReply.data();

    // vOL2 2-4.3.3 At present no public items are defined for the ListInterfaces reply.
    AddIntToMessage( 0, &p );

    return p - aReply.data();
}


/**
 * Function handleReceivedListServicesCommand
 * generates a reply with "Communications Services" + compatibility Flags.
 * @param aReply where to put the reply
 * @return int - the number of bytes put into aReply or -1 if buffer overrun.
 */
static int handleReceivedListServicesCommand( CipBufMutable aReply )
{
    EipByte* p = aReply.data();

    if( aReply.size() >= 26 )
    {
        AddIntToMessage( 1, &p );

        AddIntToMessage( g_list_services.id, &p );

        AddIntToMessage( (EipUint16) (g_list_services.byte_count - 4), &p );

        AddIntToMessage( g_list_services.protocol_version, &p );

        AddIntToMessage( g_list_services.capability_flags, &p );

        memset( p, 0, 16 );
        memcpy( p, g_list_services.name_of_service.data(),
            std::min( (int) g_list_services.name_of_service.size(), 16 ) );

        return p - aReply.data();
    }
    else
        return -1;
}


int HandleReceivedExplictTcpData( int socket, CipBufNonMutable aCommand, CipBufMutable aReply )
{
    CIPSTER_ASSERT( aReply.size() >= ENCAPSULATION_HEADER_LENGTH ); // caller bug

    int result = -1;

    if( aCommand.size() < ENCAPSULATION_HEADER_LENGTH )
        return -1;

    EncapsulationData encap;

    if( encap.DeserializeEncap( aCommand ) != ENCAPSULATION_HEADER_LENGTH )
        return -1;

    if( encap.command_code == kEncapsulationCommandNoOperation )
        return -1;

    if( encap.status )  // all commands have 0 in status, else ignore
        return -1;

    encap.data_length = 0;  // aCommand.size() has our length, establish default for reply

    // Adjust locally for encapsulation header which is both consumed in the
    // the command and reserved in the reply.
    CipBufNonMutable    command = aCommand + ENCAPSULATION_HEADER_LENGTH;
    CipBufMutable       reply = aReply + ENCAPSULATION_HEADER_LENGTH;

    // most of these functions need a reply to be sent
    switch( encap.command_code )
    {
    case kEncapsulationCommandListServices:
        if( !encap.options )
            result = handleReceivedListServicesCommand( reply );
        break;

    case kEncapsulationCommandListIdentity:
        if( !encap.options )
            result = handleReceivedListIdentityCommandImmediate( reply );
        break;

    case kEncapsulationCommandListInterfaces:
        if( !encap.options )
            result = handleReceivedListInterfacesCommand( reply );
        break;

    case kEncapsulationCommandRegisterSession:
        if( !encap.options )
        {
            EncapsulationProtocolErrorCode status;
            result = registerSession( socket, command, reply, &status, &encap.session_handle );
            encap.status = status;
        }
        break;

    case kEncapsulationCommandUnregisterSession:
        encap.status = unregisterSession( encap.session_handle );
        result = 0;
        break;

    case kEncapsulationCommandSendRequestReplyData:
        if( !encap.options && aCommand.size() >= 6 )
        {
            if( kSessionStatusValid == checkRegisteredSessions( encap.session_handle ) )
            {
                result = NotifyCommonPacketFormat(
                    command + 6,    // skip null interface handle + timeout value
                                    // which follow encap header.
                    reply           // again, this is past encap header
                    );

                if( result < 0 )
                    ecnap.status = -result;
            }
            else    // received a packet with non registered session handle
            {
                encap.status = kEncapsulationProtocolInvalidSessionHandle;
                result = 0;
            }
        }
        break;

    case kEncapsulationCommandSendUnitData:
        if( !encap.options && command.size() >= 6 )
        {
            if( kSessionStatusValid == checkRegisteredSessions( encap.session_handle ) )
            {
                result = NotifyConnectedCommonPacketFormat(
                            command + 6, // skip null interface handle + timeout value
                            reply
                            );
            }
            else    // received a packet with non registered session handle
            {
                encap.status = kEncapsulationProtocolInvalidSessionHandle;
                result = 0;
            }
        }
        break;

    default:
        // Vol2 2-3.2
        encap.status = kEncapsulationProtocolInvalidOrUnsupportedCommand;
        result = 0;
        break;
    }

    if( result >= 0 )
    {
        encap.data_length = result;
        encap.SerializeEncap( aReply );
        result += ENCAPSULATION_HEADER_LENGTH;
    }

    return result;
}


int HandleReceivedExplictUdpData( int socket, const sockaddr_in* from_address,
        CipBufNonMutable aCommand, CipBufMutable aReply, bool isUnicast )
{
    CIPSTER_ASSERT( aReply.size() >= ENCAPSULATION_HEADER_LENGTH ); // caller bug

    int result = -1;

    if( aCommand.size() < ENCAPSULATION_HEADER_LENGTH )
        return -1;

    EncapsulationData encap;

    if( encap.DeserializeEncap( aCommand ) != ENCAPSULATION_HEADER_LENGTH )
        return -1;

    if( encap.command_code == kEncapsulationCommandNoOperation )
        return -1;

    if( encap.status )  // all commands have 0 in status, else ignore
        return -1;

    encap.data_length = 0;  // aCommand.size() has our length, establish default for reply

    // Adjust locally for encapsulation header which is both consumed in the
    // the command and reserved in the reply.
    CipBufNonMutable    command = aCommand + ENCAPSULATION_HEADER_LENGTH;
    CipBufMutable       reply = aReply + ENCAPSULATION_HEADER_LENGTH;

    switch( encap.command_code )
    {
    case kEncapsulationCommandListServices:
        if( !encap.options )
            result = handleReceivedListServicesCommand( reply );
        break;

    case kEncapsulationCommandListIdentity:
        if( isUnicast )
        {
            result = handleReceivedListIdentityCommandImmediate( reply );
        }
        else
        {
            unsigned maxMSecs = encap.sender_context[0] | (encap.sender_context[1] << 8);
            unsigned timeoutMSecs = determineDelayTimeMSecs( maxMSecs );

            result = handleReceivedListIdentityCommandDelayed( socket, from_address,
                        timeoutMSecs, aCommand );
        }
        break;

    case kEncapsulationCommandListInterfaces:
        if( !encap.options )
            result = handleReceivedListInterfacesCommand( reply );
        break;

    // The following commands are not to be sent via UDP
    case kEncapsulationCommandNoOperation:
    case kEncapsulationCommandRegisterSession:
    case kEncapsulationCommandUnregisterSession:
    case kEncapsulationCommandSendRequestReplyData:
    case kEncapsulationCommandSendUnitData:
    default:
        encap.status = kEncapsulationProtocolInvalidOrUnsupportedCommand;
        break;
    }

    // if nRetVal is greater than 0, then data has to be sent
    if( result >= 0 )
    {
        encap.data_length = result;
        encap.SerializeEncap( aReply );
        result += ENCAPSULATION_HEADER_LENGTH;
    }

    return result;
}


int EncapsulationData::DeserializeEncap( CipBufNonMutable aCommand )
{
    const EipByte* p = aCommand.data();

    if( aCommand.size() >= ENCAPSULATION_HEADER_LENGTH )
    {
        command_code = GetIntFromMessage( &p );
        data_length  = GetIntFromMessage( &p );

        session_handle = GetDintFromMessage( &p );

        status = GetDintFromMessage( &p );

        for( int i=0; i<8;  ++i )
            sender_context[i] = *p++;

        options = GetDintFromMessage( &p );
    }

    int byte_count = p - aCommand.data();

    return byte_count;
}


int EncapsulationData::SerializeEncap( CipBufMutable aDst )
{
    EipByte* p = aDst.data();

    if( aDst.size() >= ENCAPSULATION_HEADER_LENGTH )
    {
        AddIntToMessage( command_code, &p );
        AddIntToMessage( data_length, &p );
        AddDintToMessage( session_handle, &p );
        AddDintToMessage( status, &p );

        for( int i=0; i<8;  ++i )
            *p++ = sender_context[i];

        AddDintToMessage( options, &p );
    }

    return p - aDst.data();
}



void CloseSession( int socket )
{
    for( int i = 0; i < CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS; ++i )
    {
        if( g_registered_sessions[i] == socket )
        {
            IApp_CloseSocket_tcp( socket );
            g_registered_sessions[i] = kEipInvalidSocket;
            break;
        }
    }
}


void EncapsulationShutDown()
{
    for( int i = 0; i < CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS; ++i )
    {
        if( kEipInvalidSocket != g_registered_sessions[i] )
        {
            IApp_CloseSocket_tcp( g_registered_sessions[i] );
            g_registered_sessions[i] = kEipInvalidSocket;
        }
    }
}


void ManageEncapsulationMessages()
{
    for( unsigned i = 0; i < ENCAP_NUMBER_OF_SUPPORTED_DELAYED_ENCAP_MESSAGES; i++ )
    {
        if( kEipInvalidSocket != g_delayed_messages[i].socket )
        {
            g_delayed_messages[i].time_out_usecs -= kOpenerTimerTickInMicroSeconds;

            if( g_delayed_messages[i].time_out_usecs < 0 )
            {
                // If delay is reached or passed, send the UDP message
                SendUdpData( &g_delayed_messages[i].receiver,
                        g_delayed_messages[i].socket,
                        g_delayed_messages[i].message,
                        g_delayed_messages[i].message_size );

                g_delayed_messages[i].socket = -1;
            }
        }
    }
}
