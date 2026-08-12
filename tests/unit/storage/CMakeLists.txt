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

add_executable(storage_protected_publication_test protected_publication_test.cpp)
target_link_libraries(storage_protected_publication_test PRIVATE emnl::osstorage emnl_warnings)
target_compile_options(storage_protected_publication_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME storage_protected_publication_test COMMAND storage_protected_publication_test)

add_executable(storage_protected_atomic_replace_test protected_atomic_replace_test.cpp)
target_link_libraries(storage_protected_atomic_replace_test PRIVATE emnl::osstorage emnl_warnings)
target_compile_options(storage_protected_atomic_replace_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME storage_protected_atomic_replace_test COMMAND storage_protected_atomic_replace_test)

add_executable(storage_protected_namespace_test protected_namespace_test.cpp)
target_link_libraries(storage_protected_namespace_test PRIVATE emnl::osstorage emnl_warnings)
target_compile_options(storage_protected_namespace_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME storage_protected_namespace_test COMMAND storage_protected_namespace_test)

add_executable(storage_protected_namespace_snapshot_test protected_namespace_snapshot_test.cpp)
target_link_libraries(storage_protected_namespace_snapshot_test PRIVATE emnl::osstorage emnl_warnings)
target_compile_options(storage_protected_namespace_snapshot_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME storage_protected_namespace_snapshot_test COMMAND storage_protected_namespace_snapshot_test)

if(EMNL_BUILD_OPENSSL_TEST_PROVIDER)
    add_executable(storage_protected_crypto_test protected_crypto_test.cpp)
    target_link_libraries(storage_protected_crypto_test PRIVATE
        emnl::osstorage
        emnl::oskeys_openssl_test_provider
        emnl_warnings
    )
    target_compile_options(storage_protected_crypto_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_test(NAME storage_protected_crypto_test COMMAND storage_protected_crypto_test)

    add_executable(storage_protected_key_record_test protected_key_record_test.cpp)
    target_link_libraries(storage_protected_key_record_test PRIVATE
        emnl::osstorage
        emnl::oskeys_openssl_test_provider
        emnl_warnings
    )
    target_compile_options(storage_protected_key_record_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_test(NAME storage_protected_key_record_test COMMAND storage_protected_key_record_test)

    add_executable(storage_protected_namespace_crypto_test protected_namespace_crypto_test.cpp)
    target_link_libraries(storage_protected_namespace_crypto_test PRIVATE
        emnl::osstorage
        emnl::oskeys_openssl_test_provider
        emnl_warnings
    )
    target_compile_options(storage_protected_namespace_crypto_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_test(NAME storage_protected_namespace_crypto_test COMMAND storage_protected_namespace_crypto_test)
endif()
