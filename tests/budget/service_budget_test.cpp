#include <cstdio>
#include <cstdint>
#include <string_view>

#include <signal.h>

#include <os/supervisor/supervisor.hpp>

#include <budget/budget.hpp>
#include <budget/measure.hpp>

namespace {

// CTest treats 77 as "skipped". A kernel or emulator that cannot report VmRSS
// must not be counted as a passing budget run.
inline constexpr int skip_exit_code = 77;

// Budget checks deliberately avoid assert(). A gate whose enforcement disappears
// under NDEBUG is not a gate, and this harness must behave identically in every
// build configuration.
[[nodiscard]] bool
check_ceiling(const char* label, std::uint64_t measured, std::uint64_t ceiling) noexcept {
    const bool within = measured <= ceiling;
    std::printf(
        "  %-22s %10llu  (ceiling %llu) %s\n",
        label,
        static_cast<unsigned long long>(measured),
        static_cast<unsigned long long>(ceiling),
        within ? "ok" : "OVER BUDGET");
    return within;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: service_budget_test <executable> <budget-name>\n");
        return 1;
    }

    const char* executable_path = argv[1];
    const std::string_view budget_name{argv[2]};

    if (!budget::measurement_supported()) {
        std::printf("skip: procfs resource fields unavailable on this host\n");
        return skip_exit_code;
    }

    auto budget_result = budget::find_budget(budget_name);
    if (!budget_result) {
        std::fprintf(
            stderr, "no compiled budget named '%s'\n", argv[2]);
        return 1;
    }
    const auto limits = budget_result.value();

    // Restart is disabled for measurement: a service that dies mid-window must
    // surface as a failure, not be silently replaced by a fresh instance whose
    // counters restart at zero.
    const os::supervisor::ServiceDescriptorV1 descriptor{
        .service_id = os::core::ServiceId{limits.service_id},
        .principal_id = os::core::PrincipalId{limits.principal_high, limits.principal_low},
        .user_id = os::core::UserId{0U},
        // argv[2] is the budget name and is already null-terminated; the table's
        // string_view is not, and ServiceDescriptorV1 takes a C string.
        .name = argv[2],
        .restart_policy = os::supervisor::RestartPolicy::never,
        .restart_delay_ms = 10U,
        .max_restarts_in_window = 0U,
        .restart_window_ms = 1000U,
        .readiness_timeout_ms = 1000U,
    };

    os::supervisor::Supervisor supervisor{
        os::supervisor::ServiceLaunchConfig{
            .descriptor = descriptor,
            .executable_path = executable_path,
        }
    };

    auto spawn_at = budget::monotonic_ms();
    if (!spawn_at) {
        std::fprintf(stderr, "monotonic clock unavailable\n");
        return 1;
    }

    auto start_result = supervisor.start();

    auto ready_at = budget::monotonic_ms();
    if (!ready_at) {
        std::fprintf(stderr, "monotonic clock unavailable\n");
        return 1;
    }

    if (!start_result) {
        std::fprintf(stderr, "service failed to start\n");
        return 1;
    }

    const auto status = supervisor.status();
    if (status.state != os::supervisor::ServiceState::running || status.native_pid <= 0) {
        std::fprintf(stderr, "service did not reach running state\n");
        return 1;
    }

    const auto ready_ms = ready_at.value() - spawn_at.value();

    auto first_sample = budget::sample_process(status.native_pid);
    if (!first_sample) {
        std::fprintf(stderr, "could not sample service process\n");
        (void)supervisor.terminate(SIGKILL);
        return 1;
    }

    budget::sleep_ms(limits.idle_window_ms);

    auto second_sample = budget::sample_process(status.native_pid);
    if (!second_sample) {
        std::fprintf(stderr, "could not re-sample service process\n");
        (void)supervisor.terminate(SIGKILL);
        return 1;
    }

    // Confirm the service survived the idle window rather than exiting early and
    // producing a flatteringly quiet measurement.
    auto maintain_result = supervisor.maintain();
    const auto post_status = supervisor.status();
    if (!maintain_result || post_status.state != os::supervisor::ServiceState::running) {
        std::fprintf(stderr, "service did not stay running across the idle window\n");
        (void)supervisor.terminate(SIGKILL);
        return 1;
    }

    const auto before = first_sample.value();
    const auto after = second_sample.value();

    const auto resident_kib =
        after.resident_kib > before.resident_kib ? after.resident_kib : before.resident_kib;
    const auto idle_switches = after.total_switches() - before.total_switches();
    const auto idle_wakeups_per_second =
        limits.idle_window_ms == 0U
            ? idle_switches
            : (idle_switches * 1000ULL) / limits.idle_window_ms;

    // Every ceiling is evaluated and reported before the verdict, so one
    // regression does not hide the numbers for the others.
    std::printf("resource budget: %s\n", argv[2]);
    const bool resident_ok =
        check_ceiling("resident_kib", resident_kib, limits.max_resident_kib);
    const bool ready_ok =
        check_ceiling("ready_ms", ready_ms, limits.max_ready_ms);
    const bool wakeups_ok = check_ceiling(
        "idle_wakeups_per_sec", idle_wakeups_per_second, limits.max_idle_wakeups_per_second);
    const bool within_budget = resident_ok && ready_ok && wakeups_ok;

    (void)supervisor.terminate(SIGKILL);

    if (!within_budget) {
        std::fprintf(
            stderr,
            "\n'%s' exceeded its resource budget.\n"
            "If this regression is intended, change the ceiling in "
            "tests/budget/include/budget/budget.hpp in the same commit so the "
            "cost is reviewed rather than absorbed.\n",
            argv[2]);
        return 1;
    }

    return 0;
}
