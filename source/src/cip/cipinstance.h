/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 * Copyright (C) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#ifndef CIPINSTANCE_H_
#define CIPINSTANCE_H_

#include <vector>

#include "cipattribute.h"
#include "cipservice.h"


/**
 * Class CipInstance
 * holds CIP intance info and instances may be contained within a #CipClass.
 */
class CipInstance
{
    friend class CipClass;

public:

    enum _CI
    {
        _I,         // Feature pertains to the Instance with id > 0
        _C,         // Feature pertains to the Class, i.e. instance 0.
    };

    CipInstance( int aInstanceId );

    virtual ~CipInstance();

    int   Id() const            { return instance_id; }

    CipClass*   Class() const   { return owning_class; }

    /// If this a CipClass at instance_id == 0 then return _C,
    /// else return _I because it is an instance.
    _CI CI_() const
    {
        return reinterpret_cast<const CipInstance*>(owning_class) == this ?
            _C : _I;
    }

    /**
     * Function Attribute
     * returns a CipAttribute of this instance or NULL if not found.
     */
    CipAttribute* Attribute( int aAttributeId ) const;

    const CipAttributes& Attributes() const;

    void ShowAttributes()
    {
        const CipAttributes& all = Attributes();

        for( CipAttributes::const_iterator it = all.begin();
            it != all.end();  ++it )
        {
            CIPSTER_TRACE_INFO( "id:%d\n", (*it)->Id() );
        }
    }

    void* Data( const CipAttribute* aAttribute )
    {
        return  aAttribute->is_offset_from_instance_start ?
                    (char*) this + aAttribute->where :
                    (char*) aAttribute->where;
    }

    /**
     * Function Service
     * returns a CipService or NULL if not found.
     * If this instance is a CipClass (w/ instance_id == 0) then
     * the class service is returned, else the instance service is returned.
     */
    CipService* Service( int aServiceId ) const;

protected:

    int             instance_id;    ///< this instance's number (unique within the class)
    CipClass*       owning_class;   ///< class the instance belongs to or NULL if none.

    void setClass( CipClass* aClass )       { owning_class = aClass; }


private:
    CipInstance( CipInstance& );                    // private because not implemented
    CipInstance& operator=( const CipInstance& );   // private because not implemented
};

typedef std::vector<CipInstance*>      CipInstances;

#endif // CIPINSTANCE_H_
