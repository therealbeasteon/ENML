function(add_emnl_core_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE emnl::oscore emnl_warnings)
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "m0")
endfunction()

add_emnl_core_test(result_test result_test.cpp)
add_emnl_core_test(result_trap_test result_trap_test.cpp)
add_emnl_core_test(checked_test checked_test.cpp)
add_emnl_core_test(strong_id_test strong_id_test.cpp)
add_emnl_core_test(native_handle_test native_handle_test.cpp)
