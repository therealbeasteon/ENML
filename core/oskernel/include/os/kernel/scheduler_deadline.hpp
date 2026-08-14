#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/scheduler.hpp>

namespace os::kernel {

namespace scheduler_deadline_errors {
inline constexpr std::uint32_t overflow = 100U;
inline constexpr std::uint32_t stale = 101U;
inline constexpr std::uint32_t early = 102U;
inline constexpr std::uint32_t inactive = 103U;
inline constexpr std::uint32_t stale_prepare = 104U;
} // namespace scheduler_deadline_errors

struct SchedulerDeadline final {
    std::uint64_t generation {0U};
    std::uint64_t absolute_nanoseconds {0U};
    bool active {false};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return generation != 0U && active;
    }
};

// Non-authoritative scheduling proposal. A prepared deadline does not revoke or
// arm anything. It becomes authority only through commit(), allowing Cookie to
// stage CPU-frame, process-translation and timer decisions before one switch is
// made visible.
struct PreparedSchedulerDeadline final {
    SchedulerDeadline deadline {};
    std::uint64_t base_generation {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return base_generation != 0U && deadline.generation != 0U;
    }
};

// Cookie's scheduler is tickless. A hardware timer is therefore not an ambient
// periodic source: it is a revocable authority minted by one scheduling
// decision. Deadline changes are transactional: prepare() is non-mutating and
// commit() performs the generation transition only after the rest of a context
// switch has been validated.
// Narrows a scheduling decision's timer to an earlier non-scheduling wakeup -
// today, the soonest bounded receive (docs/M7_2_SERVER_LOOP.md).
//
// Narrowing only. It can bring a wakeup forward and can never postpone or
// remove one, so an IPC deadline cannot weaken a scheduling guarantee.
//
// This runs *before* the authority prepares, and that ordering is the whole
// point rather than a convenience. SchedulerDeadlineAuthority is the single
// source of truth for what is armed: accept_interrupt() rejects any delivered
// deadline whose absolute time differs from current_, and rejects an interrupt
// arriving before current_ as `early`. Arming the hardware timer earlier than
// the authority believes - by clamping the plan after commit, which is the
// obvious place to put it - therefore produces an interrupt the authority
// refuses, and a receive deadline that silently never fires. Clamping the
// Decision instead keeps one armed time and one owner of it.
//
// `earliest_absolute` is zero when nothing is waiting. An already-passed
// deadline collapses to the smallest expressible wait rather than to zero,
// because zero means "no timer" in a Decision and would cancel the wakeup it
// was asked to bring forward.
[[nodiscard]] constexpr Decision narrow_decision_timer(
    Decision decision,
    std::uint64_t earliest_absolute,
    std::uint64_t now_nanoseconds) noexcept {
    if (earliest_absolute == 0ULL) return decision;
    const std::uint64_t remaining =
        earliest_absolute <= now_nanoseconds ? 1ULL : earliest_absolute - now_nanoseconds;
    if (decision.timer_nanoseconds == 0ULL || remaining < decision.timer_nanoseconds) {
        decision.timer_nanoseconds = remaining;
    }
    return decision;
}

class SchedulerDeadlineAuthority final {
public:
    [[nodiscard]] os::core::Result<PreparedSchedulerDeadline> prepare(
        const Decision& decision,
        std::uint64_t now_nanoseconds) const noexcept;

    [[nodiscard]] os::core::Result<SchedulerDeadline> commit(
        const PreparedSchedulerDeadline& prepared) noexcept;

    // Compatibility convenience for callers that have no wider transaction.
    // Unlike the old implementation, a failed prepare leaves authority intact.
    [[nodiscard]] os::core::Result<SchedulerDeadline> apply(
        const Decision& decision,
        std::uint64_t now_nanoseconds) noexcept;

    [[nodiscard]] os::core::Result<void> accept_interrupt(
        const SchedulerDeadline& delivered,
        std::uint64_t now_nanoseconds) const noexcept;

    [[nodiscard]] SchedulerDeadline current() const noexcept { return current_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

private:
    std::uint64_t generation_ {1U};
    SchedulerDeadline current_ {};
};

} // namespace os::kernel
