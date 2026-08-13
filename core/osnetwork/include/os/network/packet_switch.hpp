#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/network/capability.hpp>

namespace os::network {

struct PacketBufferCapability final {
    std::uint64_t id {0U};
    std::uint64_t generation {0U};
    std::uint64_t flow_id {0U};
    std::uint32_t length {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id != 0U && generation != 0U && flow_id != 0U && length != 0U;
    }
};

enum class PacketAdmission : std::uint8_t {
    accepted = 0U,
    invalid_grant = 1U,
    invalid_buffer = 2U,
    wrong_flow = 3U,
    stale_generation = 4U,
    packet_too_large = 5U,
    packet_budget_exhausted = 6U,
    byte_budget_exhausted = 7U,
    switch_capacity_exhausted = 8U,
};

// The packet switch tracks only opaque flow/buffer identities and byte counts.
// It does not store hostnames, application identities, filesystem handles, or
// payload copies. Actual buffer memory belongs to the future Cookie memory/DMA
// service and is transferred by kernel capability rather than raw pointer.
class PacketSwitch final {
public:
    static constexpr std::size_t max_queued_packets = 128U;

    [[nodiscard]] PacketAdmission enqueue(
        const PacketGrant& grant,
        PacketBufferCapability buffer) noexcept {
        if (!grant.valid()) return PacketAdmission::invalid_grant;
        if (!buffer.valid()) return PacketAdmission::invalid_buffer;
        if (buffer.flow_id != grant.flow.id) return PacketAdmission::wrong_flow;
        if (buffer.generation != grant.flow.generation) return PacketAdmission::stale_generation;
        if (buffer.length > grant.budget.max_packet_bytes) return PacketAdmission::packet_too_large;

        const auto usage = usage_for(grant.flow);
        if (usage.packets >= grant.budget.max_inflight_packets) {
            return PacketAdmission::packet_budget_exhausted;
        }
        if (usage.bytes > grant.budget.max_inflight_bytes - buffer.length) {
            return PacketAdmission::byte_budget_exhausted;
        }

        for (auto& slot : slots_) {
            if (!slot.occupied) {
                slot.occupied = true;
                slot.flow = grant.flow;
                slot.buffer = buffer;
                return PacketAdmission::accepted;
            }
        }
        return PacketAdmission::switch_capacity_exhausted;
    }

    [[nodiscard]] bool complete(
        FlowCapability flow,
        PacketBufferCapability buffer) noexcept {
        for (auto& slot : slots_) {
            if (!slot.occupied || slot.buffer.id != buffer.id) continue;
            if (slot.flow.id != flow.id || slot.flow.generation != flow.generation ||
                slot.buffer.generation != buffer.generation) {
                return false;
            }
            slot = {};
            return true;
        }
        return false;
    }

    void revoke_generation(std::uint64_t generation) noexcept {
        for (auto& slot : slots_) {
            if (slot.occupied && slot.flow.generation == generation) slot = {};
        }
    }

private:
    struct Slot final {
        bool occupied {false};
        FlowCapability flow {};
        PacketBufferCapability buffer {};
    };

    struct Usage final {
        std::uint16_t packets {0U};
        std::uint32_t bytes {0U};
    };

    std::array<Slot, max_queued_packets> slots_ {};

    [[nodiscard]] Usage usage_for(FlowCapability flow) const noexcept {
        Usage usage{};
        for (const auto& slot : slots_) {
            if (!slot.occupied || slot.flow.id != flow.id ||
                slot.flow.generation != flow.generation) continue;
            ++usage.packets;
            usage.bytes += slot.buffer.length;
        }
        return usage;
    }
};

} // namespace os::network
