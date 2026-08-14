#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/kernel/interrupt_delivery.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "interrupt_delivery: %s\n", what);
    }
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::kernel &&
        result.error().code == code;
}

constexpr os::kernel::ThreadId driver = 7U;
constexpr os::kernel::ThreadId other_driver = 8U;
constexpr os::kernel::InterruptSource line = 3U;
constexpr os::kernel::InterruptSource other_line = 4U;

} // namespace

int main() {
    // The whole cycle: arm, observe armed, take returns exactly what was armed,
    // and the slot is empty afterward.
    {
        os::kernel::InterruptDeliveryTable table;
        if (!check(!table.armed(driver), "armed before any delivery")) return 1;
        const os::kernel::Service service{5U, false};
        if (!check(static_cast<bool>(table.arm(driver, line, service)), "arm refused")) return 1;
        if (!check(table.armed(driver), "not armed after arm")) return 1;

        auto taken = table.take(driver);
        if (!check(static_cast<bool>(taken), "take refused")) return 1;
        if (!check(taken.value().assertions == 5U, "wrong assertion count")) return 1;
        if (!check(!taken.value().saturated, "wrong saturated flag")) return 1;
        if (!check(!table.armed(driver), "still armed after take")) return 1;
    }

    // Taking with nothing armed is refused, not a default-constructed Service.
    {
        os::kernel::InterruptDeliveryTable table;
        if (!check(refused(table.take(driver), os::kernel::interrupt_delivery_errors::not_armed),
                   "unarmed take accepted")) return 1;
    }

    // A second arm for the same driver, before the first is taken, is refused -
    // and refusing it must not disturb the first delivery still waiting.
    {
        os::kernel::InterruptDeliveryTable table;
        if (!check(static_cast<bool>(table.arm(driver, line, os::kernel::Service{1U, false})),
                   "first arm refused")) return 1;
        if (!check(refused(
                       table.arm(driver, other_line, os::kernel::Service{2U, false}),
                       os::kernel::interrupt_delivery_errors::already_armed),
                   "double arm accepted")) return 1;

        auto taken = table.take(driver);
        if (!check(static_cast<bool>(taken), "take after refused double-arm failed")) return 1;
        if (!check(taken.value().assertions == 1U,
                   "double-arm attempt overwrote the first delivery")) return 1;
    }

    // Two drivers each get their own slot; one's arm/take does not touch the
    // other's.
    {
        os::kernel::InterruptDeliveryTable table;
        if (!check(static_cast<bool>(table.arm(driver, line, os::kernel::Service{3U, false})),
                   "driver arm refused")) return 1;
        if (!check(static_cast<bool>(
                       table.arm(other_driver, other_line, os::kernel::Service{4U, true})),
                   "other_driver arm refused")) return 1;
        if (!check(table.armed(driver) && table.armed(other_driver),
                   "both drivers should be armed independently")) return 1;

        auto other_taken = table.take(other_driver);
        if (!check(static_cast<bool>(other_taken), "other_driver take refused")) return 1;
        if (!check(other_taken.value().assertions == 4U && other_taken.value().saturated,
                   "wrong delivery taken for other_driver")) return 1;
        if (!check(table.armed(driver), "driver's delivery disturbed by other_driver's take")) return 1;
    }

    // invalid_thread and invalid_interrupt_source are refused outright, the
    // same fail-closed posture InterruptTable itself applies.
    {
        os::kernel::InterruptDeliveryTable table;
        if (!check(refused(
                       table.arm(
                           os::kernel::invalid_thread, line, os::kernel::Service{}),
                       os::kernel::interrupt_delivery_errors::invalid_thread),
                   "invalid thread accepted")) return 1;
        if (!check(refused(
                       table.arm(
                           driver, os::kernel::invalid_interrupt_source, os::kernel::Service{}),
                       os::kernel::interrupt_delivery_errors::invalid_thread),
                   "invalid source accepted")) return 1;
    }

    // release() clears an armed slot for a driver that no longer exists and
    // reports whether there was anything to clear.
    {
        os::kernel::InterruptDeliveryTable table;
        if (!check(!table.release(driver), "release reported success with nothing armed")) return 1;
        if (!check(static_cast<bool>(table.arm(driver, line, os::kernel::Service{1U, false})),
                   "arm before release refused")) return 1;
        if (!check(table.release(driver), "release reported failure with a delivery armed")) return 1;
        if (!check(!table.armed(driver), "still armed after release")) return 1;
    }

    return 0;
}
