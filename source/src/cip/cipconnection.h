/*******************************************************************************
 * Copyright (c) 2011, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corporation.
 *
 ******************************************************************************/
#ifndef CIPIOCONNECTION_H_
#define CIPIOCONNECTION_H_

#include "../enet_encap/sockaddr.h"

/**
 * @file cipconnection.h
 * CIP I/O Connection implementation
 * =================================
 *
 *
 * I/O Connection Object State Transition Diagram
 * ----------------------------------------------
 * @dot
 *   digraph IOCipConnStateTransition {
 *     A[label="Any State"]
 *     N[label="Non-existent"]
 *     C[label="Configuring"]
 *     E[label="Established"]
 *     W[label="Waiting for Connection ID"]
 *     T[label="Timed Out"]
 *
 *     A->N [label="Delete"]
 *     N->C [label="Create"]
 *     C->C [label="Get/Set/Apply Attribute"]
 *     C->W [label="Apply Attribute"]
 *     W->W [label="Get/Set Attribute"]
 *     C->E [label="Apply Attribute"]
 *     E->E [label="Get/Set/Apply Attribute, Reset, Message Produced/Consumed"]
 *     W->E [label="Apply Attribute"]
 *     E->T [label="Inactivity/Watchdog"]
 *     T->E [label="Reset"]
 *     T->N [label="Delete"]
 *   }
 * @enddot
 *
 */


#include "cipepath.h"
#include "cipclass.h"

/// The port to be used per default for I/O messages on UDP, do not change this,
/// You may change g_data.cc's g_my_io_udp_port instead.
const int kEIP_IoUdpPort = 0x08AE;      // = 2222

class UdpSocket;

/**
 * Enum ConnState
 * is the set of CipConn connection states.
 * @see Vol1 Table 3-4.4.1 CipConn instance attribute_id 1
 */
enum ConnState
{
    kConnStateNonExistent               = 0,
    kConnStateConfiguring               = 1,
    kConnStateWaitingForConnectionId    = 2,    ///< only used in DeviceNet
    kConnStateEstablished               = 3,
    kConnStateTimedOut                  = 4,
    kConnStateDeferredDelete            = 5,    ///< only used in DeviceNet
    kConnStateClosing                   = 6,
};


/**
 * Enum ConnInstanceType
 * is the set of CipConn instance types.
 */
enum ConnInstanceType
{
    kConnInstanceTypeExplicit           = 0x00,
    kConnInstanceTypeIoExclusiveOwner   = 0x01,
    kConnInstanceTypeIoInputOnly        = 0x11,
    kConnInstanceTypeIoListenOnly       = 0x21
};


/**
 * Enum IOConnType
 * is a set values for the bit field named "Connection Type" within the
 * Network Connection Parameter bit collection.  There are only 4 values
 * because the bitfield is only 2 bits wide.
 */
enum IOConnType
{
    kIOConnTypeNull            = 0,
    kIOConnTypeMulticast       = 1,
    kIOConnTypePointToPoint    = 2,
    kIOConnTypeInvalid         = 3,        // reserved
};


enum ConnPriority
{
    kPriorityLow    = 0,
    kPriorityHigh   = 1,
    kPrioritySched  = 2,
    kPriorityUrgent = 3,
};


/**
 * Enum ConnTimeoutMultiplier
 * is the set of legal values for the right column of Vol1 Table 3-5.12
 */
enum ConnTimeoutMultiplier
{
    kConnTimeoutMultiplier4     = 4,
    kConnTimeoutMultiplier8     = 8,
    kConnTimeoutMultiplier16    = 16,
    kConnTimeoutMultiplier32    = 32,
    kConnTimeoutMultiplier64    = 64,
    kConnTimeoutMultiplier128   = 128,
    kConnTimeoutMultiplier256   = 256,
    kConnTimeoutMultiplier512   = 512,
};


/** @ingroup CIP_API
 * @brief Function prototype for handling the closing of connections
 *
 * @param aConn The connection object which is closing the
 * connection
 */
