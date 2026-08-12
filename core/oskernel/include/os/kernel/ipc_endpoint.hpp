#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <os/core/result.hpp>
#include <os/kernel/capability.hpp>
#include <os/kernel/rendezvous.hpp>

namespace os::kernel {

using IpcEndpointSlot = std::uint16_t;
using IpcEndpointGeneration = std::uint32_t;
using IpcTransactionId = std::uint64_t;

inline constexpr std::size_t max_ipc_endpoints = 64U;
inline constexpr std::size_t max_ipc_reply_seals = max_threads;
inline constexpr std::size_t max_ipc_pending_calls = max_threads;
inline constexpr std::size_t max_ipc_inline_bytes = 64U;

inline constexpr Rights ipc_right_send = 1U << 0U;
inline constexpr Rights ipc_right_receive = 1U << 1U;
inline constexpr Rights ipc_right_all = ipc_right_send | ipc_right_receive;

struct IpcEndpoint final {
    IpcEndpointSlot slot {0U};
    IpcEndpointGeneration generation {0U};
    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0U; }
    [[nodiscard]] friend constexpr bool operator==(const IpcEndpoint&, const IpcEndpoint&) = default;
};

inline constexpr ObjectId ipc_object_tag = 0xC1C0'0000'0000'0000ULL;
inline constexpr ObjectId ipc_object_tag_mask = 0xFFFF'0000'0000'0000ULL;
inline constexpr ObjectId ipc_object_generation_mask = 0x0000'FFFF'FFFF'0000ULL;
inline constexpr ObjectId ipc_object_slot_mask = 0x0000'0000'0000'FFFFULL;

[[nodiscard]] constexpr ObjectId ipc_object_id(IpcEndpoint endpoint) noexcept {
    if (!endpoint.valid() || endpoint.slot >= max_ipc_endpoints) return invalid_object;
    return ipc_object_tag |
        (static_cast<ObjectId>(endpoint.generation) << 16U) |
        static_cast<ObjectId>(endpoint.slot + 1U);
}

struct IpcEnvelope final {
    std::array<std::byte, max_ipc_inline_bytes> bytes {};
    std::uint8_t size {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return size <= max_ipc_inline_bytes;
    }

    [[nodiscard]] static os::core::Result<IpcEnvelope> from(
        std::span<const std::byte> source) noexcept;

    [[nodiscard]] constexpr std::span<const std::byte> view() const noexcept {
        return std::span<const std::byte>{bytes.data(), size};
    }

    [[nodiscard]] friend constexpr bool operator==(const IpcEnvelope&, const IpcEnvelope&) = default;
};

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
    IpcEnvelope request {};
    [[nodiscard]] constexpr bool valid() const noexcept {
        return caller != invalid_thread && reply.valid() && reply.caller == caller && request.valid();
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
inline constexpr std::uint32_t pending_call_limit = 170U;
inline constexpr std::uint32_t pending_call_missing = 171U;
inline constexpr std::uint32_t payload_too_large = 172U;
inline constexpr std::uint32_t reply_unavailable = 173U;
inline constexpr std::uint32_t receive_already_waiting = 174U;
} // namespace ipc_errors

class IpcEndpointTable final {
public:
    [[nodiscard]] os::core::Result<IpcEndpoint> create(ThreadId server) noexcept;
    [[nodiscard]] os::core::Result<void> retire(
        ThreadId server,
        IpcEndpoint endpoint,
        Rendezvous& rendezvous) noexcept;

    [[nodiscard]] std::size_t retire_all_owned_by(
        ThreadId server,
        Rendezvous& rendezvous) noexcept;

    [[nodiscard]] std::size_t release_thread(
        ThreadId thread,
        Rendezvous& rendezvous) noexcept;

    [[nodiscard]] os::core::Result<void> send(
        ThreadId caller,
        CapabilityId endpoint_capability,
        const CapabilityTable& capabilities,
        Rendezvous& rendezvous,
        IpcEnvelope request = {}) noexcept;

    [[nodiscard]] os::core::Result<IpcReceived> receive(
        ThreadId server,
        CapabilityId endpoint_capability,
        const CapabilityTable& capabilities,
        Rendezvous& rendezvous) noexcept;

    [[nodiscard]] os::core::Result<void> reply(
        ThreadId server,
        const IpcReplySeal& seal,
        Rendezvous& rendezvous,
        IpcEnvelope response = {}) noexcept;

    [[nodiscard]] os::core::Result<void> reply_transaction(
        ThreadId server,
        IpcTransactionId transaction,
        Rendezvous& rendezvous,
        IpcEnvelope response = {}) noexcept;

    [[nodiscard]] os::core::Result<IpcEnvelope> take_reply(ThreadId caller) noexcept;
    [[nodiscard]] bool reply_available(ThreadId caller) const noexcept;

    [[nodiscard]] bool active(IpcEndpoint endpoint) const noexcept;
    [[nodiscard]] bool receive_waiting(IpcEndpoint endpoint) const noexcept;
    [[nodiscard]] std::size_t active_endpoint_count() const noexcept { return active_; }
    [[nodiscard]] std::size_t active_reply_seal_count() const noexcept { return reply_seals_; }
    [[nodiscard]] std::size_t pending_call_count() const noexcept { return pending_calls_; }

private:
    struct EndpointSlot final {
        ThreadId server {invalid_thread};
        IpcEndpointGeneration generation {0U};
        bool active {false};
        bool receive_waiting {false};
    };
    struct ReplySlot final {
        IpcReplySeal seal {};
        bool active {false};
    };
    struct PendingSlot final {
        IpcEndpoint endpoint {};
        ThreadId caller {invalid_thread};
        ThreadId server {invalid_thread};
        IpcEnvelope request {};
        bool active {false};
    };
    struct CompletedSlot final {
        ThreadId caller {invalid_thread};
        IpcEnvelope response {};
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
    [[nodiscard]] ReplySlot* reply_slot(ThreadId server, IpcTransactionId transaction) noexcept;
    [[nodiscard]] PendingSlot* pending_slot(ThreadId caller, IpcEndpoint endpoint) noexcept;
    [[nodiscard]] PendingSlot* pending_slot(IpcEndpoint endpoint) noexcept;
    [[nodiscard]] PendingSlot* free_pending_slot() noexcept;
    [[nodiscard]] CompletedSlot* completed_slot(ThreadId caller) noexcept;
    [[nodiscard]] const CompletedSlot* completed_slot(ThreadId caller) const noexcept;
    [[nodiscard]] CompletedSlot* free_completed_slot() noexcept;
    void invalidate_replies_for(IpcEndpoint endpoint) noexcept;
    void cancel_pending_for(IpcEndpoint endpoint, Rendezvous& rendezvous) noexcept;
    void discard_completed_for(ThreadId caller) noexcept;

    std::array<EndpointSlot, max_ipc_endpoints> endpoints_ {};
    std::array<ReplySlot, max_ipc_reply_seals> replies_ {};
    std::array<PendingSlot, max_ipc_pending_calls> pending_ {};
    std::array<CompletedSlot, max_threads> completed_ {};
    std::size_t active_ {0U};
    std::size_t reply_seals_ {0U};
    std::size_t pending_calls_ {0U};
    IpcTransactionId next_transaction_ {1U};
};

} // namespace os::kernel
