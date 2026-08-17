#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/core/result.hpp>
#include <os/kernel/abi.hpp>
#include <os/kernel/address_space_syscall.hpp>
#include <os/kernel/fault_delivery.hpp>
#include <os/kernel/ipc_syscall.hpp>
#include <os/kernel/map_syscall.hpp>
#include <os/kernel/thread_admission.hpp>

// The EL0 side of the Cookie Kernel's system call surface.
//
// docs/M7_14_SYSCALL_ABI.md holds the reasoning; this file holds the contract.
// Read the document before changing a constant here - every value below is the
// answer to a question that was decided rather than defaulted, and several of
// them cannot be changed once a program depends on them.
//
// This lives outside core/oskernel and must stay there. Nothing in this module
// runs with kernel privilege, so the M7.10 line count does not count it, and
// that is where the claim "the ABI's user half is not in the trusted image"
// stays checkable rather than asserted.
//
// It depends on the kernel's *headers* and not on its library. The type of a
// call's arguments is the kernel's to define - a second definition here would
// be the second-source-of-truth defect docs/M7_11_MEMORY.md and
// docs/M7_12_CKX_FORMAT.md have each refused once already - but no kernel code
// is linked into a user program by including them.
namespace os::abi {

// Argument registers. Not a buffer size: os::kernel::max_call_arguments is a
// design constraint, and a call needing a fifth argument is carrying a
// structure that belongs in a message.
inline constexpr std::size_t argument_registers = os::kernel::max_call_arguments;

// The outcome tags, aliased from the kernel's ABI header rather than restated.
//
// They were defined here first, which was wrong for the reason this project has
// twice paid to learn: a wire constant with two definitions is two things that
// can drift, and the drift has no symptom at the point it happens. The contract
// belongs in the header both sides include. Zero is neither tag, and that is
// the design rather than an omission - see no_answer below.
inline constexpr std::uint64_t outcome_answered = os::kernel::outcome_answered;
inline constexpr std::uint64_t outcome_refused = os::kernel::outcome_refused;
inline constexpr std::size_t outcome_register = os::kernel::outcome_register;

namespace abi_errors {
// The call number is not in the ABI table.
inline constexpr std::uint32_t unknown_call = 340U;
// The caller supplied a different number of arguments than the call's
// descriptor declares. Refused rather than padded: a caller that thinks it is
// passing a deadline where the kernel reads a length is wrong in a way that
// silently corrupts, and the descriptor is the only authority on which it is.
inline constexpr std::uint32_t argument_count = 341U;
// x7 held neither tag. The kernel did not answer this call - it returned
// without writing an outcome, or nothing ran at all. Distinct from a refusal
// on purpose: "refused" is an answer and this is the absence of one.
inline constexpr std::uint32_t no_answer = 342U;
// A refusal arrived whose encoded Error had a non-zero reserved field. The
// same rule .ckx applies to its own reserved bytes: a field with no meaning
// must carry no content, or it becomes a channel.
inline constexpr std::uint32_t malformed_error = 343U;
// The call has no dispatch in the kernel and therefore no argument contract to
// encode against. Listed in calls_without_stubs and checked by test, so this is
// reachable only for a call that has been added to the ABI and not yet decided
// either way.
inline constexpr std::uint32_t not_yet_callable = 344U;
} // namespace abi_errors

[[nodiscard]] constexpr os::core::Error abi_error(std::uint32_t code) noexcept {
    return os::core::Error{os::core::ErrorDomain::kernel, 0U, code};
}

// The register set a call occupies on the way in.
//
// Every argument register is present whether or not the call uses it, because
// the encoder writes zero into the ones it does not - see encode_call.
struct SyscallRequest final {
    std::array<std::uint64_t, argument_registers> arguments {};
    std::uint16_t call {0U};

    [[nodiscard]] friend constexpr bool operator==(
        const SyscallRequest&, const SyscallRequest&) = default;
};

// The register set a call returns in, minus the outcome tag.
struct SyscallAnswer final {
    std::array<std::uint64_t, argument_registers> results {};

    [[nodiscard]] friend constexpr bool operator==(
        const SyscallAnswer&, const SyscallAnswer&) = default;
};

// An os::core::Error is {domain: u16, reserved: u16, code: u32} - exactly 64
// bits - so it crosses the register boundary whole. Nothing is narrowed into a
// number and widened back into a guess, which is the step where POSIX loses the
// domain and keeps only a code.
[[nodiscard]] constexpr std::uint64_t encode_error(os::core::Error error) noexcept {
    return os::kernel::encode_call_error(error);
}

// Why a wrapper rather than `Result<os::core::Error>`: that type cannot be
// written. `Result<T>` has a constructor from `T` and a constructor from
// `Error`, so at T = Error the two are the same signature and every value would
// have to be read as a failure. The wrapper makes "the refusal decoded, and
// here is its reason" and "the refusal did not decode" different types, which
// is what the caller actually needs to tell apart.
struct Refusal final {
    os::core::Error reason {};

