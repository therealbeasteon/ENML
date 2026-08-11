add_executable(boot_state_test state_test.cpp)
target_link_libraries(boot_state_test PRIVATE emnl::osboot emnl_warnings)
target_compile_options(boot_state_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME boot_state_test COMMAND boot_state_test)
set_tests_properties(boot_state_test PROPERTIES LABELS "m5-boot")

add_executable(boot_attestation_test attestation_test.cpp)
target_link_libraries(boot_attestation_test PRIVATE emnl::osboot emnl_warnings)
target_compile_options(boot_attestation_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME boot_attestation_test COMMAND boot_attestation_test)
set_tests_properties(boot_attestation_test PROPERTIES LABELS "m5-boot")
