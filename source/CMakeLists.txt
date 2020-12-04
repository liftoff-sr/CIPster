# Default to CMAKE_BUILD_TYPE = Release unless overridden on command line
# http://www.cmake.org/pipermail/cmake/2008-September/023808.html
if( DEFINED CMAKE_BUILD_TYPE )
    set( CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Set to either \"Release\" or \"Debug\"" )
else()
    set( CMAKE_BUILD_TYPE Release CACHE STRING "Set to either \"Release\" or \"Debug\"" )
endif()


cmake_minimum_required( VERSION 2.8.3 )


project( CIPster )

if( CMAKE_VERSION VERSION_GREATER 3.1 )
    cmake_policy(SET CMP0056 OLD)
endif()

set( CIPster_VERSION_MAJOR 1 )
set( CIPster_VERSION_MINOR 0 )

option( BYTEBUFS_INLINE "Use inline byte_bufs, which is bigger code and maybe a little faster" NO )

set( USER_INCLUDE_DIR "" CACHE PATH "Location of user specific include file (cipster_user_conf.h)" )

if( USER_INCLUDE_DIR STREQUAL "" )
    message( WARNING "Please set variable USER_INCLUDE_DIR to a directory containing a file named cipster_user_conf.h" )
endif()


include_directories( ${USER_INCLUDE_DIR} )

set( CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/buildsupport )

include( ${CMAKE_MODULE_PATH}/CIPster.cmake )

include( ${CMAKE_MODULE_PATH}/CIPster_function_checks.cmake )


# Doxygen Output #=====================================

find_package( Doxygen )
if( DOXYGEN_FOUND )
    add_custom_target( doxygen-docs
        ${CMAKE_COMMAND} -E remove_directory doc/doxygen
        COMMAND ${DOXYGEN_EXECUTABLE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS Doxyfile
        COMMENT "building doxygen docs into directory doc/doxygen/html"
        )
else()
    message( STATUS "WARNING - Doxygen not found - doxygen-docs (Source Docs) target not created" )
endif()

if( CMAKE_VERSION VERSION_GREATER 3.1 )
    find_package( Iconv )
else()
    set( CMAKE_CXX_COMPILE_OPTIONS_VISIBILITY "-fvisibility=" )
    set( CMAKE_CXX_COMPILE_OPTIONS_VISIBILITY_INLINES_HIDDEN "-fvisibility-inlines-hidden" )
endif()

if( Iconv_FOUND )
    if( NOT Iconv_IS_BUILT_IN )
        include_directories( ${Iconv_INCLUDE_DIR} )
    endif()
    # TODO put into a config.h generator
    add_definitions( -DHAVE_ICONV=1 )
else()
    message( STATUS "WARNING - libiconv not found.  BufReader::get_STRING2()
        and BufWriter::put_STRING2() will not be full featured" )
endif()

set( CIPster_TRACES OFF CACHE BOOL "Activate CIPster traces" )
if(CIPster_TRACES)
    createTraceLevelOptions()
else()
    message( "CIPster_TRACES is OFF" )
endif()


set( CIPster_TESTS OFF CACHE BOOL "Enable tests to be built" )
if( CIPster_TESTS )
    enable_testing()
    enable_language( CXX )
    set( CPPUTEST_HOME "" CACHE PATH "Path to CppUTest directory" )
    include( ${CMAKE_MODULE_PATH}/CIPster_Tests.cmake )
    add_subdirectory( tests )
endif()


if( CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang" )

    set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -ggdb3 -DDEBUG" )
    set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -Wall -Wno-reorder -Wno-sign-compare" )

    if( APPLE )
        add_definitions( -D_DARWIN_C_SOURCE )
    endif()
    if( NOT APPLE )
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_COMPILE_OPTIONS_VISIBILITY}hidden" )
    endif()
    if( NOT APPLE )
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_COMPILE_OPTIONS_VISIBILITY_INLINES_HIDDEN}" )
    endif()

    if( WIN32 )
        # https://sourceforge.net/p/mingw-w64/bugs/295/
        add_definitions( -D__USE_MINGW_ANSI_STDIO )
    else()
        add_definitions( -fPIC )        # The static library might eventually be linked into a DSO

        # enable clock_gettime:
        add_definitions( -D_POSIX_C_SOURCE=199309L )
    endif()

endif()

add_subdirectory( src )
