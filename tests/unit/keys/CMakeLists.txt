add_executable(key_registry_test key_registry_test.cpp)
target_link_libraries(key_registry_test PRIVATE emnl::oskeys emnl_warnings)
target_compile_options(key_registry_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME key_registry_test COMMAND key_registry_test)
