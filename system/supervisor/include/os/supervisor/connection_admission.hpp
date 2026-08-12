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

// External data authority must also keep exactly one attack-surface domain
// alive. Charging is intentionally absent: accepting power never acquires a USB
// data lease. NFC gets its own future controller domain rather than being
// mislabeled as sensors, so it currently cannot mint a data grant below.
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

// Power is not data authority. Cookie may accept electrical power without any
// authenticated grant, but every data-capable role requires a fresh knowledge-
// factor proof tied to the current lock generation. Re-locking advances the
// generation and makes all prior grants stale even if the cable/peer remains.
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
            // Charging never needs to mint a data-capable authority.
            return ConnectionGrant{
                .id = ++next_id_,
                .generation = generation_,
                .lock_generation = current_lock_generation,
                .transport = transport,
                .purpose = purpose,
                .data_signaling = false,
            };
        }

        // First Cookie regime deliberately requires a PIN/password for every
        // *new external data relationship*. Bluetooth bonds and physical
        // attachment are identity/proximity facts, not current user consent.
        if (!proof.valid() || proof.lock_generation != current_lock_generation) return {};
        if (!connection_subsystem_domain(transport).has_value()) return {};
        if (active_ >= max_active_grants) return {};

        ++active_;
        return ConnectionGrant{
            .id = ++next_id_,
            .generation = generation_,
            .lock_generation = current_lock_generation,
            .transport = transport,
            .purpose = purpose,
            .data_signaling = true,
        };
    }

    // Locking, duress, or explicit "disconnect external data" revokes every
    // outstanding grant in O(1): consumers validate the generation before use.
    constexpr void revoke_all() noexcept {
        ++generation_;
        if (generation_ == 0ULL) generation_ = 1ULL;
        active_ = 0U;
    }

    [[nodiscard]] constexpr bool active(
        const ConnectionGrant& grant,
        std::uint64_t current_lock_generation) const noexcept {
        return grant.valid() && grant.generation == generation_ &&
               grant.lock_generation == current_lock_generation;
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
    std::uint64_t next_id_ {0ULL};
    std::uint64_t generation_ {1ULL};
    std::size_t active_ {0U};
};

} // namespace os::supervisor
