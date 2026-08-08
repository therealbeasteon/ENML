if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    return()
endif()

add_executable(arm64_runtime_probe_test arm64_runtime_probe_test.cpp)
target_link_libraries(arm64_runtime_probe_test PRIVATE emnl::osipc emnl_warnings)
target_compile_options(arm64_runtime_probe_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME arm64_runtime_probe_test COMMAND arm64_runtime_probe_test)
set_tests_properties(arm64_runtime_probe_test PROPERTIES LABELS "arm64;qemu-user-safe")
