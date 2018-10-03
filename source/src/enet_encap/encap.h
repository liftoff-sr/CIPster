/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016, SoftPLC Corporation.
 *
 ******************************************************************************/
#ifndef CIPSTER_ENCAP_H_
#define CIPSTER_ENCAP_H_

//#include <string>
#include "networkhandler.h"
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

const int kSupportedProtocolVersion = 1;        ///< Supported Encapsulation protocol version


/// Ethernet/IP standard port (44818) that all Ethernet/IP devices must support.
const int kEIP_Reserved_Port        = 0xAF12;


/**
 * Enum EncapError
 * is the set of status codes in defined in the encapsulation protocol.
 *
 * All other codes are either legacy codes, or reserved for future use
 */
enum EncapError
{
    kEncapErrorSuccess                      = 0x0000,
    kEncapErrorInvalidOrUnsupportedCommand  = 0x0001,
    kEncapErrorInsufficientMemory           = 0x0002,
    kEncapErrorIncorrectData                = 0x0003,
    kEncapErrorInvalidSessionHandle         = 0x0064,
    kEncapErrorInvalidLength                = 0x0065,
    kEncapErrorUnsupportedProtocol          = 0x0069
};


/// Encapsulation commands
enum EncapCmd
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
 * helps with the Ethernet/IP encapsulation protocol, its header, and its state.
 */
class Encapsulation : public Serializeable
{
public:
    Encapsulation() :
        command( kEncapCmdNoOperation ), // better than random, which could fire IsBigHdr()
        payload( 0 )
    {}

    Encapsulation(
            EncapCmd aCommand,
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

    /**
     * Function Init
     * initializes the encapsulation layer.
     */
    static void Init();

    /**
     * Function ShutDown
     * stops the encapsulation layer.
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
    static int EnsuredTcpRecv( int aSocket, uint8_t* aDest, int aByteCount );

    /**
     * Function ReceiveTcpMsg
     * reads an Encapsulation message in from a TCP socket into caller's aMessage buffer.
     * @param aSocket is the open socket to read from, data is known to be available.
     * @param aMessage is where to put the Encapsulation header and payload.
     * @return int - the number of bytes in the total message or -1 if error.
     */
    static int ReceiveTcpMsg( int aSocket, BufWriter aMessage );

    /**
     * Function HandleReceivedExplicitUdpData
     * notifies the encapsulation layer that an explicit message has been
     * received via UDP.
     *
     * @param aSocket BSD socket from which data is received.
     * @param aSockAddr where to put the remote address from which the data is received.
     * @param aCommand points to a received data buffer pointing just past the
     *   encapsulation header; aCommand includes length.
     * @param aReply where to put a reply and also tells length of this buffer.
     * @param isUnicast true if unicast, else false.
     * @return int - byte count of reply that need to be sent back
     */
    static int HandleReceivedExplicitUdpData( int aSocket, const SockAddr& aSockAddr,
        BufReader aCommand,  BufWriter aReply, bool isUnicast );

    /**
     * Function HandleReceivedExplicitTcpData
     * notifies the encapsulation layer that an explicit message has been
     * received via TCP.
     *
     * @param aSocket the BSD socket from which data is received.
     * @param aCommand is the buffer that contains the received data.
     * @param aReply is the buffer that should be used for the reply.
     * @return int - byte count of reply that needs to be sent back, or -1 if error
     */
    static int HandleReceivedExplicitTcpData( int aSocket,
                    BufReader aCommand, BufWriter aReply );

    EncapCmd Command() const            { return command; }
    void SetCommand( EncapCmd aCmd )    { command = aCmd; }

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
        CIPSTER_TRACE_INFO( "%s(%d)\n", __func__, aHndl );
        session_handle = aHndl;
    }

    /**
     * Function DeserializeEncap
     * grabs fields from serialized encapsulation header into this object.
     *
     * @param aSrc the received packet
     *
     * @return int - no. bytes consumed
     * @throw BufReader's std::range_error if aSrc is too small.
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

    static int handleReceivedListIdentityCommandDelayed(
            int aSocket, const SockAddr& aFromAddress,
            unsigned aMSecDelay, BufReader aCommand );

    /**
     * Function registerSession
     * checks supported protocol, generates a session handle, and serializes a reply.
     * @param aSocket which socket this request is associated with.
     * @param aCommand
     * @param aReply where to put reply
     * @param aEncapError where to put the kEncapError
     * @param aSessionHandleResult where to put the session handle.
     *
     * @return int - num bytes serialized into aReply
     */
    static int registerSession(
            int aSocket,
            BufReader aCommand,
            BufWriter aReply,
            EncapError* aEncapError,
            CipUdint* aSessionHandleResult );

    EncapCmd    command;
    unsigned    length;
    CipUdint    session_handle;
    CipUdint    status;
    uint8_t     sender_context[8];
    CipUdint    options;

    // These are expected only for command == SendRRData or SendUnitData
    CipUdint    interface_handle;
    CipUint     timeout;

    Cpf*        payload;
};


class ListIdentity //: public Serializeable
{

public:
    ListIdentity() :
        protocol_ver( 0 ),
        vendor_id( 0 ),
        device_type( 0 ),
        product_code( 0 ),
        revision(),
        status( 0 ),
        serial_num( 0 ),
        state( 0 )
    {}

