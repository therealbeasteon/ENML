#include <cstdlib>

#include <os/network/policy.hpp>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::network;

    LinkObservation edge_like{
        .smoothed_rtt_ms = 650U,
        .downlink_kbps = 180U,
        .uplink_kbps = 60U,
        .loss_permille = 20U,
        .metered = true,
        .energy_constrained = true,
    };
    auto constrained = plan_transport(edge_like, PrivacyMode::zero_tracking);
    require(constrained.link_class == LinkClass::constrained);
    require(constrained.max_background_flows == 1U);
    require(constrained.prefer_quic);
    require(constrained.require_encrypted_transport);
    require(constrained.require_ech_when_available);
    require(!constrained.allow_tls_0rtt);
    require(constrained.resolver == ResolverMode::oblivious);
    require(constrained.relay == RelayMode::split_relay);
    require(!constrained.expose_stable_device_identifier);
    require(!constrained.allow_ambient_packet_capture);

    LinkObservation congested_modern{
        .smoothed_rtt_ms = 500U,
        .downlink_kbps = 15'000U,
        .uplink_kbps = 4'000U,
        .loss_permille = 40U,
    };
    require(classify_link(congested_modern) == LinkClass::high_latency);

    LinkObservation fast{
        .smoothed_rtt_ms = 25U,
        .downlink_kbps = 120'000U,
        .uplink_kbps = 40'000U,
    };
    auto broadband = plan_transport(fast, PrivacyMode::protected);
    require(broadband.link_class == LinkClass::broadband);
    require(broadband.max_interactive_flows > constrained.max_interactive_flows);
    require(broadband.require_encrypted_transport);
    require(!broadband.allow_cleartext_fallback);

    return EXIT_SUCCESS;
}
