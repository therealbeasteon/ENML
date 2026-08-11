#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/rendezvous.hpp>

// The Cookie Kernel's scheduler, as a state machine.
//
// The last part of the first kernel responsibility in `docs/M7_0_KERNEL.md` -
// address spaces and threads, of which threads and their scheduling are the half
// that needs no hardware. Like the rendezvous, the capability table and
// interrupt dispatch, it is pure logic and runs on a development host.
// `docs/M7_1_SCHEDULER.md` records the reasoning.
//
// Two things the references establish, and one constraint that is ENML's alone.
//
// The reference microkernel is fully preemptive and strictly priority-ordered,
// and it makes a point that matters more than the ordering: because a message
// pass is a scheduling event, threads get scheduled at send, receive and reply
// boundaries "rather than only at system timer intervals". Scheduling decisions
// ride on work that was happening anyway.
//
// The teaching references supply the failure mode. A scheduler that resets a
// thread's standing whenever it gives up the processor can be *gamed*: run for
// almost all of a slice, relinquish voluntarily, keep your position, repeat -
// and, as they put it, nearly monopolise the processor. Their fix is to account
// for the total time a thread has run at a level regardless of how often it let
// go, and to periodically boost everyone to keep long-running work from
// starving.
//
// The constraint that is ENML's own is the one that rules half of that out.
// Cookie measures idle wakeups and gates on the number - M4.7 found two
// supervised services waking roughly a hundred times a second and now measures
// zero. A periodic priority boost is a periodic timer, and a scheduler with a
// tick would spend the entire idle-wakeup budget before doing any work. So:
//
//   - **Time is charged, never refunded.** A slice is consumed by nanoseconds
//     actually run, whatever the thread does with them. Yielding, sending and
//     blocking do not give any of it back. This is the anti-gaming rule, and
//     here it is a security rule rather than a fairness one: a thread that can
//     relinquish and keep its position can monopolise a processor from
//     unprivileged code, which is the same class of denial of service as the
//     priority inversion M7.1b closed and is reachable the same way.
//
//   - **There is no tick.** The scheduler never asks for a periodic interrupt.
//     It reports the one deadline at which it next needs to be consulted, and
//     when nothing is competing for the processor it asks for no timer at all.
//     Every other scheduling point is an event that was going to happen anyway -
//     a message pass, an interrupt, a thread exiting.
//
//   - **Slices refill lazily.** Replenishment is arithmetic done at a scheduling
//     point, from monotonic time, because a decision is being made there anyway.
//     A refill that needs its own interrupt is a tick wearing a different name.
//
//   - **Priority is never stored here as truth.** Effective priority - a
//     thread's own, or that of the most urgent thread waiting on it - is the
//     rendezvous's answer, and this table caches what it was told. Two sources
//     of truth for priority is exactly how inversion comes back after being
//     fixed.
//
// Starvation is deliberate and is not a defect. A lower-priority thread does not
// run while a higher-priority one is runnable, and nothing ages. The reference's
// periodic boost answers a timesharing workload with long batch jobs; a phone
// that lets background work delay a touch response is a phone that feels broken,
// and priority 7 in `PROJECT_VISION.md` asks for less background activity, not
// more. The defence against a thread monopolising a high priority is that
// priority is granted from above rather than taken - see the threat model.
namespace os::kernel {

// How long a thread runs before equal-priority peers get a turn.
//
// Chosen against a frame rather than against a reference's number: at 60 Hz a
// frame is about 16.6 ms, so a slice has to be well inside one for equal
// priority threads to interleave within a single frame, and every halving buys
// that at the cost of another context switch. Two milliseconds is eight turns
// per frame.
inline constexpr std::uint64_t default_slice_nanoseconds = 2'000'000U;

namespace scheduler_errors {
inline constexpr std::uint32_t invalid_thread_id = 1U;
inline constexpr std::uint32_t unknown_thread = 2U;
inline constexpr std::uint32_t thread_exists = 3U;
inline constexpr std::uint32_t thread_limit = 4U;
} // namespace scheduler_errors

// What the kernel should do now.
struct Decision final {
    // Who runs, or invalid_thread when nothing is runnable and the processor
    // should idle.
    ThreadId thread {invalid_thread};
    // When to consult the scheduler again, in nanoseconds from now. Zero means
    // *no timer* - not "immediately". Nothing is competing for this priority, so
    // a timer could only preempt the chosen thread in order to choose it again.
    std::uint64_t timer_nanoseconds {0U};
    // Whether the previously running thread lost the processor rather than
    // giving it up. Reported because "was I preempted" and "did I finish" are
    // different facts, and a caller that cannot tell them apart cannot account
    // for either.
    bool preempted {false};

