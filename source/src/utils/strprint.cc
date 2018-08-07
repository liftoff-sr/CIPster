
/*******************************************************************************
 * Copyright (c) 2016-2018, SoftPLC Corporation.
 *
 ******************************************************************************/

#include <stdarg.h>

#include "../cip/cipcommon.h"


static int vprint( std::string* result, const char* format, va_list ap )
{
    char    msg[512];
    size_t  len = vsnprintf( msg, sizeof(msg), format, ap );

    if( len < sizeof(msg) )     // the output fit into msg
    {
        result->append( msg, msg + len );
        return len;
    }
    else
        return 0;       // increase size of msg[], or shorten prints
}


int StrPrintf( std::string* aResult, const char* aFormat, ... )
{
    va_list     args;

    va_start( args, aFormat );
    int ret = vprint( aResult, aFormat, args );
    va_end( args );

    return ret;
}


std::string StrPrintf( const char* aFormat, ... )
{
    std::string ret;
    va_list     args;

    va_start( args, aFormat );
    int ignore = vprint( &ret, aFormat, args );
    (void) ignore;
    va_end( args );

    return ret;
}

