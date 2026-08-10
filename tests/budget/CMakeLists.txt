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

# One generic harness drives every budget. Adding a service is a data change in
# budget.hpp plus a registration below, never a change to the measurement code.
add_executable(service_budget_test service_budget_test.cpp)
target_link_libraries(service_budget_test PRIVATE
    budget_support
    emnl::supervisor
    emnl_warnings
)
target_compile_options(service_budget_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

function(add_emnl_budget_test budget_name service_target)
    add_dependencies(service_budget_test ${service_target})
    add_test(
        NAME service_budget_${budget_name}
        COMMAND service_budget_test $<TARGET_FILE:${service_target}> ${budget_name}
    )
    set_tests_properties(service_budget_${budget_name} PROPERTIES
        LABELS "budget"
        SKIP_RETURN_CODE 77
    )
endfunction()

add_emnl_budget_test(system.echo system_echo)
add_emnl_budget_test(system.storage system.storage)

# system.keys only exists when the host/CI provider is built. Its production
# provider is a BSP responsibility, so the budget follows the same gate rather
# than pretending the service is always available.
if(EMNL_BUILD_OPENSSL_TEST_PROVIDER)
    add_emnl_budget_test(system.keys system_keys_host_ci)
endif()
