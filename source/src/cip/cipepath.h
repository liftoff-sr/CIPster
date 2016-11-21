/*******************************************************************************
 * Copyright (c) 2016, SoftPLC Corportion.
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


/**
 * Class CipSimpleDataSegment
 * holds a single data segment or is empty.
 */
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
     * Function DeserializeDataSegment
     * decodes a padded EPATH
     *
     * @param aInput starts at a possible data segment, if any, and can extend beyond it.
     *
     * @return int - Number of decoded bytes, or < 0 if error.  If error, then
     *  the absolute value of this result is the byte offset of the problem starting
     *  from aSrc at zero.  If positive, it is not an error, even though not all the
     *  available input bytes may not have been consumed.  Parsing may stop at the first
     *  segment type not allowed into this SegmentGroup which can be before aLimit is reached.
     */
    int DeserializeDataSegment( BufReader aInput );

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
    CipAppPath()
    {
    }

    /**
     * Function DeserializeAppPath
     * decodes a padded EPATH
     *
     * @param aInput starts at a possible app_path can can extend beyond it.
     *
     * @return int - Number of decoded bytes, or < 0 if error.  If negative, then
     *  the absolute value of this result is the byte offset into the problem starting
     *  from aSrc at zero.  If positive, it is not an error, even though not all the
     *  available input bytes may not have been consumed.  Parsing may stop at the first
     *  segment type not allowed into this SegmentGroup which can be before aLimit is reached.
     */
    int DeserializeAppPath( BufReader aInput, CipAppPath* aPreviousToInheritFrom = NULL );

    /**
     * Function SerializePadded
     * encodes a padded EPATH according to fields which are present in this object and
     * by a sequence given by grammar on page C-17 of Vol1.
     *
     * @param aOutput where to serialize into.
     * @return int - the number of bytes consumed, or -1 if not enough space.
     */
    int SerializeAppPath( BufWriter aOutput );

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

    void SetMember1( int aMember )
    {
        stuff[MEMBER1] = aMember;
        pbits |= 1 << MEMBER1;
    }

    void SetMember2( int aMember )
    {
        stuff[MEMBER2] = aMember;
        pbits |= 1 << MEMBER2;
    }

    void SetMember3( int aMember )
    {
        stuff[MEMBER3] = aMember;
        pbits |= 1 << MEMBER3;
    }

    bool SetSymbol( const char* aSymbol );

    bool HasClass() const               { return pbits & (1<<CLASS); }
    bool HasInstance() const            { return pbits & (1<<INSTANCE); }
    bool HasAttribute() const           { return pbits & (1<<ATTRIBUTE); }
    bool HasConnPt() const              { return pbits & (1<<CONN_PT); }
    bool HasSymbol() const              { return pbits & (1<<TAG); }
    bool HasMember1() const             { return pbits & (1<<MEMBER1); }
    bool HasMember2() const             { return pbits & (1<<MEMBER2); }
    bool HasMember3() const             { return pbits & (1<<MEMBER3); }

    bool HasLogical() const
    {
        return HasClass();
        // return pbits & ((1<<CLASS)|(1<<INSTANCE)|(1<<ATTRIBUTE));
    }

    bool IsSufficient() const
    {
        if( GetClass() == kCipAssemblyClassCode )
            return HasClass() && (HasInstance() || HasConnPt());
        else
            return HasClass() && HasInstance();
    }

    // Do not call these without first either Set()ing them or DeserializePadded() first.

    int GetClass() const
    {
        return HasClass() ? stuff[CLASS] : 0;
    }

    int GetInstance() const
    {
        return HasInstance() ? stuff[INSTANCE] : 0;
    }

    int GetAttribute() const
    {
        return HasAttribute() ? stuff[ATTRIBUTE] : 0;
    }

    int GetConnPt() const
    {
        return HasConnPt() ? stuff[CONN_PT] : 0;
    }

    /// Hide the ugli-ness pertaining to the Assembly class app_path
    int GetInstanceOrConnPt() const
    {
        if( GetClass() == kCipAssemblyClassCode )
        {
            return HasInstance() ? GetInstance() : GetConnPt();
        }
        else
            return GetInstance();
    }

    /// Return the ASCII symbol, or "" if none.  Will never exceed strlen() == 31.
    const char* GetSymbol() const;

    int GetMember1() const
    {
        return HasMember1() ? stuff[MEMBER1] : 0;
    }

    int GetMember2() const
    {
        return HasMember2() ? stuff[MEMBER2] : 0;
    }

    int GetMember3() const
    {
        return HasMember3() ? stuff[MEMBER3] : 0;
    }

    CipClass* Class() const;

    CipInstance* Instance() const;

    CipAttribute* Attribute( int aAttrId ) const;

    bool operator == ( const CipAppPath& other ) const;

    CipAppPath& operator = ( const CipAppPath& other );

    const std::string Format() const;

