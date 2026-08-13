#include <os/kernel/interrupt.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error interrupt_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

// Counts stop rather than wrap.
//
// A wrapped count is worse than no count: it reads as a small number, and a
// driver told "one assertion" after four billion will believe it and skip the
// rescan. Saturating fails in the direction that overstates how much happened,
// which is the direction that costs work rather than correctness.
inline constexpr std::uint32_t assertion_ceiling = 0xFFFFFFFFU;

void record_assertion(std::uint32_t& count, bool& saturated) noexcept {
    if (count == assertion_ceiling) {
        saturated = true;
        return;
    }
    ++count;
}

} // namespace

InterruptTable::Slot* InterruptTable::find(InterruptSource source) noexcept {
    if (source == invalid_interrupt_source) return nullptr;
    for (auto& slot : slots_) {
        if (slot.occupied && slot.source == source) return &slot;
    }
    return nullptr;
}

const InterruptTable::Slot* InterruptTable::find(InterruptSource source) const noexcept {
    if (source == invalid_interrupt_source) return nullptr;
    for (const auto& slot : slots_) {
        if (slot.occupied && slot.source == source) return &slot;
    }
    return nullptr;
}

std::size_t InterruptTable::attached_source_count() const noexcept {
    return occupied_;
}

std::uint32_t InterruptTable::spurious_count() const noexcept {
    return spurious_;
}

os::core::Result<void> InterruptTable::attach(ThreadId driver, InterruptSource source) noexcept {
    if (driver == invalid_thread) {
        return interrupt_error(interrupt_errors::invalid_driver);
    }
    if (source == invalid_interrupt_source) {
        return interrupt_error(interrupt_errors::invalid_source);
    }
    // One owner, always. See the header: sharing would turn mask-until-complete
    // into mask-until-everyone-completes and hand every driver on the line a
    // denial of service against the others.
    if (find(source) != nullptr) {
        return interrupt_error(interrupt_errors::source_taken);
    }

    for (auto& slot : slots_) {
        if (slot.occupied) continue;
        slot = Slot{source, driver, InterruptState::attached, 0U, false, true};
        ++occupied_;
        return {};
    }
    return interrupt_error(interrupt_errors::source_limit);
}

os::core::Result<void> InterruptTable::detach(ThreadId driver, InterruptSource source) noexcept {
    if (driver == invalid_thread) {
        return interrupt_error(interrupt_errors::invalid_driver);
    }
    if (source == invalid_interrupt_source) {
        return interrupt_error(interrupt_errors::invalid_source);
    }

    Slot* slot = find(source);
    if (slot == nullptr) {
        return interrupt_error(interrupt_errors::not_attached);
    }
    if (slot->owner != driver) {
        return interrupt_error(interrupt_errors::not_owner);
    }

    // Permitted mid-service. A driver abandoning a source it was working on is
    // giving up, which is its right; what it must not be able to do is leave the
    // line able to assert with nobody behind it, and the slot going away is what
    // guarantees the source stays masked.
    *slot = Slot{};
    --occupied_;
    return {};
}

os::core::Result<Dispatch> InterruptTable::dispatch(InterruptSource source) noexcept {
    // Not something hardware can arrange: source numbers are the kernel's own
    // namespace, so a zero here is a defect in the machine layer rather than a
    // line that misbehaved, and it is refused rather than counted.
    if (source == invalid_interrupt_source) {
        return os::core::Result<Dispatch>{interrupt_error(interrupt_errors::invalid_source)};
    }

    Slot* slot = find(source);
    if (slot == nullptr) {
        // A line asserting with no driver behind it is a hardware or
        // configuration fault, not a caller error - so it succeeds, waking
        // nobody, and is counted. The count is the only evidence anyone will
        // ever get that it happened.
        if (spurious_ != assertion_ceiling) ++spurious_;
        return os::core::Result<Dispatch>{Dispatch{invalid_thread, false, false}};
    }

    if (slot->state == InterruptState::attached) {
        slot->state = InterruptState::pending;
        slot->assertions = 1U;
        slot->saturated = false;
        return os::core::Result<Dispatch>{Dispatch{slot->owner, true, false}};
    }

    // Already outstanding, or the owner is working on the last one. Either way
    // this assertion is folded into the count rather than delivered separately,
    // and nobody is woken - the owner is already runnable, and counting a wakeup
    // that does not happen is how an idle-wakeup budget stops meaning anything.
    record_assertion(slot->assertions, slot->saturated);
    return os::core::Result<Dispatch>{Dispatch{slot->owner, false, true}};
}

