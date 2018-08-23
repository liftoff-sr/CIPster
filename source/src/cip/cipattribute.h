
/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/


#ifndef CIPATTRIBUTE_H_
#define CIPATTRIBUTE_H_

#include <functional>
#include "ciptypes.h"

class CipMessageRouterRequest;
class CipMessageRouterResponse;
class CipAttribute;
class CipInstance;


/** @ingroup CIP_API
 * @typedef  std::function<EipStatus (CipAttribute*, CipMessageRouterRequest*, CipMessageRouterResponse*)> AttributeFunc
 *
 * @brief Signature definition for the implementation of CIP services.
 *
 * @param aAttribute
 *
 * @param aRequest request data
 *
 * @param aResponse storage for the response data, including a buffer for
 *      extended data.  Do not advance aResponse->data BufWriter, but rather only set
 *      aRequest->data_length to the number of bytes written to aReponse->data.
 *
 * @return kEipStatusOk_SEND if service could be executed successfully and a response
 *  should be sent
 */

typedef std::function<EipStatus (CipAttribute*, CipMessageRouterRequest*, CipMessageRouterResponse*)> AttributeFunc;


/**
 * Class CipAttribute
 * holds info for a CIP attribute which may be contained by a #CipInstance
 */
class CipAttribute
{
    friend class CipInstance;

public:
    CipAttribute(
            int             aAttributeId,
            CipDataType     aType,
            AttributeFunc   aGetter,
            AttributeFunc   aSetter,
            void*           aData,
            bool            isGetableAll
            );

    virtual ~CipAttribute();

    int     Id() const                  { return attribute_id; }

    CipInstance* Instance() const       { return owning_instance; }

    CipDataType Type() const            { return type; }
    void*       Data() const            { return data; }

    bool    IsGetableSingle()           { return getter != NULL; }
    bool    IsSetableSingle()           { return setter != NULL; }
    bool    IsGetableAll()              { return is_getable_all; }


    /**
     * Function Get
     * is called by GetAttributeSingle and GetAttributeAll services
     */
    EipStatus Get( CipMessageRouterRequest* request, CipMessageRouterResponse* response );

    /**
     * Function Set
     * is called by SetAttributeSingle
     */
    EipStatus Set( CipMessageRouterRequest* request, CipMessageRouterResponse* response );

    //-------<AttrubuteFuncs>---------------------------------------------------

    // Standard attribute getter functions, and you may add your own elsewhere also:
    static EipStatus GetAttrData( CipAttribute* attr, CipMessageRouterRequest* request,
                    CipMessageRouterResponse* response );

    // Standard attribute setter functions, and you may add your own elsewhere also:
    static EipStatus SetAttrData( CipAttribute* attr, CipMessageRouterRequest* request,
                    CipMessageRouterResponse* response );

    //-------</AttrubuteFuncs>--------------------------------------------------

protected:

    int             attribute_id;
    CipDataType     type;
    bool            is_getable_all;
    void*           data;       // no ownership of data pointed to
    CipInstance*    owning_instance;

    /**
     * Function Pointer getter
     * may be fixed during construction to a custom getter function.
     */
    const AttributeFunc   getter;

    /**
     * Function Pointer setter
     * may be fixed during construction to a custom setter function.
     */
    const AttributeFunc   setter;
};

#endif  // CIPATTRIBUTE_H_
