add_executable(supervisor_lifecycle_test supervisor_lifecycle_test.cpp)
target_link_libraries(supervisor_lifecycle_test PRIVATE
    emnl::supervisor
    emnl::echo_generated
    emnl_warnings
)
target_compile_options(supervisor_lifecycle_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(
    NAME supervisor_lifecycle_test
    COMMAND supervisor_lifecycle_test $<TARGET_FILE:system_echo>
)

add_executable(supervisor_crash_loop_test supervisor_crash_loop_test.cpp)
target_link_libraries(supervisor_crash_loop_test PRIVATE
    emnl::supervisor
    emnl::echo_generated
    emnl_warnings
)
target_compile_options(supervisor_crash_loop_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(
    NAME supervisor_crash_loop_test
    COMMAND supervisor_crash_loop_test $<TARGET_FILE:system_echo>
)

add_executable(never_ready_service never_ready_service.cpp)
target_link_libraries(never_ready_service PRIVATE emnl_warnings)
target_compile_options(never_ready_service PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(supervisor_readiness_test supervisor_readiness_test.cpp)
target_link_libraries(supervisor_readiness_test PRIVATE
    emnl::supervisor
    emnl::echo_generated
    emnl_warnings
)
target_compile_options(supervisor_readiness_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(
    NAME supervisor_readiness_test
    COMMAND supervisor_readiness_test $<TARGET_FILE:never_ready_service>
)

add_executable(supervisor_identity_test supervisor_identity_test.cpp)
target_link_libraries(supervisor_identity_test PRIVATE
    emnl::supervisor
    emnl::echo_generated
    emnl_warnings
)
target_compile_options(supervisor_identity_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(
    NAME supervisor_identity_test
    COMMAND supervisor_identity_test $<TARGET_FILE:system_echo>
)

add_executable(evil_echo_service evil_echo_service.cpp)
target_link_libraries(evil_echo_service PRIVATE emnl::osservice emnl_warnings)
target_compile_options(evil_echo_service PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(supervisor_sandbox_test supervisor_sandbox_test.cpp)
target_link_libraries(supervisor_sandbox_test PRIVATE emnl::supervisor emnl_warnings)
target_compile_options(supervisor_sandbox_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(
    NAME supervisor_sandbox_test
    COMMAND supervisor_sandbox_test $<TARGET_FILE:evil_echo_service>
)

add_executable(sandbox_landlock_test sandbox_landlock_test.cpp)
target_link_libraries(sandbox_landlock_test PRIVATE emnl::ossandbox emnl_warnings)
target_compile_options(sandbox_landlock_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME sandbox_landlock_test COMMAND sandbox_landlock_test)
set_tests_properties(sandbox_landlock_test PROPERTIES SKIP_RETURN_CODE 77)

add_executable(resource_probe_service resource_probe_service.cpp)
target_link_libraries(resource_probe_service PRIVATE emnl::osservice emnl_warnings)
target_compile_options(resource_probe_service PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(supervisor_resource_limits_test supervisor_resource_limits_test.cpp)
target_link_libraries(supervisor_resource_limits_test PRIVATE emnl::supervisor emnl_warnings)
target_compile_options(supervisor_resource_limits_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(
    NAME supervisor_resource_limits_test
    COMMAND supervisor_resource_limits_test $<TARGET_FILE:resource_probe_service>
)

add_executable(supervisor_restart_revocation_test supervisor_restart_revocation_test.cpp)
target_link_libraries(supervisor_restart_revocation_test PRIVATE
    emnl::supervisor
    emnl::echo_generated
    emnl_warnings
)
target_compile_options(supervisor_restart_revocation_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(
    NAME supervisor_restart_revocation_test
    COMMAND supervisor_restart_revocation_test $<TARGET_FILE:system_echo>
)

# M2.9 product gate: two service supervisors share one boot-scoped identity
# authority. This intentionally is not labeled m0 because the frozen M0 signal
# remains the original single-service lifecycle substrate.
add_executable(supervisor_shared_identity_test supervisor_shared_identity_test.cpp)
target_link_libraries(supervisor_shared_identity_test PRIVATE
    emnl::supervisor
    emnl::echo_generated
    emnl_warnings
)
target_compile_options(supervisor_shared_identity_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_dependencies(supervisor_shared_identity_test system_echo)
add_test(
    NAME supervisor_shared_identity_test
    COMMAND supervisor_shared_identity_test $<TARGET_FILE:system_echo>
)
set_tests_properties(supervisor_shared_identity_test PROPERTIES LABELS m2-broker)

# Two tiny test-only system services with distinct ServiceIds let the broker
# prove cross-service identity publication without pulling Storage/Key policy
# into the broker's core identity transaction tests.
add_executable(identity_probe_service_a identity_probe_service.cpp)
target_compile_definitions(identity_probe_service_a PRIVATE PROBE_SERVICE_ID=0x0000F091U)
target_link_libraries(identity_probe_service_a PRIVATE emnl::osservice emnl_warnings)
target_compile_options(identity_probe_service_a PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(identity_probe_service_b identity_probe_service.cpp)
target_compile_definitions(identity_probe_service_b PRIVATE PROBE_SERVICE_ID=0x0000F092U)
target_link_libraries(identity_probe_service_b PRIVATE emnl::osservice emnl_warnings)
target_compile_options(identity_probe_service_b PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(service_broker_test service_broker_test.cpp)
target_link_libraries(service_broker_test PRIVATE emnl::supervisor emnl_warnings)
target_compile_options(service_broker_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_dependencies(service_broker_test identity_probe_service_a identity_probe_service_b)
add_test(
    NAME service_broker_test
    COMMAND service_broker_test
        $<TARGET_FILE:identity_probe_service_a>
        $<TARGET_FILE:identity_probe_service_b>
)
set_tests_properties(service_broker_test PROPERTIES LABELS m2-broker)

set_property(TEST
    supervisor_lifecycle_test
    supervisor_crash_loop_test
    supervisor_readiness_test
    supervisor_identity_test
    supervisor_sandbox_test
    sandbox_landlock_test
    supervisor_resource_limits_test
    supervisor_restart_revocation_test
    APPEND PROPERTY LABELS m0
)
