/*******************************************************************************
 * Copyright (c) 2009, Rockwell Automation, Inc.
 *
 ******************************************************************************/
#ifndef CIPSTER_TRACE_H_
#define CIPSTER_TRACE_H_

#include <cipster_user_conf.h>


/** @file trace.h
 * @brief Tracing infrastructure for CIPster
 */


/**
 * CIPSTER_TRACE_LEVEL_ERROR Enable tracing of error messages. This is the
 * default if no trace level is given.
 */
#define CIPSTER_TRACE_LEVEL_ERROR       (1<<0)

/// @def CIPSTER_TRACE_LEVEL_WARNING Enable tracing of warning messages
#define CIPSTER_TRACE_LEVEL_WARNING     (1<<1)

/// @def CIPSTER_TRACE_LEVEL_WARNING Enable tracing of state messages
#define CIPSTER_TRACE_LEVEL_STATE       (1<<2)

/// @def CIPSTER_TRACE_LEVEL_INFO Enable tracing of info messages
#define CIPSTER_TRACE_LEVEL_INFO        (1<<3)


extern int g_CIPSTER_TRACE_LEVEL;       // defined in g_data.cc


#ifdef CIPSTER_WITH_TRACES

#ifndef CIPSTER_TRACE_LEVEL

#if !defined(__GNUG__)
#pragma message( "CIPSTER_TRACE_LEVEL was not defined setting it to CIPSTER_TRACE_LEVEL_ERROR" )
#else
#warning CIPSTER_TRACE_LEVEL was not defined setting it to CIPSTER_TRACE_LEVEL_ERROR
#endif

#define CIPSTER_TRACE_LEVEL             CIPSTER_TRACE_LEVEL_ERROR
#endif

// @def CIPSTER_TRACE_ENABLED Can be used for conditional code compilation
#define CIPSTER_TRACE_ENABLED

/** @def CIPSTER_TRACE_ERR(...) Trace error messages.
 *  In order to activate this trace level set the CIPSTER_TRACE_LEVEL_ERROR flag
 *  in CIPSTER_TRACE_LEVEL.
 */
#define CIPSTER_TRACE_ERR(...)  \
  do {                          \
    if( CIPSTER_TRACE_LEVEL_ERROR & g_CIPSTER_TRACE_LEVEL ) \
        LOG_TRACE(__VA_ARGS__); \
  } while (0)

/** @def CIPSTER_TRACE_WARN(...) Trace warning messages.
 *  In order to activate this trace level set the CIPSTER_TRACE_LEVEL_WARNING
 * flag in CIPSTER_TRACE_LEVEL.
 */
#define CIPSTER_TRACE_WARN(...)  \
  do {                           \
    if( CIPSTER_TRACE_LEVEL_WARNING & g_CIPSTER_TRACE_LEVEL ) \
      LOG_TRACE(__VA_ARGS__);    \
  } while (0)

/** @def CIPSTER_TRACE_STATE(...) Trace state messages.
 *  In order to activate this trace level set the CIPSTER_TRACE_LEVEL_STATE flag
 *  in CIPSTER_TRACE_LEVEL.
 */
#define CIPSTER_TRACE_STATE(...) \
  do {                           \
    if( CIPSTER_TRACE_LEVEL_STATE & g_CIPSTER_TRACE_LEVEL ) \
        LOG_TRACE(__VA_ARGS__);  \
  } while (0)

/** @def CIPSTER_TRACE_INFO(...) Trace information messages.
 *  In order to activate this trace level set the CIPSTER_TRACE_LEVEL_INFO flag
 *  in CIPSTER_TRACE_LEVEL.
 */
#define CIPSTER_TRACE_INFO(...)  \
  do {                           \
    if( CIPSTER_TRACE_LEVEL_INFO & g_CIPSTER_TRACE_LEVEL ) \
        LOG_TRACE(__VA_ARGS__);  \
  } while (0)

#else       // define the tracing macros empty in order to save space

#undef CIPSTER_TRACE_LEVEL
#define CIPSTER_TRACE_LEVEL 0

#define CIPSTER_TRACE_ERR(...)
#define CIPSTER_TRACE_WARN(...)
#define CIPSTER_TRACE_STATE(...)
#define CIPSTER_TRACE_INFO(...)
#endif

#endif //CIPSTER_TRACE_H_
