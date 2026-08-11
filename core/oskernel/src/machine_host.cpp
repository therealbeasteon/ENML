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

} // namespace

std::size_t machine_page_size() noexcept {
    return host_page_size;
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
    if (length == 0U || (length % host_page_size) != 0U) {
        return machine_error(machine_errors::invalid_range);
    }
    if (!aligned(virtual_base) || !aligned(physical_base)) {
        return machine_error(machine_errors::alignment);
    }
    if (range_wraps(virtual_base, length) || range_wraps(physical_base, length)) {
        return machine_error(machine_errors::invalid_range);
    }

    // W^X needs no check here because it is unrepresentable: MachinePermissions
    // has no writable-and-executable value. That is the intended shape - a rule
    // enforced by a type cannot be forgotten by an implementation, and every
    // machine layer written later inherits it for free.

    for (const auto& mapping : space.mappings) {
        if (!mapping.occupied) continue;
        if (ranges_overlap(mapping.virtual_base, mapping.length, virtual_base, length)) {
            // Overlapping mappings mean the permissions in force depend on
            // which entry the hardware walks first, which is not a decision
            // anybody made.
            return machine_error(machine_errors::already_mapped);
        }
    }

    for (auto& mapping : space.mappings) {
        if (mapping.occupied) continue;
        mapping = HostMapping{virtual_base, physical_base, length, permissions, kind, true};
        ++space.occupied;
        return {};
    }
    return machine_error(machine_errors::exhausted);
}

os::core::Result<void> machine_unmap(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::size_t length) noexcept {
    if (length == 0U || !aligned(virtual_base)) {
        return machine_error(machine_errors::invalid_range);
    }
    for (auto& mapping : space.mappings) {
        if (!mapping.occupied) continue;
        if (mapping.virtual_base != virtual_base || mapping.length != length) continue;
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
    (void)space;
    if (entry == 0U || stack == 0U) {
        return machine_error(machine_errors::invalid_range);
    }
    if (!aligned(stack)) {
        return machine_error(machine_errors::alignment);
    }
    context = MachineContext{entry, stack, true};
    return {};
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
