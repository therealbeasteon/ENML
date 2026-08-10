#pragma once

#include <cstddef>
#include <cstdint>

#include <os/app/manager.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>

namespace os::app {

// Private App Manager control namespace used only by the supervised
// accessibility service. It is intentionally separate from application runtime
// sessions: an application cannot turn knowledge of this numeric ServiceId into
// authority because every request is resolved from kernel packet credentials.
inline constexpr os::core::ServiceId accessibility_broker_control_service_id{0x0000F015U};
inline constexpr std::uint32_t accessibility_broker_operation_claim = 1U;
inline constexpr std::size_t accessibility_broker_claim_request_size_v1 = 32U;
inline constexpr std::size_t accessibility_broker_claim_response_size_v1 = 40U;

using AccessibilityEndpointClaimFn = os::core::Result<BrokeredAccessibilityEndpoint> (*)(
    void* context,
    os::core::PeerIdentity caller,
    os::core::PeerIdentity target) noexcept;

// Internal composition seam. Function pointers never cross IPC; production
// binds this to ApplicationManager::take_accessibility_endpoint(), while focused
// transport tests can exercise authentication without constructing a full app
// lifecycle fixture.
struct AccessibilityEndpointBrokerBackend final {
    void* context {nullptr};
    AccessibilityEndpointClaimFn claim {nullptr};
};

[[nodiscard]] AccessibilityEndpointBrokerBackend accessibility_endpoint_backend(
    ApplicationManager& manager) noexcept;

class AccessibilityBrokerControlServer final {
public:
    AccessibilityBrokerControlServer(
        AccessibilityEndpointBrokerBackend backend,
        os::ipc::PeerIdentityResolver& identity_resolver) noexcept
        : backend_(backend), identity_resolver_(&identity_resolver) {}

    [[nodiscard]] bool valid() const noexcept {
        return backend_.claim != nullptr && identity_resolver_ != nullptr;
    }

    // Handles one request. validate_rpc_request() resolves SCM_CREDENTIALS
    // through trusted runtime identity before target payload parsing. Only the
    // canonical accessibility principal can reach the endpoint claim callback.
    [[nodiscard]] os::core::Result<void> dispatch_once(
        os::ipc::Channel& channel,
        os::core::MutableByteSpan scratch) noexcept;

private:
    AccessibilityEndpointBrokerBackend backend_ {};
    os::ipc::PeerIdentityResolver* identity_resolver_ {nullptr};
};

class AccessibilityBrokerControlClient final {
public:
    explicit AccessibilityBrokerControlClient(os::ipc::Channel& channel) noexcept
        : connection_(channel) {}

    // The caller names only the exact application PeerIdentity it wants to
    // inspect. Caller authority is never serialized; the server derives it from
    // the packet sender and trusted identity registry.
    [[nodiscard]] os::core::Result<BrokeredAccessibilityEndpoint> claim(
        os::core::PeerIdentity application,
        os::core::MutableByteSpan scratch) noexcept;

private:
    os::ipc::ClientConnection connection_;
};

} // namespace os::app
