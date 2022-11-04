/*******************************************************************************
 * Copyright (C) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#include <byte_bufs.h>
#include <trace.h>


#if defined(DEBUG) && defined(__linux__)
#include <execinfo.h>

#define TRACEZ      32

static void stack_dump( const char* aContext )
{
    void*   trace[TRACEZ];
    char**  strings;

    // see http://www.linuxjournal.com/article/6391 for info on backtrace()
    // in a signal handler.
    int trace_size = backtrace( trace, TRACEZ );

    strings = backtrace_symbols( trace, trace_size );

    printf( "%s:\n", aContext );
    for( int i = 0; i < trace_size; ++i )
        printf( " bt[%d] %s\n", i, strings[i] );

    free( strings );
}
#else
inline void stack_dump( const char* aContext ) {}      // nothing
#endif


#ifdef HAVE_ICONV
#include <iconv.h>

#define UNICODE     "UTF-16LE"  // UTF-16LE is UNICODE with fixes to shortsighted-ness.

#define UTF8        "UTF-8"     // A way to deal with all unicode chars in a
                                // platform independent way.

/**
 * Class IConv
 * is a wrapper to customize the general iconv library to convert to and
 * from UTF8 to and from little endian UNICODE.
 *
 * @author Dick Hollenbeck, SoftPLC Corporation
 */
class IConv
{
public:
    IConv()
    {
        to_unicode = iconv_open( UNICODE, UTF8 );

        if( to_unicode == iconv_t(-1) )
            throw std::runtime_error( "error from iconv_open(\"" UNICODE "\", \"" UTF8 "\")" );

        to_utf8 = iconv_open( UTF8, UNICODE );

        if( to_utf8 == iconv_t(-1) )
            throw std::runtime_error( "error from iconv_open(\"" UTF8 "\", \"" UNICODE "\" )" );
    }

    ~IConv()
    {
        iconv_close( to_unicode );
        iconv_close( to_utf8 );
    }

    // return the number of bytes consumed at destination
    int ToUTF8( char** src_ptr, size_t* src_size, char** dst_ptr, size_t* dst_size )
    {
        size_t  dst_startz = *dst_size;
        size_t  r = ::iconv( to_utf8, src_ptr, src_size, dst_ptr, dst_size );

        if( r == size_t(-1) )
        {
            if( errno == E2BIG )
                r = dst_startz;
            else
                throw std::runtime_error( "invalid UNICODE" );
        }

        return r;
    }

    // return the number of bytes consumed at destination
    int ToUNICODE( char** src_ptr, size_t* src_size, char** dst_ptr, size_t* dst_size )
    {
        size_t  dst_startz = *dst_size;
        size_t  r = ::iconv( to_unicode, src_ptr, src_size, dst_ptr, dst_size );

        if( r == size_t(-1) )
        {
            if( errno == E2BIG )
                r = dst_startz;
            else
                throw std::runtime_error( "invalid UTF8" );
        }

        return r;
    }

private:
    iconv_t to_unicode;
    iconv_t to_utf8;
};

static IConv convert;

#if defined(TEST_BYTE_BUFS)
const int WORKZ = 32;           // force multiple iconv() loops when testing.
#else
const int WORKZ = 256;
#endif

#endif


void BufWriter::overrun() const
{
    stack_dump( "write > limit" );
    throw std::overflow_error( "write > limit" );
}


BufWriter& BufWriter::put_SHORT_STRING( const std::string& aString, bool doEvenByteCountPadding )
{
    put8( (uint8_t) aString.size() );
    append( (uint8_t*) aString.c_str(), aString.size() );

    // !(size() & 1) means length is even, but since length of length byte
    // itself is odd sum can be odd when length is even.
    if( doEvenByteCountPadding && !(aString.size() & 1)  )
        put8( 0 );

    return *this;
}


BufWriter& BufWriter::put_STRING( const std::string& aString, bool doEvenByteCountPadding )
{
    put16( aString.size() );

    append( (uint8_t*) aString.c_str(), aString.size() );
    if( doEvenByteCountPadding && ( aString.size() & 1 )  )
        put8( 0 );
    return *this;
}