    ListIdentity( int aIPAddress, CipUint aVendorId, CipUint aDeviceType,
            CipUint aProductCode, CipUint aRevision, CipUint aStatus,
            CipUdint aSerialNum, const std::string& aProductName, uint8_t aState ) :
        sockaddr( kEIP_Reserved_Port, aIPAddress ),
        protocol_ver( kSupportedProtocolVersion ),
        vendor_id( aVendorId ),
        device_type( aDeviceType ),
        product_code( aProductCode ),
        status( aStatus ),
        serial_num( aSerialNum ),
        product_name( aProductName ),
        state( aState )
    {
        revision.major_revision = aRevision;
        revision.minor_revision = aRevision >> 8;
    }

    CipUint                 protocol_ver;
    SockAddr                sockaddr;
    CipUint                 vendor_id;
    CipUint                 device_type;
    CipUint                 product_code;
    CipRevision             revision;
    CipUint                 status;
    CipUdint                serial_num;
    std::string             product_name;
    uint8_t                 state;

    int DeserializeListIdendityResponse( BufReader aInput );
};


/**
 * Struct EncapSession
 * holds data for an Encapsulation Protocol Session, as well as
 * any TCP connections which have yet to be registered using encapsultation
 * protocol.  That is, an instance of this class holds either:
 *
 * 1) a registered TCP session, or
 * 2) a TCP connection which has yet to be registered as a session.
 *
 * We intend to keep track of all TCP connections using one of these for each.
 * if the client does register a session, we only have to change m_is_registered
 * from false to true.
 */
struct EncapSession
{
    EncapSession()
    {
        Clear();
    }

    void Clear()
    {
        m_socket   = kSocketInvalid;
        m_peeraddr.SetFamily( 0 );
        m_last_activity_usecs = 0;
        m_is_registered = false;
    }

    void Close()
    {
        CloseSocket( m_socket );
        Clear();
    }

    void NoteTcpActivity()
    {
        CIPSTER_TRACE_INFO( "%s[%d]\n", __func__, m_socket );
        m_last_activity_usecs = g_current_usecs;    // last activity
    }

    int         m_socket;
    SockAddr    m_peeraddr;             // peer's IP address, port, etc.
    uint64_t    m_last_activity_usecs;

    bool        m_is_registered;        // false => TCP connection only
                                        // true  => Registered ENIP Session
};


/**
 * Class SessionMgr
 * manages TCP connections and Ethernet/IP encapsulation sessions.
 * <p>
 * Before a TCP connection can graduate to a registered EncapSession, it must
 * be registered as a mere TCP connection here.  Not every TCP connection will become
 * a registered EncapSession, and can remain as a registered TCP connection.
 * A registered TCP connection is stored as an EncapSesssion instance with the
 * m_is_registered bool set to false.
 */
class SessionMgr
{
public:

    static void Init();

    static void Shutdown();

    /**
     * Function RegisterTcpConnection
     * must be called for aSocket before calling RegisterSession()
     */
    static EncapError RegisterTcpConnection( int aSocket, CipUdint* aSessionHandleResult = NULL );

    /// Register a client using the OS socket allocated when we accepted the TCP connection
    static EncapError RegisterSession( int aSocket, CipUdint* aSessionHandleResult );

    /**
     * Function UpdateRegisteredTcpConnection
     * checks if @a aSocket belongs to a registered TCP connection, and if so
     * updates its activity timer.
     *
     * @param aSocket to check
     *
     * @return EncapSession* - non-NULL if aSocket is already registered, else NULL.
     */
    static EncapSession* UpdateRegisteredTcpConnection( int aSocket );

    /**
     * Function CheckRegisteredSession
     * checks if @a aSocket belongs to a registered session.
     *
     * @param aSessionHandle a client provides session handle to verify
     * @param aSocket TCP socket that the registered session request came in on.
     *
     * @return EncapSession* - valid if session is already registered, else NULL if
     *  the session is not active or does not belong to aSocket or is not registered.
     */
    static EncapSession* CheckRegisteredSession( CipUdint aSessionHandle, int aSocket );

    /**
     * Function UnregisterSession
     * closes the corresponding TCP connection and deletes the session info.
     *
     * @param aSessionHandle to close
     * @param aSocket is used to verify that aSessionHandle has matching socket
     *
     * @return EcapError - error if the session is not registered.
     */
    static EncapError UnregisterSession( CipUdint aSessionHandle, int aSocket );

    /**
     * Function CloseBySocket
     * closes the TCP connection (which may also be a registered session)
     * associated with @a aSocket
     *
     * @param aSocket the socket of the TCP connection to close.
     * @return bool - true if aSocket was found
     */
    static bool CloseBySocket( int aSocket );

    /**
     * Function CloseBySessionHandle
     * closes the TCP connection (which may also be a registered session)
     * given by @a aSessionHandle
     *
     * @param aSessionHandle the session of the TCP connection to close.
     * @return bool - true on success, else false
     */
    static bool CloseBySessionHandle( CipUdint aSessionHandle );

    /**
     * Function AgeInactivity
     * scans all open TCP connections, some of which are also registered sessions,
     * and closes those which have been inactive for greater than the
     * CipTCPIPInterfaceInstance::inactivity_timeout_secs setting.
     * @see Vol2 2-5.5.2
     */
    static void AgeInactivity();

    /// inline for speed, translate aSessionHandle into an EncapSession pointer.
    static const EncapSession* GetSession( CipUdint aSessionHandle )
    {
        unsigned ndx = aSessionHandle - 1;

        if( ndx < UDIM(sessions) && sessions[ndx].m_socket != kSocketInvalid )
            return &sessions[ndx];

        return NULL;
    }

private:

    friend int inc_wrap( int index );
    static EncapSession sessions[CIPSTER_NUMBER_OF_SUPPORTED_SESSIONS];
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
