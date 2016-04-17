/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#include <string.h>
#include <stdlib.h>
#include "opener_api.h"
#include "cpf.h"
#include "encap.h"
#include "endianconv.h"
#include "cipcommon.h"
#include "cipmessagerouter.h"
#include "cipconnectionmanager.h"
#include "cipidentity.h"

//Identity data from cipidentity.c
extern EipUint16 vendor_id_;
extern EipUint16 device_type_;
extern EipUint16 product_code_;
extern CipRevision revision_;
extern EipUint16 status_;
extern EipUint32 serial_number_;
extern CipShortString product_name_;

//ip address data taken from TCPIPInterfaceObject
extern CipTcpIpNetworkInterfaceConfiguration interface_configuration_;

const int kSupportedProtocolVersion = 1;                    //*< Supported Encapsulation protocol version

const int kEncapsulationHeaderOptionsFlag = 0x00;           //*< Mask of which options are supported as of the current CIP specs no other option value as 0 should be supported.

const int kEncapsulationHeaderSessionHandlePosition = 4;    //*< the position of the session handle within the encapsulation header

const int kListIdentityDefaultDelayTime = 2000;             //*< Default delay time for List Identity response
const int kListIdentityMinimumDelayTime = 500;              //*< Minimum delay time for List Identity response

typedef enum
{
    kSessionStatusInvalid = -1,
    kSessionStatusValid = 0
} SessionStatus;

const int kSenderContextSize = 8;    //*< size of sender context in encapsulation header

/// @brief definition of known encapsulation commands
typedef enum
{
    kEncapsulationCommandNoOperation = 0x0000,          //*< only allowed for TCP
    kEncapsulationCommandListServices = 0x0004,         //*< allowed for both UDP and TCP
    kEncapsulationCommandListIdentity = 0x0063,         //*< allowed for both UDP and TCP
    kEncapsulationCommandListInterfaces = 0x0064,       //*< optional, allowed for both UDP and TCP
    kEncapsulationCommandRegisterSession = 0x0065,      //*< only allowed for TCP
    kEncapsulationCommandUnregisterSession = 0x0066,    //*< only allowed for TCP
    kEncapsulationCommandSendRequestReplyData = 0x006F, //*< only allowed for TCP
    kEncapsulationCommandSendUnitData = 0x0070          //*< only allowed for TCP
} EncapsulationCommand;

/// @brief definition of capability flags
typedef enum
{
    kCapabilityFlagsCipTcp = 0x0020,
    kCapabilityFlagsCipUdpClass0or1 = 0x0100
} CapabilityFlags;

#define ENCAP_NUMBER_OF_SUPPORTED_DELAYED_ENCAP_MESSAGES    2 //*< According to EIP spec at least 2 delayed message requests should be supported

#define ENCAP_MAX_DELAYED_ENCAP_MESSAGE_SIZE                ( ENCAPSULATION_HEADER_LENGTH + 39 + \
                                                              sizeof(OPENER_DEVICE_NAME) )                   // currently we only have the size of an encapsulation message

// Encapsulation layer data

/// @brief Delayed Encapsulation Message structure
typedef struct
{
    EipInt32 time_out;  //*< time out in milli seconds
    int socket;         //*< associated socket
    struct sockaddr_in receiver;
    EipByte message[ENCAP_MAX_DELAYED_ENCAP_MESSAGE_SIZE];
    unsigned message_size;
} DelayedEncapsulationMessage;

EncapsulationInterfaceInformation g_interface_information;

int g_registered_sessions[OPENER_NUMBER_OF_SUPPORTED_SESSIONS];

DelayedEncapsulationMessage g_delayed_encapsulation_messages[
    ENCAP_NUMBER_OF_SUPPORTED_DELAYED_ENCAP_MESSAGES];

//** private functions **
void HandleReceivedListServicesCommand( EncapsulationData* receive_data );

void HandleReceivedListInterfacesCommand( EncapsulationData* receive_data );

void HandleReceivedListIdentityCommandTcp( EncapsulationData* receive_data );

void HandleReceivedListIdentityCommandUdp( int socket,
        struct sockaddr_in* from_address,
        EncapsulationData* receive_data );

void HandleReceivedRegisterSessionCommand( int socket,
        EncapsulationData* receive_data );

