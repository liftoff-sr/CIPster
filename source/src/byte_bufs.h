/*******************************************************************************
 * Copyright (c) 2016, SoftPLC Corportion.
 *
 ******************************************************************************/

#ifndef BYTE_BUFS_H_
#define BYTE_BUFS_H_

#include <string.h>
#include <stdexcept>

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
    size_t      size() const    { return limit - start; }

    /// Advance the start of the buffer by the specified number of bytes and trim
    /// the size().
    BufWriter& operator+=( size_t advance )
    {
        if( advance > size() )
            throw std::overflow_error( "past end" );
        start += advance;
        return *this;
    }

    BufWriter operator+( size_t n )
    {
        BufWriter ret = *this;

        ret += n;
        return ret;
    }

    CipByte& operator * ()
    {
        if( start >= limit )
            throw std::overflow_error( "past end" );

        return *start;
    }

    BufWriter& operator++()  // prefix ++
    {
        return *this += 1;
    }

    BufWriter operator++(int)  // postfix ++
    {
        BufWriter result(*this);
        *this += 1;
        return result;
    }

    void put8( CipByte aValue )
    {
        if( start + 1 > limit )
            throw std::overflow_error( "past end" );

        *start++ = aValue;
    }

    void put16( EipUint16 aValue )
    {
        if( start + 2 > limit )
            throw std::overflow_error( "past end" );
        start[0] = (CipByte) (aValue >> 0);
        start[1] = (CipByte) (aValue >> 8);
        start += 2;
    }

    void put32( EipUint32 aValue )
    {
        if( start + 4 > limit )
            throw std::overflow_error( "past end" );
        start[0] = (CipByte) (aValue >> 0);
        start[1] = (CipByte) (aValue >> 8);
        start[2] = (CipByte) (aValue >> 16);
        start[3] = (CipByte) (aValue >> 24);
        start += 4;
    }

    void put64( EipUint64 aValue )
    {
        if( start + 8 > limit )
            throw std::overflow_error( "past end" );
        start[0] = (CipByte) (aValue >> 0);
        start[1] = (CipByte) (aValue >> 8);
        start[2] = (CipByte) (aValue >> 16);
        start[3] = (CipByte) (aValue >> 24);
        start[4] = (CipByte) (aValue >> 32);
        start[5] = (CipByte) (aValue >> 40);
        start[6] = (CipByte) (aValue >> 48);
        start[7] = (CipByte) (aValue >> 56);
        start += 8;
    }

    void put_float( float aValue )
    {
        union {
            float       f;
            EipUint32   i;
        } t;

        t.f = aValue;
        put32( t.i );
    }

    void put_double( double aValue )
    {
        union {
            double      d;
            EipUint64   i;
        } t;

        t.d = aValue;
        put64( t.i );
    }

    void put16BE( EipUint16 aValue )
    {
        if( start + 2 > limit )
            throw std::overflow_error( "past end" );
        start[1] = (CipByte) (aValue >> 0);
        start[0] = (CipByte) (aValue >> 8);
        start += 2;
    }

    void put32BE( EipUint32 aValue )
    {
        if( start + 4 > limit )
            throw std::overflow_error( "past end" );
        start[3] = (CipByte) (aValue >> 0);
        start[2] = (CipByte) (aValue >> 8);
        start[1] = (CipByte) (aValue >> 16);
        start[0] = (CipByte) (aValue >> 24);
        start += 4;
    }

    void append( const EipByte* aStart, size_t aCount )
    {
        if( start + aCount > limit )
            throw std::overflow_error( "past end" );
        memcpy( start, aStart, aCount );
        start += aCount;
    }

    void fill( size_t aCount, EipByte aValue = 0 )
    {
        if( start + aCount > limit )
            throw std::overflow_error( "past end" );
        memset( start, aValue, aCount );
        start += aCount;
    }

protected:
    CipByte*    start;
    CipByte*    limit;          // points to one past last byte
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
    size_t          size() const    { return limit - start; }

    /// Advance the start of the buffer by the specified number of bytes and trim
    /// the size().
    BufReader& operator += ( size_t advance )
    {
        if( advance > size() )
            throw std::range_error( "past end" );

        start += advance;
        return *this;
    }

    BufReader operator+( size_t n )
    {
        BufReader ret = *this;

        ret += n;
        return ret;
    }

    BufReader& operator++()  // prefix ++
    {
        return *this += 1;
    }

    BufReader operator++(int)  // postfix ++
    {
        BufReader result(*this);
        *this += 1;
        return result;
    }

    CipByte operator * () const
    {
        if( start >= limit )
            throw std::range_error( "past end" );
        return *start;
    }

    CipByte get8()
    {
        if( start + 1 > limit )
            throw std::range_error( "past end" );
        return *start++;
    }

    EipUint16 get16()
    {
        if( start + 2 > limit )
            throw std::range_error( "past end" );

        EipUint16 ret = (start[0] << 0) |
                        (start[1] << 8);
        start += 2;
        return ret;
    }

    EipUint32 get32()
    {
        if( start + 4 > limit )
            throw std::range_error( "past end" );

        EipUint32 ret = (start[0] << 0 ) |
                        (start[1] << 8 ) |
                        (start[2] << 16) |
                        (start[3] << 24) ;
        start += 4;
        return ret;
    }

    EipUint64 get64()
    {
        if( start + 8 > limit )
            throw std::range_error( "past end" );

        EipUint64 ret = ((EipUint64) start[0] << 0 ) |
                        ((EipUint64) start[1] << 8 ) |
                        ((EipUint64) start[2] << 16) |
                        ((EipUint64) start[3] << 24) |
                        ((EipUint64) start[4] << 32) |
                        ((EipUint64) start[5] << 40) |
                        ((EipUint64) start[6] << 48) |
                        ((EipUint64) start[7] << 56) ;
        start += 8;
        return ret;
    }

    EipUint16 get16BE()
    {
        if( start + 2 > limit )
            throw std::range_error( "past end" );

        EipUint16 ret = (start[1] << 0) |
                        (start[0] << 8);
        start += 2;
        return ret;
    }

    EipUint32 get32BE()
    {
        if( start + 4 > limit )
            throw std::range_error( "past end" );

        EipUint32 ret = (start[3] << 0 ) |
                        (start[2] << 8 ) |
                        (start[1] << 16) |
                        (start[0] << 24) ;
        start += 4;
        return ret;
    }

protected:
    const CipByte*  start;
    const CipByte*  limit;          // points to one past last byte
};

#endif // BYTE_BUFS_H_
