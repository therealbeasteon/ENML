#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/kernel/interrupt.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "interrupt: %s\n", what);
    }
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::kernel &&
        result.error().code == code;
}

bool in_state(
    const os::kernel::InterruptTable& table,
    os::kernel::InterruptSource source,
    os::kernel::InterruptState expected) {
    auto state = table.state_of(source);
    return state && state.value() == expected;
}

bool masked(const os::kernel::InterruptTable& table, os::kernel::InterruptSource source) {
    auto is_masked = table.is_masked(source);
    return is_masked && is_masked.value();
}

constexpr os::kernel::ThreadId driver = 7U;
constexpr os::kernel::ThreadId other_driver = 8U;
constexpr os::kernel::InterruptSource line = 3U;
constexpr os::kernel::InterruptSource other_line = 4U;

} // namespace

int main() {
    // The whole cycle. A source starts owned and unmasked; an interrupt masks it
    // and wakes its driver exactly once; servicing it and reporting the device
    // quiet leaves it owned and unmasked again.
    {
        os::kernel::InterruptTable table;
        if (!check(static_cast<bool>(table.attach(driver, line)), "attach refused")) return 1;
        if (!check(table.attached_source_count() == 1U, "wrong attached count")) return 1;
        if (!check(in_state(table, line, os::kernel::InterruptState::attached),
                   "a freshly attached source is not idle")) return 1;
        if (!check(!masked(table, line), "an idle source is masked")) return 1;

        auto first = table.dispatch(line);
        if (!check(static_cast<bool>(first), "dispatch refused")) return 1;
        if (!check(first.value().owner == driver, "dispatch named the wrong owner")) return 1;
        if (!check(first.value().wake, "the first assertion did not wake the driver")) return 1;
        if (!check(!first.value().coalesced, "the first assertion was reported as coalesced")) {
            return 1;
        }
        if (!check(in_state(table, line, os::kernel::InterruptState::pending),
                   "the source is not pending")) return 1;
        if (!check(masked(table, line), "a pending source is not masked")) return 1;

        auto collected = table.begin_service(driver, line);
        if (!check(static_cast<bool>(collected), "begin_service refused")) return 1;
        if (!check(collected.value().assertions == 1U, "wrong assertion count")) return 1;
        if (!check(!collected.value().saturated, "a single assertion reported as saturated")) {
            return 1;
        }
        if (!check(in_state(table, line, os::kernel::InterruptState::in_service),
                   "the source is not in service")) return 1;
        if (!check(masked(table, line), "a source in service is not masked")) return 1;

        auto again = table.end_service(driver, line);
        if (!check(static_cast<bool>(again), "end_service refused")) return 1;
        if (!check(!again.value(), "end_service asked for another round with no new work")) {
            return 1;
        }
        if (!check(in_state(table, line, os::kernel::InterruptState::attached),
                   "the source did not return to idle")) return 1;
        if (!check(!masked(table, line), "the source was not unmasked")) return 1;
    }

    // The livelock bound, which is the reason for masking on dispatch. A device
    // that asserts continuously gets exactly one wakeup out of the kernel, and
    // every further assertion is folded into a count. Interrupt load is bounded
    // by the driver's progress, not by the device's enthusiasm.
    {
        os::kernel::InterruptTable table;
        if (!check(static_cast<bool>(table.attach(driver, line)), "attach refused")) return 1;

        std::size_t wakes = 0U;
        constexpr std::size_t storm = 1000U;
        for (std::size_t i = 0U; i < storm; ++i) {
            auto taken = table.dispatch(line);
            if (!check(static_cast<bool>(taken), "dispatch refused mid-storm")) return 1;
            if (taken.value().wake) ++wakes;
            if (i > 0U && !check(taken.value().coalesced,
                                 "a repeat assertion was not coalesced")) return 1;
        }
        if (!check(wakes == 1U, "a storm produced more than one wakeup")) return 1;
        if (!check(masked(table, line), "the source was left unmasked during a storm")) return 1;

        auto collected = table.begin_service(driver, line);
        if (!check(static_cast<bool>(collected) && collected.value().assertions == storm,
                   "the coalesced count did not match the storm")) return 1;
    }

    // An assertion that arrives while the driver is working is not lost. The
    // driver is sent round again rather than unmasking into a line that is still
    // raised, or - on an edge-triggered source - dropping the event entirely.
    {
        os::kernel::InterruptTable table;
        (void)table.attach(driver, line);
        (void)table.dispatch(line);
        auto collected = table.begin_service(driver, line);
        if (!check(static_cast<bool>(collected) && collected.value().assertions == 1U,
                   "begin_service reported the wrong count")) return 1;

        // Fires again mid-service. Nobody is woken: the driver is already awake.
        auto during = table.dispatch(line);
        if (!check(static_cast<bool>(during), "dispatch during service refused")) return 1;
        if (!check(!during.value().wake, "an assertion during service woke an awake driver")) {
            return 1;
        }
        if (!check(during.value().coalesced, "an assertion during service was not folded")) {
            return 1;
        }

        auto again = table.end_service(driver, line);
        if (!check(static_cast<bool>(again) && again.value(),
                   "work that arrived during service was dropped")) return 1;
        if (!check(in_state(table, line, os::kernel::InterruptState::pending),
                   "the source did not go back to pending")) return 1;
        if (!check(masked(table, line), "the source was unmasked with work outstanding")) return 1;

        auto second = table.begin_service(driver, line);
        if (!check(static_cast<bool>(second) && second.value().assertions == 1U,
                   "the second round reported the wrong count")) return 1;
        auto settled = table.end_service(driver, line);
        if (!check(static_cast<bool>(settled) && !settled.value(),
                   "the source never settled")) return 1;
        if (!check(!masked(table, line), "the source was never unmasked")) return 1;
    }

    // One owner per source. Sharing is refused rather than supported, because a
    // shared line hands every driver on it a denial of service against the rest.
    {
        os::kernel::InterruptTable table;
        if (!check(static_cast<bool>(table.attach(driver, line)), "attach refused")) return 1;
        if (!check(refused(table.attach(other_driver, line),
                           os::kernel::interrupt_errors::source_taken),
                   "two drivers attached to one source")) return 1;
        // A different source is fine.
        if (!check(static_cast<bool>(table.attach(other_driver, other_line)),
                   "a second source was refused")) return 1;

        auto owner = table.owner_of(line);
        if (!check(static_cast<bool>(owner) && owner.value() == driver, "wrong owner")) return 1;
    }

    // Every operation is ownership-checked. A thread that does not own a source
    // cannot service it, complete it, or take it away.
    {
        os::kernel::InterruptTable table;
        (void)table.attach(driver, line);
        (void)table.dispatch(line);

        if (!check(refused(table.begin_service(other_driver, line),
                           os::kernel::interrupt_errors::not_owner),
                   "a stranger serviced someone else's interrupt")) return 1;
        if (!check(refused(table.detach(other_driver, line),
                           os::kernel::interrupt_errors::not_owner),
                   "a stranger detached someone else's source")) return 1;

        auto collected = table.begin_service(driver, line);
        if (!check(static_cast<bool>(collected), "the owner could not service it")) return 1;
        if (!check(refused(table.end_service(other_driver, line),
                           os::kernel::interrupt_errors::not_owner),
                   "a stranger completed someone else's interrupt")) return 1;
    }

    // The state machine refuses transitions that are not available, rather than
    // assuming the caller got the order right.
    {
        os::kernel::InterruptTable table;
        (void)table.attach(driver, line);

        if (!check(refused(table.begin_service(driver, line),
                           os::kernel::interrupt_errors::not_pending),
                   "serviced an interrupt that never arrived")) return 1;
        if (!check(refused(table.end_service(driver, line),
                           os::kernel::interrupt_errors::not_in_service),
                   "completed an interrupt that was never started")) return 1;

        (void)table.dispatch(line);
        if (!check(refused(table.end_service(driver, line),
                           os::kernel::interrupt_errors::not_in_service),
                   "completed a pending interrupt without servicing it")) return 1;
    }

    // A line asserting with nobody behind it is a hardware or configuration
    // fault, not a caller error. It succeeds, wakes nobody, and is counted -
    // because the count is the only evidence anyone will get.
    {
        os::kernel::InterruptTable table;
        auto stray = table.dispatch(line);
        if (!check(static_cast<bool>(stray), "a spurious interrupt was reported as an error")) {
            return 1;
        }
        if (!check(stray.value().owner == os::kernel::invalid_thread,
                   "a spurious interrupt named an owner")) return 1;
        if (!check(!stray.value().wake, "a spurious interrupt woke somebody")) return 1;
        if (!check(table.spurious_count() == 1U, "the spurious interrupt was not counted")) {
            return 1;
        }

        (void)table.dispatch(other_line);
        if (!check(table.spurious_count() == 2U, "spurious interrupts are not accumulating")) {
            return 1;
        }

        // A source number of zero is a defect in the machine layer rather than a
        // line that misbehaved, so it is refused rather than counted.
        if (!check(refused(table.dispatch(os::kernel::invalid_interrupt_source),
                           os::kernel::interrupt_errors::invalid_source),
                   "dispatched an invalid source")) return 1;
        if (!check(table.spurious_count() == 2U,
                   "an invalid source was counted as spurious")) return 1;
    }

    // Authority that names nobody, or nothing, is refused rather than stored.
    {
        os::kernel::InterruptTable table;
        if (!check(refused(table.attach(os::kernel::invalid_thread, line),
                           os::kernel::interrupt_errors::invalid_driver),
                   "attached a source to nobody")) return 1;
        if (!check(refused(table.attach(driver, os::kernel::invalid_interrupt_source),
                           os::kernel::interrupt_errors::invalid_source),
                   "attached to an invalid source")) return 1;
        if (!check(refused(table.detach(driver, line),
                           os::kernel::interrupt_errors::not_attached),
                   "detached a source nobody owns")) return 1;
        if (!check(table.attached_source_count() == 0U, "refused attaches left state")) return 1;
    }

    // A dead driver's sources are released and left masked. A masked orphan is a
    // dead device; an unmasked orphan is a livelock with nobody positioned to
    // stop it.
    {
        os::kernel::InterruptTable table;
        (void)table.attach(driver, line);
        (void)table.attach(driver, other_line);
        (void)table.attach(other_driver, 5U);
        // One of them mid-service, because a driver is most likely to die there.
        (void)table.dispatch(line);
        (void)table.begin_service(driver, line);

        const std::size_t released = table.detach_all_owned_by(driver);
        if (!check(released == 2U, "the dead driver's sources were not all released")) return 1;
        if (!check(table.attached_source_count() == 1U, "wrong attached count after death")) {
            return 1;
        }
        if (!check(refused(table.state_of(line), os::kernel::interrupt_errors::not_attached),
                   "a source in service survived its driver")) return 1;
        if (!check(refused(table.state_of(other_line),
                           os::kernel::interrupt_errors::not_attached),
                   "an idle source survived its driver")) return 1;
        if (!check(in_state(table, 5U, os::kernel::InterruptState::attached),
                   "another driver's source was released too")) return 1;

        // Releasing for a driver that owns nothing is not an error, it is simply
        // no work - the common case when any thread exits.
        if (!check(table.detach_all_owned_by(999U) == 0U,
                   "releasing nothing reported work")) return 1;

        // The source is free to be attached again, which is what makes a
        // restarted driver a recovery rather than a reboot.
        if (!check(static_cast<bool>(table.attach(driver, line)),
                   "a released source could not be re-attached")) return 1;
    }

    // Detaching mid-service is a driver giving up, which is its right. What it
    // must not be able to do is leave the line able to assert with nobody there.
    {
        os::kernel::InterruptTable table;
        (void)table.attach(driver, line);
        (void)table.dispatch(line);
        (void)table.begin_service(driver, line);
        if (!check(static_cast<bool>(table.detach(driver, line)),
                   "detaching mid-service was refused")) return 1;
        if (!check(refused(table.state_of(line), os::kernel::interrupt_errors::not_attached),
                   "the source survived being detached")) return 1;
    }

    // The table has a stated ceiling and refuses rather than overruns.
    {
        os::kernel::InterruptTable table;
        for (std::size_t i = 0U; i < os::kernel::max_interrupt_sources; ++i) {
            const auto source = static_cast<os::kernel::InterruptSource>(i + 1U);
            if (!check(static_cast<bool>(table.attach(driver, source)),
                       "attach refused below the ceiling")) return 1;
        }
        const auto beyond =
            static_cast<os::kernel::InterruptSource>(os::kernel::max_interrupt_sources + 1U);
        if (!check(refused(table.attach(driver, beyond),
                           os::kernel::interrupt_errors::source_limit),
                   "the table grew past its ceiling")) return 1;
        if (!check(table.detach_all_owned_by(driver) == os::kernel::max_interrupt_sources,
                   "releasing a full table did not empty it")) return 1;
        if (!check(table.attached_source_count() == 0U, "the table did not empty")) return 1;
        if (!check(static_cast<bool>(table.attach(driver, beyond)),
                   "slots were not reusable after release")) return 1;
    }

    return 0;
}
