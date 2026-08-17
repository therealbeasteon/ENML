#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>

// The complete system call surface of the Cookie Kernel.
//
// Cookie is the operating system, the Cookie Kernel is what it runs on, and
// EMNL is the security architecture both carry. docs/NAMING.md keeps those
// three apart, because the size argument below is a claim about the kernel
// alone and must not be read as a claim about the whole system.
//
// This file is written before the kernel exists, on purpose. The security case
// for writing a kernel at all rests entirely on it being small enough to review
// - independence from another kernel's defects is immediate, independence from
// defects is not - and a size ceiling discovered at the end is not a ceiling.
// So the surface is fixed here, as a table, and the kernel is whatever
// implements it.
//
// The reference point is concrete: QNX ran an entire operating system on four
// kernel services behind fourteen calls in 605 lines. Its authors also recorded
// the failure mode, which is the reason for the hard limit below - adding calls
// to speed up individual operations is how a microkernel grows back into a
// monolithic one, and the calls that matter get slower as it happens.
//
// Two rules govern changes to this file:
//
//   - A call may only be added if it cannot be a message to a service. "It
//     would be faster in the kernel" is not sufficient; almost everything is,
//     and that is precisely how the surface erodes.
//
//   - Every call except the unprivileged core is gated on a capability the
//     caller must already hold. There is no ambient authority in this ABI: a
//     thread that has not been granted the right to create address spaces
//     cannot create one, whatever its identity says.
namespace os::kernel {

inline constexpr std::uint16_t kernel_abi_version_v1 = 1U;

// The hard ceiling on the system call surface.
//
// Not a buffer size - a design constraint, checked by test. Reaching it is a
// signal to remove something or to move it into a service, never to raise the
// number.
inline constexpr std::size_t max_kernel_calls = 16U;

// The largest number of arguments any call takes. Calls needing more are
// carrying a structure, and a structure belongs in a message rather than in
// registers.
inline constexpr std::size_t max_call_arguments = 4U;

namespace errors {
inline constexpr std::uint32_t unknown_call = 1U;
// There is deliberately no runtime code for exceeding the surface ceiling: it
// is a static_assert in abi.cpp, so the condition cannot reach a running
// system. An error code nobody can return is a claim that something is checked
// at runtime when it is not.
} // namespace errors

// Zero is never a valid call. A zeroed register set must not name an operation.
enum class KernelCall : std::uint16_t {
    // --- Unprivileged core. Available to every thread, because a thread that
    // cannot send, receive or stop is not a participant in the system.
    send = 1U,
    receive = 2U,
    reply = 3U,
    yield = 4U,
    thread_exit = 5U,

    // --- Scheduling and threads.
    //
    // Two arguments: the address space to admit the thread into, and where its
    // stack is. What it deliberately does not take is the design -
    // docs/M7_12_ENTRY_BINDING.md. No entry point, because that is fixed in the
    // space's sealed root and a caller holding a space it did not author must
    // not choose where signed code begins. No thread identifier, because a
    // caller naming one learns which are live from the refusal. No priority,
    // because the new thread inherits its creator's and a caller-chosen one is
    // a scheduling-escalation lever.
    thread_create = 6U,

    // --- Address spaces. Held by the process manager alone.
    address_space_create = 7U,
    address_space_destroy = 8U,

    // --- Memory. Establishing a mapping is how a driver reaches its registers
    // and how two parties share a buffer, so it is authority, not convenience.
    map = 9U,
    unmap = 10U,

    // --- Interrupts. The handler lives in a user-space driver process; the
    // kernel only dispatches to it and waits to be told the device is quiet.
    interrupt_attach = 11U,
    interrupt_detach = 12U,
    interrupt_complete = 13U,

    // --- Capabilities. ENML's addition to the QNX four, and the reason the
    // rest of the system can treat authority as an object: if transferring one
    // is not a kernel primitive it is a convention, and a convention is what an
    // attacker with a memory-safety bug ignores.
    capability_grant = 14U,
    capability_revoke = 15U,