os::core::Result<Service> InterruptTable::begin_service(
    ThreadId driver,
    InterruptSource source) noexcept {
    if (driver == invalid_thread) {
        return os::core::Result<Service>{interrupt_error(interrupt_errors::invalid_driver)};
    }
    if (source == invalid_interrupt_source) {
        return os::core::Result<Service>{interrupt_error(interrupt_errors::invalid_source)};
    }

    Slot* slot = find(source);
    if (slot == nullptr) {
        return os::core::Result<Service>{interrupt_error(interrupt_errors::not_attached)};
    }
    if (slot->owner != driver) {
        return os::core::Result<Service>{interrupt_error(interrupt_errors::not_owner)};
    }
    if (slot->state != InterruptState::pending) {
        return os::core::Result<Service>{interrupt_error(interrupt_errors::not_pending)};
    }

    const Service collected{slot->assertions, slot->saturated};

    // The counter restarts, so what accumulates from here is what arrives *while
    // the driver is working*. That is the number end_service consults to decide
    // whether the driver has to go round again, and mixing it with the batch
    // just handed over would make an already-serviced interrupt look like new
    // work forever.
    slot->state = InterruptState::in_service;
    slot->assertions = 0U;
    slot->saturated = false;
    return os::core::Result<Service>{collected};
}

os::core::Result<bool> InterruptTable::end_service(
    ThreadId driver,
    InterruptSource source) noexcept {
    if (driver == invalid_thread) {
        return os::core::Result<bool>{interrupt_error(interrupt_errors::invalid_driver)};
    }
    if (source == invalid_interrupt_source) {
        return os::core::Result<bool>{interrupt_error(interrupt_errors::invalid_source)};
    }

    Slot* slot = find(source);
    if (slot == nullptr) {
        return os::core::Result<bool>{interrupt_error(interrupt_errors::not_attached)};
    }
    if (slot->owner != driver) {
        return os::core::Result<bool>{interrupt_error(interrupt_errors::not_owner)};
    }
    if (slot->state != InterruptState::in_service) {
        return os::core::Result<bool>{interrupt_error(interrupt_errors::not_in_service)};
    }

    // The device asserted while its driver was working. Going back to pending
    // rather than unmasking is what keeps that assertion from being lost: an
    // edge-triggered source will not raise it again, and a level-triggered one
    // would re-raise the instant it was unmasked, which is the same work with an
    // interrupt entry added to it.
    if (slot->assertions > 0U) {
        slot->state = InterruptState::pending;
        return os::core::Result<bool>{true};
    }

    slot->state = InterruptState::attached;
    return os::core::Result<bool>{false};
}

std::size_t InterruptTable::detach_all_owned_by(ThreadId driver) noexcept {
    if (driver == invalid_thread) return 0U;

    std::size_t released = 0U;
    for (auto& slot : slots_) {
        if (!slot.occupied || slot.owner != driver) continue;
        slot = Slot{};
        ++released;
    }
    occupied_ -= released;
    return released;
}

os::core::Result<InterruptState> InterruptTable::state_of(InterruptSource source) const noexcept {
    const Slot* slot = find(source);
    if (slot == nullptr) {
        return os::core::Result<InterruptState>{
            interrupt_error(interrupt_errors::not_attached)};
    }
    return os::core::Result<InterruptState>{slot->state};
}

os::core::Result<ThreadId> InterruptTable::owner_of(InterruptSource source) const noexcept {
    const Slot* slot = find(source);
    if (slot == nullptr) {
        return os::core::Result<ThreadId>{interrupt_error(interrupt_errors::not_attached)};
    }
    return os::core::Result<ThreadId>{slot->owner};
}

os::core::Result<bool> InterruptTable::is_masked(InterruptSource source) const noexcept {
    const Slot* slot = find(source);
    if (slot == nullptr) {
        return os::core::Result<bool>{interrupt_error(interrupt_errors::not_attached)};
    }
    // Masked from dispatch until the driver says the device is quiet. Only the
    // idle state leaves the line able to assert.
    return os::core::Result<bool>{slot->state != InterruptState::attached};
}

} // namespace os::kernel
