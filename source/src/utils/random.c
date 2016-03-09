/*
 * random.cpp
 *
 *  Created on: Dec 16, 2013
 *      Author: mmm
 */

#include "random.h"
#include <stdlib.h>


Random* RandomNew( SetSeed set_seed, GetNextUInt32 get_next_uint32 )
{
    Random* out = (Random*) malloc( sizeof(Random) );

    out->set_seed        = set_seed;
    out->get_next_uint32 = get_next_uint32;

    return out;
}
