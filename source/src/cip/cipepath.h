/*******************************************************************************
 * Copyright (c) 2016, SoftPLC Corporation.
 *
 ******************************************************************************/
#ifndef CIPEPATH_H_
#define CIPEPATH_H_

#include "ciptypes.h"
#include "cipcommon.h"

typedef std::vector<EipByte>    Bytes;
typedef std::vector<CipWord>    Words;


/**
 * Class SegmentGroup
 * is an abstract base class for a couple of domain specific segment groups
 * (aka paths) that uses a bitmap 'pbits' to indicate which segment types
 * are in this segment group container.
 *
 * This is all hidden behind public accessors.
 */
class SegmentGroup : public Serializeable
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
     *  available input bytes may have been consumed.  Parsing may stop at the first
     *  segment type not allowed into this SegmentGroup which can be before aLimit is reached.
     *  If zero, then the first bytes at aInput were not pertinent to this class.
     */
    int DeserializeDataSegment( BufReader aInput );

    //-----<Serializeable>------------------------------------------------------
    int Serialize( BufWriter aOutput, int aCtl = 0 ) const;
    int SerializedCount( int aCtl = 0 ) const;
    //-----</Serializeable>-----------------------------------------------------

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

    CipAppPath( int aClassId, int aInstanceId, int aAttributeId = 0 )
    {
        SetClass( aClassId );
        SetInstance( aInstanceId );
        if( aAttributeId )
            SetAttribute( aAttributeId );
    }


    /**
     * Function DeserializeAppPath
     * deserializes an application_path
     *
     * @param aInput starts at a possible app_path and can extend beyond it.
     *
     * @param aPreviousToInheritFrom is typically an immediately preceding CipAppPath
     *  that this one is to inherit values from.  The value inherited are the
     *  "more significant" fields than the first one encountered for this CipAppPath.
     *
     * @return int - Number of decoded bytes, or < 0 if error.  If negative, then
     *  the absolute value of this result is the byte offset into the problem starting
     *  from aSrc at zero.  If positive, it is not an error, even though not all the
     *  available input bytes may have been consumed.  Parsing may stop at the first
     *  segment type not allowed into this SegmentGroup which can be before aLimit is reached.
     *  If zero, then the first bytes at aInput were not an application_path.
     *
     * @throw whatever BufReader throws on buffer overrun.
     */
    int DeserializeAppPath( BufReader aInput, CipAppPath* aPreviousToInheritFrom = NULL );

    //-----<Serializeable>------------------------------------------------------
    int Serialize( BufWriter aOutput, int aCtl = 0 ) const;
    int SerializedCount( int aCtl = 0 ) const;
    //-----</Serializeable>-----------------------------------------------------

    CipAppPath& SetClass( int aClass )
    {
        stuff[CLASS] = aClass;
        pbits |= 1 << CLASS;
        return *this;
    }

    CipAppPath& SetInstance( int aInstance )
    {
        stuff[INSTANCE] = aInstance;
        pbits |= 1 << INSTANCE;
        return *this;
    }

    CipAppPath& SetAttribute( int aAttribute )
    {
        stuff[ATTRIBUTE] = aAttribute;
        pbits |= 1 << ATTRIBUTE;
        return *this;
    }

    CipAppPath& SetConnPoint( int aConnPt )
    {
        stuff[CONN_PT] = aConnPt;
        pbits |= 1 << CONN_PT;
        return *this;
    }

    CipAppPath& SetMember1( int aMember )
    {
        stuff[MEMBER1] = aMember;
        pbits |= 1 << MEMBER1;
        return *this;
    }

    CipAppPath& SetMember2( int aMember )
    {
        stuff[MEMBER2] = aMember;
        pbits |= 1 << MEMBER2;
        return *this;
    }

    CipAppPath& SetMember3( int aMember )
    {
        stuff[MEMBER3] = aMember;
        pbits |= 1 << MEMBER3;
        return *this;
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

    /// Return true if this application_path is sufficiently specified in a
    /// logical way, not a symbolic way.
    bool IsSufficient() const
    {
        if( GetClass() == kCipAssemblyClass )
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
        if( GetClass() == kCipAssemblyClass )
        {
            return HasInstance() ? GetInstance() : GetConnPt();
        }
        else
            return GetInstance();
    }

    /// Return the ASCII symbol, or "" if none.
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

    std::string Format() const;

private:

    enum Stuff
    {
        // The order of these is relied upon by inheritance mechanism.  Do not
        // arbitrarily change the order here.
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

    /**
     * Function DeserializeElectronicKey
     * parses an electronic key into this structure.
     *
     * @param aSource gives the bytes to parse and their length.
     * @return int - number of bytes consumed.  If zero, this means bytes
     *  given by aSource were not an electronic key.
     */
    int DeserializeElectronicKey( BufReader aSource );

    //-----<Serializeable>------------------------------------------------------
    int Serialize( BufWriter aOutput, int aCtl = 0 ) const;
    int SerializedCount( int aCtl = 0 ) const;
    //-----</Serializeable>-----------------------------------------------------

    /**
     * Function Check
     * compares this electronic key with this device's global data.
     *
     * @return ConnMgrStatus - kConnMgrStatusSuccess
     *  on success, other value from this enum on error.
     */
    ConnMgrStatus Check() const;
};


class CipPortSegment : public Serializeable
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

    int DeserializePortSegment( BufReader aInput );

    //-----<Serializeable>------------------------------------------------------
    int Serialize( BufWriter aOutput, int aCtl = 0 ) const;
    int SerializedCount( int aCtl = 0 ) const;
    //-----</Serializeable>-----------------------------------------------------
};


/**
 * Class CipPortSegmentGroup
 * holds optionally an electronic key segment, some network segments,
 * and a port segment.  All are optional, and the Has*() functions tell
 * what is contained.  See grammar on page 3-60 of Vol1_3.19.
 * The Port segment terminates the sequence according to the grammar.
 */
class CipPortSegmentGroup : public SegmentGroup
{
public:
    CipPortSegmentGroup()
    {
        // Base class and contained objects all properly initialize
        // themselves with help of the C++ compiler.
    }

    void Clear()
    {
        SegmentGroup::Clear();
        port.Clear();
    }

    /**
     * Function DeserializePortSegmentGroup
     * deserializes any of the segments mentioned in the class description.
     *
     * @param aInput starts at a possible port segment group and can extend beyond it.
     *
     * @return int - Number of decoded bytes, or <= 0 if error.  If error, then
     *  the absolute value of this result is the byte offset of the problem starting
     *  from aSrc at zero.  If positive, it is not an error, even though not all the
     *  available input bytes may have been consumed.  Parsing may stop at the first
     *  segment type not allowed into this SegmentGroup which can be before aLimit is reached.
     *  If zero, then the first bytes at aInput do not pertain to this class.
     *
     * @throw whatever BufReader will throw on buffer overrun.
     */
    int DeserializePortSegmentGroup( BufReader aInput );

    //-----<Serializeable>------------------------------------------------------
    int Serialize( BufWriter aOutput, int aCtl = 0 ) const;
    int SerializedCount( int aCtl = 0 ) const;
    //-----</Serializeable>-----------------------------------------------------

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
    unsigned GetPIT_MSecs() const       { return HasPIT() ? pit_usecs/1000 : 0; }

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

    const CipElectronicKeySegment& Key() const  { return key; }

protected:

    // stuff we might expect in a CipPortSegmentGroup, pbits tells what we got
    CipElectronicKeySegment key;
    CipPortSegment          port;

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
