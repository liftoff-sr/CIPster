/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corportion.
 *
 ******************************************************************************/

#ifndef CIPINSTANCE_H_
#define CIPINSTANCE_H_

#include <vector>

#include "cipattribute.h"


/**
 * Class CipInstance
 * holds CIP intance info and instances may be contained within a #CipClass.
 */
class CipInstance
{
    friend class CipClass;

public:
    typedef std::vector<CipAttribute*>      CipAttributes;

    CipInstance( int aInstanceId );

    virtual ~CipInstance();

    int   Id() const            { return instance_id; }

    CipClass*   Class() const   { return owning_class; }

    /**
     * Function AttributeInsert
     * inserts an attribute and returns true if succes, else false.
     *
     * @param aAttribute is the one to insert, and may not already be a member
     *  of another instance.  It must be dynamically allocated, not compiled in,
     *  because this container takes ownership of aAttribute.
     *
     * @return bool - true if success, else false if failed.  Currently attributes
     *  may be overrridden, so any existing CipAttribute in this instance with the
     *  same attribute_id will be deleted in favour of this one.
     */
    bool AttributeInsert( CipAttribute* aAttributes );

    CipAttribute* AttributeInsert( int aAttributeId,
        CipDataType     aCipType,
        void*           aData,
        bool            isGetableSingle = true,
        bool            isGetableAll = true,
        bool            isSetableSingle = false
        );

    /**
     * Function AttributeInsert
     * inserts an attribute and returns a pointer to it if succes, else NULL.
     *
     * @param aCookie is saved in the data member of the Attribute and will
     *  later be passed to either AttributeFunc provided.  It can point to anything
     *  convenient.
     *
     * @return CipAttribute* - the dynamically allocated attribute, or NULL if failure.
     * Currently attributes may be overrridden, so any existing CipAttribute in
     * this instance with the same attribute_id will be deleted in favour of this one.
     */
    CipAttribute* AttributeInsert( int aAttributeId,
        AttributeFunc   aGetter,
        bool            isGetableAll = true,
        AttributeFunc   aSetter = NULL,
        void*           aCookie = NULL,
        CipDataType     aCipType = kCipAny
        );

    /**
     * Function Attribute
     * returns a CipAttribute or NULL if not found.
     */
    CipAttribute* Attribute( int aAttributeId ) const;

    const CipAttributes& Attributes() const
    {
        return attributes;
    }

protected:

    void setClass( CipClass* aClass )
    {
        owning_class = aClass;
    }

    int             instance_id;    ///< this instance's number (unique within the class)
    CipClass*       owning_class;   ///< class the instance belongs to or NULL if none.
    CipAttributes   attributes;     ///< sorted pointer array to CipAttribute, unique to this instance
    int             getable_all_mask;

    void ShowAttributes()
    {
        for( CipAttributes::const_iterator it = attributes.begin();
            it != attributes.end();  ++it )
        {
            CIPSTER_TRACE_INFO( "id:%d\n", (*it)->Id() );
        }
    }

private:
    CipInstance( CipInstance& );                    // private because not implemented
    CipInstance& operator=( const CipInstance& );   // private because not implemented
};

#endif // CIPINSTANCE_H_
