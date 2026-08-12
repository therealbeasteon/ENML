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

std::size_t Kernel::live_thread_count() const noexcept { return live_count_; }

void Kernel::synchronise() noexcept {
    for (std::size_t i = 0U; i < live_count_; ++i) {
        const ThreadId thread = live_[i];
        auto state = threads_.state_of(thread);
        auto priority = threads_.effective_priority_of(thread);
        if (!state || !priority) continue;
        const bool runnable = state.value() == ThreadState::ready;
        (void)scheduler_.update(thread, runnable, priority.value());
    }
}

os::core::Result<void> Kernel::create_thread(ThreadId thread, Priority priority) noexcept {
    auto created = threads_.create_thread(thread, priority);
    if (!created) return created;
    auto admitted = scheduler_.admit(thread, priority);
    if (!admitted) {
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
    const std::size_t retired_endpoints = ipc_.retire_all_owned_by(thread, threads_);

    auto released = threads_.exit_thread(thread);
    if (!released) {
        return os::core::Result<Teardown>{released.error()};
    }

    Teardown teardown{};
    teardown.threads_released = released.value();
    teardown.ipc_endpoints_retired = retired_endpoints;
    teardown.capabilities_revoked = capabilities_.revoke_all_held_by(thread);
    teardown.interrupt_sources_released = interrupts_.detach_all_owned_by(thread);
    (void)scheduler_.retire(thread);

    untrack(thread);
    synchronise();
    return os::core::Result<Teardown>{teardown};
}

os::core::Result<void> Kernel::send(ThreadId from, ThreadId to) noexcept {
    auto sent = threads_.send(from, to);
    if (!sent) return sent;
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

os::core::Result<IpcEndpoint> Kernel::create_ipc_endpoint(ThreadId server) noexcept {
    if (!tracks(server)) {
        return os::core::Result<IpcEndpoint>{
            os::core::make_error(os::core::ErrorDomain::kernel,
                                 rendezvous_errors::unknown_thread)};
    }
    return ipc_.create(server);
}

os::core::Result<void> Kernel::retire_ipc_endpoint(
    ThreadId server,
    IpcEndpoint endpoint) noexcept {
    auto retired = ipc_.retire(server, endpoint, threads_);
    if (!retired) return retired;
    synchronise();
    return {};
}

os::core::Result<void> Kernel::ipc_send(
    ThreadId caller,
    CapabilityId endpoint_capability,
    IpcEnvelope request) noexcept {
    auto sent = ipc_.send(caller, endpoint_capability, capabilities_, threads_, request);
    if (!sent) return sent;
    synchronise();
    return {};
}

os::core::Result<IpcReceived> Kernel::ipc_receive(
    ThreadId server,
    CapabilityId endpoint_capability) noexcept {
    auto received = ipc_.receive(server, endpoint_capability, capabilities_, threads_);
    if (!received) return received;
    synchronise();
    return received;
}

os::core::Result<void> Kernel::ipc_reply(
    ThreadId server,
    const IpcReplySeal& seal,
    IpcEnvelope response) noexcept {
    auto replied = ipc_.reply(server, seal, threads_, response);
    if (!replied) return replied;
    synchronise();
    return {};
}

os::core::Result<IpcEnvelope> Kernel::ipc_take_reply(ThreadId caller) noexcept {
    if (!tracks(caller)) {
        return os::core::Result<IpcEnvelope>{
            os::core::make_error(os::core::ErrorDomain::kernel,
                                 rendezvous_errors::unknown_thread)};
    }
    return ipc_.take_reply(caller);
}

os::core::Result<Dispatch> Kernel::dispatch_interrupt(InterruptSource source) noexcept {
    auto taken = interrupts_.dispatch(source);
    if (!taken) return taken;
    if (taken.value().wake && taken.value().owner != invalid_thread) {
        synchronise();
    }
    return taken;
}

Decision Kernel::schedule(std::uint64_t now_nanoseconds) noexcept {
    return scheduler_.choose(now_nanoseconds);
}

} // namespace os::kernel
