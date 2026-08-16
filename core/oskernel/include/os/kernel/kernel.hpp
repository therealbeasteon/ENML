#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/address_space_syscall.hpp>
#include <os/kernel/capability.hpp>
#include <os/kernel/interrupt.hpp>
#include <os/kernel/interrupt_delivery.hpp>
#include <os/kernel/ipc_continuation.hpp>
#include <os/kernel/ipc_endpoint.hpp>
#include <os/kernel/memory_grant.hpp>
#include <os/kernel/process_translation.hpp>
#include <os/kernel/rendezvous.hpp>
#include <os/kernel/scheduler.hpp>
#include <os/kernel/thread_admission.hpp>
#include <os/kernel/translation_root.hpp>

namespace os::kernel {

namespace kernel_errors {
inline constexpr std::uint32_t creation_incomplete = 1U;
} // namespace kernel_errors

// What address_space_create hands back: the lifetime it opened, and the
// capability naming it. Both, because they answer different questions - the
// epoch is what the machine layer needs to build tables against, and the
// capability is what the creator will present to do anything with the space
// afterwards. Returning only the epoch would leave the creator holding
// authority it cannot name; returning only the capability would force the
// machine layer to re-derive an epoch from an object id, which is precisely
// the lookup this milestone had to add to make destroy work at all.
struct AddressSpaceCreation final {
    AddressSpaceEpoch epoch {};
    CapabilityId capability {invalid_capability};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return epoch.valid() && capability != invalid_capability;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const AddressSpaceCreation&, const AddressSpaceCreation&) = default;
};

struct Teardown final {
    std::size_t threads_released {0U};
    std::size_t capabilities_revoked {0U};
    std::size_t interrupt_sources_released {0U};
    bool interrupt_delivery_released {false};
    std::size_t ipc_endpoints_retired {0U};

    [[nodiscard]] friend constexpr bool operator==(const Teardown&, const Teardown&) = default;
};

class Kernel final {
public:
    Kernel() noexcept = default;

    [[nodiscard]] os::core::Result<void> create_thread(
        ThreadId thread,
        Priority priority = default_priority) noexcept;
    os::core::Result<Teardown> destroy_thread(ThreadId thread) noexcept;

    [[nodiscard]] os::core::Result<void> send(ThreadId from, ThreadId to) noexcept;
    [[nodiscard]] os::core::Result<ThreadId> receive(ThreadId self) noexcept;
    [[nodiscard]] os::core::Result<void> reply(ThreadId self, ThreadId caller) noexcept;
    [[nodiscard]] os::core::Result<void> yield(ThreadId self) noexcept;

    [[nodiscard]] os::core::Result<IpcEndpoint> create_ipc_endpoint(ThreadId server) noexcept;
    [[nodiscard]] os::core::Result<void> retire_ipc_endpoint(
        ThreadId server,
        IpcEndpoint endpoint) noexcept;
    [[nodiscard]] os::core::Result<void> ipc_send(
        ThreadId caller,
        CapabilityId endpoint_capability,
        IpcEnvelope request = {}) noexcept;
    [[nodiscard]] os::core::Result<IpcReceived> ipc_receive(
        ThreadId server,
        CapabilityId endpoint_capability) noexcept;
    // M7.8.2 - see ipc_endpoint.hpp's own ExecutionAuthority overloads for
    // what each of these does.
    [[nodiscard]] os::core::Result<void> ipc_send(
        ExecutionAuthority caller,
        CapabilityId endpoint_capability,
        IpcEnvelope request = {}) noexcept;
    [[nodiscard]] os::core::Result<IpcReceived> ipc_receive(
        ExecutionAuthority server,
        CapabilityId endpoint_capability) noexcept;
    [[nodiscard]] os::core::Result<void> ipc_reply(
        ThreadId server,
        const IpcReplySeal& seal,
        IpcEnvelope response = {}) noexcept;
    [[nodiscard]] os::core::Result<void> ipc_reply(
        ExecutionAuthority server,
        const IpcReplySeal& seal,
        IpcEnvelope response = {}) noexcept;
    [[nodiscard]] os::core::Result<void> ipc_reply_transaction(
        ThreadId server,
        IpcTransactionId transaction,
        IpcEnvelope response = {}) noexcept;
    [[nodiscard]] os::core::Result<void> ipc_reply_transaction(
        ExecutionAuthority server,
        IpcTransactionId transaction,
        IpcEnvelope response = {}) noexcept;
    [[nodiscard]] os::core::Result<IpcEnvelope> ipc_take_reply(ThreadId caller) noexcept;
    [[nodiscard]] os::core::Result<IpcEnvelope> ipc_take_reply(ExecutionAuthority caller) noexcept;

