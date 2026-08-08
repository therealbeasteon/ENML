#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include <os/core/error.hpp>
#include <os/package/analyzer.hpp>

namespace pkg = os::package;

namespace {

struct FileSpec final {
    std::string_view path;
    std::uint16_t flags;
    std::uint64_t size;
};

void push_u16(std::vector<std::byte>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0x00FFU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0x00FFU));
}

void push_u32(std::vector<std::byte>& bytes, std::uint32_t value) {
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0x000000FFU));
    }
}

void push_u64(std::vector<std::byte>& bytes, std::uint64_t value) {
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0x00000000000000FFULL));
    }
}

void push_text(std::vector<std::byte>& bytes, std::string_view text) {
    for (const char value : text) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
}

void write_u32_at(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    assert(offset + 4U <= bytes.size());
    for (unsigned index = 0U; index < 4U; ++index) {
        const auto shift = index * 8U;
        bytes[offset + index] = static_cast<std::byte>((value >> shift) & 0x000000FFU);
    }
}

std::vector<std::byte> build_manifest(
    std::string_view package_id,
    std::string_view entry_point,
    std::span<const std::uint32_t> permissions,
    std::span<const FileSpec> files) {
    assert(package_id.size() <= std::numeric_limits<std::uint16_t>::max());
    assert(entry_point.size() <= std::numeric_limits<std::uint16_t>::max());
    assert(permissions.size() <= std::numeric_limits<std::uint16_t>::max());
    assert(files.size() <= std::numeric_limits<std::uint16_t>::max());

    std::vector<std::byte> bytes;
    push_u32(bytes, pkg::package_manifest_magic_v1);
    push_u16(bytes, pkg::package_manifest_version_v1);
    push_u16(bytes, pkg::package_manifest_header_size_v1);
    push_u32(bytes, 0U);
    push_u16(bytes, static_cast<std::uint16_t>(package_id.size()));
    push_u16(bytes, static_cast<std::uint16_t>(entry_point.size()));
    push_u16(bytes, static_cast<std::uint16_t>(files.size()));
    push_u16(bytes, static_cast<std::uint16_t>(permissions.size()));
    push_u32(bytes, 0U);
    assert(bytes.size() == pkg::package_manifest_header_size_v1);

    push_text(bytes, package_id);
    push_text(bytes, entry_point);
    for (const auto permission : permissions) push_u32(bytes, permission);

    for (const auto& file : files) {
        assert(file.path.size() <= std::numeric_limits<std::uint16_t>::max());
        push_u16(bytes, static_cast<std::uint16_t>(file.path.size()));
        push_u16(bytes, file.flags);
        push_u64(bytes, file.size);
        push_text(bytes, file.path);
    }

    assert(bytes.size() <= std::numeric_limits<std::uint32_t>::max());
    write_u32_at(bytes, 8U, static_cast<std::uint32_t>(bytes.size()));
    return bytes;
}

void expect_error(const std::vector<std::byte>& bytes, std::uint32_t code) {
    auto result = pkg::PackageManifestAnalyzer::analyze(bytes);
    assert(!result);
    assert(result.error().domain == os::core::ErrorDomain::package);
    assert(result.error().code == code);
}

} // namespace

