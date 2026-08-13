#pragma once

#include <cstdint>

#include <os/core/error.hpp>
#include <os/core/result.hpp>
#include <os/kernel/aarch64_kernel_mapping_manifest.hpp>
#include <os/kernel/aarch64_page_tables.hpp>
#include <os/kernel/translation_root.hpp>

namespace os::kernel::aarch64 {

namespace kernel_translation_domain_errors {
inline constexpr std::uint32_t invalid_root = 210U;
inline constexpr std::uint32_t already_established = 211U;
inline constexpr std::uint32_t wrong_builder = 212U;
inline constexpr std::uint32_t invalid_plan = 213U;
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

// Immutable input to the future no-stack machine transition. Preparing this
// object proves both roots are sealed/established and that one reviewed split
// TCR value is available before any system register is modified.
struct SplitTranslationPlan final {
    std::uint64_t user_root_physical {0ULL};
    std::uint64_t kernel_root_physical {0ULL};
    std::uint64_t tcr_el1 {0ULL};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return stage1_physical_address(user_root_physical) &&
               stage1_physical_address(kernel_root_physical) &&
               user_root_physical != kernel_root_physical &&
               tcr_el1 != 0ULL;
    }
};

[[nodiscard]] inline os::core::Result<SplitTranslationPlan>
prepare_split_translation_plan(
    const SealedTranslationRoot& user_root,
    const KernelTranslationDomain& kernel_domain,
    std::uint8_t hardware_parange) noexcept {
    if (!user_root.valid() || !kernel_domain.established() || hardware_parange > 6U) {
        return os::core::make_error(
            os::core::ErrorDomain::kernel,
            kernel_translation_domain_errors::invalid_plan);
    }
    const auto tcr = split_tcr_el1_for_ips(cookie_ips(hardware_parange));
    SplitTranslationPlan plan{
        .user_root_physical = user_root.root_physical(),
        .kernel_root_physical = kernel_domain.root_physical(),
        .tcr_el1 = tcr,
    };
    if (!plan.valid()) {
        return os::core::make_error(
            os::core::ErrorDomain::kernel,
            kernel_translation_domain_errors::invalid_plan);
    }
    return plan;
}

} // namespace os::kernel::aarch64