private:

    enum Stuff
    {
        ATTRIBUTE,
        CONN_PT,
        INSTANCE,
        CLASS,
        LOGICAL_END,

        MEMBER1 = LOGICAL_END,  // up to 3 dimensions for array indices max.
        MEMBER2,
        MEMBER3,
        TAG,

        STUFF_COUNT = TAG,  // TAG is not in stuff[], but rather in tag[]
    };

    int     stuff[STUFF_COUNT];

    char    tag[42];

    int deserialize_symbolic( BufReader aInput );
    int deserialize_logical( BufReader aInput, Stuff aField, int aFormat );

    void inherit( int aStart, CipAppPath* from );

    void inherit_assembly( int aStart, CipAppPath* from );
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
     * Function DeserializePortSegmentGroup
     * decodes a padded EPATH
     *
     * @param aInput starts at a possible port segment group and can extend beyond it.
     *
     * @return int - Number of decoded bytes, or <= 0 if error.  If error, then
     *  the absolute value of this result is the byte offset of the problem starting
     *  from aSrc at zero.  If positive, it is not an error, even though not all the
     *  available input bytes may not have been consumed.  Parsing may stop at the first
     *  segment type not allowed into this SegmentGroup which can be before aLimit is reached.
     */
    int DeserializePortSegmentGroup( BufReader aInput );

    /**
     * Function SerializePortSegmentGroup
     * encodes a padded EPATH according to fields which are present in this object and
     * by a sequence given by grammar on page C-17 of Vol1.
     * @param aDst is a buffer to serialize into.
     * @param aLimit is one past the end of the buffer.
     * @return int - the number of bytes consumed, or -1 if not enough space.
     */
  //  int SerializePadded( EipByte* aDst, EipByte* aLimit );

    bool HasPortSeg() const             { return pbits & (1<<PORT); }

    bool HasPIT_USecs() const           { return pbits & (1<<PIT_USECS); }
    bool HasPIT_MSecs() const           { return pbits & (1<<PIT_MSECS); }
    bool HasPIT() const                 { return pbits & ((1<<PIT_USECS) | (1<<PIT_MSECS)); }
    bool HasKey() const                 { return pbits & (1<<KEY); }

    void SetCipPortSegment( int aPort, EipByte* aSrc, int aByteCount )
    {
        port.Set( aPort, aSrc, aByteCount );
        pbits |= 1 << PORT;
    }

    unsigned GetPIT_USecs() const       { return HasPIT() ? pit_usecs : 0; }

    void SetPIT_USecs( unsigned aUSECS )
    {
        pbits |= (1 << PIT_USECS);
        pit_usecs = aUSECS;
    }

    void SetPIT_MSecs( unsigned aMSECS )
    {
        pbits |= (1 << PIT_MSECS);
        pit_usecs = aMSECS * 1000;
    }

    // stuff we might expect in a CipPortSegmentGroup, pbits tells what we got
    CipElectronicKeySegment key;
    CipPortSegment          port;

protected:

    unsigned                pit_usecs;

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
