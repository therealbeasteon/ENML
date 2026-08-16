#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/capability.hpp>
#include <os/kernel/rendezvous.hpp>

namespace os::kernel {

namespace thread_admission_errors {
inline constexpr std::uint32_t invalid_capability = 320U;
inline constexpr std::uint32_t invalid_stack = 321U;
inline constexpr std::uint32_t identifier_exhausted = 322U;
inline constexpr std::uint32_t creation_incomplete = 323U;
} // namespace thread_admission_errors

// Where kernel-issued thread identifiers start.
//
// Boot's own hand-made threads take identifiers below this, so the counter can
// never collide with one. That is belt and braces rather than the guarantee -
// Rendezvous::create_thread refuses a duplicate and would catch a collision
// loudly - but a reserved floor means the backstop is never reached.
inline constexpr ThreadId first_admitted_thread = 0x1000U;

// What admission produces. All three are answers the caller did not supply.
//
// The identifier is chosen by the kernel because a caller that names one learns
// from the refusal: Rendezvous answers thread_exists, which a process holding
// process_control could use to enumerate every live thread on the machine. That
// is the disclosure class docs/M7_11_FAULT_PRIVACY.md refused for faulting
// addresses, reached through a different door.
//
// The entry comes from the address space's sealed root and never from the
// caller - see docs/M7_12_ENTRY_BINDING.md. It is returned here so the machine
// layer building the initial frame does not have to ask anyone for it.
struct ThreadAdmission final {
    ThreadId thread {invalid_thread};
    std::uint64_t entry {0ULL};
    std::uint64_t stack {0ULL};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return thread != invalid_thread && entry != 0ULL && stack != 0ULL;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const ThreadAdmission&, const ThreadAdmission&) = default;
};

// KernelCall::thread_create. Two arguments, and the two it does not take are
// the decisions.
//
// No entry point: that is the space's, fixed when its root was sealed. No
// identifier: the kernel issues one. No priority either - the new thread runs
// at its creator's, because a caller-chosen priority is a scheduling-escalation
// lever and nothing needs the argument yet.
//
// The stack *is* the caller's, and the asymmetry with the entry is the whole
// rule: a caller may not choose where code begins, but may choose where data
// lives. It already holds the space, so it could place a stack anywhere by
// mapping; refusing the argument would remove nothing and cost a round trip.
struct ThreadCreateSyscall final {
    CapabilityId space {invalid_capability};
    std::uint64_t stack {0ULL};
};

[[nodiscard]] os::core::Result<ThreadCreateSyscall> decode_thread_create_syscall(
    std::uint64_t x0_space,
    std::uint64_t x1_stack) noexcept;

// Issues thread identifiers that are never reused within a boot.
//
// Reuse is the mechanism behind the whole POSIX pid-reuse family of defects: a
// reference held across the death of its target silently comes to name a
// different live target and the holder cannot tell. Address spaces already
// refuse this by folding a generation into their object identifier; threads get
// the same property by the cheaper route available to them, which is that
// nothing is ever the successor.
//
// Exhaustion is an ordinary error the caller receives rather than a wrap. A
// counter that wrapped would reintroduce reuse at exactly the moment nobody is
// watching, and the no-allocator decision already established that running out
// of a kernel resource is something a caller is told.
class ThreadIdentifierIssuer final {
public:
    [[nodiscard]] os::core::Result<ThreadId> issue() noexcept;
    [[nodiscard]] ThreadId next() const noexcept { return next_; }

private:
    ThreadId next_ {first_admitted_thread};
};

} // namespace os::kernel
