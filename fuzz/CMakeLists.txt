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
