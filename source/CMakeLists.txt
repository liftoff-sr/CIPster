# Default to CMAKE_BUILD_TYPE = Release unless overridden on command line
# http://www.cmake.org/pipermail/cmake/2008-September/023808.html
if( DEFINED CMAKE_BUILD_TYPE )
    set( CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Set to either \"Release\" or \"Debug\"" )
else()
    set( CMAKE_BUILD_TYPE Release CACHE STRING "Set to either \"Release\" or \"Debug\"" )
endif()


cmake_minimum_required( VERSION 2.8.3 )


project( CIPster )


set( CIPster_VERSION_MAJOR 1 )
set( CIPster_VERSION_MINOR 0 )

set( USER_INCLUDE_DIR "" CACHE PATH "Location of user specific include file (opener_user_conf.h)" )

if( USER_INCLUDE_DIR STREQUAL "" )
    message( WARNING "Please set variable USER_INCLUDE_DIR to a directory containing a file named opener_user_conf.h" )
endif()


include_directories( ${USER_INCLUDE_DIR} )

set( CIPster_BUILDSUPPORT_DIR ${PROJECT_SOURCE_DIR}/buildsupport )

include( ${CIPster_BUILDSUPPORT_DIR}/CIPster.cmake )


include( ${CIPster_BUILDSUPPORT_DIR}/CIPster_function_checks.cmake )

set( CIPster_64_BIT_DATA_TYPES_ENABLED CACHE BOOL "Enable 64-bit data type support" )

if( CIPster_64_BIT_DATA_TYPES_ENABLED )
    add_definitions( -DCIPSTER_SUPPORT_64BIT_DATATYPES )
endif()

set( CIPster_TRACES OFF CACHE BOOL "Activate CIPster traces" )
if(CIPster_TRACES)
    createTraceLevelOptions()
endif()


set( CIPster_TESTS OFF CACHE BOOL "Enable tests to be built" )
if( CIPster_TESTS )
    enable_testing()
    enable_language( CXX )
    set( CPPUTEST_HOME "" CACHE PATH "Path to CppUTest directory" )
    include( ${CIPster_BUILDSUPPORT_DIR}/CIPster_Tests.cmake )
    add_subdirectory( tests )
endif()


if( CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang" )
    set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror=implicit-function-declaration" )

    if( NOT WIN32 )
        add_definitions( -fPIC )        # The static library might eventually be linked into a DSO

        # enable clock_gettime:
        add_definitions( -D_POSIX_C_SOURCE=199309L )
    endif()

endif()


####################################################
# Internal cache holding the available CIP objects #
####################################################
set( CIPster_ADD_CIP_OBJECTS "" CACHE INTERNAL STRING )

#######################################
# Add custom CIP objects              #
#######################################
set( CIPster_CIP_OBJECTS_DIR ${PROJECT_SOURCE_DIR}/src/cip_objects )
include( ${CIPster_BUILDSUPPORT_DIR}/CIPster_CIP_Object_generator.cmake )

add_subdirectory( src )
