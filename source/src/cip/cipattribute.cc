
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
    BufWriter out = response->data; // always copy response->data so it is not advanced.

    response->data_length = EncodeData( attr->Type(), attr->Data(), out );

    return kEipStatusOkSend;
}


EipStatus CipAttribute::SetAttrData( CipAttribute* attr,
        CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    BufReader in = request->data;

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
        response->general_status = kCipErrorAttributeNotGettable;
        return kEipStatusOkSend;
    }
    else if( request->service == kGetAttributeAll && !IsGetableAll() )
    {
        response->general_status = kCipErrorAttributeNotGettable;
        return kEipStatusOkSend;
    }
    else
    {
        CIPSTER_TRACE_INFO(
            "%s: attribute:%d  class:'%s'  instance:%d\n",
            __func__,
            request->request_path.GetAttribute(),
            Instance()->Id() == 0 ? ((CipClass*)Instance())->ClassName().c_str() :
                                    Instance()->Class()->ClassName().c_str(),
            Instance()->Id()
            );

        EipStatus ret = getter( this, request, response );

        CIPSTER_TRACE_INFO( "%s: attribute_id:%d  len:%d\n",
            __func__, Id(), response->data_length );

        return ret;
    }
}


EipStatus CipAttribute::Set( CipMessageRouterRequest* request, CipMessageRouterResponse* response )
{
    if( !IsSetableSingle() )
    {
        // it is an attribute we have, however this attribute is not setable
        response->general_status = kCipErrorAttributeNotSetable;
        return kEipStatusOkSend;
    }
    else
    {
        return setter( this, request, response );
    }
}

