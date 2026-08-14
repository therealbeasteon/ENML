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

// Minimal primary-CPU GICv3 state. Cookie intentionally starts with one CPU
// and two PPIs - the scheduler's own preemption timer, and a second PPI M7.9
// routes through Kernel::dispatch_interrupt() as a stand-in device source
// (see docs/M7_9_USER_SPACE_DRIVERS.md). SMP, SPIs, ITS/MSIs and interrupt
// affinity are separate milestones.
struct GicV3PrimaryCpu final {
    std::uintptr_t distributor {0U};
    std::uintptr_t redistributor {0U};
    std::uint32_t timer_intid {0U};
    std::uint32_t device_intid {0U};
    bool initialized {false};
};

// The supplied GICD/GICR ranges must already be mapped as device memory in the
// active EL1 translation regime. Initialization uses finite wait budgets and
// configures both requested PPIs as Group-1 non-secure; every other line at
// this redistributor is left disabled, so nothing else can assert. Only
// timer_intid is left enabled - device_intid is configured but stays masked
// until something actually attaches to it (gic_v3_set_ppi_masked), so a
// source with no owner cannot assert at all rather than merely being unable
// to reach one.
[[nodiscard]] os::core::Result<GicV3PrimaryCpu>
initialize_gic_v3_primary_cpu(
    const GicV3Discovery& topology,
    std::uint32_t timer_intid,
    std::uint32_t device_intid) noexcept;

// Acknowledge/EOI through the GICv3 system-register CPU interface. Special
// spurious INTIDs are returned to the caller and must not be EOIed.
[[nodiscard]] std::uint32_t gic_v3_acknowledge() noexcept;
void gic_v3_end_interrupt(std::uint32_t intid) noexcept;

// Masks or unmasks a single already-configured PPI at the redistributor
// (GICR_ICENABLER0/ISENABLER0), without touching the distributor or any other
// line. This is the machine-layer half of InterruptTable's mask-until-
// complete rule: the table decides a source is masked the instant it is
// dispatched, and this is what makes that decision true in hardware, on the
// timeline docs/M7_1_INTERRUPT.md specifies - masked from dispatch, unmasked
// only once Kernel::interrupt_complete succeeds. `intid` must already have
// been enabled by initialize_gic_v3_primary_cpu; this function does not
// configure group, trigger or priority.
void gic_v3_set_ppi_masked(
    std::uintptr_t redistributor, std::uint32_t intid, bool masked) noexcept;

} // namespace os::kernel::aarch64
