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
} // namespace scheduler_deadline_errors

struct SchedulerDeadline final {
    std::uint64_t generation {0U};
    std::uint64_t absolute_nanoseconds {0U};
    bool active {false};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return generation != 0U && active;
    }
};

// Cookie's scheduler is tickless. A hardware timer is therefore not an ambient
// periodic source: it is a revocable authority minted by one scheduling
// decision. Replacing/cancelling the decision advances the generation so stale
// delivery can be recognized rather than perturbing newer scheduler state.
class SchedulerDeadlineAuthority final {
public:
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
