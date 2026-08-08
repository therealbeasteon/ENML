#pragma once

#include <cstdint>

#include <os/core/error.hpp>
#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/core/strong_id.hpp>
#include <os/ipc/channel.hpp>

namespace os::ipc {

// Resolves kernel-authenticated packet credentials through trusted runtime
// process state. Implementations must reject stale/reused native PIDs.
class PeerIdentityResolver {
public:
    virtual ~PeerIdentityResolver() = default;

    [[nodiscard]] virtual os::core::Result<os::core::PeerIdentity>
    resolve(KernelPeerCredentials credentials) noexcept = 0;
};

struct RequestContext final {
    os::core::PeerIdentity peer {};
    os::core::RequestId request_id {};
};

class ClientConnection final {
public:
    explicit ClientConnection(Channel& channel) noexcept : channel_(&channel) {}

    [[nodiscard]] Channel& channel() const noexcept { return *channel_; }

    // Synchronous M0 call path. The returned payload borrows receive_buffer.
    // Multiplexed/asynchronous calls are intentionally deferred until Task<T>
    // and structured concurrency are implemented.
    [[nodiscard]] os::core::Result<InboundMessage>
    call(
        os::core::ServiceId service_id,
        std::uint32_t operation_id,
        os::core::ByteSpan request_payload,
        os::core::MutableByteSpan receive_buffer) noexcept;

private:
    Channel* channel_ {nullptr};
    std::uint64_t next_request_id_ {1};
};

[[nodiscard]] os::core::Result<void>
send_rpc_response(
    Channel& channel,
    const WireHeaderV1& request_header,
    os::core::ByteSpan response_payload) noexcept;

[[nodiscard]] os::core::Result<void>
send_rpc_error(
    Channel& channel,
    const WireHeaderV1& request_header,
    os::core::Error error) noexcept;

[[nodiscard]] os::core::Result<void>
decode_rpc_error(os::core::ByteSpan payload, os::core::Error& output) noexcept;

[[nodiscard]] os::core::Result<RequestContext>
validate_rpc_request(
    const InboundMessage& message,
    os::core::ServiceId expected_service,
    PeerIdentityResolver& identity_resolver) noexcept;

} // namespace os::ipc
