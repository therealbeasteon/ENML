add_executable(image_ckx_test ckx_test.cpp)
target_link_libraries(image_ckx_test PRIVATE emnl::osimage emnl_warnings)
target_compile_options(image_ckx_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME image_ckx_test COMMAND image_ckx_test)
set_tests_properties(image_ckx_test PROPERTIES LABELS "m7-image")
