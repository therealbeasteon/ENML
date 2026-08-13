#include <os/kernel/aarch64_preemption.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    Scheduler scheduler{};
    require(static_cast<bool>(scheduler.admit(1U, 4U)));
    require(static_cast<bool>(scheduler.admit(2U, 4U)));

    ExceptionFrame a{};
    a.elr_el1 = 0x1000U;
    a.sp_el0 = 0x8000U;
    a.spsr_el1 = 0U;
    a.x[0] = 0xA0U;
    ExceptionFrame b{};
    b.elr_el1 = 0x2000U;
    b.sp_el0 = 0x9000U;
    b.spsr_el1 = 0U;
    b.x[0] = 0xB0U;

    PreemptionCoordinator preemption{};
    require(static_cast<bool>(preemption.admit_frame(1U, a)));
    require(static_cast<bool>(preemption.admit_frame(2U, b)));

    ExceptionFrame live{};
    auto start = preemption.start(scheduler, 1'000'000U, live);
    require(static_cast<bool>(start));
    require(start.value().next == 1U);
    require(start.value().deadline.active);
    require(live.elr_el1 == a.elr_el1);

    // Thread A mutates its register state while running. The timer expiry must
    // capture that exact state before the live IRQ frame becomes B's state.
    live.x[0] = 0xA55AU;
    auto to_b = preemption.on_timer(
        scheduler,
        start.value().deadline,
        start.value().deadline.absolute_nanoseconds,
        live);
    require(static_cast<bool>(to_b));
    require(to_b.value().previous == 1U);
    require(to_b.value().next == 2U);
    require(to_b.value().switched);
    require(to_b.value().preempted);
    require(live.elr_el1 == b.elr_el1);
    require(live.x[0] == 0xB0U);

    live.x[0] = 0xB55BU;
    auto to_a = preemption.on_timer(
        scheduler,
        to_b.value().deadline,
        to_b.value().deadline.absolute_nanoseconds,
        live);
    require(static_cast<bool>(to_a));
    require(to_a.value().previous == 2U);
    require(to_a.value().next == 1U);
    require(to_a.value().switched);
    require(live.elr_el1 == a.elr_el1);
    require(live.x[0] == 0xA55AU); // preserved across A -> B -> A

    // Remove contention. The next scheduling decision must cancel the timer,
    // preserving Cookie's no-periodic-tick invariant.
    require(static_cast<bool>(scheduler.update(2U, false, 4U)));
    auto final = preemption.on_timer(
        scheduler,
        to_a.value().deadline,
        to_a.value().deadline.absolute_nanoseconds,
        live);
    require(static_cast<bool>(final));
    require(final.value().next == 1U);
    require(!final.value().deadline.active);

    return 0;
}
