/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#ifndef CIPSTER_CPF_H_
#define CIPSTER_CPF_H_

#include "typedefs.h"
#include "ciptypes.h"
#include "encap.h"


/** @ingroup ENCAP
 * @brief CPF is Common Packet Format
 * CPF packet := <number of items> {<items>}
 * item := <TypeID> <Length> <data>
 * <number of items> := two bytes
 * <TypeID> := two bytes
 * <Length> := two bytes
 * <data> := <the number of bytes specified by Length>
 */

// * @brief Definition of Item ID numbers used for address and data items in CPF structures
enum CipItemId
{
    kCipItemIdNullAddress = 0x0000,                             ///< Type: Address; Indicates that encapsulation routing is not needed.
    kCipItemIdListIdentityResponse = 0x000C,
    kCipItemIdConnectionAddress = 0x00A1,                       ///< Type: Address; Connection-based, used for connected messages, see Vol.2, p.42
    kCipItemIdConnectedDataItem = 0x00B1,                       ///< Type: Data; Connected data item, see Vol.2, p.43
    kCipItemIdUnconnectedDataItem   = 0x00B2,                   ///< Type: Data; Unconnected message
    kCipItemIdListServiceResponse   = 0x0100,
    kCipItemIdSocketAddressInfoOriginatorToTarget   = 0x8000,   ///< Type: Data; Sockaddr info item originator to target
    kCipItemIdSocketAddressInfoTargetToOriginator   = 0x8001,   ///< Type: Data; Sockaddr info item target to originator
    kCipItemIdSequencedAddressItem = 0x8002                     ///< Sequenced Address item
};


struct AddressData
{
    EipUint32   connection_identifier;
    EipUint32   sequence_number;
};


struct AddressItem
{
    CipUint     type_id;
    CipUint     length;
    AddressData data;
};


struct DataItem
{
    EipUint16   type_id;
    EipUint16   length;
    EipUint8*   data;
};


struct SocketAddressInfoItem
{
    SocketAddressInfoItem( CipItemId aType, CipUdint aIP, int aPort );

    SocketAddressInfoItem() :
        type_id( 0 ),
        length( 16 ),
        sin_family( AF_INET ),
        sin_port( 0 ),
        sin_addr( 0 )
    {
        memset( nasin_zero, 0, sizeof nasin_zero );
    }

    CipUint     type_id;
    CipUint     length;
    CipInt      sin_family;
    CipUint     sin_port;
    CipUdint    sin_addr;
    CipUsint    nasin_zero[8];

    SocketAddressInfoItem& operator=( sockaddr_in& rhs )
    {
        sin_family  = rhs.sin_family;
        sin_port    = ntohs( rhs.sin_port );
        sin_addr    = ntohl( rhs.sin_addr.s_addr );
    }

    operator sockaddr_in ()
    {
        sockaddr_in lhs;

        memset( &lhs, 0, sizeof lhs );

        lhs.sin_family      = sin_family;
        lhs.sin_port        = htons( sin_port );
        lhs.sin_addr.s_addr = htonl( sin_addr );

        return lhs;
    }
};


// this one case of a CPF packet is supported:

class CipCommonPacketFormatData
{
public:
    int         item_count;
    AddressItem address_item;
    DataItem    data_item;

    void Clear()
    {
        item_count = 0;
        rx_aii_count = 0;
        tx_aii_count = 0;
    }

    void ClearTx()
    {
        tx_aii_count = 0;
    }

    void ClearRx()
    {
        rx_aii_count = 0;
    }


    /** @ingroup ENCAP
     *  Create CPF structure out of the received data.
     *  @param  data		pointer to data which need to be structured.
     *  @param  data_length	length of data in pa_Data.
     *  @param  common_packet_format_data	pointer to structure of CPF data item.
     *  @return status
     *         EIP_OK .. success
     *         EIP_ERROR .. error
     */
    EipStatus Init( EipUint8* data, int data_length );