    [[nodiscard]] os::core::Result<void> ipc_arm_send_continuation(
        ThreadId caller,
        AddressSpaceEpoch epoch,
        std::uint64_t exchange_address,
        const AddressSpaceEpochAuthority& epochs) noexcept;
    [[nodiscard]] os::core::Result<IpcSendContinuation> ipc_take_send_continuation(
        ThreadId caller,
        AddressSpaceEpoch expected,
        const AddressSpaceEpochAuthority& epochs) noexcept;
    [[nodiscard]] os::core::Result<void> ipc_cancel_send_continuation(ThreadId caller) noexcept;

    [[nodiscard]] os::core::Result<void> ipc_arm_receive_continuation(
        ThreadId server,
        AddressSpaceEpoch epoch,
        CapabilityId endpoint_capability,
        std::uint64_t exchange_address,
        const AddressSpaceEpochAuthority& epochs,
        std::uint64_t deadline_nanoseconds = 0ULL) noexcept;
    // Forwarded so the machine layer can arm one timer for the whole table and
    // complete whichever waiter that timer belongs to, without reaching into
    // continuation state itself.
    [[nodiscard]] std::uint64_t ipc_earliest_receive_deadline() const noexcept;
    // Expires one waiter whose bounded receive has run out: takes its
    // continuation and wakes it with WakeReason::deadline_expired. Returns
    // false when nothing has expired, so a timer handler can drain in a loop.
    [[nodiscard]] os::core::Result<bool> ipc_expire_one_receive(
        std::uint64_t now_nanoseconds) noexcept;
    // Consumed by the completion path; see Rendezvous::take_deadline_expiry.
    [[nodiscard]] os::core::Result<bool> ipc_take_deadline_expiry(
        ThreadId thread) noexcept;
    [[nodiscard]] os::core::Result<IpcReceiveContinuation>
    ipc_take_expired_receive_continuation(std::uint64_t now_nanoseconds) noexcept;
    [[nodiscard]] os::core::Result<IpcReceiveContinuation> ipc_take_receive_continuation(
        ThreadId server,
        AddressSpaceEpoch expected,
        const AddressSpaceEpochAuthority& epochs) noexcept;
    [[nodiscard]] os::core::Result<void> ipc_cancel_receive_continuation(ThreadId server) noexcept;

    // Capability-checked. InterruptTable deliberately does not consult
    // CapabilityTable itself - see interrupt.hpp - so the check happens here,
    // at the same composition layer ipc_send/ipc_receive/ipc_reply already
    // check IPC capabilities at.
    [[nodiscard]] os::core::Result<void> interrupt_attach(
        ThreadId driver, CapabilityId source_capability) noexcept;
    [[nodiscard]] os::core::Result<void> interrupt_detach(
        ThreadId driver, CapabilityId source_capability) noexcept;
    // Returns whether the source must be serviced again immediately, exactly
    // as InterruptTable::end_service does - the capability check in front of
    // it does not change what the driver needs to know.
    [[nodiscard]] os::core::Result<bool> interrupt_complete(
        ThreadId driver, CapabilityId source_capability) noexcept;

    // Resolves a capability over memory to the range it names.
    //
    // Capability-checked here for the same reason every other object's check
    // is: MemoryGrantAuthority does not consult CapabilityTable and must not
    // learn to. It answers what was granted; this answers whether this holder
    // may spend it.
    //
    // The `memory_right_donate` requirement is the point. Mapping memory you
    // were given and turning it into a translation table are different
    // authorities - the second permanently changes what the range is and takes
    // it out of the holder's reach - so a process can hold memory it may use
    // without holding memory it may spend on kernel objects.
    [[nodiscard]] os::core::Result<MemoryGrant> memory_for_capability(
        ThreadId holder,
        CapabilityId capability,
        Rights required,
        const MemoryGrantAuthority& grants) const noexcept;

    // Address-space lifecycle, capability-checked at this layer for the same
    // reason interrupt_attach is: AddressSpaceEpochAuthority does not consult
    // CapabilityTable and must not learn to.
    //
    // These deliberately do not touch page tables. The kernel owns *which
    // lifetimes exist and who may name them*; the machine layer owns the
    // tables, and aarch64_create_address_space/aarch64_release_address_space
    // are what run between the calls below. Splitting it here is what keeps
    // the portable half of this milestone free of AArch64.
    [[nodiscard]] os::core::Result<AddressSpaceCreation> address_space_create(
        ThreadId creator,
        CapabilityId authority,
        AddressSpaceEpochAuthority& epochs) noexcept;

