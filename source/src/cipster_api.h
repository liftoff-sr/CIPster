/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#ifndef CIPSTER_CIPSTER_API_H_
#define CIPSTER_CIPSTER_API_H_

#include <assert.h>

#include <cipster_user_conf.h>

#include "cip/ciperror.h"
#include "cip/cipclass.h"
#include "cip/cipassembly.h"
#include "cip/cipmessagerouter.h"
#include "cip/cipethernetlink.h"
#include "cip/ciptcpipinterface.h"
#include "cip/cipconnectionmanager.h"
#include "enet_encap/encap.h"
#include "enet_encap/cpf.h"
#include "enet_encap/networkhandler.h"
#include "byte_bufs.h"


/**  @defgroup CIP_API CIPster User interface
 * @brief This is the public interface of the CIPster. It provides all function
 * needed to implement an EtherNet/IP enabled slave-device.
 */


/** @ingroup CIP_API
 * @brief Configure the data of the network interface of the device
 *
 *  This function setup the data of the network interface needed by CIPster.
 *  The multicast address is automatically calculated from he given data.
 *
 *  @param ip_address    the current IP address of the device
 *  @param subnet_mask  the subnet mask to be used
 *  @param gateway_address     the gateway address
 *  @return kEipStatusOk if the configuring worked otherwise EIP_ERROR
 */
inline EipStatus ConfigureNetworkInterface( const char* ip_address, const char* subnet_mask,
        const char* gateway_address )
{
    return CipTCPIPInterfaceClass::ConfigureNetworkInterface(
                1, ip_address, subnet_mask, gateway_address );
}

/** @ingroup CIP_API
 * @brief Configure the MAC address of the device
 *
 *  @param mac_address  the hardware MAC address of the network interface
 *  @return kEipStatusOk if the configuring worked otherwise EIP_ERROR
 */
inline void ConfigureMacAddress( const uint8_t* mac_address )
{
    CipEthernetLinkClass::ConfigureMacAddress( 1, mac_address );
}

/** @ingroup CIP_API
 * @brief Configure the domain name of the device
 * @param domain_name the domain name to be used
 */
inline void ConfigureDomainName( const char* domain_name )
{
    // The eventual API is probably this:
    CipTCPIPInterfaceClass::ConfigureDomainName( 1, domain_name );
}

/** @ingroup CIP_API
 * @brief Configure the host name of the device
 * @param host_name the host name to be used
 */
inline void ConfigureHostName( const char* host_name )
{
    // The eventual API is probably this:
    CipTCPIPInterfaceClass::ConfigureHostName( 1, host_name );
}

/** @ingroup CIP_API
 * @brief Set the serial number of the device's identity object.
 *
 * @param serial_number unique 32 bit number identifying the device
 */
void SetDeviceSerialNumber( uint32_t serial_number );

/** @ingroup CIP_API
 * @brief Set the current status of the device.
 *
 * @param device_status the new status value
 */
void SetDeviceStatus( uint16_t device_status );

/** @ingroup CIP_API
 * @brief Initialize and setup the CIP-stack
 *
 * @param unique_connection_id value passed to Connection_Manager_Init() to form
 * a "per boot" unique connection ID.
 */
void CipStackInit( uint16_t unique_connection_id );

/** @ingroup CIP_API
 * @brief Shutdown of the CIP stack
 *
 * This will
 *   - close all open I/O connections,
 *   - close all open explicit connections, and
 *   - free all memory allocated by the stack.
 *
 * Memory allocated by the application will not be freed. This has to be done
 * by the application!
 */
void ShutdownCipStack();

/** @ingroup CIP_API
 * Function GetCipClass
 * returns a pointer to a CipClass given @a aClassId
 *
 * @param aClassId the class ID of the CipClass to return
 *
 * @return CipClass* - non-NULL if success, else NULL if not found.
 */
inline CipClass* GetCipClass( int aClassId )
{
    return CipClass::Get( aClassId );
}

/** @ingroup CIP_API
 * @brief Register a CipClass into the CIP class registry.  This may only be
 *  done once for each unique class_id.
 *
 * @param aClass which CIP class to register.
 */
inline EipStatus RegisterCipClass( CipClass* aClass )
{
    return CipClass::Register( aClass );
}

