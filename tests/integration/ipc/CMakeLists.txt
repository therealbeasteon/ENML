function(add_emnl_ipc_integration_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE emnl::osipc emnl_warnings)
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_emnl_ipc_integration_test(channel_process_test channel_process_test.cpp)
add_emnl_ipc_integration_test(channel_adversarial_test channel_adversarial_test.cpp)

add_executable(channel_stress_test channel_stress_test.cpp)
target_link_libraries(channel_stress_test PRIVATE emnl::osipc emnl_warnings)
target_compile_options(channel_stress_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME channel_stress_test COMMAND channel_stress_test)