EipStatus HandleReceivedUnregisterSessionCommand( EncapsulationData* receive_data );

EipStatus HandleReceivedSendUnitDataCommand( EncapsulationData* receive_data );

EipStatus HandleReceivedSendRequestResponseDataCommand( EncapsulationData* receive_data );

int GetFreeSessionIndex();

EipInt16 CreateEncapsulationStructure( EipUint8* receive_buffer,
        int receive_buffer_length,
        EncapsulationData* encapsulation_data );

SessionStatus CheckRegisteredSessions( EncapsulationData* receive_data );

int EncapsulateData( const EncapsulationData* const send_data );

void DetermineDelayTime( EipByte* buffer_start,
        DelayedEncapsulationMessage* delayed_message_buffer );

int EncapsulateListIdentyResponseMessage( EipByte* communication_buffer );

//   @brief Initializes session list and interface information.
void EncapsulationInit()
{
    DetermineEndianess();

    /*initialize random numbers for random delayed response message generation
     * we use the ip address as seed as suggested in the spec */
    srand( interface_configuration_.ip_address );

    // initialize Sessions to invalid == free session
    for( unsigned i = 0; i < OPENER_NUMBER_OF_SUPPORTED_SESSIONS; i++ )
    {
        g_registered_sessions[i] = kEipInvalidSocket;
    }

    for( unsigned i = 0; i < ENCAP_NUMBER_OF_SUPPORTED_DELAYED_ENCAP_MESSAGES; i++ )
    {
        g_delayed_encapsulation_messages[i].socket = -1;
    }

    //TODO make the interface information configurable
    // initialize interface information
    g_interface_information.type_code = kCipItemIdListServiceResponse;
    g_interface_information.length = sizeof(g_interface_information);
    g_interface_information.encapsulation_protocol_version = 1;
    g_interface_information.capability_flags = kCapabilityFlagsCipTcp
                                               | kCapabilityFlagsCipUdpClass0or1;

    strcpy( (char*) g_interface_information.name_of_service, "Communications" );
}


int HandleReceivedExplictTcpData( int socket, EipUint8* buffer,
        unsigned length, int* remaining_bytes )
{
    int return_value = 0;
    EncapsulationData encapsulation_data;

    // eat the encapsulation header
    // the structure contains a pointer to the encapsulated data
    // returns how many bytes are left after the encapsulated data
    *remaining_bytes = CreateEncapsulationStructure( buffer, length,
            &encapsulation_data );

    if( kEncapsulationHeaderOptionsFlag == encapsulation_data.options ) //TODO generate appropriate error response
    {
        if( *remaining_bytes >= 0 )                                     // check if the message is corrupt: header size + claimed payload size > than what we actually received
        {
            // full package or more received
            encapsulation_data.status = kEncapsulationProtocolSuccess;
            return_value = kEipStatusOkSend;

            // most of these functions need a reply to be send
            switch( encapsulation_data.command_code )
            {
            case kEncapsulationCommandNoOperation:
                // NOP needs no reply and does nothing
                return_value = kEipStatusOk;
                break;

            case kEncapsulationCommandListServices:
                HandleReceivedListServicesCommand( &encapsulation_data );
                break;

            case kEncapsulationCommandListIdentity:
                HandleReceivedListIdentityCommandTcp( &encapsulation_data );
                break;

            case kEncapsulationCommandListInterfaces:
                HandleReceivedListInterfacesCommand( &encapsulation_data );
                break;

            case kEncapsulationCommandRegisterSession:
                HandleReceivedRegisterSessionCommand( socket, &encapsulation_data );
                break;

            case kEncapsulationCommandUnregisterSession:
                return_value = HandleReceivedUnregisterSessionCommand(
                        &encapsulation_data );
                break;

            case kEncapsulationCommandSendRequestReplyData:
                return_value = HandleReceivedSendRequestResponseDataCommand(
                        &encapsulation_data );
                break;

            case kEncapsulationCommandSendUnitData:
                return_value = HandleReceivedSendUnitDataCommand( &encapsulation_data );
                break;

            default:
                encapsulation_data.status = kEncapsulationProtocolInvalidCommand;
                encapsulation_data.data_length = 0;
                break;
            }

            // if nRetVal is greater than 0, then data has to be sent
            if( kEipStatusOk < return_value )
            {
                return_value = EncapsulateData( &encapsulation_data );
            }
        }
    }

    return return_value;
}


