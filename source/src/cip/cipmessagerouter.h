/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corportion.
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
    CipUsint    service;
    CipAppPath  request_path;
    BufReader   data;

    /**
     * Function DeserializeMRR
     * parses the UCMM header consisting of: service, IOI size, IOI,
     * data into a request structure
     *
     * @param aCommand the serizlized CPFD data item, i.e. CIP command
     * @return int - number of bytes consumed or -1 if error.
     */
    int DeserializeMRR( BufReader aCommand );
};

class CipCommonPacketFormatData;

/**
 * Class CipMessageRouterResponse
 *
 */
class CipMessageRouterResponse
{
public:
    CipMessageRouterResponse( CipCommonPacketFormatData* cpfd );

    /// Return a CipBuf holding the serialized CIP response.
    BufReader Payload() const
    {
        return BufReader( data.data(), data_length );
    }

    CipCommonPacketFormatData* CPFD() const     { return cpfd; }


    CipUsint reply_service;             ///< Reply service code, the requested service code + 0x80
    CipOctet reserved;                  ///< Reserved; Shall be zero

    CipUsint general_status;            ///< One of the General Status codes listed in CIP
                                        ///< Specification Volume 1, Appendix B

    CipUsint size_of_additional_status; ///< Number of additional 16 bit words in Additional Status Array

    EipUint16 additional_status[2];     ///< Array of 16 bit words; Additional status;
                                        ///< If SizeOfAdditionalStatus is 0. there is no
                                        ///< Additional Status

    BufWriter   data;                   ///< where to put CIP reply with data.size() limit
    int         data_length;            ///< how many bytes actually filled at data.data().


private:

    CipCommonPacketFormatData*  cpfd;

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
 *   CipMessageRouterResponse and its BufWriter 'data' and data_length.  This is
 *   how caller knows the length.
 * @return EipStatus
 */
EipStatus NotifyMR( BufReader aCommand, CipMessageRouterResponse* aReply );

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
