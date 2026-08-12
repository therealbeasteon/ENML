#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

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

// Power is not data authority. Cookie may accept electrical power without any
// authenticated grant, but every data-capable role requires a fresh grant tied
// to the current lock generation. Re-locking advances the generation and makes
// all prior grants stale even if the cable/peer remains physically present.
[[nodiscard]] constexpr bool requires_user_authentication(
    ConnectionPurpose purpose) noexcept {
    return purpose != ConnectionPurpose::charge_only;
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

        if (!proof.valid() || proof.lock_generation != current_lock_generation) return {};
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

    [[nodiscard]] constexpr std::size_t active_grants() const noexcept { return active_; }
    [[nodiscard]] constexpr std::uint64_t generation() const noexcept { return generation_; }

private:
    std::uint64_t next_id_ {0ULL};
    std::uint64_t generation_ {1ULL};
    std::size_t active_ {0U};
};

} // namespace os::supervisor
