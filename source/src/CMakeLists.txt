
cipster_common_includes()


set( CIP_SRCS
    cip/appcontype.cc
    cip/cipattribute.cc
    cip/cipinstance.cc
    cip/cipclass.cc
    cip/cipassembly.cc
    cip/cipcommon.cc
    cip/cipconnection.cc
    cip/cipconnectionmanager.cc
    cip/cipepath.cc
    cip/ciperror.cc
    cip/cipethernetlink.cc
    cip/cipidentity.cc
    cip/cipmessagerouter.cc
    cip/ciptcpipinterface.cc
    cip/cipvendors.cc
    )

set( ENCAP_SRCS
    enet_encap/cpf.cc
    enet_encap/encap.cc
    enet_encap/networkhandler.cc
    enet_encap/sockaddr.cc
    enet_encap/byte_bufs.cc
    )

if( BYTEBUFS_INLINE )
    add_definitions( -DBYTEBUFS_INLINE=1 )
    message( STATUS "Using inline byte_bufs" )
else()
    add_definitions( -DBYTEBUFS_INLINE=0 )
endif()

set( UTILS_SRCS
    utils/random.cc
    utils/xorshiftrandom.cc
    utils/strprint.cc
    )


add_library( eip STATIC
    g_data.cc
    ${CIPster_ADD_CIP_OBJECTS}
    ${CIP_SRCS}
    ${ENCAP_SRCS}
    ${UTILS_SRCS}
    )

add_executable( test_byte_bufs EXCLUDE_FROM_ALL
    enet_encap/byte_bufs.cc
    )
set_target_properties( test_byte_bufs PROPERTIES
    COMPILE_FLAGS -DTEST_BYTE_BUFS
    )
target_link_libraries( test_byte_bufs
    eip
    )

message( "CMAKE_INSTALL_PREFIX:${CMAKE_INSTALL_PREFIX}" )

install( TARGETS eip DESTINATION . )
