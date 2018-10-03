/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corporation.
 *
 ******************************************************************************/
#ifndef CIPSTER_CIPMESSAGEROUTER_H_
#define CIPSTER_CIPMESSAGEROUTER_H_

#include <typedefs.h>
#include "ciptypes.h"
#include "cipepath.h"
#include "cipclass.h"
#include "cipcommon.h"


/**
 * Struct CipMessageRouterRequest
 * See Vol1 - 2-4.1
 */
class CipMessageRouterRequest : public Serializeable
{
public:

    CipMessageRouterRequest() :
        service( CIPServiceCode( 0 ) )
    {}

    CipMessageRouterRequest(
            CIPServiceCode aService,
            const CipAppPath& aPath,
            const BufReader& aData ) :
        service( aService ),
        path( aPath ),
        data( aData )
    {}

    CIPServiceCode Service() const              { return service; }
    void SetService( CIPServiceCode aService )  { service = aService; }

    const CipAppPath& Path() const              { return path; }
    void SetPathAttribute( int aId )            { path.SetAttribute( aId ); }

    const BufReader& Data() const               { return data; }
    void SetData( const BufReader& aRdr )       { data = aRdr; }

    /**
     * Function DeserializeMRReq
     * parses the UCMM header consisting of: service, IOI size, IOI,
     * data into a request structure
     *
     * @param aCommand the serialized CPFD data item, i.e. CIP command
     * @return int - number of bytes consumed or -1 if error.
     */
    int DeserializeMRReq( BufReader aCommand );

    //-----<Serializeable>------------------------------------------------------
    int Serialize( BufWriter aOutput, int aCtl = 0 ) const;
    int SerializedCount( int aCtl = 0) const;
    //-----</Serializeable>-----------------------------------------------------

protected:

    CIPServiceCode  service;
    CipAppPath      path;
    BufReader       data;
};


class Cpf;

/**
 * Class CipMessageRouterResponse
 *
 */
class CipMessageRouterResponse : public Serializeable
{
public:
    CipMessageRouterResponse( Cpf* aCpf,
        BufWriter aOutput = BufWriter( mmr_temp.data(), mmr_temp.size() ) );

    void Clear();

    Cpf* CPF() const                { return cpf; }
    void SetCPF( Cpf* aCpf )        { cpf = aCpf; }

    /**
     * Function DeserializeMRRes
     * deserializes a message router response status as it comes back from a target.
     * It stops after the generic status information.  After that a call to
     * this->Reader() will return a reader which gives access to the service
     * specific response.
     *
     * @return int - the number of bytes consumed.
     */
    int DeserializeMRRes( BufReader aReply );

    //-----<Serializeable>------------------------------------------------------
    int Serialize( BufWriter aOutput, int aCtl = 0 ) const;
    int SerializedCount( int aCtl = 0 ) const;
    //-----</Serializeable>-----------------------------------------------------


    //-----<Status Values>------------------------------------------------------

    CipMessageRouterResponse& SetService( CIPServiceCode aService )
    {
        reply_service = aService;
        return *this;
    }

    CIPServiceCode Service() const  { return reply_service; }

    CipMessageRouterResponse& SetGenStatus( CipError aError )
    {
        general_status = aError;
        return *this;
    }

    CipError GenStatus() const      { return general_status; }

    /// Append an additional status word to response
    CipMessageRouterResponse& AddAdditionalSts( CipUint aStsWord )
    {
        if( size_of_additional_status < DIM( additional_status ) )
        {
            additional_status[ size_of_additional_status++ ] = aStsWord;
        }
        return *this;
    }

    int AdditionalStsCount() const  { return size_of_additional_status; }

    ConnMgrStatus ExtStatus() const
    {
        return ConnMgrStatus( size_of_additional_status ? additional_status[ 0 ] : 0 );
    }

    //-----<Data buffer stuff >------------------------------------------------

