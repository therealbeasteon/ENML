#include <os/kernel/machine.hpp>

#include <cstdint>
#include <limits>

#include <os/core/error.hpp>
#include <os/core/panic.hpp>
#include <os/kernel/aarch64.hpp>
#include <os/kernel/aarch64_asid.hpp>
#include <os/kernel/aarch64_translation.hpp>
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

void invalidate_stage1_pages(std::uint64_t virtual_base, std::uint64_t page_count) noexcept {
    // Break-before-make / teardown ordering for the current TTBR0_EL1 regime:
    // make descriptor clears visible first, invalidate each VA for all ASIDs in
    // the inner-shareable domain, then synchronize completion before execution
    // can continue using the retired authority.
    asm volatile("dsb ishst" ::: "memory");
    for (std::uint64_t page = 0ULL; page < page_count; ++page) {
        const std::uint64_t operand =
            (virtual_base + page * aarch64::architectural_page_size) >> 12U;
        asm volatile("tlbi vaae1is, %0" :: "r"(operand) : "memory");
    }
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb" ::: "memory");
}

} // namespace

std::size_t machine_page_size() noexcept {
    return static_cast<std::size_t>(aarch64::architectural_page_size);
}

os::core::Result<void> machine_bind_address_space(
    MachineAddressSpace& space,
    MachinePhysicalLedger& ledger) noexcept {
    if (space.physical_ledger != nullptr) {
        return machine_error(machine_errors::address_space_already_bound);
    }
    space.physical_ledger = &ledger;
    return {};
}

os::core::Result<void> aarch64_attach_early_stage1(
    MachineAddressSpace& space,
    aarch64::EarlyStage1Builder& builder) noexcept {
    if (space.physical_ledger == nullptr) return machine_error(machine_errors::address_space_unbound);
    if (space.early_builder != nullptr) return machine_error(machine_errors::address_space_already_bound);
    auto bound = space.mappings.bind(space.physical_ledger->mappings, builder);
    if (!bound) return bound.error();
    space.early_builder = &builder;
    return {};
}

os::core::Result<void> aarch64_reserve_physical(
    MachineAddressSpace& space,
    std::uintptr_t physical_base,
    std::size_t length,
    aarch64::PhysicalReservationKind kind) noexcept {
    if (space.early_builder == nullptr) return machine_error(machine_errors::address_space_unbound);
    return space.mappings.reserve_physical(
        static_cast<std::uint64_t>(physical_base),
        static_cast<std::uint64_t>(length),
        kind);
}

os::core::Result<void> aarch64_donate_table_page(
    MachineAddressSpace& space,
    aarch64::EarlyPageArena& arena,
    std::uintptr_t physical) noexcept {
    if (space.early_builder == nullptr || space.physical_ledger == nullptr) {
        return machine_error(machine_errors::address_space_unbound);
    }

    // Reserve first. reserve_physical re-checks every live mapping of the range
    // and refuses if any is user-accessible, so a donor that still has the page
    // mapped is rejected before the page can become a translation table it
    // could keep writing.
    auto reserved = space.mappings.reserve_physical(
        static_cast<std::uint64_t>(physical),
        aarch64::architectural_page_size,
        aarch64::PhysicalReservationKind::kernel_object);
    if (!reserved) return reserved.error();

    // If the arena refuses the page the reservation stands, and that is the
    // safe direction: a reserved page nobody uses is wasted, an unreserved page
    // in use as a table is a hole. Releasing it here would also be wrong, since
    // release_reservations drops every reservation this space owns and cannot
    // undo just this one.
    return arena.donate(static_cast<std::uint64_t>(physical));
}

os::core::Result<void> aarch64_map_user(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length,
    MachinePermissions permissions) noexcept {
    if (space.early_builder == nullptr) return machine_error(machine_errors::address_space_unbound);
    return space.mappings.map_user(
        static_cast<std::uint64_t>(virtual_base),
        static_cast<std::uint64_t>(physical_base),
        static_cast<std::uint64_t>(length),
        permissions);
}

