add_executable(network_policy_test policy_test.cpp)
target_link_libraries(network_policy_test PRIVATE emnl::osnetwork emnl_warnings)
target_compile_options(network_policy_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME network_policy_test COMMAND network_policy_test)
set_tests_properties(network_policy_test PROPERTIES LABELS "m4-shell")
