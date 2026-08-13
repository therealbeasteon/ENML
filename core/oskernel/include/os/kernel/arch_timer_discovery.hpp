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
inline constexpr std::uint32_t unsupported_trigger = 105U;
} // namespace arch_timer_discovery_errors

// GIC DT interrupt specifiers encode electrical sense in the low nibble; PPI
// affinity/CPU-mask information can occupy higher bits. Cookie's GIC runtime
// only needs edge-vs-level for this architectural PPI, but discovery preserves
// the raw firmware value for diagnostics and later board-specific policy.
inline constexpr std::uint32_t dt_irq_sense_mask = 0xFU;
inline constexpr std::uint32_t dt_irq_level_high = 4U;
inline constexpr std::uint32_t dt_irq_level_low = 8U;

[[nodiscard]] constexpr bool architected_timer_trigger_supported(
    std::uint32_t flags) noexcept {
    const auto sense = flags & dt_irq_sense_mask;
    return sense == dt_irq_level_high || sense == dt_irq_level_low;
}

struct ArchitectedTimerDiscovery final {
    std::uint32_t nonsecure_physical_intid {0U};
    // Normalized trigger class consumed by the current GIC bring-up. Both
    // level-high and level-low firmware descriptions normalize to level-high
    // because the current controller programming only distinguishes level vs
    // edge. raw_trigger_flags retains the exact DT description.
    std::uint32_t trigger_flags {0U};
    std::uint32_t raw_trigger_flags {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return nonsecure_physical_intid >= 16U && nonsecure_physical_intid < 1120U &&
               trigger_flags == dt_irq_level_high &&
               architected_timer_trigger_supported(raw_trigger_flags);
    }
};

[[nodiscard]] os::core::Result<ArchitectedTimerDiscovery>
discover_architected_timer(const FdtView& fdt) noexcept;

} // namespace os::kernel
