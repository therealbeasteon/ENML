#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>

namespace os::kernel {

struct MachineContext;
struct MachineAddressSpace;
struct MachinePhysicalLedger;

namespace machine_errors {
inline constexpr std::uint32_t unsupported = 1U;
inline constexpr std::uint32_t invalid_range = 2U;
inline constexpr std::uint32_t alignment = 3U;
inline constexpr std::uint32_t exhausted = 4U;
inline constexpr std::uint32_t already_mapped = 5U;
inline constexpr std::uint32_t not_mapped = 6U;
inline constexpr std::uint32_t writable_executable_alias = 7U;
inline constexpr std::uint32_t missing_guard_page = 8U;
inline constexpr std::uint32_t not_a_kernel_stack = 9U;
inline constexpr std::uint32_t address_space_unbound = 10U;
inline constexpr std::uint32_t address_space_already_bound = 11U;
inline constexpr std::uint32_t mapping_ledger_inconsistent = 12U;
} // namespace machine_errors

enum class MachinePermissions : std::uint8_t {
    read = 1U,
    read_write = 2U,
    read_execute = 3U,
};

enum class MachineMemoryKind : std::uint8_t {
    normal = 1U,
    device = 2U,
};

[[nodiscard]] std::size_t machine_page_size() noexcept;

[[nodiscard]] os::core::Result<void> machine_bind_address_space(
    MachineAddressSpace& space,
    MachinePhysicalLedger& ledger) noexcept;

[[nodiscard]] os::core::Result<void> machine_release_address_space(
    MachineAddressSpace& space) noexcept;

void machine_switch_context(MachineContext& from, MachineContext& to) noexcept;

[[nodiscard]] os::core::Result<void> machine_map_kernel_stack(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length) noexcept;

[[nodiscard]] os::core::Result<void> machine_prepare_context(
    MachineContext& context,
    MachineAddressSpace& space,
    std::uintptr_t entry,
    std::uintptr_t stack) noexcept;

[[nodiscard]] os::core::Result<void> machine_map(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length,
    MachinePermissions permissions,
    MachineMemoryKind kind) noexcept;

[[nodiscard]] os::core::Result<void> machine_unmap(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::size_t length) noexcept;

[[nodiscard]] os::core::Result<void> machine_mask_interrupt(std::uint32_t source) noexcept;
[[nodiscard]] os::core::Result<void> machine_unmask_interrupt(std::uint32_t source) noexcept;
[[nodiscard]] os::core::Result<void> machine_acknowledge_interrupt(std::uint32_t source) noexcept;

// Programs one future scheduler deadline. Cookie is tickless: this does not
// imply a periodic clock and callers must explicitly rearm only when the
// scheduler reports another non-zero deadline.
[[nodiscard]] os::core::Result<void> machine_set_timer(std::uint64_t nanoseconds) noexcept;

// Disables the scheduler timer when no deadline exists. This is a distinct
// machine operation rather than overloading set_timer(0): zero in Scheduler::Decision
// means "no timer", not "interrupt immediately".
[[nodiscard]] os::core::Result<void> machine_cancel_timer() noexcept;

[[nodiscard]] std::uint64_t machine_monotonic_nanoseconds() noexcept;

} // namespace os::kernel
