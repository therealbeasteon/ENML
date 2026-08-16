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
    for (auto& slot : slots_) if (slot.occupied && slot.thread == thread) return &slot;
    return nullptr;
}

const Rendezvous::Slot* Rendezvous::find(ThreadId thread) const noexcept {
    if (thread == invalid_thread) return nullptr;
    for (const auto& slot : slots_) if (slot.occupied && slot.thread == thread) return &slot;
    return nullptr;
}

std::size_t Rendezvous::live_thread_count() const noexcept { return occupied_; }

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
        if (waiting_on_it && other.base > highest) highest = other.base;
    }
    return highest;
}

os::core::Result<Priority> Rendezvous::effective_priority_of(ThreadId thread) const noexcept {
    if (find(thread) == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    return inherited_priority(thread);
}

os::core::Result<Priority> Rendezvous::base_priority_of(ThreadId thread) const noexcept {
    const Slot* slot = find(thread);
    if (slot == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    return slot->base;
}

os::core::Result<void> Rendezvous::create_thread(ThreadId thread, Priority priority) noexcept {
    if (thread == invalid_thread) return rendezvous_error(rendezvous_errors::invalid_thread_id);
    if (find(thread) != nullptr) return rendezvous_error(rendezvous_errors::thread_exists);
    for (auto& slot : slots_) {
        if (slot.occupied) continue;
        slot = Slot{thread, ThreadState::ready, invalid_thread, WakeReason::none, priority, true};
        ++occupied_;
        return {};
    }
    return rendezvous_error(rendezvous_errors::thread_limit);
}

os::core::Result<void> Rendezvous::create_admitted_thread(
    ThreadId thread, Priority priority) noexcept {
    if (thread == invalid_thread) return rendezvous_error(rendezvous_errors::invalid_thread_id);
    if (find(thread) != nullptr) return rendezvous_error(rendezvous_errors::thread_exists);
    for (auto& slot : slots_) {
        if (slot.occupied) continue;
        slot = Slot{
            thread, ThreadState::admitted, invalid_thread, WakeReason::none, priority, true};
        ++occupied_;
        return {};
    }
    return rendezvous_error(rendezvous_errors::thread_limit);
}

os::core::Result<void> Rendezvous::start_thread(ThreadId thread) noexcept {
    Slot* slot = find(thread);
    if (slot == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    if (slot->state != ThreadState::admitted) {
        return rendezvous_error(rendezvous_errors::not_runnable);
    }
    slot->state = ThreadState::ready;
    return {};
}

os::core::Result<std::size_t> Rendezvous::exit_thread(ThreadId thread) noexcept {
    Slot* slot = find(thread);
    if (slot == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);

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
    return released;
}

os::core::Result<void> Rendezvous::block_send(ThreadId from, ThreadId to) noexcept {
    if (from == invalid_thread || to == invalid_thread) {
        return rendezvous_error(rendezvous_errors::invalid_thread_id);
    }
    if (from == to) return rendezvous_error(rendezvous_errors::self_addressed);
    Slot* sender = find(from);
    Slot* target = find(to);
    if (sender == nullptr || target == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    if (sender->state != ThreadState::ready) return rendezvous_error(rendezvous_errors::not_runnable);

    sender->wake = WakeReason::none;
    sender->state = ThreadState::send_blocked;
    sender->partner = to;
    return {};
}

os::core::Result<void> Rendezvous::wait_receive(ThreadId self) noexcept {
    if (self == invalid_thread) return rendezvous_error(rendezvous_errors::invalid_thread_id);
    Slot* receiver = find(self);
    if (receiver == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    if (receiver->state != ThreadState::ready || receiver->partner != invalid_thread) {
        return rendezvous_error(rendezvous_errors::not_runnable);
    }
    receiver->wake = WakeReason::none;
    receiver->state = ThreadState::receive_blocked;
    return {};
}

os::core::Result<void> Rendezvous::deliver_waiting_receiver(
    ThreadId from,
    ThreadId to) noexcept {
    if (from == invalid_thread || to == invalid_thread) {
        return rendezvous_error(rendezvous_errors::invalid_thread_id);
    }
    if (from == to) return rendezvous_error(rendezvous_errors::self_addressed);
    Slot* sender = find(from);
    Slot* receiver = find(to);
    if (sender == nullptr || receiver == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    if (sender->state != ThreadState::ready) return rendezvous_error(rendezvous_errors::not_runnable);
    if (receiver->state != ThreadState::receive_blocked || receiver->partner != invalid_thread) {
        return rendezvous_error(rendezvous_errors::not_waiting_on_peer);
    }

    sender->wake = WakeReason::none;
    sender->state = ThreadState::reply_blocked;
    sender->partner = to;
    receiver->state = ThreadState::ready;
    receiver->partner = from;
    return {};
}

os::core::Result<void> Rendezvous::accept_sender(
    ThreadId self,
    ThreadId caller) noexcept {
    if (self == invalid_thread || caller == invalid_thread) {
        return rendezvous_error(rendezvous_errors::invalid_thread_id);
    }
    if (self == caller) return rendezvous_error(rendezvous_errors::self_addressed);
    Slot* receiver = find(self);
    Slot* sender = find(caller);
    if (receiver == nullptr || sender == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    if (receiver->state != ThreadState::ready) return rendezvous_error(rendezvous_errors::not_runnable);
    if (sender->state != ThreadState::send_blocked || sender->partner != self) {
        return rendezvous_error(rendezvous_errors::not_waiting_on_peer);
    }

    receiver->wake = WakeReason::none;
    sender->state = ThreadState::reply_blocked;
    return {};
}

os::core::Result<void> Rendezvous::cancel_receive(ThreadId self) noexcept {
    if (self == invalid_thread) return rendezvous_error(rendezvous_errors::invalid_thread_id);
    Slot* receiver = find(self);
    if (receiver == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    if (receiver->state != ThreadState::receive_blocked || receiver->partner != invalid_thread) {
        return rendezvous_error(rendezvous_errors::not_waiting_on_peer);
    }
    receiver->state = ThreadState::ready;
    receiver->wake = WakeReason::endpoint_retired;
    return {};
}

os::core::Result<void> Rendezvous::expire_receive(ThreadId self) noexcept {
    if (self == invalid_thread) return rendezvous_error(rendezvous_errors::invalid_thread_id);
    Slot* receiver = find(self);
    if (receiver == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    if (receiver->state != ThreadState::receive_blocked || receiver->partner != invalid_thread) {
        return rendezvous_error(rendezvous_errors::not_waiting_on_peer);
    }
    receiver->state = ThreadState::ready;
    receiver->wake = WakeReason::deadline_expired;
    return {};
}

os::core::Result<bool> Rendezvous::take_deadline_expiry(ThreadId self) noexcept {
    if (self == invalid_thread) return rendezvous_error(rendezvous_errors::invalid_thread_id);
    Slot* slot = find(self);
    if (slot == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    if (slot->wake != WakeReason::deadline_expired) return false;
    slot->wake = WakeReason::none;
    return true;
}

os::core::Result<void> Rendezvous::send(ThreadId from, ThreadId to) noexcept {
    Slot* target = find(to);
    if (target != nullptr && target->state == ThreadState::receive_blocked &&
        target->partner == invalid_thread) {
        return deliver_waiting_receiver(from, to);
    }
    return block_send(from, to);
}

os::core::Result<ThreadId> Rendezvous::receive(ThreadId self) noexcept {
    if (self == invalid_thread) return rendezvous_error(rendezvous_errors::invalid_thread_id);
    Slot* receiver = find(self);
    if (receiver == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    if (receiver->state != ThreadState::ready) return rendezvous_error(rendezvous_errors::not_runnable);

    receiver->wake = WakeReason::none;
    if (receiver->partner != invalid_thread) {
        const ThreadId delivered = receiver->partner;
        receiver->partner = invalid_thread;
        return delivered;
    }

    for (auto& slot : slots_) {
        if (!slot.occupied || slot.state != ThreadState::send_blocked || slot.partner != self) continue;
        auto accepted = accept_sender(self, slot.thread);
        if (!accepted) return accepted.error();
        return slot.thread;
    }

    auto waiting = wait_receive(self);
    if (!waiting) return waiting.error();
    return invalid_thread;
}

os::core::Result<void> Rendezvous::reply(ThreadId self, ThreadId caller) noexcept {
    if (self == invalid_thread || caller == invalid_thread) {
        return rendezvous_error(rendezvous_errors::invalid_thread_id);
    }
    if (self == caller) return rendezvous_error(rendezvous_errors::self_addressed);
    Slot* server = find(self);
    Slot* client = find(caller);
    if (server == nullptr || client == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    if (client->state != ThreadState::reply_blocked || client->partner != self) {
        return rendezvous_error(rendezvous_errors::not_awaiting_reply);
    }
    client->state = ThreadState::ready;
    client->partner = invalid_thread;
    client->wake = WakeReason::replied;
    return {};
}

os::core::Result<void> Rendezvous::cancel_call(
    ThreadId caller,
    ThreadId expected_server) noexcept {
    if (caller == invalid_thread || expected_server == invalid_thread) {
        return rendezvous_error(rendezvous_errors::invalid_thread_id);
    }
    Slot* client = find(caller);
    Slot* server = find(expected_server);
    if (client == nullptr || server == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    const bool blocked =
        (client->state == ThreadState::send_blocked ||
         client->state == ThreadState::reply_blocked) &&
        client->partner == expected_server;
    if (!blocked) return rendezvous_error(rendezvous_errors::not_waiting_on_peer);

    client->state = ThreadState::ready;
    client->partner = invalid_thread;
    client->wake = WakeReason::endpoint_retired;

    if (server->state == ThreadState::ready && server->partner == caller) {
        server->partner = invalid_thread;
    }
    return {};
}

os::core::Result<ThreadState> Rendezvous::state_of(ThreadId thread) const noexcept {
    const Slot* slot = find(thread);
    if (slot == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    return slot->state;
}

os::core::Result<WakeReason> Rendezvous::wake_reason_of(ThreadId thread) const noexcept {
    const Slot* slot = find(thread);
    if (slot == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    return slot->wake;
}

os::core::Result<ThreadId> Rendezvous::partner_of(ThreadId thread) const noexcept {
    const Slot* slot = find(thread);
    if (slot == nullptr) return rendezvous_error(rendezvous_errors::unknown_thread);
    return slot->partner;
}

} // namespace os::kernel
