#include <os/kernel/scheduler_deadline.hpp>

#include <cstdlib>

namespace {
// Templated so a Result can be passed directly. Result's operator bool is
// explicit, which satisfies the contextual conversion in `!value` but not
// an implicit conversion to a bool parameter.
template <typename T>
void require(const T& value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;

    SchedulerDeadlineAuthority authority{};

    Decision first{};
    first.thread = 1U;
    first.timer_nanoseconds = 2'000'000U;
    auto d1 = authority.apply(first, 10'000'000U);
    require(static_cast<bool>(d1));
    require(d1.value().valid());
    require(d1.value().absolute_nanoseconds == 12'000'000U);

    auto early = authority.accept_interrupt(d1.value(), 11'999'999U);
    require(!early);
    require(early.error().code == scheduler_deadline_errors::early);
    require(static_cast<bool>(authority.accept_interrupt(d1.value(), 12'000'000U)));

    // A new scheduling decision revokes the previous hardware deadline even
    // when it also needs a timer.
    Decision second{};
    second.thread = 2U;
    second.timer_nanoseconds = 1'000'000U;
    auto d2 = authority.apply(second, 12'000'000U);
    require(static_cast<bool>(d2));
    require(d2.value().generation != d1.value().generation);
    auto stale = authority.accept_interrupt(d1.value(), 13'000'000U);
    require(!stale);
    require(stale.error().code == scheduler_deadline_errors::stale);
    require(static_cast<bool>(authority.accept_interrupt(d2.value(), 13'000'000U)));

    // Tickless idle / uncontested execution is an explicit revocation, not a
    // zero-duration timer that immediately interrupts again.
    Decision no_timer{};
    no_timer.thread = 2U;
    no_timer.timer_nanoseconds = 0U;
    auto cancelled = authority.apply(no_timer, 13'000'000U);
    require(static_cast<bool>(cancelled));
    require(!cancelled.value().active);
    auto after_cancel = authority.accept_interrupt(d2.value(), 14'000'000U);
    require(!after_cancel);
    require(after_cancel.error().code == scheduler_deadline_errors::inactive);

    // ------------------------------------------------------------------
    // narrow_decision_timer: an IPC deadline may only bring a wakeup forward.
    // ------------------------------------------------------------------
    {
        using os::kernel::narrow_decision_timer;
        os::kernel::Decision slice{};
        slice.thread = 4U;
        slice.timer_nanoseconds = 2'000'000U; // a 2ms round-robin slice

        // Nothing waiting: untouched.
        require(narrow_decision_timer(slice, 0ULL, 1'000U).timer_nanoseconds
                == 2'000'000U);

        // A later receive deadline does not postpone the slice.
        require(narrow_decision_timer(slice, 1'000U + 9'000'000U, 1'000U)
                    .timer_nanoseconds == 2'000'000U);

        // An earlier one brings the wakeup forward.
        require(narrow_decision_timer(slice, 1'000U + 500'000U, 1'000U)
                    .timer_nanoseconds == 500'000U);

        // With no scheduling timer at all, the receive deadline supplies one.
        os::kernel::Decision idle{};
        idle.thread = 4U;
        idle.timer_nanoseconds = 0U;
        require(narrow_decision_timer(idle, 1'000U + 750U, 1'000U)
                    .timer_nanoseconds == 750U);
        require(narrow_decision_timer(idle, 0ULL, 1'000U).timer_nanoseconds == 0U);

        // Already past: the smallest expressible wait, never zero - zero means
        // "no timer" and would cancel the wakeup it was asked to hasten.
        require(narrow_decision_timer(slice, 500U, 1'000U).timer_nanoseconds == 1U);
        require(narrow_decision_timer(idle, 1'000U, 1'000U).timer_nanoseconds == 1U);

        // Narrowing never touches anything but the timer.
        const auto narrowed = narrow_decision_timer(slice, 1'000U + 10U, 1'000U);
        require(narrowed.thread == slice.thread);
        require(narrowed.preempted == slice.preempted);
    }

    return 0;
}
