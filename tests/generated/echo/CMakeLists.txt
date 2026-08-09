add_executable(echo_generated_test echo_generated_test.cpp)
target_link_libraries(echo_generated_test PRIVATE emnl::echo_generated emnl_warnings)
target_compile_options(echo_generated_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME echo_generated_test COMMAND echo_generated_test)
set_tests_properties(echo_generated_test PROPERTIES LABELS "m0")
