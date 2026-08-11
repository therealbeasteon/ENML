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

} // namespace

// These tests are written against the machine *contract*, not against the host
// implementation, so the AArch64 layer has to satisfy the same ones.
int main() {
    if (!check(os::kernel::machine_page_size() == page, "unexpected page size")) return 1;

    // W^X is enforced by the type rather than by a check, which is the point:
    // a rule a type makes unrepresentable cannot be forgotten by an
    // implementation, and every machine layer written later inherits it.
    {
        using Underlying = std::underlying_type_t<os::kernel::MachinePermissions>;
        const Underlying values[]{
            static_cast<Underlying>(os::kernel::MachinePermissions::read),
            static_cast<Underlying>(os::kernel::MachinePermissions::read_write),
            static_cast<Underlying>(os::kernel::MachinePermissions::read_execute),
        };
        // Three permissions, and no fourth naming write and execute together.
        if (!check(values[0] != values[1] && values[1] != values[2] && values[0] != values[2],
                   "permission values collide")) return 1;
    }

    os::kernel::MachineAddressSpace space{};
    if (!check(os::kernel::host_mapping_count(space) == 0U,
               "fresh address space had mappings")) return 1;

    // A well-formed mapping.
    if (!check(static_cast<bool>(os::kernel::machine_map(
                   space, 0x10000U, 0x20000U, page,
                   os::kernel::MachinePermissions::read_write,
                   os::kernel::MachineMemoryKind::normal)),
               "valid mapping refused")) return 1;
    if (!check(os::kernel::host_range_mapped(space, 0x10000U, page), "mapping not recorded")) return 1;

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
    // A range whose end wraps past the top of the address space describes
    // memory that does not exist, and every containment check would be
    // meaningless for it. The highest page-aligned base is exactly where this
    // happens.
    {
        constexpr std::uintptr_t top_page = UINTPTR_MAX & ~(page - 1U);
        if (!check(refused(os::kernel::machine_map(
                       space, top_page, 0x40000U, page,
                       os::kernel::MachinePermissions::read,
                       os::kernel::MachineMemoryKind::normal),
                       os::kernel::machine_errors::invalid_range),
                   "wrapping virtual range accepted")) return 1;
    }

    // Overlap is refused: with two mappings covering an address, which
    // permissions apply depends on which entry is walked first, and that is not
    // a decision anybody made.
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
    if (!check(os::kernel::host_mapping_count(space) == 1U,
               "a refused mapping changed the address space")) return 1;

    // Device memory is a distinct kind and maps alongside normal memory.
    if (!check(static_cast<bool>(os::kernel::machine_map(
                   space, 0x80000U, 0x90000U, page,
                   os::kernel::MachinePermissions::read_write,
                   os::kernel::MachineMemoryKind::device)),
               "device mapping refused")) return 1;

    // Unmapping must name exactly what was mapped. Splitting a mapping is where
    // a permission silently changes for the half nobody was thinking about.
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

    // Monotonic time never goes backwards, which is what every timeout above
    // the machine layer depends on.
    {
        const auto first = os::kernel::machine_monotonic_nanoseconds();
        const auto second = os::kernel::machine_monotonic_nanoseconds();
        if (!check(second >= first, "monotonic time went backwards")) return 1;
    }

    // Context preparation validates what it can.
    {
        os::kernel::MachineContext context{};
        if (!check(refused(os::kernel::machine_prepare_context(context, space, 0U, page),
                           os::kernel::machine_errors::invalid_range),
                   "null entry accepted")) return 1;
        if (!check(refused(os::kernel::machine_prepare_context(context, space, page, 1U),
                           os::kernel::machine_errors::alignment),
                   "misaligned stack accepted")) return 1;
        if (!check(static_cast<bool>(
                       os::kernel::machine_prepare_context(context, space, page, page * 2U)),
                   "valid context refused")) return 1;
    }

    // The operations that genuinely need a machine report unsupported rather
    // than pretending. A host layer that faked these would let the portable
    // kernel be tested against behaviour no real machine has - and it would
    // pass, which is worse than having no host layer at all.
    if (!check(refused(os::kernel::machine_mask_interrupt(1U),
                       os::kernel::machine_errors::unsupported),
               "host claimed to mask an interrupt")) return 1;
    if (!check(refused(os::kernel::machine_set_timer(1000U),
                       os::kernel::machine_errors::unsupported),
               "host claimed to set a timer")) return 1;

    return 0;
}
