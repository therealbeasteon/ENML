add_executable(bootstrap_test bootstrap_test.cpp)
target_link_libraries(bootstrap_test PRIVATE emnl::osservice emnl_warnings)
target_compile_options(bootstrap_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME bootstrap_test COMMAND bootstrap_test)
set_tests_properties(bootstrap_test PROPERTIES LABELS "m0")

add_executable(identity_registry_test identity_registry_test.cpp)
target_link_libraries(identity_registry_test PRIVATE emnl::osservice emnl_warnings)
target_compile_options(identity_registry_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME identity_registry_test COMMAND identity_registry_test)
set_tests_properties(identity_registry_test PROPERTIES LABELS "m0")

add_executable(recovery_policy_test recovery_policy_test.cpp)
target_link_libraries(recovery_policy_test PRIVATE emnl::supervisor emnl_warnings)
target_compile_options(recovery_policy_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME recovery_policy_test COMMAND recovery_policy_test)
set_tests_properties(recovery_policy_test PROPERTIES LABELS "m4-shell")

add_executable(subsystem_lease_test subsystem_lease_test.cpp)
target_link_libraries(subsystem_lease_test PRIVATE emnl::supervisor emnl_warnings)
target_compile_options(subsystem_lease_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME subsystem_lease_test COMMAND subsystem_lease_test)
set_tests_properties(subsystem_lease_test PROPERTIES LABELS "m4-shell")

add_executable(subsystem_lease_authority_test subsystem_lease_authority_test.cpp)
target_link_libraries(subsystem_lease_authority_test PRIVATE emnl::supervisor emnl_warnings)
target_compile_options(subsystem_lease_authority_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME subsystem_lease_authority_test COMMAND subsystem_lease_authority_test)
set_tests_properties(subsystem_lease_authority_test PROPERTIES LABELS "m4-shell")