int main() {
    constexpr std::uint16_t executable =
        static_cast<std::uint16_t>(pkg::ManifestFileFlags::executable);

    const std::array<std::uint32_t, 2> permissions{10U, 20U};
    const std::array<FileSpec, 2> files{{
        {"bin/app", executable, 4096U},
        {"res/icon.bin", 0U, 512U},
    }};

    const auto valid = build_manifest("com.emnl.notes", "bin/app", permissions, files);
    auto analyzed = pkg::PackageManifestAnalyzer::analyze(valid);
    assert(analyzed);
    assert(analyzed.value().package_id.view() == "com.emnl.notes");
    assert(analyzed.value().entry_point.view() == "bin/app");
    assert(analyzed.value().file_count == 2U);
    assert(analyzed.value().permission_count == 2U);
    assert(analyzed.value().requested_permission(0U) == 10U);
    assert(analyzed.value().requested_permission(1U) == 20U);
    assert(analyzed.value().file(0U).path.view() == "bin/app");
    assert(analyzed.value().file(0U).executable());
    assert(analyzed.value().declared_content_bytes == 4608U);

    assert(pkg::ManifestPath::parse("bin/app"));
    assert(pkg::ManifestPath::parse("assets/ui/icon-2.bin"));
    for (const char* bad : {"", "/bin/app", "bin/app/", "bin//app", "bin/./app",
                            "bin/../app", "bin\\app", "bin/app name"}) {
        auto path = pkg::ManifestPath::parse(bad);
        assert(!path);
        assert(path.error().code == pkg::manifest_errors::invalid_path);
    }

    {
        auto bytes = valid;
        bytes[0] = std::byte{0};
        expect_error(bytes, pkg::manifest_errors::invalid_magic);
    }
    {
        auto bytes = valid;
        bytes[4] = std::byte{2};
        expect_error(bytes, pkg::manifest_errors::unsupported_version);
    }
    {
        auto bytes = valid;
        bytes[20] = std::byte{1};
        expect_error(bytes, pkg::manifest_errors::invalid_reserved);
    }
    {
        const auto bytes = build_manifest("Com.emnl.notes", "bin/app", permissions, files);
        expect_error(bytes, pkg::errors::invalid_package_id);
    }
    {
        const std::array<FileSpec, 1> traversal{{{"bin/../app", executable, 1U}}};
        const auto bytes = build_manifest("com.emnl.notes", "bin/app", permissions, traversal);
        expect_error(bytes, pkg::manifest_errors::invalid_path);
    }
    {
        const std::array<FileSpec, 2> duplicate{{
            {"bin/app", executable, 1U},
            {"bin/app", 0U, 2U},
        }};
        const auto bytes = build_manifest("com.emnl.notes", "bin/app", permissions, duplicate);
        expect_error(bytes, pkg::manifest_errors::duplicate_path);
    }
    {
        const std::array<FileSpec, 1> unknown_flags{{{"bin/app", 2U, 1U}}};
        const auto bytes = build_manifest("com.emnl.notes", "bin/app", permissions, unknown_flags);
        expect_error(bytes, pkg::manifest_errors::invalid_file_flags);
    }
    {
        const std::array<std::uint32_t, 2> duplicate_permissions{7U, 7U};
        const auto bytes = build_manifest("com.emnl.notes", "bin/app", duplicate_permissions, files);
        expect_error(bytes, pkg::manifest_errors::duplicate_permission);
    }
    {
        const std::array<std::uint32_t, 2> unsorted_permissions{8U, 7U};
        const auto bytes = build_manifest("com.emnl.notes", "bin/app", unsorted_permissions, files);
        expect_error(bytes, pkg::manifest_errors::invalid_permission);
    }
    {
        const std::array<std::uint32_t, 1> zero_permission{0U};
        const auto bytes = build_manifest("com.emnl.notes", "bin/app", zero_permission, files);
        expect_error(bytes, pkg::manifest_errors::invalid_permission);
    }
    {
        const std::array<FileSpec, 1> not_executable{{{"bin/app", 0U, 1U}}};
        const auto bytes = build_manifest("com.emnl.notes", "bin/app", permissions, not_executable);
        expect_error(bytes, pkg::manifest_errors::entry_point_not_executable);
    }
    {
        const std::array<FileSpec, 1> missing{{{"bin/other", executable, 1U}}};
        const auto bytes = build_manifest("com.emnl.notes", "bin/app", permissions, missing);
        expect_error(bytes, pkg::manifest_errors::missing_entry_point);
    }
    {
        const std::array<FileSpec, 1> huge{{
            {"bin/app", executable, pkg::m1_max_declared_package_bytes + 1U},
        }};
        const auto bytes = build_manifest("com.emnl.notes", "bin/app", permissions, huge);
        expect_error(bytes, pkg::manifest_errors::declared_content_too_large);
    }
    {
        auto bytes = valid;
        bytes.push_back(std::byte{0});
        write_u32_at(bytes, 8U, static_cast<std::uint32_t>(bytes.size()));
        expect_error(bytes, pkg::manifest_errors::trailing_data);
    }
    {
        auto bytes = valid;
        bytes.pop_back();
        write_u32_at(bytes, 8U, static_cast<std::uint32_t>(bytes.size()));
        expect_error(bytes, pkg::manifest_errors::truncated);
    }
    {
        std::vector<std::byte> oversized(pkg::max_package_manifest_bytes + 1U, std::byte{0});
        expect_error(oversized, pkg::manifest_errors::invalid_size);
    }

    return 0;
}
