#include <os/network/tunnel_authority.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::network;

    TunnelAuthority authority{};
    const FlowCapability flow{
        .id = 11U,
        .generation = authority.generation(),
        .principal = os::core::PrincipalId{0xAAU, 0xBBU},
    };
    const PacketBudget budget{
        .max_packet_bytes = 1500U,
        .max_inflight_packets = 16U,
        .max_inflight_bytes = 24'000U,
    };

    const auto issued = authority.issue(flow, budget);
    require(issued.result == TunnelAuthorityResult::ok);
    require(issued.grant.valid());
    require(authority.owns_flow(issued.grant.tunnel, flow));

    auto wrong_principal = flow;
    wrong_principal.principal = os::core::PrincipalId{0xCCU, 0xDDU};
    require(!authority.owns_flow(issued.grant.tunnel, wrong_principal));

    const auto old_tunnel = issued.grant.tunnel;
    authority.revoke_all_for_restart();
    require(!authority.owns_flow(old_tunnel, flow));
    require(authority.retire(old_tunnel) == TunnelAuthorityResult::stale_generation);

    auto fresh_flow = flow;
    fresh_flow.generation = authority.generation();
    const auto fresh = authority.issue(fresh_flow, budget);
    require(fresh.result == TunnelAuthorityResult::ok);
    require(fresh.grant.tunnel.generation == authority.generation());
    require(fresh.grant.tunnel.id != old_tunnel.id);

    auto bad_budget = budget;
    bad_budget.max_inflight_packets = 0U;
    require(authority.issue(fresh_flow, bad_budget).result == TunnelAuthorityResult::invalid_budget);

    require(authority.retire(fresh.grant.tunnel) == TunnelAuthorityResult::ok);
    require(!authority.owns_flow(fresh.grant.tunnel, fresh_flow));

    return 0;
}
