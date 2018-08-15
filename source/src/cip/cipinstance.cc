/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (c) 2016, SoftPLC Corporation.
 *
 ******************************************************************************/

#include <cipinstance.h>
#include <cipclass.h>
#include <cipcommon.h>


CipInstance::CipInstance( int aInstanceId ) :
    instance_id( aInstanceId ),
    owning_class( 0 ),       // NULL (not owned) until I am inserted into a CipClass
    getable_all_mask( 0 )
{
}


CipInstance::~CipInstance()
{
    if( owning_class )      // if not nested in a meta-class
    {
        if( instance_id )   // and not nested in a public class, then I am an instance.
        {
            CIPSTER_TRACE_INFO( "deleting instance %d of class '%s'\n",
                instance_id, owning_class->ClassName().c_str() );
        }
    }

    while( attributes.size() )
    {
        delete attributes.back();
        attributes.pop_back();
    }
}


bool CipInstance::AttributeInsert( CipAttribute* aAttribute )
{
    CipAttributes::iterator it;

    CIPSTER_ASSERT( !aAttribute->owning_instance );  // only un-owned attributes may be inserted

    // Keep sorted by id
    for( it = attributes.begin(); it != attributes.end();  ++it )
    {
        if( aAttribute->Id() < (*it)->Id() )
            break;

        else if( aAttribute->Id() == (*it)->Id() )
        {
            CIPSTER_TRACE_ERR( "class '%s' instance %d already has attribute %d, overriding\n",
                owning_class ? owning_class->ClassName().c_str() : "meta-something",
                instance_id,
                aAttribute->Id()
                );

            // Re-use this slot given by position 'it'.
            delete *it;
            attributes.erase( it );    // will re-insert at this position below
            break;
        }
    }

    attributes.insert( it, aAttribute );

    aAttribute->owning_instance = this; // until now there was no owner of this attribute.

    if( aAttribute->Id() < 32 )
    {
        if( aAttribute->IsGetableAll() )
            getable_all_mask |= 1 << aAttribute->Id();
    }

    return true;
}


CipAttribute* CipInstance::AttributeInsert(
        int             aAttributeId,
        CipDataType     aCipType,
        void*           aData,
        bool            isGetableSingle,
        bool            isGetableAll,
        bool            isSetableSingle
        )
{
    CipAttribute* attribute = new CipAttribute(
            aAttributeId,
            aCipType,
            isGetableSingle ? CipAttribute::GetAttrData : NULL,
            isSetableSingle ? CipAttribute::SetAttrData : NULL,
            aData,
            isGetableAll
            );

    if( !AttributeInsert( attribute ) )
    {
        delete attribute;
        attribute = NULL;   // return NULL on failure
    }

    return attribute;
}


CipAttribute* CipInstance::AttributeInsert(
        int             aAttributeId,
        AttributeFunc   aGetter,
        bool            isGetableAll,
        AttributeFunc   aSetter,
        void*           aCookie,
        CipDataType     aDataType
        )
{
    CipAttribute* attribute = new CipAttribute(
            aAttributeId,
            aDataType,
            aGetter,
            aSetter,
            aCookie,
            isGetableAll
            );

    if( !AttributeInsert( attribute ) )
    {
        delete attribute;
        attribute = NULL;   // return NULL on failure
    }

    return attribute;
}


CipAttribute* CipInstance::Attribute( int aAttributeId ) const
{
    CipAttributes::const_iterator  it;

    // a binary search thru the vector of pointers looking for aAttributeId
    it = vec_search( attributes.begin(), attributes.end(), aAttributeId );

    if( it != attributes.end() )
        return *it;

    CIPSTER_TRACE_WARN( "attribute %d not defined\n", aAttributeId );

    return NULL;
}

