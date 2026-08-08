#include <os/package/analyzer.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>

namespace os::package {
namespace {

[[nodiscard]] constexpr os::core::Error manifest_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::package, code);
}

[[nodiscard]] constexpr bool ascii_alpha(char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

[[nodiscard]] constexpr bool ascii_digit(char value) noexcept {
    return value >= '0' && value <= '9';
}

[[nodiscard]] constexpr bool path_character_allowed(char value) noexcept {
    return ascii_alpha(value) || ascii_digit(value) || value == '.' || value == '-' || value == '_';
}

class Reader final {
public:
    explicit Reader(os::core::ByteSpan bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] bool read_u16(std::uint16_t& out) noexcept {
        if (remaining() < 2U) return false;
        const auto offset = cursor_;
        out = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes_[offset])) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes_[offset + 1U])) << 8U);
        cursor_ += 2U;
        return true;
    }

    [[nodiscard]] bool read_u32(std::uint32_t& out) noexcept {
        if (remaining() < 4U) return false;
        const auto offset = cursor_;
        out = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes_[offset])) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes_[offset + 1U])) << 8U) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes_[offset + 2U])) << 16U) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes_[offset + 3U])) << 24U);
        cursor_ += 4U;
        return true;
    }

    [[nodiscard]] bool read_u64(std::uint64_t& out) noexcept {
        if (remaining() < 8U) return false;
        const auto offset = cursor_;
        out = 0U;
        for (std::size_t index = 0U; index < 8U; ++index) {
            out |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes_[offset + index]))
                << static_cast<unsigned>(index * 8U);
        }
        cursor_ += 8U;
        return true;
    }

    [[nodiscard]] bool read_text(std::size_t size, std::string_view& out) noexcept {
        if (remaining() < size) return false;
        const auto* data = reinterpret_cast<const char*>(bytes_.data() + cursor_);
        out = std::string_view(data, size);
        cursor_ += size;
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - cursor_;
    }

    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }

private:
    os::core::ByteSpan bytes_ {};
    std::size_t cursor_ {0U};
};

} // namespace

os::core::Result<ManifestPath> ManifestPath::parse(std::string_view text) noexcept {
    if (text.empty() || text.size() > max_manifest_path_bytes ||
        text.front() == '/' || text.back() == '/') {
        return manifest_error(manifest_errors::invalid_path);
    }

    std::size_t segment_begin = 0U;
    for (std::size_t index = 0U; index <= text.size(); ++index) {
        if (index == text.size() || text[index] == '/') {
            const auto segment_size = index - segment_begin;
            if (segment_size == 0U || segment_size > max_manifest_path_segment_bytes) {
                return manifest_error(manifest_errors::invalid_path);
            }

            const auto segment = text.substr(segment_begin, segment_size);
            if (segment == "." || segment == "..") {
                return manifest_error(manifest_errors::invalid_path);
            }

            segment_begin = index + 1U;
            continue;
        }

        if (!path_character_allowed(text[index])) {
            return manifest_error(manifest_errors::invalid_path);
        }
    }

    ManifestPath result;
    std::copy(text.begin(), text.end(), result.storage_.begin());
    result.size_ = static_cast<std::uint16_t>(text.size());
    return result;
}

const ManifestFileRecord& AnalyzedPackageManifest::file(std::size_t index) const noexcept {
    assert(index < file_count);
    return files[index];
}

std::uint32_t AnalyzedPackageManifest::requested_permission(std::size_t index) const noexcept {
    assert(index < permission_count);
    return requested_permissions[index];
}

