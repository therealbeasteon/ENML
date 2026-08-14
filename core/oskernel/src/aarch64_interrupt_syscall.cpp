#include <os/kernel/aarch64_interrupt_syscall.hpp>

#include <os/core/error.hpp>
#include <os/kernel/interrupt_delivery.hpp>

#if !defined(__aarch64__)
#error "aarch64_interrupt_syscall.cpp must only be compiled for AArch64"
#endif

namespace os::kernel::aarch64 {

os::core::Result<bool> complete_interrupt_current(
    ThreadId current, ExceptionFrame& frame, Kernel& kernel) noexcept {
    auto service = kernel.take_delivered_service(current);
    if (!service) {
        const auto error = service.error();
        if (error.domain == os::core::ErrorDomain::kernel &&
            error.code == interrupt_delivery_errors::not_armed) {
            return false;
        }
        return error;
    }

    frame.x[2] = service.value().assertions;
    frame.x[3] = service.value().saturated ? 1ULL : 0ULL;
    return true;
}

} // namespace os::kernel::aarch64
