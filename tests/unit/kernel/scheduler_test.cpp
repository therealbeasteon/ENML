#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/kernel/scheduler.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "scheduler: %s\n", what);
    }
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::kernel &&
        result.error().code == code;
}

std::uint64_t remaining(
    const os::kernel::Scheduler& scheduler,
    os::kernel::ThreadId thread) {
    auto left = scheduler.remaining_slice_of(thread);
    return left ? left.value() : 0U;
}

constexpr os::kernel::ThreadId alpha = 11U;
constexpr os::kernel::ThreadId beta = 22U;
constexpr os::kernel::ThreadId gamma = 33U;

constexpr os::kernel::Priority low = 0U;
constexpr os::kernel::Priority high = 5U;

constexpr std::uint64_t slice = os::kernel::default_slice_nanoseconds;

} // namespace

int main() {
    // Tickless: one runnable thread is asked for no timer at all. A timer here
    // could only interrupt the chosen thread in order to choose it again, and
    // that interrupt is the entire measured idle-wakeup budget.
    {
        os::kernel::Scheduler scheduler;
        if (!check(static_cast<bool>(scheduler.admit(alpha, low)), "admit refused")) return 1;

        const auto decision = scheduler.choose(0U);
        if (!check(decision.thread == alpha, "the only runnable thread was not chosen")) return 1;
        if (!check(decision.timer_nanoseconds == 0U,
                   "a timer was requested with nobody to preempt for")) return 1;
        if (!check(!decision.preempted, "the first decision reported a preemption")) return 1;
        if (!check(scheduler.running() == alpha, "running() disagrees with the decision")) {
            return 1;
        }
    }

    // Nothing runnable means idle, and idling asks for no timer either. The
    // processor waits for an interrupt, which is an event that was going to
    // happen anyway.
    {
        os::kernel::Scheduler scheduler;
        (void)scheduler.admit(alpha, low);
        (void)scheduler.update(alpha, false, low);

        const auto decision = scheduler.choose(0U);
        if (!check(decision.thread == os::kernel::invalid_thread,
                   "something was scheduled with nothing runnable")) return 1;
        if (!check(decision.timer_nanoseconds == 0U, "an idle processor asked for a timer")) {
            return 1;
        }
        if (!check(scheduler.runnable_thread_count() == 0U, "wrong runnable count")) return 1;
    }

    // A timer is requested only when something is actually waiting for this
    // priority - and a *lower* priority thread is not waiting for it, because
    // strict priority with no aging means it does not run while this one can.
    // Starvation here is deliberate, and this is where it shows up as an
    // interrupt that is never taken.
    {
        os::kernel::Scheduler scheduler;
        (void)scheduler.admit(alpha, high);
        (void)scheduler.admit(beta, low);

        const auto decision = scheduler.choose(0U);
        if (!check(decision.thread == alpha, "the higher priority thread was not chosen")) {
            return 1;
        }
        if (!check(decision.timer_nanoseconds == 0U,
                   "a timer was requested to preempt for a lower priority thread")) return 1;

        // Add a peer at the same priority and a timer becomes worth setting.
        (void)scheduler.admit(gamma, high);
        const auto contended = scheduler.choose(0U);
        if (!check(contended.thread == alpha, "the running thread lost its unexpired turn")) {
            return 1;
        }
        if (!check(contended.timer_nanoseconds == slice,
                   "no timer was requested with a peer waiting")) return 1;
    }

    // Strict priority: a thread that becomes runnable above the running one
    // takes the processor immediately, and the loss is reported as a preemption.
    // This is the path priority inheritance arrives on - the rendezvous raises a
    // server's effective priority and tells the scheduler through update().
    {
        os::kernel::Scheduler scheduler;
        (void)scheduler.admit(alpha, low);
        (void)scheduler.admit(beta, high);
        (void)scheduler.update(beta, false, high);

        auto first = scheduler.choose(0U);
        if (!check(first.thread == alpha, "the only runnable thread was not chosen")) return 1;

        (void)scheduler.update(beta, true, high);
        auto second = scheduler.choose(1000U);
        if (!check(second.thread == beta, "a higher priority thread did not preempt")) return 1;
        if (!check(second.preempted, "the displaced thread was not reported as preempted")) {
            return 1;
        }
    }

    // Exhausting a slice loses the turn, and the peer gets it.
    {
        os::kernel::Scheduler scheduler;
        (void)scheduler.admit(alpha, low);
        (void)scheduler.admit(beta, low);

        auto first = scheduler.choose(0U);
        if (!check(first.thread == alpha, "admission order was not the round-robin order")) {
            return 1;
        }

        auto second = scheduler.choose(slice);
        if (!check(second.thread == beta, "an exhausted slice did not yield the processor")) {
            return 1;
        }
        if (!check(second.preempted, "running out of slice was not a preemption")) return 1;
        if (!check(remaining(scheduler, alpha) == slice,
                   "the exhausted thread was not replenished")) return 1;

        auto third = scheduler.choose(slice * 2U);
        if (!check(third.thread == alpha, "the round did not come back round")) return 1;
    }

    // The anti-gaming rule. Blocking and waking does not refund the slice: a
    // thread that has run for most of its turn comes back with what is left of
    // it, not with a fresh one. Without this, relinquishing just before the
    // slice expires monopolises the processor - and from unprivileged code.
    {
        os::kernel::Scheduler scheduler;
        (void)scheduler.admit(alpha, low);
        (void)scheduler.admit(beta, low);

        (void)scheduler.choose(0U);

        // Alpha runs for three quarters of its turn and blocks.
        const std::uint64_t spent = (slice / 4U) * 3U;
        (void)scheduler.update(alpha, false, low);
        auto handover = scheduler.choose(spent);
        if (!check(handover.thread == beta, "blocking did not hand over")) return 1;
        if (!check(!handover.preempted, "giving up the processor was called a preemption")) {
            return 1;
        }
        if (!check(remaining(scheduler, alpha) == slice - spent,
                   "a blocked thread was not charged for what it ran")) return 1;

        // Alpha wakes. It is still ahead of beta in the round-robin order, so it
        // runs - but only on what it had left.
        (void)scheduler.update(alpha, true, low);
        auto resumed = scheduler.choose(spent);
        if (!check(resumed.thread == alpha, "the woken thread did not resume")) return 1;
        if (!check(remaining(scheduler, alpha) == slice - spent,
                   "waking refunded the slice")) return 1;
        if (!check(resumed.timer_nanoseconds == slice - spent,
                   "the timer did not reflect the shortened turn")) return 1;

        // And when the remainder runs out it goes to the back, having had
        // exactly one turn's worth of processor across two runs.
        auto rotated = scheduler.choose(slice);
        if (!check(rotated.thread == beta, "the gamed thread kept the processor")) return 1;
    }

    // Yield forfeits the remainder rather than banking it, so it costs something
    // and gains nothing.
    {
        os::kernel::Scheduler scheduler;
        (void)scheduler.admit(alpha, low);
        (void)scheduler.admit(beta, low);
        (void)scheduler.choose(0U);

        if (!check(static_cast<bool>(scheduler.yield_slice(alpha)), "yield refused")) return 1;
        auto after = scheduler.choose(1000U);
        if (!check(after.thread == beta, "yielding did not give up the turn")) return 1;
        if (!check(remaining(scheduler, alpha) == slice,
                   "the yielded slice was banked rather than spent")) return 1;
    }

    // A clock that appears to go backwards charges nothing rather than wrapping.
    // An unsigned subtraction the wrong way round would bill about six hundred
    // years and turn a clock glitch into a scheduling fault.
    {
        os::kernel::Scheduler scheduler;
        (void)scheduler.admit(alpha, low);
        (void)scheduler.choose(1'000'000U);
        (void)scheduler.choose(500'000U);
        if (!check(remaining(scheduler, alpha) == slice,
                   "time going backwards consumed the slice")) return 1;
    }

    // Retiring the running thread is the ordinary case - a thread exiting - and
    // must leave the scheduler able to choose again.
    {
        os::kernel::Scheduler scheduler;
        (void)scheduler.admit(alpha, low);
        (void)scheduler.admit(beta, low);
        (void)scheduler.choose(0U);

        if (!check(static_cast<bool>(scheduler.retire(alpha)), "retire refused")) return 1;
        if (!check(scheduler.running() == os::kernel::invalid_thread,
                   "a retired thread is still recorded as running")) return 1;
        if (!check(scheduler.admitted_thread_count() == 1U, "wrong admitted count")) return 1;

        auto next = scheduler.choose(1000U);
        if (!check(next.thread == beta, "the survivor was not scheduled")) return 1;
        if (!check(!next.preempted, "a retired thread was reported as preempted")) return 1;
        if (!check(refused(scheduler.remaining_slice_of(alpha),
                           os::kernel::scheduler_errors::unknown_thread),
                   "a retired thread is still known")) return 1;
    }

    // Admission is checked rather than assumed.
    {
        os::kernel::Scheduler scheduler;
        if (!check(refused(scheduler.admit(os::kernel::invalid_thread, low),
                           os::kernel::scheduler_errors::invalid_thread_id),
                   "admitted an invalid thread")) return 1;
        (void)scheduler.admit(alpha, low);
        if (!check(refused(scheduler.admit(alpha, high),
                           os::kernel::scheduler_errors::thread_exists),
                   "admitted the same thread twice")) return 1;
        if (!check(refused(scheduler.update(beta, true, low),
                           os::kernel::scheduler_errors::unknown_thread),
                   "updated a thread that was never admitted")) return 1;
        if (!check(refused(scheduler.retire(beta),
                           os::kernel::scheduler_errors::unknown_thread),
                   "retired a thread that was never admitted")) return 1;
    }

    // The table has a stated ceiling and refuses rather than overruns.
    {
        os::kernel::Scheduler scheduler;
        for (std::size_t i = 0U; i < os::kernel::max_threads; ++i) {
            const auto thread = static_cast<os::kernel::ThreadId>(i + 1U);
            if (!check(static_cast<bool>(scheduler.admit(thread, low)),
                       "admit refused below the ceiling")) return 1;
        }
        const auto beyond = static_cast<os::kernel::ThreadId>(os::kernel::max_threads + 1U);
        if (!check(refused(scheduler.admit(beyond, low),
                           os::kernel::scheduler_errors::thread_limit),
                   "the table grew past its ceiling")) return 1;
        if (!check(scheduler.admitted_thread_count() == os::kernel::max_threads,
                   "wrong admitted count at the ceiling")) return 1;
    }

    return 0;
}