int HandleReceivedExplictUdpData( int socket, struct sockaddr_in* from_address,
        EipUint8* buffer, unsigned buffer_length,
        int* number_of_remaining_bytes, int unicast )
{
    int     ret = 0;
    bool    must_send = false;

    EncapsulationData encapsulation_data;

    // eat the encapsulation header
    // the structure contains a pointer to the encapsulated data
    // returns how many bytes are left after the encapsulated data
    *number_of_remaining_bytes = CreateEncapsulationStructure(
            buffer, buffer_length, &encapsulation_data );

    if( kEncapsulationHeaderOptionsFlag == encapsulation_data.options ) //TODO generate appropriate error response
    {
        if( *number_of_remaining_bytes >= 0 )                           // check if the message is corrupt: header size + claimed payload size > than what we actually received
        {
            // full package or more received
            encapsulation_data.status = kEncapsulationProtocolSuccess;
            must_send = true;

            // most of these functions need a reply to be send
            switch( encapsulation_data.command_code )
            {
            case kEncapsulationCommandListServices:
                HandleReceivedListServicesCommand( &encapsulation_data );
                break;

            case kEncapsulationCommandListIdentity:

                if( unicast == true )
                {
                    HandleReceivedListIdentityCommandTcp( &encapsulation_data );
                }
                else
                {
                    HandleReceivedListIdentityCommandUdp( socket, from_address,
                            &encapsulation_data );

                    must_send = false;
                } // as the response has to be delayed do not send it now

                break;

            case kEncapsulationCommandListInterfaces:
                HandleReceivedListInterfacesCommand( &encapsulation_data );
                break;

            // The following commands are not to be sent via UDP
            case kEncapsulationCommandNoOperation:
            case kEncapsulationCommandRegisterSession:
            case kEncapsulationCommandUnregisterSession:
            case kEncapsulationCommandSendRequestReplyData:
            case kEncapsulationCommandSendUnitData:
            default:
                encapsulation_data.status = kEncapsulationProtocolInvalidCommand;
                encapsulation_data.data_length = 0;
                break;
            }

            // if nRetVal is greater than 0, then data has to be sent
            if( must_send )
            {
                ret = EncapsulateData( &encapsulation_data );
            }
        }
    }

    return ret;
}


int EncapsulateData( const EncapsulationData* const send_data )
{
    EipUint8* communcation_buffer = send_data->buf_start + 2;

    AddIntToMessage( send_data->data_length, &communcation_buffer );

    //the CommBuf should already contain the correct session handle
    communcation_buffer += 4;

    AddDintToMessage( send_data->status, &communcation_buffer );

    //the CommBuf should already contain the correct sender context
    //the CommBuf should already contain the correct options value

    return ENCAPSULATION_HEADER_LENGTH + send_data->data_length;
}


/** @brief generate reply with "Communications Services" + compatibility Flags.
 *  @param receive_data pointer to structure with received data
 */
void HandleReceivedListServicesCommand( EncapsulationData* receive_data )
{
    EipUint8* buf = receive_data->buf_pos;

    receive_data->data_length = g_interface_information.length + 2;

    // copy Interface data to msg for sending
    AddIntToMessage( 1, &buf );

    AddIntToMessage( g_interface_information.type_code, &buf );

    AddIntToMessage( (EipUint16) (g_interface_information.length - 4), &buf );

    AddIntToMessage( g_interface_information.encapsulation_protocol_version, &buf );

    AddIntToMessage( g_interface_information.capability_flags, &buf );

    memcpy( buf, g_interface_information.name_of_service,
            sizeof(g_interface_information.name_of_service) );
}


void HandleReceivedListInterfacesCommand( EncapsulationData* receive_data )
{
    EipUint8* buf = receive_data->buf_pos;

    receive_data->data_length = 2;
    AddIntToMessage( 0x0000, &buf ); // copy Interface data to msg for sending
}


void HandleReceivedListIdentityCommandTcp( EncapsulationData* receive_data )
{
    receive_data->data_length = EncapsulateListIdentyResponseMessage(
            receive_data->buf_pos );
}


