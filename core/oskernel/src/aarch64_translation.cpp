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

bool privileged_access_never_available() noexcept {
    std::uint64_t mmfr1 = 0ULL;
    asm volatile("mrs %0, id_aa64mmfr1_el1" : "=r"(mmfr1));
    // PAN is ID_AA64MMFR1_EL1[23:20]. Zero means not implemented; every
    // non-zero encoding implements at least the SPAN behaviour Cookie uses, so
    // this is a "greater than zero" question rather than a version comparison.
    return ((mmfr1 >> 20U) & 0xFULL) != 0ULL;
}

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

    // Written, not read-modify-written. The previous version set three bits and
    // inherited every other one from firmware - including EL0 endianness, stack
    // alignment checking, hardware W^X, and whether EL0 could execute cache
    // maintenance. See cookie_sctlr_el1 for what each of those decides.
    asm volatile("msr sctlr_el1, %0"
                 :: "r"(cookie_sctlr_el1(privileged_access_never_available()))
                 : "memory");
    asm volatile("isb" ::: "memory");

    return {};
}

os::core::Result<void> establish_execution_controls() noexcept {
    asm volatile("msr cpacr_el1, %0" :: "r"(cookie_cpacr_el1()) : "memory");
    asm volatile("isb" ::: "memory");

    // Read back. FPEN, ZEN and SMEN must all read as trapped; a core that
    // ignored the write would leave Cookie believing it had denied a register
    // file it is not preserving, which is worse than never having tried.
    std::uint64_t cpacr = 0ULL;
    asm volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
    constexpr std::uint64_t fpen = 3ULL << 20U;
    constexpr std::uint64_t zen = 3ULL << 16U;
    constexpr std::uint64_t smen = 3ULL << 24U;
    if ((cpacr & (fpen | zen | smen)) != 0ULL) {
        return machine_error(machine_errors::unsupported);
    }
    return {};
}

} // namespace os::kernel::aarch64
