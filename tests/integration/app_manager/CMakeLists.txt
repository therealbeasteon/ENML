add_executable(m1_test_app_v1 test_app.cpp)
target_link_libraries(m1_test_app_v1 PRIVATE emnl::osapp emnl::storage_service emnl_warnings)
target_compile_definitions(m1_test_app_v1 PRIVATE EXPECTED_GENERATION=1)
target_compile_options(m1_test_app_v1 PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(m1_test_app_v2 test_app.cpp)
target_link_libraries(m1_test_app_v2 PRIVATE emnl::osapp emnl::storage_service emnl_warnings)
target_compile_definitions(m1_test_app_v2 PRIVATE EXPECTED_GENERATION=2)
target_compile_options(m1_test_app_v2 PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(m1_test_app_v3 test_app.cpp)
target_link_libraries(m1_test_app_v3 PRIVATE emnl::osapp emnl::storage_service emnl_warnings)
target_compile_definitions(m1_test_app_v3 PRIVATE EXPECTED_GENERATION=3)
target_compile_options(m1_test_app_v3 PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(app_manager_launch_test app_manager_launch_test.cpp)
target_link_libraries(app_manager_launch_test PRIVATE emnl::app_manager emnl_warnings)
target_compile_options(app_manager_launch_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_dependencies(app_manager_launch_test system.storage)

# The name deliberately retains the supervisor_ prefix so the existing QEMU
# user-mode cross gate excludes this fork/exec integration test. It runs in
# native x86-64 and native AArch64 CI, where child execution is meaningful.
add_test(
    NAME supervisor_app_manager_launch_test
    COMMAND app_manager_launch_test
        $<TARGET_FILE:system.storage>
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

# M3.2 private trusted-accessibility claim control. The forked client is
# identified from packet SCM_CREDENTIALS through a trusted resolver; caller
# authority is never accepted from payload fields.
add_executable(accessibility_control_test accessibility_control_test.cpp)
target_link_libraries(accessibility_control_test PRIVATE
    emnl::app_manager
    emnl_warnings
)
target_compile_options(accessibility_control_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME accessibility_control_test COMMAND accessibility_control_test)
set_tests_properties(accessibility_control_test PROPERTIES LABELS m3-ui)

# The M2.9 fixture consumes the versioned handle-based bootstrap. It receives
# Storage + Keys by typed ServiceId and never relies on a fixed service fd.
add_executable(m2_broker_test_app broker_test_app.cpp)
target_link_libraries(m2_broker_test_app PRIVATE
    emnl::osapp
    emnl::storage_service
    emnl::key_service
    emnl_warnings
)
target_compile_options(m2_broker_test_app PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

# M2.10 keeps bootstrap fd 3 as the private runtime service session after READY.
# This fixture observes current service generations, proves unauthorized
# ServiceIds are denied, then reacquires Storage and Keys after real Supervisor
# restarts while the original object capabilities remain stale.
add_executable(m2_runtime_session_test_app runtime_session_test_app.cpp)
target_link_libraries(m2_runtime_session_test_app PRIVATE
    emnl::osapp
    emnl::storage_service
    emnl::key_service
    emnl_warnings
)
target_compile_options(m2_runtime_session_test_app PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

if(EMNL_BUILD_OPENSSL_TEST_PROVIDER)
    add_executable(app_manager_multi_service_broker_test
        app_manager_multi_service_broker_test.cpp)
    target_link_libraries(app_manager_multi_service_broker_test PRIVATE
        emnl::app_manager
        emnl::key_service
        emnl_warnings
    )
    target_compile_options(app_manager_multi_service_broker_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_dependencies(app_manager_multi_service_broker_test
        system.storage
        system_keys_host_ci
        m2_broker_test_app
    )
    add_test(
        NAME app_manager_multi_service_broker_test
        COMMAND app_manager_multi_service_broker_test
            $<TARGET_FILE:system.storage>
            $<TARGET_FILE:system_keys_host_ci>
            $<TARGET_FILE:m2_broker_test_app>
    )
    set_tests_properties(app_manager_multi_service_broker_test PROPERTIES LABELS m2-broker)

    add_executable(app_manager_runtime_service_session_test
        app_manager_runtime_service_session_test.cpp)
    target_link_libraries(app_manager_runtime_service_session_test PRIVATE
        emnl::app_manager
        emnl::key_service
        emnl_warnings
    )
    target_compile_options(app_manager_runtime_service_session_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_dependencies(app_manager_runtime_service_session_test
        system.storage
        system_keys_host_ci
        m2_runtime_session_test_app
    )
    add_test(
        NAME app_manager_runtime_service_session_test
        COMMAND app_manager_runtime_service_session_test
            $<TARGET_FILE:system.storage>
            $<TARGET_FILE:system_keys_host_ci>
            $<TARGET_FILE:m2_runtime_session_test_app>
    )
    set_tests_properties(app_manager_runtime_service_session_test PROPERTIES LABELS m2-runtime)
endif()
