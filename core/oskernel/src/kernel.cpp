#include <os/kernel/kernel.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error interrupt_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] constexpr os::core::Error address_space_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

// The shared front half of every address-space call: the capability is held by
// this thread right now, carries the right this operation needs, and names the
// object it is supposed to name.
//
// One error for all three failures, deliberately. A caller learns that its
// capability did not authorize this, not which of "you do not hold it", "you
// hold it without that right" and "that is a capability over something else"
// applied - the same reasoning M7.8's reply path uses to keep a stale
// generation from distinguishing itself from a wrong thread.
[[nodiscard]] os::core::Result<void> address_space_capability(
    ThreadId thread,
    CapabilityId capability,
    const CapabilityTable& capabilities,
    Rights required,
    ObjectId object) noexcept {
    if (thread == invalid_thread || capability == invalid_capability) {
        return address_space_error(address_space_syscall_errors::invalid_capability);
    }
    if (!capabilities.holds(thread, capability)) {
        return address_space_error(address_space_syscall_errors::invalid_capability);
    }
    auto description = capabilities.describe(capability);
    if (!description) return description.error();
    if ((description.value().rights & required) == 0U ||
        description.value().object != object) {
        return address_space_error(address_space_syscall_errors::invalid_capability);
    }
    return {};
}