void HandleReceivedListIdentityCommandUdp( int socket,
        struct sockaddr_in* from_address,
        EncapsulationData* receive_data )
{
    DelayedEncapsulationMessage* delayed_message_buffer = NULL;

    for( unsigned i = 0; i < ENCAP_NUMBER_OF_SUPPORTED_DELAYED_ENCAP_MESSAGES; i++ )
    {
        if( kEipInvalidSocket == g_delayed_encapsulation_messages[i].socket )
        {
            delayed_message_buffer = &(g_delayed_encapsulation_messages[i]);
            break;
        }
    }

    if( NULL != delayed_message_buffer )
    {
        delayed_message_buffer->socket = socket;

        memcpy( (&delayed_message_buffer->receiver), from_address,
                sizeof(struct sockaddr_in) );

        DetermineDelayTime( receive_data->buf_start,
                delayed_message_buffer );

        memcpy( &(delayed_message_buffer->message[0]),
                receive_data->buf_start,
                ENCAPSULATION_HEADER_LENGTH );

        delayed_message_buffer->message_size = EncapsulateListIdentyResponseMessage(
                &(delayed_message_buffer->message[ENCAPSULATION_HEADER_LENGTH]) );

        EipUint8* buf = delayed_message_buffer->message + 2;

        AddIntToMessage( delayed_message_buffer->message_size,
                &buf );
        delayed_message_buffer->message_size += ENCAPSULATION_HEADER_LENGTH;
    }
}


int EncapsulateListIdentyResponseMessage( EipByte* communication_buffer )
{
    EipUint8* runner = communication_buffer;

    AddIntToMessage( 1, &runner );  // Item count: one item
    AddIntToMessage( kCipItemIdListIdentityResponse, &runner );

    EipByte* id_length_buffer = runner;
    runner += 2;    //at this place the real length will be inserted below

    AddIntToMessage( kSupportedProtocolVersion, &runner );

    EncapsulateIpAddress( htons( kOpenerEthernetPort ),
            interface_configuration_.ip_address,
            &runner );

    memset( runner, 0, 8 );
    runner += 8;

    AddIntToMessage( vendor_id_, &runner );
    AddIntToMessage( device_type_, &runner );
    AddIntToMessage( product_code_, &runner );

    *(runner)++ = revision_.major_revision;
    *(runner)++ = revision_.minor_revision;
    AddIntToMessage( status_, &runner );
    AddDintToMessage( serial_number_, &runner );

    *runner++ = (unsigned char) product_name_.length;

    memcpy( runner, product_name_.string, product_name_.length );
    runner += product_name_.length;

    *runner++ = 0xFF;

    AddIntToMessage( runner - id_length_buffer - 2,
            &id_length_buffer );      // the -2 is for not counting the length field

    return runner - communication_buffer;
}


void DetermineDelayTime( EipByte* buffer_start,
        DelayedEncapsulationMessage* delayed_message_buffer )
{
    buffer_start += 12; // start of the sender context
    EipUint16 maximum_delay_time = GetIntFromMessage( &buffer_start );

    if( 0 == maximum_delay_time )
    {
        maximum_delay_time = kListIdentityDefaultDelayTime;
    }
    else if( kListIdentityMinimumDelayTime > maximum_delay_time )    // if maximum_delay_time is between 1 and 500ms set it to 500ms
    {
        maximum_delay_time = kListIdentityMinimumDelayTime;
    }

    delayed_message_buffer->time_out = ( maximum_delay_time * rand() ) / RAND_MAX; // Sets delay time between 0 and maximum_delay_time
}


/* @brief Check supported protocol, generate session handle, send replay back to originator.
 * @param socket Socket this request is associated to. Needed for double register check
 * @param receive_data Pointer to received data with request/response.
 */
