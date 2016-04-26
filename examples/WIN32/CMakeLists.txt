add_subdirectory(sample_application)

set( PLATFORM_SPEC_SRC networkhandler.c)

#######################################
# Add common includes                 #
#######################################
opener_common_includes()

#######################################
# Add platfrom specific things        #
#######################################

opener_platform_support("INCLUDES")

set (PLATFORMLIBNAME ${CIPster_PLATFORM}PLATFORM)

add_library( ${PLATFORMLIBNAME} ${PLATFORM_SPEC_SRC})

add_executable(CIPster main.c)

target_link_libraries( CIPster ${PLATFORMLIBNAME} CIP SAMPLE_APP ENET_ENCAP ws2_32 ${CIPster_CIP_OBJECTS} )
