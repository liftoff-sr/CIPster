
opener_common_includes()

set( CIP_SRCS
    cip/appcontype.c
    cip/cipassembly.c
    cip/cipclass3connection.c
    cip/cipcommon.c
    cip/cipconnectionmanager.c
    cip/cipepath.c
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


set( OBJECTS_SRCS )

add_subdirectory( cip_objects )


# compile all sources with the C++ compiler:
set_source_files_properties(
    ${CIP_SRCS}
    ${ENCAP_SRCS}
    ${UTILS_SRCS}
    ${OBJECTS_SRCS}

        PROPERTIES
            LANGUAGE CXX
    )

add_definitions( -D__STDC_FORMAT_MACROS )       # <inttypes.h>'s PRIX32
add_definitions( -std=c++0x )


opener_common_includes()


message( "OBJECTS_SRC:${OBJECTS_SRC}" )


add_library( eip STATIC
    ${CIPster_ADD_CIP_OBJECTS}
    ${CIP_SRCS}
    ${ENCAP_SRCS}
    ${UTILS_SRCS}
    ${OBJECTS_SRCS}
    )

message( "CMAKE_INSTALL_PREFIX:${CMAKE_INSTALL_PREFIX}" )

install( TARGETS eip DESTINATION . )

