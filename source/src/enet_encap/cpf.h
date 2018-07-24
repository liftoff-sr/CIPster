/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/
#ifndef CIPSTER_CPF_H_
#define CIPSTER_CPF_H_

#include "../typedefs.h"
#include "../cip/ciptypes.h"
#include "encap.h"


/** @ingroup ENCAP
 * CPF is Common Packet Format
 */


/**
 * Enum CpfId
 * is the set of Item ID numbers used for address and data items in CPF structures
 */
enum CpfId
{
    kCpfIdNullAddress                   = 0x0000,   ///< Address: encapsulation routing is not needed.
    kCpfIdListIdentityResponse          = 0x000C,
    kCpfIdConnectedAddress              = 0x00A1,   ///< Address: connection-based, used for connected messages, see Vol2 2-6.22
    kCpfIdConnectedDataItem             = 0x00B1,   ///< Data: connected data item, see Vol.2, p.43
    kCpfIdUnconnectedDataItem           = 0x00B2,   ///< Data: Unconnected message
    kCpfIdListServiceResponse           = 0x0100,
    kCpfIdSockAddrInfo_O_to_T           = 0x8000,   ///< Data: Sockaddr Info Item originator to target
    kCpfIdSockAddrInfo_T_to_O           = 0x8001,   ///< Data: Sockaddr Info Item target to originator
    kCpfIdSequencedAddress              = 0x8002    ///< Address: Sequenced Address Item
};


/**
 * Struct AddressItem
 * is storage for the first part of the Common Packet Format.
 * @see #Cpf
 */
struct AddressItem
{
    AddressItem(
            CpfId aAddrType = kCpfIdNullAddress,
            CipUdint aConnId = 0,
            CipUdint aEncapSeqNum = 0
            ) :
        type_id( aAddrType ),
        connection_identifier( aConnId ),
        encap_sequence_number( aEncapSeqNum )
    {
        if( aAddrType == kCpfIdNullAddress )
            length = 4;
        else if( aAddrType == kCpfIdConnectedAddress )
            length = 8;
        else if( aAddrType == kCpfIdSequencedAddress )
            length = 12;
        else
            length = 0;
    }

    CpfId       type_id;
    CipUint     length;
    EipUint32   connection_identifier;
    EipUint32   encap_sequence_number;
};


/**
 * Struct DataItem
 * is storage for the second part of the Common Packet Format.
 * @see #Cpf
 */
struct DataItem
{
    DataItem( CpfId aType = kCpfIdUnconnectedDataItem ) :
        type_id( aType ),
        length( 0 ),
        data( 0 )
    {}

    CpfId       type_id;
    EipUint16   length;
    EipByte*    data;
};


/**
 * Struct SockAddrInfoItem
 * is storage for the last part of the Common Packet Format.
 * @see #Cpf
 */
struct SockAddrInfoItem
{
    SockAddrInfoItem( CpfId aType, CipUdint aIP, int aPort );

    SockAddrInfoItem() :
        type_id( kCpfIdNullAddress ),
        length( 16 ),
        sin_family( AF_INET ),
        sin_port( 0 ),
        sin_addr( 0 ),
        nasin_zero()
    {
    }

    CpfId       type_id;
    CipUint     length;
    CipInt      sin_family;
    CipUint     sin_port;
    CipUdint    sin_addr;
    CipUsint    nasin_zero[8];

