#include <array>
#include <cstdint>
#include <cstdio>
#include <type_traits>

#include <os/core/error.hpp>
#include <os/kernel/machine_host.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "machine host: %s\n", what);
    }
    return condition;
}

bool refused(const os::core::Result<void>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::kernel &&
        result.error().code == code;
}

constexpr std::uintptr_t page = 4096U;

bool bind(
    os::kernel::MachineAddressSpace& space,
    os::kernel::MachinePhysicalLedger& ledger,
    const char* what) {
    return check(static_cast<bool>(os::kernel::machine_bind_address_space(space, ledger)), what);
}

} // namespace

// These tests are written against the machine *contract*, not against one page
// table implementation, so the AArch64 layer has to satisfy the same security
// invariants when it becomes concrete.
int main() {
    if (!check(os::kernel::machine_page_size() == page, "unexpected page size")) return 1;

    // W^X is enforced first by the type: there is no permission representing a
    // writable and executable mapping.
    {
        using Underlying = std::underlying_type_t<os::kernel::MachinePermissions>;
        const Underlying values[]{
            static_cast<Underlying>(os::kernel::MachinePermissions::read),
            static_cast<Underlying>(os::kernel::MachinePermissions::read_write),
            static_cast<Underlying>(os::kernel::MachinePermissions::read_execute),
        };
        if (!check(values[0] != values[1] && values[1] != values[2] && values[0] != values[2],
                   "permission values collide")) return 1;
    }

    // An address space without the machine-wide physical authority cannot map
    // anything. Refusing is safer than silently falling back to a local-only
    // alias check.
    {
        os::kernel::MachineAddressSpace unbound{};
        if (!check(refused(os::kernel::machine_map(
                               unbound, page, page * 2U, page,
                               os::kernel::MachinePermissions::read,
                               os::kernel::MachineMemoryKind::normal),
                           os::kernel::machine_errors::address_space_unbound),
                   "an unbound address space mapped memory")) return 1;
    }

    os::kernel::MachinePhysicalLedger ledger{};
    os::kernel::MachineAddressSpace space{};
    if (!bind(space, ledger, "fresh address space did not bind")) return 1;
    if (!check(refused(os::kernel::machine_bind_address_space(space, ledger),
                       os::kernel::machine_errors::address_space_already_bound),
               "a live address space was rebound")) return 1;
    if (!check(os::kernel::host_mapping_count(space) == 0U,
               "fresh address space had mappings")) return 1;
    if (!check(os::kernel::host_physical_mapping_count(ledger) == 0U,
               "fresh physical ledger had mappings")) return 1;

    // A well-formed mapping is recorded in both views.
    if (!check(static_cast<bool>(os::kernel::machine_map(
                   space, 0x10000U, 0x20000U, page,
                   os::kernel::MachinePermissions::read_write,
                   os::kernel::MachineMemoryKind::normal)),
               "valid mapping refused")) return 1;
    if (!check(os::kernel::host_range_mapped(space, 0x10000U, page), "mapping not recorded")) return 1;
    if (!check(os::kernel::host_physical_mapping_count(ledger) == 1U,
               "physical mapping was not recorded")) return 1;

    // Alignment and range validity, on both the virtual and physical side.
    if (!check(refused(os::kernel::machine_map(
                   space, 0x10001U, 0x20000U, page,
                   os::kernel::MachinePermissions::read,
                   os::kernel::MachineMemoryKind::normal),
                   os::kernel::machine_errors::alignment),
               "misaligned virtual base accepted")) return 1;
    if (!check(refused(os::kernel::machine_map(
                   space, 0x30000U, 0x20001U, page,
                   os::kernel::MachinePermissions::read,
                   os::kernel::MachineMemoryKind::normal),
                   os::kernel::machine_errors::alignment),
               "misaligned physical base accepted")) return 1;
    if (!check(refused(os::kernel::machine_map(
                   space, 0x30000U, 0x40000U, 0U,
                   os::kernel::MachinePermissions::read,
                   os::kernel::MachineMemoryKind::normal),
                   os::kernel::machine_errors::invalid_range),
               "zero-length mapping accepted")) return 1;
    if (!check(refused(os::kernel::machine_map(
                   space, 0x30000U, 0x40000U, page + 1U,
                   os::kernel::MachinePermissions::read,
                   os::kernel::MachineMemoryKind::normal),
                   os::kernel::machine_errors::invalid_range),
               "non-page-multiple length accepted")) return 1;
    {
        constexpr std::uintptr_t top_page = UINTPTR_MAX & ~(page - 1U);
        if (!check(refused(os::kernel::machine_map(
                       space, top_page, 0x40000U, page,
                       os::kernel::MachinePermissions::read,
                       os::kernel::MachineMemoryKind::normal),
                       os::kernel::machine_errors::invalid_range),
                   "wrapping virtual range accepted")) return 1;
    }

    // Virtual overlap is refused and a failed admission leaves both tables
    // unchanged.
    if (!check(refused(os::kernel::machine_map(
                   space, 0x10000U, 0x50000U, page,
                   os::kernel::MachinePermissions::read,
                   os::kernel::MachineMemoryKind::normal),
                   os::kernel::machine_errors::already_mapped),
               "exactly overlapping mapping accepted")) return 1;
    if (!check(refused(os::kernel::machine_map(
                   space, 0x10000U - page, 0x50000U, page * 2U,
                   os::kernel::MachinePermissions::read,
                   os::kernel::MachineMemoryKind::normal),
                   os::kernel::machine_errors::already_mapped),
               "partially overlapping mapping accepted")) return 1;
    if (!check(os::kernel::host_mapping_count(space) == 1U &&
                   os::kernel::host_physical_mapping_count(ledger) == 1U,
               "a refused mapping changed mapping authority")) return 1;

    // Device memory is a distinct kind and maps alongside normal memory.
    if (!check(static_cast<bool>(os::kernel::machine_map(
                   space, 0x80000U, 0x90000U, page,
                   os::kernel::MachinePermissions::read_write,
                   os::kernel::MachineMemoryKind::device)),
               "device mapping refused")) return 1;

    // Unmapping must name exactly what was mapped and must remove the physical
    // authority record at the same time.
    if (!check(refused(os::kernel::machine_unmap(space, 0x10000U, page * 2U),
                       os::kernel::machine_errors::not_mapped),
               "partial unmap accepted")) return 1;
    if (!check(refused(os::kernel::machine_unmap(space, 0x99000U, page),
                       os::kernel::machine_errors::not_mapped),
               "unmap of an unmapped range accepted")) return 1;
    if (!check(static_cast<bool>(os::kernel::machine_unmap(space, 0x10000U, page)),
               "valid unmap refused")) return 1;
    if (!check(!os::kernel::host_range_mapped(space, 0x10000U, page),
               "unmapped range still present")) return 1;
    if (!check(os::kernel::host_physical_mapping_count(ledger) == 1U,
               "unmap left stale physical authority")) return 1;

    // Monotonic time never goes backwards.
    {
        const auto first = os::kernel::machine_monotonic_nanoseconds();
        const auto second = os::kernel::machine_monotonic_nanoseconds();
        if (!check(second >= first, "monotonic time went backwards")) return 1;
    }

    // Context preparation and guarded stacks participate in the same physical
    // authority as ordinary mappings.
    {
        os::kernel::MachinePhysicalLedger stack_ledger{};
        os::kernel::MachineAddressSpace stack_space{};
        if (!bind(stack_space, stack_ledger, "stack address space did not bind")) return 1;
        os::kernel::MachineContext context{};

        if (!check(refused(os::kernel::machine_prepare_context(context, stack_space, 0U, page),
                           os::kernel::machine_errors::invalid_range),
                   "null entry accepted")) return 1;
        if (!check(refused(os::kernel::machine_prepare_context(context, stack_space, page, 1U),
                           os::kernel::machine_errors::alignment),
                   "misaligned stack accepted")) return 1;
        if (!check(refused(
                       os::kernel::machine_prepare_context(context, stack_space, page, page * 8U),
                       os::kernel::machine_errors::not_a_kernel_stack),
                   "a context was prepared on memory that is not a kernel stack")) return 1;

        const std::uintptr_t stack_base = page * 4U;
        const std::size_t stack_length = page * 2U;
        if (!check(static_cast<bool>(os::kernel::machine_map_kernel_stack(
                       stack_space, stack_base, page * 64U, stack_length)),
                   "a well-formed kernel stack was refused")) return 1;

        const std::uintptr_t stack_top = stack_base + stack_length;
        if (!check(static_cast<bool>(
                       os::kernel::machine_prepare_context(context, stack_space, page, stack_top)),
                   "a context on a real kernel stack was refused")) return 1;
        if (!check(refused(
                       os::kernel::machine_prepare_context(context, stack_space, page, stack_base),
                       os::kernel::machine_errors::not_a_kernel_stack),
                   "the bottom of a stack was accepted as a stack pointer")) return 1;

        os::kernel::MachineAddressSpace executable_alias{};
        if (!bind(executable_alias, stack_ledger, "stack alias space did not bind")) return 1;
        if (!check(refused(os::kernel::machine_map(
                               executable_alias, page * 32U, page * 64U, stack_length,
                               os::kernel::MachinePermissions::read_execute,
                               os::kernel::MachineMemoryKind::normal),
                           os::kernel::machine_errors::writable_executable_alias),
                   "kernel stack backing became executable in another address space")) return 1;
    }

    // Every kernel stack has an unmapped page below it.
    {
        os::kernel::MachinePhysicalLedger guard_ledger{};
        os::kernel::MachineAddressSpace guarded{};
        if (!bind(guarded, guard_ledger, "guard address space did not bind")) return 1;
        const std::uintptr_t stack_base = page * 4U;

        if (!check(static_cast<bool>(os::kernel::machine_map(
                       guarded, stack_base - page, page * 32U, page,
                       os::kernel::MachinePermissions::read_write,
                       os::kernel::MachineMemoryKind::normal)),
                   "mapping the guard page location failed")) return 1;
        if (!check(refused(os::kernel::machine_map_kernel_stack(
                               guarded, stack_base, page * 64U, page * 2U),
                           os::kernel::machine_errors::missing_guard_page),
                   "a stack was established over an already-mapped guard page")) return 1;

        os::kernel::MachineAddressSpace low{};
        if (!bind(low, guard_ledger, "low address space did not bind")) return 1;
        if (!check(refused(os::kernel::machine_map_kernel_stack(low, 0U, page, page),
                           os::kernel::machine_errors::missing_guard_page),
                   "a stack at the bottom of the address space was accepted")) return 1;
    }

    // M7.4c closes the gap M7.4b named explicitly: W^X follows physical memory
    // across address spaces. Exact and partial aliases are both conflicts.
    {
        os::kernel::MachinePhysicalLedger shared_ledger{};
        os::kernel::MachineAddressSpace writer{};
        os::kernel::MachineAddressSpace executor{};
        if (!bind(writer, shared_ledger, "writer space did not bind") ||
            !bind(executor, shared_ledger, "executor space did not bind")) return 1;

        const std::uintptr_t physical = page * 128U;
        if (!check(static_cast<bool>(os::kernel::machine_map(
                       writer, page * 4U, physical, page * 2U,
                       os::kernel::MachinePermissions::read_write,
                       os::kernel::MachineMemoryKind::normal)),
                   "cross-space writer mapping refused")) return 1;
        if (!check(refused(os::kernel::machine_map(
                               executor, page * 16U, physical, page,
                               os::kernel::MachinePermissions::read_execute,
                               os::kernel::MachineMemoryKind::normal),
                           os::kernel::machine_errors::writable_executable_alias),
                   "cross-space exact W^X alias accepted")) return 1;
        if (!check(refused(os::kernel::machine_map(
                               executor, page * 16U, physical + page, page * 2U,
                               os::kernel::MachinePermissions::read_execute,
                               os::kernel::MachineMemoryKind::normal),
                           os::kernel::machine_errors::writable_executable_alias),
                   "cross-space partial W^X alias accepted")) return 1;
        if (!check(os::kernel::host_mapping_count(executor) == 0U &&
                       os::kernel::host_physical_mapping_count(shared_ledger) == 1U,
                   "failed cross-space admission left ledger residue")) return 1;

        // Same-permission aliases remain legal; shared memory is not prohibited.
        if (!check(static_cast<bool>(os::kernel::machine_map(
                       executor, page * 16U, physical, page,
                       os::kernel::MachinePermissions::read_write,
                       os::kernel::MachineMemoryKind::normal)),
                   "cross-space writable shared buffer was refused")) return 1;

        // Once every writable alias to the page is gone, executable admission
        // becomes legal. Teardown therefore revokes mapping authority, not just
        // virtual address ownership.
        if (!check(static_cast<bool>(os::kernel::machine_unmap(writer, page * 4U, page * 2U)),
                   "writer unmap failed")) return 1;
        if (!check(static_cast<bool>(os::kernel::machine_unmap(executor, page * 16U, page)),
                   "second writer unmap failed")) return 1;
        if (!check(os::kernel::host_physical_mapping_count(shared_ledger) == 0U,
                   "writer teardown left physical authority")) return 1;
        if (!check(static_cast<bool>(os::kernel::machine_map(
                       executor, page * 20U, physical, page,
                       os::kernel::MachinePermissions::read_execute,
                       os::kernel::MachineMemoryKind::normal)),
                   "executable mapping stayed blocked after writers disappeared")) return 1;

        if (!check(static_cast<bool>(os::kernel::machine_release_address_space(executor)),
                   "address-space release failed")) return 1;
        if (!check(os::kernel::host_physical_mapping_count(shared_ledger) == 0U &&
                       os::kernel::host_mapping_count(executor) == 0U,
                   "address-space release left stale mappings")) return 1;
        if (!check(refused(os::kernel::machine_map(
                               executor, page, physical, page,
                               os::kernel::MachinePermissions::read,
                               os::kernel::MachineMemoryKind::normal),
                           os::kernel::machine_errors::address_space_unbound),
                   "released address space retained mapping authority")) return 1;
        if (!bind(executor, shared_ledger, "released address space could not be rebound")) return 1;
    }

    // The physical ledger itself is bounded. Admission fails before either
    // table changes when the global metadata budget is exhausted.
    {
        os::kernel::MachinePhysicalLedger full{};
        std::array<os::kernel::MachineAddressSpace, 4U> spaces{};
        for (auto& current : spaces) {
            if (!bind(current, full, "capacity-test space did not bind")) return 1;
        }

        std::size_t admitted = 0U;
        for (std::size_t space_index = 0U; space_index < spaces.size(); ++space_index) {
            for (std::size_t mapping_index = 0U;
                 mapping_index < os::kernel::max_host_mappings;
                 ++mapping_index) {
                const std::uintptr_t virtual_base =
                    page * (1U + mapping_index * 2U);
                const std::uintptr_t physical_base =
                    page * (1024U + admitted * 2U);
                if (!check(static_cast<bool>(os::kernel::machine_map(
                               spaces[space_index], virtual_base, physical_base, page,
                               os::kernel::MachinePermissions::read,
                               os::kernel::MachineMemoryKind::normal)),
                           "physical-ledger fill failed early")) return 1;
                ++admitted;
            }
        }
        if (!check(admitted == os::kernel::max_host_physical_mappings &&
                       os::kernel::host_physical_mapping_count(full) == admitted,
                   "physical-ledger ceiling mismatch")) return 1;

        os::kernel::MachineAddressSpace extra{};
        if (!bind(extra, full, "extra capacity-test space did not bind")) return 1;
        if (!check(refused(os::kernel::machine_map(
                               extra, page, page * 4096U, page,
                               os::kernel::MachinePermissions::read,
                               os::kernel::MachineMemoryKind::normal),
                           os::kernel::machine_errors::exhausted),
                   "physical-ledger exhaustion did not fail closed")) return 1;
        if (!check(os::kernel::host_mapping_count(extra) == 0U &&
                       os::kernel::host_physical_mapping_count(full) == admitted,
                   "failed exhausted admission changed state")) return 1;
    }

    // The operations that genuinely need a machine report unsupported rather
    // than pretending.
    if (!check(refused(os::kernel::machine_mask_interrupt(1U),
                       os::kernel::machine_errors::unsupported),
               "host claimed to mask an interrupt")) return 1;
    if (!check(refused(os::kernel::machine_set_timer(1000U),
                       os::kernel::machine_errors::unsupported),
               "host claimed to set a timer")) return 1;

    return 0;
}