    // --- Faults. The answer half of the pager handshake: the kernel asks a
    // pager about a region that faulted, and this is how the backing comes
    // back. There is no matching "receive" call, deliberately - the question
    // rides back on the pager's wakeup the same way a driver's interrupt
    // service does, so being asked costs no syscall.
    //
    // One argument, and it names the backing rather than the region. The
    // kernel already knows which question this pager owes; letting the answer
    // name a region would let a pager answer one it was never asked, which is
    // a disclosure the fault path is otherwise careful to withhold.
    fault_supply = 16U,
};

// This fills the table exactly: max_kernel_calls is 16 and fault_supply is the
// sixteenth. The next call added has to raise that ceiling, and that is worth
// deciding on purpose rather than in passing - docs/M7_0_KERNEL.md holds up
// QNX's "four services behind fourteen calls" as the shape being aimed at, and
// Cookie is already two past it.

// What a caller must already hold to make the call.
//
// Deliberately coarse. A fine-grained class per call would be a second
// permission system living inside the first, and the thing being protected is
// the ability to reach the kernel at all - the fine-grained decisions belong to
// the services above it.
enum class CallAuthority : std::uint8_t {
    // No capability required. The four communication primitives and thread
    // exit; withholding these does not confine a thread, it only breaks it.
    unprivileged = 1U,
    // Creating threads and address spaces. The process manager.
    process_control = 2U,
    // Establishing mappings, including device registers.
    memory_control = 3U,
    // Attaching to interrupt vectors. Driver processes only.
    interrupt_control = 4U,
    // Granting and revoking authority. The broker.
    capability_control = 5U,
};

// --- How a call answers. docs/M7_14_SYSCALL_ABI.md.
//
// These live here, in the header both sides include, because they are the
// contract rather than either party's opinion of it. The user half
// (`core/osabi`) aliases them; nothing restates them. Two definitions of a wire
// constant are two things that can drift, and this project has now spent two
// separate diffs on exactly that shape.
//
// The outcome rides in x7 and nothing else does. Argument and result registers
// are x0-x3 (max_call_arguments is 4 and is a design constraint), x8 carries the
// call number, and x4-x6 are unused. Putting the outcome in a register of its
// own is the decision: POSIX's -1 and Linux's reserved negative range both
// reinterpret part of the result space, and both fail silently the day a
// legitimate result lands in the reserved range.
inline constexpr std::size_t outcome_register = 7U;

// Bitwise complements, so their Hamming distance is 64 - no partial corruption
// turns a refusal into an answer. Neither is zero and neither is all-ones, the
// two values a register most plausibly holds by accident.
inline constexpr std::uint64_t outcome_answered = 0xC00C'1EA5'11ED'0001ULL;
inline constexpr std::uint64_t outcome_refused = ~outcome_answered;

static_assert(outcome_answered != 0ULL);
static_assert(outcome_refused != 0ULL);
static_assert(outcome_answered != outcome_refused);
static_assert(outcome_register >= max_call_arguments,
              "the outcome must not land in a register a call uses for "
              "arguments or results");

// An os::core::Error is {domain: u16, reserved: u16, code: u32} - exactly 64
// bits - so a refusal crosses the boundary whole instead of being narrowed to a
// number and widened back into a guess, which is the step where POSIX loses the
// domain and keeps only a code.
[[nodiscard]] constexpr std::uint64_t encode_call_error(
    os::core::Error error) noexcept {
    return (static_cast<std::uint64_t>(error.domain) << 48U) |
           (static_cast<std::uint64_t>(error.reserved) << 32U) |
           static_cast<std::uint64_t>(error.code);
}

struct CallDescriptor final {
    KernelCall call {};
    CallAuthority authority {};
    std::uint8_t argument_count {0U};
    // Whether the call can block the calling thread. Any call that can block is
    // a place where a priority inversion can be constructed, so the set is kept
    // as small as the design allows and is stated rather than discovered.
    bool blocking {false};

    [[nodiscard]] friend constexpr bool
    operator==(const CallDescriptor&, const CallDescriptor&) = default;
};

// The table. This is the kernel's entire interface to everything above it.
[[nodiscard]] os::core::Result<CallDescriptor> describe_call(KernelCall call) noexcept;

// How many calls the surface currently defines.
[[nodiscard]] std::size_t kernel_call_count() noexcept;

// The call at an index, for enumerating the surface. Used by the tests that
// hold the ceiling and by any tool that needs to see the whole interface.
[[nodiscard]] os::core::Result<CallDescriptor> call_at(std::size_t index) noexcept;

// Decodes a call number arriving from a less-trusted thread.
//
// The kernel entry path is the one place in ENML where an attacker chooses a
// number that selects code, so the rule the wire formats already follow applies
// here with the least margin for error: unknown values are rejected, never
// clamped to a default and never used to index anything before validation.
[[nodiscard]] os::core::Result<CallDescriptor> decode_call(std::uint16_t raw) noexcept;

} // namespace os::kernel
