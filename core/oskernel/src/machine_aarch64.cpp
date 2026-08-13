#include <os/kernel/machine.hpp>

#include <cstdint>
#include <limits>

#include <os/core/error.hpp>
#include <os/kernel/aarch64.hpp>
#include <os/kernel/machine_aarch64.hpp>

#if !defined(__aarch64__)
#error "machine_aarch64.cpp must only be compiled for AArch64"
#endif

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error machine_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] std::uint32_t counter_frequency_hz() noexcept {
    std::uint64_t raw = 0ULL;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(raw));
    return static_cast<std::uint32_t>(raw & 0xFFFF'FFFFULL);
}

[[nodiscard]] std::uint64_t physical_counter() noexcept {
    std::uint64_t value = 0ULL;
    asm volatile("mrs %0, cntpct_el0" : "=r"(value));
    return value;
}

} // namespace

std::size_t machine_page_size() noexcept {
    return static_cast<std::size_t>(aarch64::architectural_page_size);
}

os::core::Result<void> machine_bind_address_space(
    MachineAddressSpace& space,
    MachinePhysicalLedger& ledger) noexcept {
    (void)space;
    (void)ledger;
    return machine_error(machine_errors::unsupported);
}

os::core::Result<void> machine_release_address_space(MachineAddressSpace& space) noexcept {
    (void)space;
    return machine_error(machine_errors::unsupported);
}

void machine_switch_context(MachineContext& from, MachineContext& to) noexcept {
    // Unlike the former fail-closed stub, this is now a real AArch64 kernel
    // context switch. The target must have been prepared by the machine layer;
    // switching to arbitrary disk/user bytes as a register frame would turn a
    // corrupted context object directly into control-flow authority.
    if (!to.prepared) {
        __builtin_trap();
    }
    cookie_aarch64_switch_context(&from, &to);
}

os::core::Result<void> machine_map_kernel_stack(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length) noexcept {
    (void)space;
    (void)virtual_base;
    (void)physical_base;
    (void)length;
    return machine_error(machine_errors::unsupported);
}

os::core::Result<void> machine_prepare_context(
    MachineContext& context,
    MachineAddressSpace& space,
    std::uintptr_t entry,
    std::uintptr_t stack) noexcept {
    (void)context;
    (void)space;
    (void)entry;
    (void)stack;
    // Preparing the first return address is inseparable from proving that the
    // requested stack belongs to this address space and has its required guard
    // page. M7.5c supplies that real mapping state; accepting an arbitrary stack
    // here would defeat the guard-page invariant merely to make switching demo.
    return machine_error(machine_errors::unsupported);
}

os::core::Result<void> machine_map(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length,
    MachinePermissions permissions,
    MachineMemoryKind kind) noexcept {
    (void)space;
    (void)virtual_base;
    (void)physical_base;
    (void)length;
    (void)permissions;
    (void)kind;
    return machine_error(machine_errors::unsupported);
}

os::core::Result<void> machine_unmap(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::size_t length) noexcept {
    (void)space;
    (void)virtual_base;
    (void)length;
    return machine_error(machine_errors::unsupported);
}

os::core::Result<void> machine_mask_interrupt(std::uint32_t source) noexcept {
    (void)source;
    return machine_error(machine_errors::unsupported);
}

os::core::Result<void> machine_unmask_interrupt(std::uint32_t source) noexcept {
    (void)source;
    return machine_error(machine_errors::unsupported);
}

os::core::Result<void> machine_acknowledge_interrupt(std::uint32_t source) noexcept {
    (void)source;
    return machine_error(machine_errors::unsupported);
}

os::core::Result<void> machine_set_timer(std::uint64_t nanoseconds) noexcept {
    const std::uint32_t frequency = counter_frequency_hz();
    auto ticks = aarch64::nanoseconds_to_ticks(nanoseconds, frequency);
    if (!ticks) return ticks.error();

    const std::uint64_t now = physical_counter();
    if (ticks.value() > (std::numeric_limits<std::uint64_t>::max() - now)) {
        return aarch64::error(aarch64::errors::timer_out_of_range);
    }
    const std::uint64_t deadline = now + ticks.value();

    asm volatile("msr cntp_cval_el0, %0" :: "r"(deadline) : "memory");
    asm volatile("isb" ::: "memory");
    const std::uint64_t enable = 1ULL;
    asm volatile("msr cntp_ctl_el0, %0" :: "r"(enable) : "memory");
    asm volatile("isb" ::: "memory");
    return {};
}

std::uint64_t machine_monotonic_nanoseconds() noexcept {
    return aarch64::ticks_to_nanoseconds_saturating(
        physical_counter(), counter_frequency_hz());
}

} // namespace os::kernel
