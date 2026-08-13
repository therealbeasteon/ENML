#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <os/core/identity.hpp>
#include <os/supervisor/subsystem_lease.hpp>

namespace os::supervisor {

enum class LeaseKind : std::uint8_t {
    interactive = 1U,
    background = 2U,
    system = 3U,
};

struct SubsystemLeaseToken final {
    std::uint64_t id {0U};
    std::uint64_t generation {0U};
    SubsystemDomain domain {SubsystemDomain::network};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id != 0U && generation != 0U;
    }
};

class SubsystemLeaseAuthority final {
public:
    static constexpr std::size_t max_leases = 32U;

    explicit constexpr SubsystemLeaseAuthority(SubsystemDomain domain) noexcept
        : domain_(domain) {}

    [[nodiscard]] constexpr std::optional<SubsystemLeaseToken>
    acquire(os::core::PrincipalId principal, LeaseKind kind) noexcept {
        if (!os::core::valid_principal(principal)) return std::nullopt;

        for (auto& slot : slots_) {
            if (slot.in_use) continue;
            const auto id = next_id_++;
            if (id == 0U) return std::nullopt;
            slot = Slot{
                .in_use = true,
                .id = id,
                .generation = generation_,
                .principal = principal,
                .kind = kind,
            };
            return SubsystemLeaseToken{.id = id, .generation = generation_, .domain = domain_};
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr bool
    release(os::core::PrincipalId principal, SubsystemLeaseToken token) noexcept {
        if (!valid_for_domain(token) || !os::core::valid_principal(principal)) return false;
        for (auto& slot : slots_) {
            if (!slot.in_use || slot.id != token.id) continue;
            if (slot.generation != token.generation || slot.principal != principal) return false;
            slot = {};
            return true;
        }
        return false;
    }

    [[nodiscard]] constexpr bool active(SubsystemLeaseToken token) const noexcept {
        if (!valid_for_domain(token)) return false;
        for (const auto& slot : slots_) {
            if (slot.in_use && slot.id == token.id && slot.generation == token.generation) return true;
        }
        return false;
    }

    [[nodiscard]] constexpr LeaseCount counts() const noexcept {
        LeaseCount result{};
        for (const auto& slot : slots_) {
            if (!slot.in_use || slot.generation != generation_) continue;
            switch (slot.kind) {
            case LeaseKind::interactive: ++result.interactive; break;
            case LeaseKind::background: ++result.background; break;
            case LeaseKind::system: ++result.system; break;
            }
        }
        return result;
    }

    // Called only after the quiesce transaction has revoked external authority.
    // Clearing the table and advancing generation guarantees every old lease is
    // stale when the subsystem later starts again.
    constexpr void advance_generation_after_quiesce() noexcept {
        for (auto& slot : slots_) slot = {};
        ++generation_;
        if (generation_ == 0U) generation_ = 1U;
    }

    [[nodiscard]] constexpr std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] constexpr SubsystemDomain domain() const noexcept { return domain_; }

private:
    struct Slot final {
        bool in_use {false};
        std::uint64_t id {0U};
        std::uint64_t generation {0U};
        os::core::PrincipalId principal {};
        LeaseKind kind {LeaseKind::interactive};
    };

    [[nodiscard]] constexpr bool valid_for_domain(SubsystemLeaseToken token) const noexcept {
        return token.valid() && token.domain == domain_ && token.generation == generation_;
    }

    SubsystemDomain domain_;
    std::uint64_t generation_ {1U};
    std::uint64_t next_id_ {1U};
    std::array<Slot, max_leases> slots_ {};
};

} // namespace os::supervisor
