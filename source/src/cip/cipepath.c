/*******************************************************************************
 * Copyright (c) 2016, SoftPLC Corportion.
 *
 * All rights reserved.
 *
 ******************************************************************************/

#include "cipepath.h"
#include "endianconv.h"


int CipAppPath::SerializePadded( EipByte* aDst, EipByte* aLimit )
{
    EipByte*   p = aDst;

    if( HasClass() )
    {
        int class_id = GetClass();

        if( class_id < 256 )
        {
            *p++ = 0x20;   // 8 Bit Class Id
            *p++ = (EipByte) class_id;
        }
        else
        {
            *p++ = 0x20 | 1;
            *p++ = 0;      // pad byte
            AddIntToMessage( class_id, &p );
        }
    }

    if( HasInstance() )
    {
        int instance_id = GetInstance();

        if( instance_id < 256 )
        {
            *p++ = 0x24;
            *p++ = (EipByte) instance_id;
        }
        else
        {
            *p++ = 0x24 | 1;
            *p++ = 0;
            AddIntToMessage( instance_id, &p );
        }
    }

    if( HasAttribute() )
    {
        int attr_id = GetAttribute();

        if( attr_id < 256 )
        {
            *p++ = 0x30;
            *p++ = (EipByte) attr_id;
        }
        else
        {
            *p++ = 0x30 | 1;
            *p++ = 0;
            AddIntToMessage( attr_id, &p );
        }
    }

    if( HasConnPt() )
    {
        int cn_pt = GetConnPt();

        if( cn_pt < 256 )
        {
            *p++ = 0x2c;
            *p++ = (EipByte) cn_pt;
        }
        else
        {
            *p++ = 0x2c | 1;
            *p++ = 0;
            AddIntToMessage( cn_pt, &p );
        }
    }

    return p - aDst;
}


int CipAppPath::DeserializePadded( EipByte* aSrc, EipByte* aLimit )
{
    EipByte*    p = aSrc;
    Stuff       last_member = STUFF_COUNT;      // exit loop when higher member
                                                // is seen, C-1.6 of Vol1_3.19
    Clear();

    while( p < aLimit )
    {
        int first = *p;

        int seg_type = 0xfc & first;
        int format   = 0x03 & first;

        Stuff   next;

        switch( seg_type )
        {
        case kLogicalSegmentTypeClassId:            next = CLASS;       break;
        case kLogicalSegmentTypeInstanceId:         next = INSTANCE;    break;
        case kLogicalSegmentTypeAttributeId:        next = ATTRIBUTE;   break;
        case kLogicalSegmentTypeConnectionPoint:    next = CONN_PT;     break;

        default:
            // C-1.6 of Vol1_3.19; is an expected termination point, not an error.
            goto exit;
        }

        if( next >= last_member )
        {
            // C-1.6 of Vol1_3.19; is an expected termination point, not an error.
            goto exit;
        }

        ++p;        // ate first

        int value;

        if( format == 0 )
            value = *p++;
        else if( format == 1 )
        {
            ++p;
            value = GetIntFromMessage( &p );
        }
        else if( format == 2 )
        {
            ++p;
            value = GetDintFromMessage( &p );
        }
        else
        {
            CIPSTER_TRACE_ERR( "%s: unexpected format in logical segment\n", __func__ );
            value = 0;
        }

        // a contrived Set*() mimic:
        stuff[next] = value;
        pbits |= (1 << next);

        last_member = next;
    }

exit:

    int byte_count = p - aSrc;

    if( byte_count && hierarchical_parent )
        inherit( last_member + 1 );

    return byte_count;
}


void CipAppPath::inherit( int aStart )
{
    CIPSTER_ASSERT( hierarchical_parent );

    for( int i=aStart;  i < STUFF_COUNT;  ++i )
    {
        if( !( pbits & (1<<i) ) && ( hierarchical_parent->pbits & (1<<i) ) )
        {
            stuff[i] = hierarchical_parent->stuff[i];
            pbits |= (1<<i);
        }
    }
}


static void parsePortSegment( CipPortSegment* aSegment, EipUint8** aMessage )
{
    EipUint8*   p = *aMessage;
    EipUint8    first = *p++;

    // p points to 2nd byte here.
    int link_addrz = (first & 0x10) ? *p++ : 0;

    if( (first & 0xf) == 15 )
        aSegment->port = GetIntFromMessage( &p );
    else
        aSegment->port = first & 0xf;

    aSegment->link_address.clear();

    while( link_addrz-- )
    {
        aSegment->link_address.push_back( *p++ );
    }

    // skip a byte if not an even number of them have been consumed.
    if( (p - *aMessage) & 1 )
        ++p;

    *aMessage = p;
}


int CipPortSegmentGroup::DeserializePadded( EipByte* aSrc, EipByte* aLimit )
{
    EipByte*    p = aSrc;

    Clear();

    while( p < aLimit )
    {
        int first = *p;

        if( (first & 0xe0) == kSegmentTypePort )
        {
            parsePortSegment( &port, &p );
            pbits |= (1<<PORT);
        }
        else
        {
            switch( first )
            {
            case 0x34:      // electronic key
                ++p;

                int key_format;

                key_format = *p++;

                if( key_format != 4 )
                {
                    CIPSTER_TRACE_ERR( "%s: unknown electronic key format: %d\n",
                        __func__, key_format );

                    return aSrc - (p - 1);    // return negative byte offset of error
                }

                key.vendor_id      = GetIntFromMessage( &p );
                key.device_type    = GetIntFromMessage( &p );
                key.product_code   = GetIntFromMessage( &p );
                key.major_revision = *p++;
                key.minor_revision = *p++;
                pbits |= (1<<KEY);
                break;

            case 0x43:
                ++p;
                pit_msecs = *p++;
                pbits |= (1<<PIT_MSECS);
                break;

            case 0x51:
                ++p;

                int num_words;
                num_words = *p++;

                if( num_words == 1 )
                {
                    pit_usecs = GetIntFromMessage( &p );
                    pbits |= (1<<PIT_USECS);
                }
                else if( num_words == 2 )
                {
                    pit_usecs = GetDintFromMessage( &p );
                    pbits |= (1<<PIT_USECS);
                }
                else
                {
                    CIPSTER_TRACE_ERR( "%s: unknown PIT_USECS format: %d\n", __func__, num_words );
                    return aSrc - (p - 1);    // return negative byte offset of error
                }
                break;

            default:
                goto exit;
            }
        }
    }

exit:
    return p - aSrc;
}


int CipSimpleDataSegment::DeserializePadded( EipByte* aSrc, EipByte* aLimit )
{
    EipByte*    p = aSrc;

    Clear();

    int first = *p++;

    if( first == kSegmentTypeData )
    {
        int word_count = *p++;

        while( word_count-- )
        {
            CipWord w = GetIntFromMessage( &p );
            words.push_back( w );
        }

        pbits |= 1; // caller can use HasAny()
    }

    return p - aSrc;
}

