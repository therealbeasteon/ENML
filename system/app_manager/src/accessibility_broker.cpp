#include <os/app/manager.hpp>

#include <cstdint>
#include <utility>

#include <os/core/error.hpp>

namespace os::app {
namespace {

[[nodiscard]] constexpr os::core::Error manager_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

} // namespace

os::core::Result<BrokeredAccessibilityEndpoint>
ApplicationManager::take_accessibility_endpoint(
    os::core::PeerIdentity caller,
    os::core::PeerIdentity target) noexcept {
    // Authorize the privileged caller before looking up the requested target so
    // an ordinary process cannot use error differences to probe which app
    // identities currently have accessibility sessions pending.
    if (!os::core::valid_peer_identity(caller) ||
        caller.principal != os::accessibility::accessibility_service_principal) {
        return manager_error(manager_errors::accessibility_authority_denied);
    }
    if (!os::core::valid_peer_identity(target)) {
        return manager_error(manager_errors::accessibility_target_not_found);
    }

    InstanceSlot* slot = nullptr;
    for (auto& candidate : instances_) {
        if (candidate.occupied && candidate.info.valid() && candidate.info.identity == target) {
            slot = &candidate;
            break;
        }
    }
    if (slot == nullptr) {
        return manager_error(manager_errors::accessibility_target_not_found);
    }
    if (slot->accessibility_session_id == 0U ||
        !slot->accessibility_service_endpoint.valid()) {
        return manager_error(manager_errors::accessibility_endpoint_unavailable);
    }

    BrokeredAccessibilityEndpoint endpoint{};
    endpoint.session_id = slot->accessibility_session_id;
    endpoint.application = slot->info.identity;
    endpoint.channel = std::move(slot->accessibility_service_endpoint);

    // One-shot claim: once the trusted service owns the endpoint App Manager no
    // longer retains a duplicate capability. A replacement requires the exact
    // application runtime to request a fresh pair/session over its authenticated
    // runtime session.
    slot->accessibility_session_id = 0U;
    if (!endpoint.valid()) {
        return manager_error(manager_errors::accessibility_endpoint_unavailable);
    }
    return endpoint;
}

} // namespace os::app
