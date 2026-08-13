#pragma once

#include <cstdint>

namespace os::kernel {

namespace aarch64 {
class TranslationRootSealer;
}

// A sealed translation root is an execution capability, not a raw address.
// Only the architecture sealer may mint one after a page-table builder has
// crossed its building -> sealed transition. Process/scheduler code can carry
// the token but cannot manufacture an executable memory universe from an
// arbitrary physical page.
class SealedTranslationRoot final {
public:
    constexpr SealedTranslationRoot() noexcept = default;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return root_physical_ != 0ULL;
    }

    [[nodiscard]] constexpr std::uint64_t root_physical() const noexcept {
        return root_physical_;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const SealedTranslationRoot&,
        const SealedTranslationRoot&) = default;

private:
    explicit constexpr SealedTranslationRoot(std::uint64_t root_physical) noexcept
        : root_physical_(root_physical) {}

    std::uint64_t root_physical_ {0ULL};

    friend class aarch64::TranslationRootSealer;
};

} // namespace os::kernel
