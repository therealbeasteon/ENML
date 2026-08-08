#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/wire.hpp>

namespace os::ipc {

// KernelPeerCredentials is transport evidence, not an EMNL PrincipalId.
// Higher layers must map it through supervisor-owned process state before
// making authorization decisions.
struct KernelPeerCredentials final {
    std::int64_t process_id {0};
    std::uint32_t user_id {0};
    std::uint32_t group_id {0};

    [[nodiscard]] friend constexpr bool
    operator==(const KernelPeerCredentials&, const KernelPeerCredentials&) = default;
};

class InboundMessage final {
public:
    InboundMessage(
        WireHeaderV1 header,
        os::core::ByteSpan payload,
        std::array<os::core::NativeHandle, max_handle_count>&& handles,
        std::uint16_t handle_count,
        KernelPeerCredentials sender) noexcept;

    InboundMessage(const InboundMessage&) = delete;
    InboundMessage& operator=(const InboundMessage&) = delete;
    InboundMessage(InboundMessage&&) noexcept = default;
    InboundMessage& operator=(InboundMessage&&) noexcept = default;

    [[nodiscard]] const WireHeaderV1& header() const noexcept { return header_; }
    [[nodiscard]] os::core::ByteSpan payload() const noexcept { return payload_; }
    [[nodiscard]] KernelPeerCredentials sender_credentials() const noexcept { return sender_; }

    [[nodiscard]] std::span<const os::core::NativeHandle>
    handles() const noexcept {
        return {handles_.data(), handle_count_};
    }

    [[nodiscard]] std::uint16_t handle_count() const noexcept { return handle_count_; }

    // Infrastructure-level ownership transfer. Generated typed handle codecs
    // will wrap this with type/rights checks later.
    [[nodiscard]] os::core::Result<os::core::NativeHandle>
    take_handle(std::uint16_t index) noexcept;

private:
    WireHeaderV1 header_ {};
    os::core::ByteSpan payload_ {};
    std::array<os::core::NativeHandle, max_handle_count> handles_ {};
    std::uint16_t handle_count_ {0};
    KernelPeerCredentials sender_ {};
};

class Channel final {
public:
    Channel() noexcept = default;
    explicit Channel(os::core::NativeHandle handle) noexcept;

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    Channel(Channel&&) noexcept = default;
    Channel& operator=(Channel&&) noexcept = default;

    [[nodiscard]] static os::core::Result<std::array<Channel, 2>>
    create_local_pair() noexcept;

    [[nodiscard]] static os::core::Result<Channel>
    adopt(os::core::NativeHandle handle) noexcept;

    [[nodiscard]] bool valid() const noexcept { return handle_.valid(); }
    void close() noexcept { handle_.reset(); }

    [[nodiscard]] os::core::Result<void>
    send(
        const WireHeaderV1& header,
        os::core::ByteSpan payload,
        std::span<const os::core::NativeHandle> handles = {}) noexcept;

    // The receive buffer must be at least max_wire_packet_size bytes. Returned
    // payload views borrow this buffer and stay valid only while the caller
    // keeps it unchanged/alive.
    [[nodiscard]] os::core::Result<InboundMessage>
    receive(os::core::MutableByteSpan receive_buffer) noexcept;

    // Connection-level kernel credentials. For socketpairs created before a
    // fork, use sender_credentials() on InboundMessage for the actual sender
    // of each packet.
    [[nodiscard]] os::core::Result<KernelPeerCredentials>
    peer_credentials() const noexcept;

    // Infrastructure escape hatch used by the supervisor/bootstrap code and
    // transport tests. It is not part of the application ABI.
    [[nodiscard]] int native_fd() const noexcept { return handle_.native(); }

private:
    os::core::NativeHandle handle_ {};

    [[nodiscard]] os::core::Result<void>
    enable_kernel_credentials() noexcept;
};

} // namespace os::ipc
