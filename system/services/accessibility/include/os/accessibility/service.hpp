#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/accessibility/broker.hpp>
#include <os/accessibility/transport.hpp>
#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>

namespace os::accessibility {

// Supervisor-owned administration endpoint for the trusted accessibility
// service. It is not registered into the application ServiceBroker in M3.2.
inline constexpr os::core::ServiceId accessibility_service_id{0x0000F014U};
inline constexpr std::uint32_t accessibility_service_op_claim = 1U;
inline constexpr std::uint32_t accessibility_service_op_snapshot = 2U;
inline constexpr std::uint32_t accessibility_service_op_action = 3U;
inline constexpr std::uint32_t accessibility_service_op_release = 4U;

inline constexpr std::size_t accessibility_service_identity_request_size_v1 = 32U;
inline constexpr std::size_t accessibility_service_claim_response_size_v1 = 40U;
inline constexpr std::size_t accessibility_service_action_request_size_v1 = 46U;
inline constexpr std::size_t max_accessibility_service_sessions = 16U;

namespace service_errors {
inline constexpr std::uint32_t session_capacity = 200U;
inline constexpr std::uint32_t duplicate_session = 201U;
inline constexpr std::uint32_t unknown_session = 202U;
inline constexpr std::uint32_t malformed_request = 203U;
} // namespace service_errors

struct AccessibilityServiceActionRequest final {
    os::core::PeerIdentity application {};
    std::uint64_t snapshot_revision {0U};
    os::ui::UiNodeId target {};
    os::ui::UiAction action {os::ui::UiAction::activate};
};

class AccessibilityServiceRuntime final {
public:
    AccessibilityServiceRuntime(
        os::ipc::Channel& broker_channel,
        os::core::PeerIdentity self) noexcept
        : broker_(broker_channel), self_(self) {}

    [[nodiscard]] bool valid() const noexcept {
        return os::core::valid_peer_identity(self_) &&
            self_.principal == accessibility_service_principal;
    }

    [[nodiscard]] os::core::Result<AccessibilityBrokerClaimMetadata> claim(
        os::core::PeerIdentity application,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<void> snapshot(
        os::core::PeerIdentity application,
        os::ui::AccessibilitySessionSnapshot& output,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<os::ui::UiEvent> dispatch_action(
        const AccessibilityServiceActionRequest& request,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<void> release(
        os::core::PeerIdentity application) noexcept;

    [[nodiscard]] std::size_t session_count() const noexcept;

private:
    struct SessionSlot final {
        bool occupied {false};
        std::uint64_t session_id {0U};
        os::core::PeerIdentity application {};
        os::ipc::Channel channel {};
    };

    [[nodiscard]] SessionSlot* find(os::core::PeerIdentity application) noexcept;
    [[nodiscard]] const SessionSlot* find(os::core::PeerIdentity application) const noexcept;

    AccessibilityBrokerClient broker_;
    os::core::PeerIdentity self_ {};
    std::array<SessionSlot, max_accessibility_service_sessions> sessions_ {};
};

// Private supervisor-side client. The endpoint is an unforgeable capability
// returned by Supervisor::connect(); M3.2 intentionally does not register this
// service id in the application ServiceBroker.
class AccessibilityServiceClient final {
public:
    explicit AccessibilityServiceClient(os::ipc::Channel& channel) noexcept
        : connection_(channel) {}

    [[nodiscard]] os::core::Result<AccessibilityBrokerClaimMetadata> claim(
        os::core::PeerIdentity application,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<void> snapshot(
        os::core::PeerIdentity application,
        os::ui::AccessibilitySessionSnapshot& output,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<os::ui::UiEvent> dispatch_action(
        const AccessibilityServiceActionRequest& request,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<void> release(
        os::core::PeerIdentity application,
        os::core::MutableByteSpan scratch) noexcept;

private:
    os::ipc::ClientConnection connection_;
};

class AccessibilityServiceServer final {
public:
    explicit AccessibilityServiceServer(AccessibilityServiceRuntime& runtime) noexcept
        : runtime_(&runtime) {}

    [[nodiscard]] bool valid() const noexcept {
        return runtime_ != nullptr && runtime_->valid();
    }

    [[nodiscard]] os::core::Result<void> dispatch_once(
        os::ipc::Channel& channel,
        os::core::MutableByteSpan scratch) noexcept;

private:
    AccessibilityServiceRuntime* runtime_ {nullptr};
};

} // namespace os::accessibility
