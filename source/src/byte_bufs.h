/*******************************************************************************
 * Copyright (c) 2016-2023, SoftPLC Corporation.
 *
 ******************************************************************************/

#ifndef BYTE_BUFS_H_
#define BYTE_BUFS_H_

#include <string>
#include <stdexcept>    // for the convenience of clients of these classes, which throw

class BufReader;
class BufWriter;

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
    ByteBuf( uint8_t* aStart, size_t aSize ) :
        start( aStart ),
        limit( aStart + aSize )
    {}

    ByteBuf( const BufWriter& aWriter );
    ByteBuf( const BufReader& aReader );

    uint8_t*    data() const    { return start; }
    uint8_t*    end() const     { return limit; }
    ssize_t     size() const    { return limit - start; }   // ssize_t is signed

protected:
    uint8_t*    start;
    uint8_t*    limit;          // points to one past last byte
};


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
    BufWriter( uint8_t* aStart, size_t aCount ) :
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

    uint8_t*    data() const    { return start; }
    uint8_t*    end() const     { return limit; }

    /// Return the unused size of the buffer, the remaining capacity which is empty.
    /// A negative value would indicate an overrun, but that also indicates a bug in
    /// in this class because protections are everywhere to prevent overruns.
    ssize_t     capacity() const    { return limit - start; }

    /// Advance the start of the buffer by the specified number of bytes and trim
    /// the capacity().
    BufWriter& operator+=( size_t advance );

    /// Construct a new BufWriter from this one but advance its start by n bytes.
    BufWriter operator+( size_t n );

    uint8_t& operator * ();

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

    BufWriter& put8( uint8_t aValue );

    BufWriter& put16( uint16_t aValue );

    BufWriter& put32( uint32_t aValue );

    BufWriter& put64( uint64_t aValue );

    BufWriter& put_float( float aValue );

    BufWriter& put_double( double aValue );

    /// Serialize a CIP SHORT_STRING
    BufWriter& put_SHORT_STRING( const std::string& aString, bool doEvenByteCountPadding );

    /// Serialize a CIP STRING
    BufWriter& put_STRING( const std::string& aString, bool doEvenByteCountPadding );

    /// Serialize a CIP STRING2
    BufWriter& put_STRING2( const std::string& aString );

    // Put 16 bit integer Big Endian
    BufWriter& put16BE( uint16_t aValue );

    // Put 32 bit integer Big Endian
    BufWriter& put32BE( uint32_t aValue );

    BufWriter& append( const uint8_t* aStart, size_t aCount );

    BufWriter& append( const BufReader& aReader );

    BufWriter& fill( size_t aCount, uint8_t aValue = 0 );

protected:
    uint8_t*    start;
    uint8_t*    limit;          // points to one past last byte

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

    BufReader( const uint8_t* aStart, size_t aCount ) :
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

    const uint8_t*  data() const    { return start; }
    const uint8_t*  end()  const    { return limit; }

    /// Return the un-consumed size of the buffer, the count of bytes remaining
    /// in the buffer.
    /// A negative value would indicate an overrun, but that also indicates a bug in
    /// in this class because protections are everywhere to prevent overruns.
    ssize_t     size() const        { return limit - start; }

    /// Advance the start of the buffer by the specified number of bytes and trim
    /// the size().
    BufReader& operator += ( size_t advance );

    /// Construct a new BufReader from this one but advance its start by n bytes.
    BufReader operator + ( size_t n );

    /// prefix ++
    BufReader& operator++();

    /// postfix ++
    BufReader operator++(int);

    uint8_t operator * () const;

    uint8_t operator[](int aIndex) const;

    BufReader& operator=( const ByteBuf& aRange )
    {
        start = aRange.data();
        limit = aRange.end();
        return *this;
    }

    uint8_t get8();

    uint16_t get16();

    uint32_t get32();

    uint64_t get64();

    float get_float();

    double get_double();

    /// Deserialize a CIP SHORT_STRING
    std::string get_SHORT_STRING( bool ExpectPossiblePaddingToEvenByteCount );

    /// Deserialize a CIP STRING
    std::string get_STRING( bool ExpectPossiblePaddingToEvenByteCount );

    /// Deserialize a CIP STRING2 and encode the result as UTF8 within a std::string.
    std::string get_STRING2();

    /// Get a 16 bit integer as Big Endian
    uint16_t get16BE();

    /// Get a 32 bit integer as Big Endian
    uint32_t get32BE();

protected:
    const uint8_t*  start;
    const uint8_t*  limit;          // points to one past last byte

    void overrun() const;
};


inline ByteBuf::ByteBuf( const BufReader& aReader ) :
    start( (uint8_t*) aReader.data() ),
    limit( (uint8_t*) aReader.end() )
{}

inline ByteBuf::ByteBuf( const BufWriter& aWriter ) :
    start( aWriter.data() ),
    limit( aWriter.end() )
{}


// this one is always inline
inline BufWriter& BufWriter::append( const BufReader& aReader )
{
    return append( aReader.data(), aReader.size() );
}

/// Control bits for Serializeable::Serialize(), or SerializeCount()'s aCtl
enum CTL_FLAGS
{
    CTL_OMIT_CLASS              = (1<<0),
    CTL_OMIT_INSTANCE           = (1<<1),
    CTL_OMIT_CONN_PT            = (1<<2),
    CTL_OMIT_ATTRIBUTE          = (1<<3),
    CTL_UNCOMPRESSED_EPATH      = (1<<4),
    CTL_PACKED_EPATH            = (1<<5),

    CTL_OMIT_CONN_PATH          = (1<<6),

    CTL_FORWARD_OPEN            = (1<<7),
    CTL_FORWARD_CLOSE           = (1<<8),
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
     * @param aCtl is a set of class specific bits that act as boolean flags to
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
     * @param aCtl is a set of class specific bits that act as boolean flags to
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
