#include <os/kernel/machine_host.hpp>

#include <chrono>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error machine_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

inline constexpr std::size_t host_page_size = 4096U;

[[nodiscard]] bool aligned(std::uintptr_t value) noexcept {
    return (value % host_page_size) == 0U;
}

// A range that wraps describes memory that does not exist, and every
// containment check below would be meaningless for it. Checked before the
// values are used, never after.
[[nodiscard]] bool range_wraps(std::uintptr_t base, std::size_t length) noexcept {
    return length > (UINTPTR_MAX - base);
}

[[nodiscard]] bool ranges_overlap(
    std::uintptr_t a_base,
    std::size_t a_length,
    std::uintptr_t b_base,
    std::size_t b_length) noexcept {
    return a_base < (b_base + b_length) && b_base < (a_base + a_length);
}

[[nodiscard]] bool is_writable(MachinePermissions permissions) noexcept {
    return permissions == MachinePermissions::read_write;
}

[[nodiscard]] bool is_executable(MachinePermissions permissions) noexcept {
    return permissions == MachinePermissions::read_execute;
}

[[nodiscard]] bool conflicts_across_alias(
    const HostPhysicalMapping& existing,
    std::uintptr_t physical_base,
    std::size_t length,
    MachinePermissions permissions) noexcept {
    if (!ranges_overlap(existing.physical_base, existing.length, physical_base, length)) {
        return false;
    }
    return (is_writable(existing.permissions) && is_executable(permissions)) ||
        (is_executable(existing.permissions) && is_writable(permissions));
}

[[nodiscard]] HostMapping* free_local_mapping(MachineAddressSpace& space) noexcept {
    for (auto& mapping : space.mappings) {
        if (!mapping.occupied) return &mapping;
    }
    return nullptr;
}

[[nodiscard]] HostPhysicalMapping* free_physical_mapping(
    MachinePhysicalLedger& ledger) noexcept {
    for (auto& mapping : ledger.mappings) {
        if (!mapping.occupied) return &mapping;
    }
    return nullptr;
}

[[nodiscard]] HostPhysicalMapping* exact_physical_mapping(
    MachinePhysicalLedger& ledger,
    const MachineAddressSpace& owner,
    const HostMapping& local) noexcept {
    for (auto& mapping : ledger.mappings) {
        if (!mapping.occupied || mapping.owner != &owner) continue;
        if (mapping.virtual_base == local.virtual_base &&
            mapping.physical_base == local.physical_base &&
            mapping.length == local.length &&
            mapping.permissions == local.permissions) {
            return &mapping;
        }
    }
    return nullptr;
}

[[nodiscard]] std::size_t owner_ledger_entries(
    const MachinePhysicalLedger& ledger,
    const MachineAddressSpace& owner) noexcept {
    std::size_t count = 0U;
    for (const auto& mapping : ledger.mappings) {
        if (mapping.occupied && mapping.owner == &owner) ++count;
    }
    return count;
}

