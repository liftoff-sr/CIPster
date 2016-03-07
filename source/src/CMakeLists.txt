
add_definitions( -fPIC )

#if( CMAKE_BUILD_TYPE STREQUALS Debug )

set( CMAKE_SHARED_LINKER_FLAGS "-Wl,--no-undefined" )


#######################################
# Add subdirectories                  #
#######################################
add_subdirectory( cip )
add_subdirectory( cip_objects )
add_subdirectory( enet_encap )
#add_subdirectory( ports )
add_subdirectory( utils )

#######################################
# Add common includes                 #
#######################################
opener_common_includes()

#######################################
# Add platfrom specific things        #
#######################################

opener_platform_support( "INCLUDES" )


message( "CIP_SRC:${CIP_SRC}" )

add_library( eip STATIC
    ${OpENer_ADD_CIP_OBJECTS}
    ${CIP_SRC}
    ${ENET_ENCAP_SRC}
    )
target_link_libraries( eip
    Utils
    )

message( "CMAKE_INSTALL_PREFIX:${CMAKE_INSTALL_PREFIX}" )

install( TARGETS eip DESTINATION . )

