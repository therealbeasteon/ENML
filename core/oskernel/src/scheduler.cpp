#include <os/kernel/scheduler.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error scheduler_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

} // namespace

Scheduler::Slot* Scheduler::find(ThreadId thread) noexcept {
    if (thread == invalid_thread) return nullptr;
    for (auto& slot : slots_) {
        if (slot.occupied && slot.thread == thread) return &slot;
    }
    return nullptr;
}

const Scheduler::Slot* Scheduler::find(ThreadId thread) const noexcept {
    if (thread == invalid_thread) return nullptr;
    for (const auto& slot : slots_) {
        if (slot.occupied && slot.thread == thread) return &slot;
    }
    return nullptr;
}

std::size_t Scheduler::admitted_thread_count() const noexcept {
    return occupied_;
}

std::size_t Scheduler::runnable_thread_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& slot : slots_) {
        if (slot.occupied && slot.runnable) ++count;
    }
    return count;
}

ThreadId Scheduler::running() const noexcept {
    return running_;
}

os::core::Result<std::uint64_t> Scheduler::remaining_slice_of(ThreadId thread) const noexcept {
    const Slot* slot = find(thread);
    if (slot == nullptr) {
        return os::core::Result<std::uint64_t>{
            scheduler_error(scheduler_errors::unknown_thread)};
    }
    return os::core::Result<std::uint64_t>{slot->remaining};
}

os::core::Result<bool> Scheduler::is_runnable(ThreadId thread) const noexcept {
    const Slot* slot = find(thread);
    if (slot == nullptr) {
        return os::core::Result<bool>{scheduler_error(scheduler_errors::unknown_thread)};
    }
    return os::core::Result<bool>{slot->runnable};
}

os::core::Result<void> Scheduler::admit(ThreadId thread, Priority effective) noexcept {
    if (thread == invalid_thread) {
        return scheduler_error(scheduler_errors::invalid_thread_id);
    }
    if (find(thread) != nullptr) {
        return scheduler_error(scheduler_errors::thread_exists);
    }

    for (auto& slot : slots_) {
        if (slot.occupied) continue;
        // Admission order is the round-robin order. A new thread taking its turn
        // behind those already waiting is easier to reason about than one that
        // arrives at the head, and it cannot happen twice either way.
        const std::uint64_t sequence = next_sequence_;
        ++next_sequence_;
        slot = Slot{thread, effective, true, default_slice_nanoseconds, sequence, true};
        ++occupied_;
        return {};
    }
    return scheduler_error(scheduler_errors::thread_limit);
}

os::core::Result<void> Scheduler::retire(ThreadId thread) noexcept {
    if (thread == invalid_thread) {
        return scheduler_error(scheduler_errors::invalid_thread_id);
    }
    Slot* slot = find(thread);
    if (slot == nullptr) {
        return scheduler_error(scheduler_errors::unknown_thread);
    }

    *slot = Slot{};
    --occupied_;
    // A thread that is gone is not charged for the time it was running when it
    // went. There is nothing left to charge, and the alternative - keeping the
    // slot alive to bill it - would mean a dead thread influencing who runs next.
    if (running_ == thread) {
        running_ = invalid_thread;
    }
    return {};
}

os::core::Result<void> Scheduler::update(
    ThreadId thread,
    bool runnable,
    Priority effective) noexcept {
    if (thread == invalid_thread) {
        return scheduler_error(scheduler_errors::invalid_thread_id);
    }
    Slot* slot = find(thread);
    if (slot == nullptr) {
        return scheduler_error(scheduler_errors::unknown_thread);
    }

    // Neither the remaining slice nor the round-robin position moves here.
    //
    // That is the whole anti-gaming rule in one line: blocking, yielding and
    // being woken change whether a thread is eligible, never what it has already
    // spent. A scheduler that restored standing on any of these paths is one a
    // thread can monopolise a processor with by relinquishing just before its
    // slice runs out - the attack the references describe, reachable here from
    // unprivileged code.
    slot->runnable = runnable;
    slot->effective = effective;
    return {};
}

os::core::Result<void> Scheduler::yield_slice(ThreadId thread) noexcept {
    if (thread == invalid_thread) {
        return scheduler_error(scheduler_errors::invalid_thread_id);
    }
    Slot* slot = find(thread);
    if (slot == nullptr) {
        return scheduler_error(scheduler_errors::unknown_thread);
    }

    // Spending the remainder rather than saving it. The next decision sees an
    // exhausted slice, refills it, and puts the thread behind its peers - the
    // same path a thread that used its whole turn takes, which is the point.
    slot->remaining = 0U;
    return {};
}

Decision Scheduler::choose(std::uint64_t now_nanoseconds) noexcept {
    // Charge whoever was running for the time since the last decision.
    if (have_decided_ && running_ != invalid_thread) {
        Slot* current = find(running_);
        if (current != nullptr) {
            // Monotonic time never goes backwards, and if it somehow does this
            // charges nothing rather than wrapping. An unsigned subtraction the
            // wrong way round would bill roughly six hundred years and exhaust
            // the slice instantly, turning a clock glitch into a scheduling
            // fault.
            const std::uint64_t elapsed =
                (now_nanoseconds >= last_decision_) ? (now_nanoseconds - last_decision_) : 0U;
            current->remaining = (elapsed >= current->remaining)
                ? 0U
                : (current->remaining - elapsed);
        }
    }

    // Replenish anything spent, and send it to the back of its priority.
    //
    // Lazily, here, from time that has already passed - never from a timer of
    // its own. A refill with its own interrupt is a tick wearing a different
    // name, and a tick would spend the whole measured idle-wakeup budget before
    // the scheduler did any work.
    for (auto& slot : slots_) {
        if (!slot.occupied || slot.remaining != 0U) continue;
        slot.remaining = default_slice_nanoseconds;
        slot.sequence = next_sequence_;
        ++next_sequence_;
    }

    // Strict priority, then round-robin within it.
    const Slot* chosen = nullptr;
    for (const auto& slot : slots_) {
        if (!slot.occupied || !slot.runnable) continue;
        if (chosen == nullptr) {
            chosen = &slot;
            continue;
        }
        if (slot.effective > chosen->effective) {
            chosen = &slot;
        } else if (slot.effective == chosen->effective && slot.sequence < chosen->sequence) {
            chosen = &slot;
        }
    }

    const ThreadId previous = running_;
    const ThreadId next = (chosen == nullptr) ? invalid_thread : chosen->thread;

    // Preempted means lost the processor while still able to use it. A thread
    // that blocked gave it up, and a thread that exited no longer exists to have
    // been wronged; conflating either with preemption would make the count
    // useless for the only thing it is for, which is noticing that something is
    // being pushed off the processor.
    bool preempted = false;
    if (previous != invalid_thread && previous != next) {
        const Slot* before = find(previous);
        preempted = before != nullptr && before->runnable;
    }

    std::uint64_t timer = 0U;
    if (chosen != nullptr) {
        // A timer is only worth setting if something is actually waiting for
        // this priority. A lower-priority thread is not: strict priority with no
        // aging means it does not run while this one can, so preempting for it
        // would be an interrupt taken to make no decision.
        bool contended = false;
        for (const auto& slot : slots_) {
            if (!slot.occupied || !slot.runnable) continue;
            if (slot.thread == chosen->thread) continue;
            if (slot.effective == chosen->effective) {
                contended = true;
                break;
            }
        }
        if (contended) timer = chosen->remaining;
    }

    running_ = next;
    last_decision_ = now_nanoseconds;
    have_decided_ = true;
    return Decision{next, timer, preempted};
}

} // namespace os::kernel
