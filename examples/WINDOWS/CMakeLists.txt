# Default to CMAKE_BUILD_TYPE = Release unless overridden on command line
# http://www.cmake.org/pipermail/cmake/2008-September/023808.html
if( DEFINED CMAKE_BUILD_TYPE )
    set( CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Set to either \"Release\" or \"Debug\"" )
else()
    set( CMAKE_BUILD_TYPE Release CACHE STRING "Set to either \"Release\" or \"Debug\"" )
endif()


project( "CIPster on Windows" )


include(ExternalProject)

cmake_minimum_required( VERSION 2.8.3 )


set( CIPSTER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../ )

set( USER_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/sample_application
    CACHE PATH "Location of user specific include file (cipster_user_conf.h)"
    )

if( CMAKE_BUILD_TYPE STREQUAL Debug )
    add_definitions( -DCIPSTER_WITH_TRACES -DCIPSTER_TRACE_LEVEL=15 )
    set( TRACE_SPEC "-DCIPster_TRACES=ON" )
endif()

add_definitions( -std=c++0x )

# PREFIX is for ExternalProject_Add, and tells where to build CIPster as a sub project:
# below our current out of tree build directory.
set( PREFIX ${CMAKE_CURRENT_BINARY_DIR}/build-CIPster )

# build CIPster as a nested project, the result of which is libeip.a
# in directory ${PREFIX}
ExternalProject_Add( eip
    PREFIX ${PREFIX}
    SOURCE_DIR ${CIPSTER_DIR}/source
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND}
        -DCMAKE_GENERATOR=${CMAKE_GENERATOR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${PREFIX}
        -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}
        -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DUSER_INCLUDE_DIR=${USER_INCLUDE_DIR}
        ${TRACE_SPEC}       # empty for non Debug CMAKE_BUILD_TYPE
        <SOURCE_DIR>
    BUILD_COMMAND make

    INSTALL_COMMAND make install
    )

#message( "TRACE_SPEC=${TRACE_SPEC}" )


# These variables would typically be found by a CMake find EIP module, but set
# them manually here
set( EIP_INCLUDE_DIR ${CIPSTER_DIR}/source/src )
set( EIP_LIBRARIES   ${PREFIX}/libeip.a )


if( NOT APPLE )
    set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_COMPILE_OPTIONS_VISIBILITY}hidden" )
endif()
if( NOT APPLE )
    set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_COMPILE_OPTIONS_VISIBILITY_INLINES_HIDDEN}" )
endif()


if( CMAKE_COMPILER_IS_GNUCXX )
    # statically link the gcc and socket libraries so that dynamic ones do not have to be
    # installed on the ultimate target separately.
    set( CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS} -static )
endif()


include_directories(
    .
    ${EIP_INCLUDE_DIR}
    ${USER_INCLUDE_DIR}
    )

set( PGM sample )     # name of program

set( PGM_SRCS
    main.cc
    sample_application/sampleapplication.cc
    )

add_executable( ${PGM}
    ${PGM_SRCS}
    )
target_link_libraries( ${PGM}
    ${EIP_LIBRARIES}
    ws2_32
    )
add_dependencies( ${PGM} eip )

