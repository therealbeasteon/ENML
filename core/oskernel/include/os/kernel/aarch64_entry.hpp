#pragma once

#include <os/kernel/aarch64_exception.hpp>

namespace os::kernel::aarch64 {

[[nodiscard]] bool install_exception_vectors() noexcept;

} // namespace os::kernel::aarch64

extern "C" void cookie_kernel_syscall_entry(
    os::kernel::aarch64::ExceptionFrame* frame) noexcept;

// Lower-EL synchronous exceptions are syscall candidates only.
extern "C" void cookie_aarch64_sync_exception_dispatch(
    os::kernel::aarch64::ExceptionFrame* frame) noexcept;

// Current-EL/SPx synchronous exceptions are fatal except for an exact armed
// Cookie user-copy fault guard match.
extern "C" void cookie_aarch64_current_sync_exception_dispatch(
    os::kernel::aarch64::ExceptionFrame* frame) noexcept;

// The frameless path, used by the vector entries that take no frame at all
// (IRQ/FIQ/SError on the current EL, and every AArch32 entry). It can report
// that something happened and nothing about what.
extern "C" [[noreturn]] void cookie_aarch64_unhandled_exception() noexcept;

// The path taken when a synchronous exception arrives with a saved frame and
// turns out not to be a system call this kernel serves. Distinct from the
// frameless one because ESR_EL1 and FAR_EL1 are available here, which is the
// difference between "a fault occurred" and "a level-3 translation fault on a
// write to this address from EL0" - see describe_fault. M7.11 replaces the
// halt with delivery to a userland pager; the reporting stays.
extern "C" [[noreturn]] void cookie_aarch64_unhandled_fault(
    const os::kernel::aarch64::ExceptionFrame* frame) noexcept;