typedef std::function<void (CipConn*)> ConnectionCloseFunction;

/** @ingroup CIP_API
 * @brief Function prototype for handling the timeout of connections
 *
 * @param aConn The connection object which connection timed out
 */
typedef std::function<void (CipConn*)> ConnectionTimeoutFunction;


/**
 * Class NetCnParams
 * holds the bitfields for either a forward open or large forward open's
 * "network connection parameters", and implements details that higher level
 * code should not be burdened with.
 */
class NetCnParams
{
public:

    NetCnParams()
    {
        Clear();
    }

    NetCnParams(
            int aSize,
            bool isFixed = true,
            IOConnType aType = kIOConnTypePointToPoint,
            ConnPriority aPriority = kPriorityLow
            ) :
        bits( 0 ),
        not_large( aSize <= 0x1fff )
    {
        SetConnectionType( aType );
        SetConnectionSize( aSize );
        SetFixed( isFixed );
        SetPriority( aPriority );
    }

    void Clear()
    {
        not_large = true;
        bits = 0;
    }

    bool RedundantOwner() const
    {
        return not_large ? ((bits>>15) & 1) : ((bits>>31) & 1);
    }

    NetCnParams& SetRedundantOwner( bool isRedundantlyOwned )
    {
        if( not_large )
            bits = ( bits & ~( 1 << 15 )) | ( isRedundantlyOwned << 15 );
        else
            bits = ( bits & ~( 1 << 31 )) | ( isRedundantlyOwned << 31 );
        return *this;
    }

    IOConnType ConnectionType() const
    {
        return IOConnType( not_large ? ((bits>>13) & 3) : ((bits>>29) & 3));
    }

    NetCnParams& SetConnectionType( IOConnType aType )
    {
        if( not_large )
            bits = ( bits & ~( 3 << 13 )) | ( aType << 13 );
        else
            bits = ( bits & ~( 3 << 29 )) | ( aType << 29 );
        return *this;
    }

    bool IsNull() const
    {
        return ConnectionType() == kIOConnTypeNull;
    }

    const char* ShowConnectionType() const
    {
        switch( ConnectionType() )
        {
        case kIOConnTypeNull:           return "Null";
        case kIOConnTypeMulticast:      return "Multicast";
        case kIOConnTypePointToPoint:   return "PointToPoint";
        default:                        return "Invalid";
        }
    }

    ConnPriority Priority() const
    {
        return ConnPriority( not_large ? ((bits>>10) & 3) : ((bits>>26) & 3) );
    }

    NetCnParams& SetPriority( ConnPriority aPriority )
    {
        if( not_large )
            bits = ( bits & ~(3 << 10)) | (aPriority << 10);
        else
            bits = ( bits & ~(3 << 26)) | (aPriority <<26 );
        return *this;
    }

    bool IsFixed() const
    {
        return not_large ? ((bits>>9) & 1) : ((bits>>25) & 1);
    }

    NetCnParams& SetFixed( bool isFixed )
    {
        if( not_large )
            bits = ( bits & ~(1 << 9)) | (isFixed << 9);
        else
            bits = ( bits & ~(1 << 25)) | (isFixed << 25);
        return *this;
    }

    unsigned ConnectionSize() const
    {
        return not_large ? (bits & 0x1ff) : (bits & 0xffff);
    }

    NetCnParams& SetConnectionSize( int aSize )
    {
        if( not_large )
            bits = (bits & ~0x1fff) | aSize;
        else
            bits = (bits & ~0xffff) | aSize;
        return *this;
    }

    void Set( EipUint32 aNCP, bool isLarge )
    {
        bits = aNCP;
        not_large = !isLarge;
    }

    int Serialize( BufWriter& aOutput ) const
    {
        if( not_large )
        {
            aOutput.put16( bits );
            return 2;
        }
        else
        {
            aOutput.put32( bits );
            return 4;
        }
    }

