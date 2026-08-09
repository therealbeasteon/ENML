add_executable(display_compositor_test compositor_test.cpp)
target_link_libraries(display_compositor_test PRIVATE emnl::osdisplay emnl_warnings)
target_compile_options(display_compositor_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME display_compositor_test COMMAND display_compositor_test)
set_tests_properties(display_compositor_test PROPERTIES LABELS m3-display)
