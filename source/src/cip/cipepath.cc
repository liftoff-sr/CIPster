/*******************************************************************************
 * Copyright (c) 2016, SoftPLC Corporation.
 *
 *
 ******************************************************************************/

#include <string.h>

#include <cipster_api.h>
#include <cipepath.h>
#include <byte_bufs.h>
#include <cipidentity.h>
#include <cipclass.h>


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

    return *this;
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


// make static so compiler has option of inlining this
static int serialize( BufWriter& out, int seg_type, unsigned aValue )
{
    uint8_t* start = out.data();

    if( aValue < 256 )
    {
        out.put8( seg_type ).put8( aValue );
    }
    else if( aValue < 65536 )
    {
        out.put8( seg_type | 1 ).put16( aValue );
    }
    else
    {
        out.put8( seg_type | 2 ).put32( aValue );
    }

    return out.data() - start;
}


int CipAppPath::Serialize( BufWriter aOutput, int aCtl ) const
{
    BufWriter out = aOutput;

    if( HasSymbol() )
    {
        int tag_size = strlen( tag );

        out.put8( kDataSegmentTypeAnsiExtendedSymbolMessage );
        out.put8( tag_size );

        out.append( (uint8_t*) tag, tag_size );

        if( (out.data() - aOutput.data()) & 1 )
            out.put8( 0 );               // output possible pad byte

        if( HasConnPt() )
            serialize( out, kLogicalSegmentTypeConnectionPoint, GetConnPt() );

        if( HasMember1() )
        {
            serialize( out, kLogicalSegmentTypeMemberId, GetMember1() );

            if( HasMember2() )
            {
                serialize( out, kLogicalSegmentTypeMemberId, GetMember2() );

                if( HasMember3() )
                {
                    serialize( out, kLogicalSegmentTypeMemberId, GetMember3() );
                }
            }
        }
    }

    else    // is logical
    {
        if( HasClass() && !(aCtl & CTL_OMIT_CLASS) )
            serialize( out, kLogicalSegmentTypeClassId, GetClass() );

        if( HasInstance() )
        {
            if( GetClass() == kCipAssemblyClass && (aCtl & CTL_USE_CONN_PT) )
                serialize( out, kLogicalSegmentTypeConnectionPoint, GetInstance() );
            else
                serialize( out, kLogicalSegmentTypeInstanceId, GetInstance() );
        }

        else if( HasConnPt() )
            serialize( out, kLogicalSegmentTypeConnectionPoint, GetConnPt() );

        if( HasAttribute() )
            serialize( out, kLogicalSegmentTypeAttributeId, GetAttribute() );
    }

    return out.data() - aOutput.data();
}


int CipAppPath::SerializedCount( int aCtl ) const
{
    uint8_t stack_buf[200];

    // For this class, its not that much runtime overhead to simply serialize
    // and then measure the consumption.  This strategy uses small code space.

    return Serialize( BufWriter( stack_buf, sizeof stack_buf ), aCtl );
}


int CipAppPath::deserialize_logical( BufReader aInput, CipAppPath::Stuff aField, int aFormat )
{
    CIPSTER_ASSERT( aFormat >= 0 && aFormat <= 2 );
    CIPSTER_ASSERT( aField==MEMBER1 || aField==MEMBER2 || aField==MEMBER3 ||
        aField==CONN_PT || aField==ATTRIBUTE || aField==INSTANCE || aField==CLASS );

    BufReader in = aInput;

    int value;

    if( aFormat == 0 )
        value = in.get8();
    else if( aFormat == 1 )
    {
        ++in;
        value = in.get16();
    }
    else if( aFormat == 2 )
    {
        ++in;
        value = in.get32();
    }
    else
    {
#if 1
        throw std::runtime_error( "unsupported logical segment format" );
#else
        value = 0;
#endif
    }

    stuff[aField] = value;
    pbits |= 1 << aField;

    return in.data() - aInput.data();
}


inline int CipAppPath::deserialize_symbolic( BufReader aInput )
{
    BufReader in = aInput;

    int first = *in;

    if( first == kDataSegmentTypeAnsiExtendedSymbolMessage )
    {
        ++in;     // ate first

        int byte_count = in.get8();

        if( byte_count > (int) sizeof(tag)-1 )
            throw std::runtime_error( "CipAppPath has too big AnsiExtendedSymbol" );

        memcpy( tag, in.data(), byte_count );
        tag[byte_count] = 0;
        in += byte_count;

        if( (in.data() - aInput.data()) & 1 )
            ++in;    // consume pad byte if any

        pbits |= (1<<TAG);
    }

    else if( (first & 0xe0) == kSegmentTypeSymbolic )   // Symbolic Segment
    {
        // "and"ing clamps at 31, which is less than max of nul terminated 'this->tag'
        int symbol_size = first & 0x1f;

#if 0
        if( symbol_size == 0 )
        {
            throw std::runtime_error( "zero length 'extended' Symbolic Segment" );
        }
#endif

        ++in;    // ate first

        memcpy( tag, in.data(), symbol_size );
        tag[symbol_size] = 0;
        in += symbol_size;

        if( (in.data() - aInput.data()) & 1 )
            ++in;              // skip pad byte if any

        pbits |= (1<<TAG);
    }

    return in.data() - aInput.data();
}


