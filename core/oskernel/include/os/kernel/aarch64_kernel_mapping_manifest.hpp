#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/core/result.hpp>
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
        return virtual_base != 0ULL && physical_base != 0ULL && length != 0ULL &&
               (role != KernelMappingRole::guarded_stack ||
                (kind == MachineMemoryKind::normal &&
                 permissions == MachinePermissions::read_write));
    }
};

// Bring-up-only manifest for the pre-TTBR1 regime. Process roots may replay
// only this explicit EL1 mapping set; user mappings are never represented here.
// The mature design will replace this duplication with a stable kernel TTBR1
// domain, but until then the manifest prevents accidental copying of one
// process's EL0 mappings into another root.
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

[[nodiscard]] inline os::core::Result<void> replay_kernel_mapping_manifest(
    const KernelMappingManifest& manifest,
    MachineAddressSpace& target) noexcept {
    for (std::size_t i = 0U; i < manifest.size(); ++i) {
        const auto& entry = manifest[i];
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
