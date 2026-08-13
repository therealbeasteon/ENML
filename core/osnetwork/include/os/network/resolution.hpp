#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/identity.hpp>
#include <os/network/policy.hpp>

namespace os::network {

// Presented to the resolver instead of PeerIdentity. In the real Cookie IPC
// port this becomes a kernel capability; the wire-visible value itself carries
// no PrincipalId/UserId/ProcessId. The broker retains the authoritative owner
// mapping privately for revocation/accounting.
struct ResolutionGrant final {
    std::uint64_t id {0U};
    std::uint64_t generation {0U};
    PrivacyMode privacy {PrivacyMode::protected_transport};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id != 0U && generation != 0U;
    }
};

enum class ResolutionRefusal : std::uint8_t {
    none = 0U,
    invalid_identity = 1U,
    permission_denied = 2U,
    capacity_exhausted = 3U,
    stale_grant = 4U,
    wrong_principal = 5U,
};

struct ResolutionDecision final {
    bool allowed {false};
    ResolutionRefusal refusal {ResolutionRefusal::invalid_identity};
    ResolutionGrant grant {};
};

class ResolutionAuthority final {
public:
    static constexpr std::size_t max_grants = 32U;

    [[nodiscard]] ResolutionDecision issue(
        const os::core::PeerIdentity& caller,
        bool package_network_allowed,
        PrivacyMode privacy) noexcept {
        if (!os::core::valid_peer_identity(caller)) {
            return {false, ResolutionRefusal::invalid_identity, {}};
        }
        if (!package_network_allowed) {
            return {false, ResolutionRefusal::permission_denied, {}};
        }
        Entry* slot = nullptr;
        for (auto& entry : entries_) {
            if (!entry.occupied) {
                slot = &entry;
                break;
            }
        }
        if (slot == nullptr || next_id_ == 0U || generation_ == 0U) {
            return {false, ResolutionRefusal::capacity_exhausted, {}};
        }

        const ResolutionGrant grant{next_id_++, generation_, privacy};
        slot->occupied = true;
        slot->owner = caller.principal;
        slot->grant = grant;
        return {true, ResolutionRefusal::none, grant};
    }

    [[nodiscard]] ResolutionRefusal consume(
        os::core::PrincipalId caller,
        ResolutionGrant grant) noexcept {
        for (auto& entry : entries_) {
            if (!entry.occupied || entry.grant.id != grant.id) continue;
            if (entry.grant.generation != generation_ || grant.generation != generation_) {
                return ResolutionRefusal::stale_grant;
            }
            if (entry.owner != caller) {
                return ResolutionRefusal::wrong_principal;
            }
            // One-shot by default: DNS/service-name lookup grants are not long-
            // lived identity cookies and cannot silently accumulate history.
            entry = {};
            return ResolutionRefusal::none;
        }
        return ResolutionRefusal::stale_grant;
    }

    void revoke_all_for_restart() noexcept {
        for (auto& entry : entries_) entry = {};
        ++generation_;
    }

private:
    struct Entry final {
        bool occupied {false};
        os::core::PrincipalId owner {};
        ResolutionGrant grant {};
    };

    std::array<Entry, max_grants> entries_ {};
    std::uint64_t generation_ {1U};
    std::uint64_t next_id_ {1U};
};

} // namespace os::network