int CipAppPath::DeserializeAppPath( BufReader aInput, CipAppPath* aPreviousToInheritFrom )
{
    BufReader in = aInput;
                                                // is seen, C-1.6 of Vol1_3.19
    Clear();

    int result = deserialize_symbolic( in );

    if( result < 0 )
        return result;

    if( result > 0 )
    {
        in += result;       // advance over deserialized symbolic

        if( in.size() )     // if more bytes, look for [connection_point] [member1 [member2 [member3]]]
        {
            int first = *in;

            // Grammar in C.1.5 shows we can have Connection_Point optionally here:
            if( (first & 0xfc) == kLogicalSegmentTypeConnectionPoint )
            {
                ++in;       // ate first
                in += deserialize_logical( in, CONN_PT, first & 3 );
            }

            Stuff   last_member;

            for( last_member = MEMBER1;  in.size() && last_member <= MEMBER3;
                    last_member = Stuff(last_member +1) )
            {
                /*

                    Grammar in C.1.5 shows we can have member_specification optionally here.

                    member id is the "element id" found in A-B publication
                    "Logix5000 Data Access" (Rockwell Automation Publication
                    1756-PM020D-EN-P - June 2016) and is expected only in the
                    context of a symbolic address according to that document.

                */
                first = *in;

                if( (first & 0xfc) == kLogicalSegmentTypeMemberId )
                {
                    ++in;   // ate first
                    in += deserialize_logical( in, last_member, first & 3 );
                }
                else
                    break;
            }
        }
    }

    else    // check for logical rather than symbolic
    {
        Stuff   last_member = LOGICAL_END;      // exit loop when higher member

        while( in.size() )
        {
            int first = *in;

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

            // The grammar shown in Vol1 C-1.5 shows that assembly_class_application_path
            // is weird in that it can only take INSTANCE or CONN_PT not both.  So when we
            // see these back to back, this is an app_path boundary.
            if( GetClass() == 4 && last_member == INSTANCE && next == CONN_PT )
            {
                goto logical_exit;
            }

            if( next >= last_member )
            {
                // C-1.6 of Vol1_3.19; is an expected termination point, not an error.
                goto logical_exit;
            }

            ++in;  // ate first

            in += deserialize_logical( in, next, format );

            last_member = next;
        }

logical_exit:
        if( in.data() > aInput.data() && aPreviousToInheritFrom )
        {
            if( aPreviousToInheritFrom->GetClass() == 4 )
                inherit_assembly( last_member + 1, aPreviousToInheritFrom );
            else
                inherit( last_member + 1, aPreviousToInheritFrom );
        }
    }

    int byte_count = in.data() - aInput.data();

    return byte_count;
}


std::string CipAppPath::Format() const
{
    std::string dest;

    if( HasClass() )
    {
        if( GetClass() == kCipAssemblyClass )
        {
            StrPrintf( &dest, "assembly %d", GetInstanceOrConnPt() );
        }
        else
        {
            StrPrintf( &dest, "Class:%d", GetClass() );

            if( HasInstance() )
                StrPrintf( &dest, " Instance:%d", GetInstance() );

            if( HasConnPt() )
                StrPrintf( &dest, " ConnPt:%d", GetConnPt() );
        }
    }
    else if( HasSymbol() )
    {
        dest += "Tag:";
        dest += tag;

        if( HasMember1() )
        {
            StrPrintf( &dest, "[%d]", GetMember1() );

            if( HasMember2() )
            {
                StrPrintf( &dest, "[%d]", GetMember2() );

                if( HasMember3() )
                    StrPrintf( &dest, "[%d]", GetMember3() );
            }
        }
    }

    return dest;
}


void CipAppPath::inherit( int aStart, CipAppPath* aPreviousToInheritFrom )
{
    CIPSTER_ASSERT( aPreviousToInheritFrom );

    for( int i=aStart;  i < LOGICAL_END;  ++i )
    {
        if( !( pbits & (1<<i) ) && ( aPreviousToInheritFrom->pbits & (1<<i) ) )
        {
            stuff[i] = aPreviousToInheritFrom->stuff[i];
            pbits |= (1<<i);
        }
    }
}


