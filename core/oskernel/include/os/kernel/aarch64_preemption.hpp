#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/aarch64_user_frames.hpp>
#include <os/kernel/scheduler.hpp>
#include <os/kernel/scheduler_deadline.hpp>

namespace os::kernel::aarch64 {

namespace preemption_errors {
inline constexpr std::uint32_t no_runnable_thread = 120U;
inline constexpr std::uint32_t running_thread_missing = 121U;
inline constexpr std::uint32_t wrong_running_thread = 122U;
} // namespace preemption_errors

struct PreemptionResult final {
    ThreadId previous {invalid_thread};
    ThreadId next {invalid_thread};
    SchedulerDeadline deadline {};
    bool switched {false};
    bool preempted {false};
};

// Architecture-local execution state driven by the portable scheduler. It does
// not acknowledge GIC interrupts or program timers; those remain machine-layer
// operations. This object only turns a scheduler decision into a safe EL0 frame
// transition and a generation-bound next deadline.
class PreemptionCoordinator final {
public:
    [[nodiscard]] os::core::Result<void> admit_frame(
        ThreadId thread,
        const ExceptionFrame& initial) noexcept {
        return frames_.admit(thread, initial);
    }

    [[nodiscard]] os::core::Result<PreemptionResult> start(
        Scheduler& scheduler,
        std::uint64_t now_nanoseconds,
        ExceptionFrame& live) noexcept;

    [[nodiscard]] os::core::Result<PreemptionResult> on_timer(
        Scheduler& scheduler,
        const SchedulerDeadline& delivered,
        std::uint64_t now_nanoseconds,
        ExceptionFrame& live) noexcept;

    [[nodiscard]] ThreadId running() const noexcept { return running_; }
    [[nodiscard]] SchedulerDeadline current_deadline() const noexcept {
        return deadlines_.current();
    }

private:
    [[nodiscard]] os::core::Result<PreemptionResult> apply_decision(
        Scheduler& scheduler,
        const Decision& decision,
        std::uint64_t now_nanoseconds,
        ExceptionFrame& live,
        bool capture_current) noexcept;

    UserFrameTable frames_ {};
    SchedulerDeadlineAuthority deadlines_ {};
    ThreadId running_ {invalid_thread};
};

} // namespace os::kernel::aarch64
