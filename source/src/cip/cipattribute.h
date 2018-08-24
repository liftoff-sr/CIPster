
/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/


#ifndef CIPATTRIBUTE_H_
#define CIPATTRIBUTE_H_

#define USE_MEMBER_FUNC_FOR_ATTRIBUTE_FUNC  0

#include "ciptypes.h"

// return a uint16_t to ensure that this fires the correct overload of
// CipClass::AttributeInsert(), namely one that will set
// CipAttribute::is_offset_from_instance_start = true before returning.
// Will have to redefine INSTANCE_CLASS as explained below.
// The '1' trick quiets older noisy compilers.
#define memb_offs(Member) uint16_t( \
    uintptr_t(&reinterpret_cast<INSTANCE_CLASS*>(1)->Member) - 1)

/*
    For each class derived from CipInstance you may
#undef INSTANCE_CLASS
#define INSTANCE_CLASS  DerivedInstance
    in the implementation C++ *.cc file.  See cipassembly.cc for an example.
*/
#define INSTANCE_CLASS  CipInstance

class CipMessageRouterRequest;
class CipMessageRouterResponse;
class CipAttribute;
class CipInstance;


/** @ingroup CIP_API
 * @typedef  EipStatus (*AttributeFunc)( CipAttribute *,
 *    CipMessageRouterRequest*, CipMessageRouterResponse*)
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

#if USE_MEMBER_FUNC_FOR_ATTRIBUTE_FUNC
typedef EipStatus (CipInstance::*AttributeFunc) (CipAttribute* aAttribute,
            CipMessageRouterRequest* aRequest,
            CipMessageRouterResponse* aResponse);
#else
typedef EipStatus (*AttributeFunc)( CipInstance* aInstance, CipAttribute* aAttribute,
            CipMessageRouterRequest* aRequest,
            CipMessageRouterResponse* aResponse );
#endif

/**
 * Class CipAttribute
 * holds info for a CIP attribute which may be ether:
 * 1) contained by a #CipInstance or
 * 2) a global or static variable.
 * If contained by an instance, then the @a where field is setup to hold an
 * offset from the start of the CipInstance base pointer, and this is
 * demarkated by setting @a is_offset_from_instance_start true.  Otherwise
 * @a where holds a true pointer to the static or global variable which is not
 * an instance member of the CipInstance derivative.
 *
 * There is no final public accessor for "where", as this is done only by the
 * friend class CipInstance, via CipInstance::Data(CipAttribute*).
 */
class CipAttribute
{
    friend class CipInstance;
    friend class CipClass;

public:
    CipAttribute(
            int             aAttributeId,
            CipDataType     aType,
            AttributeFunc   aGetter,
            AttributeFunc   aSetter,
            uintptr_t       aData,
            bool            isGetableAll,
            bool            isDataAnInstanceOffset = true
            );

    //~CipAttribute() {}

    int         Id() const              { return attribute_id; }
    CipDataType Type() const            { return type; }
    //void*       Data() const            { return data; }

    bool    IsGetableSingle()           { return getter != NULL; }
    bool    IsSetableSingle()           { return setter != NULL; }
    bool    IsGetableAll()              { return is_getable_all; }

    /**
     * Function Get
     * is called by GetAttributeSingle and GetAttributeAll services
     */
    EipStatus Get(
            CipInstance* aInstance,
            CipMessageRouterRequest* aRequest,
            CipMessageRouterResponse* aResponse
            );

    /**
     * Function Set
     * is called by SetAttributeSingle
     */
    EipStatus Set(
            CipInstance* aInstance,
            CipMessageRouterRequest* aRequest,
            CipMessageRouterResponse* aResponse
            );

    //-------<AttrubuteFuncs>---------------------------------------------------

    // Standard attribute getter functions, and you may add your own elsewhere also:
    static EipStatus GetAttrData( CipInstance* aInstance, CipAttribute* attr,
            CipMessageRouterRequest* request, CipMessageRouterResponse* response );

    // Standard attribute setter functions, and you may add your own elsewhere also:
    static EipStatus SetAttrData( CipInstance* aInstance, CipAttribute* attr,
            CipMessageRouterRequest* request,  CipMessageRouterResponse* response );

    //-------</AttrubuteFuncs>--------------------------------------------------

protected:

    int             attribute_id;
    CipDataType     type;
    bool            is_getable_all;
    bool            is_offset_from_instance_start;  // or pointer to static or global
    uintptr_t       where;
    CipClass*       owning_class;

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

typedef std::vector<CipAttribute*>      CipAttributes;

#endif  // CIPATTRIBUTE_H_