    [[nodiscard]] friend constexpr bool operator==(
        const Refusal&, const Refusal&) = default;
};

[[nodiscard]] os::core::Result<Refusal> decode_error(std::uint64_t raw) noexcept;

// Builds the register set for a call.
//
// The argument count comes from describe_call and never from a literal here. A
// stub that hard-codes "send takes three" is a second copy of the ABI, and the
// day the two disagree is a wire break with no symptom at the break.
//
// Registers past the declared count are written zero rather than left alone.
// A leftover value is a disclosure of the caller's state into the kernel, and -
// the reason that matters more - it makes a kernel defect invisible: a dispatch
// that reads an argument its descriptor does not declare gets something
// plausible instead of something obviously wrong.
[[nodiscard]] os::core::Result<SyscallRequest> encode_call(
    os::kernel::KernelCall call,
    const std::array<std::uint64_t, argument_registers>& arguments,
    std::size_t argument_count) noexcept;

// Reads what came back.
//
// `outcome` is x7, which the caller zeroed before the trap. Three cases and
// they are deliberately three rather than two:
//
//   outcome_answered -> the results, whatever the call defines them to be
//   outcome_refused  -> the Error the kernel encoded into x0
//   anything else    -> abi_errors::no_answer
//
// The third is the one this design exists for. A kernel path that returns
// without writing an outcome leaves the zero the caller wrote, and a zero is
// not a success. Without the caller's zeroing, that path would inherit whatever
// the *previous* call left in x7 - and a previous success would make a silent
// path look like a fresh one.
[[nodiscard]] os::core::Result<SyscallAnswer> decode_outcome(
    std::uint64_t outcome,
    const std::array<std::uint64_t, argument_registers>& registers) noexcept;

// --- Per-call encoders.
//
// One for each call whose argument contract the kernel has defined - which is
// the set with a decoder in core/oskernel. Each is checked against that decoder
// by test rather than against a second reading of the ABI document.

[[nodiscard]] os::core::Result<SyscallRequest> encode_send(
    const os::kernel::IpcSendSyscall& request) noexcept;

[[nodiscard]] os::core::Result<SyscallRequest> encode_receive(
    const os::kernel::IpcReceiveSyscall& request) noexcept;

[[nodiscard]] os::core::Result<SyscallRequest> encode_reply(
    const os::kernel::IpcReplySyscall& request) noexcept;

[[nodiscard]] os::core::Result<SyscallRequest> encode_yield() noexcept;

[[nodiscard]] os::core::Result<SyscallRequest> encode_thread_create(
    const os::kernel::ThreadCreateSyscall& request) noexcept;

[[nodiscard]] os::core::Result<SyscallRequest> encode_address_space_create(
    const os::kernel::AddressSpaceCreateSyscall& request) noexcept;

[[nodiscard]] os::core::Result<SyscallRequest> encode_address_space_destroy(
    const os::kernel::AddressSpaceDestroySyscall& request) noexcept;

[[nodiscard]] os::core::Result<SyscallRequest> encode_fault_supply(
    const os::kernel::FaultSupplySyscall& request) noexcept;

// The first of the two calls a loader needs, and the one that had been declared
// longest without existing - see docs/M7_16_MAP.md. Note what it does not take:
// a length. The mapping covers the grant the backing capability names, so there
// is no second statement of an extent the authority already fixes.
[[nodiscard]] os::core::Result<SyscallRequest> encode_map(
    const os::kernel::MapSyscall& request) noexcept;

// The other half, and note that it takes the *backing* rather than a length -
// see docs/M7_16_UNMAP.md. That is the authority half of the call, not a way of
// finding the extent: a caller that holds a space and not the memory in it may
// not take that memory away.
[[nodiscard]] os::core::Result<SyscallRequest> encode_unmap(
    const os::kernel::UnmapSyscall& request) noexcept;

// --- What is deliberately absent.
//
// These calls are in the ABI table and have no decoder in the kernel, so their
// argument contract does not exist anywhere. Writing an encoder for one would
// be inventing that contract from the user side, which is the second source of
// truth this project has now declined twice by name.
//
// The list is a constant rather than a comment so a test can hold it against
// the table: a call may be implemented or listed here, never both and never
// neither. Adding a seventeenth call to the ABI without deciding fails that
// test rather than being discovered at EL0.
inline constexpr std::array<os::kernel::KernelCall, 6U> calls_without_stubs{
    os::kernel::KernelCall::thread_exit,
    os::kernel::KernelCall::interrupt_attach,
    os::kernel::KernelCall::interrupt_detach,
    os::kernel::KernelCall::interrupt_complete,
    os::kernel::KernelCall::capability_grant,
    os::kernel::KernelCall::capability_revoke,
};

// Whether this module encodes a given call. Used by the completeness test and
// by any caller that would rather ask than assume.
[[nodiscard]] bool call_has_stub(os::kernel::KernelCall call) noexcept;

#if defined(__aarch64__)
// Issues the call and returns what came back.
//
// Declared only where it can exist. A host build gets the encoders and the
// outcome decoder - which is the entire contract - and no stub that pretends to
// trap. A seam with nothing behind it is not a capability, and this project has
// already declined one of those once.
[[nodiscard]] os::core::Result<SyscallAnswer> trap(
    const SyscallRequest& request) noexcept;
#endif

} // namespace os::abi
