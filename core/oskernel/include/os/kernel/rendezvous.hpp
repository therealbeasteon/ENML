#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>

// The Cookie Kernel's message-passing core, as a state machine.
//
// This is the largest single thing the kernel does and the part an attacker
// reaches first, so it is written as pure logic with no memory mapping, no
// scheduling and no hardware in it at all. That is not a simplification to be
// undone later: the machine layer will drive this, not replace it, and keeping
// it separable is what lets the rules below be tested and fuzzed on a
// development host years before a board exists.
//
// The shape is the one the references demonstrate rather than argue: a
// synchronous send/receive/reply rendezvous, with the message copied directly
// between address spaces and never queued in the kernel. Nothing is buffered,
// so there is no kernel memory an unprivileged thread can cause to grow, and no
// queue depth to tune. A sender waits for a receiver; that is the whole
// back-pressure mechanism.
//
// Three of the rules here are security properties rather than bookkeeping:
//
//   - **Reply is bound to a received message, never to a thread id.** A thread
//     may only reply to a caller that is currently blocked awaiting *its*
//     answer. Without this, reply takes an arbitrary thread id from a register
//     and unblocks whoever it names, which lets any thread interfere with any
//     conversation it is not part of.
//
//   - **Exiting releases everyone waiting on you.** A thread that dies while
//     others are blocked on it must leave them runnable, or a crash becomes a
//     permanent denial of service against every client that trusted it. This is
//     the same reclamation obligation the partition ledger has for a killed
//     process, in the one place where getting it wrong is unrecoverable.
//
//   - **A thread is in exactly one state.** Every transition is checked against
//     the current state rather than assumed, because a state machine that can
//     be driven into two states at once is one where the checks above stop
//     meaning anything.
namespace os::kernel {

// A ceiling, not a capacity target. The kernel knowing an exact upper bound on
// threads is what lets every structure here be a fixed array with no allocator
// underneath it.
inline constexpr std::size_t max_threads = 64U;

// Zero is never a valid thread. A zeroed register must not name one.
using ThreadId = std::uint32_t;
inline constexpr ThreadId invalid_thread = 0U;

namespace rendezvous_errors {
inline constexpr std::uint32_t invalid_thread_id = 1U;
inline constexpr std::uint32_t unknown_thread = 2U;
inline constexpr std::uint32_t thread_exists = 3U;
inline constexpr std::uint32_t thread_limit = 4U;
inline constexpr std::uint32_t not_runnable = 5U;
inline constexpr std::uint32_t self_addressed = 6U;
// Replying to a thread that is not waiting for this thread's answer.
inline constexpr std::uint32_t not_awaiting_reply = 7U;
// Receiving when already blocked, or with nothing pending and no wish to wait.
inline constexpr std::uint32_t would_block = 8U;
} // namespace rendezvous_errors

enum class ThreadState : std::uint8_t {
    // Runnable.
    ready = 1U,
    // Sent to a thread that was not yet receiving; waiting to be collected.
    send_blocked = 2U,
    // Message was taken by a server; waiting for that server's reply.
    reply_blocked = 3U,
    // Called receive with nothing pending.
    receive_blocked = 4U,
    // Gone. The slot is kept so that stale references resolve to a definite
    // answer rather than to whatever thread is created next.
    exited = 5U,
};

// Why a blocked thread became runnable again.
//
// Carried because "you are running again" is not enough information for a
// caller to know whether its message was answered or its peer died - and a
// caller that cannot tell those apart will treat a death as a reply.
enum class WakeReason : std::uint8_t {
    // Nothing has happened to this thread since it last ran.
    none = 1U,
    // A server answered.
    replied = 2U,
    // The thread this one was waiting on no longer exists.
    peer_exited = 3U,
};

class Rendezvous final {
public:
    Rendezvous() noexcept = default;

    [[nodiscard]] os::core::Result<void> create_thread(ThreadId thread) noexcept;

    // Removes a thread and releases everyone blocked on it.
    //
    // Returns how many threads were released, so a caller can observe that the
    // obligation was met rather than trusting it.
    os::core::Result<std::size_t> exit_thread(ThreadId thread) noexcept;

    // Blocks the sender until a receiver takes the message and replies.
    [[nodiscard]] os::core::Result<void> send(ThreadId from, ThreadId to) noexcept;

    // Takes a waiting message if there is one, otherwise blocks.
    //
    // Returns the sender, or invalid_thread when the caller has been parked.
    [[nodiscard]] os::core::Result<ThreadId> receive(ThreadId self) noexcept;

    // Answers a caller. Only permitted for a caller awaiting *this* thread's
    // reply, which is what makes reply an authority rather than an argument.
    [[nodiscard]] os::core::Result<void> reply(ThreadId self, ThreadId caller) noexcept;

    [[nodiscard]] os::core::Result<ThreadState> state_of(ThreadId thread) const noexcept;
    [[nodiscard]] os::core::Result<WakeReason> wake_reason_of(ThreadId thread) const noexcept;
    // Who this thread is blocked on, or invalid_thread when it is not blocked.
    [[nodiscard]] os::core::Result<ThreadId> partner_of(ThreadId thread) const noexcept;
    [[nodiscard]] std::size_t live_thread_count() const noexcept;

private:
    struct Slot final {
        ThreadId thread {invalid_thread};
        ThreadState state {ThreadState::exited};
        ThreadId partner {invalid_thread};
        WakeReason wake {WakeReason::none};
        bool occupied {false};
    };

    [[nodiscard]] Slot* find(ThreadId thread) noexcept;
    [[nodiscard]] const Slot* find(ThreadId thread) const noexcept;

    std::array<Slot, max_threads> slots_ {};
    std::size_t occupied_ {0};
};

} // namespace os::kernel
