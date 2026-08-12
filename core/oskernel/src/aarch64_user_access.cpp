#include <os/kernel/aarch64_user_access.hpp>

#include <cstdint>

#include <os/core/error.hpp>
#include <os/kernel/aarch64_asid.hpp>

#if !defined(__aarch64__)
#error "aarch64_user_access.cpp must only be compiled for AArch64"
#endif

namespace os::kernel::aarch64 {
namespace {
[[nodiscard]] constexpr os::core::Error copy_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] std::uint64_t current_ttbr0_el1() noexcept {
    std::uint64_t value = 0ULL;
    asm volatile("mrs %0, ttbr0_el1" : "=r"(value));
    return value;
}

[[nodiscard]] os::core::Result<void> validate_current(
    const UserAccessTicket& ticket,
    UserAccessIntent expected,
    std::size_t bytes,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (ticket.intent != expected) return copy_error(user_copy_errors::wrong_intent);
    if (bytes != ticket.range.length) return copy_error(user_copy_errors::size_mismatch);
    if (!user_access_still_live(ticket, translations, epochs)) {
        return copy_error(user_copy_errors::inactive_translation);
    }
    const auto expected_ttbr = ttbr0_el1_value(ticket.root_physical, ticket.epoch.asid);
    if (expected_ttbr == 0ULL || current_ttbr0_el1() != expected_ttbr) {
        return copy_error(user_copy_errors::wrong_current_translation);
    }
    return {};
}
}

os::core::Result<void> copy_from_user_current(
    const UserAccessTicket& ticket,
    std::span<std::byte> destination,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    auto valid = validate_current(
        ticket, UserAccessIntent::read_from_user, destination.size(), translations, epochs);
    if (!valid) return valid;

    const auto* source = reinterpret_cast<const std::byte*>(
        static_cast<std::uintptr_t>(ticket.range.address));
    for (std::size_t i = 0U; i < destination.size(); ++i) {
        std::uint32_t value = 0U;
        const auto* address = source + i;
        // LDTRB performs the memory access using unprivileged access semantics.
        // PAN therefore remains closed for ordinary privileged loads/stores.
        asm volatile("ldtrb %w0, [%1]" : "=r"(value) : "r"(address) : "memory");
        destination[i] = static_cast<std::byte>(value & 0xFFU);
    }
    return {};
}

os::core::Result<void> copy_to_user_current(
    const UserAccessTicket& ticket,
    std::span<const std::byte> source,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    auto valid = validate_current(
        ticket, UserAccessIntent::write_to_user, source.size(), translations, epochs);
    if (!valid) return valid;

    auto* destination = reinterpret_cast<std::byte*>(
        static_cast<std::uintptr_t>(ticket.range.address));
    for (std::size_t i = 0U; i < source.size(); ++i) {
        const auto value = static_cast<std::uint32_t>(source[i]);
        auto* address = destination + i;
        // STTRB performs the store using unprivileged access semantics.
        asm volatile("sttrb %w0, [%1]" :: "r"(value), "r"(address) : "memory");
    }
    return {};
}

} // namespace os::kernel::aarch64
