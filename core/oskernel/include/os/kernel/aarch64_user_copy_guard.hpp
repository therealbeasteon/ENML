#pragma once

#include <cstdint>

#include <os/kernel/aarch64_exception.hpp>

namespace os::kernel::aarch64 {

// Single-core bring-up guard. This becomes per-CPU before SMP is enabled.
// A same-EL data abort is recoverable only when all four facts match:
//   * a guard is armed,
//   * ELR_EL1 is the exact guarded LDTRB/STTRB instruction,
//   * FAR_EL1 is inside the exact byte range,
//   * the exception class is Data Abort from the current EL.
// Everything else remains a fatal kernel exception.
struct UserCopyFaultGuard final {
    std::uint64_t fault_pc {0ULL};
    std::uint64_t recovery_pc {0ULL};
    std::uint64_t begin {0ULL};
    std::uint64_t end_exclusive {0ULL};
    bool armed {false};
};

[[nodiscard]] bool arm_user_copy_fault_guard(
    std::uint64_t begin,
    std::uint64_t end_exclusive,
    std::uint64_t fault_pc,
    std::uint64_t recovery_pc) noexcept;

void disarm_user_copy_fault_guard() noexcept;

// Returns true only when the frame was rewritten to the matching recovery PC.
[[nodiscard]] bool recover_user_copy_fault(ExceptionFrame& frame) noexcept;

} // namespace os::kernel::aarch64
