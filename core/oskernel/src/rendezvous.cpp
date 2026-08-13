#include <os/kernel/rendezvous.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error rendezvous_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

} // namespace

Rendezvous::Slot* Rendezvous::find(ThreadId thread) noexcept {
    if (thread == invalid_thread) return nullptr;
    for (auto& slot : slots_) {
        if (slot.occupied && slot.thread == thread) return &slot;
    }
    return nullptr;
}

const Rendezvous::Slot* Rendezvous::find(ThreadId thread) const noexcept {
    if (thread == invalid_thread) return nullptr;
    for (const auto& slot : slots_) {
        if (slot.occupied && slot.thread == thread) return &slot;
    }
    return nullptr;
}

std::size_t Rendezvous::live_thread_count() const noexcept {
    return occupied_;
}

Priority Rendezvous::inherited_priority(ThreadId thread) const noexcept {
    const Slot* slot = find(thread);
    if (slot == nullptr) return default_priority;

    Priority highest = slot->base;
    for (const auto& other : slots_) {
        if (!other.occupied || other.thread == thread) continue;
        // Only threads actually waiting on this one donate. A thread that has
        // merely spoken to it in the past donates nothing, or a single past
        // caller would pin a server high forever.
        const bool waiting_on_it =
            (other.state == ThreadState::send_blocked ||
             other.state == ThreadState::reply_blocked) &&
            other.partner == thread;
        if (!waiting_on_it) continue;
        if (other.base > highest) highest = other.base;
    }
    return highest;
}

os::core::Result<Priority> Rendezvous::effective_priority_of(ThreadId thread) const noexcept {
    if (find(thread) == nullptr) {
        return os::core::Result<Priority>{rendezvous_error(rendezvous_errors::unknown_thread)};
    }
    return os::core::Result<Priority>{inherited_priority(thread)};
}

os::core::Result<Priority> Rendezvous::base_priority_of(ThreadId thread) const noexcept {
    const Slot* slot = find(thread);
    if (slot == nullptr) {
        return os::core::Result<Priority>{rendezvous_error(rendezvous_errors::unknown_thread)};
    }
    return os::core::Result<Priority>{slot->base};
}

os::core::Result<void> Rendezvous::create_thread(ThreadId thread, Priority priority) noexcept {
    if (thread == invalid_thread) {
        return rendezvous_error(rendezvous_errors::invalid_thread_id);
    }
    if (find(thread) != nullptr) {
        return rendezvous_error(rendezvous_errors::thread_exists);
    }
    for (auto& slot : slots_) {
        if (slot.occupied) continue;
        slot = Slot{thread, ThreadState::ready, invalid_thread, WakeReason::none, priority, true};
        ++occupied_;
        return {};
    }
    return rendezvous_error(rendezvous_errors::thread_limit);
}

os::core::Result<std::size_t> Rendezvous::exit_thread(ThreadId thread) noexcept {
    Slot* slot = find(thread);
    if (slot == nullptr) {
        return os::core::Result<std::size_t>{
            rendezvous_error(rendezvous_errors::unknown_thread)};
    }

    // Release everyone waiting on this thread before removing it.
    //
    // A thread that dies while others are blocked on it must not leave them
    // blocked forever: a crashing server would otherwise be a permanent denial
    // of service against every client that trusted it, and no amount of
    // supervision above the kernel can recover a thread the kernel has parked.
    //
    // They are woken with peer_exited rather than replied, because a caller
    // that cannot tell a death from an answer will treat the death as an
    // answer.
    std::size_t released = 0U;
    for (auto& other : slots_) {
        if (!other.occupied || other.thread == thread) continue;
        const bool waiting_on_it =
            (other.state == ThreadState::send_blocked ||
             other.state == ThreadState::reply_blocked ||
             other.state == ThreadState::receive_blocked) &&
            other.partner == thread;
        if (!waiting_on_it) continue;
        other.state = ThreadState::ready;
        other.partner = invalid_thread;
        other.wake = WakeReason::peer_exited;
        ++released;
    }

    // The slot is retained as exited rather than freed for reuse, so a stale
    // reference resolves to a definite answer instead of to whichever thread
    // happens to be created next.
    slot->state = ThreadState::exited;
    slot->partner = invalid_thread;
    slot->wake = WakeReason::none;
    slot->occupied = false;
    --occupied_;
    return os::core::Result<std::size_t>{released};
}

