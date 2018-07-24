/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/
#ifndef CIPSTER_ENCAP_H_
#define CIPSTER_ENCAP_H_

//#include <string>
#include "typedefs.h"
#include "../cip/cipcommon.h"


/** @file encap.h
 * @brief This file contains the public interface of the encapsulation layer
 */

/**  @defgroup ENCAP CIPster Ethernet encapsulation layer
 * The Ethernet encapsulation layer handles provides the abstraction between the Ethernet and the CIP layer.
 */


#define ENCAPSULATION_HEADER_LENGTH         24
#define ENCAPSULATION_HEADER_LENGTHX        (24+6)  // SendRRData & SendUnitData

static const int kSupportedProtocolVersion  = 1;        ///< Supported Encapsulation protocol version
static const int kEthernet_IP_Port          = 0xAF12;   ///< Ethernet/IP standard port (44818)


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


/// Encapsulation commands
enum EncapsulationCommand
{
    kEncapCmdNoOperation        = 0x00, ///< only allowed for TCP
    kEncapCmdListServices       = 0x04, ///< allowed for both UDP and TCP
    kEncapCmdListIdentity       = 0x63, ///< allowed for both UDP and TCP
    kEncapCmdListInterfaces     = 0x64, ///< optional, allowed for both UDP and TCP
    kEncapCmdRegisterSession    = 0x65, ///< only allowed for TCP
    kEncapCmdUnregisterSession  = 0x66, ///< only allowed for TCP
    kEncapCmdSendRRData         = 0x6F, ///< only allowed for TCP
    kEncapCmdSendUnitData       = 0x70, ///< only allowed for TCP, no reply
};


/**
 * Class Encapsulation
 */
class Encapsulation : public Serializeable
{
public:
    Encapsulation() :
        command( kEncapCmdNoOperation ), // better than random, which could fire IsBigHdr()
        payload( 0 )
    {}

    Encapsulation(
            EncapsulationCommand aCommand,
            CipUdint aServerSessionHandle = 0,
            Cpf* aPayload = 0
            ) :
        command( aCommand ),
        length( 0 ),
        session_handle( aServerSessionHandle ),
        status( 0 ),
        sender_context(),
        options( 0 ),
        interface_handle( 0 ),
        timeout( 0 ),
        payload( aPayload )
    {
    }

    /** @ingroup ENCAP
     * @brief Initialize the encapsulation layer.
     */
    static void Init();

    /** @ingroup ENCAP
     * @brief Shutdown the encapsulation layer.
     *
     * This means that all open sessions including their sockets are closed.
     */
    static void ShutDown();

    /**
     * Function EnsuredTcpRecv
     * may be called using a blocking TCP socket to read a certain number of bytes.
     * It will block until requested byte count is received or it will return
     * something other than the requested count if there is an error or the other
     * end of the TCP connection closes its socket.
     * @return int - the requested number of bytes or 0 if the other end closes or -1 if error.
     */
    static int EnsuredTcpRecv( int aSocket, EipByte* aDest, int aByteCount );

    /**
     * Function ReceiveTcpMsg
     * reads an Encapsulation message in on a TCP socket into caller's aMessage buffer.
     * @param aSocket is the open socket to read from, data is know to be available.
     * @param aMessage is where to put the Encapsulation header and payload.
     * @return int - the number of bytes in the total message or -1 if error.
     */
    static int ReceiveTcpMsg( int aSocket, BufWriter aMessage );

    /**
     * Function HandleReceivedExplicitUdpData
     * notifies the encapsulation layer that an explicit message has been
     * received via UDP.
     *
     * @param socket BSD socket from which data is received.
     * @param from_address remote address from which the data is received.
     * @param aCommand received data buffer pointing just past the encapsulation header and its length.
     * @param aReply where to put reply and tells its maximum length.
     * @param isUnicast true if unicast, else false.
     * @return int - byte count of reply that need to be sent back
     */
    static int HandleReceivedExplictUdpData( int socket, const sockaddr_in* from_address,
        BufReader aCommand,  BufWriter aReply, bool isUnicast );

    /**
     * Function HandleReceivedExplicitTcpData
     * notifies the encapsulation layer that an explicit message has been
     * received via TCP.
     *
     * @param socket the BSD socket from which data is received.
     * @param aCommand is the buffer that contains the received data.
     * @param aReply is the buffer that should be used for the reply.
     * @return int - byte count of reply that needs to be sent back
     */
    static int HandleReceivedExplictTcpData( int socket, BufReader aCommand, BufWriter aReply );

    EncapsulationCommand Command() const            { return command; }
    void SetCommand( EncapsulationCommand aCmd )    { command = aCmd; }

    CipUdint    Status() const              { return status; }
    void        SetStatus( CipUdint s )     { status = s; }

    CipUdint    Options() const             { return options; }

    void SetPayloadLength( unsigned aLength )
    {
        // because interface_handle and timeout may be included in some commands,
        // we adjust the caller's notion of aLength to include our two extra
        // fields when they pertain.
        if( IsBigHdr() )
            length = aLength + 6;
        else
            length = aLength;
    }

