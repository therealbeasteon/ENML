function(add_emnl_rpc_integration_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE emnl::echo_generated emnl::osipc emnl_warnings)
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_emnl_rpc_integration_test(echo_process_rpc_test echo_process_rpc_test.cpp)
add_emnl_rpc_integration_test(rpc_protocol_test rpc_protocol_test.cpp)
add_emnl_rpc_integration_test(echo_dispatch_adversarial_test echo_dispatch_adversarial_test.cpp)

add_emnl_rpc_integration_test(echo_rpc_allocation_test echo_rpc_allocation_test.cpp)

add_executable(echo_rpc_baseline_test echo_rpc_baseline_test.cpp)
target_link_libraries(echo_rpc_baseline_test PRIVATE
    emnl::echo_generated
    emnl_warnings
)
target_compile_options(echo_rpc_baseline_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME echo_rpc_baseline_test COMMAND echo_rpc_baseline_test)
