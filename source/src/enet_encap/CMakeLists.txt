#######################################
# Ethernet encapsulation library      #
#######################################

set( ENET_ENCAP_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/cpf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/encap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/endianconv.c
    PARENT_SCOPE
    )

#######################################
# Add common includes                 #
#######################################
opener_common_includes()

#######################################
# Add platform-specific includes      #
#######################################
opener_platform_support("INCLUDES")

#add_library( ENET_ENCAP OBJECT ENET_ENCAP ${ENET_ENCAP_SRC} )
