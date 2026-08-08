add_executable(m1_test_app_v1 test_app.cpp)
target_link_libraries(m1_test_app_v1 PRIVATE emnl::osapp emnl_warnings)
target_compile_definitions(m1_test_app_v1 PRIVATE EXPECTED_GENERATION=1)
target_compile_options(m1_test_app_v1 PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(m1_test_app_v2 test_app.cpp)
target_link_libraries(m1_test_app_v2 PRIVATE emnl::osapp emnl_warnings)
target_compile_definitions(m1_test_app_v2 PRIVATE EXPECTED_GENERATION=2)
target_compile_options(m1_test_app_v2 PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(m1_test_app_v3 test_app.cpp)
target_link_libraries(m1_test_app_v3 PRIVATE emnl::osapp emnl_warnings)
target_compile_definitions(m1_test_app_v3 PRIVATE EXPECTED_GENERATION=3)
target_compile_options(m1_test_app_v3 PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(app_manager_launch_test app_manager_launch_test.cpp)
target_link_libraries(app_manager_launch_test PRIVATE emnl::app_manager emnl_warnings)
target_compile_options(app_manager_launch_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

# The name deliberately retains the supervisor_ prefix so the existing QEMU
# user-mode cross gate excludes this fork/exec integration test. It runs in
# native x86-64 and native AArch64 CI, where child execution is meaningful.
add_test(
    NAME supervisor_app_manager_launch_test
    COMMAND app_manager_launch_test
        $<TARGET_FILE:system_echo>
        $<TARGET_FILE:m1_test_app_v1>
        $<TARGET_FILE:m1_test_app_v2>
        $<TARGET_FILE:m1_test_app_v3>
)

add_executable(application_sandbox_landlock_test application_sandbox_landlock_test.cpp)
target_link_libraries(application_sandbox_landlock_test PRIVATE emnl::ossandbox emnl_warnings)
target_compile_options(application_sandbox_landlock_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME application_sandbox_landlock_test COMMAND application_sandbox_landlock_test)
set_tests_properties(application_sandbox_landlock_test PROPERTIES SKIP_RETURN_CODE 77)