void HandleReceivedRegisterSessionCommand( int socket, EncapsulationData* receive_data )
{
    int session_index = 0;

    EipUint16 protocol_version = GetIntFromMessage( &receive_data->buf_pos );

    EipUint16 nOptionFlag = GetIntFromMessage( &receive_data->buf_pos );

    // check if requested protocol version is supported and the register session option flag is zero
    if( (0 < protocol_version) && (protocol_version <= kSupportedProtocolVersion)
        && (0 == nOptionFlag) ) //Option field should be zero
    {
        // check if the socket has already a session open
        for( int i = 0; i < OPENER_NUMBER_OF_SUPPORTED_SESSIONS; ++i )
        {
            if( g_registered_sessions[i] == socket )
            {
                // The socket has already registered a session this is not allowed.
                // Return the already assigned session back, the cip spec is
                // not clear about this needs to be tested.
                receive_data->session_handle = i + 1;
                receive_data->status = kEncapsulationProtocolInvalidCommand;
                session_index = kSessionStatusInvalid;

                EipUint8* buf = &receive_data->buf_start[kEncapsulationHeaderSessionHandlePosition];

                // EncapsulateData will not update the session handle so we
                // have to do it here by hand
                AddDintToMessage( receive_data->session_handle, &buf );
                break;
            }
        }

        if( kSessionStatusInvalid != session_index )
        {
            session_index = GetFreeSessionIndex();

            if( kSessionStatusInvalid == session_index ) // no more sessions available
            {
                receive_data->status = kEncapsulationProtocolInsufficientMemory;
            }
            else // successful session registered
            {
                g_registered_sessions[session_index] = socket; // store associated socket
                receive_data->session_handle = session_index + 1;
                receive_data->status = kEncapsulationProtocolSuccess;

                EipUint8* buf= &receive_data->buf_start[kEncapsulationHeaderSessionHandlePosition];

                // EncapsulateData will not update the session handle so we
                // have to do it here by hand.
                AddDintToMessage( receive_data->session_handle, &buf );
            }
        }
    }
    else    // protocol not supported
    {
        receive_data->status = kEncapsulationProtocolUnsupportedProtocol;
    }

    receive_data->data_length = 4;
}


/*   INT8 UnregisterSession(struct S_Encapsulation_Data *pa_S_ReceiveData)
 *   close all corresponding TCP connections and delete session handle.
 *      pa_S_ReceiveData pointer to unregister session request with corresponding socket handle.
 */
EipStatus HandleReceivedUnregisterSessionCommand( EncapsulationData* receive_data )
{
    int i;

    if( (0 < receive_data->session_handle)
        && (receive_data->session_handle <= OPENER_NUMBER_OF_SUPPORTED_SESSIONS) )
    {
        i = receive_data->session_handle - 1;

        if( kEipInvalidSocket != g_registered_sessions[i] )
        {
            IApp_CloseSocket_tcp( g_registered_sessions[i] );
            g_registered_sessions[i] = kEipInvalidSocket;
            return kEipStatusOk;
        }
    }

    // no such session registered
    receive_data->data_length = 0;
    receive_data->status = kEncapsulationProtocolInvalidSessionHandle;
    return kEipStatusOkSend;
}


/** @brief Call Connection Manager.
 *  @param receive_data Pointer to structure with data and header information.
 */
EipStatus HandleReceivedSendUnitDataCommand( EncapsulationData* receive_data )
{
    EipInt16    send_size;
    EipStatus   return_value = kEipStatusOkSend;

    if( receive_data->data_length >= 6 )
    {
        // Command specific data UDINT .. Interface Handle, UINT .. Timeout, CPF packets
        // don't use the data yet
        GetDintFromMessage( &receive_data->buf_pos );   // skip over null interface handle
        GetIntFromMessage( &receive_data->buf_pos );    // skip over unused timeout value
        receive_data->data_length -= 6;                 // the rest is in CPF format

        // see if the EIP session is registered
        if( kSessionStatusValid != CheckRegisteredSessions( receive_data ) )
        {
            send_size = NotifyConnectedCommonPacketFormat(
                            receive_data,
                            &receive_data->buf_start[ENCAPSULATION_HEADER_LENGTH] );

            if( 0 < send_size ) // need to send reply
            {
                receive_data->data_length = send_size;
            }
            else
            {
                return_value = kEipStatusError;
            }
        }
        else // received a package with non registered session handle
        {
            receive_data->data_length = 0;
            receive_data->status = kEncapsulationProtocolInvalidSessionHandle;
        }
    }

    return return_value;
}


/** @brief Call UCMM or Message Router if UCMM not implemented.
 *  @param receive_data Pointer to structure with data and header information.
 *  @return status  0 .. success.
 *                  -1 .. error
 */
