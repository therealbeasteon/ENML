#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/core/result.hpp>
#include <os/core/strong_id.hpp>

namespace os::package {

inline constexpr std::size_t max_package_id_bytes = 96U;
inline constexpr std::size_t signer_lineage_id_bytes = 32U;
inline constexpr std::size_t content_digest_bytes = 32U;

namespace errors {
inline constexpr std::uint32_t invalid_package_id = 1U;
inline constexpr std::uint32_t invalid_signer_lineage = 2U;
inline constexpr std::uint32_t invalid_content_digest = 3U;
inline constexpr std::uint32_t invalid_generation = 4U;
inline constexpr std::uint32_t package_id_collision = 5U;
inline constexpr std::uint32_t generation_conflict = 6U;
inline constexpr std::uint32_t generation_capacity = 7U;
inline constexpr std::uint32_t registry_full = 8U;
inline constexpr std::uint32_t unknown_package = 9U;
inline constexpr std::uint32_t unknown_generation = 10U;
inline constexpr std::uint32_t no_active_generation = 11U;
} // namespace errors

class PackageId final {
public:
    PackageId() noexcept = default;

    [[nodiscard]] static os::core::Result<PackageId>
    parse(std::string_view text) noexcept;

    [[nodiscard]] std::string_view view() const noexcept {
        return {storage_.data(), size_};
    }

    [[nodiscard]] bool valid() const noexcept { return size_ != 0U; }

    [[nodiscard]] friend bool operator==(const PackageId& lhs, const PackageId& rhs) noexcept {
        return lhs.view() == rhs.view();
    }

private:
    std::array<char, max_package_id_bytes> storage_ {};
    std::uint16_t size_ {0};
};

struct SignerLineageId final {
    // Opaque verifier-issued signer lineage identity. M1.0 deliberately does
    // not choose a signature algorithm or trust signer data from a package.
    std::array<std::byte, signer_lineage_id_bytes> bytes {};

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] friend constexpr bool
    operator==(const SignerLineageId&, const SignerLineageId&) = default;
};

struct ContentDigest final {
    // Opaque verified content identity. Crypto-suite selection belongs to the
    // trusted verifier/security layer rather than this semantic registry.
    std::array<std::byte, content_digest_bytes> bytes {};

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] friend constexpr bool
    operator==(const ContentDigest&, const ContentDigest&) = default;
};

struct ApplicationIdentity final {
    PackageId package_id {};
    SignerLineageId signer_lineage {};

    [[nodiscard]] bool valid() const noexcept {
        return package_id.valid() && signer_lineage.valid();
    }

    [[nodiscard]] friend bool
    operator==(const ApplicationIdentity& lhs, const ApplicationIdentity& rhs) noexcept {
        return lhs.package_id == rhs.package_id && lhs.signer_lineage == rhs.signer_lineage;
    }
};

struct PackageGenerationIdTag;
using PackageGenerationId = os::core::StrongId<PackageGenerationIdTag, std::uint64_t>;

struct PackageGenerationRecord final {
    ApplicationIdentity application {};
    PackageGenerationId generation {};
    ContentDigest content {};

    [[nodiscard]] bool valid() const noexcept {
        return application.valid() && generation.value() != 0U && content.valid();
    }

    [[nodiscard]] friend bool
    operator==(const PackageGenerationRecord& lhs, const PackageGenerationRecord& rhs) noexcept {
        return lhs.application == rhs.application &&
            lhs.generation == rhs.generation && lhs.content == rhs.content;
    }
};

} // namespace os::package
