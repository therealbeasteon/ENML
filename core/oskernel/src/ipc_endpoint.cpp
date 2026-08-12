#include <os/kernel/ipc_endpoint.hpp>

#include <limits>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {
[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}
[[nodiscard]] constexpr bool has_rights(Rights actual, Rights required) noexcept {
    return (actual & required) == required;
}
[[nodiscard]] os::core::Result<IpcEndpoint> decode_endpoint_object(ObjectId object) noexcept {
    if ((object & ipc_object_tag_mask) != ipc_object_tag) {
        return ipc_error(ipc_errors::invalid_capability);
    }
    const auto encoded_slot = static_cast<std::uint16_t>(object & ipc_object_slot_mask);
    const auto generation = static_cast<IpcEndpointGeneration>(
        (object & ipc_object_generation_mask) >> 16U);
    if (encoded_slot == 0U || encoded_slot > max_ipc_endpoints || generation == 0U) {
        return ipc_error(ipc_errors::invalid_endpoint);
    }
    return IpcEndpoint{
        .slot = static_cast<IpcEndpointSlot>(encoded_slot - 1U),
        .generation = generation,
    };
}
} // namespace

os::core::Result<IpcEnvelope> IpcEnvelope::from(
    std::span<const std::byte> source) noexcept {
    if (source.size() > max_ipc_inline_bytes) {
        return ipc_error(ipc_errors::payload_too_large);
    }
    IpcEnvelope envelope{};
    envelope.size = static_cast<std::uint8_t>(source.size());
    for (std::size_t i = 0U; i < source.size(); ++i) envelope.bytes[i] = source[i];
    return envelope;
}

IpcEndpointTable::EndpointSlot* IpcEndpointTable::slot_for(IpcEndpoint endpoint) noexcept {
    if (!endpoint.valid() || endpoint.slot >= endpoints_.size()) return nullptr;
    auto& slot = endpoints_[endpoint.slot];
    if (!slot.active || slot.generation != endpoint.generation) return nullptr;
    return &slot;
}

const IpcEndpointTable::EndpointSlot* IpcEndpointTable::slot_for(IpcEndpoint endpoint) const noexcept {
    if (!endpoint.valid() || endpoint.slot >= endpoints_.size()) return nullptr;
    const auto& slot = endpoints_[endpoint.slot];
    if (!slot.active || slot.generation != endpoint.generation) return nullptr;
    return &slot;
}

IpcEndpointTable::ReplySlot* IpcEndpointTable::reply_slot(const IpcReplySeal& seal) noexcept {
    if (!seal.valid()) return nullptr;
    for (auto& slot : replies_) if (slot.active && slot.seal == seal) return &slot;
    return nullptr;
}

IpcEndpointTable::ReplySlot* IpcEndpointTable::reply_slot(
    ThreadId server, IpcTransactionId transaction) noexcept {
    if (server == invalid_thread || transaction == 0U) return nullptr;
    for (auto& slot : replies_) {
        if (!slot.active) continue;
        if (slot.seal.server == server && slot.seal.transaction == transaction) return &slot;
    }
    return nullptr;
}

IpcEndpointTable::PendingSlot* IpcEndpointTable::pending_slot(
    ThreadId caller, IpcEndpoint endpoint) noexcept {
    for (auto& slot : pending_) {
        if (slot.active && slot.caller == caller && slot.endpoint == endpoint) return &slot;
    }
    return nullptr;
}

IpcEndpointTable::PendingSlot* IpcEndpointTable::pending_slot(IpcEndpoint endpoint) noexcept {
    for (auto& slot : pending_) {
        if (slot.active && slot.endpoint == endpoint) return &slot;
    }
    return nullptr;
}

IpcEndpointTable::PendingSlot* IpcEndpointTable::free_pending_slot() noexcept {
    for (auto& slot : pending_) if (!slot.active) return &slot;
    return nullptr;
}

IpcEndpointTable::CompletedSlot* IpcEndpointTable::completed_slot(ThreadId caller) noexcept {
    for (auto& slot : completed_) if (slot.active && slot.caller == caller) return &slot;
    return nullptr;
}

