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

    // ---------------------------------------------------------------------
    // Creation after boot: bind a rootless builder, donate, then build a root.
    // The order boot cannot use, because boot has no caller to ask for pages.
    // ---------------------------------------------------------------------
    {
        alignas(4096) std::array<std::byte, 6U * 4096U> donor{};
        const auto donor_base = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(donor.data()));

        EarlyPageArena fresh_arena{};
        EarlyStage1Builder fresh_builder{fresh_arena};
        NativePhysicalLedger fresh_ledger{};
        NativeAddressSpaceState created{};

        // Bind with no root at all. This is the change that breaks the cycle:
        // a root is a page, post-boot pages are donated, and donation reserves
        // through a space that must already be bound.
        require(fresh_builder.root_physical() == 0ULL);
        require(static_cast<bool>(created.bind(fresh_ledger, fresh_builder)));
        require(created.bound());

        // In that window a rootless space can reserve and nothing else. Every
        // mapping path fails closed until the root exists.
        auto premature_map = created.map(
            0x0000'0000'3000'0000ULL, donor_base, 4096ULL,
            MachinePermissions::read_write, MachineMemoryKind::normal);
        require(!premature_map);

        require(static_cast<bool>(created.reserve_physical(
            donor_base, 4096ULL, PhysicalReservationKind::kernel_object)));
        require(fresh_ledger.reserved == 1U);

        // No pages yet, so there is no root to build - and it is an ordinary
        // exhausted the caller fixes by donating, not a kernel failure.
        require(!fresh_builder.initialize());

        require(static_cast<bool>(fresh_arena.donate(donor_base)));
        auto fresh_root = fresh_builder.initialize();
        require(static_cast<bool>(fresh_root));
        require(fresh_root.value() == donor_base);
        require(fresh_builder.root_physical() == donor_base);

        // With a root, the space maps normally. Two more donations cover the
        // intermediate tables one page needs.
        require(static_cast<bool>(fresh_arena.donate(donor_base + 4096ULL)));
        require(static_cast<bool>(fresh_arena.donate(donor_base + 2ULL * 4096ULL)));
        require(static_cast<bool>(created.map(
            0x0000'0000'3000'0000ULL, 0x0000'0000'A000'0000ULL, 4096ULL,
            MachinePermissions::read_write, MachineMemoryKind::normal)));
        require(created.mapping_count() == 1U);

        // And the space created this way tears down the same as any other.
        require(static_cast<bool>(fresh_builder.unmap_page(0x0000'0000'3000'0000ULL)));
        require(static_cast<bool>(created.retire_unmapped(
            0x0000'0000'3000'0000ULL, 4096ULL)));
        require(static_cast<bool>(created.release_reservations()));
        require(static_cast<bool>(created.unbind()));
        require(!created.bound());
    }

    // ---------------------------------------------------------------------
    // The two-owner rule, which until now nothing exercised.
    //
    // forbidden_by_reservation refuses a kernel_object reservation when
    // another space holds a writable mapping of the same physical range. Its
    // first two disjuncts (user-accessible, executable) had coverage; this
    // third one did not, from either direction. M7.11's boot proof does not
    // reach it either - the pages it donates are mapped writable in
    // early_identity_space, which is bound to a different ledger, so the
    // check never fires there. A rule asserted by no test and reached by no
    // path is a rule that has already stopped being enforced; these pin it.
    // ---------------------------------------------------------------------
    {
        alignas(4096) std::array<std::byte, 4U * 4096U> tables_x{};
        alignas(4096) std::array<std::byte, 4U * 4096U> tables_y{};
        alignas(4096) std::array<std::byte, 4096U> contested{};
        const auto x_begin = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(tables_x.data()));
        const auto y_begin = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(tables_y.data()));
        const auto contested_base = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(contested.data()));

        // One ledger, two spaces. Sharing the ledger is the whole point: the
        // rule is scoped to a ledger, and the separate-ledger case below is
        // what makes that scoping explicit rather than accidental.
        NativePhysicalLedger shared{};
        EarlyPageArena arena_x{x_begin, x_begin + tables_x.size()};
        EarlyPageArena arena_y{y_begin, y_begin + tables_y.size()};
        EarlyStage1Builder builder_x{arena_x};
        EarlyStage1Builder builder_y{arena_y};
        require(builder_x.initialize());
        require(builder_y.initialize());
        NativeAddressSpaceState space_x{};
        NativeAddressSpaceState space_y{};
        require(space_x.bind(shared, builder_x));
        require(space_y.bind(shared, builder_y));

        // X maps the page writable, kernel-only and non-executable, so
        // neither of the other two disjuncts can be what refuses Y below.
        require(space_x.map(
            0x0000'0000'5000'0000ULL, contested_base, 4096ULL,
            MachinePermissions::read_write, MachineMemoryKind::normal));

        // Y cannot now claim it as a kernel object it alone may write.
        auto refused_reservation = space_y.reserve_physical(
            contested_base, 4096ULL, PhysicalReservationKind::kernel_object);
        require(!refused_reservation);
        require(refused_reservation.error().code == machine_errors::kernel_object_alias);

        // kernel_private is the weaker kind and must still be allowed here -
        // it exists precisely because the kernel's globals have to stay
        // writable in every space, so a foreign writable mapping is expected.
        require(space_y.reserve_physical(
            contested_base, 4096ULL, PhysicalReservationKind::kernel_private));

        // What kernel_private must NOT give up, checked because donated
        // translation tables now use it (aarch64_donate_table_page): a process
        // still cannot reach the range, and nothing can execute it. These are
        // the properties that make the weaker kind safe for page tables - the
        // relaxation is only between kernel-side writers, which are all the
        // kernel. If either of these ever passes, a process can read or write
        // its own page tables and the kind is no longer usable for them.
        //
        // Deliberately on a page nothing else has mapped. The first attempt at
        // this test put them on `contested`, which space_x already maps
        // writable - so the executable case was refused by the older
        // writable_executable_alias check before the reservation was ever
        // consulted, and the assertion proved that check rather than this one.
        // An isolated page is what makes the reservation the only thing that
        // can refuse these.
        alignas(4096) std::array<std::byte, 4096U> table_like{};
        const auto table_like_base = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(table_like.data()));
        require(space_y.reserve_physical(
            table_like_base, 4096ULL, PhysicalReservationKind::kernel_private));

        auto user_refused = space_y.map_user(
            0x0000'0000'7000'0000ULL, table_like_base, 4096ULL,
            MachinePermissions::read_write);
        require(!user_refused);
        require(user_refused.error().code == machine_errors::kernel_object_alias);

        auto executable_refused = space_y.map(
            0x0000'0000'8000'0000ULL, table_like_base, 4096ULL,
            MachinePermissions::read_execute, MachineMemoryKind::normal);
        require(!executable_refused);
        require(executable_refused.error().code == machine_errors::kernel_object_alias);

        // And the case that must still work, on the same page: a kernel-only
        // writable mapping from a space that does not own the reservation.
        // This is the whole point of the change - it is how the kernel edits a
        // created space's tables while running under another process's root.
        require(space_x.map(
            0x0000'0000'9000'0000ULL, table_like_base, 4096ULL,
            MachinePermissions::read_write, MachineMemoryKind::normal));
    }

    // The same conflict in the other order: reserve first, then attempt the
    // foreign writable mapping. map_impl checks it from its side, and a rule
    // enforced in only one direction is one an unlucky ordering walks past.
    {
        alignas(4096) std::array<std::byte, 4U * 4096U> tables_x{};
        alignas(4096) std::array<std::byte, 4U * 4096U> tables_y{};
        alignas(4096) std::array<std::byte, 4096U> contested{};
        const auto x_begin = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(tables_x.data()));
        const auto y_begin = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(tables_y.data()));
        const auto contested_base = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(contested.data()));

        NativePhysicalLedger shared{};
        EarlyPageArena arena_x{x_begin, x_begin + tables_x.size()};
        EarlyPageArena arena_y{y_begin, y_begin + tables_y.size()};
        EarlyStage1Builder builder_x{arena_x};
        EarlyStage1Builder builder_y{arena_y};
        require(builder_x.initialize());
        require(builder_y.initialize());
        NativeAddressSpaceState space_x{};
        NativeAddressSpaceState space_y{};
        require(space_x.bind(shared, builder_x));
        require(space_y.bind(shared, builder_y));

        require(space_x.reserve_physical(
            contested_base, 4096ULL, PhysicalReservationKind::kernel_object));

        auto refused_map = space_y.map(
            0x0000'0000'5000'0000ULL, contested_base, 4096ULL,
            MachinePermissions::read_write, MachineMemoryKind::normal);
        require(!refused_map);
        require(refused_map.error().code == machine_errors::kernel_object_alias);

        // The owner may still map what it reserved - the rule is about
        // foreign writers, not about freezing the range.
        require(space_x.map(
            0x0000'0000'5000'0000ULL, contested_base, 4096ULL,
            MachinePermissions::read_write, MachineMemoryKind::normal));

        // A read-only foreign mapping is permitted: the reservation is about
        // who may *write* a kernel object, and a reader cannot corrupt one.
        require(space_y.map(
            0x0000'0000'6000'0000ULL, contested_base, 4096ULL,
            MachinePermissions::read, MachineMemoryKind::normal));
    }

    // The boundary itself. The same pair on two ledgers must succeed - this
    // is what Cookie's boot relies on, with early_identity_space holding a
    // writable mapping while the real map reserves the same page. Asserted
    // here so that "fixing" the scope by merging the ledgers fails loudly
    // rather than silently breaking boot.
    {
        alignas(4096) std::array<std::byte, 4U * 4096U> tables_x{};
        alignas(4096) std::array<std::byte, 4U * 4096U> tables_y{};
        alignas(4096) std::array<std::byte, 4096U> shared_page{};
        const auto x_begin = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(tables_x.data()));
        const auto y_begin = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(tables_y.data()));
        const auto page_base = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(shared_page.data()));

        NativePhysicalLedger ledger_x{};
        NativePhysicalLedger ledger_y{};
        EarlyPageArena arena_x{x_begin, x_begin + tables_x.size()};
        EarlyPageArena arena_y{y_begin, y_begin + tables_y.size()};
        EarlyStage1Builder builder_x{arena_x};
        EarlyStage1Builder builder_y{arena_y};
        require(builder_x.initialize());
        require(builder_y.initialize());
        NativeAddressSpaceState space_x{};
        NativeAddressSpaceState space_y{};
        require(space_x.bind(ledger_x, builder_x));
        require(space_y.bind(ledger_y, builder_y));

        require(space_x.map(
            0x0000'0000'5000'0000ULL, page_base, 4096ULL,
            MachinePermissions::read_write, MachineMemoryKind::normal));
        require(space_y.reserve_physical(
            page_base, 4096ULL, PhysicalReservationKind::kernel_object));
    }

    return 0;
}
