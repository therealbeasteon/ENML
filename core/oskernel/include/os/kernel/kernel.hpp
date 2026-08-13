#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/capability.hpp>
#include <os/kernel/interrupt.hpp>
#include <os/kernel/rendezvous.hpp>
#include <os/kernel/scheduler.hpp>

// The Cookie Kernel, as one object.
//
// M7.1a-e built the four responsibilities from `docs/M7_0_KERNEL.md` as separate
// state machines, each testable without the others. That separation was
// deliberate and it stays - two state machines that each know about the other
// are two state machines neither of which can be tested alone - but it left a
// gap nobody had written down: four individually correct tables are not a
// kernel. Something has to make them agree, and that something is the only place
// the agreement can be enforced.
//
// `docs/M7_1_KERNEL_COMPOSITION.md` records the reasoning. Two obligations
// motivate the whole file.
//
// **A thread's death must reach every table.** Exiting releases the threads
// blocked on it (M7.1a), surrenders the capabilities it held and everything
// derived from them (M7.1c), releases its interrupt sources masked (M7.1d) and
// removes it from the run queue (M7.1e). Each of those is already implemented
// and each is already correct. The failure mode is not a wrong implementation,
// it is a forgotten call - and a forgotten call here is a capability that
// outlives its holder or an interrupt line nobody owns. So destruction is one
// operation that does all four and reports what each one released, rather than
// four operations a caller is trusted to remember.
//
// **The scheduler's view is recomputed, never patched.** The rendezvous decides
// whether a thread is runnable and what priority it effectively runs at; the
// scheduler needs both and holds a cache of them. An incrementally updated cache
// drifts the first time a path is missed, and the two ways it can drift are both
// serious: a thread ready in the rendezvous but not runnable in the scheduler is
// one that never runs again, and the reverse is a thread on the processor while
// blocked. So after any operation that can change either fact, every live
// thread's entry is recomputed from the rendezvous. This is the same argument
// M7.1b already made for inherited priority - recomputed rather than adjusted,
// because an adjustment that is missed once stays wrong - applied to the place
// where two components have to agree.
//
// The cost is a bounded scan of the thread table per system call. That is the
// price of not having a cache-coherency problem inside the kernel, and it is
// cheap at a stated ceiling of 64 threads.
namespace os::kernel {

namespace kernel_errors {
// The thread tables disagreed about capacity. Unreachable while the rendezvous
// and the scheduler share `max_threads`, and checked because a partially
// created thread is worse than a refused one.
inline constexpr std::uint32_t creation_incomplete = 1U;
} // namespace kernel_errors

// What a thread's death released. Reported rather than assumed, so a caller can
// observe the obligation was met instead of trusting that it was.
struct Teardown final {
    // Threads that were blocked on this one and are now runnable again.
    std::size_t threads_released {0U};
    // Capabilities it held, plus everything derived from them.
    std::size_t capabilities_revoked {0U};
    // Interrupt sources it owned, all left masked.
    std::size_t interrupt_sources_released {0U};

    [[nodiscard]] friend constexpr bool operator==(const Teardown&, const Teardown&) = default;
};

class Kernel final {
public:
    Kernel() noexcept = default;

    [[nodiscard]] os::core::Result<void> create_thread(
        ThreadId thread,
        Priority priority = default_priority) noexcept;

    // Destroys a thread and settles every obligation it left behind.
    //
    // The one operation in this file that would be a security defect if it were
    // four operations, because three of the four are easy to forget and none of
    // them fails loudly when forgotten.
    os::core::Result<Teardown> destroy_thread(ThreadId thread) noexcept;

    [[nodiscard]] os::core::Result<void> send(ThreadId from, ThreadId to) noexcept;
    [[nodiscard]] os::core::Result<ThreadId> receive(ThreadId self) noexcept;
    [[nodiscard]] os::core::Result<void> reply(ThreadId self, ThreadId caller) noexcept;
    [[nodiscard]] os::core::Result<void> yield(ThreadId self) noexcept;

    // Takes an interrupt and makes its owner runnable if this is the assertion
    // that starts a burst.
    os::core::Result<Dispatch> dispatch_interrupt(InterruptSource source) noexcept;

    // Who runs now, and when to ask again.
    Decision schedule(std::uint64_t now_nanoseconds) noexcept;

    // The tables, for the operations that need no composing - granting a
    // capability changes nothing about who is runnable, and attaching an
    // interrupt source changes nothing about who holds what.
    [[nodiscard]] CapabilityTable& capabilities() noexcept { return capabilities_; }
    [[nodiscard]] const CapabilityTable& capabilities() const noexcept { return capabilities_; }
    [[nodiscard]] InterruptTable& interrupts() noexcept { return interrupts_; }
    [[nodiscard]] const InterruptTable& interrupts() const noexcept { return interrupts_; }
    [[nodiscard]] const Rendezvous& threads() const noexcept { return threads_; }
    [[nodiscard]] const Scheduler& runqueue() const noexcept { return scheduler_; }

    [[nodiscard]] std::size_t live_thread_count() const noexcept;

private:
    // Recomputes every live thread's runnability and effective priority from the
    // rendezvous. Called after anything that can change either.
    void synchronise() noexcept;

    [[nodiscard]] bool tracks(ThreadId thread) const noexcept;
    void untrack(ThreadId thread) noexcept;

    Rendezvous threads_ {};
    CapabilityTable capabilities_ {};
    InterruptTable interrupts_ {};
    Scheduler scheduler_ {};

    // The kernel's own record of which threads exist, because the rendezvous
    // does not enumerate and giving it that ability would be adding a query for
    // one caller's convenience.
    std::array<ThreadId, max_threads> live_ {};
    std::size_t live_count_ {0U};
};

} // namespace os::kernel