[[nodiscard]] os::core::Result<void> map_range(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length,
    MachinePermissions permissions,
    MachineMemoryKind kind,
    bool kernel_stack) noexcept {
    if (space.physical_ledger == nullptr) {
        return machine_error(machine_errors::address_space_unbound);
    }
    if (length == 0U || (length % host_page_size) != 0U) {
        return machine_error(machine_errors::invalid_range);
    }
    if (!aligned(virtual_base) || !aligned(physical_base)) {
        return machine_error(machine_errors::alignment);
    }
    if (range_wraps(virtual_base, length) || range_wraps(physical_base, length)) {
        return machine_error(machine_errors::invalid_range);
    }

    for (const auto& mapping : space.mappings) {
        if (!mapping.occupied) continue;
        if (ranges_overlap(mapping.virtual_base, mapping.length, virtual_base, length)) {
            // Overlapping virtual mappings mean the permissions in force depend
            // on which entry the hardware walks first, which is not a decision
            // anybody made.
            return machine_error(machine_errors::already_mapped);
        }
    }

    // W^X must be global to the machine, not local to this address space. The
    // same physical page is still writable and executable simultaneously when
    // the two aliases live in different processes. Every admitted mapping is
    // therefore compared against the shared physical ledger before either table
    // is mutated.
    for (const auto& mapping : space.physical_ledger->mappings) {
        if (!mapping.occupied) continue;
        if (conflicts_across_alias(mapping, physical_base, length, permissions)) {
            return machine_error(machine_errors::writable_executable_alias);
        }
    }

    HostMapping* local_slot = free_local_mapping(space);
    HostPhysicalMapping* ledger_slot = free_physical_mapping(*space.physical_ledger);
    if (local_slot == nullptr || ledger_slot == nullptr) {
        return machine_error(machine_errors::exhausted);
    }

    *local_slot = HostMapping{
        virtual_base, physical_base, length, permissions, kind, kernel_stack, true};
    *ledger_slot = HostPhysicalMapping{
        &space, virtual_base, physical_base, length, permissions, true};
    ++space.occupied;
    ++space.physical_ledger->occupied;
    return {};
}

} // namespace

std::size_t machine_page_size() noexcept {
    return host_page_size;
}

os::core::Result<void> machine_bind_address_space(
    MachineAddressSpace& space,
    MachinePhysicalLedger& ledger) noexcept {
    if (space.physical_ledger != nullptr) {
        return machine_error(machine_errors::address_space_already_bound);
    }
    if (space.occupied != 0U) {
        return machine_error(machine_errors::mapping_ledger_inconsistent);
    }
    space.physical_ledger = &ledger;
    return {};
}

os::core::Result<void> machine_release_address_space(
    MachineAddressSpace& space) noexcept {
    if (space.physical_ledger == nullptr) {
        return machine_error(machine_errors::address_space_unbound);
    }

    MachinePhysicalLedger& ledger = *space.physical_ledger;
    if (owner_ledger_entries(ledger, space) != space.occupied) {
        return machine_error(machine_errors::mapping_ledger_inconsistent);
    }
    for (const auto& local : space.mappings) {
        if (!local.occupied) continue;
        if (exact_physical_mapping(ledger, space, local) == nullptr) {
            return machine_error(machine_errors::mapping_ledger_inconsistent);
        }
    }

    // Validate the two views before changing either. Once they agree, teardown
    // is bounded and cannot leave stale physical authority behind after process
    // death.
    for (auto& mapping : ledger.mappings) {
        if (!mapping.occupied || mapping.owner != &space) continue;
        mapping = HostPhysicalMapping{};
        --ledger.occupied;
    }
    for (auto& mapping : space.mappings) {
        if (mapping.occupied) mapping = HostMapping{};
    }
    space.occupied = 0U;
    space.physical_ledger = nullptr;
    return {};
}