os::core::Result<void> Rendezvous::send(ThreadId from, ThreadId to) noexcept {
    if (from == invalid_thread || to == invalid_thread) {
        return rendezvous_error(rendezvous_errors::invalid_thread_id);
    }
    // A thread cannot rendezvous with itself: both halves would have to be
    // blocked simultaneously, and permitting it is a self-deadlock reachable
    // from unprivileged code.
    if (from == to) {
        return rendezvous_error(rendezvous_errors::self_addressed);
    }

    Slot* sender = find(from);
    Slot* target = find(to);
    if (sender == nullptr || target == nullptr) {
        return rendezvous_error(rendezvous_errors::unknown_thread);
    }
    if (sender->state != ThreadState::ready) {
        return rendezvous_error(rendezvous_errors::not_runnable);
    }

    sender->wake = WakeReason::none;

    // The receiver was already waiting, so the rendezvous completes now: the
    // message passes and the sender moves straight to awaiting the reply.
    if (target->state == ThreadState::receive_blocked && target->partner == invalid_thread) {
        target->state = ThreadState::ready;
        target->partner = invalid_thread;
        sender->state = ThreadState::reply_blocked;
        sender->partner = to;
        return {};
    }

    // Otherwise the sender waits to be collected. Nothing is stored in the
    // kernel on its behalf; the sender itself is the queue entry, which is why
    // there is no depth to exhaust.
    sender->state = ThreadState::send_blocked;
    sender->partner = to;
    return {};
}

os::core::Result<ThreadId> Rendezvous::receive(ThreadId self) noexcept {
    Slot* receiver = find(self);
    if (self == invalid_thread) {
        return os::core::Result<ThreadId>{
            rendezvous_error(rendezvous_errors::invalid_thread_id)};
    }
    if (receiver == nullptr) {
        return os::core::Result<ThreadId>{rendezvous_error(rendezvous_errors::unknown_thread)};
    }
    if (receiver->state != ThreadState::ready) {
        return os::core::Result<ThreadId>{rendezvous_error(rendezvous_errors::not_runnable)};
    }

    receiver->wake = WakeReason::none;

    // Take the longest-waiting sender. Scanned in slot order, which is stable
    // and therefore fair enough to reason about; a priority-ordered variant is
    // a scheduling decision and belongs with the scheduler, not here.
    for (auto& slot : slots_) {
        if (!slot.occupied) continue;
        if (slot.state != ThreadState::send_blocked || slot.partner != self) continue;
        slot.state = ThreadState::reply_blocked;
        return os::core::Result<ThreadId>{slot.thread};
    }

    receiver->state = ThreadState::receive_blocked;
    receiver->partner = invalid_thread;
    return os::core::Result<ThreadId>{invalid_thread};
}

os::core::Result<void> Rendezvous::reply(ThreadId self, ThreadId caller) noexcept {
    if (self == invalid_thread || caller == invalid_thread) {
        return rendezvous_error(rendezvous_errors::invalid_thread_id);
    }
    if (self == caller) {
        return rendezvous_error(rendezvous_errors::self_addressed);
    }

    Slot* server = find(self);
    Slot* client = find(caller);
    if (server == nullptr || client == nullptr) {
        return rendezvous_error(rendezvous_errors::unknown_thread);
    }

    // The authority check, and the reason reply is not simply "unblock the
    // thread named in this register". A thread may only answer a caller that is
    // currently awaiting *its* reply. Without both halves of this test, any
    // thread could unblock any conversation it was never part of.
    if (client->state != ThreadState::reply_blocked || client->partner != self) {
        return rendezvous_error(rendezvous_errors::not_awaiting_reply);
    }

    client->state = ThreadState::ready;
    client->partner = invalid_thread;
    client->wake = WakeReason::replied;
    // Reply never blocks the server. A server that could be stalled by a client
    // declining to collect its answer would be a denial of service with no
    // defence, which is why the ABI marks this call non-blocking.
    return {};
}

os::core::Result<ThreadState> Rendezvous::state_of(ThreadId thread) const noexcept {
    const Slot* slot = find(thread);
    if (slot == nullptr) {
        return os::core::Result<ThreadState>{rendezvous_error(rendezvous_errors::unknown_thread)};
    }
    return os::core::Result<ThreadState>{slot->state};
}

os::core::Result<WakeReason> Rendezvous::wake_reason_of(ThreadId thread) const noexcept {
    const Slot* slot = find(thread);
    if (slot == nullptr) {
        return os::core::Result<WakeReason>{rendezvous_error(rendezvous_errors::unknown_thread)};
    }
    return os::core::Result<WakeReason>{slot->wake};
}

os::core::Result<ThreadId> Rendezvous::partner_of(ThreadId thread) const noexcept {
    const Slot* slot = find(thread);
    if (slot == nullptr) {
        return os::core::Result<ThreadId>{rendezvous_error(rendezvous_errors::unknown_thread)};
    }
    return os::core::Result<ThreadId>{slot->partner};
}

} // namespace os::kernel
