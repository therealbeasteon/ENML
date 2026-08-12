add_executable(network_policy_test policy_test.cpp)
target_link_libraries(network_policy_test PRIVATE emnl::osnetwork emnl_warnings)
target_compile_options(network_policy_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME network_policy_test COMMAND network_policy_test)
set_tests_properties(network_policy_test PROPERTIES LABELS "m4-shell")

add_executable(network_capability_test capability_test.cpp)
target_link_libraries(network_capability_test PRIVATE emnl::osnetwork emnl_warnings)
target_compile_options(network_capability_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME network_capability_test COMMAND network_capability_test)
set_tests_properties(network_capability_test PROPERTIES LABELS "m4-shell")

add_executable(network_broker_test broker_test.cpp)
target_link_libraries(network_broker_test PRIVATE emnl::osnetwork emnl_warnings)
target_compile_options(network_broker_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME network_broker_test COMMAND network_broker_test)
set_tests_properties(network_broker_test PROPERTIES LABELS "m4-shell")

add_executable(network_resolution_test resolution_test.cpp)
target_link_libraries(network_resolution_test PRIVATE emnl::osnetwork emnl_warnings)
target_compile_options(network_resolution_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME network_resolution_test COMMAND network_resolution_test)
set_tests_properties(network_resolution_test PROPERTIES LABELS "m4-shell")
