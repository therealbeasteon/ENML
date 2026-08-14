#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/capability.hpp>
#include <os/kernel/interrupt.hpp>
#include <os/kernel/ipc_continuation.hpp>
#include <os/kernel/ipc_endpoint.hpp>
#include <os/kernel/rendezvous.hpp>
#include <os/kernel/scheduler.hpp>

namespace os::kernel {

namespace kernel_errors {
inline constexpr std::uint32_t creation_incomplete = 1U;
} // namespace kernel_errors

struct Teardown final {
    std::size_t threads_released {0U};
    std::size_t capabilities_revoked {0U};
    std::size_t interrupt_sources_released {0U};
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
    [[nodiscard]] os::core::Result<void> ipc_reply(
        ThreadId server,
        const IpcReplySeal& seal,
        IpcEnvelope response = {}) noexcept;
    [[nodiscard]] os::core::Result<void> ipc_reply_transaction(
        ThreadId server,
        IpcTransactionId transaction,
        IpcEnvelope response = {}) noexcept;
    [[nodiscard]] os::core::Result<IpcEnvelope> ipc_take_reply(ThreadId caller) noexcept;

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
        const AddressSpaceEpochAuthority& epochs) noexcept;
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

    os::core::Result<Dispatch> dispatch_interrupt(InterruptSource source) noexcept;
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
    IpcEndpointTable ipc_ {};
    IpcContinuationTable ipc_continuations_ {};
    Scheduler scheduler_ {};

    std::array<ThreadId, max_threads> live_ {};
    std::size_t live_count_ {0U};
};

} // namespace os::kernel
