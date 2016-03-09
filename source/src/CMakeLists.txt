
opener_common_includes()
opener_platform_support("INCLUDES")

add_definitions( -fPIC )

set( CMAKE_SHARED_LINKER_FLAGS "-Wl,--no-undefined" )


set( CIP_SRCS
    cip/appcontype.c
    cip/cipassembly.c
    cip/cipclass3connection.c
    cip/cipcommon.c
    cip/cipconnectionmanager.c
    cip/cipethernetlink.c
    cip/cipidentity.c
    cip/cipioconnection.c
    cip/cipmessagerouter.c
    cip/ciptcpipinterface.c
    )

set( ENCAP_SRCS
    enet_encap/cpf.c
    enet_encap/encap.c
    enet_encap/endianconv.c
    )

set( UTILS_SRCS
    utils/random.c
    utils/xorshiftrandom.c
    )

if( true )
    # compile all sources with the C++ compiler:
    set_source_files_properties( ${CIP_SRCS} ${ENCAP_SRCS} ${UTILS_SRCS}
        PROPERTIES
            LANGUAGE CXX
        )
endif()

add_subdirectory( cip_objects )


opener_common_includes()

opener_platform_support( "INCLUDES" )


message( "CIP_SRC:${CIP_SRC}" )

add_library( eip STATIC
    ${OpENer_ADD_CIP_OBJECTS}
    ${CIP_SRCS}
    ${ENCAP_SRCS}
    ${UTILS_SRCS}
    )

message( "CMAKE_INSTALL_PREFIX:${CMAKE_INSTALL_PREFIX}" )

install( TARGETS eip DESTINATION . )

