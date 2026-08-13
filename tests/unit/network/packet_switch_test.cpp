#include <os/network/packet_switch.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::network;

    const FlowCapability flow{
        .id = 8U,
        .generation = 4U,
        .principal = os::core::PrincipalId{1U, 2U},
    };
    const PacketBudget budget{
        .max_packet_bytes = 1500U,
        .max_inflight_packets = 2U,
        .max_inflight_bytes = 2500U,
    };
    const auto grant = driver_packet_grant(flow, budget);
    PacketSwitch packet_switch{};

    const PacketBufferCapability first{1U, 4U, 8U, 1200U};
    const PacketBufferCapability second{2U, 4U, 8U, 1200U};
    const PacketBufferCapability third{3U, 4U, 8U, 100U};

    require(packet_switch.enqueue(grant, first) == PacketAdmission::accepted);
    require(packet_switch.enqueue(grant, second) == PacketAdmission::accepted);
    require(packet_switch.enqueue(grant, third) == PacketAdmission::packet_budget_exhausted);

    auto wrong_flow = third;
    wrong_flow.flow_id = 99U;
    require(packet_switch.enqueue(grant, wrong_flow) == PacketAdmission::wrong_flow);

    auto stale = third;
    stale.generation = 3U;
    require(packet_switch.enqueue(grant, stale) == PacketAdmission::stale_generation);

    auto oversized = third;
    oversized.length = 1600U;
    require(packet_switch.enqueue(grant, oversized) == PacketAdmission::packet_too_large);

    require(packet_switch.complete(flow, first));
    require(packet_switch.enqueue(grant, third) == PacketAdmission::accepted);

    packet_switch.revoke_generation(flow.generation);
    require(!packet_switch.complete(flow, second));
    require(!packet_switch.complete(flow, third));

    return 0;
}
