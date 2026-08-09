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
