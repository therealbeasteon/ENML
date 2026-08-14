#include <os/kernel/kernel.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error interrupt_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

// The one check all three interrupt syscalls share: the capability is held
// by this thread right now (fresh, not cached - a capability revoked between
// attach and complete must not still authorize complete), names a source
// object rather than some other kind of object, and carries the one right
// that exists. InterruptTable never sees this - it is checked here, at the
// same composition layer ipc_send/ipc_receive check IPC capabilities at, per
// the separation docs/M7_1_INTERRUPT.md documents.
[[nodiscard]] os::core::Result<InterruptSource> interrupt_source_for_capability(
    ThreadId driver,
    CapabilityId source_capability,
    const CapabilityTable& capabilities) noexcept {
    if (driver == invalid_thread || source_capability == invalid_capability) {
        return interrupt_error(interrupt_errors::invalid_capability);
    }
    // The legacy ThreadId-only path. It must fail closed for an M7.8
    // context-bound capability rather than comparing only the reusable
    // numeric holder - the same rule endpoint_for_capability enforces for
    // IPC, for the same reason: holds() already refuses a context-bound slot
    // here, so a recycled ThreadId cannot inherit a bound driver's standing.
    if (!capabilities.holds(driver, source_capability)) {
        return interrupt_error(interrupt_errors::invalid_capability);
    }
    auto description = capabilities.describe(source_capability);
    if (!description) return description.error();
    if ((description.value().rights & interrupt_right_attach) == 0U) {
        return interrupt_error(interrupt_errors::wrong_rights);
    }
    if ((description.value().object & interrupt_object_tag_mask) != interrupt_object_tag) {
        return interrupt_error(interrupt_errors::wrong_object);
    }
    const auto source = static_cast<InterruptSource>(
        description.value().object & ~interrupt_object_tag_mask);
    if (source == invalid_interrupt_source) {
        return interrupt_error(interrupt_errors::wrong_object);
    }
    return source;
}

} // namespace

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

void Kernel::synchronise_thread(ThreadId thread) noexcept {
    auto state = threads_.state_of(thread);
    auto priority = threads_.effective_priority_of(thread);
    if (!state || !priority) return;
    const bool runnable = state.value() == ThreadState::ready;
    (void)scheduler_.update(thread, runnable, priority.value());
}

void Kernel::synchronise_pair(ThreadId first, ThreadId second) noexcept {
    synchronise_thread(first);
    if (second != first) synchronise_thread(second);
}

void Kernel::synchronise() noexcept {
    for (std::size_t i = 0U; i < live_count_; ++i) synchronise_thread(live_[i]);
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
    synchronise_thread(thread);
    return {};
}

os::core::Result<Teardown> Kernel::destroy_thread(ThreadId thread) noexcept {
    ipc_continuations_.release_thread(thread);
    const std::size_t retired_endpoints = ipc_.release_thread(thread, threads_);

    auto released = threads_.exit_thread(thread);
    if (!released) return os::core::Result<Teardown>{released.error()};

    Teardown teardown{};
    teardown.threads_released = released.value();
    teardown.ipc_endpoints_retired = retired_endpoints;
    teardown.capabilities_revoked = capabilities_.revoke_all_held_by(thread);
    teardown.interrupt_sources_released = interrupts_.detach_all_owned_by(thread);
    teardown.interrupt_delivery_released = interrupt_deliveries_.release(thread);
    (void)scheduler_.retire(thread);

    untrack(thread);
    synchronise();
    return os::core::Result<Teardown>{teardown};
}

os::core::Result<void> Kernel::send(ThreadId from, ThreadId to) noexcept {
    auto sent = threads_.send(from, to);
    if (!sent) return sent;
    synchronise_pair(from, to);
    return {};
}

os::core::Result<ThreadId> Kernel::receive(ThreadId self) noexcept {
    auto received = threads_.receive(self);
    if (!received) return received;
    synchronise_pair(self, received.value());
    return received;
}

os::core::Result<void> Kernel::reply(ThreadId self, ThreadId caller) noexcept {
    auto replied = threads_.reply(self, caller);
    if (!replied) return replied;
    synchronise_pair(self, caller);
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
    const bool receiver_waiting = ipc_.receive_waiting(endpoint);
    auto retired = ipc_.retire(server, endpoint, threads_);
    if (!retired) return retired;
    if (receiver_waiting && ipc_continuations_.receive_armed(server)) {
        (void)ipc_continuations_.cancel_receive(server);
    }
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
    synchronise_pair(server, received.value().caller);
    return received;
}

