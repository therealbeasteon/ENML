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
