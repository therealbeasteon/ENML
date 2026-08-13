add_executable(key_registry_test key_registry_test.cpp)
target_link_libraries(key_registry_test PRIVATE emnl::oskeys emnl_warnings)
target_compile_options(key_registry_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME key_registry_test COMMAND key_registry_test)

add_executable(key_random_id_source_test key_random_id_source_test.cpp)
target_link_libraries(key_random_id_source_test PRIVATE emnl::oskeys emnl_warnings)
target_compile_options(key_random_id_source_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME key_random_id_source_test COMMAND key_random_id_source_test)

add_executable(key_hierarchy_test key_hierarchy_test.cpp)
target_link_libraries(key_hierarchy_test PRIVATE emnl::oskeys emnl_warnings)
target_compile_options(key_hierarchy_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME key_hierarchy_test COMMAND key_hierarchy_test)

add_executable(key_profile_root_erasure_test profile_root_erasure_test.cpp)
target_link_libraries(key_profile_root_erasure_test PRIVATE emnl::oskeys emnl_warnings)
target_compile_options(key_profile_root_erasure_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME key_profile_root_erasure_test COMMAND key_profile_root_erasure_test)

add_executable(key_policy_test key_policy_test.cpp)
target_link_libraries(key_policy_test PRIVATE emnl::key_service emnl_warnings)
target_compile_options(key_policy_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME key_policy_test COMMAND key_policy_test)
