/*******************************************************************************
 * Copyright (c) 2011, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/

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

#ifndef CIPIOCONNECTION_H_
#define CIPIOCONNECTION_H_

#include "cipster_api.h"
#include "cipepath.h"


EipStatus ConnectionClassInit( EipUint16 unique_connection_id );


//* @brief States of a connection
enum ConnectionState
{
    kConnectionStateNonExistent = 0,
    kConnectionStateConfiguring = 1,
    kConnectionStateWaitingForConnectionId = 2,    ///< only used in DeviceNet
    kConnectionStateEstablished = 3,
    kConnectionStateTimedOut = 4,
    kConnectionStateDeferredDelete = 5,    ///< only used in DeviceNet
    kConnectionStateClosing
};


//* @brief instance_type attributes
enum ConnInstanceType
{
    kConnInstanceTypeExplicit = 0,
    kConnInstanceTypeIoExclusiveOwner = 0x01,
    kConnInstanceTypeIoInputOnly  = 0x11,
    kConnInstanceTypeIoListenOnly = 0x21
};


/**
 * enum IOConnType
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


/**
 * Class NetCnParams
 * holds the bitfields for either a forward open or large forward open's
 * "network connection parameters", and implements details that higher level
 * code should not be burdened with.
 */
class NetCnParams
{
public:
    NetCnParams() :
        not_large( true ),
        bits( 0 )
    {}

    bool RedundantOwner() const
    {
        return not_large ? ((bits>>15)&1) : ((bits>>31)&1);
    }

    IOConnType ConnectionType() const
    {
        return not_large ? IOConnType((bits>>13)&3) : IOConnType((bits>>29)&3);
    }

    bool IsNull() const
    {
        return ConnectionType() == kIOConnTypeNull;
    }

    int Priority() const
    {
        return not_large ? ((bits>>10)&3) : ((bits>>26)&3);
    }

    bool IsFixed() const
    {
        return not_large ? ((bits>>9)&1) : ((bits>>25)&1);
    }

    unsigned ConnectionSize() const
    {
        return not_large ? (bits & 0x1ff) : (bits & 0xffff);
    }

    void SetLarge( EipUint32 aNCP )
    {
        bits = aNCP;
        not_large = false;
    }

    void SetNotLarge( EipUint16 aNCP )
    {
        bits = aNCP;
        not_large = true;
    }

private:
    bool        not_large;
    EipUint32   bits;
};


enum ConnectionTriggerType
{
    kConnectionTriggerTypeCyclic = 0,
    kConnectionTriggerTypeChangeOfState = 1,
    kConnectionTriggerTypeApplication = 2,
};


enum ConnectionTransportClass
{
    kConnectionTransportClass0 = 0,
    kConnectionTransportClass1 = 1,
    kConnectionTransportClass2 = 2,
    kConnectionTransportClass3 = 3,
};


class TransportTrigger
{
public:
    TransportTrigger() :
        bits( 0 )
    {}

    void Set( EipByte aByte )   { bits = aByte; }

    /// Return true if server else false for client.
    bool IsServer() const       { return bits & 0x80; }

    ConnectionTriggerType Trigger()  const
    {
        return ConnectionTriggerType((bits >> 4) & 7);
    }

    ConnectionTransportClass Class() const
    {
        return ConnectionTransportClass( bits & 15 );
    }

private:
    EipByte     bits;
};


//* @brief Possible values for the watch dog time out action of a connection
enum WatchdogTimeoutAction
{
    kWatchdogTimeoutActionTransitionToTimedOut = 0, ///< , invalid for explicit message connections
    kWatchdogTimeoutActionAutoDelete    = 1,        /**< Default for explicit message connections,
                                                     *  default for I/O connections on EIP */
    kWatchdogTimeoutActionAutoReset     = 2,        ///< Invalid for explicit message connections
    kWatchdogTimeoutActionDeferredDelete = 3        ///< Only valid for DeviceNet, invalid for I/O connections
};


struct LinkConsumer
{
    ConnectionState state;
    EipUint16       connection_id;

/*TODO think if this is needed anymore
 *  TCMReceiveDataFunc m_ptfuncReceiveData; */
};


struct LinkProducer
{
    ConnectionState state;
    EipUint16       connection_id;
};


struct LinkObject
{
    LinkConsumer    consumer;
    LinkProducer    producer;
};


/**
 * Struct CipConnPath
 * holds data deserialized from the connection_path portion of a
 * forward_open service request.
 */
struct CipConnPath
{
    // They arrive in this order when all are present:

    CipPortSegmentGroup     port_segs;

    CipAppPath  config_path;
    CipAppPath  consuming_path;     ///< consumption from my perspective, output from network's perspective
    CipAppPath  producing_path;     ///< production from my perspective, input from network's perspective

    CipSimpleDataSegment    data_seg;

    const std::string Format() const;

    void Clear()
    {
        port_segs.Clear();
        config_path.Clear();
        consuming_path.Clear();
        producing_path.Clear();
        data_seg.Clear();
    }
};


/**
 * Struct CipConn
 * holds the data needed for handling connections. This data is strongly related to
 * the connection object defined in the CIP-specification.
 */
#if 0
class CipConn : public CipInstance
{
public:

    CipConn( int aId ) :
        CipInstance( aId )
    {}
#else
class CipConn
{
public:


#endif

    void Clear();

    CipError parseConnectionPath( CipMessageRouterRequest* request, ConnectionManagerStatusCode* extended_error );

