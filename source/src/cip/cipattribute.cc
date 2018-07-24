
/*******************************************************************************
 * Copyright (c) 2016-2018, SoftPLC Corportion.
 *
 ******************************************************************************/

#include <byte_bufs.h>
#include <cipattribute.h>
#include <cipclass.h>
#include <cipmessagerouter.h>
#include <cipster_api.h>


CipAttribute::CipAttribute(
        int             aAttributeId,
        CipDataType     aType,
        AttributeFunc   aGetter,
        AttributeFunc   aSetter,
        void*           aData,
        bool            isGetableAll
        ) :
    attribute_id( aAttributeId ),
    type( aType ),
    is_getable_all( isGetableAll ),
    data( aData ),
    owning_instance( 0 ),
    getter( aGetter ),
    setter( aSetter )
{
}


CipAttribute::~CipAttribute()
{
}


EipStatus CipAttribute::GetAttrData( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    BufWriter out = response->Writer();

    response->SetWrittenSize( EncodeData( attr->Type(), attr->Data(), out ) );

    return kEipStatusOkSend;
}


EipStatus CipAttribute::SetAttrData( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    BufReader in = request->Data();

    int out_count = DecodeData( attr->Type(), attr->Data(), in );

    if( out_count >= 0 )
        return kEipStatusOkSend;
    else
        return kEipStatusError;
}


EipStatus CipAttribute::Get( CipMessageRouterRequest* request, CipMessageRouterResponse* response )
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
            Instance()->Id() == 0 ? ((CipClass*)Instance())->ClassName().c_str() :
                                    Instance()->Class()->ClassName().c_str(),
            Instance()->Id()
            );

        EipStatus ret = getter( this, request, response );

        CIPSTER_TRACE_INFO( "%s: attribute_id:%d  len:%u\n",
            __func__, Id(), response->WrittenSize() );

        return ret;
    }
}


EipStatus CipAttribute::Set( CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    if( !IsSetableSingle() )
    {
        // it is an attribute we have, however this attribute is not setable
        response->SetGenStatus( kCipErrorAttributeNotSetable );
        return kEipStatusOkSend;
    }
    else
    {
        return setter( this, request, response );
    }
}