// The same check for a capability over *some* space rather than a known one,
// recovering which space it names. The object is not known in advance here, so
// the tag is what proves this is an address-space capability at all.
[[nodiscard]] os::core::Result<AddressSpaceIdentity> address_space_identity_for_capability(
    ThreadId thread,
    CapabilityId capability,
    const CapabilityTable& capabilities,
    Rights required) noexcept {
    if (thread == invalid_thread || capability == invalid_capability) {
        return os::core::Result<AddressSpaceIdentity>{
            address_space_error(address_space_syscall_errors::invalid_capability)};
    }
    if (!capabilities.holds(thread, capability)) {
        return os::core::Result<AddressSpaceIdentity>{
            address_space_error(address_space_syscall_errors::invalid_capability)};
    }
    auto description = capabilities.describe(capability);
    if (!description) return description.error();
    if ((description.value().rights & required) == 0U ||
        (description.value().object & address_space_object_tag_mask) !=
            address_space_object_tag) {
        return os::core::Result<AddressSpaceIdentity>{
            address_space_error(address_space_syscall_errors::invalid_capability)};
    }

    const AddressSpaceIdentity identity{
        static_cast<AddressSpaceSlot>(description.value().object & 0xFFFFULL),
        static_cast<AddressSpaceGeneration>(
            (description.value().object >> 16U) & 0xFFFF'FFFFULL),
    };
    // Catches the authority object, whose generation is zero: it carries the
    // tag but names no space, so it must not be usable to destroy one.
    if (!identity.valid()) {
        return os::core::Result<AddressSpaceIdentity>{
            address_space_error(address_space_syscall_errors::invalid_capability)};
    }
    return identity;
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

os::core::Result<MemoryGrant> Kernel::memory_for_capability(
    ThreadId holder,
    CapabilityId capability,
    Rights required,
    const MemoryGrantAuthority& grants) const noexcept {
    if (holder == invalid_thread || capability == invalid_capability) {
        return os::core::Result<MemoryGrant>{
            address_space_error(address_space_syscall_errors::invalid_capability)};
    }
    if (!capabilities_.holds(holder, capability)) {
        return os::core::Result<MemoryGrant>{
            address_space_error(address_space_syscall_errors::invalid_capability)};
    }
    auto description = capabilities_.describe(capability);
    if (!description) return description.error();
    if ((description.value().rights & required) == 0U ||
        (description.value().object & memory_grant_object_tag_mask) !=
            memory_grant_object_tag) {
        return os::core::Result<MemoryGrant>{
            address_space_error(address_space_syscall_errors::invalid_capability)};
    }

    const MemoryGrantIdentity identity{
        static_cast<MemoryGrantSlot>(description.value().object & 0xFFFFULL),
        static_cast<MemoryGrantGeneration>(
            (description.value().object >> 16U) & 0xFFFF'FFFFULL),
    };
    // resolve() is what refuses a capability over a grant that has been
    // revoked. It cannot be skipped by holding an old capability, because the
    // generation is part of what was resolved.
    return grants.resolve(identity);
}

os::core::Result<MapAuthorization> Kernel::map_authorize(
    ThreadId caller,
    const MapSyscall& request,
    const AddressSpaceEpochAuthority& epochs,
    const MemoryGrantAuthority& grants) const noexcept {
    // The space first, and with address_space_right_hold. Not a right of its
    // own: holding a space is what furnishing it is, and the split M7.12 drew
    // was between furnishing memory and running code - which is what
    // address_space_right_admit already separates. See docs/M7_16_MAP.md.
    auto identity = address_space_identity_for_capability(
        caller, request.space, capabilities_, address_space_right_hold);
    if (!identity) return identity.error();

    // resolve() is what refuses a capability over a space that has already been
    // destroyed, and it cannot be skipped by holding an old capability because
    // the generation is part of what was resolved. Mapping into a retired space
    // would write translations nothing will ever tear down.
    auto epoch = epochs.resolve(identity.value());
    if (!epoch) return epoch.error();

    // memory_right_map, which is the right fault_supply already requires of the
    // backing it is handed. The same operation is being authorised, so it is
    // the same right rather than a second one meaning the same thing.
    auto backing = memory_for_capability(
        caller, request.backing, memory_right_map, grants);
    if (!backing) return backing.error();

    // The length is the grant's whole extent. Not an argument, so there is no
    // second statement of it to disagree with the authority, and no reconciling
    // check to get wrong at the overflowing end - which MemoryGrant::contains
    // exists to be careful about and which this call now never has to ask.
    const auto length = backing.value().length;

    // What the caller *can* still get wrong is where it asked for the mapping
    // to go: a grant of any size placed near the top of the address space runs
    // off the end. Refused here rather than left to wrap into a low address
    // that is mappable and is not what anyone asked for.
    if (length > UINT64_MAX - request.virtual_address) {
        return os::core::Result<MapAuthorization>{
            os::core::make_error(
                os::core::ErrorDomain::kernel,
                map_syscall_errors::range_overflows)};
    }

    return MapAuthorization{
        .space = identity.value(),
        .virtual_base = request.virtual_address,
        .physical_base = backing.value().physical_base,
        .length = length,
        .permissions = request.permissions,
    };
}

os::core::Result<AddressSpaceCreation> Kernel::address_space_create(
    ThreadId creator,
    CapabilityId authority,
    AddressSpaceEpochAuthority& epochs) noexcept {
    auto checked = address_space_capability(
        creator, authority, capabilities_,
        address_space_right_create, address_space_authority_object);
    if (!checked) return checked.error();

    auto epoch = epochs.acquire();
    if (!epoch) return epoch.error();

    // hold | destroy | admit. The creator built the space and there is nobody
    // else to hold any of the three yet; anything narrower is a derived
    // capability with bits removed, which is what a pager gets.
    auto minted = capabilities_.mint(
        creator, address_space_object_id(epoch.value().identity()),
        address_space_right_hold | address_space_right_destroy |
            address_space_right_admit,
        true);
    if (!minted) {
        // Give the slot back rather than leaking a lifetime nothing can name.
        //
        // A capability table full at exactly this moment would otherwise burn
        // an epoch slot permanently: the space would be active, no capability
        // would name it, and no destroy could reach it because destroy is
        // reached through a capability. There are only 63 slots, so a caller
        // that can provoke this repeatedly could exhaust address spaces
        // entirely without ever holding one.
        //
        // Unwound through the ordinary two-phase retire, not a special path.
        // Nothing has been built against this epoch - no tables, no TLB
        // entries, no thread bound - so completing immediately is honest here
        // in a way it would not be after the machine layer had touched it.
        auto retiring = epochs.begin_retire(epoch.value());
        if (retiring) {
            auto completed = epochs.complete_retire(retiring.value());
            (void)completed;
        }
        return minted.error();
    }

    return AddressSpaceCreation{
        .epoch = epoch.value(),
        .capability = minted.value(),
    };
}

os::core::Result<RetiringAddressSpaceEpoch> Kernel::address_space_begin_destroy(
    ThreadId owner,
    CapabilityId space,
    AddressSpaceEpochAuthority& epochs) noexcept {
    auto identity = address_space_identity_for_capability(
        owner, space, capabilities_, address_space_right_destroy);
    if (!identity) return identity.error();

    // resolve() is what refuses a capability over a space that has already
    // been destroyed. It cannot be skipped by holding an old capability,
    // because the generation is part of what was resolved.
    auto epoch = epochs.resolve(identity.value());
    if (!epoch) return epoch.error();
    return epochs.begin_retire(epoch.value());
}

os::core::Result<void> Kernel::address_space_complete_destroy(
    ThreadId owner,
    CapabilityId space,
    RetiringAddressSpaceEpoch retiring,
    AddressSpaceEpochAuthority& epochs) noexcept {
    auto identity = address_space_identity_for_capability(
        owner, space, capabilities_, address_space_right_destroy);
    if (!identity) return identity.error();

    // The capability presented here must name the space actually being
    // retired. Without this a holder of one space's destroy capability could
    // complete another's retirement - releasing an ASID whose translations
    // the machine layer may not have invalidated yet.
    if (retiring.epoch.identity() != identity.value()) {
        return address_space_error(address_space_syscall_errors::invalid_capability);
    }

    auto completed = epochs.complete_retire(retiring);
    if (!completed) return completed.error();

    // The space is gone, so the capability naming it must go too. Left live it
    // would be a capability over an identity that resolve() now refuses -
    // harmless today because every path re-resolves, and exactly the kind of
    // dangling authority that stops being harmless the first time one does not.
    auto revoked = capabilities_.revoke(owner, space);
    if (!revoked) return revoked.error();
    return {};
}

os::core::Result<ThreadAdmission> Kernel::thread_admit(
    ThreadId creator,
    CapabilityId space,
    std::uint64_t stack,
    SealedTranslationRoot root,
    AddressSpaceEpochAuthority& epochs,
    ProcessTranslationTable& translations) noexcept {
    if (stack == 0ULL) {
        return os::core::Result<ThreadAdmission>{
            address_space_error(thread_admission_errors::invalid_stack)};
    }
    // Non-zero is all this layer checks of the stack. Whether it is mapped is
    // the fault path's question and answering it weakly here would be a second
    // copy of a check that already exists - the same reasoning that keeps the
    // entry unverified against what the space maps.

    auto identity = address_space_identity_for_capability(
        creator, space, capabilities_, address_space_right_admit);
    if (!identity) return os::core::Result<ThreadAdmission>{identity.error()};

    // Refuses a capability over a space that has already been destroyed, for
    // the same reason destroy does: the generation is part of what resolves.
    auto epoch = epochs.resolve(identity.value());
    if (!epoch) return os::core::Result<ThreadAdmission>{epoch.error()};

    // The entry is read here and never taken from the caller. `root` is the
    // machine layer's own lookup of the space named above, so nothing an EL0
    // caller controls reaches this value. See docs/M7_12_ENTRY_BINDING.md.
    if (!root.valid()) {
        return os::core::Result<ThreadAdmission>{
            address_space_error(thread_admission_errors::invalid_capability)};
    }

    // Issued rather than accepted. A caller that named its own identifier
    // would learn from the refusal which ones are live.
    auto issued = thread_identifiers_.issue();
    if (!issued) return os::core::Result<ThreadAdmission>{issued.error()};

    // The creator's priority, not a caller's choice: a thread more urgent than
    // the thread that asked for it is a scheduling escalation, and no caller
    // needs the argument yet.
    // Read from the rendezvous rather than the scheduler because that is the
    // number synchronise_thread already treats as authoritative, and it fails
    // for a creator that is not a live thread - which is the check that a
    // caller has to exist at all.
    auto creator_priority = threads_.effective_priority_of(creator);
    if (!creator_priority) {
        return os::core::Result<ThreadAdmission>{creator_priority.error()};
    }

    // Created in ThreadState::admitted, so it is not runnable until
    // thread_start - and not runnable in the structure that *decides*
    // runnability, rather than merely absent from the runqueue.
    // synchronise_thread recomputes the runqueue from the rendezvous state on
    // every IPC operation, so a thread parked only in the scheduler is put back
    // by the next send anywhere in the system. That is not a hypothetical: it
    // is what the first version did, and hardware caught it by running the new
    // thread with no frame ahead of everything else.
    auto created = create_admitted_thread(issued.value(), creator_priority.value());
    if (!created) return os::core::Result<ThreadAdmission>{created.error()};

    auto bound = translations.bind(issued.value(), epoch.value(), root, epochs);
    if (!bound) {
        // Unwound completely. A thread that exists and is bound to nothing is
        // one the scheduler may pick and the switch path cannot resolve.
        (void)destroy_thread(issued.value());
        return os::core::Result<ThreadAdmission>{bound.error()};
    }

    return ThreadAdmission{
        .thread = issued.value(),
        .entry = root.entry(),
        .stack = stack,
    };
}

os::core::Result<void> Kernel::create_admitted_thread(
    ThreadId thread, Priority priority) noexcept {
    auto created = threads_.create_admitted_thread(thread, priority);
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
    // Reconciles the runqueue with the rendezvous, which now says `admitted`
    // and therefore not runnable. Scheduler::admit defaults to runnable, so
    // without this the two disagree from the first instant.
    synchronise_thread(thread);
    return {};
}

os::core::Result<void> Kernel::thread_start(ThreadId thread) noexcept {
    auto started = threads_.start_thread(thread);
    if (!started) return started;
    synchronise_thread(thread);
    return {};
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
