add_executable(storage_private_storage_test private_storage_test.cpp)
target_link_libraries(storage_private_storage_test PRIVATE emnl::osstorage emnl_warnings)
target_compile_options(storage_private_storage_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME storage_private_storage_test COMMAND storage_private_storage_test)

add_executable(storage_protected_format_test protected_format_test.cpp)
target_link_libraries(storage_protected_format_test PRIVATE emnl::osstorage emnl_warnings)
target_compile_options(storage_protected_format_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME storage_protected_format_test COMMAND storage_protected_format_test)
