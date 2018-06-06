/*******************************************************************************
 * Copyright (C) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/

#include <byte_bufs.h>

#include <byte_bufs.impl>


void BufWriter::put_SHORT_STRING( const std::string& aString, bool doEvenByteCountPadding )
{
    put8( (EipByte) aString.size() );
    append( (EipByte*) aString.c_str(), aString.size() );

    // !(size() & 1) means length is even, but since length of length byte
    // itself is odd sum can be odd when length is even.
    if( doEvenByteCountPadding && !(aString.size() & 1)  )
        put8( 0 );
}


void BufWriter::put_STRING( const std::string& aString, bool doEvenByteCountPadding )
{
    put16( aString.size() );

    append( (EipByte*) aString.c_str(), aString.size() );
    if( doEvenByteCountPadding && ( aString.size() & 1 )  )
        put8( 0 );
}


void BufWriter::put_STRING2( const std::string& aString )
{
    put16( aString.size() );

    // TODO: decode from UTF8 here:
    for( unsigned i = 0; i < aString.size(); ++i )
        put16( (EipByte) aString[i] );
}



//-----<BufReader>------------------------------------------------------------

std::string BufReader::get_SHORT_STRING( bool ExpectPossiblePaddingToEvenByteCount )
{
    std::string ret;
    unsigned    len = get8();

    // !(len & 1) means length is even, but since length of length byte
    // itself is odd, total sum is odd when length is even.
    bool eat_pad = ExpectPossiblePaddingToEvenByteCount && !(len & 1);

    if( len + eat_pad > size() )
        overrun();

    ret.append( (char*) start, len );

    start += len + eat_pad;

    return ret;
}


std::string BufReader::get_STRING( bool ExpectPossiblePaddingToEvenByteCount )
{
    std::string ret;
    unsigned    len = get16();

    bool    eat_pad = ExpectPossiblePaddingToEvenByteCount && (len & 1);

    if( len + eat_pad > size() )
        overrun();

    ret.append( (char*) start, len );

    start += len + eat_pad;

    return ret;
}


std::string BufReader::get_STRING2()
{
    std::string ret;
    unsigned    len = get16();

    if( len * 2 > size() )
        overrun();

    // todo, this needs to be encoded into UTF8 instead of this MSbyte truncation.
    for( unsigned i = 0; i < len; ++i )
        ret += (char) get16();

    return ret;
}
