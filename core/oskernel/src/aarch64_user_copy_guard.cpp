#include <os/kernel/aarch64_user_copy_guard.hpp>

namespace os::kernel::aarch64 {
namespace {
UserCopyFaultGuard guard{};
inline constexpr std::uint8_t exception_class_data_abort_current_el = 0x25U;
}

bool arm_user_copy_fault_guard(
    std::uint64_t begin,
    std::uint64_t end_exclusive,
    std::uint64_t fault_pc,
    std::uint64_t recovery_pc) noexcept {
    if (guard.armed || begin == 0ULL || end_exclusive <= begin ||
        fault_pc == 0ULL || recovery_pc == 0ULL) {
        return false;
    }
    guard = UserCopyFaultGuard{
        .fault_pc = fault_pc,
        .recovery_pc = recovery_pc,
        .begin = begin,
        .end_exclusive = end_exclusive,
        .armed = true,
    };
    return true;
}

void disarm_user_copy_fault_guard() noexcept {
    guard = UserCopyFaultGuard{};
}

bool recover_user_copy_fault(ExceptionFrame& frame) noexcept {
    if (!guard.armed) return false;
    const auto syndrome = decode_exception_syndrome(frame.esr_el1);
    if (syndrome.exception_class != exception_class_data_abort_current_el ||
        frame.elr_el1 != guard.fault_pc ||
        frame.far_el1 < guard.begin || frame.far_el1 >= guard.end_exclusive) {
        return false;
    }

    frame.elr_el1 = guard.recovery_pc;
    guard = UserCopyFaultGuard{};
    return true;
}

} // namespace os::kernel::aarch64