    // Two phases, because retirement genuinely has two. begin invalidates the
    // software epoch so nothing new can bind to the space, and only then may
    // the machine layer tear down translations and invalidate TLBs; complete
    // releases the ASID for reuse once it has. Collapsing them would hand an
    // ASID to a new space while stale translations for the old one could still
    // be cached - the exact hazard AddressSpaceEpochAuthority's own one-way
    // lifecycle exists to prevent.
    [[nodiscard]] os::core::Result<RetiringAddressSpaceEpoch> address_space_begin_destroy(
        ThreadId owner,
        CapabilityId space,
        AddressSpaceEpochAuthority& epochs) noexcept;
    [[nodiscard]] os::core::Result<void> address_space_complete_destroy(
        ThreadId owner,
        CapabilityId space,
        RetiringAddressSpaceEpoch retiring,
        AddressSpaceEpochAuthority& epochs) noexcept;

    // Admits a thread into an address space the caller holds with
    // address_space_right_admit. See docs/M7_12_ENTRY_BINDING.md for why the
    // things this does *not* take are the design: no entry point, no thread
    // identifier, no priority.
    //
    // `root` comes from the machine layer's own lookup of the space named by
    // `space`, never from the caller - it is how the entry reaches this layer,
    // and routing it through the caller would hand back exactly the choice the
    // sealed entry exists to withhold. The split is the same one
    // address_space_create draws: the kernel owns which lifetimes exist and who
    // may name them, the machine layer owns the tables.
    //
    // The admitted thread is left *not runnable*. It becomes runnable when the
    // machine layer has admitted an exception frame built from the returned
    // entry and stack, because a thread the scheduler can select before its
    // architectural state exists is one it can select and fail to resume.
    [[nodiscard]] os::core::Result<ThreadAdmission> thread_admit(
        ThreadId creator,
        CapabilityId space,
        std::uint64_t stack,
        SealedTranslationRoot root,
        AddressSpaceEpochAuthority& epochs,
        ProcessTranslationTable& translations) noexcept;

    os::core::Result<Dispatch> dispatch_interrupt(InterruptSource source) noexcept;
    // What begin_service collected the instant this driver was last woken, if
    // it has not already been delivered. See interrupt_delivery.hpp -
    // dispatch_interrupt is what arms this, and a driver's resume is meant to
    // consume it without a syscall, the same way a receive-blocked thread's
    // resume consumes its delivered request.
    [[nodiscard]] os::core::Result<Service> take_delivered_service(ThreadId driver) noexcept;
    Decision schedule(std::uint64_t now_nanoseconds) noexcept;

    [[nodiscard]] CapabilityTable& capabilities() noexcept { return capabilities_; }
    [[nodiscard]] const CapabilityTable& capabilities() const noexcept { return capabilities_; }
    [[nodiscard]] InterruptTable& interrupts() noexcept { return interrupts_; }
    [[nodiscard]] const InterruptTable& interrupts() const noexcept { return interrupts_; }
    [[nodiscard]] const IpcEndpointTable& ipc() const noexcept { return ipc_; }
    [[nodiscard]] const IpcContinuationTable& ipc_continuations() const noexcept { return ipc_continuations_; }
    [[nodiscard]] const Rendezvous& threads() const noexcept { return threads_; }
    [[nodiscard]] constexpr Scheduler& runqueue() noexcept { return scheduler_; }
    [[nodiscard]] constexpr const Scheduler& runqueue() const noexcept { return scheduler_; }

    [[nodiscard]] std::size_t live_thread_count() const noexcept;

private:
    void synchronise_thread(ThreadId thread) noexcept;
    void synchronise_pair(ThreadId first, ThreadId second) noexcept;
    void synchronise() noexcept;
    [[nodiscard]] bool tracks(ThreadId thread) const noexcept;
    void untrack(ThreadId thread) noexcept;

    Rendezvous threads_ {};
    CapabilityTable capabilities_ {};
    InterruptTable interrupts_ {};
    InterruptDeliveryTable interrupt_deliveries_ {};
    IpcEndpointTable ipc_ {};
    IpcContinuationTable ipc_continuations_ {};
    Scheduler scheduler_ {};
    ThreadIdentifierIssuer thread_identifiers_ {};

    std::array<ThreadId, max_threads> live_ {};
    std::size_t live_count_ {0U};
};

} // namespace os::kernel