    int SerializedCount() const     { return not_large ? 2 : 4; }

private:
    bool        not_large;
    EipUint32   bits;
};


enum ConnTriggerType
{
    kConnTriggerTypeCyclic        = 0,
    kConnTriggerTypeChangeOfState = 1,
    kConnTriggerTypeApplication   = 2,
};


enum ConnTransportClass
{
    kConnTransportClass0 = 0,
    kConnTransportClass1 = 1,
    kConnTransportClass2 = 2,
    kConnTransportClass3 = 3,
};


class TransportTrigger
{
public:
    TransportTrigger()
    {
        Clear();
    }

    TransportTrigger(
            bool isServer,
            ConnTriggerType aTrigger,
            ConnTransportClass aClass
            )
    {
        bits = (isServer << 7) | (aTrigger << 4) | aClass;
    }

    void Clear()
    {
        bits = 0;
    }

    void Set( EipByte aByte )   { bits = aByte; }

    /// Return true if server else false for client.
    bool IsServer() const       { return bits & 0x80; }

    TransportTrigger& SetServer( bool isServer )
    {
        bits = ( bits & ~(1<<7) ) | (isServer << 7);
        return *this;
    }

    ConnTriggerType Trigger() const
    {
        return ConnTriggerType( (bits >> 4) & 7 );
    }


    ConnTransportClass Class() const
    {
        return ConnTransportClass( bits & 15 );
    }

    void Serialize( BufWriter& aOutput ) const
    {
        aOutput.put8( bits );
    }

    CipByte Bits()  const       { return bits; }

protected:
    EipByte     bits;
};


/// Possible values for the watch dog time out action of a connection
enum WatchdogTimeoutAction
{
    kWatchdogTimeoutActionTransitionToTimedOut = 0, ///< , invalid for explicit message connections
    kWatchdogTimeoutActionAutoDelete    = 1,        /**< Default for explicit message connections,
                                                     *  default for I/O connections on EIP */
    kWatchdogTimeoutActionAutoReset     = 2,        ///< Invalid for explicit message connections
    kWatchdogTimeoutActionDeferredDelete = 3        ///< Only valid for DeviceNet, invalid for I/O connections
};


/*
struct LinkConsumer
{
    ConnState state;
    EipUint16       connection_id;
};


struct LinkProducer
{
    ConnState state;
    EipUint16       connection_id;
};


struct LinkObject
{
    LinkConsumer    consumer;
    LinkProducer    producer;
};
*/


/**
 * Class ConnectionPath
 * holds data deserialized from the connection_path portion of a
 * forward_open service request, or set manually in preparation for sending
 * a forward_open request.
 * @see Vol1 3-5.4.1.10  Connection Path
 */
class ConnectionPath : public Serializeable
{
public:
    void Clear()
    {
        port_segs.Clear();
        app_path[0].Clear();
        app_path[1].Clear();
        app_path[2].Clear();
        data_seg.Clear();
    }

    /**
     * Function Deserialize
     * decodes a connection path.  Useful when decoding a forward open request
     * or forward close request.
     *
     * @param aInput provides the encoded segments and should be length limited
     *   so this function knows when to stop consuming input bytes. Construct this
     *   BufReader using the word count which precedes most connection_paths.
     *
     * @return int - the count of consumed bytes from aInput
     * @throw std::overflow_error - on aInput overrrun or
     *        std::range_error    - if problem with aInput's contents.
     */
    int Deserialize( BufReader aInput );

    //-----<Serializeable>------------------------------------------------------
    int Serialize( BufWriter aOutput, int aCtl = 0 ) const;
    int SerializedCount( int aCtl = 0 ) const;
    //-----</Serializeable>-----------------------------------------------------

   // They arrive in this order when all are present in a forward_open request:
    CipPortSegmentGroup     port_segs;  // has optional electronic key.
    CipAppPath              app_path[3];
    CipSimpleDataSegment    data_seg;

#define app_path1           app_path[0]
#define app_path2           app_path[1]
#define app_path3           app_path[2]
};


