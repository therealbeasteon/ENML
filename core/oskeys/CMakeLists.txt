add_library(oskeys STATIC
    src/registry.cpp
    src/random_id_linux.cpp
    src/ciphertext.cpp
    src/persistence_linux.cpp
    src/persistence_adopt.cpp
    src/hierarchy.cpp
)

add_library(emnl::oskeys ALIAS oskeys)

target_include_directories(oskeys
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(oskeys
    PUBLIC
        emnl::oscore
        emnl_warnings
)

target_compile_options(oskeys PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

if(EMNL_BUILD_OPENSSL_TEST_PROVIDER)
    find_package(OpenSSL REQUIRED)

    add_library(oskeys_openssl_test_provider STATIC
        testing/src/openssl_provider.cpp
        testing/src/openssl_provider_persistence.cpp
        testing/src/openssl_provider_hierarchy.cpp
    )
    add_library(emnl::oskeys_openssl_test_provider ALIAS oskeys_openssl_test_provider)

    target_include_directories(oskeys_openssl_test_provider
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/testing/include>
    )

    target_link_libraries(oskeys_openssl_test_provider
        PUBLIC
            emnl::oskeys
            OpenSSL::Crypto
            emnl_warnings
    )

    target_compile_options(oskeys_openssl_test_provider PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
endif()