    ConnectionState     state;
    ConnInstanceType    instance_type;

    /* conditional
     *  EipUint16 DeviceNetProductedConnectionID;
     *  EipUint16 DeviceNetConsumedConnectionID;
     */
    EipByte     device_net_initial_comm_characteristcs;

    EipUint16   producing_connection_size;
    EipUint16   consuming_connection_size;

    EipUint32   GetExpectedPacketRateUSecs() const
    {
        return expected_packet_rate_usecs;
    }

    void SetExpectedPacketRateUSecs( EipUint32 aRateUSecs )
    {
        CIPSTER_TRACE_INFO( "%s( %d )\n", __func__, aRateUSecs );
        expected_packet_rate_usecs = aRateUSecs;
    }

    // conditional
    EipUint32   producing_connection_id;
    EipUint32   consuming_connection_id;

    LinkObject      link_object;

    int consuming_socket;
    int producing_socket;

    int mgmnt_class;

    WatchdogTimeoutAction watchdog_timeout_action;

    /* conditional
     *  UINT16 ProductionInhibitTime;
     */
    // non CIP Attributes, only relevant for opened connections
    EipByte     priority_timetick;
    EipUint8    timeout_ticks;
    EipUint16   connection_serial_number;
    EipUint16   originator_vendor_id;
    EipUint32   originator_serial_number;
    EipUint8    connection_timeout_multiplier;

    EipUint32   t_to_o_RPI_usecs;                         ///< usecs
    EipUint32   o_to_t_RPI_usecs;                         ///< usecs

    NetCnParams t_to_o_ncp;
    NetCnParams o_to_t_ncp;

    TransportTrigger    transport_trigger;          ///< TransportClass_trigger

    CipConnPath     conn_path;

    CipInstance*    consuming_instance;             ///< corresponds to conn_path.consuming_path
    CipInstance*    producing_instance;             ///< corresponds to conn_path.producing_path
    CipInstance*    config_instance;                ///< corresponds to conn_path.config_path

    EipUint32 eip_level_sequence_count_producing;   /* the EIP level sequence Count
                                                     *  for Class 0/1
                                                     *  Producing Connections may have a
                                                     *  different
                                                     *  value than SequenceCountProducing */
    EipUint32 eip_level_sequence_count_consuming;   /* the EIP level sequence Count
                                                     *  for Class 0/1
                                                     *  Producing Connections may have a
                                                     *  different
                                                     *  value than SequenceCountProducing */

    bool eip_level_sequence_count_consuming_first;  ///< true up until first received frame.

    EipUint16 sequence_count_producing;             /* sequence Count for Class 1 Producing
                                                     *  Connections */
    EipUint16 sequence_count_consuming;             /* sequence Count for Class 1 Producing
                                                     *  Connections */

    EipInt32    transmission_trigger_timer_usecs;   // signed 32 bits, in usecs
    EipInt32    inactivity_watchdog_timer_usecs;    // signed 32 bits, in usecs

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

    /** @brief Timer for the production inhibition of application triggered or
     * change-of-state I/O connections.
     */
    EipInt32 production_inhibit_timer_usecs;

    struct sockaddr_in  remote_address;     // socket address for produce
    struct sockaddr_in  originator_address; /* the address of the originator that
                                             *  established the connection. needed
                                             *  for scanning if the right packet is
                                             *  arriving */
    // pointers to connection handling functions
    ConnectionCloseFunction         connection_close_function;
    ConnectionTimeoutFunction       connection_timeout_function;
    ConnectionSendDataFunction      connection_send_data_function;
    ConnectionReceiveDataFunction   connection_receive_data_function;

    // used in the active connection list
    CipConn*    next;
    CipConn*    first;

    EipUint16   correct_originator_to_target_size;
    EipUint16   correct_target_to_originator_size;

private:
    EipUint32   expected_packet_rate_usecs;
};


/**
 * Function OpenCommunicationChannels
 * takes the data given in the connection object structure and opens
 * the necessary communication channels
 *
 * This function will use the g_stCPFDataItem!
 * @param aConn the connection object data
 * @return general status on the open process
 *    - EIP_OK ... on success
 *    - On an error the general status code to be put into the response
 */
CipError OpenCommunicationChannels( CipConn* aConn );

/**
 * Function CloseCommunicationChannelsAndRemoveFromActiveConnectionsList
 * closes the communication channels of the given connection and remove it
 * from the active connections list.
 *
 * @param cip_conn pointer to the connection object data
 */
void CloseCommunicationChannelsAndRemoveFromActiveConnectionsList( CipConn* cip_conn );

/** Copy the given connection data from pa_pstSrc to pa_pstDst
 */
inline void CopyConnectionData( CipConn* aDst, CipConn* aSrc )
{
    *aDst = *aSrc;
}

/** @brief Generate the ConnectionIDs and set the general configuration
 * parameter in the given connection object.
 *
 * @param cip_conn pointer to the connection object that should be set
 * up.
 */
void GeneralConnectionConfiguration( CipConn* cip_conn );


/**
 * Class CipConnection
 * wants to be class_id = 0x05 according to the CIP spec.
 */
class CipConnectionClass : public CipClass
{
public:
    CipConnectionClass();

    static CipError OpenIO( CipConn* aConn, ConnectionManagerStatusCode* extended_error_code );
};



#endif // CIPIOCONNECTION_H_
