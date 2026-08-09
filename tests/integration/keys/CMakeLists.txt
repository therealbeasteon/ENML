add_executable(key_service_identity_test key_service_identity_test.cpp)
target_link_libraries(key_service_identity_test PRIVATE
    emnl::key_service
    emnl_warnings
)
target_compile_options(key_service_identity_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME key_service_identity_test COMMAND key_service_identity_test)

if(EMNL_BUILD_OPENSSL_TEST_PROVIDER)
    add_executable(key_service_aead_test key_service_aead_test.cpp)
    target_link_libraries(key_service_aead_test PRIVATE
        emnl::oskeys_openssl_test_provider
        emnl_warnings
    )
    target_compile_options(key_service_aead_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_test(NAME key_service_aead_test COMMAND key_service_aead_test)

    add_executable(key_provider_persistence_test key_provider_persistence_test.cpp)
    target_link_libraries(key_provider_persistence_test PRIVATE
        emnl::oskeys_openssl_test_provider
        emnl_warnings
    )
    target_compile_options(key_provider_persistence_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_test(NAME key_provider_persistence_test COMMAND key_provider_persistence_test)

    add_executable(key_service_rpc_aead_test key_service_rpc_aead_test.cpp)
    target_link_libraries(key_service_rpc_aead_test PRIVATE
        emnl::key_service
        emnl::oskeys_openssl_test_provider
        emnl_warnings
    )
    target_compile_options(key_service_rpc_aead_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_test(NAME key_service_rpc_aead_test COMMAND key_service_rpc_aead_test)
endif()
