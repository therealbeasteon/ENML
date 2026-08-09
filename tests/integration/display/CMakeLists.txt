add_executable(compositor_service_integration_test compositor_service_integration_test.cpp)
target_link_libraries(compositor_service_integration_test PRIVATE
    emnl::compositor_service
    emnl::supervisor
    emnl_warnings
)
target_compile_options(compositor_service_integration_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_dependencies(compositor_service_integration_test system.compositor)
add_test(
    NAME compositor_service_integration_test
    COMMAND compositor_service_integration_test $<TARGET_FILE:system.compositor>
)
set_tests_properties(compositor_service_integration_test PROPERTIES LABELS m3-display)
