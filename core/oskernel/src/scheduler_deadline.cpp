#include <os/kernel/scheduler_deadline.hpp>

#include <limits>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error deadline_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] constexpr std::uint64_t next_generation(std::uint64_t current) noexcept {
    return current == std::numeric_limits<std::uint64_t>::max() ? 1U : current + 1U;
}

} // namespace

os::core::Result<SchedulerDeadline> SchedulerDeadlineAuthority::apply(
    const Decision& decision,
    std::uint64_t now_nanoseconds) noexcept {
    generation_ = next_generation(generation_);

    if (decision.timer_nanoseconds == 0U) {
        current_ = SchedulerDeadline{.generation = generation_, .active = false};
        return current_;
    }
    if (decision.timer_nanoseconds >
        std::numeric_limits<std::uint64_t>::max() - now_nanoseconds) {
        current_ = SchedulerDeadline{.generation = generation_, .active = false};
        return deadline_error(scheduler_deadline_errors::overflow);
    }

    current_ = SchedulerDeadline{
        .generation = generation_,
        .absolute_nanoseconds = now_nanoseconds + decision.timer_nanoseconds,
        .active = true,
    };
    return current_;
}

os::core::Result<void> SchedulerDeadlineAuthority::accept_interrupt(
    const SchedulerDeadline& delivered,
    std::uint64_t now_nanoseconds) const noexcept {
    if (!current_.active) return deadline_error(scheduler_deadline_errors::inactive);
    if (!delivered.active || delivered.generation != current_.generation ||
        delivered.absolute_nanoseconds != current_.absolute_nanoseconds) {
        return deadline_error(scheduler_deadline_errors::stale);
    }
    if (now_nanoseconds < current_.absolute_nanoseconds) {
        return deadline_error(scheduler_deadline_errors::early);
    }
    return {};
}

} // namespace os::kernel