os::core::Result<void> aarch64_map_user_stack(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length) noexcept {
    if (space.early_builder == nullptr) return machine_error(machine_errors::address_space_unbound);
    return space.mappings.map_user_stack(
        static_cast<std::uint64_t>(virtual_base),
        static_cast<std::uint64_t>(physical_base),
        static_cast<std::uint64_t>(length));
}

os::core::Result<void> aarch64_validate_user_context(
    MachineAddressSpace& space,
    std::uintptr_t entry,
    std::uintptr_t stack) noexcept {
    if (space.early_builder == nullptr) return machine_error(machine_errors::address_space_unbound);
    if (entry == 0U || stack == 0U ||
        !aarch64::stage1_virtual_address(static_cast<std::uint64_t>(entry)) ||
        !aarch64::page_aligned(static_cast<std::uint64_t>(stack))) {
        return machine_error(machine_errors::invalid_range);
    }
    if (!space.mappings.valid_user_entry(static_cast<std::uint64_t>(entry))) {
        return machine_error(machine_errors::not_mapped);
    }
    if (!space.mappings.valid_user_stack_top(static_cast<std::uint64_t>(stack))) {
        return machine_error(machine_errors::not_a_kernel_stack);
    }
    return {};
}

os::core::Result<void> machine_release_address_space(MachineAddressSpace& space) noexcept {
    // Still unsupported, and now for a stated reason rather than a missing
    // implementation. Bulk release is safe only once no CPU can be executing
    // in the space, and this signature cannot carry that proof: it takes the
    // space and nothing else. aarch64_release_address_space takes the retiring
    // epoch that establishes it. The portable contract asks for a guarantee
    // from an argument list that cannot express it, which is a defect in the
    // contract rather than a gap in this port.
    (void)space;
    return machine_error(machine_errors::unsupported);
}

os::core::Result<void> aarch64_release_address_space(
    MachineAddressSpace& space,
    RetiringAddressSpaceEpoch retiring) noexcept {
    if (space.early_builder == nullptr || space.physical_ledger == nullptr) {
        return machine_error(machine_errors::address_space_unbound);
    }
    if (!retiring.valid()) return machine_error(machine_errors::invalid_range);

    // Mappings first, one at a time through the same path an individual unmap
    // takes. Reservations are dropped only after the last translation is gone:
    // a reserved range that stops being reserved while it is still mapped is
    // briefly mappable from EL0 by anyone, which is the window the reservation
    // exists to close.
    for (;;) {
        auto mapping = space.mappings.any_mapping();
        if (!mapping) {
            const auto error = mapping.error();
            if (error.domain == os::core::ErrorDomain::kernel &&
                error.code == machine_errors::not_mapped) break;
            return error;
        }
        auto unmapped = machine_unmap(
            space,
            static_cast<std::uintptr_t>(mapping.value().virtual_base),
            static_cast<std::size_t>(mapping.value().length));
        if (!unmapped) return unmapped.error();
    }

    auto released = space.mappings.release_reservations();
    if (!released) return released.error();

    // The ASID's own invalidation, which the epoch authority requires before it
    // will complete retirement. Ordered after the per-mapping TLBIs rather than
    // instead of them: those retire the translations, this retires the tag they
    // were cached under, and a later space reusing the ASID must not inherit
    // either.
    auto asid_retired = aarch64::retire_process_asid(retiring);
    if (!asid_retired) return asid_retired.error();

    auto unbound = space.mappings.unbind();
    if (!unbound) return unbound.error();

    // Clear the outer handle too, or release is a one-way trip:
    // machine_bind_address_space refuses a space whose physical_ledger is still
    // set, so leaving these would make a released MachineAddressSpace
    // permanently unusable and defeat the reuse this milestone exists for.
    // Ordered last, after unbind() has proved nothing is still owned.
    space.physical_ledger = nullptr;
    space.early_builder = nullptr;
    return {};
}

void machine_switch_context(MachineContext& from, MachineContext& to) noexcept {
    if (!to.prepared) __builtin_trap();
    cookie_aarch64_switch_context(&from, &to);
}

