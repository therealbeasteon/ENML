#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/package/package.hpp>

namespace os::package {

inline constexpr std::uint32_t package_manifest_magic_v1 = 0x314B5045U; // "EPK1" LE
inline constexpr std::uint16_t package_manifest_version_v1 = 1U;
inline constexpr std::uint16_t package_manifest_header_size_v1 = 24U;
inline constexpr std::size_t max_package_manifest_bytes = 64U * 1024U;
inline constexpr std::size_t max_manifest_path_bytes = 256U;
inline constexpr std::size_t max_manifest_path_segment_bytes = 64U;
inline constexpr std::size_t max_manifest_files = 128U;
inline constexpr std::size_t max_requested_permissions = 32U;
inline constexpr std::uint64_t m1_max_declared_package_bytes = 512ULL * 1024ULL * 1024ULL;

namespace manifest_errors {
inline constexpr std::uint32_t truncated = 100U;
inline constexpr std::uint32_t invalid_magic = 101U;
inline constexpr std::uint32_t unsupported_version = 102U;
inline constexpr std::uint32_t invalid_header = 103U;
inline constexpr std::uint32_t invalid_size = 104U;
inline constexpr std::uint32_t invalid_reserved = 105U;
inline constexpr std::uint32_t invalid_path = 106U;
inline constexpr std::uint32_t duplicate_path = 107U;
inline constexpr std::uint32_t invalid_file_flags = 108U;
inline constexpr std::uint32_t invalid_permission = 109U;
inline constexpr std::uint32_t duplicate_permission = 110U;
inline constexpr std::uint32_t missing_entry_point = 111U;
inline constexpr std::uint32_t entry_point_not_executable = 112U;
inline constexpr std::uint32_t declared_content_too_large = 113U;
inline constexpr std::uint32_t trailing_data = 114U;
} // namespace manifest_errors

enum class ManifestFileFlags : std::uint16_t {
    none = 0U,
    executable = 1U << 0U,
};

inline constexpr std::uint16_t manifest_file_known_flags =
    static_cast<std::uint16_t>(ManifestFileFlags::executable);

class ManifestPath final {
public:
    ManifestPath() noexcept = default;

    [[nodiscard]] static os::core::Result<ManifestPath>
    parse(std::string_view text) noexcept;

    [[nodiscard]] std::string_view view() const noexcept {
        return {storage_.data(), size_};
    }

    [[nodiscard]] bool valid() const noexcept { return size_ != 0U; }

    [[nodiscard]] friend bool operator==(const ManifestPath& lhs, const ManifestPath& rhs) noexcept {
        return lhs.view() == rhs.view();
    }

private:
    std::array<char, max_manifest_path_bytes> storage_ {};
    std::uint16_t size_ {0U};
};

struct ManifestFileRecord final {
    ManifestPath path {};
    std::uint64_t declared_size {0U};
    std::uint16_t flags {0U};

    [[nodiscard]] bool executable() const noexcept {
        return (flags & static_cast<std::uint16_t>(ManifestFileFlags::executable)) != 0U;
    }
};

struct AnalyzedPackageManifest final {
    PackageId package_id {};
    ManifestPath entry_point {};
    std::array<ManifestFileRecord, max_manifest_files> files {};
    std::array<std::uint32_t, max_requested_permissions> requested_permissions {};
    std::size_t file_count {0U};
    std::size_t permission_count {0U};
    std::uint64_t declared_content_bytes {0U};

    [[nodiscard]] const ManifestFileRecord& file(std::size_t index) const noexcept;
    [[nodiscard]] std::uint32_t requested_permission(std::size_t index) const noexcept;
};

class PackageManifestAnalyzer final {
public:
    [[nodiscard]] static os::core::Result<AnalyzedPackageManifest>
    analyze(os::core::ByteSpan bytes) noexcept;
};

} // namespace os::package
