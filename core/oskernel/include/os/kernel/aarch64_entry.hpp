#pragma once

#include <os/kernel/aarch64_exception.hpp>

namespace os::kernel::aarch64 {

// Installs Cookie's real EL1 exception vector table into VBAR_EL1. The table is
// 2 KiB aligned by aarch64_vectors.S and validated again before the register is
// changed so linker/layout mistakes fail closed.
[[nodiscard]] bool install_exception_vectors() noexcept;

} // namespace os::kernel::aarch64

// Portable-kernel obligation. The machine layer has already established that
// this frame came from lower-EL AArch64 `SVC #0`; this function owns syscall
// number/argument decoding, capability checks, scheduling and return values.
extern "C" void cookie_kernel_syscall_entry(
    os::kernel::aarch64::ExceptionFrame* frame) noexcept;

// Called directly from aarch64_vectors.S.
extern "C" void cookie_aarch64_sync_exception_dispatch(
    os::kernel::aarch64::ExceptionFrame* frame) noexcept;

extern "C" [[noreturn]] void cookie_aarch64_unhandled_exception() noexcept;
