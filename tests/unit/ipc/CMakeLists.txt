function(add_emnl_ipc_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE emnl::osipc emnl_warnings)
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_emnl_ipc_test(encoder_test encoder_test.cpp)
add_emnl_ipc_test(decoder_test decoder_test.cpp)
add_emnl_ipc_test(wire_test wire_test.cpp)
add_emnl_ipc_test(allocation_test allocation_test.cpp)
add_emnl_ipc_test(channel_test channel_test.cpp)
add_emnl_ipc_test(rpc_test rpc_test.cpp)

add_emnl_ipc_test(channel_allocation_test channel_allocation_test.cpp)