/*
class ConnError : public std::runtime_error
{
public:
    ConnError(
            const char* aMsg,
            CipError aGenStatus,
            ConnMgrStatus aExtStatus = kConnMgrStatusSuccess
            ) :
        std::runtime_error( aMsg ),
        gen_status( aGenStatus ),
        ext_status( aExtStatus )
    {}

    CipError        gen_status;
    ConnMgrStatus   ext_status;
};
*/


/**
 * Class ConnectionData
 * contains parameters identified in Vol1 3-5.4.1 as well as a ConnectionPath.
 * The members correspond to the fields in the forward open request.  For
 * deserialization, there are these functions:
 * DeserializeForwardOpenRequest()
 * DeserializeConnectionPath().
 * For serialization there is a need for only:
 * Serialize( BufWriter aOutput, int aCtl ) since the control bits can be embellished.
 */
class ConnectionData : public Serializeable
{
    friend class CipConnectionClass;
    friend class CipConnMgrClass;
    //friend class CipConn;

public:
    ConnectionData(
            CipByte aPriorityTimeTick = 0,
            CipByte aTimeoutTicks = 0,
            CipUdint aConsumingConnectionId = 0,
            CipUdint aProducingConnectionId = 0,
            CipUint aConnectionSerialNumber = 0,
            CipUint aOriginatorVendorId = 0,
            CipUdint aOriginatorSerialNumber = 0,
            ConnTimeoutMultiplier aConnectionTimeoutMultiplier = kConnTimeoutMultiplier4,
            CipUdint aConsumingRPI_usecs = 0,
            CipUdint aProcudingRPI_usecs = 0
            );

    std::string Format() const;

    TransportTrigger&   Transport() const                   { return (TransportTrigger&) trigger; }

    CipByte     PriorityTimeTick() const                    { return priority_timetick; }
    void        SetPriorityTimeTick( CipByte aValue )       { priority_timetick = aValue; }

    CipByte     TimeoutTicks() const                        { return timeout_ticks; }
    void        SetTimeoutTicks( CipByte aValue )           { timeout_ticks = aValue; }

    CipUdint    ConsumingRPI() const                        { return consuming_RPI_usecs; }
    void        SetConsumingRPI( CipUdint aPeriodUsecs)     { consuming_RPI_usecs = aPeriodUsecs; }

    CipUdint    ProducingRPI() const                        { return producing_RPI_usecs; }
    void        SetProducingRPI( CipUdint aPeriodUsecs)     { producing_RPI_usecs = aPeriodUsecs; }

    CipUdint    ConsumingConnectionId() const               { return consuming_connection_id; }
    void        SetConsumingConnectionId( CipUdint aCid )   { consuming_connection_id = aCid; }

    CipUdint    ProducingConnectionId() const               { return producing_connection_id; }
    void        SetProducingConnectionId( CipUdint aCid )   { producing_connection_id = aCid; }

    NetCnParams&    ConsumingNCP() const                    { return (NetCnParams&) consuming_ncp; }
    NetCnParams&    ProducingNCP() const                    { return (NetCnParams&) producing_ncp; }


    ConnectionPath& ConnPath() const                        { return (ConnectionPath&) conn_path; }

    // per Vol1 3-5.4.1.10 the application path names are relative to the target node.
    // A consuming_path is for a O->T connection.
    // A producing_path is for a T->O connection.

    CipAppPath& ConfigPath() const      { return config_path    < 0 ? HasAny_No : (CipAppPath&) conn_path.app_path[config_path]; }
    CipAppPath& ConsumingPath() const   { return consuming_path < 0 ? HasAny_No : (CipAppPath&) conn_path.app_path[consuming_path]; }
    CipAppPath& ProducingPath() const   { return producing_path < 0 ? HasAny_No : (CipAppPath&) conn_path.app_path[producing_path]; }

