#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/aarch64_user_frames.hpp>
#include <os/kernel/process_translation.hpp>
#include <os/kernel/scheduler.hpp>
#include <os/kernel/scheduler_deadline.hpp>

namespace os::kernel::aarch64 {

namespace preemption_errors {
inline constexpr std::uint32_t no_runnable_thread = 120U;
inline constexpr std::uint32_t running_thread_missing = 121U;
inline constexpr std::uint32_t wrong_running_thread = 122U;
inline constexpr std::uint32_t translation_unavailable = 123U;
} // namespace preemption_errors

struct PreemptionResult final {
    ThreadId previous {invalid_thread};
    ThreadId next {invalid_thread};
    SchedulerDeadline deadline {};
    ProcessTranslationBinding translation {};
    bool switched {false};
    bool preempted {false};
};

// Architecture-local execution state driven by the portable scheduler.
//
// Cookie's switch is intentionally two-authority: a scheduler decision is not
// enough to mutate the live EL0 frame. The selected thread must first resolve to
// a still-active process translation epoch/root. Only after that validation do
// we capture/restore register state. The machine layer can then install the
// returned translation binding before ERET. This prevents a stale scheduler
// entry from partially switching execution into a dead memory universe.
class PreemptionCoordinator final {
public:
    [[nodiscard]] os::core::Result<void> admit_frame(
        ThreadId thread,
        const ExceptionFrame& initial) noexcept {
        return frames_.admit(thread, initial);
    }

    [[nodiscard]] os::core::Result<PreemptionResult> start(
        Scheduler& scheduler,
        const ProcessTranslationTable& translations,
        const AddressSpaceEpochAuthority& epochs,
        std::uint64_t now_nanoseconds,
        ExceptionFrame& live) noexcept;

    [[nodiscard]] os::core::Result<PreemptionResult> on_timer(
        Scheduler& scheduler,
        const ProcessTranslationTable& translations,
        const AddressSpaceEpochAuthority& epochs,
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
        const ProcessTranslationTable& translations,
        const AddressSpaceEpochAuthority& epochs,
        const Decision& decision,
        std::uint64_t now_nanoseconds,
        ExceptionFrame& live,
        bool capture_current) noexcept;

    UserFrameTable frames_ {};
    SchedulerDeadlineAuthority deadlines_ {};
    ThreadId running_ {invalid_thread};
};

} // namespace os::kernel::aarch64
