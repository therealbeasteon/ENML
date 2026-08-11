#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>

// The boundary between the portable Cookie Kernel and one machine.
//
// Everything above this header is architecture-independent and testable on a
// development host: the rendezvous, the capability rules, the scheduler. This
// is the seam where that stops. Below it are register windows, page tables,
// exception vectors and, unavoidably, some assembly.
//
// Defining the seam before implementing it is the same discipline as fixing the
// system call surface before writing the kernel, and for the same reason. The
// architecture-specific part of an operating system grows without anyone
// deciding to grow it - each addition is locally reasonable, and the result is
// a kernel that has to be re-verified per board. Naming the operations here
// makes that surface a decision.
//
// Two rules govern this file:
//
//   - **Assembly lives only behind these functions.** Not in the rendezvous,
//     not in a service, not inline in portable code for speed. A tree where
//     architecture assumptions are scattered is one where a port means auditing
//     everything rather than auditing one directory.
//
//   - **Nothing here makes a policy decision.** These operations do what they
//     are told. Which thread runs, which address space a mapping belongs to and
//     whether a caller may request it are decided above, where the decision can
//     be tested without hardware. A machine layer that chooses is a second
//     kernel nobody is reviewing.
//
// The operations are deliberately few, and chosen so the portable kernel never
// needs to know the width of a register, the shape of a page table entry, or
// how an interrupt controller is addressed.
namespace os::kernel {

// An opaque handle to a saved execution context. The portable kernel stores
// these and hands them back; it never inspects one, because doing so would mean
// knowing the register file.
struct MachineContext;

// An opaque handle to a translation regime - a page table root, whatever that
// means on this machine.
struct MachineAddressSpace;

namespace machine_errors {
inline constexpr std::uint32_t unsupported = 1U;
inline constexpr std::uint32_t invalid_range = 2U;
inline constexpr std::uint32_t alignment = 3U;
inline constexpr std::uint32_t exhausted = 4U;
inline constexpr std::uint32_t already_mapped = 5U;
inline constexpr std::uint32_t not_mapped = 6U;
// The same physical memory would become writable through one mapping and
// executable through another.
inline constexpr std::uint32_t writable_executable_alias = 7U;
// A kernel stack was requested with the page below it already mapped.
inline constexpr std::uint32_t missing_guard_page = 8U;
// A context was prepared on a stack that was not established as one.
inline constexpr std::uint32_t not_a_kernel_stack = 9U;
} // namespace machine_errors

// What a mapping permits. Separate from the device access policy in M6.0, which
// decides whether a mapping may be *requested*; this is what the hardware is
// told once that decision has been made.
enum class MachinePermissions : std::uint8_t {
    read = 1U,
    read_write = 2U,
    read_execute = 3U,
};

// Whether a mapping is ordinary memory or device registers.
//
// Not a hint. Device memory must not be cached, speculated into or reordered
// around, and a machine layer that treats the distinction as advisory produces
// drivers that work until the day the compiler or the core reorders something.
enum class MachineMemoryKind : std::uint8_t {
    normal = 1U,
    device = 2U,
};

// The machine's page size. The portable kernel needs this only to reject
// misaligned requests before they reach hardware.
[[nodiscard]] std::size_t machine_page_size() noexcept;

// Switches to a saved context. Does not return to the caller in the usual
// sense: it returns when something later switches back.
void machine_switch_context(MachineContext& from, MachineContext& to) noexcept;

// Establishes a kernel stack, and is the only way to get one.
//
// M7.4a requires a guard page below every kernel stack. In userspace, stack
// clash protection is something the compiler emits; in a kernel it is a mapping
// decision, and an unmapped page is what turns an overflow into a fault instead
// of into the next thread's saved state. So the rule lives here, at the only
// operation that can enforce it: the page immediately below the stack must be
// unmapped, and a request without room for one is refused rather than mapped
// and hoped about.
//
// Stacks grow downward, so the guard sits below `virtual_base` and the initial
// stack pointer is `virtual_base + length`.
[[nodiscard]] os::core::Result<void> machine_map_kernel_stack(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length) noexcept;

// Prepares a context so that switching to it begins execution at `entry` with
// `stack` as its stack, in the given address space.
//
// `stack` must be the top of a range established by machine_map_kernel_stack.
// Accepting any aligned number would make the guard page a convention that
// holds until somebody starts a thread on memory they allocated themselves, and
// a convention is not what M7.4a asked for.
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

// Masks and unmasks a single interrupt source.
//
// Deliberately per-source rather than a global disable. A global interrupt
// disable held across anything but a handful of instructions is where worst-case
// interrupt latency comes from, and latency that depends on what the kernel
// happens to be doing is not a bound anyone can state.
[[nodiscard]] os::core::Result<void> machine_mask_interrupt(std::uint32_t source) noexcept;
[[nodiscard]] os::core::Result<void> machine_unmask_interrupt(std::uint32_t source) noexcept;

// Acknowledges the interrupt currently being handled.
[[nodiscard]] os::core::Result<void> machine_acknowledge_interrupt(std::uint32_t source) noexcept;

// Schedules the next timer interrupt, in nanoseconds from now.
//
// Nanoseconds rather than ticks so the portable kernel never encodes a
// frequency. A machine layer that reports ticks makes every timeout above it
// board-specific.
[[nodiscard]] os::core::Result<void> machine_set_timer(std::uint64_t nanoseconds) noexcept;

// Monotonic time since boot, in nanoseconds. Never decreases, and does not
// stop across a suspend the kernel did not initiate.
[[nodiscard]] std::uint64_t machine_monotonic_nanoseconds() noexcept;

} // namespace os::kernel