    bool TriadEquals( const ConnectionData& aOther ) const
    {
        return connection_serial_number == aOther.connection_serial_number
            && originator_vendor_id     == aOther.originator_vendor_id
            && originator_serial_number == aOther.originator_serial_number;
    }

    bool OriginatorEquals( const ConnectionData& aOther ) const
    {
        return originator_vendor_id     == aOther.originator_vendor_id
            && originator_serial_number == aOther.originator_serial_number;
    }

    ConnectionData& SetTimeoutMultiplier( ConnTimeoutMultiplier aMultiplier );

    ConnTimeoutMultiplier TimeoutMultiplier() const
    {
        ConnTimeoutMultiplier ret =
            ConnTimeoutMultiplier( 1 << (2 + connection_timeout_multiplier_value) );

        //CIPSTER_TRACE_INFO( "%s: %d\n", __func__, ret );
        return ret;
    }

    int DeserializeForwardOpenRequest( BufReader aInput, bool isLargeForwardOpen );
    int DeserializeForwardOpenResponse( BufReader aInput );

    int DeserializeForwardCloseRequest( BufReader aInput );
    int DeserializeForwardCloseResponse( BufReader aInput );

    /**
     * Function DeserializeConnectionPath
     * decodes a connection path.  Useful when decoding a forward open request
     * or forward close request.
     *
     * @param aInput provides the encoded segments and should be length limited
     *   so this function knows when to stop consuming input bytes. Construct this
     *   BufReader using the word count which precedes most connection_paths.
     *
     * @return int - the count of consumed bytes from aInput
     * @throw std::overflow_error - on aInput overrrun or
     *        std::range_error    - if problem with aInput's contents.
     */
    int DeserializeConnectionPath( BufReader aInput )
    {
        return conn_path.Deserialize( aInput );
    }

    CipError ResolveInstances( ConnMgrStatus* aExtError );
    CipError VerifyForwardOpenParams( ConnMgrStatus* aExtError );
    CipError CorrectSizes( ConnMgrStatus* aExtError );

    //-----<Serializeable>------------------------------------------------------
    int Serialize( BufWriter aOutput, int aCtl = 0 ) const;
    int SerializedCount( int aCtl = 0 ) const;
    //-----</Serializeable>-----------------------------------------------------

    void Clear()
    {
        priority_timetick = 0;
        timeout_ticks = 0;
        consuming_connection_id = 0;
        producing_connection_id = 0;
        connection_serial_number = 0;
        originator_vendor_id = 0;
        originator_serial_number = 0;
        connection_timeout_multiplier_value = 0;

        consuming_RPI_usecs = 0;
        producing_RPI_usecs = 0;

        consuming_ncp.Clear();
        producing_ncp.Clear();
        trigger.Clear();

        conn_path.Clear();

        corrected_consuming_size = 0;
        corrected_producing_size = 0;

        consuming_instance = 0;
        producing_instance = 0;
        config_instance = 0;
        mgmnt_class = 0;
    }


protected:

    //-----<ConnectionTriad>----------------------------------------------------
    // The Connection Triad used in the Connection Manager specification includes
    // the combination of Connection Serial Number, Originator Vendor ID and
    // Originator Serial Number parameters.
    CipUint             connection_serial_number;
    CipUint             originator_vendor_id;

    // The Originator Serial Number utilized in conjunction with the Connection
    // Manager is a reference to the Identity object instance #1, attribute #6
    // (Serial Number) of the connection originator.
    CipUdint            originator_serial_number;
    //-----</ConnectionTriad>---------------------------------------------------

    TransportTrigger    trigger;

    CipByte             priority_timetick;
    CipByte             timeout_ticks;
    CipByte             connection_timeout_multiplier_value;

    CipUdint            consuming_RPI_usecs;
    NetCnParams         consuming_ncp;
    CipUdint            consuming_connection_id;

    CipUdint            producing_RPI_usecs;
    NetCnParams         producing_ncp;
    CipUdint            producing_connection_id;

