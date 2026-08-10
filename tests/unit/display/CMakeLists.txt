add_executable(display_compositor_test compositor_test.cpp)
target_link_libraries(display_compositor_test PRIVATE emnl::osdisplay emnl_warnings)
target_compile_options(display_compositor_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME display_compositor_test COMMAND display_compositor_test)
set_tests_properties(display_compositor_test PROPERTIES LABELS m3-display)

add_executable(display_input_hit_test input_hit_test.cpp)
target_link_libraries(display_input_hit_test PRIVATE emnl::osdisplay emnl_warnings)
target_compile_options(display_input_hit_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME display_input_hit_test COMMAND display_input_hit_test)
set_tests_properties(display_input_hit_test PROPERTIES LABELS m3-display)

add_executable(display_input_authority_test input_authority_test.cpp)
target_link_libraries(display_input_authority_test PRIVATE emnl::osdisplay emnl_warnings)
target_compile_options(display_input_authority_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME display_input_authority_test COMMAND display_input_authority_test)
set_tests_properties(display_input_authority_test PROPERTIES LABELS m3-display)

add_executable(display_shared_buffer_test shared_buffer_test.cpp)
target_link_libraries(display_shared_buffer_test PRIVATE emnl::osdisplay emnl_warnings)
target_compile_options(display_shared_buffer_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME display_shared_buffer_test COMMAND display_shared_buffer_test)
set_tests_properties(display_shared_buffer_test PROPERTIES LABELS m3-display)

add_executable(display_generation_namespace_test generation_namespace_test.cpp)
target_link_libraries(display_generation_namespace_test PRIVATE emnl::osdisplay emnl_warnings)
target_compile_options(display_generation_namespace_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME display_generation_namespace_test COMMAND display_generation_namespace_test)
set_tests_properties(display_generation_namespace_test PROPERTIES LABELS m3-display)

add_executable(display_trusted_overlay_test trusted_overlay_test.cpp)
target_link_libraries(display_trusted_overlay_test PRIVATE emnl::osdisplay emnl_warnings)
target_compile_options(display_trusted_overlay_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME display_trusted_overlay_test COMMAND display_trusted_overlay_test)
set_tests_properties(display_trusted_overlay_test PROPERTIES LABELS m3-display)

add_executable(display_trusted_mark_test trusted_mark_test.cpp)
target_link_libraries(display_trusted_mark_test PRIVATE emnl::osdisplay emnl_warnings)
target_compile_options(display_trusted_mark_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME display_trusted_mark_test COMMAND display_trusted_mark_test)
set_tests_properties(display_trusted_mark_test PROPERTIES LABELS m3-display)

add_executable(display_exact_activation_test exact_activation_test.cpp)
target_link_libraries(display_exact_activation_test PRIVATE emnl::osdisplay emnl_warnings)
target_compile_options(display_exact_activation_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME display_exact_activation_test COMMAND display_exact_activation_test)
set_tests_properties(display_exact_activation_test PROPERTIES LABELS "m3-display;m4-shell")
