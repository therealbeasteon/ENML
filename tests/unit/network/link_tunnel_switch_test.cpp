#include <os/network/link_tunnel_switch.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::network;

    const LinkTunnelGrant grant{
        .tunnel = TunnelCapability{7U, 4U},
        .budget = PacketBudget{1500U, 2U, 3000U},
    };
    require(grant.valid());

    LinkTunnelSwitch sw{};
    TunnelPacketCapability p1{11U, 7U, 4U, 1200U, true};
    require(sw.enqueue(grant, p1) == LinkTunnelAdmission::accepted);

    auto plaintext = p1;
    plaintext.id = 12U;
    plaintext.encrypted_tunnel_payload = false;
    require(sw.enqueue(grant, plaintext) == LinkTunnelAdmission::plaintext_rejected);

    auto wrong_tunnel = p1;
    wrong_tunnel.id = 13U;
    wrong_tunnel.tunnel_id = 8U;
    require(sw.enqueue(grant, wrong_tunnel) == LinkTunnelAdmission::wrong_tunnel);

    auto stale = p1;
    stale.id = 14U;
    stale.generation = 3U;
    require(sw.enqueue(grant, stale) == LinkTunnelAdmission::stale_generation);

    auto too_large = p1;
    too_large.id = 15U;
    too_large.length = 1501U;
    require(sw.enqueue(grant, too_large) == LinkTunnelAdmission::packet_too_large);

    TunnelPacketCapability p2{16U, 7U, 4U, 1200U, true};
    require(sw.enqueue(grant, p2) == LinkTunnelAdmission::accepted);
    TunnelPacketCapability p3{17U, 7U, 4U, 100U, true};
    require(sw.enqueue(grant, p3) == LinkTunnelAdmission::packet_budget_exhausted);

    require(sw.complete(grant.tunnel, p1));
    require(sw.enqueue(grant, p3) == LinkTunnelAdmission::accepted);

    sw.revoke_generation(4U);
    require(!sw.complete(grant.tunnel, p2));

    return 0;
}
