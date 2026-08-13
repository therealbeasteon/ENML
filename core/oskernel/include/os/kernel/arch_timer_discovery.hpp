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

// Linux/DT GIC interrupt specifiers encode the trigger sense in the low nibble;
// PPI affinity/CPU-mask information may occupy higher bits. Architected timer
// PPIs are level-sensitive. Firmware seen in the field (including QEMU virt)
// may describe the line as either level-high or level-low, so Cookie validates
// the electrical class without confusing affinity bits for trigger bits.
inline constexpr std::uint32_t dt_irq_sense_mask = 0xFU;
inline constexpr std::uint32_t dt_irq_level_high = 4U;
inline constexpr std::uint32_t dt_irq_level_low = 8U;

[[nodiscard]] constexpr bool architected_timer_trigger_supported(
    std::uint32_t flags) noexcept {
    const auto sense = flags & dt_irq_sense_mask;
    return sense == dt_irq_level_high || sense == dt_irq_level_low;
}

struct ArchitectedTimerDiscovery final {
    // Architectural interrupt ID delivered by the interrupt controller. For a
    // standard GIC PPI this is 16 + the DT PPI number, but callers never need to
    // know that encoding rule.
    std::uint32_t nonsecure_physical_intid {0U};
    std::uint32_t trigger_flags {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        // SGIs occupy 0..15. The architected physical timer is described as a
        // PPI/E-PPI and must never decode into SGI space. Its IRQ must also be
        // level-sensitive; edge-triggered timer descriptions are rejected.
        return nonsecure_physical_intid >= 16U && nonsecure_physical_intid < 1120U &&
               architected_timer_trigger_supported(trigger_flags);
    }
};

// Finds exactly one architected timer node and decodes the EL1 non-secure
// physical timer interrupt. Cookie uses firmware topology instead of assuming a
// fixed PPI number, keeping QEMU and real boards on the same path.
[[nodiscard]] os::core::Result<ArchitectedTimerDiscovery>
discover_architected_timer(const FdtView& fdt) noexcept;

} // namespace os::kernel
