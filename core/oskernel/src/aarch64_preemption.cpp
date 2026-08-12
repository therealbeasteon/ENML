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

    // Stage every failure-prone authority before mutating live execution state.
    // A scheduler choice is not enough: the selected thread must still own a
    // live process translation, both outgoing/incoming EL0 frames must be valid,
    // and the next tickless deadline must be representable.
    auto translation = translations.resolve(decision.thread, epochs);
    if (!translation) {
        return preemption_error(preemption_errors::translation_unavailable);
    }

    const ThreadId previous = running_;
    if (capture_current) {
        if (previous == invalid_thread) {
            return preemption_error(preemption_errors::running_thread_missing);
        }
        auto capture_ok = frames_.validate_capture(previous, live);
        if (!capture_ok) return capture_ok.error();
    }

    auto restore_ok = frames_.validate_restore(decision.thread);
    if (!restore_ok) return restore_ok.error();

    auto prepared_deadline = deadlines_.prepare(decision, now_nanoseconds);
    if (!prepared_deadline) return prepared_deadline.error();

    // Commit authority first, after all preflight checks. From here the frame
    // operations are guaranteed by the preflight in the current single-CPU
    // regime; SMP will place this transaction under execution-state ownership.
    auto committed_deadline = deadlines_.commit(prepared_deadline.value());
    if (!committed_deadline) return committed_deadline.error();

    if (capture_current) {
        auto captured = frames_.capture(previous, live);
        if (!captured) return captured.error();
    }

    const bool switched = previous != decision.thread;
    if (switched || previous == invalid_thread) {
        auto restored = frames_.restore(decision.thread, live);
        if (!restored) return restored.error();
    }

    running_ = decision.thread;

    return PreemptionResult{
        .previous = previous,
        .next = decision.thread,
        .deadline = committed_deadline.value(),
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