os::core::Result<AnalyzedPackageManifest>
PackageManifestAnalyzer::analyze(os::core::ByteSpan bytes) noexcept {
    if (bytes.size() < package_manifest_header_size_v1) {
        return manifest_error(manifest_errors::truncated);
    }
    if (bytes.size() > max_package_manifest_bytes) {
        return manifest_error(manifest_errors::invalid_size);
    }

    Reader reader(bytes);
    std::uint32_t magic = 0U;
    std::uint16_t version = 0U;
    std::uint16_t header_size = 0U;
    std::uint32_t total_size = 0U;
    std::uint16_t package_id_size = 0U;
    std::uint16_t entry_point_size = 0U;
    std::uint16_t file_count = 0U;
    std::uint16_t permission_count = 0U;
    std::uint32_t reserved = 0U;

    if (!reader.read_u32(magic) || !reader.read_u16(version) || !reader.read_u16(header_size) ||
        !reader.read_u32(total_size) || !reader.read_u16(package_id_size) ||
        !reader.read_u16(entry_point_size) || !reader.read_u16(file_count) ||
        !reader.read_u16(permission_count) || !reader.read_u32(reserved)) {
        return manifest_error(manifest_errors::truncated);
    }

    if (magic != package_manifest_magic_v1) {
        return manifest_error(manifest_errors::invalid_magic);
    }
    if (version != package_manifest_version_v1) {
        return manifest_error(manifest_errors::unsupported_version);
    }
    if (header_size != package_manifest_header_size_v1) {
        return manifest_error(manifest_errors::invalid_header);
    }
    if (total_size != bytes.size()) {
        return manifest_error(manifest_errors::invalid_size);
    }
    if (reserved != 0U) {
        return manifest_error(manifest_errors::invalid_reserved);
    }
    if (package_id_size == 0U || package_id_size > max_package_id_bytes ||
        entry_point_size == 0U || entry_point_size > max_manifest_path_bytes ||
        file_count == 0U || file_count > max_manifest_files ||
        permission_count > max_requested_permissions) {
        return manifest_error(manifest_errors::invalid_size);
    }

    std::string_view package_id_text;
    std::string_view entry_point_text;
    if (!reader.read_text(package_id_size, package_id_text) ||
        !reader.read_text(entry_point_size, entry_point_text)) {
        return manifest_error(manifest_errors::truncated);
    }

    auto package_id = PackageId::parse(package_id_text);
    if (!package_id) return package_id.error();

    auto entry_point = ManifestPath::parse(entry_point_text);
    if (!entry_point) return entry_point.error();

    AnalyzedPackageManifest result;
    result.package_id = package_id.value();
    result.entry_point = entry_point.value();
    result.file_count = file_count;
    result.permission_count = permission_count;

    std::uint32_t previous_permission = 0U;
    for (std::size_t index = 0U; index < permission_count; ++index) {
        std::uint32_t permission = 0U;
        if (!reader.read_u32(permission)) {
            return manifest_error(manifest_errors::truncated);
        }
        if (permission == 0U || permission < previous_permission) {
            return manifest_error(manifest_errors::invalid_permission);
        }
        if (permission == previous_permission) {
            return manifest_error(manifest_errors::duplicate_permission);
        }
        result.requested_permissions[index] = permission;
        previous_permission = permission;
    }

    bool entry_point_found = false;
    for (std::size_t index = 0U; index < file_count; ++index) {
        std::uint16_t path_size = 0U;
        std::uint16_t flags = 0U;
        std::uint64_t declared_size = 0U;
        if (!reader.read_u16(path_size) || !reader.read_u16(flags) || !reader.read_u64(declared_size)) {
            return manifest_error(manifest_errors::truncated);
        }
        if (path_size == 0U || path_size > max_manifest_path_bytes) {
            return manifest_error(manifest_errors::invalid_path);
        }
        if ((flags & static_cast<std::uint16_t>(~manifest_file_known_flags)) != 0U) {
            return manifest_error(manifest_errors::invalid_file_flags);
        }

        std::string_view path_text;
        if (!reader.read_text(path_size, path_text)) {
            return manifest_error(manifest_errors::truncated);
        }
        auto path = ManifestPath::parse(path_text);
        if (!path) return path.error();

        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (result.files[prior].path == path.value()) {
                return manifest_error(manifest_errors::duplicate_path);
            }
        }

        if (declared_size > m1_max_declared_package_bytes ||
            result.declared_content_bytes > m1_max_declared_package_bytes - declared_size) {
            return manifest_error(manifest_errors::declared_content_too_large);
        }

        result.files[index] = ManifestFileRecord{path.value(), declared_size, flags};
        result.declared_content_bytes += declared_size;

        if (result.files[index].path == result.entry_point) {
            entry_point_found = true;
            if (!result.files[index].executable()) {
                return manifest_error(manifest_errors::entry_point_not_executable);
            }
        }
    }

    if (!entry_point_found) {
        return manifest_error(manifest_errors::missing_entry_point);
    }
    if (reader.cursor() != bytes.size()) {
        return manifest_error(manifest_errors::trailing_data);
    }

    return result;
}

} // namespace os::package
