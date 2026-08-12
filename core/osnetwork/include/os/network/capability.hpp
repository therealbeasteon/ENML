#pragma once

#include <cstdint>

#include <os/core/identity.hpp>

namespace os::network {

// Network authority is per-flow and generation-bound. A driver or transport
// service receives this opaque authority rather than ambient visibility into a
// process, profile, filesystem, or the rest of the network stack.
struct FlowCapability final {
    std::uint64_t id {0U};
    std::uint64_t generation {0U};
    os::core::PrincipalId principal {};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id != 0U && generation != 0U && os::core::valid_principal(principal);
    }
};

struct PacketBudget final {
    std::uint32_t max_packet_bytes {0U};
    std::uint16_t max_inflight_packets {0U};
    std::uint32_t max_inflight_bytes {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return max_packet_bytes >= 576U && max_packet_bytes <= 65'535U &&
               max_inflight_packets != 0U && max_inflight_packets <= 256U &&
               max_inflight_bytes >= max_packet_bytes && max_inflight_bytes <= 4U * 1024U * 1024U;
    }
};

struct PacketGrant final {
    FlowCapability flow {};
    PacketBudget budget {};
    bool may_transmit {false};
    bool may_receive {false};
    bool may_observe_peer_address {false};
    bool may_observe_app_identity {false};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return flow.valid() && budget.valid() && (may_transmit || may_receive);
    }
};

// Hardware/link drivers should receive grants produced with this helper. They
// can move bounded packets but do not learn the owning app identity or remote
// destination from Cookie authority metadata.
[[nodiscard]] constexpr PacketGrant driver_packet_grant(
    FlowCapability flow,
    PacketBudget budget) noexcept {
    return PacketGrant{
        .flow = flow,
        .budget = budget,
        .may_transmit = true,
        .may_receive = true,
        .may_observe_peer_address = false,
        .may_observe_app_identity = false,
    };
}

} // namespace os::network
