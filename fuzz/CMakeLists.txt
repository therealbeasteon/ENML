if(NOT EMNL_BUILD_FUZZING)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "EMNL fuzz targets currently require Clang/libFuzzer")
endif()

add_executable(ipc_decoder_fuzz ipc/decoder_fuzz.cpp)
target_link_libraries(ipc_decoder_fuzz PRIVATE emnl::osipc emnl_warnings)
target_compile_options(ipc_decoder_fuzz PRIVATE
    -fno-exceptions
    -fno-rtti
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
)
target_link_options(ipc_decoder_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)

add_executable(kernel_ipc_syscall_fuzz kernel/ipc_syscall_fuzz.cpp)
target_link_libraries(kernel_ipc_syscall_fuzz PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_ipc_syscall_fuzz PRIVATE
    -fno-exceptions
    -fno-rtti
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
)
target_link_options(kernel_ipc_syscall_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)

add_executable(kernel_syscall_surface_fuzz kernel/syscall_surface_fuzz.cpp)
target_link_libraries(kernel_syscall_surface_fuzz PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_syscall_surface_fuzz PRIVATE
    -fno-exceptions
    -fno-rtti
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
)
target_link_options(kernel_syscall_surface_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)

add_executable(kernel_capability_fuzz kernel/capability_fuzz.cpp)
target_link_libraries(kernel_capability_fuzz PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_capability_fuzz PRIVATE
    -fno-exceptions
    -fno-rtti
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
)
target_link_options(kernel_capability_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)

add_executable(osidlc_compiler_fuzz osidlc/compiler_fuzz.cpp)
target_link_libraries(osidlc_compiler_fuzz PRIVATE osidlc_lib emnl_warnings)
target_compile_options(osidlc_compiler_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
)
target_link_options(osidlc_compiler_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)

add_executable(rpc_error_fuzz ipc/rpc_error_fuzz.cpp)
target_link_libraries(rpc_error_fuzz PRIVATE emnl::osipc emnl_warnings)
target_compile_options(rpc_error_fuzz PRIVATE
    -fno-exceptions
    -fno-rtti
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
)
target_link_options(rpc_error_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)

add_executable(storage_relative_path_fuzz storage/relative_path_fuzz.cpp)
target_link_libraries(storage_relative_path_fuzz PRIVATE emnl::osstorage emnl_warnings)
target_compile_options(storage_relative_path_fuzz PRIVATE
    -fno-exceptions
    -fno-rtti
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
)
target_link_options(storage_relative_path_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)

if(EMNL_BUILD_OPENSSL_TEST_PROVIDER)
    add_executable(keys_registry_snapshot_fuzz keys/registry_snapshot_fuzz.cpp)
    target_link_libraries(keys_registry_snapshot_fuzz PRIVATE
        emnl::oskeys_openssl_test_provider
        emnl_warnings
    )
    target_compile_options(keys_registry_snapshot_fuzz PRIVATE
        -fno-exceptions
        -fno-rtti
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(keys_registry_snapshot_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined
    )
endif()

add_executable(time_protection_fuzz time/protection_fuzz.cpp)
target_link_libraries(time_protection_fuzz PRIVATE emnl::ostime emnl_warnings)
target_compile_options(time_protection_fuzz PRIVATE
    -fno-exceptions
    -fno-rtti
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
)
target_link_options(time_protection_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)

add_executable(device_access_fuzz device/access_fuzz.cpp)
target_link_libraries(device_access_fuzz PRIVATE emnl::osdevice emnl_warnings)
target_compile_options(device_access_fuzz PRIVATE
    -fno-exceptions
    -fno-rtti
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
)
target_link_options(device_access_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)

add_executable(boot_state_fuzz boot/state_fuzz.cpp)
target_link_libraries(boot_state_fuzz PRIVATE emnl::osboot emnl_warnings)
target_compile_options(boot_state_fuzz PRIVATE
    -fno-exceptions
    -fno-rtti
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
)
target_link_options(boot_state_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)

add_executable(package_manifest_fuzz package/manifest_analyzer_fuzz.cpp)
target_link_libraries(package_manifest_fuzz PRIVATE emnl::ospkg emnl_warnings)
target_compile_options(package_manifest_fuzz PRIVATE
    -fno-exceptions
    -fno-rtti
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
)
target_link_options(package_manifest_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)
