#include <os/kernel/aarch64_mapping_state.hpp>

#include <array>
#include <cstdlib>
#include <cstdint>

namespace {
// Templated so a Result can be passed directly. Result's operator bool is
// explicit, which satisfies the contextual conversion in `!value` but not
// an implicit conversion to a bool parameter.
template <typename T>
void require(const T& value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    alignas(4096) std::array<std::byte, 24U * 4096U> tables_a{};
    alignas(4096) std::array<std::byte, 24U * 4096U> tables_b{};
    const auto a_begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(tables_a.data()));
    const auto b_begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(tables_b.data()));

    EarlyPageArena arena_a{a_begin, a_begin + tables_a.size()};
    EarlyPageArena arena_b{b_begin, b_begin + tables_b.size()};
    EarlyStage1Builder builder_a{arena_a};
    EarlyStage1Builder builder_b{arena_b};
    require(static_cast<bool>(builder_a.initialize()));
    require(static_cast<bool>(builder_b.initialize()));

    NativePhysicalLedger ledger{};
    NativeAddressSpaceState space_a{};
    NativeAddressSpaceState space_b{};
    require(static_cast<bool>(space_a.bind(ledger, builder_a)));
    require(static_cast<bool>(space_b.bind(ledger, builder_b)));

    constexpr std::uint64_t physical = 0x0000'0000'8000'0000ULL;
    constexpr std::uint64_t va_a = 0x0000'0000'4000'0000ULL;
    constexpr std::uint64_t va_b = 0x0000'0000'5000'0000ULL;

    require(static_cast<bool>(space_a.map(
        va_a, physical, 4096ULL,
        MachinePermissions::read_write, MachineMemoryKind::normal)));
    require(space_a.mapping_count() == 1U);
    require(ledger.occupied == 1U);

    auto executable_alias = space_b.map(
        va_b, physical, 4096ULL,
        MachinePermissions::read_execute, MachineMemoryKind::normal);
    require(!executable_alias);
    require(executable_alias.error().code == machine_errors::writable_executable_alias);

    // A software-ledger retirement is forbidden while the page-table leaf is
    // still valid. Hardware authority must disappear first.
    auto early_retire = space_a.retire_unmapped(va_a, 4096ULL);
    require(!early_retire);
    require(early_retire.error().code == machine_errors::mapping_ledger_inconsistent);
    require(ledger.occupied == 1U);

    require(static_cast<bool>(builder_a.unmap_page(va_a)));
    require(static_cast<bool>(space_a.retire_unmapped(va_a, 4096ULL)));
    require(space_a.mapping_count() == 0U);
    require(ledger.occupied == 0U);

    // Once the writable translation and its machine-wide authority are both
    // gone, the physical page may legitimately be admitted executable.
    require(static_cast<bool>(space_b.map(
        va_b, physical, 4096ULL,
        MachinePermissions::read_execute, MachineMemoryKind::normal)));
    require(ledger.occupied == 1U);

    constexpr std::uint64_t user_code_va = 0x0000'0000'1000'0000ULL;
    constexpr std::uint64_t user_code_pa = 0x0000'0000'8200'0000ULL;
    require(static_cast<bool>(space_a.map_user(
        user_code_va, user_code_pa, 4096ULL,
        MachinePermissions::read_execute)));
    auto user_code = space_a.exact_mapping(user_code_va, 4096ULL);
    require(static_cast<bool>(user_code));
    require(user_code.value().user_accessible);
    require(!user_code.value().kernel_stack);
    require(!user_code.value().user_stack);
    require(user_code.value().kind == MachineMemoryKind::normal);
    require(space_a.valid_user_entry(user_code_va));
    require(space_a.valid_user_entry(user_code_va + 16ULL));
    require(!space_a.valid_user_entry(user_code_va + 4096ULL));

    auto writable_user_alias = space_b.map_user(
        user_code_va + 0x200000ULL, user_code_pa, 4096ULL,
        MachinePermissions::read_write);
    require(!writable_user_alias);
    require(writable_user_alias.error().code == machine_errors::writable_executable_alias);

    constexpr std::uint64_t user_stack_va = 0x0000'0000'1001'0000ULL;
    constexpr std::uint64_t user_stack_pa = 0x0000'0000'8201'0000ULL;
    require(static_cast<bool>(space_a.map_user_stack(
        user_stack_va, user_stack_pa, 4096ULL)));
    auto user_stack = space_a.exact_mapping(user_stack_va, 4096ULL);
    require(static_cast<bool>(user_stack));
    require(user_stack.value().user_accessible);
    require(user_stack.value().user_stack);
    require(user_stack.value().permissions == MachinePermissions::read_write);
    require(space_a.valid_user_stack_top(user_stack_va + 4096ULL));
    require(!space_a.valid_user_stack_top(user_stack_va + 2048ULL));

    auto user_guard = builder_a.mapped(user_stack_va - 4096ULL);
    require(static_cast<bool>(user_guard));
    require(!user_guard.value());

    // A mapped page occupying the guard position makes user-stack creation fail.
    constexpr std::uint64_t blocked_stack_va = 0x0000'0000'1003'0000ULL;
    require(static_cast<bool>(space_a.map_user(
        blocked_stack_va - 4096ULL,
        0x0000'0000'8202'0000ULL,
        4096ULL,
        MachinePermissions::read_write)));
    auto blocked_stack = space_a.map_user_stack(
        blocked_stack_va,
        0x0000'0000'8203'0000ULL,
        4096ULL);
    require(!blocked_stack);
    require(blocked_stack.error().code == machine_errors::missing_guard_page);

    constexpr std::uint64_t stack_va = 0x0000'0000'6000'1000ULL;
    constexpr std::uint64_t stack_pa = 0x0000'0000'8100'0000ULL;
    require(static_cast<bool>(space_a.map_kernel_stack(
        stack_va, stack_pa, 2ULL * 4096ULL)));
    require(space_a.valid_kernel_stack_top(stack_va + 2ULL * 4096ULL));
    require(!space_a.valid_kernel_stack_top(stack_va + 4096ULL));

    auto guard = builder_a.mapped(stack_va - 4096ULL);
    require(static_cast<bool>(guard));
    require(!guard.value());

    // ---------------------------------------------------------------------
    // Physical memory authority: a range that holds kernel state.
    // ---------------------------------------------------------------------
    constexpr std::uint64_t tables_pa = 0x0000'0000'8300'0000ULL;
    require(static_cast<bool>(space_a.reserve_physical(
        tables_pa, 2ULL * 4096ULL, PhysicalReservationKind::kernel_object)));
    require(ledger.reserved == 1U);

    // The owner writes it - this is the kernel editing its own tables, and the
    // only translation of the range that has a legitimate use.
    require(static_cast<bool>(space_a.map(
        0x0000'0000'6100'0000ULL, tables_pa, 4096ULL,
        MachinePermissions::read_write, MachineMemoryKind::normal)));

    // Nobody else writes it, even though W^X has no objection: two writable
    // mappings of the same range are exactly what the old check permits.
    auto foreign_write = space_b.map(
        0x0000'0000'6100'0000ULL, tables_pa, 4096ULL,
        MachinePermissions::read_write, MachineMemoryKind::normal);
    require(!foreign_write);
    require(foreign_write.error().code == machine_errors::kernel_object_alias);

    // No EL0 translation at all, owner included. A process that can write its
    // own page tables has no address space; one that can read them learns the
    // physical layout of every other.
    auto user_write = space_a.map_user(
        0x0000'0000'1100'0000ULL, tables_pa, 4096ULL,
        MachinePermissions::read_write);
    require(!user_write);
    require(user_write.error().code == machine_errors::kernel_object_alias);
    auto user_read = space_b.map_user(
        0x0000'0000'1100'0000ULL, tables_pa, 4096ULL,
        MachinePermissions::read);
    require(!user_read);
    require(user_read.error().code == machine_errors::kernel_object_alias);

    // Not executable by anyone: kernel state is not code, and this range was
    // chosen by boot-time discovery rather than by the linker. Deliberately the
    // reservation's second page, which nothing has mapped: on the first page the
    // owner's writable translation makes the older W^X check fire first, so that
    // arrangement would pass while proving nothing about this rule.
    auto kernel_execute = space_a.map(
        0x0000'0000'6200'0000ULL, tables_pa + 4096ULL, 4096ULL,
        MachinePermissions::read_execute, MachineMemoryKind::normal);
    require(!kernel_execute);
    require(kernel_execute.error().code == machine_errors::kernel_object_alias);

    // A read-only kernel translation is not forbidden - diagnosis is not an
    // attack, and refusing it would buy nothing.
    require(static_cast<bool>(space_b.map(
        0x0000'0000'6300'0000ULL, tables_pa, 4096ULL,
        MachinePermissions::read, MachineMemoryKind::normal)));

    // Overlapping reservations are a disagreement about who owns a range, so
    // the second one loses rather than silently narrowing the first.
    auto overlapping = space_b.reserve_physical(
        tables_pa + 4096ULL, 4096ULL, PhysicalReservationKind::kernel_object);
    require(!overlapping);
    require(overlapping.error().code == machine_errors::already_mapped);
    require(ledger.reserved == 1U);

    // A reservation declared over a range some process can already reach must
    // fail rather than appear to protect it. Boot declares before it maps; this
    // is what happens to a caller that does not.
    constexpr std::uint64_t late_pa = 0x0000'0000'8400'0000ULL;
    require(static_cast<bool>(space_a.map_user(
        0x0000'0000'1200'0000ULL, late_pa, 4096ULL,
        MachinePermissions::read)));
    auto late_reserve = space_a.reserve_physical(
        late_pa, 4096ULL, PhysicalReservationKind::kernel_object);
    require(!late_reserve);
    require(late_reserve.error().code == machine_errors::kernel_object_alias);
    require(ledger.reserved == 1U);

    // The same range, once no EL0 translation of it survives, is reservable.
    require(static_cast<bool>(builder_a.unmap_page(0x0000'0000'1200'0000ULL)));
    require(static_cast<bool>(space_a.retire_unmapped(0x0000'0000'1200'0000ULL, 4096ULL)));
    require(static_cast<bool>(space_a.reserve_physical(
        late_pa, 4096ULL, PhysicalReservationKind::kernel_object)));
    require(ledger.reserved == 2U);

    // ---------------------------------------------------------------------
    // kernel_private: the kernel's own writable state, which every space has
    // to be able to write while TTBR0 is the only translation regime.
    // ---------------------------------------------------------------------
    constexpr std::uint64_t private_pa = 0x0000'0000'8500'0000ULL;
    require(static_cast<bool>(space_a.reserve_physical(
        private_pa, 4096ULL, PhysicalReservationKind::kernel_private)));
    require(ledger.reserved == 3U);

    // Both the owner and a second space write it. This is the whole difference
    // from kernel_object, and it is not a relaxation for convenience: EL1 runs
    // under whichever process root is installed, so the kernel's globals and
    // stack are unreachable from a scheduled process's space unless every space
    // maps them.
    require(static_cast<bool>(space_a.map(
        0x0000'0000'6400'0000ULL, private_pa, 4096ULL,
        MachinePermissions::read_write, MachineMemoryKind::normal)));
    require(static_cast<bool>(space_b.map(
        0x0000'0000'6400'0000ULL, private_pa, 4096ULL,
        MachinePermissions::read_write, MachineMemoryKind::normal)));

    // The two rules both kinds share still hold. Without these the kind would
    // be indistinguishable from no reservation at all.
    auto private_user = space_a.map_user(
        0x0000'0000'1300'0000ULL, private_pa, 4096ULL,
        MachinePermissions::read);
    require(!private_user);
    require(private_user.error().code == machine_errors::kernel_object_alias);

    constexpr std::uint64_t private_exec_pa = 0x0000'0000'8600'0000ULL;
    require(static_cast<bool>(space_a.reserve_physical(
        private_exec_pa, 4096ULL, PhysicalReservationKind::kernel_private)));
    auto private_execute = space_a.map(
        0x0000'0000'6500'0000ULL, private_exec_pa, 4096ULL,
        MachinePermissions::read_execute, MachineMemoryKind::normal);
    require(!private_execute);
    require(private_execute.error().code == machine_errors::kernel_object_alias);

    // ---------------------------------------------------------------------
    // Teardown: a space gives back everything it holds, or nothing.
    // ---------------------------------------------------------------------
    {
        alignas(4096) std::array<std::byte, 16U * 4096U> tables_c{};
        const auto c_begin = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(tables_c.data()));
        EarlyPageArena arena_c{c_begin, c_begin + tables_c.size()};
        EarlyStage1Builder builder_c{arena_c};
        require(static_cast<bool>(builder_c.initialize()));

        NativePhysicalLedger own_ledger{};
        NativeAddressSpaceState doomed{};
        require(static_cast<bool>(doomed.bind(own_ledger, builder_c)));
        require(doomed.bound());

        constexpr std::uint64_t va = 0x0000'0000'2000'0000ULL;
        constexpr std::uint64_t pa = 0x0000'0000'9000'0000ULL;
        require(static_cast<bool>(doomed.reserve_physical(
            0x0000'0000'9800'0000ULL, 4096ULL,
            PhysicalReservationKind::kernel_object)));
        require(static_cast<bool>(doomed.map(
            va, pa, 2ULL * 4096ULL,
            MachinePermissions::read_write, MachineMemoryKind::normal)));
        require(own_ledger.occupied == 1U);
        require(own_ledger.reserved == 1U);

        // Unbinding while anything is still held fails closed. An unbound space
        // cannot be asked about later, so leaving its ledger entries behind
        // would leave an owner pointer nobody consults again.
        auto premature = doomed.unbind();
        require(!premature);
        require(premature.error().code == machine_errors::mapping_ledger_inconsistent);
        require(doomed.bound());

        // Reservations gone but a mapping remaining is still not releasable.
        require(static_cast<bool>(doomed.release_reservations()));
        require(own_ledger.reserved == 0U);
        require(!doomed.unbind());

        // The teardown loop: ask, unmap, retire, ask again.
        for (;;) {
            auto mapping = doomed.any_mapping();
            if (!mapping) {
                require(mapping.error().code == machine_errors::not_mapped);
                break;
            }
            const auto pages = mapping.value().length / 4096ULL;
            for (std::uint64_t page = 0ULL; page < pages; ++page) {
                require(static_cast<bool>(builder_c.unmap_page(
                    mapping.value().virtual_base + page * 4096ULL)));
            }
            require(static_cast<bool>(doomed.retire_unmapped(
                mapping.value().virtual_base, mapping.value().length)));
        }
        require(doomed.mapping_count() == 0U);
        require(own_ledger.occupied == 0U);

        require(static_cast<bool>(doomed.unbind()));
        require(!doomed.bound());

        // Every operation on an unbound space is refused rather than silently
        // acting on a ledger it no longer belongs to.
        require(!doomed.any_mapping());
        require(!doomed.release_reservations());
        require(!doomed.unbind());
        require(!doomed.map(va, pa, 4096ULL,
                            MachinePermissions::read, MachineMemoryKind::normal));

        // Rebinding is allowed once released - the slot is genuinely free, not
        // merely marked. This is what makes an address space reusable rather
        // than one-shot.
        require(static_cast<bool>(doomed.bind(own_ledger, builder_c)));
        require(doomed.bound());
    }

    return 0;
}
