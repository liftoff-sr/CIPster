/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/
#include <string.h>
#include <stdlib.h>
#include <cipster_api.h>
#include <cpf.h>
#include <encap.h>
#include <byte_bufs.h>
#include <cipcommon.h>
#include <cipmessagerouter.h>
#include <cipconnectionmanager.h>
#include <cipidentity.h>
#include <ciptcpipinterface.h>


const int kSupportedProtocolVersion = 1;                ///< Supported Encapsulation protocol version


int ServerSessionMgr::sessions[CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS];


/// Capability flags
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
    DelayedMsg() :
        socket( -1 ),
        message_size( 0 )
    {}

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


static DelayedMsg s_delayed_messages[ENCAP_NUMBER_OF_SUPPORTED_DELAYED_ENCAP_MESSAGES];


//   @brief Initializes session list and interface information.
void EncapsulationInit()
{
    int stack_var;

    srand( (unsigned) (uintptr_t) &stack_var );

    ServerSessionMgr::Init();
}


const int kListIdentityDefaultDelayTime = 2000;
const int kListIdentityMinimumDelayTime = 500;

static unsigned determineDelayTimeMSecs( unsigned aMaxMSecs )
{
    /*  The receiver’s delay shall be a random value, in milliseconds,
        between 0 and the MaxResponseDelay specified in the ListIdentity
        request. If the sender specifies a MaxResponseDelay value of 0
        ms, a default value of 2000 ms shall be used by the receiver. If
        the sender specifies a MaxResponseDelay value of 1-500 ms, a
        value of 500 ms shall be used by the receiver. A new random
        value shall be chosen for each request.
    */

    if( aMaxMSecs == 0 )
    {
        aMaxMSecs = kListIdentityDefaultDelayTime;
    }
    else if( aMaxMSecs < kListIdentityMinimumDelayTime )
    {
        // if seed time is between 1 and 500ms inclusive, set it to 500ms
        aMaxMSecs = kListIdentityMinimumDelayTime;
    }

    // Set delay time randomly between 0 and aMaxMSecs, use 64 bit unsigned
    // integer math to scale the aMaxMSecs between 0 and aMaxMSecs.  Without
    // 64 bit math the multiply in this calculation overflows using 32 bit ints.
    // Alternatively could use float or double in the product before coming
    // back to unsigned result.
    unsigned ret = ( uint64_t(aMaxMSecs) * rand() ) / RAND_MAX;

    return ret;
}


