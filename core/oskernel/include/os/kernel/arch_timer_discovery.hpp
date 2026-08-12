#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/fdt.hpp>

namespace os::kernel {

namespace arch_timer_discovery_errors {
inline constexpr std::uint32_t not_found = 100U;
inline constexpr std::uint32_t ambiguous = 101U;
inline constexpr std::uint32_t malformed = 102U;
inline constexpr std::uint32_t unsupported_interrupt_cells = 103U;
inline constexpr std::uint32_t unsupported_interrupt_type = 104U;
} // namespace arch_timer_discovery_errors

struct ArchitectedTimerDiscovery final {
    // Architectural interrupt ID delivered by the interrupt controller. For a
    // standard GIC PPI this is 16 + the DT PPI number, but callers never need to
    // know that encoding rule.
    std::uint32_t nonsecure_physical_intid {0U};
    std::uint32_t trigger_flags {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        // SGIs occupy 0..15. The architected physical timer is described as a
        // PPI/E-PPI and must never decode into SGI space.
        return nonsecure_physical_intid >= 16U && nonsecure_physical_intid < 1120U;
    }
};

// Finds exactly one architected timer node and decodes the EL1 non-secure
// physical timer interrupt. Cookie uses firmware topology instead of assuming a
// fixed PPI number, keeping QEMU and real boards on the same path.
[[nodiscard]] os::core::Result<ArchitectedTimerDiscovery>
discover_architected_timer(const FdtView& fdt) noexcept;

} // namespace os::kernel
