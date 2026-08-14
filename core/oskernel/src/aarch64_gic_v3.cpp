#include <os/kernel/aarch64_gic_v3.hpp>

#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>

#if !defined(__aarch64__)
#error "aarch64_gic_v3.cpp must only be compiled for AArch64"
#endif

namespace os::kernel::aarch64 {
namespace {

constexpr std::uint64_t default_redistributor_stride = 0x20000ULL;
constexpr std::uint64_t gicr_sgi_base = 0x10000ULL;
constexpr std::uint64_t gicr_typer = 0x0008ULL;
constexpr std::uint64_t gicr_waker = 0x0014ULL;
constexpr std::uint64_t gicr_igroupr0 = gicr_sgi_base + 0x0080ULL;
constexpr std::uint64_t gicr_isenabler0 = gicr_sgi_base + 0x0100ULL;
constexpr std::uint64_t gicr_icenabler0 = gicr_sgi_base + 0x0180ULL;
constexpr std::uint64_t gicr_icpendr0 = gicr_sgi_base + 0x0280ULL;
constexpr std::uint64_t gicr_ipriorityr = gicr_sgi_base + 0x0400ULL;
constexpr std::uint64_t gicr_icfgr1 = gicr_sgi_base + 0x0C04ULL;

constexpr std::uint64_t gicd_ctlr = 0x0000ULL;
constexpr std::uint32_t gicd_ctlr_enable_grp1ns = 1U << 1U;
constexpr std::uint32_t gicd_ctlr_are_ns = 1U << 4U;
constexpr std::uint32_t gicd_ctlr_rwp = 1U << 31U;

constexpr std::uint32_t gicr_waker_processor_sleep = 1U << 1U;
constexpr std::uint32_t gicr_waker_children_asleep = 1U << 2U;
constexpr std::uint64_t gicr_typer_last = 1ULL << 4U;
constexpr std::uint32_t max_poll_iterations = 1'000'000U;
// Shared by every PPI this port enables - there is exactly one priority
// level in use today, so nothing yet depends on the timer preempting the
// M7.9 device PPI or vice versa. Per-PPI priority is a later concern.
constexpr std::uint8_t ppi_priority = 0x80U;

[[nodiscard]] constexpr os::core::Error gic_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

template <typename T>
[[nodiscard]] volatile T* reg(std::uintptr_t base, std::uint64_t offset) noexcept {
    return reinterpret_cast<volatile T*>(base + static_cast<std::uintptr_t>(offset));
}

[[nodiscard]] std::uint32_t read32(std::uintptr_t base, std::uint64_t offset) noexcept {
    return *reg<std::uint32_t>(base, offset);
}

void write32(std::uintptr_t base, std::uint64_t offset, std::uint32_t value) noexcept {
    *reg<std::uint32_t>(base, offset) = value;
}

[[nodiscard]] std::uint64_t read64(std::uintptr_t base, std::uint64_t offset) noexcept {
    return *reg<std::uint64_t>(base, offset);
}

[[nodiscard]] bool wait_distributor(std::uintptr_t distributor) noexcept {
    for (std::uint32_t i = 0U; i < max_poll_iterations; ++i) {
        if ((read32(distributor, gicd_ctlr) & gicd_ctlr_rwp) == 0U) return true;
    }
    return false;
}

[[nodiscard]] std::uint32_t current_affinity() noexcept {
    std::uint64_t mpidr = 0ULL;
    asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    const std::uint32_t aff0 = static_cast<std::uint32_t>(mpidr & 0xFFULL);
    const std::uint32_t aff1 = static_cast<std::uint32_t>((mpidr >> 8U) & 0xFFULL);
    const std::uint32_t aff2 = static_cast<std::uint32_t>((mpidr >> 16U) & 0xFFULL);
    const std::uint32_t aff3 = static_cast<std::uint32_t>((mpidr >> 32U) & 0xFFULL);
    return (aff3 << 24U) | (aff2 << 16U) | (aff1 << 8U) | aff0;
}

[[nodiscard]] std::uintptr_t find_redistributor(const GicV3Discovery& topology) noexcept {
    const std::uint32_t affinity = current_affinity();
    const std::uint64_t stride = topology.redistributor_stride == 0ULL
        ? default_redistributor_stride
        : topology.redistributor_stride;

    for (std::size_t region = 0U; region < topology.redistributor_count; ++region) {
        const auto& range = topology.redistributors[region];
        if (!range.valid() || stride < default_redistributor_stride ||
            range.size < default_redistributor_stride) continue;
        for (std::uint64_t offset = 0ULL;
             offset <= range.size - default_redistributor_stride;
             offset += stride) {
            const auto base = static_cast<std::uintptr_t>(range.base + offset);
            const std::uint64_t typer = read64(base, gicr_typer);
            const auto typer_affinity = static_cast<std::uint32_t>(typer >> 32U);
            if (typer_affinity == affinity) return base;
            if ((typer & gicr_typer_last) != 0ULL) break;
            if (offset > UINT64_MAX - stride) break;
        }
    }
    return 0U;
}

[[nodiscard]] bool wake_redistributor(std::uintptr_t redistributor) noexcept {
    auto waker = read32(redistributor, gicr_waker);
    waker &= ~gicr_waker_processor_sleep;
    write32(redistributor, gicr_waker, waker);
    for (std::uint32_t i = 0U; i < max_poll_iterations; ++i) {
        if ((read32(redistributor, gicr_waker) & gicr_waker_children_asleep) == 0U) return true;
    }
    return false;
}

// Groups, configures as level-triggered, prioritizes and clears any latched
// pending state for one PPI at an already-woken redistributor. Shared by
// every PPI this port enables, so the timer and the M7.9 device PPI are
// configured identically rather than by two hand-copied blocks that could
// drift. Whether the line is left enabled is the caller's decision, not this
// function's - the timer must be live from the first scheduling decision,
// but a device PPI with no owner yet should not be able to assert at all, so
// it stays disabled here and is enabled only once something attaches to it
// (cookie_kernel_syscall_entry's interrupt_attach dispatch).
void configure_ppi(std::uintptr_t redistributor, std::uint32_t intid, bool enabled) noexcept {
    // IGROUPR0/ISENABLER0/ICENABLER0/ICPENDR0 are one 32-bit register each,
    // spanning SGIs 0-15 and PPIs 16-31 together, indexed directly by intid.
    // ICFGR1 is not - it covers only the PPI half (ICFGR0 holds the SGIs),
    // two bits per interrupt, indexed by intid - 16. The two shifts below are
    // not the same quantity and must not be collapsed into one.
    const std::uint32_t bit = 1U << intid;

    auto group = read32(redistributor, gicr_igroupr0);
    group |= bit;
    write32(redistributor, gicr_igroupr0, group);

    auto config = read32(redistributor, gicr_icfgr1);
    const std::uint32_t ppi_index = intid - 16U;
    const std::uint32_t field_shift = ppi_index * 2U;
    config &= ~(0x3U << field_shift);
    write32(redistributor, gicr_icfgr1, config);

    *reg<std::uint8_t>(redistributor, gicr_ipriorityr + intid) = ppi_priority;
    write32(redistributor, gicr_icpendr0, bit);
    write32(redistributor, enabled ? gicr_isenabler0 : gicr_icenabler0, bit);
}

[[nodiscard]] bool enable_system_register_interface() noexcept {
    std::uint64_t sre = 0ULL;
    asm volatile("mrs %0, icc_sre_el1" : "=r"(sre));
    sre |= 1ULL;
    asm volatile("msr icc_sre_el1, %0" :: "r"(sre) : "memory");
    asm volatile("isb" ::: "memory");
    asm volatile("mrs %0, icc_sre_el1" : "=r"(sre));
    if ((sre & 1ULL) == 0ULL) return false;

    const std::uint64_t priority_mask = 0xFFULL;
    const std::uint64_t binary_point = 0ULL;
    const std::uint64_t enable_group1 = 1ULL;
    asm volatile("msr icc_pmr_el1, %0" :: "r"(priority_mask) : "memory");
    asm volatile("msr icc_bpr1_el1, %0" :: "r"(binary_point) : "memory");
    asm volatile("msr icc_igrpen1_el1, %0" :: "r"(enable_group1) : "memory");
    asm volatile("isb" ::: "memory");
    return true;
}

} // namespace

os::core::Result<GicV3PrimaryCpu> initialize_gic_v3_primary_cpu(
    const GicV3Discovery& topology,
    std::uint32_t timer_intid,
    std::uint32_t device_intid) noexcept {
    if (!topology.valid()) return gic_error(gic_v3_errors::invalid_topology);
    if (timer_intid < 16U || timer_intid > 31U ||
        device_intid < 16U || device_intid > 31U ||
        device_intid == timer_intid) {
        return gic_error(gic_v3_errors::unsupported_interrupt);
    }

    const auto distributor = static_cast<std::uintptr_t>(topology.distributor.base);
    const auto redistributor = find_redistributor(topology);
    if (redistributor == 0U) return gic_error(gic_v3_errors::redistributor_not_found);

    write32(distributor, gicd_ctlr, 0U);
    if (!wait_distributor(distributor)) return gic_error(gic_v3_errors::distributor_timeout);
    if (!wake_redistributor(redistributor)) return gic_error(gic_v3_errors::wake_timeout);

    configure_ppi(redistributor, timer_intid, true);
    configure_ppi(redistributor, device_intid, false);

    write32(distributor, gicd_ctlr, gicd_ctlr_are_ns | gicd_ctlr_enable_grp1ns);
    if (!wait_distributor(distributor)) return gic_error(gic_v3_errors::distributor_timeout);
    if (!enable_system_register_interface()) {
        return gic_error(gic_v3_errors::system_register_interface_unavailable);
    }

    return GicV3PrimaryCpu{
        .distributor = distributor,
        .redistributor = redistributor,
        .timer_intid = timer_intid,
        .device_intid = device_intid,
        .initialized = true,
    };
}

std::uint32_t gic_v3_acknowledge() noexcept {
    std::uint64_t value = 0ULL;
    asm volatile("mrs %0, icc_iar1_el1" : "=r"(value));
    return static_cast<std::uint32_t>(value & 0x00FF'FFFFULL);
}

void gic_v3_end_interrupt(std::uint32_t intid) noexcept {
    const std::uint64_t value = intid;
    asm volatile("msr icc_eoir1_el1, %0" :: "r"(value) : "memory");
    asm volatile("isb" ::: "memory");
}

void gic_v3_set_ppi_masked(
    std::uintptr_t redistributor, std::uint32_t intid, bool masked) noexcept {
    const std::uint32_t bit = 1U << intid;
    write32(redistributor, masked ? gicr_icenabler0 : gicr_isenabler0, bit);
}

} // namespace os::kernel::aarch64