EncapsulationProtocolErrorCode ServerSessionMgr::RegisterSession( int aSocket, CipUdint* aSessionHandleResult )
{
    for( int i = 0; i < DIM(sessions); ++i )
    {
        if( sessions[i] == aSocket )
        {
            // The socket has already registered a session. This is not allowed.
            // Return the already assigned session back, the cip spec is
            // not clear about this needs to be tested.
            *aSessionHandleResult = i + 1;
            return  kEncapsulationProtocolInvalidOrUnsupportedCommand;
        }
    }

    for( int i = 0;  i < DIM(sessions);  ++i )
    {
        if( kEipInvalidSocket == sessions[i] )
        {
            sessions[i] = aSocket;
            *aSessionHandleResult = i + 1;
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
        EncapsulationProtocolErrorCode* aEncapError, CipUdint* aSessionHandleResult )
{
    *aEncapError = kEncapsulationProtocolSuccess;

    BufWriter out = aReply;

    int version = aCommand.get16();
    int options = aCommand.get16();

    // Check if requested protocol version is supported and the
    // register session option flag is zero.
    if( version && version <= kSupportedProtocolVersion && !options )
    {
        *aEncapError = ServerSessionMgr::RegisterSession( socket, aSessionHandleResult );
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



void ServerSessionMgr::Init()
{
    // initialize Sessions to invalid == free session
    for( int i = 0; i < DIM(sessions); ++i )
    {
        sessions[i] = kEipInvalidSocket;
    }
}


void ServerSessionMgr::Shutdown()
{
    for( int i = 0; i < DIM(sessions); ++i )
    {
        if( kEipInvalidSocket != sessions[i] )
        {
            CloseSocket( sessions[i] );
            sessions[i] = kEipInvalidSocket;
        }
    }
}


bool ServerSessionMgr::CheckRegisteredSession( CipUdint aSessionHandle )
{
    // a session handle of zero is not legal, also check for that.
    if( aSessionHandle > 0 && aSessionHandle <= CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS )
    {
        if( kEipInvalidSocket != sessions[aSessionHandle - 1] )
        {
            return true;
        }
    }

    return false;
}


void ServerSessionMgr::CloseSession( int aSocket )
{
    for( int i = 0; i < DIM(sessions); ++i )
    {
        if( sessions[i] == aSocket )
        {
            CloseSocket( aSocket );
            sessions[i] = kEipInvalidSocket;
            break;
        }
    }
}


EncapsulationProtocolErrorCode ServerSessionMgr::UnregisterSession( CipUdint aSessionHandle )
{
    if( aSessionHandle && aSessionHandle <= CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS )
    {
        int ndx = aSessionHandle - 1;

        if( kEipInvalidSocket != sessions[ndx] )
        {
            CloseSocket( sessions[ndx] );
            sessions[ndx] = kEipInvalidSocket;
            return kEncapsulationProtocolSuccess;
        }
    }

    // no such session registered
    return kEncapsulationProtocolInvalidSessionHandle;
}


static int encapsulateListIdentyResponseMessage( BufWriter aReply )
{
    BufWriter out = aReply;

    out.put16( 1 );       // Item count: one item
    out.put16( kCipItemIdListIdentityResponse );

    // at this byte offset the real length will be inserted below
    BufWriter id_length = out;

    out += 2;   // skip over deferred id_length slot.

    out.put16( kSupportedProtocolVersion );

    out.put16BE( AF_INET );
    out.put16BE( kOpenerEthernetPort );
    out.put32BE( ntohl( CipTCPIPInterfaceClass::InterfaceConf( 1 ).ip_address ) );

    out.fill( 8 );

    out.put16( vendor_id_ ).put16( device_type_ ).put16( product_code_ );

    out.put8( revision_.major_revision ).put8( revision_.minor_revision );

    out.put16( status_ );
    out.put32( serial_number_ );

    out.put_SHORT_STRING( product_name_, false );

    out.put8( 0xff );      // optional STATE, not supported indicated by 0xff.

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
        if( kEipInvalidSocket == s_delayed_messages[i].socket )
        {
            delayed = &s_delayed_messages[i];
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
    CIPSTER_ASSERT( aReply.capacity() >= ENCAPSULATION_HEADER_LENGTH ); // caller bug

    int result = -1;

    if( aCommand.size() < ENCAPSULATION_HEADER_LENGTH )
        return -1;

    EncapsulationData encap;

    if( encap.DeserializeEncap( aCommand ) != ENCAPSULATION_HEADER_LENGTH )
        return -1;

    if( encap.Command() == kEncapsulationCommandNoOperation )
    {
        /*  Either an originator or a target may send a NOP command. No reply
            shall be generated by this command. The data portion of the command
            shall be from 0 to 65511 bytes long. The receiver shall ignore any
            data that is contained in the message. A NOP command does not
            require that a session be established.
        */
        return -1;
    }

    if( encap.Status() )  // all commands have 0 in status, else ignore
        return -1;

    encap.SetLength( 0 );  // aCommand.Length() has our length, establish default for reply

    // Adjust locally for fixed length encapsulation header which is both
    // consumed in the command and reserved in the reply.
    BufReader   command = aCommand + ENCAPSULATION_HEADER_LENGTH;
    BufWriter   reply = aReply + ENCAPSULATION_HEADER_LENGTH;

    // most of these functions need a reply to be sent
    switch( encap.Command() )
    {
    case kEncapsulationCommandListServices:
        if( !encap.Options() )
            result = handleReceivedListServicesCommand( reply );
        break;

    case kEncapsulationCommandListIdentity:
        if( !encap.Options() )
            result = handleReceivedListIdentityCommandImmediate( reply );
        break;

    case kEncapsulationCommandListInterfaces:
        if( !encap.Options() )
            result = handleReceivedListInterfacesCommand( reply );
        break;

    case kEncapsulationCommandRegisterSession:
        if( !encap.Options() )
        {
            EncapsulationProtocolErrorCode status;
            CipUdint    sh = 0;

            result = registerSession( socket, command, reply, &status, &sh );
            encap.SetSessionHandle( sh );
            encap.SetStatus( status );
        }
        break;

    case kEncapsulationCommandUnregisterSession:
        encap.SetStatus( ServerSessionMgr::UnregisterSession( encap.SessionHandle() ) );
        result = 0;
        break;

    case kEncapsulationCommandSendRequestReplyData:
        if( !encap.Options() && aCommand.size() >= 6 )
        {
            if( ServerSessionMgr::CheckRegisteredSession( encap.SessionHandle() ) )
            {
                CipCommonPacketFormatData cpfd( socket );

                result = cpfd.NotifyCommonPacketFormat(
                    command + 6,    // skip null interface handle + timeout value
                                    // which follow encap header.
                    reply           // again, this is past encap header
                    );

                if( result < 0 )
                {
                    encap.SetStatus( -result );

                    // reply with encap.Status() != 0, and only send encap header.
                    result = 0;
                }
            }
            else    // received a packet with non registered session handle
            {
                encap.SetStatus( kEncapsulationProtocolInvalidSessionHandle );
                result = 0;
            }
        }
        break;

    case kEncapsulationCommandSendUnitData:
        if( !encap.Options() && command.size() >= 6 )
        {
            if( ServerSessionMgr::CheckRegisteredSession( encap.SessionHandle() ) )
            {
                CipCommonPacketFormatData   cpfd( socket );

                result = cpfd.NotifyConnectedCommonPacketFormat(
                            command + 6, // skip null interface handle + timeout value
                            reply
                            );
            }
            else    // received a packet with non registered session handle
            {
                encap.SetStatus( kEncapsulationProtocolInvalidSessionHandle );
                result = 0;
            }
        }
        break;

    default:
        // Vol2 2-3.2
        encap.SetStatus( kEncapsulationProtocolInvalidOrUnsupportedCommand );
        result = 0;
        break;
    }

    if( result >= 0 )
    {
        encap.SetLength( result );
        encap.Serialize( aReply );
        result += ENCAPSULATION_HEADER_LENGTH;
    }

    return result;
}


int HandleReceivedExplictUdpData( int socket, const sockaddr_in* from_address,
        BufReader aCommand, BufWriter aReply, bool isUnicast )
{
    CIPSTER_ASSERT( aReply.capacity() >= ENCAPSULATION_HEADER_LENGTH ); // caller bug

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

    if( encap.Command() == kEncapsulationCommandNoOperation )
    {
        CIPSTER_TRACE_INFO( "%s: NOP ignored\n",  __func__ );
        return -1;
    }

    CIPSTER_TRACE_INFO( "%s: aCommand .Command():%d .size:%d  aReply.capacity:%d\n",
        __func__, encap.Command(), (int) aCommand.size(), (int) aReply.capacity() );

    if( encap.Status() )  // all commands have 0 in status, else ignore
    {
        CIPSTER_TRACE_ERR( "%s: encap.Status() != 0\n", __func__ );
        return -1;
    }

    encap.SetLength( 0 );  // aCommand.size() has our length, establish default for reply

    // Adjust locally for encapsulation header which is both consumed in the
    // the command and reserved in the reply.
    //BufReader   command = aCommand + ENCAPSULATION_HEADER_LENGTH;
    BufWriter   reply = aReply + ENCAPSULATION_HEADER_LENGTH;

    switch( encap.Command() )
    {
    case kEncapsulationCommandListServices:
        if( !encap.Options() )
            result = handleReceivedListServicesCommand( reply );
        break;

    case kEncapsulationCommandListIdentity:
        if( isUnicast )
        {
            result = handleReceivedListIdentityCommandImmediate( reply );
        }
        else
        {
            unsigned maxMSecs = encap.TimeoutMS();
            unsigned timeoutMSecs = determineDelayTimeMSecs( maxMSecs );

            result = handleReceivedListIdentityCommandDelayed( socket, from_address,
                        timeoutMSecs, aCommand );
        }
        break;

    case kEncapsulationCommandListInterfaces:
        if( !encap.Options() )
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
        encap.SetStatus( kEncapsulationProtocolInvalidOrUnsupportedCommand );
        break;
    }

    // if result is greater than 0, then data has to be sent.  If it is precisely
    // zero, then we arrived here from a handleReceivedListIdentityCommandDelayed()
    // call and should not send now but will send later.
    if( result > 0 )
    {
        encap.SetLength( result );
        encap.Serialize( aReply );
        result += ENCAPSULATION_HEADER_LENGTH;
    }

    CIPSTER_TRACE_INFO( "%s: ret:%d\n", __func__, result );

    return result;
}


int EncapsulationData::DeserializeEncap( BufReader aCommand )
{
    BufReader in = aCommand;

    command = (EncapsulationCommand) in.get16();
    length  = in.get16();

    session_handle = in.get32();

    status = in.get32();

    for( int i=0; i<8;  ++i )
        sender_context[i] = in.get8();

    options = in.get32();

    int byte_count = in.data() - aCommand.data();

    return byte_count;
}


int EncapsulationData::Serialize( BufWriter aDst )
{
    BufWriter out = aDst;

    out.put16( command )
    .put16( length )
    .put32( session_handle )
    .put32( status )
    .append( (EipByte*) sender_context, 8 )
    .put32( options );

    return out.data() - aDst.data();
}


void EncapsulationShutDown()
{
    ServerSessionMgr::Shutdown();
}


void ManageEncapsulationMessages()
{
    for( unsigned i = 0; i < ENCAP_NUMBER_OF_SUPPORTED_DELAYED_ENCAP_MESSAGES; i++ )
    {
        if( kEipInvalidSocket != s_delayed_messages[i].socket )
        {
            s_delayed_messages[i].time_out_usecs -= kOpenerTimerTickInMicroSeconds;

            if( s_delayed_messages[i].time_out_usecs < 0 )
            {
                // If delay is reached or passed, send the UDP message
                SendUdpData( &s_delayed_messages[i].receiver,
                        s_delayed_messages[i].socket,
                        s_delayed_messages[i].Payload() );

                s_delayed_messages[i].socket = -1;
            }
        }
    }
}
