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

#include "opener_api.h"
#include "cipepath.h"


const int kConnectionClassId = 0x05;

EipStatus ConnectionClassInit();


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

    int RedundantOwner() const
    {
        return not_large ? ((bits>>15)&1) : ((bits>>31)&1);
    }

    IOConnType ConnectionType() const
    {
        return not_large ? IOConnType((bits>>13)&3) : IOConnType((bits>>29)&3);
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
struct CipConn
{
#endif
    ConnectionState     state;
    ConnInstanceType    instance_type;

    /* conditional
     *  EipUint16 DeviceNetProductedConnectionID;
     *  EipUint16 DeviceNetConsumedConnectionID;
     */
    EipByte     device_net_initial_comm_characteristcs;
    EipUint16   produced_connection_size;
    EipUint16   consumed_connection_size;
    EipUint16   expected_packet_rate;

    //conditional
    EipUint32   produced_connection_id;
    EipUint32   consumed_connection_id;

    /**/
    WatchdogTimeoutAction watchdog_timeout_action;

    CipAppPath  produced_connection_path;
    CipAppPath  consumed_connection_path;

    /* conditional
     *  UINT16 ProductionInhibitTime;
     */
    // non CIP Attributes, only relevant for opened connections
    EipByte     priority_timetick;
    EipUint8    timeout_ticks;
    EipUint16   connection_serial_number;
    EipUint16   originator_vendor_id;
    EipUint32   originator_serial_number;
    EipUint16   connection_timeout_multiplier;
    EipUint32   o_to_t_requested_packet_interval;

    NetCnParams o_to_t_ncp;

    EipUint32   t_to_o_requested_packet_interval;

    NetCnParams t_to_o_ncp;

    TransportTrigger    transport_trigger;          ///< TransportClass_trigger

    CipConnectionPath   conn_path;                  ///< padded EPATH

    LinkObject          link_object;

    CipInstance* consuming_instance;

    // S_CIP_CM_Object *p_stConsumingCMObject;

    CipInstance* producing_instance;

    // S_CIP_CM_Object *p_stProducingCMObject;

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

    EipUint16 sequence_count_producing;             /* sequence Count for Class 1 Producing
                                                     *  Connections */
    EipUint16 sequence_count_consuming;             /* sequence Count for Class 1 Producing
                                                     *  Connections */

    EipInt32    transmission_trigger_timer;
    EipInt32    inactivity_watchdog_timer;

    /** @brief Minimal time between the production of two application triggered
     * or change of state triggered I/O connection messages
     */
    EipUint16 production_inhibit_time;

    /** @brief Timer for the production inhibition of application triggered or
     * change-of-state I/O connections.
     */
    EipInt32 production_inhibit_timer;

    struct sockaddr_in  remote_address;     // socket address for produce
    struct sockaddr_in  originator_address; /* the address of the originator that
                                             *  established the connection. needed
                                             *  for scanning if the right packet is
                                             *  arriving */

    int socket[2];                          // socket handles, indexed by kConsuming or kProducing

    // pointers to connection handling functions
    ConnectionCloseFunction         connection_close_function;
    ConnectionTimeoutFunction       connection_timeout_function;
    ConnectionSendDataFunction      connection_send_data_function;
    ConnectionReceiveDataFunction   connection_receive_data_function;

    // pointers to be used in the active connection list
    CipConn*    next_cip_conn;
    CipConn*    first_cip_conn;

    EipUint16   correct_originator_to_target_size;
    EipUint16   correct_target_to_originator_size;
};


/**
 * Function EstablishIoConnction
 * sets up all data in order to establish an IO connection
 *
 * This function can be called after all data has been parsed from the forward open request
 * @param aConn the connection object structure holding the parsed data from the forward open request
 * @param extended_error the extended error code in case an error happened
 * @return general status on the establishment
 *    - EIP_OK ... on success
 *    - On an error the general status code to be put into the response
 */
int EstablishIoConnction( CipConn* aConn,  EipUint16* extended_error );

/**
 * Function OpenCommunicationChannels
 * takese the data given in the connection object structure and opens
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


/**
 * Class CipConnection
 * wants to be class_id = 0x05 according to the CIP spec.
 */
class CipConnectionClass : public CipClass
{
public:
    CipConnectionClass();

};


extern EipUint8* g_config_data_buffer;
extern unsigned g_config_data_length;

#endif // CIPIOCONNECTION_H_
