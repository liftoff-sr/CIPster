/*******************************************************************************
 * Copyright (c) 2016, SoftPLC Corportion.
 *
 * All rights reserved.
 *
 ******************************************************************************/
#ifndef CIPEPATH_H_
#define CIPEPATH_H_

#include "ciptypes.h"

typedef std::vector<EipByte>    Bytes;
typedef std::vector<CipWord>    Words;


/**
 * Class SegmentGroup
 * is a base class for a couple of domain specific segment groups (aka paths) that
 * uses a bitmap 'pbits' to indicate which segment types are in this
 * segment group container.  This is all hidden behind public accessors.
 */
class SegmentGroup
{
public:
    SegmentGroup()
    {
        Clear();
    }

    void Clear()
    {
        pbits = 0;
    }

    /// Tell if this object is holding any segment
    bool HasAny() const   { return pbits != 0; }

protected:
    int     pbits;
};


class CipSimpleDataSegment : public SegmentGroup
{
public:

    CipSimpleDataSegment()
    {
        Clear();
    }

    void Clear()
    {
        words.clear();
        SegmentGroup::Clear();
    }

    /**
     * Function DeserializePadded
     * decodes a padded EPATH
     *
     * @param aSrc is the first word of the serialized input byte sequence, which is
     *  expected to be the first segment not the word count.
     *
     * @param aLimit points one past the last byte of the allowed range of serialized
     *   aSrc, this is an exclusive end.
     *
     * @return int - Number of decoded bytes, or < 0 if error.  If negative, then
     *  the absolute value of this result is the byte offset into the problem starting
     *  from aSrc at zero.  If positive, it is not an error, even though not all the
     *  available input bytes may not have been consumed.  Parsing may stop at the first
     *  segment type not allowed into this SegmentGroup which can be before aLimit is reached.
     */
    int DeserializePadded( EipByte* aSrc, EipByte* aLimit );

    Words       words;
};


/**
 * Class CipAppPath
 * holds, serializes and deserializes a CIP Application Path.
 * <p>
 * When multiple encoded paths are concatenated the delineation between paths is
 * where a segment at a higher level in the hierarchy is encountered. Multiple
 * encoded paths may be compacted when each path shares the same values at the
 * higher levels in the hierarchy. Extended Logical Segments shall not be used
 * in compressed paths. When a segment is encountered which is at the same or
 * higher level but not at the top level in the hierarchy, the preceding higher
 * levels are used for that next encoded path.
 * <p>
 * Per C-1.6 of Vol1_3.19, implemented herein.
 */
class CipAppPath : public SegmentGroup
{
public:
    CipAppPath( CipAppPath* aHierarchicalParent = NULL ) :
        hierarchical_parent( aHierarchicalParent )
    {
    }

    /**
     * Function DeserializePadded
     * decodes a padded EPATH
     *
     * @param aSrc is the first word of the serialized input byte sequence, which is
     *  expected to be the first segment not the word count.
     *
     * @param aLimit points one past the last byte of the allowed range of serialized
     *   aSrc, this is an exclusive end.
     *
     * @return int - Number of decoded bytes, or < 0 if error.  If negative, then
     *  the absolute value of this result is the byte offset into the problem starting
     *  from aSrc at zero.  If positive, it is not an error, even though not all the
     *  available input bytes may not have been consumed.  Parsing may stop at the first
     *  segment type not allowed into this SegmentGroup which can be before aLimit is reached.
     */
    int DeserializePadded( EipByte* aSrc, EipByte* aLimit );

    /**
     * Function SerializePadded
     * encodes a padded EPATH according to fields which are present in this object and
     * by a sequence given by grammar on page C-17 of Vol1.
     * @param aDst is a buffer to serialize into.
     * @param aLimit is one past the end of the buffer.
     * @return int - the number of bytes consumed, or -1 if not enough space.
     */
    int SerializePadded( EipByte* aDst, EipByte* aLimit );

    void SetClass( int aClass )
    {
        stuff[CLASS] = aClass;
        pbits |= 1 << CLASS;
    }

    void SetInstance( int aInstance )
    {
        stuff[INSTANCE] = aInstance;
        pbits |= 1 << INSTANCE;
    }

    void SetAttribute( int aAttribute )
    {
        stuff[ATTRIBUTE] = aAttribute;
        pbits |= 1 << ATTRIBUTE;
    }

