#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/gic_v3_discovery.hpp>

namespace os::kernel::aarch64 {

namespace gic_v3_errors {
inline constexpr std::uint32_t invalid_topology = 110U;
inline constexpr std::uint32_t redistributor_not_found = 111U;
inline constexpr std::uint32_t wake_timeout = 112U;
inline constexpr std::uint32_t distributor_timeout = 113U;
inline constexpr std::uint32_t system_register_interface_unavailable = 114U;
inline constexpr std::uint32_t unsupported_interrupt = 115U;
} // namespace gic_v3_errors

// Minimal primary-CPU GICv3 state. Cookie intentionally starts with one CPU and
// one PPI. SMP, SPIs, ITS/MSIs and interrupt affinity are separate milestones.
struct GicV3PrimaryCpu final {
    std::uintptr_t distributor {0U};
    std::uintptr_t redistributor {0U};
    std::uint32_t timer_intid {0U};
    bool initialized {false};
};

// The supplied GICD/GICR ranges must already be mapped as device memory in the
// active EL1 translation regime. Initialization uses finite wait budgets and
// enables only the requested architected timer PPI as Group-1 non-secure.
[[nodiscard]] os::core::Result<GicV3PrimaryCpu>
initialize_gic_v3_primary_cpu(
    const GicV3Discovery& topology,
    std::uint32_t timer_intid) noexcept;

// Acknowledge/EOI through the GICv3 system-register CPU interface. Special
// spurious INTIDs are returned to the caller and must not be EOIed.
[[nodiscard]] std::uint32_t gic_v3_acknowledge() noexcept;
void gic_v3_end_interrupt(std::uint32_t intid) noexcept;

} // namespace os::kernel::aarch64