os::core::Result<void> Kernel::ipc_send(
    ExecutionAuthority caller,
    CapabilityId endpoint_capability,
    IpcEnvelope request) noexcept {
    auto sent = ipc_.send(caller, endpoint_capability, capabilities_, threads_, request);
    if (!sent) return sent;
    synchronise();
    return {};
}

os::core::Result<IpcReceived> Kernel::ipc_receive(
    ExecutionAuthority server,
    CapabilityId endpoint_capability) noexcept {
    auto received = ipc_.receive(server, endpoint_capability, capabilities_, threads_);
    if (!received) return received;
    synchronise_pair(server.thread, received.value().caller);
    return received;
}

os::core::Result<void> Kernel::ipc_reply(
    ThreadId server,
    const IpcReplySeal& seal,
    IpcEnvelope response) noexcept {
    auto replied = ipc_.reply(server, seal, threads_, response);
    if (!replied) return replied;
    synchronise_pair(server, seal.caller);
    return {};
}

os::core::Result<void> Kernel::ipc_reply(
    ExecutionAuthority server,
    const IpcReplySeal& seal,
    IpcEnvelope response) noexcept {
    auto replied = ipc_.reply(server, seal, threads_, response);
    if (!replied) return replied;
    synchronise_pair(server.thread, seal.caller);
    return {};
}

os::core::Result<void> Kernel::ipc_reply_transaction(
    ThreadId server,
    IpcTransactionId transaction,
    IpcEnvelope response) noexcept {
    auto replied = ipc_.reply_transaction(server, transaction, threads_, response);
    if (!replied) return replied;
    synchronise();
    return {};
}

