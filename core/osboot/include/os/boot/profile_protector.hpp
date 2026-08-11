#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/boot/sealing.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/core/strong_id.hpp>

namespace os::boot {

// Persistent profile protector records contain only public policy metadata plus
// an opaque provider-owned wrapped/sealed key reference. They never contain a
// plaintext profile root or a standalone password verifier.
inline constexpr std::uint32_t profile_protector_magic = 0x4B525043U; // "CPRK" LE
inline constexpr std::uint16_t profile_protector_version = 1U;
inline constexpr std::uint16_t profile_protector_header_bytes = 80U;
inline constexpr std::size_t max_profile_protector_blob_bytes = 256U;
inline constexpr std::size_t max_profile_protector_record_bytes =
    static_cast<std::size_t>(profile_protector_header_bytes) + max_profile_protector_blob_bytes;

struct CredentialGateSlotId final {
    std::uint64_t value {0U};
    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }
    [[nodiscard]] friend constexpr auto operator<=>(
        const CredentialGateSlotId&,
        const CredentialGateSlotId&) = default;
};

struct ProtectorGeneration final {
    std::uint64_t value {0U};
    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }
    [[nodiscard]] friend constexpr auto operator<=>(
        const ProtectorGeneration&,
        const ProtectorGeneration&) = default;
};

struct SecurityEpochValue final {
    std::uint64_t value {0U};
    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }
    [[nodiscard]] friend constexpr auto operator<=>(
        const SecurityEpochValue&,
        const SecurityEpochValue&) = default;
};

struct ProfileProtectorHeaderV1 final {
    os::core::UserId user {};
    SecurityEpochValue security_epoch {};
    MeasurementDigest boot_measurement {};
    CredentialGateSlotId gate_slot {};
    ProtectorGeneration generation {};
    std::uint32_t provider_blob_size {0U};
};

struct ProfileProtectorRecordView final {
    ProfileProtectorHeaderV1 header {};
    os::core::ByteSpan authenticated_header {};
    os::core::ByteSpan provider_blob {};
};

namespace profile_protector_errors {
inline constexpr std::uint32_t invalid_header = 1U;
inline constexpr std::uint32_t unsupported_version = 2U;
inline constexpr std::uint32_t invalid_user = 3U;
inline constexpr std::uint32_t invalid_epoch = 4U;
inline constexpr std::uint32_t degenerate_measurement = 5U;
inline constexpr std::uint32_t invalid_gate_slot = 6U;
inline constexpr std::uint32_t invalid_generation = 7U;
inline constexpr std::uint32_t invalid_blob_size = 8U;
inline constexpr std::uint32_t buffer_too_small = 9U;
} // namespace profile_protector_errors

[[nodiscard]] constexpr os::core::Error profile_protector_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::boot, 0x580U + code);
}

[[nodiscard]] bool valid_profile_protector_header(
    const ProfileProtectorHeaderV1& header) noexcept;

// Encodes the canonical authenticated binding header. The opaque provider blob
// is appended by the caller/provider and MUST be authenticated by that provider
// against the exact bytes returned here.
[[nodiscard]] os::core::Result<std::size_t>
encode_profile_protector_header_v1(
    const ProfileProtectorHeaderV1& header,
    os::core::MutableByteSpan output) noexcept;

[[nodiscard]] os::core::Result<ProfileProtectorRecordView>
parse_profile_protector_record_v1(os::core::ByteSpan record) noexcept;

} // namespace os::boot
