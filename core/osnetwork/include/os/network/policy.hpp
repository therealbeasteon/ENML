#pragma once

#include <cstdint>

namespace os::network {

// Cookie classifies the path by observed properties, never by marketing radio
// generation. A congested 5G path may therefore receive the same conservative
// treatment as EDGE, while a strong legacy link can use a less restrictive
// plan. This keeps policy hardware- and modem-neutral.
enum class LinkClass : std::uint8_t {
    constrained = 1U,
    high_latency = 2U,
    balanced = 3U,
    broadband = 4U,
};

enum class PrivacyMode : std::uint8_t {
    protected = 1U,
    private_relay = 2U,
    zero_tracking = 3U,
};

enum class ResolverMode : std::uint8_t {
    encrypted = 1U,
    oblivious = 2U,
};

enum class RelayMode : std::uint8_t {
    direct = 1U,
    split_relay = 2U,
};

struct LinkObservation final {
    std::uint32_t smoothed_rtt_ms {0U};
    std::uint32_t downlink_kbps {0U};
    std::uint32_t uplink_kbps {0U};
    std::uint16_t loss_permille {0U};
    bool metered {false};
    bool energy_constrained {false};
};

struct TransportPlan final {
    LinkClass link_class {LinkClass::constrained};
    ResolverMode resolver {ResolverMode::encrypted};
    RelayMode relay {RelayMode::direct};
    std::uint8_t max_interactive_flows {2U};
    std::uint8_t max_background_flows {1U};
    bool require_encrypted_transport {true};
    bool require_ech_when_available {true};
    bool allow_cleartext_fallback {false};
    bool prefer_quic {true};
    bool allow_tls_0rtt {false};
    bool expose_stable_device_identifier {false};
    bool allow_ambient_packet_capture {false};
};

[[nodiscard]] constexpr LinkClass classify_link(const LinkObservation& link) noexcept {
    // Loss and RTT are intentionally weighted along with throughput. On a
    // narrow radio link, latency amplification from background concurrency is
    // often more damaging to user-perceived responsiveness than raw bandwidth.
    if (link.downlink_kbps < 256U || link.uplink_kbps < 96U || link.loss_permille >= 80U) {
        return LinkClass::constrained;
    }
    if (link.smoothed_rtt_ms >= 350U || link.loss_permille >= 30U) {
        return LinkClass::high_latency;
    }
    if (link.downlink_kbps < 10'000U || link.uplink_kbps < 2'000U || link.metered ||
        link.energy_constrained) {
        return LinkClass::balanced;
    }
    return LinkClass::broadband;
}

[[nodiscard]] constexpr TransportPlan
plan_transport(const LinkObservation& link, PrivacyMode privacy) noexcept {
    TransportPlan plan{};
    plan.link_class = classify_link(link);

    switch (plan.link_class) {
    case LinkClass::constrained:
        plan.max_interactive_flows = 3U;
        plan.max_background_flows = 1U;
        break;
    case LinkClass::high_latency:
        plan.max_interactive_flows = 4U;
        plan.max_background_flows = 1U;
        break;
    case LinkClass::balanced:
        plan.max_interactive_flows = 6U;
        plan.max_background_flows = 2U;
        break;
    case LinkClass::broadband:
        plan.max_interactive_flows = 12U;
        plan.max_background_flows = 4U;
        break;
    }

    // TLS 0-RTT is not a generic speed switch: replay and weaker forward
    // secrecy make it unsuitable as a default security optimization. A future
    // request layer may opt in only for operations explicitly proven replay-safe.
    plan.allow_tls_0rtt = false;

    if (privacy == PrivacyMode::private_relay || privacy == PrivacyMode::zero_tracking) {
        plan.relay = RelayMode::split_relay;
        plan.resolver = ResolverMode::oblivious;
    }

    if (privacy == PrivacyMode::zero_tracking) {
        // No protocol can hide all traffic analysis from every observer, but
        // Cookie will refuse deliberate stable identifiers and cleartext
        // fallback in its highest privacy mode.
        plan.allow_cleartext_fallback = false;
        plan.expose_stable_device_identifier = false;
        plan.allow_ambient_packet_capture = false;
    }

    return plan;
}

} // namespace os::network