os::core::Result<void> Kernel::ipc_reply_transaction(
    ExecutionAuthority server,
    IpcTransactionId transaction,
    IpcEnvelope response) noexcept {
    auto replied = ipc_.reply_transaction(server, transaction, threads_, response);
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

os::core::Result<IpcEnvelope> Kernel::ipc_take_reply(ExecutionAuthority caller) noexcept {
    if (!tracks(caller.thread)) {
        return os::core::Result<IpcEnvelope>{
            os::core::make_error(os::core::ErrorDomain::kernel,
                                 rendezvous_errors::unknown_thread)};
    }
    return ipc_.take_reply(caller);
}

os::core::Result<void> Kernel::ipc_arm_send_continuation(
    ThreadId caller,
    AddressSpaceEpoch epoch,
    std::uint64_t exchange_address,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (!tracks(caller)) {
        return os::core::make_error(os::core::ErrorDomain::kernel,
                                    rendezvous_errors::unknown_thread);
    }
    return ipc_continuations_.arm(caller, epoch, exchange_address, epochs);
}

os::core::Result<IpcSendContinuation> Kernel::ipc_take_send_continuation(
    ThreadId caller,
    AddressSpaceEpoch expected,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (!tracks(caller)) {
        return os::core::Result<IpcSendContinuation>{
            os::core::make_error(os::core::ErrorDomain::kernel,
                                 rendezvous_errors::unknown_thread)};
    }
    return ipc_continuations_.take(caller, expected, epochs);
}

os::core::Result<void> Kernel::ipc_cancel_send_continuation(ThreadId caller) noexcept {
    if (!tracks(caller)) {
        return os::core::make_error(os::core::ErrorDomain::kernel,
                                    rendezvous_errors::unknown_thread);
    }
    return ipc_continuations_.cancel(caller);
}

os::core::Result<void> Kernel::ipc_arm_receive_continuation(
    ThreadId server,
    AddressSpaceEpoch epoch,
    CapabilityId endpoint_capability,
    std::uint64_t exchange_address,
    const AddressSpaceEpochAuthority& epochs,
    std::uint64_t deadline_nanoseconds) noexcept {
    if (!tracks(server)) {
        return os::core::make_error(os::core::ErrorDomain::kernel,
                                    rendezvous_errors::unknown_thread);
    }
    return ipc_continuations_.arm_receive(
        server, epoch, endpoint_capability, exchange_address, epochs,
        deadline_nanoseconds);
}

std::uint64_t Kernel::ipc_earliest_receive_deadline() const noexcept {
    return ipc_continuations_.earliest_receive_deadline();
}

os::core::Result<bool> Kernel::ipc_expire_one_receive(
    std::uint64_t now_nanoseconds) noexcept {
    auto continuation = ipc_continuations_.take_expired_receive(now_nanoseconds);
    if (!continuation) {
        const auto error = continuation.error();
        if (error.domain == os::core::ErrorDomain::kernel &&
            error.code == ipc_continuation_errors::not_armed) {
            return false;
        }
        return error;
    }
    // The continuation is already gone at this point. If the wake fails the
    // slot must not be resurrected - a thread that cannot be woken is a thread
    // whose address space died underneath it, and leaving the continuation
    // armed would hold a deadline for a receiver that will never run.
    auto woken = threads_.expire_receive(continuation.value().server);
    if (!woken) return woken.error();
    return true;
}

os::core::Result<bool> Kernel::ipc_take_deadline_expiry(ThreadId thread) noexcept {
    return threads_.take_deadline_expiry(thread);
}

os::core::Result<IpcReceiveContinuation> Kernel::ipc_take_expired_receive_continuation(
    std::uint64_t now_nanoseconds) noexcept {
    return ipc_continuations_.take_expired_receive(now_nanoseconds);
}

os::core::Result<IpcReceiveContinuation> Kernel::ipc_take_receive_continuation(
    ThreadId server,
    AddressSpaceEpoch expected,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (!tracks(server)) {
        return os::core::Result<IpcReceiveContinuation>{
            os::core::make_error(os::core::ErrorDomain::kernel,
                                 rendezvous_errors::unknown_thread)};
    }
    return ipc_continuations_.take_receive(server, expected, epochs);
}

os::core::Result<void> Kernel::ipc_cancel_receive_continuation(ThreadId server) noexcept {
    if (!tracks(server)) {
        return os::core::make_error(os::core::ErrorDomain::kernel,
                                    rendezvous_errors::unknown_thread);
    }
    return ipc_continuations_.cancel_receive(server);
}

os::core::Result<void> Kernel::interrupt_attach(
    ThreadId driver, CapabilityId source_capability) noexcept {
    auto source = interrupt_source_for_capability(driver, source_capability, capabilities_);
    if (!source) return source.error();
    return interrupts_.attach(driver, source.value());
}

os::core::Result<void> Kernel::interrupt_detach(
    ThreadId driver, CapabilityId source_capability) noexcept {
    auto source = interrupt_source_for_capability(driver, source_capability, capabilities_);
    if (!source) return source.error();
    return interrupts_.detach(driver, source.value());
}

os::core::Result<bool> Kernel::interrupt_complete(
    ThreadId driver, CapabilityId source_capability) noexcept {
    auto source = interrupt_source_for_capability(driver, source_capability, capabilities_);
    if (!source) return source.error();
    return interrupts_.end_service(driver, source.value());
}

os::core::Result<Dispatch> Kernel::dispatch_interrupt(InterruptSource source) noexcept {
    auto taken = interrupts_.dispatch(source);
    if (!taken) return taken;

    if (taken.value().wake && taken.value().owner != invalid_thread) {
        const auto owner = taken.value().owner;
        // begin_service runs here rather than waiting for a syscall that does
        // not exist - docs/M7_1_INTERRUPT.md: "it is the transition the
        // kernel performs when it makes the driver runnable, and the count
        // rides back on the wakeup." wake is only true on the attached->
        // pending transition dispatch() just made, which is exactly
        // begin_service's own precondition, so this call is expected to
        // succeed by construction; if interrupt_deliveries_ already has an
        // outstanding delivery for this driver (a second source, not yet
        // exercised by anything built so far - see interrupt_delivery.hpp),
        // begin_service is skipped and the source stays pending in
        // InterruptTable rather than being collected into a slot that would
        // silently overwrite what the driver has not picked up yet.
        if (!interrupt_deliveries_.armed(owner)) {
            auto serviced = interrupts_.begin_service(owner, source);
            if (serviced) {
                (void)interrupt_deliveries_.arm(owner, source, serviced.value());
            }
        }
        synchronise_thread(owner);
    }
    return taken;
}

os::core::Result<Service> Kernel::take_delivered_service(ThreadId driver) noexcept {
    return interrupt_deliveries_.take(driver);
}

Decision Kernel::schedule(std::uint64_t now_nanoseconds) noexcept {
    return scheduler_.choose(now_nanoseconds);
}

} // namespace os::kernel
