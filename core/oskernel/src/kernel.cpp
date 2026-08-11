#include <os/kernel/kernel.hpp>

#include <os/core/error.hpp>

namespace os::kernel {

bool Kernel::tracks(ThreadId thread) const noexcept {
    for (std::size_t i = 0U; i < live_count_; ++i) {
        if (live_[i] == thread) return true;
    }
    return false;
}

void Kernel::untrack(ThreadId thread) noexcept {
    for (std::size_t i = 0U; i < live_count_; ++i) {
        if (live_[i] != thread) continue;
        live_[i] = live_[live_count_ - 1U];
        live_[live_count_ - 1U] = invalid_thread;
        --live_count_;
        return;
    }
}

std::size_t Kernel::live_thread_count() const noexcept {
    return live_count_;
}

void Kernel::synchronise() noexcept {
    // Recomputed, never patched. See the header: a cache of runnability and
    // priority that is adjusted along each path is a cache that is wrong the
    // first time a path is missed, and both directions of wrong are serious.
    for (std::size_t i = 0U; i < live_count_; ++i) {
        const ThreadId thread = live_[i];

        auto state = threads_.state_of(thread);
        auto priority = threads_.effective_priority_of(thread);
        if (!state || !priority) continue;

        // Only `ready` is runnable. Every blocked state is waiting on something
        // the scheduler cannot resolve, and `exited` is not a thread.
        const bool runnable = state.value() == ThreadState::ready;
        (void)scheduler_.update(thread, runnable, priority.value());
    }
}

os::core::Result<void> Kernel::create_thread(ThreadId thread, Priority priority) noexcept {
    auto created = threads_.create_thread(thread, priority);
    if (!created) return created;

    auto admitted = scheduler_.admit(thread, priority);
    if (!admitted) {
        // Rolled back rather than left half-made. A thread the rendezvous knows
        // about and the scheduler does not is a thread that can be sent to and
        // will never run, which is a deadlock with no visible cause.
        (void)threads_.exit_thread(thread);
        return os::core::Result<void>{
            os::core::make_error(os::core::ErrorDomain::kernel,
                                 kernel_errors::creation_incomplete)};
    }

    live_[live_count_] = thread;
    ++live_count_;
    synchronise();
    return {};
}

os::core::Result<Teardown> Kernel::destroy_thread(ThreadId thread) noexcept {
    // The rendezvous first, because it is the one that can refuse: it owns the
    // question of whether this thread exists at all. Once it has accepted, the
    // remaining three cannot fail and must not be skipped.
    auto released = threads_.exit_thread(thread);
    if (!released) {
        return os::core::Result<Teardown>{released.error()};
    }

    Teardown teardown{};
    teardown.threads_released = released.value();
    // Everything it held, and everything derived from what it held. A capability
    // that outlives its holder is authority nobody is accountable for.
    teardown.capabilities_revoked = capabilities_.revoke_all_held_by(thread);
    // Left masked, per M7.1d: a masked orphan source is a dead device, an
    // unmasked one is a livelock with nobody positioned to stop it.
    teardown.interrupt_sources_released = interrupts_.detach_all_owned_by(thread);
    (void)scheduler_.retire(thread);

    untrack(thread);
    // The threads this one released are now ready, and the scheduler has to
    // learn that before the next decision or they wait forever on a thread that
    // no longer exists.
    synchronise();
    return os::core::Result<Teardown>{teardown};
}

os::core::Result<void> Kernel::send(ThreadId from, ThreadId to) noexcept {
    auto sent = threads_.send(from, to);
    if (!sent) return sent;
    // A send can block the sender, unblock a waiting receiver, and raise the
    // receiver's effective priority to the sender's - three facts the scheduler
    // needs and none of which it can derive.
    synchronise();
    return {};
}

os::core::Result<ThreadId> Kernel::receive(ThreadId self) noexcept {
    auto received = threads_.receive(self);
    if (!received) return received;
    synchronise();
    return received;
}

os::core::Result<void> Kernel::reply(ThreadId self, ThreadId caller) noexcept {
    auto replied = threads_.reply(self, caller);
    if (!replied) return replied;
    // The caller is runnable again, and the server stops inheriting its
    // priority. Both change who should be on the processor.
    synchronise();
    return {};
}

os::core::Result<void> Kernel::yield(ThreadId self) noexcept {
    if (!tracks(self)) {
        return os::core::make_error(os::core::ErrorDomain::kernel,
                                    rendezvous_errors::unknown_thread);
    }
    return scheduler_.yield_slice(self);
}

os::core::Result<Dispatch> Kernel::dispatch_interrupt(InterruptSource source) noexcept {
    auto taken = interrupts_.dispatch(source);
    if (!taken) return taken;

    // Waking the owner is the composition. The interrupt table knows a driver
    // should run and the scheduler decides who does; neither can act on the
    // other's conclusion without this step, and a dispatch that marks a source
    // pending without making its driver runnable is an interrupt that is never
    // serviced.
    if (taken.value().wake && taken.value().owner != invalid_thread) {
        synchronise();
    }
    return taken;
}

Decision Kernel::schedule(std::uint64_t now_nanoseconds) noexcept {
    return scheduler_.choose(now_nanoseconds);
}

} // namespace os::kernel
