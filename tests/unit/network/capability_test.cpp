#include <os/network/capability.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::network;

    const FlowCapability flow{
        .id = 7U,
        .generation = 3U,
        .principal = os::core::PrincipalId{42U},
    };
    const PacketBudget budget{
        .max_packet_bytes = 1500U,
        .max_inflight_packets = 32U,
        .max_inflight_bytes = 48'000U,
    };

    const auto grant = driver_packet_grant(flow, budget);
    require(grant.valid());
    require(grant.may_transmit && grant.may_receive);
    require(!grant.may_observe_peer_address);
    require(!grant.may_observe_app_identity);

    auto stale = flow;
    stale.generation = 0U;
    require(!stale.valid());

    auto unbounded = budget;
    unbounded.max_inflight_packets = 0U;
    require(!unbounded.valid());

    unbounded = budget;
    unbounded.max_inflight_bytes = 8U * 1024U * 1024U;
    require(!unbounded.valid());

    return 0;
}
