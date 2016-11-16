/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * All rights reserved.
 *
 ******************************************************************************/
#ifndef CIPSTER_CIPMESSAGEROUTER_H_
#define CIPSTER_CIPMESSAGEROUTER_H_

#include "typedefs.h"
#include "ciptypes.h"
#include "cipepath.h"


/**
 * Struct CipMessageRouterRequest
 */
struct CipMessageRouterRequest
{
    CipUsint            service;
    CipAppPath          request_path;
    CipBufNonMutable    data;

    /**
     * Function DeserializeMRR
     * parses the UCMM header consisting of: service, IOI size, IOI,
     * data into a request structure
     *
     * @param aCommand the serizlized CPFD data item, i.e. CIP command
     * @return int - number of bytes consumed or -1 if error.
     */
    int DeserializeMRR( CipBufNonMutable aCommand );
};


/**
 * Class CipMessageRouterResponse
 *
 */
class CipMessageRouterResponse
{
public:
    CipUsint reply_service;                 ///< Reply service code, the requested service code + 0x80
    CipOctet reserved;                      ///< Reserved; Shall be zero

    CipUsint general_status;                ///< One of the General Status codes listed in CIP
                                            ///< Specification Volume 1, Appendix B

    CipUsint size_of_additional_status;     ///< Number of additional 16 bit words in Additional Status Array

    EipUint16 additional_status[2];         ///< Array of 16 bit words; Additional status;
                                            ///< If SizeOfAdditionalStatus is 0. there is no
                                            ///< Additional Status

    CipBufMutable   data;                   ///< where to put CIP reply with data.size() limit
    int             data_length;            ///< how many bytes actually filled at data.data().

    CipMessageRouterResponse() :
        reply_service( 0 ),
        reserved( 0 ),
        general_status( 0 ),
        size_of_additional_status( 0 ),
        data( mmr_temp.data(), mmr_temp.size() ),
        data_length( 0 )
    {
        memset( additional_status, 0, sizeof additional_status );
    }

    /// Return a CipBuf holding the serialized CIP response.
    CipBufNonMutable Payload() const
    {
        return CipBufNonMutable( data.data(), data_length );
    }

private:
    // The common packet format makes it impossible to avoid copying the
    // reply payload because there's no way to know length in advance.
    // So the message router response is generated into a temporary location
    // and then copied to the final buffer for sending on the wire.  Since we
    // are single threaded, we can use a common temporary buffer for all
    // messages.  However, hide that strategy so it can be easily changed in
    // the future.

    static std::vector<EipByte>     mmr_temp;
};


// public functions

/** @brief Initialize the data structures of the message router
 *  @return kEipStatusOk if class was initialized, otherwise kEipStatusError
 */
EipStatus CipMessageRouterInit();

/** @brief Free all data allocated by the classes created in the CIP stack
 */
void DeleteAllClasses();

/**
 * Function NotifyMR
 * notifies the MessageRouter that an explicit message (connected or unconnected)
 * has been received. This function will be called from the encapsulation layer.
 *
 * @param aCommand CPFD data payload which is the CIP part.
 * @param aReply where to put the reply, must fill in
 *   CipMessageRouterResponse and its data CipBufMutable and data_length.  This is
 *   how caller knows the length.
 * @return EipStatus
 */
EipStatus NotifyMR( CipBufNonMutable aCommand, CipMessageRouterResponse* aReply );

/**
 * Function RegisterCipClass
 * registers class at the message router.
 * In order that the message router can deliver
 * explicit messages each class has to register.
 * @param aClass CIP class to be registered
 * @return EIP_OK on success
 */
EipStatus RegisterCipClass( CipClass* aClass );

#endif // CIPSTER_CIPMESSAGEROUTER_H_