BufWriter& BufWriter::put_STRING2( const std::string& aString )
{
    put16( aString.size() );

#if HAVE_ICONV

    char    buf[WORKZ];
    char*   src_ptr  = (char*) aString.c_str();
    size_t  src_size = aString.size();

    while( src_size )
    {
        char*   dst_ptr  = buf;
        size_t  dst_size = sizeof(buf);

        try
        {
            convert.ToUNICODE(  &src_ptr, &src_size, &dst_ptr, &dst_size );
        }
        catch( const std::runtime_error& ex )
        {
            CIPSTER_TRACE_ERR( "%s: ERROR '%s'\n", __func__, ex.what() );
            break;
        }

        append( (uint8_t*) buf, sizeof(buf) - dst_size );
    }

#else

    for( unsigned i = 0; i < aString.size(); ++i )
        put16( (unsigned char) aString[i] );

#endif

    return *this;
}



//-----<BufReader>------------------------------------------------------------

void BufReader::overrun() const
{
    stack_dump( "read > limit" );
    throw std::range_error( "read > limit" );
}


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

#ifdef HAVE_ICONV

    char    buf[WORKZ];
    char*   src_ptr  = (char*) data();
    size_t  src_size = len * 2;

    // Advance this BufReader by full "len*2" now using a function which
    // also does overrun protection.  Prior to this, we snapshotted the
    // src_ptr to just past the len field.
    operator+=( src_size );

    while( src_size )
    {
        char*   dst_ptr  = buf;
        size_t  dst_size = sizeof(buf);

        try
        {
            convert.ToUTF8(  &src_ptr, &src_size, &dst_ptr, &dst_size );
        }
        catch( const std::runtime_error& ex )
        {
            CIPSTER_TRACE_ERR( "%s: ERROR '%s'\n", __func__, ex.what() );

            // std::string ret will be abbreviated, but we consumed full STRING2.
            break;
        }

        ret.append( buf, sizeof(buf) - dst_size );
    }

#else

    for( unsigned i = 0; i < len; ++i )
        ret += (char) get16();
#endif

    return ret;
}


//-----<ByteSerializer>---------------------------------------------------------

int ByteSerializer::Serialize( BufWriter aOutput, int aCtl ) const
{
    aOutput.append( BufReader(*this) );
    return size();
}


int ByteSerializer::SerializedCount( int aCtl ) const
{
    return size();
}


#if !BYTEBUFS_INLINE
 #include <byte_bufs.impl>
#endif


#if defined(TEST_BYTE_BUFS)

// The CMake build target for this is "test_byte_bufs"
// Do "make help" in the _library's_ build directory to see that.
// compile only with C++11 or greater?
// https://en.cppreference.com/w/cpp/language/string_literal

uint8_t buf[400];


int main( int argc, char** argv )
{
    const char16_t  unicode[] = u"This is some sample boring UNICODE text for input.";
    const char      utf8[]    = u8"ASCII is also UTF8, but reverse is not true, some trivia there.";

    BufWriter   w( buf, sizeof(buf) );

    // make a STRING2 in buf[].  The -2 removes trailing null char16_t.
    w.put16( (sizeof(unicode)-2)/2 );
    for( unsigned i=0; i<(sizeof(unicode)-2)/2; ++i )
        w.put16( unicode[i] );

    // Read back the STRING2 using this BufReader 'r'.
    BufReader r( buf, w.data() - buf );
    std::string s = r.get_STRING2();
    printf( "from UNICODE:'%s'\n", s.c_str() );

    // Save utf8 as UNICODE and read it back:
    s = utf8;

    BufWriter w2( buf, sizeof buf );

    w2.put_STRING2( s );

    BufReader r2( buf, w2.data() - buf );

    s = r2.get_STRING2();

    printf( "round trip UTF8->UNICODE->UTF8:'%s'\n", s.c_str() );
}
#endif
