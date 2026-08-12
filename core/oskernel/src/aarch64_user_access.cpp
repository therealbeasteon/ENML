#include <os/kernel/aarch64_user_access.hpp>

#include <cstdint>

#include <os/core/error.hpp>
#include <os/kernel/aarch64_asid.hpp>
#include <os/kernel/aarch64_user_copy_guard.hpp>

#if !defined(__aarch64__)
#error "aarch64_user_access.cpp must only be compiled for AArch64"
#endif

extern "C" bool cookie_aarch64_ldtrb_user_byte(
    const void* user_address,
    void* kernel_destination) noexcept;
extern "C" bool cookie_aarch64_sttrb_user_byte(
    void* user_address,
    std::uint32_t value) noexcept;
extern "C" char cookie_aarch64_ldtrb_user_byte_fault[];
extern "C" char cookie_aarch64_ldtrb_user_byte_recovery[];
extern "C" char cookie_aarch64_sttrb_user_byte_fault[];
extern "C" char cookie_aarch64_sttrb_user_byte_recovery[];

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

[[nodiscard]] bool arm_byte(
    std::uint64_t address,
    const void* fault_pc,
    const void* recovery_pc) noexcept {
    if (address == UINT64_MAX) return false;
    return arm_user_copy_fault_guard(
        address,
        address + 1ULL,
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(fault_pc)),
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(recovery_pc)));
}
} // namespace

os::core::Result<void> copy_from_user_current(
    const UserAccessTicket& ticket,
    std::span<std::byte> destination,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    auto valid = validate_current(
        ticket, UserAccessIntent::read_from_user, destination.size(), translations, epochs);
    if (!valid) return valid;

    for (std::size_t i = 0U; i < destination.size(); ++i) {
        const std::uint64_t address = ticket.range.address + static_cast<std::uint64_t>(i);
        if (!arm_byte(
                address,
                cookie_aarch64_ldtrb_user_byte_fault,
                cookie_aarch64_ldtrb_user_byte_recovery)) {
            return copy_error(user_copy_errors::copy_fault);
        }
        const bool copied = cookie_aarch64_ldtrb_user_byte(
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)),
            destination.data() + i);
        disarm_user_copy_fault_guard();
        if (!copied) return copy_error(user_copy_errors::copy_fault);
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

    for (std::size_t i = 0U; i < source.size(); ++i) {
        const std::uint64_t address = ticket.range.address + static_cast<std::uint64_t>(i);
        if (!arm_byte(
                address,
                cookie_aarch64_sttrb_user_byte_fault,
                cookie_aarch64_sttrb_user_byte_recovery)) {
            return copy_error(user_copy_errors::copy_fault);
        }
        const bool copied = cookie_aarch64_sttrb_user_byte(
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)),
            static_cast<std::uint32_t>(source[i]));
        disarm_user_copy_fault_guard();
        if (!copied) return copy_error(user_copy_errors::copy_fault);
    }
    return {};
}

} // namespace os::kernel::aarch64
