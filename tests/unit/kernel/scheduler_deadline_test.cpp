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

    return 0;
}
