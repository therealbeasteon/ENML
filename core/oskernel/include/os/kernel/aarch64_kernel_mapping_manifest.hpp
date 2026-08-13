#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/core/result.hpp>
#include <os/kernel/aarch64_translation.hpp>
#include <os/kernel/machine.hpp>

namespace os::kernel::aarch64 {

namespace kernel_mapping_manifest_errors {
inline constexpr std::uint32_t invalid = 140U;
inline constexpr std::uint32_t exhausted = 141U;
} // namespace kernel_mapping_manifest_errors

enum class KernelMappingRole : std::uint8_t {
    ordinary = 0U,
    guarded_stack = 1U,
};

struct KernelMappingManifestEntry final {
    std::uint64_t virtual_base {0ULL};
    std::uint64_t physical_base {0ULL};
    std::uint64_t length {0ULL};
    MachinePermissions permissions {MachinePermissions::read};
    MachineMemoryKind kind {MachineMemoryKind::normal};
    KernelMappingRole role {KernelMappingRole::ordinary};

    [[nodiscard]] constexpr bool valid() const noexcept {
        if (virtual_base == 0ULL || physical_base == 0ULL || length == 0ULL) return false;
        if (!page_aligned(virtual_base) || !page_aligned(physical_base) || !page_aligned(length)) {
            return false;
        }
        if (length > UINT64_MAX - virtual_base || length > UINT64_MAX - physical_base) {
            return false;
        }
        // Device mappings are never executable. Keep this at the reviewed
        // manifest boundary instead of relying on a deeper page-table failure.
        if (kind == MachineMemoryKind::device && permissions == MachinePermissions::read_execute) {
            return false;
        }
        if (role == KernelMappingRole::guarded_stack &&
            (kind != MachineMemoryKind::normal || permissions != MachinePermissions::read_write)) {
            return false;
        }
        return true;
    }
};

// Bring-up manifest: this is the one reviewed list of EL1 mappings. M7.7 uses
// the same physical ranges and attributes to derive the future TTBR1 aliases;
// it does not maintain a second hand-written kernel map.
class KernelMappingManifest final {
public:
    static constexpr std::size_t max_entries = 24U;

    [[nodiscard]] os::core::Result<void> add(
        KernelMappingManifestEntry entry) noexcept {
        if (!entry.valid()) return error(kernel_mapping_manifest_errors::invalid);
        if (count_ >= entries_.size()) return error(kernel_mapping_manifest_errors::exhausted);
        entries_[count_++] = entry;
        return {};
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }
    [[nodiscard]] constexpr const KernelMappingManifestEntry& operator[](
        std::size_t index) const noexcept { return entries_[index]; }

private:
    [[nodiscard]] static constexpr os::core::Error error(std::uint32_t code) noexcept {
        return os::core::make_error(os::core::ErrorDomain::kernel, code);
    }

    std::array<KernelMappingManifestEntry, max_entries> entries_ {};
    std::size_t count_ {0U};
};

// Cookie's first TTBR1 layout uses a direct, deterministic upper-region alias
// for each reviewed physical kernel range. The alias is deliberately limited to
// the same 39-bit span as TTBR1 so a mapping cannot wrap or escape the domain.
[[nodiscard]] constexpr std::uint64_t kernel_virtual_alias(
    std::uint64_t physical_address) noexcept {
    return physical_address < user_virtual_limit
        ? kernel_virtual_base + physical_address
        : 0ULL;
}

[[nodiscard]] inline os::core::Result<KernelMappingManifest>
project_kernel_manifest_to_upper(
    const KernelMappingManifest& source) noexcept {
    KernelMappingManifest projected{};
    for (std::size_t i = 0U; i < source.size(); ++i) {
        const auto& entry = source[i];
        if (!entry.valid() ||
            entry.physical_base >= user_virtual_limit ||
            entry.length > user_virtual_limit - entry.physical_base) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                kernel_mapping_manifest_errors::invalid);
        }
        const auto alias = kernel_virtual_alias(entry.physical_base);
        if (alias == 0ULL || !kernel_stage1_virtual_address(alias)) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                kernel_mapping_manifest_errors::invalid);
        }
        auto added = projected.add({
            .virtual_base = alias,
            .physical_base = entry.physical_base,
            .length = entry.length,
            .permissions = entry.permissions,
            .kind = entry.kind,
            .role = entry.role,
        });
        if (!added) return added.error();
    }
    return projected;
}

[[nodiscard]] inline os::core::Result<void> replay_kernel_mapping_manifest(
    const KernelMappingManifest& manifest,
    MachineAddressSpace& target) noexcept {
    for (std::size_t i = 0U; i < manifest.size(); ++i) {
        const auto& entry = manifest[i];
        if (!entry.valid()) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                kernel_mapping_manifest_errors::invalid);
        }
        os::core::Result<void> result =
            entry.role == KernelMappingRole::guarded_stack
                ? machine_map_kernel_stack(
                    target,
                    static_cast<std::uintptr_t>(entry.virtual_base),
                    static_cast<std::uintptr_t>(entry.physical_base),
                    static_cast<std::size_t>(entry.length))
                : machine_map(
                    target,
                    static_cast<std::uintptr_t>(entry.virtual_base),
                    static_cast<std::uintptr_t>(entry.physical_base),
                    static_cast<std::size_t>(entry.length),
                    entry.permissions,
                    entry.kind);
        if (!result) return result.error();
    }
    return {};
}

} // namespace os::kernel::aarch64
