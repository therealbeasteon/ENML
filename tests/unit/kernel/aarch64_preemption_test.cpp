#include <os/kernel/aarch64_preemption.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    AddressSpaceEpochAuthority epochs{};
    auto epoch_a = epochs.acquire();
    auto epoch_b = epochs.acquire();
    require(epoch_a && epoch_b);

    ProcessTranslationTable translations{};
    require(translations.bind(1U, epoch_a.value(), 0x1000ULL, epochs));
    require(translations.bind(2U, epoch_b.value(), 0x2000ULL, epochs));

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
    auto start = preemption.start(scheduler, translations, epochs, 1'000'000U, live);
    require(static_cast<bool>(start));
    require(start.value().next == 1U);
    require(start.value().translation.epoch == epoch_a.value());
    require(start.value().translation.root_physical == 0x1000ULL);
    require(start.value().deadline.active);
    require(live.elr_el1 == a.elr_el1);

    // Thread A mutates its register state while running. The timer expiry must
    // capture that exact state before the live IRQ frame becomes B's state.
    live.x[0] = 0xA55AU;
    auto to_b = preemption.on_timer(
        scheduler,
        translations,
        epochs,
        start.value().deadline,
        start.value().deadline.absolute_nanoseconds,
        live);
    require(static_cast<bool>(to_b));
    require(to_b.value().previous == 1U);
    require(to_b.value().next == 2U);
    require(to_b.value().translation.epoch == epoch_b.value());
    require(to_b.value().translation.root_physical == 0x2000ULL);
    require(to_b.value().switched);
    require(to_b.value().preempted);
    require(live.elr_el1 == b.elr_el1);
    require(live.x[0] == 0xB0U);

    live.x[0] = 0xB55BU;
    auto to_a = preemption.on_timer(
        scheduler,
        translations,
        epochs,
        to_b.value().deadline,
        to_b.value().deadline.absolute_nanoseconds,
        live);
    require(static_cast<bool>(to_a));
    require(to_a.value().previous == 2U);
    require(to_a.value().next == 1U);
    require(to_a.value().translation.epoch == epoch_a.value());
    require(to_a.value().switched);
    require(live.elr_el1 == a.elr_el1);
    require(live.x[0] == 0xA55AU); // preserved across A -> B -> A

    // Remove contention. The next scheduling decision must cancel the timer,
    // preserving Cookie's no-periodic-tick invariant.
    require(static_cast<bool>(scheduler.update(2U, false, 4U)));
    auto final = preemption.on_timer(
        scheduler,
        translations,
        epochs,
        to_a.value().deadline,
        to_a.value().deadline.absolute_nanoseconds,
        live);
    require(static_cast<bool>(final));
    require(final.value().next == 1U);
    require(!final.value().deadline.active);

    // A stale selected memory universe must fail before the live execution
    // frame changes. This is Cookie's atomic execution-universe boundary.
    AddressSpaceEpochAuthority stale_epochs{};
    auto stale_epoch = stale_epochs.acquire();
    require(stale_epoch);
    ProcessTranslationTable stale_translations{};
    require(stale_translations.bind(7U, stale_epoch.value(), 0x3000ULL, stale_epochs));
    auto retiring = stale_epochs.begin_retire(stale_epoch.value());
    require(retiring);

    Scheduler stale_scheduler{};
    require(stale_scheduler.admit(7U, 4U));
    PreemptionCoordinator stale_preemption{};
    ExceptionFrame stale_frame{};
    stale_frame.elr_el1 = 0x7000U;
    stale_frame.sp_el0 = 0xA000U;
    stale_frame.spsr_el1 = 0U;
    require(stale_preemption.admit_frame(7U, stale_frame));

    ExceptionFrame untouched{};
    untouched.elr_el1 = 0xDEADU;
    untouched.sp_el0 = 0xBEEFU;
    untouched.x[0] = 0xCAFEU;
    const auto before = untouched;
    auto rejected = stale_preemption.start(
        stale_scheduler, stale_translations, stale_epochs, 2'000'000U, untouched);
    require(!rejected);
    require(rejected.error().code == preemption_errors::translation_unavailable);
    require(untouched.elr_el1 == before.elr_el1);
    require(untouched.sp_el0 == before.sp_el0);
    require(untouched.x[0] == before.x[0]);

    return 0;
}
