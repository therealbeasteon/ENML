#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/interrupt.hpp>
#include <os/kernel/rendezvous.hpp>

namespace os::kernel {

namespace interrupt_delivery_errors {
inline constexpr std::uint32_t invalid_thread = 280U;
inline constexpr std::uint32_t already_armed = 281U;
inline constexpr std::uint32_t not_armed = 282U;
inline constexpr std::uint32_t exhausted = 283U;
} // namespace interrupt_delivery_errors

// Bridges InterruptTable::begin_service's result to the driver thread that
// resumes to collect it.
//
// docs/M7_1_INTERRUPT.md says begin_service is deliberately not a syscall -
// "it is the transition the kernel performs when it makes the driver
// runnable, and the count rides back on the wakeup." That sentence describes
// this table. Kernel::dispatch_interrupt calls begin_service itself the
// instant a driver is woken, and this table holds what it returned until the
// driver's own resume-time continuation - the interrupt analogue of
// complete_ipc_current - writes it into registers and takes the slot.
// Without somewhere to put it between those two points, "rides back on the
// wakeup" would have nothing to ride in.
//
// One slot per thread, not per source. A driver owning more than one
// interrupt source is not exercised by anything built so far - M7.9's first
// proof owns exactly one - and arm() refuses rather than silently
// overwriting when a delivery is already outstanding, the same discipline
// IpcContinuationTable already applies to its own single-outstanding-op-per-
// thread slots. A second source dispatched while the first delivery is still
// uncollected is left pending in InterruptTable rather than delivered here;
// it is picked up the next time that source's own line asserts and the
// driver has caught up. Recorded rather than assumed, the same way M7.9's
// design doc records its other open questions.
class InterruptDeliveryTable final {
public:
    InterruptDeliveryTable() noexcept = default;

    [[nodiscard]] bool armed(ThreadId driver) const noexcept;

    [[nodiscard]] os::core::Result<void> arm(
        ThreadId driver, InterruptSource source, Service service) noexcept;

    // Removes and returns whatever is armed for driver. Does not check which
    // source it belongs to - the driver's own resume has no reason to expect
    // a particular one, unlike interrupt_complete, which is told a specific
    // capability by its caller and checks it against InterruptTable itself.
    [[nodiscard]] os::core::Result<Service> take(ThreadId driver) noexcept;

    // Releases whatever is armed for a thread that no longer exists, mirroring
    // InterruptTable::detach_all_owned_by. Returns whether anything was armed.
    bool release(ThreadId driver) noexcept;

private:
    struct Slot final {
        ThreadId driver {invalid_thread};
        InterruptSource source {invalid_interrupt_source};
        Service service {};
        bool occupied {false};
    };

    [[nodiscard]] Slot* find(ThreadId driver) noexcept;
    [[nodiscard]] const Slot* find(ThreadId driver) const noexcept;

    std::array<Slot, max_threads> slots_ {};
};

} // namespace os::kernel
