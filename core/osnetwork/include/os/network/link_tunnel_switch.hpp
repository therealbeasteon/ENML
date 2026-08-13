#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/network/tunnel_authority.hpp>

namespace os::network {

struct TunnelPacketCapability final {
    std::uint64_t id {0U};
    std::uint64_t tunnel_id {0U};
    std::uint64_t generation {0U};
    std::uint32_t length {0U};
    bool encrypted_tunnel_payload {false};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id != 0U && tunnel_id != 0U && generation != 0U &&
               length != 0U && encrypted_tunnel_payload;
    }
};

enum class LinkTunnelAdmission : std::uint8_t {
    accepted = 0U,
    invalid_grant = 1U,
    invalid_packet = 2U,
    wrong_tunnel = 3U,
    stale_generation = 4U,
    plaintext_rejected = 5U,
    packet_too_large = 6U,
    packet_budget_exhausted = 7U,
    byte_budget_exhausted = 8U,
    capacity_exhausted = 9U,
};

// Final bounded queue before a physical link driver. This type cannot represent
// application identity, DNS names, DestinationCapability or direct peer
// metadata. Ordinary Internet payloads must already be encrypted tunnel bytes.
class LinkTunnelSwitch final {
public:
    static constexpr std::size_t max_queued_packets = 128U;

    [[nodiscard]] LinkTunnelAdmission enqueue(
        const LinkTunnelGrant& grant,
        TunnelPacketCapability packet) noexcept {
        if (!grant.valid()) return LinkTunnelAdmission::invalid_grant;
        if (packet.id == 0U || packet.tunnel_id == 0U || packet.generation == 0U || packet.length == 0U) {
            return LinkTunnelAdmission::invalid_packet;
        }
        if (!packet.encrypted_tunnel_payload) return LinkTunnelAdmission::plaintext_rejected;
        if (packet.tunnel_id != grant.tunnel.id) return LinkTunnelAdmission::wrong_tunnel;
        if (packet.generation != grant.tunnel.generation) return LinkTunnelAdmission::stale_generation;
        if (packet.length > grant.budget.max_packet_bytes) return LinkTunnelAdmission::packet_too_large;

        const auto usage = usage_for(grant.tunnel);
        if (usage.packets >= grant.budget.max_inflight_packets) {
            return LinkTunnelAdmission::packet_budget_exhausted;
        }
        if (usage.bytes > grant.budget.max_inflight_bytes - packet.length) {
            return LinkTunnelAdmission::byte_budget_exhausted;
        }

        for (auto& slot : slots_) {
            if (!slot.occupied) {
                slot = Slot{true, grant.tunnel, packet};
                return LinkTunnelAdmission::accepted;
            }
        }
        return LinkTunnelAdmission::capacity_exhausted;
    }

    [[nodiscard]] bool complete(
        TunnelCapability tunnel,
        TunnelPacketCapability packet) noexcept {
        for (auto& slot : slots_) {
            if (!slot.occupied || slot.packet.id != packet.id) continue;
            if (slot.tunnel.id != tunnel.id || slot.tunnel.generation != tunnel.generation ||
                slot.packet.tunnel_id != tunnel.id || slot.packet.generation != tunnel.generation) {
                return false;
            }
            slot = {};
            return true;
        }
        return false;
    }

    void revoke_generation(std::uint64_t generation) noexcept {
        for (auto& slot : slots_) {
            if (slot.occupied && slot.tunnel.generation == generation) slot = {};
        }
    }

private:
    struct Slot final {
        bool occupied {false};
        TunnelCapability tunnel {};
        TunnelPacketCapability packet {};
    };

    struct Usage final {
        std::uint16_t packets {0U};
        std::uint32_t bytes {0U};
    };

    std::array<Slot, max_queued_packets> slots_ {};

    [[nodiscard]] Usage usage_for(TunnelCapability tunnel) const noexcept {
        Usage usage{};
        for (const auto& slot : slots_) {
            if (!slot.occupied || slot.tunnel.id != tunnel.id ||
                slot.tunnel.generation != tunnel.generation) continue;
            ++usage.packets;
            usage.bytes += slot.packet.length;
        }
        return usage;
    }
};

} // namespace os::network
