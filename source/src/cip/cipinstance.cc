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
    owning_class( 0 )      // NULL (not owned) until I am inserted into a CipClass
{
}


CipInstance::~CipInstance()
{
    if( instance_id )   // not a public class, then I am an instance.
    {
        if( owning_class )      // if I was inserted int a class, then this is non-NULL
        {
            CIPSTER_TRACE_INFO( "deleting instance %d of class '%s'\n",
                instance_id, owning_class->ClassName().c_str() );
        }
    }
}


CipService* CipInstance::Service( int aServiceId ) const
{
    CIPSTER_ASSERT( owning_class );

    return owning_class->Service( CI_(), aServiceId );
}


CipAttribute* CipInstance::Attribute( int aAttributeId ) const
{
    CIPSTER_ASSERT( owning_class );

    return owning_class->Attribute( CI_(), aAttributeId );
}


const CipAttributes& CipInstance::Attributes() const
{
    return CI_() == _I ?
        owning_class->AttributesI() :
        owning_class->AttributesC();
}