    [[nodiscard]] friend constexpr bool operator==(const Decision&, const Decision&) = default;
};

class Scheduler final {
public:
    Scheduler() noexcept = default;

    [[nodiscard]] os::core::Result<void> admit(
        ThreadId thread,
        Priority effective = default_priority) noexcept;

    // Removes a thread. Safe to call for the thread currently running, which is
    // the case that matters - a thread exiting is the most common way one stops
    // being schedulable.
    [[nodiscard]] os::core::Result<void> retire(ThreadId thread) noexcept;

    // Tells the scheduler what the rendezvous now says about a thread.
    //
    // Called after any transition that could change either fact: a send that
    // blocks, a reply that wakes, a thread whose server inherited its priority.
    // The scheduler does not reach into the rendezvous to ask, because two state
    // machines that each know about the other are two state machines neither of
    // which can be tested alone.
    [[nodiscard]] os::core::Result<void> update(
        ThreadId thread,
        bool runnable,
        Priority effective) noexcept;

    // Gives up the rest of this thread's turn. This is what the ABI's `yield`
    // call does.
    //
    // Forfeits the remainder rather than banking it, so the thread goes to the
    // back of its priority at the next decision. Yielding therefore costs
    // something and gains nothing, which is the only way it can be offered to
    // unprivileged code: a yield that preserved standing would be the reference's
    // gaming attack with a system call in front of it.
    [[nodiscard]] os::core::Result<void> yield_slice(ThreadId thread) noexcept;

    // Charges the running thread for the time since the last decision and
    // chooses who runs now.
    //
    // `now_nanoseconds` is monotonic time from the machine layer. Calling this
    // is the only way time is accounted, which is what keeps the scheduler
    // tickless: every caller of it is already handling an event.
    Decision choose(std::uint64_t now_nanoseconds) noexcept;

    [[nodiscard]] ThreadId running() const noexcept;
    [[nodiscard]] os::core::Result<std::uint64_t> remaining_slice_of(
        ThreadId thread) const noexcept;
    [[nodiscard]] os::core::Result<bool> is_runnable(ThreadId thread) const noexcept;
    [[nodiscard]] std::size_t admitted_thread_count() const noexcept;
    [[nodiscard]] std::size_t runnable_thread_count() const noexcept;

private:
    struct Slot final {
        ThreadId thread {invalid_thread};
        Priority effective {default_priority};
        bool runnable {true};
        std::uint64_t remaining {default_slice_nanoseconds};
        // Round-robin position. Lower runs first; a thread that has never run
        // sits at zero and therefore goes ahead of one that has.
        std::uint64_t sequence {0U};
        bool occupied {false};
    };

    [[nodiscard]] Slot* find(ThreadId thread) noexcept;
    [[nodiscard]] const Slot* find(ThreadId thread) const noexcept;

    std::array<Slot, max_threads> slots_ {};
    std::size_t occupied_ {0U};
    ThreadId running_ {invalid_thread};
    // When the running thread was last given the processor, so the next decision
    // can charge it for what it used.
    std::uint64_t last_decision_ {0U};
    bool have_decided_ {false};
    std::uint64_t next_sequence_ {1U};
};

} // namespace os::kernel