    ConnectionPath      conn_path;

    //-----<Validation Variables>-----------------------------------------------
    // The following variables do not come from the forward open request,
    // but are held here for the benefit of the deriving CipConn class and for
    // validation of forward open request.
    EipUint16           corrected_consuming_size;
    EipUint16           corrected_producing_size;

    CipInstance*        consuming_instance; ///< corresponds to conn_path.consuming_path
    CipInstance*        producing_instance; ///< corresponds to conn_path.producing_path
    CipInstance*        config_instance;    ///< corresponds to conn_path.config_path

    // class id of the clazz->OpenConnection() virtual to call
    int                 mgmnt_class;
    //-----</Validation Variables>----------------------------------------------


private:

    static CipAppPath   HasAny_No;       // indices below indicate this when -1

    // indices into conn_path.app_path[], except that -1 indicates HasAny_No
    CipSint             config_path;
    CipSint             consuming_path;
    CipSint             producing_path;
};


/**
 * Class CipConn
 * holds data for a connection. This data is strongly related to
 * the connection class instance defined in the CIP-specification.
 */
class CipConn : public ConnectionData
{
    friend class CipConnectionClass;
    friend class CipConnMgrClass;
    friend class CipConnBox;

public:
    CipConn();

    static EipStatus Init( EipUint16 unique_connection_id );

    static int constructed_count;   // incremented and assigned to instance_id.

    /**
     * operator =
     * does assignment from a ConnectionData instance to this CipConnection.
     */
    CipConn& operator=( const ConnectionData& aSrc )
    {
        * static_cast<ConnectionData*>(this) = aSrc;
        return *this;
    }

    /**
     * Function Activate
     * changes state of this CipConn to activated if it can, otherwise returns
     * an error.
     */
    CipError Activate( Cpf* cpfd, ConnMgrStatus* aExtError );

    void Clear( bool doConnectionDataToo = true );

    /**
     * Function GeneralConfiguration
     * was GeneralConnectionConfiguration() and
     * generates the ConnectionIDs and sets the general configuration
     * parameters in this CipConn
     */
    void GeneralConfiguration( ConnectionData* aConnData, ConnInstanceType aType );

    CipConn& SetState( ConnState aNewState )
    {
        CIPSTER_TRACE_INFO( "%s<%d>(%s)\n", __func__, instance_id, ShowState( aNewState ) );
        state = aNewState;
        return *this;
    }

    ConnState State() const   { return state; }

    static const char* ShowState( ConnState aState );

    CipConn& SetInstanceType( ConnInstanceType aType )
    {
        CIPSTER_TRACE_INFO( "%s<%d>(%s)\n", __func__, instance_id, ShowInstanceType( aType ) );
        instance_type = aType;
        return *this;
    }

    ConnInstanceType InstanceType() const   { return instance_type; }

    static const char* ShowInstanceType( ConnInstanceType aType );

    void SetSessionHandle( CipUdint aSessionHandle )    { encap_session = aSessionHandle; }
    CipUdint SessionHandle() const                      { return encap_session; }

    bool IsIOConnection() const
    {
        // bit 0 is a 1 for all I/O connections, see enum ConnInstanceType
        return instance_type & 1;
    }

    EipUint32   ExpectedPacketRateUSecs() const
    {
        return expected_packet_rate_usecs;
    }

    CipConn& SetExpectedPacketRateUSecs( EipUint32 aRateUSecs )
    {
        EipUint32   adjusted = aRateUSecs;

        // The requested packet interval parameter needs to be a multiple of
        // kCIPsterTimerTickInMicroSeconds from the user's header file
        if( adjusted % kCIPsterTimerTickInMicroSeconds )
        {
            // Vol1 3-4.4.9 Since we are not an exact multiple, round up to
            // slower nearest integer multiple of our timer.
            adjusted = ( adjusted / kCIPsterTimerTickInMicroSeconds )
                * kCIPsterTimerTickInMicroSeconds + kCIPsterTimerTickInMicroSeconds;
        }

        CIPSTER_TRACE_INFO( "%s( %d ) adjusted=%d\n", __func__, aRateUSecs, adjusted );
        expected_packet_rate_usecs = adjusted;
        return *this;
    }

