# Default to CMAKE_BUILD_TYPE = Release unless overridden on command line
# http://www.cmake.org/pipermail/cmake/2008-September/023808.html
if( DEFINED CMAKE_BUILD_TYPE )
    set( CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Set to either \"Release\" or \"Debug\"" )
else()
    set( CMAKE_BUILD_TYPE Release CACHE STRING "Set to either \"Release\" or \"Debug\"" )
endif()


# Default to OpENer_PLATFORM = POSIX
if( DEFINED OpENer_PLATFORM )
    set( OpENer_PLATFORM ${OpENer_PLATFORM} CACHE STRINGS "Platform OpENer will be built for" )
else()
    set( OpENer_PLATFORM POSIX CACHE STRINGS "Platform OpENer will be built for" )
endif()

set( OpENer_KNOWN_PLATFORMS "POSIX" "WIN32" )
set_property(CACHE OpENer_PLATFORM PROPERTY STRINGS ${OpENer_KNOWN_PLATFORMS} )


cmake_minimum_required( VERSION 2.8.3 )

project( OpENer )

set( OpENer_VERSION_MAJOR 1 )
set( OpENer_VERSION_MINOR 2 )

set( USER_INCLUDE_DIR "" CACHE PATH "Location of user specific include file (opener_user_conf.h)" )

include_directories( ${USER_INCLUDE_DIR} )

set( OpENer_BUILDSUPPORT_DIR ${PROJECT_SOURCE_DIR}/buildsupport )

INCLUDE( ${OpENer_BUILDSUPPORT_DIR}/OpENer.cmake )


include( ${OpENer_BUILDSUPPORT_DIR}/OpENer_function_checks.cmake )

set( OpENer_64_BIT_DATA_TYPES_ENABLED CACHE BOOL "Enable 64-bit data type support" )

if( OpENer_64_BIT_DATA_TYPES_ENABLED )
    add_definitions( -DOPENER_SUPPORT_64BIT_DATATYPES )
endif()

set( OpENer_TRACES OFF CACHE BOOL "Activate OpENer traces" )
if(OpENer_TRACES)
    createTraceLevelOptions()
endif()


set( OpENer_TESTS OFF CACHE BOOL "Enable tests to be built" )
if( OpENer_TESTS )
    enable_testing()
    enable_language( CXX )
    set( CPPUTEST_HOME "" CACHE PATH "Path to CppUTest directory" )
    include( ${OpENer_BUILDSUPPORT_DIR}/OpENer_Tests.cmake )
    add_subdirectory( tests )
endif()


if( CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang" )
    set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror=implicit-function-declaration" )

    # enable clock_gettime:
    add_definitions( -D_POSIX_C_SOURCE=199309L )
endif()


####################################################
# Internal cache holding the available CIP objects #
####################################################
set( OpENer_ADD_CIP_OBJECTS "" CACHE INTERNAL STRING )

#######################################
# Add custom CIP objects              #
#######################################
set( OpENer_CIP_OBJECTS_DIR ${PROJECT_SOURCE_DIR}/src/cip_objects )
include( ${OpENer_BUILDSUPPORT_DIR}/OpENer_CIP_Object_generator.cmake )

add_subdirectory( src )