/** @ingroup CIP_API
 * @brief Serialize aDataType according to CIP encoding into aBuf
 *
 *  @param aDataType the cip type to encode
 *  @param cip_data pointer to data value.
 *  @param aBuf where response should be written
 *  @return int - byte count writte into aBuf.
 *          -1 .. error
 */
int EncodeData( CipDataType aDataType, const void* cip_data, BufWriter& aBuf );

/** @ingroup CIP_API
 * @brief Retrieve the given data according to CIP encoding from the message
 * buffer.
 *
 * This function may be used in in own services for handling data from the
 * requester (e.g., setAttributeSingle).
 *  @param aDataType the CIP type to decode
 *  @param cip_data pointer to data value to written.
 *  @param aBuf where to get the data bytes from
 *  @return length of taken bytes
 *          -1 .. error
 */
int DecodeData( CipDataType aDataType, void* cip_data, BufReader& aBuf );

/**
 * Function CreateAssemblyInstance
 * creates an instance of an assembly
 *
 * @param aInstanceId  instance number of the assembly object to create
 * @param aByteBuf     the data the assembly object should contain and its byte count.
 *
 * Assembly Objects for Configuration Data:
 *
 * CIPster treats configuration assembly objects the same way as any other
 * assembly object.  In order to support a configuration assembly object it
 * has to be created with this function.
 * The notification on received configuration data is handled with the
 * AssemblyInstance::RecvData() function.
 */
inline AssemblyInstance* CreateAssemblyInstance( int aInstanceId, ByteBuf aByteBuf )
{
    return CipAssemblyClass::CreateInstance( aInstanceId, aByteBuf );
}

class CipConn;


/** @ingroup CIP_API
 * @brief Configures the connection point for an exclusive owner connection.
 *
 * @param output_assembly_id ID of the O-to-T point to be used for this
 * connection
 * @param input_assembly_id ID of the T-to-O point to be used for this
 * connection
 * @param configuration_assembly_id ID of the configuration point to be used for
 * this connection
 * @return bool - true on success, else false if too many
 */
bool ConfigureExclusiveOwnerConnectionPoint(
        int output_assembly_id,
        int input_assembly_id,
        int configuration_assembly_id );

/** @ingroup CIP_API
 * @brief Configures the connection point for an input only connection.
 *
 * @param output_assembly_id ID of the O-to-T point to be used for this
 * connection
 * @param input_assembly_id ID of the T-to-O point to be used for this
 * connection
 * @param configuration_assembly_id ID of the configuration point to be used for
 * this connection
 * @return bool - true on success, else false if too many
 */
bool ConfigureInputOnlyConnectionPoint(
        int output_assembly_id,
        int input_assembly_id,
        int configuration_assembly_id );

/** \ingroup CIP_API
 * \brief Configures the connection point for a listen only connection.
 *
 * @param connection_number The number of the input only connection. The
 *        enumeration starts with 0. Has to be smaller than
 *        CIPSTER_CIP_NUM_LISTEN_ONLY_CONNS.
 * @param output_assembly_id ID of the O-to-T point to be used for this
 * connection
 * @param input_assembly_id ID of the T-to-O point to be used for this
 * connection
 * @param configuration_assembly_id ID of the configuration point to be used for
 * this connection
 * @return bool - true on success, else false if too many
 */
bool ConfigureListenOnlyConnectionPoint(
        int output_assembly_id,
        int input_assembly_id,
        int configuration_assembly_id );

/** @ingroup CIP_API
 * @brief Check if any of the connection timers (TransmissionTrigger or
 * WatchdogTimeout) have timed out.
 *
 * If the a timeout occurs the function performs the necessary action. This
 * function should be called periodically once every CIPSTER_TIMER_TICK
 * milliseconds.
 *
 * @return kEipStatusOk on success
 */
inline EipStatus ManageConnections()
{
    return CipConnMgrClass::ManageConnections();
}

/** @ingroup CIP_API
 * @brief Trigger the production of an application triggered connection.
 *
 * This will issue the production of the specified connection at the next
 * possible occasion. Depending on the values for the RPI and the production
 * inhibit timer. The application is informed via the
 * bool BeforeAssemblyDataSend( CipInstance* aInstance )
 * callback function when the production will happen. This function should only
 * be invoked from void HandleApplication().
 *
 * The connection can only be triggered if the application is established and it
 * is of application triggered type.
 *
 * @param output_assembly_id the output assembly connection point of the
 * connection
 * @param input_assembly_id the input assembly connection point of the
 * connection
 * @return kEipStatusOk on success
 */
