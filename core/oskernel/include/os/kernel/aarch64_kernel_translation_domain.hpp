#pragma once

#include <cstdint>

#include <os/core/error.hpp>
#include <os/core/result.hpp>
#include <os/kernel/aarch64_page_tables.hpp>

namespace os::kernel::aarch64 {

namespace kernel_translation_domain_errors {
inline constexpr std::uint32_t invalid_root = 210U;
inline constexpr std::uint32_t already_established = 211U;
} // namespace kernel_translation_domain_errors

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
