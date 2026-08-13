#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/identity.hpp>
#include <os/network/capability.hpp>
#include <os/network/policy.hpp>

namespace os::network {

// A destination is deliberately opaque to the broker's lower layers. The
// resolver/privacy domain mints this capability after resolving a service name;
// packet drivers never receive the original DNS name or application identity.
struct DestinationCapability final {
    std::uint64_t id {0U};
    std::uint64_t generation {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id != 0U && generation != 0U;
    }
};

enum class FlowPriority : std::uint8_t {
    interactive = 1U,
    background = 2U,
};

enum class BrokerRefusal : std::uint8_t {
    none = 0U,
    invalid_identity = 1U,
    permission_denied = 2U,
    invalid_destination = 3U,
    invalid_request = 4U,
    flow_budget_exhausted = 5U,
    capacity_exhausted = 6U,
    stale_flow = 7U,
    wrong_principal = 8U,
};

struct ConnectionRequest final {
    DestinationCapability destination {};
    PrivacyMode privacy {PrivacyMode::protected_transport};
    FlowPriority priority {FlowPriority::interactive};
    bool require_encryption {true};
};

struct FlowLease final {
    FlowCapability capability {};
    DestinationCapability destination {};
    TransportPlan plan {};
    FlowPriority priority {FlowPriority::interactive};
};

struct BrokerDecision final {
    bool allowed {false};
    BrokerRefusal refusal {BrokerRefusal::invalid_request};
    FlowLease lease {};
};

class ConnectionBroker final {
public:
    static constexpr std::size_t max_flows = 64U;

    [[nodiscard]] BrokerDecision request_flow(
        const os::core::PeerIdentity& caller,
        bool package_network_allowed,
        const ConnectionRequest& request,
        const LinkObservation& link) noexcept {
        if (!os::core::valid_peer_identity(caller)) {
            return refuse(BrokerRefusal::invalid_identity);
        }
        if (!package_network_allowed) {
            return refuse(BrokerRefusal::permission_denied);
        }
        if (!request.destination.valid()) {
            return refuse(BrokerRefusal::invalid_destination);
        }
        // Cookie's broker never issues a cleartext flow. An application cannot
        // opt out of transport confidentiality by asking for weaker policy.
        if (!request.require_encryption) {
            return refuse(BrokerRefusal::invalid_request);
        }

        const auto plan = plan_transport(link, request.privacy);
        const auto active = active_for(caller.principal, request.priority);
        const std::size_t allowed = request.priority == FlowPriority::interactive
            ? static_cast<std::size_t>(plan.max_interactive_flows)
            : static_cast<std::size_t>(plan.max_background_flows);
        if (active >= allowed) {
            return refuse(BrokerRefusal::flow_budget_exhausted);
        }

        Entry* slot = nullptr;
        for (auto& entry : entries_) {
            if (!entry.occupied) {
                slot = &entry;
                break;
            }
        }
        if (slot == nullptr) {
            return refuse(BrokerRefusal::capacity_exhausted);
        }

        const FlowCapability capability{
            .id = next_flow_id_++,
            .generation = generation_,
            .principal = caller.principal,
        };
        if (!capability.valid()) {
            return refuse(BrokerRefusal::capacity_exhausted);
        }

        slot->occupied = true;
        slot->lease = FlowLease{
            .capability = capability,
            .destination = request.destination,
            .plan = plan,
            .priority = request.priority,
        };
        return BrokerDecision{true, BrokerRefusal::none, slot->lease};
    }

    [[nodiscard]] BrokerRefusal release_flow(
        os::core::PrincipalId caller,
        FlowCapability capability) noexcept {
        for (auto& entry : entries_) {
            if (!entry.occupied || entry.lease.capability.id != capability.id) {
                continue;
            }
            if (entry.lease.capability.generation != generation_ ||
                capability.generation != generation_) {
                return BrokerRefusal::stale_flow;
            }
            if (entry.lease.capability.principal != caller || capability.principal != caller) {
                return BrokerRefusal::wrong_principal;
            }
            entry = {};
            return BrokerRefusal::none;
        }
        return BrokerRefusal::stale_flow;
    }

    // Recovery/restart revokes every outstanding authority from the previous
    // broker generation. IDs are not reused, and a generation wrap fails closed
    // by leaving generation zero, which makes newly formed capabilities invalid.
    void revoke_all_for_restart() noexcept {
        for (auto& entry : entries_) entry = {};
        ++generation_;
        if (generation_ == 0U) generation_ = 0U;
    }

    [[nodiscard]] constexpr std::uint64_t generation() const noexcept { return generation_; }

private:
    struct Entry final {
        bool occupied {false};
        FlowLease lease {};
    };

    std::array<Entry, max_flows> entries_ {};
    std::uint64_t generation_ {1U};
    std::uint64_t next_flow_id_ {1U};

    [[nodiscard]] static constexpr BrokerDecision refuse(BrokerRefusal reason) noexcept {
        return BrokerDecision{false, reason, {}};
    }

    [[nodiscard]] std::size_t active_for(
        os::core::PrincipalId principal,
        FlowPriority priority) const noexcept {
        std::size_t count = 0U;
        for (const auto& entry : entries_) {
            if (entry.occupied && entry.lease.capability.principal == principal &&
                entry.lease.priority == priority &&
                entry.lease.capability.generation == generation_) {
                ++count;
            }
        }
        return count;
    }
};

} // namespace os::network
