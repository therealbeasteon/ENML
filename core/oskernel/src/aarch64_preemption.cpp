#include <os/kernel/aarch64_preemption.hpp>

#include <os/core/error.hpp>

namespace os::kernel::aarch64 {
namespace {
[[nodiscard]] constexpr os::core::Error preemption_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}
}

os::core::Result<PreemptionResult> PreemptionCoordinator::apply_decision(
    Scheduler& scheduler,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs,
    const Decision& decision,
    std::uint64_t now_nanoseconds,
    ExceptionFrame& live,
    bool capture_current) noexcept {
    (void)scheduler;
    if (decision.thread == invalid_thread) {
        return preemption_error(preemption_errors::no_runnable_thread);
    }

    // Validate the target memory universe before mutating the live frame or the
    // saved outgoing frame. A stale process identity therefore cannot cause a
    // half-switch where registers belong to one thread and TTBR0 to another.
    auto translation = translations.resolve(decision.thread, epochs);
    if (!translation) {
        return preemption_error(preemption_errors::translation_unavailable);
    }

    const ThreadId previous = running_;
    if (capture_current) {
        if (previous == invalid_thread || !frames_.contains(previous)) {
            return preemption_error(preemption_errors::running_thread_missing);
        }
        auto captured = frames_.capture(previous, live);
        if (!captured) return captured.error();
    }

    if (!frames_.contains(decision.thread)) {
        return preemption_error(preemption_errors::running_thread_missing);
    }

    const bool switched = previous != decision.thread;
    if (switched || previous == invalid_thread) {
        auto restored = frames_.restore(decision.thread, live);
        if (!restored) return restored.error();
    }

    auto deadline = deadlines_.apply(decision, now_nanoseconds);
    if (!deadline) return deadline.error();
    running_ = decision.thread;

    return PreemptionResult{
        .previous = previous,
        .next = decision.thread,
        .deadline = deadline.value(),
        .translation = translation.value(),
        .switched = switched,
        .preempted = decision.preempted,
    };
}

os::core::Result<PreemptionResult> PreemptionCoordinator::start(
    Scheduler& scheduler,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs,
    std::uint64_t now_nanoseconds,
    ExceptionFrame& live) noexcept {
    if (running_ != invalid_thread) {
        return preemption_error(preemption_errors::wrong_running_thread);
    }
    const auto decision = scheduler.choose(now_nanoseconds);
    return apply_decision(
        scheduler, translations, epochs, decision, now_nanoseconds, live, false);
}

os::core::Result<PreemptionResult> PreemptionCoordinator::on_timer(
    Scheduler& scheduler,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs,
    const SchedulerDeadline& delivered,
    std::uint64_t now_nanoseconds,
    ExceptionFrame& live) noexcept {
    auto accepted = deadlines_.accept_interrupt(delivered, now_nanoseconds);
    if (!accepted) return accepted.error();
    if (running_ == invalid_thread || scheduler.running() != running_) {
        return preemption_error(preemption_errors::wrong_running_thread);
    }

    const auto decision = scheduler.choose(now_nanoseconds);
    return apply_decision(
        scheduler, translations, epochs, decision, now_nanoseconds, live, true);
}

} // namespace os::kernel::aarch64
