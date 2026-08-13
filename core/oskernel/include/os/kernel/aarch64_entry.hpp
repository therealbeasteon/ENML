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

extern "C" [[noreturn]] void cookie_aarch64_unhandled_exception() noexcept;
