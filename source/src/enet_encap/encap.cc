/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/
#include <string.h>
#include <stdlib.h>
#include "cipster_api.h"
#include "cpf.h"
#include "encap.h"
#include "byte_bufs.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "cipconnectionmanager.h"
#include "cipidentity.h"
#include "ciptcpipinterface.h"


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

    BufReader Payload() const
    {
        return BufReader( message, message_size );
    }
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
    int DeserializeEncap( BufReader aSrc );

    int SerializeEncap( BufWriter aDst );
};


static int g_registered_sessions[CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS];

static DelayedMsg g_delayed_messages[ENCAP_NUMBER_OF_SUPPORTED_DELAYED_ENCAP_MESSAGES];


//   @brief Initializes session list and interface information.
void EncapsulationInit()
{
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
static int registerSession( int socket, BufReader aCommand, BufWriter aReply,
        EncapsulationProtocolErrorCode* aEncapError, CipUdint* aSessionHandle )
{
    *aEncapError = kEncapsulationProtocolSuccess;

    BufWriter out = aReply;

    int version = aCommand.get16();
    int options = aCommand.get16();

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

    out.put16( version );
    out.put16( options );

    return out.data() - aReply.data();
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


static int encapsulateListIdentyResponseMessage( BufWriter aReply )
{
    if( aReply.size() < 40 )
        return -1;

    BufWriter out = aReply;

    out.put16( 1 );       // Item count: one item
    out.put16( kCipItemIdListIdentityResponse );

    // at this place the real length will be inserted below
    BufWriter id_length = out;

    out += 2;

    out.put16( kSupportedProtocolVersion );

    out.put16BE( AF_INET );
    out.put16BE( kOpenerEthernetPort );
    out.put32BE( ntohl( interface_configuration_.ip_address ) );

    out.fill( 8 );

    out.put16( vendor_id_ );
    out.put16( device_type_ );
    out.put16( product_code_ );

    *out++ = revision_.major_revision;
    *out++ = revision_.minor_revision;

    out.put16( status_ );
    out.put32( serial_number_ );

    out.put_SHORT_STRING( product_name_, false );

    *out++ = 0xff;      // optional STATE, not supported indicated by 0xff.

    // the -2 is for not counting the length field
    id_length.put16( out.data() - id_length.data() - 2 );

    return out.data() - aReply.data();
}


static int handleReceivedListIdentityCommandImmediate( BufWriter aReply )
{
    return encapsulateListIdentyResponseMessage( aReply );
}

static int handleReceivedListIdentityCommandDelayed( int socket, const sockaddr_in* from_address,
        unsigned aMSecDelay, BufReader aCommand )
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

        BufWriter out( delayed->message, sizeof delayed->message );

        out.append( aCommand.data(), ENCAPSULATION_HEADER_LENGTH );

        delayed->message_size = encapsulateListIdentyResponseMessage( out );

        BufWriter len( delayed->message + 2, 2 );

        len.put16( delayed->message_size );

        delayed->message_size += ENCAPSULATION_HEADER_LENGTH;
    }

    return 0;
}


static int handleReceivedListInterfacesCommand( BufWriter aReply )
{
    BufWriter out = aReply;

    // vOL2 2-4.3.3 At present no public items are defined for the ListInterfaces reply.
    out.put16( 0 );

    return out.data() - aReply.data();
}


/**
 * Function handleReceivedListServicesCommand
 * generates a reply with "Communications" + compatibility Flags.
 * @param aReply where to put the reply
 * @return int - the number of bytes put into aReply or -1 if buffer overrun.
 */
static int handleReceivedListServicesCommand( BufWriter aReply )
{
    BufWriter out = aReply;

    static const EipByte name_of_service[16] = "Communications";

    try
    {
        out.put16( 1 );
        out.put16( kCipItemIdListServiceResponse );
        out.put16( 20 );    // length of following command specific data is fixed
        out.put16( 1 );     // protocol version
        out.put16( kCapabilityFlagsCipTcp | kCapabilityFlagsCipUdpClass0or1 ); // capability_flags
        out.append( name_of_service, 16 );

        return out.data() - aReply.data();
    }
    catch( std::exception e )
    {
        return -1;
    }
}


int HandleReceivedExplictTcpData( int socket, BufReader aCommand, BufWriter aReply )
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

    // Adjust locally for fixed length encapsulation header which is both
    // consumed in the command and reserved in the reply.
    BufReader   command = aCommand + ENCAPSULATION_HEADER_LENGTH;
    BufWriter   reply = aReply + ENCAPSULATION_HEADER_LENGTH;

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
                    encap.status = -result;
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
        BufReader aCommand, BufWriter aReply, bool isUnicast )
{
    CIPSTER_ASSERT( aReply.size() >= ENCAPSULATION_HEADER_LENGTH ); // caller bug

    if( aCommand.size() < ENCAPSULATION_HEADER_LENGTH )
    {
        CIPSTER_TRACE_ERR( "%s: aCommand.size too small\n", __func__ );
        return -1;
    }

    EncapsulationData encap;

    int result = encap.DeserializeEncap( aCommand );

    if( result != ENCAPSULATION_HEADER_LENGTH )
    {
        CIPSTER_TRACE_ERR( "%s: unable to DeserializeEncap, result=%d\n", __func__, result );
        return -1;
    }

    if( encap.command_code == kEncapsulationCommandNoOperation )
    {
        CIPSTER_TRACE_INFO( "%s: NOP ignored\n",  __func__ );
        return -1;
    }

    CIPSTER_TRACE_INFO( "%s: aCommand .command_code:%d .size:%d  aReply.size:%d\n",
        __func__, encap.command_code, (int) aCommand.size(), (int) aReply.size() );

    if( encap.status )  // all commands have 0 in status, else ignore
    {
        CIPSTER_TRACE_ERR( "%s: encap.status != 0\n", __func__ );
        return -1;
    }

    encap.data_length = 0;  // aCommand.size() has our length, establish default for reply

    // Adjust locally for encapsulation header which is both consumed in the
    // the command and reserved in the reply.
    BufReader   command = aCommand + ENCAPSULATION_HEADER_LENGTH;
    BufWriter   reply = aReply + ENCAPSULATION_HEADER_LENGTH;

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
        result = -1;
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

    CIPSTER_TRACE_INFO( "%s: ret:%d\n", __func__, result );

    return result;
}


int EncapsulationData::DeserializeEncap( BufReader aCommand )
{
    BufReader in = aCommand;

    command_code = in.get16();
    data_length  = in.get16();

    session_handle = in.get32();

    status = in.get32();

    for( int i=0; i<8;  ++i )
        sender_context[i] = *in++;

    options = in.get32();

    int byte_count = in.data() - aCommand.data();

    return byte_count;
}


int EncapsulationData::SerializeEncap( BufWriter aDst )
{
    BufWriter out = aDst;

    out.put16( command_code );
    out.put16( data_length );
    out.put32( session_handle );
    out.put32( status );

    out.append( (EipByte*) sender_context, 8 );

    out.put32( options );

    return out.data() - aDst.data();
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
                        g_delayed_messages[i].Payload() );

                g_delayed_messages[i].socket = -1;
            }
        }
    }
}