os::core::Result<void> machine_map_kernel_stack(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length) noexcept {
    if (space.early_builder == nullptr) return machine_error(machine_errors::address_space_unbound);
    return space.mappings.map_kernel_stack(
        static_cast<std::uint64_t>(virtual_base),
        static_cast<std::uint64_t>(physical_base),
        static_cast<std::uint64_t>(length));
}

os::core::Result<void> machine_prepare_context(
    MachineContext& context,
    MachineAddressSpace& space,
    std::uintptr_t entry,
    std::uintptr_t stack) noexcept {
    if (space.early_builder == nullptr) return machine_error(machine_errors::address_space_unbound);
    if (entry == 0U || stack == 0U ||
        !aarch64::stage1_virtual_address(static_cast<std::uint64_t>(entry)) ||
        !aarch64::page_aligned(static_cast<std::uint64_t>(stack))) {
        return machine_error(machine_errors::invalid_range);
    }
    if (!space.mappings.valid_kernel_stack_top(static_cast<std::uint64_t>(stack))) {
        return machine_error(machine_errors::not_a_kernel_stack);
    }
    context = MachineContext{};
    context.x30 = static_cast<std::uint64_t>(entry);
    context.sp = static_cast<std::uint64_t>(stack);
    context.prepared = true;
    return {};
}

os::core::Result<void> machine_map(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length,
    MachinePermissions permissions,
    MachineMemoryKind kind) noexcept {
    if (space.early_builder == nullptr) return machine_error(machine_errors::address_space_unbound);
    return space.mappings.map(
        static_cast<std::uint64_t>(virtual_base),
        static_cast<std::uint64_t>(physical_base),
        static_cast<std::uint64_t>(length),
        permissions,
        kind);
}

os::core::Result<void> machine_unmap(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::size_t length) noexcept {
    if (space.early_builder == nullptr || space.physical_ledger == nullptr) {
        return machine_error(machine_errors::address_space_unbound);
    }
    if (length == 0U ||
        !aarch64::page_aligned(static_cast<std::uint64_t>(virtual_base)) ||
        !aarch64::page_aligned(static_cast<std::uint64_t>(length))) {
        return machine_error(machine_errors::invalid_range);
    }
    const auto exact = space.mappings.exact_mapping(
        static_cast<std::uint64_t>(virtual_base),
        static_cast<std::uint64_t>(length));
    if (!exact) return exact.error();

    const std::uint64_t page_count =
        static_cast<std::uint64_t>(length) / aarch64::architectural_page_size;
    for (std::uint64_t page = 0ULL; page < page_count; ++page) {
        auto state = space.early_builder->mapped(
            static_cast<std::uint64_t>(virtual_base) + page * aarch64::architectural_page_size);
        if (!state) return state.error();
        if (!state.value()) return machine_error(machine_errors::mapping_ledger_inconsistent);
    }

    // No recoverable failures remain after this boundary. A failed clear now
    // means the single-threaded table state changed underneath its own verified
    // ledger; returning would expose a partially retired mapping.
    for (std::uint64_t page = 0ULL; page < page_count; ++page) {
        auto cleared = space.early_builder->unmap_page(
            static_cast<std::uint64_t>(virtual_base) + page * aarch64::architectural_page_size);
        if (!cleared) os::core::invariant_violated();
    }
    invalidate_stage1_pages(static_cast<std::uint64_t>(virtual_base), page_count);
    auto retired = space.mappings.retire_unmapped(
        static_cast<std::uint64_t>(virtual_base),
        static_cast<std::uint64_t>(length));
    if (!retired) os::core::invariant_violated();
    return {};
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

os::core::Result<void> machine_cancel_timer() noexcept {
    const std::uint64_t disabled = 0ULL;
    asm volatile("msr cntp_ctl_el0, %0" :: "r"(disabled) : "memory");
    asm volatile("isb" ::: "memory");
    return {};
}

std::uint64_t machine_monotonic_nanoseconds() noexcept {
    return aarch64::ticks_to_nanoseconds_saturating(
        physical_counter(), counter_frequency_hz());
}

} // namespace os::kernel
