#include <cstdlib>

#include <os/kernel/ipc_continuation.hpp>

namespace {
// Templated so a Result can be passed directly. Result's operator bool is
// explicit, which satisfies the contextual conversion in `!value` but not
// an implicit conversion to a bool parameter.
template <typename T>
void require(const T& value) {
    if (!static_cast<bool>(value)) std::abort();
}
}

int main() {
    os::kernel::AddressSpaceEpochAuthority epochs{};
    auto epoch = epochs.acquire();
    require(static_cast<bool>(epoch));

    os::kernel::IpcContinuationTable continuations{};
    require(static_cast<bool>(continuations.arm(1U, epoch.value(), 0x4000U, epochs)));
    require(static_cast<bool>(continuations.arm_receive(2U, epoch.value(), 17U, 0x5000U, epochs)));
    require(continuations.send_armed(1U));
    require(continuations.receive_armed(2U));
    require(continuations.count() == 2U);

    auto send = continuations.take(1U, epoch.value(), epochs);
    auto receive = continuations.take_receive(2U, epoch.value(), epochs);
    require(send && receive);
    require(send.value().exchange_address == 0x4000U);
    require(receive.value().endpoint_capability == 17U);
    require(receive.value().exchange_address == 0x5000U);
    require(continuations.count() == 0U);

    // Both continuation classes are generation-bound. Retirement consumes the
    // old authority even when a later process incarnation reuses the same VA.
    require(static_cast<bool>(continuations.arm_receive(2U, epoch.value(), 17U, 0x5000U, epochs)));
    auto retiring = epochs.begin_retire(epoch.value());
    require(static_cast<bool>(retiring));
    require(!continuations.take_receive(2U, epoch.value(), epochs));
    require(!continuations.receive_armed(2U));
    require(continuations.count() == 0U);
    require(static_cast<bool>(epochs.complete_retire(retiring.value())));

    auto replacement = epochs.acquire();
    require(static_cast<bool>(replacement));
    require(static_cast<bool>(continuations.arm_receive(2U, replacement.value(), 19U, 0x5000U, epochs)));
    auto fresh = continuations.take_receive(2U, replacement.value(), epochs);
    require(static_cast<bool>(fresh));
    require(fresh.value().endpoint_capability == 19U);

    // Thread teardown is sufficient cleanup; neither blocked syscall class
    // relies on userspace running a destructor/cleanup path.
    require(static_cast<bool>(continuations.arm(3U, replacement.value(), 0x6000U, epochs)));
    require(static_cast<bool>(continuations.arm_receive(3U, replacement.value(), 21U, 0x7000U, epochs)));
    require(continuations.count() == 2U);
    continuations.release_thread(3U);
    require(continuations.count() == 0U);

    // ------------------------------------------------------------------
    // Bounded receive: deadlines are absolute, one timer serves the table.
    // ------------------------------------------------------------------
    {
        os::kernel::AddressSpaceEpochAuthority deadline_epochs{};
        auto e1 = deadline_epochs.acquire();
        require(static_cast<bool>(e1));
        os::kernel::IpcContinuationTable table{};

        // Unbounded by default: an unbounded waiter never arms a timer and
        // never expires, however far the clock runs.
        require(static_cast<bool>(table.arm_receive(
            7U, e1.value(), 5U, 0x1000U, deadline_epochs)));
        require(table.earliest_receive_deadline() == 0ULL);
        require(!table.take_expired_receive(~0ULL - 1ULL));
        require(static_cast<bool>(table.cancel_receive(7U)));

        // Three bounded waiters; the timer is armed against the soonest, not
        // one timer per waiter.
        require(static_cast<bool>(table.arm_receive(
            7U, e1.value(), 5U, 0x1000U, deadline_epochs, 900U)));
        require(static_cast<bool>(table.arm_receive(
            8U, e1.value(), 5U, 0x2000U, deadline_epochs, 300U)));
        require(static_cast<bool>(table.arm_receive(
            9U, e1.value(), 5U, 0x3000U, deadline_epochs, 600U)));
        require(table.earliest_receive_deadline() == 300U);

        // Nothing expires early.
        require(!table.take_expired_receive(299U));

        // At the deadline exactly, not one tick after.
        auto first = table.take_expired_receive(300U);
        require(static_cast<bool>(first));
        require(first.value().server == 8U);
        require(first.value().deadline_nanoseconds == 300U);
        require(table.earliest_receive_deadline() == 600U);

        // A far-future now expires the rest, lowest ThreadId first, so two
        // equally-expired waiters cannot read their order off table layout.
        auto second = table.take_expired_receive(10'000U);
        require(static_cast<bool>(second));
        require(second.value().server == 7U);
        auto third = table.take_expired_receive(10'000U);
        require(static_cast<bool>(third));
        require(third.value().server == 9U);
        require(table.earliest_receive_deadline() == 0ULL);
        require(!table.take_expired_receive(10'000U));
    }

    return 0;
}