const IpcEndpointTable::CompletedSlot* IpcEndpointTable::completed_slot(ThreadId caller) const noexcept {
    for (const auto& slot : completed_) if (slot.active && slot.caller == caller) return &slot;
    return nullptr;
}

IpcEndpointTable::CompletedSlot* IpcEndpointTable::free_completed_slot() noexcept {
    for (auto& slot : completed_) if (!slot.active) return &slot;
    return nullptr;
}

void IpcEndpointTable::discard_completed_for(ThreadId caller) noexcept {
    auto* slot = completed_slot(caller);
    if (slot != nullptr) *slot = CompletedSlot{};
}

void IpcEndpointTable::invalidate_replies_for(IpcEndpoint endpoint) noexcept {
    for (auto& slot : replies_) {
        if (!slot.active || !(slot.seal.endpoint == endpoint)) continue;
        slot = ReplySlot{};
        --reply_seals_;
    }
}

void IpcEndpointTable::cancel_pending_for(
    IpcEndpoint endpoint, Rendezvous& rendezvous) noexcept {
    for (auto& pending : pending_) {
        if (!pending.active || !(pending.endpoint == endpoint)) continue;
        (void)rendezvous.cancel_call(pending.caller, pending.server);
        pending = PendingSlot{};
        --pending_calls_;
    }
}

os::core::Result<IpcEndpoint> IpcEndpointTable::create(ThreadId server) noexcept {
    if (server == invalid_thread) return ipc_error(ipc_errors::not_endpoint_owner);
    for (std::size_t index = 0U; index < endpoints_.size(); ++index) {
        auto& slot = endpoints_[index];
        if (slot.active) continue;
        if (slot.generation == std::numeric_limits<IpcEndpointGeneration>::max()) continue;
        ++slot.generation;
        if (slot.generation == 0U) continue;
        slot.server = server;
        slot.active = true;
        slot.receive_waiting = false;
        ++active_;
        return IpcEndpoint{
            .slot = static_cast<IpcEndpointSlot>(index),
            .generation = slot.generation,
        };
    }
    return ipc_error(ipc_errors::endpoint_limit);
}

os::core::Result<void> IpcEndpointTable::retire(
    ThreadId server,
    IpcEndpoint endpoint,
    Rendezvous& rendezvous) noexcept {
    auto* slot = slot_for(endpoint);
    if (slot == nullptr) return ipc_error(ipc_errors::stale_endpoint);
    if (server == invalid_thread || slot->server != server) {
        return ipc_error(ipc_errors::not_endpoint_owner);
    }

    if (slot->receive_waiting) {
        (void)rendezvous.cancel_receive(server);
        slot->receive_waiting = false;
    }
    slot->server = invalid_thread;
    slot->active = false;
    --active_;
    invalidate_replies_for(endpoint);
    cancel_pending_for(endpoint, rendezvous);
    return {};
}

std::size_t IpcEndpointTable::retire_all_owned_by(
    ThreadId server,
    Rendezvous& rendezvous) noexcept {
    if (server == invalid_thread) return 0U;
    std::size_t retired = 0U;
    for (std::size_t index = 0U; index < endpoints_.size(); ++index) {
        auto& slot = endpoints_[index];
        if (!slot.active || slot.server != server) continue;
        const IpcEndpoint endpoint{
            .slot = static_cast<IpcEndpointSlot>(index),
            .generation = slot.generation,
        };
        if (retire(server, endpoint, rendezvous)) ++retired;
    }
    return retired;
}

std::size_t IpcEndpointTable::release_thread(
    ThreadId thread,
    Rendezvous& rendezvous) noexcept {
    if (thread == invalid_thread) return 0U;

    const std::size_t retired = retire_all_owned_by(thread, rendezvous);

    for (auto& pending : pending_) {
        if (!pending.active || pending.caller != thread) continue;
        const IpcEndpoint endpoint = pending.endpoint;
        pending = PendingSlot{};
        --pending_calls_;
        for (auto& reply : replies_) {
            if (!reply.active || reply.seal.caller != thread ||
                !(reply.seal.endpoint == endpoint)) continue;
            reply = ReplySlot{};
            --reply_seals_;
        }
    }

    for (auto& reply : replies_) {
        if (!reply.active) continue;
        if (reply.seal.caller != thread && reply.seal.server != thread) continue;
        reply = ReplySlot{};
        --reply_seals_;
    }

    discard_completed_for(thread);
    return retired;
}

