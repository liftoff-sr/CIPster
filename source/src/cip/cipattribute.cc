
/*******************************************************************************
 * Copyright (c) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#include <trace.h>
#include <byte_bufs.h>
#include <cipster_api.h>


CipAttribute::CipAttribute(
        int             aAttributeId,
        CipDataType     aType,
        AttributeFunc   aGetter,
        AttributeFunc   aSetter,
        uintptr_t       aData,
        bool            isGetableAll,
        bool            isDataAnInstanceOffset
        ) :
    attribute_id( aAttributeId ),
    type( aType ),
    is_getable_all( isGetableAll ),
    where( aData ),
    owning_class( 0 ),
    getter( aGetter ),
    setter( aSetter ),
    is_offset_from_instance_start( isDataAnInstanceOffset )
{
    /*
        Is there a problem with one of the calls to CipClass::AttributeInsert()?
        Likely you want either:
        1) an offset from instance start, in which case aData's upper bits should be 0.
        2) a full address in aData with is_offset_from_instance_start == false.
    */

    CIPSTER_ASSERT( ( is_offset_from_instance_start && !(0xffff0000 & aData))
        ||          (!is_offset_from_instance_start &&  (0xffff0000 & aData)) );

    CIPSTER_ASSERT( aAttributeId > 0 && aAttributeId <= 65535 );
}


EipStatus CipAttribute::GetAttrData( CipInstance* aInstance, CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    BufWriter out = response->Writer();

    response->SetWrittenSize( EncodeData( attr->Type(), aInstance->Data(attr), out ) );

    return kEipStatusOkSend;
}


EipStatus CipAttribute::SetAttrData( CipInstance* aInstance, CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    BufReader in = request->Data();

    int out_count = DecodeData( attr->Type(), aInstance->Data(attr), in );

    if( out_count >= 0 )
        return kEipStatusOkSend;
    else
        return kEipStatusError;
}


EipStatus CipAttribute::Get( CipInstance* aInstance,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    if( !IsGetableSingle() )
    {
        response->SetGenStatus( kCipErrorAttributeNotGettable );
        return kEipStatusOkSend;
    }
    else if( request->Service() == kGetAttributeAll && !IsGetableAll() )
    {
        response->SetGenStatus( kCipErrorAttributeNotGettable );
        return kEipStatusOkSend;
    }
    else
    {
        CIPSTER_TRACE_INFO(
            "%s: attribute:%d  class:'%s'  instance:%d\n",
            __func__,
            request->Path().GetAttribute(),
            aInstance->Class()->ClassName().c_str(),
            aInstance->Id()
            );

#if USE_MEMBER_FUNC_FOR_ATTRIBUTE_FUNC
        EipStatus ret = (aInstance->*getter)( this, request, response );
#else
        EipStatus ret = getter( aInstance, this, request, response );
#endif

        CIPSTER_TRACE_INFO( "%s: attribute_id:%d  len:%u\n",
            __func__, Id(), response->WrittenSize() );

        return ret;
    }
}


EipStatus CipAttribute::Set( CipInstance* aInstance,
    CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    if( !IsSetableSingle() )
    {
        // it is an attribute we have, however this attribute is not setable
        response->SetGenStatus( kCipErrorAttributeNotSetable );
        return kEipStatusOkSend;
    }
    else
    {
        return setter( aInstance, this, request, response );
    }
}