EipStatus TriggerConnections( int output_assembly_id, int input_assembly_id );


/**  @defgroup CIP_CALLBACK_API Callback Functions Demanded by CIPster
 * @ingroup CIP_API
 *
 * @brief These functions have to implemented in order to give the CIPster a
 * method to inform the application on certain state changes.
 */

/** @ingroup CIP_CALLBACK_API
 * @brief Allow the device specific application to perform its execution
 *
 * This function will be executed by the stack at the beginning of each
 * execution of EipStatus ManageConnections(void). It allows to implement
 * device specific application functions. Execution within this function should
 * be short.
 */
void HandleApplication();

/** @ingroup CIP_CALLBACK_API
 * Function NotifyIoConnectionEvent
 * informs the application of changes to a connection.
 *
 * @param aConn which connection
 * @param aEvent what kind of change occurred
 */
void NotifyIoConnectionEvent( CipConn* aConn, IoConnectionEvent aEvent );

/** @ingroup CIP_CALLBACK_API
 * Function AfterAssemblyDataReceived
 * is a callback function to inform application on received data for an
 * assembly object.
 *
 * This function has to be implemented by the user of the CIP-stack.
 *
 * @param aInstance the assembly instance that data was received for
 * @param aMode is the operating mode of the io connection peer.
 *   This can be one of: kModeRun, KModeIdle, or kModeUnknown.  If kModeUnknown this
 *   typically means either that the consuming connection half is not
 *   kRealTimeFmt32BitHeader or that the peer is setting assembly data using
 *   explicit messaging not implicity messaging.  In such a situation you can
 *   use the mode received via RunIdleChanged() earlier.
 *
 * @param aBytesReceivedCount is how many bytes were copied into the assembly.
 *
 * @return EipStatus - whether data could be processed
 *     - kEipStatusOk the received data was ok
 *     - EIP_ERROR the received data was wrong (especially needed for
 * configuration data assembly objects), or the received byte count did not
 * match a fixed size io connection.  For variable size connection, the received
 * size can legally be smaller than the connection size.
 *
 * Assembly Objects for Configuration Data:
 * The CIP-stack uses this function to inform on received configuration data.
 * The length of the data is already checked within the stack. Therefore the
 * user only has to check if the data is valid.
 */
EipStatus AfterAssemblyDataReceived( AssemblyInstance* aInstance,
    OpMode aMode, int aBytesReceivedCount );

/** @ingroup CIP_CALLBACK_API
 * Function BeforeAssemblyDataSend
 * informs the application that the data of an assembly object will be sent.
 * The application's duty is to update the data at @ aBuf with new data.
 *
 * Within this function the user can update the data of the assembly object
 * before it gets sent. The application can inform the stack if data has
 * changed.  Use AssemblyInstance::Buffer() and SizeBytes().
 * @param aInstance is the assembly instance that should send data.
 *
 * @return data has changed:
 *          - true assembly data has changed
 *          - false assembly data has not changed
 */
bool BeforeAssemblyDataSend( AssemblyInstance* aInstance );

/** @ingroup CIP_CALLBACK_API
 * @brief Emulate as close a possible a power cycle of the device
 *
 * @return if the service is supported the function will not return.
 *     EIP_ERROR if this service is not supported
 */
EipStatus ResetDevice();

/** @ingroup CIP_CALLBACK_API
 * @brief Reset the device to the initial configuration and emulate as close as
 * possible a power cycle of the device
 * @param also_reset_comm_parameters when true means reset everything including
 *   communications parameters, else all but comm params.
 *
 * @return if the service is supported the function will not return.
 *     EIP_ERROR if this service is not supported
 *
 * @see CIP Identity Service 5: Reset
 */
EipStatus ResetDeviceToInitialConfiguration( bool also_reset_comm_parameters );


/** @ingroup CIP_CALLBACK_API
 * @brief Inform the application that the Run/Idle State has been changed
 * by the originator.
 *
 * @param run_idle_value the current value of the run/idle flag according to CIP
 * spec Vol 1 3-6.5
 */
void RunIdleChanged( uint32_t run_idle_value );


/** @ingroup CIP_API
 * Function CloseSession
 * deletes any session associated with the aSocket and closes the socket connection.
 *
 * @param aSocket the socket of the session to close.
 * @return bool - true if aSocket was found in an open session (in which case), else false.
 */