    /** @ingroup ENCAP
     * Copy data from CPFDataItem into linear memory in message for transmission over in encapsulation.
     * @param  response  pointer to message router response which has to be aligned into linear memory.
     * @param  common_packet_format_data_item pointer to CPF structure which has to be aligned into linear memory.
     * @param  message    pointer to linear memory.
     * @return length of reply in pa_msg in bytes
     *     EIP_ERROR .. error
     */
    int AssembleIOMessage( EipUint8* message );

    /** @ingroup ENCAP
     * Copy data from MRResponse struct and CPFDataItem into linear memory in message for transmission over in encapsulation.
     * @param  response	pointer to message router response which has to be aligned into linear memory.
     * @param  common_packet_format_data_item	pointer to CPF structure which has to be aligned into linear memory.
     * @param  message		pointer to linear memory.
     * @return length of reply in pa_msg in bytes
     *     EIP_ERROR .. error
     */
    int AssembleLinearMessage( CipMessageRouterResponse* response, EipUint8* message );

    bool AppendRx( const SocketAddressInfoItem& aSocketAddressInfoItem )
    {
        if( rx_aii_count < DIM( rx_aii ) )
        {
            rx_aii[rx_aii_count++] = aSocketAddressInfoItem;
            return true;
        }
        return false;
    }

    bool AppendTx( const SocketAddressInfoItem& aSocketAddressInfoItem )
    {
        if( tx_aii_count < DIM( tx_aii ) )
        {
            tx_aii[tx_aii_count++] = aSocketAddressInfoItem;
            return true;
        }
        return false;
    }

    SocketAddressInfoItem* SearchRx( CipItemId aType )
    {
        for( int i=0; i<rx_aii_count;  ++i )
        {
            if( rx_aii[i].type_id == aType )
                return &rx_aii[i];
        }
        return NULL;
    }

    SocketAddressInfoItem* SearchTx( CipItemId aType )
    {
        for( int i=0; i<tx_aii_count;  ++i )
        {
            if( tx_aii[i].type_id == aType )
                return &tx_aii[i];
        }
        return NULL;
    }

    int RxSocketAddressInfoItemCount() const    { return rx_aii_count; }
    int TxSocketAddressInfoItemCount() const    { return tx_aii_count; }

    SocketAddressInfoItem* RxSocketAddressInfoItem( int aIndex )
    {
        if( aIndex < rx_aii_count )
        {
            return rx_aii + aIndex;
        }
        return NULL;
    }

    SocketAddressInfoItem* TxSocketAddressInfoItem( int aIndex )
    {
        if( aIndex < tx_aii_count )
        {
            return tx_aii + aIndex;
        }
        return NULL;
    }

private:
    int rx_aii_count;
    SocketAddressInfoItem rx_aii[2];

    int tx_aii_count;
    SocketAddressInfoItem tx_aii[2];
};


/** @ingroup ENCAP
 * Parse the CPF data from a received unconnected explicit message and
 * hand the data on to the message router
 *
 * @param  received_data pointer to the encapsulation structure with the received message
 * @param  reply_buffer reply buffer
 * @return number of bytes to be sent back. < 0 if nothing should be sent
 */
int NotifyCommonPacketFormat( EncapsulationData* received_data,
        EipUint8* reply_buffer );

/** @ingroup ENCAP
 * Parse the CPF data from a received connected explicit message, check
 * the connection status, update any timers, and hand the data on to
 * the message router
 *
 * @param  received_data pointer to the encapsulation structure with the received message
 * @param  reply_buffer reply buffer
 * @return number of bytes to be sent back. < 0 if nothing should be sent
 */
int NotifyConnectedCommonPacketFormat( EncapsulationData* received_data,
        EipUint8* reply_buffer );


/** @ingroup ENCAP
 * @brief Data storage for the any CPF data
 * Currently we are single threaded and need only one CPF at the time.
 * For future extensions towards multithreading maybe more CPF data items may be necessary
 */
extern CipCommonPacketFormatData g_cpf;

#endif    // CIPSTER_CPF_H_