    CipUdint TimeoutUSecs() const
    {
        CipUdint ret = expected_packet_rate_usecs * TimeoutMultiplier();
        //CIPSTER_TRACE_INFO( "%s: %d\n", __func__, ret );
        return ret;
    }

    EipInt32 TransmissionTriggerTimerUSecs() const
    {
        return transmission_trigger_timer_usecs;
    }

    CipConn& SetTransmissionTriggerTimerUSecs( EipInt32 aValue )
    {
        //CIPSTER_TRACE_INFO( "%s( %d ) CID:0x%08x PID:0x%08x\n", __func__, aValue, consuming_connection_id, producing_connection_id );
        transmission_trigger_timer_usecs = aValue;
        return *this;
    }

    CipConn& AddToTransmissionTriggerTimerUSecs( EipInt32 aUSecs )
    {
        return SetTransmissionTriggerTimerUSecs( transmission_trigger_timer_usecs + aUSecs );
    }

    EipInt32 InactivityWatchDogTimerUSecs() const
    {
        return inactivity_watchdog_timer_usecs;    // signed 32 bits, in usecs
    }

    CipConn& SetInactivityWatchDogTimerUSecs( EipInt32 aUSecs )
    {
        //CIPSTER_TRACE_INFO( "%s( %d )\n", __func__, aUSecs );
        inactivity_watchdog_timer_usecs = aUSecs;
        return *this;
    }

    CipConn& AddToInactivityWatchDogTimerUSecs( int aUSecs )
    {
        return SetInactivityWatchDogTimerUSecs( inactivity_watchdog_timer_usecs + aUSecs );
    }

    /// Some connections never timeout, some do.  Vol1 3-4.5.3
    bool HasInactivityWatchDogTimer() const
    {
        return( expected_packet_rate_usecs      // if zero, no inactivity timer
            &&  ( consuming_instance            // a consuming connection in play
                || trigger.IsServer() ));
    }

    /**
     * Function SndConnectedData
     * sends the data from the producing CIP object of the connection via the socket
     * of the connection instance on UDP.
     */
    EipStatus SendConnectedData();

    EipStatus HandleReceivedIoConnectionData( BufReader aInput );

    /**
     * Function Close
     * closes a connection. If it is an exclusive owner or input only
     * connection and in charge of the connection a new owner will be searched
     */
    void Close();

    UdpSocket* ConsumingUdp() const  { return consuming_socket; }
    UdpSocket* ProducingUdp() const  { return producing_socket; }


    void SetConsumingUdp( UdpSocket* aSocket )
    {
        consuming_socket  = aSocket;
    }

    void SetProducingUdp( UdpSocket* aSocket )
    {
        producing_socket  = aSocket;
    }

    /**
     * Function NewConnectionId
     * generates a new connection Id utilizing the Incarnation Id as
     * described in the EIP specs.
     *
     * A unique connectionID is formed from the boot-time-specified "incarnation ID"
     * and the per-new-connection-incremented connection number/counter.
     * @return EipUint32 - new connection id
     */
    static EipUint32 NewConnectionId();

    //LinkObject      link_object;

    WatchdogTimeoutAction watchdog_timeout_action;

    /** EIP level sequence Count for Class 0/1.
     *  Producing Connections may have a different value than this.
     */
    EipUint32 eip_level_sequence_count_producing;

    /**
     * EIP level sequence Count for Class 0/1.
     * Producing Connections may have a different value than this.
     */
    EipUint32 eip_level_sequence_count_consuming;

    bool eip_level_sequence_count_consuming_first;  ///< true up until first received frame.

    /// sequence Count for Class 1 producing connections
    EipUint16 sequence_count_producing;

