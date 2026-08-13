#include <os/network/blindness.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::network;

    const auto maximum = plan_network_blindness(BlindnessMode::maximum);
    require(maximum.force_tunnel_for_internet);
    require(maximum.require_split_relay);
    require(maximum.require_oblivious_dns);
    require(maximum.require_ech_when_available);
    require(maximum.require_unlinkable_relay_authorization);
    require(!maximum.allow_stable_device_identifier);
    require(!maximum.allow_ambient_packet_capture);

    require(link_layer_grant_is_blind(true, false, false, maximum));
    require(!link_layer_grant_is_blind(false, false, false, maximum));
    require(!link_layer_grant_is_blind(true, true, false, maximum));
    require(!link_layer_grant_is_blind(true, false, true, maximum));

    const UnavoidableLinkFacts facts{};
    require(facts.attachment_visible);
    require(facts.timing_visible);
    require(facts.aggregate_volume_visible);

    return 0;
}
