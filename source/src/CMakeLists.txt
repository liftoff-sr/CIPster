
cipster_common_includes()

add_definitions( -D__STDC_FORMAT_MACROS )       # <inttypes.h>'s PRIX32
add_definitions( -std=c++0x )
#add_definitions( -Wall )           # TODO


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
    cip/cipethernetlink.cc
    cip/cipidentity.cc
    cip/cipmessagerouter.cc
    cip/ciptcpipinterface.cc
    )

set( ENCAP_SRCS
    enet_encap/cpf.cc
    enet_encap/encap.cc
#    enet_encap/endianconv.cc
    )

if( BYTEBUFS_INLINE )
    add_definitions( -DBYTEBUFS_INLINE=1 )
    message( "Using inline byte_bufs" )
else()
    add_definitions( -DBYTEBUFS_INLINE=0 )
    message( "Not using inline byte_bufs" )
    list( APPEND ENCAP_SRCS enet_encap/byte_bufs.cc )
endif()

set( UTILS_SRCS
    utils/random.cc
    utils/xorshiftrandom.cc
    )


set( OBJECTS_SRCS )

add_subdirectory( cip_objects )


#message( "OBJECTS_SRC:${OBJECTS_SRC}" )

add_library( eip STATIC
    ${CIPster_ADD_CIP_OBJECTS}
    ${CIP_SRCS}
    ${ENCAP_SRCS}
    ${UTILS_SRCS}
    ${OBJECTS_SRCS}
    )

message( "CMAKE_INSTALL_PREFIX:${CMAKE_INSTALL_PREFIX}" )

install( TARGETS eip DESTINATION . )
