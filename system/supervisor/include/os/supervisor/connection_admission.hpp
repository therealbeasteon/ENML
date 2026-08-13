#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <os/supervisor/subsystem_lease.hpp>

namespace os::supervisor {

enum class ConnectionTransport : std::uint8_t {
    usb = 1U,
    thunderbolt_like = 2U,
    bluetooth = 3U,
    wifi_direct = 4U,
    nfc_accessory = 5U,
    debug_link = 6U,
};

enum class ConnectionPurpose : std::uint8_t {
    charge_only = 1U,
    audio = 2U,
    display = 3U,
    file_transfer = 4U,
    network_tether = 5U,
    accessory_control = 6U,
    debug = 7U,
};

enum class ConnectionRisk : std::uint8_t {
    no_data = 1U,
    data = 2U,
    privileged_data = 3U,
};

struct UserPresenceProof final {
    std::uint64_t authentication_generation {0ULL};
    std::uint64_t lock_generation {0ULL};
    bool password_or_pin_verified {false};
    bool duress_authentication {false};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return authentication_generation != 0ULL && lock_generation != 0ULL &&
               password_or_pin_verified && !duress_authentication;
    }
};

struct ConnectionGrant final {
    std::uint64_t id {0ULL};
    std::uint64_t generation {0ULL};
    std::uint64_t lock_generation {0ULL};
    ConnectionTransport transport {ConnectionTransport::usb};
    ConnectionPurpose purpose {ConnectionPurpose::charge_only};
    bool data_signaling {false};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id != 0ULL && generation != 0ULL && lock_generation != 0ULL;
    }
};

[[nodiscard]] constexpr ConnectionRisk connection_risk(ConnectionPurpose purpose) noexcept {
    switch (purpose) {
    case ConnectionPurpose::charge_only:
        return ConnectionRisk::no_data;
    case ConnectionPurpose::audio:
    case ConnectionPurpose::display:
        return ConnectionRisk::data;
    case ConnectionPurpose::file_transfer:
    case ConnectionPurpose::network_tether:
    case ConnectionPurpose::accessory_control:
    case ConnectionPurpose::debug:
        return ConnectionRisk::privileged_data;
    }
    return ConnectionRisk::privileged_data;
}

[[nodiscard]] constexpr std::optional<SubsystemDomain>
connection_subsystem_domain(ConnectionTransport transport) noexcept {
    switch (transport) {
    case ConnectionTransport::usb:
    case ConnectionTransport::thunderbolt_like:
    case ConnectionTransport::debug_link:
        return SubsystemDomain::usb_data;
    case ConnectionTransport::bluetooth:
        return SubsystemDomain::bluetooth;
    case ConnectionTransport::wifi_direct:
        return SubsystemDomain::network;
    case ConnectionTransport::nfc_accessory:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr bool requires_user_authentication(
    ConnectionPurpose purpose) noexcept {
    return connection_risk(purpose) != ConnectionRisk::no_data;
}

[[nodiscard]] constexpr bool grant_authorizes(
    const ConnectionGrant& grant,
    ConnectionTransport transport,
    ConnectionPurpose purpose,
    std::uint64_t current_lock_generation) noexcept {
    if (!grant.valid()) return false;
    if (grant.lock_generation != current_lock_generation) return false;
    if (grant.transport != transport || grant.purpose != purpose) return false;
    if (purpose == ConnectionPurpose::charge_only) return !grant.data_signaling;
    return grant.data_signaling;
}

class ConnectionAdmissionAuthority final {
public:
    static constexpr std::size_t max_active_grants = 16U;

    [[nodiscard]] constexpr ConnectionGrant admit(
        ConnectionTransport transport,
        ConnectionPurpose purpose,
        const UserPresenceProof& proof,
        std::uint64_t current_lock_generation) noexcept {
        if (current_lock_generation == 0ULL) return {};

        if (purpose == ConnectionPurpose::charge_only) {
            return ConnectionGrant{
                .id = next_nonzero_id(),
                .generation = generation_,
                .lock_generation = current_lock_generation,
                .transport = transport,
                .purpose = purpose,
                .data_signaling = false,
            };
        }

        if (!proof.valid() || proof.lock_generation != current_lock_generation) return {};
        if (!connection_subsystem_domain(transport).has_value()) return {};

        for (auto& slot : slots_) {
            if (slot.in_use) continue;
            const ConnectionGrant grant{
                .id = next_nonzero_id(),
                .generation = generation_,
                .lock_generation = current_lock_generation,
                .transport = transport,
                .purpose = purpose,
                .data_signaling = true,
            };
            if (!grant.valid()) return {};
            slot = Slot{.in_use = true, .grant = grant};
            ++active_;
            return grant;
        }
        return {};
    }

    // Roll back or close one data relationship without disturbing unrelated
    // sessions. Charge-only grants are deliberately not table entries because
    // accepting power never creates data attack surface.
    [[nodiscard]] constexpr bool release(ConnectionGrant grant) noexcept {
        if (!grant.valid() || !grant.data_signaling || grant.generation != generation_) return false;
        for (auto& slot : slots_) {
            if (!slot.in_use || slot.grant.id != grant.id) continue;
            if (slot.grant.generation != grant.generation ||
                slot.grant.lock_generation != grant.lock_generation ||
                slot.grant.transport != grant.transport ||
                slot.grant.purpose != grant.purpose) return false;
            slot = {};
            --active_;
            return true;
        }
        return false;
    }

    constexpr void revoke_all() noexcept {
        for (auto& slot : slots_) slot = {};
        ++generation_;
        if (generation_ == 0ULL) generation_ = 1ULL;
        active_ = 0U;
    }

    [[nodiscard]] constexpr bool active(
        const ConnectionGrant& grant,
        std::uint64_t current_lock_generation) const noexcept {
        if (!grant.valid() || grant.generation != generation_ ||
            grant.lock_generation != current_lock_generation) return false;
        if (!grant.data_signaling) return grant.purpose == ConnectionPurpose::charge_only;
        for (const auto& slot : slots_) {
            if (slot.in_use && slot.grant.id == grant.id && slot.grant.generation == grant.generation) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr std::optional<SubsystemDomain>
    required_domain(const ConnectionGrant& grant,
                    std::uint64_t current_lock_generation) const noexcept {
        if (!active(grant, current_lock_generation) || !grant.data_signaling) {
            return std::nullopt;
        }
        return connection_subsystem_domain(grant.transport);
    }

    [[nodiscard]] constexpr std::size_t active_grants() const noexcept { return active_; }
    [[nodiscard]] constexpr std::uint64_t generation() const noexcept { return generation_; }

private:
    struct Slot final {
        bool in_use {false};
        ConnectionGrant grant {};
    };

    [[nodiscard]] constexpr std::uint64_t next_nonzero_id() noexcept {
        ++next_id_;
        if (next_id_ == 0ULL) ++next_id_;
        return next_id_;
    }

    std::uint64_t next_id_ {0ULL};
    std::uint64_t generation_ {1ULL};
    std::array<Slot, max_active_grants> slots_ {};
    std::size_t active_ {0U};
};

} // namespace os::supervisor