inline bool CloseSession( int aSocket )
{
    return SessionMgr::CloseBySocket( aSocket );
}


/** @mainpage CIPster - Open Source EtherNet/IP(TM) Communication Stack
 * Documentation
 *
 * EtherNet/IP stack for adapter devices (connection target); supports multiple
 * I/O and explicit connections; includes features and objects required by the
 * CIP specification to enable devices to comply with ODVA's conformance/
 * interoperability tests.
 *
 * @section intro_sec Introduction
 *
 * This is the introduction.
 *
 * @section install_sec Building
 * How to compile, install and run CIPster on a specific platform.
 *
 * @subsection build_req_sec Requirements
 * CIPster has been developed to be highly portable. The default version targets
 * PCs with a POSIX operating system and a BSD-socket network interface. To
 * test this version we recommend a Linux PC or Windows with Cygwin installed.
 *  You will need to have the following installed:
 *   - gcc, make, binutils, etc.
 *
 * for normal building. These should be installed on most Linux installations
 * and are part of the development packages of Cygwin.
 *
 * For the development itself we recommend the use of Eclipse with the CDT
 * plugin. For your convenience CIPster already comes with an Eclipse project
 * file. This allows to just import the CIPster source tree into Eclipse.
 *
 * @subsection compile_pcs_sec Compile for PCs
 *   -# Directly in the shell
 *       -# Go into the bin/pc directory
 *       -# Invoke make
 *       -# For invoking opener type:\n
 *          ./opener ipaddress subnetmask gateway domainname hostaddress
 * macaddress\n
 *          e.g., ./opener 192.168.0.2 255.255.255.0 192.168.0.1 test.com
 * testdevice 00 15 C5 BF D0 87
 *   -# Within Eclipse
 *       -# Import the project
 *       -# Go to the bin/pc folder in the make targets view
 *       -# Choose all from the make targets
 *       -# The resulting executable will be in the directory
 *           ./bin/pc
 *       -# The command line parameters can be set in the run configuration
 * dialog of Eclipse
 *
 * @section further_reading_sec Further Topics
 *   - @ref porting
 *   - @ref extending
 *   - @ref license
 *
 * @page porting Porting CIPster
 * @section gen_config_section General Stack Configuration
 * The general stack properties have to be defined prior to building your
 * production. This is done by providing a file called cipster_user_conf.h. An
 * example file can be found in the src/ports/platform-pc directory. The
 * documentation of the example file for the necessary configuration options:
 * cipster_user_conf.h
 *
 * @copydoc cipster_user_conf.h
 *
 * @section startup_sec Startup Sequence
 * During startup of your EtherNet/IP(TM) device the following steps have to be
 * performed:
     -# Initialize CIPster: \n
        With the function CipStackInit(CipUint unique_connection_id) the
        internal data structures are correctly setup. After this
        step own CIP objects and Assembly objects instances may be created. For
        your convenience we provide the call-back function
        ApplicationInitialization. This call back function is called when the
        stack is ready to receive application specific CIP objects.
 *   -# Configure the network properties:\n
 *       With the following functions the network interface of CIPster is
 *       configured:
 *        - EipStatus ConfigureNetworkInterface(const char *ip_address,
 *        const char *subnet_mask, const char *gateway_address)
 *        - void ConfigureMACAddress(const uint8_t *mac_address)
 *        - void ConfigureDomainName(const char *domain_name)
 *        - void ConfigureHostName(const char *host_name)
 *        .
 *       Depending on your platform these data can come from a configuration
 *       file or from operating system functions. If these values should be
 *       setable remotely via explicit messages the SetAttributeSingle functions
 *       of the EtherNetLink and the TCPIPInterface object have to be adapted.
 *   -# Set the device's serial number\n
 *      According to the CIP specification a device vendor has to ensure that
 *      each of its devices has a unique 32Bit device id. You can set it with
 *      the function:
 *       - void setDeviceSerialNumber(CipUdint serial_number)
 *   -# Create Application Specific CIP Objects:\n
 *      Within the call-back function ApplicationInitialization(void) or
 *      after CipStackInit(void) has finished you may create and configure any
 *      CIP object or Assembly object instances. See the module @ref CIP_API
 *      for available functions. Currently no functions are available to
 *      remove any created objects or instances. This is planned
 *      for future versions.
 *   -# Setup the listening TCP and UDP port:\n
 *      THE ETHERNET/IP SPECIFICATION demands from devices to listen to TCP
 *      connections and UDP datagrams on the port 0xAF12 for explicit messages.
 *      Therefore before going into normal operation you need to configure your
 *      network library so that TCP and UDP messages on this port will be
 *      received and can be handed over to the Ethernet encapsulation layer.
 *
 * @section normal_op_sec Normal Operation
 * During normal operation the following tasks have to be done by the platform
 * specific code:
 *   - Establish connections requested on TCP port AF12hex
 *   - Receive explicit message data on connected TCP sockets and the UPD socket
 *     for port AF12hex. The received data has to be handed over to Ethernet
 *     encapsulation layer with the functions: \n
 *     int Encapsulation::HandleReceivedExplicitTcpData( int socket, BufReader aCommand, BufWriter aReply ),
 *     int Encapsulation::HandleReceivedExplicitUdpData(int socket_handle, const SockAddr&
 *  aFromAddress, uint8_t* buffer, unsigned buffer_length, int
 * *number_of_remaining_bytes).\n
 *     Depending if the data has been received from a TCP or from a UDP socket.
 *     As a result of this function a response may have to be sent. The data to
 *     be sent is in the given buffer pa_buf.
 *   - Create UDP sending and receiving sockets for implicit connected
 * messages\n
 *     CIPster will use function int CreateUdpSocket( const SockAddr& aSockAddr)
 *     for informing the platform specific code that a new connection is
 *     established and new sockets are necessary
 *   - Receive implicit connected data on a receiving UDP socket\n
 *     The received data has to be hand over to the Connection Manager Object
 *     with the function EipStatus RecvConnectedData( UdpSocket* aSocket,
 *      const SockAddr& aFromAddress, BufReader aCommand );
 *   - Close UDP and TCP sockets:
 *      -# Requested by CIPster through the call back function: void
 * CloseSocket(int aSocket)
 *      -# For TCP connection when the peer closed the connection CIPster needs
 *         to be informed to clean up internal data structures. This is done
 * with
 *         the function void CloseSession(int socket_handle).
 *      .
 *   - Cyclically update the connection status:\n
 *     In order that CIPster can determine when to produce new data on
 *     connections or that a connection timed out every @ref CIPSTER_TIMER_TICK
 * milliseconds the
 *     function EipStatus ManageConnections(void) has to be called.
 *
 * @section callback_funcs_sec Callback Functions
 * In order to make CIPster more platform independent and in order to inform the
 * application on certain state changes and actions within the stack a set of
 * call-back functions is provided. These call-back functions are declared in
 * the file cipster_api.h and have to be implemented by the application specific
 * code. An overview and explanation of CIPster's call-back API may be found in
 * the module @ref CIP_CALLBACK_API.
 *
 * @page extending Extending CIPster
 * CIPster provides an API for adding own CIP objects and instances with
 * specific services and attributes. Therefore CIPster can be easily adapted to
 * support different device profiles and specific CIP objects needed for your
 * device. The functions to be used are:
 *   - S_CIP_Class *CreateCIPClass(CipUdint class_id, int
 * number_of_class_attributes, CipUdint class_get_attribute_all_mask, int
 * number_of_class_services, int number_of_instance_attributes, CipUdint
 * instance_get_attribute_all_mask, int number_of_instance_services, int
 * number_of_instances, char *class_name, CipUint revision);
 *   - S_CIP_Instance *AddCIPInstances(S_CIP_Class *cip_object, int
 * number_of_instances);
 *   - S_CIP_Instance *AddCIPInstance(S_CIP_Class * cip_class, CipUdint
 * instance_id);
 *   - void InsertAttribute(S_CIP_Instance *instance, CipUint
 * attribute_number, uint8_t cip_type, void* data);
 *   - void InsertService(S_CIP_Class *class, uint8_t service_number,
 * CipServiceFunction service_function, char *service_name);
 *
 * @page license CIPster Open Source License
 * The CIPster Open Source License is an adapted BSD style license. The
 * adaptations include the use of the term EtherNet/IP(TM) and the necessary
 * guarding conditions for using CIPster in own products. For this please look
 * in license text as shown below:
 *
 * @verbinclude "license.txt"
 *
 */

#if defined(DEBUG)
void byte_dump( const char* aPrompt, uint8_t* aBytes, int aCount );
#endif

#endif  // CIPSTER_CIPSTER_API_H_
