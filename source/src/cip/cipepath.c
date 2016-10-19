/*******************************************************************************
 * Copyright (c) 2016, SoftPLC Corportion.
 *
 *
 ******************************************************************************/

#include <string.h>

#include "opener_api.h"
#include "cipepath.h"
#include "endianconv.h"


//* @brief Enum containing values which kind of logical segment is encoded
enum LogicalSegmentType
{
    kLogicalSegmentTypeClassId         = 0x00 + kSegmentTypeLogical,    ///< Class ID
    kLogicalSegmentTypeInstanceId      = 0x04 + kSegmentTypeLogical,    ///< Instance ID
    kLogicalSegmentTypeMemberId        = 0x08 + kSegmentTypeLogical,    ///< Member ID
    kLogicalSegmentTypeConnectionPoint = 0x0C + kSegmentTypeLogical,    ///< Connection Point
    kLogicalSegmentTypeAttributeId     = 0x10 + kSegmentTypeLogical,    ///< Attribute ID
    kLogicalSegmentTypeSpecial         = 0x14 + kSegmentTypeLogical,    ///< Special
    kLogicalSegmentTypeService         = 0x18 + kSegmentTypeLogical,    ///< Service ID
    kLogicalSegmentTypeExtendedLogical = 0x1C + kSegmentTypeLogical,    ///< Extended Logical
};


enum NetworkSegmentSubType
{
    kProductionTimeInhibitTimeNetworkSegment = 0x43 ///< production inhibit time network segment
};


enum DataSegmentType
{
    kDataSegmentTypeSimpleDataMessage         = kSegmentTypeData + 0x00,
    kDataSegmentTypeAnsiExtendedSymbolMessage = kSegmentTypeData + 0x11
};



CipAppPath& CipAppPath::operator = ( const CipAppPath& other )
{
    pbits = other.pbits;

    if( HasLogical() )
        memcpy( stuff, other.stuff, sizeof stuff );

    if( HasSymbol() )
        memcpy( tag, other.tag, sizeof tag );
}


bool CipAppPath::operator == ( const CipAppPath& other ) const
{
    if( pbits != other.pbits )
        return false;

    if( HasClass() && stuff[CLASS] != other.stuff[CLASS] )
        return false;

    if( HasInstance() && stuff[INSTANCE] != other.stuff[INSTANCE] )
        return false;

    if( HasAttribute() && stuff[ATTRIBUTE] != other.stuff[ATTRIBUTE] )
        return false;

    if( HasConnPt() && stuff[CONN_PT] != other.stuff[CONN_PT] )
        return false;

    if( HasSymbol() && strcmp( tag, other.tag ) )
        return false;

    return true;
}


bool CipAppPath::SetSymbol( const char* aSymbol )
{
    int slen = strlen( aSymbol );
    if( slen > (int) sizeof(tag) - 1 )
        return false;
    strcpy( tag, aSymbol );

    pbits |= (1<<TAG);

    return true;
}


const char* CipAppPath::GetSymbol() const
{
    return HasSymbol() ? tag : "";
}


CipClass* CipAppPath::Class() const
{
    return GetCipClass( GetClass() );
}


CipInstance* CipAppPath::Instance() const
{
    CipClass* clazz = GetCipClass( GetClass() );
    if( clazz )
    {
        return clazz->Instance( GetInstanceOrConnPt() );
    }
    return NULL;
}


CipAttribute* CipAppPath::Attribute( int aAttrId ) const
{
    CipInstance* instance = Instance();
    if( instance )
    {
        return instance->Attribute( aAttrId );
    }
    return NULL;
}


static EipByte* serialize( EipByte* p, int seg_type, unsigned aValue )
{
    if( aValue < 256 )
    {
        *p++ = seg_type;
        *p++ = aValue;
    }
    else if( aValue < 65536 )
    {
        *p++ = seg_type | 1;
        AddIntToMessage( aValue, &p );
    }
    else
    {
        *p++ = seg_type | 2;
        AddDintToMessage( aValue, &p );
    }
    return p;
}


int CipAppPath::SerializePadded( EipByte* aDst, EipByte* aLimit )
{
    EipByte*   p = aDst;

    if( HasSymbol() )
    {
        if( p < aLimit )
        {
            int tag_count = strlen( tag );

            *p++ = kDataSegmentTypeAnsiExtendedSymbolMessage;
            *p++ = tag_count;

            for( int i = 0; i < tag_count;  ++i )
                *p++ = tag[i];

            if( (p - aDst) & 1 )
                *p++ = 0;               // output possible pad byte
        }

        if( p < aLimit && HasConnPt() )
            p = serialize( p, kLogicalSegmentTypeConnectionPoint, GetConnPt() );

        if( p < aLimit && HasMember() )
            p = serialize( p, kLogicalSegmentTypeConnectionPoint, GetConnPt() );
    }

    else    // is logical
    {
        if( p < aLimit && HasClass() )
            p = serialize( p, kLogicalSegmentTypeClassId, GetClass() );

        if( p < aLimit && HasInstance() )
            p = serialize( p, kLogicalSegmentTypeInstanceId, GetInstance() );

        if( p < aLimit && HasAttribute() )
            p = serialize( p, kLogicalSegmentTypeAttributeId, GetAttribute() );

        if( p < aLimit && HasConnPt() )
            p = serialize( p, kLogicalSegmentTypeConnectionPoint, GetConnPt() );
    }

    return p - aDst;
}