EipStatus HandleReceivedSendRequestResponseDataCommand( EncapsulationData* receive_data )
{
    EipInt16 send_size;
    EipStatus return_value = kEipStatusOkSend;

    if( receive_data->data_length >= 6 )
    {
        // Command specific data UDINT .. Interface Handle, UINT .. Timeout, CPF packets
        // don't use the data yet
        GetDintFromMessage( &receive_data->buf_pos );   // skip over null interface handle
        GetIntFromMessage( &receive_data->buf_pos );    // skip over unused timeout value

        receive_data->data_length -= 6;                 // the rest is in CPF format

        // see if the EIP session is registered
        if( kSessionStatusValid == CheckRegisteredSessions( receive_data ) )
        {
            send_size = NotifyCommonPacketFormat(
                            receive_data,
                            &receive_data->buf_start[ENCAPSULATION_HEADER_LENGTH] );

            if( send_size >= 0 ) // need to send reply
            {
                receive_data->data_length = send_size;
            }
            else
            {
                return_value = kEipStatusError;
            }
        }
        else // received a package with non registered session handle
        {
            receive_data->data_length = 0;
            receive_data->status = kEncapsulationProtocolInvalidSessionHandle;
        }
    }

    return return_value;
}


/** @brief search for available sessions an return index.
 *  @return return index of free session in anRegisteredSessions.
 *          kInvalidSession .. no free session available
 */
int GetFreeSessionIndex()
{
    for( int session_index = 0;
         session_index < OPENER_NUMBER_OF_SUPPORTED_SESSIONS;
         session_index++ )
    {
        if( kEipInvalidSocket == g_registered_sessions[session_index] )
        {
            return session_index;
        }
    }

    return kSessionStatusInvalid;
}


/** @brief copy data from pa_buf in little endian to host in structure.
 * @param receive_buffer
 * @param length Length of the data in receive_buffer. Might be more than one message
 * @param encapsulation_data	structure to which data shall be copied
 * @return return difference between bytes in pa_buf an data_length
 *          0 .. full package received
 *          >0 .. more than one packet received
 *          <0 .. only fragment of data portion received
 */
EipInt16 CreateEncapsulationStructure( EipUint8* receive_buffer,
        int receive_buffer_length,
        EncapsulationData* encapsulation_data )
{
    encapsulation_data->buf_start = receive_buffer;
    encapsulation_data->command_code = GetIntFromMessage( &receive_buffer );
    encapsulation_data->data_length = GetIntFromMessage( &receive_buffer );
    encapsulation_data->session_handle = GetDintFromMessage( &receive_buffer );
    encapsulation_data->status = GetDintFromMessage( &receive_buffer );

    receive_buffer += kSenderContextSize;
    encapsulation_data->options = GetDintFromMessage( &receive_buffer );
    encapsulation_data->buf_pos = receive_buffer;

    return receive_buffer_length - ENCAPSULATION_HEADER_LENGTH
           - encapsulation_data->data_length;
}


/** @brief Check if received package belongs to registered session.
 *  @param receive_data Received data.
 *  @return 0 .. Session registered
 *          kInvalidSession .. invalid session -> return unsupported command received
 */
SessionStatus CheckRegisteredSessions( EncapsulationData* receive_data )
{
    if( 0 < receive_data->session_handle
        && receive_data->session_handle <= OPENER_NUMBER_OF_SUPPORTED_SESSIONS )
    {
        if( kEipInvalidSocket != g_registered_sessions[receive_data->session_handle - 1] )
        {
            return kSessionStatusValid;
        }
    }

    return kSessionStatusInvalid;
}


void CloseSession( int socket )
{
    for( int i = 0; i < OPENER_NUMBER_OF_SUPPORTED_SESSIONS; ++i )
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
    for( int i = 0; i < OPENER_NUMBER_OF_SUPPORTED_SESSIONS; ++i )
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
        if( kEipInvalidSocket != g_delayed_encapsulation_messages[i].socket )
        {
            g_delayed_encapsulation_messages[i].time_out -=
                kOpenerTimerTickInMilliSeconds;

            if( 0 > g_delayed_encapsulation_messages[i].time_out )
            {
                // If delay is reached or passed, send the UDP message
                SendUdpData( &g_delayed_encapsulation_messages[i].receiver,
                        g_delayed_encapsulation_messages[i].socket,
                        &(g_delayed_encapsulation_messages[i].message[0]),
                        g_delayed_encapsulation_messages[i].message_size );

                g_delayed_encapsulation_messages[i].socket = -1;
            }
        }
    }
}
