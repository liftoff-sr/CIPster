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


/**
 * Enum CipItemId
 * is the set of Item ID numbers used for address and data items in CPF structures
 */
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
    AddressData() :
        connection_identifier( 0 ),
        sequence_number( 0 )
    {}

    EipUint32   connection_identifier;
    EipUint32   sequence_number;
};


struct AddressItem
{
    AddressItem() :
        type_id( kCipItemIdNullAddress ),
        length( 0 )
    {}

    CipItemId   type_id;
    CipUint     length;
    AddressData data;
};


struct DataItem
{
    DataItem() :
        type_id( kCipItemIdNullAddress ),
        length( 0 ),
        data( 0 )
    {}

    CipItemId       type_id;
    EipUint16       length;
    const EipByte*  data;
};


struct SocketAddressInfoItem
{
    SocketAddressInfoItem( CipItemId aType, CipUdint aIP, int aPort );

    SocketAddressInfoItem() :
        type_id( kCipItemIdNullAddress ),
        length( 16 ),
        sin_family( AF_INET ),
        sin_port( 0 ),
        sin_addr( 0 )
    {
        memset( nasin_zero, 0, sizeof nasin_zero );
    }

    CipItemId   type_id;
    CipUint     length;
    CipInt      sin_family;
    CipUint     sin_port;
    CipUdint    sin_addr;
    CipUsint    nasin_zero[8];

    /// assign from a sockaddr_in to this
    SocketAddressInfoItem& operator=( sockaddr_in& rhs )
    {
        sin_family  = rhs.sin_family;
        sin_port    = ntohs( rhs.sin_port );
        sin_addr    = ntohl( rhs.sin_addr.s_addr );
    }

    /// convert this to a sockaddr_in
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



class CipCommonPacketFormatData
{
public:

    CipCommonPacketFormatData()
    {
        Clear();
    }


    /**
     * Function DeserializeCPFD
     * sets fields in this object from the provided serialized data.
     * Create CPF structure out of the received data.
     *
     *  @param  aSrc serialized bytes which need to be structured.
      *
     *  @return int -
     *     - >  0 : the number of bytes consumed
     *     - <= 0 : the negative offset of the problem byte
     */
    int DeserializeCPFD( CipBufNonMutable aSrc );

    /**
     * Function SerializeForMRR
     * serializes this object.
     *
     * @param  aResponse message router response or NULL if implicit response.
     * @param  aDst destination byte buffer
     * @return int - serialized byte count or -1 if error.
     */
    int SerializeCPFD( CipMessageRouterResponse* aResponse, CipBufMutable aDst );

    /**
     * Function SerializeForIO
     * serializes this object into a byte array for transmission as an implicit message.
     *
     * @param  aDataItem is the CIP response payload to serialize.
     * @param  aDst a byte buffer which is where to serialize this object
     * @return int - count of serialized bytes, or -1 if error.
     */
    int SerializeForIO( CipBufMutable aDst )
    {
        return SerializeCPFD( NULL, aDst );
    }

    void SetItemCount( int aCount )         {  item_count = aCount; }

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

    /**
     * Function AddNullAddressItem
     * adds a Null Address Item to this common packet format object.
     */
    void AddNullAddressItem()
    {
        // Precondition: Null Address Item only valid in unconnected messages
        CIPSTER_ASSERT( data_item.type_id == kCipItemIdUnconnectedDataItem );

        address_item.type_id    = kCipItemIdNullAddress;
        address_item.length     = 0;
    }

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

    CipBufNonMutable DataItemPayload() const
    {
        return CipBufNonMutable( data_item.data, data_item.length );
    }

    CipItemId DataItemType() const      { return CipItemId( data_item.type_id ); }
    CipItemId AddressItemType() const   { return CipItemId( address_item.type_id ); }

    // @todo make these private too
    AddressItem address_item;
    DataItem    data_item;

private:

    int         item_count;

    int rx_aii_count;
    SocketAddressInfoItem rx_aii[2];

    int tx_aii_count;
    SocketAddressInfoItem tx_aii[2];
};


/**
 * Function NotifyCommonPacketFormat
 * parses CPF data in @a aCommand and then hands the contained
 * Unconnected Data Item or Connected Data Item
 * on to the message router.  Upon return, a CPF header is placed into aReply and
 * the payload reply generated by the message router is appended also into aReply.
 *
 * @param  aCommand encapsulation structure with the received message
 * @param  aReply where to put the reply and what its size limit is.
 * @return int - number of bytes to be sent back. <= 0 if nothing should be sent and is the
 *  the negative of one of the values in EncapsulationProtocolErrorCode.
 */
int NotifyCommonPacketFormat( CipBufNonMutable aCommand, CipBufMutable aReply );

/**
 * Function NotifyConnectedCommonPacketFormat
 * parses the CPF data in @a aCommand which is a connected explicit message, checks
 * the connection status, updates any timers, and hands the data on to
 * the message router
 *
 * @param  aCommand encapsulation structure with the received message
 * @param  aReply where to put the reply and what its size limit is.
 * @return int - number of bytes to be sent back. <= 0 if nothing should be sent and is the
 *  the negative of one of the values in EncapsulationProtocolErrorCode.
 */
int NotifyConnectedCommonPacketFormat( CipBufNonMutable aCommand, CipBufMutable aReply );

#endif    // CIPSTER_CPF_H_
