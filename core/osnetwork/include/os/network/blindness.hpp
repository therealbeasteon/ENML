#pragma once

#include <cstdint>

namespace os::network {

enum class BlindnessMode : std::uint8_t {
    standard_encrypted = 1U,
    split_relay = 2U,
    maximum = 3U,
};

struct NetworkBlindnessPlan final {
    bool force_tunnel_for_internet {true};
    bool require_split_relay {false};
    bool require_oblivious_dns {true};
    bool require_ech_when_available {true};
    bool allow_direct_destination_exposure_to_link {false};
    bool allow_app_identity_below_broker {false};
    bool allow_dns_name_below_resolver {false};
    bool allow_stable_device_identifier {false};
    bool allow_ambient_packet_capture {false};
    bool require_unlinkable_relay_authorization {false};
    bool permit_direct_emergency_telephony {true};
};

[[nodiscard]] constexpr NetworkBlindnessPlan
plan_network_blindness(BlindnessMode mode) noexcept {
    NetworkBlindnessPlan plan{};

    switch (mode) {
    case BlindnessMode::standard_encrypted:
        break;
    case BlindnessMode::split_relay:
        plan.require_split_relay = true;
        break;
    case BlindnessMode::maximum:
        plan.require_split_relay = true;
        plan.require_unlinkable_relay_authorization = true;
        break;
    }

    return plan;
}

// Kernel/device-facing admission check. Link drivers and packet-switch code may
// only receive already-encrypted tunnel packets for ordinary Internet traffic.
// Destination-bearing or app-identity-bearing packet authority is rejected.
[[nodiscard]] constexpr bool
link_layer_grant_is_blind(
    bool packet_is_tunnel_ciphertext,
    bool grant_contains_app_identity,
    bool grant_contains_destination_metadata,
    const NetworkBlindnessPlan& plan) noexcept {
    if (plan.force_tunnel_for_internet && !packet_is_tunnel_ciphertext) {
        return false;
    }
    if (!plan.allow_app_identity_below_broker && grant_contains_app_identity) {
        return false;
    }
    if (!plan.allow_direct_destination_exposure_to_link && grant_contains_destination_metadata) {
        return false;
    }
    return true;
}

// Honest boundary: the OS cannot truthfully claim that the serving AP/carrier
// cannot observe attachment, timing or aggregate byte counts needed to carry
// traffic. "Network blindness" refers to Cookie state and higher-layer metadata,
// not to eliminating unavoidable radio/link-layer facts.
struct UnavoidableLinkFacts final {
    bool attachment_visible {true};
    bool timing_visible {true};
    bool aggregate_volume_visible {true};
};

} // namespace os::network
