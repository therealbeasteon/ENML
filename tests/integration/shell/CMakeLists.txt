add_executable(shell_service_supervisor_test
    shell_service_supervisor_test.cpp
)
target_link_libraries(shell_service_supervisor_test PRIVATE
    emnl::app_manager
    emnl::supervisor
    osshell
    emnl_warnings
)
target_compile_features(shell_service_supervisor_test PRIVATE cxx_std_20)
target_compile_options(shell_service_supervisor_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_dependencies(shell_service_supervisor_test system.shell)
add_test(
    NAME shell_service_supervisor_test
    COMMAND shell_service_supervisor_test $<TARGET_FILE:system.shell>
)
set_tests_properties(shell_service_supervisor_test PROPERTIES LABELS "m4-shell")
