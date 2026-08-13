add_executable(sandbox_substrate_test substrate_test.cpp)
target_link_libraries(sandbox_substrate_test PRIVATE emnl::ossandbox emnl_warnings)
target_compile_options(sandbox_substrate_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME sandbox_substrate_test COMMAND sandbox_substrate_test)
set_tests_properties(sandbox_substrate_test PROPERTIES LABELS "m0")