void CipAppPath::inherit_assembly( int aStart, CipAppPath* aPreviousToInheritFrom )
{
    CIPSTER_ASSERT( aPreviousToInheritFrom );

    for( int i=aStart;  i < LOGICAL_END;  ++i )
    {
        if( aStart == INSTANCE && i == INSTANCE )
            continue;

        if( !( pbits & (1<<i) ) && ( aPreviousToInheritFrom->pbits & (1<<i) ) )
        {
            stuff[i] = aPreviousToInheritFrom->stuff[i];
            pbits |= (1<<i);
        }
    }
}


// Only called from a context which is certain of segment type, so eating first
// is handled a bit differently, advancing 'in' with read is ok.
int CipPortSegment::DeserializePortSegment( BufReader aInput )
{
    BufReader   in = aInput;
    int         first = in.get8();

    // in points to 2nd byte here.
    int link_addrz = (first & 0x10) ? in.get8() : 1;

    if( (first & 0xf) == 15 )
        port = in.get16();
    else
        port = first & 0xf;

    link_address.clear();

    while( link_addrz-- )
    {
        link_address.push_back( in.get8() );
    }

    // skip a byte if not an even number of them have been consumed.
    if( (in.data() - aInput.data()) & 1 )
        ++in;

    return in.data() - aInput.data();
}


int CipPortSegment::Serialize( BufWriter aOutput, int aCtl ) const
{
    BufWriter out = aOutput;

    if( link_address.size() > 255 )
    {
        throw std::runtime_error(
            "CipPortSegment::Serialize() cannot encode a link_address with length > 255" );
    }

    if( link_address.size() == 1 )
    {
        if( port <= 15 )
        {
            out.put8( port );
        }
        else
        {
            out.put8( 0x0f );
            out.put16( port );
        }
    }
    else    // link_address.size() == 0 or > 1
    {
        if( port <= 15 )
        {
            out.put8( 0x10 | port );
            out.put8( link_address.size() );
        }
        else
        {
            out.put8( 0x1f );
            out.put8( link_address.size() );
            out.put16( port );
        }
    }

    for( unsigned i = 0; i < link_address.size();  ++i )
        out.put8( link_address[i] );

    // output a pad if odd number so far
    int byte_count = out.data() - aOutput.data();

    if( byte_count & 1 )
    {
        out.put8( 0 );
        ++byte_count;
    }

    return byte_count;
}


int CipPortSegment::SerializedCount( int aCtl ) const
{
    uint8_t stack_buf[256];

    return Serialize( BufWriter( stack_buf, sizeof stack_buf ), aCtl );
}


int CipElectronicKeySegment::DeserializeElectronicKey( BufReader aInput )
{
    BufReader in = aInput;

    int first = *in;

    if( first == 0x34 )
    {
        ++in;   // ate first

        int key_format = in.get8();

        if( key_format != 4 )
        {
            CIPSTER_TRACE_ERR( "%s: unknown electronic key format: %d\n",
                __func__, key_format );

            return aInput.data() - (in.data() - 1);    // return negative byte offset of error
        }

        vendor_id      = in.get16();
        device_type    = in.get16();
        product_code   = in.get16();
        major_revision = in.get8();
        minor_revision = in.get8();
    }

    return in.data() - aInput.data();
}


int CipElectronicKeySegment::Serialize( BufWriter aOutput, int aCtl ) const
{
    BufWriter out = aOutput;

    out.put8( 0x34 )
    .put8( 4 )
    .put16( vendor_id )
    .put16( device_type )
    .put16( product_code )
    .put8( major_revision )
    .put8( minor_revision );

    return out.data() - aOutput.data();
}


int CipElectronicKeySegment::SerializedCount( int aCtl ) const
{
    return 10;
}


ConnMgrStatus CipElectronicKeySegment::Check() const
{
    bool compatiblity_mode = major_revision & 0x80;

    // Remove compatibility bit
    int mjr_revision = major_revision & 0x7f;

    // Check VendorID and ProductCode, must match, or be 0
    if( ( vendor_id != 0     && vendor_id    != vendor_id_ )
     || ( product_code != 0  && product_code != product_code_ ) )
    {
        return kConnMgrStatusVendorIdOrProductcodeError;
    }

    // VendorID and ProductCode are correct

    // Check DeviceType, must match or 0
    if( device_type != 0 && device_type != device_type_ )
    {
        return kConnMgrStatusDeviceTypeError;
    }

    // VendorID, ProductCode and DeviceType are correct
    if( !compatiblity_mode )
    {
        if( ( mjr_revision   != 0 && mjr_revision   != revision_.major_revision )
         || ( minor_revision != 0 && minor_revision != revision_.minor_revision ) )
        {
            return kConnMgrStatusRevisionMismatch;
        }
    }

    else    // compatibility_mode
    {
        // mjr_revision must match, minor_revision must be <= my revision_.minor
        if( mjr_revision != revision_.major_revision
         || minor_revision == 0
         || minor_revision > revision_.minor_revision )
        {
            return kConnMgrStatusRevisionMismatch;
        }
    }

    return kConnMgrStatusSuccess;
}


