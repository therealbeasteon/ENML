#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/capability.hpp>
#include <os/kernel/rendezvous.hpp>

namespace os::kernel {

// Cookie IPC is endpoint-addressed, never thread-addressed. A service may restart
// with the same higher-level principal/name, but its kernel IPC incarnation gets
// a fresh endpoint generation. Stale clients therefore cannot accidentally send
// into a replacement service merely because a thread id was reused.
using IpcEndpointSlot = std::uint16_t;
using IpcEndpointGeneration = std::uint32_t;
using IpcTransactionId = std::uint64_t;

inline constexpr std::size_t max_ipc_endpoints = 64U;
inline constexpr std::size_t max_ipc_reply_seals = max_threads;

// Rights interpreted only by the IPC layer. CapabilityTable itself remains
// policy-agnostic and merely attenuates bitmasks.
inline constexpr Rights ipc_right_send = 1U << 0U;
inline constexpr Rights ipc_right_receive = 1U << 1U;
inline constexpr Rights ipc_right_all = ipc_right_send | ipc_right_receive;

struct IpcEndpoint final {
    IpcEndpointSlot slot {0U};
    IpcEndpointGeneration generation {0U};

    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0U; }
    [[nodiscard]] friend constexpr bool operator==(const IpcEndpoint&, const IpcEndpoint&) = default;
};

// Kernel object id used by CapabilityTable. The high tag prevents an IPC object
// from being confused with an untyped service-defined object id by this layer.
inline constexpr ObjectId ipc_object_tag = 0x4950'4300'0000'0000ULL; // "IPC"

[[nodiscard]] constexpr ObjectId ipc_object_id(IpcEndpoint endpoint) noexcept {
    if (!endpoint.valid()) return invalid_object;
    return ipc_object_tag |
        (static_cast<ObjectId>(endpoint.generation) << 16U) |
        static_cast<ObjectId>(endpoint.slot + 1U);
}

struct IpcReplySeal final {
    IpcEndpoint endpoint {};
    IpcTransactionId transaction {0U};
    ThreadId caller {invalid_thread};
    ThreadId server {invalid_thread};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return endpoint.valid() && transaction != 0U &&
            caller != invalid_thread && server != invalid_thread;
    }
    [[nodiscard]] friend constexpr bool operator==(const IpcReplySeal&, const IpcReplySeal&) = default;
};

struct IpcReceived final {
    ThreadId caller {invalid_thread};
    IpcReplySeal reply {};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return caller != invalid_thread && reply.valid() && reply.caller == caller;
    }
};

namespace ipc_errors {
inline constexpr std::uint32_t invalid_endpoint = 160U;
inline constexpr std::uint32_t endpoint_limit = 161U;
inline constexpr std::uint32_t stale_endpoint = 162U;
inline constexpr std::uint32_t not_endpoint_owner = 163U;
inline constexpr std::uint32_t invalid_capability = 164U;
inline constexpr std::uint32_t wrong_rights = 165U;
inline constexpr std::uint32_t reply_seal_limit = 166U;
inline constexpr std::uint32_t stale_reply_seal = 167U;
inline constexpr std::uint32_t wrong_reply_server = 168U;
inline constexpr std::uint32_t transaction_exhausted = 169U;
} // namespace ipc_errors

// Endpoint/reply authority only. Payload copying and user-buffer access remain a
// later machine boundary so this state machine stays fuzzable on a host.
class IpcEndpointTable final {
public:
    [[nodiscard]] os::core::Result<IpcEndpoint> create(ThreadId server) noexcept;
    [[nodiscard]] os::core::Result<void> retire(ThreadId server, IpcEndpoint endpoint) noexcept;

    [[nodiscard]] os::core::Result<void> send(
        ThreadId caller,
        CapabilityId endpoint_capability,
        const CapabilityTable& capabilities,
        Rendezvous& rendezvous) noexcept;

    // Returns caller=invalid_thread when receive legitimately blocks because no
    // sender is waiting. A Reply Seal exists only for a request actually taken.
    [[nodiscard]] os::core::Result<IpcReceived> receive(
        ThreadId server,
        CapabilityId endpoint_capability,
        const CapabilityTable& capabilities,
        Rendezvous& rendezvous) noexcept;

    [[nodiscard]] os::core::Result<void> reply(
        ThreadId server,
        const IpcReplySeal& seal,
        Rendezvous& rendezvous) noexcept;

    [[nodiscard]] bool active(IpcEndpoint endpoint) const noexcept;
    [[nodiscard]] std::size_t active_endpoint_count() const noexcept { return active_; }
    [[nodiscard]] std::size_t active_reply_seal_count() const noexcept { return reply_seals_; }

private:
    struct EndpointSlot final {
        ThreadId server {invalid_thread};
        IpcEndpointGeneration generation {0U};
        bool active {false};
    };
    struct ReplySlot final {
        IpcReplySeal seal {};
        bool active {false};
    };

    [[nodiscard]] os::core::Result<IpcEndpoint> endpoint_for_capability(
        ThreadId holder,
        CapabilityId capability,
        Rights required,
        const CapabilityTable& capabilities) const noexcept;
    [[nodiscard]] EndpointSlot* slot_for(IpcEndpoint endpoint) noexcept;
    [[nodiscard]] const EndpointSlot* slot_for(IpcEndpoint endpoint) const noexcept;
    [[nodiscard]] ReplySlot* reply_slot(const IpcReplySeal& seal) noexcept;
    void invalidate_replies_for(IpcEndpoint endpoint) noexcept;

    std::array<EndpointSlot, max_ipc_endpoints> endpoints_ {};
    std::array<ReplySlot, max_ipc_reply_seals> replies_ {};
    std::size_t active_ {0U};
    std::size_t reply_seals_ {0U};
    IpcTransactionId next_transaction_ {1U};
};

} // namespace os::kernel
