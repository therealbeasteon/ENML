#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>

namespace os::kernel {

inline constexpr std::size_t max_threads = 64U;
using ThreadId = std::uint32_t;
inline constexpr ThreadId invalid_thread = 0U;
using Priority = std::uint8_t;
inline constexpr Priority default_priority = 0U;

namespace rendezvous_errors {
inline constexpr std::uint32_t invalid_thread_id = 1U;
inline constexpr std::uint32_t unknown_thread = 2U;
inline constexpr std::uint32_t thread_exists = 3U;
inline constexpr std::uint32_t thread_limit = 4U;
inline constexpr std::uint32_t not_runnable = 5U;
inline constexpr std::uint32_t self_addressed = 6U;
inline constexpr std::uint32_t not_awaiting_reply = 7U;
inline constexpr std::uint32_t would_block = 8U;
inline constexpr std::uint32_t not_waiting_on_peer = 9U;
} // namespace rendezvous_errors

enum class ThreadState : std::uint8_t {
    ready = 1U,
    send_blocked = 2U,
    reply_blocked = 3U,
    receive_blocked = 4U,
    exited = 5U,
};

enum class WakeReason : std::uint8_t {
    none = 1U,
    replied = 2U,
    peer_exited = 3U,
    endpoint_retired = 4U,
    // A bounded receive reached its deadline with no message. Distinct from
    // endpoint_retired on purpose: that says the endpoint is gone and retrying
    // is pointless, this says nothing arrived in time and retrying is the
    // normal thing to do. Collapsing them would make a service treat a routine
    // timeout as a dead peer.
    deadline_expired = 5U,
};

// Pure synchronous scheduling/relationship state. Payloads, endpoint identity,
// capabilities and reply seals deliberately live outside this class so this
// remains a small state machine with fixed bounds.
class Rendezvous final {
public:
    Rendezvous() noexcept = default;

    [[nodiscard]] os::core::Result<void> create_thread(
        ThreadId thread,
        Priority priority = default_priority) noexcept;
    os::core::Result<std::size_t> exit_thread(ThreadId thread) noexcept;

    // Generic thread-addressed wrappers retained for the base rendezvous API.
    [[nodiscard]] os::core::Result<void> send(ThreadId from, ThreadId to) noexcept;
    [[nodiscard]] os::core::Result<ThreadId> receive(ThreadId self) noexcept;
    [[nodiscard]] os::core::Result<void> reply(ThreadId self, ThreadId caller) noexcept;

    // Precise primitives used by endpoint-addressed IPC. They never choose an
    // arbitrary peer: the IPC layer decides which endpoint/caller relationship
    // is eligible, while Rendezvous only performs the requested state change.
    [[nodiscard]] os::core::Result<void> block_send(ThreadId from, ThreadId to) noexcept;
    [[nodiscard]] os::core::Result<void> wait_receive(ThreadId self) noexcept;
    [[nodiscard]] os::core::Result<void> deliver_waiting_receiver(
        ThreadId from,
        ThreadId to) noexcept;
    [[nodiscard]] os::core::Result<void> accept_sender(
        ThreadId self,
        ThreadId caller) noexcept;
    [[nodiscard]] os::core::Result<void> cancel_receive(ThreadId self) noexcept;

    // Wakes a receiver whose bounded wait expired. Same transition as
    // cancel_receive and deliberately a separate entry point rather than a
    // reason parameter on it: cancel_receive is reached from endpoint
    // retirement, this from the kernel's own timer, and a shared parameter
    // would let a future caller pass the wrong one by accident. Like
    // cancel_receive it can only move a thread that is genuinely
    // receive-blocked with no partner, so arbitrary thread wakeup stays
    // impossible.
    [[nodiscard]] os::core::Result<void> expire_receive(ThreadId self) noexcept;

    // Reports whether this thread's pending wake is an expired bounded receive,
    // and clears it if so. Consuming rather than observing, and that is forced
    // rather than stylistic: every other wake has a payload whose removal is
    // the consumption - a reply is taken, a delivered message is taken - but an
    // expiry has nothing to take, because the whole point is that no message
    // arrived. Without a consuming read the completion path would report the
    // same timeout on every switch back to the thread and overwrite its
    // registers each time.
    [[nodiscard]] os::core::Result<bool> take_deadline_expiry(ThreadId self) noexcept;

    // Narrow cancellation primitive used by endpoint retirement. It can only
    // release a caller that is currently blocked on the exact expected server;
    // arbitrary thread wakeup remains impossible. If a blocked receive had
    // already been completed by this caller, its one-shot delivery identity is
    // cleared from the server too.
    [[nodiscard]] os::core::Result<void> cancel_call(
        ThreadId caller,
        ThreadId expected_server) noexcept;

    [[nodiscard]] os::core::Result<ThreadState> state_of(ThreadId thread) const noexcept;
    [[nodiscard]] os::core::Result<WakeReason> wake_reason_of(ThreadId thread) const noexcept;
    [[nodiscard]] os::core::Result<ThreadId> partner_of(ThreadId thread) const noexcept;
    [[nodiscard]] os::core::Result<Priority> effective_priority_of(ThreadId thread) const noexcept;
    [[nodiscard]] os::core::Result<Priority> base_priority_of(ThreadId thread) const noexcept;
    [[nodiscard]] std::size_t live_thread_count() const noexcept;

private:
    struct Slot final {
        ThreadId thread {invalid_thread};
        ThreadState state {ThreadState::exited};
        ThreadId partner {invalid_thread};
        WakeReason wake {WakeReason::none};
        Priority base {default_priority};
        bool occupied {false};
    };

    [[nodiscard]] Priority inherited_priority(ThreadId thread) const noexcept;
    [[nodiscard]] Slot* find(ThreadId thread) noexcept;
    [[nodiscard]] const Slot* find(ThreadId thread) const noexcept;

    std::array<Slot, max_threads> slots_ {};
    std::size_t occupied_ {0U};
};

} // namespace os::kernel
