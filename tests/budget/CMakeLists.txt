# Resource budget gates.
#
# These are measurement gates, not correctness tests: they assert that a
# supervised service stays inside a reviewed ceiling for resident memory,
# time-to-ready and idle wakeups. They carry the `budget` label and are
# deliberately not part of the frozen `m0` signal.
#
# The harness reads procfs for the *real* child process, so it is not
# qemu-user-safe: under emulation the observable resident set belongs to the
# emulator rather than to the service. Native runners only.

add_library(budget_support STATIC
    src/measure.cpp
    src/budget.cpp
)
target_include_directories(budget_support PUBLIC include)
target_link_libraries(budget_support PUBLIC emnl::oscore PRIVATE emnl_warnings)
target_compile_options(budget_support PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(service_budget_test service_budget_test.cpp)
target_link_libraries(service_budget_test PRIVATE
    budget_support
    emnl::supervisor
    emnl::echo_generated
    emnl_warnings
)
target_compile_options(service_budget_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_dependencies(service_budget_test system_echo)

add_test(
    NAME service_budget_echo_test
    COMMAND service_budget_test $<TARGET_FILE:system_echo> system.echo
)
set_tests_properties(service_budget_echo_test PROPERTIES
    LABELS "budget"
    SKIP_RETURN_CODE 77
)
