
function(cipster_add_definition)
    foreach(ARG ${ARGV})
        set_property(GLOBAL APPEND PROPERTY CIPSTER_DEFINITION ${ARG})
    endforeach(ARG)
endfunction()


# Adds common Include directories
macro(cipster_common_includes)
    set( SRC_DIR "${PROJECT_SOURCE_DIR}/src" )
    set( CIP_SRC_DIR "${SRC_DIR}/cip" )
    set( ENET_ENCAP_SRC_DIR "${SRC_DIR}/enet_encap" )
    set( UTILS_SRC_DIR "${SRC_DIR}/utils")

    include_directories(
        ${PROJECT_SOURCE_DIR}
        ${SRC_DIR}
        ${CIP_SRC_DIR}
        ${ENET_ENCAP_SRC_DIR}
        ${PORTS_SRC_DIR}
        ${UTILS_SRC_DIR}
        ${CIPster_CIP_OBJECTS_DIR}
        )
endmacro()


macro(cipster_add_cip_object NAME DESCRIPTION)
  set(CIPster_CIP_OBJECT_${NAME} OFF CACHE BOOL "${DESCRIPTION}")

  foreach(dependencies ${ARGN})
    if(NOT ${dependencies})
        return()
    endif()
  endforeach()

  if( NOT CIPster_CIP_OBJECT_${NAME} )
    return()
  endif()

endmacro()


# Creates options for trace level
macro(createTraceLevelOptions)
    add_definitions( -DCIPSTER_WITH_TRACES )
    set( TRACE_LEVEL 0 )
    set( CIPster_TRACE_LEVEL_ERROR ON CACHE BOOL "Error trace level" )
    set( CIPster_TRACE_LEVEL_WARNING ON CACHE BOOL "Warning trace level" )
    set( CIPster_TRACE_LEVEL_STATE ON CACHE BOOL "State trace level" )
    set( CIPster_TRACE_LEVEL_INFO ON CACHE BOOL "Info trace level" )

    if(CIPster_TRACE_LEVEL_ERROR)
        math( EXPR TRACE_LEVEL "${TRACE_LEVEL} + 1" )
    endif()

    if(CIPster_TRACE_LEVEL_WARNING)
        math( EXPR TRACE_LEVEL "${TRACE_LEVEL} + 2" )
    endif()

    if(CIPster_TRACE_LEVEL_STATE)
        math( EXPR TRACE_LEVEL "${TRACE_LEVEL} + 4" )
    endif()

    if(CIPster_TRACE_LEVEL_INFO)
        math( EXPR TRACE_LEVEL "${TRACE_LEVEL} + 8" )
    endif()

    add_definitions(-DCIPSTER_TRACE_LEVEL=${TRACE_LEVEL})
endmacro()
