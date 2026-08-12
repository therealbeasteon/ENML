#include <os/kernel/ipc_continuation.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {
[[nodiscard]] constexpr os::core::Error continuation_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}
}

IpcContinuationTable::SendSlot* IpcContinuationTable::send_slot_for(ThreadId caller) noexcept {
    if (caller == invalid_thread) return nullptr;
    for (auto& slot : send_slots_) {
        if (slot.occupied && slot.continuation.caller == caller) return &slot;
    }
    return nullptr;
}

IpcContinuationTable::ReceiveSlot* IpcContinuationTable::receive_slot_for(ThreadId server) noexcept {
    if (server == invalid_thread) return nullptr;
    for (auto& slot : receive_slots_) {
        if (slot.occupied && slot.continuation.server == server) return &slot;
    }
    return nullptr;
}

os::core::Result<void> IpcContinuationTable::arm(
    ThreadId caller,
    AddressSpaceEpoch epoch,
    std::uint64_t exchange_address,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (caller == invalid_thread) return continuation_error(ipc_continuation_errors::invalid_thread);
    if (!epoch.valid() || !epochs.active(epoch)) return continuation_error(ipc_continuation_errors::invalid_epoch);
    if (exchange_address == 0ULL) return continuation_error(ipc_continuation_errors::invalid_exchange);
    if (send_slot_for(caller) != nullptr) return continuation_error(ipc_continuation_errors::already_armed);

    for (auto& slot : send_slots_) {
        if (slot.occupied) continue;
        slot.occupied = true;
        slot.continuation = IpcSendContinuation{
            .caller = caller,
            .epoch = epoch,
            .exchange_address = exchange_address,
        };
        ++occupied_;
        return {};
    }
    return continuation_error(ipc_continuation_errors::exhausted);
}

os::core::Result<IpcSendContinuation> IpcContinuationTable::take(
    ThreadId caller,
    AddressSpaceEpoch expected,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    auto* slot = send_slot_for(caller);
    if (slot == nullptr) return continuation_error(ipc_continuation_errors::not_armed);

    const IpcSendContinuation continuation = slot->continuation;
    *slot = SendSlot{};
    --occupied_;

    if (!expected.valid() || !epochs.active(expected) || !(continuation.epoch == expected)) {
        return continuation_error(ipc_continuation_errors::stale);
    }
    return continuation;
}

os::core::Result<void> IpcContinuationTable::cancel(ThreadId caller) noexcept {
    auto* slot = send_slot_for(caller);
    if (slot == nullptr) return continuation_error(ipc_continuation_errors::not_armed);
    *slot = SendSlot{};
    --occupied_;
    return {};
}

os::core::Result<void> IpcContinuationTable::arm_receive(
    ThreadId server,
    AddressSpaceEpoch epoch,
    CapabilityId endpoint_capability,
    std::uint64_t exchange_address,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (server == invalid_thread) return continuation_error(ipc_continuation_errors::invalid_thread);
    if (!epoch.valid() || !epochs.active(epoch)) return continuation_error(ipc_continuation_errors::invalid_epoch);
    if (endpoint_capability == invalid_capability) return continuation_error(ipc_continuation_errors::invalid_capability);
    if (exchange_address == 0ULL) return continuation_error(ipc_continuation_errors::invalid_exchange);
    if (receive_slot_for(server) != nullptr) return continuation_error(ipc_continuation_errors::already_armed);

    for (auto& slot : receive_slots_) {
        if (slot.occupied) continue;
        slot.occupied = true;
        slot.continuation = IpcReceiveContinuation{
            .server = server,
            .epoch = epoch,
            .endpoint_capability = endpoint_capability,
            .exchange_address = exchange_address,
        };
        ++occupied_;
        return {};
    }
    return continuation_error(ipc_continuation_errors::exhausted);
}

os::core::Result<IpcReceiveContinuation> IpcContinuationTable::take_receive(
    ThreadId server,
    AddressSpaceEpoch expected,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    auto* slot = receive_slot_for(server);
    if (slot == nullptr) return continuation_error(ipc_continuation_errors::not_armed);

    const IpcReceiveContinuation continuation = slot->continuation;
    *slot = ReceiveSlot{};
    --occupied_;

    if (!expected.valid() || !epochs.active(expected) || !(continuation.epoch == expected)) {
        return continuation_error(ipc_continuation_errors::stale);
    }
    return continuation;
}

os::core::Result<void> IpcContinuationTable::cancel_receive(ThreadId server) noexcept {
    auto* slot = receive_slot_for(server);
    if (slot == nullptr) return continuation_error(ipc_continuation_errors::not_armed);
    *slot = ReceiveSlot{};
    --occupied_;
    return {};
}

void IpcContinuationTable::release_thread(ThreadId thread) noexcept {
    if (auto* send = send_slot_for(thread); send != nullptr) {
        *send = SendSlot{};
        --occupied_;
    }
    if (auto* receive = receive_slot_for(thread); receive != nullptr) {
        *receive = ReceiveSlot{};
        --occupied_;
    }
}

bool IpcContinuationTable::send_armed(ThreadId caller) const noexcept {
    if (caller == invalid_thread) return false;
    for (const auto& slot : send_slots_) {
        if (slot.occupied && slot.continuation.caller == caller) return true;
    }
    return false;
}

bool IpcContinuationTable::receive_armed(ThreadId server) const noexcept {
    if (server == invalid_thread) return false;
    for (const auto& slot : receive_slots_) {
        if (slot.occupied && slot.continuation.server == server) return true;
    }
    return false;
}

} // namespace os::kernel
