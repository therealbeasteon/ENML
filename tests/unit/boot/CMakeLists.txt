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

add_executable(boot_sealing_test sealing_test.cpp)
target_link_libraries(boot_sealing_test PRIVATE emnl::osboot emnl_warnings)
target_compile_options(boot_sealing_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME boot_sealing_test COMMAND boot_sealing_test)
set_tests_properties(boot_sealing_test PROPERTIES LABELS "m5-boot")

add_executable(boot_profile_unlock_test profile_unlock_test.cpp)
target_link_libraries(boot_profile_unlock_test PRIVATE emnl::osboot emnl_warnings)
target_compile_options(boot_profile_unlock_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME boot_profile_unlock_test COMMAND boot_profile_unlock_test)
set_tests_properties(boot_profile_unlock_test PROPERTIES LABELS "m5-boot")

add_executable(boot_profile_protector_test profile_protector_test.cpp)
target_link_libraries(boot_profile_protector_test PRIVATE emnl::osboot emnl_warnings)
target_compile_options(boot_profile_protector_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME boot_profile_protector_test COMMAND boot_profile_protector_test)
set_tests_properties(boot_profile_protector_test PROPERTIES LABELS "m5-boot")

add_executable(boot_profile_restore_test profile_restore_test.cpp)
target_link_libraries(boot_profile_restore_test PRIVATE emnl::osboot emnl_warnings)
target_compile_options(boot_profile_restore_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME boot_profile_restore_test COMMAND boot_profile_restore_test)
set_tests_properties(boot_profile_restore_test PROPERTIES LABELS "m5-boot")

add_executable(boot_transparency_test transparency_test.cpp)
target_link_libraries(boot_transparency_test PRIVATE emnl::osboot emnl_warnings)
target_compile_options(boot_transparency_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME boot_transparency_test COMMAND boot_transparency_test)
set_tests_properties(boot_transparency_test PROPERTIES LABELS "m5-boot")