os::core::Result<IpcEndpoint> IpcEndpointTable::endpoint_for_capability(
    ThreadId holder,
    CapabilityId capability,
    Rights required,
    const CapabilityTable& capabilities) const noexcept {
    if (holder == invalid_thread || capability == invalid_capability) {
        return ipc_error(ipc_errors::invalid_capability);
    }
    auto description = capabilities.describe(capability);
    if (!description || description.value().holder != holder) {
        return ipc_error(ipc_errors::invalid_capability);
    }
    if (!has_rights(description.value().rights, required)) {
        return ipc_error(ipc_errors::wrong_rights);
    }
    auto endpoint = decode_endpoint_object(description.value().object);
    if (!endpoint) return endpoint.error();
    if (!active(endpoint.value())) return ipc_error(ipc_errors::stale_endpoint);
    return endpoint.value();
}

os::core::Result<void> IpcEndpointTable::send(
    ThreadId caller,
    CapabilityId endpoint_capability,
    const CapabilityTable& capabilities,
    Rendezvous& rendezvous,
    IpcEnvelope request) noexcept {
    if (!request.valid()) return ipc_error(ipc_errors::payload_too_large);
    if (completed_slot(caller) != nullptr) return ipc_error(ipc_errors::reply_unavailable);

    auto endpoint = endpoint_for_capability(
        caller, endpoint_capability, ipc_right_send, capabilities);
    if (!endpoint) return endpoint.error();
    auto* owner = slot_for(endpoint.value());
    if (owner == nullptr) return ipc_error(ipc_errors::stale_endpoint);

    auto* pending = free_pending_slot();
    if (pending == nullptr) return ipc_error(ipc_errors::pending_call_limit);

    os::core::Result<void> sent = owner->receive_waiting
        ? rendezvous.deliver_waiting_receiver(caller, owner->server)
        : rendezvous.block_send(caller, owner->server);
    if (!sent) return sent.error();
    if (owner->receive_waiting) owner->receive_waiting = false;

    *pending = PendingSlot{
        .endpoint = endpoint.value(),
        .caller = caller,
        .server = owner->server,
        .request = request,
        .active = true,
    };
    ++pending_calls_;
    return {};
}

os::core::Result<IpcReceived> IpcEndpointTable::receive(
    ThreadId server,
    CapabilityId endpoint_capability,
    const CapabilityTable& capabilities,
    Rendezvous& rendezvous) noexcept {
    auto endpoint = endpoint_for_capability(
        server, endpoint_capability, ipc_right_receive, capabilities);
    if (!endpoint) return endpoint.error();
    auto* owner = slot_for(endpoint.value());
    if (owner == nullptr) return ipc_error(ipc_errors::stale_endpoint);
    if (owner->server != server) return ipc_error(ipc_errors::not_endpoint_owner);
    if (owner->receive_waiting) return ipc_error(ipc_errors::receive_already_waiting);

    PendingSlot* pending = nullptr;
    auto delivered = rendezvous.partner_of(server);
    if (!delivered) return delivered.error();
    if (delivered.value() != invalid_thread) {
        pending = pending_slot(delivered.value(), endpoint.value());
        if (pending == nullptr) return ipc_error(ipc_errors::pending_call_missing);
        auto consumed = rendezvous.receive(server);
        if (!consumed || consumed.value() != delivered.value()) {
            return ipc_error(ipc_errors::pending_call_missing);
        }
    } else {
        pending = pending_slot(endpoint.value());
        if (pending == nullptr) {
            auto waiting = rendezvous.wait_receive(server);
            if (!waiting) return waiting.error();
            owner->receive_waiting = true;
            return IpcReceived{};
        }

        if (next_transaction_ == 0U ||
            next_transaction_ == std::numeric_limits<IpcTransactionId>::max()) {
            return ipc_error(ipc_errors::transaction_exhausted);
        }
        ReplySlot* free = nullptr;
        for (auto& slot : replies_) if (!slot.active) { free = &slot; break; }
        if (free == nullptr) return ipc_error(ipc_errors::reply_seal_limit);

        auto accepted = rendezvous.accept_sender(server, pending->caller);
        if (!accepted) return accepted.error();

        const IpcReplySeal seal{
            .endpoint = endpoint.value(),
            .transaction = next_transaction_++,
            .caller = pending->caller,
            .server = server,
        };
        *free = ReplySlot{.seal = seal, .active = true};
        ++reply_seals_;
        return IpcReceived{
            .caller = pending->caller,
            .reply = seal,
            .request = pending->request,
        };
    }

    if (next_transaction_ == 0U ||
        next_transaction_ == std::numeric_limits<IpcTransactionId>::max()) {
        return ipc_error(ipc_errors::transaction_exhausted);
    }
    ReplySlot* free = nullptr;
    for (auto& slot : replies_) if (!slot.active) { free = &slot; break; }
    if (free == nullptr) return ipc_error(ipc_errors::reply_seal_limit);

    const IpcReplySeal seal{
        .endpoint = endpoint.value(),
        .transaction = next_transaction_++,
        .caller = pending->caller,
        .server = server,
    };
    *free = ReplySlot{.seal = seal, .active = true};
    ++reply_seals_;
    return IpcReceived{
        .caller = pending->caller,
        .reply = seal,
        .request = pending->request,
    };
}