    /// sequence Count for Class 1 consuming connections
    EipUint16 sequence_count_consuming;

    /**
     * Function GetProductionInhibitTimeUSecs
     * returns the minimal time between the production of two application triggered
     * or change of state triggered I/O connection messages
     */
    EipUint32 GetPIT_USecs() const
    {
        return conn_path.port_segs.GetPIT_USecs();
    }

    bool HasPIT() const
    {
        return conn_path.port_segs.HasPIT();
    }

    void SetPIT_USecs( EipUint32 aUSECS )
    {
        conn_path.port_segs.SetPIT_USecs( aUSECS );
    }

    /// Timer for the production inhibition of application triggered or
    /// change-of-state I/O connections.
    EipInt32 production_inhibit_timer_usecs;

    /// Destination IP address for the optional producing CIP connection held
    /// by this CipConnection instance.
    SockAddr send_address;

    /// Address to use when filtering incoming packets on the optional
    /// consuming connection.
    SockAddr recv_address;

    /// Address of the node that did the forward_open and established
    /// this connection, needed for forward_close.
    SockAddr openers_address;

    // Hooks for special events that a user or debugging task can install.
    // Otherwise they are not used.
    ConnectionCloseFunction         hook_close;
    ConnectionTimeoutFunction       hook_timeout;

    // constructor assigns in ascending sequence.  This is not a Cip ID (yet).
    // Used for debugging messages.
    int instance_id;

protected:

    void timeOut();

    ConnMgrStatus handleConfigData();

    /**
     * Function openCommunicationChannels
     * takes the data given in this CipConn and opens the necessary
     * communication channels.
     *
     * @param aCpf
     * @param aExtError where to put an extended CIP error, if any.
     * @return CipError - general status on the open process
     *    - kCipErrorSuccess ... on success
     *    - On an error the general status code to be put into the response
     */
    CipError openCommunicationChannels( Cpf* aCpf, ConnMgrStatus* aExtError );

    CipError openProducingMulticastConnection( Cpf* aCpf, ConnMgrStatus* aExtError );

    /**
     * Function openMulticastConnection
     * opens a Multicast connection in a direction dependent on @a aDirection.
     *
     * @param aDirection indicates if consuming or producing.
     * @param aCpfd  the Cpf that the forward open request arrived within.
     * @param aExtError where to put an extended CIP error, if any.
     * @return CipError -
     */
    CipError openMulticastConnection( UdpDirection aDirection,
                Cpf* aCpf, ConnMgrStatus* aExtError );

    /**
     * Function openConsumingPointToPointConnection
     * opens a Point2Point connection.
     *
     * @param aCpfd  the Cpf that the forward open request arrived within.
     * @param aExtError where to put an extended CIP error, if any.
     * @return CipError -
     */
    CipError openConsumingPointToPointConnection( Cpf* cpfd, ConnMgrStatus* aExtError );

    CipError openProducingPointToPointConnection( Cpf* aCpf, ConnMgrStatus* aExtError );

    ConnInstanceType    instance_type;

    ConnState   state;          // CIP Connection Instance attribute id 1

    EipUint32   expected_packet_rate_usecs;

    EipInt32    inactivity_watchdog_timer_usecs;    // signed 32 bits, in usecs
    EipInt32    transmission_trigger_timer_usecs;   // signed 32 bits, in usecs

    UdpSocket*  consuming_socket;
    UdpSocket*  producing_socket;
    CipUdint    encap_session;          // session_handle, 0 is not used.

private:
    // for active connection doubly linked list at g_active_conns
    CipConn*    next;
    CipConn*    prev;
};


/**
 * Class CipConnection
 * wants to be class_id = 0x05 according to the CIP spec.
 */
class CipConnectionClass : public CipClass
{
public:
    CipConnectionClass();

    static CipError OpenIO( ConnectionData* aParams, Cpf* cpfd, ConnMgrStatus* aExtError );
};



#endif // CIPIOCONNECTION_H_
