#######################################################################
# Standalone security regression test for the attribute-registration  #
# API hardening.  Links only against eip -- intentionally independent  #
# of the (currently unbuilt) CppUTest-based CIPster_Tests target.      #
#######################################################################

cipster_common_includes()

add_executable( attr_security_test attr_security_test.cpp )
target_link_libraries( attr_security_test eip )

add_test( NAME attr_security_test COMMAND attr_security_test )

# Compile-time guarantee for issue #2 (typed inserters reject the alias).
add_test( NAME attr_security_compile_fail
    COMMAND ${CMAKE_COMMAND} -E env
        ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail_check.sh
        ${PROJECT_SOURCE_DIR}
        ${USER_INCLUDE_DIR}
        ${CMAKE_CXX_COMPILER} )
