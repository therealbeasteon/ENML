add_library(emnl_key_service STATIC
    src/client.cpp
    src/service.cpp
    src/policy.cpp
    src/control.cpp
)

add_library(emnl::key_service ALIAS emnl_key_service)

target_include_directories(emnl_key_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(emnl_key_service
    PUBLIC
        emnl::oskeys
        emnl::osipc
        emnl::osservice
        emnl_warnings
)

target_compile_options(emnl_key_service PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

# Until a BSP supplies a production HierarchicalKeyProvider, the executable
# below is host/CI-only. Its output name is deliberately the real service name
# so Supervisor integration exercises the exact bootstrap/lifecycle shape; the
# linked OpenSSL provider remains explicitly test-only.
if(EMNL_BUILD_OPENSSL_TEST_PROVIDER)
    add_executable(system_keys_host_ci src/host_ci_main.cpp)
    set_target_properties(system_keys_host_ci PROPERTIES OUTPUT_NAME "system.keys")
    target_link_libraries(system_keys_host_ci PRIVATE
        emnl::key_service
        emnl::oskeys_openssl_test_provider
        emnl_warnings
    )
    target_compile_options(system_keys_host_ci PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
endif()
