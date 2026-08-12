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

    if (target->state == ThreadState::receive_blocked && target->partner == invalid_thread) {
        // Preserve who completed the blocked receive until the receive syscall
        // resumes and collects its return value. Previously this identity was
        // discarded here, which made a real server unable to mint reply authority
        // when the receiver arrived before the sender.
        target->state = ThreadState::ready;
        target->partner = from;
        sender->state = ThreadState::reply_blocked;
        sender->partner = to;
        return {};
    }

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

    // A sender may have completed this receive while the receiver was blocked.
    // The sender identity is a one-shot delivery result, not a persistent
    // relationship: consume and clear it before looking for another sender.
    if (receiver->partner != invalid_thread) {
        const ThreadId delivered = receiver->partner;
        receiver->partner = invalid_thread;
        return os::core::Result<ThreadId>{delivered};
    }

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

    if (client->state != ThreadState::reply_blocked || client->partner != self) {
        return rendezvous_error(rendezvous_errors::not_awaiting_reply);
    }

    client->state = ThreadState::ready;
    client->partner = invalid_thread;
    client->wake = WakeReason::replied;
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
