#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>

namespace os::accessibility {

// Private App Manager control namespace used only by the supervised
// accessibility service. Knowledge of this numeric id is not authority: the
// App Manager server authenticates SCM_CREDENTIALS against trusted runtime
// identity before target parsing or capability lookup.
inline constexpr os::core::ServiceId accessibility_broker_control_service_id{0x0000F015U};
inline constexpr std::uint32_t accessibility_broker_operation_claim = 1U;
inline constexpr std::size_t accessibility_broker_claim_request_size_v1 = 32U;
inline constexpr std::size_t accessibility_broker_claim_response_size_v1 = 40U;

struct AccessibilityBrokerClaimMetadata final {
    std::uint64_t session_id {0U};
    os::core::PeerIdentity application {};

    [[nodiscard]] bool valid() const noexcept {
        return session_id != 0U && os::core::valid_peer_identity(application);
    }
};

// Move-only service-side capability for one exact application accessibility
// session. App Manager transfers exactly one channel with the fixed metadata;
// the supervised accessibility service never learns an application socket path
// or accepts a descriptor from the target application's payload.
struct BrokeredApplicationSession final {
    std::uint64_t session_id {0U};
    os::core::PeerIdentity application {};
    os::ipc::Channel channel {};

    BrokeredApplicationSession() noexcept = default;
    BrokeredApplicationSession(const BrokeredApplicationSession&) = delete;
    BrokeredApplicationSession& operator=(const BrokeredApplicationSession&) = delete;
    BrokeredApplicationSession(BrokeredApplicationSession&&) noexcept = default;
    BrokeredApplicationSession& operator=(BrokeredApplicationSession&&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept {
        return session_id != 0U && os::core::valid_peer_identity(application) && channel.valid();
    }
};

[[nodiscard]] os::core::Result<std::size_t> encode_broker_claim_request_v1(
    os::core::PeerIdentity target,
    os::core::MutableByteSpan output) noexcept;

[[nodiscard]] os::core::Result<os::core::PeerIdentity> decode_broker_claim_request_v1(
    os::core::ByteSpan payload) noexcept;

[[nodiscard]] os::core::Result<std::size_t> encode_broker_claim_response_v1(
    AccessibilityBrokerClaimMetadata metadata,
    os::core::MutableByteSpan output) noexcept;

[[nodiscard]] os::core::Result<AccessibilityBrokerClaimMetadata>
decode_broker_claim_response_v1(os::core::ByteSpan payload) noexcept;

class AccessibilityBrokerClient final {
public:
    explicit AccessibilityBrokerClient(os::ipc::Channel& channel) noexcept
        : connection_(channel) {}

    // Caller identity is intentionally absent. The server derives authority
    // from packet credentials and its trusted identity registry.
    [[nodiscard]] os::core::Result<BrokeredApplicationSession> claim(
        os::core::PeerIdentity application,
        os::core::MutableByteSpan scratch) noexcept;

private:
    os::ipc::ClientConnection connection_;
};

} // namespace os::accessibility