    /// assign from a sockaddr_in to this
    SockAddrInfoItem& operator=( sockaddr_in& rhs )
    {
        sin_family  = rhs.sin_family;
        sin_port    = ntohs( rhs.sin_port );
        sin_addr    = ntohl( rhs.sin_addr.s_addr );
        return *this;
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


/**
 * Class Cpf
 * helps serializing and deserializing Common Packet Format packet payload wrappers.
 */
class Cpf : public Serializeable
{
public:

    Cpf() :
        payload( 0 )
    {
        Clear();
    }

    Cpf( CpfId aAddrType, CpfId aDataType, Serializeable* aPayload = NULL ) :
        address_item( aAddrType, aDataType ),
        data_item( aDataType ),
        payload( aPayload ),
        rx_aii_count( 0 ),
        tx_aii_count( 0 ),
        item_count( 2 )
    {}

    Cpf( const AddressItem& aAddr, CpfId aDataType = kCpfIdConnectedDataItem ) :
        address_item( aAddr ),
        data_item( aDataType ),
        payload( 0 ),
        rx_aii_count( 0 ),
        tx_aii_count( 0 ),
        item_count( 2 )
    {}

    /**
     * Function DeserializeCpf
     * sets fields in this object from the provided serialized data.
     * Create CPF structure out of the received data.
     *
     *  @param  aSrc serialized bytes which need to be structured.
      *
     *  @return int -
     *     - >  0 : the number of bytes consumed
     *     - <= 0 : the negative offset of the problem byte
     */
    int DeserializeCpf( BufReader aSrc );

    //-----<Serializeable>------------------------------------------------------
    int SerializedCount( int aCtl = 0 ) const;
    int Serialize( BufWriter aDst, int aCtl = 0 ) const;
    //-----</Serializeable>-----------------------------------------------------

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
    int NotifyConnectedCommonPacketFormat( BufReader aCommand, BufWriter aReply );

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
    int NotifyCommonPacketFormat( BufReader aCommand, BufWriter aReply );

    void SetItemCount( int aCount )         {  item_count = aCount; }

    void Clear()
    {
        item_count = 0;
        rx_aii_count = 0;
        tx_aii_count = 0;
    }

    void ClearTx()                          { tx_aii_count = 0; }
    void ClearRx()                          { rx_aii_count = 0; }

    /**
     * Function AddNullAddressItem
     * adds a Null Address Item to this common packet format object.
     */
    void AddNullAddressItem()
    {
        // Precondition: Null Address Item only valid in unconnected messages
        CIPSTER_ASSERT( data_item.type_id == kCpfIdUnconnectedDataItem );

        address_item.type_id = kCpfIdNullAddress;
        address_item.length  = 0;
    }

    bool AppendRx( const SockAddrInfoItem& aSockAddrInfoItem )
    {
        if( rx_aii_count < DIM( rx_aii ) )
        {
            rx_aii[rx_aii_count++] = aSockAddrInfoItem;
            return true;
        }
        return false;
    }

    bool AppendTx( const SockAddrInfoItem& aSockAddrInfoItem )
    {
        if( tx_aii_count < DIM( tx_aii ) )
        {
            tx_aii[tx_aii_count++] = aSockAddrInfoItem;
            return true;
        }
        return false;
    }

    const SockAddrInfoItem* SearchRx( CpfId aType ) const
    {
        for( int i=0; i<rx_aii_count;  ++i )
        {
            if( rx_aii[i].type_id == aType )
                return &rx_aii[i];
        }
        return NULL;
    }

    const SockAddrInfoItem* SearchTx( CpfId aType ) const
    {
        for( int i=0; i<tx_aii_count;  ++i )
        {
            if( tx_aii[i].type_id == aType )
                return &tx_aii[i];
        }
        return NULL;
    }

    int RxSockAddrInfoItemCount() const    { return rx_aii_count; }
    int TxSockAddrInfoItemCount() const    { return tx_aii_count; }

    SockAddrInfoItem* RxSockAddrInfoItem( int aIndex )
    {
        if( aIndex < rx_aii_count )
        {
            return rx_aii + aIndex;
        }
        return NULL;
    }

    SockAddrInfoItem* TxSockAddrInfoItem( int aIndex )
    {
        if( aIndex < tx_aii_count )
        {
            return tx_aii + aIndex;
        }
        return NULL;
    }

    BufReader DataItemPayload() const
    {
        return BufReader( data_item.data, data_item.length );
    }

    CpfId DataType() const              { return data_item.type_id; }
    Cpf&  SetDataType( CpfId aType )
    {
        data_item.type_id = aType;
        return *this;
    }

    CpfId AddrType() const              { return address_item.type_id; }
    Cpf& SetAddrType( CpfId aType )
    {
        address_item.type_id = aType;
        return *this;
    }

    Cpf& SetAddrLen( CipUint aLength )
    {
        address_item.length = aLength;
        return *this;
    }

    CipUdint AddrConnId() const     { return address_item.connection_identifier; }
    Cpf& SetAddrConnId( CipUdint aConnId )
    {
        address_item.connection_identifier = aConnId;
        return *this;
    }

    CipUdint AddrEncapSeqNum() const        { return address_item.encap_sequence_number; }
    Cpf& SetAddrEncapSeqNum( CipUdint aSeqNum )
    {
        address_item.encap_sequence_number = aSeqNum;
        return *this;
    }

    Cpf& SetPayload( Serializeable* aPayload )
    {
        payload = aPayload;
        return *this;
    }

    ByteBuf  DataRange() const
    {
        return ByteBuf( data_item.data, data_item.length );
    }

    Cpf& SetDataRange( const ByteBuf& aRange )
    {
        data_item.data   = aRange.data();
        data_item.length = aRange.size();
        return *this;
    }

protected:


    int             item_count;

    AddressItem     address_item;
    DataItem        data_item;

    Serializeable*  payload;

    int         rx_aii_count;
    SockAddrInfoItem rx_aii[2];

    int         tx_aii_count;
    SockAddrInfoItem tx_aii[2];

private:
    Cpf( const Cpf& );  // not implemented
};


#endif    // CIPSTER_CPF_H_
