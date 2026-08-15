#include <os/kernel/aarch64_entry.hpp>
#include <os/kernel/aarch64_user_copy_guard.hpp>

#include <cstdint>

#if !defined(__aarch64__)
#error "aarch64_entry.cpp must only be compiled for AArch64"
#endif

extern "C" char cookie_aarch64_vector_table[];

namespace os::kernel::aarch64 {

bool install_exception_vectors() noexcept {
    const auto address = reinterpret_cast<std::uintptr_t>(cookie_aarch64_vector_table);
    if ((address & (exception_vector_table_alignment - 1U)) != 0U) {
        return false;
    }
    asm volatile("msr vbar_el1, %0" :: "r"(address) : "memory");
    asm volatile("isb" ::: "memory");
    return true;
}

} // namespace os::kernel::aarch64

extern "C" void cookie_aarch64_current_sync_exception_dispatch(
    os::kernel::aarch64::ExceptionFrame* frame) noexcept {
    if (frame == nullptr) {
        cookie_aarch64_unhandled_exception();
    }
    if (!os::kernel::aarch64::recover_user_copy_fault(*frame)) {
        // A kernel-mode fault that no armed guard claimed. Reported with its
        // syndrome rather than as a bare halt: this is the case where the
        // faulting address is the entire diagnosis, and it is also the case
        // with no user process to blame.
        cookie_aarch64_unhandled_fault(frame);
    }
}

extern "C" void cookie_aarch64_sync_exception_dispatch(
    os::kernel::aarch64::ExceptionFrame* frame) noexcept {
    if (frame == nullptr) {
        cookie_aarch64_unhandled_exception();
    }
    const auto syndrome = os::kernel::aarch64::decode_exception_syndrome(frame->esr_el1);
    if (!os::kernel::aarch64::valid_cookie_svc(syndrome)) {
        // Every fault reaching here is from a lower EL by construction - this
        // is the lower-EL sync vector - so it is one process's problem and not
        // the machine's. The handler chooses what runs next and returns.
        cookie_aarch64_el0_fault(frame);
        return;
    }
    cookie_kernel_syscall_entry(frame);
}