int CipPortSegmentGroup::DeserializePortSegmentGroup( BufReader aInput )
{
    BufReader in = aInput;

    uint32_t   value;

    Clear();

    while( in.size() )
    {
        int result;
        int first = *in;

        if( (first & 0xe0) == kSegmentTypePort )
        {
            in += port.DeserializePortSegment( in );
            pbits |= (1<<PORT);
        }
        else
        {
            switch( first )
            {
            case 0x34:      // electronic key
                result = key.DeserializeElectronicKey( in );

                if( result < 0 )
                    return result;

                in += result;
                pbits |= (1<<KEY);
                break;

            // Vol1 C-1.4.3.3 Network Segment
            case 0x43:      // PIT msecs
                ++in;
                value = in.get8();
                SetPIT_MSecs( value );  // convert to usecs
                break;

            // Vol1 C-1.4.3.3 Network Segment
            case 0x51:      // PIT usecs
                ++in;

                int num_words;
                num_words = in.get8();

                if( num_words == 1 )
                {
                    value = in.get16();
                }
                else if( num_words == 2 )
                {
                    value = in.get32();
                }
                else
                {
                    throw std::runtime_error( "unknown PIT_USECS format" );
                }
                SetPIT_USecs( value );
                break;

            default:
                goto exit;
            }
        }
    }

exit:
    return in.data() - aInput.data();
}


int CipPortSegmentGroup::Serialize( BufWriter aOutput, int aCtl ) const
{
    BufWriter out = aOutput;

    if( HasKey() )
    {
        out += key.Serialize( out, aCtl );
    }

    // output the network segments first (before the port segment which is last),
    // which are PIT_USECS and/or PIT_MSECS

    // Vol1 C-1.4.3.3 Network Segment
    if( HasPIT_USecs() )
    {
        out.put8( 0x51 );
        out.put8( 2 );
        out.put32( GetPIT_USecs() );
    }

    // Vol1 C-1.4.3.3 Network Segment
    if( HasPIT_MSecs() )
    {
        unsigned msecs = GetPIT_MSecs();

        if( msecs > 255 )
            throw std::runtime_error(
                "CipPortSegmentGroup::Serialize() cannot encode PIT msecs > 255" );

        out.put8( 0x43 );
        out.put8( msecs );
    }

    if( HasPortSeg() )
    {
        out += port.Serialize( out, aCtl );
    }

    return out.data() - aOutput.data();
}


int CipPortSegmentGroup::SerializedCount( int aCtl ) const
{
    int count = 0;

    if( HasKey() )
        count += key.SerializedCount( aCtl );

    if( HasPIT_USecs() )
        count += 6;

    if( HasPIT_MSecs() )
        count += 2;

    if( HasPortSeg() )
        count += port.SerializedCount( aCtl );

    return count;
}


// Vol1 C-1.4.5.1
int CipSimpleDataSegment::DeserializeDataSegment( BufReader aInput )
{
    BufReader in = aInput;

    Clear();

    int first = *in;

    if( first == kDataSegmentTypeSimpleDataMessage )
    {
        ++in;   // ate first

        int word_count = in.get8();

        while( word_count-- )
        {
            CipWord w = in.get16();
            words.push_back( w );
        }

        pbits |= 1; // caller can use HasAny()
    }

    return in.data() - aInput.data();
}


// Vol1 C-1.4.5.1
int CipSimpleDataSegment::Serialize( BufWriter aOutput, int aCtl ) const
{
    BufWriter out = aOutput;

    if( words.size() > 255 )
    {
        throw std::runtime_error( StrPrintf(
                "CipSimpleDataSegment::Serialize() got %u words; too big to encode",
                (unsigned) words.size() )
                );
    }

    out.put8( kDataSegmentTypeSimpleDataMessage );
    out.put8( words.size() );
    for( unsigned i = 0; i < words.size();  ++i )
        out.put16( words[i] );

    return out.data() - aOutput.data();
}

int CipSimpleDataSegment::SerializedCount( int aCtl ) const
{
    return 1 + 1 + 2 * words.size();
}
