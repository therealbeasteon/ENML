#pragma once

#include <compare>
#include <cstdint>

#include <os/core/strong_id.hpp>

namespace os::keys {

struct KeyId final {
    std::uint64_t high {0U};
    std::uint64_t low {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return high != 0U || low != 0U;
    }

    [[nodiscard]] friend constexpr auto operator<=>(const KeyId&, const KeyId&) = default;
};

struct ProviderKeyReference final {
    std::uint64_t value {0U};

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }
    [[nodiscard]] friend constexpr auto operator<=>(
        const ProviderKeyReference&,
        const ProviderKeyReference&) = default;
};

enum class KeyPurpose : std::uint32_t {
    application_data_aead = 1U,
};

using RightsMask = std::uint32_t;

namespace key_rights {
inline constexpr RightsMask metadata = 1U << 0U;
inline constexpr RightsMask encrypt = 1U << 1U;
inline constexpr RightsMask decrypt = 1U << 2U;
inline constexpr RightsMask destroy = 1U << 3U;
inline constexpr RightsMask rotate = 1U << 4U;
inline constexpr RightsMask all = metadata | encrypt | decrypt | destroy | rotate;
} // namespace key_rights

struct KeyDescriptor final {
    KeyId id {};
    std::uint32_t version {0U};
    KeyPurpose purpose {KeyPurpose::application_data_aead};
    RightsMask rights {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id.valid() && version != 0U && rights != 0U &&
            (rights & ~key_rights::all) == 0U;
    }

    [[nodiscard]] friend constexpr auto operator<=>(
        const KeyDescriptor&,
        const KeyDescriptor&) = default;
};

struct KeyOwner final {
    os::core::PrincipalId principal {};
    os::core::UserId user {};

    [[nodiscard]] friend constexpr auto operator<=>(const KeyOwner&, const KeyOwner&) = default;
};

[[nodiscard]] constexpr bool valid_purpose(KeyPurpose purpose) noexcept {
    return purpose == KeyPurpose::application_data_aead;
}

[[nodiscard]] constexpr bool valid_rights(RightsMask value) noexcept {
    return value != 0U && (value & ~key_rights::all) == 0U;
}

} // namespace os::keys
