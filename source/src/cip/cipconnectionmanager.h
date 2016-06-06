/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#ifndef CIPSTER_CIPCONNECTIONMANAGER_H_
#define CIPSTER_CIPCONNECTIONMANAGER_H_

#include "opener_user_conf.h"
#include "opener_api.h"
#include "typedefs.h"
#include "ciptypes.h"

/**
 * @brief Sets the routing type of a connection, either
 * - Point-to-point connections (unicast)
 * - Multicast connection
 */
enum RoutingType
{
    kRoutingTypePointToPointConnection = 0x4000,
    kRoutingTypeMulticastConnection = 0x2000
};

//* @brief Connection Manager Error codes
enum ConnectionManagerStatusCode
{
    kConnectionManagerStatusCodeSuccess = 0x00,
    kConnectionManagerStatusCodeErrorConnectionInUse = 0x0100,
    kConnectionManagerStatusCodeErrorTransportTriggerNotSupported = 0x0103,
    kConnectionManagerStatusCodeErrorOwnershipConflict = 0x0106,
    kConnectionManagerStatusCodeErrorConnectionNotFoundAtTargetApplication = 0x0107,
    kConnectionManagerStatusCodeErrorInvalidOToTConnectionType  = 0x123,
    kConnectionManagerStatusCodeErrorInvalidTToOConnectionType  = 0x124,
    kConnectionManagerStatusCodeErrorInvalidOToTConnectionSize  = 0x127,
    kConnectionManagerStatusCodeErrorInvalidTToOConnectionSize  = 0x128,
    kConnectionManagerStatusCodeErrorNoMoreConnectionsAvailable = 0x0113,
    kConnectionManagerStatusCodeErrorVendorIdOrProductcodeError = 0x0114,
    kConnectionManagerStatusCodeErrorDeviceTypeError    = 0x0115,
    kConnectionManagerStatusCodeErrorRevisionMismatch   = 0x0116,
    kConnectionManagerStatusCodeInvalidConfigurationApplicationPath = 0x0129,
    kConnectionManagerStatusCodeInvalidConsumingApllicationPath     = 0x012A,
    kConnectionManagerStatusCodeInvalidProducingApplicationPath     = 0x012B,
    kConnectionManagerStatusCodeInconsistentApplicationPathCombo    = 0x012F,
    kConnectionManagerStatusCodeNonListenOnlyConnectionNotOpened    = 0x0119,
    kConnectionManagerStatusCodeErrorParameterErrorInUnconnectedSendService = 0x0205,
    kConnectionManagerStatusCodeErrorInvalidSegmentTypeInPath   = 0x0315,
    kConnectionManagerStatusCodeTargetObjectOutOfConnections    = 0x011A
};

enum ConnectionTriggerType
{
    kConnectionTriggerTypeProductionTriggerMask = 0x70,
    kConnectionTriggerTypeCyclicConnection = 0x0,
    kConnectionTriggerTypeChangeOfStateTriggeredConnection = 0x10,
    kConnectionTriggerTypeApplicationTriggeredConnection = 0x20
};

/** @brief macros for comparing sequence numbers according to CIP spec vol
 * 2 3-4.2 for int type variables
 * @define SEQ_LEQ32(a, b) Checks if sequence number a is less or equal than b
 * @define SEQ_GEQ32(a, b) Checks if sequence number a is greater or equal than
 *  b
 *  @define SEQ_GT32(a, b) Checks if sequence number a is greater than b
 */
#define SEQ_LEQ32( a, b )   ( (int) ( (a) - (b) ) <= 0 )
#define SEQ_GEQ32( a, b )   ( (int) ( (a) - (b) ) >= 0 )
#define SEQ_GT32( a, b )    ( (int) ( (a) - (b) ) > 0 )

/** @brief similar macros for comparing 16 bit sequence numbers
 * @define SEQ_LEQ16(a, b) Checks if sequence number a is less or equal than b
 * @define SEQ_GEQ16(a, b) Checks if sequence number a is greater or equal than
 *  b
 */
#define SEQ_LEQ16( a, b )   ( (short) ( (a) - (b) ) <= 0 )
#define SEQ_GEQ16( a, b )   ( (short) ( (a) - (b) ) >= 0 )

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
enum ConnectionType
{
    kConnectionTypeExplicit = 0,
    kConnectionTypeIoExclusiveOwner = 0x01,
    kConnectionTypeIoInputOnly  = 0x11,
    kConnectionTypeIoListenOnly = 0x21
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
    EipUint16 connection_id;

/*TODO think if this is needed anymore
 *  TCMReceiveDataFunc m_ptfuncReceiveData; */
};


struct LinkProducer
{
    ConnectionState state;
    EipUint16 connection_id;
};


struct LinkObject
{
    LinkConsumer    consumer;
    LinkProducer    producer;
};


/**
 * Struct CipConn
 * holds the data needed for handling connections. This data is strongly related to
 * the connection object defined in the CIP-specification. However the full
 * functionality of the connection object is not implemented. Therefore this
 * data can not be accessed with CIP means.
 */
struct CipConn
{
    ConnectionState state;
    ConnectionType  instance_type;

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
    EipUint16   produced_connection_path_length;
    CipEpath    produced_connection_path;
    EipUint16   consumed_connection_path_length;
    CipEpath    consumed_connection_path;

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
    EipUint16   o_to_t_network_connection_parameter;
    EipUint32   t_to_o_requested_packet_interval;
    EipUint16   t_to_o_network_connection_parameter;
    EipByte     transport_type_class_trigger;
    EipUint8    connection_path_size;

    CipElectronicKey    electronic_key;
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

//* @brief Connection Manager class code
static const int kCipConnectionManagerClassCode = 0x06;

// public functions

/** @brief Initialize the data of the connection manager object
 */
EipStatus ConnectionManagerInit( EipUint16 unique_connection_id );

/** @brief Get a connected object dependent on requested ConnectionID.
 *
 *   @param connection_id  requested @var connection_id of opened connection
 *   @return pointer to connected Object
 *           0 .. connection not present in device
 */
CipConn* GetConnectedObject( EipUint32 connection_id );

/**  Get a connection object for a given output assembly.
 *
 *   @param output_assembly_id requested output assembly of requested
 * connection
 *   @return pointer to connected Object
 *           0 .. connection not present in device
 */
CipConn* GetConnectedOutputAssembly( EipUint32 output_assembly_id );

/** Copy the given connection data from pa_pstSrc to pa_pstDst
 */
void CopyConnectionData( CipConn* destination, CipConn* source );

/** @brief Close the given connection
 *
 * This function will take the data form the connection and correctly closes the
 * connection (e.g., open sockets)
 * @param cip_conn pointer to the connection object structure to be
 * closed
 */
void CloseConnection( CipConn* cip_conn );

// TODO: Missing documentation
bool IsConnectedOutputAssembly( EipUint32 aInstanceId );

/** @brief Generate the ConnectionIDs and set the general configuration
 * parameter in the given connection object.
 *
 * @param cip_conn pointer to the connection object that should be set
 * up.
 */
void GeneralConnectionConfiguration( CipConn* cip_conn );

/** @brief Insert the given connection object to the list of currently active
 *  and managed connections.
 *
 * By adding a connection to the active connection list the connection manager
 * will perform the supervision and handle the timing (e.g., timeout,
 * production inhibit, etc).
 *
 * @param cip_conn pointer to the connection object to be added.
 */
void AddNewActiveConnection( CipConn* cip_conn );

// TODO: Missing documentation
void RemoveFromActiveConnections( CipConn* cip_conn );

#endif // CIPSTER_CIPCONNECTIONMANAGER_H_