    void SetConnPoint( int aConnPt )
    {
        stuff[CONN_PT] = aConnPt;
        pbits |= 1 << CONN_PT;
    }

    bool HasClass() const;
    bool HasInstance() const;
    bool HasAttribute() const;
    bool HasConnPt() const;

    int GetClass() const;
    int GetInstance() const;
    int GetAttribute() const;
    int GetConnPt() const;

private:

    CipAppPath*    hierarchical_parent;

    enum Stuff
    {
        CONN_PT,
        ATTRIBUTE,
        INSTANCE,
        CLASS,
        STUFF_COUNT,
    };

    int     stuff[STUFF_COUNT];

    bool hasClass() const               { return pbits & (1<<CLASS); }
    bool hasInstance() const            { return pbits & (1<<INSTANCE); }
    bool hasAttribute() const           { return pbits & (1<<ATTRIBUTE); }
    bool hasConnPt() const              { return pbits & (1<<CONN_PT); }
};


//-----<Network Segments>----------------------------------------------------



/**
 * Struct CipElectronicKey
 */
struct CipElectronicKeySegment
{
    int         vendor_id;          ///< Vendor ID
    int         device_type;        ///< Device Type
    int         product_code;       ///< Product Code
    int         major_revision;     /**< Major Revision and Compatibility (Bit 0-6 = Major
                                     *  Revision) Bit 7 = Compatibility */
    int         minor_revision;     ///< Minor Revision
};


class CipPortSegment
{
public:
    int         port;            ///< if == -1, means not used
    Bytes       link_address;

    CipPortSegment()
    {
        Clear();
    }

    void Clear()
    {
        port = -1;
        link_address.clear();
    }

    void Set( int aPort, EipByte* aSrc, int aByteCount )
    {
        port = aPort;
        link_address.clear();

        while( aByteCount-- > 0 )
            link_address.push_back( *aSrc++ );
    }
};


/**
 * Class CipPortSegmentGroup
 * holds optionally an electronic key segment, any number of network segments,
 * and an optional port segment.   See grammar on page 3-60 of Vol1_3.19.
 * The Port segment is not optional and terminates the sequence.
 */
class CipPortSegmentGroup : public SegmentGroup
{
public:
    CipPortSegmentGroup()  { Clear(); }

    void Clear()
    {
        SegmentGroup::Clear();
        port.Clear();
    }

    /**
     * Function DeserializePadded
     * decodes a padded EPATH
     *
     * @param aSrc is the first word of the serialized input byte sequence, which is
     *  expected to be the first segment not the word count.
     *
     * @param aLimit points one past the last byte of the allowed range of serialized
     *  aSrc, this is an exclusive end.
     *
     * @return int - Number of decoded bytes, or < 0 if error.  If negative, then
     *  the absolute value of this result is the byte offset into the problem starting
     *  from aSrc at zero.  If positive, it is not an error, even though not all the
     *  available input bytes may not have been consumed.  Parsing may stop at the first
     *  segment type not allowed into this SegmentGroup which can be before aLimit is reached.
     */
    int DeserializePadded( EipByte* aSrc, EipByte* aLimit );

    /**
     * Function SerializePadded
     * encodes a padded EPATH according to fields which are present in this object and
     * by a sequence given by grammar on page C-17 of Vol1.
     * @param aDst is a buffer to serialize into.
     * @param aLimit is one past the end of the buffer.
     * @return int - the number of bytes consumed, or -1 if not enough space.
     */
  //  int SerializePadded( EipByte* aDst, EipByte* aLimit );

    bool HasPortSeg() const             { return pbits & (1<<PORT); }

    bool HasPIT_USECS() const           { return pbits & (1<<PIT_USECS); }
    bool HasPIT_MSECS() const           { return pbits & (1<<PIT_MSECS); }
    bool HasKey() const                 { return pbits & (1<<KEY); }


    void SetCipPortSegment( int aPort, EipByte* aSrc, int aByteCount )
    {
        port.Set( aPort, aSrc, aByteCount );
        pbits |= 1 << PORT;
    }

    // stuff we might expect in a CipPortSegmentGroup, pbits tells what we got
    CipElectronicKeySegment key;
    unsigned                pit_usecs;
    unsigned                pit_msecs;
    CipPortSegment          port;

protected:
    enum Stuff
    {
        //INVALID = -1,
        KEY,
        PIT_USECS,
        PIT_MSECS,
        PORT,
        STUFF_COUNT
    };
};



#endif  // CIPEPATH_H_
