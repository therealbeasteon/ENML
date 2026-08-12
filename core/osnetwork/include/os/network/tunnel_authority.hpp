#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/network/capability.hpp>

namespace os::network {

// The physical link sees only this opaque tunnel identity. It deliberately
// contains no PrincipalId, UserId, ProcessId, hostname, DestinationCapability,
// or direct peer address.
struct TunnelCapability final {
    std::uint64_t id {0U};
    std::uint64_t generation {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id != 0U && generation != 0U;
    }
};

struct LinkTunnelGrant final {
    TunnelCapability tunnel {};
    PacketBudget budget {};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return tunnel.valid() && budget.valid();
    }
};

enum class TunnelAuthorityResult : std::uint8_t {
    ok = 0U,
    invalid_flow = 1U,
    invalid_budget = 2U,
    capacity_exhausted = 3U,
    stale_generation = 4U,
    unknown_tunnel = 5U,
};

struct TunnelIssueDecision final {
    TunnelAuthorityResult result {TunnelAuthorityResult::invalid_flow};
    LinkTunnelGrant grant {};
};

// Trusted network-domain authority. The association between an app-owned flow
// and the link-visible tunnel is kept here and never copied into LinkTunnelGrant.
// A future Cookie Kernel capability transfer will make these IDs unforgeable;
// this bounded table defines the authority/lifetime contract today.
class TunnelAuthority final {
public:
    static constexpr std::size_t max_tunnels = 32U;

    [[nodiscard]] TunnelIssueDecision issue(
        FlowCapability flow,
        PacketBudget budget) noexcept {
        if (!flow.valid() || flow.generation != generation_) {
            return {TunnelAuthorityResult::invalid_flow, {}};
        }
        if (!budget.valid()) {
            return {TunnelAuthorityResult::invalid_budget, {}};
        }
        for (auto& slot : slots_) {
            if (!slot.occupied) {
                const TunnelCapability tunnel{next_tunnel_id_++, generation_};
                if (!tunnel.valid()) {
                    return {TunnelAuthorityResult::capacity_exhausted, {}};
                }
                slot = Entry{true, flow, tunnel};
                return {TunnelAuthorityResult::ok, LinkTunnelGrant{tunnel, budget}};
            }
        }
        return {TunnelAuthorityResult::capacity_exhausted, {}};
    }

    [[nodiscard]] bool owns_flow(TunnelCapability tunnel, FlowCapability flow) const noexcept {
        if (!tunnel.valid() || !flow.valid() || tunnel.generation != generation_ ||
            flow.generation != generation_) {
            return false;
        }
        for (const auto& slot : slots_) {
            if (slot.occupied && slot.tunnel == tunnel && slot.flow == flow) return true;
        }
        return false;
    }

    [[nodiscard]] TunnelAuthorityResult retire(TunnelCapability tunnel) noexcept {
        if (!tunnel.valid() || tunnel.generation != generation_) {
            return TunnelAuthorityResult::stale_generation;
        }
        for (auto& slot : slots_) {
            if (slot.occupied && slot.tunnel == tunnel) {
                slot = {};
                return TunnelAuthorityResult::ok;
            }
        }
        return TunnelAuthorityResult::unknown_tunnel;
    }

    // Network-domain restart destroys every prior tunnel association. The link
    // must reacquire fresh authority before transmitting again.
    void revoke_all_for_restart() noexcept {
        for (auto& slot : slots_) slot = {};
        ++generation_;
        if (generation_ == 0U) generation_ = 0U;
    }

    [[nodiscard]] constexpr std::uint64_t generation() const noexcept { return generation_; }

private:
    struct Entry final {
        bool occupied {false};
        FlowCapability flow {};
        TunnelCapability tunnel {};
    };

    std::array<Entry, max_tunnels> slots_ {};
    std::uint64_t generation_ {1U};
    std::uint64_t next_tunnel_id_ {1U};
};

} // namespace os::network
