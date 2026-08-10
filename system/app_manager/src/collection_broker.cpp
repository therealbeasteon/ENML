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

os::core::Result<BrokeredCollectionEndpoint>
ApplicationManager::take_collection_endpoint(
    os::core::PeerIdentity caller,
    os::core::PeerIdentity target,
    std::uint64_t session_id) noexcept {
    // Privileged caller authorization precedes target/session lookup. An
    // ordinary process therefore cannot use the error surface as an oracle for
    // which applications currently own unclaimed collection sessions.
    if (!os::core::valid_peer_identity(caller) ||
        caller.principal != os::collection::collection_consumer_principal) {
        return manager_error(manager_errors::collection_authority_denied);
    }
    if (!os::core::valid_peer_identity(target) || session_id == 0U) {
        return manager_error(manager_errors::collection_endpoint_unavailable);
    }

    InstanceSlot* application = nullptr;
    for (auto& candidate : instances_) {
        if (candidate.occupied && candidate.info.valid() &&
            candidate.info.identity == target) {
            application = &candidate;
            break;
        }
    }
    if (application == nullptr) {
        return manager_error(manager_errors::collection_endpoint_unavailable);
    }

    CollectionEndpointSlot* session = nullptr;
    for (auto& candidate : application->collection_endpoints) {
        if (candidate.session_id == session_id && candidate.consumer_endpoint.valid()) {
            session = &candidate;
            break;
        }
    }
    if (session == nullptr) {
        return manager_error(manager_errors::collection_endpoint_unavailable);
    }

    BrokeredCollectionEndpoint endpoint{};
    endpoint.session_id = session->session_id;
    endpoint.application = application->info.identity;
    endpoint.channel = std::move(session->consumer_endpoint);

    // One-shot transfer. App Manager retains no duplicate capability after a
    // successful claim; application death/reset closes every still-unclaimed
    // slot automatically through InstanceSlot ownership.
    session->session_id = 0U;
    if (!endpoint.valid()) {
        return manager_error(manager_errors::collection_endpoint_unavailable);
    }
    return endpoint;
}

} // namespace os::app
