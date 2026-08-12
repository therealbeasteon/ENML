#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/aarch64_preemption.hpp>

namespace os::kernel::aarch64 {

namespace execution_universe_errors {
inline constexpr std::uint32_t invalid_plan = 130U;
inline constexpr std::uint32_t stale_deadline = 131U;
inline constexpr std::uint32_t machine_commit_failed = 132U;
} // namespace execution_universe_errors

// One already-authorized CPU execution universe. This object is deliberately
// derived from PreemptionResult rather than constructed from independent raw
// ThreadId/root/ASID values, so the scheduler-to-translation proof stays intact
// through the final machine commit.
struct ExecutionUniversePlan final {
    ThreadId thread {invalid_thread};
    AddressSpaceEpoch epoch {};
    std::uint64_t root_physical {0ULL};
    SchedulerDeadline deadline {};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return thread != invalid_thread && epoch.valid() && root_physical != 0ULL &&
               deadline.generation != 0ULL;
    }
};

[[nodiscard]] os::core::Result<ExecutionUniversePlan> prepare_execution_universe(
    const PreemptionResult& result) noexcept;

// Native AArch64 commit point. Programs the next tickless deadline (or cancels
// it) and installs the already-authorized TTBR0 root+ASID. Callers must update
// the live exception frame through PreemptionCoordinator before invoking this;
// a failure here is a machine/invariant failure and must not be treated as a
// recoverable scheduling choice.
[[nodiscard]] os::core::Result<void> commit_execution_universe(
    const ExecutionUniversePlan& plan,
    std::uint64_t now_nanoseconds) noexcept;

} // namespace os::kernel::aarch64