inline int CipAppPath::deserialize_logical( EipByte* aSrc, CipAppPath::Stuff aField, int aFormat )
{
    CIPSTER_ASSERT( aFormat >= 0 && aFormat <= 2 );
    CIPSTER_ASSERT( aField==MEMBER || aField==CONN_PT || aField==ATTRIBUTE ||
        aField==INSTANCE || aField==CLASS );

    EipByte* p = aSrc;

    int value;

    if( aFormat == 0 )
        value = *p++;
    else if( aFormat == 1 )
    {
        ++p;
        value = GetIntFromMessage( &p );
    }
    else if( aFormat == 2 )
    {
        ++p;
        value = GetDintFromMessage( &p );
    }

    stuff[aField] = value;
    pbits |= 1 << aField;

    return p - aSrc;
}


inline int CipAppPath::deserialize_symbolic( EipByte* aSrc, EipByte* aLimit )
{
    EipByte* p = aSrc;

    if( p < aLimit )
    {
        int first = *p;

        if( first == kDataSegmentTypeAnsiExtendedSymbolMessage )
        {
            ++p;     // ate first

            int byte_count = *p++;

            if( byte_count >= (int) sizeof(tag) )
                byte_count = sizeof(tag) - 1;

            int i = 0;
            while( i < byte_count )
                tag[i++] = *p++;

            tag[i] = 0;

            if( (p - aSrc) & 1 )
                ++p;                // consume pad byte if any

            pbits |= (1<<TAG);
        }

        else if( (first & 0xe0) == kSegmentTypeSymbolic )   // Symbolic Segment
        {
            int symbol_size = first & 0x1f; // cannot exceed 31 which is max of nul terminated 'tag'

            if( symbol_size == 0 )
            {
                CIPSTER_TRACE_ERR( "%s: saw unsupported 'extended' Symbolic Segment\n", __func__ );
                goto exit;
            }

            ++p;    // ate first

            int i = 0;
            while( i < symbol_size )
                tag[i++] = *p++;

            tag[i] = 0;

            if( (p - aSrc) & 1 )
                ++p;                // skip pad byte if any

            pbits |= (1<<TAG);
        }
    }

exit:
    return p - aSrc;
}


int CipAppPath::DeserializePadded( EipByte* aSrc, EipByte* aLimit, CipAppPath* aPreviousToInheritFrom )
{
    EipByte*    p = aSrc;
                                                // is seen, C-1.6 of Vol1_3.19
    Clear();

    int result = deserialize_symbolic( p, aLimit );

    if( result > 0 )
    {
        p += result;

        if( p < aLimit )
        {
            int first = *p;

            // Grammar in C.1.5 shows we can have Connection_Point optionally here:
            if( (first & 0xfc) == kLogicalSegmentTypeConnectionPoint )
            {
                ++p;
                p += deserialize_logical( p, CONN_PT, first & 3 );
            }
        }

        if( p < aLimit )
        {
            int first = *p;

            /*

                Grammar in C.1.5 shows we can have member_specification optionally here.

                member id is the "element id" found in A-B publication
                "Logix5000 Data Access" (Rockwell Automation Publication
                1756-PM020D-EN-P - June 2016) and is expected only in the
                context of a symbolic address accoring to that document.

            */
            if( (first & 0xfc) == kLogicalSegmentTypeMemberId )
            {

                ++p;
                p += deserialize_logical( p, MEMBER, first & 3 );
            }
        }
    }

    else    // check for logical rather than symbolic
    {
        Stuff   last_member = STUFF_COUNT;      // exit loop when higher member

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
                goto logical_exit;
            }

            if( next >= last_member )
            {
                // C-1.6 of Vol1_3.19; is an expected termination point, not an error.
                goto logical_exit;
            }

            ++p;  // ate first

            p += deserialize_logical( p, next, format );

            last_member = next;
        }

logical_exit:
        if( p > aSrc && aPreviousToInheritFrom )
            inherit( last_member + 1, aPreviousToInheritFrom );
    }

exit:
    int byte_count = p - aSrc;

    return byte_count;
}


void CipAppPath::inherit( int aStart, CipAppPath* aPreviousToInheritFrom )
{
    CIPSTER_ASSERT( aPreviousToInheritFrom );

    for( int i=aStart;  i < STUFF_COUNT;  ++i )
    {
        if( !( pbits & (1<<i) ) && ( aPreviousToInheritFrom->pbits & (1<<i) ) )
        {
            stuff[i] = aPreviousToInheritFrom->stuff[i];
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

    if( first == kDataSegmentTypeSimpleDataMessage )
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

