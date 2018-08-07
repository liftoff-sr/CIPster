/*******************************************************************************
 * Copyright (c) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#ifndef BYTE_BUFS_H_
#define BYTE_BUFS_H_

#include <string>
#include <stdexcept>    // for the convenience of clients of these classes, which throw

#include "typedefs.h"


/**
 * Class ByteBuf
 * delimits the starting point, ending point, and size of a byte array.
 * It does not take ownership of such memory, merely points to it.
 * There are no setters among the accessors because it is simple enough
 * to use the assignment operator and overwrite this object with a newly
 * constructed one.
 */
class ByteBuf
{
public:
    ByteBuf( CipByte* aStart, size_t aSize ) :
        start( aStart ),
        limit( aStart + aSize )
    {}

    CipByte*    data() const    { return start; }
    CipByte*    end() const     { return limit; }
    ssize_t     size() const    { return limit - start; }

protected:
    CipByte*    start;
    CipByte*    limit;          // points to one past last byte
};


class BufReader;


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

    BufWriter( const ByteBuf& aBuf ) :
        start( aBuf.data() ),
        limit( aBuf.end() )
    {}

    BufWriter() :
        start( 0 ),
        limit( 0 )
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

    BufWriter& operator=( const ByteBuf& aRange )
    {
        start = aRange.data();
        limit = aRange.end();
        return *this;
    }

    BufWriter& put8( CipByte aValue );

    BufWriter& put16( EipUint16 aValue );

    BufWriter& put32( EipUint32 aValue );

    BufWriter& put64( EipUint64 aValue );

    BufWriter& put_float( float aValue );

    BufWriter& put_double( double aValue );

    /// Serialize a CIP SHORT_STRING
    BufWriter& put_SHORT_STRING( const std::string& aString, bool doEvenByteCountPadding );

    /// Serialize a CIP STRING
    BufWriter& put_STRING( const std::string& aString, bool doEvenByteCountPadding );

    /// Serialize a CIP STRING2
    BufWriter& put_STRING2( const std::string& aString );

    // Put 16 bit integer Big Endian
    BufWriter& put16BE( EipUint16 aValue );

    // Put 32 bit integer Big Endian
    BufWriter& put32BE( EipUint32 aValue );

    BufWriter& append( const EipByte* aStart, size_t aCount );

    BufWriter& append( const BufReader& aReader );

    BufWriter& fill( size_t aCount, EipByte aValue = 0 );

protected:
    CipByte*    start;
    CipByte*    limit;          // points to one past last byte

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

    BufReader( const BufWriter& aWriter ):
        start( aWriter.data() ),
        limit( aWriter.end() )
    {}

    BufReader( const ByteBuf& aBuf ) :
        start( aBuf.data() ),
        limit( aBuf.end() )
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

    BufReader& operator=( const ByteBuf& aRange )
    {
        start = aRange.data();
        limit = aRange.end();
        return *this;
    }

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

    void overrun() const;
};


// this one is always inline
inline BufWriter& BufWriter::append( const BufReader& aReader )
{
    return append( aReader.data(), aReader.size() );
}

/// Control bits for Serializeable::Serialize(), or SerializeCount()'s aCtl
enum
{
    CTL_INCLUDE_CONN_PATH       = (1<<0),
};


/**
 * Class Serializeable
 * is an interface (aka abstract class) which can be implemented by any class
 * that intends to be either encoded in a message or simply supply a range of
 * bytes to be copied to a BufWriter.
 */
class Serializeable
{
public:

    /**
     * Function SerializedCount
     * returns the total byte count of this item if it were to be Serialize()ed, but does
     * the calculation without actually doing the serialization.
     *
     * @param aCtl is set of class specific bits that act as boolean flags to
     *      tune the nature of the serialization.
     * @return int - the number of bytes consumed should this object be serialized
     *      using the same provided aCtl flags.
     */
    virtual int SerializedCount( int aCtl = 0 ) const = 0;

    /**
     * Function Serialize
     * encodes this object into aWriter and returns the consumed byte count in that
     * destination BufWriter.
     *
     * @param aWriter is a BufWriter indicating size and location of a place to
     *      put the serialized bytes.
     * @param aCtl is set of class specific bits that act as boolean flags to
     *      tune the nature of the serialization.
     * @return int - the number of bytes consumed during the serialization.
     */
    virtual int Serialize( BufWriter aWriter, int aCtl = 0 ) const = 0;
};


/**
 * Class ByteSerializer
 * add a Serializeable interface to a ByteBuf
 */
class ByteSerializer : public ByteBuf, public Serializeable
{
public:
    ByteSerializer( const ByteBuf& aRange ) :
        ByteBuf( aRange )
    {}

/*
    ByteSerializer& SetRange( const ByteBuf& aRange )
    {
        *(ByteBuf*)this = aRange;
        return *this;
    }
*/

    //-----<Serializeable>------------------------------------------------------
    int Serialize( BufWriter aOutput, int aCtl = 0 ) const;
    int SerializedCount( int aCtl = 0 ) const;
    //-----</Serializeable>-----------------------------------------------------
};


#if BYTEBUFS_INLINE
 #include "byte_bufs.impl"
#endif

#endif // BYTE_BUFS_H_