    unsigned PayloadLength() const
    {
        return IsBigHdr() ? length - 6 : length;
    }

    /// Tell if this header incorporates the interface_handle and timeout fields
    bool IsBigHdr() const
    {
        return( command == kEncapCmdSendRRData || command == kEncapCmdSendUnitData );
    }

    unsigned HeaderLength() const
    {
        return IsBigHdr() ? ENCAPSULATION_HEADER_LENGTHX : ENCAPSULATION_HEADER_LENGTH;
    }

    CipUdint SessionHandle() const          { return session_handle; }
    void SetSessionHandle( CipUdint aHndl)
    {
        CIPSTER_TRACE_INFO( "%s: %d\n", __func__, aHndl );
        session_handle = aHndl;
    }

    /**
     * Function DeserializeEncap
     * grabs fields from serialized encapsulation header into this struct.
     *
     * @param aSrc the received packet
     *
     * @return int - no. bytes consumed, or -1 if error
     */
    int DeserializeEncap( BufReader aSrc );

    //-----<Serializeable>------------------------------------------------------
    int SerializedCount( int aCtl = 0 ) const;
    int Serialize( BufWriter aDst, int aCtl = 0 ) const;
    //-----</Serializeable>-----------------------------------------------------

    CipUint     TimeoutMS() const   { return sender_context[0] | (sender_context[1] << 8); }

protected:

    static int handleReceivedListServicesCommand( BufWriter aReply );

    static int handleReceivedListInterfacesCommand( BufWriter aReply );

    static int handleReceivedListIdentityCommandImmediate( BufWriter aReply );

    static int serializeListIdentityResponse( BufWriter aReply );

    static int handleReceivedListIdentityCommandDelayed( int socket, const sockaddr_in* from_address,
            unsigned aMSecDelay, BufReader aCommand );

    static int registerSession( int socket, BufReader aCommand, BufWriter aReply,
            EncapsulationProtocolErrorCode* aEncapError,
            CipUdint* aSessionHandleResult );


    EncapsulationCommand    command;
    unsigned                length;
    CipUdint                session_handle;
    CipUdint                status;
    CipByte                 sender_context[8];
    CipUdint                options;

    // These are expected only for command == SendRRData or SendUnitData
    CipUdint                interface_handle;
    CipUint                 timeout;

    Cpf*                    payload;
};


class ListIdentity //: public Serializeable
{

public:
    ListIdentity() {}

    ListIdentity( int aIPAddress, CipUint aVendorId, CipUint aDeviceType,
            CipUint aProductCode, CipUint aRevision, CipUint aStatus,
            CipUdint aSerialNum, const std::string& aProductName, CipByte aState ) :
        protocol_ver( kSupportedProtocolVersion ),
        sin_family( AF_INET ),
        sin_port( kEthernet_IP_Port ),
        sin_addr( aIPAddress ),
        sin_zero(),
        vendor_id( aVendorId ),
        device_type( aDeviceType ),
        product_code( aProductCode ),
        revision( aRevision ),
        status( aStatus ),
        serial_num( aSerialNum ),
        product_name( aProductName ),
        state( aState )
    {}

    CipUint                 protocol_ver;
    CipInt                  sin_family;
    CipUint                 sin_port;
    CipUdint                sin_addr;
    CipByte                 sin_zero[8];

    CipUint                 vendor_id;
    CipUint                 device_type;
    CipUint                 product_code;
    CipUint                 revision;
    CipUint                 status;
    CipUdint                serial_num;
    std::string             product_name;
    CipByte                 state;

    int Deserialize( BufReader aReader )
    {
        BufReader in = aReader;

        int cip_id = in.get16();

        if( cip_id != 0xc )
        {
            return -1;
        }

        int length = in.get16();

        protocol_ver = in.get16();

        sin_family   = in.get16BE();
        sin_port     = in.get16BE();
        sin_addr     = in.get32BE();

        for( int i=0; i<8; ++i )
            sin_zero[i] = in.get8();

        vendor_id    = in.get16();
        device_type  = in.get16();
        product_code = in.get16();
        revision     = in.get16();
        status       = in.get16();
        serial_num   = in.get32();
        product_name = in.get_SHORT_STRING();
        state        = in.get8();

        (void) length;

        return in.data() - aReader.data();
    }
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
     * Function CloseSession
     * deletes any session associated with the aSocket and closes the socket connection.
     *
     * @param aSocket the socket of the session to close.
     * @return bool - true if aSocket was found in an open session (in which case), else false.
     */
    static bool CloseSession( int aSocket );

private:
    static int sessions[CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS];
};


//** global variables (public) **

//** public functions **

/** @ingroup ENCAP
 * @brief Handle delayed encapsulation message responses
 *
 * Certain encapsulation message requests require a delayed sending of the response
 * message. This functions checks if messages need to be sent and performs the
 * sending.
 */
void ManageEncapsulationMessages();


#endif // CIPSTER_ENCAP_H_
