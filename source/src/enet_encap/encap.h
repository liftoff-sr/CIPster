/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/
#ifndef CIPSTER_ENCAP_H_
#define CIPSTER_ENCAP_H_

#include "typedefs.h"
#include "../cip/cipcommon.h"

/** @file encap.h
 * @brief This file contains the public interface of the encapsulation layer
 */

/**  @defgroup ENCAP CIPster Ethernet encapsulation layer
 * The Ethernet encapsulation layer handles provides the abstraction between the Ethernet and the CIP layer.
 */


#define ENCAPSULATION_HEADER_LENGTH         24


//* @brief Ethernet/IP standard port
static const int kOpenerEthernetPort = 0xAF12;

/** @brief definition of status codes in encapsulation protocol
 * All other codes are either legacy codes, or reserved for future use
 *
 */
enum EncapsulationProtocolErrorCode
{
    kEncapsulationProtocolSuccess = 0x0000,
    kEncapsulationProtocolInvalidOrUnsupportedCommand = 0x0001,
    kEncapsulationProtocolInsufficientMemory = 0x0002,
    kEncapsulationProtocolIncorrectData = 0x0003,
    kEncapsulationProtocolInvalidSessionHandle = 0x0064,
    kEncapsulationProtocolInvalidLength = 0x0065,
    kEncapsulationProtocolUnsupportedProtocol = 0x0069
};

/// Known encapsulation commands
enum EncapsulationCommand
{
    kEncapsulationCommandNoOperation = 0x0000,          ///< only allowed for TCP
    kEncapsulationCommandListServices = 0x0004,         ///< allowed for both UDP and TCP
    kEncapsulationCommandListIdentity = 0x0063,         ///< allowed for both UDP and TCP
    kEncapsulationCommandListInterfaces = 0x0064,       ///< optional, allowed for both UDP and TCP
    kEncapsulationCommandRegisterSession = 0x0065,      ///< only allowed for TCP
    kEncapsulationCommandUnregisterSession = 0x0066,    ///< only allowed for TCP
    kEncapsulationCommandSendRequestReplyData = 0x006F, ///< only allowed for TCP
    kEncapsulationCommandSendUnitData = 0x0070          ///< only allowed for TCP
};


/**
 * Class EncapsulationData
 */
class EncapsulationData : public Serializeable
{
public:
    EncapsulationData() {}

    EncapsulationData( EncapsulationCommand aCommand, CipUdint aServersSessionHandle = 0 ) :
        command( aCommand ),
        length( 0 ),
        status( 0 ),
        sender_context(),
        options( 0 )
    {
    }

    EncapsulationCommand    Command() const { return command; }

    CipUdint    Status() const              { return status; }
    void        SetStatus( CipUdint s )     { status = s; }

    CipUdint    Options() const             { return options; }

    void SetLength( unsigned aLength )      { length = aLength; }
    unsigned Length() const                 { return length; }

    CipUdint SessionHandle() const          { return session_handle; }
    void SetSessionHandle( CipUdint aHndl)  { session_handle = aHndl; }

    /**
     * Function DeserializeEncap
     * grabs fields from serialized encapsulation header into this struct.
     *
     * @param aSrc the received packet
     *
     * @return int - no. bytes consumed, or -1 if error
     */
    int DeserializeEncap( BufReader aSrc );

    int Serialize( BufWriter aDst );    // override

    CipUint     TimeoutMS() const   { return sender_context[0] | (sender_context[1] << 8); }

protected:
    EncapsulationCommand    command;
    unsigned                length;
    CipUdint                session_handle;
    CipUdint                status;
    CipByte                 sender_context[8];
    CipUdint                options;
};


/**
 * Class ServerSessionMgr
 * manages Ethernet/IP sessions originated by clients to this node, of course
 * in such a role this node is a CIP server.
 */
class ServerSessionMgr
{
public:

    static void Init();

    static void Shutdown();

    /**
     * Function CheckRegisteredSession
     * checks if received package belongs to registered session.
     *
     * @param receive_data Received data.
     * @return bool - true if session is already registered, else false
     */
    static bool CheckRegisteredSession( CipUdint aSessionHandle );

    /// Register a client using the OS socket allocated when we accepted the TCP connection
    static EncapsulationProtocolErrorCode RegisterSession( int aSocket, CipUdint* aSessionHandleResult );

    /**
     * Function UnregisterSession
     * closes all corresponding TCP connections and deletes session handle.
     */
    static EncapsulationProtocolErrorCode UnregisterSession( CipUdint aSessionHandle );

    /** @ingroup CIP_API
     * @brief Inform the encapsulation layer that the remote host has closed the
     * connection.
     *
     * According to the specifications that will clean up and close the session in
     * the encapsulation layer.
     * @param socket_handle the handler to the socket of the closed connection
     */
    static void CloseSession( int aSocket );

private:
    static int sessions[CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS];
};


//** global variables (public) **

//** public functions **
/** @ingroup ENCAP
 * @brief Initialize the encapsulation layer.
 */
void EncapsulationInit();

/** @ingroup ENCAP
 * @brief Shutdown the encapsulation layer.
 *
 * This means that all open sessions including their sockets are closed.
 */
void EncapsulationShutDown();

/** @ingroup ENCAP
 * @brief Handle delayed encapsulation message responses
 *
 * Certain encapsulation message requests require a delayed sending of the response
 * message. This functions checks if messages need to be sent and performs the
 * sending.
 */
void ManageEncapsulationMessages();


#endif // CIPSTER_ENCAP_H_
