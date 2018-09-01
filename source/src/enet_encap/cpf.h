/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016, SoftPLC Corporation.
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
 * @see #Cpf
 */
enum CpfId
{
    kCpfIdEmpty                 = -1,       ///< invalid but marks item as empty
    kCpfIdNullAddress           = 0x0000,   ///< Address: encapsulation routing is not needed.
    kCpfIdListIdentityResponse  = 0x000C,
    kCpfIdConnectedAddress      = 0x00A1,   ///< Address: connection-based, used for connected messages, see Vol2 2-6.22
    kCpfIdConnectedDataItem     = 0x00B1,   ///< Data: connected data item, see Vol.2, p.43
    kCpfIdUnconnectedDataItem   = 0x00B2,   ///< Data: Unconnected message
    kCpfIdListServiceResponse   = 0x0100,
    kCpfIdSockAddrInfo_O_T      = 0x8000,   ///< Sockaddr Info Item originator to target
    kCpfIdSockAddrInfo_T_O      = 0x8001,   ///< Sockaddr Info Item target to originator
    kCpfIdSequencedAddress      = 0x8002,   ///< Address: Sequenced Address Item
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
    uint32_t    connection_identifier;
    uint32_t    encap_sequence_number;
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
    uint16_t    length;
    uint8_t*    data;
};


/**
 * Enum SockAddrId
 * is here to limit what can be passed to the Cpf SockAddr related functions
 * to two values rather than any of the full CpfId set.
 * @see #Cpf
 */
enum SockAddrId
{
    kSockAddr_O_T   = kCpfIdSockAddrInfo_O_T,
    kSockAddr_T_O   = kCpfIdSockAddrInfo_T_O,
};


struct SaiiPair
{
    SaiiPair() :
        has_O_T( false ),
        has_T_O( false )
    {}

    bool        has_O_T;
    bool        has_T_O;
    SockAddr    O_T;
    SockAddr    T_O;
};


/**
 * Class Cpf
 * helps serializing and deserializing Common Packet Format packet payload wrappers.
 */
class Cpf : public Serializeable
{
public:

    /**
     * Constructor
     * that takes @a aSessionHandle.  This information is simply
     * saved in this instance for use by stack functions which need to know
     * from which TCP peer that this request originated from.
     */
    Cpf( const SockAddr& aPeer, CipUdint aSessionHandle );

    Cpf( CpfId aAddrType, CpfId aDataType, Serializeable* aPayload = NULL );

    Cpf( const AddressItem& aAddr, CpfId aDataType = kCpfIdConnectedDataItem );

    /**
     * Function DeserializeCpf
     * sets fields in this object from the provided serialized data.
     * Create CPF structure out of the received data.
     *
     *  @param  aInput the byte sequence to deserialize
      *
     *  @return int -
     *     - >  0 : the number of bytes consumed
     *     - <= 0 : the negative offset of the problem byte
     */
    int DeserializeCpf( BufReader aInput );

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
     * @param aCommand encapsulation structure with the received message
     * @param aReply where to put the reply and what its size limit is.
     *
     * @return int - number of bytes to be sent back. <= 0 if nothing should be sent and is the
     *  the negative of one of the values in EncapError.
     */
    int NotifyConnectedCommonPacketFormat( BufReader aCommand, BufWriter aReply );

    /**
     * Function NotifyCommonPacketFormat
     * parses CPF data in @a aCommand and then hands the contained
     * Unconnected Data Item or Connected Data Item
     * on to the message router.  Upon return, a CPF header is placed into aReply and
     * the payload reply generated by the message router is appended also into aReply.
     *
     * @param aCommand encapsulation structure with the received message
     * @param aReply where to put the reply and what its size limit is.
     *
     * @return int - number of bytes to be sent back. <= 0 if nothing should be sent and is the
     *  the negative of one of the values in EncapError.
     */
    int NotifyCommonPacketFormat( BufReader aCommand, BufWriter aReply );

    void Clear()
    {
        address_item.type_id = kCpfIdEmpty;
        data_item.type_id    = kCpfIdEmpty;

        ClearRx_T_O().ClearRx_O_T().ClearTx_T_O().ClearTx_O_T();

        /*  no, DeserializeCpf() calls Clear() and at that time client_addr
            is already set, so we need that info.
        client_addr = NULL;
        */
    }

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

    bool HasAddr() const        { return address_item.type_id != kCpfIdEmpty; }
    bool HasData() const        { return data_item.type_id != kCpfIdEmpty; }
    bool HasRx_O_T() const      { return rx.has_O_T; }
    bool HasRx_T_O() const      { return rx.has_T_O; }
    bool HasTx_O_T() const      { return tx.has_O_T; }
    bool HasTx_T_O() const      { return tx.has_T_O; }

    void AddTx( SockAddrId aType, const SockAddr& aSockAddr )
    {
        if( aType == kSockAddr_T_O )
        {
            tx.has_T_O = true;
            tx.T_O     = aSockAddr;
        }

        else if( aType == kSockAddr_O_T )
        {
            tx.has_O_T = true;
            tx.O_T     = aSockAddr;
        }
    }

    void AddRx( SockAddrId aType, const SockAddr& aSockAddr )
    {
        if( aType == kSockAddr_T_O )
        {
            rx.has_T_O = true;
            rx.T_O     = aSockAddr;
        }

        else if( aType == kSockAddr_O_T )
        {
            rx.has_O_T = true;
            rx.O_T     = aSockAddr;
        }
    }

    const SockAddr* SaiiRx( SockAddrId aType ) const
    {
        if( aType == kSockAddr_O_T && HasRx_O_T() )
        {
            return  &rx.O_T;
        }
        else if( aType == kSockAddr_T_O && HasRx_T_O() )
        {
            return &rx.T_O;
        }
        return NULL;
    }

    const SockAddr* SaiiTx( SockAddrId aType ) const
    {
        if( aType == kSockAddr_O_T && HasTx_O_T() )
        {
            return  &tx.O_T;
        }
        else if( aType == kSockAddr_T_O && HasTx_T_O() )
        {
            return &tx.T_O;
        }
        return NULL;
    }

    Cpf& ClearRx_T_O()      { rx.has_T_O = false; return *this; }
    Cpf& ClearRx_O_T()      { rx.has_O_T = false; return *this; }
    Cpf& ClearTx_T_O()      { tx.has_T_O = false; return *this; }
    Cpf& ClearTx_O_T()      { tx.has_O_T = false; return *this; }

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

    const SockAddr* TcpPeerAddr() const         { return &tcp_peer; }

    CipUdint  SessionHandle() const             { return session_handle; }
    Cpf& SetSessionHandle( CipUdint aHndl )     { session_handle = aHndl;  return *this; }

protected:
    static int serialize_sockaddr( const SockAddr& aSockAddr, BufWriter aOutput );
    static int deserialize_sockaddr( SockAddr* aSockAddr, BufReader aInput );

    AddressItem         address_item;
    DataItem            data_item;

    SaiiPair            rx;
    SaiiPair            tx;

    Serializeable*      payload;

    CipUdint            session_handle;
    SockAddr            tcp_peer;

private:
    /*
    // not implemented
    Cpf( const Cpf& );
    Cpf& operator=(const Cpf&);
    */
};


#endif    // CIPSTER_CPF_H_
