#include <os/kernel/aarch64_user_access.hpp>

#include <cstdint>

#include <os/core/error.hpp>
#include <os/kernel/aarch64_asid.hpp>
#include <os/kernel/aarch64_entry.hpp>
#include <os/kernel/aarch64_exception.hpp>

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

inline constexpr std::uint8_t exception_class_data_abort_current_el = 0x25U;

struct UserCopyFaultGuard final {
    bool armed {false};
    std::uint64_t user_begin {0ULL};
    std::uint64_t user_end {0ULL};
    std::uint64_t fault_pc {0ULL};
    std::uint64_t recovery_pc {0ULL};
};

// M7.6b is still single-core. This becomes per-CPU state before SMP bring-up;
// keeping it explicit prevents a future multicore port from accidentally
// treating one CPU's copy recovery as global authority.
UserCopyFaultGuard copy_fault_guard {};

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

void arm_copy_guard(
    const UserAccessTicket& ticket,
    const void* fault_pc,
    const void* recovery_pc) noexcept {
    copy_fault_guard = UserCopyFaultGuard{
        .armed = true,
        .user_begin = ticket.range.address,
        .user_end = ticket.range.address + static_cast<std::uint64_t>(ticket.range.length),
        .fault_pc = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(fault_pc)),
        .recovery_pc = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(recovery_pc)),
    };
    asm volatile("" ::: "memory");
}

void disarm_copy_guard() noexcept {
    asm volatile("" ::: "memory");
    copy_fault_guard = UserCopyFaultGuard{};
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

    const auto* source = reinterpret_cast<const std::byte*>(
        static_cast<std::uintptr_t>(ticket.range.address));
    arm_copy_guard(
        ticket,
        cookie_aarch64_ldtrb_user_byte_fault,
        cookie_aarch64_ldtrb_user_byte_recovery);
    for (std::size_t i = 0U; i < destination.size(); ++i) {
        if (!cookie_aarch64_ldtrb_user_byte(source + i, destination.data() + i)) {
            disarm_copy_guard();
            return copy_error(user_copy_errors::copy_fault);
        }
    }
    disarm_copy_guard();
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
    arm_copy_guard(
        ticket,
        cookie_aarch64_sttrb_user_byte_fault,
        cookie_aarch64_sttrb_user_byte_recovery);
    for (std::size_t i = 0U; i < source.size(); ++i) {
        const auto value = static_cast<std::uint32_t>(source[i]);
        if (!cookie_aarch64_sttrb_user_byte(destination + i, value)) {
            disarm_copy_guard();
            return copy_error(user_copy_errors::copy_fault);
        }
    }
    disarm_copy_guard();
    return {};
}

} // namespace os::kernel::aarch64

extern "C" void cookie_aarch64_current_sync_exception_dispatch(
    os::kernel::aarch64::ExceptionFrame* frame) noexcept {
    using namespace os::kernel::aarch64;
    if (frame != nullptr) {
        const auto syndrome = decode_exception_syndrome(frame->esr_el1);
        const auto& guard = copy_fault_guard;
        if (guard.armed &&
            syndrome.exception_class == exception_class_data_abort_current_el &&
            frame->elr_el1 == guard.fault_pc &&
            frame->far_el1 >= guard.user_begin &&
            frame->far_el1 < guard.user_end) {
            frame->elr_el1 = guard.recovery_pc;
            return;
        }
    }
    cookie_aarch64_unhandled_exception();
}
