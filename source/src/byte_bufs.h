/*******************************************************************************
 * Copyright (c) 2016-2018, SoftPLC Corportion.
 *
 ******************************************************************************/

#ifndef BYTE_BUFS_H_
#define BYTE_BUFS_H_

#include <string>
#include <stdexcept>    // for the convenience of clients of these classes, which throw

#include "typedefs.h"

/**
 * Class BufWriter
 * outlines a writable byte buffer with little endian putters.  It protects
 * from buffer overruns by throwing std::overflow_error.
 *
 * @author Dick Hollenbeck
 */
class BufWriter
{
public:
    BufWriter( CipByte* aStart, size_t aCount ) :
        start( aStart ),
        limit( aStart + aCount )
    {}

    BufWriter() :
        start( 0 ), limit( 0 )
    {}

    CipByte*    data() const    { return start; }
    CipByte*    end() const     { return limit; }

    /// Return the unused size of the buffer, the remaining capacity which is empty.
    /// A negative value would indicate an overrun, but that also indicates a bug in
    /// in this class because protections are everywhere to prevent overruns.
    ssize_t     capacity() const    { return limit - start; }

    /// Advance the start of the buffer by the specified number of bytes and trim
    /// the size().
    BufWriter& operator+=( size_t advance );

    BufWriter operator+( size_t n );

    CipByte& operator * ();

    /// prefix ++
    BufWriter& operator++();

    /// postfix ++
    BufWriter operator++(int);

    BufWriter& put8( CipByte aValue );

    BufWriter& put16( EipUint16 aValue );

    BufWriter& put32( EipUint32 aValue );

    BufWriter& put64( EipUint64 aValue );

    BufWriter& put_float( float aValue );

    BufWriter& put_double( double aValue );

    /// Serialize a CIP SHORT_STRING
    BufWriter& put_SHORT_STRING( const std::string& aString, bool doEvenByteCountPadding = true );

    /// Serialize a CIP STRING
    BufWriter& put_STRING( const std::string& aString, bool doEvenByteCountPadding = true );

    /// Serialize a CIP STRING2
    BufWriter& put_STRING2( const std::string& aString );

    // Put 16 bit integer Big Endian
    BufWriter& put16BE( EipUint16 aValue );

    // Put 32 bit integer Big Endian
    BufWriter& put32BE( EipUint32 aValue );

    BufWriter& append( const EipByte* aStart, size_t aCount );

    BufWriter& fill( size_t aCount, EipByte aValue = 0 );

protected:
    CipByte*    start;
    CipByte*    limit;          // points to one past last byte

private:
    void        overrun() const;
};


/**
 * Class BufReader
 * outlines a read only byte buffer with little endian getters.  It protects
 * from buffer overruns by throwing std::range_error.
 *
 * @author Dick Hollenbeck
 */
class BufReader
{
public:
    BufReader() :
        start( 0 ),
        limit( 0 )
    {}

    BufReader( const CipByte* aStart, size_t aCount ) :
        start( aStart ),
        limit( aStart + aCount )
    {}

    BufReader( const BufWriter& m ):
        start( m.data() ),
        limit( m.end() )
    {}

    const CipByte*  data() const    { return start; }
    const CipByte*  end()  const    { return limit; }

    /// Return the un-consumed size of the buffer, the count of bytes remaining
    /// in the buffer.
    /// A negative value would indicate an overrun, but that also indicates a bug in
    /// in this class because protections are everywhere to prevent overruns.
    ssize_t     size() const        { return limit - start; }

    /// Advance the start of the buffer by the specified number of bytes and trim
    /// the size().
    BufReader& operator += ( size_t advance );

    BufReader operator+( size_t n );

    /// prefix ++
    BufReader& operator++();

    /// postfix ++
    BufReader operator++(int);

    CipByte operator * () const;

    CipByte get8();

    EipUint16 get16();

    EipUint32 get32();

    EipUint64 get64();

    float get_float();

    double get_double();

    /// Deserialize a CIP SHORT_STRING
    std::string get_SHORT_STRING( bool ExpectPossiblePaddingToEvenByteCount = true );

    /// Deserialize a CIP STRING
    std::string get_STRING( bool ExpectPossiblePaddingToEvenByteCount = true );

    /// Deserialize a CIP STRING2 and encode the result as UTF8 within a std::string.
    std::string get_STRING2();

    /// Get a 16 bit integer as Big Endian
    EipUint16 get16BE();

    /// Get a 32 bit integer as Big Endian
    EipUint32 get32BE();

protected:
    const CipByte*  start;
    const CipByte*  limit;          // points to one past last byte

private:
    void overrun() const;
};

#if BYTEBUFS_INLINE
 #include "byte_bufs.impl"
#endif

#endif // BYTE_BUFS_H_