std::uint64_t machine_monotonic_nanoseconds() noexcept {
    // steady_clock rather than system_clock: the contract says the value never
    // decreases, and a wall clock that a user or an NTP step can move backwards
    // would break every timeout computed from it.
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

os::core::Result<void> machine_map(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length,
    MachinePermissions permissions,
    MachineMemoryKind kind) noexcept {
    // Half of W^X is unrepresentable rather than checked: MachinePermissions has
    // no writable-and-executable value. The other half - aliases - is enforced
    // by map_range against the machine-wide physical ledger.
    return map_range(space, virtual_base, physical_base, length, permissions, kind, false);
}

os::core::Result<void> machine_map_kernel_stack(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length) noexcept {
    if (space.physical_ledger == nullptr) {
        return machine_error(machine_errors::address_space_unbound);
    }
    if (!aligned(virtual_base)) {
        return machine_error(machine_errors::alignment);
    }
    // A stack at the bottom of the address space has nowhere to put a guard, and
    // the honest answer is to refuse rather than to map it and describe it as
    // guarded.
    if (virtual_base < host_page_size) {
        return machine_error(machine_errors::missing_guard_page);
    }

    const std::uintptr_t guard_base = virtual_base - host_page_size;
    for (const auto& mapping : space.mappings) {
        if (!mapping.occupied) continue;
        if (ranges_overlap(mapping.virtual_base, mapping.length, guard_base, host_page_size)) {
            return machine_error(machine_errors::missing_guard_page);
        }
    }

    // A stack is ordinary memory that is written to, so it participates in the
    // same physical ledger. Mapping its backing page executable in another
    // address space is therefore refused too.
    return map_range(
        space,
        virtual_base,
        physical_base,
        length,
        MachinePermissions::read_write,
        MachineMemoryKind::normal,
        true);
}

os::core::Result<void> machine_unmap(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::size_t length) noexcept {
    if (space.physical_ledger == nullptr) {
        return machine_error(machine_errors::address_space_unbound);
    }
    if (length == 0U || !aligned(virtual_base)) {
        return machine_error(machine_errors::invalid_range);
    }
    for (auto& mapping : space.mappings) {
        if (!mapping.occupied) continue;
        if (mapping.virtual_base != virtual_base || mapping.length != length) continue;

        HostPhysicalMapping* ledger_mapping =
            exact_physical_mapping(*space.physical_ledger, space, mapping);
        if (ledger_mapping == nullptr) {
            return machine_error(machine_errors::mapping_ledger_inconsistent);
        }

        *ledger_mapping = HostPhysicalMapping{};
        --space.physical_ledger->occupied;
        mapping = HostMapping{};
        --space.occupied;
        return {};
    }
    // Partial unmapping is refused rather than split. Splitting a mapping is
    // where a permission silently changes for the half nobody was thinking
    // about, so a caller must unmap what it mapped.
    return machine_error(machine_errors::not_mapped);
}

os::core::Result<void> machine_prepare_context(
    MachineContext& context,
    MachineAddressSpace& space,
    std::uintptr_t entry,
    std::uintptr_t stack) noexcept {
    if (space.physical_ledger == nullptr) {
        return machine_error(machine_errors::address_space_unbound);
    }
    if (entry == 0U || stack == 0U) {
        return machine_error(machine_errors::invalid_range);
    }
    if (!aligned(stack)) {
        return machine_error(machine_errors::alignment);
    }

    // The stack pointer must be the top of a range established as a kernel
    // stack, which is the only way it can be known to have a guard page beneath
    // it. Accepting any aligned address would leave the guard rule holding right
    // up until the first thread started on memory somebody allocated themselves.
    for (const auto& mapping : space.mappings) {
        if (!mapping.occupied || !mapping.kernel_stack) continue;
        if (mapping.virtual_base + mapping.length != stack) continue;
        context = MachineContext{entry, stack, true};
        return {};
    }
    return machine_error(machine_errors::not_a_kernel_stack);
}

void machine_switch_context(MachineContext& from, MachineContext& to) noexcept {
    // Deliberately does nothing on the host.
    //
    // There is no register file here to save or restore, and faking one would
    // let the portable kernel be tested against behaviour no real machine has -
    // which is worse than having no host implementation at all, because it
    // would pass.
    (void)from;
    (void)to;
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
    (void)nanoseconds;
    return machine_error(machine_errors::unsupported);
}

std::size_t host_mapping_count(const MachineAddressSpace& space) noexcept {
    return space.occupied;
}

std::size_t host_physical_mapping_count(const MachinePhysicalLedger& ledger) noexcept {
    return ledger.occupied;
}

bool host_range_mapped(
    const MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::size_t length) noexcept {
    for (const auto& mapping : space.mappings) {
        if (!mapping.occupied) continue;
        if (mapping.virtual_base == virtual_base && mapping.length == length) return true;
    }
    return false;
}

} // namespace os::kernel
