#include <os/network/broker.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::network;

    ConnectionBroker broker{};
    const os::core::PeerIdentity app{
        .principal = os::core::PrincipalId{0x11U, 0x22U},
        .user = os::core::UserId{1U},
        .process = os::core::ProcessId{9U},
    };
    const os::core::PeerIdentity other{
        .principal = os::core::PrincipalId{0x33U, 0x44U},
        .user = os::core::UserId{1U},
        .process = os::core::ProcessId{10U},
    };
    const LinkObservation edge_like{
        .smoothed_rtt_ms = 520U,
        .downlink_kbps = 180U,
        .uplink_kbps = 70U,
        .loss_permille = 25U,
        .metered = true,
        .energy_constrained = true,
    };
    const ConnectionRequest request{
        .destination = DestinationCapability{77U, 2U},
        .privacy = PrivacyMode::zero_tracking,
        .priority = FlowPriority::background,
        .require_encryption = true,
    };

    auto denied = broker.request_flow(app, false, request, edge_like);
    require(!denied.allowed && denied.refusal == BrokerRefusal::permission_denied);

    auto cleartext = request;
    cleartext.require_encryption = false;
    denied = broker.request_flow(app, true, cleartext, edge_like);
    require(!denied.allowed && denied.refusal == BrokerRefusal::invalid_request);

    const auto first = broker.request_flow(app, true, request, edge_like);
    require(first.allowed);
    require(first.lease.capability.valid());
    require(first.lease.plan.require_encrypted_transport);
    require(first.lease.plan.relay == RelayMode::split_relay);
    require(first.lease.plan.resolver == ResolverMode::oblivious);

    // Constrained links allow only one background flow per principal.
    denied = broker.request_flow(app, true, request, edge_like);
    require(!denied.allowed && denied.refusal == BrokerRefusal::flow_budget_exhausted);
    // Another principal has an independent budget.
    require(broker.request_flow(other, true, request, edge_like).allowed);

    // A different principal cannot release or steal a flow capability.
    require(broker.release_flow(other.principal, first.lease.capability) == BrokerRefusal::wrong_principal);

    const auto old_generation = broker.generation();
    broker.revoke_all_for_restart();
    require(broker.generation() == old_generation + 1U);
    require(broker.release_flow(app.principal, first.lease.capability) == BrokerRefusal::stale_flow);

    // A new broker generation may issue fresh authority; old authority remains dead.
    const auto fresh = broker.request_flow(app, true, request, edge_like);
    require(fresh.allowed);
    require(fresh.lease.capability.generation == broker.generation());
    require(fresh.lease.capability.id != first.lease.capability.id);

    return 0;
}
