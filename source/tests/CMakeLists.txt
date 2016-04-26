#######################################
# Add test subdirectories             #
#######################################

#######################################
# Add test includes                   #
#######################################
add_test_includes()


###################################################
# Copy custom test output file to binary location #
###################################################
configure_file( CTestCustom.cmake ${PROJECT_BINARY_DIR}/CTestCustom.cmake )

add_subdirectory( utils )
add_subdirectory( enet_encap )
add_executable( CIPster_Tests CIPsterTests.cpp )

find_library ( CPPUTEST_LIBRARY CppUTest ${CPPUTEST_HOME}/cpputest_build/lib )
find_library ( CPPUTESTEXT_LIBRARY CppUTestExt ${CPPUTEST_HOME}/cpputest_build/lib )

target_link_libraries( CIPster_Tests gcov ${CPPUTEST_LIBRARY} ${CPPUTESTEXT_LIBRARY} )
target_link_libraries( CIPster_Tests UtilsTest Utils )
target_link_libraries( CIPster_Tests EthernetEncapsulationTest ENET_ENCAP )

########################################
# Adds test to CTest environment       #
########################################
add_test(CIPster_Tests ${EXECUTABLE_OUTPUT_PATH}/CIPster_Tests)
