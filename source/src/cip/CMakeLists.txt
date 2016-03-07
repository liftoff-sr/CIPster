#######################################
# CIP library                         #
#######################################

#######################################
# Add common includes                 #
#######################################
opener_common_includes()

#######################################
# Add platform-specific includes      #
#######################################
opener_platform_support("INCLUDES")

set( CIP_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/appcontype.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cipassembly.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cipclass3connection.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cipcommon.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cipconnectionmanager.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cipethernetlink.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cipidentity.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cipioconnection.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cipmessagerouter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ciptcpipinterface.c
    PARENT_SCOPE
    )

#add_library( CIP OBJECT CIP ${CIP_SRC} )
