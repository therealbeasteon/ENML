#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/fault_region.hpp>
#include <os/kernel/rendezvous.hpp>

namespace os::kernel {

namespace fault_delivery_errors {
inline constexpr std::uint32_t invalid_thread = 300U;
inline constexpr std::uint32_t already_armed = 301U;
inline constexpr std::uint32_t not_armed = 302U;
inline constexpr std::uint32_t not_delivered = 303U;
inline constexpr std::uint32_t exhausted = 304U;
} // namespace fault_delivery_errors

inline constexpr std::size_t max_fault_deliveries = 16U;

// One outstanding question and the thread waiting on its answer.
//
// Deliberately carries a region and not an address, because that is the only
// thing the kernel is willing to say - see docs/M7_11_FAULT_PRIVACY.md and
// FaultReport, which this is the in-flight form of. A field holding the
// faulting address would defeat the entire disclosure decision at the one
// point where the answer leaves the kernel, which is exactly where it would be
// least visible.
struct FaultDelivery final {
    FaultRegionId region {invalid_fault_region};
    ThreadId faulting {invalid_thread};
    bool write {false};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return region != invalid_fault_region && faulting != invalid_thread;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const FaultDelivery&, const FaultDelivery&) = default;
};

// Holds a fault question between the kernel asking it and the pager answering.
//
// The interrupt analogue is InterruptDeliveryTable, and the shape is copied
// from it on purpose: one slot per pager thread, arm() refuses rather than
// overwriting, and the woken thread collects without a syscall. What is
// different is the reason a slot exists at all. An interrupt delivery is
// finished the moment the driver reads it; a fault delivery is a *question*,
// and something has to remember who is blocked on the answer until it comes.
//
// Hence two states rather than one. `pending` is armed and not yet collected;
// `delivered` is the pager holding the question and owing an answer. take()
// moves between them and answer() ends it. A one-state table would lose the
// faulting thread the instant the pager was told, and the kernel would have
// backing arriving with nobody to give it to.
//
// A pager that never answers leaves the slot in `delivered` forever, and that
// is correct rather than a leak to fix: the thread is blocked, the region's
// one announcement is already spent (FaultRegionTable::resolve consumed it),
// and nothing can ask about that transition again. release() is how a dying
// pager's debt is collected - it hands back the thread that was waiting so the
// caller can terminate it, because a thread waiting on an answer that will
// never come is not something to leave runnable.
class FaultDeliveryTable final {
public:
    FaultDeliveryTable() noexcept = default;

    [[nodiscard]] bool armed(ThreadId pager) const noexcept;

    // Asks `pager` about `report`, on behalf of the thread that faulted.
    //
    // Takes a FaultReport rather than its parts so that the only way to
    // construct a question is from something FaultRegionTable::resolve
    // produced. A caller that could assemble a region id by hand could ask a
    // pager about a region that never faulted, which is a disclosure the
    // resolve() path is careful not to permit.
    [[nodiscard]] os::core::Result<void> arm(
        ThreadId pager,
        ThreadId faulting,
        FaultReport report) noexcept;

    // The pager's resume collects the question. Leaves the slot occupied and
    // owing an answer, because the faulting thread still has to be found when
    // one arrives.
    [[nodiscard]] os::core::Result<FaultDelivery> take(ThreadId pager) noexcept;

    // The answer arrived. Returns what was asked, so the caller knows which
    // region to mark backed and which thread to resume, and frees the slot.
    // Refuses a pager that was never asked, and one that has been asked but
    // has not yet collected - answering a question you have not been told is
    // not an answer.
    [[nodiscard]] os::core::Result<FaultDelivery> answer(ThreadId pager) noexcept;

    // Releases whatever a departing pager owed, handing back the delivery so
    // the caller can terminate the thread that was waiting on it. Returns an
    // invalid delivery when the pager owed nothing.
    [[nodiscard]] FaultDelivery release(ThreadId pager) noexcept;

    [[nodiscard]] std::size_t outstanding() const noexcept { return occupied_; }

private:
    enum class State : std::uint8_t {
        free = 0U,
        pending = 1U,
        delivered = 2U,
    };

    struct Slot final {
        ThreadId pager {invalid_thread};
        FaultDelivery delivery {};
        State state {State::free};
    };

    [[nodiscard]] Slot* find(ThreadId pager) noexcept;
    [[nodiscard]] const Slot* find(ThreadId pager) const noexcept;

    std::array<Slot, max_fault_deliveries> slots_ {};
    std::size_t occupied_ {0U};
};

} // namespace os::kernel
