#######################################################################
# Standalone security regression test for the attribute-registration  #
# API hardening.  Links only against eip -- intentionally independent  #
# of the (currently unbuilt) CppUTest-based CIPster_Tests target.      #
#######################################################################

cipster_common_includes()

add_executable( attr_security_test attr_security_test.cpp )
target_link_libraries( attr_security_test eip )

add_test( NAME attr_security_test COMMAND attr_security_test )

# Regression for the CipAttribute offset/pointer constructor assert.  Meaningfully
# exercises the assert only with CIPster_TRACES=ON; passes trivially otherwise.
add_executable( attr_ctor_assert_test attr_ctor_assert_test.cpp )
target_link_libraries( attr_ctor_assert_test eip )

add_test( NAME attr_ctor_assert_test COMMAND attr_ctor_assert_test )

# Regression for PR #50: ConnectionData::CorrectSizes() must read an assembly's
# attribute-3 size via AssemblyInstance::SizeBytes(), never by reinterpret-casting
# the CipByteArray storage to ByteBuf* (a type confusion returning garbage sizes).
add_executable( assembly_size_test assembly_size_test.cpp )
target_link_libraries( assembly_size_test eip )

add_test( NAME assembly_size_test COMMAND assembly_size_test )

# Regression for PR #55: CipConn::Activate() must return a failed I/O connection
# slot back to kConnStateNonExistent (and release any grabbed UDP socket) so the
# reserved slot is not leaked after a failed Forward Open.
add_executable( forward_open_leak_test forward_open_leak_test.cpp )
target_link_libraries( forward_open_leak_test eip )

add_test( NAME forward_open_leak_test COMMAND forward_open_leak_test )

# Regression for PR #57: ConnectionData::CorrectSizes() must reject a Forward Open
# whose requested connection size is below the required per-packet protocol overhead
# (Class-1 sequence count + run/idle header), rather than accepting a negative payload.
add_executable( overhead_guard_test overhead_guard_test.cpp )
target_link_libraries( overhead_guard_test eip )

add_test( NAME overhead_guard_test COMMAND overhead_guard_test )

# Compile-time guarantee for issue #2 (typed inserters reject the alias).
add_test( NAME attr_security_compile_fail
    COMMAND ${CMAKE_COMMAND} -E env
        ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail_check.sh
        ${PROJECT_SOURCE_DIR}
        ${USER_INCLUDE_DIR}
        ${CMAKE_CXX_COMPILER} )