os::core::Result<void> IpcEndpointTable::reply(
    ThreadId server,
    const IpcReplySeal& seal,
    Rendezvous& rendezvous,
    IpcEnvelope response) noexcept {
    if (!response.valid()) return ipc_error(ipc_errors::payload_too_large);
    if (!seal.valid()) return ipc_error(ipc_errors::stale_reply_seal);
    if (server != seal.server) return ipc_error(ipc_errors::wrong_reply_server);
    const auto* endpoint = slot_for(seal.endpoint);
    if (endpoint == nullptr || endpoint->server != server) {
        return ipc_error(ipc_errors::stale_reply_seal);
    }
    auto* seal_slot = reply_slot(seal);
    auto* pending = pending_slot(seal.caller, seal.endpoint);
    if (seal_slot == nullptr || pending == nullptr) {
        return ipc_error(ipc_errors::stale_reply_seal);
    }
    if (completed_slot(seal.caller) != nullptr) {
        return ipc_error(ipc_errors::reply_unavailable);
    }
    auto* completed = free_completed_slot();
    if (completed == nullptr) return ipc_error(ipc_errors::reply_unavailable);

    *completed = CompletedSlot{
        .caller = seal.caller,
        .response = response,
        .active = true,
    };
    auto replied = rendezvous.reply(server, seal.caller);
    if (!replied) {
        *completed = CompletedSlot{};
        return replied.error();
    }

    *seal_slot = ReplySlot{};
    --reply_seals_;
    *pending = PendingSlot{};
    --pending_calls_;
    return {};
}

os::core::Result<void> IpcEndpointTable::reply_transaction(
    ThreadId server,
    IpcTransactionId transaction,
    Rendezvous& rendezvous,
    IpcEnvelope response) noexcept {
    auto* slot = reply_slot(server, transaction);
    if (slot == nullptr) return ipc_error(ipc_errors::stale_reply_seal);
    const IpcReplySeal seal = slot->seal;
    return reply(server, seal, rendezvous, response);
}

os::core::Result<IpcEnvelope> IpcEndpointTable::take_reply(ThreadId caller) noexcept {
    auto* slot = completed_slot(caller);
    if (slot == nullptr) return ipc_error(ipc_errors::reply_unavailable);
    const IpcEnvelope response = slot->response;
    *slot = CompletedSlot{};
    return response;
}

bool IpcEndpointTable::reply_available(ThreadId caller) const noexcept {
    return caller != invalid_thread && completed_slot(caller) != nullptr;
}

bool IpcEndpointTable::active(IpcEndpoint endpoint) const noexcept {
    return slot_for(endpoint) != nullptr;
}

} // namespace os::kernel
