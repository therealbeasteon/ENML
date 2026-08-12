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

[[nodiscard]] constexpr os::core::Result<IpcEndpoint> decode_endpoint_object(ObjectId object) noexcept {
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
    for (auto& slot : replies_) {
        if (slot.active && slot.seal == seal) return &slot;
    }
    return nullptr;
}

void IpcEndpointTable::invalidate_replies_for(IpcEndpoint endpoint) noexcept {
    for (auto& slot : replies_) {
        if (!slot.active || !(slot.seal.endpoint == endpoint)) continue;
        slot = ReplySlot{};
        --reply_seals_;
    }
}

os::core::Result<IpcEndpoint> IpcEndpointTable::create(ThreadId server) noexcept {
    if (server == invalid_thread) return ipc_error(ipc_errors::not_endpoint_owner);
    for (std::size_t index = 0U; index < endpoints_.size(); ++index) {
        auto& slot = endpoints_[index];
        if (slot.active) continue;
        if (slot.generation == std::numeric_limits<IpcEndpointGeneration>::max()) {
            continue;
        }
        ++slot.generation;
        if (slot.generation == 0U) continue;
        slot.server = server;
        slot.active = true;
        ++active_;
        return IpcEndpoint{
            .slot = static_cast<IpcEndpointSlot>(index),
            .generation = slot.generation,
        };
    }
    return ipc_error(ipc_errors::endpoint_limit);
}

os::core::Result<void> IpcEndpointTable::retire(
    ThreadId server, IpcEndpoint endpoint) noexcept {
    auto* slot = slot_for(endpoint);
    if (slot == nullptr) return ipc_error(ipc_errors::stale_endpoint);
    if (server == invalid_thread || slot->server != server) {
        return ipc_error(ipc_errors::not_endpoint_owner);
    }

    // Revocation is visible before reuse. The numeric slot may later return, but
    // its generation changes and all reply seals from this incarnation are dead.
    invalidate_replies_for(endpoint);
    slot->server = invalid_thread;
    slot->active = false;
    --active_;
    return {};
}

os::core::Result<IpcEndpoint> IpcEndpointTable::endpoint_for_capability(
    ThreadId holder,
    CapabilityId capability,
    Rights required,
    const CapabilityTable& capabilities) const noexcept {
    if (holder == invalid_thread || capability == invalid_capability ||
        !capabilities.holds(holder, capability)) {
        return ipc_error(ipc_errors::invalid_capability);
    }
    auto description = capabilities.describe(capability);
    if (!description) return ipc_error(ipc_errors::invalid_capability);
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
    Rendezvous& rendezvous) noexcept {
    auto endpoint = endpoint_for_capability(
        caller, endpoint_capability, ipc_right_send, capabilities);
    if (!endpoint) return endpoint.error();
    const auto* slot = slot_for(endpoint.value());
    if (slot == nullptr) return ipc_error(ipc_errors::stale_endpoint);
    return rendezvous.send(caller, slot->server);
}

os::core::Result<IpcReceived> IpcEndpointTable::receive(
    ThreadId server,
    CapabilityId endpoint_capability,
    const CapabilityTable& capabilities,
    Rendezvous& rendezvous) noexcept {
    auto endpoint = endpoint_for_capability(
        server, endpoint_capability, ipc_right_receive, capabilities);
    if (!endpoint) return endpoint.error();
    const auto* owner = slot_for(endpoint.value());
    if (owner == nullptr) return ipc_error(ipc_errors::stale_endpoint);
    if (owner->server != server) return ipc_error(ipc_errors::not_endpoint_owner);

    auto caller = rendezvous.receive(server);
    if (!caller) return caller.error();
    if (caller.value() == invalid_thread) return IpcReceived{};

    if (next_transaction_ == 0U ||
        next_transaction_ == std::numeric_limits<IpcTransactionId>::max()) {
        return ipc_error(ipc_errors::transaction_exhausted);
    }

    ReplySlot* free = nullptr;
    for (auto& slot : replies_) {
        if (!slot.active) {
            free = &slot;
            break;
        }
    }
    if (free == nullptr) return ipc_error(ipc_errors::reply_seal_limit);

    const IpcReplySeal seal{
        .endpoint = endpoint.value(),
        .transaction = next_transaction_++,
        .caller = caller.value(),
        .server = server,
    };
    *free = ReplySlot{.seal = seal, .active = true};
    ++reply_seals_;
    return IpcReceived{.caller = caller.value(), .reply = seal};
}

os::core::Result<void> IpcEndpointTable::reply(
    ThreadId server,
    const IpcReplySeal& seal,
    Rendezvous& rendezvous) noexcept {
    if (!seal.valid()) return ipc_error(ipc_errors::stale_reply_seal);
    if (server != seal.server) return ipc_error(ipc_errors::wrong_reply_server);
    const auto* endpoint = slot_for(seal.endpoint);
    if (endpoint == nullptr || endpoint->server != server) {
        return ipc_error(ipc_errors::stale_reply_seal);
    }
    auto* slot = reply_slot(seal);
    if (slot == nullptr) return ipc_error(ipc_errors::stale_reply_seal);

    // Consume authority before waking the caller. If a future machine copyout
    // step fails, the transaction must be failed/retired rather than making the
    // same reply token replayable.
    *slot = ReplySlot{};
    --reply_seals_;
    auto replied = rendezvous.reply(server, seal.caller);
    if (!replied) return replied.error();
    return {};
}

bool IpcEndpointTable::active(IpcEndpoint endpoint) const noexcept {
    return slot_for(endpoint) != nullptr;
}

} // namespace os::kernel
