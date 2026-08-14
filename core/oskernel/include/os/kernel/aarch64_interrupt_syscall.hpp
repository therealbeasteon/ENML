#pragma once

#include <os/core/result.hpp>
#include <os/kernel/aarch64_exception.hpp>
#include <os/kernel/kernel.hpp>

namespace os::kernel::aarch64 {

// Called only after the scheduler has installed `current`'s validated
// translation and before ERET, exactly where complete_ipc_current is called
// - this is the interrupt analogue of it. If Kernel::dispatch_interrupt
// already collected a Service for current (it does so the instant a driver
// is woken; see interrupt_delivery.hpp), this takes it and writes assertions
// into x2 and saturated into x3. Deliberately not x0/x1 - complete_ipc_current
// already uses those, and a thread's resume can need both completions at once
// (a driver that also does IPC), so the two must not collide.
//
// Returns false, not an error, when nothing is armed for current - most
// resumes are not a driver picking up a device, the same way most resumes
// are not a receive completing.
[[nodiscard]] os::core::Result<bool> complete_interrupt_current(
    ThreadId current, ExceptionFrame& frame, Kernel& kernel) noexcept;

} // namespace os::kernel::aarch64
