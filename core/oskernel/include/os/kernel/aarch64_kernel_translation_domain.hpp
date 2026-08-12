#pragma once

#include <cstdint>

#include <os/core/error.hpp>
#include <os/core/result.hpp>
#include <os/kernel/aarch64_translation.hpp>

namespace os::kernel::aarch64 {

namespace kernel_translation_domain_errors {
inline constexpr std::uint32_t invalid_root = 210U;
inline constexpr std::uint32_t already_established = 211U;
} // namespace kernel_translation_domain_errors

// Machine-wide EL1 translation authority. Unlike a process translation binding,
// this root has no ASID or process epoch and may be established only once for a
// boot generation. Process switching must never replace it.
class KernelTranslationDomain final {
public:
    [[nodiscard]] os::core::Result<void> establish(
        std::uint64_t root_physical) noexcept {
        if (root_physical_ != 0ULL) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                kernel_translation_domain_errors::already_established);
        }
        if (!stage1_physical_address(root_physical)) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                kernel_translation_domain_errors::invalid_root);
        }
        root_physical_ = root_physical;
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
