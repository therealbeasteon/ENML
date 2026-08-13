#include <os/kernel/aarch64_translation.hpp>

#include <cstdint>

#include <os/core/error.hpp>

#if !defined(__aarch64__)
#error "aarch64_translation.cpp must only be compiled for AArch64"
#endif

namespace os::kernel::aarch64 {
namespace {

[[nodiscard]] constexpr os::core::Error machine_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] std::uint8_t hardware_parange() noexcept {
    std::uint64_t mmfr0 = 0ULL;
    asm volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
    return static_cast<std::uint8_t>(mmfr0 & 0xFULL);
}

} // namespace

os::core::Result<void>
activate_stage1_translation(std::uint64_t level1_root_physical) noexcept {
    if (!stage1_physical_address(level1_root_physical)) {
        return machine_error(machine_errors::alignment);
    }

    const std::uint8_t parange = hardware_parange();
    if (parange > 6U) {
        return machine_error(machine_errors::unsupported);
    }
    const std::uint64_t tcr = tcr_el1_for_ips(cookie_ips(parange));
    if (tcr == 0ULL) {
        return machine_error(machine_errors::unsupported);
    }

    // Translation configuration must be globally visible before the MMU is
    // enabled. Cookie begins with TTBR0_EL1 only; TTBR1 walks are disabled in
    // TCR_EL1 until the higher-half kernel layout is separately reviewed.
    asm volatile("msr mair_el1, %0" :: "r"(default_mair_el1) : "memory");
    asm volatile("msr tcr_el1, %0" :: "r"(tcr) : "memory");
    asm volatile("msr ttbr0_el1, %0" :: "r"(level1_root_physical) : "memory");
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb" ::: "memory");

    // Remove any stale stage-1 entries from firmware/earlier boot state before
    // enabling the new regime.
    asm volatile("tlbi vmalle1" ::: "memory");
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb" ::: "memory");

    std::uint64_t sctlr = 0ULL;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    constexpr std::uint64_t m = 1ULL << 0U;
    constexpr std::uint64_t c = 1ULL << 2U;
    constexpr std::uint64_t i = 1ULL << 12U;
    sctlr |= m | c | i;
    asm volatile("msr sctlr_el1, %0" :: "r"(sctlr) : "memory");
    asm volatile("isb" ::: "memory");

    return {};
}

} // namespace os::kernel::aarch64
