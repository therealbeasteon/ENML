#include <os/kernel/aarch64_asid.hpp>

#include <os/core/error.hpp>
#include <os/kernel/machine.hpp>

#if !defined(__aarch64__)
#error "aarch64_asid.cpp must only be compiled for AArch64"
#endif

namespace os::kernel::aarch64 {
namespace {

[[nodiscard]] constexpr os::core::Error machine_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

} // namespace

os::core::Result<void> install_process_translation(
    std::uint64_t level1_root_physical,
    AddressSpaceEpoch epoch) noexcept {
    if (!epoch.valid()) return machine_error(machine_errors::invalid_range);
    const std::uint64_t ttbr = ttbr0_el1_value(level1_root_physical, epoch.asid);
    if (ttbr == 0ULL) return machine_error(machine_errors::invalid_range);

    // Unique, non-retired ASIDs let Cookie switch roots without flushing the
    // entire EL1 TLB. Ordering still matters: complete prior memory effects,
    // install the new translation identity, then synchronize instruction fetch.
    asm volatile("dsb ish" ::: "memory");
    asm volatile("msr ttbr0_el1, %0" :: "r"(ttbr) : "memory");
    asm volatile("isb" ::: "memory");
    return {};
}

os::core::Result<void> retire_process_asid(
    RetiringAddressSpaceEpoch retiring) noexcept {
    if (!retiring.valid()) return machine_error(machine_errors::invalid_range);
    const std::uint64_t operand = aside1is_operand(retiring.epoch.asid);
    if (operand == 0ULL) return machine_error(machine_errors::invalid_range);

    // Quarantine is released only after invalidating every stage-1 EL1&0 entry
    // tagged with this ASID. The software authority must remain in `retiring`
    // state until this sequence has completed.
    asm volatile("dsb ishst" ::: "memory");
    asm volatile("tlbi aside1is, %0" :: "r"(operand) : "memory");
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb" ::: "memory");
    return {};
}

} // namespace os::kernel::aarch64