    /// Return a BufWriter which defines a buffer to be filled with the
    /// serialized reply for sending.
    BufWriter  Writer() const               { return data; }

    void WriterAdvance( int aCount )
    {
        data = BufWriter( data ) + aCount;
    }

    void SetWriter( const BufWriter& w )    { data = w; }

    // Set the bound of a readable payload that exists beyond the received response
    // status on an originator's end.  Prepares for a call to this->Reader().
    void SetReader( const BufReader& r )
    {
        data = r;
        written_size = r.size();
    }

    void SetWrittenSize( int aSize )        { written_size = aSize; }
    int WrittenSize() const                 { return written_size; }

    void Show() const
    {
        CIPSTER_TRACE_INFO( "CipMessageRouterResponse:\n" );
        CIPSTER_TRACE_INFO( " reply_service:0x%02x\n", reply_service );
        CIPSTER_TRACE_INFO( " general_status:0x%02x\n", general_status );
        //CIPSTER_TRACE_INFO( " size_of_additional_status:%d\n", size_of_additional_status );

        for( int i=0; i<size_of_additional_status; ++i )
        {
            CIPSTER_TRACE_INFO( " additional_status[%d]:0x%x\n", i, additional_status[i] );
        }

        CIPSTER_TRACE_INFO( " msg len:%zd\n", Reader().size() );
    }

    /// Return a BufReader holding the received Serialize()d CIP reply minus
    /// the status info.
    BufReader   Reader() const              { return BufReader( data.data(), written_size ); }

protected:

    CIPServiceCode  reply_service;              // Reply service code, the requested service code + 0x80

    CipError        general_status;             // One of the General Status codes listed in CIP
                                                // Specification Volume 1, Appendix B

    CipInt          size_of_additional_status;  // Number of additional 16 bit words in additional_status[]
    CipUint         additional_status[2];

    ByteBuf         data;                       // data portion of the response
    int             written_size;               // how many bytes actually filled at data.data().

    Cpf*            cpf;

    // The common packet format makes it difficult to avoid copying the
    // reply data payload because SockAddrInfo can trail it and getting
    // accurate length info early enough is tough.
    // So for now the message router response is sometimes generated into a temporary location
    // and then copied to the final buffer for sending on the wire.  Since we
    // are single threaded, we can use a common temporary buffer for all
    // messages.  However, hide that strategy so it can be easily changed in
    // the future.

    static std::vector<uint8_t>     mmr_temp;
};


class CipMessageRouterClass : public CipClass
{
public:
    CipMessageRouterClass();

    CipError OpenConnection( ConnectionData* aConn, Cpf* aCpf,
                ConnMgrStatus* extended_error_code );    // override

    /**
     * Function NotifyMR
     * notifies the MessageRouter that an explicit message (connected or unconnected)
     * has been received. This function is called from the encapsulation layer.
     *
     * @param aRequest is a parsed CipMessageRouterRequest
     * @param aResponse is where to put the reply, must fill in
     *   CipMessageRouterResponse and its BufWriter (provided by Writer() ).
     *   Then call SetWrittenSize, which is how caller knows the length.
     *   Should not advance data.data().
     *
     * @return EipStatus - kEipStatusError if error and caller is not to send any reply.
     *                     kEipStatusOkSend if caller is to send reply, which may contain
     *                      an error indication in general status field.
     */
    static EipStatus NotifyMR(  CipMessageRouterRequest*  aRequest,
                                CipMessageRouterResponse* aResponse );

    /**
     * Function Init
     * initializes the message router support.
     *  @return kEipStatusOk if class was initialized, otherwise kEipStatusError
     */
    static EipStatus Init();

    CipInstance* CreateInstance( int aInstanceId );

    static EipStatus multiple_service_packet_service( CipInstance* instance,
        CipMessageRouterRequest*  request,
        CipMessageRouterResponse* response );
};

#endif // CIPSTER_CIPMESSAGEROUTER_H_
