#pragma once

#include <cstdint>

#include <os/core/error.hpp>
#include <os/core/result.hpp>
#include <os/kernel/aarch64_kernel_mapping_manifest.hpp>
#include <os/kernel/aarch64_page_tables.hpp>

namespace os::kernel::aarch64 {

namespace kernel_translation_domain_errors {
inline constexpr std::uint32_t invalid_root = 210U;
inline constexpr std::uint32_t already_established = 211U;
inline constexpr std::uint32_t wrong_builder = 212U;
} // namespace kernel_translation_domain_errors

// Populate one upper-region builder from the single reviewed kernel manifest.
// This function does not seal or activate anything: construction, sealing and
// hardware activation remain separate transactions.
[[nodiscard]] inline os::core::Result<void> populate_kernel_translation_builder(
    const KernelMappingManifest& source,
    EarlyStage1Builder& builder) noexcept {
    if (builder.region() != Stage1Region::upper ||
        builder.lifecycle() != EarlyStage1Builder::Lifecycle::building) {
        return os::core::make_error(
            os::core::ErrorDomain::kernel,
            kernel_translation_domain_errors::wrong_builder);
    }

    auto projected = project_kernel_manifest_to_upper(source);
    if (!projected) return projected.error();

    for (std::size_t i = 0U; i < projected.value().size(); ++i) {
        const auto& entry = projected.value()[i];
        for (std::uint64_t offset = 0ULL; offset < entry.length;
             offset += architectural_page_size) {
            auto mapped = builder.map_page(
                entry.virtual_base + offset,
                entry.physical_base + offset,
                entry.permissions,
                entry.kind);
            if (!mapped) return mapped.error();
        }
    }
    return {};
}

// Machine-wide EL1 translation authority. Unlike a process translation binding,
// this root has no ASID or process epoch and may be established only once for a
// boot generation. Construction authority is deliberately narrower than a raw
// physical address: only a sealed upper-region builder may establish it.
class KernelTranslationDomain final {
public:
    [[nodiscard]] os::core::Result<void> establish(
        const EarlyStage1Builder& builder) noexcept {
        if (root_physical_ != 0ULL) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                kernel_translation_domain_errors::already_established);
        }
        if (!builder.sealed_kernel_root() ||
            !stage1_physical_address(builder.root_physical())) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                kernel_translation_domain_errors::invalid_root);
        }
        root_physical_ = builder.root_physical();
        return {};
    }

    [[nodiscard]] constexpr bool established() const noexcept {
        return root_physical_ != 0ULL;
    }

    [[nodiscard]] constexpr std::uint64_t root_physical() const noexcept {
        return root_physical_;
    }

private:
    std::uint64_t root_physical_ {0ULL};
};

} // namespace os::kernel::aarch64
